/** Various macros and declarations needed in multiple translation units.
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
 */

#pragma once

#include <assert.h>             // for assert
#include <inttypes.h>           // for fixed-width integer types
#include <stdbool.h>            // for bool
#include <stddef.h>             // for NULL, size_t
#include <time.h>               // for time_t

#include <libnf.h>              // for lnf_mem_t
#include <mpi.h>                // for MPI_Comm


#define ROOT_PROC 0  // MPI root processor rank
#define MAX_STR_LEN 1024  // maximum length of a general string
#define XCHG_BUFF_SIZE (1024 * 1024)  // 1 KiB

#define FLOW_FILE_ROTATION_INTERVAL 300 //seconds
#define FLOW_FILE_PATH_FORMAT "%Y/%m/%d"
#define FLOW_FILE_NAME_PREFIX "lnf"
#define FLOW_FILE_NAME_SUFFIX "%Y%m%d%H%M%S"
#define FLOW_FILE_NAME_FORMAT FLOW_FILE_NAME_PREFIX "." FLOW_FILE_NAME_SUFFIX
#define FLOW_FILE_FORMAT FLOW_FILE_PATH_FORMAT "/" FLOW_FILE_NAME_FORMAT


// forward declarations
struct tm;
struct timespec;
struct fields;

// exported global variables
extern MPI_Comm mpi_comm_main;
extern MPI_Comm mpi_comm_progress;

typedef uint32_t xchg_rec_size_t;


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
        E_BFINDEX, //bloom filter indexing error
} error_code_t;

typedef enum { //working modes
        MODE_LIST, //list unmodified flow records
        MODE_SORT, //list ordered flow records
        MODE_AGGR, //aggregation and statistic
        MODE_META, //read only metadata
} working_mode_t;

enum {  // MPI point-to-point communication tags
    TAG_LIST,
    TAG_SORT,
    TAG_AGGR,
    TAG_TPUT1,
    TAG_TPUT2,
    TAG_TPUT3,

    TAG_STATS,     // messages contains statistics
    TAG_PROGRESS,  // messages containg progress info
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
#define STRUCT_PROCESSED_SUMM_ELEMENTS 3
struct processed_summ {
        uint64_t flows;
        uint64_t pkts;
        uint64_t bytes;
};

#define STRUCT_METADATA_SUMM_ELEMENTS 15
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
/**
 * @}
 */ //common_struct


/**
 * \defgroup func_like_macros Function-like macros
 * @{
 */
// number of elements in staticly allocated array
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

// compile-time strlen for static strings (minus one for terminating null-byte)
#define STRLEN_STATIC(str) (ARRAY_SIZE(str) - 1)

//size of structure member
#define MEMBER_SIZE(type, member) (sizeof (((type *)NULL)->member))

//intergral division with round up, aka ceil()
#define INT_DIV_CEIL(a, b) (((a) + ((b) - 1)) / (b))

//unsafe macros - double evaluation of arguments with side effects
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX_ASSIGN(a, b) ((a) = (a) > (b) ? (a) : (b))
#define MIN_ASSIGN(a, b) ((a) = (a) < (b) ? (a) : (b))

// unsafe macros - double evaluation of arguments with side effects
// the comma operator evaluates its first operand (assert which returns void)
// and discards the result, and then evaluates the second operand and returns
// this value (and type)
// alegedly faster version: ((unsigned)(number - lower) <= (upper - lower))
#define IN_RANGE_INCL(number, lower, upper) \
    (assert(lower < upper), ((number) >= (lower) && (number) <= (upper)))
#define IN_RANGE_EXCL(number, lower, upper) \
    (assert(lower < upper), ((number) > (lower) && (number) < (upper)))

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
* @brief Wrapper for a safer string appending using snprintf(). UNSAFE MACRO!
*
* The functions snprintf() and vsnprintf() do not write more than size bytes
* (including the terminating null byte). The main reason behind this macro is
* that if the output was truncated due to this limit, then THE RETURN VALUE IS
* THE NUMBER OF CHARACTERS (EXCLUDING THE TERMINATING NULL BYTE) WHICH WOULD
* HAVE BEEN WRITTEN to the final string if enough space had been available.
* Thus, a return value of size or more means that the output was truncated.
*
* Both str_term and remaining_size are modified in each call:
* If the added string did not have to be truncated, then str_term pointer is
* moved to point to the new first terminating null byte and remaining_size size
* is decreased by the length of the added string.
* If snprintf() could not write all the bytes (the added string had to be
* truncated) or completely skipped, remaining_size is set to 0 so the future
* expansions of SNPRINTF_APPEND would not case buffer overflow.
*
* The usage should be something like:
* char *const string = calloc(STR_LEN, sizeof (*string));
* char *str_term = string;  // to prevent loss of the original pointer
* size_t str_size = STR_LEN;
* SNPRINTF_APPEND(str_term, str_size, "some format", ...);
* SNPRINTF_APPEND(str_term, str_size, "some other format", ...);
*
* @param[in,out] str_term Pointer to the place in the string, where the
*                         appending should start. Usually, it is the first
*                         terminating null byte.
* @param[in,out] remaining_size Number of bytes remaining in the string.
* @param[in] format Format string to pass to snprintf().
* @param[in] ... Additional argument corresponding to the format string.
*/
#define SNPRINTF_APPEND(str_term, remaining_size, format, ...) \
    do { \
        const size_t _would_write = snprintf(str_term, remaining_size, format, \
                                             __VA_ARGS__); \
        if (_would_write >= remaining_size) {  /* the output was truncated */ \
            remaining_size = 0; \
        } else {  /* the output was not truncated */ \
            str_term += _would_write; \
            remaining_size -= _would_write; \
        } \
    } while (false)
/**
 * @}
 */ //func_like_macros


// time_func
int
tm_diff(const struct tm a, const struct tm b);

time_t
mktime_utc(struct tm *tm);


// mpi_common
void
mpi_comm_init(void);

void
mpi_comm_free(void);

int
mpi_wait_poll(MPI_Request *request, MPI_Status *status,
              const struct timespec poll_interval);


// libnf_mem
void
libnf_mem_init_ht(lnf_mem_t **const lnf_mem, const struct fields *const fields);

void
libnf_mem_init_list(lnf_mem_t **const lnf_mem,
                    const struct fields *const fields);

void
libnf_mem_free(lnf_mem_t *const lnf_mem);

uint64_t
libnf_mem_rec_cnt(lnf_mem_t *const lnf_mem);

uint64_t
libnf_mem_rec_len(lnf_mem_t *const lnf_mem);

void
libnf_mem_sort(lnf_mem_t *const lnf_mem);

const char *
libnf_sort_dir_to_str(const int sort_dir);


// libnf_common
const char *
libnf_aggr_func_to_str(const int aggr_func);
