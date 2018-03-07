/** Argument parsing and usage/help printing implementation.
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

#define _XOPEN_SOURCE //strptime()


#include "common.h"
#include "arg_parse.h"
#include "print.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <limits.h> //PATH_MAX, NAME_MAX
#include <ctype.h>

#include <getopt.h>
#include <libnf.h>


#define STAT_DELIM '#'            // stat_spec#sort_spec
#define TIME_RANGE_DELIM "#"      // begin#end
#define SORT_DELIM '#'            // field#direction
#define TIME_DELIM " \t\n\v\f\r"  // whitespace
#define FIELDS_DELIM ","          // libnf fields delimiter

#define DEFAULT_LIST_FIELDS "first,packets,bytes,srcip,dstip,srcport,dstport,proto"
#define DEFAULT_SORT_FIELDS DEFAULT_LIST_FIELDS
#define DEFAULT_AGGR_FIELDS "duration,flows,packets,bytes,bps,pps,bpp"
#define DEFAULT_STAT_SORT_KEY "flows"
#define DEFAULT_STAT_REC_LIMIT "10"

#define DEFAULT_PRETTY_TS_CONV "%F %T"

#define MIN_IP_BITS 0
#define MAX_IPV4_BITS 32
#define MAX_IPV6_BITS 128


/* Global variables. */
static const char *usage_string =
"Usage: mpiexec [MPI_options] " PACKAGE_NAME " [-a field[,...]] [-f filter]\n"
"       [-l limit] [-o field[#direction]] [-s statistic] [-t time_spec]\n"
"       [-T begin[#end]] [-v level] path ...";

static const char *help_string =
"MPI_options\n"
"      See your MPI process manager documentation, e.g., mpiexec(1).\n"
"General options\n"
"     Mandatory arguments to long options are mandatory for short options too.\n"
"\n"
"     -a, --aggregation=field[,...]\n"
"            Aggregated flow records together by any number of fields.\n"
"     -f, --filter=filter\n"
"            Process only filter matching records.\n"
"     -l, --limit=limit\n"
"            Limit the number of records to print.\n"
"     -o, --order=field[#direction]\n"
"            Set record sort order.\n"
"     -s, --statistic=statistic\n"
"            Shortcut for aggregation (-a), sort (-o) and record limit (-l).\n"
"     -t, --time-point=time_spec\n"
"            Process only single flow file, the one which includes given time.\n"
"     -T, --time-range=begin[#end]\n"
"            Process only flow files from begin to the end time range.\n"
"     -v, --verbosity=level\n"
"            Set verbosity level.\n"
"\n"
"Controlling output\n"
"     --output-items= item_list\n"
"            Set output items.\n"
"     --output-format=format\n"
"            Set output (print) format.\n"
"     --output-ts-conv=timestamp_conversion\n"
"            Set timestamp output conversion format.\n"
"     --output-ts-localtime\n"
"            Convert timestamps to local time.\n"
"     --output-volume-conv=volume_conversion\n"
"            Set volume output conversion format.\n"
"     --output-tcpflags-conv=TCP_flags_conversion\n"
"            Set TCP flags output conversion format.\n"
"     --output-addr-conv=IP_address_conversion\n"
"            Set IP address output conversion format.\n"
"     --output-proto-conv=IP_protocol_conversion\n"
"            Set IP protocol output conversion format.\n"
"     --output-duration-conv=duration_conversion\n"
"            Set duration conversion format.\n"
"     --fields=field[,...]\n"
"            Set the list of printed fields.\n"
"     --progress-bar-type=progress_bar_type\n"
"            Set progress bar type.\n"
"     --progress-bar-dest=progress_bar_destination\n"
"            Set progress bar destination.\n"
"\n"
"Other options\n"
"     --no-fast-topn\n"
"            Disable fast top-N algorithm.\n"
"     --no-bfindex\n"
"            Disable Bloom filter indexes.\n"
"\n"
"Getting help\n"
"     --help Print a help message and exit.\n"
"     --version\n"
"            Display version information and exit.\n";


enum { //command line options, have to start above ASCII
        OPT_NO_FAST_TOPN = 256, //disable fast top-N algorithm
        OPT_NO_BFINDEX,  // disable Bloom filter indexes

        OPT_OUTPUT_ITEMS, //output items (records, processed records summary,
                          //metadata summary)
        OPT_OUTPUT_FORMAT, //output (print) format
        OPT_OUTPUT_TS_CONV, //output timestamp conversion
        OPT_OUTPUT_TS_LOCALTIME, //output timestamp in localtime
        OPT_OUTPUT_VOLUME_CONV, //output volumetric field conversion
        OPT_OUTPUT_TCP_FLAGS_CONV, //output TCP flags conversion
        OPT_OUTPUT_IP_ADDR_CONV, //output IP address conversion
        OPT_OUTPUT_IP_PROTO_CONV, //output IP protocol conversion
        OPT_OUTPUT_DURATION_CONV, //output IP protocol conversion

        OPT_FIELDS, //specification of listed fields
        OPT_PROGRESS_BAR_TYPE, //type of the progress bar
        OPT_PROGRESS_BAR_DEST, //destination of the progress bar

        OPT_HELP, //print help
        OPT_VERSION, //print version
};

// variables for getopt_long() setup
static const char *const short_opts = "a:f:l:o:s:t:T:v:";
static const struct option long_opts[] = {
    // long and short options
    {"aggregation", required_argument, NULL, 'a'},
    {"filter", required_argument, NULL, 'f'},
    {"limit", required_argument, NULL, 'l'},
    {"order", required_argument, NULL, 'o'},
    {"statistic", required_argument, NULL, 's'},
    {"time-point", required_argument, NULL, 't'},
    {"time-range", required_argument, NULL, 'T'},
    {"verbosity", required_argument, NULL, 'v'},

    // long options only
    {"no-fast-topn", no_argument, NULL, OPT_NO_FAST_TOPN},
    {"no-bfindex", no_argument, NULL, OPT_NO_BFINDEX},

    {"output-items", required_argument, NULL, OPT_OUTPUT_ITEMS},
    {"output-format", required_argument, NULL, OPT_OUTPUT_FORMAT},
    {"output-ts-conv", required_argument, NULL, OPT_OUTPUT_TS_CONV},
    {"output-ts-localtime", no_argument, NULL, OPT_OUTPUT_TS_LOCALTIME},
    {"output-volume-conv", required_argument, NULL, OPT_OUTPUT_VOLUME_CONV},
    {"output-tcpflags-conv", required_argument, NULL, OPT_OUTPUT_TCP_FLAGS_CONV},
    {"output-addr-conv", required_argument, NULL, OPT_OUTPUT_IP_ADDR_CONV},
    {"output-proto-conv", required_argument, NULL, OPT_OUTPUT_IP_PROTO_CONV},
    {"output-duration-conv", required_argument, NULL, OPT_OUTPUT_DURATION_CONV},

    {"fields", required_argument, NULL, OPT_FIELDS},
    {"progress-bar-type", required_argument, NULL, OPT_PROGRESS_BAR_TYPE},
    {"progress-bar-dest", required_argument, NULL, OPT_PROGRESS_BAR_DEST},

    {"help", no_argument, NULL, OPT_HELP},
    {"version", no_argument, NULL, OPT_VERSION},

    {0, 0, 0, 0}  // termination required by getopt_long()
};



static const char *const date_formats[] = {
        /* Date formats. */
        "%Y-%m-%d", //standard date, 2015-12-31
        "%d.%m.%Y", //European, 31.12.2015
        "%m/%d/%Y", //American, 12/31/2015

        /* Time formats. */
        "%H:%M", //23:59

        /* Special formats. */
        "%a", //weekday according to the current locale, abbreviated or full
        "%b", //month according to the current locale, abbreviated or full
        "%s", //the number of seconds since the Epoch, 1970-01-01 00:00:00 UTC
};

static const char *const utc_strings[] = {
        "u",
        "ut",
        "utc",
        "U",
        "UT",
        "UTC",
};

/**
 * @defgroup str_to_int_group Family of functions converting string to integers
 *                            of various length and signedness.
 * @{
 */

/**
 * @brief Convert a signed decimal integer from a string to a long long integer.
 *
 * Wrapper to the strtoll() function.
 *
 * @param[in] string Non null and non empty string to convert.
 * @param[out] res Pointer to a conversion destination variable.
 *
 * @return NULL on success, read-only error string on error.
 */
