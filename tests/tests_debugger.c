#include "debugger.h"

#include <stdio.h>

#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            fprintf(stderr, "FAILED: %s:%d: %s\n",             \
                    __FILE__, __LINE__, #condition);          \
            return 0;                                         \
        }                                                     \
    } while (0)


static int test_disassembly(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    const uint8_t program[] = {
        0x10, 0x01, 0x34, 0x12,
        0x20, 0x12,
        0x50, 0xFC,
        0x01
    };

    for (size_t i = 0; i < sizeof(program); i++) {
        CHECK(
            memory_instruction_write8(
                memory,
                (uint32_t)i,
                program[i]
            ) == MEMORY_STATUS_OK
        );
    }

    FILE *output = tmpfile();

    CHECK(output != NULL);

    DebuggerResult result;

    CHECK(
        debugger_disassemble(
            memory,
            0,
            sizeof(program),
            output,
            &result
        ) == DEBUGGER_STATUS_OK
    );

    CHECK(result.instruction_count == 4);
    CHECK(result.next_pc == 9);

    CHECK(fflush(output) == 0);
    CHECK(fseek(output, 0, SEEK_END) == 0);

    long output_size = ftell(output);

    CHECK(output_size > 0);

    fclose(output);

    return 1;
}


static int test_invalid_opcode(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    CHECK(
        memory_instruction_write8(
            memory,
            0,
            0xFF
        ) == MEMORY_STATUS_OK
    );

    FILE *output = tmpfile();

    CHECK(output != NULL);

    DebuggerResult result;

    CHECK(
        debugger_disassemble(
            memory,
            0,
            1,
            output,
            &result
        ) == DEBUGGER_STATUS_DECODE_ERROR
    );

    CHECK(result.fault_pc == 0);
    CHECK(
        result.decoder_status ==
        DECODER_STATUS_INVALID_OPCODE
    );

    fclose(output);

    return 1;
}


static int test_incomplete_instruction(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    CHECK(
        memory_instruction_write8(
            memory,
            0,
            DIOCPU_OP_LOADI
        ) == MEMORY_STATUS_OK
    );

    CHECK(
        memory_instruction_write8(
            memory,
            1,
            0x01
        ) == MEMORY_STATUS_OK
    );

    FILE *output = tmpfile();

    CHECK(output != NULL);

    DebuggerResult result;

    CHECK(
        debugger_disassemble(
            memory,
            0,
            2,
            output,
            &result
        ) == DEBUGGER_STATUS_INCOMPLETE_INSTRUCTION
    );

    CHECK(result.fault_pc == 0);

    fclose(output);

    return 1;
}


static int test_invalid_arguments(Memory *memory)
{
    FILE *output = tmpfile();

    CHECK(output != NULL);

    DebuggerResult result;

    CHECK(
        debugger_disassemble(
            NULL,
            0,
            1,
            output,
            &result
        ) == DEBUGGER_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        debugger_disassemble(
            memory,
            0,
            1,
            NULL,
            &result
        ) == DEBUGGER_STATUS_INVALID_ARGUMENT
    );

    CHECK(
        debugger_disassemble(
            memory,
            0,
            1,
            output,
            NULL
        ) == DEBUGGER_STATUS_INVALID_ARGUMENT
    );

    fclose(output);

    return 1;
}


#define RUN_TEST(test_function)                           \
    do {                                                  \
        if (!test_function(&memory)) {                    \
            return 1;                                     \
        }                                                 \
        printf("[PASS] %s\n", #test_function);            \
        passed++;                                         \
    } while (0)


int main(void)
{
    static Memory memory;

    unsigned int passed = 0;

    RUN_TEST(test_disassembly);
    RUN_TEST(test_invalid_opcode);
    RUN_TEST(test_incomplete_instruction);
    RUN_TEST(test_invalid_arguments);

    printf("\nDebugger tests: %u passed.\n", passed);

    return 0;
}
