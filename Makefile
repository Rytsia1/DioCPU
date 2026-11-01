CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Werror -std=c11 -I./src
LIBS_GUI = -lraylib

all: cli gui test

cli: dio_cpu.exe

gui: dio8_gui.exe

test: tests_cpu.exe tests_alu.exe

dio_cpu.exe: src/main.c src/cpu.c src/assembler.c
	$(CC) -Wall -Wextra -Wpedantic -I./src -o $@ $^

dio8_gui.exe: src/gui.c src/cpu.c src/assembler.c
	$(CC) -Wall -Wextra -Wpedantic -I./src -o $@ $^ $(LIBS_GUI)

tests_cpu.exe: tests/tests_cpu.c src/cpu.c
	$(CC) -Wall -Wextra -Wpedantic -I./src -o $@ $^

tests_alu.exe: tests/tests_alu.c src/alu.c src/alu.h
	$(CC) $(CFLAGS) -o $@ tests/tests_alu.c src/alu.c

clean:
	rm -f dio_cpu.exe dio8_gui.exe tests_cpu.exe tests_alu.exe test_v2_cpu_state.exe *.o

.PHONY: all cli gui test clean
