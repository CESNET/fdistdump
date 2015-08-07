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


/* Global variables. */
extern MPI_Datatype mpi_struct_agg_param;
extern MPI_Datatype mpi_struct_shared_task_ctx;
extern MPI_Datatype mpi_struct_tm;
extern int secondary_errno;


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
                print_err(E_INTERNAL, 0, "addr_to_str()");
                return E_MEM;
        }
        ret = addr_to_str(brec->dstaddr, dstaddr_str, INET6_ADDRSTRLEN);
        if (ret == NULL) {
                print_err(E_INTERNAL, 0, "addr_to_str()");
                return E_MEM;
        }

        printf("%lu -> %lu\t", brec->first, brec->last);
        printf("%15s:%-5hu -> %15s:%-5hu\t", srcaddr_str, brec->srcport,
                        dstaddr_str, brec->dstport);
        printf("%lu\t%lu\t%lu\n", brec->pkts, brec->bytes, brec->flows);

        return E_OK;
}


void print_err(error_code_t prim_errno, int sec_errno,
                const char *format, ...)
{
        va_list arg_list;
        char proc_name[MPI_MAX_PROCESSOR_NAME];
        int world_rank, world_size, result_len;

        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);
        MPI_Get_processor_name(proc_name, &result_len);
        va_start(arg_list, format);

        fprintf(stderr, "%d:%d\t", prim_errno, sec_errno);
        fprintf(stderr, "Error (rank %d/%d, name %s): ",
                        world_rank, world_size, proc_name);
        vfprintf(stderr, format, arg_list);
        fprintf(stderr, "\n");

        va_end(arg_list);
}


void print_warn(error_code_t prim_errno, int sec_errno,
                const char *format, ...)
{
        va_list arg_list;
        char proc_name[MPI_MAX_PROCESSOR_NAME];
        int world_rank, world_size, result_len;

        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);
        MPI_Get_processor_name(proc_name, &result_len);
        va_start(arg_list, format);

        fprintf(stderr, "%d:%d\t", prim_errno, sec_errno);
        fprintf(stderr, "Warning (rank %d/%d, name %s): ",
                        world_rank, world_size, proc_name);
        vfprintf(stderr, format, arg_list);
        fprintf(stderr, "\n");

        va_end(arg_list);
}


void create_mpi_struct_agg_param(void)
{
        int block_lengths[STRUCT_AGG_PARAM_ELEMS] = {1, 1, 1, 1 /*, NEW */};
        MPI_Aint displacements[STRUCT_AGG_PARAM_ELEMS];
        MPI_Datatype types[STRUCT_AGG_PARAM_ELEMS] = {MPI_INT, MPI_INT, MPI_INT,
                MPI_INT /*, NEW */};

        displacements[0] = offsetof(struct agg_param, field);
        displacements[1] = offsetof(struct agg_param, flags);
        displacements[2] = offsetof(struct agg_param, numbits);
        displacements[3] = offsetof(struct agg_param, numbits6);
        /* displacements[NEW] = offsetof(struct agg_param, NEW); */

        MPI_Type_create_struct(STRUCT_AGG_PARAM_ELEMS, block_lengths,
                        displacements, types, &mpi_struct_agg_param);
        MPI_Type_commit(&mpi_struct_agg_param);
}


void free_mpi_struct_agg_param(void)
{
        MPI_Type_free(&mpi_struct_agg_param);
}


void create_mpi_struct_tm(void)
{
        MPI_Type_contiguous(STRUCT_TM_ELEMS, MPI_INT, &mpi_struct_tm);
        MPI_Type_commit(&mpi_struct_tm);
}


void free_mpi_struct_tm(void)
{
        MPI_Type_free(&mpi_struct_tm);
}


void create_mpi_struct_shared_task_ctx(void)
{
        int block_lengths[STRUCT_TASK_INFO_ELEMS] = {1, MAX_AGG_PARAMS, 1, 1, 1,
                1, 1, 1, 1 /*, NEW */};
        MPI_Aint displacements[STRUCT_TASK_INFO_ELEMS];
        MPI_Datatype types[STRUCT_TASK_INFO_ELEMS] = {MPI_INT,
                mpi_struct_agg_param, MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG,
                MPI_UNSIGNED_LONG, MPI_UNSIGNED_LONG, mpi_struct_tm,
                mpi_struct_tm, MPI_C_BOOL /*, NEW */};

        displacements[0] = offsetof(struct shared_task_ctx, working_mode);
        displacements[1] = offsetof(struct shared_task_ctx, agg_params);
        displacements[2] = offsetof(struct shared_task_ctx, agg_params_cnt);
        displacements[3] = offsetof(struct shared_task_ctx, filter_str_len);
        displacements[4] = offsetof(struct shared_task_ctx, path_str_len);
        displacements[5] = offsetof(struct shared_task_ctx, rec_limit);
        displacements[6] = offsetof(struct shared_task_ctx, interval_begin);
        displacements[7] = offsetof(struct shared_task_ctx, interval_end);
        displacements[8] = offsetof(struct shared_task_ctx, use_fast_topn);
        /* displacements[NEW] = offsetof(struct shared_task_ctx, NEW); */

        MPI_Type_create_struct(STRUCT_TASK_INFO_ELEMS, block_lengths,
                        displacements, types, &mpi_struct_shared_task_ctx);
        MPI_Type_commit(&mpi_struct_shared_task_ctx);
}


void free_mpi_struct_shared_task_ctx(void)
{
        MPI_Type_free(&mpi_struct_shared_task_ctx);
}


int mem_setup(lnf_mem_t *mem, const struct agg_param *ap, size_t ap_cnt)
{
        /* Default aggragation fields: first, last, flows, packets, bytes. */
        secondary_errno = lnf_mem_fastaggr(mem, LNF_FAST_AGGR_BASIC);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_mem_fastaggr()");
                return E_LNF;
        }

        for (size_t i = 0; i < ap_cnt; ++i, ++ap) {
                secondary_errno = lnf_mem_fadd(mem, ap->field, ap->flags,
                                ap->numbits, ap->numbits6);
                if (secondary_errno != LNF_OK) {
                        print_err(E_LNF, secondary_errno, "lnf_mem_fadd()");
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
                print_err(E_LNF, ret, "lnf_rec_init()");
                return E_LNF;
        }

        for (ret = lnf_mem_read(mem, rec); ret == LNF_OK;
                        ret = lnf_mem_read(mem, rec)) {
                ret = lnf_rec_fget(rec, LNF_FLD_BREC1, &brec);
                if (ret != LNF_OK) {
                        print_err(E_LNF, ret, "lnf_rec_fget()");
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


double diff_tm(struct tm end_tm, struct tm begin_tm)
{
        time_t begin_time_t, end_time_t;

        begin_time_t = mktime(&begin_tm);
        end_time_t = mktime(&end_tm);

        if (begin_time_t == -1 || end_time_t == -1) {
                print_err(E_INTERNAL, 0, "mktime()");
                return NAN;
        }

         return difftime(end_time_t, begin_time_t);
}
