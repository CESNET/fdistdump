/**
 * \file arg_parse.c
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
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

#define _XOPEN_SOURCE //strptime()

#include "arg_parse.h"
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h> //PATH_MAX, NAME_MAX

#include <getopt.h>
#include <libnf.h>


/* Global variables. */
extern int secondary_errno;

enum { //command line options, have to start above ASCII
        OPT_NO_FAST_TOPN = 256, //disable fast top-N algorithm

        OPT_OUTPUT_FORMAT, //output (print) format
        OPT_OUTPUT_TS_CONV, //output timestamp conversion
        OPT_OUTPUT_TS_LOCALTIME, //output timestamp in localtime
        OPT_OUTPUT_STAT_CONV, //output statistics conversion
        OPT_OUTPUT_TCP_FLAGS_CONV, //output TCP flags conversion
        OPT_OUTPUT_IP_ADDR_CONV, //output IP address conversion
        OPT_OUTPUT_IP_PROTO_CONV, //output IP protocol conversion
        OPT_OUTPUT_DURATION_CONV, //output IP protocol conversion
        OPT_OUTPUT_SUMMARY, //print summary?

        OPT_FIELDS, //specification of listed fields

        OPT_HELP, //print help
        OPT_VERSION, //print version
};


static const char *const date_formats[] = {
        /* Date formats. */
        "%Y-%m-%d", //standard date, 2015-12-31
        "%d.%m.%Y", //European, 31.12.2015
        "%m/%d/%Y", //American, 12/31/2015

        /* Time formats. */
        "%H:%M", //23:59

        /* Special formats. */
        "%a", //weekday according to the current locale, abbreviated or full
        "%b", //month according to the current locale, abbreviated or full
};

static const char *const utc_strings[] = {
        "u",
        "ut",
        "utc",
        "U",
        "UT",
        "UTC",
};


static int str_yes_or_no(const char *str)
{
        if (strcmp(str, "y") == 0 || strcmp(str, "yes") == 0) {
                return 1;
        } else if (strcmp(str, "n") == 0 || strcmp(str, "no") == 0) {
                return 0;
        } else {
                return -1;
        }
}


/** \brief Convert string into tm structure.
 *
 * Function tries to parse time string and fills tm with appropriate values on
 * success. String is split into tokens according to TIME_DELIM delimiters. Each
 * token is either converted (from left to right) into date, if it corresponds
 * to one of date_formats[], or UTC flag is set, if token matches one of
 * utc_strings[]. If nor date nor UTF flag is detected, E_ARG is returned.
 *
 * If NO DATE is given, today is assumed if the given hour is lesser than the
 * current hour and yesterday is assumed if it is more. If NO TIME is given,
 * midnight is assumed. If ONLY THE WEEKDAY is given, today is assumed if the
 * given day is less or equal to the current day and last week if it is more.
 * If ONLY THE MONTH is given, the current year is assumed if the given month is
 * less or equal to the current month and last year if it is more and no year is
 * given.
 *
 * Time structure is overwritten by parsed-out values. If string is successfully
 * parsed, E_OK is returned. On error, content of tm structure is undefined and
 * E_ARG is returned.
 *
 * \param[in] time_str Time string, usually gathered from command line.
 * \param[out] utc Set if one of utc_strings[] is found.
 * \param[out] tm Time structure filled with parsed-out time and date.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t str_to_tm(char *time_str, bool *utc, struct tm *tm)
{
        char *ret;
        char *token;
        char *saveptr = NULL;
        struct tm garbage; //strptime() failure would ruin tm values
        struct tm now_tm;
        const time_t now = time(NULL);

        localtime_r(&now, &now_tm);

        tm->tm_sec = tm->tm_min = tm->tm_hour = INT_MIN;
        tm->tm_wday = tm->tm_mday = tm->tm_yday = INT_MIN;
        tm->tm_mon = tm->tm_year = INT_MIN;
        tm->tm_isdst = -1;

        /* Separate time and date in time string. */
        token = strtok_r(time_str, TIME_DELIM, &saveptr); //first token
        while (token != NULL) {
                /* Try to parse date. */
                for (size_t i = 0; i < ARRAY_SIZE(date_formats); ++i) {
                        ret = strptime(token, date_formats[i], &garbage);
                        if (ret != NULL && *ret == '\0') {
                                /* Conversion succeeded, fill real struct tm. */
                                strptime(token, date_formats[i], tm);
                                goto next_token;
                        }
                }

                /* Check for UTC flag. */
                for (size_t i = 0; i < ARRAY_SIZE(utc_strings); ++i) {
                        if (strcmp(token, utc_strings[i]) == 0) {
                                *utc = true;
                                goto next_token;
                        }
                }

                print_err(E_ARG, 0, "invalid time format \"%s\"", token);
                return E_ARG; //conversion failure
next_token:
                token = strtok_r(NULL, TIME_DELIM, &saveptr); //next token
        }


        /* Only the weekday is given. */
        if (tm->tm_wday >= 0 && tm->tm_wday <= 6 && tm->tm_year == INT_MIN &&
                        tm->tm_mon == INT_MIN && tm->tm_mday == INT_MIN) {
                tm->tm_year = now_tm.tm_year;
                tm->tm_mon = now_tm.tm_mon;
                tm->tm_mday = now_tm.tm_mday -
                        (now_tm.tm_wday - tm->tm_wday + 7) % 7;
        }

        /* Only the month is given. */
        if (tm->tm_mon >= 0 && tm->tm_mon <= 11 && tm->tm_mday == INT_MIN) {
                if (tm->tm_year == INT_MIN) {
                        if (tm->tm_mon - now_tm.tm_mon > 0) { //last year
                                tm->tm_year = now_tm.tm_year - 1;
                        } else { //this year
                                tm->tm_year = now_tm.tm_year;
                        }
                }

                tm->tm_mday = 1;
        }

        /* No time is given. */
        if (tm->tm_hour == INT_MIN) {
                tm->tm_hour = 0;
        }
        if (tm->tm_min == INT_MIN) {
                tm->tm_min = 0;
        }
        if (tm->tm_sec == INT_MIN) {
                tm->tm_sec = 0;
        }

        /* No date is given. */
        if (tm->tm_hour >= 0 && tm->tm_hour <= 23 && tm->tm_mon == INT_MIN &&
                        tm->tm_mday == INT_MIN && tm->tm_wday == INT_MIN)
        {
                tm->tm_mon = now_tm.tm_mon;
                if (tm->tm_hour - now_tm.tm_hour > 0) { //yesterday
                        tm->tm_mday = now_tm.tm_mday - 1;
                } else { //today
                        tm->tm_mday = now_tm.tm_mday;
                }
        }

        /* Fill in the gaps. */
        if (tm->tm_year == INT_MIN) {
                tm->tm_year = now_tm.tm_year;
        }
        if (tm->tm_mon == INT_MIN) {
                tm->tm_mon = now_tm.tm_mon;
        }

        mktime_utc(tm); //normalization
        return E_OK;
}


