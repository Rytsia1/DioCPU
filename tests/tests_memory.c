#include "memory.h"

#include <stdio.h>

#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            fprintf(stderr, "FAILED: %s:%d: %s\n",             \
                    __FILE__, __LINE__, #condition);          \
            return 0;                                         \
        }                                                     \
    } while (0)


static int test_memory_initialization(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    for (uint32_t i = 0; i < DIOCPU_MEMORY_SIZE; i++) {
        CHECK(memory->instruction[i] == 0);
        CHECK(memory->data[i] == 0);
    }

    return 1;
}


static int test_instruction_memory(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    uint8_t value = 0;

    CHECK(
        memory_instruction_write8(memory, 0x100u, 0xAB) ==
        MEMORY_STATUS_OK
    );

    CHECK(
        memory_instruction_read8(memory, 0x100u, &value) ==
        MEMORY_STATUS_OK
    );

    CHECK(value == 0xAB);

    CHECK(
        memory_instruction_write8(
            memory,
            DIOCPU_MEMORY_MAX_ADDRESS,
            0x55
        ) == MEMORY_STATUS_OK
    );

    CHECK(
        memory_instruction_read8(
            memory,
            DIOCPU_MEMORY_SIZE,
            &value
        ) == MEMORY_STATUS_OUT_OF_RANGE
    );

    return 1;
}


static int test_data_memory_little_endian(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    uint16_t value = 0;

    CHECK(
        memory_data_write16(memory, 0x100u, 0x1234u) ==
        MEMORY_STATUS_OK
    );

    CHECK(memory->data[0x100u] == 0x34);
    CHECK(memory->data[0x101u] == 0x12);

    CHECK(
        memory_data_read16(memory, 0x100u, &value) ==
        MEMORY_STATUS_OK
    );

    CHECK(value == 0x1234u);

    return 1;
}


static int test_unaligned_access(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    uint16_t value = 0;

    CHECK(
        memory_data_write16(memory, 0x101u, 0xABCDu) ==
        MEMORY_STATUS_OK
    );

    CHECK(
        memory_data_read16(memory, 0x101u, &value) ==
        MEMORY_STATUS_OK
    );

    CHECK(value == 0xABCDu);

    return 1;
}


static int test_harvard_separation(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    uint8_t instruction_value = 0;

    CHECK(
        memory_instruction_write8(memory, 0x100u, 0x55) ==
        MEMORY_STATUS_OK
    );

    CHECK(
        memory_data_write16(memory, 0x100u, 0x1234u) ==
        MEMORY_STATUS_OK
    );

    CHECK(
        memory_instruction_read8(
            memory,
            0x100u,
            &instruction_value
        ) == MEMORY_STATUS_OK
    );

    CHECK(instruction_value == 0x55);

    CHECK(memory->data[0x100u] == 0x34);
    CHECK(memory->data[0x101u] == 0x12);

    return 1;
}


static int test_memory_boundary(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    uint32_t last_address = DIOCPU_MEMORY_MAX_ADDRESS;

    uint16_t value = 0xBEEFu;

    CHECK(
        memory_data_write16(
            memory,
            last_address - 1u,
            0xCAFEu
        ) == MEMORY_STATUS_OK
    );

    CHECK(memory->data[last_address - 1u] == 0xFE);
    CHECK(memory->data[last_address] == 0xCA);

    memory->data[last_address] = 0xAA;

    CHECK(
        memory_data_write16(
            memory,
            last_address,
            0x1234u
        ) == MEMORY_STATUS_OUT_OF_RANGE
    );

    CHECK(memory->data[last_address] == 0xAA);

    CHECK(
        memory_data_read16(
            memory,
            last_address,
            &value
        ) == MEMORY_STATUS_OUT_OF_RANGE
    );

    CHECK(value == 0xBEEFu);

    return 1;
}


static int test_invalid_arguments(Memory *memory)
{
    uint8_t byte = 0;
    uint16_t word = 0;

    CHECK(memory_init(NULL) == MEMORY_STATUS_INVALID_ARGUMENT);

    CHECK(
        memory_instruction_read8(NULL, 0, &byte) ==
        MEMORY_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        memory_instruction_read8(memory, 0, NULL) ==
        MEMORY_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        memory_instruction_write8(NULL, 0, 0) ==
        MEMORY_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        memory_data_read16(NULL, 0, &word) ==
        MEMORY_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        memory_data_read16(memory, 0, NULL) ==
        MEMORY_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        memory_data_write16(NULL, 0, 0) ==
        MEMORY_STATUS_INVALID_ARGUMENT
    );

    return 1;
}


#define RUN_TEST(test_function)                  \
    do {                                         \
        if (!test_function(&memory)) {           \
            return 1;                            \
        }                                        \
        printf("[PASS] %s\n", #test_function);   \
        passed++;                                \
    } while (0)


int main(void)
{
    static Memory memory;

    unsigned int passed = 0;

    RUN_TEST(test_memory_initialization);
    RUN_TEST(test_instruction_memory);
    RUN_TEST(test_data_memory_little_endian);
    RUN_TEST(test_unaligned_access);
    RUN_TEST(test_harvard_separation);
    RUN_TEST(test_memory_boundary);
    RUN_TEST(test_invalid_arguments);

    printf("\nMemory tests: %u passed.\n", passed);

    return 0;
}
