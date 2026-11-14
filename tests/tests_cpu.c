#include "cpu.h"

#include "assembler.h"
#include "decoder.h"
#include "memory.h"
#include "stack.h"

#include <stdio.h>

#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            fprintf(stderr, "FAILED: %s:%d: %s\n",             \
                    __FILE__, __LINE__, #condition);          \
            return 0;                                         \
        }                                                     \
    } while (0)


static DioCPU cpu;


static int prepare_cpu(const char *source)
{
    if (cpu_init(&cpu) != CPU_STATUS_OK) {
        return 0;
    }

    AssemblerResult result;

    if (assembler_assemble(
            source,
            &cpu.memory,
            &result
        ) != ASSEMBLER_STATUS_OK) {

        fprintf(
            stderr,
            "Assembly error at line %u: %s\n",
            (unsigned int)result.error_line,
            assembler_status_string(result.status)
        );

        return 0;
    }

    return cpu_reset(&cpu) == CPU_STATUS_OK;
}

static int test_full_integration(void)
{
    const char *program =
        "LOADI R1, 10\n"
        "LOADI R2, 20\n"
        "ADD R1, R2\n"
        "LOADI R3, 0x0100\n"
        "STORE R1, [0x0100]\n"
        "LOAD R4, [0x0100]\n"
        "CMP R4, R1\n"
        "JZ EQUAL\n"
        "LOADI R5, 0\n"
        "JMP FINISH\n"
        "EQUAL:\n"
        "CALL DOUBLE\n"
        "PUSH R1\n"
        "POP R6\n"
        "FINISH:\n"
        "HLT\n"
        "DOUBLE:\n"
        "LOADI R7, 2\n"
        "MUL R1, R7\n"
        "RET\n";

    CHECK(prepare_cpu(program));

    uint64_t steps = 0;

    CPUStatus status = cpu_run(
        &cpu,
        100,
        &steps
    );

    CHECK(status == CPU_STATUS_HALTED);

    CHECK(cpu.state == CPU_STATE_HALTED);

    CHECK(cpu.gpr[1] == 60);
    CHECK(cpu.gpr[2] == 20);
    CHECK(cpu.gpr[3] == 0x0100);
    CHECK(cpu.gpr[4] == 30);
    CHECK(cpu.gpr[5] == 0);
    CHECK(cpu.gpr[6] == 60);
    CHECK(cpu.gpr[7] == 2);

    CHECK(cpu.sp == DIOCPU_STACK_INITIAL_SP);

    CHECK((cpu.flags & CPU_FLAG_ZF) == 0);

    CHECK(steps == 15 || steps == 14);

    uint16_t memory_value = 0;

    CHECK(
        memory_data_read16(
            &cpu.memory,
            0x0100,
            &memory_value
        ) == MEMORY_STATUS_OK
    );

    CHECK(memory_value == 30);

    return 1;
}

static int test_call_ret(void)
{
    const char *program =
        "CALL FUNCTION\n"
        "HLT\n"
        "FUNCTION:\n"
        "LOADI R7, 0xBEEF\n"
        "RET\n";

    CHECK(prepare_cpu(program));

    uint64_t steps = 0;

    CHECK(
        cpu_run(&cpu, 20, &steps) ==
        CPU_STATUS_HALTED
    );

    CHECK(cpu.gpr[7] == 0xBEEF);

    CHECK(cpu.pc == 5);

    CHECK(cpu.sp == DIOCPU_STACK_INITIAL_SP);

    CHECK(steps == 4);

    return 1;
}

static int test_division(void)
{
    const char *program =
        "LOADI R1, 20\n"
        "LOADI R2, 6\n"
        "DIV R1, R2\n"
        "HLT\n";

    CHECK(prepare_cpu(program));

    uint64_t steps = 0;

    CHECK(
        cpu_run(&cpu, 20, &steps) ==
        CPU_STATUS_HALTED
    );

    CHECK(cpu.gpr[1] == 3);
    CHECK(cpu.gpr[2] == 2);

    return 1;
}

