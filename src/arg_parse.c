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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h> //PATH_MAX, NAME_MAX

#include <getopt.h>
#include <libnf.h>


enum { //command line options
        OPT_NO_FAST_TOPN = 256, //above ASCII
};


static const char *const time_formats[] = {
        "%H:%M %d.%m.%Y", //23:59 31.12.2015
        "%H:%M:%S %d.%m.%Y", //23:59:59 31.12.2015

        "%d.%m.%Y %H:%M", //31.12.2015 23:59
        "%d.%m.%Y %H:%M:%S", //31.12.2015 23:59:59
};


static int str_to_tm(struct tm *time, const char *time_str)
{
        size_t idx;
        char *ret;
        static const size_t time_formats_cnt = sizeof(time_formats) /
                sizeof(*time_formats);

        memset(time, 0, sizeof(struct tm));

        for (idx = 0; idx < time_formats_cnt; ++idx) {
                ret = strptime(time_str, time_formats[idx], time);

                if (ret != NULL && *ret == '\0') {
                        return E_OK; //conversion successfull
                }
        }

        return E_ARG; //conversion failure
}


static int set_interval(struct cmdline_args *args, char *interval_arg_str)
{
        char *begin_str, *end_str, *remaining_str, *saveptr;
        int ret;

        /* Split time interval string. */
        begin_str = strtok_r(interval_arg_str, INTERVAL_SEPARATOR, &saveptr);
        if (begin_str == NULL) {
                printf("invalid interval string \"%s\"\n", interval_arg_str);
                return E_ARG;
        }
        end_str = strtok_r(NULL, INTERVAL_SEPARATOR, &saveptr); //NULL is valid
        remaining_str = strtok_r(NULL, INTERVAL_SEPARATOR, &saveptr);
        if (remaining_str != NULL) {
                printf("invalid interval string \"%s\"\n", interval_arg_str);
                return E_ARG;
        }

        /* Convert time strings to struct tm. */
        ret = str_to_tm(&args->interval_begin, begin_str);
        if (ret != E_OK) {
                printf("invalid date format \"%s\"\n", begin_str);
                return E_ARG;
        }
        if (end_str == NULL) { //NULL means until now
                const time_t now = time(NULL);
                localtime_r(&now, &args->interval_end);
        } else {
                ret = str_to_tm(&args->interval_end, end_str);
                if (ret != E_OK) {
                        printf("invalid date format \"%s\"\n", end_str);
                        return E_ARG;
                }
        }

        /* Check interval sanity. */
        if (diff_tm(args->interval_end, args->interval_begin) <= 0.0) {
                printf("invalid interval duration\n");
                return E_ARG;
        }

        /* Align begining time to closest greater rotation interval. */
        while (mktime(&args->interval_begin) % FLOW_FILE_ROTATION_INTERVAL) {
                args->interval_begin.tm_sec++;;
        }

        return E_OK;
}


/** \brief Parse aggregation string and save aggregation parameters.
 *
 * Function tries to parse aggregation string, fills agg_params and
 * agg_params_cnt with appropriate values on success. Aggregation arguments are
 * separated with commas, maximum number of arguments is MAX_AGG_PARAMS.
 * Individual arguments are parsed by libnf function lnf_fld_parse(), see libnf
 * documentation for more information.
 * If argument is successfully parsed, next agg_params struct is filled and
 * agg_params_cnt is incremented. If field allready exists, it is overwritten.
 * If all arguments are successfully parsed, E_OK is returned. On error, content
 * of agg_params and agg_params_cnt is undefined and E_ARG is returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] agg_arg_str Aggregation string, usually gathered from command
 *                        line.
 * \return Error code. E_OK or E_ARG.
 */
