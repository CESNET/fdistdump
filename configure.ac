#intialization
AC_INIT([fdistdump], [0.1], [], [], [https://github.com/CESNET/fdistdump])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])

#check for compiler
AC_PROG_CC([mpicc mpic++ mpicxx mpiCC])
AC_PROG_CC_STDC
CFLAGS="$CFLAGS -pedantic -W -Wall -Wextra -Winit-self -Wcast-align \
-Wcast-qual -Wmissing-include-dirs -Wshadow"

#check for libraries
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

#output
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile src/Makefile tests/Makefile])
AC_OUTPUT
