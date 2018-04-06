# create a list of source files
set(SOURCE_LIST
    arg_parse.c arg_parse.h
    common.c common.h
    errwarn.c errwarn.h
    fields.c fields.h
    main.c
    master.c master.h
    output.c output.h
    path_array.c path_array.h
    slave.c slave.h
    )
if(ENABLE_BFINDEX)
    list(APPEND SOURCE_LIST bfindex.c bfindex.h)
endif(ENABLE_BFINDEX)

# add an executable to the project using the source files
add_executable(fdistdump ${SOURCE_LIST})

################################################################################
# generate configuration header file from a template and update the include dirs
configure_file("${PROJECT_SOURCE_DIR}/src/config.h.in"
               "${PROJECT_BINARY_DIR}/src/config.h"
               @ONLY)
include_directories("${PROJECT_BINARY_DIR}/src/")


################################################################################
# setup MPI compilation environment (variables set by FindMPI module)
foreach(option ${MPI_C_COMPILE_OPTIONS})
    target_compile_options(fdistdump PRIVATE ${option})  # no need to check
endforeach(option)
foreach(definition ${MPI_C_COMPILE_DEFINITIONS})
    target_compile_definitions(fdistdump PRIVATE ${definition})
endforeach(definition)
# add include directories
target_include_directories(fdistdump PRIVATE ${MPI_C_INCLUDE_DIRS})

# add libraries and flags to use when linking
target_link_libraries(fdistdump PRIVATE ${MPI_C_LINK_FLAGS})
foreach(lib_name ${MPI_C_LIB_NAMES})
    target_link_libraries(fdistdump PRIVATE "${MPI_${lib_name}_LIBRARY}")
endforeach(lib_name ${MPI_C_LIB_NAMES})

################################################################################
# setup OpenMP compilation environment (variables set by FindOpenMP module)
target_compile_options(fdistdump PRIVATE ${OpenMP_C_FLAGS})
foreach(lib_name ${OpenMP_C_LIB_NAMES})
    target_link_libraries(fdistdump PRIVATE "${OpenMP_${lib_name}_LIBRARY}")
endforeach(lib_name)

################################################################################
# Search for libraries to use when linking. Add full paths to a library files to
# linker options, because it bring a possibility to change it later using e.g.
# ccmake.

find_library(LIB_C c)
if(LIB_C STREQUAL "LIB_C-NOTFOUND")
    message(FATAL_ERROR "standard C library not found")
else()
    target_link_libraries(fdistdump PRIVATE "${LIB_C}")
endif(LIB_C STREQUAL "LIB_C-NOTFOUND")

find_library(LIB_MATH m)
if(LIB_MATH STREQUAL "LIB_MATH-NOTFOUND")
    message(FATAL_ERROR "standard math library not found")
else()
    target_link_libraries(fdistdump PRIVATE "${LIB_MATH}")
endif(LIB_MATH STREQUAL "LIB_MATH-NOTFOUND")

find_library(LIB_LIBNF nf)
if(LIB_LIBNF STREQUAL "LIB_LIBNF-NOTFOUND")
    message(FATAL_ERROR
            "libnf library not found\n"
            "project URL: from https://github.com/VUTBR/libnf")
else()
    target_link_libraries(fdistdump PRIVATE "${LIB_LIBNF}")
endif()

CHECK_INCLUDE_FILE("libnf.h" LIBNF_HEADER)
if(NOT LIBNF_HEADER)
    message(FATAL_ERROR "libnf.h not found "
            "(see https://github.com/VUTBR/libnf)")
endif()

if(ENABLE_BFINDEX)
    find_library(LIB_BFINDEX bfindex)
    if(LIB_BFINDEX STREQUAL "LIB_BFINDEX-NOTFOUND")
        message(FATAL_ERROR
                "bfindex library not found\n"
                "install or disable by -DENABLE_BFINDEX=OFF\n"
                "project URL: https://github.com/CESNET/bloom-filter-index")
    else()
        target_link_libraries(fdistdump PRIVATE "${LIB_BFINDEX}")
    endif()

    CHECK_INCLUDE_FILE("bf_index.h" BFINDEX_HEADER)
    if(NOT BFINDEX_HEADER)
        message(FATAL_ERROR
                "bf_index.h not found\n"
                "install or disable by -DENABLE_BFINDEX=OFF\n"
                "project URL: https://github.com/CESNET/bloom-filter-index")
    endif()
endif(ENABLE_BFINDEX)

################################################################################
# generate instalation rules for the target
install(TARGETS fdistdump RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
