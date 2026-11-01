#include "cpu_v2.h"

#include "alu.h"
#include "decoder.h"
#include "stack.h"

#include <stddef.h>
#include <string.h>
#include <stdint.h>

static CPUStatus raise_fault(
    DioCPU *cpu,
    CPUFault fault
)
{
    cpu->state = CPU_STATE_FAULTED;
    cpu->fault = fault;
    cpu->fault_pc = cpu->pc;

    return CPU_STATUS_FAULTED;
}

static CPUFault map_decoder_fault(
    DecoderStatus status
)
{
    switch (status) {

        case DECODER_STATUS_INVALID_OPCODE:
            return CPU_FAULT_INVALID_OPCODE;

        case DECODER_STATUS_INVALID_INSTRUCTION:
            return CPU_FAULT_INVALID_INSTRUCTION;

        case DECODER_STATUS_INSTRUCTION_FETCH_FAULT:
            return CPU_FAULT_INSTRUCTION_FETCH_FAULT;

        default:
            return CPU_FAULT_INVALID_INSTRUCTION;
    }
}

static int valid_address(uint32_t address)
{
    return address <= DIOCPU_MEMORY_MAX_ADDRESS;
}


static int valid_next_pc(uint32_t next_pc)
{
    return valid_address(next_pc);
}

static int valid_general_data_address(uint32_t address)
{
    if (address > DIOCPU_MEMORY_MAX_ADDRESS - 1u) {
        return 0;
    }

    uint32_t last_address = address + 1u;

    if (address >= DIOCPU_STACK_BOTTOM ||
        last_address >= DIOCPU_STACK_BOTTOM) {
        return 0;
    }

    return 1;
}

static CPUFault map_stack_fault(StackStatus status)
{
    switch (status) {

        case STACK_STATUS_OVERFLOW:
            return CPU_FAULT_STACK_OVERFLOW;

        case STACK_STATUS_UNDERFLOW:
            return CPU_FAULT_STACK_UNDERFLOW;

        case STACK_STATUS_INVALID_RETURN_ADDRESS:
            return CPU_FAULT_INVALID_INSTRUCTION;

        case STACK_STATUS_MEMORY_ERROR:
            return CPU_FAULT_MEMORY_ACCESS_FAULT;

        case STACK_STATUS_INVALID_STACK_POINTER:
            return CPU_FAULT_MEMORY_ACCESS_FAULT;

        default:
            return CPU_FAULT_INVALID_INSTRUCTION;
    }
}

static int get_alu_operation(
    InstructionOpcode opcode,
    ALUOperation *operation
)
{
    switch (opcode) {

        case DIOCPU_OP_ADD:
            *operation = ALU_ADD;
            return 1;

        case DIOCPU_OP_SUB:
            *operation = ALU_SUB;
            return 1;

        case DIOCPU_OP_MUL:
            *operation = ALU_MUL;
            return 1;

        case DIOCPU_OP_MULS:
            *operation = ALU_MULS;
            return 1;

        case DIOCPU_OP_DIV:
            *operation = ALU_DIV;
            return 1;

        case DIOCPU_OP_DIVS:
            *operation = ALU_DIVS;
            return 1;

        case DIOCPU_OP_AND:
            *operation = ALU_AND;
            return 1;

        case DIOCPU_OP_OR:
            *operation = ALU_OR;
            return 1;

        case DIOCPU_OP_XOR:
            *operation = ALU_XOR;
            return 1;

        case DIOCPU_OP_NOT:
            *operation = ALU_NOT;
            return 1;

        case DIOCPU_OP_CMP:
            *operation = ALU_CMP;
            return 1;

        default:
            return 0;
    }
}

static CPUFault map_alu_fault(ALUStatus status)
{
    switch (status) {

        case ALU_STATUS_DIVIDE_BY_ZERO:
            return CPU_FAULT_DIVIDE_BY_ZERO;

        case ALU_STATUS_ARITHMETIC_OVERFLOW:
            return CPU_FAULT_ARITHMETIC_OVERFLOW;

        case ALU_STATUS_INVALID_OPERATION:
            return CPU_FAULT_INVALID_INSTRUCTION;

        default:
            return CPU_FAULT_INVALID_INSTRUCTION;
    }
}

