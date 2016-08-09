/**
 * \file slave.c
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
 * \author Pavel Krobot, <Pavel.Krobot@cesnet.cz>
 * \date 2015
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
#include "flookup.h"

#include <string.h> //strlen()
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <limits.h> //PATH_MAX
#include <unistd.h> //access

#include <mpi.h>
#include <omp.h>
#include <libnf.h>
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
extern int secondary_errno;


/* Thread shared. */
struct slave_task_ctx {
        /* Master and slave shared task context. Received from master. */
        struct shared_task_ctx shared; //master-slave shared

        /* Slave specific task context. */
        lnf_mem_t *aggr_mem; //LNF memory used for aggregation
        lnf_filter_t *filter; //LNF compiled filter expression
        char *path_str; //file/directory/profile(s) path string
        size_t proc_rec_cntr; //processed record counter
        bool rec_limit_reached; //true if rec_limit records read
        size_t slave_cnt; //slave count
        struct processed_summ processed_summ; //read from regular records
        struct metadata_summ metadata_summ; //read from metadata records
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


static void data_summ_update(struct processed_summ *s, lnf_rec_t *rec)
{
        uint64_t tmp;

        lnf_rec_fget(rec, LNF_FLD_AGGR_FLOWS, &tmp);
        s->flows += tmp;

        lnf_rec_fget(rec, LNF_FLD_DPKTS, &tmp);
        s->pkts += tmp;

        lnf_rec_fget(rec, LNF_FLD_DOCTETS, &tmp);
        s->bytes += tmp;
}

static void data_summ_share(struct processed_summ *shared,
                struct processed_summ *private)
{
        #pragma omp atomic
        shared->flows += private->flows;

        #pragma omp atomic
        shared->pkts += private->pkts;

