#                                               -*- Autoconf -*-
# process this file with autoconf to produce a configure script

AC_PREREQ([2.68])
AC_INIT([fdistdump], [0.2.2], [wrona@cesnet.cz], [],
        [https://github.com/CESNET/fdistdump])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AC_CONFIG_SRCDIR([tests/test_common.h])
AC_CONFIG_HEADERS([config.h])


# check for programs
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_MAKE_SET

## check for an MPI compiler
AC_PROG_CC([mpicc mpic++ mpicxx mpiCC])
AM_PROG_CC_C_O  # required by subdir-objects

## check for OpenMP (after compiler is known)
AC_OPENMP  # sets OPENMP_CFLAGS to the C flags needed for supporting OpenMP


# setup flags for the C compiler and preprocessor
C_STD="-std=c11 -D_POSIX_C_SOURCE=200809L"  # use only C11 (ISO/IEC 9899:2011)
                                            # and POSIX 200809L (IEEE 1003.1-2008)
C_WARN="-Wall -Wextra -Wpedantic"
C_WARN+=" -Wfatal-errors"  # abort on the first error
C_WARN+=" -Winit-self"  # uninitialized variables that are initialized with
                        # themselves
C_WARN+=" -Wcast-align"  # a pointer is cast such that the required
                         # alignment of the target is increased
C_WARN+=" -Wcast-qual"  # a pointer is cast so as to remove a type qualifier
                        # from the target type
C_WARN+=" -Wmissing-include-dirs"  # a user-supplied include directory (-I) does
                                   # not exist
C_WARN+=" -Wshadow"  # a local variable or type declaration shadows something
C_WARN+=" -Wdouble-promotion"  # a value of type "float" is implicitly promoted
                               # to "double"
C_WARN+=" -Wswitch -Wswitch-default -Wswitch-enum"  # switch related warnings
C_WARN+=" -Wfloat-equal"  # if floating-point values are used in equality
                          # comparisons
C_WARN+=" -Wundef"  # an undefined identifier is evaluated in an "#if" directive
C_WARN+=" -Wformat=2 -Wno-format-y2k"  # disable buggy Y2K warnings

C_OTHER="-fstack-protector-strong"  # emit code to check for buffer overflows

## construct preprocessor/compiler flags using the new custom flags and the
## original flags (original are last to eventually overwrite the custom flags)
CPPFLAGS="$OPENMP_CFLAGS $CPPFLAGS"
CFLAGS="$C_STD $C_WARN $C_OTHER $OPENMP_CFLAGS $CFLAGS"

# test for particular system features
## arrange for 64-bit file offsets, known as large-file support (needed for
## GlusterFS on 32bit systems)
AC_SYS_LARGEFILE


# checks for libraries
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


# checks for header files
AC_CHECK_HEADERS([arpa/inet.h inttypes.h limits.h stddef.h stdlib.h string.h \
                  sys/socket.h unistd.h])


# checks for typedefs, structures, and compiler characteristics
AC_CHECK_HEADER_STDBOOL
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T


# checks for library functions
# AC_FUNC_MALLOC  # rpl_malloc problem
# AC_FUNC_REALLOC # rpl_malloc problem
AC_FUNC_MKTIME
AC_CHECK_FUNCS([localtime_r memset setenv strchr strdup strerror strtol \
                strtoull tzset])


# autoconf config files
AC_CONFIG_FILES([Makefile])


# define arguments
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


# write output
AC_OUTPUT


# Print summary
echo
echo "-------------------------------------------------------------------------"
echo "$PACKAGE $VERSION"
echo
echo "Build environment:"
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
echo "MPI environment:"
echo "  MPI_COMPILER: $MPI_COMPILER"
echo "  MPI_HOME    : $MPI_HOME"
echo "  MPI_INCLUDE : $MPI_INCLUDE"
echo "  MPI_SUFFIX  : $MPI_SUFFIX"
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
