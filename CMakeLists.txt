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


# set the minimum required version of cmake
cmake_minimum_required(VERSION 3.6)

################################################################################
# setup a the project (DESCRIPTION directive is not supported until 3.9)
project(fdistdump
        VERSION 0.4.1
        LANGUAGES C)
set(PROJECT_DESCRIPTION
    "a tool to query IP flow records on a distributed system")
set(PROJECT_DESCRIPTION_LONG "\
Fdistdump is a fast, scalable, distributed tool to query Internet Protocol \
flow record files. The master/slave communication model allows it to run jobs \
on unlimited number of nodes, including a single local node. Message Passing \
Interface (MPI) is used as an underlying communication protocol. Its basic \
features include listing, filtering, sorting, and aggregating records. \
Fdistdump also implements algorithm to quickly answer Top-N queries (e.g., \
find the N objects with the highest aggregate values) on a distributed system.\
")

##### includes (load and run CMake code from a file or module)
# provides a macro to check if a header file can be included in C
include(CheckIncludeFile)
# define GNU standard installation directories (vars later passed to install())
include(GNUInstallDirs)

# common functions
include(cmake/functions.cmake)

##### options that the user can optionally select by e.g., -D option=<ON|OFF>
# -D ENABLE_BFINDEX=<ON|OFF>, defaults to ON
option(ENABLE_BFINDEX
       "Enable/disable support for the Bloom filter indexing library" ON)
# -D EXECUTABLE_SUFFIX:STRING=<suffix>, defaults to empty string
set(EXECUTABLE_SUFFIX CACHE STRING "Set the executable suffix.")

# include-what-you-use: https://include-what-you-use.org/
option(USE_IWYU "Use/don't use the include-what-you-use tool." OFF)
if(USE_IWYU)
    find_program(IWYU_PATH NAMES iwyu include-what-you-use)
    if(IWYU_PATH STREQUAL "IWYU_PATH-NOTFOUND")
        message(WARNING "include-what-you-use executable not found, disabling")
    else()
        message(STATUS "include-what-you-use executable found, enabling")
        set(CMAKE_C_INCLUDE_WHAT_YOU_USE "${IWYU_PATH}" "-Xiwyu"
            "--mapping_file=${CMAKE_SOURCE_DIR}/extras/iwyu_mapping.json")
    endif(IWYU_PATH STREQUAL "IWYU_PATH-NOTFOUND")
endif(USE_IWYU)

# clang-tidy: http://clang.llvm.org/extra/clang-tidy/
option(USE_CLANG_TIDY "Use/don't use the clang-tidy tool." OFF)
if(USE_CLANG_TIDY)
    find_program(CLANG_TIDY_PATH clang-tidy)
    if(CLANG_TIDY_PATH STREQUAL "CLANG_TIDY_PATH-NOTFOUND")
        message(WARNING "clang-tidy executable not found, disabling")
    else()
        message(STATUS "clang-tidy executable found, enabling")
        set(CMAKE_C_CLANG_TIDY "${CLANG_TIDY_PATH}")
    endif(CLANG_TIDY_PATH STREQUAL "CLANG_TIDY_PATH-NOTFOUND")
endif(USE_CLANG_TIDY)

##### various project settings and checks
# always use relative path for install() DESTINATIONs
set(CMAKE_ERROR_ON_ABSOLUTE_INSTALL_DESTINATION ON)

# check the current system/platform
if(NOT UNIX)
    message(FATAL_ERROR
            "only UNIX-like OS's (including Apple OS X and CygWin) are supported")
endif()

# select build type by -D CMAKE_BUILD_TYPE=<Debug|Release|RelWithDebInfo|MinSizeRel>
# single-configuration generators are Makefile Generators, Ninja, ...
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
string(TOUPPER ${CMAKE_BUILD_TYPE} BUILD_TYPE_UPPER)

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
check_and_add_C_compile_options("${basic_warning_options}")

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
check_and_add_C_compile_options("${advanced_warning_options}")


################################################################################
# find a Message Passing Interface (MPI) implementation for all enabled languag.
find_package(MPI 2.0 REQUIRED)

# find OpenMP support for all enabled languages
find_package(OpenMP 3.0 REQUIRED)


