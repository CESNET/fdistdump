#                                               -*- Autoconf -*-
# process this file with autoconf to produce a configure script

AC_PREREQ([2.68])
AC_INIT([fdistdump], [0.4.0], [wrona@cesnet.cz], [],
        [https://github.com/CESNET/fdistdump])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AC_CONFIG_SRCDIR([tests/test_common.h])
AC_CONFIG_HEADERS([config.h])


# check for programs
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_MAKE_SET

## check for an MPI compiler
AX_PROG_CC_MPI([], [], [AC_MSG_ERROR([no C MPI compiler found])])
AM_PROG_CC_C_O  # required by subdir-objects


################################################################################
# Setup flags for the C compiler and preprocessor.
#
# AX_APPEND_COMPILE_FLAGS: for every flag, it is checked whether the compiler
# works with the flag. If it does, the flag is added to the current language's
# flags (e.g. CFLAGS).

# use only C11 (ISO/IEC 9899:2011) and POSIX 200809L (IEEE 1003.1-2008)
AX_APPEND_COMPILE_FLAGS([-std=c11 -D_POSIX_C_SOURCE=200809L])

# Try to find a compiler option that enables most reasonable warnings. The
# flags are added to the current language's flags (e.g. CFLAGS).
AX_CFLAGS_WARN_ALL
# enable even more warnings
AX_APPEND_COMPILE_FLAGS([-Wextra -Wpedantic])
# uninitialized variables that are initialized with themselves
AX_APPEND_COMPILE_FLAGS([-Winit-self])
# a pointer is cast such that the required alignment of the target is increased
AX_APPEND_COMPILE_FLAGS([-Wcast-align])
# a pointer is cast so as to remove a type qualifier from the target type
AX_APPEND_COMPILE_FLAGS([-Wcast-qual])
# a user-supplied include directory (-I) does not exist
AX_APPEND_COMPILE_FLAGS([-Wmissing-include-dirs])
# a local variable or type declaration shadows something
AX_APPEND_COMPILE_FLAGS([-Wshadow])
# the switch statement related warnings
AX_APPEND_COMPILE_FLAGS([-Wswitch  -Wswitch-bool  -Wswitch-default \
                         -Wswitch-enum -Wswitch-unreachable])
# if floating-point values are used in equality comparisons
AX_APPEND_COMPILE_FLAGS([-Wfloat-equal])
# a value of type "float" is implicitly promoted to "double"
AX_APPEND_COMPILE_FLAGS([-Wdouble-promotion])
# an undefined identifier is evaluated in an "#if" directive
AX_APPEND_COMPILE_FLAGS([-Wundef])
# format warnings, disable buggy Y2K warnings
AX_APPEND_COMPILE_FLAGS([-Wformat=2 -Wno-format-y2k])
# emit code to check for buffer overflows
AX_APPEND_COMPILE_FLAGS([-fstack-protector -fstack-protector-strong])

# check for OpenMP
# AX_OPENMP sets OPENMP_CFLAGS to the C flags needed for supporting OpenMP
AX_OPENMP([], [AC_MSG_ERROR([the current compiler does not suppert OpenMP])])
AX_APPEND_LINK_FLAGS([$OPENMP_CFLAGS])
AX_APPEND_COMPILE_FLAGS([$OPENMP_CFLAGS])
################################################################################

# test for particular system features
## arrange for 64-bit file offsets, known as large-file support (needed for
## GlusterFS on 32bit systems)
AC_SYS_LARGEFILE


# checks for libraries
AC_CHECK_LIB([m], [ceil], [], [AC_MSG_ERROR([cannot find the math library])])

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
echo "  OpenMP  : $OPENMP_CFLAGS"
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
echo -n "  Basic tests   : "; test "$enable_basic_tests" = "yes" && echo "yes" || echo "no"
echo -n "  Advanced tests: "; test "$enable_advanced_tests" = "yes" && echo "yes" || echo "no"
true  # return success
