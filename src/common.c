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
#include "print.h"

#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h> //variable argument list
#include <stddef.h> //offsetof()

#include <mpi.h>


#define TM_YEAR_BASE 1900


/**
 * \defgroup to_str_func Various elements to string converting functions
 * @{
 */
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

        default:
                assert(!"unknown working mode");
        }

        return msg;
}
/**
 * @}
 */ //to_str_func


/**
 * \defgroup libnf_mem Functions operating with libnf aggregation/sorting memory
 * @{
 */
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
               const bool sort_only_mode)
{
    assert(lnf_mem && fields);

    error_code_t ecode = E_OK;

    // initialize the memory
    int lnf_ret = lnf_mem_init(lnf_mem);
    if (lnf_ret != LNF_OK) {
        PRINT_ERROR(E_LNF, lnf_ret, "lnf_mem_init()");
        return E_LNF;
    }

    bool have_aggr_field = false;
    bool have_sort_field = false;
    for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
        if (fields[i].id == 0) {
            continue;  // field is not present
        }
        //TODO
        if ((fields[i].flags & LNF_AGGR_FLAGS) == LNF_AGGR_KEY) {
            have_aggr_field = true;
        }
        if (fields[i].flags & LNF_SORT_FLAGS) {
            have_sort_field = true;
        }
    }
    //TODO
    if (sort_only_mode) {
        //assert(have_sort_field && !have_aggr_field);
    } else {
        assert(have_aggr_field);
    }

    // is it possible to apply a fast aggregation?
    if (!sort_only_mode
            && fields[LNF_FLD_FIRST].id
            && ((fields[LNF_FLD_FIRST].flags & LNF_AGGR_FLAGS) == LNF_AGGR_MIN)
            && fields[LNF_FLD_LAST].id
            && ((fields[LNF_FLD_LAST].flags & LNF_AGGR_FLAGS) == LNF_AGGR_MAX)
            && fields[LNF_FLD_DOCTETS].id
            && ((fields[LNF_FLD_DOCTETS].flags & LNF_AGGR_FLAGS) == LNF_AGGR_SUM)
            && fields[LNF_FLD_DPKTS].id
            && ((fields[LNF_FLD_DPKTS].flags & LNF_AGGR_FLAGS) == LNF_AGGR_SUM)
            && fields[LNF_FLD_AGGR_FLOWS].id
            && ((fields[LNF_FLD_AGGR_FLOWS].flags & LNF_AGGR_FLAGS) == LNF_AGGR_SUM))
    {
        lnf_ret = lnf_mem_fastaggr(*lnf_mem, LNF_FAST_AGGR_BASIC);
        if (lnf_ret != LNF_OK) {
            ecode = E_LNF;
            PRINT_ERROR(ecode, lnf_ret, "lnf_mem_fastaggr()");
            goto error_label;
        }
    }

    // loop through the fields
    for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
        if (fields[i].id == 0) {
            continue;  // field is not present
        }

        if (fields[i].id >= LNF_FLD_CALC_BPS
                && fields[i].id <= LNF_FLD_CALC_BPP) {
            continue;
        }

        lnf_ret = lnf_mem_fadd(*lnf_mem, fields[i].id, fields[i].flags,
                               fields[i].ipv4_bits, fields[i].ipv6_bits);
        if (lnf_ret != LNF_OK) {
            ecode = E_LNF;
            PRINT_ERROR(ecode, lnf_ret, "lnf_mem_fadd()");
            goto error_label;
        }
    }

    if (sort_only_mode) {
        // switch the libnf memory to a linked list to disable aggregation
        lnf_ret = lnf_mem_setopt(lnf_mem, LNF_OPT_LISTMODE, NULL, 0);
        assert(lnf_ret == LNF_OK);
    }

    return ecode;
error_label:
    libnf_mem_free(*lnf_mem);
    *lnf_mem = NULL;
    return ecode;
}

/**
 * @brief Calculate number of records in the libnf memory.
 *
 * @param[in] lnf_mem Pointer to the libnf memory (will not be modified).
 *
 * @return Number of records in the supplied memory.
 */
uint64_t
libnf_mem_rec_cnt(lnf_mem_t *lnf_mem)
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

    return rec_cntr;
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
 * @}
 */  // libnf_mem


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
                assert(strlen(tz) < sizeof (orig_tz));
                strncpy(orig_tz, tz, sizeof (orig_tz) - 1);
                orig_tz[sizeof (orig_tz) - 1] = '\0';
        }

        /* Set time zone to UTC. mktime() would be affected by daylight saving
         * otherwise.
         */
        setenv("TZ", "", 1);
        tzset();

        ret = mktime(tm); //actual normalization within UTC time zone

        /* Restore time zone to stored value. */
        if (tz != NULL) {
                assert(setenv("TZ", orig_tz, 1) == 0);
        } else {
                assert(unsetenv("TZ") == 0);
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
