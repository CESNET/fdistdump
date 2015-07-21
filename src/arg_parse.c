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

#include "arg_parse.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>

#include <getopt.h>
#include <libnf.h>


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
 * \param[in,out] params Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] agg_arg_str Aggregation string, usually gathered from command
 *                        line.
 * \return Error code. E_OK or E_ARG.
 */
static int set_agg(params_t *params, char *agg_arg_str)
{
        char *token, *saveptr;
        int ret, fld, nb, nb6, agg;

        token = strtok_r(agg_arg_str, TOKEN_SEPARATOR, &saveptr); //first token
        while (token != NULL) {
                size_t idx;

                if (params->agg_params_cnt >= MAX_AGG_PARAMS) {
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
                for (idx = 0; idx < params->agg_params_cnt; ++idx) {
                        if (params->agg_params[idx].field == fld) {
                                break; //found the same field -> overwrite it
                        }
                }

                /* Add/overwrite field and info. */
                params->agg_params[idx].field = fld;
                params->agg_params[idx].flags |= agg; //don't overwrite sort flg
                params->agg_params[idx].numbits = nb;
                params->agg_params[idx].numbits6 = nb6;

                /* In case of new parameter, increment counter. */
                if (idx == params->agg_params_cnt) {
                        params->agg_params_cnt++;
                }

                token = strtok_r(NULL, TOKEN_SEPARATOR, &saveptr); //next token
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
 * \param[in,out] params Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] order_str Order string, usually gathered from command line.
 * \return Error code. E_OK or E_ARG.
 */
static int set_order(params_t *params, char *order_str)
{
        int ret, fld, nb, nb6, agg, sort;
        size_t idx;

        if (params->agg_params_cnt >= MAX_AGG_PARAMS) {
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

        /* TODO: neprisel jsem na zpusob, jak sortit bez agregace. Pokud
         * soucasti flagu neni zadny agregacni flag (pouze radici), tak
         * lnf_mem_fadd() prida LNF_AGGR_KEY i pro pole jako bytes, pkts, flows,
         * kde to nedava zadny smysl (ma byt LNF_AGGR_SUM). Coz je samozrejme
         * nezadouci.
         */
        /* Lookup default aggregation and sort key for field. */
        ret = lnf_fld_info(fld, LNF_FLD_INFO_AGGR, &agg, sizeof (agg));
        assert(ret == LNF_OK); //should not happen
        ret = lnf_fld_info(fld, LNF_FLD_INFO_SORT, &sort, sizeof(sort));
        assert(ret == LNF_OK); //should not happen

        /* Look for sort flag in existing parameters. */
        for (idx = 0; idx < params->agg_params_cnt; ++idx) {
                if (params->agg_params[idx].flags & LNF_SORT_FLAGS) {
                        break; //sort flag found -> overwrite it
                }
        }

        /* Save/overwrite field and info. */
        params->agg_params[idx].field = fld;
        params->agg_params[idx].flags = agg | sort;
        params->agg_params[idx].numbits = nb;
        params->agg_params[idx].numbits6 = nb6;

        /* In case of new parameter, increment counter. */
        if (idx == params->agg_params_cnt) {
                params->agg_params_cnt++;
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
 * \param[in,out] params Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] stat_arg_str Statistic string, usually gathered from command
 *                        line.
 * \return Error code. E_OK or E_ARG.
 */
static int set_stat(params_t *params, char *stat_arg_str)
{
        char *stat_str, *order_str, *saveptr;
        int ret;

        stat_str = strtok_r(stat_arg_str, STAT_SEPARATOR, &saveptr);
        if (stat_str == NULL) {
                printf("stat string err \"%s\"\n", stat_arg_str);
                return E_ARG;
        }

        ret = set_agg(params, stat_str);
        if (ret != E_OK) {
                return E_ARG;
        }

        order_str = strtok_r(NULL, STAT_SEPARATOR, &saveptr);
        if (order_str == NULL) {
                order_str = DEFAULT_STAT_ORD;
        }

        ret = set_order(params, order_str);
        if (ret != E_OK) {
                return E_ARG;
        }

        order_str = strtok_r(NULL, STAT_SEPARATOR, &saveptr);
        if (order_str != NULL) {
                printf("stat string err \"%s\"\n", order_str);
                return E_ARG;
        }

        if (params->rec_limit == 0) {
                params->rec_limit = DEFAULT_STAT_LIMIT;
        }

        return E_OK;
}


/** \brief Check and save filter expression string.
 *
 * Function checks filter by initialising it using libnf lnf_filter_init(). If
 * filter syntax is correct, pointer to filter string is stored in params struct
 * and E_OK is returned. Otherwise params struct remains untouched and E_ARG is
 * returned.
 *
 * \param[in,out] params Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] filter_str Filter expression string, usually gathered from command
 *                       line.
 * \return Error code. E_OK or E_ARG.
 */
static int set_filter(params_t *params, char *filter_str)
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
        params->filter_str = filter_str;
        return E_OK;
}


/** \brief Check, convert and save limit string.
 *
 * Function converts limit string into unsigned integer. If string is correct
 * and conversion was successfull, params->limit is set and E_OK is returned.
 * On error (overflow, invalid characters, negative value, ...) params struct is
 * kept untouched and E_ARG is returned.
 *
 * \param[in,out] params Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] limit_str Limit string, usually gathered from command line.
 * \return Error code. E_OK or E_ARG.
 */
static int set_limit(params_t *params, char *limit_str)
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

        params->rec_limit = (size_t)limit;
        return E_OK;
}


int arg_parse(params_t *params, int argc, char **argv)
{
        params->working_mode = MODE_REC; //default mode

        int opt, ret = E_OK;
        bool help = false, bad_arg = false;

        char usage_string[] = "Usage: %s options\n";
        char help_string[] = "help\n";

        static struct option long_opts[] = {
                {"aggregation", required_argument, NULL, 'a'},
                {"filter", required_argument, NULL, 'f'},
                {"limit", required_argument, NULL, 'l'},
                {"order", required_argument, NULL, 'o'},
                {"statistic", required_argument, NULL, 's'},

                {"read", required_argument, NULL, 'r'},
                {"help", no_argument, NULL, 'h'},
                {0, 0, 0, 0} //required by getopt_long()
        };
        char *short_opts = "a:f:l:o:s:r:h";

        while (!bad_arg && !help) {
                opt = getopt_long(argc, argv, short_opts, long_opts, NULL);
                if (opt == -1) {
                        break; //all options processed successfully
                }

                switch (opt) {
                case 'a'://aggregation
                        params->working_mode = MODE_AGG;
                        ret = set_agg(params, optarg);
                        break;
                case 'f'://filter expression
                        ret = set_filter(params, optarg);
                        break;
                case 'l'://limit
                        ret = set_limit(params, optarg);
                        break;
                case 'o'://order
                        params->working_mode = MODE_AGG;
                        ret = set_order(params, optarg);
                        break;
                case 's'://statistic
                        params->working_mode = MODE_AGG;
                        ret = set_stat(params, optarg);
                        break;
                case 'r'://read
                        params->dir_str = optarg;
                        break;
                case 'h'://help
                        help = true;
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
        if (params->dir_str == NULL) { //missing mandatory arguments
                fprintf(stderr, usage_string, argv[0]);
                return E_ARG;
        }


        printf("Aggregation: \n");
        for (size_t i = 0; i < params->agg_params_cnt; ++i) {
                agg_params_t *ap = params->agg_params + i;
                printf("%d, 0x%x, (%d, %d)\n", ap->field, ap->flags,
                                ap->numbits, ap->numbits6);
        }
        printf("Filter: %s\n", params->filter_str);

        return E_OK;
}

#if 0
int main(int argc, char **argv)
{
        int ret, err = EXIT_SUCCESS;
        params_t params;

        ret = arg_parse(&params, argc, argv);
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
