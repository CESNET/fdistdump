/**
 * \file file_index.c
 * \brief File file-indexing using Bloom filter indexes for IP addresses
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

#include "print.h"
#include "file_index.h"
#include "ip_tree.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <unistd.h>
#include <stdbool.h>
#include <bf_index.h>


/**
 * \brief Get a filename of an index file
 *
 * Get a filename of an index file for requested data file. The index filename
 * is acquired by replacing substring of the data filename starting with a slash
 * ("/") (or the beginning of filename if the slash is not present) and a dot
 * ("."). Such substring is replaced by F_INDEX_FN_PREFIX. If the dot character
 * is not present, function ends with an error.
 *
 * \note F_INDEX_FN_PREFIX has to be set up correctly to reflect a filename
 *   setup of given storage (i.e. setup from the time when files was stored).
 *
 * \param[in] path Data filename string
 * \return On success returns an index filename, otherwise returns NULL.
 */
static char *get_index_fn(const char *path)
{
        const char *dot = strrchr(path, '.');
        const char *slash = strrchr(path, '/');
        if (!dot){
        		PRINT_ERROR(E_IDX, 0, "unable to get file-indexing file "\
							"name for the data file (%s) - unexpected format "\
							"of a data file name (a dot character is missing)."
							, path);
        		return NULL;
        }
        if (!slash){
                slash = path;
        }

        size_t replace_len = dot - slash;

        size_t index_path_len = strlen(path)
        						- (replace_len - strlen(F_INDEX_FN_PREFIX)) + 1;

        char *index_fn = malloc(sizeof(char) * index_path_len);
        if (!index_fn){
                PRINT_ERROR(E_MEM, 0, "memory error while getting "\
							"file-indexing file name (data file %s).", path);
                return NULL;
        }

        size_t offset = slash - path;
        if (offset > 0) {
        	strncpy(index_fn, path, offset);
        	strcpy(index_fn + offset, "/");
        	offset++;
        }

        strcpy(index_fn + offset, F_INDEX_FN_PREFIX);
        offset += strlen(F_INDEX_FN_PREFIX);
        strcpy(index_fn + offset, dot);

        return index_fn;
}


/** \brief Auxiliary function for evaluation of file-indexing IP address tree
 * logical expression
 *
 * \param[in] index  Bloom filter index structure.
 * \param[in] ip_tree  Pointer to the current file-indexing IP address tree node.
 * \return Returns E_OK on success, error code otherwise.
 */
static bool ip_tree_contains_check(bfi_index_ptr_t index, ip_tree_node_t *ip_tree)
{
		switch (ip_tree->type)
		{
		case OPER_AND:
				return (ip_tree_contains_check(index, ip_tree->left)
						&& ip_tree_contains_check(index, ip_tree->right));
				break;

		case OPER_OR:
				return (ip_tree_contains_check(index, ip_tree->left)
						|| ip_tree_contains_check(index, ip_tree->right));
				break;

		case IP_ADDR_V4:
		case IP_ADDR_V6:
				{
				const size_t len = 16; // For IPv4 & IPv6
				if (sizeof(ip_tree->addr) != len){
						PRINT_ERROR(E_IDX, 0, "bad size of IP address "\
							"(expected %zu, got %zu).", len
							, sizeof(ip_tree->addr));
						return false;
				}
				return bfi_addr_is_stored(index,
							(const unsigned char *) ip_tree->addr.data , len);
				}
				break;

		default:
			PRINT_DEBUG("File-indexing: Unexpected type of a tree node "\
						"(type %d).", (int) ip_tree->type);
			return false;
		}
}


bool ips_in_file(const char *path, ip_tree_node_t *ip_tree)
{
		if (!ip_tree){
				PRINT_ERROR(E_IDX, 0, "passed an empty file-indexing tree to "\
							"the IP address presence check.");
				return false;
		}

		char *index_fn = get_index_fn(path);
		if (!index_fn){
				PRINT_DEBUG("File-indexing: filename was not created for "\
							"a data file %s.", path);
				return false;
		}

		bfi_index_ptr_t index_ptr;
		if (bfi_load_index(&index_ptr, index_fn) != BFI_E_OK){
				PRINT_ERROR(E_IDX, 0, "unable to load a file index from %s."
							, index_fn);
				free(index_fn);
				return false;
		}

		free(index_fn);

		return ip_tree_contains_check(index_ptr, ip_tree);
}
