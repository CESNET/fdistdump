#!/usr/bin/env sh

#install libnf
sudo apt-get install -qq flex bison libbz2-dev

git clone "https://github.com/VUTBR/nf-tools.git"
cd nf-tools/libnf/c/

./prepare-nfdump.sh
autoreconf -i
./configure --prefix="/usr/" --libdir="\${prefix}/lib/$(dpkg-architecture -qDEB_HOST_MULTIARCH)"

make
sudo make install


#install MPI
case "${MPI}" in
  "mpich")
    #install MPICH3 (default version in Ubuntu 14.04)
    sudo apt-get -qq install mpich libmpich-dev;;
  "openmpi")
    #install Open MPI 1.6 (default version in Ubuntu 14.04)
    sudo apt-get -qq install openmpi-bin libopenmpi-dev;;
  *)
    echo "Error: unknown MPI implementation \"${MPI}\""
    exit 1;;
esac
