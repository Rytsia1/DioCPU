#include "alu.h"

#include <limits.h>
#include <stdint.h>

/* ---------------------------------------------------------
 * FLAGS helpers
 * --------------------------------------------------------- */

bool alu_get_flag(uint16_t flags, uint16_t mask)
{
    return (flags & mask) != 0;
}

uint16_t alu_set_flag(
    uint16_t flags,
    uint16_t mask,
    bool value
)
{
    if (value) {
        flags |= mask;
    } else {
        flags &= (uint16_t)~mask;
    }

    return flags;
}

/* ---------------------------------------------------------
 * Internal helpers
 * --------------------------------------------------------- */

static int32_t alu_to_signed(uint16_t value)
{
    if ((value & 0x8000u) != 0) {
        return (int32_t)value - 0x10000;
    }

    return (int32_t)value;
}

static uint16_t alu_update_zf_sf(
    uint16_t flags,
    uint16_t result
)
{
    flags = alu_set_flag(
        flags,
        CPU_FLAG_ZF,
        result == 0
    );

    flags = alu_set_flag(
        flags,
        CPU_FLAG_SF,
        (result & 0x8000u) != 0
    );

    return flags;
}

static uint16_t alu_update_arithmetic_flags(
    uint16_t flags,
    uint16_t result,
    bool carry,
    bool overflow
)
{
    flags = alu_update_zf_sf(flags, result);

    flags = alu_set_flag(
        flags,
        CPU_FLAG_CF,
        carry
    );

    flags = alu_set_flag(
        flags,
        CPU_FLAG_OF,
        overflow
    );

    return flags;
}

static ALUResult alu_make_result(
    uint16_t result,
    uint16_t remainder,
    uint16_t flags,
    ALUStatus status
)
{
    ALUResult alu_result;

    alu_result.result = result;
    alu_result.remainder = remainder;
    alu_result.flags = flags;
    alu_result.status = status;

    return alu_result;
}

static ALUResult alu_make_fault(
    ALUStatus status,
    uint16_t current_flags
)
{
    return alu_make_result(
        0,
        0,
        current_flags,
        status
    );
}

/* ---------------------------------------------------------
 * ALU execution
 * --------------------------------------------------------- */

