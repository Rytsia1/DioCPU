#ifndef ASSEMBLER_DIO8_H
#define ASSEMBLER_DIO8_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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

char *trim(char *text);
char *clean_line(char *line);
int find_label(const Label labels[], int label_count, const char *name);
int parse_register(const char *text);
int parse_number(const char *text);
int parse_instruction(char *line, AssembledInstruction *instruction, const Label labels[], int label_count);
int first_pass(FILE *file, Label labels[], int *label_count);
int second_pass(FILE *file, CPU *cpu, const Label labels[], int label_count);

bool assemble_file(const char *filename, uint8_t *buffer, size_t buffer_size, size_t *program_size);
bool assemble_and_load(CPU *cpu, const char *filename);
void disassemble_instruction(const uint8_t *memory, uint16_t address, char *buffer, size_t buffer_size);

#endif
