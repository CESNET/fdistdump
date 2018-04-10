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

#define _XOPEN_SOURCE           // strptime()

#include "config.h"             // for PROJECT_NAME, PROJECT_VERSION

#include <assert.h>             // for assert
#include <ctype.h>              // for isspace
#include <errno.h>              // for errno, ERANGE
#include <limits.h>             // for INT_MIN, INT_MAX, LLONG_MAX, LLONG_MIN
#include <stdbool.h>            // for false, true, bool
#include <stdint.h>             // for SIZE_MAX
#include <stdio.h>              // for printf
#include <stdlib.h>             // for strtoll, strtoull
#include <string.h>             // for strcmp, strtok_r, strerror, strchr
#include <time.h>               // for NULL, gmtime_r, mktime, localtime_r

#include <omp.h>                // for omp_set_num_threads
#include <getopt.h>             // for required_argument, no_argument, getop...
#include <libnf.h>              // for lnf_fld_info, LNF_FLD_ZERO_, LNF_SORT...
#include <unistd.h>             // for optarg, optind, opterr

#include "arg_parse.h"
#include "common.h"             // for ::E_ARG, ::E_OK, error_code_t
#include "errwarn.h"            // for error/warning/info/debug messages, ...
#include "fields.h"             // for field_get_*, ...
#include "output.h"             // for output_params, ::OUTPUT_ITEM_NO, ::OU...


#define STAT_DELIM '#'            // stat_spec#sort_spec
#define TIME_RANGE_DELIM '#'      // begin#end
#define SORT_DELIM '#'            // field#direction
#define TIME_DELIM " \t\n\v\f\r"  // whitespace
#define FIELDS_DELIM ","          // libnf fields delimiter

#define DEFAULT_LIST_FIELDS "first,packets,bytes,srcip,dstip,srcport,dstport,proto,flags"
#define DEFAULT_SORT_FIELDS DEFAULT_LIST_FIELDS
#define DEFAULT_AGGR_FIELDS "duration,flows,packets,bytes,flags,bps,pps,bpp"
#define DEFAULT_STAT_SORT_KEY "flows"
#define DEFAULT_STAT_REC_LIMIT "10"


// forward delcarations
struct tm;  // make IWYU happy


/*
 * Data types declarations.
 */
enum {  // command line options
    _OPT_FIRST = 256,  // have to start above the ASCII table

    OPT_OUTPUT_FIELDS,  // specification of listed fields
    OPT_OUTPUT_ITEMS,   // output items (records, processed records summary,
                        // metadata summary)
    OPT_OUTPUT_FORMAT,          // output (print) format
    OPT_OUTPUT_RICH_HEADER,     // field names enriched with aggr. functions etc.
    OPT_OUTPUT_NO_ELLIPSIZE,    // do not ellipsize fields when they do not fit
                                // in available columns

    OPT_OUTPUT_TS_CONV,         // output timestamp conversion
    OPT_OUTPUT_VOLUME_CONV,     // output volumetric field conversion
    OPT_OUTPUT_TCP_FLAGS_CONV,  // output TCP flags conversion
    OPT_OUTPUT_IP_ADDR_CONV,    // output IP address conversion
    OPT_OUTPUT_IP_PROTO_CONV,   // output IP protocol conversion
    OPT_OUTPUT_DURATION_CONV,   // output IP protocol conversion

    OPT_PROGRESS_BAR_TYPE,  // type of the progress bar
    OPT_PROGRESS_BAR_DEST,  // destination of the progress bar

    OPT_NUM_THREADS,    // set the number of used threads
    OPT_TIME_ZONE,      // set a time zone for all time-related functionality
    OPT_NO_TPUT,        // disable the TPUT algorithm for Top-N queries
    OPT_NO_BFINDEX,     // disable Bloom filter indexes

    OPT_HELP,  // print help
    OPT_VERSION,  // print version
};


/*
 * Global variables.
 */
