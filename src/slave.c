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


#define LOOKUP_CURSOR_INIT_SIZE 1024


/* Global variables. */
extern MPI_Datatype mpi_struct_shared_task_ctx;
extern int secondary_errno;


typedef enum {
        DATA_SOURCE_FILE,
        DATA_SOURCE_DIR,
        DATA_SOURCE_INTERVAL,
} data_source_t;


/* Thread shared. */
struct slave_task_ctx {
        /* Master and slave shared task context. Received from master. */
        struct shared_task_ctx shared; //master-slave shared

        /* Slave specific task context. */
        lnf_mem_t *aggr_mem; //LNF memory used for aggregation
        lnf_filter_t *filter; //LNF compiled filter expression
        char path_str[PATH_MAX]; //file or directory path string
        size_t proc_rec_cntr; //processed record counter
        bool rec_limit_reached; //true if rec_limit records read
        size_t slave_cnt; //slave count
        struct stats stats;
};


static void isend_bytes(void *data, size_t data_size, MPI_Request *req)
{
        /* Lack of MPI_THREAD_MULTIPLE threading level implies this CS. */
        #pragma omp critical (mpi)
        {
                MPI_Wait(req, MPI_STATUS_IGNORE);
                MPI_Isend(data, data_size, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD, req);
        }
}


static void stats_update(struct stats *s, lnf_rec_t *rec)
{
        uint64_t tmp;

        lnf_rec_fget(rec, LNF_FLD_AGGR_FLOWS, &tmp);
        s->flows += tmp;

        lnf_rec_fget(rec, LNF_FLD_DPKTS, &tmp);
        s->pkts += tmp;

        lnf_rec_fget(rec, LNF_FLD_DOCTETS, &tmp);
        s->bytes += tmp;
}

static void stats_share(struct stats *shared, struct stats *private)
{
        #pragma omp atomic
        shared->flows += private->flows;

        #pragma omp atomic
        shared->pkts += private->pkts;

        #pragma omp atomic
        shared->bytes += private->bytes;
}

static void stats_send(struct stats *s)
{
        MPI_Send(s, 3, MPI_UINT64_T, ROOT_PROC, TAG_STATS, MPI_COMM_WORLD);
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

        struct stats stats = {0};
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

                        isend_bytes(db, db_off, &request);
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

                stats_update(&stats, rec); //increment private stats counters

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
                isend_bytes(db, db_off, &request);
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

        stats_share(&stc->stats, &stats); //atomic increment of shared stats

        print_debug("<task_send_file> thread %d, file %s, read %zu, "
                        "processed %zu, sent %zu B", omp_get_thread_num(),
                        path, file_rec_cntr, file_proc_rec_cntr,
                        file_sent_bytes);

        lnf_rec_free(rec);
close_file:
        lnf_close(file);

        /* Buffers will be invalid after return, wait for send to complete. */
        MPI_Wait(&request, MPI_STATUS_IGNORE);

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
        struct stats stats = {0};

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
                stats_update(&stats, rec); //increment private stats counters

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

        stats_share(&stc->stats, &stats); //atomic increment of shared stats
        print_debug("<task_store_file> thread %d, file %s, read %zu, "
                        "processed %zu", omp_get_thread_num(),
                        path, file_rec_cntr, file_proc_rec_cntr);

free_lnf_rec:
        lnf_rec_free(rec);
close_file:
        lnf_close(file);

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

        /* Receivce task info. */
        MPI_Bcast(&stc->shared, 1, mpi_struct_shared_task_ctx, ROOT_PROC,
                        MPI_COMM_WORLD);

        /* If have filter epxression, receive filter expression string. */
        if (stc->shared.filter_str_len > 0) {
                char filter_str[stc->shared.filter_str_len + 1];

                MPI_Bcast(filter_str, stc->shared.filter_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);

                filter_str[stc->shared.filter_str_len] = '\0'; //termination
                primary_errno = task_init_filter(&stc->filter, filter_str);
                //it is OK not to chech primary_errno
        }

        /* If have path string, receive path string. */
        if (stc->shared.path_str_len > 0) {
                //PATH_MAX length already checked on master side
                MPI_Bcast(stc->path_str, stc->shared.path_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);
                stc->path_str[stc->shared.path_str_len] = '\0';//termination
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

static error_code_t send_loop(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;
        int rec_len;
        lnf_mem_cursor_t *read_cursor;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records

        secondary_errno = lnf_mem_first_c(stc->aggr_mem, &read_cursor);
        if (secondary_errno == LNF_EOF) {
                goto send_terminator; //no records in memory, no problem
        } else if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_mem_first_c()");
                goto send_terminator;
        }

        /* Send all records. */
        while (true) {
                secondary_errno = lnf_mem_read_raw_c(stc->aggr_mem, read_cursor,
                                rec_buff, &rec_len, LNF_MAX_RAW_LEN);
                assert(secondary_errno != LNF_EOF);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_read_raw_c()");
                        goto send_terminator;
                }

                MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);

                secondary_errno = lnf_mem_next_c(stc->aggr_mem, &read_cursor);
                if (secondary_errno == LNF_EOF) {
                        break; //all records successfully sent
                } else if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_next_c()");
                        goto send_terminator;
                }
        }

