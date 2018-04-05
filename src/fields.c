/**
 * @brief Encapuslation of libnf fields -- data types and functions for general
 * fields, aggregation keys, sort key, and output fields.
 */

/*
 * Copyright (C) 2018 CESNET
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

#include "fields.h"

#include <assert.h>   // for assert
#include <stdbool.h>  // for false, true, bool
#include <stddef.h>   // for size_t
#include <string.h>   // for strlen

#include <libnf.h>

#include "errwarn.h"  // for error/warning/info/debug messages, ...
#include "common.h"   // for ::E_ARG, IN_RANGE_EXCL, IN_RANGE_INCL


/*
 * Private functions.
 */
/**
 * @brief Get pointer to field with the given ID from the "all" array.
 *
 * @param[in] fields Pointer to the fields structure.
 * @param[in] id ID of the libnf field.
 * @param id
 *
 * @return Address of the field if found, NULL if not found.
 */
static const struct field *
get_from_all(const struct fields *const fields, const int id)
{
    assert(fields);
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));

    for (size_t i = 0; i < fields->all_cnt; i++) {
        if (fields->all[i].id == id) {
            return fields->all + i;
        }
    }
    return NULL;
}

/**
 * @brief Get pointer to output field with the given ID from the "output_fields"
 *        array.
 *
 * @param[in] fields Pointer to the fields structure.
 * @param[in] id ID of the libnf field.
 * @param id
 *
 * @return Address of the output field if found, NULL if not found.
 */
static const struct output_field *
get_from_output(const struct fields *const fields, const int id)
{
    assert(fields);
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));

    for (size_t i = 0; i < fields->output_fields_cnt; i++) {
        if (fields->output_fields[i].field->id == id) {
            return fields->output_fields + i;
        }
    }
    return NULL;
}

/**
 * @brief Append the field to the list of all fields.
 *
 * The field must not be in the list of fields already!
 *
 * @param[in,out] fields Pointer to the fields structure.
 * @param[in] id ID of the libnf field to add as an aggregation key.
 *
 * @return Address of the new field, NULL if number of allowed fields would be
 *         exceeded by the addition.
 */
static const struct field *
add_to_all(struct fields *const fields, const int id)
{
    assert(fields);
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));
    const struct field *const ret = get_from_all(fields, id);
    assert(ret == NULL);  // make sure it is not present
    (void)ret;  // to suppress -Wunused-variable with -DNDEBUG

    // the array should not be full
    if (fields->all_cnt == ALL_FIELDS_MAX) {
        // the array is full
        ERROR(E_ARG, "fields: number of allowed fields exceeded");
        return NULL;
    }

    // added a new field
    assert(fields->all[fields->all_cnt].id == LNF_FLD_ZERO_);
    struct field *const new_field = fields->all + fields->all_cnt;
    const size_t size = field_get_size(id);
    new_field->id = id;
    new_field->size = size;
    fields->all_sizes_sum += size;
    fields->all_cnt++;

    return new_field;
}

/*
 * Public functions.
 */
/**
 * @brief Get a libnf type of the given libnf field.
 *
 * Wrapper for lnf_fld_info().
 *
 * @param[in] id ID of the libnf field.
 *
 * @return Libnf type of the given libnf field.
 */
int
field_get_type(const int id)
{
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));

    int type;
    const int ret = lnf_fld_info(id, LNF_FLD_INFO_TYPE, &type, sizeof (type));
    assert(ret == LNF_OK);
    (void)ret;  // to suppress -Wunused-variable with -DNDEBUG

    return type;
}

/**
 * @brief Get a size of the given libnf field in bytes.
 *
 * Wrapper for lnf_fld_info().
 *
 * @param[in] id ID of the libnf field.
 *
 * @return Size of the given libnf field in bytes.
 */
size_t
field_get_size(const int id)
{
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));

    int size;
    const int ret = lnf_fld_info(id, LNF_FLD_INFO_SIZE, &size, sizeof (size));
    assert(ret == LNF_OK);
    (void)ret;  // to suppress -Wunused-variable with -DNDEBUG
    assert(size > 0);

    return size;
}

/**
 * @brief Get a name of the given libnf field.
 *
 * Wrapper for lnf_fld_info().
 * The returned string is static, and will be overwritten by each consequent
 * call. This also means that the function is not thread-safe.
 *
 * @param[in] id ID of the libnf field.
 *
 * @return Static read-only null-terminated string.
 */
