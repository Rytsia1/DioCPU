#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "alu.h"

static int total_tests = 0;
static int passed_tests = 0;

#define RUN_TEST(test)              \
    do {                            \
        total_tests++;              \
        test();                     \
        passed_tests++;             \
        printf("[PASS] %s\n", #test); \
    } while (0)

#define ASSERT_EQ(actual, expected) \
    assert((actual) == (expected))

#define ASSERT_FLAG(flags, mask, expected) \
    ASSERT_EQ(alu_get_flag((flags), (mask)), (expected))

/* ---------------------------------------------------------
 * ADD tests
 * --------------------------------------------------------- */

static void test_add_basic(void)
{
    ALUResult r = alu_execute(
        ALU_ADD,
        10,
        20,
        0
    );

    ASSERT_EQ(r.status, ALU_STATUS_OK);
    ASSERT_EQ(r.result, 30);
    ASSERT_FLAG(r.flags, CPU_FLAG_ZF, false);
    ASSERT_FLAG(r.flags, CPU_FLAG_CF, false);
    ASSERT_FLAG(r.flags, CPU_FLAG_SF, false);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, false);
}

static void test_add_zero(void)
{
    ALUResult r = alu_execute(
        ALU_ADD,
        0,
        0,
        0
    );

    ASSERT_EQ(r.result, 0);
    ASSERT_FLAG(r.flags, CPU_FLAG_ZF, true);
}

static void test_add_unsigned_carry(void)
{
    ALUResult r = alu_execute(
        ALU_ADD,
        0xFFFF,
        1,
        0
    );

    ASSERT_EQ(r.result, 0);
    ASSERT_FLAG(r.flags, CPU_FLAG_ZF, true);
    ASSERT_FLAG(r.flags, CPU_FLAG_CF, true);
}

static void test_add_signed_overflow(void)
{
    /*
     * 32767 + 1 = -32768 (16-bit signed)
     */

    ALUResult r = alu_execute(
        ALU_ADD,
        0x7FFF,
        0x0001,
        0
    );

    ASSERT_EQ(r.result, 0x8000);
    ASSERT_FLAG(r.flags, CPU_FLAG_SF, true);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, true);
}

static void test_add_negative_no_overflow(void)
{
    /*
     * -1 + 1 = 0
     */

    ALUResult r = alu_execute(
        ALU_ADD,
        0xFFFF,
        0x0001,
        0
    );

    ASSERT_EQ(r.result, 0);
    ASSERT_FLAG(r.flags, CPU_FLAG_ZF, true);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, false);
}

/* ---------------------------------------------------------
 * SUB tests
 * --------------------------------------------------------- */

static void test_sub_basic(void)
{
    ALUResult r = alu_execute(
        ALU_SUB,
        50,
        20,
        0
    );

    ASSERT_EQ(r.result, 30);
    ASSERT_FLAG(r.flags, CPU_FLAG_CF, false);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, false);
}

static void test_sub_borrow(void)
{
    ALUResult r = alu_execute(
        ALU_SUB,
        10,
        20,
        0
    );

    ASSERT_EQ(r.result, 0xFFF6);
    ASSERT_FLAG(r.flags, CPU_FLAG_CF, true);
    ASSERT_FLAG(r.flags, CPU_FLAG_SF, true);
}

static void test_sub_zero(void)
{
    ALUResult r = alu_execute(
        ALU_SUB,
        25,
        25,
        0
    );

    ASSERT_EQ(r.result, 0);
    ASSERT_FLAG(r.flags, CPU_FLAG_ZF, true);
    ASSERT_FLAG(r.flags, CPU_FLAG_CF, false);
}

static void test_sub_signed_overflow(void)
{
    /*
     * -32768 - 1 = 32767 (16-bit signed)
     */

    ALUResult r = alu_execute(
        ALU_SUB,
        0x8000,
        0x0001,
        0
    );

    ASSERT_EQ(r.result, 0x7FFF);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, true);
    ASSERT_FLAG(r.flags, CPU_FLAG_SF, false);
}

