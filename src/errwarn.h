/**
 * @brief Error handling and error, warning, info, debug console messages.
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
 * The exit() function after MPI_Abort() will be never called, it serves as a
 * hint for compilers and static analysis, because they usually dont know what
 * MPI_Abort() does.
 *
 * @param[in] ecode  fdistdump error code.
 * @param[in] ...    Additional arguments beginning with a format string.
 */
#define ABORT(ecode, ...) \
    do { \
        ewid_print(ecode, "Error", __FILE__, __func__,  __LINE__, __VA_ARGS__); \
        MPI_Abort(MPI_COMM_WORLD, ecode); \
        exit(ecode); \
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
