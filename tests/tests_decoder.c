#include "decoder.h"

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


static int setup(Memory *memory)
{
    return memory_init(memory) == MEMORY_STATUS_OK;
}


/*
 * Format A: No operand instructions.
 */

static int test_no_operand_instructions(void)
{
    Memory memory;
    CHECK(setup(&memory));

    memory.instruction[0x00000] = DIOCPU_OP_NOP;
    memory.instruction[0x00001] = DIOCPU_OP_HLT;
    memory.instruction[0x00002] = DIOCPU_OP_RET;

    DecodedInstruction inst;

    CHECK(decoder_decode(&memory, 0x00000, &inst) == DECODER_STATUS_OK);
    CHECK(inst.opcode == DIOCPU_OP_NOP);
    CHECK(inst.format == INSTRUCTION_FORMAT_NONE);
    CHECK(inst.length == 1);
    CHECK(inst.pc == 0x00000);
    CHECK(inst.next_pc == 0x00001);

    CHECK(decoder_decode(&memory, 0x00001, &inst) == DECODER_STATUS_OK);
    CHECK(inst.opcode == DIOCPU_OP_HLT);
    CHECK(inst.format == INSTRUCTION_FORMAT_NONE);
    CHECK(inst.length == 1);
    CHECK(inst.pc == 0x00001);
    CHECK(inst.next_pc == 0x00002);

    CHECK(decoder_decode(&memory, 0x00002, &inst) == DECODER_STATUS_OK);
    CHECK(inst.opcode == DIOCPU_OP_RET);
    CHECK(inst.format == INSTRUCTION_FORMAT_NONE);
    CHECK(inst.length == 1);
    CHECK(inst.pc == 0x00002);
    CHECK(inst.next_pc == 0x00003);

    return 1;
}


/*
 * Format B: Register to register.
 */

static int test_register_register(void)
{
    Memory memory;
    CHECK(setup(&memory));

    /*
     * ADD R2, R5
     * Byte 0: 0x20
     * Byte 1: (2 << 4) | 5 = 0x25
     */

    memory.instruction[0x00100] = DIOCPU_OP_ADD;
    memory.instruction[0x00101] = 0x25;

    DecodedInstruction inst;

    CHECK(decoder_decode(&memory, 0x00100, &inst) == DECODER_STATUS_OK);
    CHECK(inst.opcode == DIOCPU_OP_ADD);
    CHECK(inst.format == INSTRUCTION_FORMAT_REGISTER_REGISTER);
    CHECK(inst.reg_a == 2);
    CHECK(inst.reg_b == 5);
    CHECK(inst.length == 2);
    CHECK(inst.pc == 0x00100);
    CHECK(inst.next_pc == 0x00102);

    return 1;
}


/*
 * Format C: Single register.
 */

static int test_single_register(void)
{
    Memory memory;
    CHECK(setup(&memory));

    /*
     * NOT R3
     * Byte 0: 0x33
     * Byte 1: 0x03
     */

    memory.instruction[0x00200] = DIOCPU_OP_NOT;
    memory.instruction[0x00201] = 0x03;

    DecodedInstruction inst;

    CHECK(decoder_decode(&memory, 0x00200, &inst) == DECODER_STATUS_OK);
    CHECK(inst.opcode == DIOCPU_OP_NOT);
    CHECK(inst.format == INSTRUCTION_FORMAT_REGISTER);
    CHECK(inst.reg_a == 3);
    CHECK(inst.length == 2);
    CHECK(inst.pc == 0x00200);
    CHECK(inst.next_pc == 0x00202);

    return 1;
}


/*
 * Format D: Register + Immediate 16-bit.
 */

static int test_loadi(void)
{
    Memory memory;
    CHECK(setup(&memory));

    /*
     * LOADI R4, 0x1234
     */

    memory.instruction[0x00300] = DIOCPU_OP_LOADI;
    memory.instruction[0x00301] = 0x04;
    memory.instruction[0x00302] = 0x34;
    memory.instruction[0x00303] = 0x12;

    DecodedInstruction inst;

    CHECK(decoder_decode(&memory, 0x00300, &inst) == DECODER_STATUS_OK);
    CHECK(inst.opcode == DIOCPU_OP_LOADI);
    CHECK(inst.format == INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16);
    CHECK(inst.reg_a == 4);
    CHECK(inst.immediate16 == 0x1234);
    CHECK(inst.length == 4);
    CHECK(inst.pc == 0x00300);
    CHECK(inst.next_pc == 0x00304);

    return 1;
}


/*
 * Format E: Register + Absolute Address 18-bit.
 */