static int test_divide_by_zero_fault(void)
{
    const char *program =
        "LOADI R1, 0\n"
        "LOADI R2, 0\n"
        "ADD R1, R2\n"
        "LOADI R3, 10\n"
        "LOADI R4, 0\n"
        "DIV R3, R4\n";

    CHECK(prepare_cpu(program));

    uint64_t steps = 0;

    CHECK(
        cpu_run(&cpu, 20, &steps) ==
        CPU_STATUS_FAULTED
    );

    CHECK(cpu.state == CPU_STATE_FAULTED);

    CHECK(cpu.fault == CPU_FAULT_DIVIDE_BY_ZERO);

    CHECK(cpu.fault_pc == 18);
    CHECK(cpu.pc == 18);

    CHECK(cpu.gpr[3] == 10);
    CHECK(cpu.gpr[4] == 0);

    CHECK((cpu.flags & CPU_FLAG_ZF) != 0);

    CHECK(steps == 5);

    return 1;
}

static int test_stack_memory_protection(void)
{
    const char *program =
        "LOADI R1, 0x1234\n"
        "STORE R1, [0x30000]\n";

    CHECK(prepare_cpu(program));

    uint64_t steps = 0;

    CHECK(
        cpu_run(&cpu, 10, &steps) ==
        CPU_STATUS_FAULTED
    );

    CHECK(
        cpu.fault == CPU_FAULT_MEMORY_ACCESS_FAULT
    );

    CHECK(cpu.pc == 4);

    CHECK(cpu.sp == DIOCPU_STACK_INITIAL_SP);

    CHECK(cpu.memory.data[DIOCPU_STACK_BOTTOM] == 0);

    return 1;
}

static int test_stack_underflow(void)
{
    const char *program =
        "POP R1\n";

    CHECK(prepare_cpu(program));

    cpu.gpr[1] = 0xABCD;

    CHECK(cpu_step(&cpu) == CPU_STATUS_FAULTED);

    CHECK(cpu.fault == CPU_FAULT_STACK_UNDERFLOW);

    CHECK(cpu.pc == 0);

    CHECK(cpu.sp == DIOCPU_STACK_INITIAL_SP);

    CHECK(cpu.gpr[1] == 0xABCD);

    return 1;
}

static int test_invalid_opcode(void)
{
    CHECK(cpu_init(&cpu) == CPU_STATUS_OK);

    CHECK(
        memory_instruction_write8(
            &cpu.memory,
            0,
            0xFF
        ) == MEMORY_STATUS_OK
    );

    CHECK(cpu_step(&cpu) == CPU_STATUS_FAULTED);

    CHECK(cpu.fault == CPU_FAULT_INVALID_OPCODE);

    CHECK(cpu.pc == 0);

    CHECK(cpu.state == CPU_STATE_FAULTED);

    return 1;
}

static int test_instruction_limit(void)
{
    const char *program =
        "LOOP:\n"
        "JMP LOOP\n";

    CHECK(prepare_cpu(program));

    uint64_t steps = 0;

    CHECK(
        cpu_run(&cpu, 3, &steps) ==
        CPU_STATUS_STEP_LIMIT_REACHED
    );

    CHECK(steps == 3);

    CHECK(cpu.pc == 0);

    CHECK(cpu.state == CPU_STATE_RUNNING);

    CHECK(cpu.instructions_executed == 3);

    return 1;
}