CPUStatus cpu_v2_init(DioCPU *cpu)
{
    if (cpu == NULL) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    MemoryStatus status = memory_init(&cpu->memory);

    if (status != MEMORY_STATUS_OK) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    return cpu_v2_reset(cpu);
}


CPUStatus cpu_v2_reset(DioCPU *cpu)
{
    if (cpu == NULL) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    memset(cpu->gpr, 0, sizeof(cpu->gpr));

    cpu->pc = 0;
    cpu->sp = DIOCPU_STACK_INITIAL_SP;
    cpu->mar = 0;

    cpu->flags = 0;

    cpu->state = CPU_STATE_RUNNING;

    cpu->fault = CPU_FAULT_NONE;
    cpu->fault_pc = 0;

    cpu->instructions_executed = 0;

    return CPU_STATUS_OK;
}

CPUStatus cpu_v2_step(DioCPU *cpu)
{
    if (cpu == NULL) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    if (cpu->state == CPU_STATE_HALTED) {
        return CPU_STATUS_HALTED;
    }

    if (cpu->state == CPU_STATE_FAULTED) {
        return CPU_STATUS_FAULTED;
    }

    DecodedInstruction instruction = {0};

    DecoderStatus decoder_status = decoder_decode(
        &cpu->memory,
        cpu->pc,
        &instruction
    );

    if (decoder_status != DECODER_STATUS_OK) {
        return raise_fault(
            cpu,
            map_decoder_fault(decoder_status)
        );
    }

switch (instruction.opcode) {

        case DIOCPU_OP_NOP:

            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_INSTRUCTION_FETCH_FAULT
                );
            }

            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;

            return CPU_STATUS_OK;


        case DIOCPU_OP_HLT:

            /*
             * Normally PC moves to the next instruction.
             * If HLT occupies the last memory byte, retain
             * the current PC because 0x40000 is not valid.
             */

            if (valid_next_pc(instruction.next_pc)) {
                cpu->pc = instruction.next_pc;
            }

            cpu->state = CPU_STATE_HALTED;
            cpu->instructions_executed++;

            return CPU_STATUS_HALTED;

case DIOCPU_OP_LOADI:

            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_INSTRUCTION_FETCH_FAULT
                );
            }

            cpu->gpr[instruction.reg_a] =
                instruction.immediate16;

            cpu->pc = instruction.next_pc;

            cpu->instructions_executed++;

            return CPU_STATUS_OK;

case DIOCPU_OP_LOAD:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_INSTRUCTION_FETCH_FAULT
                );
            }

            uint32_t address = instruction.absolute_address;

            if (!valid_general_data_address(address)) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_MEMORY_ACCESS_FAULT
                );
            }

            uint16_t value = 0;

            MemoryStatus status = memory_data_read16(
                &cpu->memory,
                address,
                &value
            );

            if (status != MEMORY_STATUS_OK) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_MEMORY_ACCESS_FAULT
                );
            }

            cpu->gpr[instruction.reg_a] = value;
            cpu->mar = address;
            cpu->pc = instruction.next_pc;

            cpu->instructions_executed++;

            return CPU_STATUS_OK;
        }

case DIOCPU_OP_STORE:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_INSTRUCTION_FETCH_FAULT
                );
            }

            uint32_t address = instruction.absolute_address;

            if (!valid_general_data_address(address)) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_MEMORY_ACCESS_FAULT
                );
            }

            uint16_t value = cpu->gpr[instruction.reg_a];

            MemoryStatus status = memory_data_write16(
                &cpu->memory,
                address,
                value
            );

            if (status != MEMORY_STATUS_OK) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_MEMORY_ACCESS_FAULT
                );
            }

            cpu->mar = address;
            cpu->pc = instruction.next_pc;

            cpu->instructions_executed++;

            return CPU_STATUS_OK;
        }