static const char *const USAGE_STRING =
"Usage: mpiexec [MPI_options] " PROJECT_NAME " [options] path ...\n"
"       mpiexec [global_MPI_options] \\\n"
"               [local_MPI_options] " PROJECT_NAME " [options] : \\\n"
"               [local_MPI_options] " PROJECT_NAME " [options] path1 ... : \\\n"
"               [local_MPI_options] " PROJECT_NAME " [options] path2 ... :  ...";

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
    // output
    {"output-fields", required_argument, NULL, OPT_OUTPUT_FIELDS},
    {"output-items", required_argument, NULL, OPT_OUTPUT_ITEMS},
    {"output-format", required_argument, NULL, OPT_OUTPUT_FORMAT},
    {"output-rich-header", no_argument, NULL, OPT_OUTPUT_RICH_HEADER},
    {"output-no-ellipsize", no_argument, NULL, OPT_OUTPUT_NO_ELLIPSIZE},

    {"output-ts-conv", required_argument, NULL, OPT_OUTPUT_TS_CONV},
    {"output-volume-conv", required_argument, NULL, OPT_OUTPUT_VOLUME_CONV},
    {"output-tcpflags-conv", required_argument, NULL, OPT_OUTPUT_TCP_FLAGS_CONV},
    {"output-addr-conv", required_argument, NULL, OPT_OUTPUT_IP_ADDR_CONV},
    {"output-proto-conv", required_argument, NULL, OPT_OUTPUT_IP_PROTO_CONV},
    {"output-duration-conv", required_argument, NULL, OPT_OUTPUT_DURATION_CONV},

    // progress bar
    {"progress-bar-type", required_argument, NULL, OPT_PROGRESS_BAR_TYPE},
    {"progress-bar-dest", required_argument, NULL, OPT_PROGRESS_BAR_DEST},

    // other
    {"num-threads", required_argument, NULL, OPT_NUM_THREADS},
    {"time-zone", required_argument, NULL, OPT_TIME_ZONE},
    {"no-tput", no_argument, NULL, OPT_NO_TPUT},
    {"no-bfindex", no_argument, NULL, OPT_NO_BFINDEX},

    // getting help
    {"help", no_argument, NULL, OPT_HELP},
    {"version", no_argument, NULL, OPT_VERSION},

    {0, 0, 0, 0}  // termination required by getopt_long()
};

static const char *const date_formats[] = {
    // date formats
    "%Y-%m-%d",  // standard date: 2015-12-31
    "%d.%m.%Y",  // European: 31.12.2015
    "%m/%d/%Y",  // American: 12/31/2015

    // time formats
    "%H:%M",  // 23:59

    // special formats
    "%a",  // weekday according to the current locale, abbreviated or full
    "%b",  // month according to the current locale, abbreviated or full
    "%s",  // the number of seconds since the Epoch, 1970-01-01 00:00:00 UTC
};


/*
 * Private functions.
 */
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
/**  @} */  // str_to_int_group


/**
 * @defgroup time_string_parsing
 * @{ */
/**
 * @brief Parse string-represented date and time into struct tm.
 *
 * Function tries to parse time string and fills tm with appropriate values on
 * success. String is split into tokens according to TIME_DELIM delimiters. Each
 * token is converted (from left to right) into date if it corresponds to one of
 * date_formats[].
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
 * @param[out] tm Time structure filled with parsed-out time and date.
 *
 * @return E_OK on success, E_ARG on failure.
 */
