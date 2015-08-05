/**
 * \file communication.h
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

#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include "../slave.h"
#include "../master.h"
#include "../common.h"

#include <stddef.h> //size_t

#ifdef CHOOSE_COMM_MPI
#include "mpi/comm_mpi.h"
#include "mpi/comm_mpi_master.h"
#include "mpi/comm_mpi_slave.h"
//#elif ...
#endif //CHOOSE_COMM_...

#define UNUSED(x) (void)(x)

/**
    Side of processing (and communication)
*/
typedef enum {
        FDD_MASTER,
        FDD_SLAVE,
} fdistdump_side_t;

/**
    Global context, same structure for master and slave.
*/
typedef struct global_context_s{
    size_t slave_cnt;
    fdistdump_side_t side;
} global_context_t;

/// ////////////////////////////////////////////////////////////////////////////
/// Communication interface (function pointers):

/** \brief Global initialization, need to be done by both, master and slave.
 *
 * Initialize global_context structure and other communication module specific
 * initialization. Implementation of this function have to consider empty
 * global_context and fill all items of this structure.
 *
 * \param[in] argc Count of program arguments (argc from main).
 * \param[in] argv Array of program arguments (argv from main). DO NOT alter
                   content of this array.
 * \param[out] g_ctx Pointer to global context structure.
 * \return E_OK on success, error code otherwise.
 */
int (* comm_init_global) (int , char **, global_context_t *);

/** \brief Global finalization, need to be done by both, master and slave.
 *
 * No mandatory behavior, only communication module implementation specific
 * stuff.
 *
 * \param[out] g_ctx Pointer to global context structure.
 * \return E_OK on success, error code otherwise.
 */
int (* comm_fin_global) (global_context_t *);

/** \brief Receive bytes of given size.
 *
 * \param[in] size Expected size of data
 * \param[out] buff Pointer to data buffer.
 * \return E_OK on success, error code otherwise.
 */
int (* comm_recv_bytes) (int , char *);

/**
 * TODO description
 */
int (* comm_bcast_zero_msg) ();

/**
 * TODO description
 */
int (* comm_send_zero_msg) ();

/**
 * TODO description
 */
int (* comm_send_bytes) (int , char *);

/**
 * TODO description
 */
int (* comm_receive_rec_len) (int *);


// -----------------------------------------------------------------------------
// Master interface ------------------------------------------------------------

/** \brief Parse master specific argument.
 *
 * Function parses given command line argument, which is specific to given
 * communication implementation.
 *
 * \param[in] opt Option specifier.
 * \param[in] optarg Option argument.
 * \param[out] m_par Pointer to master parameters specific structure.
 * \return E_OK on success, E_ARG on argument error.
 */
int (* comm_parse_arg) (int , char *, master_params_t *);

/** \brief Create structure for master specific parameters.
 *
 * \param[out] p_m_par Pointer to pointer to master parameters specific
 * structure.
 * \return E_OK on success, error code on error.
 */
int (* comm_create_master_params) (master_params_t **);

/** \brief Free structure for master specific parameters.
 *
 * \param[in] m_par Pointer to master parameters specific structure.
 */
void (* comm_free_master_params) (master_params_t *);

/** \brief Create master specific context.
 *
 * Initialization specific to given communication implementation.
 *
 * \param[out] p_m_ctx Pointer to pointer to master context structure.
 * \param[in] m_par Pointer to structure with master specific parameters.
 * \param[in] slave_cnt Count of active slave nodes.
 * \return E_OK on success, error code on error.
 */
int (* comm_init_master_ctx) (master_context_t **, master_params_t *, size_t);

/** \brief Free master specific context.
 *
 * \param[in] m_ctx Pointer to master context structure.
 */
void (* comm_destroy_master_ctx) (master_context_t **, master_params_t *,
                                  size_t);

/** \brief Broadcast task setup to all slave nodes.
 *
 * \param[in] t_setup Pointer to task setup structure.
 * \return E_OK on success, error code otherwise.
 */
int (* comm_bcast_task_setup) (task_setup_t *);

/**
 * TODO description
 */
int (* comm_irecv_loop) (master_context_t *, size_t , size_t , recv_callback_t ,
                         void *);

/**
 * TODO description
 */
int (* comm_recv_loop) (master_context_t *, size_t , size_t , recv_callback_t ,
                        void *);

/**
 * TODO description
 */
int (* comm_bcast_bytes) (int , char *);


// -----------------------------------------------------------------------------
// Slave interface -------------------------------------------------------------

/** \brief Parse slave specific argument.
 *
 * Function parses given command line argument, which is specific to given
 * communication implementation.
 *
 * \param[in] opt Option specifier.
 * \param[in] optarg Option argument.
 * \param[out] m_par Pointer to slave parameters specific structure.
 * \return E_OK on success, E_ARG on argument error.
 */
int (* comm_parse_arg_slave) (int , char *, slave_params_t *);

/** \brief Create structure for slave specific parameters.
 *
 * \param[out] p_m_par Pointer to pointer to slave parameters specific structure.
 * \return E_OK on success, error code on error.
 */
int (* comm_create_slave_params) (slave_params_t **);

/** \brief Free structure for slave specific parameters.
 *
 * \param[out] m_par Pointer to slave parameters specific structure.
 */
void (* comm_free_slave_params) (slave_params_t *);

/** \brief Receive (static size) part of task setup.
 *
 * \param[in] t_setup Pointer to task setup structure (static size part).
 * \return E_OK on success, error code otherwise.
 */
int (* comm_recv_task_static_setup) (task_setup_static_t *);

/**
 * TODO description
 */
int (* comm_isend_bytes) (void *, size_t);

/**
 * TODO description
 */
int (* comm_send_loop) (slave_task_t *);

#ifdef CHOOSE_COMM_MPI
inline void comm_interface_init (void)
{
        comm_init_global = comm_init_global_mpi;
        comm_fin_global = comm_fin_global_mpi;

        comm_recv_bytes = recv_bytes_mpi;
        comm_send_bytes = send_bytes_mpi;
        comm_send_zero_msg = send_zero_msg_mpi;
        comm_receive_rec_len = receive_rec_len_mpi;

        //Master
        comm_create_master_params = create_m_par_mpi;
        comm_free_master_params = free_m_par_mpi;
        comm_parse_arg = parse_arg_mpi;
        comm_init_master_ctx = init_master_ctx_mpi;
        comm_destroy_master_ctx = destroy_master_ctx_mpi;

        comm_bcast_task_setup = bcast_task_setup_mpi;
        comm_irecv_loop = irecv_loop_mpi;
        comm_recv_loop = recv_loop_mpi;
        comm_bcast_bytes = bcast_bytes_mpi;
        comm_bcast_zero_msg = bcast_zero_msg_mpi;

        //Slave
        comm_create_slave_params = create_s_par_mpi;
        comm_free_slave_params = free_s_par_mpi;
        comm_parse_arg_slave = parse_arg_slave_mpi;
//        comm_init_slave_ctx = ..;
//        comm_destroy_slave_ctx = ..;

        comm_recv_task_static_setup = recv_task_static_setup_mpi;

        comm_isend_bytes = isend_bytes_mpi;
        comm_send_loop = send_loop_mpi;
}
//#elif ...
#endif


#endif //COMMUNICATION_H
