/**
 * @brief Program entry point.
 *
 * Initialization of the MPI execution environment, command-line arguments
 * parsing, master/slave execution split. Returns EXIT_SUCCESS on success or
 * aborts on error.
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
 */

#include "config.h"

#include <stdlib.h>     // for EXIT_SUCCESS

#include <mpi.h>        // for MPI_Abort, MPI_COMM_WORLD, MPI_Comm_rank, MPI...

#include "arg_parse.h"  // for arg_parse, cmdline_args
#include "common.h"     // for ::E_MPI, mpi_create_communicators, ::E_OK
#include "master.h"     // for master_main
#include "print.h"      // for ERROR_IF, PRINT_DEBUG
#include "slave.h"      // for slave_main


/**
 * @brief Program entry point.
 *
 * Contains initialization of the MPI execution environment, calls command-line
 * arguments parsing, performs a master/slave execution split. Returns
 * EXIT_SUCCESS on success or aborts on error.
 *
 * @param argc Number of command-line arguments in argument vector.
 * @param argv Vector of command-line argument strings.
 *
 * @return EXIT_SUCCESS on success of all processes, calls MPI_Abort() on error.
 */
int
main(int argc, char *argv[])
{
    /*
     * Initialize MPI and check supported thread level. MPI_THREAD_MULTIPLE is
     * required. MPICH supports it, but Open MPI < 3.0 does not support it by
     * default.
     */
    int thread_provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &thread_provided);
    ERROR_IF(thread_provided != MPI_THREAD_MULTIPLE, E_MPI,
            "an insufficient level of thread support, MPI_THREAD_MULTIPLE is required.");

    // determine the calling processes rank and the total number of proecesses
    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // check if there are at least two processes
    ERROR_IF(world_size < 2, E_MPI, PACKAGE_NAME
            " requires at least 2 copies of the program to run "
            "(one for the master and the others for the slaves). "
            "Did you use MPI process manager, e.g., mpiexec, mpirun, ...?");

    // parse command line arguments in all processes
    struct cmdline_args args = { 0 };
    error_code_t ecode = arg_parse(&args, argc, argv, world_rank == ROOT_PROC);
    ERROR_IF(ecode != E_OK, ecode, "parsing arguments failed");

    // duplicate MPI_COMM_WORLD and create mpi_comm_main and mpi_comm_progress_bar
    mpi_comm_init();
    PRINT_DEBUG("created MPI communicators mpi_comm_main and mpi_comm_progress_bar");

    // split master and slave code
    if (world_rank == ROOT_PROC) {
        master_main(&args);
    } else {
        slave_main(&args);
    }

    // deallocate MPI communicators created by mpi_comm_init()
    mpi_comm_free();

    if (ecode == E_OK || ecode == E_HELP) {
        PRINT_DEBUG("terminating with success");
        MPI_Finalize();
        return EXIT_SUCCESS;
    } else {
        PRINT_DEBUG("terminating MPI execution environment due to an error");
        MPI_Abort(MPI_COMM_WORLD, ecode);
    }
}
