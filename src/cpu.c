#include "cpu.h"

#include "alu.h"
#include "control_unit.h"
#include "cycle_engine.h"
#include "datapath.h"
#include "decoder.h"
#include "microop.h"
#include "stack.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define DIOCPU_MAX_INTERRUPT_NESTING 16u

static int valid_address(uint32_t address) {
  return address <= DIOCPU_MEMORY_MAX_ADDRESS;
}

static int valid_next_pc(uint32_t next_pc) { return valid_address(next_pc); }

static int valid_general_data_address(uint32_t address) {
  if (address > DIOCPU_MEMORY_MAX_ADDRESS - 1u) {
    return 0;
  }

  uint32_t last_address = address + 1u;

  if (address >= DIOCPU_STACK_BOTTOM || last_address >= DIOCPU_STACK_BOTTOM) {
    return 0;
  }

  return 1;
}

static int valid_stack_data_address(const DioCPU *cpu, uint32_t address) {
  if (address > DIOCPU_STACK_TOP - 1u) {
    return 0;
  }

  if (address < cpu->sp || address < DIOCPU_STACK_BOTTOM) {
    return 0;
  }

  return 1;
}

static int valid_ivt_base(uint32_t ivtr) {
  uint32_t table_bytes = DIOCPU_IVT_VECTOR_COUNT * DIOCPU_IVT_ENTRY_SIZE;

  if (ivtr > DIOCPU_STACK_BOTTOM) {
    return 0;
  }

  if (table_bytes > DIOCPU_STACK_BOTTOM - ivtr) {
    return 0;
  }

  return 1;
}

static int fault_to_vector(CPUFault fault, uint8_t *vector) {
  switch (fault) {
  case CPU_FAULT_INVALID_OPCODE:
    *vector = 0x01u;
    return 1;
  case CPU_FAULT_INVALID_INSTRUCTION:
    *vector = 0x02u;
    return 1;
  case CPU_FAULT_MEMORY_ACCESS_FAULT:
    *vector = 0x03u;
    return 1;
  case CPU_FAULT_DIVIDE_BY_ZERO:
    *vector = 0x04u;
    return 1;
  case CPU_FAULT_ARITHMETIC_OVERFLOW:
    *vector = 0x05u;
    return 1;
  case CPU_FAULT_STACK_OVERFLOW:
    *vector = 0x06u;
    return 1;
  case CPU_FAULT_STACK_UNDERFLOW:
    *vector = 0x07u;
    return 1;
  case CPU_FAULT_INSTRUCTION_FETCH_FAULT:
    *vector = 0x08u;
    return 1;
  default:
    return 0;
  }
}

