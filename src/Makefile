CC=gcc

RTDINC=/home/clu/devel/dm7820/DM7820_Linux_v02.00.00/include
RTDLIB=/home/clu/devel/dm7820/DM7820_Linux_v02.00.00/lib

# HDF5_INC=/home/clu/devel/hdf5-1.8.11/src
# HDF5_HL_INC=/home/clu/devel/hdf5-1.8.11/hl/src
# HDF5_LIB=/home/clu/devel/hdf5-1.8.11/src/.libs
# HDF5_HL_LIB=/home/clu/devel/hdf5-1.8.11/hl/src/.libs

# HDF5_INC=/home/clu/devel/hdf5-1.8.4-patch1/src
# HDF5_HL_INC=/home/clu/devel/hdf5-1.8.4-patch1/hl/src
# HDF5_LIB=/home/clu/devel/hdf5-1.8.4-patch1/src/.libs
# HDF5_HL_LIB=/home/clu/devel/hdf5-1.8.4-patch1/hl/src/.libs

DEBUG_FLAGS=-g
#INCLUDE_FLAGS=-I$(RTDINC) -I$(HDF5_INC) -I$(HDF5_HL_INC)
INCLUDE_FLAGS=-I$(RTDINC)
#LIBRARY_FLAGS=-L$(RTDLIB) -lrtd-dm7820 -lpthread
#LIBRARY_FLAGS=-L$(RTDLIB) -L$(HDF5_LIB) -L$(HDF5_HL_LIB) -lm -lrtd-dm7820
LIBRARY_FLAGS=-L$(RTDLIB) -lm -lrtd-dm7820
#OPTIMIZE_FLAGS=-mtune=native -march=native
OPTIMIZE_FLAGS=


HDF5_FLAGS=-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D_BSD_SOURCE
CFLAGS=$(DEBUG_FLAGS) $(INCLUDE_FLAGS) $(OPTIMIZE_FLAGS) -Wall
LD_FLAGS=$(LIBRARY_FLAGS)


all: tmif

tmif: tmif.c tmif_hdf5.o
	$(CC) tmif.c tmif_hdf5.o $(CFLAGS) -o $@ $(LIBRARY_FLAGS) -lhdf5 -lhdf5_hl -lpthread

tmif_hdf5.o: tmif_hdf5.c
	${CC} -c -o $@ $< ${CFLAGS} ${HDF5_FLAGS}

#test_output: test_output.c
#	@$(CC) test_output.c $(CFLAGS) -o $@ $(LIBRARY_FLAGS)

test_dma: test_dma.c
	@$(CC) test_dma.c $(CFLAGS) -o $@ $(LIBRARY_FLAGS)

clean:
	rm -f *.o tmif
