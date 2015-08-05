/**
 * \file comm_mpi_slave.c
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
#include "comm_mpi_slave.h"
#include "../communication.h"
#include "../../common.h"
#include "../../slave.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include <mpi.h>

/* Global MPI data types. */
extern MPI_Datatype agg_params_mpit;
extern MPI_Datatype task_setup_mpit;
extern MPI_Datatype struct_tm_mpit;



int create_s_par_mpi (slave_params_t **p_s_par)
{
        slave_params_t *s_par = (slave_params_t *) malloc
                                        (sizeof(slave_params_t));
        if (s_par == NULL){
                return E_MEM;
        }
        *p_s_par = s_par;

        return E_OK;
}

void free_s_par_mpi (slave_params_t *s_par)
{
        free(s_par);
}

int parse_arg_slave_mpi (int opt, char *optarg, slave_params_t *s_par)
{
        UNUSED(opt);
        UNUSED(optarg);
        UNUSED(s_par);

        return E_ARG;
}

/* Receive (static size) part of task setup. */
int recv_task_static_setup_mpi(task_setup_static_t *t_setup)
{
        MPI_Bcast(t_setup, 1, task_setup_mpit, ROOT_PROC,  MPI_COMM_WORLD);

        return E_OK;
}

/* TODO comment */
int isend_bytes_mpi(void *src, size_t bytes)
{
        MPI_Request req = MPI_REQUEST_NULL;

        MPI_Wait(&req, MPI_STATUS_IGNORE);
        MPI_Isend(src, bytes, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD,
                        &req);

        return E_OK;
}

/* TODO comment */
/// TODO move to slave.c, keep here only send function
int send_loop_mpi(slave_task_t *st)
{
        int ret, err = E_OK, rec_len;
        lnf_mem_cursor_t *read_cursor;
        char rec_buff[LNF_MAX_RAW_LEN]; //TODO: send mutliple records

        ret = lnf_mem_first_c(st->agg, &read_cursor);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_first_c()");
                err = E_LNF;
                goto cleanup;
        }

        /* Send all records. */
        while (true) {
                ret = lnf_mem_read_raw_c(st->agg, read_cursor, rec_buff,
                                &rec_len, LNF_MAX_RAW_LEN);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_read_raw_c()");
                        err = E_LNF;
                        goto cleanup;
                }

                MPI_Send(rec_buff, rec_len, MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);

                ret = lnf_mem_next_c(st->agg, &read_cursor);
                if (ret == LNF_EOF) {
                        break; //all records sent
                } else if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_next_c()");
                        err = E_LNF;
                        goto cleanup;
                }
        }

cleanup:
        /* Top-N done, notify master by empty DATA message. */
        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);

        return err;
}
