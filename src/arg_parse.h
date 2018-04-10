/**
 * @brief Argument parsing and usage/help printing.
 */

/*
 * Copyright 2015-2018 CESNET
 *
 * This file is part of Fdistdump.
 *
 * Fdistdump is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Fdistdump is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Fdistdump.  If not, see <http://www.gnu.org/licenses/>.
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
