/**
 * \file vector.h
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
 * \date 2016
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

#ifndef VECTOR_H
#define VECTOR_H


#include "common.h"

#include <string.h>
#include <assert.h>


#define VECTOR_INIT_CAPACITY 2


struct vector {
        void *data; //data storage
        size_t size; //number of data elements

        size_t capacity; //number of allocated data elements
        size_t element_size; //element size in bytes

        //void *it_begin; //iterator begin
        //void *it_end; //iterator end
};


/* Initialize vector structure. Allocation is lazy. */
static void vector_init(struct vector *v, size_t element_size)
{
        memset(v, 0, sizeof (*v));
        v->element_size = element_size;
}

/* Free all allocated memory and clear all settings. */
static void vector_free(struct vector *v)
{
        free(v->data);
        memset(v, 0, sizeof (*v));
}

/* Reserve (allocate) capacity for desired number of elements. */
static void vector_reserve(struct vector *v, size_t capacity)
{
        if (v->capacity >= capacity) {
                return; //nothing to do
        }

        v->data = realloc_wr(v->data, capacity, v->element_size, true);
        v->capacity = capacity;
}

/* Resizes the vector to contain count elements. */
static void vector_resize(struct vector *v, size_t count)
{
        v->size = count;
}

/* Get pointer to the element on the element_index position. */
static void * vector_get_ptr(const struct vector *v, size_t element_index)
{
        assert(element_index < v->size);
        return (uint8_t *)v->data + v->element_size * element_index;
}

/* Add a new element into the vector. */
static bool vector_add(struct vector *v, const void *element)
{
        assert(v->size <= v->capacity);

        /* Is there a free space in the array for another element? */
        if (v->size == v->capacity) { //no, ask for another space
                const size_t alloc_size = (v->capacity == 0) ?
                        VECTOR_INIT_CAPACITY : v->capacity * 2;

                v->data = realloc_wr(v->data, alloc_size, v->element_size,
                                true);
                v->capacity = alloc_size;
        }

        /* Append the elements to the vector. */
        memcpy((void *)((uint8_t *)v->data + v->size * v->element_size),
                        element, v->element_size);
        v->size++;

        return true;
}

/* Concatenate vectors, append the src to the dest. */
static bool vector_concat(struct vector *dest, const struct vector *src)
{
        size_t err = 0;

        assert(dest->element_size == src->element_size);
        for (size_t i = 0; i < src->size; ++i) {
                err += vector_add(dest, vector_get_ptr(src, i));
        }

        return (err == 0);
}

/* Clear the vector but don't free allocated memory. */
static void vector_clear(struct vector *v)
{
        v->size = 0;
}

/* Clear the vector but don't free allocated memory. */
//static void vector_iter_init(struct vector *v)
//{
//        v->it_begin = v->data;
//        v->it_end = (uint8_t *)v->data + v->size * v->element_size;
//}


#endif //MASTER_H
