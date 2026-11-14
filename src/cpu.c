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

  cpu->pipeline.initialized = false;
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
  pipeline_reset(&cpu->pipeline, 0u);

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

static bool pipe_is_empty(const PipelineState *p) {
  return !p->if_id.valid && !p->id_ex.valid && !p->ex_mem.valid &&
         !p->mem_wb.valid;
}

static bool pipe_has_enabled_irq(const DioCPU *cpu) {
  if ((cpu->flags & CPU_FLAG_IF) == 0u) {
    return false;
  }

  for (uint32_t w = 0; w < DIOCPU_IRQ_WORDS; w++) {
    if (cpu->irq_pending[w] != 0u) {
      return true;
    }
  }

  return false;
}

static void pipe_clear_all(PipelineState *p, uint32_t fetch_pc) {
  memset(&p->if_id, 0, sizeof(p->if_id));
  memset(&p->id_ex, 0, sizeof(p->id_ex));
  memset(&p->ex_mem, 0, sizeof(p->ex_mem));
  memset(&p->mem_wb, 0, sizeof(p->mem_wb));
  p->ex_cycles_remaining = 0u;
  p->mem_cycles_remaining = 0u;
  p->fetch_pc = fetch_pc;
}

static bool pipe_slot_serializes_if(const DecodedInstruction *ir,
                                    const ControlSignals *ctrl,
                                    uint8_t fault) {
  if (fault != (uint8_t)CPU_FAULT_NONE) {
    return false;
  }
  PipelineRegUsage usage;
  pipeline_collect_usage(ir, ctrl, &usage);
  return usage.serializes_frontend;
}

static void pipe_record_slot(PipelineSlotSummary *slot, bool valid,
                             bool stalled, bool flushed, bool forwarded,
                             uint32_t pc, InstructionOpcode opcode) {
  slot->valid = valid;
  slot->stalled = stalled;
  slot->flushed = flushed;
  slot->forwarded = forwarded;
  slot->pc = pc;
  slot->opcode = opcode;
}

static void pipe_build_ex_operands(const DioCPU *cpu,
                                   const PipelineIDEXLatch *ex,
                                   const PipelineEXMEMLatch *cur_ex_mem,
                                   const PipelineMEMWBLatch *cur_mem_wb,
                                   uint16_t eff_gpr[CPU_GPR_COUNT],
                                   uint16_t *eff_flags, bool *out_forwarded) {
  memcpy(eff_gpr, cpu->gpr, sizeof(uint16_t) * CPU_GPR_COUNT);
  *eff_flags = cpu->flags;
  *out_forwarded = false;

  if (!cpu->pipeline.config.forwarding_enabled) {
    return;
  }

  PipelineRegUsage cons;
  pipeline_collect_usage(&ex->ir, &ex->ctrl, &cons);

  uint16_t wb_write_mask = 0u;
  bool wb_writes_flags = false;
  if (cur_mem_wb->valid && cur_mem_wb->fault == (uint8_t)CPU_FAULT_NONE) {
    if (cur_mem_wb->ctrl.reg_write_a) {
      wb_write_mask |= (uint16_t)(1u << cur_mem_wb->ir.reg_a);
    }
    if (cur_mem_wb->ctrl.reg_write_b) {
      wb_write_mask |= (uint16_t)(1u << cur_mem_wb->ir.reg_b);
    }
    wb_writes_flags = cur_mem_wb->ctrl.flags_write_alu;
  }

  uint16_t mem_write_mask = 0u;
  bool mem_writes_flags = false;
  if (cur_ex_mem->valid && cur_ex_mem->fault == (uint8_t)CPU_FAULT_NONE) {
    if (cur_ex_mem->ctrl.reg_write_a) {
      eff_gpr[cur_ex_mem->ir.reg_a] = cur_ex_mem->wb_value;
      mem_write_mask |= (uint16_t)(1u << cur_ex_mem->ir.reg_a);
    }
    if (cur_ex_mem->ctrl.reg_write_b) {
      eff_gpr[cur_ex_mem->ir.reg_b] = cur_ex_mem->wb_rem;
      mem_write_mask |= (uint16_t)(1u << cur_ex_mem->ir.reg_b);
    }
    if (cur_ex_mem->ctrl.flags_write_alu) {
      *eff_flags = (*eff_flags & (uint16_t)~CPU_FLAGS_DEFINED_MASK) |
                   (cur_ex_mem->alu_flags & CPU_FLAGS_DEFINED_MASK);
      mem_writes_flags = true;
    }
  }

  if ((cons.read_gpr_mask & (mem_write_mask | wb_write_mask)) != 0u ||
      (cons.reads_flags && (mem_writes_flags || wb_writes_flags))) {
    *out_forwarded = true;
  }
}

