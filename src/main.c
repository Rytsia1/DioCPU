
#include <stdio.h>
#include "cpu.h"

int main(void){
    CPU cpu;
    cpu_init(&cpu);

    printf("=== Dio-8 CPU ===\n");
    printf("PC: 0x%02X\n", cpu.pc);
    printf("SP: 0x%02X\n", cpu.sp);

    for (int i = 0; i < REGISTER_COUNT; i++) {
        printf("Register %d: 0x%02X\n", i, cpu.registers[i]);
    }

    printf("Zero Flag: %d\n", cpu.zero_flag);
    printf("Carry Flag: %d\n", cpu.carry_flag);
    printf("Halted: %d\n", cpu.halted);

    return 0;
}

