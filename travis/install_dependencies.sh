#!/usr/bin/env bash

set -eu

# install dpkg-dev for dpkg-architecture
sudo apt-get install -qq dpkg-dev
ARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH)"

# install libnf
sudo apt-get install -qq flex bison libbz2-dev
git clone --recursive "https://github.com/VUTBR/libnf.git"
cd libnf/
./prepare-nfdump.sh
autoreconf -i
./configure --prefix="/usr/" --libdir="\${prefix}/lib/$ARCH"
make
sudo make install
cd ..

# install the Bloom filter indexing library
sudo apt-get install -qq flex bison libbz2-dev
git clone "https://github.com/CESNET/bloom-filter-index.git"
cd bloom-filter-index/
autoreconf -i
./configure --prefix="/usr/" --libdir="\${prefix}/lib/$ARCH"
make
sudo make install
cd ..

# install MPI
case "$MPI" in
    "mpich")  # install MPICH3 (default version in Ubuntu 14.04)
        sudo apt-get -qq install mpich libmpich-dev
        ;;
    "openmpi")  # install Open MPI 1.6 (default version in Ubuntu 14.04)
        sudo apt-get -qq install openmpi-bin libopenmpi-dev
        ;;
    *)
        echo "Error: unknown MPI implementation '$MPI'"
        exit 1
        ;;
esac