/* ---------------------------------------------------------
 * CMP tests
 * --------------------------------------------------------- */

static void test_cmp_equal(void)
{
    ALUResult r = alu_execute(
        ALU_CMP,
        100,
        100,
        0
    );

    ASSERT_EQ(r.result, 0);
    ASSERT_FLAG(r.flags, CPU_FLAG_ZF, true);
}

static void test_cmp_less_than(void)
{
    ALUResult r = alu_execute(
        ALU_CMP,
        10,
        20,
        0
    );

    ASSERT_EQ(r.result, 0xFFF6);
    ASSERT_FLAG(r.flags, CPU_FLAG_CF, true);
}

/* ---------------------------------------------------------
 * MUL tests
 * --------------------------------------------------------- */

static void test_mul_basic(void)
{
    ALUResult r = alu_execute(
        ALU_MUL,
        10,
        20,
        0
    );

    ASSERT_EQ(r.result, 200);
    ASSERT_FLAG(r.flags, CPU_FLAG_CF, false);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, false);
}

static void test_mul_unsigned_overflow(void)
{
    /*
     * 0xFFFF * 2 = 0x1FFFE
     * Low 16 bits = 0xFFFE
     */

    ALUResult r = alu_execute(
        ALU_MUL,
        0xFFFF,
        2,
        0
    );

    ASSERT_EQ(r.result, 0xFFFE);
    ASSERT_FLAG(r.flags, CPU_FLAG_CF, true);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, false);
}

/* ---------------------------------------------------------
 * MULS tests
 * --------------------------------------------------------- */

static void test_muls_basic(void)
{
    /*
     * -2 * 3 = -6
     */

    ALUResult r = alu_execute(
        ALU_MULS,
        0xFFFE,
        3,
        0
    );

    ASSERT_EQ(r.result, 0xFFFA);
    ASSERT_FLAG(r.flags, CPU_FLAG_SF, true);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, false);
}

static void test_muls_overflow(void)
{
    /*
     * 32767 * 2 = 65534
     * Signed 16-bit overflow.
     */

    ALUResult r = alu_execute(
        ALU_MULS,
        0x7FFF,
        2,
        0
    );

    ASSERT_EQ(r.result, 0xFFFE);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, true);
}

/* ---------------------------------------------------------
 * DIV tests
 * --------------------------------------------------------- */

static void test_div_basic(void)
{
    /*
     * 100 / 7 = 14 remainder 2
     */

    ALUResult r = alu_execute(
        ALU_DIV,
        100,
        7,
        0
    );

    ASSERT_EQ(r.status, ALU_STATUS_OK);
    ASSERT_EQ(r.result, 14);
    ASSERT_EQ(r.remainder, 2);
}

static void test_div_zero_quotient(void)
{
    /*
     * 3 / 10 = 0 remainder 3
     */

    ALUResult r = alu_execute(
        ALU_DIV,
        3,
        10,
        0
    );

    ASSERT_EQ(r.result, 0);
    ASSERT_EQ(r.remainder, 3);
    ASSERT_FLAG(r.flags, CPU_FLAG_ZF, true);
}

static void test_div_by_zero(void)
{
    uint16_t initial_flags =
        CPU_FLAG_ZF | CPU_FLAG_CF;

    ALUResult r = alu_execute(
        ALU_DIV,
        100,
        0,
        initial_flags
    );

    ASSERT_EQ(
        r.status,
        ALU_STATUS_DIVIDE_BY_ZERO
    );

    ASSERT_EQ(r.flags, initial_flags);
}

/* ---------------------------------------------------------
 * DIVS tests
 * --------------------------------------------------------- */

static void test_divs_basic(void)
{
    /*
     * -100 / 7 = -14 remainder -2
     */

    ALUResult r = alu_execute(
        ALU_DIVS,
        0xFF9C,
        7,
        0
    );

    ASSERT_EQ(r.status, ALU_STATUS_OK);
    ASSERT_EQ(r.result, 0xFFF2);
    ASSERT_EQ(r.remainder, 0xFFFE);
}

