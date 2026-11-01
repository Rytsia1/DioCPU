#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include "cpu_v2.h"

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

// 1. Inisialisasi awal CPU State (16 register, PC, SP 0x3FFFE, MAR, FLAGS, status RUNNING)
static void test_v2_initial_state(void)
{
    CpuV2 cpu;
    cpu_v2_init(&cpu);

    for (int i = 0; i < V2_REGISTER_COUNT; i++)
    {
        ASSERT_EQ(cpu.r[i], 0);
    }

    ASSERT_EQ(cpu.pc, 0);
    ASSERT_EQ(cpu.sp, V2_INITIAL_SP);
    ASSERT_EQ(cpu.mar, 0);
    ASSERT_EQ(cpu.flags, 0);
    ASSERT_EQ(cpu.status, CPU_STATUS_RUNNING);
    ASSERT_EQ(cpu.fault.type, FAULT_NONE);
    ASSERT_EQ(cpu.fault.address, 0);
}

// 2. Pembacaan dan penulisan register 16-bit (R0 - R15)
static void test_v2_register_read_write(void)
{
    CpuV2 cpu;
    cpu_v2_init(&cpu);

    for (uint8_t i = 0; i < V2_REGISTER_COUNT; i++)
    {
        uint16_t test_val = (uint16_t)(0x1000u * i + 0x00FFu);
        ASSERT_TRUE(cpu_v2_set_register(&cpu, i, test_val));

        uint16_t read_val = 0;
        ASSERT_TRUE(cpu_v2_get_register(&cpu, i, &read_val));
        ASSERT_EQ(read_val, test_val);
    }
}

// 3. Validasi batas register (indeks >= 16 memicu FAULT_INVALID_REGISTER)
static void test_v2_invalid_register_access(void)
{
    CpuV2 cpu;
    cpu_v2_init(&cpu);
    cpu.pc = 0x0010;

    ASSERT_TRUE(!cpu_v2_set_register(&cpu, 16, 0x1234));
    ASSERT_EQ(cpu.status, CPU_STATUS_FAULTED);
    ASSERT_EQ(cpu.fault.type, FAULT_INVALID_REGISTER);
    ASSERT_EQ(cpu.fault.address, 0x0010);

    uint16_t val = 0;
    ASSERT_TRUE(!cpu_v2_get_register(&cpu, 16, &val));
}

// 4. Operasi bitmask Zero Flag (ZF)
static void test_v2_flag_zero(void)
{
    CpuV2 cpu;
    cpu_v2_init(&cpu);

    ASSERT_TRUE(!cpu_v2_get_flag(&cpu, FLAG_ZF));

    cpu_v2_set_flag(&cpu, FLAG_ZF, true);
    ASSERT_TRUE(cpu_v2_get_flag(&cpu, FLAG_ZF));
    ASSERT_EQ(cpu.flags, FLAG_ZF);

    cpu_v2_set_flag(&cpu, FLAG_ZF, false);
    ASSERT_TRUE(!cpu_v2_get_flag(&cpu, FLAG_ZF));
    ASSERT_EQ(cpu.flags, 0);
}

// 5. Operasi bitmask Carry Flag (CF)
static void test_v2_flag_carry(void)
{
    CpuV2 cpu;
    cpu_v2_init(&cpu);

    ASSERT_TRUE(!cpu_v2_get_flag(&cpu, FLAG_CF));

    cpu_v2_set_flag(&cpu, FLAG_CF, true);
    ASSERT_TRUE(cpu_v2_get_flag(&cpu, FLAG_CF));
    ASSERT_EQ(cpu.flags, FLAG_CF);

    cpu_v2_set_flag(&cpu, FLAG_CF, false);
    ASSERT_TRUE(!cpu_v2_get_flag(&cpu, FLAG_CF));
}

// 6. Operasi bitmask Sign Flag (SF)
static void test_v2_flag_sign(void)
{
    CpuV2 cpu;
    cpu_v2_init(&cpu);

    ASSERT_TRUE(!cpu_v2_get_flag(&cpu, FLAG_SF));

    cpu_v2_set_flag(&cpu, FLAG_SF, true);
    ASSERT_TRUE(cpu_v2_get_flag(&cpu, FLAG_SF));
    ASSERT_EQ(cpu.flags, FLAG_SF);

    cpu_v2_set_flag(&cpu, FLAG_SF, false);
    ASSERT_TRUE(!cpu_v2_get_flag(&cpu, FLAG_SF));
}

// 7. Operasi bitmask Overflow Flag (OF)
static void test_v2_flag_overflow(void)
{
    CpuV2 cpu;
    cpu_v2_init(&cpu);

    ASSERT_TRUE(!cpu_v2_get_flag(&cpu, FLAG_OF));

    cpu_v2_set_flag(&cpu, FLAG_OF, true);
    ASSERT_TRUE(cpu_v2_get_flag(&cpu, FLAG_OF));
    ASSERT_EQ(cpu.flags, FLAG_OF);

    cpu_v2_set_flag(&cpu, FLAG_OF, false);
    ASSERT_TRUE(!cpu_v2_get_flag(&cpu, FLAG_OF));
}

// 8. Batas alamat 18-bit untuk PC, SP, dan MAR (0x00000 - 0x3FFFF)
static void test_v2_address_bounds_18bit(void)
{
    CpuV2 cpu;
    cpu_v2_init(&cpu);

    ASSERT_TRUE(cpu_v2_set_pc(&cpu, 0x0003FFFF));
    ASSERT_EQ(cpu.pc, 0x0003FFFF);

    ASSERT_TRUE(cpu_v2_set_sp(&cpu, 0x0003FFFE));
    ASSERT_EQ(cpu.sp, 0x0003FFFE);

    ASSERT_TRUE(cpu_v2_set_mar(&cpu, 0x0001ABCD));
    ASSERT_EQ(cpu.mar, 0x0001ABCD);

    // Alamat di luar 18-bit (> 0x3FFFF) memicu FAULT_OUT_OF_BOUNDS
    ASSERT_TRUE(!cpu_v2_set_pc(&cpu, 0x00040000));
    ASSERT_EQ(cpu.status, CPU_STATUS_FAULTED);
    ASSERT_EQ(cpu.fault.type, FAULT_OUT_OF_BOUNDS);
}

// 9. Penanganan dan perekaman Fault CPU
static void test_v2_fault_handling(void)
{
    CpuV2 cpu;
    cpu_v2_init(&cpu);

    cpu_v2_trigger_fault(&cpu, FAULT_INVALID_OPCODE, 0x00001234);

    ASSERT_EQ(cpu.status, CPU_STATUS_FAULTED);
    ASSERT_EQ(cpu.fault.type, FAULT_INVALID_OPCODE);
    ASSERT_EQ(cpu.fault.address, 0x00001234);
}

int main(void)
{
    printf("=== Running DioCPU v2 (V2-2: Register File & CPU State) Unit Tests ===\n\n");

    RUN_TEST(test_v2_initial_state);
    RUN_TEST(test_v2_register_read_write);
    RUN_TEST(test_v2_invalid_register_access);
    RUN_TEST(test_v2_flag_zero);
    RUN_TEST(test_v2_flag_carry);
    RUN_TEST(test_v2_flag_sign);
    RUN_TEST(test_v2_flag_overflow);
    RUN_TEST(test_v2_address_bounds_18bit);
    RUN_TEST(test_v2_fault_handling);

    printf("\nResult: %d tests, %d failed, %d passed\n",
           total_tests, failed_tests, passed_tests);

    return (failed_tests == 0) ? 0 : 1;
}
