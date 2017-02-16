/** Declarations for console message printing (debug,info,warning, error).
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

#pragma once


#include "config.h"
#include "common.h"


typedef enum {
        VERBOSITY_QUIET,
        VERBOSITY_ERROR,
        VERBOSITY_WARNING,
        VERBOSITY_INFO,
        VERBOSITY_DEBUG,
} verbosity_t;


/* Export verbosity to all translation units where this header is included. */
extern verbosity_t verbosity;


/** \breif Print error/warning/info/debug macros.
 *
 * \param[in] e1     fdistdump error code.
 * \param[in] e2     External error code (e.g. code returned by libnf).
 * \param[in] ...    Additional arguments beginning with a format string.
 */
#define PRINT_ERROR(e1, e2, ...) \
        do { \
                if (verbosity >= VERBOSITY_ERROR) { \
                        print_msg(e1, e2, "ERROR", __FILE__, __func__, \
                                        __LINE__,  __VA_ARGS__); \
                } \
        } while (0)

#define PRINT_WARNING(e1, e2, ...) \
        do { \
                if (verbosity >= VERBOSITY_WARNING) { \
                        print_msg(e1, e2, "WARNING", __FILE__, __func__, \
                                        __LINE__, __VA_ARGS__); \
                } \
        } while (0)

#define PRINT_INFO(...) \
        do { \
                if (verbosity >= VERBOSITY_INFO) { \
                        print_msg(E_OK, 0, "INFO", __FILE__, __func__, \
                                        __LINE__, __VA_ARGS__); \
                } \
        } while (0)

#define PRINT_DEBUG(...) \
        do { \
                if (verbosity >= VERBOSITY_DEBUG) { \
                        print_msg(E_OK, 0, "DEBUG", __FILE__, __func__, \
                                        __LINE__, __VA_ARGS__); \
                } \
        } while (0)


/** \breif Print error/warning/info/debug message.
 *
 * Function prints everything to the string first and than prints that string
 * to the output by a single call of fprintf(). This is a try to prevent MPI to
 * damage messages by combining output from different ranks in random order.
 *
 * This function may be called directly, but its better to use macros from
 * header file (PRINT{ERROR,WARNING,INFO,DEBUG}).
 *
 * \param[in] e1     fdistdump error code.
 * \param[in] e2     External error code (e.g. code returned by libnf).
 * \param[in] prefix Message prefix (e.g. ERROR, WARNING, ...).
 * \param[in] file   Name of the current input file (e.g. __FILE__).
 * \param[in] func   Name of the current function (e.g. __func__).
 * \param[in] line   Current input line number (e.g. __LINE__).
 * \param[in] ...    Additional arguments beginning with a format string.
 */
void print_msg(error_code_t e1, int e2, const char *prefix, const char *file,
                const char *func, const int line, ...);
