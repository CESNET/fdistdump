/**
 * \file flookup.c
 * \brief
 * \author Pavel Krobot, <Pavel.Krobot@cesnet.cz>
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

#include "common.h"
#include "flookup.h"

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h> //size_t
#include <limits.h> //PATH_MAX
#include <assert.h>

#include <dirent.h>
#include <sys/stat.h>


#define F_ARRAY_INIT_SIZE 50


extern int secondary_errno;


/* initialize filenames-array */
void f_array_init(f_array_t *fa)
{
        fa->f_items = NULL;
        fa->f_cnt = 0;
        fa->a_size = 0;
}

/* free all allocated filenames-array memory*/
void f_array_free(f_array_t *fa)
{
        for (size_t i = 0; i < fa->f_cnt; ++i) {
                free(fa->f_items[i].f_name);
        }

        free(fa->f_items);

        fa->f_cnt = 0;
        fa->a_size = 0;
}

/* resize filenames-array */
error_code_t f_array_resize(f_array_t *fa)
{
        f_item_t *new_array;
        const size_t new_size = (fa->a_size == 0) ?
                F_ARRAY_INIT_SIZE : fa->a_size * 2;

        new_array = realloc(fa->f_items, new_size * sizeof (f_item_t));
        if (new_array == NULL) {
                print_err(E_MEM, 0, "realloc()");
                return E_MEM;
        }

        fa->f_items = new_array;
        fa->a_size = new_size;

        return E_OK;
}

/* add filename-item into filename-array */
error_code_t f_array_add(f_array_t *fa, const char *f_name, off_t f_size)
{
        if (fa->f_cnt == fa->a_size) {
                error_code_t ret = f_array_resize(fa);
                if (ret != E_OK) {
                        return ret;
                }
        }

        fa->f_items[fa->f_cnt].f_name = strdup(f_name);
        if (fa->f_items[fa->f_cnt].f_name == NULL) {
                print_err(E_MEM, 0, "strdup()");
                return E_MEM;
        }

        fa->f_items[fa->f_cnt].f_size = f_size;
        fa->f_cnt++;

        return E_OK;
}


/* return number of files in file array */
size_t f_array_get_count(const f_array_t *fa)
{
        return fa->f_cnt;
}

/* return sum of sizes of all files in file array */
off_t f_array_get_size_sum(const f_array_t *fa)
{
        off_t size_sum = 0;

        for (size_t i = 0; i < fa->f_cnt; ++i) {
                size_sum += fa->f_items[i].f_size;
        }

        return size_sum;
}


static error_code_t f_array_fill_from_time(f_array_t *fa, const char *path,
                struct tm begin, struct tm end)
{
        error_code_t primary_errno = E_OK;
        char new_path[PATH_MAX];
        size_t offset = strlen(path);
        struct tm ctx = begin;
        struct stat stat_buff;


        assert(offset + 2 <= PATH_MAX);
        strcpy(new_path, path);
        new_path[offset] = '/';
        offset++;

        /* Loop through the entire time range. */
        while (tm_diff(end, ctx) > 0) {
                /* Construct path string from time. */
                if (strftime(new_path + offset, PATH_MAX - offset,
                                        FLOW_FILE_FORMAT, &ctx) == 0) {
                        return E_PATH;
                }

                /* Increment context by rotation interval and normalize. */
                ctx.tm_sec += FLOW_FILE_ROTATION_INTERVAL;
                mktime_utc(&ctx);

                /* Check file existence. */
                if (stat(new_path, &stat_buff) != 0) {
                        secondary_errno = errno;
                        print_warn(E_PATH, secondary_errno, "%s \"%s\"",
                                        strerror(errno), new_path);
                } else {
                        primary_errno = f_array_add(fa, new_path,
                                        stat_buff.st_size);
                        if (primary_errno != E_OK) {
                                return primary_errno;
                        }
                }
        }


        return E_OK;
}

static error_code_t f_array_fill_from_path(f_array_t *fa, char *path)
{
        error_code_t primary_errno = E_OK;

        DIR *dir;
        struct dirent *entry;

        char new_path[PATH_MAX];
        struct stat stat_buff;


        /* Detect file type. */
        if (stat(path, &stat_buff) != 0) {
                secondary_errno = errno;
                print_warn(E_PATH, secondary_errno, "%s \"%s\"",
                                strerror(errno), path);
                return E_PATH;
        }

        if (!S_ISDIR(stat_buff.st_mode)) { //path is everything but direcotry
                return f_array_add(fa, path, stat_buff.st_size);
        }

        /* Path is directory. */
        dir = opendir(path);
        if (dir == NULL) {
                secondary_errno = errno;
                print_err(E_PATH, secondary_errno, "%s \"%s\"", strerror(errno),
                                path);
                return E_PATH;
        }

        /* Get all filenames, dot starting filenames are ignored. */
        while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') {
                        continue;
                }

                assert(strlen(path) + strlen(entry->d_name) + 2 <= PATH_MAX);
                strcpy(new_path, path);
                strcat(new_path, "/");
                strcat(new_path, entry->d_name);

                primary_errno = f_array_fill_from_path(fa, new_path);
                if (primary_errno != E_OK) {
                        return primary_errno;
                }
        }

        closedir(dir);


        return primary_errno;
}

error_code_t f_array_fill(f_array_t *fa, char *paths, struct tm begin,
                struct tm end)
{
        error_code_t primary_errno = E_OK;

        struct stat stat_buff;
        const bool have_time_range = tm_diff(end, begin) > 0;

        char *token;
        char *saveptr;


        for (token = strtok_r(paths, "\x1C", &saveptr); //first token
                        token != NULL;
                        token = strtok_r(NULL, "\x1C", &saveptr)) //next
        {
                /* Detect file type. */
                if (stat(token, &stat_buff) != 0) {
                        secondary_errno = errno;
                        print_warn(E_PATH, secondary_errno, "%s \"%s\"",
                                        strerror(errno), token);
                        continue;
                }

                if (have_time_range && S_ISDIR(stat_buff.st_mode)) {
                        primary_errno = f_array_fill_from_time(fa, token, begin,
                                        end);
                } else {
                        primary_errno = f_array_fill_from_path(fa, token);
                }

                if (primary_errno != E_OK) {
                        break;
                }
        }


        return primary_errno;
}
