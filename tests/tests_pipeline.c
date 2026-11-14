#include "assembler.h"
#include "cpu.h"
#include "decoder.h"
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

static int test_pipeline_raw_forwarding_vs_stall(void)
{
    static DioCPU cpu_seq;
    static DioCPU cpu_fwd;
    static DioCPU cpu_nofwd;

    const char *program =
        "LOADI R1, 10\n"
        "LOADI R2, 20\n"
        "ADD R1, R2\n"     /* R1 = 30 (EX/MEM forward R1, R2) */
        "ADDI R1, 5\n"     /* R1 = 35 (EX/MEM forward R1) */
        "SUB R1, R2\n"     /* R1 = 15 (EX/MEM forward R1) */
        "INC R1\n"         /* R1 = 16 (EX/MEM forward R1) */
        "MUL R1, R2\n"     /* R1 = 320 (EX/MEM forward R1, 2-cyc EX) */
        "STORE R1, [0x00100]\n"
        "HLT\n";

    AssemblerResult res;
    CHECK(cpu_init(&cpu_seq) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_seq.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_seq) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_fwd) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_fwd.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_fwd) == CPU_STATUS_OK);
    CHECK(cpu_set_pipeline_forwarding(&cpu_fwd, true) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_nofwd) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_nofwd.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_nofwd) == CPU_STATUS_OK);
    CHECK(cpu_set_pipeline_forwarding(&cpu_nofwd, false) == CPU_STATUS_OK);

    uint64_t seq_steps = 0;
    uint64_t fwd_cycles = 0;
    uint64_t nofwd_cycles = 0;

    CHECK(cpu_run(&cpu_seq, 1000, &seq_steps) == CPU_STATUS_HALTED);
    CHECK(cpu_pipeline_run(&cpu_fwd, 1000, &fwd_cycles) == CPU_STATUS_HALTED);
    CHECK(cpu_pipeline_run(&cpu_nofwd, 1000, &nofwd_cycles) == CPU_STATUS_HALTED);

    CHECK(cpu_fwd.gpr[1] == 320u && cpu_nofwd.gpr[1] == 320u && cpu_seq.gpr[1] == 320u);
    CHECK(cpu_fwd.pc == cpu_seq.pc && cpu_nofwd.pc == cpu_seq.pc);
    CHECK(cpu_fwd.instructions_executed == cpu_seq.instructions_executed);
    CHECK(cpu_nofwd.instructions_executed == cpu_seq.instructions_executed);

    uint16_t mem_seq = 0, mem_fwd = 0, mem_nofwd = 0;
    CHECK(memory_data_read16(&cpu_seq.memory, 0x00100u, &mem_seq) == MEMORY_STATUS_OK);
    CHECK(memory_data_read16(&cpu_fwd.memory, 0x00100u, &mem_fwd) == MEMORY_STATUS_OK);
    CHECK(memory_data_read16(&cpu_nofwd.memory, 0x00100u, &mem_nofwd) == MEMORY_STATUS_OK);
    CHECK(mem_seq == 320u && mem_fwd == 320u && mem_nofwd == 320u);

    CHECK(cpu_fwd.pipeline.stats.forwards > 0u);
    CHECK(cpu_fwd.pipeline.stats.data_hazard_stalls == 0u);
    CHECK(cpu_nofwd.pipeline.stats.forwards == 0u);
    CHECK(cpu_nofwd.pipeline.stats.data_hazard_stalls > 0u);
    CHECK(fwd_cycles < nofwd_cycles);
    CHECK(fwd_cycles < cpu_seq.cycle.total_cycles);

    char sum_buf[192];
    size_t len = pipeline_format_summary(&cpu_fwd.pipeline, sum_buf, sizeof(sum_buf));
    CHECK(len > 0u);
    CHECK(strstr(sum_buf, "retired=9") != NULL);

    return 1;
}

