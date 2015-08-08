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
#include <sys/stat.h> //stat()


#define LOOKUP_CURSOR_INIT_SIZE 1024

/* Global variables. */
extern int secondary_errno;

extern MPI_Datatype mpi_struct_shared_task_ctx;

typedef enum {
        DATA_SOURCE_FILE,
        DATA_SOURCE_DIR,
        DATA_SOURCE_INTERVAL,
} data_source_t;

struct slave_task_ctx {
        /* Shared task context. */
        struct shared_task_ctx shared; //will be received

        /* Slave specific task context. */
        lnf_rec_t *rec; //LNF record (to reduce usage of rec_init and rec_free)
        lnf_filter_t *filter; //LNF compiled filter expression
        lnf_mem_t *agg_mem; //LNF memory

        data_source_t data_source; //how flow files are obtained
        char path_str[PATH_MAX]; //file of directory path string
        DIR *dir_ctx; //used in case of directory as data source
        char cur_file_path[PATH_MAX]; //currently processed file absolute path
        bool no_more_files; //true if there is no more files to read

        size_t proc_rec_cntr; //processed record counter
        bool rec_limit_reached; //true if rec_limit records read

        size_t slave_cnt; //slave count
};


static void isend_bytes(void *src, size_t bytes, MPI_Request *req)
{
        MPI_Wait(req, MPI_STATUS_IGNORE);
        MPI_Isend(src, bytes, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD,
                        req);
}


static error_code_t task_process_file(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;
        size_t file_rec_cntr = 0;
        size_t file_proc_rec_cntr = 0;
        static lnf_brec1_t data_buff[2][XCHG_BUFF_ELEMS];
        size_t data_idx = 0;
        bool buff_idx = 0;
        MPI_Request request = MPI_REQUEST_NULL;
        lnf_file_t *file;

        /* Open flow file. */
        secondary_errno = lnf_open(&file, stc->cur_file_path, LNF_READ, NULL);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "unable to open file \"%s\"",
                                stc->cur_file_path);
                return E_LNF;
        }

        /* Read all records from file. */
        while ((secondary_errno = lnf_read(file, stc->rec)) == LNF_OK) {
                file_rec_cntr++;

                /* Apply filter (if there is any). */
                if (stc->filter && !lnf_filter_match(stc->filter, stc->rec)) {
                        continue;
                }
                stc->proc_rec_cntr++;
                file_proc_rec_cntr++;

                /*
                 * Aggreagation -> write record to memory and continue.
                 * Ignore record limit.
                 */
                if (stc->agg_mem) {
                        secondary_errno = lnf_mem_write(stc->agg_mem, stc->rec);
                        if (secondary_errno != LNF_OK) {
                                primary_errno = E_LNF;
                                print_err(primary_errno, secondary_errno,
                                                "lnf_mem_write()");
                                goto close_file;
                        }
                        continue;
                }

                /*
                 * No aggregation -> store record to buffer.
                 * Send buffer, if buffer full, otherwise continue reading.
                 */
                secondary_errno = lnf_rec_fget(stc->rec, LNF_FLD_BREC1,
                                data_buff[buff_idx] + data_idx++);
                assert(secondary_errno == LNF_OK);

                if (data_idx == XCHG_BUFF_ELEMS) { //buffer full
                        isend_bytes(data_buff[buff_idx], XCHG_BUFF_SIZE,
                                        &request);
                        data_idx = 0;
                        buff_idx = !buff_idx; //toggle buffers
                }

                if (stc->proc_rec_cntr == stc->shared.rec_limit) {
                        stc->rec_limit_reached = true;
                        break; //record limit reached
                }
        }

        /* Check if we reach end of file. */
        if (!stc->rec_limit_reached && secondary_errno != LNF_EOF) {
                primary_errno = E_LNF; //no, we didn't
        }

        /* Send remaining records (if there are any). */
        if (data_idx != 0) {
                isend_bytes(data_buff[buff_idx], data_idx * sizeof(lnf_brec1_t),
                                &request);
        }

close_file:
        lnf_close(file);

        print_debug("file %s: read %lu, processed %lu\n", stc->cur_file_path,
                        file_rec_cntr, file_proc_rec_cntr);

        return E_OK;
}


