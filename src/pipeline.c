#include "pipeline.h"

#include <stdio.h>
#include <string.h>

void pipeline_reset(PipelineState *pipe, uint32_t initial_pc)
{
    if (pipe == NULL) {
        return;
    }

    bool prev_fwd = pipe->initialized ? pipe->config.forwarding_enabled : true;
    uint8_t prev_mem_lat = (pipe->initialized && pipe->config.mem_stage_latency > 0u)
                               ? pipe->config.mem_stage_latency
                               : 2u;
    BranchPredictorConfig prev_bp = pipe->initialized && bp_config_is_valid(&pipe->bp.config)
                                        ? pipe->bp.config
                                        : bp_default_config();

    memset(pipe, 0, sizeof(*pipe));
    pipe->initialized = true;
    pipe->config.forwarding_enabled = prev_fwd;
    pipe->config.mem_stage_latency = prev_mem_lat;
    pipe->fetch_pc = initial_pc;
    bp_init(&pipe->bp, &prev_bp);
}

void pipeline_collect_usage(
    const DecodedInstruction *ir,
    const ControlSignals *ctrl,
    PipelineRegUsage *usage
)
{
    if (usage == NULL) {
        return;
    }

    memset(usage, 0, sizeof(*usage));

    if (ir == NULL || ctrl == NULL) {
        return;
    }

    switch (ctrl->alu_src_a) {
        case ALU_SRC_A_REG_A:
            usage->read_gpr_mask |= (uint16_t)(1u << ir->reg_a);
            break;
        case ALU_SRC_A_REG_B:
            usage->read_gpr_mask |= (uint16_t)(1u << ir->reg_b);
            break;
        case ALU_SRC_A_SP:
            usage->reads_sp = true;
            break;
        case ALU_SRC_A_NONE:
        default:
            break;
    }

    switch (ctrl->alu_src_b) {
        case ALU_SRC_B_REG_B:
            usage->read_gpr_mask |= (uint16_t)(1u << ir->reg_b);
            break;
        case ALU_SRC_B_REG_C:
            usage->read_gpr_mask |= (uint16_t)(1u << ir->reg_c);
            break;
        default:
            break;
    }

    if (ctrl->agu_enable) {
        if (ctrl->addr_mode == ADDR_MODE_REG_OFFSET) {
            usage->read_gpr_mask |= (uint16_t)(1u << ir->reg_b);
        } else if (ctrl->addr_mode == ADDR_MODE_INDEXED) {
            usage->read_gpr_mask |= (uint16_t)(1u << ir->reg_b);
            usage->read_gpr_mask |= (uint16_t)(1u << ir->reg_c);
        } else if (ctrl->addr_mode == ADDR_MODE_SP_OFFSET) {
            usage->reads_sp = true;
        }
    }

    if (ctrl->wb_src == WB_SRC_REG_B) {
        usage->read_gpr_mask |= (uint16_t)(1u << ir->reg_b);
    }

    if (ctrl->mem_write) {
        usage->read_gpr_mask |= (uint16_t)(1u << ir->reg_a);
    }

    if (ctrl->pc_src == PC_SRC_REG_A) {
        usage->read_gpr_mask |= (uint16_t)(1u << ir->reg_a);
    }

    if (ctrl->reg_write_a) {
        usage->write_gpr_mask |= (uint16_t)(1u << ir->reg_a);
        if (ctrl->wb_src == WB_SRC_MEM_DATA) {
            usage->is_load_producer = true;
        }
    }

    if (ctrl->reg_write_b) {
        usage->write_gpr_mask |= (uint16_t)(1u << ir->reg_b);
    }

    usage->reads_flags = ctrl->is_branch || (ctrl->alu_enable && !ctrl->is_interrupt);
    usage->writes_flags = ctrl->flags_write_alu || ctrl->flags_write_system ||
                          ctrl->is_interrupt;

    if (ctrl->stack_push || ctrl->stack_pop) {
        usage->reads_sp = true;
        usage->writes_sp = true;
    }

    usage->serializes_system = ctrl->is_return || ctrl->is_interrupt ||
                               ctrl->sets_ivtr || ctrl->flags_write_system;

    usage->serializes_frontend = ctrl->is_halt || usage->serializes_system;
}

