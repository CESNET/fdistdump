/**
 * \file ip_tree.c
 * \brief Creation and destroying of an file-indexing IP address tree.
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

#include "common.h"
#include "print.h"
#include "ip_tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include <ffilter.h>
#include <libnf.h>
#include <arpa/inet.h>

//#define DEBUG

/** \brief Internal file-indexing IP address tree error codes. */
typedef enum{
        IPT_OK = 0,
        IPT_ERR,
        IPT_LIMIT
} ipt_ecode_t;


/* Count of IP address in a file-indexing IP address tree. */
unsigned int ip_cnt = 0;


static ipt_ecode_t build_ip_tree(ip_tree_node_t **, ff_node_t *);


/** \brief Auxiliary function for getting of file-indexing IP address tree.
 *
 * Function fills an IP address from filter node into a file-indexing IP
 * address tree node.
 *
 * \note If an "in" operator or subnet is used, IP address limit is
 *       automatically and file-indexing won't be used in current query.
 *
 * \param[in] ip_node  Pointer to the actual address node of the IP address
 *                     tree.
 * \param[in] filter_node  Pointer to the actual address node of the filter
 *                         tree.
 * \return Returns E_OK on success, error code otherwise.
 */
static ipt_ecode_t process_ip_node(ip_tree_node_t **ip_node,
                                     ff_node_t *filter_node)
{
        /// TODO FF_VER
//                ff_ver_t ip_ver = ((ff_net_t *)filter_node->value)->ver;
        int ip_ver = ((ff_net_t *)filter_node->value)->ver;
        ff_ip_t *ip_addr = (ff_ip_t *)&((ff_net_t *)filter_node->value)->ip;
        ff_ip_t *ip_mask = (ff_ip_t *)&((ff_net_t *)filter_node->value)->mask;

        if (filter_node->oper == FF_OP_EQ) {
                /// TODO FF_VER
                if (ip_ver == 4){
                        (*ip_node)->type = IP_ADDR_V4;

                        if (ip_mask->data[3] != 0xffffffff
                            || (ip_cnt + 1) > MAX_IP_ADDRESES){
                                PRINT_DEBUG("IP address limit reached while "\
                                            "getting a file-indexing IP "\
                                            "address tree (reason: too many "\
                                            "addresses or network is used - IPv4).");
                                return IPT_LIMIT;
                        }
                /// TODO FF_VER
                } else if (ip_ver == 6){
                        (*ip_node)->type = IP_ADDR_V6;

                        if (ip_mask->data[0] != 0xffffffff
                            || ip_mask->data[1] != 0xffffffff
                            || ip_mask->data[2] != 0xffffffff
                            || ip_mask->data[3] != 0xffffffff
                            || (ip_cnt + 1) > MAX_IP_ADDRESES)
                        {
                                PRINT_DEBUG("IP address limit reached while "\
                                            "getting a file-indexing IP "\
                                            "address tree (reason: too many "\
                                            "addresses or network is used - IPv6).");
                                return IPT_LIMIT;
                        }
                }

                memcpy(&((*ip_node)->addr), ip_addr, sizeof(ff_ip_t));
                ip_cnt++;
                (*ip_node)->left = NULL;
                (*ip_node)->right = NULL;
        } else {
                PRINT_DEBUG("IP address limit reached while getting an "\
                            "file-indexing IP address tree (reason: other "\
                            "operator than EQ is used).");
                return IPT_LIMIT;
        }

        return IPT_OK;
}

/** \brief Auxiliary function for getting of file-indexing IP address tree.
 *
 * Function gets subtree of an operator (AND/OR) node. Creates left and right
 * child nodes and calls function to build a subtree for both.
 *
 * \note If a filter tree node is other than AND/OR/IP it is ignored and
 *       processing of a subtree of this node is stopped.
 *
 * \param[in] ip_node  Pointer to the actual operator node of the IP address
 *                     tree.
 * \param[in] filter_node  Pointer to the actual operator node of the filter
 *                         tree.
 * \return Returns E_OK on success, error code otherwise.
 */