const char *
field_get_name(const int id)
{
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));

    static char name_buff[LNF_INFO_BUFSIZE];
    const int ret = lnf_fld_info(id, LNF_FLD_INFO_NAME, name_buff,
                                 sizeof (name_buff));
    assert(ret == LNF_OK);
    (void)ret;  // to suppress -Wunused-variable with -DNDEBUG
    assert(strlen(name_buff) > 0);

    return name_buff;
}

/**
 * @brief Get a default aggregation function for the given libnf field.
 *
 * Wrapper for lnf_fld_info().
 *
 * @param[in] id ID of the libnf field.
 *
 * @return LNF_AGGR_MIN, LNF_AGGR_MAX, LNF_AGGR_SUM, or LNF_AGGR_OR.
 */
int
field_get_aggr_func(const int id)
{
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));

    // query and set default aggregation function (min, max, sum, OR, ... )
    int aggr_func;
    const int ret = lnf_fld_info(id, LNF_FLD_INFO_AGGR, &aggr_func,
                                 sizeof (aggr_func));
    assert(ret == LNF_OK);
    (void)ret;  // to suppress -Wunused-variable with -DNDEBUG
    assert(IN_RANGE_INCL(aggr_func, LNF_AGGR_MIN, LNF_AGGR_KEY));
    if (aggr_func == LNF_AGGR_KEY) {
        // LNF_AGGR_KEY means the field does not have any default aggregation
        // function, use LNF_AGGR_MIN
        return LNF_AGGR_MIN;
    } else {
        return aggr_func;
    }
}

/**
 * @brief Get a default sorting direction for the given libnf field.
 *
 * Wrapper for lnf_fld_info().
 *
 * @param[in] id ID of the libnf field.
 *
 * @return LNF_SORT_NONE (if there is no default direction), LNF_SORT_ASC
 *         (ascending), or LNF_SORT_DESC (descending).
 */
int
field_get_sort_dir(const int id)
{
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));

    int direction;
    const int ret = lnf_fld_info(id, LNF_FLD_INFO_SORT, &direction,
                                 sizeof (direction));
    assert(ret == LNF_OK);
    (void)ret;  // to suppress -Wunused-variable with -DNDEBUG
    assert(direction == LNF_SORT_NONE || direction == LNF_SORT_ASC
           || direction == LNF_SORT_DESC);

    return direction;
}


/**
 * @brief Parse a libnf field text representation.
 *
 * Valid text representation of a single libnf field is "field[/alignment[/IPv6
 * alignment]], e.g., srcip or srcip/24, or srcip/24/64. Alignment is accepted
 * for every field, but are only used when the field is an aggregation key.
 * Moreover, only IP addresses and timestamps are currently affected. Alignment
 * in conjunction with IP addresses works as a netmask. In conjunction with
 * timestamps (e.g., first or Blast), the greater the alignment value is, the
 * less precise the timestamp is. If slashes are not part of the string, no
 * alignment is used. If slashes are part of the string but alignment is not,
 * the behaviour is undefined.
 *
 * Aliases are not allowed due to problems it causes later (conversion etc.).
 *
 * @param[in] str Field in its text representation.
 * @param[out] id Parsed field ID.
 * @param[out] alignment Netmask length for IPv4, alignemnt LNF_UINT64
 *                       (e.g.,timestamps).
 * @param[out] ipv6_alignment Netmask length for IPv6.
 *
 * @return True on success, false on failure.
 */
bool
field_parse(const char str[], int *const id, int *const alignment,
            int *const ipv6_alignment)
{
    assert(str && id && alignment && ipv6_alignment);

    int alignment_tmp;
    int ipv6_alignment_tmp;
    const int id_in = lnf_fld_parse(str, &alignment_tmp, &ipv6_alignment_tmp);

    // test field string validity
    if (id_in == LNF_FLD_ZERO_ || id_in == LNF_ERR_OTHER) {
        ERROR(E_ARG, "unknown libnf field `%s'", str);
        return false;
    }

    // test aliases
    if (IN_RANGE_INCL(id_in, LNF_FLD_DPKTS_ALIAS, LNF_FLD_DSTADDR_ALIAS)
            || id_in == LNF_FLD_PAIR_ADDR_ALIAS)
    {
        ERROR(E_ARG, "libnf field `%s' is an alias, use the original name",
              field_get_name(id_in));
        return false;
    }

    // test alignment validity (netmask length in case of IP address fields)
    assert(alignment_tmp >= 0 && ipv6_alignment_tmp >= 0);
    if (field_get_type(id_in) == LNF_ADDR) {
        if (!IN_RANGE_INCL(alignment_tmp, IP_NETMASK_LEN_MIN,
                           IPV4_NETMASK_LEN_MAX)) {
            ERROR(E_ARG, "invalid IPv4 netmask length: %d", alignment_tmp);
            return false;
        } else if (!IN_RANGE_INCL(ipv6_alignment_tmp, IP_NETMASK_LEN_MIN,
                                  IPV6_NETMASK_LEN_MAX)) {
            ERROR(E_ARG, "invalid IPv6 netmask length: %d", ipv6_alignment_tmp);
            return false;
        }
    }

    // everything went fine, set the output variables
    *id = id_in;
    *alignment = alignment_tmp;
    *ipv6_alignment = ipv6_alignment_tmp;
    return true;
}


