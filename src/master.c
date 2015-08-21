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


/* Global variables. */
extern MPI_Datatype mpi_struct_shared_task_ctx;
extern int secondary_errno;


struct mem_insert_callback_data {
        lnf_mem_t *mem;
        lnf_rec_t *rec;
};


typedef error_code_t(*recv_callback_t)(char *data, size_t data_len, void *user);


static void prepare_shared_task_ctx(struct shared_task_ctx *stc,
                const struct cmdline_args *args)
{
        stc->working_mode = args->working_mode;
        stc->agg_params_cnt = args->agg_params_cnt;

        memcpy(stc->agg_params, args->agg_params, args->agg_params_cnt *
                        sizeof(struct agg_param));

        stc->rec_limit = args->rec_limit;

        if (args->filter_str == NULL) {
                stc->filter_str_len = 0;
        } else {
                stc->filter_str_len = strlen(args->filter_str);
        }
        if (args->path_str == NULL) {
                stc->path_str_len = 0;
        } else {
                stc->path_str_len = strlen(args->path_str);
        }

        stc->interval_begin = args->interval_begin;
        stc->interval_end = args->interval_end;

        stc->use_fast_topn = args->use_fast_topn;
}


static error_code_t mem_write_callback(char *data, size_t data_len, void *user)
{
        (void)data_len;
        struct mem_insert_callback_data *micd =
                (struct mem_insert_callback_data *)user;

        secondary_errno = lnf_rec_fset(micd->rec, LNF_FLD_BREC1, data);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_rec_fset()");
                return E_LNF;
        }
        secondary_errno = lnf_mem_write(micd->mem, micd->rec);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_mem_write()");
                return E_LNF;
        }

        return E_OK;
}

static error_code_t mem_write_raw_callback(char *data, size_t data_len,
                void *user)
{
        secondary_errno = lnf_mem_write_raw((lnf_mem_t *)user, data, data_len);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_mem_write_raw()");
                return E_LNF;
        }

        return E_OK;
}

static error_code_t print_brec_callback(char *data, size_t data_len, void *user)
{
        (void)data_len;
        (void)user;

        printf("%s\n", mylnf_brec_to_str(*(lnf_brec1_t*)data));

        return E_OK;
}


static error_code_t fast_topn_bcast_all(lnf_mem_t *mem)
{
        int rec_len = 0;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records
        lnf_mem_cursor_t *read_cursor;

        //TODO: handle error, if no records received
        secondary_errno = lnf_mem_first_c(mem, &read_cursor);
        if (secondary_errno == LNF_EOF) {
                goto send_terminator; //no records in memory, no problem
        } else if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_mem_first_c()");
                return E_LNF;
        }

        /* Broadcast all records. */
        while (true) {
                secondary_errno = lnf_mem_read_raw_c(mem, read_cursor, rec_buff,
                                &rec_len, LNF_MAX_RAW_LEN);
                assert(secondary_errno != LNF_EOF);
                if (secondary_errno != LNF_OK) {
                        print_err(E_LNF, secondary_errno,
                                        "lnf_mem_read_raw_c()");
                        return E_LNF;
                }

                MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);
                MPI_Bcast(rec_buff, rec_len, MPI_BYTE, ROOT_PROC,
                                MPI_COMM_WORLD);

                secondary_errno = lnf_mem_next_c(mem, &read_cursor);
                if (secondary_errno == LNF_EOF) {
                        break; //all records successfully sent
                } else if (secondary_errno != LNF_OK) {
                        print_err(E_LNF, secondary_errno, "lnf_mem_next_c()");
                        return E_LNF;
                }
        }

send_terminator:
        /* Phase 2 done, notify slaves by zero record length. */
        rec_len = 0;
        MPI_Bcast(&rec_len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);

        return E_OK;
}


static error_code_t recv_loop(size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user)
{
        error_code_t primary_errno = E_OK;
        int rec_len = 0;
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
                primary_errno = recv_callback(rec_buff, rec_len, user);
                if (primary_errno != E_OK) {
                        break; //don't receive next message
                }

                if (++rec_cntr == rec_limit) {
                        limit_exceeded = true;
                }
        }

        return primary_errno;
}

