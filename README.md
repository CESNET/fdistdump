# fdistdump - a tool to query IP flow records on a distributed system

| Branch  | Travis CI | Coverity |
|---------|-----------|----------|
| master  | [![Build Status](https://travis-ci.org/CESNET/fdistdump.svg?branch=master)](https://travis-ci.org/CESNET/fdistdump) | [![Coverity Scan Status](https://scan.coverity.com/projects/5969/badge.svg)](https://scan.coverity.com/projects/5969 "Coverity Scan Status") |
| develop | [![Build Status](https://travis-ci.org/CESNET/fdistdump.svg?branch=develop)](https://travis-ci.org/CESNET/fdistdump) | [![Coverity Scan Status](https://scan.coverity.com/projects/5969/badge.svg)](https://scan.coverity.com/projects/5969 "Coverity Scan Status") |

Fdistdump is a fast, scalable, distributed tool to query Internet Protocol flow
record files. The master/slave communication model allows it to run jobs on
unlimited number of nodes, including a single local node. Message Passing
Interface (MPI) is used as an underlying communication protocol. Its basic
features include listing, filtering, sorting, and aggregating records.
Fdistdump also implements algorithm to quickly answer Top-N queries (e.g., find
the N objects with the highest aggregate values) on a distributed system.
fdistdump is a fast, scalable and distributed tool to query Internet Protocol
flow files. The master/slave communication model allows it to run jobs on
unlimited number of nodes, including a single local node. Among other features,
fdistdump allows you to list, sort and aggregate records with the possibility to
apply powerful record filter.

It is written in the C language and Message Passing Interface (MPI) is used as
an underlying communication protocol. It is currently supported only on Linux,
other operating systems are not supported/tested.

## Prerequisites
* [libnf](https://github.com/VUTBR/libnf "libnf GitHub page"): C interface for
  processing nfdump files.
* [MPI](http://www.mpi-forum.org/ "Message Passing Interface Forum"): Message
  Passing Interface. fdistdump requires implementation supporting standard
  MPI-2.0 or newer (Open MPI, MPICH2, MPICH3, MVAPICH2, Intel® MPI Library,
  ...).
* [bloom-filter-index](https://github.com/CESNET/bloom-filter-index "bloom-filter-index GitHub page")
  (optional): A library for indexing IP addresses in flow records using Bloom
  filters.

## Installation
There are two options how to get fdistdump: prebuilt packages and source code.

### From the Prebuilt Packages
This is the easiest way of installation, but we current build packages only for
Fedora-based distributions. We are using Copr (a Fedora community build
service) to create the repositories for several distributions and architectures.
Check out the
[CESNET group page](https://copr.fedorainfracloud.org/groups/g/CESNET/coprs/).
To install fdistdump, you will need an
[@CESNET/fdistdump repository](https://copr.fedorainfracloud.org/coprs/g/CESNET/fdistdump/)
and a
[@CESNET/NEMEA repository](https://copr.fedorainfracloud.org/coprs/g/CESNET/NEMEA/),
which contains some dependencies (libnf and bloom-filter-index libraries).

The following shell commands show how to install fdistdump from repository on
distributions using YUM (e.g., RHEL and CentOS):
```Shell
yum install yum-plugin-copr
yum copr enable @CESNET/fdistdump
yum copr enable @CESNET/NEMEA
yum install fdistdump-mpich     # version compiled against MPICH
yum install fdistdump-openmpi   # version compiled against Open MPI
module load mpi/mpich-x86_64    # load environment for the MPICH version
module load mpi/openmpi-x86_64  # load environment for the Open MPI version
```

The following commands show how to install fdistdump from repository on
distributions using DNF (e.g., Fedora):
```Shell
dnf copr enable @CESNET/fdistdump
dnf copr enable @CESNET/NEMEA
dnf install fdistdump-mpich     # version compiled against MPICH
dnf install fdistdump-openmpi   # version compiled against Open MPI
module load mpi/mpich-x86_64    # load environment for the MPICH version
module load mpi/openmpi-x86_64  # load environment for the Open MPI version
```

### From the Source
If you cannot/do not want to use the prebuilt packages, here is how to compile
the source code.

#### TL;DR
```Shell
git clone https://github.com/CESNET/fdistdump.git
mkdir fdistdump_build
cd fdistdump_build
cmake ../fdistdump
cmake --build .
sudo make install
```

#### Download the Source Code
- Git: `git clone https://github.com/CESNET/fdistdump.git`
- Tarballs: https://github.com/CESNET/fdistdump/releases/latest

#### Generate a Build Tree
Fdistdump uses CMake build system. CMake can handle in-place and out-of-place
builds, enabling several builds from the same source tree. Thus, if a build
directory is removed, the source files remain unaffected.

Out-of-place or out-of-source builds are recommend. An out-of-source build puts
the generated files in a completely separate directory, so that source tree is
unchanged. You can build multiple variants in separate directories, e.g.,
fdistdump_release, fdistdump_debug, ...
```Shell
mkdir fdistdump_debug fdistdump_release
tree -L 2
.
├── fdistdump
│   ├── AUTHORS
│   ├── cmake
│   ├── CMakeLists.txt
│   ├── doc
│   ├── examples
│   ├── extras
│   ├── COPYING
│   ├── pkg
│   ├── README.md
│   ├── src
│   ├── tests
│   └── TODO
├── fdistdump_debug
└── fdistdump_release
```

Then you can change the current directory and point CMake to the source tree:
```Shell
cd fdistdump_release
cmake ../fdistdump
```

In-place or in-source builds are also possible. An in-source build puts the
generated files in the source tree:
```Shell
cd fdistdump
cmake .
```

#### Configure the CMake Settings
The most convenient way to configure CMake is to use tools like ccmake (CMake
curses interface) or cmake-gui. However, it is also possible to create a CMake
cache entry directly from the command-line using options
`-D <var>:<type>=<value>` or `-D <var>=<value>`.

Fdistdump-defined variables are:
- `ENABLE_BFINDEX=<ON|OFF>`: Enables/disables the bloom-filter-index library.
   Enabled by default.
- `EXECUTABLE_SUFFIX:STRING`: Appends `value` to every produced
   executable. Disabled by default.

There are also many [CMake-defined variables](https://cmake.org/cmake/help/latest/manual/cmake-variables.7.html),
the most useful are:
- `CMAKE_BUILD_TYPE:STRING`: Specifies the build type on single-configuration
   generators. Possible values are Debug, Release, RelWithDebInfo, MinSizeRel.
   Defaults to Release.
- `CMAKE_INSTALL_PREFIX:PATH`: Installation directory. Defaults to `/usr/local`.

#### Build
If the current directory is the build tree, the you can simply run
`cmake --build .` or `make` if you generated a standard Unix makefile.

#### Install
If you generated a standard Unix makefile, you can simply run `make install`
or `sudo make install`.

One can use the DESTDIR mechanism in order to relocate the whole installation.
It is used in order to install software at non-default location:
```Shell
make DESTDIR=/home/user install
```
which will install using the installation directory (e.g., `/usr/local`)
prepended with the DESTDIR value which finally gives `/home/user/usr/local`.

#### Build Packages
```Shell
cmake --build . --target package          # archives with binaries
cmake --build . --target package_source   # archives with source
cmake --build . --target package_rpm      # RPMs
cmake --build . --target package_srpm     # source RPMs
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
For complete documentation and more examples see the fdistdump manual page:
```Shell
man fdistdump
```
