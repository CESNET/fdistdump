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

#define _BSD_SOURCE //d_type

#include "slave.h"
#include "common.h"

#include <string.h> //strlen()
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <limits.h> //PATH_MAX
#include <unistd.h> //access
#include <stdint.h>

#include <mpi.h>
#include <libnf.h>
#include <dirent.h> //list directory
#include <sys/stat.h>


#define LOOKUP_CURSOR_INIT_SIZE 1024


extern MPI_Datatype mpi_struct_task_info;

typedef enum {
        DATA_SOURCE_FILE,
        DATA_SOURCE_DIR,
        DATA_SOURCE_INTERVAL,
} data_source_t;

struct slave_task {
        working_mode_t working_mode;
        lnf_rec_t *rec;
        lnf_filter_t *filter;
        lnf_mem_t *agg;
        size_t rec_limit; //record limit
        size_t proc_rec_cntr; //processed record counter

        data_source_t data_source; //how flow files are obtained
        char path_str[PATH_MAX];
        DIR *dir_ctx; //used in case of directory as data source
        struct tm interval_begin; //begin of time interval
        struct tm interval_end; //end of time interval

        char cur_file_path[PATH_MAX]; //current flow file absolute path

        int sort_key; //LNF field set as key for sorting in memory

        bool use_fast_topn; //enables fast top-N algorithm
};


static void isend_bytes(void *src, size_t bytes, MPI_Request *req)
{
        MPI_Wait(req, MPI_STATUS_IGNORE);
        MPI_Isend(src, bytes, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD,
                        req);
}


static void task_process_file(struct slave_task *st)
{
        size_t file_rec_cntr = 0, file_proc_rec_cntr = 0;
        int ret, err = E_OK;

        lnf_file_t *file;

        static lnf_brec1_t data_buff[2][XCHG_BUFF_ELEMS];
        size_t data_idx = 0;
        bool buff_idx = 0;
        MPI_Request request = MPI_REQUEST_NULL;

        /* Open flow file. */
        ret = lnf_open(&file, st->cur_file_path, LNF_READ, NULL);
        if (ret != LNF_OK) {
                print_err("cannot open file %s", st->cur_file_path);
                err = ret;
                return;
        }

        /* Read all records from file. */
        for (ret = lnf_read(file, st->rec); ret == LNF_OK;
                        ret = lnf_read(file, st->rec)) {

                file_rec_cntr++;

                /* Apply filter (if there is any). */
                if (st->filter && !lnf_filter_match(st->filter, st->rec)) {
                        continue;
                }
                st->proc_rec_cntr++;
                file_proc_rec_cntr++;

                /* Aggreagation -> write record to mem (ignore record limit). */
                if (st->agg) {
                        ret = lnf_mem_write(st->agg, st->rec);
                        assert(ret == LNF_OK); //TODO: return value
                        continue;
                }

                /* No aggregation -> store to buffer and send if buffer full. */
                ret = lnf_rec_fget(st->rec, LNF_FLD_BREC1, data_buff[buff_idx] +
                                data_idx++);
                assert(ret == LNF_OK);

                if (data_idx == XCHG_BUFF_ELEMS) { //buffer full -> send
                        isend_bytes(data_buff[buff_idx], XCHG_BUFF_SIZE,
                                        &request);
                        data_idx = 0;
                        buff_idx = !buff_idx; //toggle buffers
                }

                if (st->proc_rec_cntr == st->rec_limit) {
                        break; //record limit reached
                }
        }
        err = (ret == LNF_EOF) ? err : ret; //if error during lnf_read()

        /* Send remaining records (if there are any). */
        if (data_idx != 0) {
                isend_bytes(data_buff[buff_idx], data_idx * sizeof(lnf_brec1_t),
                                &request);
        }

        /* Per file cleanup. */
        lnf_close(file);

        print_debug("slave: file %s\tread %lu\tprocessed %lu\n",
                        st->cur_file_path, file_rec_cntr, file_proc_rec_cntr);
}


static int task_init_filter(lnf_filter_t **filter, char *filter_str)
{
        size_t filter_str_len = strlen(filter_str);
        int ret, err = E_OK;

        /* Filter can be empty - don't use filter at all. */
        if (filter_str_len == 0) {
                return err;
        }

        /* Initialize filter. */
        ret = lnf_filter_init(filter, filter_str);
        if (ret != LNF_OK) {
                print_err("LNF - cannot initialise filter \"%s\"", filter_str);
                err = E_LNF;
        }

        return err;
}


