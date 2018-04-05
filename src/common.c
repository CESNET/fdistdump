/** Various functions and variables needed in multiple translation units.
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

#include "common.h"

#include <assert.h>             // for assert
#include <errno.h>              // for errno
#include <stddef.h>             // for NULL, size_t
#include <stdlib.h>             // for setenv, unsetenv, getenv
#include <string.h>             // for strlen, strncpy
#include <time.h>               // for nanosleep, timespec

#include <mpi.h>                // for MPI_Comm

#include "errwarn.h"            // for error/warning/info/debug messages, ...
#include "fields.h"             // for struct fields


#define TM_YEAR_BASE 1900


/*
 * Global variables.
 */
MPI_Comm mpi_comm_main = MPI_COMM_NULL;
MPI_Comm mpi_comm_progress = MPI_COMM_NULL;


/**
 * @defgroup time_func Time related functions.
 * @{
 */
/**
 * @brief Calculate the difference in seconds between beginning and end.
 *
 * @param a Beginning.
 * @param b End.
 *
 * @return Difference in seconds.
 */
int
tm_diff(const struct tm a, const struct tm b)
{
    const int a4 = (a.tm_year >> 2) + (TM_YEAR_BASE >> 2) - ! (a.tm_year & 3);
    const int b4 = (b.tm_year >> 2) + (TM_YEAR_BASE >> 2) - ! (b.tm_year & 3);
    const int a100 = a4 / 25 - (a4 % 25 < 0);
    const int b100 = b4 / 25 - (b4 % 25 < 0);
    const int a400 = a100 >> 2;
    const int b400 = b100 >> 2;
    const int intervening_leap_days = (a4 - b4) - (a100 - b100) + (a400 - b400);
    const int years = a.tm_year - b.tm_year;
    const int days = (365 * years + intervening_leap_days
                      + (a.tm_yday - b.tm_yday));

    return (60 * (60 * (24 * days + (a.tm_hour - b.tm_hour))
                  + (a.tm_min - b.tm_min)) + (a.tm_sec - b.tm_sec));
}

/**
 * @brief Call mktime() with UTC timezone to prevent daylight saving to affect
 *        mktime().
 *
 * The mktime() function converts a broken-down time structure, expressed as
 * local time, to calendar time representation.
 *
 * @param tm A pointer to the broken-down time structure.
 *
 * @return Calendar time representation.
 */
time_t
mktime_utc(struct tm *tm)
{

    // save current time zone environment variable
    const char *const tz = getenv("TZ");
    char orig_tz[256];
    if (tz) {
        assert(strlen(tz) < sizeof (orig_tz));
        strncpy(orig_tz, tz, sizeof (orig_tz) - 1);
        orig_tz[sizeof (orig_tz) - 1] = '\0';
    }

    // set time zone to UTC
    int ret = setenv("TZ", "", 1);
    ABORT_IF(ret, E_INTERNAL, "setenv(): %s", strerror(errno));
    tzset();  // apply changes in TZ

    // do the normalization within the UTC time zone
    const time_t calendar_time = mktime(tm);

    // restore time zone to the original value
    if (tz) {
        ret = setenv("TZ", orig_tz, 1);
        ABORT_IF(ret, E_INTERNAL, "setenv(): %s", strerror(errno));
    } else {
        ret = unsetenv("TZ");
        ABORT_IF(ret, E_INTERNAL, "unsetenv(): %s", strerror(errno));
    }
    tzset();  // apply changes in TZ

    return calendar_time;
}
/**
 * @}
 */  // time_func


/**
 * @defgroup mpi_common
 * @{
 */
/**
 * @brief Create MPI communicators mpi_comm_main and mpi_comm_progress as a
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
mpi_comm_init(void)
{
    MPI_Comm_dup(MPI_COMM_WORLD, &mpi_comm_main);
    MPI_Comm_dup(MPI_COMM_WORLD, &mpi_comm_progress);
}

/**
 * @brief Deallocate MPI communicators created by mpi_comm_init().
 */
