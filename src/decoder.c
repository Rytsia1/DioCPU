#include "decoder.h"

#include <stddef.h>
#include <string.h>

/*
 * Single source of truth for all DioCPU opcodes, mnemonics,
 * instruction formats, lengths, and operand counts.
 */

static const ISAInstructionInfo g_isa_table[] = {
    { DIOCPU_OP_NOP,     "NOP",     INSTRUCTION_FORMAT_NONE,                     1, 0 },
    { DIOCPU_OP_HLT,     "HLT",     INSTRUCTION_FORMAT_NONE,                     1, 0 },
    { DIOCPU_OP_JMP,     "JMP",     INSTRUCTION_FORMAT_ABSOLUTE18,               4, 1 },
    { DIOCPU_OP_CALL,    "CALL",    INSTRUCTION_FORMAT_ABSOLUTE18,               4, 1 },
    { DIOCPU_OP_RET,     "RET",     INSTRUCTION_FORMAT_NONE,                     1, 0 },
    { DIOCPU_OP_IRET,    "IRET",    INSTRUCTION_FORMAT_NONE,                     1, 0 },
    { DIOCPU_OP_CLI,     "CLI",     INSTRUCTION_FORMAT_NONE,                     1, 0 },
    { DIOCPU_OP_STI,     "STI",     INSTRUCTION_FORMAT_NONE,                     1, 0 },
    { DIOCPU_OP_INT,     "INT",     INSTRUCTION_FORMAT_IMMEDIATE8,               2, 1 },
    { DIOCPU_OP_SETIVT,  "SETIVT",  INSTRUCTION_FORMAT_ABSOLUTE18,               4, 1 },
    { DIOCPU_OP_JMPR,    "JMPR",    INSTRUCTION_FORMAT_REGISTER,                 2, 1 },
    { DIOCPU_OP_CALLR,   "CALLR",   INSTRUCTION_FORMAT_REGISTER,                 2, 1 },

    { DIOCPU_OP_LOADI,   "LOADI",   INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16,     4, 2 },
    { DIOCPU_OP_LOAD,    "LOAD",    INSTRUCTION_FORMAT_REGISTER_ABSOLUTE18,      5, 2 },
    { DIOCPU_OP_STORE,   "STORE",   INSTRUCTION_FORMAT_REGISTER_ABSOLUTE18,      5, 2 },
    { DIOCPU_OP_PUSH,    "PUSH",    INSTRUCTION_FORMAT_REGISTER,                 2, 1 },
    { DIOCPU_OP_POP,     "POP",     INSTRUCTION_FORMAT_REGISTER,                 2, 1 },
    { DIOCPU_OP_MOV,     "MOV",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_LOADR,   "LOADR",   INSTRUCTION_FORMAT_REGISTER_INDIRECT_OFFSET, 4, 2 },
    { DIOCPU_OP_STORER,  "STORER",  INSTRUCTION_FORMAT_REGISTER_INDIRECT_OFFSET, 4, 2 },
    { DIOCPU_OP_LOADX,   "LOADX",   INSTRUCTION_FORMAT_REGISTER_INDEXED,         3, 2 },
    { DIOCPU_OP_STOREX,  "STOREX",  INSTRUCTION_FORMAT_REGISTER_INDEXED,         3, 2 },
    { DIOCPU_OP_LOADSP,  "LOADSP",  INSTRUCTION_FORMAT_REGISTER_SP_OFFSET,       4, 2 },
    { DIOCPU_OP_STORESP, "STORESP", INSTRUCTION_FORMAT_REGISTER_SP_OFFSET,       4, 2 },
    { DIOCPU_OP_LEA,     "LEA",     INSTRUCTION_FORMAT_REGISTER_INDIRECT_OFFSET, 4, 2 },

    { DIOCPU_OP_ADD,     "ADD",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_SUB,     "SUB",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_MUL,     "MUL",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_MULS,    "MULS",    INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_DIV,     "DIV",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_DIVS,    "DIVS",    INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_ADDI,    "ADDI",    INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16,     4, 2 },
    { DIOCPU_OP_SUBI,    "SUBI",    INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16,     4, 2 },
    { DIOCPU_OP_INC,     "INC",     INSTRUCTION_FORMAT_REGISTER,                 2, 1 },
    { DIOCPU_OP_DEC,     "DEC",     INSTRUCTION_FORMAT_REGISTER,                 2, 1 },

    { DIOCPU_OP_AND,     "AND",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_OR,      "OR",      INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_XOR,     "XOR",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_NOT,     "NOT",     INSTRUCTION_FORMAT_REGISTER,                 2, 1 },
    { DIOCPU_OP_SHL,     "SHL",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_SHR,     "SHR",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_SAR,     "SAR",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_SHLI,    "SHLI",    INSTRUCTION_FORMAT_REGISTER_IMMEDIATE8,      3, 2 },
    { DIOCPU_OP_SHRI,    "SHRI",    INSTRUCTION_FORMAT_REGISTER_IMMEDIATE8,      3, 2 },
    { DIOCPU_OP_SARI,    "SARI",    INSTRUCTION_FORMAT_REGISTER_IMMEDIATE8,      3, 2 },

    { DIOCPU_OP_CMP,     "CMP",     INSTRUCTION_FORMAT_REGISTER_REGISTER,        2, 2 },
    { DIOCPU_OP_CMPI,    "CMPI",    INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16,     4, 2 },

    { DIOCPU_OP_JZ,      "JZ",      INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JNZ,     "JNZ",     INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JC,      "JC",      INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JNC,     "JNC",     INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JS,      "JS",      INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JNS,     "JNS",     INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JO,      "JO",      INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JNO,     "JNO",     INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JL,      "JL",      INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JGE,     "JGE",     INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JLE,     "JLE",     INSTRUCTION_FORMAT_RELATIVE8,                2, 1 },
    { DIOCPU_OP_JG,      "JG",      INSTRUCTION_FORMAT_RELATIVE8,                2, 1 }
};

