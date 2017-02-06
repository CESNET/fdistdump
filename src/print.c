/**
 * \file print.c
 * \brief Implementation of console message printing.
 * \author Jan Wrona, <wrona@cesnet.cz>
 * \date 2016
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


#include "common.h"
#include "print.h"

#include <stdarg.h> //variable argument list
#include <assert.h>

#ifdef _OPENMP
#include <omp.h>
#endif //_OPENMP
#include <mpi.h>


/* Default verbosity levele is warning. */
verbosity_t verbosity = VERBOSITY_WARNING;


/**
 * \defgroup print_func Error/warning/info/debug printing functions.
 * @{
 */
/** \brief Convert error_code_t error code to human-readable string.
 *
 * \param[in] e1 fdistdump error code.
 * \return Static string at most MAX_STR_LEN long.
 */
static char * error_code_to_str(error_code_t e1)
{
        static char msg[MAX_STR_LEN];

        switch (e1) {
        case E_OK:
                snprintf(msg, MAX_STR_LEN, "no error");
                break;

        case E_EOF:
                snprintf(msg, MAX_STR_LEN, "end of file");
                break;

        case E_MEM:
                snprintf(msg, MAX_STR_LEN, "out of memory");
                break;

        case E_MPI:
                snprintf(msg, MAX_STR_LEN, "MPI");
                break;

        case E_LNF:
                snprintf(msg, MAX_STR_LEN, "libnf");
                break;

        case E_INTERNAL:
                snprintf(msg, MAX_STR_LEN, "internal");
                break;

        case E_ARG:
                snprintf(msg, MAX_STR_LEN, "command line argument");
                break;

        case E_PATH:
                snprintf(msg, MAX_STR_LEN, "path");
                break;

        case E_IDX:
                snprintf(msg, MAX_STR_LEN, "index");
                break;

        default:
                assert(!"unknown error code");
        };

        return msg;
}


void print_msg(error_code_t e1, int e2, const char *prefix, const char *file,
                const char *func, const int line, ...)
{
        FILE *stream = stderr;
        va_list arg_list;

        char res[MAX_STR_LEN];
        size_t off = 0;

        /* Add prefix for every verbosity. */
        off += snprintf(res + off, MAX_STR_LEN - off, "%s: ", prefix);

        /* Add fdistdump error info. */
        if (e1 != E_OK) {
                off += snprintf(res + off, MAX_STR_LEN - off, "%s: ",
                                error_code_to_str(e1));

                /* Add external (libnf) error info. */
                if (e1 == E_LNF && e2 == LNF_ERR_OTHER_MSG) {
                        char lnf_error_str[LNF_MAX_STRING];

                        lnf_error(lnf_error_str, LNF_MAX_STRING);
                        off += snprintf(res + off, MAX_STR_LEN - off, "%s: ",
                                        lnf_error_str);
                }
        }

        /* Add additional string from format (first in arg_list) and varargs. */
        va_start(arg_list, line);
        off += vsnprintf(res + off, MAX_STR_LEN - off,
                        va_arg(arg_list, const char *), arg_list);
        va_end(arg_list);

        /* Add location MPI and OpenMP info only for debug verbosity. */
        if (verbosity >= VERBOSITY_DEBUG) {
                char mpi_processor_name[MPI_MAX_PROCESSOR_NAME + 1];
                int mpi_processor_name_len;
                int mpi_world_rank;
                int mpi_world_size;

                MPI_Comm_rank(MPI_COMM_WORLD, &mpi_world_rank);
                MPI_Comm_size(MPI_COMM_WORLD, &mpi_world_size);
                MPI_Get_processor_name(mpi_processor_name,
                                &mpi_processor_name_len);
                mpi_processor_name[mpi_processor_name_len] = '\0';

                off += snprintf(res + off, MAX_STR_LEN - off,
                                "\t[location: %s:%s():%d]", file, func, line);
                off += snprintf(res + off, MAX_STR_LEN - off,
                                " [MPI: %d/%d %s]", mpi_world_rank,
                                mpi_world_size, mpi_processor_name);
#ifdef _OPENMP
                if (omp_in_parallel()) {
                        off += snprintf(res + off, MAX_STR_LEN - off,
                                        " [OpenMP: %d/%d]",
                                        omp_get_thread_num(),
                                        omp_get_num_threads());
                }
#endif //_OPENMP
        }

        off += snprintf(res + off, MAX_STR_LEN - off, "\n");
        /* Finally print string res. */
        fprintf(stream, res);
}
/**
 * @}
 */ //print_func
