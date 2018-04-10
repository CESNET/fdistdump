# Copyright 2018 CESNET
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
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fdistdump. If not, see <http://www.gnu.org/licenses/>.
# generate a man page file from a template


string(TIMESTAMP DATE "%Y-%m-%d")
string(TOUPPER "${PROJECT_NAME}" PROJECT_NAME_UPPER)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/man/${PROJECT_NAME}.1.in"
               "${CMAKE_CURRENT_BINARY_DIR}/man/${PROJECT_NAME}.1"
               @ONLY)

# generate instalation rules for the documentation
install(FILES "${PROJECT_BINARY_DIR}/doc/man/${PROJECT_NAME}.1"
        DESTINATION "${CMAKE_INSTALL_MANDIR}/man1"
        COMPONENT "man-pages")