        #pragma omp atomic
        shared->bytes += private->bytes;
}

static void data_summ_send(struct processed_summ *s)
{
        MPI_Send(s, 3, MPI_UINT64_T, ROOT_PROC, TAG_STATS, MPI_COMM_WORLD);
}


static void metadata_summ_read(struct metadata_summ *shared, lnf_file_t *file)
{
        struct metadata_summ private;


        /* Flows: read to local variable, check validity and update shared. */
        assert(lnf_info(file, LNF_INFO_FLOWS, &private.flows,
                                sizeof (private.flows)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_FLOWS_TCP, &private.flows_tcp,
                                sizeof (private.flows_tcp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_FLOWS_UDP, &private.flows_udp,
                                sizeof (private.flows_udp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_FLOWS_ICMP, &private.flows_icmp,
                                sizeof (private.flows_icmp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_FLOWS_OTHER, &private.flows_other,
                                sizeof (private.flows_other)) == LNF_OK);

        if (private.flows != private.flows_tcp + private.flows_udp +
                        private.flows_icmp + private.flows_other) {
                print_warn(E_LNF, 0, "metadata flow count mismatch "
                                "(total != TCP + UDP + ICMP + other)");
        }

        #pragma omp atomic
        shared->flows += private.flows;
        #pragma omp atomic
        shared->flows_tcp += private.flows_tcp;
        #pragma omp atomic
        shared->flows_udp += private.flows_udp;
        #pragma omp atomic
        shared->flows_icmp += private.flows_icmp;
        #pragma omp atomic
        shared->flows_other += private.flows_other;

        /* Packets: read to local variable, check validity and update shared. */
        assert(lnf_info(file, LNF_INFO_PACKETS, &private.pkts,
                                sizeof (private.pkts)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_PACKETS_TCP, &private.pkts_tcp,
                                sizeof (private.pkts_tcp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_PACKETS_UDP, &private.pkts_udp,
                                sizeof (private.pkts_udp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_PACKETS_ICMP, &private.pkts_icmp,
                                sizeof (private.pkts_icmp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_PACKETS_OTHER, &private.pkts_other,
                                sizeof (private.pkts_other)) == LNF_OK);

        if (private.pkts != private.pkts_tcp + private.pkts_udp +
                        private.pkts_icmp + private.pkts_other) {
                print_warn(E_LNF, 0, "metadata packet count mismatch "
                                "(total != TCP + UDP + ICMP + other)");
        }

        #pragma omp atomic
        shared->pkts += private.pkts;
        #pragma omp atomic
        shared->pkts_tcp += private.pkts_tcp;
        #pragma omp atomic
        shared->pkts_udp += private.pkts_udp;
        #pragma omp atomic
        shared->pkts_icmp += private.pkts_icmp;
        #pragma omp atomic
        shared->pkts_other += private.pkts_other;

        /* Bytes: read to local variable, check validity and update shared. */
        assert(lnf_info(file, LNF_INFO_BYTES, &private.bytes,
                                sizeof (private.bytes)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_BYTES_TCP, &private.bytes_tcp,
                                sizeof (private.bytes_tcp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_BYTES_UDP, &private.bytes_udp,
                                sizeof (private.bytes_udp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_BYTES_ICMP, &private.bytes_icmp,
                                sizeof (private.bytes_icmp)) == LNF_OK);
        assert(lnf_info(file, LNF_INFO_BYTES_OTHER, &private.bytes_other,
                                sizeof (private.bytes_other)) == LNF_OK);

        if (private.bytes != private.bytes_tcp + private.bytes_udp +
                        private.bytes_icmp + private.bytes_other) {
                print_warn(E_LNF, 0, "metadata bytes count mismatch "
                                "(total != TCP + UDP + ICMP + other)");
        }

        #pragma omp atomic
        shared->bytes += private.bytes;
        #pragma omp atomic
        shared->bytes_tcp += private.bytes_tcp;
        #pragma omp atomic
        shared->bytes_udp += private.bytes_udp;
        #pragma omp atomic
        shared->bytes_icmp += private.bytes_icmp;
        #pragma omp atomic
        shared->bytes_other += private.bytes_other;
}

static void metadata_summ_send(struct metadata_summ *s)
{
        MPI_Send(s, 15, MPI_UINT64_T, ROOT_PROC, TAG_STATS, MPI_COMM_WORLD);
}

static error_code_t task_send_file(struct slave_task_ctx *stc, const char *path)
{
        error_code_t primary_errno = E_OK;
        int secondary_errno;

        size_t file_rec_cntr = 0;
        size_t file_proc_rec_cntr = 0;
        size_t file_sent_bytes = 0;

        uint8_t db_mem[2][XCHG_BUFF_SIZE]; //data buffer
        bool db_mem_idx = 0; //currently used data buffer index
        uint8_t *db = db_mem[db_mem_idx]; //pointer to currently used data buff
        size_t db_off = 0; //data buffer offset
        size_t db_rec_cntr = 0; //number of records in current buffer

        MPI_Request request = MPI_REQUEST_NULL;
        lnf_file_t *file;
        lnf_rec_t *rec;
        uint32_t rec_size = 0;

        struct processed_summ processed_summ = {0};
        struct {
                int id;
                size_t size;
        } fast_fields[LNF_FLD_TERM_];//fields array compressed for faster access
        size_t fast_fields_cnt = 0;


        /* Open flow file. */
        secondary_errno = lnf_open(&file, path, LNF_READ, NULL);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "unable to open file \"%s\"",
                                path);
                return E_LNF;
        }

        //TODO: no longer using OMP tasks
        /* Initialize LNF record. Have to be unique in each OMP task. */
        secondary_errno = lnf_rec_init(&rec);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_rec_init()");
                goto close_file;
        }

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
        while ((secondary_errno = lnf_read(file, rec)) == LNF_OK) {
                file_rec_cntr++;

                /* Apply filter (if there is any). */
                if (stc->filter && !lnf_filter_match(stc->filter, rec)) {
                        continue;
                }
                file_proc_rec_cntr++;

                /* Is there enough space in the buffer for the next record? */
                if (db_off + rec_size + sizeof (rec_size) > XCHG_BUFF_SIZE) {
                        /* Check record limit and break if exceeded. */
                        if (stc->rec_limit_reached) {
                                db_rec_cntr = 0;
                                break; //record limit reached by another thread
                        }

                        wait_isend_cs(db, db_off, &request);
                        file_sent_bytes += db_off;

                        /* Increment shared counter. */
                        #pragma omp atomic
                        stc->proc_rec_cntr += db_rec_cntr;

                        /* Clear buffer context variables. */
                        db_off = 0;
                        db_rec_cntr = 0;
                        db_mem_idx = !db_mem_idx; //toggle data buffers
                        db = db_mem[db_mem_idx];

                        /* Check record limit again and break if exceeded. */
                        if (stc->shared.rec_limit && stc->proc_rec_cntr >=
                                        stc->shared.rec_limit) {
                                stc->rec_limit_reached = true;
                                break; //record limit reached by this thread
                        }
                }

                /* Increment private processed_summ counters. */
                data_summ_update(&processed_summ, rec);

                *(uint32_t *)(db + db_off) = rec_size;
                db_off += sizeof (rec_size);

                /* Loop through the fields and fill the data buffer. */
                for (size_t i = 0; i < fast_fields_cnt; ++i) {
                        lnf_rec_fget(rec, fast_fields[i].id, db + db_off);
                        db_off += fast_fields[i].size;
                }

                db_rec_cntr++;
        }

        /* Send remaining records if data buffer is not empty. */
        if (db_rec_cntr != 0) {
                wait_isend_cs(db, db_off, &request);
                file_sent_bytes += db_off;

                #pragma omp atomic
                stc->proc_rec_cntr += db_rec_cntr; //increment shared counter
        }

        /* Eventually set record limit reached flag. */
        if (stc->shared.rec_limit && stc->proc_rec_cntr >=
                        stc->shared.rec_limit) {
                stc->rec_limit_reached = true;
        /* Otherwise check if EOF was reached. */
        } else if (secondary_errno != LNF_EOF) {
                primary_errno = E_LNF; //no it wasn't, a problem occured
        }

        /* Atomic increment of shared processed_summ by all threads. */
        data_summ_share(&stc->processed_summ, &processed_summ);
        metadata_summ_read(&stc->metadata_summ, file);

        lnf_rec_free(rec);
close_file:
        lnf_close(file);

        /* Buffers will be invalid after return, wait for send to complete. */
        #pragma omp critical (mpi)
        {
                MPI_Wait(&request, MPI_STATUS_IGNORE);
        }

        print_debug("<task_send_file> thread %d, file %s, read %zu, "
                        "processed %zu, sent %zu B", omp_get_thread_num(),
                        path, file_rec_cntr, file_proc_rec_cntr,
                        file_sent_bytes);


        return primary_errno;
}


static error_code_t task_store_file(struct slave_task_ctx *stc,
                const char *path)
{
        error_code_t primary_errno = E_OK;
        int secondary_errno;

        size_t file_rec_cntr = 0;
        size_t file_proc_rec_cntr = 0;
        lnf_file_t *file;
        lnf_rec_t *rec;
        struct processed_summ processed_summ = {0};

        /* Open flow file. */
        secondary_errno = lnf_open(&file, path, LNF_READ, NULL);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "unable to open file \"%s\"",
                                path);
                return E_LNF;
        }

        /* Initialize LNF record. Have to be unique in each OMP task. */
        secondary_errno = lnf_rec_init(&rec);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_rec_init()");
                goto close_file;
        }

        /*
         * Read all records from file. Hot path.
         * Aggreagation -> write record to memory and continue.
         * Ignore record limit.
         */
        while ((secondary_errno = lnf_read(file, rec)) == LNF_OK) {
                file_rec_cntr++;

                /* Apply filter (if there is any). */
                if (stc->filter && !lnf_filter_match(stc->filter, rec)) {
                        continue;
                }
                file_proc_rec_cntr++;
                /* Increment private processed_summ counters. */
                data_summ_update(&processed_summ, rec);

                secondary_errno = lnf_mem_write(stc->aggr_mem, rec);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_write()");
                        goto free_lnf_rec;
                }

        }

        /* Check if we reach end of file. */
        if (secondary_errno != LNF_EOF) {
                primary_errno = E_LNF; //no, we didn't, a problem occured
        }

        /* Atomic increment of shared processed_summ by all threads. */
        data_summ_share(&stc->processed_summ, &processed_summ);
        metadata_summ_read(&stc->metadata_summ, file);

free_lnf_rec:
        lnf_rec_free(rec);
close_file:
        lnf_close(file);

        print_debug("<task_store_file> thread %d, file %s, read %zu, "
                        "processed %zu", omp_get_thread_num(),
                        path, file_rec_cntr, file_proc_rec_cntr);

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
        free(stc->path_str);
}


static error_code_t task_init_filter(lnf_filter_t **filter, char *filter_str)
{
        assert(filter != NULL && filter_str != NULL && strlen(filter_str) != 0);

        /* Initialize filter. */
        //TODO: try new filter
        secondary_errno = lnf_filter_init(filter, filter_str);
        //secondary_errno = lnf_filter_init_v2(filter, filter_str);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno,
                                "cannot initialise filter \"%s\"", filter_str);
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
                print_err(E_MEM, 0, "calloc()");
                return E_MEM;
        }
        MPI_Bcast(stc->path_str, stc->shared.path_str_len, MPI_CHAR, ROOT_PROC,
                        MPI_COMM_WORLD);

