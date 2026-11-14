#include "assembler.h"
#include "control_unit.h"
#include "cpu.h"
#include "microop.h"
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

static int test_microop_sequence_generation(void)
{
    DecodedInstruction instr = {0};
    ControlSignals ctrl = {0};
    MicroOpSequence seq = {0};

    /* ADD R1, R2 -> FETCH_DECODE -> READ_OPERANDS -> ALU -> REG_WRITEBACK -> PC_UPDATE */
    instr.opcode = DIOCPU_OP_ADD;
    instr.reg_a = 1;
    instr.reg_b = 2;
    instr.next_pc = 2;
    CHECK(control_unit_decode(&instr, 0, &ctrl) == CONTROL_UNIT_OK);
    CHECK(microop_build_sequence(&instr, &ctrl, &seq));
    CHECK(seq.count == 5);
    CHECK(seq.ops[0].kind == UOP_FETCH_DECODE);
    CHECK(seq.ops[1].kind == UOP_READ_OPERANDS);
    CHECK(seq.ops[2].kind == UOP_ALU);
    CHECK(seq.ops[3].kind == UOP_REG_WRITEBACK);
    CHECK(seq.ops[4].kind == UOP_PC_UPDATE);

    /* LOAD R1, [0x0100] -> FETCH_DECODE -> READ_OPERANDS -> AGU -> MEM_READ -> REG_WRITEBACK -> PC_UPDATE */
    memset(&instr, 0, sizeof(instr));
    instr.opcode = DIOCPU_OP_LOAD;
    instr.absolute_address = 0x0100;
    instr.next_pc = 5;
    CHECK(control_unit_decode(&instr, 0, &ctrl) == CONTROL_UNIT_OK);
    CHECK(microop_build_sequence(&instr, &ctrl, &seq));
    CHECK(seq.count == 6);
    CHECK(seq.ops[2].kind == UOP_AGU);
    CHECK(seq.ops[3].kind == UOP_MEM_READ);
    CHECK(seq.ops[4].kind == UOP_REG_WRITEBACK);
    CHECK(seq.ops[5].kind == UOP_PC_UPDATE);

    char formatted[256];
    CHECK(microop_format_sequence(&seq, formatted, sizeof(formatted)) > 0);
    CHECK(strstr(formatted, "FETCH_DECODE -> READ_OPERANDS -> AGU -> MEM_READ") != NULL);

    return 1;
}

static int test_microop_step_by_step_execution(void)
{
    static DioCPU cpu;
    CHECK(cpu_init(&cpu) == CPU_STATUS_OK);

    const char *program =
        "    LOADI R1, 10\n"
        "    LOADI R2, 25\n"
        "    ADD R1, R2\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(assembler_assemble(program, &cpu.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu) == CPU_STATUS_OK);

    /* Complete first two LOADI instructions via cpu_step */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.gpr[1] == 10);
    CHECK(cpu.gpr[2] == 25);
    CHECK(cpu.instructions_executed == 2);
    CHECK(!cpu.uop.active);

    /* Now step ADD R1, R2 micro-op by micro-op (5 uops total) */
    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK); /* 1: FETCH_DECODE */
    CHECK(cpu.uop.active);
    CHECK(cpu.uop.last_uop == UOP_FETCH_DECODE);
    CHECK(cpu.gpr[1] == 10);
    CHECK(cpu.instructions_executed == 2);

    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK); /* 2: READ_OPERANDS */
    CHECK(cpu.uop.last_uop == UOP_READ_OPERANDS);
    CHECK(cpu.datapath.alu_in_a == 10);
    CHECK(cpu.datapath.alu_in_b == 25);
    CHECK(cpu.gpr[1] == 10);

    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK); /* 3: ALU */
    CHECK(cpu.uop.last_uop == UOP_ALU);
    CHECK(cpu.datapath.alu_out == 35);
    CHECK(cpu.gpr[1] == 10); /* Not yet written back! */

    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK); /* 4: REG_WRITEBACK */
    CHECK(cpu.uop.last_uop == UOP_REG_WRITEBACK);
    CHECK(cpu.gpr[1] == 35);
    CHECK(cpu.pc == 8); /* PC not yet advanced! */

    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK); /* 5: PC_UPDATE */
    CHECK(cpu.uop.last_uop == UOP_PC_UPDATE);
    CHECK(!cpu.uop.active);
    CHECK(cpu.pc == 10);
    CHECK(cpu.instructions_executed == 3);

    /* Next instruction HLT: step first uop, then finish with cpu_step */
    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.uop.active);
    CHECK(cpu.uop.last_uop == UOP_FETCH_DECODE);
    CHECK(cpu_step(&cpu) == CPU_STATUS_HALTED);
    CHECK(cpu.state == CPU_STATE_HALTED);
    CHECK(cpu.instructions_executed == 4);

    return 1;
}