send_terminator:
        /* All sent or error, notify master by empty DATA message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        return primary_errno;
}


static error_code_t fast_topn_send_loop(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;
        int rec_len;
        lnf_mem_cursor_t *read_cursor;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records
        uint64_t threshold;
        lnf_rec_t *rec;
        int sort_key = LNF_SORT_NONE;
        size_t sent_cnt = 0;

        /* Send first rec_limit (top-N) records. */
        for (size_t i = 0; i < stc->shared.rec_limit; ++i) {
                if (i == 0) {
                        secondary_errno = lnf_mem_first_c(stc->aggr_mem,
                                        &read_cursor);
                } else {
                        secondary_errno = lnf_mem_next_c(stc->aggr_mem,
                                        &read_cursor);
                }
                if (secondary_errno == LNF_EOF) {
                        goto send_terminator; //no more records in memory
                } else if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_first_c or lnf_mem_next_c()");
                        goto send_terminator;
                }

                secondary_errno = lnf_mem_read_raw_c(stc->aggr_mem, read_cursor,
                                rec_buff, &rec_len, LNF_MAX_RAW_LEN);
                assert(secondary_errno != LNF_EOF);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_read_raw_c()");
                        goto send_terminator;
                }

                MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);
                sent_cnt++;
        }

        /* Initialize LNF record. Have to be unique in each OMP task. */
        secondary_errno = lnf_rec_init(&rec);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_rec_init()");
                goto send_terminator;
        }


        /* Find sort key in fields. */
        for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
                if (stc->shared.fields[i].flags & LNF_SORT_FLAGS) {
                        sort_key = stc->shared.fields[i].id;
                        break;
                }
        }
        assert(sort_key != LNF_SORT_NONE);

        /* Compute threshold from sort key of Nth record. */
        //TODO: handle also ascending order
        secondary_errno = lnf_mem_read_c(stc->aggr_mem, read_cursor, rec);
        assert(secondary_errno != LNF_EOF);
        if (secondary_errno == LNF_OK) {
                secondary_errno = lnf_rec_fget(rec, sort_key, &threshold);
                assert(secondary_errno == LNF_OK);
                threshold /= stc->slave_cnt;
        } else {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_mem_read_c()");
                goto free_lnf_rec;
        }

        /* Send records until key value >= threshold (top-K records). */
        while (true) {
                uint64_t key_value;

                secondary_errno = lnf_mem_next_c(stc->aggr_mem, &read_cursor);
                if (secondary_errno == LNF_EOF) {
                        break; //all records in memory successfully sent
                } else if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_next_c()");
                        goto free_lnf_rec;
                }

                secondary_errno = lnf_mem_read_c(stc->aggr_mem, read_cursor,
                                rec);
                assert(secondary_errno != LNF_EOF);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_read_c()");
                        goto free_lnf_rec;
                }

                secondary_errno = lnf_rec_fget(rec, sort_key, &key_value);
                assert(secondary_errno == LNF_OK);
                if (key_value < threshold) {
                        break; //threshold reached
                }

                secondary_errno = lnf_mem_read_raw_c(stc->aggr_mem, read_cursor,
                                rec_buff, &rec_len, LNF_MAX_RAW_LEN);
                assert(secondary_errno != LNF_EOF);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_read_raw_c()");
                        goto free_lnf_rec;
                }

                MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);
                sent_cnt++;
        }

