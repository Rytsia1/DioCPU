#include "stack.h"

#include <stdio.h>

#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            fprintf(stderr, "FAILED: %s:%d: %s\n",             \
                    __FILE__, __LINE__, #condition);          \
            return 0;                                         \
        }                                                     \
    } while (0)


static int prepare(Memory *memory, uint32_t *sp)
{
    if (memory_init(memory) != MEMORY_STATUS_OK) {
        return 0;
    }

    if (stack_reset(sp) != STACK_STATUS_OK) {
        return 0;
    }

    return 1;
}


static int test_stack_reset(Memory *memory, uint32_t *sp)
{
    (void)memory;

    CHECK(stack_reset(sp) == STACK_STATUS_OK);
    CHECK(*sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}


static int test_push_pop16(Memory *memory, uint32_t *sp)
{
    CHECK(prepare(memory, sp));

    CHECK(stack_push16(memory, sp, 0x1234u) == STACK_STATUS_OK);

    CHECK(*sp == DIOCPU_STACK_INITIAL_SP - 2u);

    CHECK(memory->data[*sp] == 0x34);
    CHECK(memory->data[*sp + 1u] == 0x12);

    uint16_t value = 0;

    CHECK(stack_pop16(memory, sp, &value) == STACK_STATUS_OK);

    CHECK(value == 0x1234u);
    CHECK(*sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}


static int test_lifo_behavior(Memory *memory, uint32_t *sp)
{
    CHECK(prepare(memory, sp));

    uint16_t value = 0;

    CHECK(stack_push16(memory, sp, 100u) == STACK_STATUS_OK);
    CHECK(stack_push16(memory, sp, 200u) == STACK_STATUS_OK);
    CHECK(stack_push16(memory, sp, 300u) == STACK_STATUS_OK);

    CHECK(stack_pop16(memory, sp, &value) == STACK_STATUS_OK);
    CHECK(value == 300u);

    CHECK(stack_pop16(memory, sp, &value) == STACK_STATUS_OK);
    CHECK(value == 200u);

    CHECK(stack_pop16(memory, sp, &value) == STACK_STATUS_OK);
    CHECK(value == 100u);

    CHECK(*sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}


static int test_stack_underflow(Memory *memory, uint32_t *sp)
{
    CHECK(prepare(memory, sp));

    uint16_t value = 0xBEEFu;

    CHECK(
        stack_pop16(memory, sp, &value) ==
        STACK_STATUS_UNDERFLOW
    );

    CHECK(value == 0xBEEFu);
    CHECK(*sp == DIOCPU_STACK_INITIAL_SP);

    uint32_t return_address = 0x12345u;

    CHECK(
        stack_ret(memory, sp, &return_address) ==
        STACK_STATUS_UNDERFLOW
    );

    CHECK(return_address == 0x12345u);
    CHECK(*sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}


static int test_stack_overflow(Memory *memory, uint32_t *sp)
{
    CHECK(prepare(memory, sp));

    *sp = DIOCPU_STACK_BOTTOM + 2u;

    CHECK(stack_push16(memory, sp, 0xABCDu) == STACK_STATUS_OK);

    CHECK(*sp == DIOCPU_STACK_BOTTOM);

    uint8_t old_low = memory->data[DIOCPU_STACK_BOTTOM];
    uint8_t old_high = memory->data[DIOCPU_STACK_BOTTOM + 1u];

    CHECK(
        stack_push16(memory, sp, 0x1234u) ==
        STACK_STATUS_OVERFLOW
    );

    CHECK(*sp == DIOCPU_STACK_BOTTOM);

    CHECK(memory->data[DIOCPU_STACK_BOTTOM] == old_low);
    CHECK(memory->data[DIOCPU_STACK_BOTTOM + 1u] == old_high);

    return 1;
}


static int test_call_ret(Memory *memory, uint32_t *sp)
{
    CHECK(prepare(memory, sp));

    uint32_t expected_return_address = 0x23456u;
    uint32_t actual_return_address = 0;

    CHECK(
        stack_call(memory, sp, expected_return_address) ==
        STACK_STATUS_OK
    );

    CHECK(*sp == DIOCPU_STACK_INITIAL_SP - 4u);

    CHECK(memory->data[*sp] == 0x56);
    CHECK(memory->data[*sp + 1u] == 0x34);
    CHECK(memory->data[*sp + 2u] == 0x02);
    CHECK(memory->data[*sp + 3u] == 0x00);

    CHECK(
        stack_ret(memory, sp, &actual_return_address) ==
        STACK_STATUS_OK
    );

    CHECK(actual_return_address == expected_return_address);
    CHECK(*sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}


static int test_nested_stack_operations(Memory *memory, uint32_t *sp)
{
    CHECK(prepare(memory, sp));

    uint16_t value = 0;
    uint32_t return_address = 0;

    CHECK(stack_push16(memory, sp, 0xABCDu) == STACK_STATUS_OK);

    CHECK(stack_call(memory, sp, 0x12345u) == STACK_STATUS_OK);

    CHECK(stack_ret(memory, sp, &return_address) == STACK_STATUS_OK);

    CHECK(return_address == 0x12345u);

    CHECK(stack_pop16(memory, sp, &value) == STACK_STATUS_OK);

    CHECK(value == 0xABCDu);
    CHECK(*sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}


static int test_invalid_return_address(Memory *memory, uint32_t *sp)
{
    CHECK(prepare(memory, sp));

    CHECK(
        stack_call(memory, sp, 0x40000u) ==
        STACK_STATUS_INVALID_RETURN_ADDRESS
    );

    CHECK(*sp == DIOCPU_STACK_INITIAL_SP);

    CHECK(stack_call(memory, sp, 0x12345u) == STACK_STATUS_OK);

    memory->data[*sp + 2u] = 0x04;

    uint32_t return_address = 0xBEEFu;

    uint32_t old_sp = *sp;

    CHECK(
        stack_ret(memory, sp, &return_address) ==
        STACK_STATUS_INVALID_RETURN_ADDRESS
    );

    CHECK(*sp == old_sp);
    CHECK(return_address == 0xBEEFu);

    return 1;
}


static int test_invalid_arguments(Memory *memory, uint32_t *sp)
{
    uint16_t value = 0;
    uint32_t address = 0;

    CHECK(stack_reset(NULL) == STACK_STATUS_INVALID_ARGUMENT);

    CHECK(
        stack_push16(NULL, sp, 0) ==
        STACK_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        stack_push16(memory, NULL, 0) ==
        STACK_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        stack_pop16(NULL, sp, &value) ==
        STACK_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        stack_pop16(memory, sp, NULL) ==
        STACK_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        stack_call(NULL, sp, 0) ==
        STACK_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        stack_ret(NULL, sp, &address) ==
        STACK_STATUS_INVALID_ARGUMENT
    );

    return 1;
}


#define RUN_TEST(test_function)                            \
    do {                                                   \
        if (!test_function(&memory, &sp)) {                \
            return 1;                                      \
        }                                                  \
        printf("[PASS] %s\n", #test_function);             \
        passed++;                                          \
    } while (0)


int main(void)
{
    static Memory memory;

    uint32_t sp = 0;

    unsigned int passed = 0;

    RUN_TEST(test_stack_reset);
    RUN_TEST(test_push_pop16);
    RUN_TEST(test_lifo_behavior);
    RUN_TEST(test_stack_underflow);
    RUN_TEST(test_stack_overflow);
    RUN_TEST(test_call_ret);
    RUN_TEST(test_nested_stack_operations);
    RUN_TEST(test_invalid_return_address);
    RUN_TEST(test_invalid_arguments);

    printf("\nStack tests: %u passed.\n", passed);

    return 0;
}