static int test_microop_fault_rollback_on_iret_underflow(void)
{
    static DioCPU cpu;
    CHECK(cpu_init(&cpu) == CPU_STATUS_OK);

    /* Push only 2 bytes onto stack so IRET succeeds on POP_FLAGS then faults on POP_PC */
    const char *program =
        "    LOADI R1, 0x001F\n"
        "    PUSH R1\n"
        "    IRET\n";

    AssemblerResult res;
    CHECK(assembler_assemble(program, &cpu.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu) == CPU_STATUS_OK);

    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    uint32_t sp_before_iret = cpu.sp;
    uint16_t flags_before_iret = cpu.flags;

    CHECK(cpu_step(&cpu) == CPU_STATUS_FAULTED);
    CHECK(cpu.fault == CPU_FAULT_STACK_UNDERFLOW);
    CHECK(cpu.sp == sp_before_iret);
    CHECK(cpu.flags == flags_before_iret);
    CHECK(!cpu.uop.active);

    return 1;
}

static int test_microop_irq_sequence(void)
{
    static DioCPU cpu;
    CHECK(cpu_init(&cpu) == CPU_STATUS_OK);

    const char *program =
        "    JMP MAIN\n"
        "ISR:\n"
        "    INC R3\n"
        "    IRET\n"
        "MAIN:\n"
        "    STI\n"
        "    NOP\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(assembler_assemble(program, &cpu.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu) == CPU_STATUS_OK);

    DecodedInstruction inst;
    CHECK(decoder_decode(&cpu.memory, 0, &inst) == DECODER_STATUS_OK);
    uint32_t isr_pc = inst.next_pc;

    CHECK(cpu_register_interrupt_handler(&cpu, 0x15, isr_pc) == CPU_STATUS_OK);

    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* JMP MAIN */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* STI */
    CHECK(cpu_raise_irq(&cpu, 0x15) == CPU_STATUS_OK);

    /* Step IRQ entry micro-op by micro-op (5 uops: IVT_LOOKUP, PUSH_PC, PUSH_FLAGS, UPDATE_SYS_FLAGS, PC_UPDATE) */
    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.uop.sequence.is_irq_sequence);
    CHECK(cpu.uop.last_uop == UOP_IVT_LOOKUP);
    CHECK(cpu.uop.ivt_handler_pc == isr_pc);

    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.uop.last_uop == UOP_STACK_PUSH_PC);

    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.uop.last_uop == UOP_STACK_PUSH_FLAGS);

    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.uop.last_uop == UOP_UPDATE_SYS_FLAGS);
    CHECK((cpu.flags & CPU_FLAG_IF) == 0);

    CHECK(cpu_microop_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.uop.last_uop == UOP_PC_UPDATE);
    CHECK(cpu.pc == isr_pc);
    CHECK(!cpu.uop.active);

    uint64_t steps = 0;
    CHECK(cpu_run(&cpu, 20, &steps) == CPU_STATUS_HALTED);
    CHECK(cpu.gpr[3] == 1);
    CHECK(cpu.interrupt_count == 1);
    CHECK(cpu.sp == DIOCPU_STACK_INITIAL_SP);

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

    RUN_TEST(test_microop_sequence_generation);
    RUN_TEST(test_microop_step_by_step_execution);
    RUN_TEST(test_microop_fault_rollback_on_iret_underflow);
    RUN_TEST(test_microop_irq_sequence);

    printf("\nMicro-operation Engine tests: %u passed.\n", passed);
    return 0;
}

