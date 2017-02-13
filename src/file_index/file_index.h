/**
 * \file file_index.h
 * \brief Header file for file-indexing using Bloom filter indexes for IP
 * addresses
 * \author Pavel Krobot, <Pavel.Krobot@cesnet.cz>
 * \date 2016
 */

/*
 * Copyright (C) 2016 CESNET
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


#include "common.h"

#include <stdbool.h>
#include <libnf.h>
#include <ffilter.h>


/* Prefix of file-index file names */
#define FIDX_FN_PREFIX ".bf_index"


struct fidx_ip_tree_node; //forward declaration


/** \brief Get a file-indexing IP address tree from a filter tree.
 *
 * Indexing IP address tree is a tree with 4 type of nodes:
 *   - AND / OR nodes - with the meaning of logical AND / OR operators,
 *   - IPv4 / IPv6 address - node containing single IP address.
 *
 * \note Filter tree nodes other than AND/OR/IP are ignored.
 *
 * \param[in] filter_root  Pointer to the root of the filter tree
 * \param[in] idx_root  Pointer to the root of the file-indexing IP address tree.
 * \return Returns E_OK on success, error code otherwise.
 */
error_code_t fidx_get_tree(ff_node_t *filter_root,
                struct fidx_ip_tree_node **idx_root);

/** \brief Destroy a file-indexing IP address tree.
 *
 * \note Function sets the root node to NULL.
 *
 * \param[in] fidx_ip_tree_node  Pointer to the root of a tree to destroy.
 */
void fidx_destroy_tree(struct fidx_ip_tree_node **fidx_ip_tree_node);

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
bool fidx_ips_in_file(const char *path, struct fidx_ip_tree_node *ip_tree);


#endif // _FDD_FILE_INDEX_H
