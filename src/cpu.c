#include "cpu.h"

#include "alu.h"
#include "decoder.h"
#include "stack.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DIOCPU_MAX_INTERRUPT_NESTING 16u

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

static int valid_stack_data_address(const DioCPU *cpu, uint32_t address)
{
    if (address > DIOCPU_STACK_TOP - 1u) {
        return 0;
    }

    if (address < cpu->sp || address < DIOCPU_STACK_BOTTOM) {
        return 0;
    }

    return 1;
}

static int valid_ivt_base(uint32_t ivtr)
{
    uint32_t table_bytes = DIOCPU_IVT_VECTOR_COUNT * DIOCPU_IVT_ENTRY_SIZE;

    if (ivtr > DIOCPU_STACK_BOTTOM) {
        return 0;
    }

    if (table_bytes > DIOCPU_STACK_BOTTOM - ivtr) {
        return 0;
    }

    return 1;
}

static int fault_to_vector(CPUFault fault, uint8_t *vector)
{
    switch (fault) {
        case CPU_FAULT_INVALID_OPCODE:        *vector = 0x01u; return 1;
        case CPU_FAULT_INVALID_INSTRUCTION:   *vector = 0x02u; return 1;
        case CPU_FAULT_MEMORY_ACCESS_FAULT:   *vector = 0x03u; return 1;
        case CPU_FAULT_DIVIDE_BY_ZERO:        *vector = 0x04u; return 1;
        case CPU_FAULT_ARITHMETIC_OVERFLOW:   *vector = 0x05u; return 1;
        case CPU_FAULT_STACK_OVERFLOW:        *vector = 0x06u; return 1;
        case CPU_FAULT_STACK_UNDERFLOW:       *vector = 0x07u; return 1;
        case CPU_FAULT_INSTRUCTION_FETCH_FAULT: *vector = 0x08u; return 1;
        default:                              return 0;
    }
}

