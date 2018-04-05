# set the minimum required version of cmake
cmake_minimum_required(VERSION 3.5)

# set a name and version for the project
project(fdistdump
        VERSION 0.4.1
        DESCRIPTION "a tool to query IP flow records on a distributed system"
        LANGUAGES C)

# check the current system/platform
if(NOT UNIX)
    message(FATAL_ERROR "only UNIX-like OS's (including Apple OS X and CygWin) are supported")
endif()

include(cmake/functions.cmake)

set(CMAKE_VERBOSE_MAKEFILE ON)  # TODO: remove

# command-line option() usage: -Doption=ON or -Doption=OFF
option(ENABLE_BFINDEX
       "Enable/disable support for the Bloom filter indexing library"
       ON)

################################################################################
# Setup options for the C compiler, preprocessor, and linker.

# Use only C11 (ISO/IEC 9899:2011) and POSIX 200809L (IEEE 1003.1-2008).
set(CMAKE_C_STANDARD 11)  # require C11
set(CMAKE_C_STANDARD_REQUIRED TRUE)  # do not decay below C11
set(CMAKE_C_EXTENSIONS FALSE)  # use -std=c11 instead of -std=gnu11
add_definitions(-D_POSIX_C_SOURCE=200809L)  # POSIX 200809L

# add project-global compile options
set(basic_warning_options
    -Wall       # all the warnings about questionable constructions, etc.
    -Wextra     # some extra warning flags that are not enabled by -Wall
    -Wpedantic  # all the warnings demanded by strict ISO C and ISO C++
    )
check_and_add_compile_options("${basic_warning_options}")

set(advanced_warning_options
    -Winit-self  # uninitialized variables that are initialized with themselves
    -Wcast-align  # a pointer is cast such that the required alignment of the target is increased
    -Wcast-qual  # a pointer is cast so as to remove a type qualifier from the target type
    -Wmissing-include-dirs  # a user-supplied include directory (-I) does not exist
    -Wshadow  # a local variable or type declaration shadows something
    # switch statement related warnings
    -Wswitch -Wswitch-bool -Wswitch-default -Wswitch-enum -Wswitch-unreachable
    -Wfloat-equal  # if floating-point values are used in equality comparisons
    -Wdouble-promotion  # a value of type "float" is implicitly promoted to "double"
    -Wundef  # an undefined identifier is evaluated in an "#if" directive
    -Wformat=2 -Wno-format-y2k  # format warnings, disable buggy Y2K warnings
    -fstack-protector -fstack-protector-strong  # emit code to check for buffer overflows
    )
check_and_add_compile_options("${advanced_warning_options}")


# find a Message Passing Interface (MPI) implementation for all enabled languag.
find_package(MPI REQUIRED)
set(mpi_c_version_needed 2.0)
if (MPI_C_VERSION VERSION_LESS mpi_c_version_needed)
    message(FATAL_ERROR
            "MPI v${MPI_C_VERSION} detected, but at least v${mpi_c_version_needed} is required")
endif()

# find OpenMP support for all enabled languages
find_package(OpenMP REQUIRED)
set(openmp_version_needed 3.0)
if (OpenMP_C_VERSION VERSION_LESS openmp_version_needed)
    message(FATAL_ERROR
            "OpenMP v${OpenMP_C_VERSION} detected, but at least v${openmp_version_needed} is required")
endif()


################################################################################
add_subdirectory(src)

################################################################################
message(STATUS)
message("Project:\n"
        "\tname        = ${PROJECT_NAME}\n"
        "\tdescription = ${PROJECT_DESCRIPTION}\n"
        "\tversion     = ${PROJECT_VERSION}\n"
        "\tsource dir  = ${PROJECT_SOURCE_DIR}\n"
        "\tbinary dir  = ${PROJECT_BINARY_DIR}"
        )

message("Options:\n"
        "\tbfindex = ${ENABLE_BFINDEX} (change by -DENABLE_BFINDEX=<ON|OFF>)\n"
        )

message(STATUS)
message("C build environment:\n"
        "\tinstall prefix = ${CMAKE_INSTALL_PREFIX}\n"
        "\tbuild type     = ${BUILD_TYPE_UPPER}\n"
        "\n"
        "\tcompiler ID      = ${CMAKE_C_COMPILER_ID}\n"
        "\tcompiler version = ${CMAKE_C_COMPILER_VERSION}\n"
        "\tcompiler path    = ${CMAKE_C_COMPILER}\n"
        "\n"
        "\tOpenMP version   = ${OpenMP_C_VERSION}\n"
        "\tOpenMP flags     = ${OpenMP_C_FLAGS}\n"
        "\tOpenMP libraries = ${OpenMP_C_LIB_NAMES}\n"
        )
message(STATUS)
message("MPI:\n"
        "\tversion             = ${MPI_C_VERSION}\n"
        "\tcompiler (not used) = ${MPI_C_COMPILER}\n"
        "\tcompile options     = ${MPI_C_COMPILE_OPTIONS}\n"
        "\tcompile definitions = ${MPI_C_COMPILE_DEFINITIONS}\n"
        "\tinclude dirs        = ${MPI_C_INCLUDE_DIRS}\n"
        "\tlink flags          = ${MPI_C_LINK_FLAGS}\n"
        "\tlibraries           = ${MPI_C_LIBRARIES}\n"
        )

message(STATUS "CMAKE_SYSTEM     = ${CMAKE_SYSTEM}")
