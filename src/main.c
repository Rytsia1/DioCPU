#include <stdio.h>
#include <stddef.h>

#include "cpu.h"

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
        OP_LOAD, 0, 10,
        OP_LOAD, 1, 20,
        OP_ADD,  0, 1,
        OP_HLT,  0, 0
    };
    for (size_t i = 0; i < sizeof(program); i++) {
    cpu.memory[i] = program[i];
}

    cpu_run(&cpu);

    printf("=== Dio-8 CPU ===\n");

    printf("PC: 0x%02X\n", cpu.pc);
    printf("SP: 0x%02X\n", cpu.sp);

    for (int i = 0; i < REGISTER_COUNT; i++) {
        printf("Register %d: %u\n",
               i,
               cpu.registers[i]);
    }

    printf("Zero Flag: %d\n", cpu.zero_flag);
    printf("Carry Flag: %d\n", cpu.carry_flag);
    printf("Halted: %d\n", cpu.halted);

    return 0;
}