static error_code_t irecv_loop(size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user)
{
        error_code_t primary_errno = E_OK;
        char *continuous_data_buff; //big chunk of memory
        char *data_buff[slave_cnt][2]; //pointers to continuous_data_buff
        bool data_buff_idx[slave_cnt]; //index to currently used data_buff
        MPI_Request requests[slave_cnt];
        MPI_Status status;
        size_t rec_cntr = 0; //processed records
        bool limit_exceeded = false;

        /* Allocate two receive buffers for each slave as continuous memory. */
        continuous_data_buff = malloc(2 * XCHG_BUFF_SIZE * slave_cnt);
        if (continuous_data_buff == NULL) {
                secondary_errno = 0;
                print_err(E_MEM, secondary_errno, "malloc()");
                return E_MEM;
        }

        /*
         * There are two buffers for each slave. The first one is passed to
         * nonblocking MPI receive function, the second one is processed at the
         * same time. After both these operations are completed, buffers are
         * switched. This switching is independet for each slave, that's why
         * array data_buff_idx[slave_cnt] is needed.
         * continuous_data_buff is partitioned in data_buff in following manner:
         *
         * <--------- XCHG_BUFF_SIZE -------> <-------- XCHG_BUFF_SIZE -------->
         * ---------------------------------------------------------------------
         * |         data_buff[0][0]         |          data_buff[0][1]        |
         * ---------------------------------------------------------------------
         * |         data_buff[1][0]         |          data_buff[1][1]        |
         * ---------------------------------------------------------------------
         * .                                                                   .
         * .                                                                   .
         * .                                                                   .
         * ---------------------------------------------------------------------
         * |   data_buff[slave_cnt - 1][0]   |   data_buff[slave_cnt - 1][1]   |
         * ---------------------------------------------------------------------
         */
        for (size_t i = 0; i < slave_cnt; ++i) {
                requests[i] = MPI_REQUEST_NULL;

                data_buff[i][0] = continuous_data_buff +
                        (i * 2 * XCHG_BUFF_SIZE);
                data_buff[i][1] = data_buff[i][0] + XCHG_BUFF_SIZE;
        }
        memset(data_buff_idx, 0, slave_cnt * sizeof(data_buff_idx[0]));

        /* Start first individual nonblocking data receive from every slave. */
        for (size_t i = 0; i < slave_cnt ; ++i) {
                char *free_buff = data_buff[i][data_buff_idx[i]]; //shortcut

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
                        continue; //empty message -> slave finished
                }

                /* Determine which buffer is free and which is currently used.*/
                full_buff = data_buff[slave_idx][data_buff_idx[slave_idx]];
                data_buff_idx[slave_idx] = !data_buff_idx[slave_idx]; //toggle
                free_buff = data_buff[slave_idx][data_buff_idx[slave_idx]];

                /* Start receiving next message into free buffer. */
                MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE,
                                status.MPI_SOURCE, TAG_DATA, MPI_COMM_WORLD,
                                &requests[slave_idx]);

                if (limit_exceeded) {
                        continue; //do not process but continue receiving
                }

                /* Call callback function for each received record. */
                for (size_t i = 0; i < msg_size / sizeof (lnf_brec1_t); ++i) {
                        primary_errno = recv_callback(full_buff +
                                        i * sizeof(lnf_brec1_t),
                                        sizeof (lnf_brec1_t), user);
                        if (primary_errno != E_OK) {
                                goto free_continuous_data_buff;
                        }

                        if (++rec_cntr == rec_limit) {
                                limit_exceeded = true;
                                break;
                        }
                }
        }

free_continuous_data_buff:
        free(continuous_data_buff);

        return primary_errno;
}


static error_code_t recv_stat(size_t slave_cnt, lnf_mem_t *stats)
{
        error_code_t primary_errno = E_OK;
        int rec_len = 0;
        char rec_buff[LNF_MAX_RAW_LEN];
        MPI_Status status;

        /* Data receiving loop. */
        for (size_t i = 0; i < slave_cnt; ++i) {
                /* Receive message from any slave. */
                MPI_Recv(rec_buff, LNF_MAX_RAW_LEN, MPI_BYTE, MPI_ANY_SOURCE,
                                TAG_STATS, MPI_COMM_WORLD, &status);

                /* Determine actual size of the received message. */
                MPI_Get_count(&status, MPI_BYTE, &rec_len);

                secondary_errno = lnf_mem_write_raw(stats, rec_buff, rec_len);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_mem_write_raw()");
                        break;
                }
        }

        return primary_errno;
}


static error_code_t mode_list_main(size_t slave_cnt, size_t rec_limit)
{
        return irecv_loop(slave_cnt, rec_limit, print_brec_callback, NULL);
}


