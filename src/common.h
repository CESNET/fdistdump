/**
 * \file common.h
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
 * \author Pavel Krobot, <Pavel.Krobot@cesnet.cz>
 * \date 2015
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

#ifndef COMMON_H
#define COMMON_H

#include <stddef.h> //size_t

#include <libnf.h>
#include <mpi.h>

#define MAX_FN_LEN 2048
#define MAX_AGG_PARAMS 16 //maximum count of -a parameters

#define XCHG_BUFF_MAX_SIZE (1024 * 1024) //KiB
#define XCHG_BUFF_ELEMS (XCHG_BUFF_MAX_SIZE / sizeof(lnf_brec1_t))
#define XCHG_BUFF_SIZE (XCHG_BUFF_ELEMS * sizeof(lnf_brec1_t))


/* Enumerations. */
enum {
        E_OK,
        E_MEM,
        E_MPI,
        E_LNF,
        E_INTERNAL,
        E_ARG,
        E_HELP,
};

typedef enum { //working modes
        MODE_REC,
        MODE_AGG,
} working_mode_t;


/* Data types. */
//WATCH OUT: reflect changes also in agg_params_mpit
#define AGG_PARAMS_T_ELEMS 4
typedef struct {
        int field;
        int flags;
        int numbits;
        int numbits6;
} agg_params_t;

//WATCH OUT: reflect changes also in task_info_mpit
#define INITIAL_INTO_T_ELEMS 7
typedef struct {
        working_mode_t working_mode; //working mode

        agg_params_t agg_params[MAX_AGG_PARAMS]; //aggregation pamrameters
        size_t agg_params_cnt; //aggregation parameters count

        size_t filter_str_len; //filter expression string length
        size_t path_str_len; //path string length

        size_t rec_limit; //record/aggregation limit

        size_t slave_cnt; //active slave count
        /* note (MPI): this information has to be sent explicitly, since there
           could be another framework than MPI used for communication */

} task_info_t;


/* MPI related */
#define ROOT_PROC 0

enum { //tags
        TAG_CMD,
        TAG_TASK,
        TAG_FILTER,
        TAG_AGG,
        TAG_DATA,
        TAG_TOPN_ID,
};

enum { //control commands
        CMD_RELEASE,
};


/* Function-like macros */


/* Debugging macros */
#ifdef DEBUG

#define print_debug(...) \
        do { \
        printf(__VA_ARGS__); \
        } while (0)
#else
#define print_debug(...)

#endif //DEBUG


/** \brief Print basic record.
 *
 * Prints instance of lnf_brec1_t as one line.
 *
 * \param[in] brec Basic record.
 */
void print_brec(const lnf_brec1_t brec);


/** \brief Print error message.
 *
 * Wrapper to fprintf(), add prefix including process rank and name.
 *
 * \param[in] format Format string passed to fprintf().
 * \param[in] va_list Variable argument list passed to vfprintf().
 */
void print_err(const char *format, ...);


void create_agg_params_mpit(MPI_Datatype *agg_params_mpit);
void free_agg_params_mpit(MPI_Datatype *agg_params_mpit);
void create_task_info_mpit(MPI_Datatype *task_info_mpit,
                MPI_Datatype agg_params_mpit);
void free_task_info_mpit(MPI_Datatype *task_info_mpit);
int agg_init(lnf_mem_t **agg, const agg_params_t *agg_params,
                size_t agg_params_cnt);

/**
 * \brief Prepare Top-N statistics memory structure.
 */
int stats_init(lnf_mem_t **stats);

#endif //COMMON_H