static error_code_t
str_to_tm(char *time_str, struct tm *tm)
{
    assert(time_str && time_str[0] != '\n' && tm);

    // fill the tm struct with after-strptime-identifiable values
    tm->tm_sec = tm->tm_min = tm->tm_hour = INT_MIN;
    tm->tm_wday = tm->tm_mday = tm->tm_yday = INT_MIN;
    tm->tm_mon = tm->tm_year = INT_MIN;
    tm->tm_isdst = -1;

    // tokenize the string into time and date
    char *saveptr = NULL;
    for (char *token = strtok_r(time_str, TIME_DELIM, &saveptr);
            token != NULL;
            token = strtok_r(NULL, TIME_DELIM, &saveptr))
    {
        // try to parse the token
        for (size_t i = 0; i < ARRAY_SIZE(date_formats); ++i) {
            struct tm attempt;  // strptime() failure would ruin tm values
            const char *ret = strptime(token, date_formats[i], &attempt);
            if (ret && *ret == '\0') {
                // conversion succeeded, fill the output struct tm
                ret = strptime(token, date_formats[i], tm);
                assert(ret && *ret == '\0');
                goto next_token;
            }
        }

        ERROR(E_ARG, "invalid time specifier `%s'", token);
        return E_ARG;  // conversion failure
next_token: ;
    }

    // fill the elements which were left untouched by strptime()
    struct tm now_tm;
    const time_t now = time(NULL);  // number of seconds since the Epoch UTC
    ABORT_IF(now == (time_t)-1, E_INTERNAL, "time(): %s", strerror(errno));
    void *const ret = localtime_r(&now, &now_tm);
    ABORT_IF(!ret, E_INTERNAL, "localtime_r(): %s", strerror(errno));

    // only weekday is given
    if (tm->tm_wday >= 0 && tm->tm_wday <= 6 && tm->tm_year == INT_MIN
            && tm->tm_mon == INT_MIN && tm->tm_mday == INT_MIN)
    {
        tm->tm_year = now_tm.tm_year;
        tm->tm_mon = now_tm.tm_mon;
        tm->tm_mday = now_tm.tm_mday - (now_tm.tm_wday - tm->tm_wday + 7) % 7;
    }

    // only month is given
    if (tm->tm_mon >= 0 && tm->tm_mon <= 11 && tm->tm_mday == INT_MIN) {
        if (tm->tm_year == INT_MIN) {
            if (tm->tm_mon - now_tm.tm_mon > 0) {  // last year
                tm->tm_year = now_tm.tm_year - 1;
            } else {                               // this year
                tm->tm_year = now_tm.tm_year;
            }
        }
        tm->tm_mday = 1;
    }

    // noo time is given (only date)
    if (tm->tm_hour == INT_MIN) {
        tm->tm_hour = 0;
    }
    if (tm->tm_min == INT_MIN) {
        tm->tm_min = 0;
    }
    if (tm->tm_sec == INT_MIN) {
        tm->tm_sec = 0;
    }

    // no date is given (only time)
    if (tm->tm_hour >= 0 && tm->tm_hour <= 23 && tm->tm_mon == INT_MIN
            && tm->tm_mday == INT_MIN && tm->tm_wday == INT_MIN)
    {
        tm->tm_mon = now_tm.tm_mon;
        if (tm->tm_hour - now_tm.tm_hour > 0) {  // yesterday
            tm->tm_mday = now_tm.tm_mday - 1;
        } else {                                 // today
            tm->tm_mday = now_tm.tm_mday;
        }
    }

    // fill the "gaps"
    if (tm->tm_year == INT_MIN) {
        tm->tm_year = now_tm.tm_year;
    }
    if (tm->tm_mon == INT_MIN) {
        tm->tm_mon = now_tm.tm_mon;
    }

    mktime_utc(tm);  // normalization
    return E_OK;
}

/**
 * @brief Convert broken-down local time to UTC.
 *
 * This is done by:
 *   - calling mktime(), which converts a broken-down time structure, expressed
 *   as local time, to calendar time (negative value of tm_isdst means that
 *   mktime() should use timezone information to determine whether DST is in
 *   effect at the specified time),
 *   - calling gmtime(), which converts the calendar time to broken-down time
 *   representation, expressed in Coordinated Universal Time (UTC).
 *
 * @param[in] local Broken-down representation of the local time.
 * @param[out] utc Ponter to the output broken-down representation of the local
 *                 converted to UTC.
 */
static void
tm_local_to_utc(struct tm local, struct tm *const utc)
{
    assert(utc);

    local.tm_isdst = -1;  // auto-detection
    const time_t calendar_local = mktime(&local);
    ABORT_IF(calendar_local == (time_t)-1, E_INTERNAL, "mktime(): %s",
             strerror(errno));
    void *const ret = gmtime_r(&calendar_local, utc);
    ABORT_IF(!ret, E_INTERNAL, "gmtime_r(): %s", strerror(errno));
}

/**
 * @brief Parse time point specification string and save the results.
 *
 * Function tries to parse time point string, fills time_begin and time_end with
 * appropriate values on success. Beggining time is aligned to the beginning of
 * the rotation interval, ending time is set to the beginning of the next
 * rotation interval. Therefor exacly one flow file will be processed.
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
 * If range string is successfully parsed, E_OK is returned. On error, content
 * of time_begin and time_end is undefined and E_ARG is returned.
 *
 * @param[in,out] args Structure with parsed command line options.
 * @param[in] range_str Time string, usually gathered from the command line.
 *
 * @return E_OK on success, E_ARG on failure.
 */
