# For every option in the list, check whether the C compiler supports the
# option. If yes, add that option to the compilation of source files. If no,
# print warning and skip the option.
include(CheckCCompilerFlag)
function(check_and_add_compile_options option_list)
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
endfunction(check_and_add_compile_options)

# Capitalize the given string str and save the result in the res_var.
function(string_capitalize str res_var)
    string(SUBSTRING "${str}" 0  1 first_letter)
    string(SUBSTRING "${str}" 1 -1 rest)
    string(TOUPPER "${first_letter}" first_letter_upper)
    string(CONCAT str_capitalized "${first_letter_upper}" "${rest}")
    set("${res_var}" "${str_capitalized}" PARENT_SCOPE)
endfunction(string_capitalize)
