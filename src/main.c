#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cpu.h"

#define MAX_LABELS 16

typedef struct {
    char name[16];
    uint8_t address;
} Label;

typedef struct {
    uint8_t opcode;
    uint8_t operand1;
    uint8_t operand2;

    const char *label;
} AssemblyInstruction;

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

int find_label(
    const Label labels[],
    size_t label_count,
    const char *name
)
{
    for (size_t i = 0; i < label_count; i++) {
        if (strcmp(labels[i].name, name) == 0) {
            return labels[i].address;
        }
    }

    return -1;
}

bool assemble_program(CPU *cpu)
{
    AssemblyInstruction instructions[] = {
        { OP_LOAD, 0, 3, NULL },      // LOAD R0, 3
        { OP_LOAD, 1, 1, NULL },      // LOAD R1, 1
        { OP_SUB,  0, 1, NULL },      // LOOP: SUB R0, R1
        { OP_JNZ,  0, 0, "LOOP" },    // JNZ LOOP
        { OP_HLT,  0, 0, NULL }       // HLT
    };

    Label labels[MAX_LABELS];
    size_t label_count = 0;

    size_t instruction_count =
        sizeof(instructions) / sizeof(instructions[0]);

    /*
     * PASS 1:
     * Catat alamat label.
     */
    uint16_t address = 0;

    for (size_t i = 0; i < instruction_count; i++) {
        if (i == 2) {
            strcpy(labels[label_count].name, "LOOP");
            labels[label_count].address = (uint8_t)address;
            label_count++;
        }

        address += INSTRUCTION_SIZE;
    }

    /*
     * PASS 2:
     * Ubah instruksi menjadi bytecode.
     */
    address = 0;

    for (size_t i = 0; i < instruction_count; i++) {
        uint8_t operand1 = instructions[i].operand1;

        if (instructions[i].label != NULL) {
            int target = find_label(
                labels,
                label_count,
                instructions[i].label
            );

            if (target < 0) {
                printf("Error: label not found: %s\n",
                       instructions[i].label);
                return false;
            }

            operand1 = (uint8_t)target;
        }

        if (address + INSTRUCTION_SIZE > MEMORY_SIZE) {
            printf("Error: program is too large.\n");
            return false;
        }

        cpu->memory[address] = instructions[i].opcode;
        cpu->memory[address + 1] = operand1;
        cpu->memory[address + 2] = instructions[i].operand2;

        address += INSTRUCTION_SIZE;
    }

    return true;
}

void read_assembly_file(const char *filename)
{
    FILE *file = fopen(filename, "r");

    if (file == NULL)
    {
        printf("Error: Cannot open file %s\n", filename);
        return;
    }

    char line[256];

    printf("=== Reading Assembly File ===\n");

    while (fgets(line, sizeof(line), file) != NULL)
    {
        printf("%s", line);
    }

    fclose(file);
}

int main(void)
{
    read_assembly_file("countdown.asm");

    CPU cpu;
    cpu_init(&cpu);

    if (!assemble_program(&cpu)) {
        return 1;
    }

    printf("\n=== Dio-8 Debugger ===\n\n");

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
