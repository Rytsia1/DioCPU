#ifndef DIOCPU_STACK_H
#define DIOCPU_STACK_H

#include "memory.h"

#include <stdint.h>

#define DIOCPU_STACK_BOTTOM      0x30000u
#define DIOCPU_STACK_TOP         0x3FFFFu
#define DIOCPU_STACK_INITIAL_SP  0x3FFFEu

typedef enum {
    STACK_STATUS_OK = 0,
    STACK_STATUS_INVALID_ARGUMENT,
    STACK_STATUS_INVALID_STACK_POINTER,
    STACK_STATUS_INVALID_RETURN_ADDRESS,
    STACK_STATUS_OVERFLOW,
    STACK_STATUS_UNDERFLOW,
    STACK_STATUS_MEMORY_ERROR
} StackStatus;

/* Stack initialization */
StackStatus stack_reset(uint32_t *sp);

/* 16-bit stack operations */
StackStatus stack_push16(
    Memory *memory,
    uint32_t *sp,
    uint16_t value
);

StackStatus stack_pop16(
    const Memory *memory,
    uint32_t *sp,
    uint16_t *value
);

/* CALL and RET operations */
StackStatus stack_call(
    Memory *memory,
    uint32_t *sp,
    uint32_t return_address
);

StackStatus stack_ret(
    const Memory *memory,
    uint32_t *sp,
    uint32_t *return_address
);

#endif
