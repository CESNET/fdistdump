#file: Makefile
#author: Jan Wrona, <wrona@cesnet.cz>
#author: Pavel Krobot, <Pavel.Krobot@cesnet.cz>
#date: 2015

#Copyright (C) 2015 CESNET
#
#LICENSE TERMS
#
#Redistribution and use in source and binary forms, with or without
#modification, are permitted provided that the following conditions
#are met:
#1. Redistributions of source code must retain the above copyright
#   notice, this list of conditions and the following disclaimer.
#2. Redistributions in binary form must reproduce the above copyright
#   notice, this list of conditions and the following disclaimer in
#   the documentation and/or other materials provided with the
#   distribution.
#3. Neither the name of the Company nor the names of its contributors
#   may be used to endorse or promote products derived from this
#   software without specific prior written permission.
#
#ALTERNATIVELY, provided that this notice is retained in full, this
#product may be distributed under the terms of the GNU General Public
#License (GPL) version 2 or later, in which case the provisions
#of the GPL apply INSTEAD OF those given above.
#
#This software is provided ``as is'', and any express or implied
#warranties, including, but not limited to, the implied warranties of
#merchantability and fitness for a particular purpose are disclaimed.
#In no event shall the company or contributors be liable for any
#direct, indirect, incidental, special, exemplary, or consequential
#damages (including, but not limited to, procurement of substitute
#goods or services; loss of use, data, or profits; or business
#interruption) however caused and on any theory of liability, whether
#in contract, strict liability, or tort (including negligence or
#otherwise) arising in any way out of the use of this software, even
#if advised of the possibility of such damage.

################################################################################
# files
TARGET = fdistdump
SOURCES = main.c common.c master.c slave.c arg_parse.c
HEADERS = $(SOURCES:.c=.h) comm/communication.h
OBJECTS = $(SOURCES:.c=.o)
OTHER_FILES = Makefile

# communication
MPI_SOURCES = comm/mpi/comm_mpi.c comm/mpi/comm_mpi_master.c \
	 comm/mpi/comm_mpi_slave.c
MPI_HEADERS = $(MPI_SOURCES:.c=.h)
MPI_OBJECTS = $(MPI_SOURCES:.c=.o)
################################################################################
# compiler, linker and options
CC = mpicc
CCFLAGS = -std=gnu11 -pedantic -W -Wall -Wextra -Winit-self -Wcast-align \
	 -Wcast-qual -Wmissing-include-dirs -Wshadow -fdiagnostics-color=auto \
	 -g

LINK = $(CC)
LINKFLAGS =
LIBS = -lnf

################################################################################
# suffix rules
.SUFFIXES: .c .o
.c.o:
	$(CC) $(CCFLAGS) -c -o "$@" "$<"

################################################################################
#build rules
all: mpi $(SOURCES) $(TARGET)

mpi: $(MPI_SOURCES) $(MPI_OBJECTS)

debug: CCFLAGS += -DDEBUG
debug: all

ndebug: CCFLAGS += -DNDEBUG
ndebug: all

$(TARGET): $(OBJECTS) $(MPI_OBJECTS)
	$(LINK) $(LINKFLAGS) $(LIBS) -o $(TARGET) $(OBJECTS) $(MPI_OBJECTS)

################################################################################
#other rules
clean_mpi:
	rm -f $(MPI_OBJECTS)

clean: clean_mpi
	rm -f $(OBJECTS)

distclean: clean
	rm -f $(TARGET)
