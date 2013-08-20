CC=gcc

RTDINC=/home/clu/devel/dm7820-latest/DM7820_Linux_v02.00.00/include
RTDLIB=/home/clu/devel/dm7820-latest/DM7820_Linux_v02.00.00/lib

DEBUG_FLAGS=
INCLUDE_FLAGS=-I$(RTDINC)
LIBRARY_FLAGS=-L$(RTDLIB) -lrtd-dm7820 -lpthread
OPTIMIZE_FLAGS=-mtune=native -march=native


CFLAGS=$(DEBUG_FLAGS) $(INCLUDE_FLAGS) $(OPTIMIZE_FLAGS) -Wall
LD_FLAGS=$(LIBRARY_FLAGS)

all: tmif

tmif: tmif.c
	$(CC) tmif.c $(CFLAGS) -o $@ $(LIBRARY_FLAGS)

clean:
	rm -f *.o tmif
