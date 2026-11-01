CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Werror -std=c11 -I./src
LIBS_GUI = -lraylib

CORE_SOURCES = \
	src/cpu.c \
	src/alu.c \
	src/memory.c \
	src/stack.c \
	src/decoder.c \
	src/assembler.c \
	src/debugger.c

all: cli gui test

cli: diocpu.exe

gui: diocpu_gui.exe

# Main Executables
diocpu.exe: src/main.c $(CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

diocpu_gui.exe: src/gui.c $(CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_GUI)

# Test Suite Executables
tests_cpu.exe: tests/tests_cpu.c $(CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

tests_alu.exe: tests/tests_alu.c src/alu.c
	$(CC) $(CFLAGS) -o $@ $^

tests_memory.exe: tests/tests_memory.c src/memory.c
	$(CC) $(CFLAGS) -o $@ $^

tests_stack.exe: tests/tests_stack.c src/stack.c src/memory.c
	$(CC) $(CFLAGS) -o $@ $^

tests_decoder.exe: tests/tests_decoder.c src/decoder.c src/memory.c
	$(CC) $(CFLAGS) -o $@ $^

tests_assembler.exe: tests/tests_assembler.c src/assembler.c src/decoder.c src/memory.c
	$(CC) $(CFLAGS) -o $@ $^

tests_debugger.exe: tests/tests_debugger.c src/debugger.c src/decoder.c src/memory.c
	$(CC) $(CFLAGS) -o $@ $^

tests_gui_integration.exe: tests/tests_gui_integration.c $(CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_GUI)

test: tests_cpu.exe tests_alu.exe tests_memory.exe tests_stack.exe tests_decoder.exe tests_assembler.exe tests_debugger.exe tests_gui_integration.exe
	./tests_cpu.exe
	./tests_alu.exe
	./tests_memory.exe
	./tests_stack.exe
	./tests_decoder.exe
	./tests_assembler.exe
	./tests_debugger.exe
	./tests_gui_integration.exe

cpu-test: tests_cpu.exe
	./tests_cpu.exe

alu-test: tests_alu.exe
	./tests_alu.exe

memory-test: tests_memory.exe
	./tests_memory.exe

stack-test: tests_stack.exe
	./tests_stack.exe

decoder-test: tests_decoder.exe
	./tests_decoder.exe

assembler-test: tests_assembler.exe
	./tests_assembler.exe

debugger-test: tests_debugger.exe
	./tests_debugger.exe

gui-test: tests_gui_integration.exe
	./tests_gui_integration.exe

clean:
	rm -f diocpu.exe diocpu_gui.exe tests_cpu.exe tests_alu.exe tests_memory.exe tests_stack.exe tests_decoder.exe tests_assembler.exe tests_debugger.exe tests_gui_integration.exe *.o

.PHONY: all cli gui test clean cpu-test alu-test memory-test stack-test decoder-test assembler-test debugger-test gui-test
