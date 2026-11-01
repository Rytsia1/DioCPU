#ifndef DIOCPU_MEMORY_H
#define DIOCPU_MEMORY_H

#include <stdint.h>

#define DIOCPU_MEMORY_SIZE 0x40000u
#define DIOCPU_MEMORY_MAX_ADDRESS (DIOCPU_MEMORY_SIZE - 1u)

typedef struct {
    uint8_t instruction[DIOCPU_MEMORY_SIZE];
    uint8_t data[DIOCPU_MEMORY_SIZE];
} Memory;

typedef enum {
    MEMORY_STATUS_OK = 0,
    MEMORY_STATUS_INVALID_ARGUMENT,
    MEMORY_STATUS_OUT_OF_RANGE
} MemoryStatus;

MemoryStatus memory_init(Memory *memory);

MemoryStatus memory_instruction_read8(
    const Memory *memory,
    uint32_t address,
    uint8_t *value
);

MemoryStatus memory_instruction_write8(
    Memory *memory,
    uint32_t address,
    uint8_t value
);

MemoryStatus memory_data_read16(
    const Memory *memory,
    uint32_t address,
    uint16_t *value
);

MemoryStatus memory_data_write16(
    Memory *memory,
    uint32_t address,
    uint16_t value
);

#endif
