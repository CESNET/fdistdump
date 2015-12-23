#!/usr/bin/env sh
set -e

if [ "$#" -ne 1 ]
then
        echo "Usage: ${0} mpi_implementation"
        exit 1
fi

case $1 in
        mpich)
                #install MPICH3 (default version in Ubuntu 14.04)
                sudo apt-get -qq install mpich libmpich-dev;;
        openmpi)
                #install Open MPI 1.6 (default version in Ubuntu 14.04)
                sudo apt-get -qq install openmpi-bin libopenmpi-dev;;
        *)
                echo "Error: unknown MPI implementation:" $1;
                exit 1;;
esac
