#!/usr/bin/env sh
set -e

if [ "$#" -ne 1 ]
then
        echo "Usage: ${0} mpi_implementation"
        exit 1
fi

case $1 in
        mpich)
                #default MPICH version in Ubuntu 12.10 LTS (1) doesn't comply
                #MPI 2.2, we need at least MPICH 2
                sudo apt-get install -q mpich2 libmpich2-dev;;
        openmpi)
                #default OMPI version in Ubuntu 12.10 LTS (1.4) doesn't comply
                #MPI 2.2, we need at least OMPI 1.5
                sudo apt-get install -q openmpi1.5-bin libopenmpi1.5-dev;;
        *)
                echo "Error: unknown MPI implementation:" $1;
                exit 1;;
esac
