#include "control_unit.h"
#include "cpu.h"
#include "datapath.h"
#include "assembler.h"
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

static int test_control_unit_signals(void)
{
    DecodedInstruction instr = {0};
    ControlSignals ctrl = {0};

    /* LOADI R1, 0x1234 */
    instr.opcode = DIOCPU_OP_LOADI;
    instr.format = INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16;
    instr.reg_a = 1;
    instr.immediate16 = 0x1234;
    instr.next_pc = 4;

    CHECK(control_unit_decode(&instr, 0, &ctrl) == CONTROL_UNIT_OK);
    CHECK(ctrl.reg_write_a);
    CHECK(!ctrl.reg_write_b);
    CHECK(ctrl.wb_src == WB_SRC_IMM16);
    CHECK(ctrl.pc_src == PC_SRC_NEXT_PC);

    /* LOADR R3, [R2 + 8] */
    memset(&instr, 0, sizeof(instr));
    instr.opcode = DIOCPU_OP_LOADR;
    instr.format = INSTRUCTION_FORMAT_REGISTER_INDIRECT_OFFSET;
    instr.reg_a = 3;
    instr.reg_b = 2;
    instr.offset16 = 8;
    instr.next_pc = 4;

    CHECK(control_unit_decode(&instr, 0, &ctrl) == CONTROL_UNIT_OK);
    CHECK(ctrl.agu_enable);
    CHECK(ctrl.addr_mode == ADDR_MODE_REG_OFFSET);
    CHECK(ctrl.alu_src_a == ALU_SRC_A_REG_B);
    CHECK(ctrl.alu_src_b == ALU_SRC_B_OFFSET16);
    CHECK(ctrl.mem_read);
    CHECK(ctrl.reg_write_a);
    CHECK(ctrl.wb_src == WB_SRC_MEM_DATA);

    /* DIV R4, R5 */
    memset(&instr, 0, sizeof(instr));
    instr.opcode = DIOCPU_OP_DIV;
    instr.reg_a = 4;
    instr.reg_b = 5;
    instr.next_pc = 2;

    CHECK(control_unit_decode(&instr, 0, &ctrl) == CONTROL_UNIT_OK);
    CHECK(ctrl.alu_enable);
    CHECK(ctrl.alu_op == ALU_DIV);
    CHECK(ctrl.reg_write_a);
    CHECK(ctrl.reg_write_b);
    CHECK(ctrl.flags_write_alu);

    /* JZ taken vs not taken */
    memset(&instr, 0, sizeof(instr));
    instr.opcode = DIOCPU_OP_JZ;
    instr.relative_offset = -4;
    instr.next_pc = 10;

    CHECK(control_unit_decode(&instr, CPU_FLAG_ZF, &ctrl) == CONTROL_UNIT_OK);
    CHECK(ctrl.is_branch && ctrl.branch_taken);
    CHECK(ctrl.pc_src == PC_SRC_REL8_TAKEN);

    CHECK(control_unit_decode(&instr, 0, &ctrl) == CONTROL_UNIT_OK);
    CHECK(ctrl.is_branch && !ctrl.branch_taken);
    CHECK(ctrl.pc_src == PC_SRC_NEXT_PC);

    char formatted[160];
    size_t len = control_unit_format_signals(&ctrl, formatted, sizeof(formatted));
    CHECK(len > 0);
    CHECK(strstr(formatted, "PCSrc=NEXT_PC") != NULL);

    return 1;
}