inline static const char *
str_to_llint(const char *const string, long long int *res)
{
    assert(string && string[0] != '\0' && res);  // *nptr is not '\0'

    errno = 0;  // clear previous errors
    char *endptr = NULL;
    *res = strtoll(string, &endptr, 10);
    assert(endptr);
    if (*endptr != '\0') { // if **endptr is '\0', the entire string is valid
        return "invalid characters";
    // LLONG_MIN and LLONG_MAX may be valid values!
    //} else if (*res == LLONG_MIN) {
    //    return "would case underflow";
    //} else if (*res == LLONG_MAX) {
    //    return "would case overflow";
    } else if (errno != 0) {  // ERANGE on over/underflow, EINVAL on inval. base
        return strerror(errno);
    } else {  // conversion was successful
        return NULL;
    }
}

/**
 * @brief Convert a signed decimal integer from a string to a long integer.
 *
 * Wrapper to the strtol() function.
 *
 * @param[in] string Non null and non empty string to convert.
 * @param[out] res Pointer to a conversion destination variable.
 *
 * @return NULL on success, read-only error string on error.
 */
inline static const char *
str_to_lint(const char *const string, long int *res)
{
    assert(string && string[0] != '\0' && res);  // *nptr is not '\0'

    long long int tmp_res;
    const char *const error_string = str_to_llint(string, &tmp_res);
    if (error_string) {
        return error_string;
    }

#if LLONG_MIN != LONG_MIN || LLONG_MAX != LONG_MAX
    // conversion to long long int was successful, try conversion to long int
    if (!IN_RANGE_INCL(tmp_res, LONG_MIN, LONG_MAX)) {
        errno = ERANGE;
        return strerror(errno);
    }
#endif

    // conversion to was successful
    *res = tmp_res;
    return NULL;
}

/**
 * @brief Convert a signed decimal integer from a string to an integer.
 *
 * @param[in] string Non null and non empty string to convert.
 * @param[out] res Pointer to a conversion destination variable.
 *
 * @return NULL on success, read-only error string on error.
 */
inline static const char *
str_to_int(const char *const string, int *res)
{
    assert(string && string[0] != '\0' && res);  // *nptr is not '\0'

    long long int tmp_res;
    const char *const error_string = str_to_llint(string, &tmp_res);
    if (error_string) {
        return error_string;
    }

#if LLONG_MIN != INT_MIN || LLONG_MAX != INT_MAX
    // conversion to long long int was successful, try conversion to int
    if (!IN_RANGE_INCL(tmp_res, INT_MIN, INT_MAX)) {
        errno = ERANGE;
        return strerror(errno);
    }
#endif

    // conversion was successful
    *res = tmp_res;
    return NULL;
}

/**
 * @brief Convert an unsigned decimal integer from a string to a long long
 *        unsigned integer.
 *
 * Wrapper to the strtoull() function.
 *
 * @param[in] string Non null and non empty string to convert.
 * @param[out] res Pointer to a conversion destination variable.
 *
 * @return NULL on success, read-only error string on error.
 */
inline static const char *
str_to_lluint(const char *const string, long long unsigned int *res)
{
    assert(string && string[0] != '\0' && res);  // *nptr is not '\0'

    // skip all initial white space and then check for minus sign
    const char *neg_check = string;
    while (isspace(*neg_check)) {
        neg_check++;
    }
    if (*neg_check == '-') {
        return "negative value";
    }

    errno = 0;  // clear previous errors
    char *endptr = NULL;
    *res = strtoull(string, &endptr, 10);
    assert(endptr);
    if (*endptr != '\0') { // if **endptr is '\0', the entire string is valid
        return "invalid characters";
    // ULLONG_MAX may be a valid value!
    //} else if (*res == ULLONG_MAX) {
    //    return "would case overflow";
    } else if (errno != 0) {  // ERANGE on overflow, EINVAL on invalid base
        return strerror(errno);
    } else {  // conversion was successful
        return NULL;
    }
}

/**
 * @brief Convert an unsigned decimal integer from a string to a long unsigned
 *        integer.
 *
 * Wrapper to the strtoul() function.
 *
 * @param[in] string Non null and non empty string to convert.
 * @param[out] res Pointer to a conversion destination variable.
 *
 * @return NULL on success, read-only error string on error.
 */
inline static const char *
str_to_luint(const char *const string, long unsigned int *res)
{
    assert(string && string[0] != '\0' && res);  // *nptr is not '\0'

    long long unsigned int tmp_res;
    const char *const error_string = str_to_lluint(string, &tmp_res);
    if (error_string) {
        return error_string;
    }

#if ULLONG_MAX != ULONG_MAX
    // conversion to long long unsigned int was successful, try conversion to
    // long unsigned int
    if (tmp_res > ULONG_MAX) {
        errno = ERANGE;
        return strerror(errno);
    }
#endif

    // conversion was successful
    *res = tmp_res;
    return NULL;
}

/**
 * @brief Convert an unsigned decimal integer from a string to an unsigned
 *        integer.
 *
 * @param[in] string Non null and non empty string to convert.
 * @param[out] res Pointer to a conversion destination variable.
 *
 * @return NULL on success, read-only error string on error.
 */
inline static const char *
str_to_uint(const char *const string, unsigned int *res)
{
    assert(string && string[0] != '\0' && res);  // *nptr is not '\0'

    long long unsigned int tmp_res;
    const char *const error_string = str_to_lluint(string, &tmp_res);
    if (error_string) {
        return error_string;
    }

#if ULLONG_MAX != UINT_MAX
    // conversion to long long unsigned int was successful, try conversion to
    // unsigned int
    if (tmp_res > UINT_MAX) {
        errno = ERANGE;
        return strerror(errno);
    }
#endif

    // conversion was successful
    *res = tmp_res;
    return NULL;
}

/**
 * @brief Convert an unsigned decimal integer from a string to size_t.
 *
 * @param[in] string Non null and non empty string to convert.
 * @param[out] res Pointer to a conversion destination variable.
 *
 * @return NULL on success, read-only error string on error.
 */
inline static const char *
str_to_size_t(const char *const string, size_t *res)
{
    assert(string && string[0] != '\0' && res);  // *nptr is not '\0'

    long long unsigned int tmp_res;
    const char *const error_string = str_to_lluint(string, &tmp_res);
    if (error_string) {
        return error_string;
    }

    // conversion to long long unsigned int was successful, try conversion to
    // size_t
    if (tmp_res > SIZE_MAX) {
        errno = ERANGE;
        return strerror(errno);
    }

    // conversion was successful
    *res = tmp_res;
    return NULL;
}

/**  @} */  // str_to_int_group


/**
 * @defgroup fields_group Functionality for manipulating with libnf fields.
 * @{
 */

/**
 * @brief Convert libnf field ID to its textual representation.
 *
 * @param field_id ID of the field.
 *
 * @return Static read-only null-terminated string.
 */
static const char *
fields_id_to_str(const int field_id)
{
    assert(IN_RANGE_EXCL(field_id, LNF_FLD_ZERO_, LNF_FLD_TERM_));

    static char field_name_buff[LNF_INFO_BUFSIZE];
    const int ret = lnf_fld_info(field_id, LNF_FLD_INFO_NAME, field_name_buff,
                                 sizeof (field_name_buff));
    assert(ret == LNF_OK);
    return field_name_buff;
}

/**
 * @brief Add ordinary field to the field_info array.
 *
 * Ordinary field is non-aggregation-key and non-sort-key field. Aliases are not
 * allowed due to problems it causes later (conversion etc.). Dependencties of
 * computed fields LNF_FLD_CALC_* (e.g., LNF_FLD_FIRST and LNF_FLD_LAST for
 * LNF_FLD_CALC_DURATION, LNF_FLD_DOCTETS and LNF_FLD_CALC_DURATION for
 * LNF_FLD_CALC_BPS, ...) are handled by libnf.
 *
 * TODO: --fields srcport,srcport without an aggregation causes
 *       WARNING: command line argument: field `srcport' already set as an
 *       aggregation key
 *
 * @param[out] field_info Pointer to the structure to fill with the new field.
 * @param[in] field_id ID of the new ordinary field.
 * @param[in] ipv4_bits Use ony first bits of the IPv4 address; [0, 32].
 * @param[in] ipv6_bits Use ony first bits of the IPv6 address; [0, 128].
 *
 * @return E_OK on success, E_ARG on failure.
 */