static int test_pipeline_load_use_hazard_and_memory_ordering(void)
{
    static DioCPU cpu;
    CHECK(cpu_init(&cpu) == CPU_STATUS_OK);

    const char *program =
        "LOADI R1, 40\n"
        "STORE R1, [0x00100]\n"
        "LOAD R2, [0x00100]\n"
        "ADDI R2, 2\n"           /* Immediate Load-Use hazard on R2 */
        "STORE R2, [0x00102]\n"  /* EX/MEM forward on R2 for STORE value */
        "HLT\n";

    AssemblerResult res;
    CHECK(assembler_assemble(program, &cpu.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu) == CPU_STATUS_OK);

    uint64_t cycles = 0;
    CHECK(cpu_pipeline_run(&cpu, 1000, &cycles) == CPU_STATUS_HALTED);
    CHECK(cpu.gpr[2] == 42u);

    uint16_t stored = 0;
    CHECK(memory_data_read16(&cpu.memory, 0x00102u, &stored) == MEMORY_STATUS_OK);
    CHECK(stored == 42u);
    CHECK(cpu.pipeline.stats.load_use_stalls >= 1u);
    CHECK(cpu.pipeline.stats.data_hazard_stalls >= 1u);
    CHECK(cpu.pipeline.stats.structural_mem_stalls > 0u);
    CHECK(cpu.pipeline.stats.forwards >= 1u);

    /* Ensure stage snapshot resets / halted behavior stays clean */
    CHECK(cpu_pipeline_step_cycle(&cpu) == CPU_STATUS_HALTED);
    CHECK(cpu.pipeline.stats.cycles == cycles);

    return 1;
}

static int test_pipeline_branches_calls_and_loop_parity(void)
{
    static DioCPU cpu_seq;
    static DioCPU cpu_pipe;

    const char *program =
        "    JMP START\n"
        "DOUBLE:\n"
        "    PUSH R2\n"
        "    MOV R2, R1\n"
        "    ADD R1, R2\n"
        "    POP R2\n"
        "    RET\n"
        "START:\n"
        "    LOADI R1, 1\n"
        "    LOADI R3, 5\n"
        "    LOADI R4, 0\n"
        "    LOADI R5, 0x0100\n"
        "LOOP:\n"
        "    CALL DOUBLE\n"
        "    STORER R1, [R5 + 0]\n"
        "    ADDI R5, 2\n"
        "    ADD R4, R1\n"
        "    DEC R3\n"
        "    JNZ LOOP\n"
        "    LOADI R6, 0x0100\n"
        "    LEA R7, [R6 + 4]\n"
        "    LOADR R8, [R7 + 0]\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(cpu_init(&cpu_seq) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_seq.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_seq) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_pipe) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_pipe.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_pipe) == CPU_STATUS_OK);

    uint64_t seq_steps = 0;
    uint64_t pipe_cycles = 0;
    CHECK(cpu_run(&cpu_seq, 5000, &seq_steps) == CPU_STATUS_HALTED);
    CHECK(cpu_pipeline_run(&cpu_pipe, 10000, &pipe_cycles) == CPU_STATUS_HALTED);

    for (unsigned int i = 0; i < CPU_GPR_COUNT; i++) {
        CHECK(cpu_pipe.gpr[i] == cpu_seq.gpr[i]);
    }
    CHECK(cpu_pipe.pc == cpu_seq.pc);
    CHECK(cpu_pipe.sp == DIOCPU_STACK_INITIAL_SP);
    CHECK(cpu_pipe.sp == cpu_seq.sp);
    CHECK(cpu_pipe.flags == cpu_seq.flags);
    CHECK(cpu_pipe.instructions_executed == cpu_seq.instructions_executed);
    CHECK(cpu_pipe.gpr[1] == 32u); /* 1 -> 2 -> 4 -> 8 -> 16 -> 32 */
    CHECK(cpu_pipe.gpr[4] == 62u); /* 2+4+8+16+32 */
    CHECK(cpu_pipe.gpr[8] == 8u);  /* 3rd element at 0x0104 */

    CHECK(cpu_pipe.pipeline.stats.control_hazard_flushes > 0u);
    CHECK(pipeline_cpi(&cpu_pipe.pipeline) <
          cycle_engine_cpi(&cpu_seq.cycle, cpu_seq.instructions_executed));
    CHECK(pipeline_ipc(&cpu_pipe.pipeline) >
          cycle_engine_ipc(&cpu_seq.cycle, cpu_seq.instructions_executed));

    return 1;
}