static int set_agg(struct cmdline_args *args, char *agg_arg_str)
{
        char *token, *saveptr;
        int ret, fld, nb, nb6, agg;

        token = strtok_r(agg_arg_str, AGG_SEPARATOR, &saveptr); //first token
        while (token != NULL) {
                size_t idx;

                if (args->agg_params_cnt >= MAX_AGG_PARAMS) {
                        printf("agg count err\n");
                        return E_ARG;
                }

                /* Parse token, return field. */
                fld = lnf_fld_parse(token, &nb, &nb6);
                if (fld == LNF_FLD_ZERO_ || fld == LNF_ERR_OTHER) {
                        printf("agg field err \"%s\"\n", token);
                        return E_ARG;
                }
                if (nb < 0 || nb > 32 || nb6 < 0 || nb6 > 128) {
                        printf("agg numbits err\n");
                        return E_ARG;
                }

                /* Lookup default aggregation key for field. */
                ret = lnf_fld_info(fld, LNF_FLD_INFO_AGGR, &agg, sizeof (agg));
                assert(ret == LNF_OK); //should not happen

                /* Look if this field is allready in. */
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

                token = strtok_r(NULL, AGG_SEPARATOR, &saveptr); //next token
        }

        return E_OK;
}


/** \brief Parse order string and save order parameters.
 *
 * Function tries to parse order string, fills agg_params and agg_params_cnt
 * with appropriate value (because ordering implies aggregation). String is
 * parsed by libnf function lnf_fld_parse(), see libnf documentation for more
 * information.
 * If argument is successfully parsed next agg_params struct is filled,
 * agg_params_cnt is incremented and E_OK is returned. If there allready is sort
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
static int set_order(struct cmdline_args *args, char *order_str)
{
        int ret, fld, nb, nb6, agg, sort;
        size_t idx;

        if (args->agg_params_cnt >= MAX_AGG_PARAMS) {
                printf("agg count err\n");
                return E_ARG;
        }

        /* Parse string, return field. */
        fld = lnf_fld_parse(order_str, &nb, &nb6);
        if (fld == LNF_FLD_ZERO_ || fld == LNF_ERR_OTHER) {
                printf("order string err \"%s\"\n", order_str);
                return E_ARG;
        }
        if (nb < 0 || nb > 32 || nb6 < 0 || nb6 > 128) {
                printf("order numbits err\n");
                return E_ARG;
        }

        /* Get default aggregation and sort key for this field. */
        ret = lnf_fld_info(fld, LNF_FLD_INFO_AGGR, &agg, sizeof (agg));
        assert(ret == LNF_OK);
        ret = lnf_fld_info(fld, LNF_FLD_INFO_SORT, &sort, sizeof(sort));
        assert(ret == LNF_OK);

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
 * aggregation, sort and limit. Therefore statistic string expected as
 * "aggragation[/order]". Aggregation is passed to set_agg() and order, if set,
 * is passed to set_order(). If order is not set, DEFAULT_STAT_ORD is used.
 * If limit was not set yet (-l parameter), DEFAULT_STAT_LIMIT is used.
 * If statistic string is successfully parsed, next agg_params struct is filled,
 * agg_params_cnt is incremented and E_OK is returned. If field allready exists,
 * it is overwritten. On error, content of agg_params and agg_params_cnt is
 * undefined and E_ARG is returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] stat_arg_str Statistic string, usually gathered from command
 *                        line.
 * \return Error code. E_OK or E_ARG.
 */
