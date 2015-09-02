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
        OPT_NO_FAST_TOPN = 256, //disable fast topn-N algorithm

        OPT_OUTPUT_FORMAT, //output (print) format
        OPT_OUTPUT_TS_CONV, //output timestamp conversion
        OPT_OUTPUT_STAT_CONV, //output statistics conversion
        OPT_OUTPUT_TCP_FLAGS_CONV, //output TCP flags conversion
        OPT_OUTPUT_IP_PROTO_CONV, //output IP protocol conversion

        OPT_HELP, //print help
        OPT_VERSION, //print version
};


static const char *const date_formats[] = {
        "%Y-%m-%d", //standard date, 2015-12-31
        "%d.%m.%Y", //european, 31.12.2015
        "%m/%d/%Y", //american, 12/31/2015
};

static const char *const time_formats[] = {
        "%H:%M", //23:59
        "%H:%M", //23:59
};

static const char *const utc_strings[] = {
        "u",
        "ut",
        "utc",
        "U",
        "UT",
        "UTC",
};


/** \brief Convert string into tm structure.
 *
 * Function tries to parse time string and fills tm with appropriate values on
 * success. String is split into tokens according to TIME_DELIM delimiter. Each
 * token is converted (from left to right) into one of three categories. Date,
 * if it corresponds to one of date_formats[], time if if corresponds to one of
 * time_formats[] or UTC flag is set, if token matches to one of utc_strings[].
 * If none of these categories is detected, E_ARG is returned.
 * Time structure is overwritten by parsed-out values. If more then one valid
 * token of the same category is found, the later one is used. If only date is
 * found in time_str, used time is 00:00. If only time is found in time_str,
 * date used is today. If both date and time is present, both is used.
 * If string is successfuly parsed, E_OK is returned. On error, content
 * of tm structure is undefined and E_ARG is returned.
 *
 * \param[in] time_str Time string, usually gathered from command line.
 * \param[out] utc Set it one of utc_strings[] is found.
 * \param[out] tm Time structure filled with parsed-out time and date.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t str_to_tm(char *time_str, bool *utc, struct tm *tm)
{
        char *ret;
        char *token;
        char *saveptr = NULL;
        struct tm garbage = {0}; //strptime() failure would ruin tm values
        const time_t now = time(NULL);

        /* Default is today midnight. */
        localtime_r(&now, tm);
        tm->tm_sec = 0;
        tm->tm_min = 0;
        tm->tm_hour = 0;

        /* Separate time and date in time string. */
        token = strtok_r(time_str, TIME_DELIM, &saveptr); //first token
        while (token != NULL) {
                /* Try to parse date.*/
                for (size_t i = 0; i < ARRAY_SIZE(date_formats); ++i) {
                        ret = strptime(token, date_formats[i], &garbage);
                        if (ret != NULL && *ret == '\0') {
                                /* Conversion succeeded, fill real struct tm. */
                                strptime(token, date_formats[i], tm);
                                goto next_token;
                        }
                }

                /* Try to parse time.*/
                for (size_t i = 0; i < ARRAY_SIZE(time_formats); ++i) {
                        ret = strptime(token, time_formats[i], &garbage);
                        if (ret != NULL && *ret == '\0') {
                                /* Conversion succeeded, fill real struct tm. */
                                strptime(token, time_formats[i], tm);
                                goto next_token; //success
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

        return E_OK;
}


/** \brief Parse and store time interval string.
 *
 * Function tries to parse time interval string, fills interval_begin and
 * interval_end with appropriate values on success. Beginning and ending dates
 * (and times) are  separated with INTERVAL_DELIM, if ending date is not
 * specified, current time is used.
 * If interval string is successfuly parsed, E_OK is returned. On error, content
 * of interval_begin and interval_end is undefined and E_ARG is returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] agg_arg_str Aggregation string, usually gathered from command
 *                        line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_time_interval(struct cmdline_args *args,
                char *interval_arg_str)
{
        error_code_t primary_errno = E_OK;
        char *begin_str;
        char *end_str;
        char *remaining_str;
        char *saveptr = NULL;
        bool begin_utc = false;
        bool end_utc = false;

        assert(args != NULL && interval_arg_str != NULL);

        /* Split time interval string. */
        begin_str = strtok_r(interval_arg_str, INTERVAL_DELIM, &saveptr);
        if (begin_str == NULL) {
                print_err(E_ARG, 0, "invalid interval string \"%s\"\n",
                                interval_arg_str);
                return E_ARG;
        }
        end_str = strtok_r(NULL, INTERVAL_DELIM, &saveptr); //NULL is valid
        remaining_str = strtok_r(NULL, INTERVAL_DELIM, &saveptr);
        if (remaining_str != NULL) {
                print_err(E_ARG, 0, "invalid interval string \"%s\"\n",
                                interval_arg_str);
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
                print_err(E_ARG, 0, "zero or negative interval duration");
                return E_ARG;
        }

        /* Align begining time to closest greater rotation interval. */
        while (mktime_utc(&args->interval_begin) % FLOW_FILE_ROTATION_INTERVAL){
                args->interval_begin.tm_sec++;;
        }

        return E_OK;
}


/** \brief Parse aggregation string and save aggregation parameters.
 *
 * Function tries to parse aggregation string, fills agg_params and
 * agg_params_cnt with appropriate values on success. Aggregation arguments are
 * separated with AGG_DELIM, maximum number of arguments is MAX_AGG_PARAMS.
 * Individual arguments are parsed by libnf function lnf_fld_parse(), see libnf
 * documentation for more information.
 * If argument is successfuly parsed, next agg_param struct is filled and
 * agg_params_cnt is incremented. If field already exists, it is overwritten.
 * If all arguments are successfuly parsed, E_OK is returned. On error, content
 * of agg_params and agg_params_cnt is undefined and E_ARG is returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] agg_arg_str Aggregation string, usually gathered from command
 *                        line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_agg(struct cmdline_args *args, char *agg_arg_str)
{
        char *token;
        char *saveptr = NULL;
        int fld;
        int nb;
        int nb6;
        int agg;

        token = strtok_r(agg_arg_str, AGG_DELIM, &saveptr); //first token
        while (token != NULL) {
                size_t idx;

                if (args->agg_params_cnt >= MAX_AGG_PARAMS) {
                        print_err(E_ARG, 0, "too many aggregations "
                                        "(limit is %lu)", MAX_AGG_PARAMS);
                        return E_ARG;
                }

                /* Parse token, return field. */
                fld = lnf_fld_parse(token, &nb, &nb6);
                if (fld == LNF_FLD_ZERO_ || fld == LNF_ERR_OTHER) {
                        print_err(E_ARG, 0, "unknown aggragation field \"%s\"",
                                        token);
                        return E_ARG;
                }
                if (nb < 0 || nb > 32) {
                        print_err(E_ARG, 0, "bad number of IPv4 bits: %d", nb);
                        return E_ARG;
                } else if (nb6 < 0 || nb6 > 128) {
                        print_err(E_ARG, 0, "bad number of IPv6 bits: %d", nb6);
                        return E_ARG;
                }

                /* Lookup default aggregation key for field. */
                secondary_errno = lnf_fld_info(fld, LNF_FLD_INFO_AGGR, &agg,
                                sizeof (agg));
                assert(secondary_errno == LNF_OK);

                /* Lookup if this field is already in. */
                for (idx = 0; idx < args->agg_params_cnt; ++idx) {
                        if (args->agg_params[idx].field == fld) {
                                break; //found the same field -> overwrite it
                        }
                }

                /* Add/overwrite field and info. */
                args->agg_params[idx].field = fld;
                args->agg_params[idx].flags |= agg; //don't overwrite sort flg
                args->agg_params[idx].numbits = nb;
                args->agg_params[idx].numbits6 = nb6;

                /* In case of new parameter, increment counter. */
                if (idx == args->agg_params_cnt) {
                        args->agg_params_cnt++;
                }

                token = strtok_r(NULL, AGG_DELIM, &saveptr); //next token
        }

        return E_OK;
}


