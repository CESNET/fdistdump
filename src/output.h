/**
 * @brief Declarations for printing IP flow records and fields.
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

#include <inttypes.h>           // for fixed-width integer types
#include <stdbool.h>            // for bool

#include <libnf.h>              // for lnf_mem_t


// forward declarations
struct fields;
struct metadata_summ;
struct processed_summ;


/*
 * Data types declarations.
 */
typedef enum {
    OUTPUT_ITEM_UNSET,
    OUTPUT_ITEM_YES,
    OUTPUT_ITEM_NO,
} output_item_t;

typedef enum {
    OUTPUT_FORMAT_UNSET,
    OUTPUT_FORMAT_PRETTY,
    OUTPUT_FORMAT_CSV,
} output_format_t;

typedef enum {
    OUTPUT_TS_CONV_UNSET,
    OUTPUT_TS_CONV_NONE,
    OUTPUT_TS_CONV_PRETTY,
} output_ts_conv_t;

typedef enum {
    OUTPUT_VOLUME_CONV_UNSET,
    OUTPUT_VOLUME_CONV_NONE,
    OUTPUT_VOLUME_CONV_METRIC_PREFIX,
    OUTPUT_VOLUME_CONV_BINARY_PREFIX,
} output_stat_conv_t;

typedef enum {
    OUTPUT_TCP_FLAGS_CONV_UNSET,
    OUTPUT_TCP_FLAGS_CONV_NONE,
    OUTPUT_TCP_FLAGS_CONV_STR,
} output_tcp_flags_conv_t;

typedef enum {
    OUTPUT_IP_ADDR_CONV_UNSET,
    OUTPUT_IP_ADDR_CONV_NONE,
    OUTPUT_IP_ADDR_CONV_STR,
} output_ip_addr_conv_t;

typedef enum {
    OUTPUT_IP_PROTO_CONV_UNSET,
    OUTPUT_IP_PROTO_CONV_NONE,
    OUTPUT_IP_PROTO_CONV_STR,
} output_ip_proto_conv_t;

typedef enum {
    OUTPUT_DURATION_CONV_UNSET,
    OUTPUT_DURATION_CONV_NONE,
    OUTPUT_DURATION_CONV_STR,
} output_duration_conv_t;


struct output_params {
    // items
    output_item_t print_records;
    output_item_t print_processed_summ;
    output_item_t print_metadata_summ;

    // format
    output_format_t format;
    bool ellipsize;
    bool rich_header;

    // conversions
    output_ts_conv_t ts_conv;
    output_stat_conv_t volume_conv;
    output_tcp_flags_conv_t tcp_flags_conv;
    output_ip_addr_conv_t ip_addr_conv;
    output_ip_proto_conv_t ip_proto_conv;
    output_duration_conv_t duration_conv;
};


/*
 * Public function prototypes.
 */
void
output_init(struct output_params op, const struct fields *const fields);

void
output_free(void);

void
print_stream_names();

void
print_stream_next(const uint8_t *const data);

void
print_batch(lnf_mem_t *const lnf_mem, const uint64_t limit);

void
print_processed_summ(const struct processed_summ *const s,
                     const double duration);

void
print_metadata_summ(const struct metadata_summ *const s);
