#include "assembler.h"
#include "cpu.h"
#include "cycle_engine.h"
#include "decoder.h"
#include "microop.h"

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

static int test_cycle_by_cycle_multi_cycle_uop_and_stalls(void)
{
    static DioCPU cpu;
    CHECK(cpu_init(&cpu) == CPU_STATUS_OK);

    const char *program =
        "LOADI R1, 6\n"
        "LOADI R2, 7\n"
        "MUL R1, R2\n"
        "STORE R1, [0x00100]\n"
        "LOAD R3, [0x00100]\n"
        "DIV R3, R2\n"
        "HLT\n";

    AssemblerResult res;
    CHECK(assembler_assemble(program, &cpu.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu) == CPU_STATUS_OK);

    /* Two LOADI instructions: 3 cycles each (FETCH_DECODE + REG_WRITEBACK + PC_UPDATE) */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.cycle.last_instruction_cycles == 3u);
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.cycle.last_instruction_cycles == 3u);
    CHECK(cpu.cycle.total_cycles == 6u);
    CHECK(cpu.cycle.busy_stall_cycles == 0u);

    /* MUL R1, R2: 6 cycles (1+1+2+1+1), 1 stall cycle during ALU */
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* cyc 7: FETCH_DECODE */
    CHECK(cpu.cycle.total_cycles == 7u);
    CHECK(cpu.cycle.current_stage == CYCLE_STAGE_FETCH);
    CHECK(!cpu.cycle.uop_in_flight);
    CHECK(!cpu.cycle.current_cycle_stalled);
    CHECK(microop_sequence_total_cycles(&cpu.uop.sequence) == 6u);

    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* cyc 8: READ_OPERANDS */
    CHECK(cpu.cycle.current_stage == CYCLE_STAGE_OPERAND);
    CHECK(!cpu.cycle.uop_in_flight);

    uint64_t uops_before_alu = cpu.uop.microops_executed;
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* cyc 9: ALU 1/2 (stall) */
    CHECK(cpu.cycle.current_stage == CYCLE_STAGE_EXECUTE);
    CHECK(cpu.cycle.active_uop.kind == UOP_ALU);
    CHECK(cpu.cycle.uop_in_flight);
    CHECK(cpu.cycle.current_cycle_stalled);
    CHECK(cpu.cycle.uop_cycles_elapsed == 1u);
    CHECK(cpu.cycle.uop_cycles_remaining == 1u);
    CHECK(cpu.cycle.busy_stall_cycles == 1u);
    CHECK(cpu.uop.microops_executed == uops_before_alu);
    CHECK(cpu.gpr[1] == 6u);

    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* cyc 10: ALU 2/2 (retire) */
    CHECK(!cpu.cycle.uop_in_flight);
    CHECK(!cpu.cycle.current_cycle_stalled);
    CHECK(cpu.cycle.uop_cycles_elapsed == 2u);
    CHECK(cpu.uop.microops_executed == uops_before_alu + 1u);

    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* cyc 11: REG_WRITEBACK */
    CHECK(cpu.cycle.current_stage == CYCLE_STAGE_WRITEBACK);
    CHECK(cpu.gpr[1] == 42u);

    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* cyc 12: PC_UPDATE */
    CHECK(cpu.cycle.total_cycles == 12u);
    CHECK(cpu.cycle.last_instruction_cycles == 6u);
    CHECK(cpu.instructions_executed == 3u);

    /* STORE (6 cyc, 1 stall) & LOAD (7 cyc, 1 stall) */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.cycle.last_instruction_cycles == 6u);
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.cycle.last_instruction_cycles == 7u);
    CHECK(cpu.gpr[3] == 42u);

    /* DIV R3, R2 (8 cyc, 3 stall cycles inside 4-cycle ALU) */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.cycle.last_instruction_cycles == 8u);
    CHECK(cpu.gpr[3] == 6u);
    CHECK(cpu.gpr[2] == 0u);
    CHECK(cpu.cycle.busy_stall_cycles == 6u); /* MUL(1)+STORE(1)+LOAD(1)+DIV(3) */

    /* HLT (2 cyc) */
    CHECK(cpu_step(&cpu) == CPU_STATUS_HALTED);
    CHECK(cpu.cycle.last_instruction_cycles == 2u);
    CHECK(cpu.cycle.total_cycles == 35u);
    CHECK(cpu.instructions_executed == 7u);

    /* Halted CPU must not advance clock cycles */
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_HALTED);
    CHECK(cpu.cycle.total_cycles == 35u);

    char status_buf[192];
    size_t len = cycle_engine_format_status(
        &cpu.cycle,
        cpu.instructions_executed,
        status_buf,
        sizeof(status_buf)
    );
    CHECK(len > 0u);
    CHECK(strstr(status_buf, "cycles=35") != NULL);
    CHECK(strstr(status_buf, "stall=6") != NULL);

    return 1;
}