/**
 * @brief Add an output field to the fields structure.
 *
 * Output field is a non-aggregation-key non-sort-key field.
 * Dependencties of computed fields LNF_FLD_CALC_* (e.g., LNF_FLD_FIRST and
 * LNF_FLD_LAST for LNF_FLD_CALC_DURATION, LNF_FLD_DOCTETS and
 * LNF_FLD_CALC_DURATION for LNF_FLD_CALC_BPS, ...) are handled by libnf.
 *
 * @param[in,out] fields Pointer to the fields structure.
 * @param[in] id ID of the libnf field to add.
 *
 * @return True success, false on failure.
 */
bool
fields_add_output_field(struct fields *const fields, const int id)
{
    assert(fields);
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));

    if (fields->output_fields_cnt == OUTPUT_FIELDS_MAX) {
        ERROR(E_ARG, "fields: number of allowed output fields exceeded");
        return false;
    }

    if (get_from_all(fields, id)) {
        DEBUG("fields: `%s' is already present", field_get_name(id));
        return true;
    }

    const struct field *const new_field = add_to_all(fields, id);
    if (!new_field) {
        return false;  // number of fields exceeded
    }

    assert(new_field
           && fields->output_fields[fields->output_fields_cnt].field == NULL);
    DEBUG("fields: adding `%s' as an output field", field_get_name(id));

    fields->output_fields[fields->output_fields_cnt].field = new_field;
    fields->output_fields[fields->output_fields_cnt].aggr_func =
        field_get_aggr_func(id);
    fields->output_fields_cnt++;

    return true;
}

/**
 * @brief Add an aggregation key field to the fields structure.
 *
 * Almost every libnf field can operate as an aggregation key, except for the
 * following limitations:
 *   - calculated fields (except for the durataion),
 *   - compound fields (such as LNF_FLD_BREC1).
 * One field can be both aggregation and sort key at the same time.
 *
 * Dependencties of computed field LNF_FLD_CALC_DURATION (LNF_FLD_FIRST and
 * LNF_FLD_LAST) are handled by libnf; they are both used as aggregation keys
 * (using MIN/MAX aggregation functions) internally.
 *
 *
 * @param[in,out] fields Pointer to the fields structure.
 * @param[in] id ID of the libnf field to add as an aggregation key.
 * @param[out] alignment Netmask length for IPv4, alignemnt LNF_UINT64
 *                       (e.g.,timestamps).
 * @param[out] ipv6_alignment Netmask length for IPv6.
 *
 * @return True on success, false on failure.
 */
bool
fields_add_aggr_key(struct fields *const fields, const int id,
                    const int alignment, const int ipv6_alignment)
{
    assert(fields);
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));
    // assert(IN_RANGE_INCL(alignment, IP_NETMASK_LEN_MIN,
    //        IPV4_NETMASK_LEN_MAX));
    // assert(IN_RANGE_INCL(ipv6_alignment, IP_NETMASK_LEN_MIN,
    //        IPV6_NETMASK_LEN_MAX));

    if (fields->aggr_keys_cnt == AGGR_KEYS_MAX) {
        ERROR(E_ARG, "fields: number of allowed aggregation keys exceeded");
        return false;
    }

    if (get_from_all(fields, id)) {
        DEBUG("fields: `%s' is already present", field_get_name(id));
        return true;
    }

    // test fields which cannot be used as aggregation keys (libnf limitation)
    if (IN_RANGE_INCL(id, LNF_FLD_CALC_BPS, LNF_FLD_CALC_BPP)
            || id == LNF_FLD_BREC1)
    {
        ERROR(E_ARG, "fields: `%s' cannot be set as an aggregation key",
              field_get_name(id));
        return false;
    }

    const struct field *const new_field = add_to_all(fields, id);
    if (!new_field) {
        return false;  // number of fields exceeded
    }

    assert(new_field && fields->aggr_keys[fields->aggr_keys_cnt].field == NULL);
    DEBUG("fields: adding `%s' as an aggregation key", field_get_name(id));

    fields->aggr_keys[fields->aggr_keys_cnt].field = new_field;
    fields->aggr_keys[fields->aggr_keys_cnt].alignment = alignment;
    fields->aggr_keys[fields->aggr_keys_cnt].ipv6_alignment = ipv6_alignment;
    fields->aggr_keys_cnt++;

    return true;
}