static ipt_ecode_t get_operator_subtree(ip_tree_node_t **ip_node,
                                      ff_node_t *filter_node)
{
        ipt_ecode_t ret;

        // Allocate nodes
        (*ip_node)->left = calloc(1, sizeof(ip_tree_node_t));
        (*ip_node)->right = calloc(1, sizeof(ip_tree_node_t));
        if (!(*ip_node)->left || !(*ip_node)->right){
                PRINT_ERROR(E_MEM, 0, "memory error while getting "\
                            "file-indexing IP address tree (child node "\
                            "allocation).");
                return IPT_ERR;
        }

        // Build a left child node subtree
        PRINT_DEBUG("File-indexing IP tree: Processing left child node.");
        ret = build_ip_tree(&(*ip_node)->left, filter_node->left);
        if (ret != IPT_OK){
                return ret;
        }

        PRINT_DEBUG("File-indexing IP tree: Processing right child node.");
        // Build a right child node subtree
        ret = build_ip_tree(&(*ip_node)->right, filter_node->right);
        if (ret != IPT_OK){
                return ret;
        }

        /* Reduce IP address tree nodes if possible.
         * - If an operator node has no relevant operand nodes, remove
         *   the operator node
         * - If there is only one operand node of an operator node (AND/OR)
         *   remove the operator node and use the operand node directly.
         * - If both operand nodes of an operator node contains exactly the same
         *   address use one of the operand node directly (and remove the other
         *   operand node and the operator node). e.g. in the case of
         *   the "ip a.b.c.d" predicate, which generates "srcip a.b.c.d OR dstip
         *   a.b.c.d" filter subtree. */
        if (!(*ip_node)->left) {
                if (!(*ip_node)->right) {
                        /* Both child nodes are NULL -> we don't need the parent
                         * node either */
                        PRINT_DEBUG("File-indexing IP tree: Reducing whole "\
                                    "node - no relevant child nodes.");
                        free(*ip_node);
                        *ip_node = NULL;
                } else {
                        /* Left child node is NULL, a Right child node carries
                         * some data -> make the right node the parent node. */
                        PRINT_DEBUG("File-indexing IP tree: Reducing operator "\
                                    "node - using right child node directly.");
                        ip_tree_node_t *tmp_node_ptr = (*ip_node)->right;
                        free(*ip_node);
                        *ip_node = tmp_node_ptr;
                }
        } else if (!(*ip_node)->right) {
                /* Right child node is NULL, left child node carries
                 * some data -> make left node the parent node. */
                PRINT_DEBUG("File-indexing IP tree: Reducing operator node "\
                                    "- using left child node directly.");
                ip_tree_node_t *tmp_node_ptr = (*ip_node)->left;
                free(*ip_node);
                *ip_node = tmp_node_ptr;
        } else {
                ip_node_type_t left_type = (*ip_node)->left->type;
                ip_node_type_t right_type = (*ip_node)->right->type;
                if ((left_type == IP_ADDR_V4 || left_type == IP_ADDR_V6)
                    && (right_type == IP_ADDR_V4 || right_type == IP_ADDR_V6)
                    && (memcmp((const void *)&((*ip_node)->left->addr),
                               (const void *)&((*ip_node)->right->addr),
                               sizeof((*ip_node)->left->addr)) == 0))
                {
                /* Both child nodes are address nodes, check if IP addresses
                 * matches (reduce nodes if yes) - term "ip a.b.c.d" is
                 * considered as "srcip a.b.c.d or dstip a.b.c.d" by ffilter.
                 * For the file-indexing only one address is needed. */
                        PRINT_DEBUG("File-indexing IP tree: Reducing operator "\
                                    "node - using left child node directly "\
                                    "(same child nodes).");
                        ip_tree_node_t *tmp_node_ptr = (*ip_node)->left;
                        free((*ip_node)->right);
                        free(*ip_node);
                        *ip_node = tmp_node_ptr;
                }
        }

        return IPT_OK;
}

/** \brief Auxiliary function for getting of file-indexing IP address tree.
 *
 * Function inspects actual node type and calls function for operator subtree
 * creation or for IP address node creation.
 *
 * \note If a filter tree node is other than AND/OR/IP it is ignored and
 *       processing of a subtree of this node is stopped.
 *
 * \param[in] ip_node  Pointer to the actual node of the IP address tree.
 * \param[in] filter_node  Pointer to the actual node of the filter tree.
 * \return Returns E_OK on success, error code otherwise.
 */
