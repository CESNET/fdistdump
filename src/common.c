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
#include <assert.h>
#include <string.h>
#include <stdarg.h> //variable argument list
#include <stddef.h> //offsetof()

#include <mpi.h>
#include <arpa/inet.h> //inet_ntop()

#define MAX_MSG_LEN (MPI_MAX_PROCESSOR_NAME + 100)
#define TM_YEAR_BASE 1900

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

        if (IN6_IS_ADDR_V4COMPAT(addr.data)) { //IPv4
                if (buff_size < INET_ADDRSTRLEN) {
                        return NULL;
                }
                ret = inet_ntop(AF_INET, &addr.data[3], buff, buff_size);
        } else { //IPv6
                if (buff_size < INET6_ADDRSTRLEN) {
                        return NULL;
                }
                ret = inet_ntop(AF_INET6, &addr, buff, buff_size);
        }

        assert(ret != NULL);

        return buff;
}


static char * get_processor_info(void)
{
        static char msg[MAX_MSG_LEN];
        size_t msg_offset = 0;
        char proc_name[MPI_MAX_PROCESSOR_NAME];
        int world_rank, world_size, result_len;

        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);
        MPI_Get_processor_name(proc_name, &result_len);

        if (world_rank == ROOT_PROC) {
                msg_offset += snprintf(msg, MAX_FN_LEN, "master, ");
        } else {
                msg_offset += snprintf(msg, MAX_FN_LEN, "slave, ");
        }

        snprintf(msg + msg_offset, MAX_MSG_LEN - msg_offset,
                        "rank %d/%d with processor name %s", world_rank,
                        world_size, proc_name);

        return msg;
}


static char * error_code_to_str(error_code_t prim_errno)
{
        static char msg[100];

        switch (prim_errno) {
        case E_OK:
        case E_PASS:
        case E_EOF:
                sprintf(msg, "no error");
                break;

        case E_MEM:
                sprintf(msg, "memory");
                break;

        case E_MPI:
                sprintf(msg, "MPI");
                break;

        case E_LNF:
                sprintf(msg, "LNF");
                break;

        case E_INTERNAL:
                sprintf(msg, "internal");
                break;

        case E_ARG:
                sprintf(msg, "command line argument");
                break;

        case E_PATH:
                sprintf(msg, "path");
                break;

        default:
                assert(!"unknown error code");
        };

        return msg;
}


