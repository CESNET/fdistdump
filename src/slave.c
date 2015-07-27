/**
 * \file slave.c
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
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

#include <mpi.h>
#include <libnf.h>
#include <dirent.h> //list directory
#include <sys/stat.h>

extern MPI_Datatype task_info_mpit;

typedef enum {
        DATA_SOURCE_FILE,
        DATA_SOURCE_DIR,
        DATA_SOURCE_INTERVAL,
} data_source_t;

struct slave_task_t {
        working_mode_t working_mode;
        lnf_rec_t *rec;
        lnf_filter_t *filter;
        lnf_mem_t *agg;
        size_t rec_limit; //record limit
        size_t proc_rec_cntr; //processed record counter
        size_t slave_cnt; //slave count

        data_source_t data_source; //how flow files are obtained
        char path_str[PATH_MAX];
        DIR *dir_ctx; //used in case of directory as data source
        struct tm interval_begin; //begin of time interval
        struct tm interval_end; //end of time interval

        char cur_file_path[PATH_MAX]; //current flow file absolute path

        int sort_key; //LNF field set as key for sorting in memory
};


static void isend_bytes(void *src, size_t bytes, MPI_Request *req)
{
        MPI_Wait(req, MPI_STATUS_IGNORE);
        MPI_Isend(src, bytes, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD,
                        req);
}


static void task_process_file(struct slave_task_t *t)
{
        size_t file_rec_cntr = 0, file_proc_rec_cntr = 0;
        int ret, err = LNF_OK;

        lnf_file_t *file;

        static lnf_brec1_t data_buff[2][XCHG_BUFF_ELEMS];
        size_t data_idx = 0;
        bool buff_idx = 0;
        MPI_Request request = MPI_REQUEST_NULL;

        /* Open flow file. */
        ret = lnf_open(&file, t->cur_file_path, LNF_READ, NULL);
        if (ret != LNF_OK) {
                print_err("cannot open file %s", t->cur_file_path);
                err = ret;
                return;
        }

        /* Read all records from file. */
        for (ret = lnf_read(file, t->rec); ret == LNF_OK;
                        ret = lnf_read(file, t->rec)) {

                file_rec_cntr++;

                /* Apply filter (if there is any). */
                if (t->filter && !lnf_filter_match(t->filter, t->rec)) {
                        continue;
                }
                t->proc_rec_cntr++;
                file_proc_rec_cntr++;

                /* Aggreagation -> write record to mem (ignore record limit). */
                if (t->agg) {
                        ret = lnf_mem_write(t->agg, t->rec);
                        assert(ret == LNF_OK); //TODO: return value
                        continue;
                }

                /* No aggregation -> store to buffer and send if buffer full. */
                ret = lnf_rec_fget(t->rec, LNF_FLD_BREC1, data_buff[buff_idx] +
                                data_idx++);
                assert(ret == LNF_OK); //should'n happen

                if (data_idx == XCHG_BUFF_ELEMS) { //buffer full -> send
                        isend_bytes(data_buff[buff_idx], XCHG_BUFF_SIZE,
                                        &request);
                        data_idx = 0;
                        buff_idx = !buff_idx; //toggle buffers
                }

                if (t->proc_rec_cntr == t->rec_limit) {
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
                        t->cur_file_path, file_rec_cntr, file_proc_rec_cntr);
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


static int task_await_new(struct slave_task_t *t)
{
        int ret;
        task_info_t ti;

        MPI_Bcast(&ti, 1, task_info_mpit, ROOT_PROC, MPI_COMM_WORLD);

        if (ti.filter_str_len > 0) { //have filter epxression
                char filter_str[ti.filter_str_len + 1];

                MPI_Bcast(filter_str, ti.filter_str_len, MPI_CHAR, ROOT_PROC,
                                MPI_COMM_WORLD);

                filter_str[ti.filter_str_len] = '\0';
                task_init_filter(&t->filter, filter_str);
        }

        if (ti.path_str_len > 0) { //have path string
                //PATH_MAX length check on master side
                MPI_Bcast(t->path_str, ti.path_str_len, MPI_CHAR, ROOT_PROC,
                                MPI_COMM_WORLD);
                t->path_str[ti.path_str_len] = '\0';
        }

        t->working_mode = ti.working_mode;
        t->rec_limit = ti.rec_limit;
        t->slave_cnt = ti.slave_cnt;
        t->interval_begin = ti.interval_begin;
        t->interval_end = ti.interval_end;

        switch (t->working_mode) {
        case MODE_REC:
                break;
        case MODE_AGG:
                ret = agg_init(&t->agg, ti.agg_params, ti.agg_params_cnt);

                /* Find and store sort key. */
                for (size_t i = 0; i < ti.agg_params_cnt; ++i) {
                        if (ti.agg_params[i].flags & LNF_SORT_FLAGS) {
                                t->sort_key = ti.agg_params[i].field;
                        }
                }
                break;
        default:
                assert(!"unknown working mode received");
        }

        /* Initialize empty LNF record to future reading. */
        ret = lnf_rec_init(&t->rec);
        if (ret != LNF_OK) {
                print_err("cannot initialise empty record object");
                t->rec = NULL;
        }

        if (ti.path_str_len > 0) { //have path string (file or directory)
                struct stat stat_buff;

                ret = stat(t->path_str, &stat_buff);
                if (ret == -1) {
                        print_err("%s \"%s\"", strerror(errno), t->path_str);
                        return E_ARG;
                }

                if (S_ISDIR(stat_buff.st_mode)) { //path is directory
                        t->data_source = DATA_SOURCE_DIR;

                        t->dir_ctx = opendir(t->path_str);
                        if (t->dir_ctx == NULL) {
                                print_err("cannot open directory \"%s\"",
                                                t->path_str);
                                return E_ARG;
                        }
                        if (ti.path_str_len >= (PATH_MAX - NAME_MAX)) {
                                errno = ENAMETOOLONG;
                                print_err("%s \"%s\"", strerror(errno),
                                                t->path_str);
                                return E_ARG;
                        }

                        //check and add missing terminating slash
                        if (t->path_str[ti.path_str_len - 1] != '/') {
                                t->path_str[ti.path_str_len] = '/';
                                ti.path_str_len++;
                        }
                } else { //path is file
                        t->data_source = DATA_SOURCE_FILE;
                }
        } else { //don't have path string - create file names from time interval
                t->data_source = DATA_SOURCE_INTERVAL;
        }

        return E_OK;
}

int task_next_file(struct slave_task_t *t)
{
        struct dirent *dir_entry;
        int ret;

        switch (t->data_source) {
        case DATA_SOURCE_FILE:
                if (strlen(t->cur_file_path) == 0) {
                        strcpy(t->cur_file_path, t->path_str);
                } else {
                        return E_EOF;
                }
                break;

        case DATA_SOURCE_DIR:
                do { //in directory skip everything but regular files
                        dir_entry = readdir(t->dir_ctx);
                } while (dir_entry && dir_entry->d_type != DT_REG);

                if (dir_entry != NULL) { //found regular file
                        strcpy(t->cur_file_path, t->path_str); //copy dirname
                        strcat(t->cur_file_path, dir_entry->d_name); //+filename
                } else { //didn't find next regular file
                        return E_EOF;
                }
                break;

        case DATA_SOURCE_INTERVAL:
                /* Loop through entire interval, modify interval_begin. */
                while (diff_tm(t->interval_end, t->interval_begin) > 0.0) {
                        //TODO: meaningfull path check and error report
                        strftime(t->cur_file_path, sizeof(t->cur_file_path),
                                        FLOW_FILE_PATH, &t->interval_begin);
                        t->interval_begin.tm_sec += FLOW_FILE_ROTATION_INTERVAL;
                        mktime(&t->interval_begin); //struct tm normalization

                        ret = access(t->cur_file_path, F_OK);
                        if (ret == 0) {
                                return E_OK; //file exists
                        }

                        printf("warning: skipping nonexistent file \"%s\"\n",
                                        t->cur_file_path);
                }
                return E_EOF; //whole interval

        default:
                assert(!"unknown data source");
        }

        return E_OK;
}

static void task_free(struct slave_task_t *t)
{
        closedir(t->dir_ctx);
        if (t->rec) {
                lnf_rec_free(t->rec);
        }
        if (t->filter) {
                lnf_filter_free(t->filter);
        }
        if (t->agg) {
                lnf_mem_free(t->agg);
        }
}


/* Receive first top-n identifiers (aggregation field(s) values) from master */
int recv_topn_ids(size_t n, lnf_mem_t *mem, lnf_mem_cursor_t **cursors)
{
        char buff[LNF_MAX_RAW_LEN];
        int len;
        int ret;

        for (size_t i = 0; i < n; ++i){

                MPI_Bcast(&len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);
                MPI_Bcast(&buff, len, MPI_BYTE, ROOT_PROC, MPI_COMM_WORLD);

                ret = lnf_mem_lookup_raw_c(mem, buff, len, &cursors[i]);

                printf("MOJE: Received %d bytes.\n", len);

                if (ret != LNF_OK) {
                        if (ret == LNF_EOF) {
                                print_debug("Record not found.");
                                cursors[i] = NULL;
                                continue;
                        } else {
                                print_err("LNF Lookup error.");
                                return E_LNF;
                        }
                }
        }

//        MPI_Bcast(NULL, 0, MPI_BYTE, ROOT_PROC, MPI_COMM_WORLD);

        return E_OK;
}


int slave(int world_rank, int world_size)
{
        (void)world_rank; (void)world_size;
        int ret, err = LNF_OK;

        struct slave_task_t task = { 0 };

        ret = task_await_new(&task);
        if (ret != E_OK) {
                err = ret;
                goto task_done_lbl;
        }

        while (task_next_file(&task) == E_OK) {
                task_process_file(&task);

                if (task.rec_limit && task.rec_limit == task.proc_rec_cntr) {
                        break; //record limit reached, don't read next file
                }
        }
        //TODO task_next_file() return code

        if (task.agg) {
                char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records
                int rec_len;
                size_t send_cnt = 0;
                lnf_mem_cursor_t *read_cursor;
                lnf_mem_cursor_t **topn_cursors;


                ret = lnf_mem_first_c(task.agg, &read_cursor);
                if (ret != LNF_OK) {
                        print_err("LNF - cannot initialise cursor");
                        return E_LNF;
                }

                /*
                 * Top-N phase 1.
                 */
                /* Send first N records. */
                while (true) {
                        ret = lnf_mem_read_raw_c(task.agg, read_cursor,
                                        rec_buff, &rec_len, LNF_MAX_RAW_LEN);
                        if (ret != LNF_OK) {
                                print_err("LNF - lnf_mem_read_raw_c()");
                                return E_LNF;
                        }

                        MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC,
                                        TAG_DATA, MPI_COMM_WORLD);

                        if (++send_cnt == task.rec_limit) {
                                break; //Nth record sent
                        }

                        ret = lnf_mem_next_c(task.agg, &read_cursor);
                        if (ret == LNF_EOF) {
                                break; //we run out of records
                        } else if (ret != LNF_OK) {
                                print_err("LNF - cannot move cursor");
                                return E_LNF;
                        }
                }
                /* Compute treshold from Nth record. */
                //TODO
                /* Send N+1 to treshold records. */
                //TODO
                /* Phase 1 done, notify master by empty TASK message. */
                MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);

                /*
                 * Top-N phase 3.
                 */
                topn_cursors = (lnf_mem_cursor_t **) malloc (task.rec_limit *
                        sizeof (lnf_mem_cursor_t *));
                if (topn_cursors == NULL) {
                        print_err("malloc error");
                        return E_MEM;
                }

                ret = recv_topn_ids(task.rec_limit, task.agg, topn_cursors);
                if (ret != E_OK) {
                        return ret;
                }

                for (size_t i = 0; i < task.rec_limit; ++i) {
                        lnf_mem_read_raw_c(task.agg, &topn_cursors[i], rec_buff,
                                &rec_len, LNF_MAX_RAW_LEN);
                        MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC,
                                        TAG_DATA, MPI_COMM_WORLD);
                }

                /* Phase 3 done, notify master by empty TASK message. */
                MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA,
                         MPI_COMM_WORLD);
        }

task_done_lbl:
        /* Task done, notify master by empty message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        task_free(&task);

        return err;
}
