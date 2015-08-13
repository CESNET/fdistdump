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
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <assert.h>

#include <mpi.h>
#include <libnf.h>


/* Global varables. */
MPI_Datatype mpi_struct_agg_param;
MPI_Datatype mpi_struct_shared_task_ctx;
MPI_Datatype mpi_struct_tm;
int secondary_errno;

int main(int argc, char **argv)
{
        error_code_t primary_errno = E_OK;
        int world_rank;
        int world_size;
        double duration;
        struct cmdline_args args = {0};

        MPI_Init(&argc, &argv);
        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);

        if (world_rank == ROOT_PROC) {
                primary_errno = arg_parse(&args, argc, argv);
                switch (primary_errno) {
                case E_OK:
                        break;

                case E_PASS: //help or error was printed
                        args.working_mode = MODE_PASS;
                        break;

                case E_ARG:
                        args.working_mode = MODE_PASS;
                        break;

                default:
                        assert(!"unknown error code received");
                }
        }

        if (world_size <= 1) {
                printf("%s requires at least 2 copies of the program to run. "
                                "Did you use mpirun? "
                                "Try to run program again with --help.\n",
                                PACKAGE_NAME);
                return EXIT_FAILURE;
        }

        /* Create MPI data types (global variables). */
        create_mpi_struct_agg_param();
        create_mpi_struct_tm();
        create_mpi_struct_shared_task_ctx();

        /* Start time measurement. */
        MPI_Barrier(MPI_COMM_WORLD);
        duration = -MPI_Wtime();

        /* Split master and slave code. */
        if (world_rank == ROOT_PROC) {
                primary_errno = master(world_size, &args);
        } else {
                primary_errno = slave(world_size);
        }

        /* End time measurement. */
        MPI_Barrier(MPI_COMM_WORLD);
        duration += MPI_Wtime();

        if (world_rank == ROOT_PROC) {
                print_debug("total duration: %fs", duration);
        }

        /* Free MPI data types (global variables). */
        free_mpi_struct_shared_task_ctx();
        free_mpi_struct_tm();
        free_mpi_struct_agg_param();

        MPI_Finalize();
        if (primary_errno == E_OK || primary_errno == E_PASS) {
                return EXIT_SUCCESS;
        } else {
                return EXIT_FAILURE;
        }
}
