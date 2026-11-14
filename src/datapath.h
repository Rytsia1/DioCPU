#ifndef DIOCPU_DATAPATH_H
#define DIOCPU_DATAPATH_H

#include "alu.h"
#include "control_unit.h"
#include "decoder.h"
#include "memory.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t ir_pc;
    DecodedInstruction ir;
    ControlSignals ctrl;

    uint32_t bus_addr;
    uint16_t mdr;

    uint16_t alu_in_a;
    uint16_t alu_in_b;
    uint16_t alu_out;
    uint16_t alu_rem;

    int64_t agu_raw_addr;
    uint32_t effective_addr;
    bool effective_addr_valid;

    uint32_t next_pc_target;
    bool valid;
} DatapathState;

void datapath_reset(DatapathState *dp);

DecoderStatus datapath_fetch_decode(
    const Memory *memory,
    uint32_t pc,
    uint16_t flags,
    DatapathState *dp
);

void datapath_read_operands(
    DatapathState *dp,
    const uint16_t *gpr,
    size_t gpr_count,
    uint32_t sp
);

ALUStatus datapath_run_alu(
    DatapathState *dp,
    uint16_t current_flags,
    uint16_t *out_alu_flags
);

uint16_t datapath_select_writeback(
    const DatapathState *dp,
    const uint16_t *gpr,
    size_t gpr_count
);

bool datapath_resolve_next_pc(
    const DatapathState *dp,
    const uint16_t *gpr,
    size_t gpr_count,
    uint32_t current_pc,
    uint32_t stack_ret_pc,
    uint32_t ivt_handler_pc,
    uint32_t *out_next_pc
);

#endif

