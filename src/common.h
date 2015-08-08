/**
 * \file common.h
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

#ifndef COMMON_H
#define COMMON_H

#include <stddef.h> //size_t
#include <time.h> //struct tm
#include <stdbool.h>

#include <libnf.h>
#include <mpi.h>

#define MAX_FN_LEN 2048
#define MAX_AGG_PARAMS 16 //maximum count of -a parameters

#define XCHG_BUFF_MAX_SIZE (1024 * 1024) //KiB
#define XCHG_BUFF_ELEMS (XCHG_BUFF_MAX_SIZE / sizeof(lnf_brec1_t))
#define XCHG_BUFF_SIZE (XCHG_BUFF_ELEMS * sizeof(lnf_brec1_t))

//TODO: move to configuration file and as parameter options
#define FLOW_FILE_ROTATION_INTERVAL 300 //seconds
#define FLOW_FILE_BASE_DIR "/data/profiles_data/"
#define FLOW_FILE_PROFILE "live"
#define FLOW_FILE_SOURCE "telia"
#define FLOW_FILE_PATH_FORMAT "%Y/%m/%d/"
#define FLOW_FILE_NAME_FORMAT "nfcapd.%Y%m%d%H%M"
#define FLOW_FILE_PATH (FLOW_FILE_BASE_DIR FLOW_FILE_PROFILE "/" \
                FLOW_FILE_SOURCE "/" FLOW_FILE_PATH_FORMAT \
                FLOW_FILE_NAME_FORMAT)


/* Enumerations. */
typedef enum { //error return codes
        E_OK, //no error
        E_MEM, //memory
        E_MPI, //MPI
        E_LNF, //libnf
        E_INTERNAL, //internal
        E_ARG, //command line arguments
        E_HELP, //print help
        E_EOF, //end of file
        E_PATH, //problem with access to file/directory
} error_code_t;

typedef enum { //working modes
        MODE_LIST, //list unmodified flow records
        MODE_SORT, //list ordered flow records
        MODE_AGGR, //aggregation and statistic
} working_mode_t;


/* Data types. */
//WATCH OUT: reflect changes also in mpi_struct_agg_param
#define STRUCT_AGG_PARAM_ELEMS 4
struct agg_param {
        int field;
        int flags;
        int numbits;
        int numbits6;
};

//WATCH OUT: reflect changes also in mpi_struct_shared_task_ctx
#define STRUCT_TASK_INFO_ELEMS 9
struct shared_task_ctx {
        working_mode_t working_mode; //working mode

        struct agg_param agg_params[MAX_AGG_PARAMS]; //aggregation pamrameters
        size_t agg_params_cnt; //aggregation parameters count

        size_t filter_str_len; //filter expression string length
        size_t path_str_len; //path string length

        size_t rec_limit; //record/aggregation limit

        struct tm interval_begin; //begin of time interval
        struct tm interval_end; //end of time interval

        bool use_fast_topn; //enables fast top-N algorithm
};

//WATCH OUT: reflect changes in struct tm from time.h also in mpi_struct_tm
#define STRUCT_TM_ELEMS 9

/* MPI related */
#define ROOT_PROC 0

enum { //tags
        TAG_CMD,
        TAG_TASK,
        TAG_FILTER,
        TAG_AGG,
        TAG_DATA,
};

enum { //control commands
        CMD_RELEASE,
};


/* Function-like macros */


/* Debugging macros */
#ifdef DEBUG

#define print_debug(...) \
        do { \
        printf(__VA_ARGS__); \
        } while (0)
#else
#define print_debug(...)

#endif //DEBUG


/** \brief Print basic record.
 *
 * Prints instance of lnf_brec1_t as one line.
 *
 * \param[in] brec Basic record.
 * \return Error code. E_OK or E_MEM.
 */
int print_brec(const lnf_brec1_t *brec);


/** \brief Print error message.
 *
 * Wrapper to fprintf(), add prefix including process rank and name.
 *
 * \param[in] format Format string passed to fprintf().
 * \param[in] va_list Variable argument list passed to vfprintf().
 */
void print_err(error_code_t prim_errno, int sec_errno,
                const char *format, ...);
void print_warn(error_code_t prim_errno, int sec_errno,
                const char *format, ...);


void create_mpi_struct_agg_param(void);
void free_mpi_struct_agg_param(void);
void create_mpi_struct_tm(void);
void free_mpi_struct_tm(void);
void create_mpi_struct_shared_task_ctx(void);
void free_mpi_struct_shared_task_ctx(void);

int mem_setup(lnf_mem_t *mem, const struct agg_param *ap, size_t ap_cnt);
int mem_print(lnf_mem_t *mem, size_t limit);

double diff_tm(struct tm end_tm, struct tm begin_tm);

/** \brief Portable version of timegm().
 *
 * The mktime() function modifies the fields of the tm structure, if structure
 * members are outside their valid interval, they will be normalized (so that,
 * for  example,  40  October is changed  into  9 November). We need this. But
 * also tm_isdst is set (regardless of its initial value) to a positive value or
 * to 0, respectively, to indicate whether DST is or is not in effect at the
 * specified time. This is what we don't need. Therefore, time zone is set to
 * UTC before calling mktime() in this function and restore previous time zone
 * afterwards. mktime() will normalize tm structure, nothing more.
 *
 * \param[inout] tm Broken-down time. May be altered (normalized).
 * \return Calendar time representation of tm.
 */
time_t mktime_utc(struct tm *tm);

#endif //COMMON_H
