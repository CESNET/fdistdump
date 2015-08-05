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

#define CHOOSE_COMM_MPI ///TODO automatic "HAVE_MPI"-like


#include <stddef.h> //size_t
#include <time.h> //struct tm
#include <stdbool.h>

#include <libnf.h>

#define MAX_FN_LEN 2048
#define MAX_AGG_PARAMS 16 //maximum count of -a parameters

#define XCHG_BUFF_MAX_SIZE (1024 * 1024) //KiB
#define XCHG_BUFF_ELEMS (XCHG_BUFF_MAX_SIZE / sizeof(lnf_brec1_t))
#define XCHG_BUFF_SIZE (XCHG_BUFF_ELEMS * sizeof(lnf_brec1_t))

//TODO: move to configuration file and as parameter options
#define FLOW_FILE_ROTATION_INTERVAL 300 //seconds
#define FLOW_FILE_BASE_DIR "/data/profiles_data/"
#define FLOW_FILE_PROFILE "live"
#define FLOW_FILE_SOURCE "telia"
#define FLOW_FILE_PATH_FORMAT "%Y/%m/%d/"
#define FLOW_FILE_NAME_FORMAT "nfcapd.%Y%m%d%H%M"
#define FLOW_FILE_PATH (FLOW_FILE_BASE_DIR FLOW_FILE_PROFILE "/" \
                FLOW_FILE_SOURCE "/" FLOW_FILE_PATH_FORMAT \
                FLOW_FILE_NAME_FORMAT)

#ifdef CHOOSE_COMM_MPI
        #define FDD_ONE_BINARY
#else
        #define FDD_SPLIT_BINARY_MASTER
        #define FDD_SPLIT_BINARY_SLAVE
#endif

/* Enumerations. */
enum { //error return codes
        E_OK, //no error
        E_MEM, //memory
        E_MPI, //MPI
        E_LNF, //libnf
        E_INTERNAL, //internal
        E_ARG, //command line arguments
        E_HELP, //print help
        E_EOF, //end of file
};

typedef enum { //working modes
        MODE_REC, //list unmodified flow records
        MODE_ORD, //list ordered flow records
        MODE_AGG, //aggregation and statistic
} working_mode_t;

/* Data types. */
//WATCH OUT: MPI: reflect changes also in agg_params_mpit()
typedef struct agg_params{
        int field;
        int flags;
        int numbits;
        int numbits6;
} agg_params_t;

//WATCH OUT: MPI: reflect changes also in task_setup_mpit()
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

        struct tm interval_begin; //begin and end of time interval
        struct tm interval_end;

        bool use_fast_topn; //enables fast top-N algorithm
} task_setup_static_t;

typedef struct {
        task_setup_static_t s;

        char *filter_str; //filter expression string
        char *path_str; //path string
} task_setup_t;


/* Debugging macros */
#ifdef DEBUG
        #define print_debug(...) \
                do { \
                printf(__VA_ARGS__); \
                } while (0)
#else
        #define print_debug(...)
#endif //DEBUG


/**
 * TODO description
 */
void clear_task_setup(task_setup_static_t *t_setup);

/** \brief Print error message.
 *
 * ///TODO * Wrapper to fprintf(), add prefix including process rank and name.
 *
 * \param[in] format Format string passed to fprintf().
 * \param[in] va_list Variable argument list passed to vfprintf().
 */
void print_err(const char *format, ...);

//int agg_init(lnf_mem_t **agg, const agg_params_t *agg_params,
//                size_t agg_params_cnt);

/**
 * TODO description
 */
int mem_setup(lnf_mem_t *mem, const struct agg_params *ap, size_t ap_cnt);

/** \brief Print basic record.
 *
 * Prints instance of lnf_brec1_t as one line.
 *
 * \param[in] brec Basic record.
 */
int print_brec(const lnf_brec1_t *brec);

/**
 * TODO description
 */
int mem_print(lnf_mem_t *mem, size_t limit);

/**
 * TODO description
 */
double diff_tm(struct tm end_tm, struct tm begin_tm);

#endif //COMMON_H
