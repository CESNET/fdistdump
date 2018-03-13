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
#include <time.h>               // for tzset, mktime

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

// exported global variabled
extern MPI_Comm mpi_comm_main;
extern MPI_Comm mpi_comm_progress_bar;

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

struct field_info {
        int id;
        int flags;
        int ipv4_bits;
        int ipv6_bits;
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
 * @}
 */ //func_like_macros


/** \brief Convert working_mode_t working mode to human-readable string.
 *
 * \return Static string at most MAX_STR_LEN long.
 */
char * working_mode_to_str(working_mode_t working_mode);


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


/**
 * @brief Create MPI communicators mpi_comm_main and mpi_comm_progress_bar as a
 *        duplicates of MPI_COMM_WORLD.
 *
 * From the MPI perspective it is incorrect to start multiple collective
 * communications on the same communicator in same time (more info in Section
 * 5.13 in the MPI standard). We need collective communication for progress bar
 * and for general use during the query, and because this code is executed
 * concurrently, we need two separate communicators.
 *
 * MPI_Comm_dup() is collective on the input communicator, so it is
 * erroneous for a thread to attempt to duplicate a communicator that is
 * simultaneously involved in any other collective in any other thread.
 */
void
mpi_comm_init(void);

/**
 * @brief Deallocate MPI communicators created by mpi_comm_init().
 */
void
mpi_comm_free(void);


/**
 * @brief Allocate a libnf memory and configure for specified fields.
 *
 * If sort_only_mode is false, the memory will be a hash table to perform
 * aggregation based on one or more aggregation keys.
 * If sort_only_mode is true, the memory will be a linked list to store each
 * record as it is.
 * Destructor function libnf_mem_free() should be called to free the memory.
 *
 * @param[in] lnf_mem Double pointer to the libnf memory data type.
 * @param[in] fields Array of field_info structures based on which the memory
 *                   will be configured.
 * @param sort_only_mode Switches between hash table and linked list.
 *
 * @return E_OK on success, E_LNF on failure.
 */
error_code_t
libnf_mem_init(lnf_mem_t **const lnf_mem, const struct field_info fields[],
               const bool sort_only_mode);

/**
 * @brief Calculate number of records in the libnf memory.
 *
 * @param[in] lnf_mem Pointer to the libnf memory (will not be modified).
 *
 * @return Number of records in the supplied memory.
 */
uint64_t
libnf_mem_rec_cnt(lnf_mem_t *lnf_mem);

/**
 * @brief Return length of the first record in the libnf memory in bytes.
 *
 * For now, all records in the libnf memory has same length, because libnf does
 * not support variable-sized records.
 *
 * @param[in] lnf_mem Pointer to the libnf memory (will not be modified).
 *
 * @return Length of the first record in the libnf memory in bytes.
 */
uint64_t
libnf_mem_rec_len(lnf_mem_t *lnf_mem);

/**
 * @brief Sort the records in the memory if sort key is set
 *
 * It is not necessar to call this function to sort the records, because libnf
 * does the sorting automatically with every access. This function triggers the
 * sorting process by requiring first record, then it returns. This is useful
 * for example for measuring how long does the sorting take.
 *
 * @param[in] lnf_mem Pointer to the libnf memory (will not be modified).
 */
void
libnf_mem_sort(lnf_mem_t *lnf_mem);

/**
 * @brief Free memory allocated by libnf_mem_init().
 *
 * @param[in] lnf_mem Pointer to the libnf memory data type.
 */
void
libnf_mem_free(lnf_mem_t *const lnf_mem);


int field_get_type(int field);
size_t field_get_size(int field);
