/** File indexing using Bloom filter indexes for IP addresses.
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
 *
 */


#include "print.h"
#include "bfindex.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>
#include <stdbool.h>

#include <bf_index.h>


#define MAX_IP_ADDRESES 20  /**< Maximum number of IP addrs. used in a filter
                              (using a more may lead to index inefficiency) */


static const ff_ip_t ipv6_mask_unused = { .data = { UINT32_MAX, UINT32_MAX,
                                                    UINT32_MAX, UINT32_MAX } };
/** \brief Internal IP address tree error codes. */
static enum {
    BFINDEX_E_OK,
    BFINDEX_E_MEM,
    BFINDEX_E_LIMIT,
    BFINDEX_E_NO_EQ,
    BFINDEX_E_MASK,
} global_ecode = BFINDEX_E_OK;


/** \brief File indexing IP address tree node types. */
typedef enum {
    NODE_TYPE_UNSET,
    NODE_TYPE_OPER_AND,
    NODE_TYPE_OPER_OR,
    NODE_TYPE_ADDR_V4,
    NODE_TYPE_ADDR_V6,
} node_type_t;

/** \brief File indexing IP address binary tree node.
 */
struct bfindex_node {
    node_type_t type;  /**< Type of this node (operator or address) */
    union {  // anonymous union (C11 feature)
        ff_ip_t addr;  /**< IP address storage (address node only) */
        struct {  // anonymous struct (C11 feature)
            struct bfindex_node *left;  /**< Left children */
            struct bfindex_node *right; /**< Right children */
        };  /**< Children nodes (operator node only) */
    };
};


// forward declaration
static struct bfindex_node *
build_node(const ff_node_t *ff_node);


/** \brief Return true if node is an address type.
 *
 * \param[in] type Node type
 * \return true if type is address, false otherwise
 */
static bool
node_type_is_addr(node_type_t type)
{
    return type == NODE_TYPE_ADDR_V4 || type == NODE_TYPE_ADDR_V6;
}

/** \brief Return true if node is an operator type.
 *
 * \param[in] type Node type
 * \return true if type is operator, false otherwise
 */
static bool
node_type_is_oper(node_type_t type)
{
    return type == NODE_TYPE_OPER_AND || type == NODE_TYPE_OPER_OR;
}

/** \brief Construct a bfindex address node from a filter address node.
 *
 * Allocate and initialize the address (IPv4 or IPv6) node. If filter node is
 * using anything but the equalily operator or a netmask, raise an error.
 *
 * \param[in] filter_node  Pointer to an address node of the filter tree.
 * \return  Pointer to the new bfindex address node on success, NULL otherwise.
 */
static struct bfindex_node *
build_addr_node(const ff_node_t *ff_node)
{
    assert(ff_node && ff_node->type == FF_TYPE_ADDR);
    PRINT_DEBUG("bfindex: build: build_addr_node");

    static size_t ip_cnt; /**< Count of IP addresses in the IP address tree */

    if (ff_node->oper != FF_OP_EQ) {
        PRINT_DEBUG("bfindex: build: other operator than EQ is used");
        global_ecode = BFINDEX_E_NO_EQ;
        return NULL;
    }
    if (++ip_cnt > MAX_IP_ADDRESES) {
        PRINT_DEBUG("bfindex: build: too many IP addresses");
        global_ecode = BFINDEX_E_LIMIT;
        return NULL;
    }

    const ff_net_t *const ff_net = (const ff_net_t *)ff_node->value;
    node_type_t node_type;
    bool using_mask;
    switch (ff_net->ver) {
    case 4:
        node_type = NODE_TYPE_ADDR_V4;
        using_mask = (ff_net->mask.data[3] != UINT32_MAX);
        break;
    case 6:
        node_type = NODE_TYPE_ADDR_V6;
        using_mask = (memcmp(ff_net->mask.data, ipv6_mask_unused.data,
                             sizeof (ipv6_mask_unused.data)) != 0);
        break;
    default:
        assert(!"unknown ff_net->ver");
        break;
    }

    if (using_mask) {
        PRINT_DEBUG("bfindex: build: network mask is used");
        global_ecode = BFINDEX_E_MASK;
        return NULL;
    }

    struct bfindex_node *const node = malloc(sizeof (*node));
    if (!node) {
        PRINT_ERROR(E_MEM, 0, "build: node allocation");
        global_ecode = BFINDEX_E_MEM;
        return NULL;
    }
    node->type = node_type;
    memcpy(&node->addr, &ff_net->ip, sizeof (ff_net->ip));

    return node;
}