/** \brief Parse and store time interval string.
 *
 * Function tries to parse time interval string, fills interval_begin and
 * interval_end with appropriate values on success. Beginning and ending dates
 * (and times) are  separated with INTERVAL_DELIM, if ending date is not
 * specified, current time is used.
 * If interval string is successfully parsed, E_OK is returned. On error,
 * content of interval_begin and interval_end is undefined and E_ARG is
 * returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] interval_str Aggregation string, usually gathered from command
 *                        line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_time_interval(struct cmdline_args *args,
                char *interval_str)
{
        error_code_t primary_errno = E_OK;
        char *begin_str;
        char *end_str;
        char *trailing_str;
        char *saveptr = NULL;
        bool begin_utc = false;
        bool end_utc = false;

        assert(args != NULL && interval_str != NULL);

        /* Split time interval string. */
        begin_str = strtok_r(interval_str, INTERVAL_DELIM, &saveptr);
        if (begin_str == NULL) {
                print_err(E_ARG, 0, "invalid interval string \"%s\"\n",
                                interval_str);
                return E_ARG;
        }
        end_str = strtok_r(NULL, INTERVAL_DELIM, &saveptr); //NULL is valid
        trailing_str = strtok_r(NULL, INTERVAL_DELIM, &saveptr);
        if (trailing_str != NULL) {
                print_err(E_ARG, 0, "interval trailing string \"%s\"\n",
                                trailing_str);
                return E_ARG;
        }

        /* Convert time strings to tm structure. */
        primary_errno = str_to_tm(begin_str, &begin_utc, &args->interval_begin);
        if (primary_errno != E_OK) {
                return E_ARG;
        }
        if (end_str == NULL) { //NULL means until now
                const time_t now = time(NULL);
                localtime_r(&now, &args->interval_end);
        } else {
                primary_errno = str_to_tm(end_str, &end_utc,
                                &args->interval_end);
                if (primary_errno != E_OK) {
                        return E_ARG;
                }
        }

        if (!begin_utc) {
                time_t tmp;

                //let mktime() decide about DST
                args->interval_begin.tm_isdst = -1;
                tmp = mktime(&args->interval_begin);
                gmtime_r(&tmp, &args->interval_begin);
        }
        if (!end_utc) {
                time_t tmp;

                //let mktime() decide about DST
                args->interval_end.tm_isdst = -1;
                tmp = mktime(&args->interval_end);
                gmtime_r(&tmp, &args->interval_end);
        }

        /* Check interval sanity. */
        if (tm_diff(args->interval_end, args->interval_begin) <= 0) {
                char begin[255], end[255];

                strftime(begin, sizeof(begin), "%c", &args->interval_begin);
                strftime(end, sizeof(end), "%c", &args->interval_end);

                print_err(E_ARG, 0, "zero or negative interval duration");
                print_err(E_ARG, 0, "interval: %s - %s", begin, end);

                return E_ARG;
        }

        /* Align beginning time to closest greater rotation interval. */
        while (mktime_utc(&args->interval_begin) % FLOW_FILE_ROTATION_INTERVAL){
                args->interval_begin.tm_sec++;;
        }

        return E_OK;
}


