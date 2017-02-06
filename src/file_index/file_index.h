/**
 * \file file_index.h
 * \brief Header file for file-indexing using Bloom filter indexes for IP
 * addresses
 * \author Pavel Krobot, <Pavel.Krobot@cesnet.cz>
 * \date 2016 - 2017
 */

/*
 * Copyright (C) 2016, 2017 CESNET
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

#ifndef _FDD_FILE_INDEX_H
#define _FDD_FILE_INDEX_H

#include "ip_tree.h"

#include <stdbool.h>

/* Prefix of file-index file names */
#define F_INDEX_FN_PREFIX ".bf_index"


/** \brief Check if IP addresses in a file-indexing IP address tree are
 * contained in a file.
 *
 * Functions moves through the file-indexing IP address tree and checks if IP
 * addresses stored in IP address nodes are contained in a Bloom filter index
 * of a given data file. Returns result of logical expression represented by
 * AND/OR nodes of the file-indexing IP address tree and results of these
 * checks.
 *
 * \param[in] path  Path to the data file.
 * \param[in] ip_tree  Pointer to the file-indexing IP address tree.
 * \return Returns true or false according to the result of the evaluation of
 *         the logical expression represented by the file-indexing IP address
 *         tree.
 */
bool ips_in_file(const char *path, ip_tree_node_t *ip_tree);

#endif // _FDD_FILE_INDEX_H