static error_code_t
set_time_point(struct cmdline_args *const args, char *const time_str)
{
    assert(args && time_str && time_str[0] != '\0');

    // parse the local time string into the broken-down local time
    struct tm broken_down_local;
    const error_code_t ecode = str_to_tm(time_str, &broken_down_local);
    if (ecode != E_OK) {
        return E_ARG;
    }

    // convert to UTC
    tm_local_to_utc(broken_down_local, &args->time_begin);

    /*
     * Align the time to the beginning of the rotation interval. Then copy the
     * aligned time to the ending time and align it to create interval of
     * FLOW_FILE_ROTATION_INTERVAL length.
     */
    while (mktime_utc(&args->time_begin) % FLOW_FILE_ROTATION_INTERVAL) {
        args->time_begin.tm_sec--;  // go one second back
    }
    memcpy(&args->time_end, &args->time_begin, sizeof (args->time_end));
    args->time_end.tm_sec++;
    while (mktime_utc(&args->time_end) % FLOW_FILE_ROTATION_INTERVAL) {
        args->time_end.tm_sec++;  // go one second forward
    }

    // check time range sanity
    assert(tm_diff(args->time_end, args->time_begin) 
           == FLOW_FILE_ROTATION_INTERVAL);

    if (verbosity >= VERBOSITY_DEBUG) {
        char begin_local[256];
        char begin_utc[256];
        char end_utc[256];
        strftime(begin_local, sizeof(begin_local), "%c", &broken_down_local);
        strftime(begin_utc, sizeof(begin_utc), "%c", &args->time_begin);
        strftime(end_utc, sizeof(end_utc), "%c", &args->time_end);
        DEBUG("args: set_time_point: `%s' (from `%s' to `%s' aligned UTC)",
              begin_local, begin_utc, end_utc);
    }

    return E_OK;
}

/**
 * @brief Parse time range specification string and save the results.
 *
 * Function tries to parse time range string, fills time_begin and time_end with
 * appropriate values on success. Beginning and ending dates (and times) are
 * separated with TIME_RANGE_DELIM, if ending date is not specified, current
 * time is used.
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
 * @param[in,out] args Structure with parsed command line options.
 * @param[in] range_str Time range string, usually gathered from command line.
 *
 * @return E_OK on success, E_ARG on failure.
 */
static error_code_t
set_time_range(struct cmdline_args *const args, char *const range_str)
{
    assert(args && range_str && range_str[0] != '\0');

    // split time range string into beginning and ending
    char *const begin_str = range_str;
    char *end_str = NULL;
    char *const delim_pos = strchr(begin_str, TIME_RANGE_DELIM);  // find the first #
    if (delim_pos) {  // delimiter found, have both begin and end
        *delim_pos = '\0';  // to terminate begin_str
        end_str = delim_pos + 1;
    } // else delimiter not found, have only begin

    // parse the local time string into the broken-down local time
    struct tm begin_broken_down_local;
    error_code_t ecode = str_to_tm(begin_str, &begin_broken_down_local);
    if (ecode != E_OK) {
        return E_ARG;
    }

    struct tm end_broken_down_local;
    if (end_str) {
        ecode = str_to_tm(end_str, &end_broken_down_local);
        if (ecode != E_OK) {
            return E_ARG;
        }
    } else {  // no end time means the end is now
        const time_t now = time(NULL);  // number of seconds since the Epoch UTC
        ABORT_IF(now == (time_t)-1, E_INTERNAL, "time(): %s", strerror(errno));
        void *const ret = localtime_r(&now, &end_broken_down_local);
        ABORT_IF(!ret, E_INTERNAL, "localtime_r(): %s", strerror(errno));
    }

    // convert to UTC
    tm_local_to_utc(begin_broken_down_local, &args->time_begin);
    tm_local_to_utc(end_broken_down_local, &args->time_end);

    // align beginning to the beginning of the rotation interval
    while (mktime_utc(&args->time_begin) % FLOW_FILE_ROTATION_INTERVAL){
        args->time_begin.tm_sec--;
    }
    // align ending to the ending of the rotation interval
    while (mktime_utc(&args->time_end) % FLOW_FILE_ROTATION_INTERVAL){
        args->time_end.tm_sec++;
    }

    if (verbosity >= VERBOSITY_DEBUG) {
        char begin_local[256];
        char begin_utc[256];
        char end_local[256];
        char end_utc[256];
        strftime(begin_local, sizeof(begin_local), "%c", &begin_broken_down_local);
        strftime(begin_utc, sizeof(begin_utc), "%c", &args->time_begin);
        strftime(end_local, sizeof(end_local), "%c", &end_broken_down_local);
        strftime(end_utc, sizeof(end_utc), "%c", &args->time_end);
        DEBUG("args: set_time_range: from `%s' to `%s' (from `%s' to `%s' aligned UTC)",
              begin_local, begin_utc, end_local, end_utc);
    }

    // check time range sanity
    if (tm_diff(args->time_end, args->time_begin) <= 0) {
        ERROR(E_ARG, "zero or negative time range duration");
        return E_ARG;
    }
    return E_OK;
}
/**  @} */  // time_string_parsing group


