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

#include <mpi.h>
#include <libnf.h>
#include <dirent.h>

extern MPI_Datatype task_info_mpit;

struct slave_task_t {
        working_mode_t working_mode;
        lnf_rec_t *rec;
        lnf_filter_t *filter;
        lnf_mem_t *agg;
        DIR *dir;
        char *pathname;
        size_t rec_limit, read_rec_cntr, proc_rec_cntr, slave_cnt;
};


static void isend_bytes(void *src, size_t bytes, MPI_Request *req)
{
        MPI_Wait(req, MPI_STATUS_IGNORE);
        MPI_Isend(src, bytes, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD,
                        req);
}


static void process_file(struct slave_task_t *t, const char *file_name)
{
        size_t file_rec_cntr = 0, file_filter_cntr = 0;
        int ret, err = LNF_OK;

        lnf_file_t *file;

        static lnf_brec1_t data_buff[2][XCHG_BUFF_ELEMS];
        size_t data_idx = 0;
        bool buff_idx = 0;
        MPI_Request request = MPI_REQUEST_NULL;

        /* Open flow file. */
        ret = lnf_open(&file, file_name, LNF_READ, NULL);
        if (ret != LNF_OK) {
                print_err("cannot open file %s", file_name);
                err = ret;
                return;
        }

        /* Read all records from file. */
        for (ret = lnf_read(file, t->rec); ret == LNF_OK;
                        ret = lnf_read(file, t->rec)) {
                /* Break if record limit reached. */
                /// TODO only for MODE_REC
//                if (t->rec_limit && t->proc_rec_cntr == t->rec_limit) {
//                        break;
//                }
                t->read_rec_cntr++;
                file_rec_cntr++;

                /* Apply filter (if there is any). */
                if (t->filter && !lnf_filter_match(t->filter, t->rec)) {
                        continue;
                }
                t->proc_rec_cntr++;
                file_filter_cntr++;

                /* If mode is aggreagation, write record to heap. */
                if (t->agg) {
                        ret = lnf_mem_write(t->agg, t->rec);
                        assert(ret == LNF_OK); //TODO
                        continue;
                }

                /* Pick required data from record. */
                ret = lnf_rec_fget(t->rec, LNF_FLD_BREC1, data_buff[buff_idx] +
                                data_idx++);
                assert(ret == LNF_OK); //should'n happen

                if (data_idx == XCHG_BUFF_ELEMS) {
                        isend_bytes(data_buff[buff_idx], XCHG_BUFF_SIZE,
                                        &request);
                        data_idx = 0;
                        buff_idx = !buff_idx;
                }
        }
        err = (ret == LNF_EOF) ? err : ret; //if error during lnf_red ()

        /* Send remaining records. */
        if (data_idx) {
                isend_bytes(data_buff[buff_idx], data_idx * sizeof(lnf_brec1_t),
                                &request);
        }

        /* Per file cleanup. */
        lnf_close(file);

        print_debug("slave: file %s\tread %lu\tmatched %lu\n",
                        file_name, file_rec_cntr, file_filter_cntr);
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

        t->working_mode = ti.working_mode;
        t->rec_limit = ti.rec_limit;
        t->slave_cnt = ti.slave_cnt;

        if (ti.filter_str_len > 0) { //have filter epxression
                char filter_str[ti.filter_str_len + 1];

                MPI_Bcast(filter_str, ti.filter_str_len, MPI_CHAR, ROOT_PROC,
                                MPI_COMM_WORLD);

                filter_str[ti.filter_str_len] = '\0';
                task_init_filter(&t->filter, filter_str);
        }

        if (ti.path_str_len > 0) { //have path string
                t->pathname = calloc(ti.path_str_len + 1, sizeof(char));
                if (t->pathname == NULL) {
                        print_err("calloc error");
                        return E_MEM;
                }

                MPI_Bcast(t->pathname, ti.path_str_len, MPI_CHAR, ROOT_PROC,
                                MPI_COMM_WORLD);
        }


        switch (t->working_mode) {
        case MODE_REC:
                break;
        case MODE_AGG:
                ret = agg_init(&t->agg, ti.agg_params, ti.agg_params_cnt);
                break;
        default:
                assert(!"unknown working mode received");
        }

        /* Filter. */

        /* Initialize empty LNF record to future reading. */
        ret = lnf_rec_init(&t->rec);
        if (ret != LNF_OK) {
                print_err("cannot initialise empty record object");
                t->rec = NULL;
        }

        if ((t->dir = opendir(t->pathname)) == NULL) {
                print_err("cannot open directory \"%s\"", t->pathname);
                return E_INTERNAL;
        }

        return E_OK;
}

const char* task_next_file(struct slave_task_t *t)
{
        struct dirent *ent;
        static char path[1000]; //TODO

        do {
                ent = readdir(t->dir);
        } while (ent && ent->d_type != DT_REG);

        if (ent) {
                strcpy(path, t->pathname);
                return strcat(path, ent->d_name);
        }

        return NULL;
}

static void task_free(struct slave_task_t *t)
{
        free(t->pathname);
        closedir(t->dir);
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
        task_await_new(&task);

        for (const char *fn = task_next_file(&task); fn != NULL;
                        fn = task_next_file(&task)) {
                process_file(&task, fn);

                /* Break if record limit reached. */
                if (task.rec_limit && task.proc_rec_cntr == task.rec_limit) {
                        break;
                }
        }

        if (task.agg) {
                char buff[LNF_MAX_RAW_LEN];
                //char *data_buff[2][];
                int len;
                MPI_Request req = MPI_REQUEST_NULL;
                lnf_mem_cursor_t **topn_cursors;


                for (ret = lnf_mem_read_raw(task.agg, buff, &len,
                                        LNF_MAX_RAW_LEN); ret == LNF_OK;
                                ret = lnf_mem_read_raw(task.agg, buff, &len,
                                        LNF_MAX_RAW_LEN)) {
                        isend_bytes(buff, len, &req);
                        MPI_Wait(&req, MPI_STATUS_IGNORE); //TODO: buffer switching
                }

                /* first iteration done, notify master by empty TASK message. */
                MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA,
                         MPI_COMM_WORLD);

                /* second iteration of top-N algorithm */
                topn_cursors = (lnf_mem_cursor_t **) malloc (task.rec_limit *
                        sizeof (lnf_mem_cursor_t *));
                if (topn_cursors == NULL) {
                        print_err("malloc error");
                        return E_MEM; //fatal
                }

                ret = recv_topn_ids(task.rec_limit, task.agg, topn_cursors);
                if (ret != E_OK) {
                        return ret;
                }

                for (size_t i = 0; i < task.rec_limit; ++i) {

                        lnf_mem_read_raw_c(task.agg, &topn_cursors[i], buff,
                                &len, LNF_MAX_RAW_LEN);
                        isend_bytes(buff, len, &req);
                        MPI_Wait(&req, MPI_STATUS_IGNORE); //TODO: buffer switching

                }

                /* first iteration done, notify master by empty TASK message. */
                MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA,
                         MPI_COMM_WORLD);

        }

        /* Task done, notify master by empty TASK message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);


        task_free(&task);

        return E_OK;
}