static ipt_ecode_t build_ip_tree(ip_tree_node_t **ip_node,
                                     ff_node_t *filter_node)
{
        if(!filter_node || !(*ip_node)){
                return IPT_ERR;
        }

        if (filter_node->type == FF_TYPE_UNSUPPORTED) {
                /* Probably an operator node ... */
                if (filter_node->oper == FF_OP_AND){
                        PRINT_DEBUG("File-indexing IP tree: Processing AND node.");
                        (*ip_node)->type = OPER_AND;
                        return get_operator_subtree(ip_node, filter_node);
                } else if (filter_node->oper == FF_OP_OR){
                        PRINT_DEBUG("File-indexing IP tree: Processing OR node.");
                        (*ip_node)->type = OPER_OR;
                        return get_operator_subtree(ip_node, filter_node);
                } else {
                        PRINT_DEBUG("File-indexing IP tree: Skipping other node.");
                        free(*ip_node);
                        *ip_node = NULL;
                }
        } else if (filter_node->type == FF_TYPE_ADDR){
                /* IP address node */
                PRINT_DEBUG("File-indexing IP tree: Processing IP address node.");
                return process_ip_node(ip_node, filter_node);
        } else {
                PRINT_DEBUG("File-indexing IP tree: Skipping other node.");
                /* Node of an another type */
                free(*ip_node);
                *ip_node = NULL;
        }

        return IPT_OK;
}




error_code_t get_ip_tree(ff_node_t *filter_root, ip_tree_node_t **idx_root)
{
        ipt_ecode_t ret;

        *idx_root = calloc (1, sizeof(ip_tree_node_t));
        if (!(*idx_root)){
                PRINT_ERROR(E_MEM, 0, "memory error while getting a "\
							"file-indexing IP address tree (root allocation).");
                return E_IDX;
        }

        ret = build_ip_tree(idx_root, filter_root);
        if (ret == IPT_LIMIT){
                 /// TODO DESTORY INDEX??
                 PRINT_WARNING(E_IDX, 0, "the file-indexing limit for "\
                                "a maximal count of IP addresses in a filter "\
                                "has been reached. File indexes wont't be "\
                                "used in current query.");
                PRINT_DEBUG("Destroying file-indexing IP address tree.");
                return E_OK;
        }else if (ret == IPT_ERR){
                /// TODO DESTORY INDEX??
                PRINT_ERROR(E_IDX, 0, "unable to build file-indexing IP "\
                            "address tree. File indexes wont't be used in "\
                            "current query.");
                PRINT_DEBUG("Destroying file-indexing IP address tree.");
                return E_IDX;
        }

        return E_OK;
}


void destroy_ip_tree(ip_tree_node_t **ip_tree_node)
{
        if (!(*ip_tree_node)) {
                return;
        }

        destroy_ip_tree(&((*ip_tree_node)->left));

        destroy_ip_tree(&((*ip_tree_node)->right));

        free(*ip_tree_node);
        *ip_tree_node = NULL;
}




//int main (int argc, char **argv)
//{
//        char buf[LNF_MAX_STRING];
//        lnf_filter_t *filterp;
//
//        if (argc != 2){
//                fprintf(stderr, "Error: Missing filter argument.\n");
//                return 1;
//        }
//
//        char *filter = argv[1];
//
//        if (lnf_filter_init_v2(&filterp, filter) != LNF_OK){
//                fprintf(stderr, "Error: Can not initialize new (libnf) filter '%s'\n", filter);
//                lnf_error(buf, LNF_MAX_STRING);
//                fprintf(stderr, "Error detail: %s\n", buf);
//                return 1;
//        }
//
//        #ifdef DEBUG
//        printf("Getting libnf filter pointer...\n");
//        fflush(stdout);
//        #endif // DEBUG
//        ff_t *filter_internal = (ff_t *) lnf_filter_ffilter_ptr(filterp);
//
//        #ifdef DEBUG
//        printf("Getting IP tree...\n");
//        fflush(stdout);
//        print_ff_tree((ff_node_t *) filter_internal->root);
////        return 0;
//        #endif // DEBUG
//        ip_tree_node_t *ip_tree_root;
//        if (get_ip_tree((ff_node_t *) filter_internal->root, &ip_tree_root)!= E_OK){
//                fprintf(stderr, "Error: Unable to create IP tree\n");
//                lnf_filter_free(filterp);
//                return 1;
//        }
//
//        lnf_filter_free(filterp);
//
//        #ifdef DEBUG
//        printf("Printing IP tree...\n");
//        fflush(stdout);
//        #endif // DEBUG
//        if (ip_tree_root){
//                print_tree(ip_tree_root, 0);
//                print_tree(ip_tree_root, 1);
//        }
//
//        #ifdef DEBUG
//        printf("Destroying IP tree...\n");
//        fflush(stdout);
//        #endif // DEBUG
//        if (ip_tree_root){
//                destroy_ip_tree(&ip_tree_root);
//        }
//
//        return 0;
//}