static void add_field(struct field_info *fields, int fld, int ipv4_bits,
                int ipv6_bits, bool is_aggr_key)
{
        int aggr_type = 0; //aggregation type

        if (is_aggr_key) { //doesn't matter if already present or not, overwrite
                /* Aggregation by duration requires special treatment.*/
                if (fld == LNF_FLD_CALC_DURATION) {
                        fields[LNF_FLD_FIRST].id = LNF_FLD_FIRST;
                        fields[LNF_FLD_FIRST].flags |= LNF_AGGR_MIN;
                        fields[LNF_FLD_LAST].id = LNF_FLD_LAST;
                        fields[LNF_FLD_LAST].flags |= LNF_AGGR_MIN;
                }

                aggr_type = LNF_AGGR_KEY;

        } else if (fields[fld].id == 0) { //field isn't present
                /* Lookup default aggregation type. */
                assert(lnf_fld_info(fld, LNF_FLD_INFO_AGGR, &aggr_type,
                                        sizeof (aggr_type)) == LNF_OK);

                /* Duration requires first and last fields. */
                if (fld == LNF_FLD_CALC_DURATION) {
                        add_field(fields, LNF_FLD_FIRST, 0, 0, false);
                        add_field(fields, LNF_FLD_LAST, 0, 0, false);
                }
                /* Bytes per second requires bytes and duration fields. */
                if (fld == LNF_FLD_CALC_BPS) {
                        add_field(fields, LNF_FLD_DOCTETS, 0, 0, false);
                        add_field(fields, LNF_FLD_CALC_DURATION, 0, 0, false);
                }
                /* Packets per second requires packets and duration fields. */
                if (fld == LNF_FLD_CALC_PPS) {
                        add_field(fields, LNF_FLD_DPKTS, 0, 0, false);
                        add_field(fields, LNF_FLD_CALC_DURATION, 0, 0, false);
                }
                /* Bytes per packet requires bytes and packets fields. */
                if (fld == LNF_FLD_CALC_BPP) {
                        add_field(fields, LNF_FLD_DOCTETS, 0, 0, false);
                        add_field(fields, LNF_FLD_DPKTS, 0, 0, false);
                }
        } else { //field already present, but isn't aggregation key
                return; //keep it untouched
        }

        fields[fld].id = fld;
        fields[fld].flags &= ~LNF_AGGR_FLAGS; //clear aggregation flags
        fields[fld].flags |= aggr_type; //logical OR with sort type
        fields[fld].ipv4_bits = ipv4_bits;
        fields[fld].ipv6_bits = ipv6_bits;
}

static error_code_t add_fields_from_str(struct field_info *fields,
                char *fields_str, bool is_aggr_key)
{
        char *token;
        char *saveptr = NULL;

        for (token = strtok_r(fields_str, FIELDS_DELIM, &saveptr); //first token
                        token != NULL;
                        token = strtok_r(NULL, FIELDS_DELIM, &saveptr)) //next
        {
                int fld; //field ID
                int ipv4_bits; //use ony first bits of IP address
                int ipv6_bits;


                fld = lnf_fld_parse(token, &ipv4_bits, &ipv6_bits);
                if (fld == LNF_FLD_ZERO_ || fld == LNF_ERR_OTHER) {
                        print_err(E_ARG, 0, "unknown LNF field \"%s\"", token);
                        return E_ARG;
                }
                if (ipv4_bits < 0 || ipv4_bits > 32) {
                        print_err(E_ARG, 0, "bad number of IPv4 bits: %d",
                                        ipv4_bits);
                        return E_ARG;
                } else if (ipv6_bits < 0 || ipv6_bits > 128) {
                        print_err(E_ARG, 0, "bad number of IPv6 bits: %d",
                                        ipv6_bits);
                        return E_ARG;
                }

                /* There are some limitations on aggregation key. */
                if (is_aggr_key) {
                        if (fld >= LNF_FLD_CALC_BPS &&
                                        fld <= LNF_FLD_CALC_BPP) {
                                //aggregation using LNF_DOUBLE is unimplemented
                                print_err(E_ARG, 0, "LNF field \"%s\" cannot be"
                                                " set as aggregation key",
                                                token);
                                return E_ARG;
                        } else if (fld == LNF_FLD_BREC1) {
                                print_err(E_ARG, 0, "LNF field \"%s\" cannot be"
                                                " set as aggregation key",
                                                token);
                                //doesn't make sense
                                return E_ARG;
                        }
                }

                add_field(fields, fld, ipv4_bits, ipv6_bits, is_aggr_key);
        }

        return E_OK; //all fields are valid
}

