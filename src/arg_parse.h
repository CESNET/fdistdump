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
#include "output.h"

#include <stddef.h> //size_t
#include <stdbool.h>


#define STAT_DELIM "/" //statistic/order
#define INTERVAL_DELIM "#" //begin#end
#define SORT_DELIM "#" //flows#asc
#define TIME_DELIM " \t\n\v\f\r" //whitespace

#define DEFAULT_LIST_FIELDS "first,pkts,bytes,srcip,dstip,srcport,dstport,proto"
#define DEFAULT_SORT_FIELDS DEFAULT_LIST_FIELDS
#define DEFAULT_AGGR_FIELDS "duration,flows,pkts,bytes,bps,pps,bpp"
#define DEFAULT_STAT_SORT_KEY "flows"
#define DEFAULT_STAT_REC_LIMIT 10


//TODO: include shared_task_ctx
struct cmdline_args {
        working_mode_t working_mode; //working mode (records, aggregation, topN)
        struct field_info fields[LNF_FLD_TERM_];

        char *filter_str; //filter expression string
        char *path_str; //path string

        size_t rec_limit; //read/aggregate/topN record limit

        struct tm interval_begin; //begin of time interval
        struct tm interval_end; //end of time interval

        bool use_fast_topn; //enables fast top-N algorithm

        struct output_params output_params; //output (printing) parameters
        progress_bar_t progress_bar;
};


/** \brief Parse command line arguments and fill parameters structure.
 *
 * If all arguments are successfully parsed and stored, E_OK is returned.
 * If help or version was required, help string is printed and E_PASS is
 * returned.
 * On error (invalid options or arguments, ...), error string is printed and
 * E_ARG is returned.
 *
 * \param[out] params Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] argc Argument count.
 * \param[in] argv Command line argument strings.
 * \return Error code. E_OK, E_PASS or E_ARG.
 */
error_code_t arg_parse(struct cmdline_args *args, int argc, char **argv);

#endif //ARG_PARSE_H
