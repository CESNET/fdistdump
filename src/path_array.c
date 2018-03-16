/** Preprocessing of user specified paths and creation of array of specific
 * paths flow files from string or time range.
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
 */

#include "path_array.h"

#include <assert.h>                // for assert
#include <ctype.h>                 // for isdigit
#include <errno.h>                 // for errno, ENAMETOOLONG
#include <limits.h>                // for PATH_MAX
#include <stdbool.h>               // for false, bool, true
#include <stddef.h>                // for size_t, NULL
#include <stdlib.h>                // for free, atoi, malloc, realloc
#include <string.h>                // for strerror, strlen, strcat, strchr
#include <time.h>                  // for strftime

#include <dirent.h>                // for closedir, dirent, opendir, readdir
#include <mpi.h>                   // for MPI_Comm_rank, MPI_COMM_WORLD
#include <sys/stat.h>              // for stat, S_ISDIR
#include <unistd.h>                // for gethostname

#ifdef HAVE_LIBBFINDEX
#include "bfindex.h"
#endif  // HAVE_LIBBFINDEX
#include "common.h"                // for ::E_OK, ::E_PATH, error_code_t
#include "errwarn.h"            // for error/warning/info/debug messages, ...


#define PATH_ARRAY_INIT_SIZE 50


struct path_array_ctx {
        char **names;
        size_t names_cnt;
        size_t names_size;
};


/* Add path into the path array. */
static error_code_t add_file(struct path_array_ctx *pa_ctx, const char *name)
{
        /* Is there a free space in the array for another file? */
        if (pa_ctx->names_cnt == pa_ctx->names_size) { //no, ask for another space
                char **new_names;

                new_names = realloc(pa_ctx->names,
                                pa_ctx->names_size * 2 * sizeof (*pa_ctx->names));
                if (new_names == NULL) { //failure
                        ERROR(E_MEM, "realloc()");
                        return E_MEM;
                } else { //success
                        pa_ctx->names = new_names;
                        pa_ctx->names_size *= 2;
                }
        }

        /* Allocate space for the name and copy it there. */
        pa_ctx->names[pa_ctx->names_cnt] = strdup(name);
        if (pa_ctx->names[pa_ctx->names_cnt] == NULL) {
                ERROR(E_MEM, "strdup()");
                return E_MEM;
        } else {
                pa_ctx->names_cnt++;
                return E_OK;
        }
}


static error_code_t fill_from_time(struct path_array_ctx *pa_ctx,
                char path[PATH_MAX], const struct tm begin,
                const struct tm end)
{
        error_code_t ecode = E_OK;
        size_t offset = strlen(path);
        struct tm ctx = begin;
        struct stat stat_buff;


        /* Make sure there is the terminating slash. */
        if (path[offset - 1] != '/') {
                path[offset++] = '/';
        }

        /* Loop through the entire time range. */
        while (tm_diff(end, ctx) > 0) {
                /* Construct path string from time. */
                if (strftime(path + offset, PATH_MAX - offset, FLOW_FILE_FORMAT,
                                        &ctx) == 0) {
                        errno = ENAMETOOLONG;
                        WARNING(E_PATH, "%s `%s'", strerror(errno), path);
                        continue;
                }

                /* Increment context by rotation interval and normalize. */
                ctx.tm_sec += FLOW_FILE_ROTATION_INTERVAL;
                mktime_utc(&ctx);

                /* Check file existence. */
                if (stat(path, &stat_buff) != 0) {
                        WARNING(E_PATH, "%s `%s'", strerror(errno), path);
                } else {
                        ecode = add_file(pa_ctx, path);
                        if (ecode != E_OK) {
                                return ecode;
                        }
                }
        }


        return E_OK;
}

static error_code_t fill_from_path(struct path_array_ctx *pa_ctx,
                const char path[PATH_MAX])
{
        error_code_t ecode = E_OK;
        DIR *dir;
        struct dirent *entry;
        struct stat stat_buff;


        /* Detect file type. */
        if (stat(path, &stat_buff) != 0) {
                WARNING(E_PATH, "%s `%s'", strerror(errno), path);
                return E_OK;  // not a fatal error
        }

        if (!S_ISDIR(stat_buff.st_mode)) {
                /* Path is not a directory. */
                return add_file(pa_ctx, path);
        }

        dir = opendir(path);
        if (dir == NULL) {
                WARNING(E_PATH, "%s `%s'", strerror(errno), path);
                return E_OK;  // not a fatal error
        }

        /* Loop through all the files. */
        while ((entry = readdir(dir)) != NULL) {
                char new_path[PATH_MAX];

                /* Dot starting (hidden) files are ignored. */
                if (entry->d_name[0] == '.') {
                        continue;  // skip this file
                }
                /* bfindex files are ignored. */
#ifdef HAVE_LIBBFINDEX
                if (strncmp(entry->d_name, BFINDEX_FILE_NAME_PREFIX ".",
                            STRLEN_STATIC(BFINDEX_FILE_NAME_PREFIX ".")) == 0) {
                        continue;  // skip this file
                }
#endif  // HAVE_LIBBFINDEX
                /* Too long filenames are ignored. */
                if (strlen(path) + strlen(entry->d_name) + 1 > PATH_MAX) {
                        errno = ENAMETOOLONG;
                        WARNING(E_PATH, "%s `%s'", strerror(errno), path);
                        continue;  // skip this file
                }

                /* Construct new path: append child to the parent. */
                strcpy(new_path, path);
                strcat(new_path, "/");
                strcat(new_path, entry->d_name);

                ecode = fill_from_path(pa_ctx, new_path);
                if (ecode != E_OK) {
                        break;
                }
        }
        closedir(dir);


        return ecode;
}


