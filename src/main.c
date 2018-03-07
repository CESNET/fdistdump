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

#include "common.h"
#include "master.h"
#include "slave.h"
#include "arg_parse.h"
#include "print.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <mpi.h>


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
    error_code_t ecode = E_OK;

    /*
     * Initialize MPI and check supported thread level. We need at least
     * MPI_THREAD_SERIALIZED. MPI_THREAD_MULTIPLE would be great, but
     * Open MPI < 3.0 does not support it by default.
     */
    int thread_provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_SERIALIZED, &thread_provided);
    if (thread_provided != MPI_THREAD_SERIALIZED
            && thread_provided != MPI_THREAD_MULTIPLE) {
        ecode = E_MPI;
        PRINT_ERROR(ecode, thread_provided,
                    "an insufficient level of thread support. "
                    "At least MPI_THREAD_SERIALIZED is required.");
        goto finalize;
    }

    // determine the calling processes rank and the total number of proecesses
    int world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
    int world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &world_size);

    // check if there are at least two processes
    if (world_size <= 1) {
        ecode = E_MPI;
        PRINT_ERROR(ecode, 0, PACKAGE_NAME " requires at least 2 copies of the program to run "
                    "(one for the master and the others for the slaves). "
                    "Did you use MPI process manager, e.g., mpiexec, mpirun, ...?");
        goto finalize;
    }

    // parse command line arguments in all processes
    struct cmdline_args args = { 0 };
    ecode = arg_parse(&args, argc, argv, world_rank == ROOT_PROC);
    if (ecode != E_OK) {
        goto finalize;
    }

    // split master and slave code
    if (world_rank == ROOT_PROC) {
        ecode = master_main(world_size, &args);
    } else {
        ecode = slave_main(world_size, &args);
    }

finalize:
    if (ecode == E_OK || ecode == E_HELP) {
        PRINT_DEBUG("terminating with success");
        MPI_Finalize();
        return EXIT_SUCCESS;
    } else {
        PRINT_DEBUG("terminating MPI execution environment due to an error");
        MPI_Abort(MPI_COMM_WORLD, ecode);
    }
}
