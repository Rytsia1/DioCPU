#include "assembler.h"
#include "branch_predictor.h"
#include "cpu.h"
#include "pipeline.h"
#include "stack.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            fprintf(stderr, "FAILED: %s:%d: %s\n",             \
                    __FILE__, __LINE__, #condition);          \
            return 0;                                         \
        }                                                     \
    } while (0)

static int test_bp_config_and_reset_validation(void)
{
    BranchPredictorConfig cfg = bp_default_config();
    CHECK(cfg.mode == BP_MODE_NONE);
    CHECK(bp_config_is_valid(&cfg));

    BranchPredictorConfig bad = cfg;
    bad.bht_entries = 48u; /* not power of two */
    CHECK(!bp_config_is_valid(&bad));

    bad = cfg;
    bad.btb_entries = 256u; /* > max 128 */
    CHECK(!bp_config_is_valid(&bad));

    bad = cfg;
    bad.ghr_bits = 0u;
    CHECK(!bp_config_is_valid(&bad));

    bad = cfg;
    bad.ghr_bits = 9u;
    CHECK(!bp_config_is_valid(&bad));

    BranchPredictor bp;
    cfg.mode = BP_MODE_2BIT;
    CHECK(bp_init(&bp, &cfg));
    CHECK(bp.bht[0] == 1u); /* Weakly Not-Taken initial state */
    bp.bht[0] = 3u;
    bp.ghr = 0x0Fu;
    bp.stats.updates = 5u;
    bp_reset(&bp);
    CHECK(bp.config.mode == BP_MODE_2BIT);
    CHECK(bp.bht[0] == 1u);
    CHECK(bp.ghr == 0u);
    CHECK(bp.stats.updates == 0u);

    return 1;
}

static int test_bp_static_btfnt_reduces_loop_flushes_and_cycles(void)
{
    static DioCPU cpu_seq;
    static DioCPU cpu_none;
    static DioCPU cpu_btfnt;

    const char *program =
        "    JMP START\n"
        "STEP_ADD:\n"
        "    ADDI R2, 3\n"
        "    RET\n"
        "START:\n"
        "    LOADI R1, 8\n"
        "    LOADI R2, 0\n"
        "LOOP:\n"
        "    CALL STEP_ADD\n"
        "    DEC R1\n"
        "    JNZ LOOP\n"
        "    STORE R2, [0x0100]\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(cpu_init(&cpu_seq) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_seq.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_seq) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_none) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_none.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_none) == CPU_STATUS_OK);
    CHECK(cpu_set_branch_predictor_mode(&cpu_none, BP_MODE_NONE) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_btfnt) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_btfnt.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_btfnt) == CPU_STATUS_OK);
    CHECK(cpu_set_branch_predictor_mode(&cpu_btfnt, BP_MODE_STATIC_BTFNT) == CPU_STATUS_OK);

    uint64_t seq_steps = 0u, cyc_none = 0u, cyc_btfnt = 0u;
    CHECK(cpu_run(&cpu_seq, 5000, &seq_steps) == CPU_STATUS_HALTED);
    CHECK(cpu_pipeline_run(&cpu_none, 5000, &cyc_none) == CPU_STATUS_HALTED);
    CHECK(cpu_pipeline_run(&cpu_btfnt, 5000, &cyc_btfnt) == CPU_STATUS_HALTED);

    CHECK(cpu_btfnt.gpr[2] == 24u && cpu_seq.gpr[2] == 24u && cpu_none.gpr[2] == 24u);
    CHECK(cpu_btfnt.sp == DIOCPU_STACK_INITIAL_SP);
    CHECK(cpu_btfnt.instructions_executed == cpu_seq.instructions_executed);
    CHECK(cpu_btfnt.pc == cpu_seq.pc);

    /* BTFNT eliminates flushes on JMP, all 8 CALLs, and 7 backward JNZs; only exit JNZ mispredicts */
    CHECK(cpu_btfnt.pipeline.stats.control_hazard_flushes < cpu_none.pipeline.stats.control_hazard_flushes);
    CHECK(cyc_btfnt < cyc_none);
    CHECK(cpu_btfnt.pipeline.bp.stats.mispredictions == 1u);
    CHECK(bp_accuracy(&cpu_btfnt.pipeline.bp.stats) > 0.90);

    return 1;
}

