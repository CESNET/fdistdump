include(CheckIncludeFile)

set(SRC_LIST
    arg_parse.c arg_parse.h
    common.c common.h
    errwarn.c errwarn.h
    fields.c fields.h
    main.c
    master.c master.h
    output.c output.h
    path_array.c path_array.h
    slave.c slave.h
    bfindex.c bfindex.h
    )
add_executable(fdistdump ${SRC_LIST})

# generate configuration header file from a template
configure_file("${PROJECT_SOURCE_DIR}/src/config.h.in"
               "${PROJECT_BINARY_DIR}/src/config.h")
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
target_link_libraries(fdistdump PRIVATE ${MPI_C_LIBRARIES})

################################################################################
# setup OpenMP compilation environment (variables set by FindOpenMP module)
target_compile_options(fdistdump PRIVATE ${OpenMP_C_FLAGS})  # no need to check
foreach(lib_name ${OpenMP_C_LIB_NAMES})
    target_link_libraries(fdistdump PRIVATE ${lib_name})
endforeach(lib_name)

################################################################################
# find and link with the standard math library
find_library(LIBNF_LIB m)
if (LIBNF_LIB STREQUAL "LIBNF_LIB-NOTFOUND")
    message(FATAL_ERROR "math library not found")
else()
    target_link_libraries(fdistdump PRIVATE m)
endif (LIBNF_LIB STREQUAL "LIBNF_LIB-NOTFOUND")

# find and link with the libnf library
find_library(LIBNF_LIB nf)
if (LIBNF_LIB STREQUAL "LIBNF_LIB-NOTFOUND")
    message(FATAL_ERROR "libnf library not found (see https://github.com/VUTBR/libnf)")
else()
    target_link_libraries(fdistdump PRIVATE nf)
endif ()
CHECK_INCLUDE_FILE("libnf.h" LIBNF_HEADER)
if (NOT LIBNF_HEADER)
    message(FATAL_ERROR "libnf.h not found (see https://github.com/VUTBR/libnf)")
endif()

# find and link with the bfindex library
find_library(BFINDEX_LIB bfindex)
if (BFINDEX_LIB STREQUAL "BFINDEX_LIB-NOTFOUND")
    message(FATAL_ERROR "bfindex library not found (see https://github.com/CESNET/bloom-filter-index)")
else()
    target_link_libraries(fdistdump PRIVATE bfindex)
endif ()
CHECK_INCLUDE_FILE("bf_index.h" BFINDEX_HEADER)
if (NOT BFINDEX_HEADER)
    message(FATAL_ERROR "bf_index.h not found (https://github.com/CESNET/bloom-filter-index)")
endif()