int task_next_file(struct slave_task *st)
{
        struct dirent *dir_entry;
        int ret;

        switch (st->data_source) {
        case DATA_SOURCE_FILE:
                if (strlen(st->cur_file_path) == 0) {
                        strcpy(st->cur_file_path, st->path_str);
                } else {
                        return E_EOF;
                }
                break;

        case DATA_SOURCE_DIR:
                do { //in directory skip everything but regular files
                        dir_entry = readdir(st->dir_ctx);
                } while (dir_entry && dir_entry->d_type != DT_REG);

                if (dir_entry != NULL) { //found regular file
                        strcpy(st->cur_file_path, st->path_str); //copy dirname
                        strcat(st->cur_file_path, dir_entry->d_name); //+filename
                } else { //didn't find next regular file
                        return E_EOF;
                }
                break;

        case DATA_SOURCE_INTERVAL:
                /* Loop through entire interval, modify interval_begin. */
                while (diff_tm(st->interval_end, st->interval_begin) > 0.0) {
                        //TODO: meaningfull path check and error report
                        strftime(st->cur_file_path, sizeof(st->cur_file_path),
                                        FLOW_FILE_PATH, &st->interval_begin);
                        st->interval_begin.tm_sec += FLOW_FILE_ROTATION_INTERVAL;
                        mktime(&st->interval_begin); //struct tm normalization

                        ret = access(st->cur_file_path, F_OK);
                        if (ret == 0) {
                                return E_OK; //file exists
                        }

                        printf("warning: skipping nonexistent file \"%s\"\n",
                                        st->cur_file_path);
                }
                return E_EOF; //whole interval

        default:
                assert(!"unknown data source");
        }

        return E_OK;
}

static void task_free(struct slave_task *st)
{
        closedir(st->dir_ctx);
        if (st->rec) {
                lnf_rec_free(st->rec);
        }
        if (st->filter) {
                lnf_filter_free(st->filter);
        }
        if (st->agg) {
                lnf_mem_free(st->agg);
        }
}


static int task_await_new(struct slave_task *st)
{
        int ret;
        struct task_info ti;

        MPI_Bcast(&ti, 1, mpi_struct_task_info, ROOT_PROC, MPI_COMM_WORLD);

        if (ti.filter_str_len > 0) { //have filter epxression
                char filter_str[ti.filter_str_len + 1];

                MPI_Bcast(filter_str, ti.filter_str_len, MPI_CHAR, ROOT_PROC,
                                MPI_COMM_WORLD);

                filter_str[ti.filter_str_len] = '\0';
                task_init_filter(&st->filter, filter_str);
        }

        if (ti.path_str_len > 0) { //have path string
                //PATH_MAX length check on master side
                MPI_Bcast(st->path_str, ti.path_str_len, MPI_CHAR, ROOT_PROC,
                                MPI_COMM_WORLD);
                st->path_str[ti.path_str_len] = '\0';
        }

        st->working_mode = ti.working_mode;
        st->rec_limit = ti.rec_limit;
        st->interval_begin = ti.interval_begin;
        st->interval_end = ti.interval_end;
        st->use_fast_topn = ti.use_fast_topn;

        switch (st->working_mode) {
        case MODE_REC:
                break;
        case MODE_ORD:
                if (st->rec_limit == 0) {
                        break; //don't need memory, local sort would be useless
                }

                /* Sort all records localy, then send fist rec_limit records. */
                ret = lnf_mem_init(&st->agg);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_init()");
                        return E_LNF;
                }

                ret = lnf_mem_setopt(st->agg, LNF_OPT_LISTMODE, NULL, 0);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_setopt()");
                        return E_LNF;
                }

                ret = mem_setup(st->agg, ti.agg_params, ti.agg_params_cnt);
                if (ret != E_OK) {
                        return E_LNF;
                }
                break;
        case MODE_AGG:
                ret = lnf_mem_init(&st->agg);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_init()");
                        return E_LNF;
                }

                ret = mem_setup(st->agg, ti.agg_params, ti.agg_params_cnt);
                if (ret != E_OK) {
                        return E_LNF;
                }

                /* Find and store sort key. */
                for (size_t i = 0; i < ti.agg_params_cnt; ++i) {
                        if (ti.agg_params[i].flags & LNF_SORT_FLAGS) {
                                st->sort_key = ti.agg_params[i].field;
                        }
                }
                break;
        default:
                assert(!"unknown working mode");
        }

        /* Initialize empty LNF record to future reading. */
        ret = lnf_rec_init(&st->rec);
        if (ret != LNF_OK) {
                print_err("cannot initialise empty record object");
                st->rec = NULL;
        }

        if (ti.path_str_len > 0) { //have path string (file or directory)
                struct stat stat_buff;

                ret = stat(st->path_str, &stat_buff);
                if (ret == -1) {
                        print_err("%s \"%s\"", strerror(errno), st->path_str);
                        return E_ARG;
                }

                if (S_ISDIR(stat_buff.st_mode)) { //path is directory
                        st->data_source = DATA_SOURCE_DIR;

                        st->dir_ctx = opendir(st->path_str);
                        if (st->dir_ctx == NULL) {
                                print_err("cannot open directory \"%s\"",
                                                st->path_str);
                                return E_ARG;
                        }
                        if (ti.path_str_len >= (PATH_MAX - NAME_MAX)) {
                                errno = ENAMETOOLONG;
                                print_err("%s \"%s\"", strerror(errno),
                                                st->path_str);
                                return E_ARG;
                        }

                        //check and add missing terminating slash
                        if (st->path_str[ti.path_str_len - 1] != '/') {
                                st->path_str[ti.path_str_len] = '/';
                                ti.path_str_len++;
                        }
                } else { //path is file
                        st->data_source = DATA_SOURCE_FILE;
                }
        } else { //don't have path string - create file names from time interval
                st->data_source = DATA_SOURCE_INTERVAL;
        }

        return E_OK;
}


