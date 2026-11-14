#include "control_unit.h"

#include <stdio.h>
#include <string.h>

int control_unit_eval_branch(InstructionOpcode opcode, uint16_t flags)
{
    int zf = (flags & CPU_FLAG_ZF) != 0;
    int cf = (flags & CPU_FLAG_CF) != 0;
    int sf = (flags & CPU_FLAG_SF) != 0;
    int of = (flags & CPU_FLAG_OF) != 0;

    switch (opcode) {
        case DIOCPU_OP_JZ:  return zf;
        case DIOCPU_OP_JNZ: return !zf;
        case DIOCPU_OP_JC:  return cf;
        case DIOCPU_OP_JNC: return !cf;
        case DIOCPU_OP_JS:  return sf;
        case DIOCPU_OP_JNS: return !sf;
        case DIOCPU_OP_JO:  return of;
        case DIOCPU_OP_JNO: return !of;
        case DIOCPU_OP_JL:  return sf != of;
        case DIOCPU_OP_JGE: return sf == of;
        case DIOCPU_OP_JLE: return zf || (sf != of);
        case DIOCPU_OP_JG:  return !zf && (sf == of);
        default:            return 0;
    }
}

static void configure_alu_op(
    ControlSignals *s,
    ALUOperation op,
    AluSrcBSelect src_b,
    bool write_a,
    bool write_b
)
{
    s->alu_enable = true;
    s->alu_op = op;
    s->alu_src_a = ALU_SRC_A_REG_A;
    s->alu_src_b = src_b;
    s->reg_write_a = write_a;
    s->reg_write_b = write_b;
    s->wb_src = write_a ? WB_SRC_ALU_RESULT : WB_SRC_NONE;
    s->flags_write_alu = true;
}