case DIOCPU_OP_PUSH:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_INSTRUCTION_FETCH_FAULT
                );
            }

            StackStatus status = stack_push16(
                &cpu->memory,
                &cpu->sp,
                cpu->gpr[instruction.reg_a]
            );

            if (status != STACK_STATUS_OK) {
                return raise_fault(
                    cpu,
                    map_stack_fault(status)
                );
            }

            cpu->mar = cpu->sp;
            cpu->pc = instruction.next_pc;

            cpu->instructions_executed++;

            return CPU_STATUS_OK;
        }


        case DIOCPU_OP_POP:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_INSTRUCTION_FETCH_FAULT
                );
            }

            uint32_t old_sp = cpu->sp;
            uint16_t value = 0;

            StackStatus status = stack_pop16(
                &cpu->memory,
                &cpu->sp,
                &value
            );

            if (status != STACK_STATUS_OK) {
                return raise_fault(
                    cpu,
                    map_stack_fault(status)
                );
            }

            cpu->gpr[instruction.reg_a] = value;

            cpu->mar = old_sp;
            cpu->pc = instruction.next_pc;

            cpu->instructions_executed++;

            return CPU_STATUS_OK;
        }

case DIOCPU_OP_CALL:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_INVALID_INSTRUCTION
                );
            }

            StackStatus status = stack_call(
                &cpu->memory,
                &cpu->sp,
                instruction.next_pc
            );

            if (status != STACK_STATUS_OK) {
                return raise_fault(
                    cpu,
                    map_stack_fault(status)
                );
            }

            cpu->mar = cpu->sp;
            cpu->pc = instruction.absolute_address;

            cpu->instructions_executed++;

            return CPU_STATUS_OK;
        }


        case DIOCPU_OP_RET:
        {
            uint32_t old_sp = cpu->sp;
            uint32_t return_address = 0;

            StackStatus status = stack_ret(
                &cpu->memory,
                &cpu->sp,
                &return_address
            );

            if (status != STACK_STATUS_OK) {
                return raise_fault(
                    cpu,
                    map_stack_fault(status)
                );
            }

            cpu->mar = old_sp;
            cpu->pc = return_address;

            cpu->instructions_executed++;

            return CPU_STATUS_OK;
        }

case DIOCPU_OP_ADD:
        case DIOCPU_OP_SUB:
        case DIOCPU_OP_MUL:
        case DIOCPU_OP_MULS:
        case DIOCPU_OP_DIV:
        case DIOCPU_OP_DIVS:

        case DIOCPU_OP_AND:
        case DIOCPU_OP_OR:
        case DIOCPU_OP_XOR:
        case DIOCPU_OP_NOT:

        case DIOCPU_OP_CMP:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(
                    cpu,
                    CPU_FAULT_INSTRUCTION_FETCH_FAULT
                );
            }

            /*
             * Division writes two destinations:
             * quotient to RA and remainder to RB.
             * Therefore RA and RB must be different.
             */

            if ((instruction.opcode == DIOCPU_OP_DIV ||
                 instruction.opcode == DIOCPU_OP_DIVS) &&
                instruction.reg_a == instruction.reg_b) {

                return raise_fault(
                    cpu,
                    CPU_FAULT_INVALID_INSTRUCTION
                );
            }

            ALUOperation operation;

            if (!get_alu_operation(
                    instruction.opcode,
                    &operation
                )) {

                return raise_fault(
                    cpu,
                    CPU_FAULT_INVALID_INSTRUCTION
                );
            }

            uint16_t a = cpu->gpr[instruction.reg_a];
            uint16_t b = cpu->gpr[instruction.reg_b];

            ALUResult result = alu_execute(
                operation,
                a,
                b,
                cpu->flags
            );

            if (result.status != ALU_STATUS_OK) {
                return raise_fault(
                    cpu,
                    map_alu_fault(result.status)
                );
            }

            /*
             * Commit register changes only after
             * ALU execution succeeds.
             */

            if (instruction.opcode == DIOCPU_OP_CMP) {

                /*
                 * CMP updates FLAGS only.
                 */

            } else if (
                instruction.opcode == DIOCPU_OP_DIV ||
                instruction.opcode == DIOCPU_OP_DIVS
            ) {

                cpu->gpr[instruction.reg_a] = result.result;

                cpu->gpr[instruction.reg_b] = result.remainder;

            } else {

                cpu->gpr[instruction.reg_a] = result.result;
            }

            cpu->flags =
                result.flags & CPU_FLAGS_DEFINED_MASK;

            cpu->pc = instruction.next_pc;

            cpu->instructions_executed++;

            return CPU_STATUS_OK;
        }

