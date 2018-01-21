# fdistdump - distributed IP flow files processing tool

| Branch  | Travis CI | Coverity |
|---------|-----------|----------|
| master  | [![Build Status](https://travis-ci.org/CESNET/fdistdump.svg?branch=master)](https://travis-ci.org/CESNET/fdistdump) | [![Coverity Scan Status](https://scan.coverity.com/projects/5969/badge.svg)](https://scan.coverity.com/projects/5969 "Coverity Scan Status") |
| develop | [![Build Status](https://travis-ci.org/CESNET/fdistdump.svg?branch=develop)](https://travis-ci.org/CESNET/fdistdump) | [![Coverity Scan Status](https://scan.coverity.com/projects/5969/badge.svg)](https://scan.coverity.com/projects/5969 "Coverity Scan Status") |

fdistdump is a fast, scalable and distributed tool to query Internet Protocol
flow files. The master/slave communication model allows it to run jobs on
unlimited number of nodes, including a single local node. Among other features,
fdistdump allows you to list, sort and aggregate records with the possibility
to apply powerful record filter.

It is written in the C language and Message Passing Interface (MPI) is used as
an underlying communication protocol. It is currently developed on Linux, other
operating systems are not tested.

## Prerequisites
* [libnf](https://github.com/VUTBR/libnf "libnf GitHub page")
\- C interface for processing nfdump files.
* [MPI](http://www.mpi-forum.org/ "Message Passing Interface Forum")
\- Message Passing Interface. fdistdump requires implementation supporting
standard MPI-2.0 or newer (Open MPI, MPICH2, MPICH3, MVAPICH2, Intel® MPI
Library, ...).
* [bloom-filter-index](https://github.com/CESNET/bloom-filter-index "bloom-filter-index GitHub page")
(optional) - A library for indexing IP addresses in flow records using Bloom
filters. To disable, use `--disable-bfindex` option for the configure script.

## Building and installing
fdistdump is built with GNU build system (Autotools). You can obtain source
code either from a git repository or from a package.

### From the git
```Shell
git clone https://github.com/CESNET/fdistdump.git
cd fdistdump/
autoreconf -i
./configure
make
sudo make install
```

### From the tarball
```Shell
./configure
make
sudo make install
```

### From the repository
We are using Copr (a Fedora community build service) to create our repositories
for several distributions and architectures. Check out the
[CESNET group page](https://copr.fedorainfracloud.org/groups/g/CESNET/coprs/).
To install fdistdump, you will need an
[@CESNET/fdistdump repository](https://copr.fedorainfracloud.org/coprs/g/CESNET/fdistdump/)
and a
[@CESNET/NEMEA repository](https://copr.fedorainfracloud.org/coprs/g/CESNET/NEMEA/),
which contains some dependencies (libnf and bloom-filter-index libraries).

```Shell
yum install yum-plugin-copr
yum copr enable @CESNET/fdistdump
yum copr enable @CESNET/NEMEA
yum install fdistdump-mpich     # version compiled against MPICH
yum install fdistdump-openmpi   # version compiled against Open MPI
module load mpi/mpich-x86_64    # load environment for the MPICH version
module load mpi/openmpi-x86_64  # load environment for the Open MPI version
```

### Building tests
Several tests are included in the tests directory. Test run will query the same
data using both fdistdump and nfdump, results should match. Therefore, nfdump
is required to be installed to run tests.
```Shell
./configure --enable-basic-tests --enable-advanced-tests
make
make check
```

## Running
fdistdump is an MPI application and is necessary to launch it using one of
mpiexec, mpirun, orterun or however your MPI implementation calls the process
manager. Before running fdistdump, please make sure that your machine (or
cluster) is configured correctly and ready to launch applications using MPI.

### Prepare MPI
This can be tested for example by starting program *hostname* using MPI
launcher. Following should launch two instances of *hostname* on the local
node, therefore local node's DNS name should be printed twice:
```Shell
mpiexec -n 2 hostname
```

Following should launch *hostname* on every specified node. Therefore DNS name
of every specified node should be printed to your local console:
```Shell
mpiexec -host node1,node2,... hostname
```

If there was a problem running these commands, there is probably a problem with
configuration of your cluster (network settings, firewall rules, MPI
configuration, ...). For more information about running MPI jobs see
documentation for your process manager, e.g. man mpiexec.

### Run your first query
MPI allows you to to specify how many processes will be launched on each node.
The first process will always be in the role of master, remaining processes
will be the slaves. It is mandatory to have exactly one master and at least one
slave process, otherwise job won't start. Master process doesn't read any data,
so you can launch it on a dedicated node (better option) or it can share a node
with slave process.

To run fdistdump on a **single node**, you should launch two processes. First
one, the master, is necessary to manage the slave, print progress and results.
The second one, the slave, is there to do all the hard work. It will read and
process all the flow files and send the results back to the master. Following
example command will print statistic about the source ports in flow_file:
```Shell
mpiexec -n 2 fdistdump -s srcport flow_file
```

To run fdistdump on **multiple nodes**, you should launch one process on each
node in your cluster, plus one. One slave process per node and the extra one is
for the master. As was stated earlier, you can launch master on a dedicated
node, where no flow data is stored, but it can also share node with slave
process. Following example command will launch dedicated master on *m_node* and
slaves on *sl1_node* and *sl2_node*. Both slaves will read flow_file and send
results back to master:
```Shell
mpiexec -host m_node,sl1_node,sl2_node fdistdump -s srcport flow_file
```

## Documentation
For complete documentation and more examples see fdistdump manual page:
```Shell
man fdistdump
```