static int test_bp_1bit_vs_2bit_nested_loop_advantage(void)
{
    static DioCPU cpu_1bit;
    static DioCPU cpu_2bit;

    /* Nested loop: inner branch runs 4 times (3 Taken, 1 Exit) across 5 outer passes */
    const char *program =
        "    LOADI R1, 5\n"     /* outer count */
        "    LOADI R3, 0\n"     /* accumulator */
        "OUTER:\n"
        "    LOADI R2, 4\n"     /* inner count */
        "INNER:\n"
        "    INC R3\n"
        "    DEC R2\n"
        "    JNZ INNER\n"       /* Taken 3x, NT 1x per outer pass */
        "    DEC R1\n"
        "    JNZ OUTER\n"       /* Taken 4x, NT 1x */
        "    HLT\n";

    AssemblerResult res;
    CHECK(cpu_init(&cpu_1bit) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_1bit.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_1bit) == CPU_STATUS_OK);
    CHECK(cpu_set_branch_predictor_mode(&cpu_1bit, BP_MODE_1BIT) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_2bit) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_2bit.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_2bit) == CPU_STATUS_OK);
    CHECK(cpu_set_branch_predictor_mode(&cpu_2bit, BP_MODE_2BIT) == CPU_STATUS_OK);

    uint64_t cyc_1bit = 0u, cyc_2bit = 0u;
    CHECK(cpu_pipeline_run(&cpu_1bit, 10000, &cyc_1bit) == CPU_STATUS_HALTED);
    CHECK(cpu_pipeline_run(&cpu_2bit, 10000, &cyc_2bit) == CPU_STATUS_HALTED);

    CHECK(cpu_1bit.gpr[3] == 20u && cpu_2bit.gpr[3] == 20u);
    CHECK(cpu_1bit.instructions_executed == cpu_2bit.instructions_executed);

    /* Under 2-bit hysteresis, loop exits keep Strongly->Weakly Taken instead of flipping on re-entry */
    CHECK(cpu_2bit.pipeline.bp.stats.mispredictions < cpu_1bit.pipeline.bp.stats.mispredictions);
    CHECK(bp_accuracy(&cpu_2bit.pipeline.bp.stats) > bp_accuracy(&cpu_1bit.pipeline.bp.stats));
    CHECK(cyc_2bit < cyc_1bit);

    return 1;
}

static int test_bp_gshare_alternating_branch_correlation(void)
{
    static DioCPU cpu_2bit;
    static DioCPU cpu_gshare;

    /* R2 toggles 0 -> 1 -> 0 -> 1 ... every iteration for 16 steps; JNZ SKIP alternates T, NT, T, NT */
    const char *program =
        "    LOADI R1, 16\n"
        "    LOADI R2, 0\n"
        "    LOADI R3, 0\n"
        "    LOADI R4, 1\n"
        "LOOP:\n"
        "    XOR R2, R4\n"
        "    CMPI R2, 0\n"
        "    JNZ ODD_STEP\n"
        "    ADDI R3, 2\n"
        "ODD_STEP:\n"
        "    DEC R1\n"
        "    JNZ LOOP\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(cpu_init(&cpu_2bit) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_2bit.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_2bit) == CPU_STATUS_OK);
    CHECK(cpu_set_branch_predictor_mode(&cpu_2bit, BP_MODE_2BIT) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_gshare) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_gshare.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_gshare) == CPU_STATUS_OK);
    CHECK(cpu_set_branch_predictor_mode(&cpu_gshare, BP_MODE_GSHARE) == CPU_STATUS_OK);

    uint64_t cyc_2bit = 0u, cyc_gshare = 0u;
    CHECK(cpu_pipeline_run(&cpu_2bit, 10000, &cyc_2bit) == CPU_STATUS_HALTED);
    CHECK(cpu_pipeline_run(&cpu_gshare, 10000, &cyc_gshare) == CPU_STATUS_HALTED);

    /* 8 even steps add 2 each => R3 = 16 */
    CHECK(cpu_2bit.gpr[3] == 16u && cpu_gshare.gpr[3] == 16u);
    CHECK(cpu_2bit.instructions_executed == cpu_gshare.instructions_executed);

    /* GSHARE uses global history correlation to outperform local 2-bit on alternating branch */
    CHECK(cpu_gshare.pipeline.bp.stats.mispredictions < cpu_2bit.pipeline.bp.stats.mispredictions);
    CHECK(bp_accuracy(&cpu_gshare.pipeline.bp.stats) > bp_accuracy(&cpu_2bit.pipeline.bp.stats));
    CHECK(cyc_gshare < cyc_2bit);

    return 1;
}

