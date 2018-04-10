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
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fdistdump.  If not, see <http://www.gnu.org/licenses/>.


# For every option in the list, check whether the C compiler supports the
# option. If yes, add that option to the compilation of source files. If no,
# print warning and skip the option.
include(CheckCCompilerFlag)
function(check_and_add_C_compile_options option_list)
    message(STATUS "Checking compiler options ${option_list}")
    foreach(option ${option_list})
        # create a name meeting the CMake variable identifier grammar rules
        string(REGEX REPLACE "[^a-zA-Z0-9_]" "" option_name "${option}")

        # check whether the C compiler supports a given option
        check_c_compiler_flag("${option}" ${option_name})
        if (${option_name})
            # add option to the compilation of source files
            add_compile_options("${option}")
        else()
            message(WARNING "Option `${option}' not supported")
        endif(${option_name})
    endforeach(option)
endfunction(check_and_add_C_compile_options)

# Capitalize the given string str and save the result in the res_var.
function(string_capitalize str res_var)
    string(SUBSTRING "${str}" 0  1 first_letter)
    string(SUBSTRING "${str}" 1 -1 rest)
    string(TOUPPER "${first_letter}" first_letter_upper)
    string(CONCAT str_capitalized "${first_letter_upper}" "${rest}")
    set("${res_var}" "${str_capitalized}" PARENT_SCOPE)
endfunction(string_capitalize)