bool pipeline_detect_data_hazard(
    const PipelineIDEXLatch *consumer_id,
    const PipelineIDEXLatch *ex_stage,
    const PipelineEXMEMLatch *mem_stage,
    const PipelineMEMWBLatch *wb_stage,
    bool forwarding_enabled,
    bool *out_load_use,
    bool *out_system_stall
)
{
    if (out_load_use != NULL) {
        *out_load_use = false;
    }
    if (out_system_stall != NULL) {
        *out_system_stall = false;
    }

    if (consumer_id == NULL || !consumer_id->valid ||
        consumer_id->fault != 0u) {
        return false;
    }

    PipelineRegUsage cons;
    pipeline_collect_usage(&consumer_id->ir, &consumer_id->ctrl, &cons);

    /* System instructions (INT/IRET/CLI/STI/SETIVT/RET) wait for older in-flight state */
    if (cons.serializes_system) {
        bool older_busy =
            (ex_stage != NULL && ex_stage->valid) ||
            (mem_stage != NULL && mem_stage->valid) ||
            (wb_stage != NULL && wb_stage->valid);
        if (older_busy) {
            if (out_system_stall != NULL) {
                *out_system_stall = true;
            }
            return true;
        }
    }

    if (ex_stage != NULL && ex_stage->valid && ex_stage->fault == 0u) {
        PipelineRegUsage ex_u;
        pipeline_collect_usage(&ex_stage->ir, &ex_stage->ctrl, &ex_u);

        /* SP is modified in MEM, so an SP consumer stalls while an SP writer is in EX */
        if ((cons.reads_sp || cons.writes_sp) && ex_u.writes_sp) {
            return true;
        }

        bool gpr_conflict = (cons.read_gpr_mask & ex_u.write_gpr_mask) != 0u;
        bool flag_conflict = cons.reads_flags && ex_u.writes_flags;

        if (ex_u.is_load_producer && gpr_conflict) {
            if (out_load_use != NULL) {
                *out_load_use = true;
            }
            return true;
        }

        if (!forwarding_enabled && (gpr_conflict || flag_conflict)) {
            return true;
        }
    }

    if (!forwarding_enabled &&
        mem_stage != NULL &&
        mem_stage->valid &&
        mem_stage->fault == 0u) {
        PipelineRegUsage mem_u;
        pipeline_collect_usage(&mem_stage->ir, &mem_stage->ctrl, &mem_u);

        bool gpr_conflict = (cons.read_gpr_mask & mem_u.write_gpr_mask) != 0u;
        bool flag_conflict = cons.reads_flags && mem_u.writes_flags;
        if (gpr_conflict || flag_conflict) {
            return true;
        }
    }

    return false;
}

const char *pipeline_stage_name(PipelineStageId stage)
{
    switch (stage) {
        case PIPE_STAGE_IF:  return "IF";
        case PIPE_STAGE_ID:  return "ID";
        case PIPE_STAGE_EX:  return "EX";
        case PIPE_STAGE_MEM: return "MEM";
        case PIPE_STAGE_WB:  return "WB";
        default:             return "UNKNOWN";
    }
}

double pipeline_cpi(const PipelineState *pipe)
{
    if (pipe == NULL || pipe->stats.instructions_retired == 0u) {
        return 0.0;
    }

    return (double)pipe->stats.cycles / (double)pipe->stats.instructions_retired;
}

double pipeline_ipc(const PipelineState *pipe)
{
    if (pipe == NULL || pipe->stats.cycles == 0u) {
        return 0.0;
    }

    return (double)pipe->stats.instructions_retired / (double)pipe->stats.cycles;
}

size_t pipeline_format_summary(
    const PipelineState *pipe,
    char *buffer,
    size_t size
)
{
    if (pipe == NULL || buffer == NULL || size == 0u) {
        return 0;
    }

    int written = snprintf(
        buffer,
        size,
        "cycles=%llu retired=%llu cpi=%.2f ipc=%.2f fwd=%llu stalls=%llu (load_use=%llu flush=%llu)",
        (unsigned long long)pipe->stats.cycles,
        (unsigned long long)pipe->stats.instructions_retired,
        pipeline_cpi(pipe),
        pipeline_ipc(pipe),
        (unsigned long long)pipe->stats.forwards,
        (unsigned long long)pipe->stats.stall_cycles,
        (unsigned long long)pipe->stats.load_use_stalls,
        (unsigned long long)pipe->stats.control_hazard_flushes
    );

    if (written < 0) {
        buffer[0] = '\0';
        return 0;
    }

    size_t len = (size_t)written;
    return (len >= size) ? (size - 1u) : len;
}
