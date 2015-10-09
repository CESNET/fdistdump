if ENABLE_ADV_TESTS
if !SKIP_ADVANCED_TESTS
SUBDIRS=advanced
endif
endif

TESTS=mpi_init.test create_mpi_struct.test arguments.test mem_init.test mem_setup.test

#mem_store_print.test
#EXTRA_DIST =

check_PROGRAMS=mpi_init create_mpi_struct arguments mem_init mem_setup
#mem_store_print

bin_PROGRAMS=mpi_init create_mpi_struct arguments mem_init mem_setup
#mem_store_print

#AM_LDFLAGS=-lnf
COM_CPPFLAGS=-I../src


mpi_init_SOURCES=mpi_init.c test_common.c
mpi_init_CPPFLAGS=$(COM_CPPFLAGS)

create_mpi_struct_SOURCES=create_mpi_struct.c test_common.c ../src/common.c
create_mpi_struct_CPPFLAGS=$(COM_CPPFLAGS)

arguments_SOURCES=arguments.c test_common.c ../src/common.c \
                  ../src/arg_parse.c
arguments_CPPFLAGS=$(COM_CPPFLAGS)

mem_init_SOURCES=mem_init.c test_common.c
mem_init_CPPFLAGS=$(COM_CPPFLAGS)

mem_setup_SOURCES=mem_setup.c test_common.c ../src/common.c \
                  ../src/arg_parse.c
mem_setup_CPPFLAGS=$(COM_CPPFLAGS)

#mem_store_print_SOURCES=mem_store_print.c test_common.c ../src/common.c \
#                  ../src/arg_parse.c
#mem_store_print_CPPFLAGS=$(COM_CPPFLAGS)


clean-local:
	rm -f mpi_init create_mpi_struct arguments mem_init mem_setup
	rm -f *.o