#fdistdump binary
bin_PROGRAMS=fdistdump
fdistdump_SOURCES=src/main.c \
		  src/master.c src/master.h \
		  src/slave.c src/slave.h \
		  src/common.c src/common.h \
		  src/arg_parse.c src/arg_parse.h \
		  src/flookup.c src/flookup.h \
		  src/output.c src/output.h


#define variables for tests
TESTS=
check_PROGRAMS=
COM_CPPFLAGS=-I./src

#basic tests and its binaries
if ENABLE_BASIC_TESTS
TESTS+=tests/mpi_init.test \
      tests/create_mpi_struct.test \
      tests/arguments.test \
      tests/mem_init.test \
      tests/mem_setup.test

check_PROGRAMS+=test_mpi_init \
	       test_create_mpi_struct \
	       test_arguments \
	       test_mem_init \
	       test_mem_setup

test_mpi_init_SOURCES=tests/mpi_init.c \
		      tests/test_common.c
test_mpi_init_CPPFLAGS=$(COM_CPPFLAGS)

test_create_mpi_struct_SOURCES=tests/create_mpi_struct.c \
			       tests/test_common.c \
			       src/common.c
test_create_mpi_struct_CPPFLAGS=$(COM_CPPFLAGS)

test_arguments_SOURCES=tests/arguments.c \
		       tests/test_common.c \
		       src/common.c \
		       src/arg_parse.c
test_arguments_CPPFLAGS=$(COM_CPPFLAGS)

test_mem_init_SOURCES=tests/mem_init.c \
		      tests/test_common.c
test_mem_init_CPPFLAGS=$(COM_CPPFLAGS)

test_mem_setup_SOURCES=tests/mem_setup.c \
		       tests/test_common.c \
		       src/common.c \
		       src/arg_parse.c
test_mem_setup_CPPFLAGS=$(COM_CPPFLAGS)
endif #ENABLE_BASIC_TESTS

#advanced tests and its binaries
if ENABLE_ADVANCED_TESTS
TESTS+=tests/advanced/create_test_data.test \
      tests/advanced/list_flows_basic.test \
      tests/advanced/list_flows_filter.test \
      tests/advanced/aggreg_simple.test \
      tests/advanced/aggreg_filter.test \
      tests/advanced/aggreg_multi.test \
      tests/advanced/stats_simple.test \
      tests/advanced/stats_filter.test \
      tests/advanced/cleanup_test_data.sh

check_PROGRAMS+=test_create_test_data

test_create_test_data_SOURCES=tests/advanced/create_test_data.c
test_create_test_data_CPPFLAGS=$(COM_CPPFLAGS)
endif #ENABLE_ADVANCED_TESTS