        if (stc->shared.filter_str_len > 0) {
                char filter_str[stc->shared.filter_str_len + 1];

                MPI_Bcast(filter_str, stc->shared.filter_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);

                filter_str[stc->shared.filter_str_len] = '\0'; //termination
                primary_errno = task_init_filter(&stc->filter, filter_str);
                //it is OK not to chech primary_errno
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

        case MODE_PASS:
                return E_PASS;

        default:
                assert(!"unknown working mode");
        }

        return primary_errno;
}


static error_code_t isend_loop(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;

        uint8_t db_mem[2][XCHG_BUFF_SIZE]; //data buffer
        bool db_mem_idx = 0; //currently used data buffer index
        uint8_t *db = db_mem[db_mem_idx]; //pointer to currently used data buff
        size_t db_off = 0; //data buffer offset
        size_t db_rec_cntr = 0; //number of records in current buffer

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
                                (char *)db + db_off + sizeof (rec_size),
                                (int *)&rec_size,
                                XCHG_BUFF_SIZE - db_off - sizeof (rec_size));
                assert(secondary_errno != LNF_EOF);

                /* Was there enough space in the buffer for the record? */
                if (secondary_errno != LNF_ERR_NOMEM) {
                        *(uint32_t *)(db + db_off) = rec_size; //write rec size
                        db_off += sizeof (rec_size) + rec_size; //shift db off

                        db_rec_cntr++;
                        rec_cntr++;

                        /* Shift cursor to next record. */
                        secondary_errno = lnf_mem_next_c(stc->aggr_mem,
                                        &cursor);
                } else {
                        /* Send buffer. */
                        MPI_Wait(&request, MPI_STATUS_IGNORE);
                        MPI_Isend(db, db_off, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                        MPI_COMM_WORLD, &request);
                        byte_cntr += db_off;

                        /* Clear buffer context variables. */
                        db_off = 0;
                        db_rec_cntr = 0;
                        db_mem_idx = !db_mem_idx; //toggle data buffers
                        db = db_mem[db_mem_idx];
                }
        }
        if (secondary_errno != LNF_EOF) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno,
                                "lnf_mem_next_c() or lnf_mem_first_c()");
        }


        /* Send remaining records if data buffer is not empty. */
        if (db_rec_cntr != 0) {
                MPI_Wait(&request, MPI_STATUS_IGNORE);
                MPI_Isend(db, db_off, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD, &request);
                byte_cntr += db_off;
        }

        /* Buffers will be invalid after return, wait for send to complete. */
        MPI_Wait(&request, MPI_STATUS_IGNORE);

        print_debug("<isend_loop> read %zu, sent %zu B", rec_cntr, byte_cntr);

        return primary_errno;
}


