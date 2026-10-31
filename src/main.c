#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

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

static void print_instruction(const CPU *cpu, uint16_t address) {
    uint8_t opcode = cpu->memory[address];
    uint8_t operand1 = cpu->memory[address + 1];
    uint8_t operand2 = cpu->memory[address + 2];

    printf("0x%02X: ", address);

    switch (opcode) {
        case OP_LOAD:
            printf("LOAD R%u, %u", operand1, operand2);
            break;

        case OP_ADD:
            printf("ADD R%u, R%u", operand1, operand2);
            break;

        case OP_SUB:
            printf("SUB R%u, R%u", operand1, operand2);
            break;

        case OP_JMP:
            printf("JMP 0x%02X", operand1);
            break;

        case OP_JZ:
            printf("JZ 0x%02X", operand1);
            break;

        case OP_JNZ:
            printf("JNZ 0x%02X", operand1);
            break;

        case OP_HLT:
            printf("HLT");
            break;

        default:
            printf("UNKNOWN 0x%02X", opcode);
            break;
    }
}

void assemble_program(CPU *cpu)
{
    uint8_t program[] = {
        OP_LOAD, 0, 3,    // LOAD R0, 3
        OP_LOAD, 1, 1,    // LOAD R1, 1
        OP_SUB,  0, 1,    // LOOP: SUB R0, R1
        OP_JNZ,  6, 0,    // JNZ LOOP
        OP_HLT,  0, 0     // HLT
    };

    for (size_t i = 0; i < sizeof(program); i++) {
        cpu->memory[i] = program[i];
    }
}

int main(void) {
    CPU cpu;

    cpu_init(&cpu);

    assemble_program(&cpu);

    printf("=== Dio-8 Debugger ===\n\n");

    printf("Initial CPU State:\n");
    print_cpu_state(&cpu);

    printf("\nExecution Trace:\n");

    while (!cpu.halted) {
        uint16_t current_pc = cpu.pc;

        printf("\nExecuting: ");
        print_instruction(&cpu, current_pc);
        printf("\n");

        cpu_step(&cpu);

        print_cpu_state(&cpu);
    }

    printf("\nCPU execution finished.\n");

    return 0;
}
