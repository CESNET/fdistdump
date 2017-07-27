#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script

AC_PREREQ([2.68])
AC_INIT([fdistdump], [0.2.2], [wrona@cesnet.cz], [],
        [https://github.com/CESNET/fdistdump])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AC_CONFIG_SRCDIR([tests/test_common.h])
AC_CONFIG_HEADERS([config.h])


# Checks for programs
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_MAKE_SET

## Use only C11 (ISO/IEC 9899:2011) and POSIX 200809L (IEEE 1003.1-2008)
CFLAGS="-std=c11 -D_POSIX_C_SOURCE=200809L"
CFLAGS="$CFLAGS -g -O3"

## Checks for MPI compiler
AC_PROG_CC([mpicc mpic++ mpicxx mpiCC])
AM_PROG_CC_C_O  # required by subdir-objects
CFLAGS="$CFLAGS -Wall -Wextra -Wpedantic -Winit-self -Wcast-align -Wcast-qual \
-Wmissing-include-dirs -Wshadow"

## Check OpenMP, add flags
AC_OPENMP
CFLAGS="$CFLAGS $OPENMP_CFLAGS"  # add to compile flags
CPPFLAGS="$CPPFLAGS $OPENMP_CFLAGS"  # add to preprocessor flags


# Test for particular system features
## arrange for 64-bit file offsets, known as large-file support
## needed for GlusterFS on 32bit systems
AC_SYS_LARGEFILE


# Checks for libraries
## libnf
AC_CHECK_LIB([nf], [lnf_open], [], [AC_MSG_ERROR([cannot find the libnf \
library (see https://github.com/VUTBR/libnf)])])
AC_CHECK_HEADER([libnf.h], [], [AC_MSG_ERROR([cannot find the libnf.h header \
file])])
## MPI
## check version 1
AC_SEARCH_LIBS([MPI_Send], [mpi], [],
               [AC_MSG_ERROR([need at least MPI-2.0 implementation])])
## check version 2
AC_SEARCH_LIBS([MPI_Put], [mpi], [],
               [AC_MSG_ERROR([need at least MPI-2.0 implementation])])
## check version 3
AC_SEARCH_LIBS([MPI_Ibarrier], [mpi]) #version 3 not needed

## --disable-bfindex
AC_ARG_ENABLE([bfindex], AS_HELP_STRING([--disable-bfindex], [Disable support \
              for the Bloom filter indexing library]))
AS_IF([test "$enable_bfindex" != "no"], [
      AC_CHECK_LIB([bfindex], [bfi_init_index], [],
                   [AC_MSG_ERROR([cannot find the bfindex library (see \
https://github.com/CESNET/bloom-filter-index)])])
      AC_CHECK_HEADERS([bf_index.h], [],
                       [AC_MSG_ERROR([cannot find the bf_index.h header file])])
])
AM_CONDITIONAL([ENABLE_BFINDEX], [test "$enable_bfindex" != "no"])


# Checks for header files
AC_CHECK_HEADERS([arpa/inet.h inttypes.h limits.h stddef.h stdlib.h string.h \
                  sys/socket.h unistd.h])


# Checks for typedefs, structures, and compiler characteristics
AC_CHECK_HEADER_STDBOOL
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T


# Checks for library functions
# AC_FUNC_MALLOC  # rpl_malloc problem
# AC_FUNC_REALLOC # rpl_malloc problem
AC_FUNC_MKTIME
AC_CHECK_FUNCS([localtime_r memset setenv strchr strdup strerror strtol \
                strtoull tzset])


# Autoconf config files
AC_CONFIG_FILES([Makefile])


# Define arguments
## --enable-basic-tests
AC_ARG_ENABLE([basic-tests],
              AS_HELP_STRING([--enable-basic-tests],
                             [Enable basic testing modules]))
AM_CONDITIONAL([ENABLE_BASIC_TESTS], [test "$enable_basic_tests" = "yes"])

## --enable-advanced-tests
AC_ARG_ENABLE([advanced-tests],
              AS_HELP_STRING([--enable-advanced-tests],
                             [Enable advanced testing modules]))
AS_IF([test "$enable_advanced_tests" = "yes"], [
       AC_PROG_SED
       AC_CHECK_PROG(CUT_CHECK, cut, yes)
       if test "$CUT_CHECK" != "yes"
       then
               AC_MSG_ERROR([Please install cut before running advanced tests.])
       fi
       AC_CHECK_PROG(NFDUMP_CHECK, nfdump, yes)
       if test "$NFDUMP_CHECK" != "yes"
       then
               AC_MSG_ERROR([Please install nfdump before running advanced \
tests.])
       fi
       ])
AM_CONDITIONAL([ENABLE_ADVANCED_TESTS],
               [test "$enable_advanced_tests" = "yes"])


# Write output
AC_OUTPUT


# Print summary
echo
echo "-------------------------------------------------------------------------"
echo "$PACKAGE $VERSION"
echo
echo "Compilation:"
# compiler
echo "  CC      : $CC"
# extra flags to give to the C preprocessor and programs that use it
echo "  CPPFLAGS: $CPPFLAGS"
# extra flags to give to the C compiler
echo "  CFLAGS  : $CFLAGS"
# extra flags to give to the C++ compiler
echo "  CXXFLAGS: $CXXFLAGS"
# extra flags to give to compilers when they are supposed to invoke the linker
# (such as -L)
echo "  LDFLAGS : $LDFLAGS"
# library flags or names given to compilers when they are supposed to invoke the
# linker
echo "  LDLIBS  : $LDLIBS"
# automake uses $LIBS instead of LDLIBS
echo "  LIBS    : $LIBS"
echo
echo "Libraries:"
echo -n "  libnf  : "; test "$enable_libnf" != "no" && echo "yes" || echo "no"
echo -n "  bfindex: "; test "$enable_bfindex" != "no" && echo "yes" || echo "no"
echo
echo "Optional features:"
echo -n "  OpenMP        : "; test "$enable_openmp" != "no" && echo "yes ($OPENMP_CFLAGS)" || echo "no"
echo -n "  Basic tests   : "; test "$enable_basic_tests" = "yes" && echo "yes" || echo "no"
echo -n "  Advanced tests: "; test "$enable_advanced_tests" = "yes" && echo "yes" || echo "no"
true  # return success
