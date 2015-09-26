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
        f_item *new_array;

        /// TODO consider of removing this, allocate F_ARRAY_RESIZE_AMOUNT right away
        if (fa->a_size == 0) { // first record, allocate memory only for one file
                fa->f_items = malloc(sizeof (f_item));
                if (fa->f_items == NULL) {
                        return E_MEM;
                }
                fa->a_size = 1;
        } else {
                new_array = realloc(fa->f_items,
                                    (fa->a_size + F_ARRAY_RESIZE_AMOUNT) *
                                    sizeof(f_item));

                if (new_array == NULL){
                        // f_array_free(fa);
                        return E_MEM;
                }

                fa->f_items = new_array;
                fa->a_size += F_ARRAY_RESIZE_AMOUNT;
        }

        return E_OK;
}

/* add filename-item into filename-array */
error_code_t f_array_add(f_array_t *fa, char *f_name, off_t f_size)
{
        if (fa->f_cnt == fa->a_size) {
                error_code_t ret = f_array_resize(fa);
                if (ret != E_OK) {
                        return ret;
                }
        }

        fa->f_items[fa->f_cnt].f_name = strdup(f_name);
        if (fa->f_items[fa->f_cnt].f_name == NULL) {
                return E_MEM;
        }

        fa->f_items[fa->f_cnt].f_size = f_size;

        fa->f_cnt++;

        return E_OK;
}

/* fill file array by file names according to time range expression */
error_code_t f_array_fill_from_time(f_array_t *fa, char *time_expr)
{
        (void) fa;
        (void) time_expr;

        return E_OK;
}

/* fill file array by file names according to path expression */
error_code_t f_array_fill_from_path(f_array_t *fa, char *path)
{
        DIR *dirp;
        struct dirent *dp;
        struct stat fs_buff;
        char new_path[PATH_MAX] = "";

        /* detect file type */
        if (stat(path, &fs_buff) != 0) {
                secondary_errno = errno;
                print_err(E_PATH, secondary_errno, "%s \"%s\"", strerror(errno),
                                path);
                return E_PATH;
        }

        /* regular file */
        if (S_ISREG(fs_buff.st_mode)) {
                /// TODO report size
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
