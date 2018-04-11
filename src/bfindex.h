/**
 * @brief Declarations for file indexing using Bloom filter indexes for IP
 *        addresses.
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

#include <stdbool.h>  // for bool

#include <ffilter.h>  // for ff_node_t


#define BFINDEX_FILE_NAME_PREFIX "bfi." /**< bfindex file prefix */


// forward declarations
struct bfindex_node;


/** \brief Construct a bfindex IP address tree from a filter tree.
 *
 * Indexing IP address tree is a tree with 2 type of nodes: operator nodes
 * (logical AND or OR) and IP address nodes (a storage for IPv4 or IPv6). Filter
 * tree may contain many other types of nodes, but those are ignored.
 *
 * \param[in] filter_root  Pointer to the root node of the filter tree.
 * \return  Pointer to the root node of the bfindex tree on success, NULL
 *          otherwise.
 */
struct bfindex_node *
bfindex_init(const ff_node_t *filter_root);

/** \brief Destroy the bfindex IP address tree.
 *
 * \param[in] bfindex_node  Pointer to node of the bfindex tree. Usually (but
 *                          not necessarily) the root node.
 */
void
bfindex_free(struct bfindex_node *bfindex_node);

/** \brief Create a bfindex file path from the flow file path.
 *
 * If the flow file name has the standard prefix FLOW_FILE_NAME_PREFIX, the
 * bfindex file name is constructed by subtituting this prefix with
 * BFINDEX_FILE_NAME_PREFIX. Otherwise, the bfindex file name is constructed by
 * prefixing the flow file name with BFINDEX_FILE_NAME_PREFIX.
 *
 * \param[in] flow_file_path  Flow file path string.
 * \return  Bfindex file path string on success, NULL in case of memory
 *          allocation error. It is callers responsibility to free the returned
 *          pointer.
 */
char *
bfindex_flow_to_index_path(const char *flow_file_path);

/** \brief Query the bfindex file for IP addresses contained in the bfindex IP
 *         address tree.
 *
 * Open and load the supplied bfindex file. It there is a problem during the
 * this phase, warning message is printed and true is returned immediately.
 *
 * If bfindex file was loaded successfully, the bfindex IP address tree is
 * evaluated:
 *   - operator nodes are evaluated recursively,
 *   - address nodes' IPs are looked up in the bfindex file.
 *
 * \param[in] bfindex_node     Pointer to node of the bfindex tree. Usually (but
 *                             not necessarily) the root node.
 * \param[in] index_file_path  Bfindex file path string.
 * \return True if at least one IP address is "possibly in set" (then flow file
 *         cannot be ommited from further processing), false if all IP addresses
 *         are "definitely not in set" (then flow file can be ommited from
 *         further processing, because filter would not match any flow record).
 */
bool
bfindex_contains(const struct bfindex_node *bfindex_root,
                 char *index_file_path);
