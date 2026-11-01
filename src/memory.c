#include "memory.h"

#include <string.h>

MemoryStatus memory_init(Memory *memory)
{
    if (memory == NULL) {
        return MEMORY_STATUS_INVALID_ARGUMENT;
    }

    memset(memory, 0, sizeof(*memory));

    return MEMORY_STATUS_OK;
}

MemoryStatus memory_instruction_read8(
    const Memory *memory,
    uint32_t address,
    uint8_t *value
)
{
    if (memory == NULL || value == NULL) {
        return MEMORY_STATUS_INVALID_ARGUMENT;
    }

    if (address > DIOCPU_MEMORY_MAX_ADDRESS) {
        return MEMORY_STATUS_OUT_OF_RANGE;
    }

    *value = memory->instruction[address];

    return MEMORY_STATUS_OK;
}

MemoryStatus memory_instruction_write8(
    Memory *memory,
    uint32_t address,
    uint8_t value
)
{
    if (memory == NULL) {
        return MEMORY_STATUS_INVALID_ARGUMENT;
    }

    if (address > DIOCPU_MEMORY_MAX_ADDRESS) {
        return MEMORY_STATUS_OUT_OF_RANGE;
    }

    memory->instruction[address] = value;

    return MEMORY_STATUS_OK;
}

MemoryStatus memory_data_read16(
    const Memory *memory,
    uint32_t address,
    uint16_t *value
)
{
    if (memory == NULL || value == NULL) {
        return MEMORY_STATUS_INVALID_ARGUMENT;
    }

    if (address > DIOCPU_MEMORY_SIZE - 2u) {
        return MEMORY_STATUS_OUT_OF_RANGE;
    }

    uint16_t low = memory->data[address];
    uint16_t high = memory->data[address + 1u];

    *value = (uint16_t)(low | (high << 8));

    return MEMORY_STATUS_OK;
}

MemoryStatus memory_data_write16(
    Memory *memory,
    uint32_t address,
    uint16_t value
)
{
    if (memory == NULL) {
        return MEMORY_STATUS_INVALID_ARGUMENT;
    }

    if (address > DIOCPU_MEMORY_SIZE - 2u) {
        return MEMORY_STATUS_OUT_OF_RANGE;
    }

    memory->data[address] = (uint8_t)(value & 0x00FFu);

    memory->data[address + 1u] = (uint8_t)(value >> 8);

    return MEMORY_STATUS_OK;
}
