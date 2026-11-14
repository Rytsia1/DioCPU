#ifndef DIOCPU_CONTROL_UNIT_H
#define DIOCPU_CONTROL_UNIT_H

#include "alu.h"
#include "decoder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    ALU_SRC_A_NONE = 0,
    ALU_SRC_A_REG_A,
    ALU_SRC_A_REG_B,
    ALU_SRC_A_SP
} AluSrcASelect;

typedef enum {
    ALU_SRC_B_NONE = 0,
    ALU_SRC_B_REG_B,
    ALU_SRC_B_REG_C,
    ALU_SRC_B_IMM16,
    ALU_SRC_B_IMM8,
    ALU_SRC_B_OFFSET16,
    ALU_SRC_B_CONST_1,
    ALU_SRC_B_ZERO
} AluSrcBSelect;

typedef enum {
    ADDR_MODE_NONE = 0,
    ADDR_MODE_ABS18,
    ADDR_MODE_REG_OFFSET,
    ADDR_MODE_INDEXED,
    ADDR_MODE_SP_OFFSET
} AddrModeSelect;

typedef enum {
    WB_SRC_NONE = 0,
    WB_SRC_ALU_RESULT,
    WB_SRC_MEM_DATA,
    WB_SRC_IMM16,
    WB_SRC_REG_B,
    WB_SRC_EFF_ADDR
} RegWriteSrcSelect;

typedef enum {
    PC_SRC_NEXT_PC = 0,
    PC_SRC_ABS18,
    PC_SRC_REG_A,
    PC_SRC_REL8_TAKEN,
    PC_SRC_STACK_RET,
    PC_SRC_IVT_HANDLER,
    PC_SRC_HOLD
} PcSrcSelect;

typedef enum {
    CONTROL_UNIT_OK = 0,
    CONTROL_UNIT_INVALID_ARGUMENT,
    CONTROL_UNIT_UNSUPPORTED_OPCODE
} ControlUnitStatus;

typedef struct {
    AluSrcASelect alu_src_a;
    AluSrcBSelect alu_src_b;
    AddrModeSelect addr_mode;
    RegWriteSrcSelect wb_src;
    PcSrcSelect pc_src;

    ALUOperation alu_op;

    bool alu_enable;
    bool agu_enable;

    bool reg_write_a;
    bool reg_write_b;

    bool mem_read;
    bool mem_write;
    bool mem_is_stack_frame;

    bool stack_push;
    bool stack_pop;
    bool stack_is_32bit;

    bool flags_write_alu;
    bool flags_write_system;

    bool is_branch;
    bool branch_taken;
    bool is_jump;
    bool is_call;
    bool is_return;
    bool is_interrupt;
    bool is_halt;
    bool sets_ivtr;
} ControlSignals;

int control_unit_eval_branch(InstructionOpcode opcode, uint16_t flags);

ControlUnitStatus control_unit_decode(
    const DecodedInstruction *instruction,
    uint16_t flags,
    ControlSignals *signals
);

const char *control_unit_alu_src_a_string(AluSrcASelect sel);
const char *control_unit_alu_src_b_string(AluSrcBSelect sel);
const char *control_unit_addr_mode_string(AddrModeSelect sel);
const char *control_unit_wb_src_string(RegWriteSrcSelect sel);
const char *control_unit_pc_src_string(PcSrcSelect sel);

size_t control_unit_format_signals(
    const ControlSignals *signals,
    char *buffer,
    size_t size
);

#endif