static int test_load_store(void)
{
    Memory memory;
    CHECK(setup(&memory));

    /*
     * LOAD R1, [0x23456]
     */

    memory.instruction[0x00400] = DIOCPU_OP_LOAD;
    memory.instruction[0x00401] = 0x01;
    memory.instruction[0x00402] = 0x56;
    memory.instruction[0x00403] = 0x34;
    memory.instruction[0x00404] = 0x02;

    DecodedInstruction inst;

    CHECK(decoder_decode(&memory, 0x00400, &inst) == DECODER_STATUS_OK);
    CHECK(inst.opcode == DIOCPU_OP_LOAD);
    CHECK(inst.format == INSTRUCTION_FORMAT_REGISTER_ABSOLUTE18);
    CHECK(inst.reg_a == 1);
    CHECK(inst.absolute_address == 0x23456);
    CHECK(inst.length == 5);
    CHECK(inst.pc == 0x00400);
    CHECK(inst.next_pc == 0x00405);

    return 1;
}


/*
 * Format F: Absolute Address 18-bit control-flow.
 */

static int test_absolute_control_flow(void)
{
    Memory memory;
    CHECK(setup(&memory));

    /*
     * JMP 0x12345
     */

    memory.instruction[0x00500] = DIOCPU_OP_JMP;
    memory.instruction[0x00501] = 0x45;
    memory.instruction[0x00502] = 0x23;
    memory.instruction[0x00503] = 0x01;

    DecodedInstruction inst;

    CHECK(decoder_decode(&memory, 0x00500, &inst) == DECODER_STATUS_OK);
    CHECK(inst.opcode == DIOCPU_OP_JMP);
    CHECK(inst.format == INSTRUCTION_FORMAT_ABSOLUTE18);
    CHECK(inst.absolute_address == 0x12345);
    CHECK(inst.length == 4);
    CHECK(inst.pc == 0x00500);
    CHECK(inst.next_pc == 0x00504);

    return 1;
}


/*
 * Format G: Relative branch.
 */

static int test_relative_branches(void)
{
    Memory memory;
    CHECK(setup(&memory));

    /*
     * JZ -2 (0xFE)
     */

    memory.instruction[0x00600] = DIOCPU_OP_JZ;
    memory.instruction[0x00601] = 0xFE;

    DecodedInstruction inst;

    CHECK(decoder_decode(&memory, 0x00600, &inst) == DECODER_STATUS_OK);
    CHECK(inst.opcode == DIOCPU_OP_JZ);
    CHECK(inst.format == INSTRUCTION_FORMAT_RELATIVE8);
    CHECK(inst.relative_offset == -2);
    CHECK(inst.length == 2);
    CHECK(inst.pc == 0x00600);
    CHECK(inst.next_pc == 0x00602);

    return 1;
}


/*
 * Invalid opcode test.
 */

static int test_invalid_opcode(void)
{
    Memory memory;
    CHECK(setup(&memory));

    memory.instruction[0x00700] = 0xFF;

    DecodedInstruction inst;

    memset(&inst, 0xAA, sizeof(inst));

    DecoderStatus status = decoder_decode(&memory, 0x00700, &inst);

    CHECK(status == DECODER_STATUS_INVALID_OPCODE);

    return 1;
}


/*
 * Reserved bits validation.
 */

static int test_reserved_bits(void)
{
    Memory memory;
    CHECK(setup(&memory));

    /*
     * NOT with invalid register byte (upper bits not zero).
     */

    memory.instruction[0x00800] = DIOCPU_OP_NOT;
    memory.instruction[0x00801] = 0xF1;

    DecodedInstruction inst;

    CHECK(decoder_decode(&memory, 0x00800, &inst) ==
          DECODER_STATUS_INVALID_INSTRUCTION);

    /*
     * JMP with invalid upper address bits (high byte > 0x03).
     */

    memory.instruction[0x00810] = DIOCPU_OP_JMP;
    memory.instruction[0x00811] = 0x00;
    memory.instruction[0x00812] = 0x00;
    memory.instruction[0x00813] = 0x04;

    CHECK(decoder_decode(&memory, 0x00810, &inst) ==
          DECODER_STATUS_INVALID_INSTRUCTION);

    return 1;
}


/*
 * Fetch fault at end of memory.
 */

static int test_instruction_fetch_fault(void)
{
    Memory memory;
    CHECK(setup(&memory));

    /*
     * LOADI requires 4 bytes.
     * Place at the very end of Instruction Memory.
     */

    uint32_t pc = DIOCPU_MEMORY_MAX_ADDRESS - 1;

    memory.instruction[pc] = DIOCPU_OP_LOADI;

    DecodedInstruction inst;

    CHECK(decoder_decode(&memory, pc, &inst) ==
          DECODER_STATUS_INSTRUCTION_FETCH_FAULT);

    return 1;
}


/*
 * Argument validation test.
 */

static int test_invalid_arguments(void)
{
    Memory memory;
    DecodedInstruction inst;

    CHECK(decoder_decode(NULL, 0, &inst) ==
          DECODER_STATUS_INVALID_ARGUMENT);

    CHECK(decoder_decode(&memory, 0, NULL) ==
          DECODER_STATUS_INVALID_ARGUMENT);

    return 1;
}

