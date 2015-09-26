/**
 * \file flookup.h
 * \brief
 * \author Tomas Podermanski
 * \author Pavel Krobot, <Pavel.Krobot@cesnet.cz>
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

#ifndef FLOOKUP_H
#define FLOOKUP_H

#define F_ARRAY_RESIZE_AMOUNT 50

#include "common.h"

#include <stddef.h> //size_t

/* linked list of file names */
typedef struct f_item_s {
        char *f_name;
        off_t f_size;
} f_item;

typedef struct f_array_s {
        f_item *f_items;
        size_t f_cnt;
        size_t a_size;
} f_array_t;


/* file list operations */
void f_array_init(f_array_t *fa);
void f_array_free(f_array_t *fa);
error_code_t f_array_resize(f_array_t *fa);
error_code_t f_array_add(f_array_t *fa, char *f_name, off_t f_size);
//
//void flist_init(flist_t **fl);
//error_code_t flist_push(flist_t **fl, char *f_name);
//error_code_t flist_pop(flist_t **fl, char *f_buff);
//size_t flist_count(flist_t **fl);
error_code_t f_array_fill_from_time(f_array_t *fa, char *time_expr);
error_code_t f_array_fill_from_path(f_array_t *fa, char *path);

#endif
