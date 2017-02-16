/**
 * \file path_array.c
 * \brief
 * \author Pavel Krobot, <Pavel.Krobot@cesnet.cz>
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

#include "common.h"
#include "path_array.h"
#include "print.h"
#include "file_index/file_index.h"

#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h> //size_t
#include <limits.h> //PATH_MAX
#include <assert.h>
#include <ctype.h> //isdigit()
#include <unistd.h> //gethostname()

#include <dirent.h>
#include <sys/stat.h>

#include <mpi.h>


#define PATH_ARRAY_INIT_SIZE 50


struct path_array_ctx {
        char **names;
        size_t names_cnt;
        size_t names_size;
};


/* Add path into the path array. */
static error_code_t add_file(struct path_array_ctx *pac, const char *name)
{
        /* Is there a free space in the array for another file? */
        if (pac->names_cnt == pac->names_size) { //no, ask for another space
                char **new_names;

                new_names = realloc(pac->names,
                                pac->names_size * 2 * sizeof (*pac->names));
                if (new_names == NULL) { //failure
                        PRINT_ERROR(E_MEM, 0, "realloc()");
                        return E_MEM;
                } else { //success
                        pac->names = new_names;
                        pac->names_size *= 2;
                }
        }

        /* Allocate space for the name and copy it there. */
        pac->names[pac->names_cnt] = strdup(name);
        if (pac->names[pac->names_cnt] == NULL) {
                PRINT_ERROR(E_MEM, 0, "strdup()");
                return E_MEM;
        } else {
                pac->names_cnt++;
                return E_OK;
        }
}


static error_code_t fill_from_time(struct path_array_ctx *pac,
                char path[PATH_MAX], const struct tm begin,
                const struct tm end)
{
        error_code_t primary_errno = E_OK;
        size_t offset = strlen(path);
        struct tm ctx = begin;
        struct stat stat_buff;


        /* Make sure there is a terminating slash. */
        if (path[offset - 1] != '/') {
                path[offset++] = '/';
        }

        /* Loop through the entire time range. */
        while (tm_diff(end, ctx) > 0) {
                /* Construct path string from time. */
                if (strftime(path + offset, PATH_MAX - offset, FLOW_FILE_FORMAT,
                                        &ctx) == 0) {
                        errno = ENAMETOOLONG;
                        PRINT_WARNING(E_PATH, errno, "%s \"%s\"", strerror(errno),
                                        path);
                        continue;
                }

                /* Increment context by rotation interval and normalize. */
                ctx.tm_sec += FLOW_FILE_ROTATION_INTERVAL;
                mktime_utc(&ctx);

                /* Check file existence. */
                if (stat(path, &stat_buff) != 0) {
                        PRINT_WARNING(E_PATH, errno, "%s \"%s\"", strerror(errno),
                                        path);
                } else {
                        primary_errno = add_file(pac, path);
                        if (primary_errno != E_OK) {
                                return primary_errno;
                        }
                }
        }


        return E_OK;
}

static error_code_t fill_from_path(struct path_array_ctx *pac,
                const char path[PATH_MAX])
{
        error_code_t primary_errno = E_OK;
        DIR *dir;
        struct dirent *entry;
        struct stat stat_buff;


        /* Detect file type. */
        if (stat(path, &stat_buff) != 0) {
                PRINT_WARNING(E_PATH, errno, "%s \"%s\"", strerror(errno), path);
                return E_OK; //not a fatal error
        }

        if (!S_ISDIR(stat_buff.st_mode)) {
                /* Path is not a directory. */
                return add_file(pac, path);
        }

        dir = opendir(path);
        if (dir == NULL) {
                PRINT_WARNING(E_PATH, errno, "%s \"%s\"", strerror(errno), path);
                return E_OK; //not a fatal error
        }

        /* Loop through all the files. */
        while ((entry = readdir(dir)) != NULL) {
                char new_path[PATH_MAX];

                /* Dot starting/file-indexing filenames are ignored. */
                if (entry->d_name[0] == '.' || strncmp(entry->d_name,
                                        FIDX_FN_PREFIX,
                                        sizeof (FIDX_FN_PREFIX)) == 0) {
                        continue; //skip the file
                }
                /* Too long filenames are ignored. */
                if (strlen(path) + strlen(entry->d_name) + 1 > PATH_MAX) {
                        errno = ENAMETOOLONG;
                        PRINT_WARNING(E_PATH, errno, "%s \"%s\"", strerror(errno),
                                        path);
                        continue; //skip the file
                }

                /* Construct new path: append child to the parent. */
                strcpy(new_path, path);
                strcat(new_path, "/");
                strcat(new_path, entry->d_name);

                primary_errno = fill_from_path(pac, new_path);
                if (primary_errno != E_OK) {
                        break;
                }
        }
        closedir(dir);


        return primary_errno;
}