static bool pipe_step_wb(DioCPU *cpu, const PipelineMEMWBLatch *wb,
                         CPUStatus *out_status) {
  PipelineState *p = &cpu->pipeline;
  if (!wb->valid) {
    return false;
  }

  pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_WB], true, false, false, false,
                   wb->pc, wb->ir.opcode);

  if (wb->fault != (uint8_t)CPU_FAULT_NONE) {
    cpu->pc = wb->pc;
    *out_status = raise_fault(cpu, (CPUFault)wb->fault);
    pipe_clear_all(p, cpu->pc);
    return true;
  }

  if (wb->ctrl.reg_write_a) {
    cpu->gpr[wb->ir.reg_a] = wb->wb_value;
  }
  if (wb->ctrl.reg_write_b) {
    cpu->gpr[wb->ir.reg_b] = wb->wb_rem;
  }

  if (wb->ctrl.is_interrupt && wb->ctrl.is_return) {
    cpu->flags = wb->restored_flags & CPU_FLAGS_ALL_MASK;
    if (cpu->interrupt_nesting > 0u) {
      cpu->interrupt_nesting--;
    }
  } else {
    if (wb->ctrl.flags_write_alu) {
      cpu->flags = (cpu->flags & (uint16_t)~CPU_FLAGS_DEFINED_MASK) |
                   (wb->alu_flags & CPU_FLAGS_DEFINED_MASK);
    }
    if (wb->ctrl.flags_write_system && !wb->ctrl.is_interrupt) {
      if (wb->ir.opcode == DIOCPU_OP_CLI) {
        cpu->flags &= (uint16_t)~CPU_FLAG_IF;
      } else if (wb->ir.opcode == DIOCPU_OP_STI) {
        cpu->flags |= CPU_FLAG_IF;
      }
    }
  }

  if (wb->ctrl.sets_ivtr) {
    cpu->ivtr = wb->ir.absolute_address;
  }

  cpu->instructions_executed++;
  p->stats.instructions_retired++;
  cpu->pc = wb->committed_next_pc;

  if (wb->ctrl.is_halt) {
    cpu->state = CPU_STATE_HALTED;
    pipe_clear_all(p, cpu->pc);
    *out_status = CPU_STATUS_HALTED;
    return true;
  }

  return false;
}