ControlUnitStatus control_unit_decode(
    const DecodedInstruction *instruction,
    uint16_t flags,
    ControlSignals *signals
)
{
    if (instruction == NULL || signals == NULL) {
        return CONTROL_UNIT_INVALID_ARGUMENT;
    }

    ControlSignals s;
    memset(&s, 0, sizeof(s));
    s.pc_src = PC_SRC_NEXT_PC;

    switch (instruction->opcode) {

        case DIOCPU_OP_NOP:
            break;

        case DIOCPU_OP_HLT:
            s.is_halt = true;
            s.pc_src = PC_SRC_HOLD;
            break;

        case DIOCPU_OP_CLI:
        case DIOCPU_OP_STI:
            s.flags_write_system = true;
            break;

        case DIOCPU_OP_SETIVT:
            s.sets_ivtr = true;
            s.addr_mode = ADDR_MODE_ABS18;
            break;

        case DIOCPU_OP_INT:
            s.is_interrupt = true;
            s.stack_push = true;
            s.stack_is_32bit = true;
            s.flags_write_system = true;
            s.pc_src = PC_SRC_IVT_HANDLER;
            break;

        case DIOCPU_OP_IRET:
            s.is_return = true;
            s.is_interrupt = true;
            s.stack_pop = true;
            s.stack_is_32bit = true;
            s.flags_write_alu = true;
            s.flags_write_system = true;
            s.pc_src = PC_SRC_STACK_RET;
            break;

        case DIOCPU_OP_JMP:
            s.is_jump = true;
            s.addr_mode = ADDR_MODE_ABS18;
            s.pc_src = PC_SRC_ABS18;
            break;

        case DIOCPU_OP_JMPR:
            s.is_jump = true;
            s.alu_src_a = ALU_SRC_A_REG_A;
            s.pc_src = PC_SRC_REG_A;
            break;

        case DIOCPU_OP_CALL:
            s.is_call = true;
            s.stack_push = true;
            s.stack_is_32bit = true;
            s.addr_mode = ADDR_MODE_ABS18;
            s.pc_src = PC_SRC_ABS18;
            break;

        case DIOCPU_OP_CALLR:
            s.is_call = true;
            s.stack_push = true;
            s.stack_is_32bit = true;
            s.alu_src_a = ALU_SRC_A_REG_A;
            s.pc_src = PC_SRC_REG_A;
            break;

        case DIOCPU_OP_RET:
            s.is_return = true;
            s.stack_pop = true;
            s.stack_is_32bit = true;
            s.pc_src = PC_SRC_STACK_RET;
            break;

        case DIOCPU_OP_LOADI:
            s.reg_write_a = true;
            s.wb_src = WB_SRC_IMM16;
            break;

        case DIOCPU_OP_MOV:
            s.alu_src_b = ALU_SRC_B_REG_B;
            s.reg_write_a = true;
            s.wb_src = WB_SRC_REG_B;
            break;

        case DIOCPU_OP_LOAD:
            s.agu_enable = true;
            s.addr_mode = ADDR_MODE_ABS18;
            s.mem_read = true;
            s.reg_write_a = true;
            s.wb_src = WB_SRC_MEM_DATA;
            break;

        case DIOCPU_OP_LOADR:
            s.agu_enable = true;
            s.addr_mode = ADDR_MODE_REG_OFFSET;
            s.alu_src_a = ALU_SRC_A_REG_B;
            s.alu_src_b = ALU_SRC_B_OFFSET16;
            s.mem_read = true;
            s.reg_write_a = true;
            s.wb_src = WB_SRC_MEM_DATA;
            break;

        case DIOCPU_OP_LOADX:
            s.agu_enable = true;
            s.addr_mode = ADDR_MODE_INDEXED;
            s.alu_src_a = ALU_SRC_A_REG_B;
            s.alu_src_b = ALU_SRC_B_REG_C;
            s.mem_read = true;
            s.reg_write_a = true;
            s.wb_src = WB_SRC_MEM_DATA;
            break;

        case DIOCPU_OP_LOADSP:
            s.agu_enable = true;
            s.addr_mode = ADDR_MODE_SP_OFFSET;
            s.alu_src_a = ALU_SRC_A_SP;
            s.alu_src_b = ALU_SRC_B_OFFSET16;
            s.mem_read = true;
            s.mem_is_stack_frame = true;
            s.reg_write_a = true;
            s.wb_src = WB_SRC_MEM_DATA;
            break;

        case DIOCPU_OP_STORE:
            s.agu_enable = true;
            s.addr_mode = ADDR_MODE_ABS18;
            s.mem_write = true;
            break;

        case DIOCPU_OP_STORER:
            s.agu_enable = true;
            s.addr_mode = ADDR_MODE_REG_OFFSET;
            s.alu_src_a = ALU_SRC_A_REG_B;
            s.alu_src_b = ALU_SRC_B_OFFSET16;
            s.mem_write = true;
            break;

        case DIOCPU_OP_STOREX:
            s.agu_enable = true;
            s.addr_mode = ADDR_MODE_INDEXED;
            s.alu_src_a = ALU_SRC_A_REG_B;
            s.alu_src_b = ALU_SRC_B_REG_C;
            s.mem_write = true;
            break;

        case DIOCPU_OP_STORESP:
            s.agu_enable = true;
            s.addr_mode = ADDR_MODE_SP_OFFSET;
            s.alu_src_a = ALU_SRC_A_SP;
            s.alu_src_b = ALU_SRC_B_OFFSET16;
            s.mem_write = true;
            s.mem_is_stack_frame = true;
            break;

        case DIOCPU_OP_LEA:
            s.agu_enable = true;
            s.addr_mode = ADDR_MODE_REG_OFFSET;
            s.alu_src_a = ALU_SRC_A_REG_B;
            s.alu_src_b = ALU_SRC_B_OFFSET16;
            s.reg_write_a = true;
            s.wb_src = WB_SRC_EFF_ADDR;
            break;

        case DIOCPU_OP_PUSH:
            s.stack_push = true;
            s.alu_src_a = ALU_SRC_A_REG_A;
            break;

        case DIOCPU_OP_POP:
            s.stack_pop = true;
            s.reg_write_a = true;
            s.wb_src = WB_SRC_MEM_DATA;
            break;

        case DIOCPU_OP_ADD:  configure_alu_op(&s, ALU_ADD,  ALU_SRC_B_REG_B, true,  false); break;
        case DIOCPU_OP_SUB:  configure_alu_op(&s, ALU_SUB,  ALU_SRC_B_REG_B, true,  false); break;
        case DIOCPU_OP_MUL:  configure_alu_op(&s, ALU_MUL,  ALU_SRC_B_REG_B, true,  false); break;
        case DIOCPU_OP_MULS: configure_alu_op(&s, ALU_MULS, ALU_SRC_B_REG_B, true,  false); break;
        case DIOCPU_OP_DIV:  configure_alu_op(&s, ALU_DIV,  ALU_SRC_B_REG_B, true,  true);  break;
        case DIOCPU_OP_DIVS: configure_alu_op(&s, ALU_DIVS, ALU_SRC_B_REG_B, true,  true);  break;
        case DIOCPU_OP_ADDI: configure_alu_op(&s, ALU_ADD,  ALU_SRC_B_IMM16, true,  false); break;
        case DIOCPU_OP_SUBI: configure_alu_op(&s, ALU_SUB,  ALU_SRC_B_IMM16, true,  false); break;
        case DIOCPU_OP_INC:  configure_alu_op(&s, ALU_ADD,  ALU_SRC_B_CONST_1, true, false); break;
        case DIOCPU_OP_DEC:  configure_alu_op(&s, ALU_SUB,  ALU_SRC_B_CONST_1, true, false); break;

        case DIOCPU_OP_AND:  configure_alu_op(&s, ALU_AND,  ALU_SRC_B_REG_B, true,  false); break;
        case DIOCPU_OP_OR:   configure_alu_op(&s, ALU_OR,   ALU_SRC_B_REG_B, true,  false); break;
        case DIOCPU_OP_XOR:  configure_alu_op(&s, ALU_XOR,  ALU_SRC_B_REG_B, true,  false); break;
        case DIOCPU_OP_NOT:  configure_alu_op(&s, ALU_NOT,  ALU_SRC_B_ZERO,  true,  false); break;
        case DIOCPU_OP_SHL:  configure_alu_op(&s, ALU_SHL,  ALU_SRC_B_REG_B, true,  false); break;
        case DIOCPU_OP_SHR:  configure_alu_op(&s, ALU_SHR,  ALU_SRC_B_REG_B, true,  false); break;
        case DIOCPU_OP_SAR:  configure_alu_op(&s, ALU_SAR,  ALU_SRC_B_REG_B, true,  false); break;
        case DIOCPU_OP_SHLI: configure_alu_op(&s, ALU_SHL,  ALU_SRC_B_IMM8,  true,  false); break;
        case DIOCPU_OP_SHRI: configure_alu_op(&s, ALU_SHR,  ALU_SRC_B_IMM8,  true,  false); break;
        case DIOCPU_OP_SARI: configure_alu_op(&s, ALU_SAR,  ALU_SRC_B_IMM8,  true,  false); break;
        case DIOCPU_OP_CMP:  configure_alu_op(&s, ALU_CMP,  ALU_SRC_B_REG_B, false, false); break;
        case DIOCPU_OP_CMPI: configure_alu_op(&s, ALU_CMP,  ALU_SRC_B_IMM16, false, false); break;

        case DIOCPU_OP_JZ:
        case DIOCPU_OP_JNZ:
        case DIOCPU_OP_JC:
        case DIOCPU_OP_JNC:
        case DIOCPU_OP_JS:
        case DIOCPU_OP_JNS:
        case DIOCPU_OP_JO:
        case DIOCPU_OP_JNO:
        case DIOCPU_OP_JL:
        case DIOCPU_OP_JGE:
        case DIOCPU_OP_JLE:
        case DIOCPU_OP_JG:
            s.is_branch = true;
            s.branch_taken = control_unit_eval_branch(instruction->opcode, flags) != 0;
            s.pc_src = s.branch_taken ? PC_SRC_REL8_TAKEN : PC_SRC_NEXT_PC;
            break;

        default:
            return CONTROL_UNIT_UNSUPPORTED_OPCODE;
    }

    *signals = s;
    return CONTROL_UNIT_OK;
}

