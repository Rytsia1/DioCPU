#ifndef DIOCPU_MICROOP_H
#define DIOCPU_MICROOP_H

#include "control_unit.h"
#include "decoder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIOCPU_MAX_MICROOPS 10u

typedef enum {
    UOP_NONE = 0,
    UOP_FETCH_DECODE,
    UOP_READ_OPERANDS,
    UOP_AGU,
    UOP_ALU,
    UOP_MEM_READ,
    UOP_MEM_WRITE,
    UOP_STACK_PUSH16,
    UOP_STACK_POP16,
    UOP_STACK_PUSH_PC,
    UOP_STACK_POP_PC,
    UOP_STACK_PUSH_FLAGS,
    UOP_STACK_POP_FLAGS,
    UOP_IVT_LOOKUP,
    UOP_SET_IVTR,
    UOP_UPDATE_SYS_FLAGS,
    UOP_REG_WRITEBACK,
    UOP_PC_UPDATE,
    UOP_HALT
} MicroOpKind;

typedef struct {
    MicroOpKind kind;
    uint8_t latency_cycles;
} MicroOp;

typedef struct {
    MicroOp ops[DIOCPU_MAX_MICROOPS];
    uint8_t count;
    bool is_irq_sequence;
    uint8_t irq_vector;
} MicroOpSequence;

typedef struct {
    MicroOpSequence sequence;
    uint8_t step_index;
    bool active;

    uint32_t saved_sp;
    uint16_t saved_flags;
    uint32_t stack_ret_pc;
    uint32_t ivt_handler_pc;
    uint16_t new_alu_flags;

    MicroOpKind last_uop;
    uint64_t microops_executed;
} MicroOpContext;

void microop_context_reset(MicroOpContext *ctx);

bool microop_build_sequence(
    const DecodedInstruction *instr,
    const ControlSignals *ctrl,
    MicroOpSequence *sequence
);

bool microop_build_irq_sequence(
    uint8_t vector,
    MicroOpSequence *sequence
);

const char *microop_kind_name(MicroOpKind kind);

uint32_t microop_sequence_total_cycles(const MicroOpSequence *sequence);

size_t microop_format_sequence(
    const MicroOpSequence *sequence,
    char *buffer,
    size_t size
);

#endif

