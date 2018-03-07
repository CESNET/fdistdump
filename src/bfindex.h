/** Declarations for file indexing using Bloom filter indexes for IP addresses.
 *
 * A Bloom filter is a space-efficient probabilistic data structure, conceived
 * by Burton Howard Bloom in 1970, that is used to test whether an element is a
 * member of a set. False positive matches are possible, but false negatives are
 * not -- in other words, a query returns either "possibly in set" or
 * "definitely not in set". [Wikipedia]
 *
 * In this case, the set is a set of source and destination IP addresses in all
 * records in the flow file and we want to know whether certain IP address is
 * contained in the file or not. Bloom filter is used only in conjunction with a
 * record filter containing one or more IP addresses.
*/

/*
 * Copyright (C) 2017 CESNET
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
 */

#include <stdbool.h>
#include <libnf.h>
#include <ffilter.h>


#define BFINDEX_FILE_NAME_PREFIX "bfi" /**< bfindex file prefix */


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