static error_code_t
fields_add_ordinary(struct field_info *field_info, int field_id, int ipv4_bits,
                    int ipv6_bits)
{
    assert(field_info && IN_RANGE_EXCL(field_id, LNF_FLD_ZERO_, LNF_FLD_TERM_));
    assert(IN_RANGE_INCL(ipv4_bits, MIN_IP_BITS, MAX_IPV4_BITS));
    assert(IN_RANGE_INCL(ipv6_bits, MIN_IP_BITS, MAX_IPV6_BITS));

    // test aliases
    if (IN_RANGE_INCL(field_id, LNF_FLD_DPKTS_ALIAS, LNF_FLD_DSTADDR_ALIAS)
            || field_id == LNF_FLD_PAIR_ADDR_ALIAS)
    {
        PRINT_ERROR(E_ARG, 0, "libnf field `%s' is an alias, use the original name",
                    fields_id_to_str(field_id));
        return E_ARG;
    }

    if ((field_info->flags & LNF_AGGR_FLAGS) == LNF_AGGR_KEY) {
        // field was previously added as an aggregation key
        PRINT_WARNING(E_ARG, 0, "field `%s' already set as an aggregation key",
                      fields_id_to_str(field_id));
    } else {
        PRINT_DEBUG("fields: adding `%s' as an ordinary field",
                    fields_id_to_str(field_id));
        field_info->id = field_id;
        // query and set default aggregation function (min, max, sum, OR, ... )
        int aggr_func;
        const int ret = lnf_fld_info(field_id, LNF_FLD_INFO_AGGR, &aggr_func,
                                     sizeof (aggr_func));
        assert(ret == LNF_OK);
        field_info->flags |= aggr_func;  // OR with sort flags
        // set IPv4/IPv6 bits, otherwise libnf memory would clear IP address
        // fields which is undesired with pure sorting mode
        field_info->ipv4_bits = ipv4_bits;
        field_info->ipv6_bits = ipv6_bits;
    }

    return E_OK;
}

/**
 * @brief Add aggregation key field to the field_info array.
 *
 * Almost every libnf field can operate as an aggregation key, except for the
 * following limitations:
 *   - calculated fields (except for the durataion),
 *   - composite fields (such as LNF_FLD_BREC1).
 *
 * Dependencties of computed field LNF_FLD_CALC_DURATION (LNF_FLD_FIRST and
 * LNF_FLD_LAST) are handled by libnf; they are both used as aggregation keys
 * (using MIN/MAX aggregation functions) internally.
 *
 * If field_id is already present in fields[] (as an ordinary field or a sort
 * key), it is also made an aggregation key. One field can be both aggregation
 * and sort key at the same time.
 *
 * @param[out] field_info Pointer to the structure to fill with the new
 *                        aggregation key field.
 * @param[in] field_id ID of the new aggregation key field.
 * @param[in] ipv4_bits Use ony first bits of the IPv4 address; [0, 32].
 * @param[in] ipv6_bits Use ony first bits of the IPv6 address; [0, 128].
 *
 * @return E_OK on success, E_ARG on failure.
 */
static error_code_t
fields_add_aggr_key(struct field_info *field_info, int field_id, int ipv4_bits,
                    int ipv6_bits)
{
    assert(field_info && IN_RANGE_EXCL(field_id, LNF_FLD_ZERO_, LNF_FLD_TERM_));
    assert(IN_RANGE_INCL(ipv4_bits, MIN_IP_BITS, MAX_IPV4_BITS));
    assert(IN_RANGE_INCL(ipv6_bits, MIN_IP_BITS, MAX_IPV6_BITS));

    // test aliases
    if (IN_RANGE_INCL(field_id, LNF_FLD_DPKTS_ALIAS, LNF_FLD_DSTADDR_ALIAS)
            || field_id == LNF_FLD_PAIR_ADDR_ALIAS)
    {
        PRINT_ERROR(E_ARG, 0, "libnf field `%s' is an alias, use the original name",
                    fields_id_to_str(field_id));
        return E_ARG;
    }

    // test fields which cannot be used as aggregation keys (libnf limitation)
    if (IN_RANGE_INCL(field_id, LNF_FLD_CALC_BPS, LNF_FLD_CALC_BPP)
            || field_id == LNF_FLD_BREC1)
    {
        PRINT_ERROR(E_ARG, 0, "libnf field `%s' cannot be set as an aggregation key",
                    fields_id_to_str(field_id));
        return E_ARG;
    }

    // add field as an aggregation key
    const int aggr_flags = field_info->flags & LNF_AGGR_FLAGS;
    if (aggr_flags && aggr_flags != LNF_AGGR_KEY) {
        // field was previously added as an ordinary field
        PRINT_WARNING(E_ARG, 0, "field `%s' already set as an ordinary field",
                      fields_id_to_str(field_id));
    } else {
        PRINT_DEBUG("fields: adding `%s' as an aggregation key",
                    fields_id_to_str(field_id));
        field_info->id = field_id;
        field_info->flags &= ~LNF_AGGR_FLAGS;  // clear aggregation flags
        field_info->flags |= LNF_AGGR_KEY;     // merge with sort flags
        field_info->ipv4_bits = ipv4_bits;
        field_info->ipv6_bits = ipv6_bits;
    }

    return E_OK;
}

/**
 * @brief Add sort key field to the field_info array.
 *
 * Wheter libnf field can operate as a sort key depends on its default sort
 * direction: LNF_SORT_NONE means it cannot. Dependencties of the computed
 * fields are handled by libnf. If field_id is already present in fields[] (as
 * an ordinary field or an aggregation key), it is also made a sort key. One
 * field can be both aggregation and sort key at the same time.
 *
 * @param[out] field_info Pointer to the structure to fill with the new sort key
 *                        field.
 * @param[in] field_id ID of the new sort key field.
 * @param[in] direction Sort direction -- ascending, descending, or
 *                      LNF_SORT_NONE for default direction of the given field.
 * @param[in] ipv4_bits Use ony first bits of the IPv4 address; [0, 32].
 * @param[in] ipv6_bits Use ony first bits of the IPv6 address; [0, 128].
 *
 * @return E_OK on success, E_ARG on failure.
 */
static error_code_t
fields_add_sort_key(struct field_info *field_info, int field_id, int direction,
                    int ipv4_bits, int ipv6_bits)
{
    assert(field_info && IN_RANGE_EXCL(field_id, LNF_FLD_ZERO_, LNF_FLD_TERM_));
    assert(direction == LNF_SORT_NONE || direction == LNF_SORT_ASC
           || direction == LNF_SORT_DESC);
    assert(IN_RANGE_INCL(ipv4_bits, MIN_IP_BITS, MAX_IPV4_BITS));
    assert(IN_RANGE_INCL(ipv6_bits, MIN_IP_BITS, MAX_IPV6_BITS));

    // this function may be called only once
    static int sort_key_set = false;
    if (sort_key_set) {
        PRINT_WARNING(E_ARG, 0, "sort key already set, only one sort key is allowed");
    } else {
        sort_key_set = true;
    }

    // test aliases
    if (IN_RANGE_INCL(field_id, LNF_FLD_DPKTS_ALIAS, LNF_FLD_DSTADDR_ALIAS)
            || field_id == LNF_FLD_PAIR_ADDR_ALIAS)
    {
        PRINT_ERROR(E_ARG, 0, "libnf field `%s' is an alias, use the original name",
                    fields_id_to_str(field_id));
        return E_ARG;
    }

    // test if the field can operate as a sort key by querying default direction
    int default_direction;
    const int ret = lnf_fld_info(field_id, LNF_FLD_INFO_SORT,
                                 &default_direction,
                                 sizeof (default_direction));
    assert(ret == LNF_OK);
    assert(default_direction == LNF_SORT_NONE
           || default_direction == LNF_SORT_ASC
           || default_direction == LNF_SORT_DESC);
    if (default_direction == LNF_SORT_NONE) {
        PRINT_ERROR(E_ARG, 0, "libnf field `%s' cannot be used as a sort key",
                    fields_id_to_str(field_id));
        return E_ARG;
    }

    PRINT_DEBUG("fields: adding `%s' as a sort key",
                fields_id_to_str(field_id));

    if (direction == LNF_SORT_NONE) {
        direction = default_direction;
    }
    field_info->id = field_id;
    field_info->flags |= direction;  // merge with aggregation flags
    // set IPv4/IPv6 bits, otherwise libnf memory would clear IP address fields
    // which is undesired with pure sorting mode
    field_info->ipv4_bits = ipv4_bits;
    field_info->ipv6_bits = ipv6_bits;

    return E_OK;
}