/**
 * @brief Set a sort key field in the fields structure.
 *
 * Wheter libnf field can operate as a sort key depends on its default sort
 * direction: LNF_SORT_NONE means it cannot.
 * Dependencties of the computed fields are handled automatically by libnf.
 * One field can be both aggregation and sort key at the same time.
 *
 * @param[in,out] fields Pointer to the fields structure.
 * @param[in] id ID of the libnf field to add as an aggregation key.
 * @param[in] direction Sort direction -- ascending, descending, or
 *                      LNF_SORT_NONE for default direction of the given field.
 *
 * @return True on success, false on failure.
 */
bool
fields_set_sort_key(struct fields *const fields, const int id,
                    const int direction)
{
    assert(fields);
    assert(IN_RANGE_EXCL(id, LNF_FLD_ZERO_, LNF_FLD_TERM_));
    assert(direction == LNF_SORT_NONE || direction == LNF_SORT_ASC
           || direction == LNF_SORT_DESC);

    // test if the field can operate as a sort key by querying its default
    // direction
    const int default_direction = field_get_sort_dir(id);
    if (default_direction == LNF_SORT_NONE) {
        ERROR(E_ARG, "fields: `%s' cannot be used as a sort key",
              field_get_name(id));
        return false;
    }

    const struct field *new_field = get_from_all(fields, id);
    if (!new_field) {  // sort key is not an aggregation key
        new_field = add_to_all(fields, id);
        if (!new_field) {
            return false;  // number of fields exceeded
        }
    }

    assert(new_field && fields->sort_key.field == NULL);
    DEBUG("fields: setting `%s' as a sort key", field_get_name(id));

    fields->sort_key.field = new_field;
    fields->sort_key.direction =
        direction == LNF_SORT_NONE ? default_direction : direction;
    fields->sort_key.aggr_func = field_get_aggr_func(id);

    return true;
}

/**
 * @brief Can lnf_mem_fastaggr() be applied with the given fields?
 *
 * @param[in] fields Pointer to the fields structure.
 *
 * @return True if all and only the required output fields are present and all
 *         have required aggregation funtion, false otherwise.
 */
