#include "assembler.h"
#include "decoder.h"

#include <stdio.h>

#define CHECK(condition)                                      \
    do {                                                      \
        if (!(condition)) {                                   \
            fprintf(stderr, "FAILED: %s:%d: %s\n",             \
                    __FILE__, __LINE__, #condition);          \
            return 0;                                         \
        }                                                     \
    } while (0)


static int test_two_pass_assembly(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    memory->data[0x100] = 0xAA;

    const char *source =
        "START:\n"
        "    LOADI R1, 10\n"
        "    LOADI R2, 20\n"
        "    ADD R1, R2\n"
        "    JZ DONE\n"
        "    JMP START\n"
        "DONE:\n"
        "    HLT\n";

    AssemblerResult result;

    CHECK(
        assembler_assemble(source, memory, &result) ==
        ASSEMBLER_STATUS_OK
    );

    CHECK(result.code_size == 17);
    CHECK(result.instruction_count == 6);

    CHECK(memory->data[0x100] == 0xAA);

    DecodedInstruction instruction = {0};

    CHECK(
        decoder_decode(memory, 0, &instruction) ==
        DECODER_STATUS_OK
    );

    CHECK(instruction.opcode == DIOCPU_OP_LOADI);
    CHECK(instruction.reg_a == 1);
    CHECK(instruction.immediate16 == 10);

    CHECK(
        decoder_decode(memory, 8, &instruction) ==
        DECODER_STATUS_OK
    );

    CHECK(instruction.opcode == DIOCPU_OP_ADD);
    CHECK(instruction.reg_a == 1);
    CHECK(instruction.reg_b == 2);

    CHECK(
        decoder_decode(memory, 10, &instruction) ==
        DECODER_STATUS_OK
    );

    CHECK(instruction.opcode == DIOCPU_OP_JZ);
    CHECK(instruction.relative_offset == 4);

    CHECK(
        decoder_decode(memory, 12, &instruction) ==
        DECODER_STATUS_OK
    );

    CHECK(instruction.opcode == DIOCPU_OP_JMP);
    CHECK(instruction.absolute_address == 0);

    CHECK(
        decoder_decode(memory, 16, &instruction) ==
        DECODER_STATUS_OK
    );

    CHECK(instruction.opcode == DIOCPU_OP_HLT);

    return 1;
}


static int test_undefined_label(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    CHECK(
        memory_instruction_write8(memory, 0, 0xAA) ==
        MEMORY_STATUS_OK
    );

    const char *source =
        "JMP UNKNOWN_LABEL\n";

    AssemblerResult result;

    CHECK(
        assembler_assemble(source, memory, &result) ==
        ASSEMBLER_STATUS_UNDEFINED_LABEL
    );

    CHECK(result.error_line == 1);

    /* Previous instruction memory remains unchanged. */

    CHECK(memory->instruction[0] == 0xAA);

    return 1;
}


static int test_duplicate_label(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    const char *source =
        "LOOP:\n"
        "NOP\n"
        "LOOP:\n"
        "HLT\n";

    AssemblerResult result;

    CHECK(
        assembler_assemble(source, memory, &result) ==
        ASSEMBLER_STATUS_DUPLICATE_LABEL
    );

    CHECK(result.error_line == 3);

    return 1;
}


static int test_invalid_register(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    const char *source =
        "LOADI R16, 100\n";

    AssemblerResult result;

    CHECK(
        assembler_assemble(source, memory, &result) ==
        ASSEMBLER_STATUS_INVALID_REGISTER
    );

    return 1;
}


static int test_invalid_immediate(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    const char *source =
        "LOADI R1, 70000\n";

    AssemblerResult result;

    CHECK(
        assembler_assemble(source, memory, &result) ==
        ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE
    );

    return 1;
}


static int test_branch_range(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    /*
     * Branch from address 0 to address 130.
     * PC_next is 2, so offset is 128.
     * Signed 8-bit maximum is 127.
     */

    char source[1024];

    int written = snprintf(
        source,
        sizeof(source),
        "JZ TARGET\n"
    );

    CHECK(written > 0);

    for (int i = 0; i < 128; i++) {
        size_t used = (size_t)written;

        int count = snprintf(
            source + used,
            sizeof(source) - used,
            "NOP\n"
        );

        CHECK(count > 0);

        written += count;
    }

    size_t used = (size_t)written;

    int count = snprintf(
        source + used,
        sizeof(source) - used,
        "TARGET:\nHLT\n"
    );

    CHECK(count > 0);

    AssemblerResult result;

    CHECK(
        assembler_assemble(source, memory, &result) ==
        ASSEMBLER_STATUS_BRANCH_OUT_OF_RANGE
    );

    return 1;
}


static int test_comments_and_case(Memory *memory)
{
    CHECK(memory_init(memory) == MEMORY_STATUS_OK);

    const char *source =
        "loadi r1, 0x1234 ; comment\n"
        "add R1, R2 # another comment\n"
        "hlt\n";

    AssemblerResult result;

    CHECK(
        assembler_assemble(source, memory, &result) ==
        ASSEMBLER_STATUS_OK
    );

    CHECK(result.code_size == 7);
    CHECK(result.instruction_count == 3);

    CHECK(memory->instruction[0] == 0x10);
    CHECK(memory->instruction[1] == 0x01);
    CHECK(memory->instruction[2] == 0x34);
    CHECK(memory->instruction[3] == 0x12);

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

    RUN_TEST(test_two_pass_assembly);
    RUN_TEST(test_undefined_label);
    RUN_TEST(test_duplicate_label);
    RUN_TEST(test_invalid_register);
    RUN_TEST(test_invalid_immediate);
    RUN_TEST(test_branch_range);
    RUN_TEST(test_comments_and_case);

    printf("\nAssembler tests: %u passed.\n", passed);

    return 0;
}
