/** Slave process query functionality.
 */

/*
 * Copyright (C) 2015 CESNET
 *
 * LICENSE TERMS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#include "common.h"
#include "slave.h"
#include "path_array.h"
#include "print.h"
#include "file_index/file_index.h"

#include <string.h> //strlen()
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <limits.h> //PATH_MAX
#include <unistd.h> //access

#ifdef _OPENMP
#include <omp.h>
#endif //_OPENMP
#include <mpi.h>
#include <libnf.h>
#include <ffilter.h>
#include <dirent.h> //list directory
#include <sys/stat.h> //stat()


#if LNF_MAX_RAW_LEN > XCHG_BUFF_SIZE
#error "LNF_MAX_RAW_LEN > XCHG_BUFF_SIZE"
#endif

#if LNF_MAX_RAW_LEN > UINT32_MAX
#error "LNF_MAX_RAW_LEN > UINT32_MAX"
#endif

#define LOOKUP_CURSOR_INIT_SIZE 1024


/* Global variables. */
extern MPI_Datatype mpi_struct_shared_task_ctx;


/* Thread shared. */
struct slave_task_ctx {
        /* Master and slave shared task context. Received from master. */
        struct shared_task_ctx shared; //master-slave shared

        /* Slave specific task context. */
        lnf_mem_t *aggr_mem; //LNF memory used for aggregation
        lnf_filter_t *filter; //LNF compiled filter expression
        struct fidx_ip_tree_node *idx_tree; //indexing IP address tree (created from
                                       //the LNF filter)

        uint8_t *buff[2]; //two chunks of memory for the data buffers
        char *path_str; //file/directory/profile(s) path string
        size_t proc_rec_cntr; //processed record counter
        bool rec_limit_reached; //true if rec_limit records read
        size_t slave_cnt; //slave count
        struct processed_summ processed_summ; //summary of processed records
        struct metadata_summ metadata_summ; //summary of flow files metadata
};

/* Thread private. */
struct thread_ctx {
        lnf_file_t *file; //LNF file
        lnf_rec_t *rec; //LNF record

        uint8_t *buff[2]; //two chunks of memory for the data buffers
        struct processed_summ processed_summ; //summary of processed records
        struct metadata_summ metadata_summ; //summary of flow files metadata
};


static void wait_isend_cs(void *data, size_t data_size, MPI_Request *req)
{
        /* Lack of MPI_THREAD_MULTIPLE threading level implies this CS. */
        #pragma omp critical (mpi)
        {
                MPI_Wait(req, MPI_STATUS_IGNORE);
                MPI_Isend(data, data_size, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD, req);
        }
}


static void processed_summ_update(struct processed_summ *private, lnf_rec_t *rec)
{
        uint64_t tmp;

        lnf_rec_fget(rec, LNF_FLD_AGGR_FLOWS, &tmp);
        private->flows += tmp;

        lnf_rec_fget(rec, LNF_FLD_DPKTS, &tmp);
        private->pkts += tmp;

        lnf_rec_fget(rec, LNF_FLD_DOCTETS, &tmp);
        private->bytes += tmp;
}

static void processed_summ_share(struct processed_summ *shared,
                const struct processed_summ *private)
{
        #pragma omp atomic
        shared->flows += private->flows;

        #pragma omp atomic
        shared->pkts += private->pkts;

        #pragma omp atomic
        shared->bytes += private->bytes;
}

