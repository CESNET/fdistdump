#fdistdump binary
bin_PROGRAMS=fdistdump
fdistdump_base=src/main.c \
	       src/master.c src/master.h \
	       src/slave.c src/slave.h \
	       src/common.c src/common.h \
	       src/arg_parse.c src/arg_parse.h \
	       src/path_array.c src/path_array.h \
	       src/output.c src/output.h \
	       src/print.c src/print.h \
	       src/vector.c src/vector.h
fdistdump_file_index=src/file_index/file_index.c src/file_index/file_index.h
fdistdump_clustering=src/clustering/clustering.c src/clustering/clustering.h \
		     src/clustering/metric_space.c src/clustering/metric_space.h \
		     src/clustering/vp_tree.c src/clustering/vp_tree.h
fdistdump_SOURCES=$(fdistdump_base) $(fdistdump_file_index) \
		  $(fdistdump_clustering)

#define variables for tests
TESTS=
check_PROGRAMS=
AM_CPPFLAGS=-I./src
BASIC_TESTS=tests/mpi_init.test \
	    tests/create_mpi_struct.test \
	    tests/arguments.test \
	    tests/mem_init.test \
	    tests/mem_setup.test
ADVANCED_TESTS=tests/advanced/create_test_data.test \
	       tests/advanced/list_flows_basic.test \
	       tests/advanced/list_flows_filter.test \
	       tests/advanced/aggreg_simple.test \
	       tests/advanced/aggreg_filter.test \
	       tests/advanced/aggreg_multi.test \
	       tests/advanced/stats_simple.test \
	       tests/advanced/stats_filter.test \
	       tests/advanced/cleanup_test_data.sh

#basic tests and its binaries
if ENABLE_BASIC_TESTS
TESTS+=$(BASIC_TESTS)
check_PROGRAMS+=test_mpi_init \
		test_create_mpi_struct \
		test_arguments \
		test_mem_init \
		test_mem_setup

test_mpi_init_SOURCES=tests/mpi_init.c \
		      tests/test_common.c tests/test_common.h

test_create_mpi_struct_SOURCES=tests/create_mpi_struct.c \
			       tests/test_common.c tests/test_common.h \
			       src/common.c

test_arguments_SOURCES=tests/arguments.c \
		       tests/test_common.c tests/test_common.h \
		       src/common.c \
		       src/arg_parse.c

test_mem_init_SOURCES=tests/mem_init.c \
		      tests/test_common.c tests/test_common.h

test_mem_setup_SOURCES=tests/mem_setup.c \
		       tests/test_common.c tests/test_common.h \
		       src/common.c \
		       src/arg_parse.c
endif #ENABLE_BASIC_TESTS

#advanced tests and its binaries
if ENABLE_ADVANCED_TESTS
TESTS+=$(ADVANCED_TESTS)
check_PROGRAMS+=test_create_test_data

test_create_test_data_SOURCES=tests/advanced/create_test_data.c
endif #ENABLE_ADVANCED_TESTS


#add some files to the distribution package
##manual page
dist_man_MANS=doc/man/fdistdump.1
##README.md, licence
EXTRA_DIST=README.md LICENSE
##test scripts
EXTRA_DIST+=$(BASIC_TESTS) $(ADVANCED_TESTS)
EXTRA_DIST+=tests/advanced/tests_setup.sh tests/advanced/diff_results.sh
##examples
EXTRA_DIST+=examples
