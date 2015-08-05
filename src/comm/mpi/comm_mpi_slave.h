/**
 * \file comm_mpi_slave.h
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

#ifndef COMM_MPI_SLAVE_H
#define COMM_MPI_SLAVE_H

//#include "comm_mpi.h"
#include "../../slave.h"
//
//#include <mpi.h>
//#include <stdbool.h>
//
typedef struct slave_params_s{
        int none;
} slave_params_t;
//
//typedef struct slave_context{
//        MPI_Request *requests;//[slave_cnt];
//        MPI_Status status;
//        char **data_buff[2];//[slave_cnt]; //two buffers for each slave
//        bool *data_buff_idx;//[slave_cnt]; //current buffer index
//} slave_context;

/// Not used here
int create_s_par_mpi (slave_params_t **p_s_par);

/// Not used here
void free_s_par_mpi (slave_params_t *s_par);

/// Not used here, always return E_ARG
int parse_arg_slave_mpi (int opt, char *optarg, slave_params_t *s_par);

/// Receive (static size) part of task setup. Internally using bcast.
int recv_task_static_setup_mpi(task_setup_static_t *t_setup);

/**
 * TODO description
 */
int isend_bytes_mpi(void *src, size_t bytes);

/**
 * TODO description
 */
int send_loop_mpi(slave_task_t *st);

//int send_top_k_records_mpi(slave_task *task, global_context_t *g_ctx);
//int send_all_sent_mpi();
//int recv_topn_ids_mpi(size_t n, lnf_mem_t *mem, lnf_mem_cursor_t **cursors);
//int send_topn_records_mpi(size_t n, lnf_mem_t *mem, lnf_mem_cursor_t **cursors);
//int task_await_new_mpi(slave_task *t);
//

#endif //COMM_MPI_SLAVE_H
