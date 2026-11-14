#include "branch_predictor.h"

#include "memory.h"

#include <stdio.h>
#include <string.h>

static bool bp_is_power_of_two_u32(uint32_t v)
{
    return v != 0u && (v & (v - 1u)) == 0u;
}

static bool bp_opcode_is_unconditional(InstructionOpcode opcode)
{
    return opcode == DIOCPU_OP_JMP ||
           opcode == DIOCPU_OP_JMPR ||
           opcode == DIOCPU_OP_CALL ||
           opcode == DIOCPU_OP_CALLR;
}

static bool bp_opcode_is_conditional(InstructionOpcode opcode)
{
    return opcode >= DIOCPU_OP_JZ && opcode <= DIOCPU_OP_JG;
}

static bool bp_opcode_is_direct_uncond(InstructionOpcode opcode)
{
    return opcode == DIOCPU_OP_JMP || opcode == DIOCPU_OP_CALL;
}

static uint32_t bp_bht_index(const BranchPredictor *bp, uint32_t pc, uint16_t ghr_val)
{
    uint32_t mask = (uint32_t)bp->config.bht_entries - 1u;
    if (bp->config.mode == BP_MODE_GSHARE) {
        uint32_t ghr_mask = (1u << bp->config.ghr_bits) - 1u;
        return (pc ^ ((uint32_t)ghr_val & ghr_mask)) & mask;
    }
    return pc & mask;
}

static uint32_t bp_btb_index(const BranchPredictor *bp, uint32_t pc)
{
    return pc & ((uint32_t)bp->config.btb_entries - 1u);
}

static void bp_seed_bht_counters(BranchPredictor *bp)
{
    if (bp->config.mode == BP_MODE_2BIT || bp->config.mode == BP_MODE_GSHARE) {
        /* Initialize 2-bit saturating counters to Weakly Not-Taken (1) */
        memset(bp->bht, 1, sizeof(bp->bht));
    } else {
        memset(bp->bht, 0, sizeof(bp->bht));
    }
}

BranchPredictorConfig bp_default_config(void)
{
    BranchPredictorConfig cfg;
    cfg.mode = BP_MODE_NONE;
    cfg.bht_entries = 64u;
    cfg.btb_entries = 64u;
    cfg.ghr_bits = 4u;
    return cfg;
}

bool bp_config_is_valid(const BranchPredictorConfig *config)
{
    if (config == NULL) {
        return false;
    }

    if (config->mode < BP_MODE_NONE || config->mode > BP_MODE_GSHARE) {
        return false;
    }

    if (!bp_is_power_of_two_u32(config->bht_entries) ||
        config->bht_entries > DIOCPU_BP_MAX_BHT_ENTRIES) {
        return false;
    }

    if (!bp_is_power_of_two_u32(config->btb_entries) ||
        config->btb_entries > DIOCPU_BP_MAX_BTB_ENTRIES) {
        return false;
    }

    if (config->ghr_bits == 0u || config->ghr_bits > DIOCPU_BP_MAX_GHR_BITS) {
        return false;
    }

    return true;
}

bool bp_init(BranchPredictor *bp, const BranchPredictorConfig *config)
{
    if (bp == NULL || !bp_config_is_valid(config)) {
        return false;
    }

    memset(bp, 0, sizeof(*bp));
    bp->config = *config;
    bp_seed_bht_counters(bp);
    return true;
}

void bp_reset(BranchPredictor *bp)
{
    if (bp == NULL) {
        return;
    }

    BranchPredictorConfig saved = bp->config;
    if (!bp_config_is_valid(&saved)) {
        saved = bp_default_config();
    }
    memset(bp, 0, sizeof(*bp));
    bp->config = saved;
    bp_seed_bht_counters(bp);
}

bool bp_instruction_is_control_flow(const DecodedInstruction *ir)
{
    if (ir == NULL) {
        return false;
    }

    return bp_opcode_is_unconditional(ir->opcode) ||
           bp_opcode_is_conditional(ir->opcode);
}

void bp_predict(
    BranchPredictor *bp,
    uint32_t pc,
    const DecodedInstruction *ir,
    bool *out_taken,
    uint32_t *out_next_pc,
    uint16_t *out_ghr_snapshot
)
{
    if (ir == NULL || out_taken == NULL || out_next_pc == NULL) {
        return;
    }

    *out_taken = false;
    *out_next_pc = ir->next_pc;
    if (out_ghr_snapshot != NULL) {
        *out_ghr_snapshot = (bp != NULL) ? bp->ghr : 0u;
    }

    if (bp == NULL || bp->config.mode == BP_MODE_NONE ||
        !bp_instruction_is_control_flow(ir)) {
        return;
    }

    bp->stats.lookups++;

    bool is_uncond = bp_opcode_is_unconditional(ir->opcode);

    if (bp->config.mode == BP_MODE_STATIC_BTFNT) {
        if (bp_opcode_is_direct_uncond(ir->opcode)) {
            if (ir->absolute_address <= DIOCPU_MEMORY_MAX_ADDRESS) {
                *out_taken = true;
                *out_next_pc = ir->absolute_address;
                bp->stats.predictions_taken++;
                return;
            }
        } else if (is_uncond) {
            uint32_t btb_idx = bp_btb_index(bp, pc);
            const BranchTargetEntry *e = &bp->btb[btb_idx];
            if (e->valid && e->tag == pc &&
                e->target_pc <= DIOCPU_MEMORY_MAX_ADDRESS) {
                bp->stats.btb_hits++;
                *out_taken = true;
                *out_next_pc = e->target_pc;
                bp->stats.predictions_taken++;
                return;
            }
            bp->stats.btb_misses++;
        } else if (ir->relative_offset < 0) {
            int64_t tgt = (int64_t)ir->next_pc + (int64_t)ir->relative_offset;
            if (tgt >= 0 && tgt <= (int64_t)DIOCPU_MEMORY_MAX_ADDRESS) {
                *out_taken = true;
                *out_next_pc = (uint32_t)tgt;
                bp->stats.predictions_taken++;
                return;
            }
        }

        bp->stats.predictions_not_taken++;
        return;
    }

    /* Dynamic predictors: require BTB hit to supply speculative target PC at IF */
    uint32_t btb_idx = bp_btb_index(bp, pc);
    const BranchTargetEntry *e = &bp->btb[btb_idx];
    bool btb_hit = e->valid && e->tag == pc &&
                   e->target_pc <= DIOCPU_MEMORY_MAX_ADDRESS;
    if (!btb_hit) {
        bp->stats.btb_misses++;
        bp->stats.predictions_not_taken++;
        return;
    }

    bp->stats.btb_hits++;

    bool dir_taken = false;
    if (is_uncond) {
        dir_taken = true;
    } else {
        uint32_t idx = bp_bht_index(bp, pc, bp->ghr);
        uint8_t ctr = bp->bht[idx];
        if (bp->config.mode == BP_MODE_1BIT) {
            dir_taken = (ctr != 0u);
        } else {
            dir_taken = (ctr >= 2u);
        }
    }

    if (dir_taken) {
        *out_taken = true;
        *out_next_pc = e->target_pc;
        bp->stats.predictions_taken++;
    } else {
        *out_taken = false;
        *out_next_pc = ir->next_pc;
        bp->stats.predictions_not_taken++;
    }
}

