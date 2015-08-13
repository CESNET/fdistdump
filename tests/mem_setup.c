/**
 * \file mem_setup.c
 * \brief LibNF memory setup test.
 *
 * This tests lnf memory setup. Memory setup is passed through program
 * arguments and is done twice, once for normal mode and once for list mode.
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
#include "../src/arg_parse.h"

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

        // master process only...
        if (world_rank == 0) {
                struct cmdline_args args = {0};

                lnf_mem_t *mem;

                if (arg_parse(&args, argc, argv) != E_OK){
                        state = TE_ERR;
                        goto done;
                }

                for (int i = 0; i < 2; ++i){ // try normal and list mode
                        if(lnf_mem_init(&mem) != LNF_OK){
                                state = TE_ERR;
                                log_error("mem_setup: memory  init.");
                                goto done;
                        }

                        if(i == 0){
                                if (lnf_mem_setopt(mem, LNF_OPT_LISTMODE, NULL,
                                                   0) != LNF_OK){
                                            state = TE_ERR;
                                            log_error(
                                                "mem_setup: setopt list-mode.");
                                            goto done;
                                }
                        }

                        if (mem_setup(mem, args.agg_params, args.agg_params_cnt)
                            != E_OK){
                                log_error("mem_setup: memory setup.");
                                goto done;
                        }


                        lnf_mem_free(mem);
                }
        }

done:
        MPI_Finalize();

        return state;
}
