#ifndef CPU_H
#define CPU_H

#include <stdbool.h>
#include <stdint.h>

#define MEMORY_SIZE 256
#define REGISTER_COUNT 4
#define INSTRUCTION_SIZE 3

#define OP_LOAD 0x01
#define OP_ADD  0x02
#define OP_SUB  0x03
#define OP_JMP  0x04
#define OP_JZ   0x05
#define OP_JNZ  0x06
#define OP_HLT  0xFF

typedef struct {
    uint8_t registers[REGISTER_COUNT];

    uint16_t pc; // Program Counter
    uint8_t sp; // Stack Pointer

    uint8_t memory[MEMORY_SIZE];

    bool zero_flag;
    bool carry_flag;
    bool halted;
} CPU;

void cpu_init(CPU *cpu);

bool cpu_step(CPU *cpu);
void cpu_run(CPU *cpu);

#endif