const ISAInstructionInfo *isa_lookup_opcode(InstructionOpcode opcode)
{
    size_t count = sizeof(g_isa_table) / sizeof(g_isa_table[0]);

    for (size_t i = 0; i < count; i++) {
        if (g_isa_table[i].opcode == opcode) {
            return &g_isa_table[i];
        }
    }

    return NULL;
}

const ISAInstructionInfo *isa_lookup_mnemonic(const char *mnemonic)
{
    if (mnemonic == NULL) {
        return NULL;
    }

    size_t count = sizeof(g_isa_table) / sizeof(g_isa_table[0]);

    for (size_t i = 0; i < count; i++) {
        if (strcmp(g_isa_table[i].mnemonic, mnemonic) == 0) {
            return &g_isa_table[i];
        }
    }

    return NULL;
}

const char *isa_opcode_name(InstructionOpcode opcode)
{
    const ISAInstructionInfo *info = isa_lookup_opcode(opcode);
    return (info != NULL) ? info->mnemonic : "UNKNOWN";
}

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

static DecoderStatus fetch_uint16(
    const Memory *memory,
    uint32_t address,
    uint16_t *result
)
{
    uint8_t low;
    uint8_t high;

    DecoderStatus status = fetch_byte(memory, address, &low);
    if (status != DECODER_STATUS_OK) {
        return status;
    }

    status = fetch_byte(memory, address + 1u, &high);
    if (status != DECODER_STATUS_OK) {
        return status;
    }

    *result = (uint16_t)low | ((uint16_t)high << 8u);

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
    if (raw <= 0x7Fu) {
        return (int8_t)raw;
    }

    return (int8_t)((int16_t)raw - 0x100);
}

static int16_t decode_signed16(uint16_t raw)
{
    if (raw <= 0x7FFFu) {
        return (int16_t)raw;
    }

    return (int16_t)((int32_t)raw - 0x10000);
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

    const ISAInstructionInfo *info = isa_lookup_opcode(decoded.opcode);

    if (info == NULL) {
        return DECODER_STATUS_INVALID_OPCODE;
    }

    decoded.format = info->format;
    decoded.length = info->length;

    switch (info->format) {

        case INSTRUCTION_FORMAT_NONE:
            break;

        case INSTRUCTION_FORMAT_REGISTER:
            status = fetch_register(memory, pc + 1u, &decoded.reg_a);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            break;

        case INSTRUCTION_FORMAT_REGISTER_REGISTER:
        {
            uint8_t operand;
            status = fetch_byte(memory, pc + 1u, &operand);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            decoded.reg_a = (operand >> 4u) & 0x0Fu;
            decoded.reg_b = operand & 0x0Fu;
            break;
        }

        case INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16:
        {
            status = fetch_register(memory, pc + 1u, &decoded.reg_a);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            status = fetch_uint16(memory, pc + 2u, &decoded.immediate16);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            break;
        }

        case INSTRUCTION_FORMAT_REGISTER_ABSOLUTE18:
            status = fetch_register(memory, pc + 1u, &decoded.reg_a);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            status = fetch_absolute18(memory, pc + 2u, &decoded.absolute_address);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            break;

        case INSTRUCTION_FORMAT_ABSOLUTE18:
            status = fetch_absolute18(memory, pc + 1u, &decoded.absolute_address);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            break;

        case INSTRUCTION_FORMAT_RELATIVE8:
        {
            uint8_t raw_offset;
            status = fetch_byte(memory, pc + 1u, &raw_offset);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            decoded.relative_offset = decode_signed8(raw_offset);
            break;
        }

        case INSTRUCTION_FORMAT_IMMEDIATE8:
            status = fetch_byte(memory, pc + 1u, &decoded.immediate8);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            break;

        case INSTRUCTION_FORMAT_REGISTER_IMMEDIATE8:
            status = fetch_register(memory, pc + 1u, &decoded.reg_a);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            status = fetch_byte(memory, pc + 2u, &decoded.immediate8);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            break;

        case INSTRUCTION_FORMAT_REGISTER_INDIRECT_OFFSET:
        {
            uint8_t operand;
            uint16_t raw_offset;

            status = fetch_byte(memory, pc + 1u, &operand);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            decoded.reg_a = (operand >> 4u) & 0x0Fu;
            decoded.reg_b = operand & 0x0Fu;

            status = fetch_uint16(memory, pc + 2u, &raw_offset);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            decoded.offset16 = decode_signed16(raw_offset);
            break;
        }

        case INSTRUCTION_FORMAT_REGISTER_INDEXED:
        {
            uint8_t operand;

            status = fetch_byte(memory, pc + 1u, &operand);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            decoded.reg_a = (operand >> 4u) & 0x0Fu;
            decoded.reg_b = operand & 0x0Fu;

            status = fetch_register(memory, pc + 2u, &decoded.reg_c);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            break;
        }

        case INSTRUCTION_FORMAT_REGISTER_SP_OFFSET:
        {
            uint16_t raw_offset;

            status = fetch_register(memory, pc + 1u, &decoded.reg_a);
            if (status != DECODER_STATUS_OK) {
                return status;
            }

            status = fetch_uint16(memory, pc + 2u, &raw_offset);
            if (status != DECODER_STATUS_OK) {
                return status;
            }
            decoded.offset16 = decode_signed16(raw_offset);
            break;
        }

        default:
            return DECODER_STATUS_INVALID_OPCODE;
    }

    decoded.next_pc = pc + decoded.length;
    *instruction = decoded;

    return DECODER_STATUS_OK;
}