static error_code_t task_set_next_file(struct slave_task_ctx *stc)
{
        struct dirent *dir_entry;

        assert(stc != NULL);

        if (stc->no_more_files) {
                return E_EOF;
        }

        switch (stc->data_source) {
        case DATA_SOURCE_FILE: //one file
                strcpy(stc->cur_file_path, stc->path_str);
                stc->no_more_files = true;
                return E_OK;

        case DATA_SOURCE_DIR: //whole directory without descending
                do { //skip all directories
                        dir_entry = readdir(stc->dir_ctx);
                } while (dir_entry && dir_entry->d_type == DT_DIR);

                if (dir_entry == NULL) { //didn't find any new file
                        stc->no_more_files = true;
                        return E_EOF;
                }

                /* Found new file -> construct path. */
                strcpy(stc->cur_file_path, stc->path_str); //insert dirname
                strcat(stc->cur_file_path, dir_entry->d_name); //append filename
                return E_OK;

        case DATA_SOURCE_INTERVAL: //all files coresponding to time interval
                /* Loop through entire interval, ctx kept in interval_begin. */
                while (diff_tm(stc->shared.interval_end,
                                        stc->shared.interval_begin) > 0.0) {
                        /* Construct path string from time. */
                        if (strftime(stc->cur_file_path,
                                                sizeof(stc->cur_file_path),
                                                FLOW_FILE_PATH,
                                                &stc->shared.interval_begin)
                                        == 0) {
                                secondary_errno = 0;
                                print_err(E_PATH, secondary_errno,
                                                "strftime()");
                                return E_PATH;
                        }
                        /* Increment context by rotation interval. */
                        stc->shared.interval_begin.tm_sec +=
                                FLOW_FILE_ROTATION_INTERVAL;
                        mktime(&stc->shared.interval_begin); //normalization

                        if (access(stc->cur_file_path, F_OK) == 0) {
                                return E_OK; //file exists
                        }

                        print_warn(E_PATH, 0, "skipping non existing file "
                                        "\"%s\"\n", stc->cur_file_path);
                }

                stc->no_more_files = true;
                return E_EOF; //whole interval read

        default:
                assert(!"unknown data source");
        }

        assert(!"task_set_next_file()");
}


static void task_free(struct slave_task_ctx *stc)
{
        if (stc->dir_ctx != NULL) {
                closedir(stc->dir_ctx);
        }
        if (stc->rec != NULL) {
                lnf_rec_free(stc->rec);
        }
        if (stc->filter != NULL) {
                lnf_filter_free(stc->filter);
        }
        if (stc->agg_mem != NULL) {
                lnf_mem_free(stc->agg_mem);
        }
}


static error_code_t task_init_filter(lnf_filter_t **filter, char *filter_str)
{
        assert(filter != NULL && filter_str != NULL && strlen(filter_str) != 0);

        /* Initialize filter. */
        secondary_errno = lnf_filter_init(filter, filter_str);
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

                filter_str[stc->shared.filter_str_len] = '\0'; //add termination
                primary_errno = task_init_filter(&stc->filter, filter_str);
                //it is OK not to chech primary_errno
        }

        /* If have path string, receive path string. */
        if (stc->shared.path_str_len > 0) {
                //PATH_MAX length already checked on master side
                MPI_Bcast(stc->path_str, stc->shared.path_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);
                stc->path_str[stc->shared.path_str_len] = '\0';//add termination
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

                /* Sort all records localy, then send fist rec_limit records. */
                secondary_errno = lnf_mem_init(&stc->agg_mem);
                if (secondary_errno != LNF_OK) {
                        print_err(E_LNF, secondary_errno, "lnf_mem_init()");
                        stc->agg_mem = NULL;
                        return E_LNF;
                }

                secondary_errno = lnf_mem_setopt(stc->agg_mem, LNF_OPT_LISTMODE,
                                NULL, 0);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_setopt()");
                        goto free_lnf_mem;
                }

                primary_errno = mem_setup(stc->agg_mem, stc->shared.agg_params,
                                stc->shared.agg_params_cnt);
                if (primary_errno != E_OK) {
                        goto free_lnf_mem;
                }
                return E_OK;

        case MODE_AGGR:
                secondary_errno = lnf_mem_init(&stc->agg_mem);
                if (secondary_errno != LNF_OK) {
                        print_err(E_LNF, secondary_errno, "lnf_mem_init()");
                        stc->agg_mem = NULL;
                        return E_LNF;
                }

                primary_errno = mem_setup(stc->agg_mem, stc->shared.agg_params,
                                stc->shared.agg_params_cnt);
                if (primary_errno != E_OK) {
                        goto free_lnf_mem;
                }

                return E_OK;

        default:
                assert(!"unknown working mode");
        }

