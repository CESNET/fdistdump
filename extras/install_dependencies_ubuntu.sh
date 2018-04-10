#!/usr/bin/env bash

# Copyright 2016-2018 CESNET
#
# This file is part of Fdistdump.
#
# Fdistdump is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Fdistdump is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Fdistdump.  If not, see <http://www.gnu.org/licenses/>.

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
