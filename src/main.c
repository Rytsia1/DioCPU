#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"

#define MAX_LABELS 16
#define LINE_BUFFER_SIZE 128

typedef struct {
    char name[16];
    uint8_t address;
} Label;

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

static int find_label(
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

static char *trim_and_strip_comment(char *line) {
    char *comment = strchr(line, ';');
    if (comment) {
        *comment = '\0';
    }

    while (*line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') {
        line++;
    }

    size_t len = strlen(line);
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r' || line[len - 1] == '\n')) {
        line[len - 1] = '\0';
        len--;
    }

    return line;
}

static bool assemble_file(CPU *cpu, const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Error opening assembly file");
        return false;
    }

    Label labels[MAX_LABELS];
    size_t label_count = 0;
    char buffer[LINE_BUFFER_SIZE];
    uint16_t address = 0;

    /*
     * PASS 1:
     * Menghitung alamat instruksi dan mencatat posisi label.
     */
    while (fgets(buffer, sizeof(buffer), file)) {
        char *line = trim_and_strip_comment(buffer);
        if (*line == '\0') {
            continue;
        }

        size_t len = strlen(line);
        if (line[len - 1] == ':') {
            line[len - 1] = '\0'; // Hapus karakter ':'

            if (label_count >= MAX_LABELS) {
                printf("Error: too many labels.\n");
                fclose(file);
                return false;
            }

            strncpy(labels[label_count].name, line, sizeof(labels[label_count].name) - 1);
            labels[label_count].name[sizeof(labels[label_count].name) - 1] = '\0';
            labels[label_count].address = (uint8_t)address;
            label_count++;
        } else {
            address += INSTRUCTION_SIZE;
        }
    }

    /*
     * PASS 2:
     * Mengubah instruksi teks menjadi bytecode di memori CPU.
     */
    rewind(file);
    address = 0;

    while (fgets(buffer, sizeof(buffer), file)) {
        char *line = trim_and_strip_comment(buffer);
        if (*line == '\0') {
            continue;
        }

        size_t len = strlen(line);
        if (line[len - 1] == ':') {
            continue; // Label sudah diproses pada Pass 1
        }

        if (address + INSTRUCTION_SIZE > MEMORY_SIZE) {
            printf("Error: program is too large for CPU memory.\n");
            fclose(file);
            return false;
        }

        char mnemonic[16] = {0};
        if (sscanf(line, "%15s", mnemonic) != 1) {
            continue;
        }

        if (strcmp(mnemonic, "LOAD") == 0) {
            unsigned int reg = 0, val = 0;
            if (sscanf(line, "%*s R%u , %u", &reg, &val) != 2) {
                printf("Error parsing LOAD: %s\n", line);
                fclose(file);
                return false;
            }
            cpu->memory[address] = OP_LOAD;
            cpu->memory[address + 1] = (uint8_t)reg;
            cpu->memory[address + 2] = (uint8_t)val;
        } else if (strcmp(mnemonic, "ADD") == 0) {
            unsigned int reg1 = 0, reg2 = 0;
            if (sscanf(line, "%*s R%u , R%u", &reg1, &reg2) != 2) {
                printf("Error parsing ADD: %s\n", line);
                fclose(file);
                return false;
            }
            cpu->memory[address] = OP_ADD;
            cpu->memory[address + 1] = (uint8_t)reg1;
            cpu->memory[address + 2] = (uint8_t)reg2;
        } else if (strcmp(mnemonic, "SUB") == 0) {
            unsigned int reg1 = 0, reg2 = 0;
            if (sscanf(line, "%*s R%u , R%u", &reg1, &reg2) != 2) {
                printf("Error parsing SUB: %s\n", line);
                fclose(file);
                return false;
            }
            cpu->memory[address] = OP_SUB;
            cpu->memory[address + 1] = (uint8_t)reg1;
            cpu->memory[address + 2] = (uint8_t)reg2;
        } else if (strcmp(mnemonic, "JMP") == 0 ||
                   strcmp(mnemonic, "JZ") == 0 ||
                   strcmp(mnemonic, "JNZ") == 0) {
            char target[32] = {0};
            if (sscanf(line, "%*s %31s", target) != 1) {
                printf("Error parsing jump instruction: %s\n", line);
                fclose(file);
                return false;
            }

            uint8_t target_addr = 0;
            int found = find_label(labels, label_count, target);
            if (found >= 0) {
                target_addr = (uint8_t)found;
            } else {
                char *endptr = NULL;
                long val = strtol(target, &endptr, 0);
                if (endptr && *endptr == '\0') {
                    target_addr = (uint8_t)val;
                } else {
                    printf("Error: label not found: %s\n", target);
                    fclose(file);
                    return false;
                }
            }

            if (strcmp(mnemonic, "JMP") == 0) {
                cpu->memory[address] = OP_JMP;
            } else if (strcmp(mnemonic, "JZ") == 0) {
                cpu->memory[address] = OP_JZ;
            } else {
                cpu->memory[address] = OP_JNZ;
            }
            cpu->memory[address + 1] = target_addr;
            cpu->memory[address + 2] = 0;
        } else if (strcmp(mnemonic, "HLT") == 0) {
            cpu->memory[address] = OP_HLT;
            cpu->memory[address + 1] = 0;
            cpu->memory[address + 2] = 0;
        } else {
            printf("Error: unknown instruction: %s\n", mnemonic);
            fclose(file);
            return false;
        }

        address += INSTRUCTION_SIZE;
    }

    fclose(file);
    return true;
}

int main(int argc, char *argv[]) {
    const char *program_file = (argc > 1) ? argv[1] : "programs/countdown.asm";

    CPU cpu;
    cpu_init(&cpu);

    printf("Assembling %s...\n", program_file);
    if (!assemble_file(&cpu, program_file)) {
        return 1;
    }

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