case DIOCPU_OP_JMP:

            cpu->pc = instruction.absolute_address;

            cpu->instructions_executed++;

            return CPU_STATUS_OK;


        case DIOCPU_OP_JZ:
        case DIOCPU_OP_JNZ:
        case DIOCPU_OP_JC:
        case DIOCPU_OP_JNC:
        {
            int taken = 0;

            switch (instruction.opcode) {

                case DIOCPU_OP_JZ:
                    taken = (cpu->flags & CPU_FLAG_ZF) != 0;
                    break;

                case DIOCPU_OP_JNZ:
                    taken = (cpu->flags & CPU_FLAG_ZF) == 0;
                    break;

                case DIOCPU_OP_JC:
                    taken = (cpu->flags & CPU_FLAG_CF) != 0;
                    break;

                case DIOCPU_OP_JNC:
                    taken = (cpu->flags & CPU_FLAG_CF) == 0;
                    break;

                default:
                    break;
            }

            int64_t target = instruction.next_pc;

            if (taken) {
                target += instruction.relative_offset;
            }

            if (target < 0 ||
                target > DIOCPU_MEMORY_MAX_ADDRESS) {

                return raise_fault(
                    cpu,
                    CPU_FAULT_INSTRUCTION_FETCH_FAULT
                );
            }

            cpu->pc = (uint32_t)target;

            cpu->instructions_executed++;

            return CPU_STATUS_OK;
        }


        default:

            return raise_fault(
                cpu,
                CPU_FAULT_INVALID_INSTRUCTION
            );
    }
}

CPUStatus cpu_v2_run(
    DioCPU *cpu,
    uint64_t max_steps,
    uint64_t *steps_executed
)
{
    if (cpu == NULL || steps_executed == NULL) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    *steps_executed = 0;

    if (cpu->state == CPU_STATE_HALTED) {
        return CPU_STATUS_HALTED;
    }

    if (cpu->state == CPU_STATE_FAULTED) {
        return CPU_STATUS_FAULTED;
    }

    uint64_t initial_count = cpu->instructions_executed;

    for (uint64_t i = 0; i < max_steps; i++) {

        CPUStatus status = cpu_v2_step(cpu);

        if (status != CPU_STATUS_OK) {

            *steps_executed =
                cpu->instructions_executed - initial_count;

            return status;
        }
    }

    *steps_executed =
        cpu->instructions_executed - initial_count;

    return CPU_STATUS_STEP_LIMIT_REACHED;
}

const char *cpu_v2_fault_string(CPUFault fault)
{
    switch (fault) {

        case CPU_FAULT_NONE:
            return "No fault";

        case CPU_FAULT_INVALID_OPCODE:
            return "Invalid opcode";

        case CPU_FAULT_INVALID_INSTRUCTION:
            return "Invalid instruction";

        case CPU_FAULT_MEMORY_ACCESS_FAULT:
            return "Memory access fault";

        case CPU_FAULT_DIVIDE_BY_ZERO:
            return "Divide by zero";

        case CPU_FAULT_ARITHMETIC_OVERFLOW:
            return "Arithmetic overflow";

        case CPU_FAULT_STACK_OVERFLOW:
            return "Stack overflow";

        case CPU_FAULT_STACK_UNDERFLOW:
            return "Stack underflow";

        case CPU_FAULT_INSTRUCTION_FETCH_FAULT:
            return "Instruction fetch fault";

        default:
            return "Unknown fault";
    }
}


const char *cpu_v2_state_string(CPUState state)
{
    switch (state) {

        case CPU_STATE_RUNNING:
            return "RUNNING";

        case CPU_STATE_HALTED:
            return "HALTED";

        case CPU_STATE_FAULTED:
            return "FAULTED";

        default:
            return "UNKNOWN";
    }
}


const char *cpu_v2_status_string(CPUStatus status)
{
    switch (status) {

        case CPU_STATUS_OK:
            return "OK";

        case CPU_STATUS_INVALID_ARGUMENT:
            return "Invalid argument";

        case CPU_STATUS_HALTED:
            return "HALTED";

        case CPU_STATUS_FAULTED:
            return "FAULTED";

        case CPU_STATUS_STEP_LIMIT_REACHED:
            return "Step limit reached";

        default:
            return "Unknown status";
    }
}
