#include "datapath.h"

#include <string.h>

void datapath_reset(DatapathState *dp)
{
    if (dp == NULL) {
        return;
    }

    memset(dp, 0, sizeof(*dp));
}

DecoderStatus datapath_fetch_decode(
    const Memory *memory,
    uint32_t pc,
    uint16_t flags,
    DatapathState *dp
)
{
    if (memory == NULL || dp == NULL) {
        return DECODER_STATUS_INVALID_ARGUMENT;
    }

    DecodedInstruction decoded = {0};
    DecoderStatus status = decoder_decode(memory, pc, &decoded);
    if (status != DECODER_STATUS_OK) {
        dp->valid = false;
        dp->bus_addr = pc;
        return status;
    }

    ControlSignals ctrl;
    if (control_unit_decode(&decoded, flags, &ctrl) != CONTROL_UNIT_OK) {
        dp->valid = false;
        return DECODER_STATUS_INVALID_INSTRUCTION;
    }

    dp->ir_pc = pc;
    dp->ir = decoded;
    dp->ctrl = ctrl;
    dp->bus_addr = pc;
    dp->mdr = 0;
    dp->alu_in_a = 0;
    dp->alu_in_b = 0;
    dp->alu_out = 0;
    dp->alu_rem = 0;
    dp->agu_raw_addr = 0;
    dp->effective_addr = 0;
    dp->effective_addr_valid = false;
    dp->next_pc_target = decoded.next_pc;
    dp->valid = true;

    return DECODER_STATUS_OK;
}

static uint16_t read_gpr_safe(
    const uint16_t *gpr,
    size_t gpr_count,
    uint8_t index
)
{
    if (gpr == NULL || index >= gpr_count) {
        return 0;
    }

    return gpr[index];
}

void datapath_read_operands(
    DatapathState *dp,
    const uint16_t *gpr,
    size_t gpr_count,
    uint32_t sp
)
{
    if (dp == NULL || !dp->valid) {
        return;
    }

    uint16_t a = 0;
    uint16_t b = 0;

    switch (dp->ctrl.alu_src_a) {
        case ALU_SRC_A_REG_A:
            a = read_gpr_safe(gpr, gpr_count, dp->ir.reg_a);
            break;
        case ALU_SRC_A_REG_B:
            a = read_gpr_safe(gpr, gpr_count, dp->ir.reg_b);
            break;
        case ALU_SRC_A_SP:
            a = (uint16_t)(sp & 0xFFFFu);
            break;
        case ALU_SRC_A_NONE:
        default:
            a = 0;
            break;
    }

    switch (dp->ctrl.alu_src_b) {
        case ALU_SRC_B_REG_B:
            b = read_gpr_safe(gpr, gpr_count, dp->ir.reg_b);
            break;
        case ALU_SRC_B_REG_C:
            b = read_gpr_safe(gpr, gpr_count, dp->ir.reg_c);
            break;
        case ALU_SRC_B_IMM16:
            b = dp->ir.immediate16;
            break;
        case ALU_SRC_B_IMM8:
            b = (uint16_t)dp->ir.immediate8;
            break;
        case ALU_SRC_B_OFFSET16:
            b = (uint16_t)dp->ir.offset16;
            break;
        case ALU_SRC_B_CONST_1:
            b = 1u;
            break;
        case ALU_SRC_B_ZERO:
        case ALU_SRC_B_NONE:
        default:
            b = 0;
            break;
    }

    dp->alu_in_a = a;
    dp->alu_in_b = b;

    if (!dp->ctrl.agu_enable) {
        dp->agu_raw_addr = 0;
        dp->effective_addr = 0;
        dp->effective_addr_valid = false;
        return;
    }

    int64_t raw = 0;

    switch (dp->ctrl.addr_mode) {
        case ADDR_MODE_ABS18:
            raw = (int64_t)dp->ir.absolute_address;
            break;
        case ADDR_MODE_REG_OFFSET:
            raw = (int64_t)read_gpr_safe(gpr, gpr_count, dp->ir.reg_b) +
                  (int64_t)dp->ir.offset16;
            break;
        case ADDR_MODE_INDEXED:
            raw = (int64_t)read_gpr_safe(gpr, gpr_count, dp->ir.reg_b) +
                  (int64_t)read_gpr_safe(gpr, gpr_count, dp->ir.reg_c);
            break;
        case ADDR_MODE_SP_OFFSET:
            raw = (int64_t)sp + (int64_t)dp->ir.offset16;
            break;
        case ADDR_MODE_NONE:
        default:
            raw = 0;
            break;
    }

    dp->agu_raw_addr = raw;
    if (raw >= 0 && raw <= (int64_t)DIOCPU_MEMORY_MAX_ADDRESS) {
        dp->effective_addr = (uint32_t)raw;
        dp->effective_addr_valid = true;
        dp->bus_addr = dp->effective_addr;
    } else {
        dp->effective_addr = (uint32_t)(raw & 0xFFFFu);
        dp->effective_addr_valid = false;
    }
}

