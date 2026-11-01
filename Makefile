CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -I./src
CFLAGS_V2 = -std=c11 -Wall -Wextra -Wpedantic -Werror -I./src/v2
LIBS_GUI = -lraylib

all: cli gui test test_v2

cli: dio_cpu.exe

gui: dio8_gui.exe

test: tests_cpu.exe

test_v2: test_v2_cpu_state.exe

dio_cpu.exe: src/main.c src/cpu.c src/assembler.c
	$(CC) $(CFLAGS) -o $@ $^

dio8_gui.exe: src/gui.c src/cpu.c src/assembler.c
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_GUI)

tests_cpu.exe: tests/tests_cpu.c src/cpu.c
	$(CC) $(CFLAGS) -o $@ $^

test_v2_cpu_state.exe: tests/test_v2_cpu_state.c src/v2/cpu_v2.c
	$(CC) $(CFLAGS_V2) -o $@ $^

clean:
	rm -f dio_cpu.exe dio8_gui.exe tests_cpu.exe test_v2_cpu_state.exe *.o

.PHONY: all cli gui test test_v2 clean
