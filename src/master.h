/**
 * @brief Master process declarations.
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

// forward declarations
struct cmdline_args;

/**
 * @brief Master's process entry point.
 *
 * Entry point to the code executed only by the master process (usually with
 * rank 0).
 *
 * @param[in] args Parsed command-line arguments.
 */
void
master_main(const struct cmdline_args *args);