ALUResult alu_execute(
    ALUOperation operation,
    uint16_t a,
    uint16_t b,
    uint16_t current_flags
)
{
    uint16_t result = 0;
    uint16_t remainder = 0;
    uint16_t flags = current_flags;

    switch (operation) {

        /* ---------------------------------------------
         * ADD
         *
         * result = a + b
         * CF = unsigned carry
         * OF = signed overflow
         * --------------------------------------------- */

        case ALU_ADD:
        {
            uint32_t wide_result =
                (uint32_t)a + (uint32_t)b;

            result = (uint16_t)wide_result;

            bool carry =
                wide_result > 0xFFFFu;

            bool overflow =
                ((~(a ^ b) & (a ^ result) & 0x8000u) != 0);

            flags = alu_update_arithmetic_flags(
                flags,
                result,
                carry,
                overflow
            );

            break;
        }

        /* ---------------------------------------------
         * SUB
         *
         * result = a - b
         * CF = unsigned borrow
         * OF = signed overflow
         * --------------------------------------------- */

        case ALU_SUB:
        case ALU_CMP:
        {
            result = (uint16_t)(a - b);

            bool borrow = a < b;

            bool overflow =
                (((a ^ b) & (a ^ result) & 0x8000u) != 0);

            flags = alu_update_arithmetic_flags(
                flags,
                result,
                borrow,
                overflow
            );

            /*
             * CMP uses subtraction to update FLAGS.
             * The CPU execution layer must not write
             * the result into a destination register.
             */

            break;
        }

        /* ---------------------------------------------
         * MUL
         *
         * Unsigned multiplication.
         * Only low 16 bits are retained.
         *
         * CF = 1 if product exceeds 0xFFFF
         * OF = 0
         * --------------------------------------------- */

        case ALU_MUL:
        {
            uint32_t product =
                (uint32_t)a * (uint32_t)b;

            result = (uint16_t)product;

            bool carry =
                product > 0xFFFFu;

            flags = alu_update_arithmetic_flags(
                flags,
                result,
                carry,
                false
            );

            break;
        }

        /* ---------------------------------------------
         * MULS
         *
         * Signed multiplication.
         * Only low 16 bits are retained.
         *
         * CF = 0
         * OF = signed overflow
         * --------------------------------------------- */

        case ALU_MULS:
        {
            int32_t signed_a = alu_to_signed(a);
            int32_t signed_b = alu_to_signed(b);

            int32_t product =
                signed_a * signed_b;

            result = (uint16_t)(uint32_t)product;

            bool overflow =
                product < INT16_MIN ||
                product > INT16_MAX;

            flags = alu_update_arithmetic_flags(
                flags,
                result,
                false,
                overflow
            );

            break;
        }

        /* ---------------------------------------------
         * DIV
         *
         * Unsigned division.
         *
         * result    = quotient
         * remainder = remainder
         *
         * CF = 0
         * OF = 0
         * --------------------------------------------- */

        case ALU_DIV:
        {
            if (b == 0) {
                return alu_make_fault(
                    ALU_STATUS_DIVIDE_BY_ZERO,
                    current_flags
                );
            }

            result = (uint16_t)(a / b);
            remainder = (uint16_t)(a % b);

            flags = alu_update_arithmetic_flags(
                flags,
                result,
                false,
                false
            );

            break;
        }

        /* ---------------------------------------------
         * DIVS
         *
         * Signed division.
         *
         * result    = quotient
         * remainder = remainder
         *
         * CF = 0
         * OF = 0
         *
         * -32768 / -1 causes arithmetic overflow.
         * --------------------------------------------- */

        case ALU_DIVS:
        {
            int32_t signed_a = alu_to_signed(a);
            int32_t signed_b = alu_to_signed(b);

            if (signed_b == 0) {
                return alu_make_fault(
                    ALU_STATUS_DIVIDE_BY_ZERO,
                    current_flags
                );
            }

            if (
                signed_a == INT16_MIN &&
                signed_b == -1
            ) {
                return alu_make_fault(
                    ALU_STATUS_ARITHMETIC_OVERFLOW,
                    current_flags
                );
            }

            int32_t quotient =
                signed_a / signed_b;

            int32_t signed_remainder =
                signed_a % signed_b;

            result = (uint16_t)quotient;
            remainder = (uint16_t)signed_remainder;

            flags = alu_update_arithmetic_flags(
                flags,
                result,
                false,
                false
            );

            break;
        }

        /* ---------------------------------------------
         * AND
         * --------------------------------------------- */

        case ALU_AND:
        {
            result = a & b;

            flags = alu_update_arithmetic_flags(
                flags,
                result,
                false,
                false
            );

            break;
        }

        /* ---------------------------------------------
         * OR
         * --------------------------------------------- */

        case ALU_OR:
        {
            result = a | b;

            flags = alu_update_arithmetic_flags(
                flags,
                result,
                false,
                false
            );

            break;
        }

        /* ---------------------------------------------
         * XOR
         * --------------------------------------------- */

        case ALU_XOR:
        {
            result = a ^ b;

            flags = alu_update_arithmetic_flags(
                flags,
                result,
                false,
                false
            );

            break;
        }

        /* ---------------------------------------------
         * NOT
         *
         * CF and OF remain unchanged.
         * --------------------------------------------- */

        case ALU_NOT:
        {
            result = (uint16_t)~a;

            flags = alu_update_zf_sf(
                flags,
                result
            );

            break;
        }

        /* ---------------------------------------------
         * Invalid operation
         * --------------------------------------------- */

        default:
        {
            return alu_make_fault(
                ALU_STATUS_INVALID_OPERATION,
                current_flags
            );
        }
    }

    /*
     * Reserved FLAGS bits 4–15 remain zero
     * in the baseline architecture.
     */

    flags &= CPU_FLAGS_DEFINED_MASK;

    return alu_make_result(
        result,
        remainder,
        flags,
        ALU_STATUS_OK
    );
}