ALUStatus datapath_run_alu(
    DatapathState *dp,
    uint16_t current_flags,
    uint16_t *out_alu_flags
)
{
    if (dp == NULL || out_alu_flags == NULL || !dp->valid) {
        return ALU_STATUS_INVALID_OPERATION;
    }

    ALUResult res = alu_execute(
        dp->ctrl.alu_op,
        dp->alu_in_a,
        dp->alu_in_b,
        current_flags & CPU_FLAGS_DEFINED_MASK
    );

    if (res.status != ALU_STATUS_OK) {
        return res.status;
    }

    dp->alu_out = res.result;
    dp->alu_rem = res.remainder;
    *out_alu_flags = res.flags & CPU_FLAGS_DEFINED_MASK;

    return ALU_STATUS_OK;
}

uint16_t datapath_select_writeback(
    const DatapathState *dp,
    const uint16_t *gpr,
    size_t gpr_count
)
{
    if (dp == NULL || !dp->valid) {
        return 0;
    }

    switch (dp->ctrl.wb_src) {
        case WB_SRC_ALU_RESULT:
            return dp->alu_out;
        case WB_SRC_MEM_DATA:
            return dp->mdr;
        case WB_SRC_IMM16:
            return dp->ir.immediate16;
        case WB_SRC_REG_B:
            return read_gpr_safe(gpr, gpr_count, dp->ir.reg_b);
        case WB_SRC_EFF_ADDR:
            return (uint16_t)(dp->agu_raw_addr & 0xFFFF);
        case WB_SRC_NONE:
        default:
            return 0;
    }
}

bool datapath_resolve_next_pc(
    const DatapathState *dp,
    const uint16_t *gpr,
    size_t gpr_count,
    uint32_t current_pc,
    uint32_t stack_ret_pc,
    uint32_t ivt_handler_pc,
    uint32_t *out_next_pc
)
{
    if (dp == NULL || out_next_pc == NULL || !dp->valid) {
        return false;
    }

    int64_t target = 0;

    switch (dp->ctrl.pc_src) {
        case PC_SRC_NEXT_PC:
            target = (int64_t)dp->ir.next_pc;
            break;
        case PC_SRC_ABS18:
            target = (int64_t)dp->ir.absolute_address;
            break;
        case PC_SRC_REG_A:
            target = (int64_t)read_gpr_safe(gpr, gpr_count, dp->ir.reg_a);
            break;
        case PC_SRC_REL8_TAKEN:
            target = (int64_t)dp->ir.next_pc + (int64_t)dp->ir.relative_offset;
            break;
        case PC_SRC_STACK_RET:
            target = (int64_t)stack_ret_pc;
            break;
        case PC_SRC_IVT_HANDLER:
            target = (int64_t)ivt_handler_pc;
            break;
        case PC_SRC_HOLD:
            target = (dp->ir.next_pc <= DIOCPU_MEMORY_MAX_ADDRESS)
                ? (int64_t)dp->ir.next_pc
                : (int64_t)current_pc;
            break;
        default:
            return false;
    }

    if (target < 0 || target > (int64_t)DIOCPU_MEMORY_MAX_ADDRESS) {
        return false;
    }

    *out_next_pc = (uint32_t)target;
    return true;
}

