/**
 * @brief Encapuslation of libnf fields -- data types and functions for general
 * fields, aggregation keys, sort key, and output fields.
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

#include <stdbool.h>     // for bool
#include <stddef.h>      // for size_t


#define IP_NETMASK_LEN_MIN 0
#define IPV4_NETMASK_LEN_MAX 32
#define IPV6_NETMASK_LEN_MAX 128


/*
 * Data types declarations.
 */
/**
 * @brief Base libnf field structure.
 */
struct field {
    int id;  //!< A libnf field ID, can range from LNF_FLD_ZERO_ to LNF_FLD_TERM_.
    size_t size;  //!< Size of the field data in bytes.
};

/**
 * @brief Aggregation key, specialization of the base field structure.
 */
struct aggr_key {
    const struct field *field;  //!< Pointer to the base field structure.
    int alignment;  //!< Netmask length for IPv4, alignemnt LNF_UINT64 (e.g.,
                    //!< timestamps).
    int ipv6_alignment;  //!< Netmask length for IPv6.
};

/**
 * @brief Sort key, specialization of the base field structure.
 */
struct sort_key {
    const struct field *field;  //!< Pointer to the base field structure.
    int direction;  //!< Libnf sort direction, can be either LNF_SORT_ASC for
                    //!< ascending or LNF_SORT_DESC for descending.
    int aggr_func;  //!< Libnf aggregation function. It is used only when both
                    //!< aggregation and sorting are in effect. The value can be
                    //!< one of LNF_AGGR_MIN, LNF_AGGR_MAX, LNF_AGGR_SUM, or
                    //!< LNF_AGGR_OR.
};

/**
 * @brief Output field structure.
 */
struct output_field {
    const struct field *field;  //!< Pointer to the base field structure.
    int aggr_func;  //!< Libnf aggregation function. It is used only when
                    //!< aggregation is in effect. The value can be one of
                    //!< LNF_AGGR_MIN, LNF_AGGR_MAX, LNF_AGGR_SUM, or LNF_AGGR_OR.
};

#define AGGR_KEYS_MAX 10
#define OUTPUT_FIELDS_MAX 30
#define ALL_FIELDS_MAX AGGR_KEYS_MAX + OUTPUT_FIELDS_MAX + 1  // +1 for sort key
/**
 * @brief Structure encapsulating aggregation keys, sort key, and output fields.
 *
 * Arrays aggr_keys and output_fields are disjoint. Array output_fields and sort
 * key are disjoint. Array aggr_keys may contain the same field as contains the
 * sort_key. Array "all" contains all aggr_keys, output_fields, and sort key in
 * order in which the fields came.
 */
struct fields {
    struct field all[ALL_FIELDS_MAX];
    size_t all_cnt;
    size_t all_sizes_sum;

    struct aggr_key aggr_keys[AGGR_KEYS_MAX];
    size_t aggr_keys_cnt;

    struct sort_key sort_key;

    struct output_field output_fields[OUTPUT_FIELDS_MAX];
    size_t output_fields_cnt;
};


/*
 * Public function prototypes.
 */
int
field_get_type(const int id);

size_t
field_get_size(const int id);

const char *
field_get_name(const int id);

int
field_get_aggr_func(const int id);

int
field_get_sort_dir(const int id);

bool
field_parse(const char str[], int *const id, int *const alignment,
            int *const ipv6_alignment);


bool
fields_add_output_field(struct fields *const fields, const int id);

bool
fields_add_aggr_key(struct fields *const fields, const int id,
                    const int alignment, const int ipv6_alignment);

bool
fields_set_sort_key(struct fields *const fields, const int id,
                    const int direction);

bool
fields_can_use_fast_aggr(const struct fields *const fields);

bool
fields_check(const struct fields *const fields);

void
fields_print_debug(const struct fields *const fields);
