/**
 * \file main.c
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
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
#include "slave.h"
#include "common.h"
#include "arg_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>

#include <mpi.h>
#include <libnf.h>


/* Global MPI data types. */
MPI_Datatype mpi_struct_agg_param, mpi_struct_task_info, mpi_struct_tm;

int main(int argc, char **argv)
{
        double duration;
        int world_rank, world_size, ret;
        struct cmdline_args args = {0};

        MPI_Init(&argc, &argv);
        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);

        if (world_rank == ROOT_PROC) {
                ret = arg_parse(&args, argc, argv);
                switch (ret) {
                case E_OK:
                        break;
                case E_HELP:
                        MPI_Abort(MPI_COMM_WORLD, EXIT_SUCCESS);
                        return EXIT_SUCCESS;
                        break;
                case E_ARG:
                        MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
                        return EXIT_FAILURE;
                        break;
                default:
                        assert(!"unknown error code received");
                        break;
                }
        }

        /* Start time measurement. */
        MPI_Barrier(MPI_COMM_WORLD);
        duration = -MPI_Wtime();

        /* Create MPI data types (global variables). */
        create_mpi_struct_agg_param();
        create_mpi_struct_tm();
        create_mpi_struct_task_info();

        /* Split master and slave code. */
        if (world_rank == ROOT_PROC) {
                master(world_rank, world_size, &args);
        } else {
                slave(world_rank, world_size);
        }

        /* End time measurement. */
        MPI_Barrier(MPI_COMM_WORLD);
        duration += MPI_Wtime();

        if (world_rank == ROOT_PROC) {
                /* CPUs, slaves, duration */
                printf("%d\t%d\t%f\n", world_size, world_size - 1, duration);
        }

        /* Free MPI data types (global variables). */
        free_mpi_struct_task_info();
        free_mpi_struct_tm();
        free_mpi_struct_agg_param();

        MPI_Finalize();
        return EXIT_SUCCESS;
}