void bp_update(
    BranchPredictor *bp,
    uint32_t pc,
    const DecodedInstruction *ir,
    const ControlSignals *ctrl,
    bool pred_taken,
    uint32_t pred_next_pc,
    uint16_t pred_ghr_snapshot,
    bool actual_taken,
    uint32_t actual_next_pc
)
{
    if (bp == NULL || ir == NULL || ctrl == NULL ||
        bp->config.mode == BP_MODE_NONE ||
        !bp_instruction_is_control_flow(ir)) {
        return;
    }

    bp->stats.updates++;

    bool right_dir = (pred_taken == actual_taken);
    bool right_tgt = !actual_taken || (pred_next_pc == actual_next_pc);
    if (right_dir && right_tgt) {
        bp->stats.correct_predictions++;
    } else {
        bp->stats.mispredictions++;
        if (!right_dir) {
            bp->stats.direction_mispredictions++;
        } else {
            bp->stats.target_mispredictions++;
        }
    }

    bool is_uncond = bp_opcode_is_unconditional(ir->opcode);

    if (actual_taken && actual_next_pc <= DIOCPU_MEMORY_MAX_ADDRESS) {
        uint32_t btb_idx = bp_btb_index(bp, pc);
        bp->btb[btb_idx].valid = true;
        bp->btb[btb_idx].tag = pc;
        bp->btb[btb_idx].target_pc = actual_next_pc;
        bp->btb[btb_idx].is_unconditional = is_uncond;
    }

    if (!is_uncond && ctrl->is_branch &&
        (bp->config.mode == BP_MODE_1BIT ||
         bp->config.mode == BP_MODE_2BIT ||
         bp->config.mode == BP_MODE_GSHARE)) {
        uint32_t idx = bp_bht_index(bp, pc, pred_ghr_snapshot);
        if (bp->config.mode == BP_MODE_1BIT) {
            bp->bht[idx] = actual_taken ? 1u : 0u;
        } else {
            uint8_t ctr = bp->bht[idx] & 0x03u;
            if (actual_taken) {
                if (ctr < 3u) {
                    ctr++;
                }
            } else {
                if (ctr > 0u) {
                    ctr--;
                }
            }
            bp->bht[idx] = ctr;
        }

        uint16_t ghr_mask = (uint16_t)((1u << bp->config.ghr_bits) - 1u);
        bp->ghr = (uint16_t)(((bp->ghr << 1u) | (actual_taken ? 1u : 0u)) & ghr_mask);
    }
}

const char *bp_mode_name(BranchPredictorMode mode)
{
    switch (mode) {
        case BP_MODE_NONE:         return "NONE";
        case BP_MODE_STATIC_BTFNT: return "BTFNT";
        case BP_MODE_1BIT:         return "1BIT";
        case BP_MODE_2BIT:         return "2BIT";
        case BP_MODE_GSHARE:       return "GSHARE";
        default:                   return "UNKNOWN";
    }
}

double bp_accuracy(const BranchPredictorStats *stats)
{
    if (stats == NULL || stats->updates == 0u) {
        return 0.0;
    }

    return (double)stats->correct_predictions / (double)stats->updates;
}

size_t bp_format_stats(
    const BranchPredictor *bp,
    char *buffer,
    size_t size
)
{
    if (bp == NULL || buffer == NULL || size == 0u) {
        return 0u;
    }

    int written = snprintf(
        buffer,
        size,
        "BP[%s]: pred=%llu hit=%llu miss=%llu (acc=%.1f%%) dir_miss=%llu tgt_miss=%llu btb_hit=%llu",
        bp_mode_name(bp->config.mode),
        (unsigned long long)bp->stats.updates,
        (unsigned long long)bp->stats.correct_predictions,
        (unsigned long long)bp->stats.mispredictions,
        bp_accuracy(&bp->stats) * 100.0,
        (unsigned long long)bp->stats.direction_mispredictions,
        (unsigned long long)bp->stats.target_mispredictions,
        (unsigned long long)bp->stats.btb_hits
    );

    if (written < 0) {
        buffer[0] = '\0';
        return 0u;
    }

    size_t len = (size_t)written;
    return (len >= size) ? (size - 1u) : len;
}