/**
 * @brief Parse single field info from its text representation.
 *
 * Valid text representation is field[/[IPv4 bits][/[IPv6 bits]]], e.g., srcip
 * or srcip/24 or srcip/24/64 or srcip//. If slashes are part of the string but
 * bits are not, bits default to 0. Bits are accepted for every field, but only
 * makes sense for IP address fields.
 *
 * @param[in] field_str Field in its text representation.
 * @param[out] field_id Parsed field ID.
 * @param[out] ipv4_bits Parsed IPv4 bits for IP address fields, 0 otherwise.
 * @param[out] ipv6_bits Parsed IPv6 bits for IP address fields, 0 otherwise.
 *
 * @return E_OK on success, E_ARG on failure.
 */
static error_code_t
fields_parse_str(const char field_str[], int *field_id, int *ipv4_bits,
                 int *ipv6_bits)
{
    assert(field_str && field_id && ipv4_bits && ipv6_bits);

    int ipv4_bits_in;
    int ipv6_bits_in;
    int field_id_in = lnf_fld_parse(field_str, &ipv4_bits_in, &ipv6_bits_in);

    // test field string validity
    if (field_id_in == LNF_FLD_ZERO_ || field_id_in == LNF_ERR_OTHER) {
        PRINT_ERROR(E_ARG, 0, "unknown libnf field `%s'", field_str);
        return E_ARG;
    }

    // test netmask validity (in case of IPv4/IPv6 address field)
    if (!IN_RANGE_INCL(ipv4_bits_in, MIN_IP_BITS, MAX_IPV4_BITS)) {
        PRINT_ERROR(E_ARG, 0, "invalid number of IPv4 bits: %d", ipv4_bits_in);
        return E_ARG;
    } else if (!IN_RANGE_INCL(ipv6_bits_in, MIN_IP_BITS, MAX_IPV6_BITS)) {
        PRINT_ERROR(E_ARG, 0, "invalid number of IPv6 bits: %d", ipv6_bits_in);
        return E_ARG;
    }

    // everything went fine, set output variables
    *field_id = field_id_in;
    *ipv4_bits = ipv4_bits_in;
    *ipv6_bits = ipv6_bits_in;

    return E_OK;
}

/**
 * @brief Parse multiple field infos from a string.
 *
 * Valid string contains of a FIELDS_DELIM separated field specifications (as
 * described in @ref fields_parse_str).
 *
 * @param[in,out] fields Array of field_info structures to be filled.
 * @param[in] fields_str FIELDS_DELIM separated field specifiacations.
 * @param[in] are_aggr_keys Flag determining whether fields should be added as
 *                          aggregation keys or as a ordinary fields.
 *
 * @return E_OK on success, other error code on failure.
 */
static error_code_t
fields_add_from_str(struct field_info fields[], char *fields_str,
                    bool are_aggr_keys)
{
    assert(fields && fields_str);

    error_code_t ecode = E_OK;

    char *saveptr = NULL;
    for (char *token = strtok_r(fields_str, FIELDS_DELIM, &saveptr);
            token != NULL;
            token = strtok_r(NULL, FIELDS_DELIM, &saveptr))
    {
        int field_id;
        int ipv4_bits;  // use ony first bits of the IP address
        int ipv6_bits;  // --- || ---
        ecode = fields_parse_str(token, &field_id, &ipv4_bits, &ipv6_bits);
        if (ecode != E_OK) {
            return ecode;
        }

        if (are_aggr_keys) {
            ecode = fields_add_aggr_key(fields + field_id, field_id, ipv4_bits,
                                        ipv6_bits);
        } else {
            ecode = fields_add_ordinary(fields + field_id, field_id, ipv4_bits,
                                        ipv6_bits);
        }

        if (ecode != E_OK) {
            break;
        }
    }

    return ecode;
}

#if 0
/**
 * @brief Print all non-zero fields from supplied field_info array.
 *
 * @param[in] fields Array of field_info structures to be printed.
 */
static void
fields_print(const struct field_info fields[])
{
    assert(fields);

    printf("\t%-15s%-12s%-12s%-11s%s\n", "name", "aggr flags", "sort flags",
           "IPv4 bits", "IPv6 bits");
    for (size_t i = 0; i < LNF_FLD_TERM_; ++i) {
        if (!fields[i].id) {
            continue;
        }

        printf("\t%-15s0x%-10x0x%-10x%-11d%d\n", fields_id_to_str(i),
                fields[i].flags & LNF_AGGR_FLAGS,
                fields[i].flags & LNF_SORT_FLAGS,
                fields[i].ipv4_bits, fields[i].ipv6_bits);
    }
}
#endif

/**  @} */  // fields_group


/**
 * @brief Convert string into tm structure.
 *
 * Function tries to parse time string and fills tm with appropriate values on
 * success. String is split into tokens according to TIME_DELIM delimiters. Each
 * token is either converted (from left to right) into date, if it corresponds
 * to one of date_formats[], or UTC flag is set, if token matches one of
 * utc_strings[]. If nor date nor UTF flag is detected, E_ARG is returned.
 *
 * If NO DATE is given, today is assumed if the given hour is lesser than the
 * current hour and yesterday is assumed if it is more. If NO TIME is given,
 * midnight is assumed. If ONLY THE WEEKDAY is given, today is assumed if the
 * given day is less or equal to the current day and last week if it is more.
 * If ONLY THE MONTH is given, the current year is assumed if the given month is
 * less or equal to the current month and last year if it is more and no year is
 * given.
 *
 * Time structure is overwritten by parsed-out values. If string is successfully
 * parsed, E_OK is returned. On error, content of tm structure is undefined and
 * E_ARG is returned.
 *
 * @param[in] time_str Time string, usually gathered from command line.
 * @param[out] utc Set if one of utc_strings[] is found.
 * @param[out] tm Time structure filled with parsed-out time and date.
 * @return Error code. E_OK or E_ARG.
 */
static error_code_t str_to_tm(char *time_str, bool *utc, struct tm *tm)
{
        char *ret;
        char *token;
        char *saveptr = NULL;
        struct tm garbage; //strptime() failure would ruin tm values
        struct tm now_tm;
        const time_t now = time(NULL);

        localtime_r(&now, &now_tm);

        tm->tm_sec = tm->tm_min = tm->tm_hour = INT_MIN;
        tm->tm_wday = tm->tm_mday = tm->tm_yday = INT_MIN;
        tm->tm_mon = tm->tm_year = INT_MIN;
        tm->tm_isdst = -1;

        /* Separate time and date in time string. */
        token = strtok_r(time_str, TIME_DELIM, &saveptr); //first token
        while (token != NULL) {
                /* Try to parse date. */
                for (size_t i = 0; i < ARRAY_SIZE(date_formats); ++i) {
                        ret = strptime(token, date_formats[i], &garbage);
                        if (ret != NULL && *ret == '\0') {
                                /* Conversion succeeded, fill real struct tm. */
                                strptime(token, date_formats[i], tm);
                                goto next_token;
                        }
                }

                /* Check for UTC flag. */
                for (size_t i = 0; i < ARRAY_SIZE(utc_strings); ++i) {
                        if (strcmp(token, utc_strings[i]) == 0) {
                                *utc = true;
                                goto next_token;
                        }
                }

                PRINT_ERROR(E_ARG, 0, "invalid time specifier `%s'", token);
                return E_ARG; //conversion failure
next_token:
                token = strtok_r(NULL, TIME_DELIM, &saveptr); //next token
        }


        /* Only the weekday is given. */
        if (tm->tm_wday >= 0 && tm->tm_wday <= 6 && tm->tm_year == INT_MIN &&
                        tm->tm_mon == INT_MIN && tm->tm_mday == INT_MIN) {
                tm->tm_year = now_tm.tm_year;
                tm->tm_mon = now_tm.tm_mon;
                tm->tm_mday = now_tm.tm_mday -
                        (now_tm.tm_wday - tm->tm_wday + 7) % 7;
        }

        /* Only the month is given. */
        if (tm->tm_mon >= 0 && tm->tm_mon <= 11 && tm->tm_mday == INT_MIN) {
                if (tm->tm_year == INT_MIN) {
                        if (tm->tm_mon - now_tm.tm_mon > 0) { //last year
                                tm->tm_year = now_tm.tm_year - 1;
                        } else { //this year
                                tm->tm_year = now_tm.tm_year;
                        }
                }

                tm->tm_mday = 1;
        }

        /* No time is given. */
        if (tm->tm_hour == INT_MIN) {
                tm->tm_hour = 0;
        }
        if (tm->tm_min == INT_MIN) {
                tm->tm_min = 0;
        }
        if (tm->tm_sec == INT_MIN) {
                tm->tm_sec = 0;
        }

        /* No date is given. */
        if (tm->tm_hour >= 0 && tm->tm_hour <= 23 && tm->tm_mon == INT_MIN &&
                        tm->tm_mday == INT_MIN && tm->tm_wday == INT_MIN)
        {
                tm->tm_mon = now_tm.tm_mon;
                if (tm->tm_hour - now_tm.tm_hour > 0) { //yesterday
                        tm->tm_mday = now_tm.tm_mday - 1;
                } else { //today
                        tm->tm_mday = now_tm.tm_mday;
                }
        }

        /* Fill in the gaps. */
        if (tm->tm_year == INT_MIN) {
                tm->tm_year = now_tm.tm_year;
        }
        if (tm->tm_mon == INT_MIN) {
                tm->tm_mon = now_tm.tm_mon;
        }

        mktime_utc(tm); //normalization
        return E_OK;
}


