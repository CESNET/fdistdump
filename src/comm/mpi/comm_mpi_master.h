/**
 * \file comm_mpi_master.h
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

#ifndef COMM_MPI_MASTER_H
#define COMM_MPI_MASTER_H

#include "../../common.h"
#include "../../master.h"

#include <stddef.h> //size_t

typedef struct master_params_s{
        int none;
} master_params_t;

typedef struct master_context_s{
        MPI_Request *requests; //[slave_cnt];
        MPI_Status status;
        char **data_buff[2]; //[slave_cnt]; //two buffers for each slave
        bool *data_buff_idx; //[slave_cnt]; //current buffer index
} master_context_t;

/// Not used here
int create_m_par_mpi (master_params_t **p_m_par);

/// Not used here
void free_m_par_mpi (master_params_t *m_par);

/// Not used here, always return E_ARG
int parse_arg_mpi (int opt, char *optarg, master_params_t *m_par);

/**
 * TODO Description
 */
int init_master_ctx_mpi (master_context_t **m_ctx, master_params_t *m_par,
                         size_t slave_cnt);

/**
 * TODO Description
 */
void destroy_master_ctx_mpi (master_context_t **m_ctx, master_params_t *m_par,
                         size_t slave_cnt);

/// Broadcast task setup to all slave nodes.
int bcast_task_setup_mpi(task_setup_t *t_setup);

/**
 * TODO Description
 */
int irecv_loop_mpi(master_context_t *m_ctx, size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user);

/**
 * TODO Description
 */
int recv_loop_mpi(master_context_t *m_ctx, size_t slave_cnt, size_t rec_limit,
                recv_callback_t recv_callback, void *user);

#endif //COMM_MPI_MASTER_H