/** \brief Transform format string into path string.
 *
 * The format string is a character string composed of zero or more directives:
 * ordinary characters (not %), which are copied unchanged to the output path;
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
 * \param[in] format Format string.
 * \param[out] path  Path creted from the format string.
 * \return True if path should be processed, false if path should be skipped.
 */
static bool path_preprocessor(const char *format, char path[PATH_MAX])
{
        char tmp[PATH_MAX];
        char *last_path = tmp;
        char *perc_sign;


        if (strlen(format) >= PATH_MAX) {
                PRINT_WARNING(E_PATH, 0, "conversion specifier too long, "
                                "skipping \"%s\"", format);
                return false;
        }

        strcpy(tmp, format); //copy all except initial conversion specification

        /* Format starts by %DIGIT. */
        if (tmp[0] == '%' && isdigit(tmp[1])) {
                last_path = tmp + 2;
                int world_rank;

                while (isdigit(*last_path) && last_path++); //skip all digits
                if (*last_path++ != ':') { //check for terminating colon
                        PRINT_WARNING(E_PATH, 0, "invalid conversion specifier, "
                                        "skipping \"%s\"", format);
                        return false;
                }

                MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
                if (world_rank != atoi(tmp + 1)) {
                        return false; //this path is not for me (different rank)
                }
        }

        /* last_path now contains all except the initial conversion specifier.*/
        perc_sign = strchr(last_path, '%'); //find first percent sign
        while (perc_sign != NULL) {
                *perc_sign = '\0';
                strcat(path, last_path); //copy format path till percent sign

                perc_sign++; //move pointer to escaped character
                switch (*perc_sign) {
                case 'h':
                        errno = 0;
                        gethostname(path + strlen(path),
                                        PATH_MAX - strlen(path));
                        if (errno != 0) {
                                errno = ENAMETOOLONG;
                                PRINT_WARNING(E_PATH, errno, "%s \"%s\"",
                                                strerror(errno), format);
                                return false;
                        }
                        break;
                default:
                        PRINT_WARNING(E_PATH, 0, "unknown conversion specifier, "
                                        "skipping \"%s\"", format);
                        return false;
                }

                last_path = perc_sign + 1; //ptr to next regular character
                perc_sign = strchr(last_path, '%');
        }

        strcat(path, last_path); //copy rest of the format string
        PRINT_DEBUG("format: %s\tpath: %s", format, path);


        return true;
}

/* Generate array of paths from paths string and optional time range. */
char ** path_array_gen(char *paths, const struct tm begin,
                const struct tm end, size_t *cnt)
{
        error_code_t primary_errno = E_OK;
        struct path_array_ctx pac = { 0 };
        const bool have_time_range = tm_diff(end, begin) > 0;
        char *token;
        char *saveptr;


        pac.names = malloc(PATH_ARRAY_INIT_SIZE * sizeof (*pac.names));
        if (pac.names == NULL) {
                PRINT_ERROR(E_MEM, 0, "malloc()");
                return NULL;
        } else {
                pac.names_size = PATH_ARRAY_INIT_SIZE;
        }

        /* Split paths string into particular paths. */
        for (token = strtok_r(paths, "\x1C", &saveptr); //first token
                        token != NULL;
                        token = strtok_r(NULL, "\x1C", &saveptr)) //next
        {
                char path[PATH_MAX] = { 0 };
                struct stat stat_buff;

                /* Apply preprocessor rules and continue or skip path. */
                if (!path_preprocessor(token, path)) {
                        continue; //skip path
                }

                /* Check for file existence and other errors. */
                if (stat(path, &stat_buff) != 0) {
                        PRINT_WARNING(E_PATH, errno, "%s \"%s\"", strerror(errno),
                                        path);
                        continue; //skip path
                }

                /*
                 * Generate filenames from time range if time range is available
                 * and path is a directory. Use path as a filename otherwise.
                 */
                if (have_time_range && S_ISDIR(stat_buff.st_mode)) {
                        primary_errno = fill_from_time(&pac, path, begin, end);
                } else {
                        primary_errno = fill_from_path(&pac, path);
                }
                if (primary_errno != E_OK) {
                        path_array_free(pac.names, pac.names_cnt);
                        return NULL;
                }
        }


        *cnt = pac.names_cnt;
        return pac.names;
}

/* Free all file names and array. */
void path_array_free(char **names, size_t names_cnt)
{
        if (names != NULL) {
                for (size_t i = 0; i < names_cnt; ++i) {
                        free(names[i]);
                }
                free(names);
        }
}