static error_code_t fast_topn_isend_loop(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;

        uint8_t db_mem[2][XCHG_BUFF_SIZE]; //data buffer
        bool db_mem_idx = 0; //currently used data buffer index
        uint8_t *db = db_mem[db_mem_idx]; //pointer to currently used data buff
        size_t db_off = 0; //data buffer offset
        size_t db_rec_cntr = 0; //number of records in current buffer

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


        /* Initialize LNF record. TODO: use global record */
        secondary_errno = lnf_rec_init(&rec);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_rec_init()");
                goto send_terminator;
        }


        /* Set cursor to the first record in the memory. */
        secondary_errno = lnf_mem_first_c(stc->aggr_mem, &cursor);

        /* Loop through the first rec_limit or less records. */
        while (rec_cntr < stc->shared.rec_limit && cursor != NULL) {
                /* Read if there is enough space. Save space for record size. */
                secondary_errno = lnf_mem_read_raw_c(stc->aggr_mem, cursor,
                                (char *)db + db_off + sizeof (rec_size),
                                (int *)&rec_size,
                                XCHG_BUFF_SIZE - db_off - sizeof (rec_size));
                assert(secondary_errno != LNF_EOF);

                /* Was there enough space in the buffer for the record? */
                if (secondary_errno != LNF_ERR_NOMEM) {
                        *(uint32_t *)(db + db_off) = rec_size; //write rec size
                        db_off += sizeof (rec_size) + rec_size; //shift db off

                        db_rec_cntr++;
                        rec_cntr++;

                        /* Shift cursor to next record. */
                        nth_rec_cursor = cursor; //save for future usage
                        secondary_errno = lnf_mem_next_c(stc->aggr_mem,
                                        &cursor);
                } else {
                        /* Send buffer. */
                        MPI_Wait(&request, MPI_STATUS_IGNORE);
                        MPI_Isend(db, db_off, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                        MPI_COMM_WORLD, &request);
                        byte_cntr += db_off;

                        /* Clear buffer context variables. */
                        db_off = 0;
                        db_rec_cntr = 0;
                        db_mem_idx = !db_mem_idx; //toggle data buffers
                        db = db_mem[db_mem_idx];
                }
        }
        if (secondary_errno == LNF_EOF) {
                goto send_remaining; //no records in memory or all records read
        } else if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno,
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
                print_err(primary_errno, secondary_errno, "lnf_mem_read_c()");
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
                        print_err(primary_errno, secondary_errno,
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
                                (char *)db + db_off + sizeof (rec_size),
                                (int *)&rec_size,
                                XCHG_BUFF_SIZE - db_off - sizeof (rec_size));
                assert(secondary_errno != LNF_EOF);

                /* Was there enough space in the buffer for the record? */
                if (secondary_errno != LNF_ERR_NOMEM) {
                        *(uint32_t *)(db + db_off) = rec_size; //write rec size
                        db_off += sizeof (rec_size) + rec_size; //shift db off

                        db_rec_cntr++;
                        rec_cntr++;

                        /* Shift cursor to next record. */
                        secondary_errno = lnf_mem_next_c(stc->aggr_mem,
                                        &cursor);
                } else {
                        /* Send buffer. */
                        MPI_Wait(&request, MPI_STATUS_IGNORE);
                        MPI_Isend(db, db_off, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                        MPI_COMM_WORLD, &request);
                        byte_cntr += db_off;

                        /* Clear buffer context variables. */
                        db_off = 0;
                        db_rec_cntr = 0;
                        db_mem_idx = !db_mem_idx; //toggle data buffers
                        db = db_mem[db_mem_idx];
                }
        }
        if (secondary_errno != LNF_OK && secondary_errno != LNF_EOF) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_mem_next_c()");
                goto free_lnf_rec;
        }


