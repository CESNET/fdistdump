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

# Change the executable suffix. Fedora-based distros require executables to be
# suffixed with $MPI_SUFFIX. CMAKE_EXECUTABLE_SUFFIX variable does not work as
# expected (it is read-only).
if(EXECUTABLE_SUFFIX)
    set_target_properties(fdistdump PROPERTIES SUFFIX "${EXECUTABLE_SUFFIX}")
endif(EXECUTABLE_SUFFIX)

################################################################################
# generate configuration header file from a template and update the include dirs
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config.h.in"
               "${CMAKE_CURRENT_BINARY_DIR}/config.h"
               @ONLY)
include_directories("${PROJECT_BINARY_DIR}/src/")


################################################################################
# setup MPI compilation environment (variables set by FindMPI module)
# XXX: target_compile_* commands treats options with spaces as single options
#      and can handle a list -- do not expand lists.
#      Other target_* commands works somewhat magically.

# the FindMPI module was improved in CMake 3.10
if(CMAKE_VERSION VERSION_LESS 3.10)
    # add compilation options (cannot use target_compile_options because
    # MPI_C_COMPILE_FLAGS are space separated)
    string(APPEND CMAKE_C_FLAGS " ${MPI_C_COMPILE_FLAGS}")

    # add include directories
    target_include_directories(fdistdump PRIVATE "${MPI_C_INCLUDE_PATH}")
else()
    # add compilation options and definitiones (both lists)
    target_compile_options(fdistdump PRIVATE "${MPI_C_COMPILE_OPTIONS}")
    target_compile_definitions(fdistdump PRIVATE "${MPI_C_COMPILE_DEFINITIONS}")

    # add include directories
    target_include_directories(fdistdump PRIVATE "${MPI_C_INCLUDE_DIRS}")
endif(CMAKE_VERSION VERSION_LESS 3.10)

# add libraries and flags to use when linking
# strip whitespaces to satisfy policy CMP0004
string(STRIP "${MPI_C_LINK_FLAGS}" MPI_C_LINK_FLAGS)
target_link_libraries(fdistdump PRIVATE "${MPI_C_LINK_FLAGS}")
target_link_libraries(fdistdump PRIVATE "${MPI_C_LIBRARIES}")

################################################################################
# setup OpenMP compilation environment (variables set by FindOpenMP module)
# Add compilation options. Cannot use target_compile_options because
# MPI_C_COMPILE_FLAGS are space separated. CMAKE_C_FLAGS are also used when
# linking, so no additional libraries have to be added.
string(APPEND CMAKE_C_FLAGS " ${OpenMP_C_FLAGS}")

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
                "install or disable by -D ENABLE_BFINDEX=OFF\n"
                "project URL: https://github.com/CESNET/bloom-filter-index")
    else()
        target_link_libraries(fdistdump PRIVATE "${LIB_BFINDEX}")
    endif()

    CHECK_INCLUDE_FILE("bf_index.h" BFINDEX_HEADER)
    if(NOT BFINDEX_HEADER)
        message(FATAL_ERROR
                "bf_index.h not found\n"
                "install or disable by -D ENABLE_BFINDEX=OFF\n"
                "project URL: https://github.com/CESNET/bloom-filter-index")
    endif()
endif(ENABLE_BFINDEX)

################################################################################
# generate instalation rules for the target
install(TARGETS fdistdump
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
        COMPONENT "executables")
