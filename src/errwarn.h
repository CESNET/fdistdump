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

#pragma once

#include <stdbool.h>  // for false
#include <stdio.h>    // for snprintf, size_t

#include <mpi.h>      // for MPI_COMM_WORLD

#include "common.h"   // for error_code_t


// declare verbosity_t
typedef enum {
    VERBOSITY_QUIET,
    VERBOSITY_ERROR,
    VERBOSITY_WARNING,
    VERBOSITY_INFO,
    VERBOSITY_DEBUG,
} verbosity_t;

// export global verbosity (defined in errwarn.c)
extern verbosity_t verbosity;


/*
 * Function-like macros.
 */
/**
 * @brief Print error message and terminate MPI execution environment.
 *
 * @param[in] ecode  fdistdump error code.
 * @param[in] ...    Additional arguments beginning with a format string.
 */
#define ABORT(ecode, ...) \
    do { \
        ewid_print(ecode, "Error", __FILE__, __func__,  __LINE__, __VA_ARGS__); \
        MPI_Abort(MPI_COMM_WORLD, ecode); \
    } while (0)

/**
 * @brief If the condition is evaluated as true, print error message and
 *        terminate MPI execution environment.
 *
 * @param[in] cond   Any valid expression statement.
 * @param[in] ecode  fdistdump error code.
 * @param[in] ...    Additional arguments beginning with a format string.
 */
#define ABORT_IF(cond, ecode, ...) \
    do { \
        if (cond) { \
            ABORT(ecode, __VA_ARGS__); \
        } \
    } while (0)

/**
 * @brief Print error/warning/info/debug message macros.
 *
 * @param[in] ecode  fdistdump error code.
 * @param[in] ...    Additional arguments beginning with a format string.
 */
#define ERROR(ecode, ...) \
    do { \
        if (verbosity >= VERBOSITY_ERROR) { \
            ewid_print(ecode, "Error", __FILE__, __func__, __LINE__,  \
                       __VA_ARGS__); \
        } \
    } while (0)

#define WARNING(ecode, ...) \
    do { \
        if (verbosity >= VERBOSITY_WARNING) { \
            ewid_print(ecode, "Warning", __FILE__, __func__, __LINE__, \
                       __VA_ARGS__); \
        } \
    } while (0)

#define INFO(...) \
    do { \
        if (verbosity >= VERBOSITY_INFO) { \
            ewid_print(E_OK, "Info", __FILE__, __func__, __LINE__, \
                       __VA_ARGS__); \
        } \
    } while (0)

#define DEBUG(...) \
    do { \
        if (verbosity >= VERBOSITY_DEBUG) { \
            ewid_print(E_OK, "Debug", __FILE__, __func__, __LINE__, \
                       __VA_ARGS__); \
        } \
    } while (0)


/*
 * Function prototypes.
 */
void
ewid_print(const error_code_t ecode, const char *const prefix,
           const char *const file, const char *const func, const int line, ...);
