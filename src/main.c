#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

#include "cpu.h"

#define MAX_LABELS 32
#define LABEL_NAME_SIZE 32

typedef struct
{
    char name[LABEL_NAME_SIZE];
    uint8_t address;
} Label;

typedef struct
{
    uint8_t opcode;
    uint8_t operand1;
    uint8_t operand2;
} AssembledInstruction;

void print_cpu_state(const CPU *cpu) {
    printf("PC: 0x%02X | ", cpu->pc);

    for (int i = 0; i < REGISTER_COUNT; i++) {
        printf("R%d: %3u | ", i, cpu->registers[i]);
    }

    printf("ZF: %d | CF: %d | HALT: %d\n",
           cpu->zero_flag,
           cpu->carry_flag,
           cpu->halted);
}

void print_memory(const CPU *cpu) {
    const uint16_t bytes_per_row = 16;

    printf("\n=== Memory Viewer ===\n");

    // Cetak header kolom
    printf("ADDR  ");

    for (uint16_t column = 0; column < bytes_per_row; column++) {
        printf("%02X ", column);
    }

    printf("\n");

    // Cetak seluruh isi memory
    for (uint16_t address = 0;
         address < MEMORY_SIZE;
         address += bytes_per_row) {

        // Cetak alamat awal setiap baris
        printf("%04X: ", address);

        // Cetak 16 byte dalam hexadecimal
        for (uint16_t column = 0;
             column < bytes_per_row;
             column++) {

            printf("%02X ", cpu->memory[address + column]);
        }

        printf("\n");
    }
}

void print_instruction(const CPU *cpu, uint16_t address) {
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

        case OP_OUT:
            if (operand1 == OUT_MODE_REGISTER)
            {
                printf("OUT R%u", operand2);
            }
            else if (operand1 == OUT_MODE_IMMEDIATE)
            {
                printf("OUT %u", operand2);
            }
            else
            {
                printf("OUT INVALID");
            }
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
    int label_count,
    const char *name
)
{
    for (int i = 0; i < label_count; i++)
    {
        if (strcmp(labels[i].name, name) == 0)
        {
            return labels[i].address;
        }
    }

    return -1;
}

char *trim(char *text)
{
    // Lewati spasi di awal baris
    while (isspace((unsigned char)*text))
    {
        text++;
    }

    // Jika baris kosong, langsung kembalikan
    if (*text == '\0')
    {
        return text;
    }

    // Cari karakter terakhir yang bukan spasi
    char *end = text + strlen(text) - 1;

    while (end > text && isspace((unsigned char)*end))
    {
        end--;
    }

    // Akhiri string setelah karakter terakhir
    *(end + 1) = '\0';

    return text;
}

char *clean_line(char *line)
{
    // Cari awal komentar
    char *comment = strchr(line, ';');

    if (comment != NULL)
    {
        *comment = '\0';
    }

    // Hapus spasi di awal dan akhir
    return trim(line);
}

int first_pass(
    FILE *file,
    Label labels[],
    int *label_count
)
{
    char line[256];
    int address = 0;

    *label_count = 0;

    rewind(file);

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *cleaned = clean_line(line);

        // Lewati baris kosong dan komentar
        if (*cleaned == '\0')
        {
            continue;
        }

        // Apakah baris ini merupakan label?
        char *colon = strchr(cleaned, ':');

        if (colon != NULL)
        {
            *colon = '\0';

            char *label_name = trim(cleaned);

            if (*label_name == '\0')
            {
                printf("Error: Empty label name\n");
                return 0;
            }

            if (strlen(label_name) >= LABEL_NAME_SIZE)
            {
                printf("Error: Label name too long: %s\n", label_name);
                return 0;
            }

            if (*label_count >= MAX_LABELS)
            {
                printf("Error: Too many labels\n");
                return 0;
            }

            // Cegah label duplikat
            if (find_label(labels, *label_count, label_name) >= 0)
            {
                printf("Error: Duplicate label: %s\n", label_name);
                return 0;
            }

            strcpy(labels[*label_count].name, label_name);
            labels[*label_count].address = (uint8_t)address;

            (*label_count)++;

            // Label boleh berada pada baris yang sama
            // dengan instruksi, misalnya LOOP: HLT
            cleaned = trim(colon + 1);

            if (*cleaned == '\0')
            {
                continue;
            }
        }

        // Baris ini mengandung instruksi
        address += INSTRUCTION_SIZE;

        if (address > MEMORY_SIZE)
        {
            printf("Error: Program exceeds CPU memory\n");
            return 0;
        }
    }

    return 1;
}

int parse_register(const char *text)
{
    if (text[0] != 'R')
    {
        return -1;
    }

    // Pastikan setelah angka register tidak ada karakter lain
    if (text[1] < '0' || text[1] > '3' || text[2] != '\0')
    {
        return -1;
    }

    return text[1] - '0';
}

int parse_number(const char *text)
{
    char *end;
    errno = 0;

    long value = strtol(text, &end, 0);

    if (text == end || *end != '\0')
    {
        return -1;
    }

    if (errno == ERANGE || value < 0 || value > UINT8_MAX)
    {
        return -1;
    }

    return (int)value;
}