static int test_bp_indirect_btb_and_cache_speculative_safety(void)
{
    static DioCPU cpu_seq;
    static DioCPU cpu_combo;

    /* Mix indirect CALLR via R6, loop JNZ, and speculative wrong-path HLT guard after TAKEN branch */
    const char *program =
        "    JMP MAIN\n"
        "WORKER:\n"
        "    PUSH R2\n"
        "    LOADI R2, 5\n"
        "    ADD R1, R2\n"
        "    STORER R1, [R5 + 0]\n"
        "    ADDI R5, 2\n"
        "    POP R2\n"
        "    RET\n"
        "MAIN:\n"
        "    LOADI R1, 0\n"
        "    LOADI R3, 6\n"
        "    LOADI R5, 0x0120\n"
        "    LOADI R6, 4\n" /* WORKER immediately follows 4-byte JMP MAIN */
        "LOOP:\n"
        "    CALLR R6\n"
        "    DEC R3\n"
        "    JNZ LOOP\n"
        "    LOADI R4, 1\n"
        "    CMPI R4, 1\n"
        "    JZ AFTER_TRAP\n"
        "    HLT\n"          /* Speculative wrong-path trap if JZ not flushed or predicted wrong */
        "AFTER_TRAP:\n"
        "    ADDI R1, 10\n"  /* Must execute after AFTER_TRAP: R1 = 6*5 + 10 = 40 */
        "    STORE R1, [0x0140]\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(cpu_init(&cpu_seq) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_seq.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_seq) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_combo) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_combo.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_combo) == CPU_STATUS_OK);
    CHECK(cpu_set_cache_enabled(&cpu_combo, true) == CPU_STATUS_OK);
    CHECK(cpu_set_branch_predictor_mode(&cpu_combo, BP_MODE_2BIT) == CPU_STATUS_OK);

    uint64_t seq_steps = 0u, combo_cycles = 0u;
    CHECK(cpu_run(&cpu_seq, 10000, &seq_steps) == CPU_STATUS_HALTED);
    CHECK(cpu_pipeline_run(&cpu_combo, 10000, &combo_cycles) == CPU_STATUS_HALTED);
    CHECK(cpu_flush_caches(&cpu_combo, NULL) == CPU_STATUS_OK);

    CHECK(cpu_seq.gpr[1] == 40u && cpu_combo.gpr[1] == 40u);
    CHECK(cpu_combo.pc == cpu_seq.pc);
    CHECK(cpu_combo.sp == DIOCPU_STACK_INITIAL_SP);
    CHECK(cpu_combo.flags == cpu_seq.flags);
    CHECK(cpu_combo.instructions_executed == cpu_seq.instructions_executed);

    uint16_t final_mem = 0u;
    CHECK(memory_data_read16(&cpu_combo.memory, 0x0140u, &final_mem) == MEMORY_STATUS_OK);
    CHECK(final_mem == 40u);

    /* CALLR BTB learned WORKER on call 1 and hit BTB on subsequent calls */
    CHECK(cpu_combo.pipeline.bp.stats.btb_hits > 0u);
    CHECK(cpu_combo.cache.icache.stats.hits > 0u);
    CHECK(cpu_combo.cache.dcache.stats.hits > 0u);

    char bp_buf[192];
    CHECK(bp_format_stats(&cpu_combo.pipeline.bp, bp_buf, sizeof(bp_buf)) > 0u);
    CHECK(strstr(bp_buf, "BP[2BIT]:") != NULL);

    return 1;
}

int main(void)
{
    int (*tests[])(void) = {
        test_bp_config_and_reset_validation,
        test_bp_static_btfnt_reduces_loop_flushes_and_cycles,
        test_bp_1bit_vs_2bit_nested_loop_advantage,
        test_bp_gshare_alternating_branch_correlation,
        test_bp_indirect_btb_and_cache_speculative_safety
    };

    size_t count = sizeof(tests) / sizeof(tests[0]);
    for (size_t i = 0; i < count; i++) {
        if (!tests[i]()) {
            return 1;
        }
    }

    printf("All %u branch predictor tests passed!\n", (unsigned int)count);
    return 0;
}