const char *control_unit_alu_src_a_string(AluSrcASelect sel)
{
    switch (sel) {
        case ALU_SRC_A_NONE:  return "NONE";
        case ALU_SRC_A_REG_A: return "RA";
        case ALU_SRC_A_REG_B: return "RB";
        case ALU_SRC_A_SP:    return "SP";
        default:              return "UNKNOWN";
    }
}

const char *control_unit_alu_src_b_string(AluSrcBSelect sel)
{
    switch (sel) {
        case ALU_SRC_B_NONE:     return "NONE";
        case ALU_SRC_B_REG_B:    return "RB";
        case ALU_SRC_B_REG_C:    return "RC";
        case ALU_SRC_B_IMM16:    return "IMM16";
        case ALU_SRC_B_IMM8:     return "IMM8";
        case ALU_SRC_B_OFFSET16: return "OFF16";
        case ALU_SRC_B_CONST_1:  return "CONST_1";
        case ALU_SRC_B_ZERO:     return "ZERO";
        default:                 return "UNKNOWN";
    }
}

const char *control_unit_addr_mode_string(AddrModeSelect sel)
{
    switch (sel) {
        case ADDR_MODE_NONE:       return "NONE";
        case ADDR_MODE_ABS18:      return "ABS18";
        case ADDR_MODE_REG_OFFSET: return "REG_OFFSET";
        case ADDR_MODE_INDEXED:    return "INDEXED";
        case ADDR_MODE_SP_OFFSET:  return "SP_OFFSET";
        default:                   return "UNKNOWN";
    }
}

