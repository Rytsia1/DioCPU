#ifndef DIOCPU_PIPELINE_H
#define DIOCPU_PIPELINE_H

#include "control_unit.h"
#include "decoder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    PIPE_STAGE_IF = 0,
    PIPE_STAGE_ID,
    PIPE_STAGE_EX,
    PIPE_STAGE_MEM,
    PIPE_STAGE_WB,
    PIPE_STAGE_COUNT
} PipelineStageId;

typedef struct {
    bool forwarding_enabled;
    uint8_t mem_stage_latency;
} PipelineConfig;

typedef struct {
    bool valid;
    uint32_t pc;
    DecodedInstruction ir;
    uint8_t fault;
} PipelineIFIDLatch;

typedef struct {
    bool valid;
    uint32_t pc;
    DecodedInstruction ir;
    ControlSignals ctrl;
    uint8_t fault;
} PipelineIDEXLatch;

typedef struct {
    bool valid;
    uint32_t pc;
    DecodedInstruction ir;
    ControlSignals ctrl;
    uint8_t fault;

    uint32_t effective_addr;
    bool effective_addr_valid;
    int64_t agu_raw_addr;

    uint16_t wb_value;
    uint16_t wb_rem;
    uint16_t alu_flags;
    uint16_t store_value;
    uint32_t committed_next_pc;
} PipelineEXMEMLatch;

typedef struct {
    bool valid;
    uint32_t pc;
    DecodedInstruction ir;
    ControlSignals ctrl;
    uint8_t fault;

    uint16_t wb_value;
    uint16_t wb_rem;
    uint16_t alu_flags;
    uint16_t restored_flags;
    uint32_t committed_next_pc;
} PipelineMEMWBLatch;

typedef struct {
    bool valid;
    bool stalled;
    bool flushed;
    bool forwarded;
    uint32_t pc;
    InstructionOpcode opcode;
} PipelineSlotSummary;

typedef struct {
    uint64_t cycles;
    uint64_t instructions_retired;
    uint64_t stall_cycles;
    uint64_t data_hazard_stalls;
    uint64_t load_use_stalls;
    uint64_t control_hazard_flushes;
    uint64_t control_stalls;
    uint64_t structural_ex_stalls;
    uint64_t structural_mem_stalls;
    uint64_t forwards;
    uint64_t bubbles;
} PipelineStats;

typedef struct {
    uint16_t read_gpr_mask;
    uint16_t write_gpr_mask;
    bool reads_flags;
    bool writes_flags;
    bool reads_sp;
    bool writes_sp;
    bool is_load_producer;
    bool serializes_frontend;
    bool serializes_system;
} PipelineRegUsage;

typedef struct {
    bool initialized;
    PipelineConfig config;

    uint32_t fetch_pc;
    uint8_t ex_cycles_remaining;
    uint8_t mem_cycles_remaining;
    uint8_t irq_cycles_remaining;
    uint8_t pending_irq_vector;
    bool irq_entry_active;

    PipelineIFIDLatch if_id;
    PipelineIDEXLatch id_ex;
    PipelineEXMEMLatch ex_mem;
    PipelineMEMWBLatch mem_wb;

    PipelineSlotSummary stage_snapshot[PIPE_STAGE_COUNT];
    PipelineStats stats;
} PipelineState;

void pipeline_reset(PipelineState *pipe, uint32_t initial_pc);

void pipeline_collect_usage(
    const DecodedInstruction *ir,
    const ControlSignals *ctrl,
    PipelineRegUsage *usage
);

bool pipeline_detect_data_hazard(
    const PipelineIDEXLatch *consumer_id,
    const PipelineIDEXLatch *ex_stage,
    const PipelineEXMEMLatch *mem_stage,
    const PipelineMEMWBLatch *wb_stage,
    bool forwarding_enabled,
    bool *out_load_use,
    bool *out_system_stall
);

const char *pipeline_stage_name(PipelineStageId stage);
double pipeline_cpi(const PipelineState *pipe);
double pipeline_ipc(const PipelineState *pipe);
size_t pipeline_format_summary(
    const PipelineState *pipe,
    char *buffer,
    size_t size
);

#endif
