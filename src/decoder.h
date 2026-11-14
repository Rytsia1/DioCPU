#ifndef DIOCPU_DECODER_H
#define DIOCPU_DECODER_H

#include "memory.h"

#include <stdint.h>

/*
 * DioCPU instruction opcodes.
 */

typedef enum {
    DIOCPU_OP_NOP    = 0x00,
    DIOCPU_OP_HLT    = 0x01,
    DIOCPU_OP_JMP    = 0x02,
    DIOCPU_OP_CALL   = 0x03,
    DIOCPU_OP_RET    = 0x04,
    DIOCPU_OP_IRET   = 0x05,
    DIOCPU_OP_CLI    = 0x06,
    DIOCPU_OP_STI    = 0x07,
    DIOCPU_OP_INT    = 0x08,
    DIOCPU_OP_SETIVT = 0x09,
    DIOCPU_OP_JMPR   = 0x0A,
    DIOCPU_OP_CALLR  = 0x0B,

    DIOCPU_OP_LOADI   = 0x10,
    DIOCPU_OP_LOAD    = 0x11,
    DIOCPU_OP_STORE   = 0x12,
    DIOCPU_OP_PUSH    = 0x13,
    DIOCPU_OP_POP     = 0x14,
    DIOCPU_OP_MOV     = 0x15,
    DIOCPU_OP_LOADR   = 0x16,
    DIOCPU_OP_STORER  = 0x17,
    DIOCPU_OP_LOADX   = 0x18,
    DIOCPU_OP_STOREX  = 0x19,
    DIOCPU_OP_LOADSP  = 0x1A,
    DIOCPU_OP_STORESP = 0x1B,
    DIOCPU_OP_LEA     = 0x1C,

    DIOCPU_OP_ADD   = 0x20,
    DIOCPU_OP_SUB   = 0x21,
    DIOCPU_OP_MUL   = 0x22,
    DIOCPU_OP_MULS  = 0x23,
    DIOCPU_OP_DIV   = 0x24,
    DIOCPU_OP_DIVS  = 0x25,
    DIOCPU_OP_ADDI  = 0x26,
    DIOCPU_OP_SUBI  = 0x27,
    DIOCPU_OP_INC   = 0x28,
    DIOCPU_OP_DEC   = 0x29,

    DIOCPU_OP_AND   = 0x30,
    DIOCPU_OP_OR    = 0x31,
    DIOCPU_OP_XOR   = 0x32,
    DIOCPU_OP_NOT   = 0x33,
    DIOCPU_OP_SHL   = 0x34,
    DIOCPU_OP_SHR   = 0x35,
    DIOCPU_OP_SAR   = 0x36,
    DIOCPU_OP_SHLI  = 0x37,
    DIOCPU_OP_SHRI  = 0x38,
    DIOCPU_OP_SARI  = 0x39,

    DIOCPU_OP_CMP   = 0x40,
    DIOCPU_OP_CMPI  = 0x41,

    DIOCPU_OP_JZ    = 0x50,
    DIOCPU_OP_JNZ   = 0x51,
    DIOCPU_OP_JC    = 0x52,
    DIOCPU_OP_JNC   = 0x53,
    DIOCPU_OP_JS    = 0x54,
    DIOCPU_OP_JNS   = 0x55,
    DIOCPU_OP_JO    = 0x56,
    DIOCPU_OP_JNO   = 0x57,
    DIOCPU_OP_JL    = 0x58,
    DIOCPU_OP_JGE   = 0x59,
    DIOCPU_OP_JLE   = 0x5A,
    DIOCPU_OP_JG    = 0x5B

} InstructionOpcode;


/*
 * Instruction formats.
 */

typedef enum {
    INSTRUCTION_FORMAT_NONE = 0,
    INSTRUCTION_FORMAT_REGISTER,
    INSTRUCTION_FORMAT_REGISTER_REGISTER,
    INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16,
    INSTRUCTION_FORMAT_REGISTER_ABSOLUTE18,
    INSTRUCTION_FORMAT_ABSOLUTE18,
    INSTRUCTION_FORMAT_RELATIVE8,
    INSTRUCTION_FORMAT_IMMEDIATE8,
    INSTRUCTION_FORMAT_REGISTER_IMMEDIATE8,
    INSTRUCTION_FORMAT_REGISTER_INDIRECT_OFFSET,
    INSTRUCTION_FORMAT_REGISTER_INDEXED,
    INSTRUCTION_FORMAT_REGISTER_SP_OFFSET

} InstructionFormat;


/*
 * Centralized ISA instruction descriptor (single source of truth).
 */

typedef struct {
    InstructionOpcode opcode;
    const char *mnemonic;
    InstructionFormat format;
    uint8_t length;
    uint8_t operand_count;
} ISAInstructionInfo;


/*
 * Decoder status.
 */

typedef enum {
    DECODER_STATUS_OK = 0,
    DECODER_STATUS_INVALID_ARGUMENT,
    DECODER_STATUS_INVALID_OPCODE,
    DECODER_STATUS_INVALID_INSTRUCTION,
    DECODER_STATUS_INSTRUCTION_FETCH_FAULT

} DecoderStatus;


/*
 * Decoded instruction representation.
 */

typedef struct {
    InstructionOpcode opcode;
    InstructionFormat format;

    uint32_t pc;
    uint32_t next_pc;

    uint32_t absolute_address;

    uint16_t immediate16;

    int16_t offset16;

    int8_t relative_offset;

    uint8_t immediate8;

    uint8_t reg_a;
    uint8_t reg_b;
    uint8_t reg_c;

    uint8_t length;

} DecodedInstruction;


/*
 * ISA descriptor lookup helpers.
 */

const ISAInstructionInfo *isa_lookup_opcode(InstructionOpcode opcode);
const ISAInstructionInfo *isa_lookup_mnemonic(const char *mnemonic);
const char *isa_opcode_name(InstructionOpcode opcode);


/*
 * Decode one instruction from Instruction Memory.
 */

DecoderStatus decoder_decode(
    const Memory *memory,
    uint32_t pc,
    DecodedInstruction *instruction
);

#endif
