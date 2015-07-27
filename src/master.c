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

#include <libgen.h> //basename()
#include <mpi.h>
#include <libnf.h>

#define STATISTICS_MEM /// TODO - process statistics with aggreg AND LIST FLOWS

/* Global MPI data types. */
extern MPI_Datatype task_info_mpit;


void fill_task_info(task_info_t *ti, const params_t *params, size_t slave_count)
{
        ti->working_mode = params->working_mode;
        ti->agg_params_cnt = params->agg_params_cnt;

        memcpy(ti->agg_params, params->agg_params,
                        params->agg_params_cnt * sizeof(agg_params_t));

        ti->rec_limit = params->rec_limit;

        if (params->filter_str == NULL) {
                ti->filter_str_len = 0;
        } else {
                ti->filter_str_len = strlen(params->filter_str);
        }
        if (params->path_str == NULL) {
                ti->path_str_len = 0;
        } else {
                ti->path_str_len = strlen(params->path_str);
        }
        ti->slave_cnt = slave_count;

        ti->interval_begin = params->interval_begin;
        ti->interval_end = params->interval_end;
}

/* Send first top-n identifiers (aggregation field(s) values) to slave nodes */
int bcast_topn_ids (size_t n, lnf_mem_t **mem)
{
        char buff[LNF_MAX_RAW_LEN];
        int len;
        int ret;

        /* Send request for Top-N records */
        for (size_t i = 0; i < n; ++i){

                ret = lnf_mem_read_raw(*mem, buff, &len, LNF_MAX_RAW_LEN);

                if (ret != LNF_OK){
                    print_err("MOJE: cannot read raw memory record.");
                    return E_LNF;
                }

                MPI_Bcast(&len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);
                MPI_Bcast(&buff, len, MPI_BYTE, ROOT_PROC, MPI_COMM_WORLD);

                printf("MOJE: Record sent.\n");
        }

//        MPI_Bcast(NULL, 0, MPI_BYTE, ROOT_PROC, MPI_COMM_WORLD);

        return E_OK;
}