static void pipe_step_mem(DioCPU *cpu, const PipelineEXMEMLatch *ex,
                          PipelineEXMEMLatch *next_ex_mem,
                          PipelineMEMWBLatch *next_mem_wb, bool *out_mem_stall,
                          bool *out_mem_redirect, uint32_t *out_redirect_pc) {
  PipelineState *p = &cpu->pipeline;
  *out_mem_stall = false;
  *out_mem_redirect = false;
  *out_redirect_pc = 0u;

  if (!ex->valid) {
    return;
  }

  bool does_mem = ex->ctrl.mem_read || ex->ctrl.mem_write ||
                  ex->ctrl.stack_push || ex->ctrl.stack_pop ||
                  (ex->ctrl.is_interrupt && !ex->ctrl.is_return);

  if (ex->fault == (uint8_t)CPU_FAULT_NONE && does_mem) {
    if (p->mem_cycles_remaining == 0u) {
      uint8_t lat = (p->config.mem_stage_latency > 0u)
                        ? p->config.mem_stage_latency
                        : 1u;
      if (lat > 1u) {
        p->mem_cycles_remaining = (uint8_t)(lat - 1u);
        *out_mem_stall = true;
      }
    } else {
      p->mem_cycles_remaining--;
      if (p->mem_cycles_remaining > 0u) {
        *out_mem_stall = true;
      }
    }

    if (*out_mem_stall) {
      *next_ex_mem = *ex;
      p->stats.structural_mem_stalls++;
      p->stats.stall_cycles++;
      p->stats.bubbles++;
      pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_MEM], true, true, false,
                       false, ex->pc, ex->ir.opcode);
      return;
    }
  }

  memset(next_mem_wb, 0, sizeof(*next_mem_wb));
  next_mem_wb->valid = true;
  next_mem_wb->pc = ex->pc;
  next_mem_wb->ir = ex->ir;
  next_mem_wb->ctrl = ex->ctrl;
  next_mem_wb->fault = ex->fault;
  next_mem_wb->wb_value = ex->wb_value;
  next_mem_wb->wb_rem = ex->wb_rem;
  next_mem_wb->alu_flags = ex->alu_flags;
  next_mem_wb->committed_next_pc = ex->committed_next_pc;

  pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_MEM], true, false, false,
                   false, ex->pc, ex->ir.opcode);

  if (ex->fault != (uint8_t)CPU_FAULT_NONE) {
    return;
  }

  if (ex->ctrl.mem_read) {
    int addr_ok = ex->ctrl.mem_is_stack_frame
                      ? valid_stack_data_address(cpu, ex->effective_addr)
                      : valid_general_data_address(ex->effective_addr);
    uint16_t val = 0u;
    if (!ex->effective_addr_valid || !addr_ok ||
        memory_data_read16(&cpu->memory, ex->effective_addr, &val) !=
            MEMORY_STATUS_OK) {
      next_mem_wb->fault = (uint8_t)CPU_FAULT_MEMORY_ACCESS_FAULT;
      return;
    }
    cpu->mar = ex->effective_addr;
    next_mem_wb->wb_value = val;
    return;
  }

  if (ex->ctrl.mem_write) {
    int addr_ok = ex->ctrl.mem_is_stack_frame
                      ? valid_stack_data_address(cpu, ex->effective_addr)
                      : valid_general_data_address(ex->effective_addr);
    if (!ex->effective_addr_valid || !addr_ok ||
        memory_data_write16(&cpu->memory, ex->effective_addr,
                            ex->store_value) != MEMORY_STATUS_OK) {
      next_mem_wb->fault = (uint8_t)CPU_FAULT_MEMORY_ACCESS_FAULT;
      return;
    }
    cpu->mar = ex->effective_addr;
    return;
  }

  if (ex->ctrl.is_interrupt && !ex->ctrl.is_return) {
    /* Software INT imm8 */
    if (cpu->interrupt_nesting >= DIOCPU_MAX_INTERRUPT_NESTING) {
      next_mem_wb->fault = (uint8_t)CPU_FAULT_STACK_OVERFLOW;
      return;
    }
    uint32_t handler_pc = 0u;
    int has_handler = 0;
    if (!lookup_ivt_handler(cpu, ex->ir.immediate8, &handler_pc,
                            &has_handler) ||
        !has_handler) {
      next_mem_wb->fault = (uint8_t)CPU_FAULT_UNHANDLED_INTERRUPT;
      return;
    }

    uint32_t saved_sp = cpu->sp;
    StackStatus st = stack_call(&cpu->memory, &cpu->sp, ex->ir.next_pc);
    if (st != STACK_STATUS_OK) {
      cpu->sp = saved_sp;
      next_mem_wb->fault = (uint8_t)map_stack_fault(st);
      return;
    }
    st = stack_push16(&cpu->memory, &cpu->sp, cpu->flags);
    if (st != STACK_STATUS_OK) {
      cpu->sp = saved_sp;
      next_mem_wb->fault = (uint8_t)map_stack_fault(st);
      return;
    }

    cpu->flags &= (uint16_t)~CPU_FLAG_IF;
    cpu->mar = cpu->sp;
    cpu->interrupt_count++;
    cpu->interrupt_nesting++;
    cpu->last_interrupt_vector = ex->ir.immediate8;
    cpu->fault = CPU_FAULT_NONE;

    next_mem_wb->committed_next_pc = handler_pc;
    *out_mem_redirect = true;
    *out_redirect_pc = handler_pc;
    return;
  }

  if (ex->ctrl.is_interrupt && ex->ctrl.is_return) {
    /* IRET */
    uint32_t saved_sp = cpu->sp;
    uint16_t popped_flags = 0u;
    StackStatus st = stack_pop16(&cpu->memory, &cpu->sp, &popped_flags);
    if (st != STACK_STATUS_OK) {
      cpu->sp = saved_sp;
      next_mem_wb->fault = (uint8_t)map_stack_fault(st);
      return;
    }
    uint32_t ret_old_sp = cpu->sp;
    uint32_t ret_pc = 0u;
    st = stack_ret(&cpu->memory, &cpu->sp, &ret_pc);
    if (st != STACK_STATUS_OK) {
      cpu->sp = saved_sp;
      next_mem_wb->fault = (uint8_t)map_stack_fault(st);
      return;
    }
    if (!valid_address(ret_pc)) {
      cpu->sp = saved_sp;
      next_mem_wb->fault = (uint8_t)CPU_FAULT_INSTRUCTION_FETCH_FAULT;
      return;
    }
    cpu->mar = ret_old_sp;
    next_mem_wb->restored_flags = popped_flags & CPU_FLAGS_ALL_MASK;
    next_mem_wb->committed_next_pc = ret_pc;
    *out_mem_redirect = true;
    *out_redirect_pc = ret_pc;
    return;
  }

  if (ex->ctrl.stack_push) {
    StackStatus st =
        ex->ctrl.stack_is_32bit
            ? stack_call(&cpu->memory, &cpu->sp, ex->ir.next_pc)
            : stack_push16(&cpu->memory, &cpu->sp, ex->store_value);
    if (st != STACK_STATUS_OK) {
      next_mem_wb->fault = (uint8_t)map_stack_fault(st);
      return;
    }
    cpu->mar = cpu->sp;
    return;
  }

  if (ex->ctrl.stack_pop) {
    uint32_t old_sp = cpu->sp;
    if (ex->ctrl.stack_is_32bit) {
      uint32_t ret_pc = 0u;
      StackStatus st = stack_ret(&cpu->memory, &cpu->sp, &ret_pc);
      if (st != STACK_STATUS_OK) {
        next_mem_wb->fault = (uint8_t)map_stack_fault(st);
        return;
      }
      if (!valid_address(ret_pc)) {
        next_mem_wb->fault = (uint8_t)CPU_FAULT_INSTRUCTION_FETCH_FAULT;
        return;
      }
      cpu->mar = old_sp;
      next_mem_wb->committed_next_pc = ret_pc;
      *out_mem_redirect = true;
      *out_redirect_pc = ret_pc;
    } else {
      uint16_t val = 0u;
      StackStatus st = stack_pop16(&cpu->memory, &cpu->sp, &val);
      if (st != STACK_STATUS_OK) {
        next_mem_wb->fault = (uint8_t)map_stack_fault(st);
        return;
      }
      cpu->mar = old_sp;
      next_mem_wb->wb_value = val;
    }
  }
}

