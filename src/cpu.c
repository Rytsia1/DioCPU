#include "cpu.h"

#include "alu.h"
#include "control_unit.h"
#include "datapath.h"
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
        case CPU_FAULT_INVALID_OPCODE:          *vector = 0x01u; return 1;
        case CPU_FAULT_INVALID_INSTRUCTION:     *vector = 0x02u; return 1;
        case CPU_FAULT_MEMORY_ACCESS_FAULT:     *vector = 0x03u; return 1;
        case CPU_FAULT_DIVIDE_BY_ZERO:          *vector = 0x04u; return 1;
        case CPU_FAULT_ARITHMETIC_OVERFLOW:     *vector = 0x05u; return 1;
        case CPU_FAULT_STACK_OVERFLOW:          *vector = 0x06u; return 1;
        case CPU_FAULT_STACK_UNDERFLOW:         *vector = 0x07u; return 1;
        case CPU_FAULT_INSTRUCTION_FETCH_FAULT: *vector = 0x08u; return 1;
        default:                                return 0;
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
        case STACK_STATUS_OVERFLOW:              return CPU_FAULT_STACK_OVERFLOW;
        case STACK_STATUS_UNDERFLOW:             return CPU_FAULT_STACK_UNDERFLOW;
        case STACK_STATUS_INVALID_RETURN_ADDRESS:return CPU_FAULT_INVALID_INSTRUCTION;
        case STACK_STATUS_MEMORY_ERROR:          return CPU_FAULT_MEMORY_ACCESS_FAULT;
        case STACK_STATUS_INVALID_STACK_POINTER: return CPU_FAULT_MEMORY_ACCESS_FAULT;
        default:                                 return CPU_FAULT_INVALID_INSTRUCTION;
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

static CPUStatus raise_fault(DioCPU *cpu, CPUFault fault)
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
                cpu->datapath.next_pc_target = cpu->pc;
                return CPU_STATUS_OK;
            }
        }
    }

    cpu->state = CPU_STATE_FAULTED;
    cpu->fault = fault;
    cpu->fault_pc = cpu->pc;

    return CPU_STATUS_FAULTED;
}

static CPUFault map_decoder_fault(DecoderStatus status)
{
    switch (status) {
        case DECODER_STATUS_INVALID_OPCODE:          return CPU_FAULT_INVALID_OPCODE;
        case DECODER_STATUS_INVALID_INSTRUCTION:     return CPU_FAULT_INVALID_INSTRUCTION;
        case DECODER_STATUS_INSTRUCTION_FETCH_FAULT: return CPU_FAULT_INSTRUCTION_FETCH_FAULT;
        default:                                     return CPU_FAULT_INVALID_INSTRUCTION;
    }
}

