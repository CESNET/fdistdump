/**
 * @brief Program entry point.
 *
 * Initialization of the MPI execution environment, command-line arguments
 * parsing, master/slave execution split. Returns EXIT_SUCCESS on success or
 * aborts on error.
 */

/*
 * Copyright 2015-2018 CESNET
 *
 * This file is part of Fdistdump.
 *
 * Fdistdump is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Fdistdump is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Fdistdump.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdlib.h>     // for EXIT_SUCCESS

#include <mpi.h>        // for MPI_Abort, MPI_COMM_WORLD, MPI_Comm_rank, MPI...

#include "arg_parse.h"  // for arg_parse, cmdline_args
#include "common.h"     // for ::E_MPI, mpi_create_communicators, ::E_OK
#include "errwarn.h"            // for error/warning/info/debug messages, ...
#include "master.h"     // for master_main
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
    ABORT_IF(thread_provided != MPI_THREAD_MULTIPLE, E_MPI,
             "an insufficient level of thread support, MPI_THREAD_MULTIPLE is required.");

    // determine the calling processes rank and the total number of proecesses
    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // check if there are at least two processes
    ABORT_IF(world_size < 2, E_MPI,
             " at least 2 copies of the program are required "
             "(one for the master and the others for the slaves). "
             "Did you use MPI process manager, e.g., mpiexec, mpirun, ...?");

    // parse command line arguments in all processes
    struct cmdline_args args = { .working_mode = MODE_UNSET };
    error_code_t ecode = arg_parse(&args, argc, argv, world_rank == ROOT_PROC);
    if (ecode == E_HELP) {
        MPI_Finalize();
        return EXIT_SUCCESS;
    }
    ABORT_IF(ecode != E_OK, ecode, "parsing arguments failed");

    // duplicate MPI_COMM_WORLD and create mpi_comm_main and mpi_comm_progress
    mpi_comm_init();
    DEBUG("created MPI communicators mpi_comm_main and mpi_comm_progress");

    // split master and slave code
    if (world_rank == ROOT_PROC) {
        master_main(&args);
    } else {
        slave_main(&args);
    }

    // deallocate MPI communicators created by mpi_comm_init()
    mpi_comm_free();

    if (ecode == E_OK || ecode == E_HELP) {
        DEBUG("terminating with success");
        MPI_Finalize();
        return EXIT_SUCCESS;
    } else {
        DEBUG("terminating MPI execution environment due to an error");
        MPI_Abort(MPI_COMM_WORLD, ecode);
    }
}