/** \brief Prune (reduce) bfindex IP address (sub)tree if possible.
 *
 * If operator node has no children, remove the operator node.
 * If operator node has only one children, use the operand node directly.
 * If operator node has both children and they are exactly the same address
 * nodes, use one of the address nodes directly. (For example expression "ip
 * a.b.c.d" is internally represented by expression "srcip a.b.c.d or dstip
 * a.b.c.d". For the puropose of bfindexing, only one address is needed.)
 *
 * \param[in] node  Pointer to an operator node of the bfindex tree.
 * \return  Pointer to the node which should be used instead of the node
 *          supplied in the argument.
 */
static struct bfindex_node *
prune_oper_subtree(struct bfindex_node *node)
{
    assert(node && node_type_is_oper(node->type));

    if (!node->left && !node->right) {  // both child nodes are empty
            PRINT_DEBUG("bfindex: reduce: removing operator node without child nodes");
            return NULL;
    } else if (!node->left) {   // the left is empty, the right is not
            PRINT_DEBUG("bfindex: reduce: using right child node directly");
            return node->right;
    } else if (!node->right) {  // the rigth is empty, the left is not
            PRINT_DEBUG("bfindex: reduce: using left child node directly");
        return node->left;
    } else if (node_type_is_addr(node->left->type)
            && node_type_is_addr(node->right->type)
            && node->left->type == node->right->type
            && (memcmp(&node->left->addr, &node->right->addr,
                sizeof (node->left->addr)) == 0)) {
        // both child nodes are not empty and contains the same address
        PRINT_DEBUG("bfindex: reduce: using left child node directly because left and right child nodes are the same");
        free(node->right);
        return node->left;
    }

    return node;
}

/** \brief Construct a bfindex operator node from a filter operator node.
 *
 * Allocate and initialize the operator (AND/OR) node itself and its left and
 * right children recursively.
 *
 * \param[in] filter_node  Pointer to an operator node of the filter tree.
 * \return  Pointer to the new bfindex operator node on success, NULL otherwise.
 */
static struct bfindex_node *
build_oper_node(const ff_node_t *ff_node)
{
    assert(ff_node && ff_node->type == FF_TYPE_UNSUPPORTED);
    PRINT_DEBUG("bfindex: build: build_oper_node");

    node_type_t node_type;
    switch (ff_node->oper) {
    case FF_OP_AND:
        node_type = NODE_TYPE_OPER_AND;
        break;
    case FF_OP_OR:
        node_type = NODE_TYPE_OPER_OR;
        break;
    case FF_OP_UNDEF:
    case FF_OP_NOT:
    case FF_OP_IN:
    case FF_OP_YES:
    case FF_OP_NOOP:
    case FF_OP_EQ:
    case FF_OP_LT:
    case FF_OP_GT:
    case FF_OP_ISSET:
    case FF_OP_ISNSET:
    case FF_OP_EXIST:
    case FF_OP_TERM_:
        PRINT_DEBUG("bfindex: build: skipping other node");
        global_ecode = BFINDEX_E_OK;
        return NULL;
    default:
        assert(!"unknown ff_node_t.oper");
    }

    struct bfindex_node *const node = malloc(sizeof (*node));
    if (!node) {
        PRINT_ERROR(E_MEM, 0, "build: node allocation");
        global_ecode = BFINDEX_E_MEM;
        return NULL;
    }
    node->type = node_type;
    node->left = build_node(ff_node->left);
    node->right = build_node(ff_node->right);

    struct bfindex_node *const reduced_node = prune_oper_subtree(node);
    if (reduced_node != node) {
        free(node);
        return reduced_node;
    } else {
        return node;
    }
}

