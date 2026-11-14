#ifndef DIOCPU_BRANCH_PREDICTOR_H
#define DIOCPU_BRANCH_PREDICTOR_H

#include "control_unit.h"
#include "decoder.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DIOCPU_BP_MAX_BHT_ENTRIES 256u
#define DIOCPU_BP_MAX_BTB_ENTRIES 128u
#define DIOCPU_BP_MAX_GHR_BITS    8u

typedef enum {
    BP_MODE_NONE = 0,
    BP_MODE_STATIC_BTFNT,
    BP_MODE_1BIT,
    BP_MODE_2BIT,
    BP_MODE_GSHARE
} BranchPredictorMode;

typedef struct {
    BranchPredictorMode mode;
    uint16_t bht_entries;
    uint16_t btb_entries;
    uint8_t ghr_bits;
} BranchPredictorConfig;

typedef struct {
    bool valid;
    uint32_t tag;
    uint32_t target_pc;
    bool is_unconditional;
} BranchTargetEntry;

typedef struct {
    uint64_t lookups;
    uint64_t predictions_taken;
    uint64_t predictions_not_taken;
    uint64_t updates;
    uint64_t correct_predictions;
    uint64_t mispredictions;
    uint64_t direction_mispredictions;
    uint64_t target_mispredictions;
    uint64_t btb_hits;
    uint64_t btb_misses;
} BranchPredictorStats;

typedef struct {
    BranchPredictorConfig config;
    uint8_t bht[DIOCPU_BP_MAX_BHT_ENTRIES];
    BranchTargetEntry btb[DIOCPU_BP_MAX_BTB_ENTRIES];
    uint16_t ghr;
    BranchPredictorStats stats;
} BranchPredictor;

BranchPredictorConfig bp_default_config(void);
bool bp_config_is_valid(const BranchPredictorConfig *config);
bool bp_init(BranchPredictor *bp, const BranchPredictorConfig *config);
void bp_reset(BranchPredictor *bp);

bool bp_instruction_is_control_flow(const DecodedInstruction *ir);

void bp_predict(
    BranchPredictor *bp,
    uint32_t pc,
    const DecodedInstruction *ir,
    bool *out_taken,
    uint32_t *out_next_pc,
    uint16_t *out_ghr_snapshot
);

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
);

const char *bp_mode_name(BranchPredictorMode mode);
double bp_accuracy(const BranchPredictorStats *stats);
size_t bp_format_stats(
    const BranchPredictor *bp,
    char *buffer,
    size_t size
);

#endif