/* Read to the temporary variable, check validity and update private. */
static void metadata_summ_update(struct metadata_summ *private, lnf_file_t *file)
{
        struct metadata_summ tmp;


        /* Flows. */
        assert(lnf_info(file, LNF_INFO_FLOWS, &tmp.flows,
                                sizeof (tmp.flows)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_FLOWS_TCP, &tmp.flows_tcp,
                                sizeof (tmp.flows_tcp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_FLOWS_UDP, &tmp.flows_udp,
                                sizeof (tmp.flows_udp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_FLOWS_ICMP, &tmp.flows_icmp,
                                sizeof (tmp.flows_icmp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_FLOWS_OTHER, &tmp.flows_other,
                                sizeof (tmp.flows_other)) == LNF_OK);

        if (tmp.flows != tmp.flows_tcp + tmp.flows_udp + tmp.flows_icmp +
                        tmp.flows_other) {
                PRINT_WARNING(E_LNF, 0, "metadata flow count mismatch "
                                "(total != TCP + UDP + ICMP + other)");
        }

        private->flows += tmp.flows;
        private->flows_tcp += tmp.flows_tcp;
        private->flows_udp += tmp.flows_udp;
        private->flows_icmp += tmp.flows_icmp;
        private->flows_other += tmp.flows_other;

        /* Packets. */
        assert(lnf_info(file, LNF_INFO_PACKETS, &tmp.pkts,
                                sizeof (tmp.pkts)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_PACKETS_TCP, &tmp.pkts_tcp,
                                sizeof (tmp.pkts_tcp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_PACKETS_UDP, &tmp.pkts_udp,
                                sizeof (tmp.pkts_udp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_PACKETS_ICMP, &tmp.pkts_icmp,
                                sizeof (tmp.pkts_icmp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_PACKETS_OTHER, &tmp.pkts_other,
                                sizeof (tmp.pkts_other)) == LNF_OK);

        if (tmp.pkts != tmp.pkts_tcp + tmp.pkts_udp + tmp.pkts_icmp +
                        tmp.pkts_other) {
                PRINT_WARNING(E_LNF, 0, "metadata packet count mismatch "
                                "(total != TCP + UDP + ICMP + other)");
        }

        private->pkts += tmp.pkts;
        private->pkts_tcp += tmp.pkts_tcp;
        private->pkts_udp += tmp.pkts_udp;
        private->pkts_icmp += tmp.pkts_icmp;
        private->pkts_other += tmp.pkts_other;

        /* Bytes. */
        assert(lnf_info(file, LNF_INFO_BYTES, &tmp.bytes,
                                sizeof (tmp.bytes)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_BYTES_TCP, &tmp.bytes_tcp,
                                sizeof (tmp.bytes_tcp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_BYTES_UDP, &tmp.bytes_udp,
                                sizeof (tmp.bytes_udp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_BYTES_ICMP, &tmp.bytes_icmp,
                                sizeof (tmp.bytes_icmp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_BYTES_OTHER, &tmp.bytes_other,
                                sizeof (tmp.bytes_other)) == LNF_OK);

        if (tmp.bytes != tmp.bytes_tcp + tmp.bytes_udp + tmp.bytes_icmp +
                        tmp.bytes_other) {
                PRINT_WARNING(E_LNF, 0, "metadata bytes count mismatch "
                                "(total != TCP + UDP + ICMP + other)");
        }

        private->bytes += tmp.bytes;
        private->bytes_tcp += tmp.bytes_tcp;
        private->bytes_udp += tmp.bytes_udp;
        private->bytes_icmp += tmp.bytes_icmp;
        private->bytes_other += tmp.bytes_other;
}

static void metadata_summ_share(struct metadata_summ *shared,
                const struct metadata_summ *private)
{
        #pragma omp atomic
        shared->flows += private->flows;
        #pragma omp atomic
        shared->flows_tcp += private->flows_tcp;
        #pragma omp atomic
        shared->flows_udp += private->flows_udp;
        #pragma omp atomic
        shared->flows_icmp += private->flows_icmp;
        #pragma omp atomic
        shared->flows_other += private->flows_other;

        #pragma omp atomic
        shared->pkts += private->pkts;
        #pragma omp atomic
        shared->pkts_tcp += private->pkts_tcp;
        #pragma omp atomic
        shared->pkts_udp += private->pkts_udp;
        #pragma omp atomic
        shared->pkts_icmp += private->pkts_icmp;
        #pragma omp atomic
        shared->pkts_other += private->pkts_other;

        #pragma omp atomic
        shared->bytes += private->bytes;
        #pragma omp atomic
        shared->bytes_tcp += private->bytes_tcp;
        #pragma omp atomic
        shared->bytes_udp += private->bytes_udp;
        #pragma omp atomic
        shared->bytes_icmp += private->bytes_icmp;
        #pragma omp atomic
        shared->bytes_other += private->bytes_other;
}


static error_code_t task_send_file(struct slave_task_ctx *stc,
                struct thread_ctx *tc)
{
        int secondary_errno;

        size_t file_rec_cntr = 0;
        size_t file_proc_rec_cntr = 0;
        size_t file_sent_bytes = 0;

        bool buff_idx = 0; //index to the currently used data buffer
        size_t buff_off = 0; //current data buffer offset
        size_t buff_rec_cntr = 0; //number of records in the current buffer

        MPI_Request request = MPI_REQUEST_NULL;
        uint32_t rec_size = 0;

        struct {
                int id;
                size_t size;
        } fast_fields[LNF_FLD_TERM_];//fields array compressed for faster access
        size_t fast_fields_cnt = 0;


        /* Fill fast fields array and calculate constant record size. */
        for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
                if (stc->shared.fields[i].id == 0) {
                        continue; //field is not present
                }
                fast_fields[fast_fields_cnt].id = i;
                rec_size += fast_fields[fast_fields_cnt].size =
                        field_get_size(i);
                fast_fields_cnt++;
        }

        /*
         * Read all records from file. Hot path.
         * No aggregation -> store record to buffer.
         * Send buffer, if buffer full, otherwise continue reading.
         */
        while ((secondary_errno = lnf_read(tc->file, tc->rec)) == LNF_OK) {
                file_rec_cntr++;

                /* Apply filter (if there is any). */
                if (stc->filter && !lnf_filter_match(stc->filter, tc->rec)) {
                        continue;
                }
                file_proc_rec_cntr++;

                /* Is there enough space in the buffer for the next record? */
                if (buff_off + rec_size + sizeof (rec_size) > XCHG_BUFF_SIZE) {
                        /* Check record limit and break if exceeded. */
                        #pragma omp flush
                        if (stc->rec_limit_reached) {
                                buff_rec_cntr = 0;
                                break; //record limit reached by another thread
                        }

                        wait_isend_cs(tc->buff[buff_idx], buff_off, &request);
                        file_sent_bytes += buff_off;

                        /* Increment shared counter. */
                        #pragma omp atomic
                        stc->proc_rec_cntr += buff_rec_cntr;

                        /* Clear buffer context variables. */
                        buff_off = 0;
                        buff_rec_cntr = 0;
                        buff_idx = !buff_idx; //toggle data buffers

                        /* Check record limit again and break if exceeded. */
                        if (stc->shared.rec_limit && stc->proc_rec_cntr >=
                                        stc->shared.rec_limit) {
                                stc->rec_limit_reached = true;
                                #pragma omp flush
                                break; //record limit reached by this thread
                        }
                }

                /* Increment the private processed summary counters. */
                processed_summ_update(&tc->processed_summ, tc->rec);

                *(uint32_t *)(tc->buff[buff_idx] + buff_off) = rec_size;
                buff_off += sizeof (rec_size);

                /* Loop through the fields and fill the data buffer. */
                for (size_t i = 0; i < fast_fields_cnt; ++i) {
                        lnf_rec_fget(tc->rec, fast_fields[i].id,
                                        tc->buff[buff_idx] + buff_off);
                        buff_off += fast_fields[i].size;
                }

                buff_rec_cntr++;
        }

        /* Send remaining records if data buffer is not empty. */
        if (buff_rec_cntr != 0) {
                wait_isend_cs(tc->buff[buff_idx], buff_off, &request);
                file_sent_bytes += buff_off;

                #pragma omp atomic
                stc->proc_rec_cntr += buff_rec_cntr; //increment shared counter
        }

        /* Either set record limit reached flag or check if EOF was reached. */
        if (stc->shared.rec_limit && stc->proc_rec_cntr >=
                        stc->shared.rec_limit) {
                stc->rec_limit_reached = true;
                #pragma omp flush
        } else if (secondary_errno != LNF_EOF) {
                PRINT_WARNING(E_LNF, secondary_errno, "EOF wasn't reached");
        }

        /* Buffers will be invalid after return, wait for send to complete. */
        #pragma omp critical (mpi)
        {
                MPI_Wait(&request, MPI_STATUS_IGNORE);
        }

        PRINT_DEBUG("read %zu, processed %zu, sent %zu B", file_rec_cntr,
                        file_proc_rec_cntr, file_sent_bytes);

        return E_OK;
}


static error_code_t task_store_file(struct slave_task_ctx *stc,
                struct thread_ctx *tc)
{
        error_code_t primary_errno = E_OK;
        int secondary_errno;

        size_t file_rec_cntr = 0;
        size_t file_proc_rec_cntr = 0;

        /*
         * Read all records from file. Hot path.
         * Aggreagation -> write record to memory and continue.
         * Ignore record limit.
         */
        while ((secondary_errno = lnf_read(tc->file, tc->rec)) == LNF_OK) {
                file_rec_cntr++;

                /* Apply filter (if there is any). */
                if (stc->filter && !lnf_filter_match(stc->filter, tc->rec)) {
                        continue;
                }
                file_proc_rec_cntr++;

                /* Increment the private processed summary counters. */
                processed_summ_update(&tc->processed_summ, tc->rec);

                secondary_errno = lnf_mem_write(stc->aggr_mem, tc->rec);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        PRINT_ERROR(primary_errno, secondary_errno,
                                        "lnf_mem_write()");
                        break;
                }
        }

        /* Check if we reach end of file. */
        if (secondary_errno != LNF_EOF) {
                PRINT_WARNING(E_LNF, secondary_errno, "EOF wasn't reached");
        }

        PRINT_DEBUG("read %zu, processed %zu", file_rec_cntr,
                        file_proc_rec_cntr);

        return primary_errno;
}

static void task_free(struct slave_task_ctx *stc)
{
        if (stc->filter) {
                lnf_filter_free(stc->filter);
        }
        if (stc->aggr_mem) {
                free_aggr_mem(stc->aggr_mem);
        }
        if (stc->idx_tree){
                fidx_destroy_tree(&stc->idx_tree);
        }
        free(stc->path_str);
}


static error_code_t task_init_filter(lnf_filter_t **filter,
                struct fidx_ip_tree_node **idx_tree, char *filter_str)
{
        int secondary_errno;


        assert(filter != NULL && filter_str != NULL && strlen(filter_str) != 0);

        /* Initialize filter. */
        //secondary_errno = lnf_filter_init(filter, filter_str); //old filter
        secondary_errno = lnf_filter_init_v2(filter, filter_str); //new filter
        if (secondary_errno != LNF_OK) {
                PRINT_ERROR(E_LNF, secondary_errno,
                                "cannot initialise filter \"%s\"", filter_str);
                return E_LNF;

        }

        //TODO: IF indexing
        ff_t *filter_tree = (ff_t *)lnf_filter_ffilter_ptr(*filter);
        if (fidx_get_tree((ff_node_t *)filter_tree->root, idx_tree) != E_OK) {
                PRINT_ERROR(E_IDX, 0,
                                "unable to create an indexing IP address tree");
                //TURN OFF INDEXING, DESTROY TREE IF EXISTS
                return E_LNF;
        }

        return E_OK;
}


static error_code_t task_receive_ctx(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;


        assert(stc != NULL);

        /* Receive task context, path string and optional filter string. */
        MPI_Bcast(&stc->shared, 1, mpi_struct_shared_task_ctx, ROOT_PROC,
                        MPI_COMM_WORLD);

        stc->path_str = calloc(stc->shared.path_str_len + 1, sizeof (char));
        if (stc->path_str == NULL) {
                PRINT_ERROR(E_MEM, 0, "calloc()");
                return E_MEM;
        }
        MPI_Bcast(stc->path_str, stc->shared.path_str_len, MPI_CHAR, ROOT_PROC,
                        MPI_COMM_WORLD);

        if (stc->shared.filter_str_len > 0) {
                char filter_str[stc->shared.filter_str_len + 1];

                MPI_Bcast(filter_str, stc->shared.filter_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);

                filter_str[stc->shared.filter_str_len] = '\0'; //termination
                primary_errno = task_init_filter(&stc->filter, &stc->idx_tree,
                                filter_str);
                //it is OK not to check primary_errno
        }


        return primary_errno;
}


static error_code_t task_init_mode(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;

        assert(stc != NULL);

        switch (stc->shared.working_mode) {
        case MODE_LIST:
                return E_OK;

        case MODE_SORT:
                if (stc->shared.rec_limit == 0) {
                        break; //don't need memory, local sort would be useless
                }

                /* Sort all records, then send first rec_limit records .*/
                primary_errno = init_aggr_mem(&stc->aggr_mem,
                                stc->shared.fields);
                if (primary_errno != E_OK) {
                        return primary_errno;
                }
                lnf_mem_setopt(stc->aggr_mem, LNF_OPT_LISTMODE, NULL, 0);

                break;

        case MODE_AGGR:
                /* Initialize aggregation memory and set memory parameters. */
                primary_errno = init_aggr_mem(&stc->aggr_mem,
                                stc->shared.fields);

                break;

        case MODE_META:
                /* Nothing to do. */
                return E_OK;

        default:
                assert(!"unknown working mode");
        }

        return primary_errno;
}


static error_code_t isend_loop(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;
        int secondary_errno;

        bool buff_idx = 0; //currently used data buffer index
        size_t buff_off = 0; //data buffer offset
        size_t buff_rec_cntr = 0; //number of records in current buffer

        uint32_t rec_size;
        size_t byte_cntr = 0;
        size_t rec_cntr = 0;

        lnf_mem_cursor_t *cursor;
        MPI_Request request = MPI_REQUEST_NULL;


        /* Set cursor to the first record in the memory. */
        secondary_errno = lnf_mem_first_c(stc->aggr_mem, &cursor);

        /* Loop throught all the records. */
        while (cursor != NULL) {
                /* Read if there is enough space. Save space for record size. */
                secondary_errno = lnf_mem_read_raw_c(stc->aggr_mem, cursor,
                                (char *)stc->buff[buff_idx] + buff_off + sizeof (rec_size),
                                (int *)&rec_size,
                                XCHG_BUFF_SIZE - buff_off - sizeof (rec_size));
                assert(secondary_errno != LNF_EOF);

                /* Was there enough space in the buffer for the record? */
                if (secondary_errno != LNF_ERR_NOMEM) { //yes
                        *(uint32_t *)(stc->buff[buff_idx] + buff_off) = rec_size;
                        buff_off += sizeof (rec_size) + rec_size;

                        buff_rec_cntr++;
                        rec_cntr++;

                        /* Shift cursor to next record. */
                        secondary_errno = lnf_mem_next_c(stc->aggr_mem,
                                        &cursor);
                } else { //no
                        /* Send buffer. */
                        MPI_Wait(&request, MPI_STATUS_IGNORE);
                        MPI_Isend(stc->buff[buff_idx], buff_off, MPI_BYTE,
                                        ROOT_PROC, TAG_DATA, MPI_COMM_WORLD,
                                        &request);
                        byte_cntr += buff_off;

                        /* Clear buffer context variables. */
                        buff_off = 0;
                        buff_rec_cntr = 0;
                        buff_idx = !buff_idx; //toggle data buffers
                }
        }
        if (secondary_errno != LNF_EOF) {
                primary_errno = E_LNF;
                PRINT_ERROR(primary_errno, secondary_errno,
                                "lnf_mem_next_c() or lnf_mem_first_c()");
        }


        /* Send remaining records if data buffer is not empty. */
        if (buff_rec_cntr != 0) {
                MPI_Wait(&request, MPI_STATUS_IGNORE);
                MPI_Isend(stc->buff[buff_idx], buff_off, MPI_BYTE, ROOT_PROC,
                                TAG_DATA, MPI_COMM_WORLD, &request);
                byte_cntr += buff_off;
        }

        /* Buffers will be invalid after return, wait for send to complete. */
        MPI_Wait(&request, MPI_STATUS_IGNORE);

        PRINT_DEBUG("read %zu, sent %zu B", rec_cntr, byte_cntr);

        return primary_errno;
}


static error_code_t fast_topn_isend_loop(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;
        int secondary_errno;

        bool buff_idx = 0; //currently used data buffer index
        size_t buff_off = 0; //data buffer offset
        size_t buff_rec_cntr = 0; //number of records in current buffer

        uint32_t rec_size;
        size_t byte_cntr = 0;
        size_t rec_cntr = 0;

        lnf_mem_cursor_t *cursor; //cursor to current record
        lnf_mem_cursor_t *nth_rec_cursor = NULL; //cursor to Nth record
        MPI_Request request = MPI_REQUEST_NULL;

        uint64_t threshold;
        lnf_rec_t *rec;
        int sort_field = LNF_FLD_ZERO_;
        int sort_direction = LNF_SORT_NONE;


        /* Initialize LNF record. */
        secondary_errno = lnf_rec_init(&rec);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                PRINT_ERROR(primary_errno, secondary_errno, "lnf_rec_init()");
                goto send_terminator;
        }


        /* Set cursor to the first record in the memory. */
        secondary_errno = lnf_mem_first_c(stc->aggr_mem, &cursor);

        /* Loop through the first rec_limit or less records. */
        while (rec_cntr < stc->shared.rec_limit && cursor != NULL) {
                /* Read if there is enough space. Save space for record size. */
                secondary_errno = lnf_mem_read_raw_c(stc->aggr_mem, cursor,
                                (char *)stc->buff[buff_idx] + buff_off + sizeof (rec_size),
                                (int *)&rec_size,
                                XCHG_BUFF_SIZE - buff_off - sizeof (rec_size));
                assert(secondary_errno != LNF_EOF);

                /* Was there enough space in the buffer for the record? */
                if (secondary_errno != LNF_ERR_NOMEM) { //yes
                        *(uint32_t *)(stc->buff[buff_idx] + buff_off) = rec_size;
                        buff_off += sizeof (rec_size) + rec_size;

                        buff_rec_cntr++;
                        rec_cntr++;

                        /* Shift cursor to next record. */
                        nth_rec_cursor = cursor; //save for future usage
                        secondary_errno = lnf_mem_next_c(stc->aggr_mem,
                                        &cursor);
                } else { //no
                        /* Send buffer. */
                        MPI_Wait(&request, MPI_STATUS_IGNORE);
                        MPI_Isend(stc->buff[buff_idx], buff_off, MPI_BYTE,
                                        ROOT_PROC, TAG_DATA, MPI_COMM_WORLD,
                                        &request);
                        byte_cntr += buff_off;

                        /* Clear buffer context variables. */
                        buff_off = 0;
                        buff_rec_cntr = 0;
                        buff_idx = !buff_idx; //toggle data buffers
                }
        }
        if (secondary_errno == LNF_EOF) {
                goto send_remaining; //no records in memory or all records read
        } else if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                PRINT_ERROR(primary_errno, secondary_errno,
                                "lnf_mem_next_c() or lnf_mem_first_c()");
                goto free_lnf_rec;
        }


        /* Find sort attributes (key and direction) in the fields array. */
        for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
                if (stc->shared.fields[i].flags & LNF_SORT_FLAGS) {
                        sort_field = stc->shared.fields[i].id;
                        sort_direction =
                                stc->shared.fields[i].flags & LNF_SORT_FLAGS;
                        break;
                }
        }
        assert(sort_field != LNF_FLD_ZERO_);
        assert(sort_direction == LNF_SORT_ASC ||
                        sort_direction == LNF_SORT_DESC);

        /*
         * Read Nth record from sorted memory, fetch value of sort key and
         * compute threshold based on this value and slave count.
         */
        secondary_errno = lnf_mem_read_c(stc->aggr_mem, nth_rec_cursor, rec);
        assert(secondary_errno != LNF_EOF);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                PRINT_ERROR(primary_errno, secondary_errno, "lnf_mem_read_c()");
                goto free_lnf_rec;
        }

        secondary_errno = lnf_rec_fget(rec, sort_field, &threshold);
        assert(secondary_errno == LNF_OK);
        if (sort_direction == LNF_SORT_ASC) {
                threshold *= stc->slave_cnt;
        } else if (sort_direction == LNF_SORT_DESC) {
                threshold /= stc->slave_cnt;
        }


        /*
         * Send records until we have records and until:
         * sort field >= threshold if direction is descending
         * sort field <= threshold if direction is ascending
         */
        while (cursor != NULL) {
                uint64_t key_value;

                /* Read and check value of sort key. */
                secondary_errno = lnf_mem_read_c(stc->aggr_mem, cursor, rec);
                assert(secondary_errno != LNF_EOF);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        PRINT_ERROR(primary_errno, secondary_errno,
                                        "lnf_mem_read_c()");
                        goto free_lnf_rec;
                }

                secondary_errno = lnf_rec_fget(rec, sort_field, &key_value);
                assert(secondary_errno == LNF_OK);
                if ((sort_direction == LNF_SORT_ASC && key_value > threshold) ||
                                (sort_direction == LNF_SORT_DESC &&
                                 key_value < threshold)) {
                        break; //threshold reached
                }

                /* Read if there is enough space. Save space for record size. */
                secondary_errno = lnf_mem_read_raw_c(stc->aggr_mem, cursor,
                                (char *)stc->buff[buff_idx] + buff_off + sizeof (rec_size),
                                (int *)&rec_size,
                                XCHG_BUFF_SIZE - buff_off - sizeof (rec_size));
                assert(secondary_errno != LNF_EOF);

                /* Was there enough space in the buffer for the record? */
                if (secondary_errno != LNF_ERR_NOMEM) {
                        *(uint32_t *)(stc->buff[buff_idx] + buff_off) = rec_size;
                        buff_off += sizeof (rec_size) + rec_size;

                        buff_rec_cntr++;
                        rec_cntr++;

                        /* Shift cursor to next record. */
                        secondary_errno = lnf_mem_next_c(stc->aggr_mem,
                                        &cursor);
                } else {
                        /* Send buffer. */
                        MPI_Wait(&request, MPI_STATUS_IGNORE);
                        MPI_Isend(stc->buff[buff_idx], buff_off, MPI_BYTE,
                                        ROOT_PROC, TAG_DATA, MPI_COMM_WORLD,
                                        &request);
                        byte_cntr += buff_off;

                        /* Clear buffer context variables. */
                        buff_off = 0;
                        buff_rec_cntr = 0;
                        buff_idx = !buff_idx; //toggle data buffers
                }
        }
        if (secondary_errno != LNF_OK && secondary_errno != LNF_EOF) {
                primary_errno = E_LNF;
                PRINT_ERROR(primary_errno, secondary_errno, "lnf_mem_next_c()");
                goto free_lnf_rec;
        }


send_remaining:
        /* Send remaining records if data buffer is not empty. */
        if (buff_rec_cntr != 0) {
                MPI_Wait(&request, MPI_STATUS_IGNORE);
                MPI_Isend(stc->buff[buff_idx], buff_off, MPI_BYTE, ROOT_PROC,
                                TAG_DATA, MPI_COMM_WORLD, &request);
                byte_cntr += buff_off;
        }

free_lnf_rec:
        lnf_rec_free(rec);

send_terminator:
        /* Phase 1 done, notify master by empty DATA message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        /* Buffers will be invalid after return, wait for send to complete. */
        MPI_Wait(&request, MPI_STATUS_IGNORE);

        PRINT_DEBUG("read %zu, sent %zu B", rec_cntr, byte_cntr);

        return primary_errno;
}


static error_code_t fast_topn_recv_lookup_send(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;
        int secondary_errno;

        int rec_len;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records
        lnf_mem_cursor_t **lookup_cursors;
        size_t lookup_cursors_idx = 0;
        size_t lookup_cursors_size = LOOKUP_CURSOR_INIT_SIZE;
        size_t received_cnt = 0;


        /* Allocate some lookup cursors. */
        lookup_cursors = malloc(lookup_cursors_size *
                        sizeof (lnf_mem_cursor_t *));
        if (lookup_cursors == NULL) {
                secondary_errno = 0;
                PRINT_ERROR(E_MEM, secondary_errno, "malloc()");
                return E_MEM;
        }

        /* Receive all records. */
        while (true) {
                MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);
                if (rec_len == 0) {
                        break; //zero length -> all records received
                }

                MPI_Bcast(rec_buff, rec_len, MPI_BYTE, ROOT_PROC,
                                MPI_COMM_WORLD);
                received_cnt++;

                secondary_errno = lnf_mem_lookup_raw_c(stc->aggr_mem, rec_buff,
                                rec_len, &lookup_cursors[lookup_cursors_idx]);
                if (secondary_errno == LNF_EOF) {
                        continue; //record not found, nevermind
                } else if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        PRINT_ERROR(primary_errno, secondary_errno,
                                        "lnf_mem_lookup_raw_c()");
                        goto free_lookup_cursors;
                }
                lookup_cursors_idx++; //record found

                /* Add lookup cursors if needed. */
                if (lookup_cursors_idx == lookup_cursors_size) {
                        lnf_mem_cursor_t **tmp;

                        lookup_cursors_size *= 2; //increase size
                        tmp = realloc(lookup_cursors, lookup_cursors_size *
                                        sizeof (lnf_mem_cursor_t *));
                        if (tmp == NULL) {
                                primary_errno = E_MEM;
                                secondary_errno = 0;
                                PRINT_ERROR(primary_errno, secondary_errno,
                                                "realloc()");
                                goto free_lookup_cursors;
                        }
                        lookup_cursors = tmp;
                }
        }

        /* Send back found records. */
        for (size_t i = 0; i < lookup_cursors_idx; ++i) {
                //TODO: optimalization - send back only relevant records
                secondary_errno = lnf_mem_read_raw_c(stc->aggr_mem,
                                lookup_cursors[i], rec_buff, &rec_len,
                                LNF_MAX_RAW_LEN);
                assert(secondary_errno != LNF_EOF);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        PRINT_ERROR(primary_errno, secondary_errno,
                                        "lnf_mem_read_raw_c()");
                        goto free_lookup_cursors;
                }

                MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);
        }