/** \brief Construct a bfindex tree node from a filter tree node.
 *
 * Indexing IP address node can be either operator node (logical AND or OR) and
 * IP address nodes (a storage for IPv4 or IPv6). Filter may contain other types
 * of nodes, but those are ignored.
 *
 * \param[in] filter_node  Pointer to a node of the filter tree.
 * \return  Pointer to the new node of the bfindex tree on success, NULL
 *          otherwise.
 */
static struct bfindex_node *
build_node(const ff_node_t *ff_node)
{
    assert(ff_node);
    PRINT_DEBUG("bfindex: build: build_node, type = %d", ff_node->type);

    switch (ff_node->type) {
    case FF_TYPE_ADDR:         // address node
        return build_addr_node(ff_node);
    case FF_TYPE_UNSUPPORTED:  // operator node (probably)
        return build_oper_node(ff_node);
    case FF_TYPE_UNSIGNED:
    case FF_TYPE_UNSIGNED_BIG:
    case FF_TYPE_SIGNED:
    case FF_TYPE_SIGNED_BIG:
    case FF_TYPE_UINT8:
    case FF_TYPE_UINT16:
    case FF_TYPE_UINT32:
    case FF_TYPE_UINT64:
    case FF_TYPE_INT8:
    case FF_TYPE_INT16:
    case FF_TYPE_INT32:
    case FF_TYPE_INT64:
    case FF_TYPE_DOUBLE:
    case FF_TYPE_MAC:
    case FF_TYPE_STRING:
    case FF_TYPE_MPLS:
    case FF_TYPE_TIMESTAMP:
    case FF_TYPE_TIMESTAMP_BIG:
    case FF_TYPE_TERM_:
        PRINT_DEBUG("bfindex: build: skipping unknown node");
        global_ecode = BFINDEX_E_OK;
        return NULL;
    default:                   // filter node of some another type
        assert(!"unknown ff_node_t.type");
    }
}

/** \brief Recursive logical evaluation of bfindex IP address tree.
 *
 * \param[in] index_ptr     Bloom filter index library internal structure.
 * \param[in] bfindex_node  Pointer to node of the bfindex tree. Usually (but
 *                          not necessarily) the root node.
 * \return Logical AND/OR of left and right children evaluation if node is
 *         AND/OR operator type, true/false if node is address type and its
 *         IP address is "possibly in set"/"definitely not in set".
 */
static bool
bfindex_tree_contains(const bfi_index_ptr_t index_ptr,
                      const struct bfindex_node *node)
{
    switch (node->type) {
    case NODE_TYPE_OPER_AND:
        return bfindex_tree_contains(index_ptr, node->left)
               && bfindex_tree_contains(index_ptr, node->right);
        case NODE_TYPE_OPER_OR:
            return bfindex_tree_contains(index_ptr, node->left)
                   || bfindex_tree_contains(index_ptr, node->right);
        case NODE_TYPE_ADDR_V4:
            return bfi_addr_is_stored(index_ptr,
                                      (const unsigned char *)node->addr.data,
                                      sizeof (node->addr));
        case NODE_TYPE_ADDR_V6:
            return bfi_addr_is_stored(index_ptr,
                                      (const unsigned char *)node->addr.data,
                                      sizeof (node->addr));

        case NODE_TYPE_UNSET:
            assert(!"illegal node type");
        default:
            assert(!"unknown node type");
    }
}

# if 0
#include <arpa/inet.h>
static void
filter_dump(const ff_node_t *ff_node)
{
    if (ff_node) {
        printf("0x%lx: type = %d, size = %zu\tL: 0x%lx\tR: 0x%lx\n",
                (long unsigned)ff_node, ff_node->type, ff_node->vsize,
                (long unsigned)ff_node->left, (long unsigned)ff_node->right);

        if (ff_node->type == FF_TYPE_ADDR) {
            assert(ff_node->vsize == sizeof (ff_net_t));
            const ff_net_t *const ff_net = (const ff_net_t *)ff_node->value;
            char buff[INET6_ADDRSTRLEN];
            switch (ff_net->ver) {
                case 4:
                    assert(inet_ntop(AF_INET, ff_net->ip.data + 3, buff,
                                INET6_ADDRSTRLEN));
                    break;
                case 6:
                    assert(inet_ntop(AF_INET6, &ff_net->ip, buff,
                                INET6_ADDRSTRLEN));
                    break;
                default:
                    assert(!"unknown ff_net_t.ver (IP version)");
            }
            printf("\tIPv%d: %s\n", ff_net->ver, buff);
        }

        filter_dump(ff_node->left);
        filter_dump(ff_node->right);
    }
}
#endif


