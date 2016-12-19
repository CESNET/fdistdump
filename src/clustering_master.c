/**
 * \file clustering_master.c
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
 * \date 2016
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
#include "master.h"
#include "output.h"
#include "print.h"

#include <string.h> //strlen()
#include <stdbool.h>
#include <assert.h>
#include <errno.h>

#include <mpi.h>
#include <libnf.h>


/* Global variables. */
extern MPI_Datatype mpi_struct_shared_task_ctx;


struct master_task_ctx {
        /* Master and slave shared task context. */
        struct shared_task_ctx shared;

        /* Master specific task context. */
        size_t slave_cnt;
        struct output_params output_params;
};


static void construct_master_task_ctx(struct master_task_ctx *mtc,
                const struct cmdline_args *args, int world_size)
{
        /* Fill shared content. */
        mtc->shared.working_mode = args->working_mode;
        memcpy(mtc->shared.fields, args->fields,
                        MEMBER_SIZE(struct master_task_ctx, shared.fields));

        mtc->shared.path_str_len =
                (args->path_str == NULL) ? 0 : strlen(args->path_str);

        mtc->shared.filter_str_len =
                (args->filter_str == NULL) ? 0 : strlen(args->filter_str);

        mtc->shared.rec_limit = args->rec_limit;

        mtc->shared.time_begin = args->time_begin;
        mtc->shared.time_end = args->time_end;

        mtc->shared.use_fast_topn = args->use_fast_topn;


        /* Fill master specific content. */
        mtc->slave_cnt = world_size - 1; //all nodes without master
        mtc->output_params = args->output_params;
}


