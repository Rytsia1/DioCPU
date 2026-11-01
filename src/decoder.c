#include "decoder.h"

#include <stddef.h>

static DecoderStatus fetch_byte(
    const Memory *memory,
    uint32_t address,
    uint8_t *value
)
{
    if (memory == NULL || value == NULL) {
        return DECODER_STATUS_INVALID_ARGUMENT;
    }

    MemoryStatus status = memory_instruction_read8(
        memory,
        address,
        value
    );

    if (status == MEMORY_STATUS_OK) {
        return DECODER_STATUS_OK;
    }

    if (status == MEMORY_STATUS_INVALID_ARGUMENT) {
        return DECODER_STATUS_INVALID_ARGUMENT;
    }

    return DECODER_STATUS_INSTRUCTION_FETCH_FAULT;
}

static DecoderStatus fetch_register(
    const Memory *memory,
    uint32_t address,
    uint8_t *reg
)
{
    uint8_t encoded;

    DecoderStatus status = fetch_byte(
        memory,
        address,
        &encoded
    );

    if (status != DECODER_STATUS_OK) {
        return status;
    }

    /*
     * Upper 4 bits are reserved.
     * They must always be zero.
     */

    if ((encoded & 0xF0u) != 0u) {
        return DECODER_STATUS_INVALID_INSTRUCTION;
    }

    *reg = encoded & 0x0Fu;

    return DECODER_STATUS_OK;
}

static DecoderStatus fetch_absolute18(
    const Memory *memory,
    uint32_t address,
    uint32_t *result
)
{
    uint8_t low;
    uint8_t middle;
    uint8_t high;

    DecoderStatus status;

    status = fetch_byte(memory, address, &low);

    if (status != DECODER_STATUS_OK) {
        return status;
    }

    status = fetch_byte(memory, address + 1u, &middle);

    if (status != DECODER_STATUS_OK) {
        return status;
    }

    status = fetch_byte(memory, address + 2u, &high);

    if (status != DECODER_STATUS_OK) {
        return status;
    }

    /*
     * Only the lowest two bits of the
     * third address byte are available.
     */

    if ((high & 0xFCu) != 0u) {
        return DECODER_STATUS_INVALID_INSTRUCTION;
    }

    *result =
        (uint32_t)low |
        ((uint32_t)middle << 8u) |
        ((uint32_t)high << 16u);

    return DECODER_STATUS_OK;
}

static int8_t decode_signed8(uint8_t raw)
{
    int16_t value;

    if (raw <= 0x7Fu) {
        value = (int16_t)raw;
    } else {
        value = (int16_t)raw - 0x100;
    }

    return (int8_t)value;
}