send_remaining:
        /* Send remaining records if data buffer is not empty. */
        if (db_rec_cntr != 0) {
                MPI_Wait(&request, MPI_STATUS_IGNORE);
                MPI_Isend(db, db_off, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD, &request);
                byte_cntr += db_off;
        }

free_lnf_rec:
        lnf_rec_free(rec);

send_terminator:
        /* Phase 1 done, notify master by empty DATA message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        /* Buffers will be invalid after return, wait for send to complete. */
        MPI_Wait(&request, MPI_STATUS_IGNORE);

        print_debug("<fast_topn_isend_loop> read %zu, sent %zu B", rec_cntr,
                        byte_cntr);

        return primary_errno;
}


static error_code_t fast_topn_recv_lookup_send(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;
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
                print_err(E_MEM, secondary_errno, "malloc()");
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
                        print_err(primary_errno, secondary_errno,
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
                                print_err(primary_errno, secondary_errno,
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
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_read_raw_c()");
                        goto free_lookup_cursors;
                }

                MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);
        }

free_lookup_cursors:
        free(lookup_cursors);

        print_debug("<fast_topn_recv_lookup_send> received %zu, "
                        "found and sent %zu", received_cnt, lookup_cursors_idx);
        return primary_errno;
}


static error_code_t task_postprocess(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;

        switch (stc->shared.working_mode) {
        case MODE_LIST:
                break; //all records already sent while reading

        case MODE_SORT:
                if (stc->shared.rec_limit != 0) {
                        primary_errno = isend_loop(stc);
                } //else all records already sent while reading

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

                break;

        default:
                assert(!"unknown working mode");
        }

        print_debug("<task_postprocess> done");
        return primary_errno;
}


