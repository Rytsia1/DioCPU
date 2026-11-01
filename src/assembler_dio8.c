#include "assembler_dio8.h"

#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

char *trim(char *text)
{
    while (isspace((unsigned char)*text))
    {
        text++;
    }

    if (*text == '\0')
    {
        return text;
    }

    char *end = text + strlen(text) - 1;

    while (end > text && isspace((unsigned char)*end))
    {
        end--;
    }

    *(end + 1) = '\0';

    return text;
}

char *clean_line(char *line)
{
    char *comment = strchr(line, ';');

    if (comment != NULL)
    {
        *comment = '\0';
    }

    return trim(line);
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

        if (*cleaned == '\0')
        {
            continue;
        }

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

            if (find_label(labels, *label_count, label_name) >= 0)
            {
                printf("Error: Duplicate label: %s\n", label_name);
                return 0;
            }

            strcpy(labels[*label_count].name, label_name);
            labels[*label_count].address = (uint8_t)address;

            (*label_count)++;

            cleaned = trim(colon + 1);

            if (*cleaned == '\0')
            {
                continue;
            }
        }

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

        char *colon = strchr(cleaned, ':');

        if (colon != NULL)
        {
            *colon = '\0';
            cleaned = trim(colon + 1);

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

        if (address + INSTRUCTION_SIZE > MEMORY_SIZE)
        {
            printf("Error: Program exceeds CPU memory\n");
            return 0;
        }

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

bool assemble_file(const char *filename, uint8_t *buffer, size_t buffer_size, size_t *program_size)
{
    FILE *file = fopen(filename, "r");
    if (file == NULL)
    {
        printf("Cannot open %s\n", filename);
        return false;
    }

    Label labels[MAX_LABELS];
    int label_count = 0;

    CPU temp_cpu;
    cpu_init(&temp_cpu);

    if (!first_pass(file, labels, &label_count))
    {
        fclose(file);
        return false;
    }

    if (!second_pass(file, &temp_cpu, labels, label_count))
    {
        fclose(file);
        return false;
    }

    fclose(file);

    if (buffer != NULL && buffer_size >= MEMORY_SIZE)
    {
        memcpy(buffer, temp_cpu.memory, MEMORY_SIZE);
    }

    if (program_size != NULL)
    {
        *program_size = MEMORY_SIZE;
    }

    return true;
}

bool assemble_and_load(CPU *cpu, const char *filename)
{
    uint8_t temp_buffer[MEMORY_SIZE];
    size_t prog_size = 0;

    if (!assemble_file(filename, temp_buffer, sizeof(temp_buffer), &prog_size))
    {
        return false;
    }

    cpu_init(cpu);
    memcpy(cpu->memory, temp_buffer, MEMORY_SIZE);
    cpu->pc = 0;
    cpu->halted = false;

    return true;
}

void disassemble_instruction(const uint8_t *memory, uint16_t address, char *buffer, size_t buffer_size)
{
    if (address + INSTRUCTION_SIZE > MEMORY_SIZE)
    {
        snprintf(buffer, buffer_size, "0x%02X  ???", address);
        return;
    }

    uint8_t opcode = memory[address];
    uint8_t operand1 = memory[address + 1];
    uint8_t operand2 = memory[address + 2];

    switch (opcode)
    {
        case OP_LOAD:
            snprintf(buffer, buffer_size, "0x%02X  LOAD R%u, %u", address, operand1, operand2);
            break;

        case OP_ADD:
            snprintf(buffer, buffer_size, "0x%02X  ADD R%u, R%u", address, operand1, operand2);
            break;

        case OP_SUB:
            snprintf(buffer, buffer_size, "0x%02X  SUB R%u, R%u", address, operand1, operand2);
            break;

        case OP_JMP:
            snprintf(buffer, buffer_size, "0x%02X  JMP 0x%02X", address, operand1);
            break;

        case OP_JZ:
            snprintf(buffer, buffer_size, "0x%02X  JZ 0x%02X", address, operand1);
            break;

        case OP_JNZ:
            snprintf(buffer, buffer_size, "0x%02X  JNZ 0x%02X", address, operand1);
            break;

        case OP_OUT:
            if (operand1 == OUT_MODE_REGISTER)
            {
                snprintf(buffer, buffer_size, "0x%02X  OUT R%u", address, operand2);
            }
            else if (operand1 == OUT_MODE_IMMEDIATE)
            {
                snprintf(buffer, buffer_size, "0x%02X  OUT %u", address, operand2);
            }
            else
            {
                snprintf(buffer, buffer_size, "0x%02X  OUT INVALID", address);
            }
            break;

        case OP_HLT:
            snprintf(buffer, buffer_size, "0x%02X  HLT", address);
            break;

        default:
            snprintf(buffer, buffer_size, "0x%02X  0x%02X 0x%02X 0x%02X", address, opcode, operand1, operand2);
            break;
    }
}