/** \brief Parse and store time range string.
 *
 * Function tries to parse time range string, fills time_begin and
 * time_end with appropriate values on success. Beginning and ending dates
 * (and times) are  separated with TIME_RANGE_DELIM, if ending date is not
 * specified, current time is used.
 *
 * Alignment of the boundaries to the FLOW_FILE_ROTATION_INTERVAL is perfomed.
 * Beginning time is aligned to the beginning of the rotation interval, ending
 * time is aligned to the ending of the rotation interval:
 *
 * 0     5    10    15    20   -------->   0     5    10    15    20
 * |_____|_____|_____|_____|   alignment   |_____|_____|_____|_____|
 *          ^     ^                              ^           ^
 *        begin  end                           begin        end
 *
 * If range string is successfully parsed, E_OK is returned. On error,
 * content of time_begin and time_end is undefined and E_ARG is
 * returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] range_str Time range string, usually gathered from command line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_time_range(struct cmdline_args *args, char *range_str)
{
        error_code_t ecode = E_OK;
        char *begin_str;
        char *end_str;
        char *trailing_str;
        char *saveptr = NULL;
        bool begin_utc = false;
        bool end_utc = false;

        assert(args != NULL && range_str != NULL);

        /* Split time range string. */
        // TODO: change to strchr()
        begin_str = strtok_r(range_str, TIME_RANGE_DELIM, &saveptr);
        if (begin_str == NULL) {
                PRINT_ERROR(E_ARG, 0, "invalid time range string `%s'\n",
                                range_str);
                return E_ARG;
        }
        end_str = strtok_r(NULL, TIME_RANGE_DELIM, &saveptr); //NULL is valid
        trailing_str = strtok_r(NULL, TIME_RANGE_DELIM, &saveptr);
        if (trailing_str != NULL) {
                PRINT_ERROR(E_ARG, 0, "time range trailing string `%s'\n",
                                trailing_str);
                return E_ARG;
        }

        /* Convert time strings to tm structure. */
        ecode = str_to_tm(begin_str, &begin_utc, &args->time_begin);
        if (ecode != E_OK) {
                return E_ARG;
        }
        if (end_str == NULL) { //NULL means until now
                const time_t now = time(NULL);
                localtime_r(&now, &args->time_end);
        } else {
                ecode = str_to_tm(end_str, &end_utc, &args->time_end);
                if (ecode != E_OK) {
                        return E_ARG;
                }
        }

        if (!begin_utc) {
                time_t tmp;

                //input time is localtime, let mktime() decide about DST
                args->time_begin.tm_isdst = -1;
                tmp = mktime(&args->time_begin);
                gmtime_r(&tmp, &args->time_begin);
        }
        if (!end_utc) {
                time_t tmp;

                //input time is localtime, let mktime() decide about DST
                args->time_end.tm_isdst = -1;
                tmp = mktime(&args->time_end);
                gmtime_r(&tmp, &args->time_end);
        }

        /* Align beginning time to the beginning of the rotation interval. */
        while (mktime_utc(&args->time_begin) % FLOW_FILE_ROTATION_INTERVAL){
                args->time_begin.tm_sec--;
        }
        /* Align ending time to the ending of the rotation interval. */
        while (mktime_utc(&args->time_end) % FLOW_FILE_ROTATION_INTERVAL){
                args->time_end.tm_sec++;
        }

        /* Check time range sanity. */
        if (tm_diff(args->time_end, args->time_begin) <= 0) {
                char begin[255], end[255];

                strftime(begin, sizeof(begin), "%c", &args->time_begin);
                strftime(end, sizeof(end), "%c", &args->time_end);

                PRINT_ERROR(E_ARG, 0, "zero or negative time range duration");
                PRINT_ERROR(E_ARG, 0, "time range (after the alignment): %s - %s",
                                begin, end);

                return E_ARG;
        }

        return E_OK;
}


/** \brief Parse and store time point string.
 *
 * Function tries to parse time point string, fills time_begin and time_end
 * with appropriate values on success. Beggining time is aligned to the
 * beginning of the rotation interval, ending time is set to the beginning of
 * the next rotation interval. Therefor exacly one flow file will be processed.
 *
 * Alignment of the boundaries to the FLOW_FILE_ROTATION_INTERVAL is perfomed.
 * Beginning time is aligned to the beginning of the rotation interval, ending
 * time is aligned to the ending of the rotation interval:
 *
 * 0     5    10    15    20   -------->   0     5    10    15    20
 * |_____|_____|_____|_____|   alignment   |_____|_____|_____|_____|
 *          ^                                    ^     ^
 *        begin                                begin  end
 *
 * If range string is successfully parsed, E_OK is returned. On error,
 * content of time_begin and time_end is undefined and E_ARG is
 * returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] range_str Time string, usually gathered from the command line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_time_point(struct cmdline_args *args, char *time_str)
{
        error_code_t ecode = E_OK;
        bool utc = false;

        assert(args != NULL && time_str != NULL);

        /* Convert time string to the tm structure. */
        ecode = str_to_tm(time_str, &utc, &args->time_begin);
        if (ecode != E_OK) {
                return E_ARG;
        }

        if (!utc) {
                time_t tmp;

                //input time is localtime, let mktime() decide about DST
                args->time_begin.tm_isdst = -1;
                tmp = mktime(&args->time_begin);
                gmtime_r(&tmp, &args->time_begin);
        }

        /* Align time to the beginning of the rotation interval. */
        while (mktime_utc(&args->time_begin) % FLOW_FILE_ROTATION_INTERVAL){
                args->time_begin.tm_sec--;
        }

        /*
         * Copy the aligned time to the ending time and increment ending and
         * align it to create interval of FLOW_FILE_ROTATION_INTERVAL length.
         */
        memcpy(&args->time_end, &args->time_begin, sizeof (args->time_end));
        args->time_end.tm_sec++;
        while (mktime_utc(&args->time_end) % FLOW_FILE_ROTATION_INTERVAL){
                args->time_end.tm_sec++;
        }

        /* Check time range sanity. */
        assert(tm_diff(args->time_end, args->time_begin) > 0);

        return E_OK;
}


/**
 * @brief Parse sort specification string and save the results.
 *
 * Sort specification string is expected in "field[#direction]" format, where
 * field is libnf field capable of working as sort key (see \ref
 * fields_add_sort_key) and direction is either "asc" for ascending direction or
 * "desc" for descending direction.
 *
 * @param[in,out] fields Array of field_info structures to be filled.
 * @param[in] stat_spec Sort specification string, usually gathered from the
 *                      command line.
 *
 * @return E_OK on success, E_ARG otherwise.
 */