static int test_pipeline_precise_fault_and_interrupts(void)
{
    /* Part 1: Precise exception — younger instructions behind DIV-by-0 must not commit */
    static DioCPU cpu_fault;
    CHECK(cpu_init(&cpu_fault) == CPU_STATUS_OK);

    const char *fault_prog =
        "LOADI R1, 10\n"
        "LOADI R2, 0\n"
        "DIV R1, R2\n"
        "LOADI R3, 99\n"
        "STORE R3, [0x00300]\n"
        "HLT\n";

    AssemblerResult res;
    CHECK(assembler_assemble(fault_prog, &cpu_fault.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_fault) == CPU_STATUS_OK);

    uint64_t cyc = 0;
    CHECK(cpu_pipeline_run(&cpu_fault, 1000, &cyc) == CPU_STATUS_FAULTED);
    CHECK(cpu_fault.state == CPU_STATE_FAULTED);
    CHECK(cpu_fault.fault == CPU_FAULT_DIVIDE_BY_ZERO);
    CHECK(cpu_fault.gpr[3] == 0u);

    uint16_t mem_val = 0xFFFFu;
    CHECK(memory_data_read16(&cpu_fault.memory, 0x00300u, &mem_val) == MEMORY_STATUS_OK);
    CHECK(mem_val == 0u);

    /* Part 2: Software INT, IRET, and Hardware IRQ in pipeline */
    static DioCPU cpu_seq;
    static DioCPU cpu_pipe;
    const char *int_prog =
        "    JMP MAIN\n"
        "ISR:\n"
        "    ADDI R4, 10\n"
        "    IRET\n"
        "MAIN:\n"
        "    LOADI R4, 5\n"
        "    STI\n"
        "    INT 0x20\n"
        "    NOP\n"
        "    HLT\n";

    CHECK(cpu_init(&cpu_seq) == CPU_STATUS_OK);
    CHECK(assembler_assemble(int_prog, &cpu_seq.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_seq) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_pipe) == CPU_STATUS_OK);
    CHECK(assembler_assemble(int_prog, &cpu_pipe.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_pipe) == CPU_STATUS_OK);

    DecodedInstruction first_inst;
    CHECK(decoder_decode(&cpu_seq.memory, 0, &first_inst) == DECODER_STATUS_OK);
    CHECK(cpu_register_interrupt_handler(&cpu_seq, 0x20, first_inst.next_pc) == CPU_STATUS_OK);
    CHECK(cpu_register_interrupt_handler(&cpu_pipe, 0x20, first_inst.next_pc) == CPU_STATUS_OK);

    /* Inject hardware IRQ 0x20 partway through pipelined run too */
    CHECK(cpu_step(&cpu_seq) == CPU_STATUS_OK); /* JMP MAIN */
    CHECK(cpu_step(&cpu_seq) == CPU_STATUS_OK); /* LOADI R4, 5 */
    CHECK(cpu_step(&cpu_seq) == CPU_STATUS_OK); /* STI */
    CHECK(cpu_raise_irq(&cpu_seq, 0x20) == CPU_STATUS_OK);
    uint64_t seq_steps = 0;
    CHECK(cpu_run(&cpu_seq, 100, &seq_steps) == CPU_STATUS_HALTED);

    uint64_t pipe_cyc = 0;
    CHECK(cpu_pipeline_run(&cpu_pipe, 1000, &pipe_cyc) == CPU_STATUS_HALTED);
    CHECK(cpu_pipe.gpr[4] == 15u);
    CHECK(cpu_pipe.interrupt_count == 1u);
    CHECK(cpu_pipe.sp == DIOCPU_STACK_INITIAL_SP);
    CHECK(cpu_seq.gpr[4] == 25u); /* STI + IRQ + INT 0x20 */
    CHECK(cpu_seq.interrupt_count == 2u);

    /* Test hardware IRQ directly in pipeline */
    CHECK(cpu_reset(&cpu_pipe) == CPU_STATUS_OK);
    CHECK(cpu_register_interrupt_handler(&cpu_pipe, 0x20, first_inst.next_pc) == CPU_STATUS_OK);
    for (int i = 0; i < 6; i++) {
        CHECK(cpu_pipeline_step_cycle(&cpu_pipe) == CPU_STATUS_OK);
    }
    CHECK(cpu_raise_irq(&cpu_pipe, 0x20) == CPU_STATUS_OK);
    CHECK(cpu_pipeline_run(&cpu_pipe, 1000, &pipe_cyc) == CPU_STATUS_HALTED);
    CHECK(cpu_pipe.gpr[4] == 25u);
    CHECK(cpu_pipe.interrupt_count == 2u);
    CHECK(cpu_pipe.sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}

#define RUN_TEST(fn)                             \
    do {                                         \
        if (!fn()) {                             \
            return 1;                            \
        }                                        \
        printf("[PASS] %s\n", #fn);              \
        passed++;                                \
    } while (0)

int main(void)
{
    unsigned int passed = 0;

    RUN_TEST(test_pipeline_raw_forwarding_vs_stall);
    RUN_TEST(test_pipeline_load_use_hazard_and_memory_ordering);
    RUN_TEST(test_pipeline_branches_calls_and_loop_parity);
    RUN_TEST(test_pipeline_precise_fault_and_interrupts);

    printf("\nPipeline tests: %u passed.\n", passed);
    return 0;
}

