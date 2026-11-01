CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -Werror -std=c11 -I./src
LIBS_GUI = -lraylib

all: cli gui test cpu-v2 gui-v2 gui-v2-test

cli: dio_cpu.exe

gui: dio8_gui.exe

test: tests_cpu.exe tests_alu.exe tests_memory.exe tests_stack.exe tests_decoder.exe tests_assembler.exe tests_debugger.exe tests_cpu_v2.exe tests_gui_v2_integration.exe

dio_cpu.exe: src/main.c src/cpu.c src/assembler_dio8.c
	$(CC) -Wall -Wextra -Wpedantic -I./src -o $@ $^

dio8_gui.exe: src/gui.c src/cpu.c src/assembler_dio8.c
	$(CC) -Wall -Wextra -Wpedantic -I./src -o $@ $^ $(LIBS_GUI)

tests_cpu.exe: tests/tests_cpu.c src/cpu.c
	$(CC) -Wall -Wextra -Wpedantic -I./src -o $@ $^

tests_alu.exe: tests/tests_alu.c src/alu.c src/alu.h
	$(CC) $(CFLAGS) -o $@ tests/tests_alu.c src/alu.c

tests_memory.exe: tests/tests_memory.c src/memory.c src/memory.h
	$(CC) $(CFLAGS) -o $@ tests/tests_memory.c src/memory.c

tests_stack.exe: tests/tests_stack.c src/stack.c src/memory.c src/stack.h src/memory.h
	$(CC) $(CFLAGS) -o $@ tests/tests_stack.c src/stack.c src/memory.c

tests_decoder.exe: tests/tests_decoder.c src/decoder.c src/memory.c src/decoder.h src/memory.h
	$(CC) $(CFLAGS) -o $@ tests/tests_decoder.c src/decoder.c src/memory.c

tests_assembler.exe: tests/tests_assembler.c src/assembler.c src/decoder.c src/memory.c
	$(CC) $(CFLAGS) -o $@ tests/tests_assembler.c src/assembler.c src/decoder.c src/memory.c

tests_debugger.exe: tests/tests_debugger.c src/debugger.c src/decoder.c src/memory.c
	$(CC) $(CFLAGS) -o $@ tests/tests_debugger.c src/debugger.c src/decoder.c src/memory.c

# ==========================================
# DioCPU V2
# ==========================================

V2_CORE_SOURCES = \
	src/cpu_v2.c \
	src/alu.c \
	src/memory.c \
	src/stack.c \
	src/decoder.c \
	src/assembler.c \
	src/debugger.c

# Build V2 CLI
diocpu_v2.exe: src/main_v2.c $(V2_CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

# Build V2 integration tests
tests_cpu_v2.exe: tests/tests_cpu_v2.c $(V2_CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^

# Build V2 GUI Dashboard
diocpu_v2_gui.exe: src/gui_v2.c $(V2_CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_GUI)

# Build V2 GUI integration tests
tests_gui_v2_integration.exe: tests/tests_gui_v2_integration.c $(V2_CORE_SOURCES)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_GUI)

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

# Run V2 tests
cpu-v2-test: tests_cpu_v2.exe
	./tests_cpu_v2.exe

# Run V2 GUI integration tests
gui-v2-test: tests_gui_v2_integration.exe
	./tests_gui_v2_integration.exe

# Build and test
cpu-v2: diocpu_v2.exe tests_cpu_v2.exe

gui-v2: diocpu_v2_gui.exe

clean:
	rm -f dio_cpu.exe dio8_gui.exe tests_cpu.exe tests_alu.exe tests_memory.exe tests_stack.exe tests_decoder.exe tests_assembler.exe tests_debugger.exe test_v2_cpu_state.exe diocpu_v2.exe tests_cpu_v2.exe diocpu_v2_gui.exe tests_gui_v2_integration.exe *.o

.PHONY: all cli gui test clean memory-test stack-test decoder-test assembler-test debugger-test cpu-v2 cpu-v2-test gui-v2 gui-v2-test
