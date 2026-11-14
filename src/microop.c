#include "microop.h"

#include <stdio.h>
#include <string.h>

static void append_uop(
    MicroOpSequence *seq,
    MicroOpKind kind,
    uint8_t latency_cycles
)
{
    if (seq->count < DIOCPU_MAX_MICROOPS) {
        seq->ops[seq->count].kind = kind;
        seq->ops[seq->count].latency_cycles = latency_cycles;
        seq->count++;
    }
}

void microop_context_reset(MicroOpContext *ctx)
{
    if (ctx == NULL) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
}

bool microop_build_sequence(
    const DecodedInstruction *instr,
    const ControlSignals *ctrl,
    MicroOpSequence *sequence
)
{
    if (instr == NULL || ctrl == NULL || sequence == NULL) {
        return false;
    }

    memset(sequence, 0, sizeof(*sequence));
    append_uop(sequence, UOP_FETCH_DECODE, 1u);

    if (ctrl->is_halt) {
        append_uop(sequence, UOP_HALT, 1u);
        return true;
    }

    if (ctrl->sets_ivtr) {
        append_uop(sequence, UOP_SET_IVTR, 1u);
        append_uop(sequence, UOP_PC_UPDATE, 1u);
        return true;
    }

    if (ctrl->is_interrupt && !ctrl->is_return) {
        /* Software INT imm8 */
        append_uop(sequence, UOP_IVT_LOOKUP, 1u);
        append_uop(sequence, UOP_STACK_PUSH_PC, 2u);
        append_uop(sequence, UOP_STACK_PUSH_FLAGS, 1u);
        append_uop(sequence, UOP_UPDATE_SYS_FLAGS, 1u);
        append_uop(sequence, UOP_PC_UPDATE, 1u);
        return true;
    }

    if (ctrl->is_interrupt && ctrl->is_return) {
        /* IRET */
        append_uop(sequence, UOP_STACK_POP_FLAGS, 1u);
        append_uop(sequence, UOP_STACK_POP_PC, 2u);
        append_uop(sequence, UOP_PC_UPDATE, 1u);
        return true;
    }

    if (ctrl->flags_write_system && !ctrl->is_interrupt) {
        /* CLI / STI */
        append_uop(sequence, UOP_UPDATE_SYS_FLAGS, 1u);
        append_uop(sequence, UOP_PC_UPDATE, 1u);
        return true;
    }

    if (ctrl->alu_src_a != ALU_SRC_A_NONE ||
        ctrl->alu_src_b != ALU_SRC_B_NONE ||
        ctrl->agu_enable) {
        append_uop(sequence, UOP_READ_OPERANDS, 1u);
    }

    if (ctrl->agu_enable) {
        append_uop(sequence, UOP_AGU, 1u);
    }

    if (ctrl->alu_enable) {
        uint8_t alu_lat = 1u;
        if (ctrl->alu_op == ALU_MUL || ctrl->alu_op == ALU_MULS) {
            alu_lat = 2u;
        } else if (ctrl->alu_op == ALU_DIV || ctrl->alu_op == ALU_DIVS) {
            alu_lat = 4u;
        }
        append_uop(sequence, UOP_ALU, alu_lat);
    }

    if (ctrl->mem_read) {
        append_uop(sequence, UOP_MEM_READ, 2u);
    }

    if (ctrl->mem_write) {
        append_uop(sequence, UOP_MEM_WRITE, 2u);
    }

    if (ctrl->stack_push) {
        if (ctrl->stack_is_32bit) {
            append_uop(sequence, UOP_STACK_PUSH_PC, 2u);
        } else {
            append_uop(sequence, UOP_STACK_PUSH16, 2u);
        }
    }

    if (ctrl->stack_pop) {
        if (ctrl->stack_is_32bit) {
            append_uop(sequence, UOP_STACK_POP_PC, 2u);
        } else {
            append_uop(sequence, UOP_STACK_POP16, 2u);
        }
    }

    if (ctrl->reg_write_a || ctrl->reg_write_b || ctrl->flags_write_alu) {
        append_uop(sequence, UOP_REG_WRITEBACK, 1u);
    }

    append_uop(sequence, UOP_PC_UPDATE, 1u);
    return true;
}

bool microop_build_irq_sequence(
    uint8_t vector,
    MicroOpSequence *sequence
)
{
    if (sequence == NULL) {
        return false;
    }

    memset(sequence, 0, sizeof(*sequence));
    sequence->is_irq_sequence = true;
    sequence->irq_vector = vector;

    append_uop(sequence, UOP_IVT_LOOKUP, 1u);
    append_uop(sequence, UOP_STACK_PUSH_PC, 2u);
    append_uop(sequence, UOP_STACK_PUSH_FLAGS, 1u);
    append_uop(sequence, UOP_UPDATE_SYS_FLAGS, 1u);
    append_uop(sequence, UOP_PC_UPDATE, 1u);

    return true;
}

const char *microop_kind_name(MicroOpKind kind)
{
    switch (kind) {
        case UOP_NONE:             return "NONE";
        case UOP_FETCH_DECODE:     return "FETCH_DECODE";
        case UOP_READ_OPERANDS:    return "READ_OPERANDS";
        case UOP_AGU:              return "AGU";
        case UOP_ALU:              return "ALU";
        case UOP_MEM_READ:         return "MEM_READ";
        case UOP_MEM_WRITE:        return "MEM_WRITE";
        case UOP_STACK_PUSH16:     return "STACK_PUSH16";
        case UOP_STACK_POP16:      return "STACK_POP16";
        case UOP_STACK_PUSH_PC:    return "STACK_PUSH_PC";
        case UOP_STACK_POP_PC:     return "STACK_POP_PC";
        case UOP_STACK_PUSH_FLAGS: return "STACK_PUSH_FLAGS";
        case UOP_STACK_POP_FLAGS:  return "STACK_POP_FLAGS";
        case UOP_IVT_LOOKUP:       return "IVT_LOOKUP";
        case UOP_SET_IVTR:         return "SET_IVTR";
        case UOP_UPDATE_SYS_FLAGS: return "UPDATE_SYS_FLAGS";
        case UOP_REG_WRITEBACK:    return "REG_WRITEBACK";
        case UOP_PC_UPDATE:        return "PC_UPDATE";
        case UOP_HALT:             return "HALT";
        default:                   return "UNKNOWN";
    }
}

size_t microop_format_sequence(
    const MicroOpSequence *sequence,
    char *buffer,
    size_t size
)
{
    if (sequence == NULL || buffer == NULL || size == 0) {
        return 0;
    }

    size_t offset = 0;
    buffer[0] = '\0';

    for (uint8_t i = 0; i < sequence->count; i++) {
        int written = snprintf(
            buffer + offset,
            size - offset,
            "%s%s",
            (i > 0) ? " -> " : "",
            microop_kind_name(sequence->ops[i].kind)
        );
        if (written < 0) {
            break;
        }
        size_t adv = (size_t)written;
        if (adv >= size - offset) {
            offset = size - 1u;
            break;
        }
        offset += adv;
    }

    return offset;
}