static CPUFault map_alu_fault(ALUStatus status)
{
    switch (status) {
        case ALU_STATUS_DIVIDE_BY_ZERO:     return CPU_FAULT_DIVIDE_BY_ZERO;
        case ALU_STATUS_ARITHMETIC_OVERFLOW:return CPU_FAULT_ARITHMETIC_OVERFLOW;
        case ALU_STATUS_INVALID_OPERATION:  return CPU_FAULT_INVALID_INSTRUCTION;
        default:                            return CPU_FAULT_INVALID_INSTRUCTION;
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

    datapath_reset(&cpu->datapath);

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

static int service_pending_irq(DioCPU *cpu, CPUStatus *out_status)
{
    if ((cpu->flags & CPU_FLAG_IF) == 0u) {
        return 0;
    }

    for (uint32_t w = 0; w < DIOCPU_IRQ_WORDS; w++) {
        if (cpu->irq_pending[w] == 0u) {
            continue;
        }
        for (uint32_t bit = 0; bit < 32u; bit++) {
            if ((cpu->irq_pending[w] & (1u << bit)) != 0u) {
                uint8_t vec = (uint8_t)((w << 5u) | bit);
                cpu->irq_pending[w] &= ~(1u << bit);

                CPUFault irq_fault = CPU_FAULT_NONE;
                if (!enter_interrupt(cpu, vec, cpu->pc, &irq_fault)) {
                    *out_status = raise_fault(cpu, irq_fault);
                    return 1;
                }

                cpu->datapath.valid = false;
                cpu->datapath.next_pc_target = cpu->pc;
                cpu->instructions_executed++;
                *out_status = CPU_STATUS_OK;
                return 1;
            }
        }
    }

    return 0;
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

    CPUStatus irq_status = CPU_STATUS_OK;
    if (service_pending_irq(cpu, &irq_status)) {
        return irq_status;
    }

    DatapathState *dp = &cpu->datapath;

    /* Stage 1: Fetch, Decode, and Control Signal Generation */
    DecoderStatus decoder_status = datapath_fetch_decode(
        &cpu->memory,
        cpu->pc,
        cpu->flags,
        dp
    );

    if (decoder_status != DECODER_STATUS_OK) {
        return raise_fault(cpu, map_decoder_fault(decoder_status));
    }

    /* Stage 2: Register File Read & Address Generation Unit (AGU) */
    datapath_read_operands(dp, cpu->gpr, CPU_GPR_COUNT, cpu->sp);

    const ControlSignals *ctrl = &dp->ctrl;
    const DecodedInstruction *instr = &dp->ir;

    /* Sequential next_pc validation for non-retiring-control-flow opcodes */
    if (!ctrl->is_halt &&
        !ctrl->is_jump &&
        !ctrl->is_return &&
        !ctrl->is_branch) {
        if (!valid_next_pc(instr->next_pc)) {
            return raise_fault(
                cpu,
                ctrl->is_call ? CPU_FAULT_INVALID_INSTRUCTION : CPU_FAULT_INSTRUCTION_FETCH_FAULT
            );
        }
    }

    /* Stage 3: Execute, Memory, Stack, and Interrupt Control */
    if (ctrl->is_halt) {
        uint32_t next_pc = cpu->pc;
        (void)datapath_resolve_next_pc(dp, cpu->gpr, CPU_GPR_COUNT, cpu->pc, 0, 0, &next_pc);
        dp->next_pc_target = next_pc;
        cpu->pc = next_pc;
        cpu->state = CPU_STATE_HALTED;
        cpu->instructions_executed++;
        return CPU_STATUS_HALTED;
    }

    if (ctrl->sets_ivtr) {
        if (!valid_ivt_base(instr->absolute_address)) {
            return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
        }
        cpu->ivtr = instr->absolute_address;
    }

    if (ctrl->is_interrupt && !ctrl->is_return) {
        /* Software INT imm8 */
        CPUFault trap_fault = CPU_FAULT_NONE;
        if (!enter_interrupt(cpu, instr->immediate8, instr->next_pc, &trap_fault)) {
            return raise_fault(cpu, trap_fault);
        }
        dp->next_pc_target = cpu->pc;
        cpu->instructions_executed++;
        return CPU_STATUS_OK;
    }

    if (ctrl->is_return && ctrl->is_interrupt) {
        /* IRET */
        uint32_t old_sp = cpu->sp;
        uint16_t saved_flags = 0;
        uint32_t return_address = 0;

        StackStatus st = stack_pop16(&cpu->memory, &cpu->sp, &saved_flags);
        if (st != STACK_STATUS_OK) {
            cpu->sp = old_sp;
            return raise_fault(cpu, map_stack_fault(st));
        }

        st = stack_ret(&cpu->memory, &cpu->sp, &return_address);
        if (st != STACK_STATUS_OK) {
            cpu->sp = old_sp;
            return raise_fault(cpu, map_stack_fault(st));
        }

        dp->mdr = saved_flags;
        dp->bus_addr = old_sp;
        dp->next_pc_target = return_address;
        cpu->flags = saved_flags & CPU_FLAGS_ALL_MASK;
        cpu->mar = old_sp;
        cpu->pc = return_address;
        if (cpu->interrupt_nesting > 0u) {
            cpu->interrupt_nesting--;
        }
        cpu->instructions_executed++;
        return CPU_STATUS_OK;
    }

    if (ctrl->flags_write_system && !ctrl->is_interrupt) {
        if (instr->opcode == DIOCPU_OP_CLI) {
            cpu->flags &= (uint16_t)~CPU_FLAG_IF;
        } else if (instr->opcode == DIOCPU_OP_STI) {
            cpu->flags |= CPU_FLAG_IF;
        }
    }

    uint16_t new_alu_flags = cpu->flags & CPU_FLAGS_DEFINED_MASK;

    if (ctrl->alu_enable) {
        if ((instr->opcode == DIOCPU_OP_DIV || instr->opcode == DIOCPU_OP_DIVS) &&
            instr->reg_a == instr->reg_b) {
            return raise_fault(cpu, CPU_FAULT_INVALID_INSTRUCTION);
        }

        ALUStatus alu_status = datapath_run_alu(dp, cpu->flags, &new_alu_flags);
        if (alu_status != ALU_STATUS_OK) {
            return raise_fault(cpu, map_alu_fault(alu_status));
        }
    }

    if (ctrl->mem_read) {
        if (!dp->effective_addr_valid) {
            return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
        }
        int addr_ok = ctrl->mem_is_stack_frame
            ? valid_stack_data_address(cpu, dp->effective_addr)
            : valid_general_data_address(dp->effective_addr);

        if (!addr_ok) {
            return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
        }

        if (memory_data_read16(&cpu->memory, dp->effective_addr, &dp->mdr) != MEMORY_STATUS_OK) {
            return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
        }

        cpu->mar = dp->effective_addr;
    }

    if (ctrl->mem_write) {
        if (!dp->effective_addr_valid) {
            return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
        }
        int addr_ok = ctrl->mem_is_stack_frame
            ? valid_stack_data_address(cpu, dp->effective_addr)
            : valid_general_data_address(dp->effective_addr);

        if (!addr_ok) {
            return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
        }

        dp->mdr = cpu->gpr[instr->reg_a];

        if (memory_data_write16(&cpu->memory, dp->effective_addr, dp->mdr) != MEMORY_STATUS_OK) {
            return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
        }

        cpu->mar = dp->effective_addr;
    }

    uint32_t stack_ret_pc = 0;

    if (ctrl->stack_push) {
        if (ctrl->stack_is_32bit) {
            uint32_t call_target = 0;
            if (!datapath_resolve_next_pc(dp, cpu->gpr, CPU_GPR_COUNT, cpu->pc, 0, 0, &call_target)) {
                return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
            }

            StackStatus st = stack_call(&cpu->memory, &cpu->sp, instr->next_pc);
            if (st != STACK_STATUS_OK) {
                return raise_fault(cpu, map_stack_fault(st));
            }
            cpu->mar = cpu->sp;
            dp->bus_addr = cpu->sp;
        } else {
            dp->mdr = dp->alu_in_a;
            StackStatus st = stack_push16(&cpu->memory, &cpu->sp, dp->mdr);
            if (st != STACK_STATUS_OK) {
                return raise_fault(cpu, map_stack_fault(st));
            }
            cpu->mar = cpu->sp;
            dp->bus_addr = cpu->sp;
        }
    }

    if (ctrl->stack_pop) {
        uint32_t old_sp = cpu->sp;
        if (ctrl->stack_is_32bit) {
            StackStatus st = stack_ret(&cpu->memory, &cpu->sp, &stack_ret_pc);
            if (st != STACK_STATUS_OK) {
                return raise_fault(cpu, map_stack_fault(st));
            }
            cpu->mar = old_sp;
            dp->bus_addr = old_sp;
        } else {
            StackStatus st = stack_pop16(&cpu->memory, &cpu->sp, &dp->mdr);
            if (st != STACK_STATUS_OK) {
                return raise_fault(cpu, map_stack_fault(st));
            }
            cpu->mar = old_sp;
            dp->bus_addr = old_sp;
        }
    }

    /* Stage 4: Register Writeback, FLAGS Update, and PC Multiplexer */
    uint32_t resolved_next_pc = 0;
    if (!datapath_resolve_next_pc(
            dp,
            cpu->gpr,
            CPU_GPR_COUNT,
            cpu->pc,
            stack_ret_pc,
            0,
            &resolved_next_pc)) {
        return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
    }

    if (ctrl->reg_write_a) {
        cpu->gpr[instr->reg_a] = datapath_select_writeback(dp, cpu->gpr, CPU_GPR_COUNT);
    }

    if (ctrl->reg_write_b) {
        cpu->gpr[instr->reg_b] = dp->alu_rem;
    }

    if (ctrl->flags_write_alu) {
        cpu->flags = (cpu->flags & (uint16_t)~CPU_FLAGS_DEFINED_MASK) |
                     (new_alu_flags & CPU_FLAGS_DEFINED_MASK);
    }

    dp->next_pc_target = resolved_next_pc;
    cpu->pc = resolved_next_pc;
    cpu->instructions_executed++;

    return CPU_STATUS_OK;
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
        case CPU_FAULT_NONE:                    return "No fault";
        case CPU_FAULT_INVALID_OPCODE:          return "Invalid opcode";
        case CPU_FAULT_INVALID_INSTRUCTION:     return "Invalid instruction";
        case CPU_FAULT_MEMORY_ACCESS_FAULT:     return "Memory access fault";
        case CPU_FAULT_DIVIDE_BY_ZERO:          return "Divide by zero";
        case CPU_FAULT_ARITHMETIC_OVERFLOW:     return "Arithmetic overflow";
        case CPU_FAULT_STACK_OVERFLOW:          return "Stack overflow";
        case CPU_FAULT_STACK_UNDERFLOW:         return "Stack underflow";
        case CPU_FAULT_INSTRUCTION_FETCH_FAULT: return "Instruction fetch fault";
        case CPU_FAULT_UNHANDLED_INTERRUPT:     return "Unhandled interrupt";
        default:                                return "Unknown fault";
    }
}

const char *cpu_state_string(CPUState state)
{
    switch (state) {
        case CPU_STATE_RUNNING: return "RUNNING";
        case CPU_STATE_HALTED:  return "HALTED";
        case CPU_STATE_FAULTED: return "FAULTED";
        default:                return "UNKNOWN";
    }
}

const char *cpu_status_string(CPUStatus status)
{
    switch (status) {
        case CPU_STATUS_OK:                 return "OK";
        case CPU_STATUS_INVALID_ARGUMENT:   return "Invalid argument";
        case CPU_STATUS_HALTED:             return "HALTED";
        case CPU_STATUS_FAULTED:            return "FAULTED";
        case CPU_STATUS_STEP_LIMIT_REACHED: return "Step limit reached";
        default:                            return "Unknown status";
    }
}