static error_code_t set_sort_field(struct field_info *fields, char *sort_str)
{
        char *field_str;
        char *sort_direction_str;
        char *trailing_str;
        char *saveptr = NULL;
        int fld; //field ID
        int sort_direction;
        int ipv4_bits; //use ony first bits of IP address
        int ipv6_bits;

        /* Parse fields. */
        field_str = strtok_r(sort_str, SORT_DELIM, &saveptr);
        if (field_str == NULL) {
                print_err(E_ARG, 0, "invalid sort string \"%s\"\n", sort_str);
                return E_ARG;
        }

        fld = lnf_fld_parse(field_str, &ipv4_bits, &ipv6_bits);
        if (fld == LNF_FLD_ZERO_ || fld == LNF_ERR_OTHER) {
                print_err(E_ARG, 0, "unknown LNF field \"%s\"", field_str);
                return E_ARG;
        }
        if (ipv4_bits < 0 || ipv4_bits > 32) {
                print_err(E_ARG, 0, "bad number of IPv4 bits: %d",
                                ipv4_bits);
                return E_ARG;
        } else if (ipv6_bits < 0 || ipv6_bits > 128) {
                print_err(E_ARG, 0, "bad number of IPv6 bits: %d",
                                ipv6_bits);
                return E_ARG;
        }

        /* Parse sort direction. */
        sort_direction_str = strtok_r(NULL, SORT_DELIM, &saveptr);
        if (sort_direction_str == NULL) { //default sort direction
                assert(lnf_fld_info(fld, LNF_FLD_INFO_SORT, &sort_direction,
                                        sizeof (sort_direction)) == LNF_OK);
        } else if (strcmp(sort_direction_str, "asc") == 0) { //ascending dir
                sort_direction = LNF_SORT_ASC;
        } else if (strcmp(sort_direction_str, "desc") == 0) { //descending dir
                sort_direction = LNF_SORT_DESC;
        } else { //unknown sort direction
                print_err(E_ARG, 0, "invalid sort type \"%s\"",
                                sort_direction_str);
                return E_ARG;
        }

        /* Check for undesirable remaining characters. */
        trailing_str = strtok_r(NULL, SORT_DELIM, &saveptr);
        if (trailing_str != NULL) {
                print_err(E_ARG, 0, "trailing sort string \"%s\"\n",
                                trailing_str);
                return E_ARG;
        }


        add_field(fields, fld, ipv4_bits, ipv6_bits, 0);

        /* Clear sort flags in all fields. Only one sort key is allowed. */
        for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
                fields[i].flags &= ~LNF_SORT_FLAGS;
        }

        fields[fld].id = fld;
        fields[fld].flags |= sort_direction;
        fields[fld].ipv4_bits = ipv4_bits;
        fields[fld].ipv6_bits = ipv6_bits;

        return E_OK; //all fields are valid
}


/** \brief Parse statistic string and save parameters.
 *
 * Function tries to parse statistic string. Statistic is only shortcut for
 * aggregation, sort and limit. Therefore, statistic string is expected as
 * "fields[/sort_key]". Aggregation fields are set. If sort key isn't present,
 * DEFAULT_STAT_SORT_KEY is used. Record limit will be set to
 * DEFAULT_STAT_REC_LIMIT in case it was not set previously. If statistic string
 * is successfully parsed, E_OK is returned. On error, E_ARG is returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] stat_str Statistic string, usually gathered from command
 *                        line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_stat(struct cmdline_args *args, char *stat_str)
{
        error_code_t primary_errno;
        char *fields_str;
        char *sort_key_str;
        char *trailing_str;
        char *saveptr = NULL;

        fields_str = strtok_r(stat_str, STAT_DELIM, &saveptr);
        if (fields_str == NULL) {
                print_err(E_ARG, 0, "invalid statistic string \"%s\"\n",
                                stat_str);
                return E_ARG;
        }
        primary_errno = add_fields_from_str(args->fields, fields_str, true);
        if (primary_errno != E_OK) {
                return primary_errno;
        }

        sort_key_str = strtok_r(NULL, STAT_DELIM, &saveptr);
        if (sort_key_str == NULL) {
                char tmp[] = DEFAULT_STAT_SORT_KEY;

                primary_errno = set_sort_field(args->fields, tmp);
        } else {
                primary_errno = set_sort_field(args->fields, sort_key_str);
        }
        if (primary_errno != E_OK) {
                return primary_errno;
        }

        trailing_str = strtok_r(NULL, STAT_DELIM, &saveptr);
        if (trailing_str != NULL) {
                print_err(E_ARG, 0, "statistic trailing string \"%s\"\n",
                                trailing_str);
                return E_ARG;
        }

        /* If record limit is unset, set it to DEFAULT_STAT_REC_LIMIT. */
        if (args->rec_limit == SIZE_MAX) {
                args->rec_limit = DEFAULT_STAT_REC_LIMIT;
        }

        return E_OK;
}


