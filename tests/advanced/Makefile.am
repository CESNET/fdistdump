TESTS=create_testing_data.test list_flows_basic.test list_flows_filter.test aggreg_simple.test aggreg_filter.test aggreg_multi.test stats_simple.test stats_filter.test

check_PROGRAMS=create_test_data
bin_PROGRAMS=create_test_data

create_test_data_SOURCES=create_test_data.c
create_test_data_CPPFLAGS=$(COM_CPPFLAGS)

clean-local:
	rm -f create_test_data
	rm -f *.o

check-hook:
	./cleanup_test_data.sh

check: check-hook