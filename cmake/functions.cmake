# get all propreties that cmake supports
execute_process(COMMAND cmake --help-property-list
    OUTPUT_VARIABLE CMAKE_PROPERTY_LIST)
# convert command output into a CMake list
string(REGEX REPLACE ";" "\\\\;" CMAKE_PROPERTY_LIST "${CMAKE_PROPERTY_LIST}")
string(REGEX REPLACE "\n" ";" CMAKE_PROPERTY_LIST "${CMAKE_PROPERTY_LIST}")

# print all properties that cmake supports
function(print_properties)
    message ("CMAKE_PROPERTY_LIST = ${CMAKE_PROPERTY_LIST}")
endfunction(print_properties)

# print all properties of the given target
function(print_target_properties tgt)
    if(NOT TARGET ${tgt})
        message("There is no target named '${tgt}'")
        return()
    endif()

    foreach (prop ${CMAKE_PROPERTY_LIST})
        string(REPLACE "<CONFIG>" "${CMAKE_BUILD_TYPE}" prop ${prop})
        if(prop STREQUAL "LOCATION" OR prop MATCHES "^LOCATION_"
                OR prop MATCHES "_LOCATION$")
            continue()
        endif()

        get_property(propval TARGET ${tgt} PROPERTY ${prop} SET)
        if (propval)
            get_target_property(propval ${tgt} ${prop})
            message ("${tgt} ${prop} = ${propval}")
        endif()
    endforeach(prop)
endfunction(print_target_properties)


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
