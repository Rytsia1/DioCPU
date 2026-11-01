#include "stack.h"

#include <stddef.h>

static StackStatus validate_stack_pointer(uint32_t sp)
{
    if ((sp & 1u) != 0u) {
        return STACK_STATUS_INVALID_STACK_POINTER;
    }

    if (sp < DIOCPU_STACK_BOTTOM ||
        sp > DIOCPU_STACK_INITIAL_SP) {
        return STACK_STATUS_INVALID_STACK_POINTER;
    }

    return STACK_STATUS_OK;
}

StackStatus stack_reset(uint32_t *sp)
{
    if (sp == NULL) {
        return STACK_STATUS_INVALID_ARGUMENT;
    }

    *sp = DIOCPU_STACK_INITIAL_SP;

    return STACK_STATUS_OK;
}

StackStatus stack_push16(
    Memory *memory,
    uint32_t *sp,
    uint16_t value
)
{
    if (memory == NULL || sp == NULL) {
        return STACK_STATUS_INVALID_ARGUMENT;
    }

    StackStatus validation = validate_stack_pointer(*sp);

    if (validation != STACK_STATUS_OK) {
        return validation;
    }

    if (*sp < DIOCPU_STACK_BOTTOM + 2u) {
        return STACK_STATUS_OVERFLOW;
    }

    uint32_t new_sp = *sp - 2u;

    MemoryStatus status = memory_data_write16(
        memory,
        new_sp,
        value
    );

    if (status != MEMORY_STATUS_OK) {
        return STACK_STATUS_MEMORY_ERROR;
    }

    *sp = new_sp;

    return STACK_STATUS_OK;
}

StackStatus stack_pop16(
    const Memory *memory,
    uint32_t *sp,
    uint16_t *value
)
{
    if (memory == NULL || sp == NULL || value == NULL) {
        return STACK_STATUS_INVALID_ARGUMENT;
    }

    StackStatus validation = validate_stack_pointer(*sp);

    if (validation != STACK_STATUS_OK) {
        return validation;
    }

    if (*sp > DIOCPU_STACK_INITIAL_SP - 2u) {
        return STACK_STATUS_UNDERFLOW;
    }

    uint16_t result = 0;

    MemoryStatus status = memory_data_read16(
        memory,
        *sp,
        &result
    );

    if (status != MEMORY_STATUS_OK) {
        return STACK_STATUS_MEMORY_ERROR;
    }

    *value = result;
    *sp += 2u;

    return STACK_STATUS_OK;
}

StackStatus stack_call(
    Memory *memory,
    uint32_t *sp,
    uint32_t return_address
)
{
    if (memory == NULL || sp == NULL) {
        return STACK_STATUS_INVALID_ARGUMENT;
    }

    if (return_address > DIOCPU_MEMORY_MAX_ADDRESS) {
        return STACK_STATUS_INVALID_RETURN_ADDRESS;
    }

    StackStatus validation = validate_stack_pointer(*sp);

    if (validation != STACK_STATUS_OK) {
        return validation;
    }

    if (*sp < DIOCPU_STACK_BOTTOM + 4u) {
        return STACK_STATUS_OVERFLOW;
    }

    uint32_t new_sp = *sp - 4u;

    uint16_t low_word = (uint16_t)(return_address & 0xFFFFu);

    uint16_t high_word = (uint16_t)(return_address >> 16u);

    MemoryStatus status = memory_data_write16(
        memory,
        new_sp,
        low_word
    );

    if (status != MEMORY_STATUS_OK) {
        return STACK_STATUS_MEMORY_ERROR;
    }

    status = memory_data_write16(
        memory,
        new_sp + 2u,
        high_word
    );

    if (status != MEMORY_STATUS_OK) {
        return STACK_STATUS_MEMORY_ERROR;
    }

    *sp = new_sp;

    return STACK_STATUS_OK;
}

StackStatus stack_ret(
    const Memory *memory,
    uint32_t *sp,
    uint32_t *return_address
)
{
    if (memory == NULL || sp == NULL || return_address == NULL) {
        return STACK_STATUS_INVALID_ARGUMENT;
    }

    StackStatus validation = validate_stack_pointer(*sp);

    if (validation != STACK_STATUS_OK) {
        return validation;
    }

    if (*sp > DIOCPU_STACK_INITIAL_SP - 4u) {
        return STACK_STATUS_UNDERFLOW;
    }

    uint16_t low_word = 0;
    uint16_t high_word = 0;

    MemoryStatus status = memory_data_read16(
        memory,
        *sp,
        &low_word
    );

    if (status != MEMORY_STATUS_OK) {
        return STACK_STATUS_MEMORY_ERROR;
    }

    status = memory_data_read16(
        memory,
        *sp + 2u,
        &high_word
    );

    if (status != MEMORY_STATUS_OK) {
        return STACK_STATUS_MEMORY_ERROR;
    }

    uint32_t result =
        (uint32_t)low_word |
        ((uint32_t)high_word << 16u);

    if (result > DIOCPU_MEMORY_MAX_ADDRESS) {
        return STACK_STATUS_INVALID_RETURN_ADDRESS;
    }

    *return_address = result;
    *sp += 4u;

    return STACK_STATUS_OK;
}