static int lookup_ivt_handler(
    const DioCPU *cpu,
    uint8_t vector,
    uint32_t *handler_pc,
    int *has_handler
)
{
    *handler_pc = 0;
    *has_handler = 0;

    if (!valid_ivt_base(cpu->ivtr)) {
        return 0;
    }

    uint32_t entry_addr = cpu->ivtr + ((uint32_t)vector * DIOCPU_IVT_ENTRY_SIZE);
    uint16_t low = 0;
    uint16_t high = 0;

    if (memory_data_read16(&cpu->memory, entry_addr, &low) != MEMORY_STATUS_OK ||
        memory_data_read16(&cpu->memory, entry_addr + 2u, &high) != MEMORY_STATUS_OK) {
        return 0;
    }

    uint32_t raw = (uint32_t)low | ((uint32_t)high << 16u);

    if (raw == 0u) {
        return 1;
    }

    if ((raw & 0x7FFC0000u) != 0u) {
        return 0;
    }

    *handler_pc = raw & 0x3FFFFu;
    *has_handler = 1;
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

static int enter_interrupt(
    DioCPU *cpu,
    uint8_t vector,
    uint32_t return_pc,
    CPUFault *out_fault
)
{
    if (cpu->interrupt_nesting >= DIOCPU_MAX_INTERRUPT_NESTING) {
        *out_fault = CPU_FAULT_STACK_OVERFLOW;
        return 0;
    }

    uint32_t handler_pc = 0;
    int has_handler = 0;

    if (!lookup_ivt_handler(cpu, vector, &handler_pc, &has_handler) || !has_handler) {
        *out_fault = CPU_FAULT_UNHANDLED_INTERRUPT;
        return 0;
    }

    uint32_t saved_sp = cpu->sp;

    StackStatus st = stack_call(&cpu->memory, &cpu->sp, return_pc);
    if (st != STACK_STATUS_OK) {
        cpu->sp = saved_sp;
        *out_fault = map_stack_fault(st);
        return 0;
    }

    st = stack_push16(&cpu->memory, &cpu->sp, cpu->flags);
    if (st != STACK_STATUS_OK) {
        cpu->sp = saved_sp;
        *out_fault = map_stack_fault(st);
        return 0;
    }

    cpu->flags &= (uint16_t)~CPU_FLAG_IF;
    cpu->mar = cpu->sp;
    cpu->pc = handler_pc;
    cpu->interrupt_count++;
    cpu->interrupt_nesting++;
    cpu->last_interrupt_vector = vector;
    cpu->fault = CPU_FAULT_NONE;

    return 1;
}

static CPUStatus raise_fault(
    DioCPU *cpu,
    CPUFault fault
)
{
    uint8_t vec = 0;

    if (fault != CPU_FAULT_STACK_OVERFLOW &&
        fault != CPU_FAULT_UNHANDLED_INTERRUPT &&
        cpu->interrupt_nesting < DIOCPU_MAX_INTERRUPT_NESTING &&
        fault_to_vector(fault, &vec)) {

        uint32_t handler_pc = 0;
        int has_handler = 0;

        if (lookup_ivt_handler(cpu, vec, &handler_pc, &has_handler) && has_handler) {
            CPUFault trap_fault = CPU_FAULT_NONE;
            if (enter_interrupt(cpu, vec, cpu->pc, &trap_fault)) {
                return CPU_STATUS_OK;
            }
        }
    }

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

static int get_alu_operation(
    InstructionOpcode opcode,
    ALUOperation *operation
)
{
    switch (opcode) {

        case DIOCPU_OP_ADD:
        case DIOCPU_OP_ADDI:
        case DIOCPU_OP_INC:
            *operation = ALU_ADD;
            return 1;

        case DIOCPU_OP_SUB:
        case DIOCPU_OP_SUBI:
        case DIOCPU_OP_DEC:
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

        case DIOCPU_OP_SHL:
        case DIOCPU_OP_SHLI:
            *operation = ALU_SHL;
            return 1;

        case DIOCPU_OP_SHR:
        case DIOCPU_OP_SHRI:
            *operation = ALU_SHR;
            return 1;

        case DIOCPU_OP_SAR:
        case DIOCPU_OP_SARI:
            *operation = ALU_SAR;
            return 1;

        case DIOCPU_OP_CMP:
        case DIOCPU_OP_CMPI:
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

static CPUStatus execute_alu_instruction(
    DioCPU *cpu,
    const DecodedInstruction *instruction,
    uint16_t operand_b
)
{
    if (!valid_next_pc(instruction->next_pc)) {
        return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
    }

    if ((instruction->opcode == DIOCPU_OP_DIV ||
         instruction->opcode == DIOCPU_OP_DIVS) &&
        instruction->reg_a == instruction->reg_b) {
        return raise_fault(cpu, CPU_FAULT_INVALID_INSTRUCTION);
    }

    ALUOperation operation;
    if (!get_alu_operation(instruction->opcode, &operation)) {
        return raise_fault(cpu, CPU_FAULT_INVALID_INSTRUCTION);
    }

    uint16_t a = cpu->gpr[instruction->reg_a];

    ALUResult result = alu_execute(
        operation,
        a,
        operand_b,
        cpu->flags & CPU_FLAGS_DEFINED_MASK
    );

    if (result.status != ALU_STATUS_OK) {
        return raise_fault(cpu, map_alu_fault(result.status));
    }

    if (instruction->opcode == DIOCPU_OP_CMP ||
        instruction->opcode == DIOCPU_OP_CMPI) {
        /* CMP / CMPI updates FLAGS only. */
    } else if (instruction->opcode == DIOCPU_OP_DIV ||
               instruction->opcode == DIOCPU_OP_DIVS) {
        cpu->gpr[instruction->reg_a] = result.result;
        cpu->gpr[instruction->reg_b] = result.remainder;
    } else {
        cpu->gpr[instruction->reg_a] = result.result;
    }

    cpu->flags = (cpu->flags & (uint16_t)~CPU_FLAGS_DEFINED_MASK) |
                 (result.flags & CPU_FLAGS_DEFINED_MASK);

    cpu->pc = instruction->next_pc;
    cpu->instructions_executed++;

    return CPU_STATUS_OK;
}

static int evaluate_branch_condition(InstructionOpcode opcode, uint16_t flags)
{
    int zf = (flags & CPU_FLAG_ZF) != 0;
    int cf = (flags & CPU_FLAG_CF) != 0;
    int sf = (flags & CPU_FLAG_SF) != 0;
    int of = (flags & CPU_FLAG_OF) != 0;

    switch (opcode) {
        case DIOCPU_OP_JZ:  return zf;
        case DIOCPU_OP_JNZ: return !zf;
        case DIOCPU_OP_JC:  return cf;
        case DIOCPU_OP_JNC: return !cf;
        case DIOCPU_OP_JS:  return sf;
        case DIOCPU_OP_JNS: return !sf;
        case DIOCPU_OP_JO:  return of;
        case DIOCPU_OP_JNO: return !of;
        case DIOCPU_OP_JL:  return sf != of;
        case DIOCPU_OP_JGE: return sf == of;
        case DIOCPU_OP_JLE: return zf || (sf != of);
        case DIOCPU_OP_JG:  return !zf && (sf == of);
        default:            return 0;
    }
}

CPUStatus cpu_init(DioCPU *cpu)
{
    if (cpu == NULL) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    MemoryStatus status = memory_init(&cpu->memory);

    if (status != MEMORY_STATUS_OK) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    return cpu_reset(cpu);
}

CPUStatus cpu_reset(DioCPU *cpu)
{
    if (cpu == NULL) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    memset(cpu->gpr, 0, sizeof(cpu->gpr));

    cpu->pc = 0;
    cpu->sp = DIOCPU_STACK_INITIAL_SP;
    cpu->mar = 0;
    cpu->ivtr = DIOCPU_DEFAULT_IVT_BASE;

    cpu->flags = 0;

    cpu->state = CPU_STATE_RUNNING;

    cpu->fault = CPU_FAULT_NONE;
    cpu->fault_pc = 0;

    cpu->instructions_executed = 0;

    memset(cpu->irq_pending, 0, sizeof(cpu->irq_pending));
    cpu->interrupt_count = 0;
    cpu->interrupt_nesting = 0;
    cpu->last_interrupt_vector = 0;

    return CPU_STATUS_OK;
}

CPUStatus cpu_raise_irq(DioCPU *cpu, uint8_t vector)
{
    if (cpu == NULL) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    cpu->irq_pending[vector >> 5u] |= (1u << (vector & 31u));
    return CPU_STATUS_OK;
}

CPUStatus cpu_clear_irq(DioCPU *cpu, uint8_t vector)
{
    if (cpu == NULL) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    cpu->irq_pending[vector >> 5u] &= ~(1u << (vector & 31u));
    return CPU_STATUS_OK;
}

CPUStatus cpu_register_interrupt_handler(
    DioCPU *cpu,
    uint8_t vector,
    uint32_t handler_pc
)
{
    if (cpu == NULL || !valid_address(handler_pc) || !valid_ivt_base(cpu->ivtr)) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    uint32_t entry = DIOCPU_IVT_PRESENT_BIT | (handler_pc & 0x3FFFFu);
    uint32_t addr = cpu->ivtr + ((uint32_t)vector * DIOCPU_IVT_ENTRY_SIZE);

    if (memory_data_write16(&cpu->memory, addr, (uint16_t)(entry & 0xFFFFu)) != MEMORY_STATUS_OK ||
        memory_data_write16(&cpu->memory, addr + 2u, (uint16_t)(entry >> 16u)) != MEMORY_STATUS_OK) {
        return CPU_STATUS_INVALID_ARGUMENT;
    }

    return CPU_STATUS_OK;
}

CPUStatus cpu_step(DioCPU *cpu)
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

    /* Service pending maskable hardware interrupts before next fetch. */
    if ((cpu->flags & CPU_FLAG_IF) != 0u) {
        for (uint32_t w = 0; w < DIOCPU_IRQ_WORDS; w++) {
            if (cpu->irq_pending[w] != 0u) {
                for (uint32_t bit = 0; bit < 32u; bit++) {
                    if ((cpu->irq_pending[w] & (1u << bit)) != 0u) {
                        uint8_t vec = (uint8_t)((w << 5u) | bit);
                        cpu->irq_pending[w] &= ~(1u << bit);

                        CPUFault irq_fault = CPU_FAULT_NONE;
                        if (!enter_interrupt(cpu, vec, cpu->pc, &irq_fault)) {
                            return raise_fault(cpu, irq_fault);
                        }

                        cpu->instructions_executed++;
                        return CPU_STATUS_OK;
                    }
                }
            }
        }
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
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }
            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;

        case DIOCPU_OP_HLT:
            if (valid_next_pc(instruction.next_pc)) {
                cpu->pc = instruction.next_pc;
            }
            cpu->state = CPU_STATE_HALTED;
            cpu->instructions_executed++;
            return CPU_STATUS_HALTED;

        case DIOCPU_OP_CLI:
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }
            cpu->flags &= (uint16_t)~CPU_FLAG_IF;
            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;

        case DIOCPU_OP_STI:
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }
            cpu->flags |= CPU_FLAG_IF;
            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;

        case DIOCPU_OP_SETIVT:
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }
            if (!valid_ivt_base(instruction.absolute_address)) {
                return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
            }
            cpu->ivtr = instruction.absolute_address;
            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;

        case DIOCPU_OP_INT:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }

            CPUFault trap_fault = CPU_FAULT_NONE;
            if (!enter_interrupt(
                    cpu,
                    instruction.immediate8,
                    instruction.next_pc,
                    &trap_fault)) {
                return raise_fault(cpu, trap_fault);
            }

            cpu->instructions_executed++;
            return CPU_STATUS_OK;
        }

        case DIOCPU_OP_IRET:
        {
            uint32_t old_sp = cpu->sp;
            uint16_t saved_flags = 0;
            uint32_t return_address = 0;

            StackStatus status = stack_pop16(
                &cpu->memory,
                &cpu->sp,
                &saved_flags
            );

            if (status != STACK_STATUS_OK) {
                cpu->sp = old_sp;
                return raise_fault(cpu, map_stack_fault(status));
            }

            status = stack_ret(
                &cpu->memory,
                &cpu->sp,
                &return_address
            );

            if (status != STACK_STATUS_OK) {
                cpu->sp = old_sp;
                return raise_fault(cpu, map_stack_fault(status));
            }

            cpu->flags = saved_flags & CPU_FLAGS_ALL_MASK;
            cpu->mar = old_sp;
            cpu->pc = return_address;
            if (cpu->interrupt_nesting > 0u) {
                cpu->interrupt_nesting--;
            }
            cpu->instructions_executed++;
            return CPU_STATUS_OK;
        }

        case DIOCPU_OP_LOADI:
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }
            cpu->gpr[instruction.reg_a] = instruction.immediate16;
            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;

        case DIOCPU_OP_MOV:
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }
            cpu->gpr[instruction.reg_a] = cpu->gpr[instruction.reg_b];
            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;

        case DIOCPU_OP_LEA:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }
            int32_t eff = (int32_t)cpu->gpr[instruction.reg_b] + (int32_t)instruction.offset16;
            cpu->gpr[instruction.reg_a] = (uint16_t)eff;
            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;
        }

        case DIOCPU_OP_LOAD:
        case DIOCPU_OP_LOADR:
        case DIOCPU_OP_LOADX:
        case DIOCPU_OP_LOADSP:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }

            int64_t raw_addr = 0;
            int is_stack_op = (instruction.opcode == DIOCPU_OP_LOADSP);

            if (instruction.opcode == DIOCPU_OP_LOAD) {
                raw_addr = (int64_t)instruction.absolute_address;
            } else if (instruction.opcode == DIOCPU_OP_LOADR) {
                raw_addr = (int64_t)cpu->gpr[instruction.reg_b] + (int64_t)instruction.offset16;
            } else if (instruction.opcode == DIOCPU_OP_LOADX) {
                raw_addr = (int64_t)cpu->gpr[instruction.reg_b] + (int64_t)cpu->gpr[instruction.reg_c];
            } else {
                raw_addr = (int64_t)cpu->sp + (int64_t)instruction.offset16;
            }

            if (raw_addr < 0 || raw_addr > (int64_t)DIOCPU_MEMORY_MAX_ADDRESS) {
                return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
            }

            uint32_t address = (uint32_t)raw_addr;
            int addr_ok = is_stack_op
                ? valid_stack_data_address(cpu, address)
                : valid_general_data_address(address);

            if (!addr_ok) {
                return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
            }

            uint16_t value = 0;
            if (memory_data_read16(&cpu->memory, address, &value) != MEMORY_STATUS_OK) {
                return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
            }

            cpu->gpr[instruction.reg_a] = value;
            cpu->mar = address;
            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;
        }

        case DIOCPU_OP_STORE:
        case DIOCPU_OP_STORER:
        case DIOCPU_OP_STOREX:
        case DIOCPU_OP_STORESP:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }

            int64_t raw_addr = 0;
            int is_stack_op = (instruction.opcode == DIOCPU_OP_STORESP);

            if (instruction.opcode == DIOCPU_OP_STORE) {
                raw_addr = (int64_t)instruction.absolute_address;
            } else if (instruction.opcode == DIOCPU_OP_STORER) {
                raw_addr = (int64_t)cpu->gpr[instruction.reg_b] + (int64_t)instruction.offset16;
            } else if (instruction.opcode == DIOCPU_OP_STOREX) {
                raw_addr = (int64_t)cpu->gpr[instruction.reg_b] + (int64_t)cpu->gpr[instruction.reg_c];
            } else {
                raw_addr = (int64_t)cpu->sp + (int64_t)instruction.offset16;
            }

            if (raw_addr < 0 || raw_addr > (int64_t)DIOCPU_MEMORY_MAX_ADDRESS) {
                return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
            }

            uint32_t address = (uint32_t)raw_addr;
            int addr_ok = is_stack_op
                ? valid_stack_data_address(cpu, address)
                : valid_general_data_address(address);

            if (!addr_ok) {
                return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
            }

            uint16_t value = cpu->gpr[instruction.reg_a];
            if (memory_data_write16(&cpu->memory, address, value) != MEMORY_STATUS_OK) {
                return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
            }

            cpu->mar = address;
            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;
        }

        case DIOCPU_OP_PUSH:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }

            StackStatus status = stack_push16(
                &cpu->memory,
                &cpu->sp,
                cpu->gpr[instruction.reg_a]
            );

            if (status != STACK_STATUS_OK) {
                return raise_fault(cpu, map_stack_fault(status));
            }

            cpu->mar = cpu->sp;
            cpu->pc = instruction.next_pc;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;
        }

        case DIOCPU_OP_POP:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }

            uint32_t old_sp = cpu->sp;
            uint16_t value = 0;

            StackStatus status = stack_pop16(
                &cpu->memory,
                &cpu->sp,
                &value
            );

            if (status != STACK_STATUS_OK) {
                return raise_fault(cpu, map_stack_fault(status));
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
                return raise_fault(cpu, CPU_FAULT_INVALID_INSTRUCTION);
            }

            StackStatus status = stack_call(
                &cpu->memory,
                &cpu->sp,
                instruction.next_pc
            );

            if (status != STACK_STATUS_OK) {
                return raise_fault(cpu, map_stack_fault(status));
            }

            cpu->mar = cpu->sp;
            cpu->pc = instruction.absolute_address;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;
        }

        case DIOCPU_OP_CALLR:
        {
            if (!valid_next_pc(instruction.next_pc)) {
                return raise_fault(cpu, CPU_FAULT_INVALID_INSTRUCTION);
            }

            uint32_t target = (uint32_t)cpu->gpr[instruction.reg_a];
            if (!valid_address(target)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }

            StackStatus status = stack_call(
                &cpu->memory,
                &cpu->sp,
                instruction.next_pc
            );

            if (status != STACK_STATUS_OK) {
                return raise_fault(cpu, map_stack_fault(status));
            }

            cpu->mar = cpu->sp;
            cpu->pc = target;
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
                return raise_fault(cpu, map_stack_fault(status));
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
        case DIOCPU_OP_SHL:
        case DIOCPU_OP_SHR:
        case DIOCPU_OP_SAR:
        case DIOCPU_OP_CMP:
            return execute_alu_instruction(
                cpu,
                &instruction,
                cpu->gpr[instruction.reg_b]
            );

        case DIOCPU_OP_ADDI:
        case DIOCPU_OP_SUBI:
        case DIOCPU_OP_CMPI:
            return execute_alu_instruction(
                cpu,
                &instruction,
                instruction.immediate16
            );

        case DIOCPU_OP_INC:
        case DIOCPU_OP_DEC:
            return execute_alu_instruction(
                cpu,
                &instruction,
                1u
            );

        case DIOCPU_OP_SHLI:
        case DIOCPU_OP_SHRI:
        case DIOCPU_OP_SARI:
            return execute_alu_instruction(
                cpu,
                &instruction,
                (uint16_t)instruction.immediate8
            );

        case DIOCPU_OP_JMP:
            cpu->pc = instruction.absolute_address;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;

        case DIOCPU_OP_JMPR:
            cpu->pc = (uint32_t)cpu->gpr[instruction.reg_a];
            cpu->instructions_executed++;
            return CPU_STATUS_OK;

        case DIOCPU_OP_JZ:
        case DIOCPU_OP_JNZ:
        case DIOCPU_OP_JC:
        case DIOCPU_OP_JNC:
        case DIOCPU_OP_JS:
        case DIOCPU_OP_JNS:
        case DIOCPU_OP_JO:
        case DIOCPU_OP_JNO:
        case DIOCPU_OP_JL:
        case DIOCPU_OP_JGE:
        case DIOCPU_OP_JLE:
        case DIOCPU_OP_JG:
        {
            int taken = evaluate_branch_condition(instruction.opcode, cpu->flags);
            int64_t target = instruction.next_pc;

            if (taken) {
                target += instruction.relative_offset;
            }

            if (target < 0 || target > (int64_t)DIOCPU_MEMORY_MAX_ADDRESS) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }

            cpu->pc = (uint32_t)target;
            cpu->instructions_executed++;
            return CPU_STATUS_OK;
        }

        default:
            return raise_fault(cpu, CPU_FAULT_INVALID_INSTRUCTION);
    }
}

CPUStatus cpu_run(
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

        CPUStatus status = cpu_step(cpu);

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

const char *cpu_fault_string(CPUFault fault)
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

        case CPU_FAULT_UNHANDLED_INTERRUPT:
            return "Unhandled interrupt";

        default:
            return "Unknown fault";
    }
}

const char *cpu_state_string(CPUState state)
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

const char *cpu_status_string(CPUStatus status)
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