free_lookup_cursors:
        free(lookup_cursors);

        PRINT_DEBUG("received %zu, found and sent %zu", received_cnt,
                        lookup_cursors_idx);

        return primary_errno;
}


static error_code_t task_postprocess(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;

        switch (stc->shared.working_mode) {
        case MODE_LIST:
                /* All records already sent during reading, send terminator. */
                MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);
                break;

        case MODE_SORT:
                if (stc->shared.rec_limit != 0) {
                        primary_errno = isend_loop(stc);
                } //else all records already sent while reading
                MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

                break;

        case MODE_AGGR:
                if (stc->shared.use_fast_topn) {
                        primary_errno = fast_topn_isend_loop(stc);
                        if (primary_errno != E_OK) {
                                return primary_errno;
                        }

                        primary_errno = fast_topn_recv_lookup_send(stc);
                } else {
                        primary_errno = isend_loop(stc);
                }
                MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

                break;

        case MODE_META:
                /* Nothing to do. */
                break;

        default:
                assert(!"unknown working mode");
        }

        PRINT_DEBUG("done");

        return primary_errno;
}


static void progress_report_init(size_t files_cnt)
{
        MPI_Gather(&files_cnt, 1, MPI_UNSIGNED_LONG, NULL, 0, MPI_UNSIGNED_LONG,
                        ROOT_PROC, MPI_COMM_WORLD);
}

