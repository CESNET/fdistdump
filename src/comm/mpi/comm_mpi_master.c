/**
 * \file comm_mpi_master.c
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

#include "../../master.h"
#include "../../common.h"
#include "../communication.h"
#include "comm_mpi.h"
#include "comm_mpi_master.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <mpi.h>


/* Global MPI data types. */
extern MPI_Datatype agg_params_mpit;
extern MPI_Datatype task_setup_mpit;
extern MPI_Datatype struct_tm_mpit;



int create_m_par_mpi (master_params_t **p_m_par)
{
        master_params_t *m_par = (master_params_t *) malloc
                                        (sizeof(master_params_t));
        if (m_par == NULL){
                return E_MEM;
        }
        *p_m_par = m_par;

        return E_OK;
}

void free_m_par_mpi (master_params_t *m_par)
{
        free(m_par);
}

int parse_arg_mpi (int opt, char *optarg, master_params_t *m_par)
{
        UNUSED(opt);
        UNUSED(optarg);
        UNUSED(m_par);

        return E_ARG;
}

int init_master_ctx_mpi (master_context_t **m_ctx, master_params_t *m_par,
                         size_t slave_cnt)
{
        (void) m_ctx;
        UNUSED(m_par);

        master_context_t *mc = (master_context_t *) malloc (
                                                    sizeof(master_context_t));
        if (mc == NULL){
                print_err("malloc error");
                return E_MEM;
        }

        mc->requests = (MPI_Request *) malloc(sizeof (MPI_Request) * slave_cnt);
        if (mc->requests == NULL){
                print_err("malloc error");
                return E_MEM;
        }

        mc->data_buff_idx = (bool *) malloc(sizeof (bool) * slave_cnt);
        if (mc->data_buff_idx == NULL){
                print_err("malloc error");
                return E_MEM;
        }

        mc->data_buff[0] = (char **) malloc(sizeof (char *) * slave_cnt);
        mc->data_buff[1] = (char **) malloc(sizeof (char *) * slave_cnt);
        if (mc->data_buff[0] == NULL || mc->data_buff[0] == NULL){
                print_err("malloc error");
                return E_MEM;
        }

        /* Allocate receive buffers. */
        for (size_t i = 0; i < slave_cnt; ++i) {
                mc->requests[i] = MPI_REQUEST_NULL;
                mc->data_buff_idx[i] = 0;

                mc->data_buff[0][i] = malloc(XCHG_BUFF_SIZE);
                mc->data_buff[1][i] = malloc(XCHG_BUFF_SIZE);
                if (mc->data_buff[0][i] == NULL ||
                    mc->data_buff[1][i] == NULL) {
                        print_err("malloc error");
                        return E_MEM; //fatal
                }
        }

        *m_ctx = mc;

        return E_OK;
}


void destroy_master_ctx_mpi (master_context_t **m_ctx, master_params_t *m_par,
                         size_t slave_cnt)
{
        UNUSED(m_par);
        master_context_t *mc = *m_ctx;

        for (size_t i = 0; i < slave_cnt; ++i){
                free(mc->data_buff[0][i]);
                free(mc->data_buff[1][i]);
        }
        free(mc->data_buff[0]);
        free(mc->data_buff[1]);
        free((*m_ctx)->requests);
        free(mc->data_buff_idx);
        free(mc);

        *m_ctx = NULL;
}

/* TODO description */
int recv_loop_mpi(master_context_t *m_ctx, size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user)
{
        (void) m_ctx;
        /// TODO use m_ctx instead
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

/* TODO description */
int irecv_loop_mpi(master_context_t *m_ctx, size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user)
{
        (void) m_ctx;
        /// TODO use m_ctx instead
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

/* Broadcast task setup to all slave nodes.*/
int bcast_task_setup_mpi(task_setup_t *t_setup)
{
//        printf("------------------------------------------------------\n");
//        printf("W-mod: %d\n", t_setup->s.working_mode);
//        printf("Agg-par-cnt: %d\n", t_setup->s.agg_params_cnt);
//        for (int i = 0; i < t_setup->s.agg_params_cnt; ++i)
//        {
//                printf("Agg-par%d: %d", i, t_setup->s.agg_params[i].field);
//                printf(", %d\n", i, t_setup->s.agg_params[i].flags);
//        }
//        printf("F-len: %d\n", t_setup->s.filter_str_len);
//        printf("P-len: %d\n", t_setup->s.path_str_len);
//        printf("Rec-lim: %d\n", t_setup->s.rec_limit);
//        printf("Slave-cnt: %d\n", t_setup->s.slave_cnt);
//
////        struct tm interval_begin; //begin and end of time interval
////        struct tm interval_end;
//
//        if(t_setup->s.use_fast_topn){
//                printf("BCAST:FAST\n");
//        } else {
//                printf("BCAST:NO-FAST\n");
//        }
//        printf("------------------------------------------------------\n");

        MPI_Bcast(&t_setup->s, 1, task_setup_mpit, ROOT_PROC, MPI_COMM_WORLD);

        if (t_setup->s.filter_str_len > 0) {
                MPI_Bcast(t_setup->filter_str, t_setup->s.filter_str_len,
                                MPI_CHAR, ROOT_PROC, MPI_COMM_WORLD);
        }

        if (t_setup->s.path_str_len > 0) {
                MPI_Bcast(t_setup->path_str, t_setup->s.path_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);
        }

        return E_OK;
}