void
mpi_comm_free(void)
{
    MPI_Comm_free(&mpi_comm_main);
    MPI_Comm_free(&mpi_comm_progress);
}

/**
 * @brief Alternative for MPI_Wait() without busy wait.
 *
 * Use MPI_Test() to tests for the completion of a specific send or receive and
 * nanosleep() to avoid busy wait (which is what MPI_Wait() uses by default). If
 * poll_interval is zero, use MPI_Wait().
 *
 * @param[in] request Communication request (handle).
 * @param[out] status Status object (status).
 * @param[in] poll_interval Suspend execution between consecutive MPI_Test()
 *                          calls for (at least) this time has elapsed.
 *
 * @return Return code of the last MPI call or -1 if nanosleep() was interrupted
 *         by a signal.
 */
int
mpi_wait_poll(MPI_Request *request, MPI_Status *status,
              const struct timespec poll_interval)
{
    if (poll_interval.tv_sec == 0 && poll_interval.tv_nsec == 0) {
        return MPI_Wait(request, status);
    }

    int ret;
    while (true) {
        int op_completed;
        ret = MPI_Test(request, &op_completed, status);
        if (op_completed) {
            return ret;
        }
        if (nanosleep(&poll_interval, NULL) != 0) {
            return -1;  // interrupted by a signal
        }
    }
}
/**
 * @}
 */  // mpi_common


/**
 * @defgroup libnf_mem Convenient wrappers operating with libnf memory.
 * @{
 */
/**
 * @brief Allocate a libnf hash table memory and configure for specified fields.
 *
 * The memory will be a hash table designated to perform aggregation based on
 * one or more aggregation keys. Sorting of the aggregated records is also
 * possible.
 * Destructor function libnf_mem_free() should be called to free the memory.
 *
 * Note about alignment for sort key and output fields:
 *   - Use 32/128 alignment for addresses (alignment is a netmask), because we
 *     always want to have a full address.
 *   - Use zero alignment for all other fields. Otherwise, libnf would clear
 *     last bits of LNF_UINT64 type fields (to align timestamps...).
 *
 * @param[in] lnf_mem Double pointer to the libnf memory data type.
 * @param[in] fields Pointer to the fields structure.
 */
