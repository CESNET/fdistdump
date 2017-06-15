/** Declarations for the general purpose vector data type.
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

#pragma once


#include "common.h"


struct vector {
        void *data; //data storage
        size_t size; //number of data elements

        size_t capacity; //number of allocated data elements
        size_t element_size; //element size in bytes

        //void *it_begin; //iterator begin
        //void *it_end; //iterator end
};


/* Initialize vector structure. */
void vector_init(struct vector *v, size_t element_size);

/* Free all allocated memory and clear all settings. */
void vector_free(struct vector *v);

/* Reserve (allocate) capacity for desired number of elements. */
void vector_reserve(struct vector *v, size_t capacity);

/* Reduces memory usage by freeing unused memory (capacity to size). */
void vector_shrink_to_fit(struct vector *v);

/* Add a new element into the vector. */
bool vector_add(struct vector *v, const void *element);
/* Add a new element into the vector (thread-safe version). */
bool vector_add_ts(struct vector *v, const void *element);

/* Concatenate two *different* vectors, append the src to the dest. */
void vector_concat(struct vector *dest, const struct vector *src);

/* Clear the vector but don't free allocated memory. */
void vector_clear(struct vector *v);
