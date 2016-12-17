/**
 * \file common.h
 * \brief Common fdistdump prototypes, macros, data types, enumerations, etc.
 * \author Jan Wrona, <wrona@cesnet.cz>
 * \date 2016
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

#include "config.h"

#include <stddef.h> //size_t
#include <time.h> //struct tm
#include <stdbool.h>
#include <inttypes.h> //exact width integer types

#include <libnf.h>


#define ROOT_PROC 0 //MPI root processor number
#define MAX_STR_LEN 1024 //maximum length of a general string
#define XCHG_BUFF_SIZE (1024 * 1024) //1 KiB

//TODO: move to the configuration file and as parameter options
#define FLOW_FILE_ROTATION_INTERVAL 300 //seconds
#define FLOW_FILE_PATH_FORMAT "%Y/%m/%d"
#define FLOW_FILE_NAME_FORMAT "nfcapd.%Y%m%d%H%M"
#define FLOW_FILE_FORMAT (FLOW_FILE_PATH_FORMAT "/" FLOW_FILE_NAME_FORMAT)


/**
 * \defgroup common_enum Common enumerations usable everywhere
 * @{
 */
typedef enum { //error return codes
        E_OK, //no error, continue processing
        E_HELP, //no error, print help/version and exit
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
        MODE_META, //read only metadata
} working_mode_t;

enum { //tags
        TAG_DATA, //message contains data (records)
        TAG_STATS, //message contains statistics
        TAG_PROGRESS, //message containg progress info
};

typedef enum { //progress bar type
        PROGRESS_BAR_UNSET,
        PROGRESS_BAR_NONE,
        PROGRESS_BAR_TOTAL,
        PROGRESS_BAR_PERSLAVE,
        PROGRESS_BAR_JSON,
} progress_bar_type_t;
/**
 * @}
 */ //common_enum


/**
 * \defgroup common_struct Common structures usable everywhere
 * @{
 */
struct processed_summ {
        uint64_t flows;
        uint64_t pkts;
        uint64_t bytes;
};

struct metadata_summ {
        uint64_t flows;
        uint64_t flows_tcp;
        uint64_t flows_udp;
        uint64_t flows_icmp;
        uint64_t flows_other;

        uint64_t pkts;
        uint64_t pkts_tcp;
        uint64_t pkts_udp;
        uint64_t pkts_icmp;
        uint64_t pkts_other;

        uint64_t bytes;
        uint64_t bytes_tcp;
        uint64_t bytes_udp;
        uint64_t bytes_icmp;
        uint64_t bytes_other;
};


//XXX: reflect changes also in mpi_struct_shared_task_ctx
#define STRUCT_FIELD_INFO_ELEMS 4
struct field_info {
        int id;
        int flags;
        int ipv4_bits;
        int ipv6_bits;
};

//XXX: reflect changes also in mpi_struct_shared_task_ctx
#define STRUCT_SHARED_TASK_CTX_ELEMS 8
struct shared_task_ctx {
        working_mode_t working_mode; //working mode
        struct field_info fields[LNF_FLD_TERM_]; //present LNF fields

        size_t filter_str_len; //filter expression string length
        size_t path_str_len; //path string length

        size_t rec_limit; //record/aggregation limit

        struct tm time_begin; //beginning of the time range
        struct tm time_end; //end of the time range

        bool use_fast_topn; //enables fast top-N algorithm
};
/**
 * @}
 */ //common_struct


/**
 * \defgroup func_like_macros Function-like macros
 * @{
 */
//size of staticly allocated array
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

//size of structure member
#define MEMBER_SIZE(type, member) (sizeof (((type *)NULL)->member))

//intergral division with round up, aka ceil()
#define INT_DIV_CEIL(a, b) (((a) + ((b) - 1)) / (b))

//unsafe macros - double evaluation of arguments with side effects
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX_ASSIGN(a, b) ((a) = (a) > (b) ? (a) : (b))
#define MIN_ASSIGN(a, b) ((a) = (a) < (b) ? (a) : (b))

//safe, but braced-group within expression is GCC extension forbidden by ISO C
#if 0
#define MAX(a, b) \
        ({ \
                 __typeof__(a) _a = (a); \
                 __typeof__(b) _b = (b); \
                 _a > _b ? _a : _b; \
         })
#define MIN(a, b) \
        ({ \
                 __typeof__(a) _a = (a); \
                 __typeof__(b) _b = (b); \
                 _a > _b ? _a : _b; \
         })
#endif
/**
 * @}
 */ //func_like_macros


/** \brief Convert working_mode_t working mode to human-readable string.
 *
 * \return Static string at most MAX_STR_LEN long.
 */
char * working_mode_to_str(working_mode_t working_mode);


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
 * \param[in] fields LNF fields and theirs parameters.
 * \return E_OK on success, E_LNF on error.
 */
error_code_t init_aggr_mem(lnf_mem_t **mem, const struct field_info *fields);

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


int field_get_type(int field);
size_t field_get_size(int field);


void * malloc_wr(size_t nmemb, size_t size, bool abort);
void * calloc_wr(size_t nmemb, size_t size, bool abort);
void * realloc_wr(void *ptr, size_t nmemb, size_t size, bool abort);


#endif //COMMON_H
