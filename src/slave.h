/**
 * \file slave.h
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

#ifndef SLAVE_H
#define SLAVE_H

#include "common.h"

#include <stddef.h> //size_t
#include <limits.h> //PATH_MAX
#include <dirent.h> //DIR

#include <libnf.h>


// Register name
typedef struct global_context_s global_context_t;


/**
 * TODO: Desc
*/
typedef enum {
        DATA_SOURCE_FILE,
        DATA_SOURCE_DIR,
        DATA_SOURCE_INTERVAL,
} data_source_t;

/**
 * TODO: Desc
*/
typedef struct slave_task_s {
        task_setup_static_t setup;

        lnf_rec_t *rec;
        lnf_filter_t *filter;
        lnf_mem_t *agg;

        size_t proc_rec_cntr; //processed record counter

        data_source_t data_source; //how flow files are obtained
        char path_str[PATH_MAX];
        DIR *dir_ctx; //used in case of directory as data source

        char cur_file_path[PATH_MAX]; //current flow file absolute path

        int sort_key; //LNF field set as key for sorting in memory
}slave_task_t;

/** \brief Slave program function.
 *
 * \param[in] argc Count of program arguments (argc from main).
 * \param[in] argv Array of program arguments (argv from main).
 * \param[in] g_ctx Pointer to filled global_context structure.
 * \return E_OK on success, error code otherwise.
 */
int slave(int argc, char **argv, global_context_t *g_ctx);

#endif //SLAVE_H
