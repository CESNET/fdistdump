/**
 * \file master.c
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

#include "master.h"
#include "common.h"

#include <string.h> //strlen()
#include <stdbool.h>
#include <assert.h>

#include <mpi.h>
#include <libnf.h>


/* Global MPI data types. */
extern MPI_Datatype task_info_mpit;


static void fill_task_info(task_info_t *ti, const struct cmdline_args *args,
                size_t slave_cnt)
{
        ti->working_mode = args->working_mode;
        ti->agg_params_cnt = args->agg_params_cnt;

        memcpy(ti->agg_params, args->agg_params, args->agg_params_cnt *
                        sizeof(struct agg_params));

        ti->rec_limit = args->rec_limit;

        if (args->filter_str == NULL) {
                ti->filter_str_len = 0;
        } else {
                ti->filter_str_len = strlen(args->filter_str);
        }
        if (args->path_str == NULL) {
                ti->path_str_len = 0;
        } else {
                ti->path_str_len = strlen(args->path_str);
        }
        ti->slave_cnt = slave_cnt;

        ti->interval_begin = args->interval_begin;
        ti->interval_end = args->interval_end;

        ti->use_fast_topn = args->use_fast_topn;
}


static int fast_topn_bcast_all(lnf_mem_t *mem)
{
        int ret, rec_len;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records
        lnf_mem_cursor_t *read_cursor;

        ret = lnf_mem_first_c(mem, &read_cursor);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_first_c()");
                return E_LNF;
        }

        /* Broadcast all records. */
        while (true) {
                ret = lnf_mem_read_raw_c(mem, read_cursor, rec_buff, &rec_len,
                                LNF_MAX_RAW_LEN);
                assert(ret != LNF_EOF);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_read_raw_c()");
                        return E_LNF;
                }

                MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);
                MPI_Bcast(rec_buff, rec_len, MPI_BYTE, ROOT_PROC,
                                MPI_COMM_WORLD);

                ret = lnf_mem_next_c(mem, &read_cursor);
                if (ret == LNF_EOF) {
                        break; //all records sent
                } else if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_next_c()");
                        return E_LNF;
                }
        }

        /* Phase 2 done, notify slaves by zero record length. */
        rec_len = 0;
        MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);

        return E_OK;
}


struct mem_insert_callback_data {
        lnf_mem_t *mem;
        lnf_rec_t *rec;
};

typedef int (*recv_callback_t)(char *data, size_t data_len, void *user);

static int mem_write_callback(char *data, size_t data_len, void *user)
{
        (void)data_len;
        int ret;
        struct mem_insert_callback_data *micd =
                (struct mem_insert_callback_data *)user;

        ret = lnf_rec_fset(micd->rec, LNF_FLD_BREC1, data);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_rec_fset()");
                return E_LNF;
        }
        ret = lnf_mem_write(micd->mem, micd->rec);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_write()");
                return E_LNF;
        }

        return E_OK;
}

static int mem_write_raw_callback(char *data, size_t data_len, void *user)
{
        int ret;

        ret = lnf_mem_write_raw((lnf_mem_t *)user, data, data_len);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_write_raw() %X", ret);
                return E_LNF;
        }

        return E_OK;
}

static int print_brec_callback(char *data, size_t data_len, void *user)
{
        (void)data_len;
        (void)user;

        return print_brec((const lnf_brec1_t *)data);
}


static int recv_loop(size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user)
{
        int ret, err = E_OK, rec_len;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: receive mutliple records
        MPI_Status status;
        size_t rec_cntr = 0, active_slaves = slave_cnt; //every slave is active
        bool limit_exceeded = false;

        /* Data receiving loop. */
        while (active_slaves) {
                /* Receive message from any slave. */
                MPI_Recv(rec_buff, LNF_MAX_RAW_LEN, MPI_BYTE, MPI_ANY_SOURCE,
                                TAG_DATA, MPI_COMM_WORLD, &status);

                /* Determine actual size of the received message. */
                MPI_Get_count(&status, MPI_BYTE, &rec_len);
                if (rec_len == 0) {
                        active_slaves--;
                        continue; //empty message -> slave finished
                }

                if (limit_exceeded) {
                        continue; //do not process but continue receiving
                }

                /* Call callback function for each received record. */
                ret = recv_callback(rec_buff, rec_len, user);
                if (ret != E_OK) {
                        err = ret;
                        break; //don't receive next message
                }

                if (++rec_cntr == rec_limit) {
                        limit_exceeded = true;
                }
        }

        return err;
}