const char *control_unit_wb_src_string(RegWriteSrcSelect sel)
{
    switch (sel) {
        case WB_SRC_NONE:       return "NONE";
        case WB_SRC_ALU_RESULT: return "ALU";
        case WB_SRC_MEM_DATA:   return "MEM";
        case WB_SRC_IMM16:      return "IMM16";
        case WB_SRC_REG_B:      return "RB";
        case WB_SRC_EFF_ADDR:   return "AGU";
        default:                return "UNKNOWN";
    }
}

const char *control_unit_pc_src_string(PcSrcSelect sel)
{
    switch (sel) {
        case PC_SRC_NEXT_PC:     return "NEXT_PC";
        case PC_SRC_ABS18:       return "ABS18";
        case PC_SRC_REG_A:       return "RA";
        case PC_SRC_REL8_TAKEN:  return "REL8";
        case PC_SRC_STACK_RET:   return "STACK";
        case PC_SRC_IVT_HANDLER: return "IVT";
        case PC_SRC_HOLD:        return "HOLD";
        default:                 return "UNKNOWN";
    }
}

size_t control_unit_format_signals(
    const ControlSignals *signals,
    char *buffer,
    size_t size
)
{
    if (signals == NULL || buffer == NULL || size == 0) {
        return 0;
    }

    int written = snprintf(
        buffer,
        size,
        "PCSrc=%s WBSrc=%s ALUSrc=(%s,%s) Addr=%s%s%s%s%s%s%s%s%s%s",
        control_unit_pc_src_string(signals->pc_src),
        control_unit_wb_src_string(signals->wb_src),
        control_unit_alu_src_a_string(signals->alu_src_a),
        control_unit_alu_src_b_string(signals->alu_src_b),
        control_unit_addr_mode_string(signals->addr_mode),
        signals->reg_write_a ? " RegWrA" : "",
        signals->reg_write_b ? " RegWrB" : "",
        signals->mem_read ? " MemRd" : "",
        signals->mem_write ? " MemWr" : "",
        signals->stack_push ? " StackPush" : "",
        signals->stack_pop ? " StackPop" : "",
        signals->alu_enable ? " ALU" : "",
        signals->flags_write_alu ? " FlagsALU" : "",
        signals->flags_write_system ? " FlagsSys" : ""
    );

    if (written < 0) {
        buffer[0] = '\0';
        return 0;
    }

    return (size_t)written;
}

