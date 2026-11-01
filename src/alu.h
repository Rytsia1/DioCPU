#ifndef DIOCPU_ALU_H
#define DIOCPU_ALU_H

#include <stdbool.h>
#include <stdint.h>

/* FLAGS bit definitions */
#define CPU_FLAG_ZF  (1u << 0)
#define CPU_FLAG_CF  (1u << 1)
#define CPU_FLAG_SF  (1u << 2)
#define CPU_FLAG_OF  (1u << 3)

#define CPU_FLAGS_DEFINED_MASK 0x000F

typedef enum {
    ALU_ADD,
    ALU_SUB,
    ALU_MUL,
    ALU_MULS,
    ALU_DIV,
    ALU_DIVS,
    ALU_AND,
    ALU_OR,
    ALU_XOR,
    ALU_NOT,
    ALU_CMP
} ALUOperation;

typedef enum {
    ALU_STATUS_OK = 0,
    ALU_STATUS_DIVIDE_BY_ZERO,
    ALU_STATUS_ARITHMETIC_OVERFLOW,
    ALU_STATUS_INVALID_OPERATION
} ALUStatus;

typedef struct {
    uint16_t result;
    uint16_t remainder;
    uint16_t flags;
    ALUStatus status;
} ALUResult;

/*
 * Executes an ALU operation.
 *
 * a, b:
 *   16-bit input operands.
 *
 * current_flags:
 *   FLAGS before the operation.
 *
 * Return:
 *   Result, remainder, updated flags, and operation status.
 *
 * On failure:
 *   result and remainder are zero.
 *   flags remain unchanged.
 */
ALUResult alu_execute(
    ALUOperation operation,
    uint16_t a,
    uint16_t b,
    uint16_t current_flags
);

/* FLAGS helpers */
bool alu_get_flag(uint16_t flags, uint16_t mask);

uint16_t alu_set_flag(
    uint16_t flags,
    uint16_t mask,
    bool value
);

#endif
