#!/usr/bin/env bash

# Author: Pavel Krobot, <Pavel.Krobot@cesnet.cz>
# Date: 2015
#
# Description: Common variables for tests aimed on comparing FDistDump and
# NFDump results.
#
#
#
# Copyright (C) 2015 CESNET
#
# LICENSE TERMS
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in
#    the documentation and/or other materials provided with the
#    distribution.
# 3. Neither the name of the Company nor the names of its contributors
#    may be used to endorse or promote products derived from this
#    software without specific prior written permission.
#
# ALTERNATIVELY, provided that this notice is retained in full, this
# product may be distributed under the terms of the GNU General Public
# License (GPL) version 2 or later, in which case the provisions
# of the GPL apply INSTEAD OF those given above.
#
# This software is provided ``as is'', and any express or implied
# warranties, including, but not limited to, the implied warranties of
# merchantability and fitness for a particular purpose are disclaimed.
# In no event shall the company or contributors be liable for any
# direct, indirect, incidental, special, exemplary, or consequential
# damages (including, but not limited to, procurement of substitute
# goods or services; loss of use, data, or profits; or business
# interruption) however caused and on any theory of liability, whether
# in contract, strict liability, or tort (including negligence or
# otherwise) arising in any way out of the use of this software, even
# if advised of the possibility of such damage.

#if [ ! -z "$FDD_ADV_TESTS_MISSING" ]; then
#      echo "Common setup: Error: Mandatory programs for advanced test are not installed ($FDD_ADV_TESTS_MISSING)."
#      return 77
#fi

LOC_DIR=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

# data source file in nfdump format for fdd/nfd queries
G_INPUT_DATA="${LOC_DIR}/test_data.nfcap"

# output files with results from queries (fdd/nfd)
G_FDD_RESULTS="${LOC_DIR}/fdd.results"
G_NFD_RESULTS="${LOC_DIR}/nfd.results"

# location of FDistDump binary
G_FDIST_DUMP=${LOC_DIR}/../../src/fdistdump

# query types
G_QTYPE_LISTFLOWS=1
G_QTYPE_AGGREG=2
G_QTYPE_STATS=3

return 0