free_lnf_mem:
        lnf_mem_free(stc->agg_mem);
        stc->agg_mem = NULL;

        return primary_errno;
}


static error_code_t task_init_data_source(struct slave_task_ctx *stc)
{
        int err;
        struct stat stat_buff;

        /* Don't have path string - construct file names from time interval. */
        if (stc->shared.path_str_len == 0) {
                stc->data_source = DATA_SOURCE_INTERVAL;
                return E_OK;
        }

        /* Have path string, don't know if file or direcory yet. */
        err = stat(stc->path_str, &stat_buff);
        if (err == -1) { //path doesn't exist, permissions, ...
                secondary_errno = errno;
                print_err(E_PATH, secondary_errno, "%s \"%s\"", strerror(errno),
                                stc->path_str);
                return E_PATH;
        }

        if (!S_ISDIR(stat_buff.st_mode)) { //path isn't directory
                stc->data_source = DATA_SOURCE_FILE;
                return E_OK;
        }

        /* Now we know that path points to directory. */
        if (stc->shared.path_str_len >= (PATH_MAX - NAME_MAX) - 1) {
                secondary_errno = ENAMETOOLONG;
                print_err(E_PATH, secondary_errno, "%s \"%s\"",
                                strerror(secondary_errno), stc->path_str);
                return E_PATH;
        }

        stc->data_source = DATA_SOURCE_DIR;
        stc->dir_ctx = opendir(stc->path_str);
        if (stc->dir_ctx == NULL) { //cannot access directory
                secondary_errno = errno;
                print_err(E_PATH, secondary_errno, "%s \"%s\"", strerror(errno),
                                stc->path_str);
                return E_PATH;
        }

        /* Check/add missing terminating slash. One byte should be available. */
        if (stc->path_str[stc->shared.path_str_len - 1] != '/') {
                stc->path_str[stc->shared.path_str_len++] = '/';
        }

        return E_OK;
}