int master(int world_rank, int world_size, const params_t *params)
{
        (void)world_rank;
        const size_t slave_cnt = world_size - 1; //all nodes without master
        int ret = E_OK;
        size_t rec_cntr = 0; //printed records

        MPI_Request requests[slave_cnt];
        MPI_Status status;
        task_info_t ti = {0};
        lnf_mem_t *agg;
      #ifdef STATISTICS_MEM
        lnf_mem_t *stats;
      #endif // STATISTICS_MEM

        char *data_buff[2][slave_cnt]; //two buffers for each slave
        bool data_buff_idx[slave_cnt]; //current buffer index

        /* Allocate receive buffers. */
        for (size_t i = 0; i < slave_cnt; ++i) {
                requests[i] = MPI_REQUEST_NULL;
                data_buff_idx[i] = 0;

                data_buff[0][i] = malloc(XCHG_BUFF_SIZE);
                data_buff[1][i] = malloc(XCHG_BUFF_SIZE);
                if (data_buff[0][i] == NULL || data_buff[1][i] == NULL) {
                        print_err("malloc error");
                        return E_MEM; //fatal
                }
        }

        /* Initialize memory for aggregation on master. */
        if (params->working_mode != MODE_REC) {
                ret = agg_init(&agg, params->agg_params,
                                params->agg_params_cnt);
                if (ret != E_OK) {
                        return E_LNF;
                }
        }

    #ifdef STATISTICS_MEM
        ret = agg_init(&stats, params->agg_params, params->agg_params_cnt);
        if (ret != E_OK) {
                return E_LNF;
        }
    #endif // STATISTICS_MEM

        /* Fill task info struct for slaves (working mode etc). */
        fill_task_info(&ti, params, slave_cnt);

        /* Initialization phase.
         * Broadcast task info, optional filter string and optional path string.
         */
        MPI_Bcast(&ti, 1, task_info_mpit, ROOT_PROC, MPI_COMM_WORLD);
        if (ti.filter_str_len > 0) {
                MPI_Bcast(params->filter_str, ti.filter_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);
        }
        if (ti.path_str_len > 0) {
                MPI_Bcast(params->path_str, ti.path_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);
        }

        /* Start first individual nonblocking data receive from every slave. */
        for (size_t i = 0; i < slave_cnt ; ++i) {
                char *free_buff = data_buff[data_buff_idx[i]][i]; //shortcut

                MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE, i + 1, TAG_DATA,
                                MPI_COMM_WORLD, &requests[i]);
        }

        /* Data receiving loop. */
        while (1) {
                int msg_size, slave_idx;
                char *full_buff, *free_buff; //shortcuts

                /* Wait for message from any slave. */
                MPI_Waitany(slave_cnt, requests, &slave_idx, &status);
                if (slave_idx == MPI_UNDEFINED) { //no active slave anymore
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

                /* Receive into free buffer. */
                MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE,
                                status.MPI_SOURCE, TAG_DATA, MPI_COMM_WORLD,
                                &requests[slave_idx]);

                print_debug("received %d bytes from %d\n", msg_size,
                                status.MPI_SOURCE);

                /* Either store or process received records. */
                if (params->working_mode != MODE_REC) {
                        ret = lnf_mem_write_raw(agg, full_buff, msg_size);
                        if (ret != LNF_OK) {
                                print_err("cannot write raw memory record");
                                return E_LNF;
                        }
                    #ifdef STATISTICS_MEM /// TODO - process statistics with aggreg AND LIST FLOWS
                        ret = lnf_mem_write_raw(stats, full_buff, msg_size);
                        if (ret != LNF_OK) {
                                print_err("cannot write raw memory record");
                                return E_LNF;
                        }
                    #endif // STATISTICS_MEM
                } else {
                        for (size_t i = 0; i < msg_size / sizeof (lnf_brec1_t);
                                        ++i) {
                                if (params->rec_limit &&
                                                rec_cntr == params->rec_limit) {
                                        break;
                                }
                                print_brec(((lnf_brec1_t*)full_buff)[i]);
                                rec_cntr++;
                        }
                }
        }

        /* Received Top-K records from every slave node, request relevant global
           Top - N records. */
        if (params->working_mode != MODE_REC) {
                ret = bcast_topn_ids (params->rec_limit, &agg);
                if (ret != E_OK) {
                        return ret;
                }

                lnf_mem_read_reset(agg);

                /* Clear agg-memheap */
                lnf_mem_free(agg);
                ret = agg_init(&agg, params->agg_params,
                                params->agg_params_cnt);
                if (ret != E_OK) {
                        return E_LNF;
                }


                /* Receive requested Top-N records */
                /* Start first individual nonblocking data receive from every
                   slave. */
                for (size_t i = 0; i < slave_cnt ; ++i) {
                        char *free_buff = data_buff[data_buff_idx[i]][i];

                        MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE, i + 1,
                                  TAG_DATA, MPI_COMM_WORLD, &requests[i]);
                }

                /* Data receiving loop. */
                while (1) {
                        int msg_size, slave_idx;
                        char *full_buff, *free_buff; //shortcuts

                        /* Wait for message from any slave. */
                        MPI_Waitany(slave_cnt, requests, &slave_idx, &status);
                        if (slave_idx == MPI_UNDEFINED) { //no active slaves
                                break;
                        }

                        /* Determine actual size of received message. */
                        MPI_Get_count(&status, MPI_BYTE, &msg_size);
                        if (msg_size == 0) {
                                print_debug("second iteration on %d finished\n",
                                                status.MPI_SOURCE);
                                continue; //empty message -> slave finished
                        }

                        full_buff = data_buff[data_buff_idx[slave_idx]][slave_idx];
                        data_buff_idx[slave_idx] = !data_buff_idx[slave_idx]; //toggle
                        free_buff = data_buff[data_buff_idx[slave_idx]][slave_idx];

                        MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE,
                                        status.MPI_SOURCE, TAG_DATA,
                                        MPI_COMM_WORLD, &requests[slave_idx]);

                        print_debug("received %d bytes from %d (2)\n", msg_size,
                                        status.MPI_SOURCE);

                        /* Store or process received records. */
                        ret = lnf_mem_write_raw(agg, full_buff, msg_size);
                        if (ret != LNF_OK) {
                                print_err("cannot write raw memory record");
                                return E_LNF;
                        }
                }

                /* Print result. */
                lnf_rec_t *rec;
                lnf_brec1_t brec;

                ret = lnf_rec_init(&rec);
                if (ret != LNF_OK) {
                        print_err("cannot initialise empty record object");
                        return E_LNF;
                }

                for (ret = lnf_mem_read(agg, rec); ret == LNF_OK;
                                ret = lnf_mem_read(agg, rec)) {
                        rec_cntr++;
                        ret = lnf_rec_fget(rec, LNF_FLD_BREC1, &brec);
                        print_brec(brec);

                        if (params->rec_limit &&
                                        rec_cntr == params->rec_limit) {
                                break;
                        }
                }

                fflush(stdout);
                fflush(stderr);

                lnf_rec_free(rec);
                lnf_mem_free(agg);
        }


        /// TODO - process statistics message.
        /// TODO - process statistics with aggreg AND LIST FLOWS


    #ifdef STATISTICS_MEM
        lnf_mem_free(stats);
    #endif // STATISTICS_MEM

        /* Cleanup. */
        for (size_t i = 0; i < slave_cnt; ++i) {
                free(data_buff[1][i]);
                free(data_buff[0][i]);
        }

        return E_OK;
}
