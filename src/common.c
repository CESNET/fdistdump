/**
 * \file common.c
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

#include "common.h"

#include <stdio.h>
#include <stdarg.h> //variable argument list
#include <assert.h>
#include <stddef.h> //offsetof()

#include <mpi.h>
#include <arpa/inet.h> //inet_ntop()


/** \brief Convert libnf address to string.
 *
 * Distinguish IPv4 vs IPv6 address and use inet_ntop() to convert binary
 * representation to string. In case of small buffer, NULL is returned.
 *
 * \param[in] addr Binary address representation.
 * \param[out] buff String address representation.
 * \param[in] buff_len Buffer size.
 * \return String address representation.
 */
static char* addr_to_str(const lnf_ip_t addr, char *buff, size_t buff_size)
{
        const char *ret;

        if (addr.data[0]) { //IPv6
                if (buff_size < INET6_ADDRSTRLEN) {
                        return NULL;
                }
                ret = inet_ntop(AF_INET6, &addr, buff, buff_size);
        } else { //IPv4
                if (buff_size < INET_ADDRSTRLEN) {
                        return NULL;
                }
                ret = inet_ntop(AF_INET, &addr.data[3], buff, buff_size);
        }

        assert(ret != NULL); //shouldn't happen

        return buff;
}


void print_brec(const lnf_brec1_t brec)
{
        static char srcaddr_str[INET6_ADDRSTRLEN];
        static char dstaddr_str[INET6_ADDRSTRLEN];

        addr_to_str(brec.srcaddr, srcaddr_str, INET6_ADDRSTRLEN);
        addr_to_str(brec.dstaddr, dstaddr_str, INET6_ADDRSTRLEN);

        printf("%lu -> %lu\t", brec.first, brec.last);
        printf("%15s:%-5hu -> %15s:%-5hu\t", srcaddr_str, brec.srcport,
                        dstaddr_str, brec.dstport);
        printf("%lu\t%lu\t%lu\n", brec.pkts, brec.bytes, brec.flows);
}


void print_err(const char *format, ...)
{
        va_list arg_list;
        char proc_name[MPI_MAX_PROCESSOR_NAME];
        int world_rank, world_size, result_len;

        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);
        MPI_Get_processor_name(proc_name, &result_len);
        va_start(arg_list, format);

        fprintf(stderr, "Error (rank %d/%d, name %s): ",
                        world_rank, world_size, proc_name);
        vfprintf(stderr, format, arg_list);
        fprintf(stderr, "\n");

        va_end(arg_list);
}


void create_agg_params_mpit(MPI_Datatype *agg_params_mpit)
{
        int block_lengths[AGG_PARAMS_T_ELEMS] = {1, 1, 1, 1};
        MPI_Aint displacements[AGG_PARAMS_T_ELEMS];
        MPI_Datatype types[AGG_PARAMS_T_ELEMS] = {MPI_INT, MPI_INT, MPI_INT,
                MPI_INT};

        displacements[0] = offsetof(agg_params_t, field);
        displacements[1] = offsetof(agg_params_t, flags);
        displacements[2] = offsetof(agg_params_t, numbits);
        displacements[3] = offsetof(agg_params_t, numbits6);

        MPI_Type_create_struct(AGG_PARAMS_T_ELEMS, block_lengths, displacements,
                        types, agg_params_mpit);
        MPI_Type_commit(agg_params_mpit);
}

void free_agg_params_mpit(MPI_Datatype *agg_params_mpit)
{
        MPI_Type_free(agg_params_mpit);
}

void create_task_info_mpit(MPI_Datatype *task_info_mpit,
                MPI_Datatype agg_params_mpit)
{
        int block_lengths[INITIAL_INTO_T_ELEMS] = {1, MAX_AGG_PARAMS, 1, 1, 1,
                1, 1};
        MPI_Aint displacements[INITIAL_INTO_T_ELEMS];
        MPI_Datatype types[INITIAL_INTO_T_ELEMS] = {MPI_INT, agg_params_mpit,
                MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG,
                MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG};

        displacements[0] = offsetof(task_info_t, working_mode);
        displacements[1] = offsetof(task_info_t, agg_params);
        displacements[2] = offsetof(task_info_t, agg_params_cnt);
        displacements[3] = offsetof(task_info_t, filter_str_len);
        displacements[4] = offsetof(task_info_t, path_str_len);
        displacements[5] = offsetof(task_info_t, rec_limit);
        displacements[6] = offsetof(task_info_t, slave_cnt);

        MPI_Type_create_struct(INITIAL_INTO_T_ELEMS, block_lengths,
                        displacements, types, task_info_mpit);
        MPI_Type_commit(task_info_mpit);
}

void free_task_info_mpit(MPI_Datatype *task_info_mpit)
{
        MPI_Type_free(task_info_mpit);
}

int agg_init(lnf_mem_t **agg, const agg_params_t *agg_params,
                size_t agg_params_cnt)
{
        int ret;

        ret = lnf_mem_init(agg);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_init() returned %d", ret);
                return E_LNF;
        }

        /* Default aggragation fields: first, last, flows, packets, bytes. */
        ret = lnf_mem_fastaggr(*agg, LNF_FAST_AGGR_BASIC);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_fastaggr() returned %d", ret);
                lnf_mem_free(*agg);
                return E_LNF;
        }

        for (size_t i = 0; i < agg_params_cnt; ++i) {
                const agg_params_t *ap = agg_params + i; //shortcut

                ret = lnf_mem_fadd(*agg, ap->field, ap->flags, ap->numbits,
                                ap->numbits6);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_fadd() error");
                        lnf_mem_free(*agg);
                        return E_LNF;
                }
        }

        return E_OK;
}

/* Prepare statistics memory structure */
int stats_init(lnf_mem_t **stats)
{
        int ret;

        ret = lnf_mem_init(stats);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_init() returned %d (stats).", ret);
                return E_LNF;
        }

        ret = lnf_mem_fadd(stats, LNF_FLD_AGGR_FLOWS, LNF_AGGR_SUM, 0, 0);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_fadd() error (statistics, flows).");
                lnf_mem_free(*stats);
                return E_LNF;
        }

        lnf_mem_fadd(stats, LNF_FLD_DPKTS, LNF_AGGR_SUM, 0, 0);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_fadd() error (statistics, packets).");
                lnf_mem_free(*stats);
                return E_LNF;
        }

        lnf_mem_fadd(stats, LNF_FLD_DOCTETS, LNF_AGGR_SUM, 0, 0);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_fadd() error (statistics, bytes).");
                lnf_mem_free(*stats);
                return E_LNF;
        }

        return E_OK;
}