static error_code_t mode_sort_main(size_t slave_cnt, size_t rec_limit,
                const struct agg_param *ap, size_t ap_cnt)
{
        error_code_t primary_errno = E_OK;
        struct mem_insert_callback_data callback_data = {0};

        /* Initialize aggregation memory and set memory parameters. */
        primary_errno = init_aggr_mem(&callback_data.mem, ap, ap_cnt);
        if (primary_errno != E_OK) {
                return primary_errno;
        }
        /* Switch memory to linked list (better for sorting). */
        secondary_errno = lnf_mem_setopt(callback_data.mem, LNF_OPT_LISTMODE,
                        NULL, 0);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_mem_setopt()");
                goto free_aggr_mem;
        }

        /* Initialize empty LNF record for writing. */
        secondary_errno = lnf_rec_init(&callback_data.rec);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_rec_init()");
                goto free_aggr_mem;
        }

        /* Fill memory with records. */
        if (rec_limit == 0) { //slow ordering, all records exchanged
                primary_errno = irecv_loop(slave_cnt, 0, mem_write_callback,
                                &callback_data);
                if (primary_errno != E_OK) {
                        goto free_lnf_rec;
                }
        } else { //fast ordering, minimum of records exchanged
                primary_errno = recv_loop(slave_cnt, 0, mem_write_raw_callback,
                                callback_data.mem);
                if (primary_errno != E_OK) {
                        goto free_lnf_rec;
                }
        }

        /* Print all records in memory. */
        primary_errno = print_aggr_mem(callback_data.mem, rec_limit, ap,
                        ap_cnt);

free_lnf_rec:
        lnf_rec_free(callback_data.rec);
free_aggr_mem:
        free_aggr_mem(callback_data.mem);

        return primary_errno;
}


static error_code_t mode_aggr_main(size_t slave_cnt, size_t rec_limit,
                const struct agg_param *ap, size_t ap_cnt, bool use_fast_topn)
{
        error_code_t primary_errno = E_OK;
        lnf_mem_t *aggr_mem;
        lnf_mem_t *stat_mem;

        /* Initialize aggregation memory and set memory parameters. */
        primary_errno = init_aggr_mem(&aggr_mem, ap, ap_cnt);
        if (primary_errno != E_OK) {
                return primary_errno;
        }

        primary_errno = recv_loop(slave_cnt, 0, mem_write_raw_callback,
                        aggr_mem);
        if (primary_errno != E_OK) {
                goto free_aggr_mem;
        }

        if (use_fast_topn) {
                //TODO: if file doesn't exist on slave, this will cause deadlock
                primary_errno = fast_topn_bcast_all(aggr_mem);
                if (primary_errno != E_OK) {
                        goto free_aggr_mem;
                }

                /* Reset memory - all records will be received again. */
                //TODO: optimalization - add records to memory, don't reset
                free_aggr_mem(aggr_mem);
                primary_errno = init_aggr_mem(&aggr_mem, ap, ap_cnt);
                if (primary_errno != E_OK) {
                        free_aggr_mem(stat_mem);
                        return primary_errno;
                }

                primary_errno = recv_loop(slave_cnt, 0, mem_write_raw_callback,
                                aggr_mem);
                if (primary_errno != E_OK) {
                        goto free_aggr_mem;
                }
        }

        /* Print all records in memory. */
        primary_errno = print_aggr_mem(aggr_mem, rec_limit, ap, ap_cnt);
        if (primary_errno != E_OK) {
                goto free_aggr_mem;
        }

        /* Initialize, receive, print and free statistics memory. */
        primary_errno = init_stat_mem(&stat_mem);
        if (primary_errno != E_OK) {
                goto free_aggr_mem;
        }
        primary_errno = recv_stat(slave_cnt, stat_mem);
        if (primary_errno != E_OK) {
                goto free_aggr_mem;
        }
        primary_errno = print_stat_mem(stat_mem);
        free_stat_mem(stat_mem);

free_aggr_mem:
        free_aggr_mem(aggr_mem);

        return primary_errno;
}


error_code_t master(int world_size, const struct cmdline_args *args)
{
        const size_t slave_cnt = world_size - 1; //all nodes without master

        struct shared_task_ctx stc = {0};

        /* Fill shared_task_ctx struct for slaves (working mode etc). */
        prepare_shared_task_ctx(&stc, args);

        /* Broadcast task info, optional filter string and path string. */
        MPI_Bcast(&stc, 1, mpi_struct_shared_task_ctx, ROOT_PROC,
                        MPI_COMM_WORLD);
        if (stc.filter_str_len > 0) {
                MPI_Bcast(args->filter_str, stc.filter_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);
        }
        if (stc.path_str_len > 0) {
                MPI_Bcast(args->path_str, stc.path_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);
        }

        /* Send, receive, process. */
        switch (args->working_mode) {
        case MODE_PASS: //only termination msg will be received from each slave
        case MODE_LIST:
                return mode_list_main(slave_cnt, args->rec_limit);

        case MODE_SORT:
                return mode_sort_main(slave_cnt, args->rec_limit,
                                args->agg_params, args->agg_params_cnt);

        case MODE_AGGR:
                return mode_aggr_main(slave_cnt, args->rec_limit,
                                args->agg_params, args->agg_params_cnt,
                                args->use_fast_topn);

        default:
                assert(!"unknown working mode");
        }

        assert(!"master()");
}
