#include "debugger.h"

#include <stdint.h>


static const char *opcode_name(InstructionOpcode opcode)
{
    switch (opcode) {

        case DIOCPU_OP_NOP: return "NOP";
        case DIOCPU_OP_HLT: return "HLT";
        case DIOCPU_OP_JMP: return "JMP";
        case DIOCPU_OP_CALL: return "CALL";
        case DIOCPU_OP_RET: return "RET";

        case DIOCPU_OP_LOADI: return "LOADI";
        case DIOCPU_OP_LOAD: return "LOAD";
        case DIOCPU_OP_STORE: return "STORE";
        case DIOCPU_OP_PUSH: return "PUSH";
        case DIOCPU_OP_POP: return "POP";

        case DIOCPU_OP_ADD: return "ADD";
        case DIOCPU_OP_SUB: return "SUB";
        case DIOCPU_OP_MUL: return "MUL";
        case DIOCPU_OP_MULS: return "MULS";
        case DIOCPU_OP_DIV: return "DIV";
        case DIOCPU_OP_DIVS: return "DIVS";

        case DIOCPU_OP_AND: return "AND";
        case DIOCPU_OP_OR: return "OR";
        case DIOCPU_OP_XOR: return "XOR";
        case DIOCPU_OP_NOT: return "NOT";

        case DIOCPU_OP_CMP: return "CMP";

        case DIOCPU_OP_JZ: return "JZ";
        case DIOCPU_OP_JNZ: return "JNZ";
        case DIOCPU_OP_JC: return "JC";
        case DIOCPU_OP_JNC: return "JNC";

        default: return "UNKNOWN";
    }
}

static int print_instruction(
    const Memory *memory,
    const DecodedInstruction *instruction,
    FILE *output
)
{
    if (fprintf(
            output,
            "%05X: ",
            (unsigned int)instruction->pc
        ) < 0) {
        return 0;
    }

    /*
     * Print raw instruction bytes.
     * Maximum instruction size is 5 bytes.
     */

    for (uint8_t i = 0; i < 5; i++) {

        if (i < instruction->length) {
            if (fprintf(
                    output,
                    "%02X ",
                    (unsigned int)
                    memory->instruction[instruction->pc + i]
                ) < 0) {
                return 0;
            }
        } else {
            if (fprintf(output, "   ") < 0) {
                return 0;
            }
        }
    }

    if (fprintf(output, "  ") < 0) {
        return 0;
    }

    const char *name = opcode_name(instruction->opcode);

    switch (instruction->format) {

        case INSTRUCTION_FORMAT_NONE:

            if (fprintf(output, "%s", name) < 0) {
                return 0;
            }

            break;


        case INSTRUCTION_FORMAT_REGISTER:

            if (fprintf(
                    output,
                    "%s R%u",
                    name,
                    instruction->reg_a
                ) < 0) {
                return 0;
            }

            break;


        case INSTRUCTION_FORMAT_REGISTER_REGISTER:

            if (fprintf(
                    output,
                    "%s R%u, R%u",
                    name,
                    instruction->reg_a,
                    instruction->reg_b
                ) < 0) {
                return 0;
            }

            break;


        case INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16:

            if (fprintf(
                    output,
                    "%s R%u, 0x%04X",
                    name,
                    instruction->reg_a,
                    instruction->immediate16
                ) < 0) {
                return 0;
            }

            break;


        case INSTRUCTION_FORMAT_REGISTER_ABSOLUTE18:

            if (fprintf(
                    output,
                    "%s R%u, [0x%05X]",
                    name,
                    instruction->reg_a,
                    (unsigned int)instruction->absolute_address
                ) < 0) {
                return 0;
            }

            break;


        case INSTRUCTION_FORMAT_ABSOLUTE18:

            if (fprintf(
                    output,
                    "%s 0x%05X",
                    name,
                    (unsigned int)instruction->absolute_address
                ) < 0) {
                return 0;
            }

            break;


        case INSTRUCTION_FORMAT_RELATIVE8:
        {
            int64_t target =
                (int64_t)instruction->next_pc +
                (int64_t)instruction->relative_offset;

            if (target < 0 ||
                target > DIOCPU_MEMORY_MAX_ADDRESS) {

                if (fprintf(
                        output,
                        "%s %+d -> <OUT OF RANGE>",
                        name,
                        (int)instruction->relative_offset
                    ) < 0) {
                    return 0;
                }

            } else {

                if (fprintf(
                        output,
                        "%s %+d -> 0x%05X",
                        name,
                        (int)instruction->relative_offset,
                        (unsigned int)target
                    ) < 0) {
                    return 0;
                }
            }

            break;
        }


        default:
            return 0;
    }

    return fprintf(output, "\n") >= 0;
}

DebuggerStatus debugger_disassemble(
    const Memory *memory,
    uint32_t start_pc,
    uint32_t byte_count,
    FILE *output,
    DebuggerResult *result
)
{
    if (memory == NULL ||
        output == NULL ||
        result == NULL) {
        return DEBUGGER_STATUS_INVALID_ARGUMENT;
    }

    *result = (DebuggerResult){0};

    if (start_pc > DIOCPU_MEMORY_SIZE ||
        byte_count > DIOCPU_MEMORY_SIZE - start_pc) {
        return DEBUGGER_STATUS_ADDRESS_RANGE;
    }

    uint32_t pc = start_pc;
    uint32_t end = start_pc + byte_count;

    DebuggerResult local = {0};

    local.decoder_status = DECODER_STATUS_OK;

    while (pc < end) {

        DecodedInstruction instruction = {0};

        DecoderStatus status = decoder_decode(
            memory,
            pc,
            &instruction
        );

        if (status != DECODER_STATUS_OK) {

            local.fault_pc = pc;
            local.decoder_status = status;

            *result = local;

            return DEBUGGER_STATUS_DECODE_ERROR;
        }

        /*
         * The requested region must end
         * on an instruction boundary.
         */

        if (instruction.next_pc > end) {

            local.fault_pc = pc;
            local.decoder_status = DECODER_STATUS_OK;

            *result = local;

            return DEBUGGER_STATUS_INCOMPLETE_INSTRUCTION;
        }

        if (!print_instruction(
                memory,
                &instruction,
                output
            ) || ferror(output)) {

            local.fault_pc = pc;

            *result = local;

            return DEBUGGER_STATUS_OUTPUT_ERROR;
        }

        pc = instruction.next_pc;

        local.instruction_count++;
    }

    local.next_pc = pc;

    *result = local;

    return DEBUGGER_STATUS_OK;
}
