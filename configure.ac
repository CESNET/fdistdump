#initialization
AC_INIT([fdistdump], [0.1], [], [], [https://github.com/CESNET/fdistdump])
AM_INIT_AUTOMAKE([foreign -Wall -Werror subdir-objects])
AC_CONFIG_HEADERS([config.h])

#create Makefiles
AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile tests/advanced/Makefile])

#check for compiler
AC_PROG_CC([mpicc mpic++ mpicxx mpiCC])
AC_PROG_CC_STDC
CFLAGS="$CFLAGS -pedantic -W -Wall -Wextra -Winit-self -Wcast-align \
-Wcast-qual -Wmissing-include-dirs -Wshadow"
AM_PROG_CC_C_O

#checks for programs
AC_PROG_INSTALL

#checks for libraries
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

#checks for header files
AC_CHECK_HEADERS([arpa/inet.h assert.h dirent.h errno.h getopt.h inttypes.h \
                  libnf.h limits.h mpi.h omp.h stdarg.h stdbool.h stddef.h \
                  stdio.h stdlib.h string.h sys/stat.h time.h unistd.h])


#checks for typedefs, structures, and compiler characteristics
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_TYPE_UINT8_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T

#checks for library functions
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_FUNC_MKTIME

#define arguments
AC_ARG_ENABLE([tests], AC_HELP_STRING([--disable-tests],
              [enable testing modules (enabled by default)]),
        [AM_CONDITIONAL([ENABLE_TESTS], [test "x$enableval" = xyes])],
        [AM_CONDITIONAL([ENABLE_TESTS], [true])])

AC_ARG_ENABLE([advanced-tests], AC_HELP_STRING([--enable-advanced-tests],
              [enable advanced testsing modules, requires --enable-tests (disabled by default)]),
        [AM_CONDITIONAL([ENABLE_ADV_TESTS], [test "x$enableval" = xyes])],
        [AM_CONDITIONAL([ENABLE_ADV_TESTS], [false])])

#check if we can run advanced tests
if test -z "$ENABLE_ADV_TESTS_TRUE";then
        AC_PROG_SED
        if test -z "$SED"; then
                AC_MSG_WARN(["sed" is not installed (needed by advanced tests)])
                if test ! -z "$__SKIP_ADVANCED"; then
                        __SKIP_ADVANCED+="\" and \""
                fi
                __SKIP_ADVANCED+="sed"
        fi

        AC_PROG_AWK
        if test -z "$AWK"; then
                AC_MSG_WARN(["awk" is not installed (needed by advanced tests)])
                if test ! -z "$__SKIP_ADVANCED"; then
                        __SKIP_ADVANCED+="\" and \""
                fi
                __SKIP_ADVANCED+="awk"
        fi

        AC_CHECK_PROGS([CUT], [cut], [])
        if test -z "$CUT"; then
                AC_MSG_WARN(["cut" is not installed (needed by advanced tests)])
                if test ! -z "$__SKIP_ADVANCED"; then
                        __SKIP_ADVANCED+="\" and \""
                fi
                __SKIP_ADVANCED+="cut"
        fi

        AC_CHECK_PROGS([NFD], [nfdump], [])
        if test -z "$NFD"; then
                AC_MSG_WARN(["nfdump" is not installed (needed by advanced tests)])
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
echo "Compilation............: make (or gmake)"
echo "  CPPFLAGS.............: $CPPFLAGS"
echo "  CFLAGS...............: $CFLAGS"
echo "  CXXFLAGS.............: $CXXFLAGS"
echo "  LDFLAGS..............: $LDFLAGS"
echo
echo "Installation...........: make install (as root if needed, with 'su' or 'sudo')"
echo "  prefix...............: $prefix"
echo
echo "Tests..................: `if [[ -z "$ENABLE_TESTS_TRUE" ]]; then \
        echo enabled; else echo disabled; fi`"

if test -z "$ENABLE_ADV_TESTS_TRUE"
then
        ADV_STR="enabled"
else
        ADV_STR="disabled"

        if test ! -z "$__SKIP_ADVANCED"
        then
                ADV_STR+=" (missing "
                ADV_STR+=$__SKIP_ADVANCED
                ADV_STR+=")"
        fi
fi
echo "Advanced tests.........: $ADV_STR"
echo