int send_loop(struct slave_task *st)
{
        int ret, err = E_OK, rec_len;
        lnf_mem_cursor_t *read_cursor;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records

        ret = lnf_mem_first_c(st->agg, &read_cursor);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_first_c()");
                err = E_LNF;
                goto cleanup;
        }

        /* Send all records. */
        while (true) {
                ret = lnf_mem_read_raw_c(st->agg, read_cursor, rec_buff,
                                &rec_len, LNF_MAX_RAW_LEN);
                assert(ret != LNF_EOF);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_read_raw_c()");
                        err = E_LNF;
                        goto cleanup;
                }

                MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);

                ret = lnf_mem_next_c(st->agg, &read_cursor);
                if (ret == LNF_EOF) {
                        break; //all records sent
                } else if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_next_c()");
                        err = E_LNF;
                        goto cleanup;
                }
        }

cleanup:
        /* Top-N done, notify master by empty DATA message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        return err;
}


int fast_topn_send_k(struct slave_task *st, size_t slave_cnt)
{
        int ret, err = E_OK, rec_len;
        lnf_mem_cursor_t *read_cursor;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records
        size_t sent_cnt = 0;
        uint64_t threshold;

        ret = lnf_mem_first_c(st->agg, &read_cursor);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_first_c()");
                err = E_LNF;
                goto cleanup;
        }

        /* Send first N (top-N) records. */
        while (true) {
                ret = lnf_mem_read_raw_c(st->agg, read_cursor, rec_buff,
                                &rec_len, LNF_MAX_RAW_LEN);
                assert(ret != LNF_EOF);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_read_raw_c()");
                        err = E_LNF;
                        goto cleanup;
                }

                MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);

                if (++sent_cnt == st->rec_limit) {
                        break; //Nth record sent
                }

                ret = lnf_mem_next_c(st->agg, &read_cursor);
                if (ret == LNF_EOF) {
                        goto cleanup; //all records sent
                } else if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_next_c()");
                        err = E_LNF;
                        goto cleanup;
                }
        }

        /* Compute threshold from Nth record. */
        ret = lnf_mem_read_c(st->agg, read_cursor, st->rec);
        assert(ret != LNF_EOF);
        if (ret == LNF_OK) {
                ret = lnf_rec_fget(st->rec, st->sort_key, &threshold);
                assert(ret == LNF_OK);
                threshold /= slave_cnt;
        } else {
                print_err("LNF - lnf_mem_read_c()");
                err = E_LNF;
                goto cleanup;
        }

        /* Send records until key value >= threshold (top-K records). */
        while (true) {
                uint64_t key_value;

                ret = lnf_mem_next_c(st->agg, &read_cursor);
                if (ret == LNF_EOF) {
                        break; //all records read
                } else if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_next_c()");
                        err = E_LNF;
                        goto cleanup;
                }

                ret = lnf_mem_read_c(st->agg, read_cursor, st->rec);
                assert(ret != LNF_EOF);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_read_c()");
                        err = E_LNF;
                        goto cleanup;
                }

                ret = lnf_rec_fget(st->rec, st->sort_key, &key_value);
                assert(ret == LNF_OK);
                if (key_value < threshold) {
                        break; //threshold reached
                }

                ret = lnf_mem_read_raw_c(st->agg, read_cursor, rec_buff,
                                &rec_len, LNF_MAX_RAW_LEN);
                assert(ret != LNF_EOF);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_read_raw_c()");
                        err = E_LNF;
                        goto cleanup;
                }

                MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);
                sent_cnt++; //only informative
        }

