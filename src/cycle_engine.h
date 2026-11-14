#ifndef DIOCPU_CYCLE_ENGINE_H
#define DIOCPU_CYCLE_ENGINE_H

#include "microop.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIOCPU_CYCLE_TRACE_CAP 32u

typedef enum {
    CYCLE_STAGE_IDLE = 0,
    CYCLE_STAGE_FETCH,
    CYCLE_STAGE_OPERAND,
    CYCLE_STAGE_EXECUTE,
    CYCLE_STAGE_MEMORY,
    CYCLE_STAGE_WRITEBACK,
    CYCLE_STAGE_FAULT
} CycleStage;

typedef struct {
    uint64_t cycle_number;
    uint32_t pc;
    MicroOpKind uop_kind;
    CycleStage stage;
    uint8_t uop_cycle_index;
    uint8_t uop_latency;
    bool stalled;
    bool retired_uop;
    bool retired_instruction;
    bool is_irq;
} CycleTraceEntry;

typedef struct {
    uint64_t total_cycles;
    uint64_t active_cycles;
    uint64_t busy_stall_cycles;
    uint64_t interrupt_cycles;
    uint64_t last_instruction_cycles;
    uint64_t current_instruction_cycles;

    bool uop_in_flight;
    MicroOp active_uop;
    uint32_t active_pc;
    uint8_t uop_cycles_elapsed;
    uint8_t uop_cycles_remaining;
    CycleStage current_stage;
    bool current_cycle_stalled;

    bool cycle_breakpoint_enabled;
    uint64_t cycle_breakpoint;

    CycleTraceEntry trace[DIOCPU_CYCLE_TRACE_CAP];
    uint8_t trace_count;
    uint8_t trace_head;
} CycleEngine;

void cycle_engine_reset(CycleEngine *engine);

CycleStage cycle_stage_from_uop(MicroOpKind kind);
const char *cycle_stage_name(CycleStage stage);

double cycle_engine_cpi(
    const CycleEngine *engine,
    uint64_t instructions_executed
);

double cycle_engine_ipc(
    const CycleEngine *engine,
    uint64_t instructions_executed
);

void cycle_engine_record_tick(
    CycleEngine *engine,
    uint32_t pc,
    MicroOp op,
    uint8_t cycle_in_uop,
    bool stalled,
    bool retired_uop,
    bool retired_instruction,
    bool is_irq
);

size_t cycle_engine_get_recent_trace(
    const CycleEngine *engine,
    CycleTraceEntry *out_entries,
    size_t max_entries
);

size_t cycle_engine_format_status(
    const CycleEngine *engine,
    uint64_t instructions_executed,
    char *buffer,
    size_t size
);

#endif