static int test_cycle_execution_mode_equivalence(void)
{
    static DioCPU cpu_instr;
    static DioCPU cpu_uop;
    static DioCPU cpu_cyc;

    const char *program =
        "    LOADI R1, 5\n"
        "    LOADI R2, 0\n"
        "    LOADI R3, 3\n"
        "LOOP:\n"
        "    ADD R2, R1\n"
        "    DEC R1\n"
        "    JNZ LOOP\n"
        "    MUL R2, R3\n"
        "    STORE R2, [0x00200]\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(cpu_init(&cpu_instr) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_instr.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_instr) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_uop) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_uop.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_uop) == CPU_STATUS_OK);

    CHECK(cpu_init(&cpu_cyc) == CPU_STATUS_OK);
    CHECK(assembler_assemble(program, &cpu_cyc.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu_cyc) == CPU_STATUS_OK);

    uint64_t steps = 0;
    CHECK(cpu_run(&cpu_instr, 1000, &steps) == CPU_STATUS_HALTED);

    CPUStatus st = CPU_STATUS_OK;
    for (int i = 0; i < 1000 && st == CPU_STATUS_OK; i++) {
        st = cpu_microop_step(&cpu_uop);
    }
    CHECK(st == CPU_STATUS_HALTED);

    uint64_t cyc_steps = 0;
    CHECK(cpu_run_cycles(&cpu_cyc, 5000, &cyc_steps) == CPU_STATUS_HALTED);

    CHECK(cpu_instr.pc == cpu_uop.pc && cpu_uop.pc == cpu_cyc.pc);
    CHECK(cpu_instr.gpr[2] == 45u);
    CHECK(cpu_uop.gpr[2] == 45u);
    CHECK(cpu_cyc.gpr[2] == 45u);
    CHECK(cpu_instr.instructions_executed == cpu_uop.instructions_executed);
    CHECK(cpu_uop.instructions_executed == cpu_cyc.instructions_executed);
    CHECK(cpu_instr.uop.microops_executed == cpu_uop.uop.microops_executed);
    CHECK(cpu_uop.uop.microops_executed == cpu_cyc.uop.microops_executed);
    CHECK(cpu_instr.cycle.total_cycles == cpu_uop.cycle.total_cycles);
    CHECK(cpu_uop.cycle.total_cycles == cpu_cyc.cycle.total_cycles);
    CHECK(cyc_steps == cpu_cyc.cycle.total_cycles);
    CHECK(cpu_instr.cycle.busy_stall_cycles == cpu_cyc.cycle.busy_stall_cycles);

    double cpi = cycle_engine_cpi(&cpu_cyc.cycle, cpu_cyc.instructions_executed);
    double ipc = cycle_engine_ipc(&cpu_cyc.cycle, cpu_cyc.instructions_executed);
    double diff = (cpi * ipc) - 1.0;
    CHECK(cpi > 1.0);
    CHECK(diff > -1e-9 && diff < 1e-9);

    return 1;
}

static int test_cycle_breakpoint_and_run_cycles(void)
{
    static DioCPU cpu;
    CHECK(cpu_init(&cpu) == CPU_STATUS_OK);

    const char *program =
        "LOADI R1, 4\n"   /* cycles 1..3 */
        "LOADI R2, 9\n"   /* cycles 4..6 */
        "MUL R1, R2\n"    /* cycles 7..12 (cycle 9 is ALU 1/2 stall!) */
        "HLT\n";          /* cycles 13..14 */

    AssemblerResult res;
    CHECK(assembler_assemble(program, &cpu.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu) == CPU_STATUS_OK);

    CHECK(cpu_set_cycle_breakpoint(&cpu, 9u) == CPU_STATUS_OK);

    uint64_t ran = 0;
    CHECK(cpu_run_cycles(&cpu, 100u, &ran) == CPU_STATUS_BREAKPOINT_REACHED);
    CHECK(ran == 9u);
    CHECK(cpu.cycle.total_cycles == 9u);
    CHECK(cpu.cycle.uop_in_flight);
    CHECK(cpu.cycle.active_uop.kind == UOP_ALU);

    /* Re-running without clearing stays at breakpoint */
    CHECK(cpu_run_cycles(&cpu, 100u, &ran) == CPU_STATUS_BREAKPOINT_REACHED);
    CHECK(ran == 0u);

    /* Single cycle step crosses cycle 9 -> 10 and retires ALU */
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.cycle.total_cycles == 10u);
    CHECK(!cpu.cycle.uop_in_flight);

    /* Clear breakpoint and run remaining cycles to HALT */
    CHECK(cpu_clear_cycle_breakpoint(&cpu) == CPU_STATUS_OK);
    CHECK(cpu_run_cycles(&cpu, 100u, &ran) == CPU_STATUS_HALTED);
    CHECK(cpu.cycle.total_cycles == 14u);
    CHECK(ran == 4u);
    CHECK(cpu.gpr[1] == 36u);

    return 1;
}

