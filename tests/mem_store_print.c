/**
 * \file mem_store_print.c
 * \brief Storing records in memory and printing of memory.
 *
 * This tests storing flows in LibNF memory and printing of this memory. Testing
 * flow values are passed through program arguments along with count of copies
 * of this flow. All copies of given flow are then stored into lnf memory
 * structure. After that all records from memory are printed to stdout.
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
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <arpa/inet.h>

#include <libnf.h>
#include <mpi.h>

MPI_Datatype mpi_struct_agg_param;
MPI_Datatype mpi_struct_shared_task_ctx;
MPI_Datatype mpi_struct_tm;
int secondary_errno; // unused, declared for ../src/common.c

static int add_field(char *token, struct agg_param *agg_params,
                     int *agg_params_cnt){

        int fld, nb, nb6, agg;

        /* Parse token, return field. */
        fld = lnf_fld_parse(token, &nb, &nb6);
        if (fld == LNF_FLD_ZERO_ || fld == LNF_ERR_OTHER) {
                return TE_ERR;
        }
        if (nb < 0 || nb > 32 || nb6 < 0 || nb6 > 128) {
                return TE_ERR;
        }

        /* Lookup default aggregation key for field. */
        if (lnf_fld_info(fld, LNF_FLD_INFO_AGGR, &agg, sizeof (agg)) != LNF_OK){
            return TE_ERR;
        }

        agg_params[*agg_params_cnt].field = fld;
        agg_params[*agg_params_cnt].flags = agg;
        agg_params[*agg_params_cnt].numbits = nb;
        agg_params[*agg_params_cnt].numbits6 = nb6;

        *agg_params_cnt = *agg_params_cnt + 1;

        return TE_OK;
}

int main(int argc, char **argv)
{
        int world_rank;
        int world_size;
        int state = TE_OK;
        int rec_copy_cnt = 3;

        MPI_Init(&argc, &argv);
        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);

        if (world_rank == 0) {
                lnf_mem_t *mem;
                lnf_rec_t *recp;
                lnf_brec1_t brec;

                struct agg_param agg_params[5];
                int agg_param_cnt = 0;

                char opt;

                if (argc != 21 && argc != 23) {
                        log_error("mem_store_print: Wrong argument count.");
                        state = TE_ERR;
                }

                memset(brec.dstaddr.data, 0,
                       sizeof(brec.dstaddr.data[0]) * 4);
                memset(brec.srcaddr.data, 0,
                       sizeof(brec.srcaddr.data[0]) * 4);

                while ((opt = getopt(argc, argv, "c:s:S:d:D:f:p:b:P:t:T:")) != -1) {
                        switch (opt) {
                            case 'c':
                                    rec_copy_cnt = atoi(optarg);
                                    break;
                            case 's':
                                    inet_pton(AF_INET, optarg,
                                              &brec.srcaddr.data[3]);
                                    break;
                            case 'S':
                                    brec.srcport = (uint16_t) atoi(optarg);
                                    break;
                            case 'd':
                                    inet_pton(AF_INET, optarg,
                                              &brec.dstaddr.data[3]);
                                    break;
                            case 'D':
                                    brec.dstport = (uint16_t) atoi(optarg);
                                    break;
                            case 'f':
                                    brec.flows = strtoull(optarg, NULL, 0);
                                    break;
                            case 'p':
                                    brec.pkts = strtoull(optarg, NULL, 0);
                                    break;
                            case 'b':
                                    brec.bytes = strtoull(optarg, NULL, 0);
                                    break;
                            case 'P':
                                    brec.prot = (uint8_t) atoi(optarg);
                                    break;
                            case 't':
                                    brec.first = strtoull(optarg, NULL, 0);
                                    break;
                            case 'T':
                                    brec.last = strtoull(optarg, NULL, 0);
                                    break;
                            default:
                                    log_error("mem_store_print: " \
                                              "Wrong argument %c.",
                                              opt);
                                    state = TE_ERR;
                                    break;
                        }
                }

//                printf("%" PRIu64 " - %" PRIu64 ", %s:%u -> %s:%u, %i, %i\n",
//                       time_start, time_end, src_ip, src_port, dst_ip, dst_port,
//                       packets, bytes);

                if(lnf_mem_init(&mem) != LNF_OK){
                        state = TE_ERR;
                        log_error("mem_store_print: memory  init.");
                        goto done;
                }

                if(lnf_rec_init(&recp) != LNF_OK){
                        state = TE_ERR;
                        log_error("mem_store_print: rec init.");
                        goto done;
                }

                if (lnf_mem_setopt(mem, LNF_OPT_LISTMODE, NULL, 0) !=
                    LNF_OK){
                        state = TE_ERR;
                        log_error("mem_store_print: setopt list-mode.");
                        goto master_done;
                }

                if (add_field("srcip", agg_params, &agg_param_cnt) != TE_OK){
                        state = TE_ERR;
                        log_error("mem_store_print: add-field.");
                        goto master_done;
                }
                if (add_field("dstip", agg_params, &agg_param_cnt) != TE_OK){
                        state = TE_ERR;
                        log_error("mem_store_print: add-field.");
                        goto master_done;
                }
                if (add_field("srcport", agg_params, &agg_param_cnt) != TE_OK){
                        state = TE_ERR;
                        log_error("mem_store_print: add-field.");
                        goto master_done;
                }
                if (add_field("dstport", agg_params, &agg_param_cnt) != TE_OK){
                        state = TE_ERR;
                        log_error("mem_store_print: add-field.");
                        goto master_done;
                }
                if (add_field("proto", agg_params, &agg_param_cnt) != TE_OK){
                        state = TE_ERR;
                        log_error("mem_store_print: add-field.");
                        goto master_done;
                }

                if (mem_setup(mem, agg_params, agg_param_cnt) !=
                    E_OK){
                        log_error("mem_store_print: memory setup.");
                        goto done;
                }

                if (lnf_rec_fset(recp, LNF_FLD_BREC1, &brec) != LNF_OK) {
                        log_error("mem_store_print: rec-fset.");
                        state = TE_ERR;
                        goto master_done;
                }


                for (int i = 0; i < rec_copy_cnt; ++i){
                    if (lnf_mem_write(mem, recp) != LNF_OK) {
                            log_error("mem_store_print: rec-fset.");
                            state = TE_ERR;
                            goto master_done;
                    }
                }

                mem_print(mem, 0);
master_done:
                lnf_mem_free(mem);
                lnf_rec_free(recp);
        }

done:
        MPI_Finalize();

        return state;
}