void
libnf_mem_init_ht(lnf_mem_t **const lnf_mem, const struct fields *const fields)
{
    assert(lnf_mem && fields);
    assert(fields->aggr_keys_cnt > 0);

    // initialize the memory
    int lnf_ret = lnf_mem_init(lnf_mem);
    ABORT_IF(lnf_ret != LNF_OK, E_LNF, "lnf_mem_init()");

    // loop through all aggregation keys and add them
    const bool have_sort_key = (fields->sort_key.field != NULL);
    bool sort_key_is_one_of_aggregation_keys = false;
    for (size_t i = 0; i < fields->aggr_keys_cnt; ++i) {
        int flags = LNF_AGGR_KEY;

        if (have_sort_key
                && fields->sort_key.field == fields->aggr_keys[i].field)
        {
            // sort key is one of aggregation keys, merge the flags
            sort_key_is_one_of_aggregation_keys = true;
            flags |= fields->sort_key.direction;
        }

        lnf_ret = lnf_mem_fadd(*lnf_mem, fields->aggr_keys[i].field->id, flags,
                               fields->aggr_keys[i].alignment,
                               fields->aggr_keys[i].ipv6_alignment);
        ABORT_IF(lnf_ret != LNF_OK, E_LNF, "lnf_mem_fadd() aggregation key");
    }

    // add sort key if there is one and it was not one of aggregation keys
    if (have_sort_key && !sort_key_is_one_of_aggregation_keys) {
        int alignment;
        int ipv6_alignment;
        if (field_get_type(fields->sort_key.field->id) == LNF_ADDR) {
            alignment = IPV4_NETMASK_LEN_MAX;
            ipv6_alignment = IPV6_NETMASK_LEN_MAX;
        } else {
            alignment = 0;
            ipv6_alignment = 0;
        }
        const int flags = fields->sort_key.direction | fields->sort_key.aggr_func;
        lnf_ret = lnf_mem_fadd(*lnf_mem, fields->sort_key.field->id,
                               flags, alignment, ipv6_alignment);
        ABORT_IF(lnf_ret != LNF_OK, E_LNF, "lnf_mem_fadd() sort key");
    }


    if (fields_can_use_fast_aggr(fields)) {
        // the output fields comply with the fast aggregation, use it
        INFO("using the libnf fast aggregation mode");
        lnf_ret = lnf_mem_fastaggr(*lnf_mem, LNF_FAST_AGGR_BASIC);
        ABORT_IF(lnf_ret != LNF_OK, E_LNF, "lnf_mem_fastaggr()");
    } else {
        // the output fields do not comply with the fast aggregation, add all
        // output fields
        for (size_t i = 0; i < fields->output_fields_cnt; i++) {
            int alignment;
            int ipv6_alignment;
            if (field_get_type(fields->output_fields[i].field->id) == LNF_ADDR) {
                alignment = IPV4_NETMASK_LEN_MAX;
                ipv6_alignment = IPV6_NETMASK_LEN_MAX;
            } else {
                alignment = 0;
                ipv6_alignment = 0;
            }
            lnf_ret = lnf_mem_fadd(*lnf_mem, fields->output_fields[i].field->id,
                    fields->output_fields[i].aggr_func, alignment,
                    ipv6_alignment);
            ABORT_IF(lnf_ret != LNF_OK, E_LNF, "lnf_mem_fadd() output field");
        }
    }
}

/**
 * @brief Allocate a libnf linked list memory and configure for specified fields.
 *
 * The memory will be a linked list designated to store each record as it is,
 * (i.e., no aggregation).
 * Destructor function libnf_mem_free() should be called to free the memory.
 *
 * Note about alignment for sort key and output fields:
 *   - Use 32/128 alignment for addresses (alignment is a netmask), because we
 *     always want to have a full address.
 *   - Use zero alignment for all other fields. Otherwise, libnf would clear
 *     last bits of LNF_UINT64 type fields (to align timestamps...).
 *
 * @param[in] lnf_mem Double pointer to the libnf memory data type.
 * @param[in] fields Pointer to the fields structure.
 */
void
libnf_mem_init_list(lnf_mem_t **const lnf_mem,
                    const struct fields *const fields)
{
    assert(lnf_mem && fields);
    assert(fields->sort_key.field);

    // initialize the memory and switch it to a linked list to disable
    // aggregation
    int lnf_ret = lnf_mem_init(lnf_mem);
    ABORT_IF(lnf_ret != LNF_OK, E_LNF, "lnf_mem_init()");
    lnf_ret = lnf_mem_setopt(*lnf_mem, LNF_OPT_LISTMODE, NULL, 0);
    ABORT_IF(lnf_ret != LNF_OK, E_LNF, "lnf_mem_setopt()");

    // add all fields
    for (size_t i = 0; i < fields->all_cnt; ++i) {
        int flags = LNF_AGGR_AUTO;  // AUTO means nothing for linked list

        if (fields->sort_key.field->id == fields->all[i].id) {
            // this field is a sort key
            flags |= fields->sort_key.direction;
        }

        int alignment;
        int ipv6_alignment;
        if (field_get_type(fields->all[i].id) == LNF_ADDR) {
            alignment = IPV4_NETMASK_LEN_MAX;
            ipv6_alignment = IPV6_NETMASK_LEN_MAX;
        } else {
            alignment = 0;
            ipv6_alignment = 0;
        }

        lnf_ret = lnf_mem_fadd(*lnf_mem, fields->all[i].id, flags, alignment,
                               ipv6_alignment);
        ABORT_IF(lnf_ret != LNF_OK, E_LNF, "lnf_mem_fadd()");
    }
}

