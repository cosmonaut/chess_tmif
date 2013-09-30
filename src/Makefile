CC=gcc

RTDINC=/home/clu/devel/dm7820/DM7820_Linux_v02.00.00/include
RTDLIB=/home/clu/devel/dm7820/DM7820_Linux_v02.00.00/lib

DEBUG_FLAGS=
INCLUDE_FLAGS=-I$(RTDINC)
LIBRARY_FLAGS=-L$(RTDLIB) -lrtd-dm7820 -lpthread
OPTIMIZE_FLAGS=-mtune=native -march=native


HDF5_FLAGS=-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_BSD_SOURCE
CFLAGS=$(DEBUG_FLAGS) $(INCLUDE_FLAGS) $(OPTIMIZE_FLAGS) -Wall
LD_FLAGS=$(LIBRARY_FLAGS)


all: tmif

tmif: tmif.c tmif_hdf5.o
	$(CC) tmif.c tmif_hdf5.o $(CFLAGS) -o $@ $(LIBRARY_FLAGS) -lhdf5 -lhdf5_hl

tmif_hdf5.o: tmif_hdf5.c
	@${CC} -c -o $@ $< ${CFLAGS} ${HDF5_FLAGS}

test_output: test_output.c
	$(CC) test_output.c $(CFLAGS) -o $@ $(LIBRARY_FLAGS)

clean:
	rm -f *.o tmif
