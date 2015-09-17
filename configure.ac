AC_PREREQ([2.69])
AC_INIT([fdistdump], [0.1], [], [], [https://github.com/CESNET/fdistdump])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AC_CONFIG_SRCDIR([config.h.in])
AC_CONFIG_HEADERS([config.h])

#check for compiler
AC_PROG_CC([mpicc mpic++ mpicxx mpiCC])
AC_PROG_CC_STDC
CFLAGS="$CFLAGS -pedantic -W -Wall -Wextra -Winit-self -Wcast-align \
-Wcast-qual -Wmissing-include-dirs -Wshadow"
AM_PROG_CC_C_O

# Checks for programs.
AC_PROG_AWK
AC_PROG_INSTALL
AC_PROG_MAKE_SET

# Checks for libraries.
AC_SEARCH_LIBS([lnf_open], [nf], [], [AC_MSG_ERROR([unable to find the libnf \
                library])])

#check MPI version
AC_SEARCH_LIBS([MPI_Send], [mpi], [],
               [AC_MSG_ERROR([need MPI version >= 2.2])]) #version 1 needed
AC_SEARCH_LIBS([MPI_Put], [mpi], [],
               [AC_MSG_ERROR([need MPI version >= 2.2])]) #version 2 nedded
AC_SEARCH_LIBS([MPI_Ibarrier], [mpi]) #version 3 not needed

#check OpenMP, add flags
AC_OPENMP
CFLAGS="$CFLAGS $OPENMP_CFLAGS"
CPPFLAGS="$CPPFLAGS $OPENMP_CFLAGS"

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h inttypes.h limits.h stddef.h stdlib.h string.h sys/socket.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_C_INLINE
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MKTIME
AC_FUNC_REALLOC
AC_CHECK_FUNCS([localtime_r memset setenv strdup strerror strtol strtoull tzset])

AC_CONFIG_FILES([Makefile
                 src/Makefile
                 tests/Makefile
                 tests/advanced/Makefile])
AC_ARG_ENABLE([tests], AC_HELP_STRING([--disable-tests],[Compile with testing modules]),
        [AM_CONDITIONAL([ENABLE_TESTS], [test "x$enableval" = xyes])],
        [AM_CONDITIONAL([ENABLE_TESTS], [true])])

AC_ARG_ENABLE([advanced-tests], AC_HELP_STRING([--enable-advanced-tests], [Run advanced tests (--enable-tests must be used for any effect)]),
        [AM_CONDITIONAL([ENABLE_ADV_TESTS], [test "x$enableval" = xyes])],
        [AM_CONDITIONAL([ENABLE_ADV_TESTS], [false])])

if test -z "$ENABLE_ADV_TESTS_TRUE";then
        AC_PROG_SED
        if test -z "$SED"; then
                AC_MSG_WARN(["sed" is not installed (needed by advanced tests).])
                if test ! -z "$__SKIP_ADVANCED"; then
                        __SKIP_ADVANCED+="\" and \""
                fi
                __SKIP_ADVANCED+="sed"
        fi

        AC_PROG_AWK
        if test -z "$AWK"; then
                AC_MSG_WARN(["awk" is not installed (needed by advanced tests).])
                if test ! -z "$__SKIP_ADVANCED"; then
                        __SKIP_ADVANCED+="\" and \""
                fi
                __SKIP_ADVANCED+="awk"
        fi

        AC_CHECK_PROGS([CUT], [cut], [])
        if test -z "$CUT"; then
                AC_MSG_WARN(["cut" is not installed (needed by advanced tests).])
                if test ! -z "$__SKIP_ADVANCED"; then
                        __SKIP_ADVANCED+="\" and \""
                fi
                __SKIP_ADVANCED+="cut"
        fi

        AC_CHECK_PROGS([NFD], [nfdump], [])
        if test -z "$NFD"; then
                AC_MSG_WARN(["nfdump" is not installed (needed by advanced tests).])
                if test ! -z "$__SKIP_ADVANCED"; then
                        __SKIP_ADVANCED+="\" and \""
                fi
                __SKIP_ADVANCED+="nfdump"
        fi
fi

AM_CONDITIONAL([SKIP_ADVANCED_TESTS], [test -z "$ENABLE_ADV_TESTS_TRUE" && test ! -z "$__SKIP_ADVANCED"])


AC_OUTPUT

### Print summary: -------------------------------------------------------------
echo
echo
echo "------------------------------------------------------------------------"
echo "$PACKAGE $VERSION $GIT_VERSION"
echo "------------------------------------------------------------------------"
echo
echo
echo "Configuration Options Summary:"
echo
#echo "  ASM.(32 bit only)..: $ASM"
#echo "  Static binary......: $static"
#echo
#echo "Documentation..........: `if test "x$DX_FLAG_doc" = x1; then echo enabled; else echo disabled; fi`"
#echo
echo "Compilation............: make (or gmake)"
echo "  CPPFLAGS.............: $CPPFLAGS"
echo "  CFLAGS...............: $CFLAGS"
echo "  CXXFLAGS.............: $CXXFLAGS"
echo "  LDFLAGS..............: $LDFLAGS"
echo
echo "Installation...........: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............: $prefix"
echo
echo "Tests..................: `if [[ -z "$ENABLE_TESTS_TRUE" ]]; then echo enabled; else echo disabled; fi`"
echo "Advanced tests.........: `if [[ -z "$ENABLE_ADV_TESTS_TRUE" ]]; then echo enabled; else echo disabled; fi`"
if test -z "$ENABLE_ADV_TESTS_TRUE";then
        if test ! -z "$__SKIP_ADVANCED"; then
                echo "  Advanced tests will be skipped since \"$__SKIP_ADVANCED\" is not installed."
        fi
fi
echo
