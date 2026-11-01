#ifndef DIOCPU_DECODER_H
#define DIOCPU_DECODER_H

#include "memory.h"

#include <stdint.h>

/*
 * DioCPU instruction opcodes.
 */

typedef enum {
    DIOCPU_OP_NOP   = 0x00,
    DIOCPU_OP_HLT   = 0x01,
    DIOCPU_OP_JMP   = 0x02,
    DIOCPU_OP_CALL  = 0x03,
    DIOCPU_OP_RET   = 0x04,

    DIOCPU_OP_LOADI = 0x10,
    DIOCPU_OP_LOAD  = 0x11,
    DIOCPU_OP_STORE = 0x12,
    DIOCPU_OP_PUSH  = 0x13,
    DIOCPU_OP_POP   = 0x14,

    DIOCPU_OP_ADD   = 0x20,
    DIOCPU_OP_SUB   = 0x21,
    DIOCPU_OP_MUL   = 0x22,
    DIOCPU_OP_MULS  = 0x23,
    DIOCPU_OP_DIV   = 0x24,
    DIOCPU_OP_DIVS  = 0x25,

    DIOCPU_OP_AND   = 0x30,
    DIOCPU_OP_OR    = 0x31,
    DIOCPU_OP_XOR   = 0x32,
    DIOCPU_OP_NOT   = 0x33,

    DIOCPU_OP_CMP   = 0x40,

    DIOCPU_OP_JZ    = 0x50,
    DIOCPU_OP_JNZ   = 0x51,
    DIOCPU_OP_JC    = 0x52,
    DIOCPU_OP_JNC   = 0x53

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
    INSTRUCTION_FORMAT_RELATIVE8

} InstructionFormat;


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

    int8_t relative_offset;

    uint8_t reg_a;
    uint8_t reg_b;

    uint8_t length;

} DecodedInstruction;


/*
 * Decode one instruction from Instruction Memory.
 */

DecoderStatus decoder_decode(
    const Memory *memory,
    uint32_t pc,
    DecodedInstruction *instruction
);

#endif