/**
 * @brief Free memory allocated by libnf_mem_init().
 *
 * @param[in] lnf_mem Pointer to the libnf memory data type.
 */
void
libnf_mem_free(lnf_mem_t *const lnf_mem)
{
    assert(lnf_mem);
    lnf_mem_free(lnf_mem);
}

/**
 * @brief Calculate number of records in the libnf memory.
 *
 * @param[in] lnf_mem Pointer to the libnf memory (will not be modified).
 *
 * @return Number of records in the supplied memory.
 */
uint64_t
libnf_mem_rec_cnt(lnf_mem_t *const lnf_mem)
{
    uint64_t rec_cntr = 0;

    // initialize the libnf cursor
    lnf_mem_cursor_t *cursor;
    int lnf_ret = lnf_mem_first_c(lnf_mem, &cursor);
    assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));

    while (cursor) {
        lnf_ret = lnf_mem_next_c(lnf_mem, &cursor);
        assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
        rec_cntr++;
    }
    (void)lnf_ret;  // to suppress -Wunused-variable with -DNDEBUG

    return rec_cntr;
}

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
libnf_mem_rec_len(lnf_mem_t *const lnf_mem)
{
    // initialize the libnf cursor
    lnf_mem_cursor_t *cursor;
    int lnf_ret = lnf_mem_first_c(lnf_mem, &cursor);
    assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
    if (!cursor) {
        return 0;
    }

    // query size of one record
    char rec_buff[LNF_MAX_RAW_LEN];
    int rec_len = 0;
    lnf_ret = lnf_mem_read_raw_c(lnf_mem, cursor, rec_buff, &rec_len,
                                 sizeof (rec_buff));
    assert(rec_len > 0 && lnf_ret == LNF_OK);
    (void)lnf_ret;  // to suppress -Wunused-variable with -DNDEBUG

    return rec_len;
}

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
libnf_mem_sort(lnf_mem_t *const lnf_mem)
{
    // initialize the libnf cursor
    lnf_mem_cursor_t *cursor;
    int lnf_ret = lnf_mem_first_c(lnf_mem, &cursor);
    assert((cursor && lnf_ret == LNF_OK) || (!cursor && lnf_ret == LNF_EOF));
    (void)lnf_ret;  // to suppress -Wunused-variable with -DNDEBUG
}
/**
 * @}
 */  // libnf_mem

/**
 * @defgroup libnf_common Common libnf functions and wrappers.
 * @{
 */
/**
 * @brief Convert libnf sort direction to string.
 *
 * @param sort_dir Libnf sort direction LNF_SORT_NONE, LNF_SORT_ASC, or
 *                 LNF_SORT_DESC.
 *
 * @return Static read-only string.
 */
const char *
libnf_sort_dir_to_str(const int sort_dir)
{
    switch (sort_dir) {
    case LNF_SORT_ASC:
        return "asc";
    case LNF_SORT_DESC:
        return "desc";
    default:
        ABORT(E_INTERNAL, "unknown sort direction");
    }
}

/**
 * @brief Convert libnf aggregation function to string.
 *
 * @param sort_dir Libnf aggregation function LNF_AGGR_MIN, LNF_AGGR_MAX,
 *                 LNF_AGGR_SUM, or LNF_AGGR_OR.
 *
 * @return Static read-only string.
 */
const char *
libnf_aggr_func_to_str(const int aggr_func)
{
    switch (aggr_func) {
    case LNF_AGGR_MIN:
        return "min";
    case LNF_AGGR_MAX:
        return "max";
    case LNF_AGGR_SUM:
        return "sum";
    case LNF_AGGR_OR:
        return "or";
    default:
        ABORT(E_INTERNAL, "unknown aggregation function");
    }
}
/**
 * @}
 */  // libnf_common
