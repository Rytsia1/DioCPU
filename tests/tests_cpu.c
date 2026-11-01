#include "cpu.h"

#include "assembler.h"
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

    printf("\nCPU V2 tests: %u passed.\n", passed);

    return 0;
}
