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
#include <math.h> //NAN

#include <mpi.h>
#include <arpa/inet.h> //inet_ntop()


/* Global MPI data types. */
extern MPI_Datatype agg_params_mpit, task_info_mpit, struct_tm_mpit;


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

        assert(ret != NULL);

        return buff;
}


int print_brec(const lnf_brec1_t *brec)
{
        char *ret;

        static char srcaddr_str[INET6_ADDRSTRLEN];
        static char dstaddr_str[INET6_ADDRSTRLEN];

        ret = addr_to_str(brec->srcaddr, srcaddr_str, INET6_ADDRSTRLEN);
        if (ret == NULL) {
                print_err("Internal - addr_to_str()");
                return E_MEM;
        }
        ret = addr_to_str(brec->dstaddr, dstaddr_str, INET6_ADDRSTRLEN);
        if (ret == NULL) {
                print_err("Internal - addr_to_str()");
                return E_MEM;
        }

        printf("%lu -> %lu\t", brec->first, brec->last);
        printf("%15s:%-5hu -> %15s:%-5hu\t", srcaddr_str, brec->srcport,
                        dstaddr_str, brec->dstport);
        printf("%lu\t%lu\t%lu\n", brec->pkts, brec->bytes, brec->flows);

        return E_OK;
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


void create_agg_params_mpit(void)
{
        int block_lengths[AGG_PARAMS_T_ELEMS] = {1, 1, 1, 1};
        MPI_Aint displacements[AGG_PARAMS_T_ELEMS];
        MPI_Datatype types[AGG_PARAMS_T_ELEMS] = {MPI_INT, MPI_INT, MPI_INT,
                MPI_INT};

        displacements[0] = offsetof(struct agg_params, field);
        displacements[1] = offsetof(struct agg_params, flags);
        displacements[2] = offsetof(struct agg_params, numbits);
        displacements[3] = offsetof(struct agg_params, numbits6);

        MPI_Type_create_struct(AGG_PARAMS_T_ELEMS, block_lengths, displacements,
                        types, &agg_params_mpit);
        MPI_Type_commit(&agg_params_mpit);
}


void free_agg_params_mpit(void)
{
        MPI_Type_free(&agg_params_mpit);
}


void create_struct_tm_mpit(void)
{
        MPI_Type_contiguous(STRUCT_TM_ELEMS, MPI_INT, &struct_tm_mpit);
        MPI_Type_commit(&struct_tm_mpit);
}


void free_struct_tm_mpit(void)
{
        MPI_Type_free(&struct_tm_mpit);
}


void create_task_info_mpit(void)
{
        int block_lengths[TASK_INFO_T_ELEMS] = {1, MAX_AGG_PARAMS, 1, 1, 1,
                1, 1, 1, 1, 1};
        MPI_Aint displacements[TASK_INFO_T_ELEMS];
        MPI_Datatype types[TASK_INFO_T_ELEMS] = {MPI_INT, agg_params_mpit,
                MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG,
                MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG, struct_tm_mpit,
                struct_tm_mpit, MPI_C_BOOL};

        displacements[0] = offsetof(task_info_t, working_mode);
        displacements[1] = offsetof(task_info_t, agg_params);
        displacements[2] = offsetof(task_info_t, agg_params_cnt);
        displacements[3] = offsetof(task_info_t, filter_str_len);
        displacements[4] = offsetof(task_info_t, path_str_len);
        displacements[5] = offsetof(task_info_t, rec_limit);
        displacements[6] = offsetof(task_info_t, slave_cnt);
        displacements[7] = offsetof(task_info_t, interval_begin);
        displacements[8] = offsetof(task_info_t, interval_end);
        displacements[9] = offsetof(task_info_t, use_fast_topn);

        MPI_Type_create_struct(TASK_INFO_T_ELEMS, block_lengths,
                        displacements, types, &task_info_mpit);
        MPI_Type_commit(&task_info_mpit);
}


void free_task_info_mpit(void)
{
        MPI_Type_free(&task_info_mpit);
}


int mem_setup(lnf_mem_t *mem, const struct agg_params *ap, size_t ap_cnt)
{
        int ret;

        /* Default aggragation fields: first, last, flows, packets, bytes. */
        ret = lnf_mem_fastaggr(mem, LNF_FAST_AGGR_BASIC);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_mem_fastaggr() returned %d", ret);
                return E_LNF;
        }

        for (size_t i = 0; i < ap_cnt; ++i, ++ap) {
                ret = lnf_mem_fadd(mem, ap->field, ap->flags, ap->numbits,
                                ap->numbits6);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_mem_fadd() error");
                        return E_LNF;
                }
        }

        return E_OK;
}


int mem_print(lnf_mem_t *mem, size_t limit)
{
        int ret, err = E_OK;
        size_t rec_cntr = 0;
        lnf_rec_t *rec;
        lnf_brec1_t brec;

        ret = lnf_rec_init(&rec);
        if (ret != LNF_OK) {
                print_err("LNF - lnf_rec_init()");
                return E_LNF;
        }

        for (ret = lnf_mem_read(mem, rec); ret == LNF_OK;
                        ret = lnf_mem_read(mem, rec)) {
                ret = lnf_rec_fget(rec, LNF_FLD_BREC1, &brec);
                if (ret != LNF_OK) {
                        print_err("LNF - lnf_rec_fget()");
                        err = E_LNF;
                        break;
                }

                print_brec(&brec);
                rec_cntr++;

                if (rec_cntr == limit) {
                        break;
                }
        }

        lnf_rec_free(rec);

        return err;
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


double diff_tm(struct tm end_tm, struct tm begin_tm)
{
        time_t begin_time_t, end_time_t;

        begin_time_t = mktime(&begin_tm);
        end_time_t = mktime(&end_tm);

        if (begin_time_t == -1 || end_time_t == -1) {
                printf("mktime() error\n");
                return NAN;
        }

         return difftime(end_time_t, begin_time_t);
}