#if 0
static error_code_t recv_loop(size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user)
{
        error_code_t primary_errno = E_OK;
        int rec_len = 0;
        uint8_t rec_buff[LNF_MAX_RAW_LEN]; //TODO: receive mutliple records
        MPI_Status status;
        size_t rec_cntr = 0;
        size_t active_slaves = slave_cnt; //every slave is active
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

        uint8_t *buff_mem; //big chunk of memory for all data buffers
        uint8_t *buff[slave_cnt][2]; //pointers to the buff_mem
        bool buff_idx[slave_cnt]; //indexes to the currently used data buffers

        MPI_Request requests[slave_cnt + 1]; //plus one for progress
        MPI_Status status;

        size_t rec_cntr = 0; //processed records
        size_t byte_cntr = 0; //received bytes
        bool limit_exceeded = false;


        /* Allocate two receive buffers for each slave as a continuous memory.*/
        buff_mem = malloc(2 * XCHG_BUFF_SIZE * slave_cnt * sizeof (*buff_mem));
        if (buff_mem == NULL) {
                secondary_errno = 0;
                PRINT_ERROR(E_MEM, secondary_errno, "malloc()");
                return E_MEM;
        }

        /*
         * There are two buffers for each slave. The first one is passed to
         * nonblocking MPI receive function, the second one is processed at the
         * same time. After both these operations are completed, buffers are
         * switched. Buffer switching (toggling) is independent for each slave,
         * that's why array buff_idx[slave_cnt] is needed.
         * buff_mem is partitioned in buff in the following manner:
         *
         * <--------- XCHG_BUFF_SIZE -------> <-------- XCHG_BUFF_SIZE -------->
         * ---------------------------------------------------------------------
         * |           buff[0][0]            |            buff[0][1]           |
         * --------------------------------------------------------------------
         * |           buff[1][0]            |            buff[1][1]           |
         * ---------------------------------------------------------------------
         * .                                                                   .
         * .                                                                   .
         * .                                                                   .
         * ---------------------------------------------------------------------
         * |     buff[slave_cnt - 1][0]      |     buff[slave_cnt - 1][1]      |
         * ---------------------------------------------------------------------
         */
        for (size_t i = 0; i < slave_cnt; ++i) {
                requests[i] = MPI_REQUEST_NULL;

                buff[i][0] = buff_mem + (i * 2 * XCHG_BUFF_SIZE);
                buff[i][1] = buff[i][0] + XCHG_BUFF_SIZE;
        }
        memset(buff_idx, 0, slave_cnt * sizeof (buff_idx[0]));

        /* Start first individual nonblocking data receive from every slave. */
        for (size_t i = 0; i < slave_cnt; ++i) {
                uint8_t *free_buff = buff[i][buff_idx[i]]; //shortcut

                MPI_Irecv(free_buff, XCHG_BUFF_SIZE, MPI_BYTE, i + 1, TAG_DATA,
                                MPI_COMM_WORLD, &requests[i]);
        }

        /* Start first nonblocking progress report receive from any slave. */
        if (progress_bar_ctx.files_sum > 0) {
                MPI_Irecv(NULL, 0, MPI_BYTE, MPI_ANY_SOURCE, TAG_PROGRESS,
                                MPI_COMM_WORLD, &requests[slave_cnt]);
        } else {
                requests[slave_cnt] = MPI_REQUEST_NULL;
        }

        /* Data receiving loop. */
        while (true) {
                int msg_size;
                int slave_idx;
                uint8_t *rec_ptr; //pointer to record in data buffer
                uint8_t *msg_end; //record boundary

                /* Wait for data or status report from any slave. */
                MPI_Waitany(slave_cnt + 1, requests, &slave_idx, &status);

                if (slave_idx == MPI_UNDEFINED) { //no active slaves anymore
                        break;
                }

                if (status.MPI_TAG == TAG_PROGRESS) {
                        bool finished = progress_bar_refresh(status.MPI_SOURCE);

                        if (!finished) { //expext next progress report
                                MPI_Irecv(NULL, 0, MPI_BYTE, MPI_ANY_SOURCE,
                                                TAG_PROGRESS, MPI_COMM_WORLD,
                                                &requests[slave_cnt]);
                        }

                        continue;
                }

                assert(status.MPI_TAG == TAG_DATA);

                /* Determine actual size of received message. */
                MPI_Get_count(&status, MPI_BYTE, &msg_size);
                if (msg_size == 0) {
                        continue; //empty message -> slave finished
                }
                byte_cntr += msg_size;

                rec_ptr = buff[slave_idx][buff_idx[slave_idx]]; //first record
                msg_end = rec_ptr + msg_size; //end of the last record
                buff_idx[slave_idx] = !buff_idx[slave_idx]; //toggle buffers

                /* Start receiving next message into free buffer. */
                MPI_Irecv(buff[slave_idx][buff_idx[slave_idx]], XCHG_BUFF_SIZE,
                                MPI_BYTE, status.MPI_SOURCE, TAG_DATA,
                                MPI_COMM_WORLD, &requests[slave_idx]);

                if (limit_exceeded) {
                        continue; //do not process but continue receiving
                }

                /*
                 * Call callback function for each record in received message.
                 * Each record is prefixed with 4 bytes long record size.
                 */
                while (rec_ptr < msg_end) {
                        uint32_t rec_size = *(uint32_t *)(rec_ptr);

                        rec_ptr += sizeof (rec_size); //shift to record data

                        primary_errno = recv_callback(rec_ptr, rec_size, user);
                        if (primary_errno != E_OK) {
                                goto free_db_mem;
                        }

                        rec_ptr += rec_size; //shift to next record

                        if (++rec_cntr == rec_limit) {
                                limit_exceeded = true;
                                break;
                        }
                }
        }

free_db_mem:
        free(buff_mem);

        PRINT_DEBUG("processed %zu records, received %zu B", rec_cntr,
                        byte_cntr);
        return primary_errno;
}
#endif


error_code_t clustering_master(int world_size, const struct cmdline_args *args)
{
        error_code_t primary_errno = E_OK;
        struct master_task_ctx mtc; //master task context
        double duration = -MPI_Wtime(); //start time measurement


        memset(&mtc, 0, sizeof (mtc));

        /* Construct master_task_ctx struct from command-line arguments. */
        construct_master_task_ctx(&mtc, args, world_size);
        output_setup(mtc.output_params, mtc.shared.fields);


        /* Broadcast task context, path string and optional filter string. */
        MPI_Bcast(&mtc.shared, 1, mpi_struct_shared_task_ctx, ROOT_PROC,
                        MPI_COMM_WORLD);

        MPI_Bcast(args->path_str, mtc.shared.path_str_len, MPI_CHAR, ROOT_PROC,
                        MPI_COMM_WORLD);

        if (mtc.shared.filter_str_len > 0) {
                MPI_Bcast(args->filter_str, mtc.shared.filter_str_len,
                                MPI_CHAR, ROOT_PROC, MPI_COMM_WORLD);
        }


        /* Send, receive, process. */


        duration += MPI_Wtime(); //end time measurement

        return primary_errno;
}