DecoderStatus decoder_decode(
    const Memory *memory,
    uint32_t pc,
    DecodedInstruction *instruction
)
{
    if (memory == NULL || instruction == NULL) {
        return DECODER_STATUS_INVALID_ARGUMENT;
    }

    if (pc > DIOCPU_MEMORY_MAX_ADDRESS) {
        return DECODER_STATUS_INSTRUCTION_FETCH_FAULT;
    }

    DecodedInstruction decoded = {0};

    decoded.pc = pc;

    uint8_t opcode_byte;

    DecoderStatus status = fetch_byte(
        memory,
        pc,
        &opcode_byte
    );

    if (status != DECODER_STATUS_OK) {
        return status;
    }

    decoded.opcode = (InstructionOpcode)opcode_byte;


    /*
     * No operand instructions.
     */

    switch (decoded.opcode) {

        case DIOCPU_OP_NOP:
        case DIOCPU_OP_HLT:
        case DIOCPU_OP_RET:

            decoded.format = INSTRUCTION_FORMAT_NONE;
            decoded.length = 1;

            break;


        /*
         * Register-to-register instructions.
         */

        case DIOCPU_OP_ADD:
        case DIOCPU_OP_SUB:
        case DIOCPU_OP_MUL:
        case DIOCPU_OP_MULS:
        case DIOCPU_OP_DIV:
        case DIOCPU_OP_DIVS:

        case DIOCPU_OP_AND:
        case DIOCPU_OP_OR:
        case DIOCPU_OP_XOR:

        case DIOCPU_OP_CMP:
        {
            uint8_t operand;

            status = fetch_byte(
                memory,
                pc + 1u,
                &operand
            );

            if (status != DECODER_STATUS_OK) {
                return status;
            }

            decoded.reg_a = (operand >> 4u) & 0x0Fu;
            decoded.reg_b = operand & 0x0Fu;

            decoded.format =
                INSTRUCTION_FORMAT_REGISTER_REGISTER;

            decoded.length = 2;

            break;
        }


        /*
         * Single-register instructions.
         */

        case DIOCPU_OP_NOT:
        case DIOCPU_OP_PUSH:
        case DIOCPU_OP_POP:

            status = fetch_register(
                memory,
                pc + 1u,
                &decoded.reg_a
            );

            if (status != DECODER_STATUS_OK) {
                return status;
            }

            decoded.format = INSTRUCTION_FORMAT_REGISTER;
            decoded.length = 2;

            break;


        /*
         * Register + 16-bit immediate.
         */

        case DIOCPU_OP_LOADI:
        {
            uint8_t low;
            uint8_t high;

            status = fetch_register(
                memory,
                pc + 1u,
                &decoded.reg_a
            );

            if (status != DECODER_STATUS_OK) {
                return status;
            }

            status = fetch_byte(
                memory,
                pc + 2u,
                &low
            );

            if (status != DECODER_STATUS_OK) {
                return status;
            }

            status = fetch_byte(
                memory,
                pc + 3u,
                &high
            );

            if (status != DECODER_STATUS_OK) {
                return status;
            }

            decoded.immediate16 =
                (uint16_t)low |
                ((uint16_t)high << 8u);

            decoded.format =
                INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16;

            decoded.length = 4;

            break;
        }


        /*
         * Register + absolute 18-bit address.
         */

        case DIOCPU_OP_LOAD:
        case DIOCPU_OP_STORE:

            status = fetch_register(
                memory,
                pc + 1u,
                &decoded.reg_a
            );

            if (status != DECODER_STATUS_OK) {
                return status;
            }

            status = fetch_absolute18(
                memory,
                pc + 2u,
                &decoded.absolute_address
            );

            if (status != DECODER_STATUS_OK) {
                return status;
            }

            decoded.format =
                INSTRUCTION_FORMAT_REGISTER_ABSOLUTE18;

            decoded.length = 5;

            break;


        /*
         * Absolute 18-bit control-flow instructions.
         */

        case DIOCPU_OP_JMP:
        case DIOCPU_OP_CALL:

            status = fetch_absolute18(
                memory,
                pc + 1u,
                &decoded.absolute_address
            );

            if (status != DECODER_STATUS_OK) {
                return status;
            }

            decoded.format = INSTRUCTION_FORMAT_ABSOLUTE18;
            decoded.length = 4;

            break;


        /*
         * Signed relative 8-bit branches.
         */

        case DIOCPU_OP_JZ:
        case DIOCPU_OP_JNZ:
        case DIOCPU_OP_JC:
        case DIOCPU_OP_JNC:
        {
            uint8_t raw_offset;

            status = fetch_byte(
                memory,
                pc + 1u,
                &raw_offset
            );

            if (status != DECODER_STATUS_OK) {
                return status;
            }

            decoded.relative_offset = decode_signed8(raw_offset);

            decoded.format = INSTRUCTION_FORMAT_RELATIVE8;
            decoded.length = 2;

            break;
        }


        /*
         * Unknown opcode.
         */

        default:
            return DECODER_STATUS_INVALID_OPCODE;
    }


    /*
     * Calculate the address of the next instruction.
     */

    decoded.next_pc = pc + decoded.length;


    /*
     * Only update output after successful decoding.
     */

    *instruction = decoded;

    return DECODER_STATUS_OK;
}
