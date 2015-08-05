/**
 * \file comm_mpi.h
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

#ifndef COMM_MPI_H
#define COMM_MPI_H

#include <mpi.h>


#define ROOT_PROC 0

//Set this value to count of items of structure agg_params_t defined in common.h
//WATCH OUT: reflect changes also in agg_params_mpit()
#define AGG_PARAMS_T_ELEMS 4

//Set this value to count of items of structure task_info_t defined in common.h
//WATCH OUT: reflect changes also in task_setup_mpit()
#define TASK_SETUP_S_T_ELEMS 10

//Set this value to count of items in struct tm defined in time.h
//WATCH OUT: reflect changes in struct tm from time.h also in struct_tm_mpit()
#define STRUCT_TM_ELEMS 9


enum { //tags
        TAG_CMD,
        TAG_TASK,
        TAG_FILTER,
        TAG_AGG,
        TAG_DATA,
};

//enum { //control commands
//        CMD_RELEASE,
//};

// Register name
typedef struct global_context_s global_context_t;

/** \brief Global initialization (MPI implementation of comm_init_global()).
 *
 * Initialize global_context structure, prepare data types used for
 * communication for usage by MPI. Synchronize all processes on barrier.
 *
 * \param[in] argc Count of program arguments (argc from main).
 * \param[in] argv Array of program arguments (argv from main). DO NOT change
                   content of this array.
 * \param[out] g_ctx Pointer to global context structure.
 * \return E_OK on success (always).
 */
int comm_init_global_mpi(int argc, char **argv, global_context_t *g_ctx);

/** \brief Global finalization (MPI implementation of comm_fin_global()).
 *
 * Free prepared data types, MPI finalization.
 *
 * \param[out] g_ctx Pointer to global context structure. - unused here
 * \return E_OK on success (always).
 */
int comm_fin_global_mpi(global_context_t *g_ctx);

/** \brief (MPI) Receive bytes of given size.
 *
 * \param[in] size Expected size of data
 * \param[out] buff Pointer to data buffer.
 * \return E_OK on success, error code otherwise.
 */
int recv_bytes_mpi (int len, char *buff);

/** \brief (MPI) Broadcast bytes of given size.
 *
 * Broadcast size of data first, than data itself.
 *
 * \param[in] size Size of data
 * \param[out] buff Pointer to data buffer.
 * \return E_OK on success, error code otherwise.
 */
int bcast_bytes_mpi (int len, char *buff);

/**
 * TODO description
 */
int bcast_zero_msg_mpi();

/**
 * TODO description
 */
int send_zero_msg_mpi();

/**
 * TODO description
 */
int send_bytes_mpi(int len, char *buff);

/**
 * TODO description
 */
int receive_rec_len_mpi (int *len);

#endif //COMM_MPI_H
