/**
 * @brief Error handling and error, warning, info, debug console messages.
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

#include "errwarn.h"

#include <assert.h>  // for assert
#include <stdarg.h>  // for va_arg, va_end, va_list, va_start

#ifdef _OPENMP
#include <omp.h>
#endif //_OPENMP
#include <libnf.h>   // for lnf_error, LNF_MAX_STRING, LNF_ERR_OTHER_MSG
#include <mpi.h>     // for MPI_Comm_rank, MPI_Comm_size, MPI_Get_processor_...

#include "common.h"  // for MAX_STR_LEN, ::E_LNF, ::E_OK, error_code_t, ::E_ARG


/*
 * Global variables.
 */
verbosity_t verbosity = VERBOSITY_WARNING;  // default is warning


/*
 * Static functions.
 */
/**
 * @brief Convert error_code_t ecode to a human-readable string.
 *
 * @param[in] ecode fdistdump error code.
 *
 * @return Static read-only string.
 */
static char *
error_code_to_str(const error_code_t ecode)
{
    switch (ecode) {
    case E_OK:
        return "no error";
    case E_EOF:
        return "end of file";
    case E_MEM:
        return "out of memory";
    case E_MPI:
        return "MPI";
    case E_LNF:
        return "libnf";
    case E_INTERNAL:
        return "internal";
    case E_ARG:
        return "command line arguments";
    case E_PATH:
        return "path";
    case E_BFINDEX:
        return "bfindex";
    case E_HELP:
        assert(!"illegal error code");
    default:
        assert(!"unknown error code");
    };
}


/*
 * Public functions.
 */
/**
 * @brief Print an Error/Warning/Info/Debug (EWID) message.
 *
 * First, write output to the string. Than, print that string to the output by a
 * single call of fprintf(). This is to prevent MPI to damage messages by
 * combining output from different ranks in random order.
 *
 * This function may be called directly, but its better to use macros from
 * header file {ERROR,WARNING,INFO,DEBUG}.
 *
 * @param[in] ecode  fdistdump error code.
 * @param[in] prefix Message prefix (e.g., Error, Warning, ...).
 * @param[in] file   Name of the current input file (use the __FILE__ macro).
 * @param[in] func   Name of the current function (use the. __func__ macro).
 * @param[in] line   Current input line number (use the  __LINE__ macro).
 * @param[in] ...    Additional arguments beginning with a format string.
 */
void
ewid_print(const error_code_t ecode, const char *const prefix,
           const char *const file, const char *const func, const int line, ...)
{
    char str[MAX_STR_LEN];
    char *str_term = str;
    size_t remaining = MAX_STR_LEN;

    // initialize with prefix for every verbosity level
    SNPRINTF_APPEND(str_term, remaining, "%s: ", prefix);

    // append fdistdump error code string
    if (ecode != E_OK) {
        SNPRINTF_APPEND(str_term, remaining, "%s: ", error_code_to_str(ecode));
    }

    // append user string based on the format (first in arg_list) and va_args
    va_list arg_list;
    va_start(arg_list, line);
    const char *format_str = va_arg(arg_list, const char *);
    const size_t would_write = vsnprintf(str_term, remaining, format_str,
                                         arg_list);
    if (would_write >= remaining) {  // the output was truncated
        remaining = 0;
    } else {  // the output was not truncated
        str_term += would_write;
        remaining -= would_write;
    }
    va_end(arg_list);

    // append source code location information
    SNPRINTF_APPEND(str_term, remaining, "\t[src: %s:%s():%d]", file, func,
                    line);

    // append MPI information
    int mpi_world_rank;
    MPI_Comm_rank(MPI_COMM_WORLD, &mpi_world_rank);
    int mpi_world_size;
    MPI_Comm_size(MPI_COMM_WORLD, &mpi_world_size);
    char mpi_processor_name[MPI_MAX_PROCESSOR_NAME + 1];
    int mpi_processor_name_len;
    MPI_Get_processor_name(mpi_processor_name, &mpi_processor_name_len);
    mpi_processor_name[mpi_processor_name_len] = '\0';
    SNPRINTF_APPEND(str_term, remaining, " [MPI: %d/%d %s]", mpi_world_rank + 1,
                   mpi_world_size, mpi_processor_name);

#ifdef _OPENMP
    SNPRINTF_APPEND(str_term, remaining, " [OpenMP: %d/%d]",
                    omp_get_thread_num() + 1, omp_get_num_threads());
#endif  // _OPENMP

    // append a newline and print string from the beginning
    SNPRINTF_APPEND(str_term, remaining, "%s", "\n");
    fputs(str, stderr);
}
