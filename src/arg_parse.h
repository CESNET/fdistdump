/**
 * @brief Argument parsing and usage/help printing.
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

#include <inttypes.h>  // for fixed-width integer types
#include <stdbool.h>   // for bool
#include <time.h>      // for struct tm

#include "fields.h"    // for struct fields
#include "common.h"    // for error_code_t, field_info, progress...
#include "output.h"    // for struct output_params


/*
 * Data types declarations.
 */
struct cmdline_args {
    working_mode_t working_mode;  // working mode (records, aggregation, topN)

    char *const *paths;  // paths_cnt sized array of user specified paths
    uint64_t paths_cnt;
    struct tm time_begin;  // beginning of the time range
    struct tm time_end;    // end of the time range

    char *filter_str;  // input filter expression string
    uint64_t rec_limit;  // output record limit
    bool use_tput;  // enables the TPUT algorithm
    bool use_bfindex;    // enables the Bloom filter indexes

    progress_bar_type_t progress_bar_type;
    char *progress_bar_dest;

    struct output_params output_params;

    struct fields fields;  // libnf fields (aggregation/sort/output keys)
};


/*
 * Public function prototypes.
 */
error_code_t
arg_parse(struct cmdline_args *args, int argc, char *const argv[],
          bool root_proc);