static error_code_t
parse_sort_spec(struct field_info fields[], char *sort_spec)
{
    assert(fields && sort_spec && sort_spec[0] != '\0');

    PRINT_DEBUG("args: parsing sort spec `%s'", sort_spec);
    error_code_t ecode;
    int direction = LNF_SORT_NONE;
    char *const delim_pos = strchr(sort_spec, SORT_DELIM);  // find first
    if (delim_pos) {  // delimiter found, direction should follow
        *delim_pos = '\0';  // to distinguish between the sort key and direction
        char *const direction_str = delim_pos + 1;
        PRINT_DEBUG("args: sort spec delimiter found, using `%s' as a sort key and `%s' as a direction",
                    sort_spec, direction_str);
        if (strcmp(direction_str, "asc") == 0) {  // ascending direction
            direction = LNF_SORT_ASC;
        } else if (strcmp(direction_str, "desc") == 0) {  // descending dir.
            direction = LNF_SORT_DESC;
        } else {  // invalid sort direction
            PRINT_ERROR(E_ARG, 0, "invalid sort direction `%s'", direction_str);
            return E_ARG;
        }
    } else {  // delimiter not found, direction not specified; use the default
        PRINT_DEBUG("args: sort spec delimiter not found, using whole sort spec as a sort key and its default direction");
    }

    // parse sort key from string; netmask is pointless in case of sort key
    int field_id;
    int ipv4_bits;
    int ipv6_bits;
    ecode = fields_parse_str(sort_spec, &field_id, &ipv4_bits, &ipv6_bits);
    if (ecode != E_OK) {
        return ecode;
    }

    return fields_add_sort_key(fields + field_id, field_id, direction,
                               ipv4_bits, ipv6_bits);
}


/**
 * @brief Parse statistic specification string.
 *
 * Statistic is only shortcut for aggregation, sort and limit. Therefore,
 * statistic string is expected in "fields[#sort_spec]" format. If sort spec
 * isn't present, DEFAULT_STAT_SORT_KEY is used.
 *
 * @param[in] stat_optarg Statistic specification string, usually gathered from
 *                      the command line.
 * @param[out] aggr_optarg Pointer to the aggregation optarg string destination.
 * @param[out] sort_optarg Pointer to the sort optarg string destination.
 * @param[out] limit_optarg Pointer to the limit optarg string destination.
 */
static void
parse_stat_spec(char *stat_optarg, char *aggr_optarg[], char *sort_optarg[],
                char *limit_optarg[])
{
    assert(stat_optarg && stat_optarg[0] != '\0' && aggr_optarg && sort_optarg
           && limit_optarg);

    PRINT_DEBUG("args: stat spec: parsing `%s'", stat_optarg);
    *aggr_optarg = stat_optarg;

    char *const delim_pos = strchr(stat_optarg, STAT_DELIM);  // find first
    if (delim_pos) {  // delimiter found, sort spec should follow
        *delim_pos = '\0';  // to distinguish between the fields and the rest
        *sort_optarg = delim_pos + 1;
    } else {  // delimiter not found, sort spec not specified; use the defaults
        *sort_optarg = DEFAULT_STAT_SORT_KEY;
    }

    *limit_optarg = DEFAULT_STAT_REC_LIMIT;
    PRINT_DEBUG("args: stat spec: aggr spec = `%s', sort spec = `%s', limit spec = `%s'",
                *aggr_optarg, *sort_optarg, *limit_optarg);
}


/** \brief Check and save filter expression string.
 *
 * Function checks filter by initializing it using libnf. If filter syntax is
 * correct, pointer to filter string is stored in arguments structure and E_OK
 * is returned. Otherwise arguments structure remains untouched and E_ARG is
 * returned.
 *
 * \param[in,out] args Structure with parsed command line parameters and other
 *                   program settings.
 * \param[in] filter_str Filter expression string, usually gathered from command
 *                       line.
 * \return Error code. E_OK or E_ARG.
 */
static error_code_t set_filter(struct cmdline_args *args, char *filter_str)
{
        lnf_filter_t *filter;

        /* Try to initialize filter. */
        int lnf_ret = lnf_filter_init_v2(&filter, filter_str);  // new fltr.
        if (lnf_ret != LNF_OK) {
                PRINT_ERROR(E_ARG, lnf_ret, "cannot initialise filter `%s'",
                            filter_str);
                return E_ARG;
        }

        lnf_filter_free(filter);
        args->filter_str = filter_str;

        return E_OK;
}


static error_code_t set_output_items(struct output_params *op, char *items_str)
{
        char *token;
        char *saveptr = NULL;


        op->print_records = OUTPUT_ITEM_NO;
        op->print_processed_summ = OUTPUT_ITEM_NO;
        op->print_metadata_summ = OUTPUT_ITEM_NO;

        for (token = strtok_r(items_str, FIELDS_DELIM, &saveptr); //first token
                        token != NULL;
                        token = strtok_r(NULL, FIELDS_DELIM, &saveptr)) //next
        {
                if (strcmp(token, "records") == 0 ||
                                strcmp(token, "r") == 0) {
                        op->print_records = OUTPUT_ITEM_YES;
                } else if (strcmp(token, "processed-records-summary") == 0 ||
                                strcmp(token, "p") == 0) {
                        op->print_processed_summ = OUTPUT_ITEM_YES;
                } else if (strcmp(token, "metadata-summary") == 0 ||
                                strcmp(token, "m") == 0) {
                        op->print_metadata_summ = OUTPUT_ITEM_YES;
                } else {
                        PRINT_ERROR(E_ARG, 0, "unknown output item `%s'", token);
                        return E_ARG;
                }
        }


        return E_OK;
}