static int irecv_loop(size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user)
{
        int ret, err = E_OK;
        char *data_buff[2][slave_cnt]; //two buffers per slave
        bool data_buff_idx[slave_cnt]; //current buffer index
        MPI_Request requests[slave_cnt];
        MPI_Status status;
        size_t rec_cntr = 0; //processed records
        bool limit_exceeded = false;

        /* Allocate receive buffers. */
        for (size_t i = 0; i < slave_cnt; ++i) {
                requests[i] = MPI_REQUEST_NULL;
                data_buff_idx[i] = 0;

                data_buff[0][i] = NULL; //in case of malloc failure
                data_buff[1][i] = NULL;

                data_buff[0][i] = malloc(XCHG_BUFF_SIZE);
                data_buff[1][i] = malloc(XCHG_BUFF_SIZE);
                if (data_buff[0][i] == NULL || data_buff[1][i] == NULL) {
                        print_err("malloc error");
                        err = E_MEM;
                        goto cleanup;
                }
        }

        /* Start first individual nonblocking data receive from every slave. */
        for (size_t i = 0; i < slave_cnt ; ++i) {
                char *free_buff = data_buff[data_buff_idx[i]][i]; //shortcut

                MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE, i + 1, TAG_DATA,
                                MPI_COMM_WORLD, &requests[i]);
        }

        /* Data receiving loop. */
        while (true) {
                int msg_size, slave_idx;
                char *full_buff, *free_buff; //shortcuts

                /* Wait for message from any slave. */
                MPI_Waitany(slave_cnt, requests, &slave_idx, &status);
                if (slave_idx == MPI_UNDEFINED) { //no active slaves anymore
                        break;
                }

                /* Determine actual size of received message. */
                MPI_Get_count(&status, MPI_BYTE, &msg_size);
                if (msg_size == 0) {
                        print_debug("task assigned to %d finished\n",
                                        status.MPI_SOURCE);
                        continue; //empty message -> slave finished
                }

                /* Determine which buffer is free and which is currently used.*/
                full_buff = data_buff[data_buff_idx[slave_idx]][slave_idx];
                data_buff_idx[slave_idx] = !data_buff_idx[slave_idx]; //toggle
                free_buff = data_buff[data_buff_idx[slave_idx]][slave_idx];

                /* Start receiving next message into free buffer. */
                MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE,
                                status.MPI_SOURCE, TAG_DATA, MPI_COMM_WORLD,
                                &requests[slave_idx]);

                if (limit_exceeded) {
                        continue; //do not process but continue receiving
                }

                /* Call callback function for each received record. */
                for (size_t i = 0; i < msg_size / sizeof (lnf_brec1_t); ++i) {
                        ret = recv_callback(full_buff + i * sizeof(lnf_brec1_t),
                                        sizeof (lnf_brec1_t), user);
                        if (ret != E_OK) {
                                err = ret;
                                goto cleanup; //don't receive next message
                        }

                        if (++rec_cntr == rec_limit) {
                                limit_exceeded = true;
                                break;
                        }
                }
        }

cleanup:
        for (size_t i = 0; i < slave_cnt; ++i) {
                free(data_buff[1][i]);
                free(data_buff[0][i]);
        }

        return err;
}


static int mode_rec_main(size_t slave_cnt, size_t rec_limit)
{
        return irecv_loop(slave_cnt, rec_limit, print_brec_callback, NULL);
}


static int mode_ord_main(size_t slave_cnt, size_t rec_limit,
                const struct agg_params *ap, size_t ap_cnt)
{
        int ret, err = E_OK;
        struct mem_insert_callback_data callback_data = {0};

        /* Initialize aggregation memory. */
        ret = lnf_mem_init(&callback_data.mem);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_init()");
                callback_data.mem = NULL;
                err = E_LNF;
                goto cleanup;
        }
        /* Initialize empty LNF record for writing. */
        ret = lnf_rec_init(&callback_data.rec);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_rec_init()");
                callback_data.rec = NULL;
                err = E_LNF;
                goto cleanup;
        }

        /* Switch memory to linked list (better for sorting). */
        ret = lnf_mem_setopt(callback_data.mem, LNF_OPT_LISTMODE, NULL, 0);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_setopt()");
                err = E_LNF;
                goto cleanup;
        }

        /* Set memory parameters. */
        ret = mem_setup(callback_data.mem, ap, ap_cnt);
        if (ret != E_OK) {
                err = E_LNF;
                goto cleanup;
        }

        /* Fill memory with records. */
        if (rec_limit != 0) { //fast ordering, minimum of records exchanged
                printf("Fast ordering mode.\n");
                ret = recv_loop(slave_cnt, 0, mem_write_raw_callback,
                                callback_data.mem);
                if (ret != E_OK) {
                        err = ret;
                        goto cleanup;
                }
        } else { //slow ordering, all records exchanged
                printf("Slow ordering mode.\n");
                ret = irecv_loop(slave_cnt, 0, mem_write_callback,
                                &callback_data);
                if (ret != E_OK) {
                        err = ret;
                        goto cleanup;
                }
        }

        /* Print all records in memory. */
        ret = mem_print(callback_data.mem, rec_limit);
        if (ret != E_OK) {
                return ret;
        }