static int test_datapath_stages_and_agu(void)
{
    Memory mem;
    CHECK(memory_init(&mem) == MEMORY_STATUS_OK);

    /* LOADR R1, [R2 + 6] */
    mem.instruction[0] = DIOCPU_OP_LOADR;
    mem.instruction[1] = 0x12;
    mem.instruction[2] = 0x06;
    mem.instruction[3] = 0x00;

    DatapathState dp;
    datapath_reset(&dp);

    CHECK(datapath_fetch_decode(&mem, 0, 0, &dp) == DECODER_STATUS_OK);
    CHECK(dp.valid);
    CHECK(dp.ir.opcode == DIOCPU_OP_LOADR);

    uint16_t gpr[16] = {0};
    gpr[2] = 0x0200;

    datapath_read_operands(&dp, gpr, 16, DIOCPU_STACK_INITIAL_SP);
    CHECK(dp.alu_in_a == 0x0200);
    CHECK(dp.alu_in_b == 6);
    CHECK(dp.effective_addr_valid);
    CHECK(dp.effective_addr == 0x0206);
    CHECK(dp.bus_addr == 0x0206);

    dp.mdr = 0xCAFE;
    CHECK(datapath_select_writeback(&dp, gpr, 16) == 0xCAFE);

    uint32_t next_pc = 0;
    CHECK(datapath_resolve_next_pc(&dp, gpr, 16, 0, 0, 0, &next_pc));
    CHECK(next_pc == 4);

    return 1;
}

static int test_cpu_datapath_observability(void)
{
    static DioCPU cpu;
    CHECK(cpu_init(&cpu) == CPU_STATUS_OK);

    const char *program =
        "    LOADI R1, 25\n"
        "    LOADI R2, 4\n"
        "    DIV R1, R2\n"
        "    LOADI R3, 0x0100\n"
        "    STORE R1, [R3 + 2]\n"
        "    LOAD R4, [R3 + 2]\n"
        "    HLT\n";

    AssemblerResult res;
    CHECK(assembler_assemble(program, &cpu.memory, &res) == ASSEMBLER_STATUS_OK);
    CHECK(cpu_reset(&cpu) == CPU_STATUS_OK);

    /* Step 1: LOADI R1, 25 */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.datapath.valid);
    CHECK(cpu.datapath.ir.opcode == DIOCPU_OP_LOADI);
    CHECK(cpu.datapath.ctrl.wb_src == WB_SRC_IMM16);
    CHECK(cpu.datapath.next_pc_target == 4);

    /* Step 2: LOADI R2, 4 */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);

    /* Step 3: DIV R1, R2 -> 25 / 4 = 6 rem 1 */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.datapath.ir.opcode == DIOCPU_OP_DIV);
    CHECK(cpu.datapath.alu_in_a == 25);
    CHECK(cpu.datapath.alu_in_b == 4);
    CHECK(cpu.datapath.alu_out == 6);
    CHECK(cpu.datapath.alu_rem == 1);
    CHECK(cpu.datapath.ctrl.reg_write_a);
    CHECK(cpu.datapath.ctrl.reg_write_b);
    CHECK(cpu.gpr[1] == 6);
    CHECK(cpu.gpr[2] == 1);

    /* Step 4: LOADI R3, 0x0100 */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);

    /* Step 5: STORE R1, [R3 + 2] */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.datapath.ir.opcode == DIOCPU_OP_STORER);
    CHECK(cpu.datapath.effective_addr == 0x0102);
    CHECK(cpu.datapath.mdr == 6);
    CHECK(cpu.datapath.ctrl.mem_write);

    /* Step 6: LOAD R4, [R3 + 2] */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.datapath.ir.opcode == DIOCPU_OP_LOADR);
    CHECK(cpu.datapath.effective_addr == 0x0102);
    CHECK(cpu.datapath.mdr == 6);
    CHECK(cpu.gpr[4] == 6);

    /* Step 7: HLT */
    CHECK(cpu_step(&cpu) == CPU_STATUS_HALTED);
    CHECK(cpu.datapath.ctrl.is_halt);

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

    RUN_TEST(test_control_unit_signals);
    RUN_TEST(test_datapath_stages_and_agu);
    RUN_TEST(test_cpu_datapath_observability);

    printf("\nDatapath & Control Unit tests: %u passed.\n", passed);
    return 0;
}