/** \brief Parse order string and save order parameters.
 *
 * Function tries to parse order string, fills agg_params and agg_params_cnt
 * with appropriate value. String is parsed by libnf function lnf_fld_parse(),
 * see libnf documentation for more information.
 * If argument is successfuly parsed next agg_params struct is filled,
 * agg_params_cnt is incremented and E_OK is returned. If there already is sort
 * flag among aggregation parameters, overwrite this parameter with new field
 * and sort flag. On error (bad string, maximum aggregation count exceeded, bad
 * numbers of bits, ...), content of agg_params and agg_params_cnt is kept
 * untouched and E_ARG is returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] order_str Order string, usually gathered from command line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_order(struct cmdline_args *args, char *order_str)
{
        int fld;
        int nb;
        int nb6;
        int agg;
        int sort;
        size_t idx;

        if (args->agg_params_cnt >= MAX_AGG_PARAMS) {
                print_err(E_ARG, 0, "too many aggregations "
                                "(limit is %lu)", MAX_AGG_PARAMS);
                return E_ARG;
        }

        /* Parse string, return field. */
        fld = lnf_fld_parse(order_str, &nb, &nb6);
        if (fld == LNF_FLD_ZERO_ || fld == LNF_ERR_OTHER) {
                print_err(E_ARG, 0, "unknown order field \"%s\"", order_str);
                return E_ARG;
        }
        if (nb < 0 || nb > 32) {
                print_err(E_ARG, 0, "bad number of IPv4 bits: %d", nb);
                return E_ARG;
        } else if (nb6 < 0 || nb6 > 128) {
                print_err(E_ARG, 0, "bad number of IPv6 bits: %d", nb6);
                return E_ARG;
        }

        /* Get default aggregation and sort key for this field. */
        secondary_errno = lnf_fld_info(fld, LNF_FLD_INFO_AGGR, &agg,
                        sizeof (agg));
        assert(secondary_errno == LNF_OK);
        secondary_errno = lnf_fld_info(fld, LNF_FLD_INFO_SORT, &sort,
                        sizeof(sort));
        assert(secondary_errno == LNF_OK);

        /* Lookup sort flag in existing parameters. */
        for (idx = 0; idx < args->agg_params_cnt; ++idx) {
                if (args->agg_params[idx].flags & LNF_SORT_FLAGS) {
                        break; //sort flag found -> overwrite it
                }
        }

        /* Save/overwrite field and info. */
        args->agg_params[idx].field = fld;
        args->agg_params[idx].flags = agg | sort;
        args->agg_params[idx].numbits = nb;
        args->agg_params[idx].numbits6 = nb6;

        /* In case of new parameter, increment counter. */
        if (idx == args->agg_params_cnt) {
                args->agg_params_cnt++;
        }

        return E_OK;
}