static void progress_report_next(void)
{
        MPI_Request request = MPI_REQUEST_NULL;


        #pragma omp critical (mpi)
        {
                MPI_Isend(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_PROGRESS,
                                MPI_COMM_WORLD, &request);
                MPI_Request_free(&request);
        }
}


/*
 * There are two data buffers for each thread. The first one filled and
 * then passed to nonblocking MPI send function. In the meantime, the
 * second one is filled. After both these operations are completed,
 * buffers are switched and the whole process repeats until all data are
 * sent.
 */
static error_code_t process_parallel(struct slave_task_ctx *stc, char **paths,
                size_t paths_cnt)
{
        error_code_t primary_errno = E_OK;
        int secondary_errno;
        struct thread_ctx tc = { 0 };


        /* Initialize LNF record, only once for each thread. */
        secondary_errno = lnf_rec_init(&tc.rec);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                PRINT_ERROR(primary_errno, secondary_errno, "lnf_rec_init()");
                goto merge_mem;
        }

        /*
         * For master thread use already alocated buffers, for everybody else
         * allocate two new data buffers for records storage.
         */
        #pragma omp master
        {
                tc.buff[0] = stc->buff[0];
                tc.buff[1] = stc->buff[1];
        }
        if (tc.buff[0] != stc->buff[0]) {
                tc.buff[0] = malloc(XCHG_BUFF_SIZE * sizeof (**tc.buff));
                tc.buff[1] = malloc(XCHG_BUFF_SIZE * sizeof (**tc.buff));
                if (tc.buff[0] == NULL || tc.buff[1] == NULL) {
                        primary_errno = E_MEM;
                        PRINT_ERROR(E_MEM, 0, "malloc()");
                        goto free_lnf_rec;
                }
        }

        /* Parallel loop through all the files. */
        #pragma omp for schedule(guided) nowait
        for (size_t i = 0; i < paths_cnt; ++i) {
                /* Error on one of the threads. */
                if (primary_errno != E_OK) {
                        goto my_continue; //OpenMP cannot break from for loop
                }

                /* Open a flow file. */
                secondary_errno = lnf_open(&tc.file, paths[i], LNF_READ, NULL);
                if (secondary_errno != LNF_OK) {
                        PRINT_WARNING(E_LNF, secondary_errno,
                                        "unable to open file \"%s\"", paths[i]);
                        goto my_continue;
                }

                /* Increment the private metadata summary counters. */
                metadata_summ_update(&tc.metadata_summ, tc.file);

                //TODO: IF indexing
                if (stc->idx_tree) {
                        if (fidx_ips_in_file(paths[i], stc->idx_tree) == false) {
                                PRINT_DEBUG("file-indexing: skipping file \"%s\"",
                                                paths[i]);
                                goto my_continue;
                        }
                }

                /* Process the file according to the working mode. */
                switch (stc->shared.working_mode) {
                //According to GCC, #pragma omp flush may only be used in
                //compound statements before #pragma.
                #pragma omp flush //for stc->rec_limit_reached
                case MODE_LIST:
                        if (!stc->rec_limit_reached) {
                                primary_errno = task_send_file(stc, &tc);
                        }
                        break;

                case MODE_SORT:
                        if (stc->shared.rec_limit == 0) {
                                /* Local sort would be useless. */
                                primary_errno = task_send_file(stc, &tc);
                        } else {
                                /* Perform local sort first. */
                                primary_errno = task_store_file(stc, &tc);
                        }
                        break;

                case MODE_AGGR:
                        primary_errno = task_store_file(stc, &tc);
                        break;

                case MODE_META:
                        /* Metadata already read. */
                        break;

                default:
                        assert(!"unknown working mode");
                }

                lnf_close(tc.file);

my_continue:
                /* Report that another file has been processed. */
                progress_report_next();
        } //don't wait for other threads and start merging memory immediately

        /* Atomic addition of private summary counters into shared. */
        processed_summ_share(&stc->processed_summ, &tc.processed_summ);
        metadata_summ_share(&stc->metadata_summ, &tc.metadata_summ);

        /* Free data buffers. */
        if (tc.buff[0] != stc->buff[0]) {
                free(tc.buff[1]);
                free(tc.buff[0]);
        }