static void pipe_step_ex(DioCPU *cpu, const PipelineIDEXLatch *id_ex,
                         const PipelineEXMEMLatch *cur_ex_mem,
                         const PipelineMEMWBLatch *cur_mem_wb,
                         bool mem_stall, bool mem_redirect,
                         PipelineIDEXLatch *next_id_ex,
                         PipelineEXMEMLatch *next_ex_mem, bool *out_ex_stall,
                         bool *out_ex_redirect, uint32_t *out_redirect_pc) {
  PipelineState *p = &cpu->pipeline;
  *out_ex_stall = false;
  *out_ex_redirect = false;
  *out_redirect_pc = 0u;

  if (mem_redirect) {
    p->ex_cycles_remaining = 0u;
    if (id_ex->valid) {
      p->stats.bubbles++;
      pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_EX], true, false, true,
                       false, id_ex->pc, id_ex->ir.opcode);
    }
    return;
  }

  if (mem_stall) {
    if (id_ex->valid) {
      *next_id_ex = *id_ex;
      pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_EX], true, true, false,
                       false, id_ex->pc, id_ex->ir.opcode);
    }
    return;
  }

  if (!id_ex->valid) {
    return;
  }

  if (id_ex->fault != (uint8_t)CPU_FAULT_NONE) {
    memset(next_ex_mem, 0, sizeof(*next_ex_mem));
    next_ex_mem->valid = true;
    next_ex_mem->pc = id_ex->pc;
    next_ex_mem->ir = id_ex->ir;
    next_ex_mem->ctrl = id_ex->ctrl;
    next_ex_mem->fault = id_ex->fault;
    pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_EX], true, false, false,
                     false, id_ex->pc, id_ex->ir.opcode);
    return;
  }

  uint8_t ex_lat = 1u;
  if (id_ex->ctrl.alu_enable) {
    if (id_ex->ctrl.alu_op == ALU_MUL || id_ex->ctrl.alu_op == ALU_MULS) {
      ex_lat = 2u;
    } else if (id_ex->ctrl.alu_op == ALU_DIV ||
               id_ex->ctrl.alu_op == ALU_DIVS) {
      ex_lat = 4u;
    }
  }

  if (ex_lat > 1u) {
    if (p->ex_cycles_remaining == 0u) {
      p->ex_cycles_remaining = (uint8_t)(ex_lat - 1u);
      *out_ex_stall = true;
    } else {
      p->ex_cycles_remaining--;
      if (p->ex_cycles_remaining > 0u) {
        *out_ex_stall = true;
      }
    }
  }

  if (*out_ex_stall) {
    *next_id_ex = *id_ex;
    p->stats.structural_ex_stalls++;
    p->stats.stall_cycles++;
    p->stats.bubbles++;
    pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_EX], true, true, false,
                     false, id_ex->pc, id_ex->ir.opcode);
    return;
  }

  uint16_t eff_gpr[CPU_GPR_COUNT];
  uint16_t eff_flags = 0u;
  bool forwarded = false;
  pipe_build_ex_operands(cpu, id_ex, cur_ex_mem, cur_mem_wb, eff_gpr,
                         &eff_flags, &forwarded);
  if (forwarded) {
    p->stats.forwards++;
  }

  DatapathState dp;
  datapath_reset(&dp);
  dp.valid = true;
  dp.ir_pc = id_ex->pc;
  dp.ir = id_ex->ir;
  dp.ctrl = id_ex->ctrl;

  if (dp.ctrl.is_branch) {
    bool taken = control_unit_eval_branch(dp.ir.opcode, eff_flags) != 0;
    dp.ctrl.branch_taken = taken;
    dp.ctrl.pc_src = taken ? PC_SRC_REL8_TAKEN : PC_SRC_NEXT_PC;
  }

  memset(next_ex_mem, 0, sizeof(*next_ex_mem));
  next_ex_mem->valid = true;
  next_ex_mem->pc = id_ex->pc;
  next_ex_mem->ir = dp.ir;
  next_ex_mem->ctrl = dp.ctrl;
  next_ex_mem->alu_flags = eff_flags & CPU_FLAGS_DEFINED_MASK;
  next_ex_mem->committed_next_pc = dp.ir.next_pc;

  pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_EX], true, false, false,
                   forwarded, id_ex->pc, id_ex->ir.opcode);

  if (dp.ctrl.sets_ivtr && !valid_ivt_base(dp.ir.absolute_address)) {
    next_ex_mem->fault = (uint8_t)CPU_FAULT_MEMORY_ACCESS_FAULT;
    return;
  }

  if (!dp.ctrl.is_halt && !dp.ctrl.is_jump && !dp.ctrl.is_return &&
      !dp.ctrl.is_branch) {
    if (!valid_next_pc(dp.ir.next_pc)) {
      next_ex_mem->fault =
          (uint8_t)(dp.ctrl.is_call ? CPU_FAULT_INVALID_INSTRUCTION
                                    : CPU_FAULT_INSTRUCTION_FETCH_FAULT);
      return;
    }
  }

  datapath_read_operands(&dp, eff_gpr, CPU_GPR_COUNT, cpu->sp);

  next_ex_mem->effective_addr = dp.effective_addr;
  next_ex_mem->effective_addr_valid = dp.effective_addr_valid;
  next_ex_mem->agu_raw_addr = dp.agu_raw_addr;

  if (dp.ctrl.alu_enable) {
    if ((dp.ir.opcode == DIOCPU_OP_DIV || dp.ir.opcode == DIOCPU_OP_DIVS) &&
        dp.ir.reg_a == dp.ir.reg_b) {
      next_ex_mem->fault = (uint8_t)CPU_FAULT_INVALID_INSTRUCTION;
      return;
    }
    uint16_t new_alu_flags = eff_flags & CPU_FLAGS_DEFINED_MASK;
    ALUStatus alu_st = datapath_run_alu(&dp, eff_flags, &new_alu_flags);
    if (alu_st != ALU_STATUS_OK) {
      next_ex_mem->fault = (uint8_t)map_alu_fault(alu_st);
      return;
    }
    next_ex_mem->alu_flags = new_alu_flags;
  }

  if (dp.ctrl.reg_write_a && dp.ctrl.wb_src != WB_SRC_MEM_DATA) {
    next_ex_mem->wb_value =
        datapath_select_writeback(&dp, eff_gpr, CPU_GPR_COUNT);
  }
  if (dp.ctrl.reg_write_b) {
    next_ex_mem->wb_rem = dp.alu_rem;
  }

  if (dp.ctrl.mem_write) {
    next_ex_mem->store_value = eff_gpr[dp.ir.reg_a];
  } else if (dp.ctrl.stack_push && !dp.ctrl.stack_is_32bit) {
    next_ex_mem->store_value = dp.alu_in_a;
  }

  if (dp.ctrl.is_jump || dp.ctrl.is_call ||
      (dp.ctrl.is_branch && dp.ctrl.branch_taken)) {
    uint32_t target_pc = 0u;
    if (!datapath_resolve_next_pc(&dp, eff_gpr, CPU_GPR_COUNT, dp.ir_pc, 0u, 0u,
                                  &target_pc)) {
      next_ex_mem->fault = (uint8_t)CPU_FAULT_INSTRUCTION_FETCH_FAULT;
      return;
    }
    next_ex_mem->committed_next_pc = target_pc;
    *out_ex_redirect = true;
    *out_redirect_pc = target_pc;
    p->stats.control_hazard_flushes++;
  } else if (dp.ctrl.is_halt) {
    uint32_t hlt_next_pc = dp.ir_pc;
    (void)datapath_resolve_next_pc(&dp, eff_gpr, CPU_GPR_COUNT, dp.ir_pc, 0u,
                                   0u, &hlt_next_pc);
    next_ex_mem->committed_next_pc = hlt_next_pc;
  }
}

