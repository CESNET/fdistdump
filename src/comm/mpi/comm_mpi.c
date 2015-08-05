/**
 * \file comm_mpi.c
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


#include "comm_mpi.h"
#include "../communication.h"
#include "../../common.h"

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h> //offsetof()

#include <mpi.h>


/* Global MPI data types. */
MPI_Datatype agg_params_mpit;
MPI_Datatype task_setup_mpit;
MPI_Datatype struct_tm_mpit;


static void create_agg_params_mpit(void)
{
        int block_lengths[AGG_PARAMS_T_ELEMS] = {1, 1, 1, 1};
        MPI_Aint displacements[AGG_PARAMS_T_ELEMS];
        MPI_Datatype types[AGG_PARAMS_T_ELEMS] = {MPI_INT, MPI_INT, MPI_INT,
                MPI_INT};

        displacements[0] = offsetof(agg_params_t, field);
        displacements[1] = offsetof(agg_params_t, flags);
        displacements[2] = offsetof(agg_params_t, numbits);
        displacements[3] = offsetof(agg_params_t, numbits6);

        MPI_Type_create_struct(AGG_PARAMS_T_ELEMS, block_lengths, displacements,
                        types, &agg_params_mpit);
        MPI_Type_commit(&agg_params_mpit);
}

static void free_agg_params_mpit(void)
{
        MPI_Type_free(&agg_params_mpit);
}


static void create_task_setup_mpit(void)
{
        int block_lengths[TASK_SETUP_S_T_ELEMS] = {1, MAX_AGG_PARAMS, 1, 1, 1,
                1, 1, 1, 1, 1/*, NEW_ITEM_CNT*/};
        MPI_Aint displacements[TASK_SETUP_S_T_ELEMS];
        MPI_Datatype types[TASK_SETUP_S_T_ELEMS] = {MPI_INT, agg_params_mpit,
                MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG,
                MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG, struct_tm_mpit,
                struct_tm_mpit, MPI_C_BOOL/*, NEW_ITEM_TYPE*/};

        displacements[0] = offsetof(task_setup_static_t, working_mode);
        displacements[1] = offsetof(task_setup_static_t, agg_params);
        displacements[2] = offsetof(task_setup_static_t, agg_params_cnt);
        displacements[3] = offsetof(task_setup_static_t, filter_str_len);
        displacements[4] = offsetof(task_setup_static_t, path_str_len);
        displacements[5] = offsetof(task_setup_static_t, rec_limit);
        displacements[6] = offsetof(task_setup_static_t, slave_cnt);
        displacements[7] = offsetof(task_setup_static_t, interval_begin);
        displacements[8] = offsetof(task_setup_static_t, interval_end);
        displacements[9] = offsetof(task_setup_static_t, use_fast_topn);
        /*displacements[..] = offsetof(task_setup_static_t, new_item);*/

        MPI_Type_create_struct(TASK_SETUP_S_T_ELEMS, block_lengths,
                        displacements, types, &task_setup_mpit);
        MPI_Type_commit(&task_setup_mpit);
}

static void free_task_setup_mpit(void)
{
        MPI_Type_free(&task_setup_mpit);
}


static void create_struct_tm_mpit(void)
{
        MPI_Type_contiguous(STRUCT_TM_ELEMS, MPI_INT, &struct_tm_mpit);
        MPI_Type_commit(&struct_tm_mpit);
}

static void free_struct_tm_mpit(void)
{
        MPI_Type_free(&struct_tm_mpit);
}



/* Global initialization (MPI implementation of comm_init_global()) */
int comm_init_global_mpi(int argc, char **argv, global_context_t *g_ctx)
{
        int world_rank;
        int world_size;

        MPI_Init(&argc, &argv);
        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);

        if (world_rank == ROOT_PROC) {
                g_ctx->side = FDD_MASTER;
        } else {
                g_ctx->side = FDD_SLAVE;
        }
        g_ctx->slave_cnt = world_size - 1;

        /* Create MPI data types (global variables). */
        create_agg_params_mpit();
        create_struct_tm_mpit();
        create_task_setup_mpit();

        MPI_Barrier(MPI_COMM_WORLD); /// TODO useless?? (edit comment also)

        return E_OK;
}

/* Global finalization (MPI implementation of comm_fin_global()). */
int comm_fin_global_mpi(global_context_t *g_ctx)
{

        UNUSED(g_ctx);

        /* Free MPI data types (global variables). */
        free_task_setup_mpit();
        free_struct_tm_mpit();
        free_agg_params_mpit();

        MPI_Finalize();

        return E_OK;
}

/* TODO comment */
int bcast_zero_msg_mpi()
{
        /// TODO data same as send_zero_msg_mpi ??
        int data = 0;
        MPI_Bcast(&data, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);

        return E_OK;
}

/* TODO comment */
int send_zero_msg_mpi()
{
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        return E_OK;
}

/* TODO comment */
int send_bytes_mpi(int len, char *buff)
{
        MPI_Send(buff, len, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        return E_OK;
}

/* Receive bytes of given size. */
int recv_bytes_mpi (int len, char *buff)
{

        MPI_Bcast(buff, len, MPI_CHAR, ROOT_PROC, MPI_COMM_WORLD);

        return E_OK;
}

/* Broadcast bytes of given size. */
int bcast_bytes_mpi (int len, char *buff)
{

        MPI_Bcast(&len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);
        MPI_Bcast(buff, len, MPI_BYTE, ROOT_PROC, MPI_COMM_WORLD);

        return E_OK;
}

/* TODO comment */
int receive_rec_len_mpi (int *len)
{

        MPI_Bcast(len, 1, MPI_INT, ROOT_PROC, MPI_COMM_WORLD);

        return E_OK;
}