static int lookup_ivt_handler(const DioCPU *cpu, uint8_t vector,
                              uint32_t *handler_pc, int *has_handler) {
  *handler_pc = 0;
  *has_handler = 0;

  if (!valid_ivt_base(cpu->ivtr)) {
    return 0;
  }

  uint32_t entry_addr = cpu->ivtr + ((uint32_t)vector * DIOCPU_IVT_ENTRY_SIZE);
  uint16_t low = 0;
  uint16_t high = 0;

  if (memory_data_read16(&cpu->memory, entry_addr, &low) != MEMORY_STATUS_OK ||
      memory_data_read16(&cpu->memory, entry_addr + 2u, &high) !=
          MEMORY_STATUS_OK) {
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

static CPUFault map_stack_fault(StackStatus status) {
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

static int enter_interrupt(DioCPU *cpu, uint8_t vector, uint32_t return_pc,
                           CPUFault *out_fault) {
  if (cpu->interrupt_nesting >= DIOCPU_MAX_INTERRUPT_NESTING) {
    *out_fault = CPU_FAULT_STACK_OVERFLOW;
    return 0;
  }

  uint32_t handler_pc = 0;
  int has_handler = 0;

  if (!lookup_ivt_handler(cpu, vector, &handler_pc, &has_handler) ||
      !has_handler) {
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

static CPUStatus raise_fault(DioCPU *cpu, CPUFault fault) {
  if (cpu->uop.active) {
    cpu->sp = cpu->uop.saved_sp;
    cpu->flags = cpu->uop.saved_flags;
    cpu->uop.active = false;
  }
  cpu->cycle.uop_in_flight = false;
  cpu->cycle.uop_cycles_remaining = 0u;

  uint8_t vec = 0;

  if (fault != CPU_FAULT_STACK_OVERFLOW &&
      fault != CPU_FAULT_UNHANDLED_INTERRUPT &&
      cpu->interrupt_nesting < DIOCPU_MAX_INTERRUPT_NESTING &&
      fault_to_vector(fault, &vec)) {

    uint32_t handler_pc = 0;
    int has_handler = 0;

    if (lookup_ivt_handler(cpu, vec, &handler_pc, &has_handler) &&
        has_handler) {
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

static CPUFault map_decoder_fault(DecoderStatus status) {
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

static CPUFault map_alu_fault(ALUStatus status) {
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

CPUStatus cpu_init(DioCPU *cpu) {
  if (cpu == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  MemoryStatus status = memory_init(&cpu->memory);
  if (status != MEMORY_STATUS_OK) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  return cpu_reset(cpu);
}

CPUStatus cpu_reset(DioCPU *cpu) {
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
  microop_context_reset(&cpu->uop);
  cycle_engine_reset(&cpu->cycle);

  return CPU_STATUS_OK;
}

CPUStatus cpu_raise_irq(DioCPU *cpu, uint8_t vector) {
  if (cpu == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  cpu->irq_pending[vector >> 5u] |= (1u << (vector & 31u));
  return CPU_STATUS_OK;
}

CPUStatus cpu_clear_irq(DioCPU *cpu, uint8_t vector) {
  if (cpu == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  cpu->irq_pending[vector >> 5u] &= ~(1u << (vector & 31u));
  return CPU_STATUS_OK;
}

CPUStatus cpu_register_interrupt_handler(DioCPU *cpu, uint8_t vector,
                                         uint32_t handler_pc) {
  if (cpu == NULL || !valid_address(handler_pc) || !valid_ivt_base(cpu->ivtr)) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  uint32_t entry = DIOCPU_IVT_PRESENT_BIT | (handler_pc & 0x3FFFFu);
  uint32_t addr = cpu->ivtr + ((uint32_t)vector * DIOCPU_IVT_ENTRY_SIZE);

  if (memory_data_write16(&cpu->memory, addr, (uint16_t)(entry & 0xFFFFu)) !=
          MEMORY_STATUS_OK ||
      memory_data_write16(&cpu->memory, addr + 2u, (uint16_t)(entry >> 16u)) !=
          MEMORY_STATUS_OK) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  return CPU_STATUS_OK;
}

static int find_pending_irq(DioCPU *cpu, uint8_t *out_vector) {
  if ((cpu->flags & CPU_FLAG_IF) == 0u) {
    return 0;
  }

  for (uint32_t w = 0; w < DIOCPU_IRQ_WORDS; w++) {
    if (cpu->irq_pending[w] == 0u) {
      continue;
    }
    for (uint32_t bit = 0; bit < 32u; bit++) {
      if ((cpu->irq_pending[w] & (1u << bit)) != 0u) {
        *out_vector = (uint8_t)((w << 5u) | bit);
        cpu->irq_pending[w] &= ~(1u << bit);
        return 1;
      }
    }
  }

  return 0;
}

static CPUStatus begin_instruction_or_irq(DioCPU *cpu) {
  MicroOpContext *uop = &cpu->uop;
  DatapathState *dp = &cpu->datapath;

  uop->saved_sp = cpu->sp;
  uop->saved_flags = cpu->flags;
  uop->stack_ret_pc = 0;
  uop->ivt_handler_pc = 0;
  uop->new_alu_flags = cpu->flags & CPU_FLAGS_DEFINED_MASK;

  uint8_t irq_vec = 0;
  if (find_pending_irq(cpu, &irq_vec)) {
    dp->valid = false;
    microop_build_irq_sequence(irq_vec, &uop->sequence);
    uop->active = true;
    uop->step_index = 0;
    return CPU_STATUS_OK;
  }

  DecoderStatus decoder_status =
      datapath_fetch_decode(&cpu->memory, cpu->pc, cpu->flags, dp);

  if (decoder_status != DECODER_STATUS_OK) {
    return raise_fault(cpu, map_decoder_fault(decoder_status));
  }

  const ControlSignals *ctrl = &dp->ctrl;
  const DecodedInstruction *instr = &dp->ir;

  if (!ctrl->is_halt && !ctrl->is_jump && !ctrl->is_return &&
      !ctrl->is_branch) {
    if (!valid_next_pc(instr->next_pc)) {
      return raise_fault(cpu, ctrl->is_call
                                  ? CPU_FAULT_INVALID_INSTRUCTION
                                  : CPU_FAULT_INSTRUCTION_FETCH_FAULT);
    }
  }

  if (!microop_build_sequence(instr, ctrl, &uop->sequence)) {
    return raise_fault(cpu, CPU_FAULT_INVALID_INSTRUCTION);
  }

  uop->active = true;
  uop->step_index = 1u; /* UOP_FETCH_DECODE just completed */
  uop->last_uop = UOP_FETCH_DECODE;
  uop->microops_executed++;

  return CPU_STATUS_OK;
}

static CPUStatus execute_single_uop(DioCPU *cpu, MicroOp op) {
  MicroOpContext *uop = &cpu->uop;
  DatapathState *dp = &cpu->datapath;
  const ControlSignals *ctrl = &dp->ctrl;
  const DecodedInstruction *instr = &dp->ir;

  switch (op.kind) {

  case UOP_READ_OPERANDS:
    datapath_read_operands(dp, cpu->gpr, CPU_GPR_COUNT, cpu->sp);
    break;

  case UOP_AGU:
    /* AGU calculation occurs during operand read; bounds checked on mem access.
     */
    break;

  case UOP_ALU: {
    if ((instr->opcode == DIOCPU_OP_DIV || instr->opcode == DIOCPU_OP_DIVS) &&
        instr->reg_a == instr->reg_b) {
      return raise_fault(cpu, CPU_FAULT_INVALID_INSTRUCTION);
    }
    ALUStatus alu_status =
        datapath_run_alu(dp, cpu->flags, &uop->new_alu_flags);
    if (alu_status != ALU_STATUS_OK) {
      return raise_fault(cpu, map_alu_fault(alu_status));
    }
    break;
  }

  case UOP_MEM_READ: {
    if (!dp->effective_addr_valid) {
      return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
    }
    int addr_ok = ctrl->mem_is_stack_frame
                      ? valid_stack_data_address(cpu, dp->effective_addr)
                      : valid_general_data_address(dp->effective_addr);
    if (!addr_ok) {
      return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
    }
    if (memory_data_read16(&cpu->memory, dp->effective_addr, &dp->mdr) !=
        MEMORY_STATUS_OK) {
      return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
    }
    cpu->mar = dp->effective_addr;
    break;
  }

  case UOP_MEM_WRITE: {
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
    if (memory_data_write16(&cpu->memory, dp->effective_addr, dp->mdr) !=
        MEMORY_STATUS_OK) {
      return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
    }
    cpu->mar = dp->effective_addr;
    break;
  }

  case UOP_STACK_PUSH16: {
    dp->mdr = dp->alu_in_a;
    StackStatus st = stack_push16(&cpu->memory, &cpu->sp, dp->mdr);
    if (st != STACK_STATUS_OK) {
      return raise_fault(cpu, map_stack_fault(st));
    }
    cpu->mar = cpu->sp;
    dp->bus_addr = cpu->sp;
    break;
  }

  case UOP_STACK_POP16: {
    uint32_t old_sp = cpu->sp;
    StackStatus st = stack_pop16(&cpu->memory, &cpu->sp, &dp->mdr);
    if (st != STACK_STATUS_OK) {
      return raise_fault(cpu, map_stack_fault(st));
    }
    cpu->mar = old_sp;
    dp->bus_addr = old_sp;
    break;
  }

  case UOP_IVT_LOOKUP: {
    if (cpu->interrupt_nesting >= DIOCPU_MAX_INTERRUPT_NESTING) {
      return raise_fault(cpu, CPU_FAULT_STACK_OVERFLOW);
    }
    uint8_t vec = uop->sequence.is_irq_sequence ? uop->sequence.irq_vector
                                                : instr->immediate8;
    int has_handler = 0;
    if (!lookup_ivt_handler(cpu, vec, &uop->ivt_handler_pc, &has_handler) ||
        !has_handler) {
      return raise_fault(cpu, CPU_FAULT_UNHANDLED_INTERRUPT);
    }
    break;
  }

  case UOP_STACK_PUSH_PC: {
    uint32_t return_pc =
        uop->sequence.is_irq_sequence ? cpu->pc : instr->next_pc;
    if (!uop->sequence.is_irq_sequence && !ctrl->is_interrupt) {
      uint32_t call_target = 0;
      if (!datapath_resolve_next_pc(dp, cpu->gpr, CPU_GPR_COUNT, cpu->pc, 0, 0,
                                    &call_target)) {
        return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
      }
    }
    StackStatus st = stack_call(&cpu->memory, &cpu->sp, return_pc);
    if (st != STACK_STATUS_OK) {
      return raise_fault(cpu, map_stack_fault(st));
    }
    cpu->mar = cpu->sp;
    dp->bus_addr = cpu->sp;
    break;
  }

  case UOP_STACK_PUSH_FLAGS: {
    StackStatus st = stack_push16(&cpu->memory, &cpu->sp, uop->saved_flags);
    if (st != STACK_STATUS_OK) {
      return raise_fault(cpu, map_stack_fault(st));
    }
    cpu->mar = cpu->sp;
    dp->bus_addr = cpu->sp;
    break;
  }

  case UOP_STACK_POP_FLAGS: {
    uint32_t old_sp = cpu->sp;
    StackStatus st = stack_pop16(&cpu->memory, &cpu->sp, &dp->mdr);
    if (st != STACK_STATUS_OK) {
      return raise_fault(cpu, map_stack_fault(st));
    }
    cpu->flags = dp->mdr & CPU_FLAGS_ALL_MASK;
    cpu->mar = old_sp;
    dp->bus_addr = old_sp;
    break;
  }

  case UOP_STACK_POP_PC: {
    uint32_t old_sp = cpu->sp;
    StackStatus st = stack_ret(&cpu->memory, &cpu->sp, &uop->stack_ret_pc);
    if (st != STACK_STATUS_OK) {
      return raise_fault(cpu, map_stack_fault(st));
    }
    cpu->mar = old_sp;
    dp->bus_addr = old_sp;
    break;
  }

  case UOP_SET_IVTR:
    if (!valid_ivt_base(instr->absolute_address)) {
      return raise_fault(cpu, CPU_FAULT_MEMORY_ACCESS_FAULT);
    }
    cpu->ivtr = instr->absolute_address;
    break;

  case UOP_UPDATE_SYS_FLAGS:
    if (uop->sequence.is_irq_sequence || ctrl->is_interrupt) {
      uint8_t vec = uop->sequence.is_irq_sequence ? uop->sequence.irq_vector
                                                  : instr->immediate8;
      cpu->flags &= (uint16_t)~CPU_FLAG_IF;
      cpu->interrupt_count++;
      cpu->interrupt_nesting++;
      cpu->last_interrupt_vector = vec;
      cpu->fault = CPU_FAULT_NONE;
    } else if (instr->opcode == DIOCPU_OP_CLI) {
      cpu->flags &= (uint16_t)~CPU_FLAG_IF;
    } else if (instr->opcode == DIOCPU_OP_STI) {
      cpu->flags |= CPU_FLAG_IF;
    }
    break;

  case UOP_REG_WRITEBACK:
    if (ctrl->reg_write_a) {
      cpu->gpr[instr->reg_a] =
          datapath_select_writeback(dp, cpu->gpr, CPU_GPR_COUNT);
    }
    if (ctrl->reg_write_b) {
      cpu->gpr[instr->reg_b] = dp->alu_rem;
    }
    if (ctrl->flags_write_alu) {
      cpu->flags = (cpu->flags & (uint16_t)~CPU_FLAGS_DEFINED_MASK) |
                   (uop->new_alu_flags & CPU_FLAGS_DEFINED_MASK);
    }
    break;

  case UOP_PC_UPDATE: {
    if (uop->sequence.is_irq_sequence) {
      dp->next_pc_target = uop->ivt_handler_pc;
      cpu->pc = uop->ivt_handler_pc;
    } else {
      uint32_t resolved_next_pc = 0;
      if (!datapath_resolve_next_pc(dp, cpu->gpr, CPU_GPR_COUNT, cpu->pc,
                                    uop->stack_ret_pc, uop->ivt_handler_pc,
                                    &resolved_next_pc)) {
        return raise_fault(cpu, CPU_FAULT_INSTRUCTION_FETCH_FAULT);
      }
      if (ctrl->is_interrupt && ctrl->is_return &&
          cpu->interrupt_nesting > 0u) {
        cpu->interrupt_nesting--;
      }
      dp->next_pc_target = resolved_next_pc;
      cpu->pc = resolved_next_pc;
    }

    cpu->instructions_executed++;
    uop->active = false;
    break;
  }

  case UOP_HALT: {
    uint32_t next_pc = cpu->pc;
    (void)datapath_resolve_next_pc(dp, cpu->gpr, CPU_GPR_COUNT, cpu->pc, 0, 0,
                                   &next_pc);
    dp->next_pc_target = next_pc;
    cpu->pc = next_pc;
    cpu->state = CPU_STATE_HALTED;
    cpu->instructions_executed++;
    uop->active = false;
    uop->last_uop = op.kind;
    uop->microops_executed++;
    return CPU_STATUS_HALTED;
  }

  default:
    return raise_fault(cpu, CPU_FAULT_INVALID_INSTRUCTION);
  }

  uop->last_uop = op.kind;
  uop->microops_executed++;
  return CPU_STATUS_OK;
}

static CPUStatus finish_uop_on_current_cycle(DioCPU *cpu, uint64_t prev_instrs) {
  MicroOpContext *uop = &cpu->uop;
  CycleEngine *ce = &cpu->cycle;
  bool is_irq = uop->sequence.is_irq_sequence;
  uint32_t tick_pc = ce->active_pc;
  MicroOp op = ce->active_uop;
  uint8_t cyc_idx = ce->uop_cycles_elapsed;

  uop->step_index++;
  CPUStatus st = execute_single_uop(cpu, op);

  if (st == CPU_STATUS_FAULTED) {
    ce->current_stage = CYCLE_STAGE_FAULT;
    ce->current_instruction_cycles = 0u;
    cycle_engine_record_tick(ce, tick_pc, op, cyc_idx, false, false, false,
                             is_irq);
    return CPU_STATUS_FAULTED;
  }

  if (!uop->active && cpu->instructions_executed == prev_instrs) {
    /* Micro-op raised a fault that trapped via IVT */
    ce->current_stage = CYCLE_STAGE_FAULT;
    ce->last_instruction_cycles = ce->current_instruction_cycles;
    ce->current_instruction_cycles = 0u;
    cycle_engine_record_tick(ce, tick_pc, op, cyc_idx, false, false, false,
                             is_irq);
    return CPU_STATUS_OK;
  }

  bool retired_instr = !uop->active;
  if (retired_instr) {
    ce->last_instruction_cycles = ce->current_instruction_cycles;
    ce->current_instruction_cycles = 0u;
  }

  cycle_engine_record_tick(ce, tick_pc, op, cyc_idx, false, true, retired_instr,
                           is_irq);
  return st;
}

static CPUStatus issue_uop_on_current_cycle(DioCPU *cpu, uint32_t exec_pc,
                                            uint64_t prev_instrs) {
  MicroOpContext *uop = &cpu->uop;
  CycleEngine *ce = &cpu->cycle;

  if (uop->step_index >= uop->sequence.count) {
    uop->active = false;
    ce->current_stage = CYCLE_STAGE_FAULT;
    ce->current_instruction_cycles = 0u;
    return raise_fault(cpu, CPU_FAULT_INVALID_INSTRUCTION);
  }

  MicroOp op = uop->sequence.ops[uop->step_index];
  uint8_t lat = (op.latency_cycles > 0u) ? op.latency_cycles : 1u;

  ce->active_uop = op;
  ce->active_uop.latency_cycles = lat;
  ce->active_pc = exec_pc;
  ce->uop_cycles_elapsed = 1u;
  ce->uop_cycles_remaining = (uint8_t)(lat - 1u);
  ce->current_stage = cycle_stage_from_uop(op.kind);

  if (ce->uop_cycles_remaining > 0u) {
    ce->uop_in_flight = true;
    ce->busy_stall_cycles++;
    ce->current_cycle_stalled = true;
    cycle_engine_record_tick(ce, exec_pc, ce->active_uop, 1u, true, false,
                             false, uop->sequence.is_irq_sequence);
    return CPU_STATUS_OK;
  }

  ce->uop_in_flight = false;
  ce->current_cycle_stalled = false;
  return finish_uop_on_current_cycle(cpu, prev_instrs);
}

CPUStatus cpu_cycle_step(DioCPU *cpu) {
  if (cpu == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  if (cpu->state == CPU_STATE_HALTED) {
    return CPU_STATUS_HALTED;
  }

  if (cpu->state == CPU_STATE_FAULTED) {
    return CPU_STATUS_FAULTED;
  }

  MicroOpContext *uop = &cpu->uop;
  DatapathState *dp = &cpu->datapath;
  CycleEngine *ce = &cpu->cycle;
  uint64_t prev_instrs = cpu->instructions_executed;

  ce->total_cycles++;
  ce->active_cycles++;
  ce->current_instruction_cycles++;
  ce->current_cycle_stalled = false;

  if (ce->uop_in_flight) {
    if (uop->sequence.is_irq_sequence) {
      ce->interrupt_cycles++;
    }
    ce->uop_cycles_elapsed++;
    ce->uop_cycles_remaining--;
    ce->current_stage = cycle_stage_from_uop(ce->active_uop.kind);

    if (ce->uop_cycles_remaining > 0u) {
      ce->busy_stall_cycles++;
      ce->current_cycle_stalled = true;
      cycle_engine_record_tick(ce, ce->active_pc, ce->active_uop,
                               ce->uop_cycles_elapsed, true, false, false,
                               uop->sequence.is_irq_sequence);
      return CPU_STATUS_OK;
    }

    ce->uop_in_flight = false;
    return finish_uop_on_current_cycle(cpu, prev_instrs);
  }

  if (!uop->active) {
    uint32_t start_pc = cpu->pc;
    CPUStatus st = begin_instruction_or_irq(cpu);
    if (st != CPU_STATUS_OK) {
      MicroOp fetch_op = {UOP_FETCH_DECODE, 1u};
      ce->active_uop = fetch_op;
      ce->active_pc = start_pc;
      ce->uop_cycles_elapsed = 1u;
      ce->uop_cycles_remaining = 0u;
      ce->current_stage = CYCLE_STAGE_FAULT;
      ce->current_instruction_cycles = 0u;
      cycle_engine_record_tick(ce, start_pc, fetch_op, 1u, false, false, false,
                               false);
      return st;
    }

    if (!uop->active) {
      /* Fetch/decode fault trapped via IVT */
      MicroOp fetch_op = {UOP_FETCH_DECODE, 1u};
      ce->active_uop = fetch_op;
      ce->active_pc = start_pc;
      ce->uop_cycles_elapsed = 1u;
      ce->uop_cycles_remaining = 0u;
      ce->current_stage = CYCLE_STAGE_FAULT;
      ce->last_instruction_cycles = ce->current_instruction_cycles;
      ce->current_instruction_cycles = 0u;
      cycle_engine_record_tick(ce, start_pc, fetch_op, 1u, false, false, false,
                               false);
      return CPU_STATUS_OK;
    }

    if (!uop->sequence.is_irq_sequence) {
      MicroOp fetch_op = uop->sequence.ops[0];
      ce->active_uop = fetch_op;
      ce->active_pc = dp->ir_pc;
      ce->uop_cycles_elapsed = 1u;
      ce->uop_cycles_remaining = 0u;
      ce->uop_in_flight = false;
      ce->current_stage = CYCLE_STAGE_FETCH;
      cycle_engine_record_tick(ce, dp->ir_pc, fetch_op, 1u, false, true, false,
                               false);
      return CPU_STATUS_OK;
    }

    ce->interrupt_cycles++;
    return issue_uop_on_current_cycle(cpu, cpu->pc, prev_instrs);
  }

  if (uop->sequence.is_irq_sequence) {
    ce->interrupt_cycles++;
  }
  uint32_t exec_pc =
      (!uop->sequence.is_irq_sequence && dp->valid) ? dp->ir_pc : cpu->pc;
  return issue_uop_on_current_cycle(cpu, exec_pc, prev_instrs);
}

CPUStatus cpu_run_cycles(DioCPU *cpu, uint64_t max_cycles,
                         uint64_t *cycles_executed) {
  if (cpu == NULL || cycles_executed == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  *cycles_executed = 0u;

  if (cpu->state == CPU_STATE_HALTED) {
    return CPU_STATUS_HALTED;
  }

  if (cpu->state == CPU_STATE_FAULTED) {
    return CPU_STATUS_FAULTED;
  }

  if (cpu->cycle.cycle_breakpoint_enabled &&
      cpu->cycle.total_cycles == cpu->cycle.cycle_breakpoint) {
    return CPU_STATUS_BREAKPOINT_REACHED;
  }

  uint64_t start_cycles = cpu->cycle.total_cycles;

  for (uint64_t i = 0; i < max_cycles; i++) {
    CPUStatus st = cpu_cycle_step(cpu);
    if (st != CPU_STATUS_OK) {
      *cycles_executed = cpu->cycle.total_cycles - start_cycles;
      return st;
    }
    if (cpu->cycle.cycle_breakpoint_enabled &&
        cpu->cycle.total_cycles == cpu->cycle.cycle_breakpoint) {
      *cycles_executed = cpu->cycle.total_cycles - start_cycles;
      return CPU_STATUS_BREAKPOINT_REACHED;
    }
  }

  *cycles_executed = cpu->cycle.total_cycles - start_cycles;
  return CPU_STATUS_STEP_LIMIT_REACHED;
}

CPUStatus cpu_set_cycle_breakpoint(DioCPU *cpu, uint64_t target_cycle) {
  if (cpu == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  cpu->cycle.cycle_breakpoint_enabled = true;
  cpu->cycle.cycle_breakpoint = target_cycle;
  return CPU_STATUS_OK;
}

CPUStatus cpu_clear_cycle_breakpoint(DioCPU *cpu) {
  if (cpu == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  cpu->cycle.cycle_breakpoint_enabled = false;
  cpu->cycle.cycle_breakpoint = 0u;
  return CPU_STATUS_OK;
}

CPUStatus cpu_microop_step(DioCPU *cpu) {
  if (cpu == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  do {
    CPUStatus st = cpu_cycle_step(cpu);
    if (st != CPU_STATUS_OK) {
      return st;
    }
  } while (cpu->cycle.uop_in_flight);

  return CPU_STATUS_OK;
}

CPUStatus cpu_step(DioCPU *cpu) {
  if (cpu == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  if (cpu->state == CPU_STATE_HALTED) {
    return CPU_STATUS_HALTED;
  }

  if (cpu->state == CPU_STATE_FAULTED) {
    return CPU_STATUS_FAULTED;
  }

  uint64_t initial_instrs = cpu->instructions_executed;

  do {
    CPUStatus st = cpu_microop_step(cpu);
    if (st != CPU_STATUS_OK) {
      return st;
    }
    /* If an instruction faulted and trapped via IVT, uop.active became false
     * without incrementing instructions_executed. */
    if (!cpu->uop.active && cpu->instructions_executed == initial_instrs) {
      return CPU_STATUS_OK;
    }
  } while (cpu->uop.active);

  return CPU_STATUS_OK;
}

CPUStatus cpu_run(DioCPU *cpu, uint64_t max_steps, uint64_t *steps_executed) {
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

      *steps_executed = cpu->instructions_executed - initial_count;

      return status;
    }
  }

  *steps_executed = cpu->instructions_executed - initial_count;

  return CPU_STATUS_STEP_LIMIT_REACHED;
}

const char *cpu_fault_string(CPUFault fault) {
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

const char *cpu_state_string(CPUState state) {
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

const char *cpu_status_string(CPUStatus status) {
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
  case CPU_STATUS_BREAKPOINT_REACHED:
    return "Breakpoint reached";
  default:
    return "Unknown status";
  }
}