bool
fields_can_use_fast_aggr(const struct fields *const fields)
{
    assert(fields);

    const struct { int id; int aggr_func; } required_params[] = {
        { LNF_FLD_FIRST, LNF_AGGR_MIN }, { LNF_FLD_LAST, LNF_AGGR_MAX },
        { LNF_FLD_DOCTETS, LNF_AGGR_SUM }, { LNF_FLD_DPKTS, LNF_AGGR_SUM },
        { LNF_FLD_AGGR_FLOWS, LNF_AGGR_SUM }
    };

    if (fields->output_fields_cnt != ARRAY_SIZE(required_params)) {
        return false;
    }

    // loop through all required fields
    for (size_t i = 0; i < ARRAY_SIZE(required_params); ++i) {
        const struct output_field *const of =
            get_from_output(fields, required_params[i].id);
        if (!of || of->aggr_func != required_params[i].aggr_func) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Check validity of the fields.
 *
 * @param[in] fields Pointer to the fields structure.
 *
 * @return True if valid, false if invalid.
 */
bool
fields_check(const struct fields *const fields)
{
    assert(fields);

    if (fields->aggr_keys_cnt > AGGR_KEYS_MAX) {
        return false;
    }
    for (size_t i = 0; i < fields->aggr_keys_cnt; ++i) {
        const struct aggr_key ak = fields->aggr_keys[i];
        if(ak.field == NULL
                || !IN_RANGE_EXCL(ak.field->id, LNF_FLD_ZERO_, LNF_FLD_TERM_)
                || ak.alignment < 0
                || ak.ipv6_alignment < 0)
        {
            return false;
        }
    }

    const struct sort_key sk = fields->sort_key;
    if (sk.field) {
        if (!IN_RANGE_EXCL(sk.field->id, LNF_FLD_ZERO_, LNF_FLD_TERM_)
                || (sk.direction != LNF_SORT_ASC && sk.direction != LNF_SORT_DESC)
                || !IN_RANGE_INCL(sk.aggr_func, LNF_AGGR_MIN, LNF_AGGR_KEY))
        {
            return false;
        }
    }

    if (fields->output_fields_cnt > OUTPUT_FIELDS_MAX) {
        return false;
    }
    for (size_t i = 0; i < fields->output_fields_cnt; ++i) {
        const struct output_field of = fields->output_fields[i];
        if (of.field == NULL
                || !IN_RANGE_EXCL(of.field->id, LNF_FLD_ZERO_, LNF_FLD_TERM_)
                || !IN_RANGE_INCL(of.aggr_func, LNF_AGGR_MIN, LNF_AGGR_KEY))
        {
            return false;
        }
    }

    if (fields->all_cnt > ALL_FIELDS_MAX) {
        return false;
    }
    size_t size_sum = 0;
    for (size_t i = 0; i < fields->all_cnt; ++i) {
        const struct field f = fields->all[i];
        size_sum += f.size;
        if (!f.id || !f.size) {
            return false;
        }
    }
    if (size_sum != fields->all_sizes_sum) {
        return false;
    }


    size_t i = 0;
    for ( ; i < fields->all_cnt; ++i) {
        const struct field *const field_ptr = fields->all + i;
        assert(field_ptr->id);

        bool in_aggr_keys = false;
        for (size_t j = 0; j < fields->aggr_keys_cnt; ++j) {
            if (fields->aggr_keys[j].field == field_ptr) {
                in_aggr_keys = true;
                break;
            }
        }
        const bool is_sort_key = fields->sort_key.field == field_ptr ? true : false;
        bool in_output_fields = false;
        for (size_t j = 0; j < fields->output_fields_cnt; ++j) {
            if (fields->output_fields[j].field == field_ptr) {
                in_output_fields = true;
                break;
            }
        }

        if (!in_aggr_keys && !is_sort_key && !in_output_fields) {
            return false;
        } else if (in_aggr_keys && in_output_fields) {
            return false;
        } else if (is_sort_key && in_output_fields) {
            return false;
        }
    }
    for ( ; i < ALL_FIELDS_MAX; ++i) {
        if (fields->all[i].id) {
            return false;
        }
    }

    return true;
}

/**
 * @brief Print fields.
 *
 * @param[in] fields Pointer to the fields structure.
 */
void
fields_print_debug(const struct fields *const fields)
{
    assert(fields);

    DEBUG("fields: %zu aggregation key(s):", fields->aggr_keys_cnt);
    for (size_t i = 0; i < fields->aggr_keys_cnt; ++i) {
        const struct aggr_key ak = fields->aggr_keys[i];
        DEBUG("\tID = 0x%2.2x, name = %s, alignment = %d, IPv6 alignment = %d",
              ak.field->id, field_get_name(ak.field->id), ak.alignment,
              ak.ipv6_alignment);
    }

    if (fields->sort_key.field) {
        const struct sort_key sk = fields->sort_key;
        DEBUG("fields: sort key:");
        DEBUG("\tID = 0x%2.2x, name = %s, direction = %s, aggregation function = %s",
              sk.field->id, field_get_name(sk.field->id),
              libnf_sort_dir_to_str(sk.direction),
              libnf_aggr_func_to_str(sk.aggr_func));
    } else {
        DEBUG("fields: no sort key");
    }

    DEBUG("fields: %zu output field(s):", fields->output_fields_cnt);
    for (size_t i = 0; i < fields->output_fields_cnt; i++) {
        const struct output_field of = fields->output_fields[i];
        DEBUG("\tID = 0x%2.2x, name = %s, aggregation function = %s",
              of.field->id, field_get_name(of.field->id),
              libnf_aggr_func_to_str(of.aggr_func));
    }

    DEBUG("fields: %zu field(s) in total:", fields->all_cnt);
    for (size_t i = 0; i < fields->all_cnt; ++i) {
        const struct field f = fields->all[i];
        DEBUG("\tID = 0x%2.2x, name = %s, size = %zu", f.id,
              field_get_name(f.id), f.size);
    }
}
