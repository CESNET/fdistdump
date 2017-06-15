/** General purpose vector data type.
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

#include "vector.h"

#include <string.h>
#include <assert.h>

#ifdef _OPENMP
#include <omp.h>
#endif //_OPENMP


#define VECTOR_INIT_CAPACITY 2
#define VECTOR_DATA_END(vec) ((void *)((uint8_t *)(vec)->data + (vec)->size * \
                        (vec)->element_size))


/* Initialize vector structure. */
void vector_init(struct vector *v, size_t element_size)
{
        memset(v, 0, sizeof (*v));
        v->element_size = element_size;
}

/* Free all allocated memory and clear all settings. */
void vector_free(struct vector *v)
{
        free(v->data);
        memset(v, 0, sizeof (*v));
}

/* Reserve (allocate) capacity for desired number of elements. */
void vector_reserve(struct vector *v, size_t capacity)
{
        if (v->capacity >= capacity) {
                return; //nothing to do
        }

        v->data = realloc_wr(v->data, capacity, v->element_size, true);
        v->capacity = capacity;
}

/* Reduces memory usage by freeing unused memory (capacity to size). */
void vector_shrink_to_fit(struct vector *v)
{
        if (v->capacity == v->size) {
                return; //nothing to do
        }

        v->data = realloc_wr(v->data, v->size, v->element_size, true);
        v->capacity = v->size;
}

/* Resizes the vector to contain count elements. */
//static void vector_resize(struct vector *v, size_t count)
//{
//        v->size = count;
//}

/* Get pointer to the element on the element_index position. */
//static void * vector_get_ptr(const struct vector *v, size_t element_index)
//{
//        assert(element_index < v->size);
//        return (uint8_t *)v->data + v->element_size * element_index;
//}

/* Add a new element into the vector. */
bool vector_add(struct vector *v, const void *element)
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
        memcpy(VECTOR_DATA_END(v), element, v->element_size);
        v->size++;

        return true;
}

/* Add a new element into the vector (thread-safe version). */
bool vector_add_r(struct vector *v, const void *element)
{
        #pragma omp critical (vector)
        {
                assert(v->size <= v->capacity);

                /* Is there a free space in the array for another element? */
                if (v->size == v->capacity) { //no, ask for another space
                        const size_t alloc_size = (v->capacity == 0) ?
                                VECTOR_INIT_CAPACITY : v->capacity * 2;

                        v->data = realloc_wr(v->data, alloc_size,
                                        v->element_size, true);
                        v->capacity = alloc_size;
                }

                /* Append the elements to the vector. */
                memcpy(VECTOR_DATA_END(v), element, v->element_size);
                v->size++;
        }

        return true;
}

/* Concatenate two *different* vectors, append the src to the dest. */
void vector_concat(struct vector *dest, const struct vector *src)
{
        assert(dest != src);
        assert(dest->element_size == src->element_size);

        vector_reserve(dest, dest->size + src->size);
        memcpy(VECTOR_DATA_END(dest), src->data, src->size * src->element_size);
        dest->size += src->size;
}

/* Clear the vector but don't free allocated memory. */
void vector_clear(struct vector *v)
{
        v->size = 0;
}

/* Clear the vector but don't free allocated memory. */
//static void vector_iter_init(struct vector *v)
//{
//        v->it_begin = v->data;
//        v->it_end = VECTOR_DATA_END(v);
//}
