#ifndef DIOCPU_ASSEMBLER_H
#define DIOCPU_ASSEMBLER_H

#include "memory.h"

#include <stdint.h>

typedef enum {
    ASSEMBLER_STATUS_OK = 0,
    ASSEMBLER_STATUS_INVALID_ARGUMENT,
    ASSEMBLER_STATUS_OUT_OF_MEMORY,
    ASSEMBLER_STATUS_SYNTAX_ERROR,
    ASSEMBLER_STATUS_UNKNOWN_MNEMONIC,
    ASSEMBLER_STATUS_DUPLICATE_LABEL,
    ASSEMBLER_STATUS_UNDEFINED_LABEL,
    ASSEMBLER_STATUS_INVALID_REGISTER,
    ASSEMBLER_STATUS_INVALID_NUMBER,
    ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE,
    ASSEMBLER_STATUS_BRANCH_OUT_OF_RANGE,
    ASSEMBLER_STATUS_PROGRAM_TOO_LARGE,
    ASSEMBLER_STATUS_TOO_MANY_LABELS
} AssemblerStatus;

typedef struct {
    uint32_t code_size;
    uint32_t instruction_count;
    uint32_t error_line;

    AssemblerStatus status;
} AssemblerResult;

AssemblerStatus assembler_assemble(
    const char *source,
    Memory *memory,
    AssemblerResult *result
);

const char *assembler_status_string(
    AssemblerStatus status
);

#endif
