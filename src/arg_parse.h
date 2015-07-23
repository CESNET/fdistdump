/**
 * \file arg_parse.h
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

#ifndef ARG_PARSE_H
#define ARG_PARSE_H

#include "common.h"

#include <stddef.h> //size_t
#include <time.h> //struct tm

#define AGG_SEPARATOR "," //srcport,srcip
#define STAT_SEPARATOR "/" //statistic/order
#define INTERVAL_SEPARATOR "#" //begin#end

#define DEFAULT_STAT_ORD "flows"
#define DEFAULT_STAT_LIMIT 10

#define FILE_ROTATION_INTERVAL 300 //seconds
#define FILE_NAME_FORMAT "nfcapd.%Y%m%d%H%M"

typedef struct {
        working_mode_t working_mode; //working mode (records, aggregation, topN)

        agg_params_t agg_params[MAX_AGG_PARAMS]; //aggregation parameters
        size_t agg_params_cnt; //aggregation parameters count

        char *filter_str; //filter expression string
        size_t rec_limit; //read/aggregate/topN record limit

        char *path_str; //path string
        struct tm interval_begin, interval_end; //begin and end of interval
} params_t;


/** \brief Parse command line arguments and fill params struct.
 *
 * If all arguments are successfully parsed and stored, E_OK is returned.
 * If help was required, help string is printed and E_HELP is returned.
 * On error (invalid options or arguments, ...), error string is printed and
 * E_ARG is returned.
 *
 * \param[out] params Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] argc Argument count.
 * \param[in] argv Command line argument strings.
 * \return Error code. E_OK, E_HELP or E_ARG.
 */
int arg_parse(params_t *params, int argc, char **argv);

#endif //ARG_PARSE_H