static int set_stat(struct cmdline_args *args, char *stat_arg_str)
{
        char *stat_str, *order_str, *saveptr;
        int ret;

        stat_str = strtok_r(stat_arg_str, STAT_SEPARATOR, &saveptr);
        if (stat_str == NULL) {
                printf("stat string err \"%s\"\n", stat_arg_str);
                return E_ARG;
        }

        ret = set_agg(args, stat_str);
        if (ret != E_OK) {
                return E_ARG;
        }

        order_str = strtok_r(NULL, STAT_SEPARATOR, &saveptr);
        if (order_str == NULL) {
                order_str = DEFAULT_STAT_ORD;
        }

        ret = set_order(args, order_str);
        if (ret != E_OK) {
                return E_ARG;
        }

        order_str = strtok_r(NULL, STAT_SEPARATOR, &saveptr);
        if (order_str != NULL) {
                printf("stat string err \"%s\"\n", order_str);
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
static int set_filter(struct cmdline_args *args, char *filter_str)
{
        int ret;
        lnf_filter_t *filter;

        /* Try to initialize filter. */
        ret = lnf_filter_init(&filter, filter_str);
        if (ret != LNF_OK) {
                printf("cannot initialise filter \"%s\"\n", filter_str);
                return E_ARG;
        }

        lnf_filter_free(filter);
        args->filter_str = filter_str;
        return E_OK;
}


/** \brief Check, convert and save limit string.
 *
 * Function converts limit string into unsigned integer. If string is correct
 * and conversion was successfull, args->limit is set and E_OK is returned.
 * On error (overflow, invalid characters, negative value, ...) args struct is
 * kept untouched and E_ARG is returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] limit_str Limit string, usually gathered from command line.
 * \return Error code. E_OK or E_ARG.
 */
static int set_limit(struct cmdline_args *args, char *limit_str)
{
        char *endptr;
        long long int limit;

        errno = 0;
        limit = strtoll(limit_str, &endptr, 0);

        /* Check for various possible errors. */
        if (errno != 0) {
                perror(limit_str);
                return E_ARG;
        }
        if (*endptr != '\0') //remaining characters
        {
                printf("%s: invalid limit\n", limit_str);
                return E_ARG;
        }
        if (limit < 0) //negatve limit
        {
                printf("%s: negative limit\n", limit_str);
                return E_ARG;
        }

        args->rec_limit = (size_t)limit;
        return E_OK;
}


void set_defaults(struct cmdline_args *args)
{
        args->working_mode = MODE_REC;
        args->use_fast_topn = true;
}


int arg_parse(struct cmdline_args *args, int argc, char **argv)
{
        int opt, ret = E_OK, sort_key = LNF_FLD_ZERO_;
        bool help = false, bad_arg = false;
        size_t input_arg_cnt = 0;

        const char usage_string[] = "Usage: %s options\n";
        const char help_string[] = "help\n";

        const struct option long_opts[] = {
                {"aggregation", required_argument, NULL, 'a'},
                {"filter", required_argument, NULL, 'f'},
                {"time-interval", required_argument, NULL, 'i'},
                {"limit", required_argument, NULL, 'l'},
                {"order", required_argument, NULL, 'o'},
                {"statistic", required_argument, NULL, 's'},
                {"read", required_argument, NULL, 'r'},

                {"no-fast-topn", no_argument, NULL, OPT_NO_FAST_TOPN},

                {"help", no_argument, NULL, 'h'},
                {0, 0, 0, 0} //required by getopt_long()
        };
        const char *short_opts = "a:f:i:l:o:s:r:h";

        set_defaults(args);

        while (!bad_arg && !help) {
                opt = getopt_long(argc, argv, short_opts, long_opts, NULL);
                if (opt == -1) {
                        break; //all options processed successfully
                }

                switch (opt) {
                case 'a'://aggregation
                        ret = set_agg(args, optarg);
                        break;
                case 'f'://filter expression
                        ret = set_filter(args, optarg);
                        break;
                case 'i'://time interval
                        ret = set_interval(args, optarg);
                        input_arg_cnt++;
                        break;
                case 'l'://limit
                        ret = set_limit(args, optarg);
                        break;
                case 'o'://order
                        ret = set_order(args, optarg);
                        break;
                case 's'://statistic
                        ret = set_stat(args, optarg);
                        break;
                case 'r'://path to read file(s) from
                        args->path_str = optarg;
                        input_arg_cnt++;
                        break;
                case 'h'://help
                        help = true;
                        break;

                case OPT_NO_FAST_TOPN: //disable fast top-N algorithm
                        args->use_fast_topn = false;
                        break;

                default: /* '?' or '0' */
                        bad_arg = true;
                        break;
                }

                if (ret != E_OK) {
                        bad_arg = true;
                }
        }

        if (help) //print help and exit
        {
                printf(usage_string, argv[0]);
                printf("%s", help_string);
                return E_HELP;
        }

        if (bad_arg) { //error allready reported
                return E_ARG;
        }
        if (optind != argc) //remaining arguments
        {
                fprintf(stderr, usage_string, argv[0]);
                return E_ARG;
        }

        /* Correct data input check. */
        if (input_arg_cnt == 0) {
                fprintf(stderr, "Missing -i or -r.\n");
                fprintf(stderr, usage_string, argv[0]);
                return E_ARG;
        } else if (input_arg_cnt > 1) {
                fprintf(stderr, "-i and -r are mutually exclusive.\n");
                fprintf(stderr, usage_string, argv[0]);
                return E_ARG;
        }
        if (args->path_str && (strlen(args->path_str) >= PATH_MAX)) {
                errno = ENAMETOOLONG;
                perror(args->path_str);
                return E_ARG;
        }

        /* Determine working mode. */
        if (args->agg_params_cnt == 0) { //no aggregation -> list records
                args->working_mode = MODE_REC;
        } else if (args->agg_params_cnt == 1) { //aggregation or ordering
                if (args->agg_params[0].flags & LNF_SORT_FLAGS) {
                        args->working_mode = MODE_ORD;
                } else {
                        args->working_mode = MODE_AGG;
                }
        } else { //aggregation
                args->working_mode = MODE_AGG;
        }

        /* Fast top-N makes sense only under certain conditions. */
        if (args->working_mode == MODE_AGG) {
                /* No record limit - all records have to be sent. */
                if (args->rec_limit == 0) { //no record limit
                        args->use_fast_topn = false;
                }
                /* Only statistical items makes sense. */
                for (size_t i = 0; i < args->agg_params_cnt; ++i) {
                        if (args->agg_params[i].flags & LNF_SORT_FLAGS) {
                                sort_key = args->agg_params[i].field;
                        }
                }
                if (sort_key < LNF_FLD_DOCTETS ||
                                sort_key > LNF_FLD_AGGR_FLOWS) {
                        args->use_fast_topn = false;
                }
        }


#ifdef DEBUG
        char buff_from[255], buff_to[255];

        printf("aggregation: \n");
        for (size_t i = 0; i < args->agg_params_cnt; ++i) {
                struct agg_params *ap = args->agg_params + i;
                printf("\t%d, 0x%x, (%d, %d)\n", ap->field, ap->flags,
                                ap->numbits, ap->numbits6);
        }
        printf("filter: %s\n", args->filter_str);

        strftime(buff_from, sizeof(buff_from), "%c", &args->interval_begin);
        strftime(buff_to, sizeof(buff_to), "%c", &args->interval_end);

        printf("interval: %s - %s\n", buff_from, buff_to);

        while (diff_tm(args->interval_end, args->interval_begin) > 0.0) {
                strftime(buff_from, sizeof(buff_from), "%Y/%m/%d/"
                                FLOW_FILE_NAME_FORMAT, &args->interval_begin);
                printf("%s\n", buff_from);
                args->interval_begin.tm_sec += FLOW_FILE_ROTATION_INTERVAL;
                mktime(&args->interval_begin); //normalization
        }
#endif //DEBUG

        return E_OK;
}

#if 0
int main(int argc, char **argv)
{
        int ret, err = EXIT_SUCCESS;
        struct cmdline_args args;

        ret = arg_parse(&args, argc, argv);
        switch (ret) {
        case E_OK:
                //continue
                break;
        case E_HELP:
                break;
        default:
                err = ret;
                break;
        }

        return err;
}
#endif //0
