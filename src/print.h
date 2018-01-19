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


/**
* @brief Wrapper for a safer string appending using snprintf(). UNSAFE MACRO!
*
* The functions snprintf() and vsnprintf() do not write more than size bytes
* (including the terminating null byte). The main reason behind this macro is
* that if the output was truncated due to this limit, then THE RETURN VALUE IS
* THE NUMBER OF CHARACTERS (EXCLUDING THE TERMINATING NULL BYTE) WHICH WOULD
* HAVE BEEN WRITTEN to the final string if enough space had been available.
* Thus, a return value of size or more means that the output was truncated.
*
* Both str_term and remaining_size are modified in each call:
* If the added string did not have to be truncated, then str_term pointer is
* moved to point to the new first terminating null byte and remaining_size size
* is decreased by the length of the added string.
* If snprintf() could not write all the bytes (the added string had to be
* truncated) or completely skipped, remaining_size is set to 0 so the future
* expansions of SNPRINTF_APPEND would not case buffer overflow.
*
* The usage should be something like:
* char *const string = calloc(STR_LEN, sizeof (*string));
* char *str_term = string;  // to prevent loss of the original pointer
* size_t str_size = STR_LEN;
* SNPRINTF_APPEND(str_term, str_size, "some format", ...);
* SNPRINTF_APPEND(str_term, str_size, "some other format", ...);
*
* @param[in,out] str_term Pointer to the place in the string, where the
*                         appending should start. Usually, it is the first
*                         terminating null byte.
* @param[in,out] remaining_size Number of bytes remaining in the string.
* @param[in] format Format string to pass to snprintf().
* @param[in] ... Additional argument corresponding to the format string.
*/
#define SNPRINTF_APPEND(str_term, remaining_size, format, ...) \
    do { \
        const size_t _would_write = snprintf(str_term, remaining_size, format, \
                                             __VA_ARGS__); \
        if (_would_write >= remaining_size) {  /* the output was truncated */ \
            remaining_size = 0; \
        } else {  /* the output was not truncated */ \
            str_term += _would_write; \
            remaining_size -= _would_write; \
        } \
    } while (false)

/** \brief Print error/warning/info/debug macros.
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


/** \brief Print error/warning/info/debug message.
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