static void test_divs_overflow(void)
{
    /*
     * -32768 / -1 is not representable
     * in signed 16-bit.
     */

    ALUResult r = alu_execute(
        ALU_DIVS,
        0x8000,
        0xFFFF,
        0
    );

    ASSERT_EQ(
        r.status,
        ALU_STATUS_ARITHMETIC_OVERFLOW
    );
}

/* ---------------------------------------------------------
 * Logical tests
 * --------------------------------------------------------- */

static void test_and(void)
{
    ALUResult r = alu_execute(
        ALU_AND,
        0x00FF,
        0x0F0F,
        0
    );

    ASSERT_EQ(r.result, 0x000F);
    ASSERT_FLAG(r.flags, CPU_FLAG_CF, false);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, false);
}

static void test_or(void)
{
    ALUResult r = alu_execute(
        ALU_OR,
        0x00F0,
        0x000F,
        0
    );

    ASSERT_EQ(r.result, 0x00FF);
}

static void test_xor(void)
{
    ALUResult r = alu_execute(
        ALU_XOR,
        0xFFFF,
        0xFFFF,
        0
    );

    ASSERT_EQ(r.result, 0);
    ASSERT_FLAG(r.flags, CPU_FLAG_ZF, true);
}

static void test_not(void)
{
    uint16_t initial_flags =
        CPU_FLAG_CF | CPU_FLAG_OF;

    ALUResult r = alu_execute(
        ALU_NOT,
        0x0000,
        0,
        initial_flags
    );

    ASSERT_EQ(r.result, 0xFFFF);

    /* NOT preserves CF and OF */
    ASSERT_FLAG(r.flags, CPU_FLAG_CF, true);
    ASSERT_FLAG(r.flags, CPU_FLAG_OF, true);

    ASSERT_FLAG(r.flags, CPU_FLAG_SF, true);
    ASSERT_FLAG(r.flags, CPU_FLAG_ZF, false);
}

/* ---------------------------------------------------------
 * Reserved FLAGS tests
 * --------------------------------------------------------- */

static void test_reserved_flags_zero(void)
{
    ALUResult r = alu_execute(
        ALU_ADD,
        1,
        1,
        0xFFF0
    );

    ASSERT_EQ(
        r.flags & (uint16_t)~CPU_FLAGS_DEFINED_MASK,
        0
    );
}

/* ---------------------------------------------------------
 * Main
 * --------------------------------------------------------- */

int main(void)
{
    printf("=== DioCPU v2 ALU Tests ===\n\n");

    RUN_TEST(test_add_basic);
    RUN_TEST(test_add_zero);
    RUN_TEST(test_add_unsigned_carry);
    RUN_TEST(test_add_signed_overflow);
    RUN_TEST(test_add_negative_no_overflow);

    RUN_TEST(test_sub_basic);
    RUN_TEST(test_sub_borrow);
    RUN_TEST(test_sub_zero);
    RUN_TEST(test_sub_signed_overflow);

    RUN_TEST(test_cmp_equal);
    RUN_TEST(test_cmp_less_than);

    RUN_TEST(test_mul_basic);
    RUN_TEST(test_mul_unsigned_overflow);

    RUN_TEST(test_muls_basic);
    RUN_TEST(test_muls_overflow);

    RUN_TEST(test_div_basic);
    RUN_TEST(test_div_zero_quotient);
    RUN_TEST(test_div_by_zero);

    RUN_TEST(test_divs_basic);
    RUN_TEST(test_divs_overflow);

    RUN_TEST(test_and);
    RUN_TEST(test_or);
    RUN_TEST(test_xor);
    RUN_TEST(test_not);

    RUN_TEST(test_reserved_flags_zero);

    printf(
        "\nResult: %d/%d tests passed\n",
        passed_tests,
        total_tests
    );

    return 0;
}