static int test_addressing_modes_and_stack_frame(void)
{
    const char *program =
        "    LOADI R1, 0x0200\n"
        "    LOADI R2, 0x1122\n"
        "    STORE R2, [R1]\n"
        "    LOADI R3, 0x3344\n"
        "    STORE R3, [R1 + 4]\n"
        "    LOADI R4, 4\n"
        "    LOAD R5, [R1 + R4]\n"
        "    LEA R6, [R1 + 4]\n"
        "    LOAD R7, [R6 - 4]\n"
        "    PUSH R2\n"
        "    PUSH R3\n"
        "    LOAD R8, [SP + 2]\n"
        "    LOADI R9, 0x5566\n"
        "    STORE R9, [SP]\n"
        "    POP R10\n"
        "    POP R11\n"
        "    HLT\n";

    CHECK(prepare_cpu(program));

    uint64_t steps = 0;
    CHECK(cpu_run(&cpu, 100, &steps) == CPU_STATUS_HALTED);
    CHECK(cpu.gpr[5] == 0x3344);
    CHECK(cpu.gpr[6] == 0x0204);
    CHECK(cpu.gpr[7] == 0x1122);
    CHECK(cpu.gpr[8] == 0x1122);
    CHECK(cpu.gpr[10] == 0x5566);
    CHECK(cpu.gpr[11] == 0x1122);
    CHECK(cpu.sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}

static int test_extended_alu_shifts_and_branches(void)
{
    const char *program =
        "    LOADI R1, 5\n"
        "    MOV R2, R1\n"
        "    INC R2\n"
        "    ADD R2, 10\n"
        "    SUB R2, 4\n"
        "    DEC R2\n"
        "    SHL R2, 1\n"
        "    SHR R2, 2\n"
        "    LOADI R3, -8\n"
        "    SAR R3, 1\n"
        "    CMP R3, 0\n"
        "    JS NEG_OK\n"
        "    HLT\n"
        "NEG_OK:\n"
        "    CMPI R3, -2\n"
        "    JL LT_OK\n"
        "    HLT\n"
        "LT_OK:\n"
        "    LOADI R4, FINISH\n"
        "    CALL R4\n"
        "    HLT\n"
        "FINISH:\n"
        "    LOADI R5, 0xCAFE\n"
        "    RET\n";

    CHECK(prepare_cpu(program));

    uint64_t steps = 0;
    CHECK(cpu_run(&cpu, 100, &steps) == CPU_STATUS_HALTED);
    CHECK(cpu.gpr[2] == 5);
    CHECK(cpu.gpr[3] == (uint16_t)(int16_t)-4);
    CHECK(cpu.gpr[5] == 0xCAFE);
    CHECK(cpu.sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}

static int test_software_and_hardware_interrupts(void)
{
    const char *program =
        "    JMP MAIN\n"
        "SW_ISR:\n"
        "    ADD R1, 100\n"
        "    IRET\n"
        "IRQ_ISR:\n"
        "    ADD R2, 50\n"
        "    IRET\n"
        "MAIN:\n"
        "    LOADI R1, 7\n"
        "    LOADI R2, 3\n"
        "    INT 0x20\n"
        "    CLI\n"
        "    NOP\n"
        "    STI\n"
        "    NOP\n"
        "    HLT\n";

    CHECK(prepare_cpu(program));

    /* Find ISR addresses via decoder walk or assemble labels? Let's verify exact PCs! */
    DecodedInstruction inst;
    CHECK(decoder_decode(&cpu.memory, 0, &inst) == DECODER_STATUS_OK);
    uint32_t sw_isr_pc = inst.next_pc; /* right after JMP MAIN */
    CHECK(decoder_decode(&cpu.memory, sw_isr_pc, &inst) == DECODER_STATUS_OK);
    uint32_t sw_iret_pc = inst.next_pc;
    CHECK(decoder_decode(&cpu.memory, sw_iret_pc, &inst) == DECODER_STATUS_OK);
    uint32_t irq_isr_pc = inst.next_pc;

    CHECK(cpu_register_interrupt_handler(&cpu, 0x20, sw_isr_pc) == CPU_STATUS_OK);
    CHECK(cpu_register_interrupt_handler(&cpu, 0x10, irq_isr_pc) == CPU_STATUS_OK);

    /* Step JMP MAIN, LOADI R1, LOADI R2, INT 0x20 */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* INT 0x20 */
    CHECK(cpu.pc == sw_isr_pc);
    CHECK(cpu.interrupt_nesting == 1);
    CHECK((cpu.flags & CPU_FLAG_IF) == 0);

    /* Execute SW_ISR: ADD R1, 100; IRET */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* IRET */
    CHECK(cpu.gpr[1] == 107);
    CHECK(cpu.interrupt_nesting == 0);

    /* Raise IRQ 0x10 while IF == 0 (after CLI): must remain pending until STI */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* CLI */
    CHECK(cpu_raise_irq(&cpu, 0x10) == CPU_STATUS_OK);
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* NOP (IRQ masked!) */
    CHECK(cpu.gpr[2] == 3);
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK); /* STI */
    CHECK((cpu.flags & CPU_FLAG_IF) != 0);

    /* Next step services pending IRQ 0x10 before NOP */
    CHECK(cpu_step(&cpu) == CPU_STATUS_OK);
    CHECK(cpu.pc == irq_isr_pc);

    uint64_t steps = 0;
    CHECK(cpu_run(&cpu, 20, &steps) == CPU_STATUS_HALTED);
    CHECK(cpu.gpr[2] == 53);
    CHECK(cpu.interrupt_count == 2);
    CHECK(cpu.sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}

static int test_exception_trap_via_ivt(void)
{
    const char *program =
        "    JMP START\n"
        "DIV0_HANDLER:\n"
        "    LOADI R3, 0xDEAD\n"
        "    LOADI R4, 1\n"
        "    IRET\n"
        "START:\n"
        "    LOADI R1, 10\n"
        "    LOADI R4, 0\n"
        "    DIV R1, R4\n"
        "    HLT\n";

    CHECK(prepare_cpu(program));

    DecodedInstruction inst;
    CHECK(decoder_decode(&cpu.memory, 0, &inst) == DECODER_STATUS_OK);
    uint32_t handler_pc = inst.next_pc;

    /* Vector 0x04 = CPU_FAULT_DIVIDE_BY_ZERO */
    CHECK(cpu_register_interrupt_handler(&cpu, 0x04, handler_pc) == CPU_STATUS_OK);

    uint64_t steps = 0;
    CHECK(cpu_run(&cpu, 30, &steps) == CPU_STATUS_HALTED);
    CHECK(cpu.state == CPU_STATE_HALTED);
    CHECK(cpu.gpr[3] == 0xDEAD);
    CHECK(cpu.gpr[1] == 10);
    CHECK(cpu.gpr[4] == 0);
    CHECK(cpu.interrupt_count == 1);
    CHECK(cpu.sp == DIOCPU_STACK_INITIAL_SP);

    return 1;
}

#define RUN_TEST(test_function)                         \
    do {                                                \
        if (!test_function()) {                         \
            return 1;                                   \
        }                                               \
        printf("[PASS] %s\n", #test_function);          \
        passed++;                                       \
    } while (0)


int main(void)
{
    unsigned int passed = 0;

    RUN_TEST(test_full_integration);
    RUN_TEST(test_call_ret);
    RUN_TEST(test_division);
    RUN_TEST(test_divide_by_zero_fault);
    RUN_TEST(test_stack_memory_protection);
    RUN_TEST(test_stack_underflow);
    RUN_TEST(test_invalid_opcode);
    RUN_TEST(test_instruction_limit);
    RUN_TEST(test_addressing_modes_and_stack_frame);
    RUN_TEST(test_extended_alu_shifts_and_branches);
    RUN_TEST(test_software_and_hardware_interrupts);
    RUN_TEST(test_exception_trap_via_ivt);

    printf("\nCPU V2 tests: %u passed.\n", passed);

    return 0;
}