struct bfindex_node *
bfindex_init(const ff_node_t *filter_root)
{
    assert(filter_root);

    struct bfindex_node *const bfindex_root = build_node(filter_root);
    if (!bfindex_root) {
        PRINT_INFO("bfindex: init: file indexes cannot be used due to unsuitable filter");
        return NULL;
    } else if (global_ecode != BFINDEX_E_OK) {
        PRINT_WARNING(E_BFINDEX, 0, "init: file indexes will not be used due to error during operator/address tree initialization");
        bfindex_free(bfindex_root);
        return NULL;
    } else {
        return bfindex_root;
    }
}

void
bfindex_free(struct bfindex_node *bfindex_node)
{
    PRINT_DEBUG("bfindex: free: recursive node deallocation");

    if (bfindex_node) {
        if (node_type_is_oper(bfindex_node->type)) {
            bfindex_free(bfindex_node->left);
            bfindex_free(bfindex_node->right);
        }
        free(bfindex_node);
    }
}

char *
bfindex_flow_to_index_path(const char *flow_file_path)
{
    assert(flow_file_path);
    const size_t flow_file_path_len = strlen(flow_file_path);
    assert(flow_file_path[flow_file_path_len - 1] != '/');  // not a directory

    const char *dir_name;
    size_t dir_name_len;
    const char *file_name = strrchr(flow_file_path, '/');  // find the last occ.
    size_t file_name_len;
    if (file_name) {  // slash found
        dir_name = flow_file_path;
        file_name = file_name + 1;  // skip the slash
        file_name_len = strlen(file_name);
        dir_name_len = flow_file_path_len - file_name_len;
    } else {          // slash not found
        dir_name = "\0";
        dir_name_len = 0;
        file_name = flow_file_path;  // the whole path is a file name
        file_name_len = flow_file_path_len;
    }

    if (strncmp(FLOW_FILE_NAME_PREFIX ".", file_name,
                STRLEN_STATIC(FLOW_FILE_NAME_PREFIX ".")) == 0) {
        // flow file prefix found, cut it from file name
        file_name += STRLEN_STATIC(FLOW_FILE_NAME_PREFIX ".");
        file_name_len = strlen(file_name);
    }
    // if flow file prefix not found, prepend file name with bfindex prefix

    // TODO: use static buffer instead?
    char *const index_path = malloc(
            dir_name_len  // directory name with terminating slash
            + STRLEN_STATIC(BFINDEX_FILE_NAME_PREFIX)  // bfindex prefix
            + 1  // dot separator
            + file_name_len  // (rest of the) original file name
            + 1);  // terminating null-byte
    if (!index_path) {
        PRINT_ERROR(E_MEM, 0, "path string allocation");
        return NULL;
    }
    strncpy(index_path, dir_name, dir_name_len);
    index_path[dir_name_len] = '\0';
    strcat(index_path, BFINDEX_FILE_NAME_PREFIX ".");
    strcat(index_path, file_name);

    return index_path;
}

bool
bfindex_contains(const struct bfindex_node *bfindex_node,
                 char *index_file_path)
{
    assert(bfindex_node && index_file_path);

    bfi_index_ptr_t index_ptr;
    bfi_ecode_t bfi_ecode = bfi_load_index(&index_ptr, index_file_path);
    if (bfi_ecode != BFI_E_OK) {
        PRINT_WARNING(E_BFINDEX, 0, "contains: unable to load file `%s': %s",
                      index_file_path, bfi_get_error_msg(bfi_ecode));
        return true;
    }

    return bfindex_tree_contains(index_ptr, bfindex_node);
}
