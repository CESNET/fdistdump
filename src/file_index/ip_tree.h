/**
 * \file ip_tree.h
 * \brief Header file for definition, creation and destroying of
 * a file-indexing IP address tree.
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

#ifndef _FDD_IP_TREE_H
#define _FDD_IP_TREE_H

#include "common.h"

#include <libnf.h>
#include <ffilter.h>

/** \brief Maximal count of IP addresses used in a query filter.
 *
 * \note Using a bigger count of IP addresses may lead to the index inefficiency.
 */
#define MAX_IP_ADDRESES 20


/** \brief File-indexing IP address tree node types. */
typedef enum e_ip_node_type{
        OPER_AND = 1,
        OPER_OR,
        IP_ADDR_V4,
        IP_ADDR_V6,
        DONT_CARE
} ip_node_type_t;


/** \brief File-indexing IP address tree node structure.
 *
 * This structure contains information about single node in a file-indexing IP.
 */
typedef struct ip_tree_node_s{
        ip_node_type_t type; /**< Type of a node (operators / different version
                                    of addresses). */

        ff_ip_t addr;        /**< If node is one of the IP_ADDR_... type, an IP
                                  address itself is stored here. */

		/**< If node is one of the IP_ADDR_... type both pointers are set to
		     NULL. If node is one of the OPER_... type: */
        struct ip_tree_node_s *left; /**< Contains pointer to the left node
                                          (i.e. first operand). */
		struct ip_tree_node_s *right; /**< Contains pointer to the right node
		                                   (i.e. second operand). */
}ip_tree_node_t;


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
error_code_t get_ip_tree(ff_node_t *filter_root, ip_tree_node_t **idx_root);

/** \brief Destroy a file-indexing IP address tree.
 *
 * \note Function sets the root node to NULL.
 *
 * \param[in] ip_tree_node  Pointer to the root of a tree to destroy.
 */
void destroy_ip_tree(ip_tree_node_t **ip_tree_node);

#endif // _FDD_IP_TREE_H
