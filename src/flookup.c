/**
 * \file flookup.c
 * \brief
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

#include "common.h"
#include "flookup.h"

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h> //size_t
#include <limits.h> //PATH_MAX

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


/* fill file array by file names according to time range expression */
error_code_t f_array_fill_from_time(f_array_t *fa, const struct tm begin,
                const struct tm end)
{
        error_code_t primary_errno = E_OK;
        char new_path[PATH_MAX] = "";
        struct tm ctx = begin;

        /* Loop through entire interval. */
        while (tm_diff(end, ctx) > 0) {
                /* Construct path string from time. */
                if (strftime(new_path, PATH_MAX, FLOW_FILE_PATH, &ctx) == 0) {
                        print_err(E_PATH, 0, "strftime()");
                        return E_PATH;
                }

                /* Increment context by rotation interval and normalize. */
                ctx.tm_sec += FLOW_FILE_ROTATION_INTERVAL;
                mktime_utc(&ctx);

                primary_errno = f_array_fill_from_path(fa, new_path);
                if (primary_errno != E_OK) {
                        return primary_errno;
                }
        }

        return E_OK;
}

/* fill file array by file names according to path expression */
error_code_t f_array_fill_from_path(f_array_t *fa, const char *path)
{
        DIR *dirp;
        struct dirent *dp;
        struct stat fs_buff;
        char new_path[PATH_MAX] = "";

        /* detect file type */
        if (stat(path, &fs_buff) != 0) {
                secondary_errno = errno;
                print_warn(E_PATH, secondary_errno, "%s \"%s\"",
                                strerror(errno), path);
                return E_OK;
        }

        /* regular file */
        if (S_ISREG(fs_buff.st_mode)) {
                return f_array_add(fa, path, fs_buff.st_size);

        /* directory */
        } else if (S_ISDIR(fs_buff.st_mode)) {
                if ((dirp = opendir(path)) == NULL) {
                        secondary_errno = errno;
                        print_err(E_PATH, secondary_errno, "%s \"%s\"",
                                        strerror(errno), path);
                        return E_PATH;
                }

                /// TODO consider NORMAL profiles dir structure here ------>>>
                /* get all relevant filenames */
                while ((dp = readdir(dirp)) != NULL) {
                        /* ignore file names starting with dot */
                        /// TODO ignore unwanted subprofiles dirs OR
                        ///      consider only expected dirs
                        if (dp->d_name[0] != '.') {
                                strcpy(new_path, path);
                                strcat(new_path, "/");
                                strcat(new_path, dp->d_name);
                                error_code_t ret = f_array_fill_from_path(fa,
                                                new_path);
                                if (ret != E_OK) {
                                        return ret;
                                }
                        }
                }
                closedir(dirp);
                /// <<<- TODO consider NORMAL profiles dir structure there <<<
        }

        return E_OK;
}

/*
int main(void)
{
        f_array_t files;

        f_array_init(&files);

        flist_lookup_files_path(&files, "..");

        for (size_t i = 0; i < files.f_cnt; ++i) {
                printf("Processing: %s (%zu)\n", files.f_items[i].f_name,
                                files.f_items[i].f_size);
        }

        f_array_free(&files);

}
*/