################################################################################
add_subdirectory(src)
add_subdirectory(doc)
add_subdirectory(pkg)

################################################################################
# generate instalation rules for the license file
install(FILES COPYING
        DESTINATION "${CMAKE_INSTALL_DOCDIR}"
        COMPONENT "documentation")

################################################################################
# finally print usefull informations
message("Project:\n"
        "\tname        = ${PROJECT_NAME}\n"
        "\tdescription = ${PROJECT_DESCRIPTION}\n"
        "\tversion     = ${PROJECT_VERSION}\n"
        "\tsource dir  = ${PROJECT_SOURCE_DIR}\n"
        "\tbinary dir  = ${PROJECT_BINARY_DIR}\n"
        )
message("Options:\n"
        "\tbfindex           = ${ENABLE_BFINDEX} (set by -D ENABLE_BFINDEX=<ON|OFF>)\n"
        "\texecutable suffix = ${EXECUTABLE_SUFFIX} (set by -D EXECUTABLE_SUFFIX:STRING=<suffix>)\n"
        )
message("Installation directories:\n"
        "\tprefix = ${CMAKE_INSTALL_PREFIX} (set by -D CMAKE_INSTALL_PREFIX:PATH=<path>)\n"
        "\tbin    = ${CMAKE_INSTALL_FULL_BINDIR}\n"
        "\tman    = ${CMAKE_INSTALL_FULL_MANDIR}\n"
        "\tdoc    = ${CMAKE_INSTALL_FULL_DOCDIR}\n"
        )
message("C build environment:\n"
        "\tcompiler ID      = ${CMAKE_C_COMPILER_ID}\n"
        "\tcompiler version = ${CMAKE_C_COMPILER_VERSION}\n"
        "\tcompiler path    = ${CMAKE_C_COMPILER}\n"
        "\n"
        "\tbuild type         = ${CMAKE_BUILD_TYPE}\n"
        "\tgeneral options    = ${CMAKE_C_FLAGS}\n"
        "\tbuild type options = ${CMAKE_C_FLAGS_${BUILD_TYPE_UPPER}}\n"
        "\n"
        "\tOpenMP version   = ${OpenMP_C_VERSION}\n"  # since 3.9
        "\tOpenMP options   = ${OpenMP_C_FLAGS}\n"
        "\tOpenMP libraries = ${OpenMP_C_LIB_NAMES}\n"  # since 3.9
        "\n"
        "\tIWYU             = ${USE_IWYU}\n"
        "\tIWYU cmd         = ${CMAKE_C_INCLUDE_WHAT_YOU_USE}\n"
        "\n"
        "\tclang-tidy       = ${USE_CLANG_TIDY}\n"
        "\tclang-tidy cmd   = ${CMAKE_C_CLANG_TIDY}\n"
        )
if(CMAKE_VERSION VERSION_LESS 3.10)
    # the FindMPI module was improved in CMake 3.10
    message("MPI:\n"
            "\tcompiler (not used) = ${MPI_C_COMPILER}\n"
            "\tcompile options     = ${MPI_C_COMPILE_FLAGS}\n"
            "\tinclude path        = ${MPI_C_INCLUDE_PATH}\n"
            "\tlinker options      = ${MPI_C_LINK_FLAGS}\n"
            "\tlibraries           = ${MPI_C_LIBRARIES}\n"
            )
else()
    message("MPI:\n"
            "\tversion             = ${MPI_C_VERSION}\n"
            "\tcompiler (not used) = ${MPI_C_COMPILER}\n"
            "\tcompile options     = ${MPI_C_COMPILE_OPTIONS}\n"
            "\tcompile definitions = ${MPI_C_COMPILE_DEFINITIONS}\n"
            "\tinclude dirs        = ${MPI_C_INCLUDE_DIRS}\n"
            "\tlinker options      = ${MPI_C_LINK_FLAGS}\n"
            "\tlibraries           = ${MPI_C_LIB_NAMES}\n"
            )
endif(CMAKE_VERSION VERSION_LESS 3.10)
message("Various:\n"
        "\tCMake version  = ${CMAKE_VERSION}\n"
        "\tsystem name    = ${CMAKE_SYSTEM_NAME}\n"
        "\tsystem version = ${CMAKE_SYSTEM_VERSION}\n"
        )