/**
 * @brief Transform format string into path string.
 *
 * The format string is a character string composed of zero or more directives:
 * ordinary characters (not %), which are copied unchanged to the output path,
 * and conversion specifications, each of which results in an additional action.
 * Each conversion specification is introduced by the character % followed by a
 * conversion specifier character.
 *
 * If format begins with "%DIGITS:", then path is targeted only for one specific
 * slave, the one with DIGITS equal to the MPI rank of the slave.
 *
 * Conversion specifiers:
 *   h: converted into the hostname of the node
 *
 * @param[in] format Format string.
 * @param[out] path Path creted from the format string.
 *
 * @return True if path should be processed, false if path should be skipped.
 */
static bool
path_preprocessor(const char *format, char path[PATH_MAX])
{
    assert(format && path);

    if (strlen(format) >= PATH_MAX) {
        WARNING(E_PATH, "conversion specifier too long, skipping `%s'", format);
        return false;
    }

    char tmp[PATH_MAX];
    char *last_path = tmp;
    strcpy(tmp, format);

    // format starts by %DIGIT
    if (tmp[0] == '%' && isdigit(tmp[1])) {
        last_path = tmp + 2;

        while (isdigit(*last_path) && last_path++);  // skip all digits
        if (*last_path++ != ':') {  // check for the terminating colon
            WARNING(E_PATH, "invalid conversion specifier, skipping `%s'",
                    format);
            return false;
        }

        int world_rank;
        MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
        if (world_rank != atoi(tmp + 1)) {
            return false;  // this path is not for me, it is for different rank
        }
    }

    // last_path now contains all except the initial rank directive
    char *perc_sign = strchr(last_path, '%');  // find the first directive
    while (perc_sign) {
        // copy the format string till the percent sign
        *perc_sign = '\0';
        strcat(path, last_path);

        perc_sign++; // move the pointer to the escaped character
        switch (*perc_sign) {
        case 'h':
            errno = 0;
            gethostname(path + strlen(path), PATH_MAX - strlen(path));
            if (errno != 0) {
                errno = ENAMETOOLONG;
                WARNING(E_PATH, "%s `%s'", strerror(errno), format);
                return false;
            }
            break;

        default:
            WARNING(E_PATH, "unknown conversion specifier, skipping `%s'",
                    format);
            return false;
        }

        last_path = perc_sign + 1;  // a pointer to the next regular character
        perc_sign = strchr(last_path, '%');
    }

    strcat(path, last_path);  // copy the rest of the format string
    DEBUG("path preprocessor: `%s' -> `%s'", format, path);

    return true;
}


char **
path_array_gen(char *const paths[], size_t paths_cnt, const struct tm begin,
               const struct tm end, size_t *out_paths_cnt)
{
    assert(paths && *paths && paths_cnt > 0 && out_paths_cnt);

    // allocate memory for the generated paths
    struct path_array_ctx pa_ctx = { 0 };
    pa_ctx.names = malloc(PATH_ARRAY_INIT_SIZE * sizeof (*pa_ctx.names));
    if (!pa_ctx.names) {
        ERROR(E_MEM, "malloc()");
        return NULL;
    } else {
        pa_ctx.names_size = PATH_ARRAY_INIT_SIZE;
    }

    for (size_t i = 0; i < paths_cnt; ++i) {
        // apply preprocessor rules, skip the path on error
        char new_path[PATH_MAX] = { 0 };
        if (!path_preprocessor(paths[i], new_path)) {
            continue;
        }

        // check for file existence and other errors
        struct stat stat_buff;
        if (stat(new_path, &stat_buff) != 0) {
            WARNING(E_PATH, "%s `%s'", strerror(errno), new_path);
            continue;
        }

        /*
         * Generate filenames from the time range if it is available (end to
         * start difference is > 0) and new_path is a directory. If new_path is
         * not a directory, process it as a regular file.
         */
        error_code_t ecode = E_OK;
        if (tm_diff(end, begin) > 0 && S_ISDIR(stat_buff.st_mode)) {
            ecode = fill_from_time(&pa_ctx, new_path, begin, end);
        } else {
            ecode = fill_from_path(&pa_ctx, new_path);
        }
        if (ecode != E_OK) {
            path_array_free(pa_ctx.names, pa_ctx.names_cnt);
            return NULL;
        }
    }

    *out_paths_cnt = pa_ctx.names_cnt;
    return pa_ctx.names;
}

void
path_array_free(char *paths[], size_t paths_cnt)
{
    if (paths) {
        for (size_t i = 0; i < paths_cnt; ++i) {
            free(paths[i]);
        }
        free(paths);
    }
}