int parse_instruction(
    char *line,
    AssembledInstruction *instruction,
    const Label labels[],
    int label_count
)
{
    char *mnemonic = strtok(line, " \t");
    char *operand1 = strtok(NULL, " \t,");
    char *operand2 = strtok(NULL, " \t,");

    if (mnemonic == NULL)
    {
        return 0;
    }

    instruction->opcode = 0;
    instruction->operand1 = 0;
    instruction->operand2 = 0;

    if (strcmp(mnemonic, "LOAD") == 0)
    {
        if (operand1 == NULL || operand2 == NULL)
        {
            return -1;
        }

        int reg = parse_register(operand1);
        int value = parse_number(operand2);

        if (reg < 0 || value < 0)
        {
            return -1;
        }

        instruction->opcode = OP_LOAD;
        instruction->operand1 = (uint8_t)reg;
        instruction->operand2 = (uint8_t)value;
    }
    else if (strcmp(mnemonic, "ADD") == 0)
    {
        if (operand1 == NULL || operand2 == NULL)
        {
            return -1;
        }

        int reg1 = parse_register(operand1);
        int reg2 = parse_register(operand2);

        if (reg1 < 0 || reg2 < 0)
        {
            return -1;
        }

        instruction->opcode = OP_ADD;
        instruction->operand1 = (uint8_t)reg1;
        instruction->operand2 = (uint8_t)reg2;
    }
    else if (strcmp(mnemonic, "SUB") == 0)
    {
        if (operand1 == NULL || operand2 == NULL)
        {
            return -1;
        }

        int reg1 = parse_register(operand1);
        int reg2 = parse_register(operand2);

        if (reg1 < 0 || reg2 < 0)
        {
            return -1;
        }

        instruction->opcode = OP_SUB;
        instruction->operand1 = (uint8_t)reg1;
        instruction->operand2 = (uint8_t)reg2;
    }
    else if (strcmp(mnemonic, "OUT") == 0)
    {
        if (operand1 == NULL || operand2 != NULL)
        {
            return -1;
        }

        int reg = parse_register(operand1);

        if (reg >= 0)
        {
            instruction->opcode = OP_OUT;
            instruction->operand1 = OUT_MODE_REGISTER;
            instruction->operand2 = (uint8_t)reg;
        }
        else
        {
            int value = parse_number(operand1);

            if (value < 0)
            {
                return -1;
            }

            instruction->opcode = OP_OUT;
            instruction->operand1 = OUT_MODE_IMMEDIATE;
            instruction->operand2 = (uint8_t)value;
        }
    }
    else if (
        strcmp(mnemonic, "JMP") == 0 ||
        strcmp(mnemonic, "JZ") == 0 ||
        strcmp(mnemonic, "JNZ") == 0
    )
    {
        if (operand1 == NULL || operand2 != NULL)
        {
            return -1;
        }

        int target = find_label(
            labels,
            label_count,
            operand1
        );

        if (target < 0)
        {
            printf("Error: Undefined label: %s\n", operand1);
            return -1;
        }

        if (strcmp(mnemonic, "JMP") == 0)
        {
            instruction->opcode = OP_JMP;
        }
        else if (strcmp(mnemonic, "JZ") == 0)
        {
            instruction->opcode = OP_JZ;
        }
        else
        {
            instruction->opcode = OP_JNZ;
        }

        instruction->operand1 = (uint8_t)target;
        instruction->operand2 = 0;
    }
    else if (strcmp(mnemonic, "HLT") == 0)
    {
        if (operand1 != NULL)
        {
            return -1;
        }

        instruction->opcode = OP_HLT;
    }
    else
    {
        return -1;
    }

    return 1;
}

int second_pass(
    FILE *file,
    CPU *cpu,
    const Label labels[],
    int label_count
)
{
    char line[256];
    int address = 0;

    rewind(file);

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *cleaned = clean_line(line);

        if (*cleaned == '\0')
        {
            continue;
        }

        // Hilangkan label jika ada
        char *colon = strchr(cleaned, ':');

        if (colon != NULL)
        {
            *colon = '\0';
            cleaned = trim(colon + 1);

            // Baris hanya berisi label
            if (*cleaned == '\0')
            {
                continue;
            }
        }

        AssembledInstruction instruction;

        int result = parse_instruction(
            cleaned,
            &instruction,
            labels,
            label_count
        );

        if (result != 1)
        {
            printf(
                "Assembly error near address 0x%02X\n",
                address
            );

            return 0;
        }

        // Pastikan instruksi tidak melewati batas memori
        if (address + INSTRUCTION_SIZE > MEMORY_SIZE)
        {
            printf("Error: Program exceeds CPU memory\n");
            return 0;
        }

        // Simpan machine code ke memori CPU
        cpu->memory[address] = instruction.opcode;
        cpu->memory[address + 1] = instruction.operand1;
        cpu->memory[address + 2] = instruction.operand2;

        printf(
            "Assembled 0x%02X: %02X %02X %02X\n",
            address,
            instruction.opcode,
            instruction.operand1,
            instruction.operand2
        );

        address += INSTRUCTION_SIZE;
    }

    return 1;
}

int main(void)
{
    FILE *file = fopen("countdown.asm", "r");

    if (file == NULL)
    {
        printf("Cannot open countdown.asm\n");
        return 1;
    }

    Label labels[MAX_LABELS];
    int label_count = 0;

    CPU cpu;
    cpu_init(&cpu);

    // Pass 1: mencari label
    if (!first_pass(file, labels, &label_count))
    {
        fclose(file);
        return 1;
    }

    // Pass 2: menerjemahkan instruksi
    if (!second_pass(file, &cpu, labels, label_count))
    {
        fclose(file);
        return 1;
    }

    fclose(file);

    // Reset PC sebelum menjalankan program
    cpu.pc = 0;
    cpu.halted = false;

    printf("\n=== Dio-8 Debugger ===\n\n");

    printf("Initial CPU State:\n");
    print_cpu_state(&cpu);

    print_memory(&cpu);

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
