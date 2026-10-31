CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -I./src
LIBS_GUI = -lraylib

all: cli gui test

cli: dio_cpu.exe

gui: dio8_gui.exe

test: tests_cpu.exe

dio_cpu.exe: src/main.c src/cpu.c src/assembler.c
	$(CC) $(CFLAGS) -o $@ $^

dio8_gui.exe: src/gui.c src/cpu.c src/assembler.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_GUI)

tests_cpu.exe: tests/tests_cpu.c src/cpu.c
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f dio_cpu.exe dio8_gui.exe tests_cpu.exe *.o

.PHONY: all cli gui test clean
