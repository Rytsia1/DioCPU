#include <stdio.h>
#include <stddef.h>

#include "cpu.h"

static void print_cpu_state(const CPU *cpu) {
    printf("PC: 0x%02X | ", cpu->pc);

    for (int i = 0; i < REGISTER_COUNT; i++) {
        printf("R%d: %3u | ", i, cpu->registers[i]);
    }

    printf("ZF: %d | CF: %d | HALT: %d\n",
           cpu->zero_flag,
           cpu->carry_flag,
           cpu->halted);
}


int main(void) {
    CPU cpu;

    cpu_init(&cpu);

    // Program:
    //
    // LOAD R0, 10
    // LOAD R1, 20
    // ADD  R0, R1
    // HLT

    uint8_t program[] = {
        OP_LOAD, 0, 255,
        OP_LOAD, 1, 1,
        OP_ADD,  0, 1,
        OP_HLT,  0, 0
    };

for (size_t i = 0; i < sizeof(program); i++) {
    cpu.memory[i] = program[i];
}

printf("=== Dio-8 Debugger ===\n\n");

printf("Initial CPU State:\n");
print_cpu_state(&cpu);

printf("\nExecution Trace:\n");

while (!cpu.halted) {
    uint8_t current_pc = cpu.pc;
    uint8_t opcode = cpu.memory[current_pc];

    printf("\nExecuting instruction at 0x%02X: ", current_pc);

    switch (opcode) {
        case OP_LOAD:
            printf("LOAD\n");
            break;

        case OP_ADD:
            printf("ADD\n");
            break;

        case OP_SUB:
            printf("SUB\n");
            break;

        case OP_JMP:
            printf("JMP\n");
            break;

        case OP_HLT:
            printf("HLT\n");
            break;

        default:
            printf("UNKNOWN\n");
            break;
    }

    cpu_step(&cpu);

    print_cpu_state(&cpu);
}

    printf("\nCPU execution finished.\n");  

    return 0;
}