static int test_m1_formats_and_isa_table(void)
{
    Memory memory;
    CHECK(setup(&memory));

    /* INT 0x21 */
    memory.instruction[0x00900] = DIOCPU_OP_INT;
    memory.instruction[0x00901] = 0x21;

    /* SHLI R4, 3 */
    memory.instruction[0x00902] = DIOCPU_OP_SHLI;
    memory.instruction[0x00903] = 0x04;
    memory.instruction[0x00904] = 0x03;

    /* LOADR R1, [R2 - 4] */
    memory.instruction[0x00905] = DIOCPU_OP_LOADR;
    memory.instruction[0x00906] = 0x12;
    memory.instruction[0x00907] = 0xFC;
    memory.instruction[0x00908] = 0xFF;

    /* LOADX R5, [R6 + R7] */
    memory.instruction[0x00909] = DIOCPU_OP_LOADX;
    memory.instruction[0x0090A] = 0x56;
    memory.instruction[0x0090B] = 0x07;

    /* STORESP R8, [SP + 6] */
    memory.instruction[0x0090C] = DIOCPU_OP_STORESP;
    memory.instruction[0x0090D] = 0x08;
    memory.instruction[0x0090E] = 0x06;
    memory.instruction[0x0090F] = 0x00;

    DecodedInstruction inst;

    CHECK(decoder_decode(&memory, 0x00900, &inst) == DECODER_STATUS_OK);
    CHECK(inst.format == INSTRUCTION_FORMAT_IMMEDIATE8);
    CHECK(inst.immediate8 == 0x21);
    CHECK(inst.length == 2);

    CHECK(decoder_decode(&memory, 0x00902, &inst) == DECODER_STATUS_OK);
    CHECK(inst.format == INSTRUCTION_FORMAT_REGISTER_IMMEDIATE8);
    CHECK(inst.reg_a == 4);
    CHECK(inst.immediate8 == 3);
    CHECK(inst.length == 3);

    CHECK(decoder_decode(&memory, 0x00905, &inst) == DECODER_STATUS_OK);
    CHECK(inst.format == INSTRUCTION_FORMAT_REGISTER_INDIRECT_OFFSET);
    CHECK(inst.reg_a == 1);
    CHECK(inst.reg_b == 2);
    CHECK(inst.offset16 == -4);
    CHECK(inst.length == 4);

    CHECK(decoder_decode(&memory, 0x00909, &inst) == DECODER_STATUS_OK);
    CHECK(inst.format == INSTRUCTION_FORMAT_REGISTER_INDEXED);
    CHECK(inst.reg_a == 5);
    CHECK(inst.reg_b == 6);
    CHECK(inst.reg_c == 7);
    CHECK(inst.length == 3);

    CHECK(decoder_decode(&memory, 0x0090C, &inst) == DECODER_STATUS_OK);
    CHECK(inst.format == INSTRUCTION_FORMAT_REGISTER_SP_OFFSET);
    CHECK(inst.reg_a == 8);
    CHECK(inst.offset16 == 6);
    CHECK(inst.length == 4);

    /* Reserved upper bits on LOADX third byte must fail */
    memory.instruction[0x00920] = DIOCPU_OP_LOADX;
    memory.instruction[0x00921] = 0x12;
    memory.instruction[0x00922] = 0xF3;
    CHECK(decoder_decode(&memory, 0x00920, &inst) == DECODER_STATUS_INVALID_INSTRUCTION);

    CHECK(isa_lookup_mnemonic("IRET")->opcode == DIOCPU_OP_IRET);
    CHECK(strcmp(isa_opcode_name(DIOCPU_OP_JG), "JG") == 0);

    return 1;
}

int main(void)
{
    int passed = 0;

    if (test_no_operand_instructions()) {
        printf("[PASS] test_no_operand_instructions\n");
        passed++;
    }

    if (test_register_register()) {
        printf("[PASS] test_register_register\n");
        passed++;
    }

    if (test_single_register()) {
        printf("[PASS] test_single_register\n");
        passed++;
    }

    if (test_loadi()) {
        printf("[PASS] test_loadi\n");
        passed++;
    }

    if (test_load_store()) {
        printf("[PASS] test_load_store\n");
        passed++;
    }

    if (test_absolute_control_flow()) {
        printf("[PASS] test_absolute_control_flow\n");
        passed++;
    }

    if (test_relative_branches()) {
        printf("[PASS] test_relative_branches\n");
        passed++;
    }

    if (test_invalid_opcode()) {
        printf("[PASS] test_invalid_opcode\n");
        passed++;
    }

    if (test_reserved_bits()) {
        printf("[PASS] test_reserved_bits\n");
        passed++;
    }

    if (test_instruction_fetch_fault()) {
        printf("[PASS] test_instruction_fetch_fault\n");
        passed++;
    }

    if (test_invalid_arguments()) {
        printf("[PASS] test_invalid_arguments\n");
        passed++;
    }

    if (test_m1_formats_and_isa_table()) {
        printf("[PASS] test_m1_formats_and_isa_table\n");
        passed++;
    }

    printf("\nDecoder tests: %d passed.\n", passed);

    return (passed == 12) ? 0 : 1;
}
