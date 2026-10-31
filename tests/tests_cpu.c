#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include "cpu.h"

static int total_tests = 0;
static int passed_tests = 0;
static int failed_tests = 0;

#define ASSERT_TRUE(condition) do { \
    if (!(condition)) { \
        printf("FAILED (line %d: %s)\n", __LINE__, #condition); \
        failed_tests++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("FAILED (line %d: %s == %s [%u != %u])\n", __LINE__, #a, #b, (unsigned)(a), (unsigned)(b)); \
        failed_tests++; \
        return; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    total_tests++; \
    int prev_failed = failed_tests; \
    fn(); \
    if (failed_tests == prev_failed) { \
        passed_tests++; \
    } \
} while (0)

static void test_cpu_init(void) {
    CPU cpu;
    cpu_init(&cpu);

    for (int i = 0; i < REGISTER_COUNT; i++) {
        ASSERT_EQ(cpu.registers[i], 0);
    }
    ASSERT_EQ(cpu.pc, 0);
    ASSERT_EQ(cpu.sp, MEMORY_SIZE - 1);
    ASSERT_TRUE(!cpu.zero_flag);
    ASSERT_TRUE(!cpu.carry_flag);
    ASSERT_TRUE(!cpu.halted);
}

static void test_load_basic(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.memory[0] = OP_LOAD;
    cpu.memory[1] = 0;
    cpu.memory[2] = 42;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_EQ(cpu.registers[0], 42);
    ASSERT_EQ(cpu.pc, 3);
}

static void test_load_zero_flag_set(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.memory[0] = OP_LOAD;
    cpu.memory[1] = 1;
    cpu.memory[2] = 0;

    cpu_step(&cpu);
    ASSERT_TRUE(cpu.zero_flag);
}

static void test_load_zero_flag_cleared(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.zero_flag = true;
    cpu.memory[0] = OP_LOAD;
    cpu.memory[1] = 2;
    cpu.memory[2] = 10;

    cpu_step(&cpu);
    ASSERT_TRUE(!cpu.zero_flag);
}

static void test_load_invalid_register(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.memory[0] = OP_LOAD;
    cpu.memory[1] = REGISTER_COUNT;
    cpu.memory[2] = 10;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(!ok);
    ASSERT_TRUE(cpu.halted);
}

static void test_add_basic(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.registers[0] = 15;
    cpu.registers[1] = 25;
    cpu.memory[0] = OP_ADD;
    cpu.memory[1] = 0;
    cpu.memory[2] = 1;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_EQ(cpu.registers[0], 40);
    ASSERT_TRUE(!cpu.carry_flag);
    ASSERT_TRUE(!cpu.zero_flag);
}

static void test_add_zero_flag(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.registers[0] = 0;
    cpu.registers[1] = 0;
    cpu.memory[0] = OP_ADD;
    cpu.memory[1] = 0;
    cpu.memory[2] = 1;

    cpu_step(&cpu);
    ASSERT_EQ(cpu.registers[0], 0);
    ASSERT_TRUE(cpu.zero_flag);
}

static void test_add_carry_flag(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.registers[0] = 200;
    cpu.registers[1] = 100;
    cpu.memory[0] = OP_ADD;
    cpu.memory[1] = 0;
    cpu.memory[2] = 1;

    cpu_step(&cpu);
    ASSERT_EQ(cpu.registers[0], (uint8_t)(200 + 100));
    ASSERT_TRUE(cpu.carry_flag);
    ASSERT_TRUE(!cpu.zero_flag);
}

static void test_add_invalid_register(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.memory[0] = OP_ADD;
    cpu.memory[1] = 0;
    cpu.memory[2] = REGISTER_COUNT;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(!ok);
    ASSERT_TRUE(cpu.halted);
}

static void test_sub_basic(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.registers[0] = 50;
    cpu.registers[1] = 20;
    cpu.memory[0] = OP_SUB;
    cpu.memory[1] = 0;
    cpu.memory[2] = 1;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_EQ(cpu.registers[0], 30);
    ASSERT_TRUE(!cpu.carry_flag);
    ASSERT_TRUE(!cpu.zero_flag);
}

static void test_sub_zero_flag(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.registers[0] = 25;
    cpu.registers[1] = 25;
    cpu.memory[0] = OP_SUB;
    cpu.memory[1] = 0;
    cpu.memory[2] = 1;

    cpu_step(&cpu);
    ASSERT_EQ(cpu.registers[0], 0);
    ASSERT_TRUE(cpu.zero_flag);
    ASSERT_TRUE(!cpu.carry_flag);
}

static void test_sub_carry_flag_borrow(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.registers[0] = 10;
    cpu.registers[1] = 20;
    cpu.memory[0] = OP_SUB;
    cpu.memory[1] = 0;
    cpu.memory[2] = 1;

    cpu_step(&cpu);
    ASSERT_EQ(cpu.registers[0], (uint8_t)(10 - 20));
    ASSERT_TRUE(cpu.carry_flag);
}

static void test_sub_invalid_register(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.memory[0] = OP_SUB;
    cpu.memory[1] = REGISTER_COUNT;
    cpu.memory[2] = 0;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(!ok);
    ASSERT_TRUE(cpu.halted);
}

static void test_jmp(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.memory[0] = OP_JMP;
    cpu.memory[1] = 0x12;
    cpu.memory[2] = 0;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_EQ(cpu.pc, 0x12);
}

static void test_jz_taken(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.zero_flag = true;
    cpu.memory[0] = OP_JZ;
    cpu.memory[1] = 0x15;
    cpu.memory[2] = 0;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_EQ(cpu.pc, 0x15);
}

static void test_jz_not_taken(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.zero_flag = false;
    cpu.memory[0] = OP_JZ;
    cpu.memory[1] = 0x15;
    cpu.memory[2] = 0;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_EQ(cpu.pc, 3);
}

static void test_jnz_taken(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.zero_flag = false;
    cpu.memory[0] = OP_JNZ;
    cpu.memory[1] = 0x18;
    cpu.memory[2] = 0;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_EQ(cpu.pc, 0x18);
}

static void test_jnz_not_taken(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.zero_flag = true;
    cpu.memory[0] = OP_JNZ;
    cpu.memory[1] = 0x18;
    cpu.memory[2] = 0;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_EQ(cpu.pc, 3);
}

static void test_out_register(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.registers[0] = 77;
    cpu.memory[0] = OP_OUT;
    cpu.memory[1] = OUT_MODE_REGISTER;
    cpu.memory[2] = 0;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(!cpu.halted);
}

static void test_out_immediate(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.memory[0] = OP_OUT;
    cpu.memory[1] = OUT_MODE_IMMEDIATE;
    cpu.memory[2] = 88;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(!cpu.halted);
}

static void test_out_invalid_mode(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.memory[0] = OP_OUT;
    cpu.memory[1] = 0x05;
    cpu.memory[2] = 0;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(!ok);
    ASSERT_TRUE(cpu.halted);
}

static void test_out_invalid_register(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.memory[0] = OP_OUT;
    cpu.memory[1] = OUT_MODE_REGISTER;
    cpu.memory[2] = REGISTER_COUNT;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(!ok);
    ASSERT_TRUE(cpu.halted);
}

static void test_hlt(void) {
    CPU cpu;
    cpu_init(&cpu);
    cpu.memory[0] = OP_HLT;
    cpu.memory[1] = 0;
    cpu.memory[2] = 0;

    bool ok = cpu_step(&cpu);
    ASSERT_TRUE(!ok);
    ASSERT_TRUE(cpu.halted);
}

static void test_cpu_run(void) {
    CPU cpu;
    cpu_init(&cpu);
    uint8_t program[] = {
        OP_LOAD, 0, 3,
        OP_LOAD, 1, 1,
        OP_SUB,  0, 1,
        OP_JNZ,  6, 0,
        OP_HLT,  0, 0
    };
    for (size_t i = 0; i < sizeof(program); i++) {
        cpu.memory[i] = program[i];
    }

    cpu_run(&cpu);
    ASSERT_TRUE(cpu.halted);
    ASSERT_EQ(cpu.registers[0], 0);
    ASSERT_EQ(cpu.registers[1], 1);
    ASSERT_TRUE(cpu.zero_flag);
}

int main(void) {
    printf("=== Running Dio-8 CPU Unit Tests ===\n\n");

    RUN_TEST(test_cpu_init);
    RUN_TEST(test_load_basic);
    RUN_TEST(test_load_zero_flag_set);
    RUN_TEST(test_load_zero_flag_cleared);
    RUN_TEST(test_load_invalid_register);
    RUN_TEST(test_add_basic);
    RUN_TEST(test_add_zero_flag);
    RUN_TEST(test_add_carry_flag);
    RUN_TEST(test_add_invalid_register);
    RUN_TEST(test_sub_basic);
    RUN_TEST(test_sub_zero_flag);
    RUN_TEST(test_sub_carry_flag_borrow);
    RUN_TEST(test_sub_invalid_register);
    RUN_TEST(test_jmp);
    RUN_TEST(test_jz_taken);
    RUN_TEST(test_jz_not_taken);
    RUN_TEST(test_jnz_taken);
    RUN_TEST(test_jnz_not_taken);
    RUN_TEST(test_out_register);
    RUN_TEST(test_out_immediate);
    RUN_TEST(test_out_invalid_mode);
    RUN_TEST(test_out_invalid_register);
    RUN_TEST(test_hlt);
    RUN_TEST(test_cpu_run);

    printf("\nResult: %d tests, %d failed, %d passed\n",
           total_tests, failed_tests, passed_tests);

    return (failed_tests == 0) ? 0 : 1;
}
