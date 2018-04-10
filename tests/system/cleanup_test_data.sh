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


# Delete created testing file(s).


ADV_TESTS_HOME=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )

# import common setup
. ${ADV_TESTS_HOME}/tests_setup.sh

rm -f $G_INPUT_DATA