free_lnf_rec:
        /* Free LNF record. */
        lnf_rec_free(tc.rec);

merge_mem:
        /* Merge thread specific hash tables into one. */
        if (stc->aggr_mem) {
                lnf_mem_merge_threads(stc->aggr_mem);
        }


        return primary_errno;
}

error_code_t slave(int world_size)
{
        error_code_t primary_errno = E_OK;
        struct slave_task_ctx stc;
        char **paths = NULL;
        size_t paths_cnt = 0;

        memset(&stc, 0, sizeof (stc));
        stc.slave_cnt = world_size - 1; //all nodes without master

        /* Allocate two data buffers for records storage. */
        stc.buff[0] = malloc(XCHG_BUFF_SIZE * sizeof (**stc.buff));
        stc.buff[1] = malloc(XCHG_BUFF_SIZE * sizeof (**stc.buff));
        if (stc.buff[0] == NULL || stc.buff[1] == NULL) {
                primary_errno = E_MEM;
                PRINT_ERROR(E_MEM, 0, "malloc()");
                goto finalize_task;
        }

        //TODO: goto free_buffers will cause deadlock because of progress bar
        /* Wait for reception of task context from master. */
        primary_errno = task_receive_ctx(&stc);
        if (primary_errno != E_OK) {
                goto free_buffers;
        }

        /* Mode specific initialization. */
        primary_errno = task_init_mode(&stc);
        if (primary_errno != E_OK) {
                goto free_buffers;
        }

        /* Data source specific initialization. */
        paths = path_array_gen(stc.path_str, stc.shared.time_begin,
                        stc.shared.time_end, &paths_cnt);
        if (paths == NULL) {
                primary_errno = E_PATH;
                goto free_buffers;
        }

        /* Report number of files to be processed. */
        progress_report_init(paths_cnt);


#ifdef _OPENMP
        /* Spawn at most files count threads. */
        if (paths_cnt < (size_t)omp_get_max_threads()) {
                omp_set_num_threads(paths_cnt);
        }
#endif //_OPENMP

        #pragma omp parallel reduction(max:primary_errno)
        {
                primary_errno = process_parallel(&stc, paths, paths_cnt);
        } //impicit barrier
        if (primary_errno != E_OK) {
                goto free_buffers;
        }

        /*
         * In case of aggregation or sorting, records were stored into memory
         * and we need to process and send them to master.
         */
        primary_errno = task_postprocess(&stc);

        /* Reduce statistics to the master. */
        MPI_Reduce(&stc.processed_summ, NULL, 3, MPI_UINT64_T, MPI_SUM,
                        ROOT_PROC, MPI_COMM_WORLD);
        MPI_Reduce(&stc.metadata_summ, NULL, 15, MPI_UINT64_T, MPI_SUM,
                        ROOT_PROC, MPI_COMM_WORLD);

free_buffers:
        free(stc.buff[1]);
        free(stc.buff[0]);

finalize_task:
        path_array_free(paths, paths_cnt);
        task_free(&stc);

        return primary_errno;
}
