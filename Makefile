# Makefile for ND100 VM

CC= gcc
#CFLAGS = -ggdb
#CFLAGS = -Wall -O3 -pg -fno-aggressive-loop-optimizations
CFLAGS = -Wall -O3 -g 

OBJS=cpu.o mon.o decode.o float.o floppy.o io.o rtc.o nd100lib.o nd100em.o

all: nd100em

clean:
	rm -f cpu.o mon.o trace.o decode.o float.o floppy.o io.o rtc.o nd100lib.o nd100em.o cpu_state.o nd100em core

cpu.o: cpu.c cpu.h nd100.h
	$(CC) $(CFLAGS) -c cpu.c

cpu_state.o: cpu_state.c cpu_state.h cpu.h nd100.h
	$(CC) $(CFLAGS) -c cpu_state.c

rtc.o: rtc.c rtc.h nd100.h
	$(CC) $(CFLAGS) -c rtc.c

trace.o: trace.c trace.h nd100.h
	$(CC) $(CFLAGS) -c trace.c

mon.o: mon.c nd100.h mon.h
	$(CC) $(CFLAGS) -c mon.c

decode.o: decode.c
	$(CC) $(CFLAGS) -c decode.c

float.o: float.c
	$(CC) $(CFLAGS) -c float.c

io.o: io.c nd100.h io.h
	$(CC) $(CFLAGS) -c io.c

floppy.o: floppy.c
	$(CC) $(CFLAGS) -c floppy.c

nd100lib.o: nd100lib.c nd100lib.h nd100.h
	$(CC) $(CFLAGS) -c nd100lib.c

nd100em.o: nd100em.c nd100em.h nd100.h
	$(CC) $(CFLAGS) -c nd100em.c

nd100em: nd100em.o nd100lib.o cpu.o rtc.o mon.o decode.o float.o floppy.o io.o trace.o cpu_state.o
	$(CC) $(CFLAGS) -pthread nd100em.o nd100lib.o cpu.o rtc.o mon.o decode.o float.o floppy.o io.o trace.o cpu_state.o -lconfig -lm -o nd100em

