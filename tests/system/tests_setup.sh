#!/usr/bin/env bash

# Copyright 2015-2018 CESNET
#
# This file is part of Fdistdump.
#
# Fdistdump is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fdistdump is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fdistdump.  If not, see <http://www.gnu.org/licenses/>.


# Common variables for tests aimed on comparing FDistDump and NFDump results.


LOC_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

#data source file in nfdump format for fdd/nfd queries
G_INPUT_DATA="${LOC_DIR}/test_data.nfcap"

#output files with results from queries (fdd/nfd)
G_FDD_RESULTS="${LOC_DIR}/fdd.results"
G_NFD_RESULTS="${LOC_DIR}/nfd.results"

#location of FDistDump binary
G_FDIST_DUMP=fdistdump

#query types
G_QTYPE_LISTFLOWS=1
G_QTYPE_AGGREG=2
G_QTYPE_STATS=3
