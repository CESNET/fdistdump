/**
 * \file common.c
 * \brief Implementation of common fdistdump functionality.
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

#define MAX_PROC_INFO_NAME (MPI_MAX_PROCESSOR_NAME + 100)
#define TM_YEAR_BASE 1900

/**
 * \defgroup glob_var Global variables
 * @{
 */
extern MPI_Datatype mpi_struct_shared_task_ctx;
extern int secondary_errno;
/**
 * @}
 */ //glob_var


/**
 * \defgroup to_str_func Various elements to string converting functions
 * @{
 */
/** \brief Convert processor name and role to string.
 *
 * Return static string containing MPI processor name, role (master or slave)
 * and MPI rank.
 *
 * \return Static string at most MAX_PROC_INFO_NAME long.
 */
static char * proc_info_to_str(void)
{
        static char msg[MAX_PROC_INFO_NAME];
        char proc_name[MPI_MAX_PROCESSOR_NAME];
        int world_rank, world_size, result_len;

        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        MPI_Comm_size(MPI_COMM_WORLD, &world_size);
        MPI_Get_processor_name(proc_name, &result_len);

        snprintf(msg, MAX_PROC_INFO_NAME, "%d/%d, %s", world_rank, world_size,
                        proc_name);

        return msg;
}

/** \brief Convert error_code_t error code to human-readable string.
 *
 * \return Static string at most MAX_STR_LEN long.
 */
static char * error_code_to_str(error_code_t prim_errno)
{
        static char msg[MAX_STR_LEN];

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

char * working_mode_to_str(working_mode_t working_mode)
{
        static char msg[MAX_STR_LEN];

        switch (working_mode) {
        case MODE_LIST:
                snprintf(msg, MAX_STR_LEN, "list records");
                break;

        case MODE_SORT:
                snprintf(msg, MAX_STR_LEN, "sort records");
                break;

        case MODE_AGGR:
                snprintf(msg, MAX_STR_LEN, "aggregate records");
                break;

        case MODE_META:
                snprintf(msg, MAX_STR_LEN, "metadata only");
                break;

        case MODE_PASS:
                snprintf(msg, MAX_STR_LEN, "pass");
                break;

        default:
                assert(!"unknown working mode");
        }

        return msg;
}
/**
 * @}
 */ //to_str_func


/**
 * \defgroup print_func Error/warning/debug printing functions
 * @{
 */
void print_err(error_code_t prim_errno, int sec_errno,
                const char *format, ...)
{
        (void)sec_errno; //TODO
        va_list arg_list;
        va_start(arg_list, format);
        char lnf_error_str[LNF_MAX_STRING];

        fprintf(stderr, "Error on %s caused by %s: ", proc_info_to_str(),
                        error_code_to_str(prim_errno));
        vfprintf(stderr, format, arg_list);

        if (prim_errno == E_LNF && secondary_errno == LNF_ERR_OTHER_MSG) {
                lnf_error(lnf_error_str, LNF_MAX_STRING);
                fprintf(stderr, "\nLNF error string: %s", lnf_error_str);
        }

        fprintf(stderr, "\n");

        va_end(arg_list);
}

void print_warn(error_code_t prim_errno, int sec_errno,
                const char *format, ...)
{
        (void)sec_errno; //TODO
        va_list arg_list;
        va_start(arg_list, format);

        fprintf(stderr, "Warning on %s caused by %s: ", proc_info_to_str(),
                        error_code_to_str(prim_errno));
        vfprintf(stderr, format, arg_list);
        fprintf(stderr, "\n");

        va_end(arg_list);
}

void print_debug(const char *format, ...)
{
#ifdef DEBUG
        va_list arg_list;
        va_start(arg_list, format);

        fprintf(stderr, "DEBUG on %s: ", proc_info_to_str());
        vfprintf(stderr, format, arg_list);
        fprintf(stderr, "\n");

        va_end(arg_list);
#else //DEBUG
        (void)format;
#endif //DEBUG
}
/**
 * @}
 */ //print_func


/**
 * \defgroup mpi_type_func Functions constructing/destructing MPI data types
 * @{
 */
#define STRUCT_TM_ELEMS 9
void create_mpi_struct_shared_task_ctx(void)
{
        int block_lengths[STRUCT_SHARED_TASK_CTX_ELEMS] = {
                1, //working_mode
                (STRUCT_FIELD_INFO_ELEMS * LNF_FLD_TERM_), //fields
                1, //filter_str_len
                1, //path_str_len
                1, //rec_limit
                STRUCT_TM_ELEMS, //time_begin
                STRUCT_TM_ELEMS, //time_end
                1, //use_fast_topn
                /* NEW */
        };
        MPI_Aint displacements[STRUCT_SHARED_TASK_CTX_ELEMS] = {
                offsetof(struct shared_task_ctx, working_mode),
                offsetof(struct shared_task_ctx, fields),
                offsetof(struct shared_task_ctx, filter_str_len),
                offsetof(struct shared_task_ctx, path_str_len),
                offsetof(struct shared_task_ctx, rec_limit),
                offsetof(struct shared_task_ctx, time_begin),
                offsetof(struct shared_task_ctx, time_end),
                offsetof(struct shared_task_ctx, use_fast_topn),
                /* offsetof(struct shared_task_ctx, NEW), */
        };
        MPI_Datatype types[STRUCT_SHARED_TASK_CTX_ELEMS] = {
                MPI_INT, //working_mode
                MPI_INT, //fields
                MPI_UNSIGNED_LONG, //filter_str_len
                MPI_UNSIGNED_LONG, //path_str_len
                MPI_UNSIGNED_LONG, //rec_limit
                MPI_INT, //time_begin
                MPI_INT, //time_end
                MPI_C_BOOL, //use_fast_topn
                /* NEW */
        };

        MPI_Type_create_struct(STRUCT_SHARED_TASK_CTX_ELEMS, block_lengths,
                        displacements, types, &mpi_struct_shared_task_ctx);
        MPI_Type_commit(&mpi_struct_shared_task_ctx);
}

void free_mpi_struct_shared_task_ctx(void)
{
        MPI_Type_free(&mpi_struct_shared_task_ctx);
}
/**
 * @}
 */ //mpi_type_func


/**
 * \defgroup aggr_mem Functions operating with aggregation memory
 * @{
 */
error_code_t init_aggr_mem(lnf_mem_t **mem, const struct field_info *fields)
{
        secondary_errno = lnf_mem_init(mem);
        if (secondary_errno != LNF_OK) {
                print_err(E_LNF, secondary_errno, "lnf_mem_init()");
                return E_LNF;
        }

        /* Is it possible to apply fast aggregation? */
        if (fields[LNF_FLD_FIRST].id &&
                        ((fields[LNF_FLD_FIRST].flags & LNF_AGGR_FLAGS) ==
                         LNF_AGGR_MIN) &&
                        fields[LNF_FLD_LAST].id &&
                        ((fields[LNF_FLD_LAST].flags & LNF_AGGR_FLAGS) ==
                         LNF_AGGR_MAX) &&
                        fields[LNF_FLD_DOCTETS].id &&
                        ((fields[LNF_FLD_DOCTETS].flags & LNF_AGGR_FLAGS) ==
                         LNF_AGGR_SUM) &&
                        fields[LNF_FLD_DPKTS].id &&
                        ((fields[LNF_FLD_DPKTS].flags & LNF_AGGR_FLAGS) ==
                         LNF_AGGR_SUM) &&
                        fields[LNF_FLD_AGGR_FLOWS].id &&
                        ((fields[LNF_FLD_AGGR_FLOWS].flags & LNF_AGGR_FLAGS) ==
                         LNF_AGGR_SUM))
        {
                secondary_errno = lnf_mem_fastaggr(*mem, LNF_FAST_AGGR_BASIC);
                if (secondary_errno != LNF_OK) {
                        print_err(E_LNF, secondary_errno, "lnf_mem_fastaggr()");
                        free_aggr_mem(*mem);
                        *mem = NULL;
                        return E_LNF;
                }
        }

        /* Loop through the fields. */
        for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
                if (fields[i].id == 0) {
                        continue; //field is not present
                }

                if (fields[i].id >= LNF_FLD_CALC_BPS &&
                                fields[i].id <= LNF_FLD_CALC_BPP) {
                        continue;
                }

                secondary_errno = lnf_mem_fadd(*mem, fields[i].id,
                                fields[i].flags, fields[i].ipv4_bits,
                                fields[i].ipv6_bits);
                if (secondary_errno != LNF_OK) {
                        print_err(E_LNF, secondary_errno,
                                        "lnf_mem_fadd()");
                        free_aggr_mem(*mem);
                        *mem = NULL;
                        return E_LNF;
                }
        }

        return E_OK;
}