static int test_cycle_trace_irq_and_fault_cleanup(void)
{
    static DioCPU cpu;
    CHECK(cpu_init(&cpu) == CPU_STATUS_OK);

    const char *program =
        "    JMP MAIN\n"
        "ISR:\n"
        "    LOADI R4, 99\n"
        "    IRET\n"
        "MAIN:\n"
        "    STI\n"
        "    NOP\n"
        "    LOADI R1, 10\n"
        "    LOADI R2, 0\n"
        "    DIV R1, R2\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(assembler_assemble(program, &cpu.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu) == CPU_STATUS_OK);

    DecodedInstruction first_inst;
    CHECK(decoder_decode(&cpu.memory, 0, &first_inst) == DECODER_STATUS_OK);
    CHECK(cpu_register_interrupt_handler(&cpu, 0x12, first_inst.next_pc) == CPU_STATUS_OK);

    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* JMP MAIN */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* STI */
    CHECK(cpu_raise_irq(&cpu, 0x12) == CPU_STATUS_OK);

    uint64_t cycles_before_irq = cpu.cycle.total_cycles;
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* IRQ entry sequence (6 cycles) */
    CHECK(cpu.cycle.interrupt_cycles == 6u);
    CHECK(cpu.cycle.total_cycles == cycles_before_irq + 6u);

    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* LOADI R4, 99 */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* IRET */
    CHECK(cpu.gpr[4] == 99u);

    CycleTraceEntry entries[DIOCPU_CYCLE_TRACE_CAP];
    size_t count = cycle_engine_get_recent_trace(&cpu.cycle, entries, DIOCPU_CYCLE_TRACE_CAP);
    CHECK(count > 0u && count <= DIOCPU_CYCLE_TRACE_CAP);
    for (size_t i = 1; i < count; i++) {
        CHECK(entries[i].cycle_number == entries[i - 1u].cycle_number + 1u);
    }
    CHECK(entries[count - 1u].retired_instruction);
    CHECK(entries[count - 1u].uop_kind == UOP_PC_UPDATE);

    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* NOP */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* LOADI R1, 10 */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* LOADI R2, 0 */

    /* Step DIV R1, R2 cycle-by-cycle up to fault on ALU cycle 4/4 */
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* FETCH_DECODE */
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* READ_OPERANDS */
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* ALU 1/4 */
    CHECK(cpu.cycle.uop_in_flight);
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* ALU 2/4 */
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_OK); /* ALU 3/4 */
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_FAULTED); /* ALU 4/4 -> DIV BY ZERO */

    CHECK(cpu.state == CPU_STATE_FAULTED);
    CHECK(cpu.fault == CPU_FAULT_DIVIDE_BY_ZERO);
    CHECK(!cpu.cycle.uop_in_flight);
    CHECK(cpu.cycle.current_stage == CYCLE_STAGE_FAULT);

    uint64_t faulted_cycles = cpu.cycle.total_cycles;
    CHECK(cpu_cycle_step(&cpu) == CPU_STATUS_FAULTED);
    CHECK(cpu.cycle.total_cycles == faulted_cycles);

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

    RUN_TEST(test_cycle_by_cycle_multi_cycle_uop_and_stalls);
    RUN_TEST(test_cycle_execution_mode_equivalence);
    RUN_TEST(test_cycle_breakpoint_and_run_cycles);
    RUN_TEST(test_cycle_trace_irq_and_fault_cleanup);

    printf("\nCycle-Accurate Engine tests: %u passed.\n", passed);
    return 0;
}
