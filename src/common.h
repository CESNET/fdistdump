/**
 * \file common.h
 * \brief Common fdistdump prototypes, macros, data types, enumerations, etc.
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
#include <inttypes.h> //exact width integer types

#include <libnf.h>

#define ROOT_PROC 0 //MPI root processor number

#define MAX_STR_LEN 1024 //maximum length of a general string
#define MAX_AGG_PARAMS 16 //maximum count of aggregation parameters

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


/**
 * \defgroup common_enum Common enumerations usable everywhere
 * @{
 */
typedef enum { //error return codes
        E_OK, //no error, continue processing
        E_PASS, //no error, no action required
        E_EOF, //no error, end of file

        E_MEM, //memory
        E_MPI, //MPI
        E_LNF, //libnf
        E_INTERNAL, //internal
        E_ARG, //command line arguments
        E_PATH, //problem with access to file/directory
} error_code_t;

typedef enum { //working modes
        MODE_LIST, //list unmodified flow records
        MODE_SORT, //list ordered flow records
        MODE_AGGR, //aggregation and statistic
        MODE_PASS, //do nothing
} working_mode_t;

enum { //tags
        TAG_DATA, //message contains data (records)
        TAG_STATS, //message contains statistics
};
/**
 * @}
 */ //common_enum


/**
 * \defgroup common_struct Common structures usable everywhere
 * @{
 */
struct stats {
        uint64_t flows;
        uint64_t pkts;
        uint64_t bytes;
};

//XXX: reflect changes also in mpi_struct_agg_param
#define STRUCT_AGG_PARAM_ELEMS 4
struct agg_param {
        int field;
        int flags;
        int numbits;
        int numbits6;
};

//XXX: reflect changes also in mpi_struct_shared_task_ctx
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

//XXX: reflect changes in struct tm from time.h also in mpi_struct_tm
#define STRUCT_TM_ELEMS 9
/**
 * @}
 */ //common_struct


/**
 * \defgroup func_like_macros Function-like macros
 * @{
 */
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#define MEMBER_SIZE(type, member) sizeof(((type *)0)->member)

//unsafe macros - double evaluation of arguments with side effects
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

//safe, but braced-group within expression is GCC extension forbidden by ISO C
/*
 * #define MAX(a, b) \
 *       ({ \
 *                __typeof__(a) _a = (a); \
 *                __typeof__(b) _b = (b); \
 *                _a > _b ? _a : _b; \
 *        })
 */
/*
 * #define MIN(a, b) \
 *        ({ \
 *                 __typeof__(a) _a = (a); \
 *                 __typeof__(b) _b = (b); \
 *                 _a > _b ? _a : _b; \
 *         })
 */

#define BIT_SET(var, idx) ((var) |= (1 << (idx)))
#define BIT_CLEAR(var, idx) ((var) &= ~(1 << (idx)))
#define BIT_TOGGLE(var, idx) ((var) ^= (1 << (idx)))
#define BIT_TEST(var, idx) ((var) & (1 << (idx)))
/**
 * @}
 */ //func_like_macros


/** \brief Convert working_mode_t working mode to human-readable string.
 *
 * \return Static string at most MAX_STR_LEN long.
 */
char * working_mode_to_str(working_mode_t working_mode);


/** \brief Print error message.
 *
 * Print detailed error information to stderr. Provided format is prefixed by
 * error cause and MPI process info.
 *
 * \param[in] prim_errno Primary errno.
 * \param[in] sec_errno Secondary errno.
 * \param[in] format Format string passed to vfprintf().
 * \param[in] va_list Variable argument list passed to vfprintf().
 */
void print_err(error_code_t prim_errno, int sec_errno,
                const char *format, ...);

/** \brief Print warning message.
 *
 * Print detailed warning information to stderr. Provided format is prefixed by
 * warning cause and MPI process info.
 *
 * \param[in] prim_errno Primary errno.
 * \param[in] sec_errno Secondary errno.
 * \param[in] format Format string passed to vfprintf().
 * \param[in] va_list Variable argument list passed to vfprintf().
 */
void print_warn(error_code_t prim_errno, int sec_errno,
                const char *format, ...);

/** \brief Print debug message.
 *
 * If DEBUG is defined, print provided debug string to stdout. Format is
 * prefixed by MPI process info. If DEBUG is not defined, function will do
 * nothing.
 *
 * \param[in] format Format string passed to vfprintf().
 * \param[in] va_list Variable argument list passed to vfprintf().
 */
void print_debug(const char *format, ...);


/** \brief Construct MPI structure mpi_struct_agg_param.
 *
 * Global variable MPI_Datatype mpi_struct_agg_param is constructed as mirror
 * to struct agg_param. Every change to struct agg_param must be reflected.
 */
void create_mpi_struct_agg_param(void);

/** \brief Destruct MPI structure mpi_struct_agg_param.
 */
void free_mpi_struct_agg_param(void);

/** \brief Construct MPI contiguous mpi_struct_tm.
 *
 * Global variable MPI_Datatype mpi_struct_tm is constructed as mirror
 * to struct tm. Every change to struct tm must be reflected.
 */
void create_mpi_struct_tm(void);

/** \brief Destruct MPI contiguous mpi_struct_tm.
 */
void free_mpi_struct_tm(void);

/** \brief Construct MPI structure mpi_struct_shared_task_ctx.
 *
 * Global variable MPI_Datatype mpi_struct_shared_task_ctx is constructed as
 * mirror to struct shared_task_ctx. Every change to struct shared_task_ctx must
 * be reflected.
 */
void create_mpi_struct_shared_task_ctx(void);

/** \brief Destruct MPI structure mpi_struct_shared_task_ctx.
 */
void free_mpi_struct_shared_task_ctx(void);


/** \brief Initialize LNF aggregation memory.
 *
 * Initialize aggregation memory and set memory parameters. mem will be
 * allocated, therefore have to be freed by free_aggr_mem().
 *
 * \param[inout] mem Pointer to pointer to LNF memory structure.
 * \return E_OK on success, error code otherwise.
 */
error_code_t init_aggr_mem(lnf_mem_t **mem, const struct agg_param *ap,
                size_t ap_cnt);

/** \brief Free LNF aggregation memory.
 *
 * \param[inout] mem Pointer to LNF memory structure.
 */
void free_aggr_mem(lnf_mem_t *mem);


/** \brief Yield the time difference between a and b.
 *
 * Measured in seconds, ignoring leap seconds. Compute intervening leap days
 * correctly even if year is negative. Take care to avoid int overflow in leap
 * day calculations, but it's OK to assume that A and B are close to each other.
 * Copy paste from glibc 2.22.
 *
 * \param[in] a Subtrahend.
 * \param[in] b Minuend.
 * \return Difference in seconds.
 */
int tm_diff(const struct tm a, const struct tm b);

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