/** \brief Parse statistic string and save parameters.
 *
 * Function tries to parse statistic string. Statistic is only shortcut for
 * aggregation, sort and limit. Therefore statistic string is expected as
 * "aggragation[/order]". Aggregation string is passed to set_agg() and order,
 * if set, is passed to set_order(). If order is not set, DEFAULT_STAT_ORD is
 * used. If limit (-l parameter) wasn't previously set, or was disabled, it will
 * be overwritten by DEFAULT_STAT_LIMIT. It is possible to disable limit, but
 * -l 0 have to appear after -s on command line.
 * If statistic string is successfuly parsed, next agg_params struct is filled,
 * agg_params_cnt is incremented and E_OK is returned. If field already exists,
 * it is overwritten. On error, content of agg_params and agg_params_cnt is
 * undefined and E_ARG is returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] stat_arg_str Statistic string, usually gathered from command
 *                        line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_stat(struct cmdline_args *args, char *stat_arg_str)
{
        error_code_t primary_errno;
        char *stat_str;
        char *order_str;
        char *remaining_str;
        char *saveptr = NULL;

        stat_str = strtok_r(stat_arg_str, STAT_DELIM, &saveptr);
        if (stat_str == NULL) {
                print_err(E_ARG, 0, "invalid statistic string \"%s\"\n",
                                stat_arg_str);
                return E_ARG;
        }

        primary_errno = set_agg(args, stat_str);
        if (primary_errno != E_OK) {
                return primary_errno;
        }

        order_str = strtok_r(NULL, STAT_DELIM, &saveptr);
        if (order_str == NULL) {
                order_str = DEFAULT_STAT_ORD;
        }

        primary_errno = set_order(args, order_str);
        if (primary_errno != E_OK) {
                return primary_errno;
        }

        remaining_str = strtok_r(NULL, STAT_DELIM, &saveptr);
        if (remaining_str != NULL) {
                print_err(E_ARG, 0, "invalid statistic string \"%s\"\n",
                                stat_arg_str);
                return E_ARG;
        }

        if (args->rec_limit == 0) {
                args->rec_limit = DEFAULT_STAT_LIMIT;
        }

        return E_OK;
}


/** \brief Check and save filter expression string.
 *
 * Function checks filter by initialising it using libnf lnf_filter_init(). If
 * filter syntax is correct, pointer to filter string is stored in args struct
 * and E_OK is returned. Otherwise args struct remains untouched and E_ARG is
 * returned.
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
 * On error (overflow, invalid characters, negative value, ...) args struct is
 * kept untouched and E_ARG is returned.
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

        args->rec_limit = (size_t)limit;

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
                print_err(E_ARG, 0, "unknown output statistics conversion string "
                                "\"%s\"", stat_conv_str);
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

static error_code_t set_output_ip_proto_conv(struct output_params *op,
                char *ip_proto_conv_str)
{
        if (strcmp(ip_proto_conv_str, "none") == 0) {
                op->ip_proto_conv = OUTPUT_IP_PROTO_CONV_NONE;
        } else if (strcmp(ip_proto_conv_str, "str") == 0) {
                op->ip_proto_conv = OUTPUT_IP_PROTO_CONV_STR;
        } else {
                print_err(E_ARG, 0, "unknown internet protocol conversion string "
                                "\"%s\"", ip_proto_conv_str);
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
                {"output-stat-conv", required_argument, NULL,
                        OPT_OUTPUT_STAT_CONV},
                {"output-tcpflags-conv", required_argument, NULL,
                        OPT_OUTPUT_TCP_FLAGS_CONV},
                {"output-proto-conv", required_argument, NULL,
                        OPT_OUTPUT_IP_PROTO_CONV},
                {"help", no_argument, NULL, OPT_HELP},
                {"version", no_argument, NULL, OPT_VERSION},

                {0, 0, 0, 0} //termination required by getopt_long()
        };


        /* Set argument default values. */
        args->use_fast_topn = true;


        /* Loop through all the command-line arguments. */
        while (true) {
                opt = getopt_long(argc, argv, short_opts, long_opts, NULL);
                if (opt == -1) {
                        break; //all options processed successfuly
                }

                switch (opt) {
                case 'a': //aggregation
                        primary_errno = set_agg(args, optarg);
                        break;

                case 'f': //filter expression
                        primary_errno = set_filter(args, optarg);
                        break;

                case 'l': //limit
                        primary_errno = set_limit(args, optarg);
                        break;

                case 'o': //order
                        primary_errno = set_order(args, optarg);
                        break;

                case 'r': //path to input file or directory
                        args->path_str = optarg;
                        input_arg_cnt++;
                        break;

                case 's': //statistic
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

                case OPT_OUTPUT_STAT_CONV:
                        primary_errno = set_output_stat_conv(
                                        &args->output_params, optarg);
                        break;

                case OPT_OUTPUT_TCP_FLAGS_CONV:
                        primary_errno = set_output_tcp_flags_conv(
                                        &args->output_params, optarg);
                        break;

                case OPT_OUTPUT_IP_PROTO_CONV:
                        primary_errno = set_output_ip_proto_conv(
                                        &args->output_params, optarg);
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

        if (optind != argc) { //remaining arguments
                print_err(E_ARG, 0, "unknown positional argument \"%s\"",
                                argv[optind]);
                fprintf(stderr, usage_string);
                return E_ARG;
        }

        /* Correct data input check. */
        if (input_arg_cnt == 0) {
                print_err(E_ARG, 0, "missing data input (-i or -r)");
                fprintf(stderr, usage_string);
                return E_ARG;
        } else if (input_arg_cnt > 1) {
                print_err(E_ARG, 0, "only one data input allowed (-i or -r)");
                fprintf(stderr, usage_string);
                return E_ARG;
        }
        if (args->path_str && (strlen(args->path_str) >= PATH_MAX)) {
                print_err(E_ARG, 0, "path string too long (limit is %lu)",
                                PATH_MAX);
                return E_ARG;
        }

        /* Determine working mode. */
        if (args->agg_params_cnt == 0) { //no aggregation -> list records
                args->working_mode = MODE_LIST;
        } else if (args->agg_params_cnt == 1) { //aggregation or ordering
                if (args->agg_params[0].flags & LNF_SORT_FLAGS) {
                        args->working_mode = MODE_SORT;
                } else {
                        args->working_mode = MODE_AGGR;
                }
        } else { //aggregation
                args->working_mode = MODE_AGGR;
        }

        /* Fast top-N makes sense only under certain conditions.
         * Reasons to disable fast top-N algorithm:
         * - user request by command line argument
         * - no record limit (all records would be exchanged anyway)
         * - sort key isn't statistical field (flows, packets, bytes, ...)
         */
        if (args->working_mode == MODE_AGGR) {
                if (args->rec_limit == 0) {
                        args->use_fast_topn = false;
                }
                /* Only statistical items makes sense. */
                for (size_t i = 0; i < args->agg_params_cnt; ++i) {
                        if (args->agg_params[i].flags & LNF_SORT_FLAGS) {
                                sort_key = args->agg_params[i].field;
                                break;
                        }
                }
                if (sort_key < LNF_FLD_DOCTETS ||
                                sort_key > LNF_FLD_AGGR_FLOWS) {
                        args->use_fast_topn = false;
                }
        }

        /* Set default output parameters. */
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

                if (args->output_params.ip_proto_conv ==
                                OUTPUT_IP_PROTO_CONV_UNSET) {
                        args->output_params.ip_proto_conv =
                                OUTPUT_IP_PROTO_CONV_STR;
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

                if (args->output_params.ip_proto_conv ==
                                OUTPUT_IP_PROTO_CONV_UNSET) {
                        args->output_params.ip_proto_conv =
                                OUTPUT_IP_PROTO_CONV_NONE;
                }
                break;
        default:
                assert(!"unkwnown output parameters format");
        }


#ifdef DEBUG
        print_debug("------------------------------------------------------");
        print_debug("mode: %s", working_mode_to_str(args->working_mode));
        if (args->working_mode == MODE_AGGR && args->use_fast_topn) {
                print_debug("flags: using fast top-N algorithm");
        }
        for (size_t i = 0; i < args->agg_params_cnt; ++i) {
                struct agg_param *ap = args->agg_params + i;
                print_debug("aggregation %lu: %d, 0x%x, (%d, %d)", i, ap->field,
                                ap->flags, ap->numbits,ap->numbits6);
        }
        if(args->filter_str != NULL) {
                print_debug("filter: %s", args->filter_str);
        }

        if (args->path_str != NULL) {
                print_debug("path: %s", args->path_str);
        } else {
                char begin[255], end[255];

                strftime(begin, sizeof(begin), "%c", &args->interval_begin);
                strftime(end, sizeof(end), "%c", &args->interval_end);
                print_debug("interval: %s - %s", begin, end);
        }
        print_debug("------------------------------------------------------\n");
#endif //DEBUG

        return E_OK;
}