CPUStatus cpu_set_pipeline_forwarding(DioCPU *cpu, bool enabled) {
  if (cpu == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  cpu->pipeline.config.forwarding_enabled = enabled;
  return CPU_STATUS_OK;
}

CPUStatus cpu_pipeline_step_cycle(DioCPU *cpu) {
  if (cpu == NULL) {
    return CPU_STATUS_INVALID_ARGUMENT;
  }

  if (cpu->state == CPU_STATE_HALTED) {
    return CPU_STATUS_HALTED;
  }

  if (cpu->state == CPU_STATE_FAULTED) {
    return CPU_STATUS_FAULTED;
  }

  PipelineState *p = &cpu->pipeline;
  if (!p->initialized) {
    pipeline_reset(p, cpu->pc);
  }

  p->stats.cycles++;
  memset(p->stage_snapshot, 0, sizeof(p->stage_snapshot));

  /* Handle multi-cycle hardware IRQ entry once pipeline is drained */
  if (p->irq_entry_active) {
    if (p->irq_cycles_remaining > 0u) {
      p->irq_cycles_remaining--;
      p->stats.stall_cycles++;
      return CPU_STATUS_OK;
    }
    p->irq_entry_active = false;
    CPUFault irq_fault = CPU_FAULT_NONE;
    if (!enter_interrupt(cpu, p->pending_irq_vector, p->fetch_pc,
                         &irq_fault)) {
      return raise_fault(cpu, irq_fault);
    }
    p->fetch_pc = cpu->pc;
    return CPU_STATUS_OK;
  }

  PipelineIFIDLatch cur_if_id = p->if_id;
  PipelineIDEXLatch cur_id_ex = p->id_ex;
  PipelineEXMEMLatch cur_ex_mem = p->ex_mem;
  PipelineMEMWBLatch cur_mem_wb = p->mem_wb;

  /* Step WB first; faults or HLT flush younger stages cleanly */
  CPUStatus wb_status = CPU_STATUS_OK;
  if (pipe_step_wb(cpu, &cur_mem_wb, &wb_status)) {
    return wb_status;
  }

  PipelineIFIDLatch next_if_id = {0};
  PipelineIDEXLatch next_id_ex = {0};
  PipelineEXMEMLatch next_ex_mem = {0};
  PipelineMEMWBLatch next_mem_wb = {0};

  bool mem_stall = false;
  bool mem_redirect = false;
  uint32_t mem_redirect_pc = 0u;
  pipe_step_mem(cpu, &cur_ex_mem, &next_ex_mem, &next_mem_wb, &mem_stall,
                &mem_redirect, &mem_redirect_pc);

  if (mem_redirect) {
    p->stats.control_hazard_flushes++;
  }

  bool ex_stall = false;
  bool ex_redirect = false;
  uint32_t ex_redirect_pc = 0u;
  pipe_step_ex(cpu, &cur_id_ex, &cur_ex_mem, &cur_mem_wb, mem_stall,
               mem_redirect, &next_id_ex, &next_ex_mem, &ex_stall, &ex_redirect,
               &ex_redirect_pc);

  bool id_stall = false;
  if (mem_redirect || ex_redirect) {
    if (cur_if_id.valid) {
      p->stats.bubbles++;
      pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_ID], true, false, true,
                       false, cur_if_id.pc, cur_if_id.ir.opcode);
    }
  } else if (mem_stall || ex_stall) {
    next_if_id = cur_if_id;
    if (cur_if_id.valid) {
      pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_ID], true, true, false,
                       false, cur_if_id.pc, cur_if_id.ir.opcode);
    }
  } else if (cur_if_id.valid) {
    PipelineIDEXLatch decoded_id = {0};
    decoded_id.valid = true;
    decoded_id.pc = cur_if_id.pc;
    decoded_id.ir = cur_if_id.ir;
    decoded_id.fault = cur_if_id.fault;

    if (decoded_id.fault == (uint8_t)CPU_FAULT_NONE) {
      if (control_unit_decode(&cur_if_id.ir, cpu->flags, &decoded_id.ctrl) !=
          CONTROL_UNIT_OK) {
        decoded_id.fault = (uint8_t)CPU_FAULT_INVALID_INSTRUCTION;
      }
    }

    bool load_use = false;
    bool sys_stall = false;
    if (pipeline_detect_data_hazard(&decoded_id, &cur_id_ex, &cur_ex_mem,
                                    &cur_mem_wb, p->config.forwarding_enabled,
                                    &load_use, &sys_stall)) {
      id_stall = true;
      next_if_id = cur_if_id;
      p->stats.stall_cycles++;
      p->stats.bubbles++;
      if (load_use) {
        p->stats.load_use_stalls++;
        p->stats.data_hazard_stalls++;
      } else if (sys_stall) {
        p->stats.control_stalls++;
      } else {
        p->stats.data_hazard_stalls++;
      }
      pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_ID], true, true, false,
                       false, cur_if_id.pc, cur_if_id.ir.opcode);
    } else {
      next_id_ex = decoded_id;
      pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_ID], true, false, false,
                       false, cur_if_id.pc, cur_if_id.ir.opcode);
    }
  }

  /* Step IF according to cascade: mem_redirect > ex_redirect > stalls > hold */
  if (mem_redirect) {
    p->fetch_pc = mem_redirect_pc;
    p->stats.bubbles++;
  } else if (ex_redirect) {
    p->fetch_pc = ex_redirect_pc;
    p->stats.bubbles++;
  } else if (mem_stall || ex_stall || id_stall) {
    pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_IF], cur_if_id.valid, true,
                     false, false, p->fetch_pc, DIOCPU_OP_NOP);
  } else {
    bool serialize_hold =
        (next_id_ex.valid &&
         pipe_slot_serializes_if(&next_id_ex.ir, &next_id_ex.ctrl,
                                 next_id_ex.fault)) ||
        (next_ex_mem.valid &&
         pipe_slot_serializes_if(&next_ex_mem.ir, &next_ex_mem.ctrl,
                                 next_ex_mem.fault)) ||
        (next_mem_wb.valid &&
         pipe_slot_serializes_if(&next_mem_wb.ir, &next_mem_wb.ctrl,
                                 next_mem_wb.fault));

    if (!serialize_hold && !pipe_has_enabled_irq(cpu)) {
      DecodedInstruction fetched = {0};
      DecoderStatus dst = decoder_decode(&cpu->memory, p->fetch_pc, &fetched);
      next_if_id.valid = true;
      next_if_id.pc = p->fetch_pc;
      if (dst == DECODER_STATUS_OK) {
        next_if_id.ir = fetched;
        next_if_id.fault = (uint8_t)CPU_FAULT_NONE;
        p->fetch_pc = fetched.next_pc;
        pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_IF], true, false, false,
                         false, next_if_id.pc, fetched.opcode);
      } else {
        next_if_id.fault = (uint8_t)map_decoder_fault(dst);
        pipe_record_slot(&p->stage_snapshot[PIPE_STAGE_IF], true, false, false,
                         false, next_if_id.pc, DIOCPU_OP_NOP);
      }
    } else if (serialize_hold && !(next_id_ex.valid && next_id_ex.ctrl.is_halt) &&
               !(next_ex_mem.valid && next_ex_mem.ctrl.is_halt) &&
               !(next_mem_wb.valid && next_mem_wb.ctrl.is_halt)) {
      p->stats.control_stalls++;
    }
  }

  p->if_id = next_if_id;
  p->id_ex = next_id_ex;
  p->ex_mem = next_ex_mem;
  p->mem_wb = next_mem_wb;

  /* If pipeline just drained and an enabled IRQ is pending, enter 6-cycle IRQ sequence */
  if (pipe_is_empty(p) && pipe_has_enabled_irq(cpu)) {
    uint8_t vec = 0u;
    if (find_pending_irq(cpu, &vec)) {
      p->irq_entry_active = true;
      p->irq_cycles_remaining = 5u;
      p->pending_irq_vector = vec;
    }
  }

  return CPU_STATUS_OK;
}

CPUStatus cpu_pipeline_run(DioCPU *cpu, uint64_t max_cycles,
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

  uint64_t start_cycles = cpu->pipeline.stats.cycles;

  for (uint64_t i = 0; i < max_cycles; i++) {
    CPUStatus st = cpu_pipeline_step_cycle(cpu);
    if (st != CPU_STATUS_OK) {
      *cycles_executed = cpu->pipeline.stats.cycles - start_cycles;
      return st;
    }
  }

  *cycles_executed = cpu->pipeline.stats.cycles - start_cycles;
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