/**
 * @brief Parse multiple libnf field from a string and save the results into the
 *        fields structure.
 *
 * Valid string contains of a FIELDS_DELIM separated field specifications (as
 * described in @ref fields_parse).
 *
 * @param[in] fields_spec FIELDS_DELIM separated field specifiacations.
 * @param[in,out] fields Pointer to the fields structure to be filled.
 * @param[in] are_aggr_keys Flag determining whether fields should be added as
 *                          aggregation keys or as a output fields.
 *
 * @return True on success, false on failure.
 */
static bool
parse_fields(const char *const fields_spec, struct fields *const fields,
             const bool are_aggr_keys)
{
    assert(fields_spec && fields);

    // make a modifiable copy of the fields_spec string
    char *const fields_spec_mod = strdup(fields_spec);
    ABORT_IF(!fields_spec_mod, E_MEM, strerror(errno));

    DEBUG("args: parsing fields spec `%s'", fields_spec_mod);

    size_t fields_found = 0;
    char *saveptr = NULL;
    for (char *token = strtok_r(fields_spec_mod, FIELDS_DELIM, &saveptr);
            token != NULL;
            token = strtok_r(NULL, FIELDS_DELIM, &saveptr))
    {
        int field_id;
        int field_alignment;
        int ipv6_alignment;
        bool success = field_parse(token, &field_id, &field_alignment,
                                   &ipv6_alignment);
        if (!success) {
            return false;
        }

        if (are_aggr_keys) {
            success = fields_add_aggr_key(fields, field_id, field_alignment,
                                          ipv6_alignment);
        } else {
            success = fields_add_output_field(fields, field_id);
        }
        if (!success) {
            return false;
        }
        fields_found++;
    }
    free(fields_spec_mod);

    if (are_aggr_keys && fields_found == 0) {
        ERROR(E_ARG, "aggregation enabled, but no aggregation key specified");
        return false;
    }

    return true;
}

/**
 * @brief Parse sort specification string and save the results into the fields
 *        structure.
 *
 * Sort specification string is expected in "field[#direction]" format, where
 * the field is a libnf field able to work as sort key (see \ref fields_set_sort_key)
 * and direction is either "asc" for ascending direction or "desc" for
 * descending sort direction.
 *
 * @param[in] stat_spec Sort specification string, usually gathered from the
 *                      command line.
 * @param[in,out] fields Pointer to the fields structure to be filled.
 *
 * @return True on success, false otherwise.
 */