void progress_report_init(size_t files_cnt)
{
        MPI_Gather(&files_cnt, 1, MPI_UNSIGNED_LONG, NULL, 0, MPI_UNSIGNED_LONG,
                        ROOT_PROC, MPI_COMM_WORLD);
}

void progress_report_next(void)
{
        MPI_Request request = MPI_REQUEST_NULL;


        #pragma omp critical (mpi)
        {
                MPI_Isend(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_PROGRESS,
                                MPI_COMM_WORLD, &request);
                MPI_Request_free(&request);
        }
}


error_code_t slave(int world_size)
{
        error_code_t primary_errno = E_OK;
        struct slave_task_ctx stc;
        f_array_t files;


        memset(&stc, 0, sizeof (stc));
        f_array_init(&files);

        stc.slave_cnt = world_size - 1; //all nodes without master


        /* Wait for reception of task context from master. */
        primary_errno = task_receive_ctx(&stc);
        if (primary_errno != E_OK) {
                goto finalize_task;
        }

        /* Mode specific initialization. */
        primary_errno = task_init_mode(&stc);
        if (primary_errno != E_OK) {
                goto finalize_task;
        }

        /* Data source specific initialization. */
        primary_errno = f_array_fill(&files, stc.path_str,
                        stc.shared.time_begin, stc.shared.time_end);
        if (primary_errno != E_OK) {
                goto finalize_task;
        }

        /* Report number of files to be processed. */
        progress_report_init(files.f_cnt);


        //TODO: return codes, secondary_errno
        #pragma omp parallel
        {
                /* Parallel loop through all the files. */
                #pragma omp for schedule(guided) nowait
                for (size_t i = 0; i < files.f_cnt; ++i) {
                        const char *path = files.f_items[i].f_name;

                        if (stc.aggr_mem) {
                                primary_errno = task_store_file(&stc, path);
                        } else if (!stc.rec_limit_reached) {
                                primary_errno = task_send_file(&stc, path);
                        }

                        /* Report that another file has been processed. */
                        progress_report_next();
                } //don't wait for other threads and start merging memory

                /* Merge thread specific hash tables into one. */
                if (stc.aggr_mem) {
                        lnf_mem_merge_threads(stc.aggr_mem);
                }
        } //impicit barrier

        /*
         * In case of aggregation or sorting, records were stored into memory
         * and we need to process and send them to master.
         */
        primary_errno = task_postprocess(&stc);

finalize_task:
        /* Send terminator to master even on error. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        data_summ_send(&stc.processed_summ);
        metadata_summ_send(&stc.metadata_summ);

        f_array_free(&files);
        task_free(&stc);

        return primary_errno;
}
