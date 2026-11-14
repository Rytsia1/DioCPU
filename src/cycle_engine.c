#include "cycle_engine.h"

#include <stdio.h>
#include <string.h>

void cycle_engine_reset(CycleEngine *engine)
{
    if (engine == NULL) {
        return;
    }

    memset(engine, 0, sizeof(*engine));
}

CycleStage cycle_stage_from_uop(MicroOpKind kind)
{
    switch (kind) {
        case UOP_FETCH_DECODE:
            return CYCLE_STAGE_FETCH;

        case UOP_READ_OPERANDS:
        case UOP_AGU:
        case UOP_IVT_LOOKUP:
            return CYCLE_STAGE_OPERAND;

        case UOP_ALU:
        case UOP_SET_IVTR:
        case UOP_UPDATE_SYS_FLAGS:
        case UOP_HALT:
            return CYCLE_STAGE_EXECUTE;

        case UOP_MEM_READ:
        case UOP_MEM_WRITE:
        case UOP_STACK_PUSH16:
        case UOP_STACK_POP16:
        case UOP_STACK_PUSH_PC:
        case UOP_STACK_POP_PC:
        case UOP_STACK_PUSH_FLAGS:
        case UOP_STACK_POP_FLAGS:
            return CYCLE_STAGE_MEMORY;

        case UOP_REG_WRITEBACK:
        case UOP_PC_UPDATE:
            return CYCLE_STAGE_WRITEBACK;

        case UOP_NONE:
        default:
            return CYCLE_STAGE_IDLE;
    }
}

const char *cycle_stage_name(CycleStage stage)
{
    switch (stage) {
        case CYCLE_STAGE_IDLE:      return "IDLE";
        case CYCLE_STAGE_FETCH:     return "FETCH";
        case CYCLE_STAGE_OPERAND:   return "OPERAND";
        case CYCLE_STAGE_EXECUTE:   return "EXECUTE";
        case CYCLE_STAGE_MEMORY:    return "MEMORY";
        case CYCLE_STAGE_WRITEBACK: return "WRITEBACK";
        case CYCLE_STAGE_FAULT:     return "FAULT";
        default:                    return "UNKNOWN";
    }
}

double cycle_engine_cpi(
    const CycleEngine *engine,
    uint64_t instructions_executed
)
{
    if (engine == NULL || instructions_executed == 0u) {
        return 0.0;
    }

    return (double)engine->total_cycles / (double)instructions_executed;
}

double cycle_engine_ipc(
    const CycleEngine *engine,
    uint64_t instructions_executed
)
{
    if (engine == NULL || engine->total_cycles == 0u) {
        return 0.0;
    }

    return (double)instructions_executed / (double)engine->total_cycles;
}

void cycle_engine_record_tick(
    CycleEngine *engine,
    uint32_t pc,
    MicroOp op,
    uint8_t cycle_in_uop,
    bool stalled,
    bool retired_uop,
    bool retired_instruction,
    bool is_irq
)
{
    if (engine == NULL) {
        return;
    }

    CycleTraceEntry entry;
    entry.cycle_number = engine->total_cycles;
    entry.pc = pc;
    entry.uop_kind = op.kind;
    entry.stage = engine->current_stage;
    entry.uop_cycle_index = cycle_in_uop;
    entry.uop_latency = op.latency_cycles;
    entry.stalled = stalled;
    entry.retired_uop = retired_uop;
    entry.retired_instruction = retired_instruction;
    entry.is_irq = is_irq;

    engine->trace[engine->trace_head] = entry;
    engine->trace_head = (uint8_t)((engine->trace_head + 1u) % DIOCPU_CYCLE_TRACE_CAP);
    if (engine->trace_count < DIOCPU_CYCLE_TRACE_CAP) {
        engine->trace_count++;
    }
}

size_t cycle_engine_get_recent_trace(
    const CycleEngine *engine,
    CycleTraceEntry *out_entries,
    size_t max_entries
)
{
    if (engine == NULL || out_entries == NULL || max_entries == 0u ||
        engine->trace_count == 0u) {
        return 0;
    }

    size_t available = (size_t)engine->trace_count;
    size_t to_copy = (available < max_entries) ? available : max_entries;

    /* Copy oldest-to-newest among the requested trailing window */
    size_t oldest_idx = ((size_t)engine->trace_head + DIOCPU_CYCLE_TRACE_CAP - to_copy) %
                        DIOCPU_CYCLE_TRACE_CAP;

    for (size_t i = 0; i < to_copy; i++) {
        size_t idx = (oldest_idx + i) % DIOCPU_CYCLE_TRACE_CAP;
        out_entries[i] = engine->trace[idx];
    }

    return to_copy;
}

size_t cycle_engine_format_status(
    const CycleEngine *engine,
    uint64_t instructions_executed,
    char *buffer,
    size_t size
)
{
    if (engine == NULL || buffer == NULL || size == 0u) {
        return 0;
    }

    int written = snprintf(
        buffer,
        size,
        "cycles=%llu stall=%llu irq=%llu stage=%s uop=%s(%u/%u) cpi=%.2f",
        (unsigned long long)engine->total_cycles,
        (unsigned long long)engine->busy_stall_cycles,
        (unsigned long long)engine->interrupt_cycles,
        cycle_stage_name(engine->current_stage),
        microop_kind_name(engine->active_uop.kind),
        (unsigned int)engine->uop_cycles_elapsed,
        (unsigned int)engine->active_uop.latency_cycles,
        cycle_engine_cpi(engine, instructions_executed)
    );

    if (written < 0) {
        buffer[0] = '\0';
        return 0;
    }

    size_t out_len = (size_t)written;
    return (out_len >= size) ? (size - 1u) : out_len;
}