static bool
parse_sort_spec(const char *const sort_spec, struct fields *const fields)
{
    assert(sort_spec && fields);

    DEBUG("args: parsing sort spec `%s'", sort_spec);

    int field_id;
    int field_alignment;
    int ipv6_alignment;
    int direction = LNF_SORT_NONE;
    const char *const delim_pos = strchr(sort_spec, SORT_DELIM);  // find the first #
    if (delim_pos) {  // delimiter found, direction should follow
        char *field_str = strndup(sort_spec, delim_pos - sort_spec);
        ABORT_IF(!field_str, E_MEM, strerror(errno));

        const char *const direction_str = delim_pos + 1;
        DEBUG("args: sort spec delimiter found, using `%s' as a sort key and `%s' as a direction",
              sort_spec, direction_str);
        if (strcmp(direction_str, "asc") == 0) {  // ascending direction
            direction = LNF_SORT_ASC;
        } else if (strcmp(direction_str, "desc") == 0) {  // descending dir.
            direction = LNF_SORT_DESC;
        } else {  // invalid sort direction
            ERROR(E_ARG, "invalid sort direction `%s'", direction_str);
            return E_ARG;
        }

        // parse sort key from string; netmask is pointless in case of sort key
        if (!field_parse(field_str, &field_id, &field_alignment,
                         &ipv6_alignment))
        {
            return false;
        }
        free(field_str);
    } else {  // delimiter not found, direction not specified; use the default
        DEBUG("args: sort spec delimiter not found, using whole sort spec as a sort key and its default direction");

        // parse sort key from string; netmask is pointless in case of sort key
        if (!field_parse(sort_spec, &field_id, &field_alignment,
                         &ipv6_alignment))
        {
            return false;
        }
    }

    return fields_set_sort_key(fields, field_id, direction);
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
parse_stat_spec(char *stat_optarg, const char *aggr_optarg[],
                const char *sort_optarg[], const char *limit_optarg[])
{
    assert(stat_optarg && aggr_optarg && sort_optarg && limit_optarg);

    DEBUG("args: stat spec: parsing `%s'", stat_optarg);
    *aggr_optarg = stat_optarg;

    char *const delim_pos = strchr(stat_optarg, STAT_DELIM);  // find first
    if (delim_pos) {  // delimiter found, sort spec should follow
        *delim_pos = '\0';  // to distinguish between the fields and the rest
        *sort_optarg = delim_pos + 1;
    } else {  // delimiter not found, sort spec not specified; use the defaults
        *sort_optarg = DEFAULT_STAT_SORT_KEY;
    }

    *limit_optarg = DEFAULT_STAT_REC_LIMIT;
    DEBUG("args: stat spec: aggr spec = `%s', sort spec = `%s', limit spec = `%s'",
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
                ERROR(E_ARG, "cannot initialize filter `%s'", filter_str);
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
                        ERROR(E_ARG, "unknown output item `%s'", token);
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
                ERROR(E_ARG, "unknown output format string `%s'", format_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t set_output_ts_conv(struct output_params *op,
                char *ts_conv_str)
{
        if (strcmp(ts_conv_str, "none") == 0) {
                op->ts_conv= OUTPUT_TS_CONV_NONE;
        } else if (strcmp(ts_conv_str, "pretty") == 0) {
                op->ts_conv= OUTPUT_TS_CONV_PRETTY;
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
                ERROR(E_ARG, "unknown output volume conversion string `%s'",
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
                ERROR(E_ARG, "unknown tcp flags conversion string `%s'",
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
                ERROR(E_ARG, "unknown IP address conversion string `%s'",
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
                ERROR(E_ARG, "unknown internet protocol conversion string `%s'",
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
                ERROR(E_ARG, "unknown duration conversion string `%s'",
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
                ERROR(E_ARG, "unknown progress bar type `%s'",
                      progress_bar_type_str);
                return E_ARG;
        }

        return E_OK;
}

static error_code_t
set_verbosity_level(const char *const verbosity_str)
{
    const char *const conversion_err = str_to_int(verbosity_str,
                                                  (int *)&verbosity);
    if (conversion_err) {
        ERROR(E_ARG, "invalid verbosity level `%s': %s", verbosity_str,
              conversion_err);
        return E_ARG;
    }
    if (!IN_RANGE_INCL(verbosity, VERBOSITY_QUIET, VERBOSITY_DEBUG)) {
        ERROR(E_ARG, "invalid verbosity level `%s': allowed range is [%d,%d]",
              verbosity_str, VERBOSITY_QUIET, VERBOSITY_DEBUG);
        return E_ARG;
    }

    if (verbosity == VERBOSITY_INFO) {
        INFO("args: setting verbosity level to info");
    } else if (verbosity == VERBOSITY_DEBUG) {
        DEBUG("args: setting verbosity level to debug");
    }

    return E_OK;
}

static error_code_t
set_num_threads(const char *const num_threads_str)
{
    int num_threads = 0;
    const char *const conversion_err = str_to_int(num_threads_str, &num_threads);
    if (conversion_err) {
        ERROR(E_ARG, "invalid number of threads `%s': %s", num_threads_str,
              conversion_err);
        return E_ARG;
    } else if (num_threads < 1) {
        ERROR(E_ARG, "invalid number of threads `%s': has to be a positive number",
              num_threads_str);
        return E_ARG;
    }

    INFO("args: setting number of threads to %d", num_threads);
    omp_set_num_threads(num_threads);
    return E_OK;
}

/**
 * @brief Set time zone to initialize time conversion information for all
 *        time-related functionality.
 *
 * If time_zone_optarg is NULL, the UTC is used. If it contains "system", the
 * system time zone is used. Otherwise, user-specified time zone is used.
 *
 * From tzset(3):
 *   - If the TZ variable does not appear in the environment, the system
 *   timezone is used.
 *   - If the TZ variable does appear in the environment, but its value is
 *   empty, or its value cannot be interpreted using any of the formats
 *   specified below, then Coordinated Universal Time (UTC) is used.
 *   - Otherwise, the value of TZ is used.
 *
 * @param time_zone_optarg NULL, "system", or valid POSIX format of the TZ
 *                         environment variable.
 */
static void
set_time_zone(const char *const time_zone_optarg)
{
    if (time_zone_optarg) {
        if (strcmp(time_zone_optarg, "system") == 0) {
            DEBUG("args: using the system time zone");
            int ret = unsetenv("TZ");
            ABORT_IF(ret, E_INTERNAL, "unsetenv(): %s", strerror(errno));
        } else {
            DEBUG("args: using the user-specified time zone `%s'",
                  time_zone_optarg);
            int ret = setenv("TZ", time_zone_optarg, 1);
            ABORT_IF(ret, E_INTERNAL, "setenv(): %s", strerror(errno));
        }
    } else {
        DEBUG("args: using UTC time zone");
        int ret = setenv("TZ", "", 1);
        ABORT_IF(ret, E_INTERNAL, "setenv(): %s", strerror(errno));
    }
    tzset();  // apply changes in TZ
}

/*
 * Public functions.
 */
error_code_t
arg_parse(struct cmdline_args *args, int argc, char *const argv[],
          bool root_proc)
{
    error_code_t ecode = E_OK;

    // set default values for certain arguments
    args->use_tput = true;
    args->use_bfindex = true;
    args->rec_limit = SIZE_MAX;  // SIZE_MAX means record limit is unset

    args->output_params.ellipsize = true;  // ellipsize long fields
    args->output_params.rich_header = false;  // header with aggr. func. etc.

    // revent all non-root processes from printing getopt_long() errors
    if (!root_proc) {
        opterr = 0;
    }

    const char *aggr_optarg = NULL;
    const char *limit_optarg = NULL;
    const char *sort_optarg = NULL;
    char *filter_optarg = NULL;
    char *time_point_optarg = NULL;
    char *time_range_optarg = NULL;
    char *output_fields_optarg = NULL;
    char *time_zone_optarg = NULL;
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
            ecode = set_verbosity_level(optarg);
            break;

        // output-affecting options
        case OPT_OUTPUT_FIELDS:
            output_fields_optarg = optarg;
            break;
        case OPT_OUTPUT_ITEMS:
            ecode = set_output_items(&args->output_params, optarg);
            break;
        case OPT_OUTPUT_FORMAT:
            ecode = set_output_format(&args->output_params, optarg);
            break;
        case OPT_OUTPUT_RICH_HEADER:
            args->output_params.rich_header = true;
            break;
        case OPT_OUTPUT_NO_ELLIPSIZE:
            args->output_params.ellipsize = false;
            break;

        case OPT_OUTPUT_TS_CONV:
            ecode = set_output_ts_conv(&args->output_params, optarg);
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

        // progress bar
        case OPT_PROGRESS_BAR_TYPE:
            ecode = set_progress_bar_type(&args->progress_bar_type, optarg);
            break;
        case OPT_PROGRESS_BAR_DEST:
            args->progress_bar_dest = optarg;
            break;

        // other
        case OPT_NUM_THREADS:
            ecode = set_num_threads(optarg);
            break;
        case OPT_TIME_ZONE:
            time_zone_optarg = optarg;
            break;
        case OPT_NO_TPUT:
            args->use_tput = false;
            break;
        case OPT_NO_BFINDEX:
            args->use_bfindex = false;
            break;

        // getting help
        case OPT_HELP:
            if (root_proc) {
                printf("%s\n\n", USAGE_STRING);
                printf("For the complete documentation see man 1 " PROJECT_NAME
                       ".\n");
            }
            return E_HELP;
        case OPT_VERSION:  // version
            if (root_proc) {
                printf("%s %s\n", PROJECT_NAME, PROJECT_VERSION);
            }
            return E_HELP;


        default:  // '?' or '0'
            return E_ARG;
        }

        if (ecode != E_OK) {
            return ecode;
        }
    }  // while (true) loop through all command-line arguments

    ////////////////////////////////////////////////////////////////////////////
    set_time_zone(time_zone_optarg);

    // parse aggregation and sort option argument options
    if (aggr_optarg) {  // aggregation mode with optional sorting
        args->working_mode = MODE_AGGR;
        if (!parse_fields(aggr_optarg, &args->fields, true)) {
            return E_ARG;
        }
        if (sort_optarg) {
            DEBUG("args: using aggregation mode with sorting");
            if (!parse_sort_spec(sort_optarg, &args->fields)) {
                return E_ARG;
            }
        } else {
            DEBUG("args: using aggregation mode without sorting");
        }
    } else if (sort_optarg) {  // sort mode
        DEBUG("args: using sorting mode");
        args->working_mode = MODE_SORT;
        if (!parse_sort_spec(sort_optarg, &args->fields)) {
            return E_ARG;
        }
    } else {  // listing mode
        DEBUG("args: using listing mode");
        args->working_mode = MODE_LIST;
    }

    // parse record limit argument option
    if (limit_optarg) {  // record limit specified
        const char *const conversion_err = str_to_luint(limit_optarg,
                                                        &args->rec_limit);
        if (conversion_err) {
            ERROR(E_ARG, "record limit `%s': %s", limit_optarg,
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
        ERROR(E_ARG, "time point and time range are mutually exclusive");
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
        ERROR(E_ARG, "missing path");
        return E_ARG;
    } else {
        args->paths = argv + optind;
        args->paths_cnt = argc - optind;
    }

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

    // parse output fields option argument
    if (output_fields_optarg) {
        if (!parse_fields(output_fields_optarg, &args->fields, false)) {
            return E_ARG;
        }
    } else {  // output fields not specidied, use default values
        bool ret = true;
        switch (args->working_mode) {
        case MODE_LIST:
            ret = parse_fields(DEFAULT_LIST_FIELDS, &args->fields, false);
            break;
        case MODE_SORT:
            ret = parse_fields(DEFAULT_SORT_FIELDS, &args->fields, false);
            break;
        case MODE_AGGR:
            ret = parse_fields(DEFAULT_AGGR_FIELDS, &args->fields, false);
            break;
        case MODE_META:
            break;
        case MODE_UNSET:
            ABORT(E_INTERNAL, "invalid working mode");
        default:
            ABORT(E_INTERNAL, "unknown working mode");
        }
        assert(ret);
        (void)ret;  // to suppress -Wunused-variable with -DNDEBUG
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
            args->output_params.ts_conv = OUTPUT_TS_CONV_PRETTY;
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
        ABORT(E_INTERNAL, "illegal output parameters format");
    default:
        ABORT(E_INTERNAL, "unkwnown output parameters format");
    }

    ////////////////////////////////////////////////////////////////////////////
    /*
     * The TPUT algorithm for Top-N queries requires:
     *   - aggregation,
     *   - record limit (without it all records would be exchanged anyway),
     *   - sorting by one of traffic volume fields (data octets, packets, out
     *     bytes, out packets, or aggregated flows).
     * If these condition are not satisfied, TPUT will be disabled.
     */
    if (args->use_tput) {
        if (args->working_mode != MODE_AGGR
                || !args->rec_limit
                || !args->fields.sort_key.field
                || !IN_RANGE_INCL(args->fields.sort_key.field->id,
                                  LNF_FLD_DOCTETS, LNF_FLD_AGGR_FLOWS))
        {
            INFO("disabling TPUT, one or more conditions were not met");
            args->use_tput = false;
        }
    }

    const bool ret = fields_check(&args->fields);
    assert(ret);
    (void)ret;  // to suppress -Wunused-variable with -DNDEBUG

    ////////////////////////////////////////////////////////////////////////////
    if (root_proc && verbosity >= VERBOSITY_DEBUG) {
        fields_print_debug(&args->fields);

        //char begin[255], end[255];
        //char *path = args->path_str;
        //int c;

        //printf("------------------------------------------------------\n");
        //printf("mode: %s\n", working_mode_to_str(args->working_mode));
        //if (args->working_mode == MODE_AGGR && args->use_tput) {
        //    printf("flags: using the TPUT algorithm for Top-N queries\n");
        //}

        //if(args->filter_str) {
        //    printf("filter: %s\n", args->filter_str);
        //}

        //printf("paths:\n\t");
        //while ((c = *path++)) {
        //    if (c == 0x1C) { //substitute separator with end of line
        //        putchar('\n');
        //        putchar('\t');
        //    } else {
        //        putchar(c);
        //    }
        //}
        //putchar('\n');

        //strftime(begin, sizeof(begin), "%c", &args->time_begin);
        //strftime(end, sizeof(end), "%c", &args->time_end);
        //printf("time range: %s - %s\n", begin, end);
        //printf("------------------------------------------------------\n\n");
    }

    return E_OK;
}
