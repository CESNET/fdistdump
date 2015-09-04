/**
 * \file output.h
 * \brief Prototypes of functions for printing records and fields.
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

#ifndef OUTPUT_H
#define OUTPUT_H

#include "common.h"

#include <stddef.h> //size_t

#include <libnf.h>


typedef enum {
        OUTPUT_FORMAT_UNSET,
        OUTPUT_FORMAT_PRETTY,
        OUTPUT_FORMAT_CSV,
} output_format_t;

typedef enum {
        OUTPUT_TS_CONV_UNSET,
        OUTPUT_TS_CONV_NONE,
        OUTPUT_TS_CONV_STR,
} output_ts_conv_t;

typedef enum {
        OUTPUT_STAT_CONV_UNSET,
        OUTPUT_STAT_CONV_NONE,
        OUTPUT_STAT_CONV_METRIC_PREFIX,
        OUTPUT_STAT_CONV_BINARY_PREFIX,
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
        OUTPUT_SUMMARY_UNSET,
        OUTPUT_SUMMARY_YES,
        OUTPUT_SUMMARY_NO,
} output_summary_t;


struct output_params {
        output_format_t format;

        output_ts_conv_t ts_conv;
        char *ts_conv_str;

        output_stat_conv_t stat_conv;
        output_tcp_flags_conv_t tcp_flags_conv;
        output_ip_addr_conv_t ip_addr_conv;
        output_ip_proto_conv_t ip_proto_conv;

        output_summary_t summary;
};

void output_setup(struct output_params op);

const char * field_to_str(int field, const void *data);

error_code_t print_aggr_mem(lnf_mem_t *mem, size_t limit,
                const struct agg_param *ap, size_t ap_cnt);

/* Print statistics. */
void print_stats(const struct stats *stats);

#endif //OUTPUT_H
