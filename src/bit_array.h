/**
 * \file bit_array.h
 * \brief Header-only implementation of bit array ADT.
 * \author Jan Wrona, <wrona@cesnet.cz>
 * \date 2015
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

#ifndef BIT_ARRAY_H
#define BIT_ARRAY_H

#include "config.h" //HAVE_FFSL

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define BIT_SET(var, pos) ((var) |= (1ull << (pos)))
#define BIT_CLEAR(var, pos) ((var) &= ~(1ull << (pos)))
#define BIT_TOGGLE(var, pos) ((var) ^= (1ull << (pos)))
#define BIT_TEST(var, pos) ((var) & (1ull << (pos)))
#define BIT_IS_SET(var, pos) (!!BIT_TEST(var, pos))
#define BIT_IS_CLEAR(var, pos) (!(BIT_TEST(var, pos)))


struct bit_array {
        unsigned long *data;
        size_t data_bits; //number of bits demanded by user
        size_t data_elems; //number of data elements

        size_t set_bits_cnt; //number of bits set

#ifdef HAVE_FFSL
        unsigned long *iter_data_copy; //data copy for iteration
        unsigned long *iter_data_ptr; //pointer to data copy
#endif
        size_t iter_cursor; //iterator cursor
};

static inline struct bit_array * bit_array_init(size_t bits)
{
        struct bit_array *ba;
        const size_t bits_in_data = 8 * MEMBER_SIZE(struct bit_array, data[0]);

        ba = calloc(1, sizeof (struct bit_array));
        if (ba == NULL) {
                return NULL;
        }

        ba->data_bits = bits;
        ba->data_elems = INT_DIV_CEIL(bits, bits_in_data);

        ba->data = calloc(ba->data_elems,
                        MEMBER_SIZE(struct bit_array, data[0]));
        if (ba->data == NULL) {
                free(ba);
                return NULL;
        }

#ifdef HAVE_FFSL
        ba->iter_data_copy = calloc(ba->data_elems,
                        MEMBER_SIZE(struct bit_array, iter_data_copy[0]));
        if (ba->iter_data_copy == NULL) {
                free(ba->data);
                free(ba);
                return NULL;
        }
#endif

        return ba;
}

static inline void bit_array_free(struct bit_array *ba)
{
        assert(ba != NULL);

#ifdef HAVE_FFSL
        free(ba->iter_data_copy);
#endif
        free(ba->data);
        free(ba);
}

static inline int bit_array_test(struct bit_array *ba, size_t position)
{
        assert(ba != NULL);

        const size_t data_idx =
                position / (8 * MEMBER_SIZE(struct bit_array, data[0]));
        const size_t bit_idx =
                position % (8 * MEMBER_SIZE(struct bit_array, data[0]));

        return BIT_IS_SET(ba->data[data_idx], bit_idx);
}

static inline void bit_array_set(struct bit_array *ba, size_t position)
{
        size_t data_idx;
        size_t bit_idx;

        assert(ba != NULL);

        if (position >= ba->data_bits) {
                return;
        }

        data_idx = position / (8 * MEMBER_SIZE(struct bit_array, data[0]));
        bit_idx = position % (8 * MEMBER_SIZE(struct bit_array, data[0]));

        ba->set_bits_cnt += BIT_IS_CLEAR(ba->data[data_idx], bit_idx);
        BIT_SET(ba->data[data_idx], bit_idx);
}

static inline void bit_array_clear(struct bit_array *ba, size_t position)
{
        size_t data_idx;
        size_t bit_idx;

        assert(ba != NULL);

        if (position >= ba->data_bits) {
                return;
        }

        data_idx = position / (8 * MEMBER_SIZE(struct bit_array, data[0]));
        bit_idx = position % (8 * MEMBER_SIZE(struct bit_array, data[0]));

        ba->set_bits_cnt -= BIT_IS_SET(ba->data[data_idx], bit_idx);
        BIT_CLEAR(ba->data[data_idx], bit_idx);
}

static inline void bit_array_toggle(struct bit_array *ba, size_t position)
{
        size_t data_idx;
        size_t bit_idx;

        assert(ba != NULL);

        if (position >= ba->data_bits) {
                return;
        }

        data_idx = position / (8 * MEMBER_SIZE(struct bit_array, data[0]));
        bit_idx = position % (8 * MEMBER_SIZE(struct bit_array, data[0]));

        if (BIT_TEST(ba->data[data_idx], bit_idx)) {
                ba->set_bits_cnt--;
        } else {
                ba->set_bits_cnt++;
        }

        BIT_TOGGLE(ba->data[data_idx], bit_idx);
}

static inline void bit_array_iter_init(struct bit_array *ba)
{
        assert(ba != NULL);

#ifdef HAVE_FFSL
        memcpy(ba->iter_data_copy, ba->data, ba->data_elems *
                        MEMBER_SIZE(struct bit_array, data[0]));
        ba->iter_data_ptr = ba->iter_data_copy;
#endif
        ba->iter_cursor = 0;
}

static inline int bit_array_iter_next(struct bit_array *ba)
{
        assert(ba != NULL);

#ifdef HAVE_FFSL
        for ( ; ba->iter_cursor < ba->data_elems; ++ba->iter_cursor) {
                /* XXX: Builtin is fast, but GCC specific. */
                const int pos = __builtin_ffsl(*(ba->iter_data_ptr));
                /* ffsl() is _GNU_SOURCE. */
                //const int pos = ffsl(*(ba->iter_data_ptr));

                if (pos == 0) {
                        ba->iter_data_ptr++;
                        continue;
                }

                BIT_CLEAR(*(ba->iter_data_ptr), pos - 1);
                return (ba->iter_cursor * 8 * MEMBER_SIZE(struct bit_array,
                                        data[0]) + (pos - 1));
        }
#else
        for ( ; ba->iter_cursor < ba->data_bits; ++ba->iter_cursor) {
                size_t data_idx = ba->iter_cursor /
                        (8 * MEMBER_SIZE(struct bit_array, data[0]));
                size_t bit_idx = ba->iter_cursor %
                        (8 * MEMBER_SIZE(struct bit_array, data[0]));

                if (BIT_TEST(ba->data[data_idx], bit_idx)) {
                        return ba->iter_cursor++;
                }
        }
#endif
        return -1;
}

static inline size_t bit_array_get_set_bits_cnt(const struct bit_array *ba)
{
        assert(ba != NULL);

        return ba->set_bits_cnt;
}

#endif //BIT_ARRAY_H