void free_aggr_mem(lnf_mem_t *mem)
{
        lnf_mem_free(mem);
}
/**
 * @}
 */ //aggr_mem


/**
 * \defgroup time_func Time related functions
 * @{
 */
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
        char orig_tz[128];
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
/**
 * @}
 */ //time_func


/**
 * \defgroup lnf_fields_func LNF fields related functions
 * @{
 */
int field_get_type(int field)
{
        int type;

        if (field <= LNF_FLD_ZERO_ || field >= LNF_FLD_TERM_) {
                return -1;
        }

        lnf_fld_info(field, LNF_FLD_INFO_TYPE, &type, sizeof (type));

        return type;
}

size_t field_get_size(int field)
{
        const int type = field_get_type(field);

        if (type == -1) {
                return 0;
        }

        switch (type) {
        case LNF_UINT8:
                return sizeof (uint8_t);

        case LNF_UINT16:
                return sizeof (uint16_t);

        case LNF_UINT32:
                return sizeof (uint32_t);

        case LNF_UINT64:
                return sizeof (uint64_t);

        case LNF_DOUBLE:
                return sizeof (double);

        case LNF_ADDR:
                return sizeof (lnf_ip_t);

        case LNF_MAC:
                return sizeof (lnf_mac_t);

        case LNF_BASIC_RECORD1:
                return sizeof (lnf_brec1_t);

        case LNF_NONE:
        case LNF_STRING:
        case LNF_MPLS:
                assert(!"unimplemented LNF data type");

        default:
                assert(!"unknown LNF data type");
        }
}
/**
 * @}
 */ //lnf_fields_func