cleanup:
        if (callback_data.rec != NULL) {
                lnf_rec_free(callback_data.rec);
        }
        if (callback_data.mem != NULL) {
                lnf_mem_free(callback_data.mem);
        }

        return err;
}


static int mode_agg_main(size_t slave_cnt, size_t rec_limit,
                const struct agg_params *ap, size_t ap_cnt, bool use_fast_topn)
{
        int ret, err = E_OK;
        lnf_mem_t *mem;

        /* Initialize aggregation memory. */
        ret = lnf_mem_init(&mem);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_init()");
                mem = NULL;
                err = E_LNF;
                goto cleanup;
        }
        ret = mem_setup(mem, ap, ap_cnt);
        if (ret != E_OK) {
                err = E_LNF;
                goto cleanup;
        }

        ret = recv_loop(slave_cnt, 0, mem_write_raw_callback, mem);
        if (ret != E_OK) {
                err = ret;
                goto cleanup;
        }

        if (use_fast_topn) {
                ret = fast_topn_bcast_all(mem);
                if (ret != E_OK) {
                        err = ret;
                        goto cleanup;
                }

                /* Reset memory - all records will be received again. */
                //TODO: optimalization - add records to memory, don't reset
                lnf_mem_free(mem);
                ret = lnf_mem_init(&mem);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_init()");
                        mem = NULL;
                        err = E_LNF;
                        goto cleanup;
                }

                ret = mem_setup(mem, ap, ap_cnt);
                if (ret != E_OK) {
                        err = ret;
                        goto cleanup;
                }

                ret = recv_loop(slave_cnt, 0, mem_write_raw_callback,
                                mem);
                if (ret != E_OK) {
                        err = ret;
                        goto cleanup;
                }
        }

        ret = mem_print(mem, rec_limit);
        if (ret != E_OK) {
                err = ret;
                goto cleanup;
        }

cleanup:
        if (mem != NULL) {
                lnf_mem_free(mem);
        }

        return err;
}


int master(int world_rank, int world_size, const struct cmdline_args *args)
{
        (void)world_rank;
        int ret, err = E_OK;
        const size_t slave_cnt = world_size - 1; //all nodes without master

        task_info_t ti = {0};

        /* Fill task info struct for slaves (working mode etc). */
        fill_task_info(&ti, args, slave_cnt);

        /* Broadcast task info, optional filter string and path string. */
        MPI_Bcast(&ti, 1, task_info_mpit, ROOT_PROC, MPI_COMM_WORLD);
        if (ti.filter_str_len > 0) {
                MPI_Bcast(args->filter_str, ti.filter_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);
        }
        if (ti.path_str_len > 0) {
                MPI_Bcast(args->path_str, ti.path_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);
        }

        /* Send, receive, process. */
        switch (args->working_mode) {
        case MODE_REC:
                ret = mode_rec_main(slave_cnt, args->rec_limit);
                if (ret != E_OK) {
                        err = ret;
                }
                break;
        case MODE_ORD:
                ret = mode_ord_main(slave_cnt, args->rec_limit,
                                args->agg_params, args->agg_params_cnt);
                if (ret != E_OK) {
                        err = ret;
                }
                break;
        case MODE_AGG:
                ret = mode_agg_main(slave_cnt, args->rec_limit,
                                args->agg_params, args->agg_params_cnt,
                                args->use_fast_topn);
                if (ret != E_OK) {
                        err = ret;
                }
                break;
        default:
                assert(!"unknown working mode");
        }

        if (err != E_OK) {
                printf("MASTER: returning with error\n");
        }
        return err;
}