static error_code_t set_output_format(struct output_params *op,
                char *format_str)
{
        if (strcmp(format_str, "csv") == 0) {
                op->format = OUTPUT_FORMAT_CSV;
        } else if (strcmp(format_str, "pretty") == 0) {
                op->format = OUTPUT_FORMAT_PRETTY;
        } else {
                PRINT_ERROR(E_ARG, 0, "unknown output format string `%s'",
                                format_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_ts_conv(struct output_params *op,
                char *ts_conv_str)
{
        if (strcmp(ts_conv_str, "none") == 0) {
                op->ts_conv= OUTPUT_TS_CONV_NONE;
        } else {
                op->ts_conv= OUTPUT_TS_CONV_STR;
                op->ts_conv_str = ts_conv_str;
        }

        return E_OK;
}

static error_code_t set_output_volume_conv(struct output_params *op,
                char *volume_conv_str)
{
        if (strcmp(volume_conv_str, "none") == 0) {
                op->volume_conv= OUTPUT_VOLUME_CONV_NONE;
        } else if (strcmp(volume_conv_str, "metric-prefix") == 0) {
                op->volume_conv = OUTPUT_VOLUME_CONV_METRIC_PREFIX;
        } else if (strcmp(volume_conv_str, "binary-prefix") == 0) {
                op->volume_conv = OUTPUT_VOLUME_CONV_BINARY_PREFIX;
        } else {
                PRINT_ERROR(E_ARG, 0,
                            "unknown output volume conversion string `%s'",
                            volume_conv_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_tcp_flags_conv(struct output_params *op,
                char *tcp_flags_conv_str)
{
        if (strcmp(tcp_flags_conv_str, "none") == 0) {
                op->tcp_flags_conv = OUTPUT_TCP_FLAGS_CONV_NONE;
        } else if (strcmp(tcp_flags_conv_str, "str") == 0) {
                op->tcp_flags_conv = OUTPUT_TCP_FLAGS_CONV_STR;
        } else {
                PRINT_ERROR(E_ARG, 0, "unknown tcp flags conversion string `%s'",
                            tcp_flags_conv_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_ip_addr_conv(struct output_params *op,
                char *ip_addr_conv_str)
{
        if (strcmp(ip_addr_conv_str, "none") == 0) {
                op->ip_addr_conv = OUTPUT_IP_ADDR_CONV_NONE;
        } else if (strcmp(ip_addr_conv_str, "str") == 0) {
                op->ip_addr_conv = OUTPUT_IP_ADDR_CONV_STR;
        } else {
                PRINT_ERROR(E_ARG, 0, "unknown IP address conversion string `%s'",
                            ip_addr_conv_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_ip_proto_conv(struct output_params *op,
                char *ip_proto_conv_str)
{
        if (strcmp(ip_proto_conv_str, "none") == 0) {
                op->ip_proto_conv = OUTPUT_IP_PROTO_CONV_NONE;
        } else if (strcmp(ip_proto_conv_str, "str") == 0) {
                op->ip_proto_conv = OUTPUT_IP_PROTO_CONV_STR;
        } else {
                PRINT_ERROR(E_ARG, 0, "unknown internet protocol conversion string `%s'",
                            ip_proto_conv_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_duration_conv(struct output_params *op,
                char *duration_conv_str)
{
        if (strcmp(duration_conv_str, "none") == 0) {
                op->duration_conv = OUTPUT_DURATION_CONV_NONE;
        } else if (strcmp(duration_conv_str, "str") == 0) {
                op->duration_conv = OUTPUT_DURATION_CONV_STR;
        } else {
                PRINT_ERROR(E_ARG, 0, "unknown duration conversion string `%s'",
                                duration_conv_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_progress_bar_type(progress_bar_type_t *type,
                const char *progress_bar_type_str)
{
        if (strcmp(progress_bar_type_str, "none") == 0) {
                *type = PROGRESS_BAR_NONE;
        } else if (strcmp(progress_bar_type_str, "total") == 0) {
                *type = PROGRESS_BAR_TOTAL;
        } else if (strcmp(progress_bar_type_str, "perslave") == 0) {
                *type = PROGRESS_BAR_PERSLAVE;
        } else if (strcmp(progress_bar_type_str, "json") == 0) {
                *type = PROGRESS_BAR_JSON;
        } else {
                PRINT_ERROR(E_ARG, 0, "unknown progress bar type `%s'",
                                progress_bar_type_str);
                return E_ARG;
        }

        return E_OK;
}

error_code_t
arg_parse(struct cmdline_args *args, int argc, char *const argv[],
          bool root_proc)
{
    error_code_t ecode = E_OK;

    // set default values for certain arguments
    args->use_fast_topn = true;
    args->use_bfindex = true;
    args->rec_limit = SIZE_MAX;  // SIZE_MAX means record limit is unset

    // revent all non-root processes from printing getopt_long() errors
    if (!root_proc) {
        opterr = 0;
    }

    char *aggr_optarg = NULL;
    char *filter_optarg = NULL;
    char *limit_optarg = NULL;
    char *sort_optarg = NULL;
    char *time_point_optarg = NULL;
    char *time_range_optarg = NULL;
    char *verbosity_optarg = NULL;
    char *ordinary_fields_optarg = NULL;
    // loop through all the command-line arguments
    while (true) {
        const int opt = getopt_long(argc, argv, short_opts, long_opts, NULL);
        if (opt == -1) {
            break;  // all options processed successfully
        }

        switch (opt) {
        case 'a':  // or "--aggregation"
            aggr_optarg = optarg;
            break;
        case 'f':  // or "--filter": input filter expression
            filter_optarg = optarg;
            break;
        case 'l':  // or "--limit:" output record limit
            limit_optarg = optarg;
            break;
        case 'o':  // or "--order:" sort specification string
            sort_optarg = optarg;
            break;
        case 's':  // or "--statistic": Top-N/statistic specification string
            parse_stat_spec(optarg, &aggr_optarg, &sort_optarg, &limit_optarg);
            break;
        case 't':  // or "--time-point"
            time_point_optarg = optarg;
            break;
        case 'T':  // or "--time-range"
            time_range_optarg = optarg;
            break;
        case 'v':  // or "--verbosity"
            verbosity_optarg = optarg;
            break;

        case OPT_NO_FAST_TOPN:  // disable fast top-N algorithm
            args->use_fast_topn = false;
            break;
        case OPT_NO_BFINDEX:    // disable Bloom filter indexes
            args->use_bfindex = false;
            break;

        // output format affecting options
        case OPT_OUTPUT_ITEMS:
            ecode = set_output_items(&args->output_params, optarg);
            break;
        case OPT_OUTPUT_FORMAT:
            ecode = set_output_format(&args->output_params, optarg);
            break;
        case OPT_OUTPUT_TS_CONV:
            ecode = set_output_ts_conv(&args->output_params, optarg);
            break;
        case OPT_OUTPUT_TS_LOCALTIME:
            args->output_params.ts_localtime = true;
            break;
        case OPT_OUTPUT_VOLUME_CONV:
            ecode = set_output_volume_conv(&args->output_params, optarg);
            break;
        case OPT_OUTPUT_TCP_FLAGS_CONV:
            ecode = set_output_tcp_flags_conv(&args->output_params, optarg);
            break;
        case OPT_OUTPUT_IP_ADDR_CONV:
            ecode = set_output_ip_addr_conv(&args->output_params, optarg);
            break;
        case OPT_OUTPUT_IP_PROTO_CONV:
            ecode = set_output_ip_proto_conv(&args->output_params, optarg);
            break;
        case OPT_OUTPUT_DURATION_CONV:
            ecode = set_output_duration_conv(&args->output_params, optarg);
            break;

        case OPT_FIELDS:
            ordinary_fields_optarg = optarg;
            break;
        case OPT_PROGRESS_BAR_TYPE:
            ecode = set_progress_bar_type(&args->progress_bar_type, optarg);
            break;
        case OPT_PROGRESS_BAR_DEST:
            args->progress_bar_dest = optarg;
            break;

        case OPT_HELP:  // help
            if (root_proc) {
                printf("%s\n\n", usage_string);
                printf("%s", help_string);
            }
            return E_HELP;

        case OPT_VERSION:  // version
            if (root_proc) {
                printf("%s\n", PACKAGE_STRING);
            }
            return E_HELP;

        default:  // '?' or '0'
            return E_ARG;
        }

        if (ecode != E_OK) {
            return ecode;
        }
    }  // while (true) loop through all the command-line arguments

    ////////////////////////////////////////////////////////////////////////////
    // parse and set verbosity level (early to affect also argument parsing)
    if (verbosity_optarg) {
        const char *const conversion_err = str_to_int(verbosity_optarg,
                                                      (int *)&verbosity);
        if (conversion_err) {
            PRINT_ERROR(E_ARG, 0, "invalid verbosity level `%s': %s",
                        verbosity_optarg, conversion_err);
            return E_ARG;
        }
        if (!IN_RANGE_INCL(verbosity, VERBOSITY_QUIET, VERBOSITY_DEBUG)) {
            PRINT_ERROR(E_ARG, 0, "invalid verbosity level `%s': allowed range is [%d,%d]",
                        verbosity_optarg, VERBOSITY_QUIET, VERBOSITY_DEBUG);
            return E_ARG;
        }
        PRINT_DEBUG("args: setting verbosity level to debug");
    }

    // parse aggregation and sort option argument options
    if (aggr_optarg) {  // aggregation mode with optional sorting
        args->working_mode = MODE_AGGR;
        ecode = fields_add_from_str(args->fields, aggr_optarg, true);
        if (ecode != E_OK) {
            return ecode;
        }
        if (sort_optarg) {
            PRINT_DEBUG("args: using aggregation mode with sorting");
            ecode = parse_sort_spec(args->fields, sort_optarg);
            if (ecode != E_OK) {
                return ecode;
            }
        } else {
            PRINT_DEBUG("args: using aggregation mode without sorting");
        }
    } else if (sort_optarg) {  // sort mode
        PRINT_DEBUG("args: using sorting mode");
        args->working_mode = MODE_SORT;
        ecode = parse_sort_spec(args->fields, sort_optarg);
        if (ecode != E_OK) {
            return ecode;
        }
    } else {  // listing mode
        PRINT_DEBUG("args: using listing mode");
        args->working_mode = MODE_LIST;
    }

    // parse record limit argument option
    if (limit_optarg) {  // record limit specified
        const char *const conversion_err = str_to_size_t(limit_optarg,
                                                         &args->rec_limit);
        if (conversion_err) {
            PRINT_ERROR(E_ARG, 0, "record limit `%s': %s", limit_optarg,
                        conversion_err);
            return E_ARG;
        }
    } else {  // record limit not specified, disable record limit
        args->rec_limit = 0;
    }

    // check filter validity
    if (filter_optarg) {
        ecode = set_filter(args, filter_optarg);
        if (ecode) {
            return ecode;
        }
    }

    // parse time point and time range argument options
    if (time_point_optarg && time_range_optarg) {
        PRINT_ERROR(E_ARG, 0, "time point and time range are mutually exclusive");
        return E_ARG;
    } else if (time_point_optarg) {
        ecode = set_time_point(args, time_point_optarg);
        if (ecode) {
            return ecode;
        }
    } else if (time_range_optarg) {
        ecode = set_time_range(args, time_range_optarg);
        if (ecode) {
            return ecode;
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    /*
     * Non-option arguments are paths. Combine them into one path string,
     * separate them by the "File Separator" (0x1C) character.
     */
    if (optind == argc) {  // at least one path is mandatory
        PRINT_ERROR(E_ARG, 0, "missing path");
        return E_ARG;
    } else {
        args->paths = argv + optind;
        args->paths_cnt = argc - optind;
    }

    // TODO
    //// loop through all non-option args and copy them into the path string
    //for (int i = optind; i < argc; ++i) {
    //    strcat(args->path_str, argv[i]);
    //    if (i != argc - 1) {  //not the last option, add separator
    //        args->path_str[strlen(args->path_str)] = FILE_SEPARATOR;
    //    }
    //}

    ////////////////////////////////////////////////////////////////////////////
    // enable metadata-only mode if neither records nor
    // processed-records-summary is desired
    if (args->output_params.print_processed_summ == OUTPUT_ITEM_NO
            && args->output_params.print_records == OUTPUT_ITEM_NO)
    {
        args->working_mode = MODE_META;
    }

    // if progress bar type was not set, enable basic progress bar
    if (args->progress_bar_type == PROGRESS_BAR_UNSET) {
        args->progress_bar_type = PROGRESS_BAR_TOTAL;
    }

    // parse ordinary fields option argument
    if (ordinary_fields_optarg) {
        ecode = fields_add_from_str(args->fields, ordinary_fields_optarg,
                                    false);
        if (ecode != E_OK) {
            return ecode;
        }
    } else {  // ordinary fields not specidied, use default values
        switch (args->working_mode) {
        case MODE_LIST:
        {
            char tmp[] = DEFAULT_LIST_FIELDS;  // modifiable copy
            ecode = fields_add_from_str(args->fields, tmp, false);
            assert(ecode == E_OK);
            break;
        }
        case MODE_SORT:
        {
            char tmp[] = DEFAULT_SORT_FIELDS;
            ecode = fields_add_from_str(args->fields, tmp, false);
            assert(ecode == E_OK);
            break;
        }
        case MODE_AGGR:
        {
            char tmp[] = DEFAULT_AGGR_FIELDS;
            ecode = fields_add_from_str(args->fields, tmp, false);
            assert(ecode == E_OK);
            break;
        }
        case MODE_META:
            break;
        default:
            assert(!"unknown working mode");
        }
    }

    // set default output format and conversion parameters
    if (args->output_params.format == OUTPUT_FORMAT_UNSET) {
        args->output_params.format = OUTPUT_FORMAT_PRETTY;
    }
    switch (args->output_params.format) {
    case OUTPUT_FORMAT_PRETTY:
        if (args->output_params.print_records == OUTPUT_ITEM_UNSET) {
            args->output_params.print_records = OUTPUT_ITEM_YES;
        }
        if (args->output_params.print_processed_summ == OUTPUT_ITEM_UNSET) {
            args->output_params.print_processed_summ = OUTPUT_ITEM_YES;
        }
        if (args->output_params.print_metadata_summ == OUTPUT_ITEM_UNSET) {
            args->output_params.print_metadata_summ = OUTPUT_ITEM_NO;
        }

        if (args->output_params.ts_conv == OUTPUT_TS_CONV_UNSET) {
            args->output_params.ts_conv = OUTPUT_TS_CONV_STR;
            args->output_params.ts_conv_str = DEFAULT_PRETTY_TS_CONV;
        }
        if (args->output_params.volume_conv == OUTPUT_VOLUME_CONV_UNSET) {
            args->output_params.volume_conv = OUTPUT_VOLUME_CONV_METRIC_PREFIX;
        }
        if (args->output_params.tcp_flags_conv == OUTPUT_TCP_FLAGS_CONV_UNSET) {
            args->output_params.tcp_flags_conv = OUTPUT_TCP_FLAGS_CONV_STR;
        }
        if (args->output_params.ip_addr_conv == OUTPUT_IP_ADDR_CONV_UNSET) {
            args->output_params.ip_addr_conv = OUTPUT_IP_ADDR_CONV_STR;
        }
        if (args->output_params.ip_proto_conv == OUTPUT_IP_PROTO_CONV_UNSET) {
            args->output_params.ip_proto_conv = OUTPUT_IP_PROTO_CONV_STR;
        }
        if (args->output_params.duration_conv == OUTPUT_DURATION_CONV_UNSET) {
            args->output_params.duration_conv = OUTPUT_DURATION_CONV_STR;
        }
        break;

    case OUTPUT_FORMAT_CSV:
        if (args->output_params.print_records == OUTPUT_ITEM_UNSET) {
            args->output_params.print_records = OUTPUT_ITEM_YES;
        }
        if (args->output_params.print_processed_summ == OUTPUT_ITEM_UNSET) {
            args->output_params.print_processed_summ = OUTPUT_ITEM_NO;
        }
        if (args->output_params.print_metadata_summ == OUTPUT_ITEM_UNSET) {
            args->output_params.print_metadata_summ = OUTPUT_ITEM_NO;
        }

        if (args->output_params.ts_conv == OUTPUT_TS_CONV_UNSET) {
            args->output_params.ts_conv = OUTPUT_TS_CONV_NONE;
        }
        if (args->output_params.volume_conv == OUTPUT_VOLUME_CONV_UNSET) {
            args->output_params.volume_conv = OUTPUT_VOLUME_CONV_NONE;
        }
        if (args->output_params.tcp_flags_conv == OUTPUT_TCP_FLAGS_CONV_UNSET) {
            args->output_params.tcp_flags_conv = OUTPUT_TCP_FLAGS_CONV_NONE;
        }
        if (args->output_params.ip_addr_conv == OUTPUT_IP_ADDR_CONV_UNSET) {
            args->output_params.ip_addr_conv = OUTPUT_IP_ADDR_CONV_NONE;
        }
        if (args->output_params.ip_proto_conv == OUTPUT_IP_PROTO_CONV_UNSET) {
            args->output_params.ip_proto_conv = OUTPUT_IP_PROTO_CONV_NONE;
        }
        if (args->output_params.duration_conv == OUTPUT_DURATION_CONV_UNSET) {
            args->output_params.duration_conv = OUTPUT_DURATION_CONV_NONE;
        }
        break;

    case OUTPUT_FORMAT_UNSET:
        assert(!"illegal output parameters format");
    default:
        assert(!"unkwnown output parameters format");
    }

    ////////////////////////////////////////////////////////////////////////////
    /*
     * Reasons to forcibly disable the fast Top-N algorithm are:
     *   - no record limit specified (all records would be exchanged anyway),
     *   - sorting is disabled or sort key is not one of traffic volume fields
     *     (data octets, packets, out bytes, out packets and aggregated flows).
     */
    if (args->working_mode == MODE_AGGR) {
        // find sort key among fields
        int sort_key = LNF_FLD_ZERO_;
        for (size_t i = LNF_FLD_ZERO_; i < LNF_FLD_TERM_; ++i) {
            if (args->fields[i].flags & LNF_SORT_FLAGS) {
                sort_key = i;
                break;
            }
        }
        if (!IN_RANGE_INCL(sort_key, LNF_FLD_DOCTETS, LNF_FLD_AGGR_FLOWS)
                || !args->rec_limit)
        {
            args->use_fast_topn = false;
        }
    }

    ////////////////////////////////////////////////////////////////////////////
    // TODO: cleanup this
    // if (root_proc && verbosity >= VERBOSITY_DEBUG) {
    //     char begin[255], end[255];
    //     char *path = args->path_str;
    //     int c;

    //     printf("------------------------------------------------------\n");
    //     printf("mode: %s\n", working_mode_to_str(args->working_mode));
    //     if (args->working_mode == MODE_AGGR && args->use_fast_topn) {
    //         printf("flags: using fast top-N algorithm\n");
    //     }

    //     printf("fields:\n");
    //     fields_print(args->fields);

    //     if(args->filter_str) {
    //         printf("filter: %s\n", args->filter_str);
    //     }

    //     printf("paths:\n\t");
    //     while ((c = *path++)) {
    //         if (c == 0x1C) { //substitute separator with end of line
    //             putchar('\n');
    //             putchar('\t');
    //         } else {
    //             putchar(c);
    //         }
    //     }
    //     putchar('\n');

    //     strftime(begin, sizeof(begin), "%c", &args->time_begin);
    //     strftime(end, sizeof(end), "%c", &args->time_end);
    //     printf("time range: %s - %s\n", begin, end);
    //     printf("------------------------------------------------------\n\n");
    // }

    return E_OK;
}
