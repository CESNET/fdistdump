/**
 * @brief Preprocessing of user specified paths and creation of array of
 * specific paths to flow files from string(s) and time range.
 */

/*
 * Copyright 2015-2018 CESNET
 *
 * This file is part of Fdistdump.
 *
 * Fdistdump is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Fdistdump is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Fdistdump.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <stddef.h>                // for size_t


// forward declarations
struct tm;


char **
path_array_gen(char *const paths[], size_t paths_cnt, const struct tm begin,
               const struct tm end, size_t *out_paths_cnt);
void
path_array_free(char *paths[], size_t paths_cnt);