static error_code_t send_loop(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;
        int rec_len;
        lnf_mem_cursor_t *read_cursor;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records

        secondary_errno = lnf_mem_first_c(stc->agg_mem, &read_cursor);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_mem_first_c()");
                goto send_terminator;
        }

        /* Send all records. */
        while (true) {
                secondary_errno = lnf_mem_read_raw_c(stc->agg_mem, read_cursor,
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

                secondary_errno = lnf_mem_next_c(stc->agg_mem, &read_cursor);
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
        int sort_key = LNF_SORT_NONE;
        lnf_mem_cursor_t *read_cursor;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records
        uint64_t threshold;

        /* Send first rec_limit (top-N) records. */
        for (size_t i = 0; i < stc->shared.rec_limit; ++i) {
                if (i == 0) {
                        secondary_errno = lnf_mem_first_c(stc->agg_mem,
                                        &read_cursor);
                } else {
                        secondary_errno = lnf_mem_next_c(stc->agg_mem,
                                        &read_cursor);
                        if (secondary_errno == LNF_EOF) {
                                break; //all records in memory successfully sent
                        }
                }
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_first_c or lnf_mem_next_c()");
                        goto send_terminator;
                }

                secondary_errno = lnf_mem_read_raw_c(stc->agg_mem, read_cursor,
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
        }

        /* Find sort key in aggregation parameters. */
        for (size_t i = 0; i < stc->shared.agg_params_cnt; ++i) {
                if (stc->shared.agg_params[i].flags & LNF_SORT_FLAGS) {
                        sort_key = stc->shared.agg_params[i].field;
                        break;
                }
        }

        /* Compute threshold from sort key of Nth record. */
        secondary_errno = lnf_mem_read_c(stc->agg_mem, read_cursor, stc->rec);
        assert(secondary_errno != LNF_EOF);
        if (secondary_errno == LNF_OK) {
                secondary_errno = lnf_rec_fget(stc->rec, sort_key, &threshold);
                assert(secondary_errno == LNF_OK);
                threshold /= stc->slave_cnt;
        } else {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_mem_read_c()");
                goto send_terminator;
        }

        /* Send records until key value >= threshold (top-K records). */
        while (true) {
                uint64_t key_value;

                secondary_errno = lnf_mem_next_c(stc->agg_mem, &read_cursor);
                if (secondary_errno == LNF_EOF) {
                        break; //all records in memory successfully sent
                } else if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_next_c()");
                        goto send_terminator;
                }

                secondary_errno = lnf_mem_read_c(stc->agg_mem, read_cursor,
                                stc->rec);
                assert(secondary_errno != LNF_EOF);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_read_c()");
                        goto send_terminator;
                }

                secondary_errno = lnf_rec_fget(stc->rec, sort_key, &key_value);
                assert(secondary_errno == LNF_OK);
                if (key_value < threshold) {
                        break; //threshold reached
                }

                secondary_errno = lnf_mem_read_raw_c(stc->agg_mem, read_cursor,
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
        }

send_terminator:
        /* Phase 1 done, notify master by empty DATA message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

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

        /* Allocate some lookup cursors. */
        lookup_cursors = malloc(lookup_cursors_size *
                        sizeof(lnf_mem_cursor_t *));
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

                secondary_errno = lnf_mem_lookup_raw_c(stc->agg_mem, rec_buff,
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
                secondary_errno = lnf_mem_read_raw_c(stc->agg_mem,
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

        /* Phase 3 done, notification message is sent at the end of the task. */
        return primary_errno;
}


static error_code_t task_process_mem(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;

        /* Reasons to disable fast top-N algorithm:
         * - user request by command line argument
         * - no record limit (all records would be exchanged anyway)
         * - sort key isn't statistical fld (flows, packets, bytes, ...)
         */
        switch (stc->shared.working_mode) {
        case MODE_LIST:
                return E_OK; //all records already sent while reading

        case MODE_SORT:
                if (stc->shared.rec_limit == 0) {
                        return E_OK; //all records already sent while reading
                }

                return send_loop(stc);

        case MODE_AGGR:
                if (stc->shared.use_fast_topn) {
                        primary_errno = fast_topn_send_loop(stc);
                        if (primary_errno != E_OK) {
                                return primary_errno;
                        }

                        return fast_topn_recv_lookup_send(stc);
                } else {
                        return send_loop(stc);
                }

        default:
                assert(!"unknown working mode");
        }

        assert(!"task_process_mem()");
}


error_code_t slave(int world_size)
{
        error_code_t primary_errno = E_OK;
        struct slave_task_ctx stc;
        memset(&stc, 0, sizeof(stc));

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
        primary_errno = task_init_data_source(&stc);
        if (primary_errno != E_OK) {
                goto finalize_task;
        }
        /* Initialize LNF record to reduce usage of rec_init and rec_free. */
        secondary_errno = lnf_rec_init(&stc.rec);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_rec_init()");
                stc.rec = NULL;
                goto finalize_task;
        }

        /* Loop through all the files, process them. */
        while ((primary_errno = task_set_next_file(&stc)) == E_OK) {
                primary_errno = task_process_file(&stc);
                if (primary_errno != E_OK) {
                        goto finalize_task;
                }

                if (stc.rec_limit_reached) {
                        break; //don't read next file
                }
        }

        /* Check if we read all files. */
        if (!stc.rec_limit_reached && primary_errno != E_EOF) {
                goto finalize_task; //no, we didn't
        }

        /*
         * In case of aggregation or sorting, records were stored into memory.
         * Now we need to process and send there records to master.
         */
        primary_errno = task_process_mem(&stc);

finalize_task:
        /* Task done, notify master by empty message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        task_free(&stc);
        return primary_errno;
}