/** \brief Check and save filter expression string.
 *
 * Function checks filter by initializing it using libnf lnf_filter_init(). If
 * filter syntax is correct, pointer to filter string is stored in arguments
 * structure and E_OK is returned. Otherwise arguments structure remains
 * untouched and E_ARG is returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] filter_str Filter expression string, usually gathered from command
 *                       line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_filter(struct cmdline_args *args, char *filter_str)
{
        lnf_filter_t *filter;

        /* Try to initialize filter. */
        //TODO: try new filter
        secondary_errno = lnf_filter_init(&filter, filter_str);
        if (secondary_errno != LNF_OK) {
                print_err(E_ARG, secondary_errno,
                                "cannot initialise filter \"%s\"", filter_str);
                return E_ARG;
        }

        lnf_filter_free(filter);
        args->filter_str = filter_str;

        return E_OK;
}


/** \brief Check, parse and save limit string.
 *
 * Function converts limit string into unsigned integer. If string is correct
 * and conversion was successful, args->rec_limit is set and E_OK is returned.
 * On error (overflow, invalid characters, negative value, ...) arguments
 * structure is kept untouched and E_ARG is returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] limit_str Limit string, usually gathered from command line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_limit(struct cmdline_args *args, char *limit_str)
{
        char *endptr;
        long int limit;

        errno = 0; //erase possible previous error number
        limit = strtol(limit_str, &endptr, 0);

        /* Check for various possible errors. */
        if (errno != 0) {
                perror(limit_str);
                return E_ARG;
        }
        if (*endptr != '\0') { //remaining characters
                print_err(E_ARG, 0, "invalid limit \"%s\"", limit_str);
                return E_ARG;
        }
        if (limit < 0) { //negatve limit
                print_err(E_ARG, 0, "negative limit \"%s\"", limit_str);
                return E_ARG;
        }

        args->rec_limit = limit; //will never reach SIZE_MAX (hopefully)

        return E_OK;
}


