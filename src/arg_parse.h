/** Argument parsing declarations.
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

#pragma once


#include "common.h"
#include "output.h"

#include <stddef.h> //size_t
#include <stdbool.h>


//TODO: include shared_task_ctx
struct cmdline_args {
        working_mode_t working_mode; //working mode (records, aggregation, topN)
        struct field_info fields[LNF_FLD_TERM_];

        char *filter_str; //filter expression string
        char *path_str; //path string

        size_t rec_limit; //read/aggregate/topN record limit

        struct tm time_begin; //beginning of the time range
        struct tm time_end; //end of the time range

        bool use_fast_topn; //enables fast top-N algorithm
        bool use_bfindex;  // enables Bloom filter indexes

        struct output_params output_params; //output (printing) parameters
        progress_bar_type_t progress_bar_type;
        char *progress_bar_dest;
};


/**
 * @brief  Parse command line arguments and fill parameters structure.
 *
 * @param[out] args Structure with parsed command line argument and other
 *                  program settings.
 * @param argc Command line argument vector size.
 * @param argv Command line argument vector.
 * @param root_proc True if process is root, false otherwise.
 *
 * @return E_OK on success, E_HELP if help or usage strings were printed, E_ARG
 *         otherwise.
 */
error_code_t
arg_parse(struct cmdline_args *args, int argc, char *const argv[],
          bool root_proc);

/**
 * @brief Free parameters structure allocated by arg_parse().
 *
 * @param[in] args Structure with parsed command line argument and other program
 *                 settings.
 */
void
arg_free(struct cmdline_args *args);