cleanup:
        /* Phase 1 done, notify master by empty DATA message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        return err;
}


int fast_topn_recv_lookup_send(struct slave_task *st)
{
        int ret, err = E_OK, rec_len;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records
        lnf_mem_cursor_t **lookup_cursor;
        size_t lookup_cursor_idx = 0;
        size_t lookup_cursor_size = 0;

        /* Allocate some lookup cursors. */
        lookup_cursor_size += LOOKUP_CURSOR_INIT_SIZE;
        lookup_cursor = malloc(lookup_cursor_size * sizeof(lnf_mem_cursor_t *));
        if (lookup_cursor == NULL) {
                print_err("malloc error");
                err = E_MEM;
                goto cleanup;
        }

        /* Receive all records. */
        while (true) {
                MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);
                if (rec_len == 0) {
                        break; //zero length -> all records received
                }

                MPI_Bcast(rec_buff, rec_len, MPI_BYTE, ROOT_PROC,
                                MPI_COMM_WORLD);

                ret = lnf_mem_lookup_raw_c(st->agg, rec_buff, rec_len,
                                &lookup_cursor[lookup_cursor_idx]);
                if (ret == LNF_EOF) {
                        continue; //record not found
                } else if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_lookup_raw_c()");
                        err = E_LNF;
                        goto cleanup;
                }
                lookup_cursor_idx++; //record found

                /* Add lookup cursors if needed. */
                if (lookup_cursor_idx == lookup_cursor_size) {
                        lnf_mem_cursor_t **tmp;

                        lookup_cursor_size *= 2; //increase size
                        tmp = realloc(lookup_cursor, lookup_cursor_size *
                                        sizeof (lnf_mem_cursor_t *));
                        if (tmp == NULL) {
                                print_err("realloc error");
                                err = E_MEM;
                                goto cleanup;
                        }
                        lookup_cursor = tmp;
                }
        }

        /* Send found records. */
        for (size_t i = 0; i < lookup_cursor_idx; ++i) {
                //TODO: optimalization - send back only relevant records
                ret = lnf_mem_read_raw_c(st->agg, lookup_cursor[i], rec_buff,
                                &rec_len, LNF_MAX_RAW_LEN);
                assert(ret != LNF_EOF);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_read_raw_c()");
                        err = E_LNF;
                        goto cleanup;
                }

                MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);
        }

cleanup:
        free(lookup_cursor);

        /* Phase 3 done, notification message is sent at the end of the task. */

        return err;
}


int slave(int world_rank, int world_size)
{
        (void)world_rank;
        int ret, err = E_OK;
        const size_t slave_cnt = world_size - 1; //all nodes without master
        struct slave_task st = {0};

        ret = task_await_new(&st);
        if (ret != E_OK) {
                err = ret;
                goto task_done_lbl;
        }

        while (task_next_file(&st) == E_OK) {
                task_process_file(&st);

                if (!st.agg && st.rec_limit &&
                                st.rec_limit == st.proc_rec_cntr) {
                        break; //record limit reached, don't read next file
                }
        }
        //TODO task_next_file() return code

        /* Reasons to disable fast top-N algorithm:
         * - user request by command line argument
         * - no record limit (all records would be exchanged anyway)
         * - sort key isn't statistical fld (flows, packets, bytes, ...)
         */
        switch (st.working_mode) {
        case MODE_REC: //all records allready sent while reading
                break;
        case MODE_ORD:
                if (st.rec_limit != 0) {
                        ret = send_loop(&st);
                        if (ret != E_OK) {
                                err = ret;
                                goto task_done_lbl;
                        }
                } //if no record limit, all records allready sent while reading
                break;
        case MODE_AGG:
                if (st.use_fast_topn) {
                        ret = fast_topn_send_k(&st, slave_cnt);
                        if (ret != E_OK) {
                                err = ret;
                                goto task_done_lbl;
                        }

                        ret = fast_topn_recv_lookup_send(&st);
                        if (ret != E_OK) {
                                err = ret;
                                goto task_done_lbl;
                        }
                } else {
                        ret = send_loop(&st);
                        if (ret != E_OK) {
                                err = ret;
                                goto task_done_lbl;
                        }
                }
                break;
        default:
                assert(!"unknown working mode");
        }

task_done_lbl:
        /* Task done, notify master by empty message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        task_free(&st);

        return err;
}
