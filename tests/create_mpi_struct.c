/**
 * \file create_mpi_struct.c
 * \brief Test of creation of custom MPI structures.
 *
 * This tests creation of custom MPI structures used for communication in
 * FDistDump. Structures are created and filled by testing data TODO.
 *
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

#include "test_common.h"
#include "../src/common.h"

#include <stdio.h>
#include <stdlib.h>

#include <mpi.h>

MPI_Datatype mpi_struct_agg_param;
MPI_Datatype mpi_struct_shared_task_ctx;
MPI_Datatype mpi_struct_tm;
int secondary_errno; // unused, declared for ../src/common.c

int main(int argc, char **argv)
{
        int world_rank;
        int world_size;
        int state = TE_OK;

        MPI_Init(&argc, &argv);
        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);

//        create_mpi_struct_agg_param();
//        create_mpi_struct_tm();
        create_mpi_struct_shared_task_ctx();

        free_mpi_struct_shared_task_ctx();
//        free_mpi_struct_tm();
//        free_mpi_struct_agg_param();

        MPI_Finalize();

        return state;
}
