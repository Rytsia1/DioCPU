#include "cpu.h"

#include <string.h>

void cpu_init(CPU *cpu) {
    memset(cpu, 0, sizeof(*cpu));

    // Stack starts at the last byte of memory.
    cpu->sp = MEMORY_SIZE - 1;
}