error_code_t print_brec(const lnf_brec1_t *brec)
{
        char *ret;

        static char srcaddr_str[INET6_ADDRSTRLEN];
        static char dstaddr_str[INET6_ADDRSTRLEN];

        ret = addr_to_str(brec->srcaddr, srcaddr_str, INET6_ADDRSTRLEN);
        if (ret == NULL) {
                print_err(E_INTERNAL, 0, "addr_to_str()");
                return E_INTERNAL;
        }
        ret = addr_to_str(brec->dstaddr, dstaddr_str, INET6_ADDRSTRLEN);
        if (ret == NULL) {
                print_err(E_INTERNAL, 0, "addr_to_str()");
                return E_INTERNAL;
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
        (void)sec_errno; //TODO
        va_list arg_list;
        va_start(arg_list, format);

        fprintf(stderr, "Error on %s caused by %s: ", get_processor_info(),
                        error_code_to_str(prim_errno));
        vfprintf(stderr, format, arg_list);
        fprintf(stderr, "\n");

        va_end(arg_list);
}


void print_warn(error_code_t prim_errno, int sec_errno,
                const char *format, ...)
{
        (void)sec_errno; //TODO
        va_list arg_list;
        va_start(arg_list, format);

        fprintf(stderr, "Warning on %s caused by %s: ", get_processor_info(),
                        error_code_to_str(prim_errno));
        vfprintf(stderr, format, arg_list);
        fprintf(stderr, "\n");

        va_end(arg_list);
}

#ifdef DEBUG
void print_debug(const char *format, ...)
{
        va_list arg_list;
        va_start(arg_list, format);

        fprintf(stderr, "DEBUG on %s: ", get_processor_info());
        vfprintf(stderr, format, arg_list);
        fprintf(stderr, "\n");

        va_end(arg_list);
}
#else
void print_debug(const char *format, ...)
{
        (void)format;
}
#endif


char * working_mode_to_str(working_mode_t working_mode)
{
        static char msg[MAX_MSG_LEN];

        switch (working_mode) {
        case MODE_LIST:
                snprintf(msg, MAX_MSG_LEN, "list records");
                break;

        case MODE_SORT:
                snprintf(msg, MAX_MSG_LEN, "sort records");
                break;

        case MODE_AGGR:
                snprintf(msg, MAX_MSG_LEN, "aggregate records");
                break;

        case MODE_PASS:
                snprintf(msg, MAX_MSG_LEN, "pass");
                break;

        default:
                assert(!"unknown working mode");
        }

        return msg;
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


error_code_t mem_setup(lnf_mem_t *mem, const struct agg_param *ap,
                size_t ap_cnt)
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


error_code_t mem_print(lnf_mem_t *mem, size_t limit)
{
        error_code_t primary_errno = E_OK;
        size_t rec_cntr = 0;
        lnf_rec_t *rec;
        lnf_brec1_t brec;

        secondary_errno = lnf_rec_init(&rec);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_rec_init()");
                return E_LNF;
        }

        secondary_errno = lnf_mem_read(mem, rec); //read first
        while (secondary_errno == LNF_OK) {
                secondary_errno = lnf_rec_fget(rec, LNF_FLD_BREC1, &brec);
                if (secondary_errno != LNF_OK) {
                        primary_errno = E_LNF;
                        print_err(primary_errno, secondary_errno,
                                        "lnf_rec_fget()");
                        goto free_lnf_rec;
                }

                print_brec(&brec);

                if (++rec_cntr == limit) {
                        goto free_lnf_rec;
                }

                secondary_errno = lnf_mem_read(mem, rec); //read next
        }
        if (secondary_errno != LNF_EOF) {
                primary_errno = E_LNF;
                print_err(primary_errno, secondary_errno, "lnf_mem_read()");
        }

free_lnf_rec:
        lnf_rec_free(rec);

        return primary_errno;
}


int tm_diff(const struct tm a, const struct tm b)
{
        int a4 = (a.tm_year >> 2) + (TM_YEAR_BASE >> 2) - ! (a.tm_year & 3);
        int b4 = (b.tm_year >> 2) + (TM_YEAR_BASE >> 2) - ! (b.tm_year & 3);
        int a100 = a4 / 25 - (a4 % 25 < 0);
        int b100 = b4 / 25 - (b4 % 25 < 0);
        int a400 = a100 >> 2;
        int b400 = b100 >> 2;
        int intervening_leap_days = (a4 - b4) - (a100 - b100) + (a400 - b400);
        int years = a.tm_year - b.tm_year;
        int days = (365 * years + intervening_leap_days +
                        (a.tm_yday - b.tm_yday));

        return (60 * (60 * (24 * days + (a.tm_hour - b.tm_hour)) +
                                (a.tm_min - b.tm_min)) + (a.tm_sec - b.tm_sec));
}


time_t mktime_utc(struct tm *tm)
{
        time_t ret;
        static char orig_tz[128];
        char *tz;

        /* Save current time zone environment variable. */
        tz = getenv("TZ");
        if (tz != NULL) {
                assert(strlen(tz) < 128);
                strncpy(orig_tz, tz, 128);
        }

        /* Set time zone to UTC. mktime() would be affected by daylight saving
         * otherwise.
         */
        setenv("TZ", "", 1);
        tzset();

        ret = mktime(tm); //actual normalization within UTC time zone

        /* Restore time zone to stored value. */
        if (tz != NULL) {
                setenv("TZ", orig_tz, 1);
        } else {
                unsetenv("TZ");
        }
        tzset();

        return ret;
}

/* Initialize memory for traffic volume statistics.*/
int init_statistics(lnf_mem_t **stat_mem_p)
{
        if(lnf_mem_init(stat_mem_p) != LNF_OK){
                return E_LNF;
        }


        if (lnf_mem_fadd(*stat_mem_p, LNF_FLD_AGGR_FLOWS, LNF_AGGR_SUM, 0, 0)
            != LNF_OK){
                free_statistics(stat_mem_p);
                return E_LNF;
        }
        if (lnf_mem_fadd(*stat_mem_p, LNF_FLD_DPKTS, LNF_AGGR_SUM, 0, 0)
            != LNF_OK){
                free_statistics(stat_mem_p);
                return E_LNF;
        }
        if (lnf_mem_fadd(*stat_mem_p, LNF_FLD_DOCTETS, LNF_AGGR_SUM, 0, 0)
            != LNF_OK){
                free_statistics(stat_mem_p);
                return E_LNF;
        }

        return E_OK;
}


/* Free statistics memory. */
void free_statistics(lnf_mem_t **stat_mem_p)
{
   lnf_mem_free(*stat_mem_p);
   *stat_mem_p = NULL;
}