free_lnf_rec:
        lnf_rec_free(rec);
send_terminator:
        /* Phase 1 done, notify master by empty DATA message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        print_debug("<fast_topn_send_loop> sent %zu", sent_cnt);
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

        /* Phase 3 done, notify master by empty DATA message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        print_debug("<fast_topn_recv_lookup_send> received %zu, "
                        "found and sent %zu", received_cnt, lookup_cursors_idx);
        return primary_errno;
}


static error_code_t task_postprocess(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;

        switch (stc->shared.working_mode) {
        case MODE_LIST:
                MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);
                break; //all records already sent while reading

        case MODE_SORT:
                if (stc->shared.rec_limit == 0) {
                        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                        MPI_COMM_WORLD);
                } else {
                        primary_errno = send_loop(stc);
                }

                break;

        case MODE_AGGR:
                if (stc->shared.use_fast_topn) {
                        primary_errno = fast_topn_send_loop(stc);
                        if (primary_errno != E_OK) {
                                return primary_errno;
                        }

                        primary_errno = fast_topn_recv_lookup_send(stc);
                } else {
                        primary_errno = send_loop(stc);
                }

                break;

        default:
                assert(!"unknown working mode");
        }

        print_debug("<task_postprocess> done");
        return primary_errno;
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
        if (stc.shared.path_str_len == 0) {
                primary_errno = f_array_fill_from_time(&files,
                                stc.shared.interval_begin,
                                stc.shared.interval_end);
                if (primary_errno != E_OK) {
                        goto finalize_task;
                }
        } else {
                primary_errno = f_array_fill_from_path(&files, stc.path_str);
                if (primary_errno != E_OK) {
                        goto finalize_task;
                }
        }

        /* Report number of files to be processed. */
        MPI_Gather(&files.f_cnt, 1, MPI_UNSIGNED_LONG, NULL, 0,
                        MPI_UNSIGNED_LONG, ROOT_PROC, MPI_COMM_WORLD);


        //TODO: return codes, secondary_errno
        #pragma omp parallel
        {
                /* Parallel loop through all the files. */
                #pragma omp for schedule(guided)
                for (size_t i = 0; i < files.f_cnt; ++i) {
                        const char *path = files.f_items[i].f_name;

                        if (stc.aggr_mem) {
                                primary_errno = task_store_file(&stc, path);
                        } else if (!stc.rec_limit_reached) {
                                primary_errno = task_send_file(&stc, path);
                        }

                        /* Report that another file has been processed. */
                        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_PROGRESS,
                                        MPI_COMM_WORLD);
                }

                /* Merge thread specific hash tables into one. */
                if (stc.aggr_mem) {
                        lnf_mem_merge_threads(stc.aggr_mem);
                }
        }

        /*
         * In case of aggregation or sorting, records were stored into memory
         * and we need to process and send them to master.
         */
        primary_errno = task_postprocess(&stc);

finalize_task:
        stats_send(&stc.stats);

        if (primary_errno != E_OK) { //always send terminator to master on error
                MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);
        }

        f_array_free(&files);
        task_free(&stc);

        return primary_errno;
}
