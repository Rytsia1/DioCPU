#ifndef CPU_H
#define CPU_H

#include <stdbool.h>
#include <stdint.h>

#define MEMORY_SIZE 256
#define REGISTER_COUNT 4

typedef struct {
    uint8_t registers[REGISTER_COUNT];

    uint8_t pc; // Program Counter
    uint8_t sp; // Stack Pointer

    uint8_t memory[MEMORY_SIZE];

    bool zero_flag;
    bool carry_flag;
    bool halted;
} CPU;

void cpu_init(CPU *cpu);

#endif // CPU_H