static error_code_t set_output_format(struct output_params *op,
                char *format_str)
{
        if (strcmp(format_str, "csv") == 0) {
                op->format = OUTPUT_FORMAT_CSV;
        } else if (strcmp(format_str, "pretty") == 0) {
                op->format = OUTPUT_FORMAT_PRETTY;
        } else {
                print_err(E_ARG, 0, "unknown output format string \"%s\"",
                                format_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_ts_conv(struct output_params *op,
                char *ts_conv_str)
{
        if (strcmp(ts_conv_str, "none") == 0) {
                op->ts_conv= OUTPUT_TS_CONV_NONE;
        } else {
                op->ts_conv= OUTPUT_TS_CONV_STR;
                op->ts_conv_str = ts_conv_str;
        }

        return E_OK;
}

static error_code_t set_output_stat_conv(struct output_params *op,
                char *stat_conv_str)
{
        if (strcmp(stat_conv_str, "none") == 0) {
                op->stat_conv= OUTPUT_STAT_CONV_NONE;
        } else if (strcmp(stat_conv_str, "metric-prefix") == 0) {
                op->stat_conv = OUTPUT_STAT_CONV_METRIC_PREFIX;
        } else if (strcmp(stat_conv_str, "binary-prefix") == 0) {
                op->stat_conv = OUTPUT_STAT_CONV_BINARY_PREFIX;
        } else {
                print_err(E_ARG, 0, "unknown output statistics conversion "
                                "string \"%s\"", stat_conv_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_tcp_flags_conv(struct output_params *op,
                char *tcp_flags_conv_str)
{
        if (strcmp(tcp_flags_conv_str, "none") == 0) {
                op->tcp_flags_conv = OUTPUT_TCP_FLAGS_CONV_NONE;
        } else if (strcmp(tcp_flags_conv_str, "str") == 0) {
                op->tcp_flags_conv = OUTPUT_TCP_FLAGS_CONV_STR;
        } else {
                print_err(E_ARG, 0, "unknown tcp flags conversion string "
                                "\"%s\"", tcp_flags_conv_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_ip_addr_conv(struct output_params *op,
                char *ip_addr_conv_str)
{
        if (strcmp(ip_addr_conv_str, "none") == 0) {
                op->ip_addr_conv = OUTPUT_IP_ADDR_CONV_NONE;
        } else if (strcmp(ip_addr_conv_str, "str") == 0) {
                op->ip_addr_conv = OUTPUT_IP_ADDR_CONV_STR;
        } else {
                print_err(E_ARG, 0, "unknown IP address conversion string "
                                "\"%s\"", ip_addr_conv_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_ip_proto_conv(struct output_params *op,
                char *ip_proto_conv_str)
{
        if (strcmp(ip_proto_conv_str, "none") == 0) {
                op->ip_proto_conv = OUTPUT_IP_PROTO_CONV_NONE;
        } else if (strcmp(ip_proto_conv_str, "str") == 0) {
                op->ip_proto_conv = OUTPUT_IP_PROTO_CONV_STR;
        } else {
                print_err(E_ARG, 0, "unknown internet protocol conversion "
                                "string \"%s\"", ip_proto_conv_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_duration_conv(struct output_params *op,
                char *duration_conv_str)
{
        if (strcmp(duration_conv_str, "none") == 0) {
                op->duration_conv = OUTPUT_DURATION_CONV_NONE;
        } else if (strcmp(duration_conv_str, "str") == 0) {
                op->duration_conv = OUTPUT_DURATION_CONV_STR;
        } else {
                print_err(E_ARG, 0, "unknown duration conversion string \"%s\"",
                                duration_conv_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_summary(struct output_params *op,
                char *summary_str)
{
        switch (str_yes_or_no(summary_str)) {
        case 0: //no
                op->summary = OUTPUT_SUMMARY_NO;
                break;
        case 1: //yes
                op->summary = OUTPUT_SUMMARY_YES;
                break;
        default: //other
                print_err(E_ARG, 0, "unknown output summary string" "\"%s\"",
                                summary_str);
                return E_ARG;
        }

        return E_OK;
}


error_code_t arg_parse(struct cmdline_args *args, int argc, char **argv)
{
        error_code_t primary_errno = E_OK;
        int opt;
        int sort_key = LNF_FLD_ZERO_;
        size_t input_arg_cnt = 0;
        bool have_fields = false; //LNF fields are specified

        const char usage_string[] = "Usage: mpirun [ options ] " PACKAGE_NAME
                " [ <args> ]\n";
        const char help_string[] = "help\n";
        const char *short_opts = "a:f:l:o:r:s:t:";
        const struct option long_opts[] = {
                /* Long and short. */
                {"aggregation", required_argument, NULL, 'a'},
                {"filter", required_argument, NULL, 'f'},
                {"limit", required_argument, NULL, 'l'},
                {"order", required_argument, NULL, 'o'},
                {"read", required_argument, NULL, 'r'},
                {"statistic", required_argument, NULL, 's'},
                {"time", required_argument, NULL, 't'},

                /* Long only. */
                {"no-fast-topn", no_argument, NULL, OPT_NO_FAST_TOPN},

                {"output-format", required_argument, NULL, OPT_OUTPUT_FORMAT},
                {"output-ts-conv", required_argument, NULL, OPT_OUTPUT_TS_CONV},
                {"output-ts-localtime", no_argument, NULL,
                        OPT_OUTPUT_TS_LOCALTIME},
                {"output-stat-conv", required_argument, NULL,
                        OPT_OUTPUT_STAT_CONV},
                {"output-tcpflags-conv", required_argument, NULL,
                        OPT_OUTPUT_TCP_FLAGS_CONV},
                {"output-addr-conv", required_argument, NULL,
                        OPT_OUTPUT_IP_ADDR_CONV},
                {"output-proto-conv", required_argument, NULL,
                        OPT_OUTPUT_IP_PROTO_CONV},
                {"output-duration-conv", required_argument, NULL,
                        OPT_OUTPUT_DURATION_CONV},
                {"output-summary", required_argument, NULL, OPT_OUTPUT_SUMMARY},

                {"fields", required_argument, NULL, OPT_FIELDS},

                {"help", no_argument, NULL, OPT_HELP},
                {"version", no_argument, NULL, OPT_VERSION},

                {0, 0, 0, 0} //termination required by getopt_long()
        };


        /* Set argument default values. */
        args->use_fast_topn = true;
        args->rec_limit = SIZE_MAX; //SIZE_MAX means record limit is unset


        /* Loop through all the command-line arguments. */
        while (true) {
                opt = getopt_long(argc, argv, short_opts, long_opts, NULL);
                if (opt == -1) {
                        break; //all options processed successfully
                }

                switch (opt) {
                case 'a': //aggregation
                        args->working_mode = MODE_AGGR;
                        primary_errno = add_fields_from_str(args->fields,
                                        optarg, true);
                        break;

                case 'f': //filter expression
                        primary_errno = set_filter(args, optarg);
                        break;

                case 'l': //limit
                        primary_errno = set_limit(args, optarg);
                        break;

                case 'o': //order
                        //don't overwrite aggregation mode
                        if (args->working_mode == MODE_LIST) {
                                args->working_mode = MODE_SORT;
                        }
                        primary_errno = set_sort_field(args->fields, optarg);
                        break;

                case 'r': //path to input file or directory
                        args->path_str = optarg;
                        input_arg_cnt++;
                        break;

                case 's': //statistic
                        args->working_mode = MODE_AGGR;
                        primary_errno = set_stat(args, optarg);
                        break;

                case 't': //time interval
                        primary_errno = set_time_interval(args, optarg);
                        input_arg_cnt++;
                        break;


                case OPT_NO_FAST_TOPN: //disable fast top-N algorithm
                        args->use_fast_topn = false;
                        break;


                case OPT_OUTPUT_FORMAT:
                        primary_errno = set_output_format(&args->output_params,
                                        optarg);
                        break;

                case OPT_OUTPUT_TS_CONV:
                        primary_errno = set_output_ts_conv(&args->output_params,
                                        optarg);
                        break;

                case OPT_OUTPUT_TS_LOCALTIME:
                        args->output_params.ts_localtime = true;
                        break;

                case OPT_OUTPUT_STAT_CONV:
                        primary_errno = set_output_stat_conv(
                                        &args->output_params, optarg);
                        break;

                case OPT_OUTPUT_TCP_FLAGS_CONV:
                        primary_errno = set_output_tcp_flags_conv(
                                        &args->output_params, optarg);
                        break;

                case OPT_OUTPUT_IP_ADDR_CONV:
                        primary_errno = set_output_ip_addr_conv(
                                        &args->output_params, optarg);
                        break;

                case OPT_OUTPUT_IP_PROTO_CONV:
                        primary_errno = set_output_ip_proto_conv(
                                        &args->output_params, optarg);
                        break;

                case OPT_OUTPUT_DURATION_CONV:
                        primary_errno = set_output_duration_conv(
                                        &args->output_params, optarg);
                        break;

                case OPT_OUTPUT_SUMMARY:
                        primary_errno = set_output_summary(&args->output_params,
                                        optarg);
                        break;


                case OPT_FIELDS:
                        primary_errno = add_fields_from_str(args->fields,
                                        optarg, false);
                        have_fields = true;
                        break;


                case OPT_HELP: //help
                        printf(usage_string);
                        printf("%s", help_string);
                        return E_PASS;

                case OPT_VERSION: //version
                        printf("%s\n", PACKAGE_STRING);
                        return E_PASS;


                default: /* '?' or '0' */
                        return E_ARG;
                }

                if (primary_errno != E_OK) {
                        return primary_errno;
                }
        }


        /* Non-option arguments are undesirable. */
        if (optind != argc) {
                print_err(E_ARG, 0, "unknown non-option argument \"%s\"",
                                argv[optind]);
                fprintf(stderr, usage_string);
                return E_ARG;
        }

        /* Correct data input check. */
        if (input_arg_cnt == 0) {
                print_err(E_ARG, 0, "missing data input specifier (-r or -t)");
                fprintf(stderr, usage_string);
                return E_ARG;
        } else if (input_arg_cnt > 1) {
                print_err(E_ARG, 0, "only one data input specifier allowed");
                fprintf(stderr, usage_string);
                return E_ARG;
        }

        if (args->path_str && (strlen(args->path_str) >= PATH_MAX)) {
                print_err(E_ARG, 0, "path string too long (limit is %lu)",
                                PATH_MAX);
                return E_ARG;
        }


        /* If record limit was not set, disable record limit. */
        if (args->rec_limit == SIZE_MAX) {
                args->rec_limit = 0;
        }

        /* Set some mode specific defaluts. */
        switch (args->working_mode) {
        case MODE_LIST:
                if (!have_fields) {
                        char tmp[] = DEFAULT_LIST_FIELDS;
                        assert(add_fields_from_str(args->fields, tmp, false) ==
                                        E_OK);
                }
                break;

        case MODE_SORT:
                if (!have_fields) {
                        char tmp[] = DEFAULT_SORT_FIELDS;
                        assert(add_fields_from_str(args->fields, tmp, false) ==
                                        E_OK);
                }
                break;

        case MODE_AGGR:
                if (!have_fields) {
                        char tmp[] = DEFAULT_AGGR_FIELDS;
                        assert(add_fields_from_str(args->fields, tmp, false) ==
                                        E_OK);
                }

                /* Find sort key in fields. */
                for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
                        if (args->fields[i].flags & LNF_SORT_FLAGS) {
                                sort_key = args->fields[i].id;
                                break;
                        }
                }

                /* Fast top-N makes sense only under certain conditions. Reasons
                 * to disable fast top-N algorithm are:
                 * - user request by command line argument
                 * - no record limit (all records would be exchanged anyway)
                 * - sort key isn't one of traffic volume fields (data octets,
                 *   packets, out bytes, out packets and aggregated flows)
                 */
                if (args->rec_limit == 0 || sort_key < LNF_FLD_DOCTETS ||
                                sort_key > LNF_FLD_AGGR_FLOWS) {
                        args->use_fast_topn = false;
                }

                break;

        case MODE_PASS:
                break;

        default:
                assert(!"unknown working mode");
        }


        /* Set default output format and conversion parameters. */
        if (args->output_params.format == OUTPUT_FORMAT_UNSET) {
                args->output_params.format = OUTPUT_FORMAT_PRETTY;
        }

        switch (args->output_params.format) {
        case OUTPUT_FORMAT_PRETTY:
                if (args->output_params.ts_conv == OUTPUT_TS_CONV_UNSET) {
                        args->output_params.ts_conv = OUTPUT_TS_CONV_STR;
                        args->output_params.ts_conv_str = "%F %T";
                }

                if (args->output_params.stat_conv == OUTPUT_STAT_CONV_UNSET) {
                        args->output_params.stat_conv =
                                OUTPUT_STAT_CONV_METRIC_PREFIX;
                }

                if (args->output_params.tcp_flags_conv ==
                                OUTPUT_TCP_FLAGS_CONV_UNSET) {
                        args->output_params.tcp_flags_conv =
                                OUTPUT_TCP_FLAGS_CONV_STR;
                }

                if (args->output_params.ip_addr_conv ==
                                OUTPUT_IP_ADDR_CONV_UNSET) {
                        args->output_params.ip_addr_conv =
                                OUTPUT_IP_ADDR_CONV_STR;
                }

                if (args->output_params.ip_proto_conv ==
                                OUTPUT_IP_PROTO_CONV_UNSET) {
                        args->output_params.ip_proto_conv =
                                OUTPUT_IP_PROTO_CONV_STR;
                }

                if (args->output_params.duration_conv ==
                                OUTPUT_DURATION_CONV_UNSET) {
                        args->output_params.duration_conv =
                                OUTPUT_DURATION_CONV_STR;
                }

                if (args->output_params.summary == OUTPUT_SUMMARY_UNSET) {
                        args->output_params.summary = OUTPUT_SUMMARY_YES;
                }
                break;

        case OUTPUT_FORMAT_CSV:
                if (args->output_params.ts_conv == OUTPUT_TS_CONV_UNSET) {
                        args->output_params.ts_conv = OUTPUT_TS_CONV_NONE;
                }

                if (args->output_params.stat_conv == OUTPUT_STAT_CONV_UNSET) {
                        args->output_params.stat_conv = OUTPUT_STAT_CONV_NONE;
                }

                if (args->output_params.tcp_flags_conv ==
                                OUTPUT_TCP_FLAGS_CONV_UNSET) {
                        args->output_params.tcp_flags_conv =
                                OUTPUT_TCP_FLAGS_CONV_NONE;
                }

                if (args->output_params.ip_addr_conv ==
                                OUTPUT_IP_ADDR_CONV_UNSET) {
                        args->output_params.ip_addr_conv =
                                OUTPUT_IP_ADDR_CONV_NONE;
                }

                if (args->output_params.ip_proto_conv ==
                                OUTPUT_IP_PROTO_CONV_UNSET) {
                        args->output_params.ip_proto_conv =
                                OUTPUT_IP_PROTO_CONV_NONE;
                }

                if (args->output_params.duration_conv ==
                                OUTPUT_DURATION_CONV_UNSET) {
                        args->output_params.duration_conv =
                                OUTPUT_DURATION_CONV_NONE;
                }

                if (args->output_params.summary == OUTPUT_SUMMARY_UNSET) {
                        args->output_params.summary = OUTPUT_SUMMARY_NO;
                }
                break;
        default:
                assert(!"unkwnown output parameters format");
        }


#ifdef DEBUG
        {
        static char fld_name_buff[LNF_INFO_BUFSIZE];
        int field;

        printf("------------------------------------------------------\n");
        printf("mode: %s\n", working_mode_to_str(args->working_mode));
        if (args->working_mode == MODE_AGGR && args->use_fast_topn) {
                printf("flags: using fast top-N algorithm\n");
        }

        printf("fields:\n");
        printf("\t%-15s%-12s%-12s%-11s%s\n", "name", "aggr flags",
                        "sort flags", "IPv4 bits", "IPv6 bits");
        for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
                if (args->fields[i].id == 0) {
                        continue;
                }

                lnf_fld_info(args->fields[i].id, LNF_FLD_INFO_NAME,
                                fld_name_buff, LNF_INFO_BUFSIZE);
                printf("\t%-15s0x%-10x0x%-10x%-11d%d\n", fld_name_buff,
                                args->fields[i].flags & LNF_AGGR_FLAGS,
                                args->fields[i].flags & LNF_SORT_FLAGS,
                                args->fields[i].ipv4_bits,
                                args->fields[i].ipv6_bits);
        }

        if(args->filter_str != NULL) {
                printf("filter: %s\n", args->filter_str);
        }

        if (args->path_str != NULL) {
                printf("path: %s\n", args->path_str);
        } else {
                char begin[255], end[255];

                strftime(begin, sizeof(begin), "%c", &args->interval_begin);
                strftime(end, sizeof(end), "%c", &args->interval_end);
                printf("interval: %s - %s\n", begin, end);
        }
        printf("------------------------------------------------------\n\n");
        }
#endif //DEBUG

        return E_OK;
}
