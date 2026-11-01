#ifndef DIOCPU_V2_H
#define DIOCPU_V2_H

#include "memory.h"

#include <stdint.h>

#define CPU_GPR_COUNT 16u

#ifndef CPU_FLAG_ZF
#define CPU_FLAG_ZF (1u << 0)
#endif
#ifndef CPU_FLAG_CF
#define CPU_FLAG_CF (1u << 1)
#endif
#ifndef CPU_FLAG_SF
#define CPU_FLAG_SF (1u << 2)
#endif
#ifndef CPU_FLAG_OF
#define CPU_FLAG_OF (1u << 3)
#endif

#ifndef CPU_FLAGS_DEFINED_MASK
#define CPU_FLAGS_DEFINED_MASK 0x000F
#endif

typedef enum {
    CPU_STATE_RUNNING = 0,
    CPU_STATE_HALTED,
    CPU_STATE_FAULTED
} CPUState;

typedef enum {
    CPU_FAULT_NONE = 0,
    CPU_FAULT_INVALID_OPCODE,
    CPU_FAULT_INVALID_INSTRUCTION,
    CPU_FAULT_MEMORY_ACCESS_FAULT,
    CPU_FAULT_DIVIDE_BY_ZERO,
    CPU_FAULT_ARITHMETIC_OVERFLOW,
    CPU_FAULT_STACK_OVERFLOW,
    CPU_FAULT_STACK_UNDERFLOW,
    CPU_FAULT_INSTRUCTION_FETCH_FAULT
} CPUFault;

typedef enum {
    CPU_STATUS_OK = 0,
    CPU_STATUS_INVALID_ARGUMENT,
    CPU_STATUS_HALTED,
    CPU_STATUS_FAULTED,
    CPU_STATUS_STEP_LIMIT_REACHED
} CPUStatus;

typedef struct {
    uint16_t gpr[CPU_GPR_COUNT];

    uint32_t pc;
    uint32_t sp;
    uint32_t mar;

    uint16_t flags;

    CPUState state;

    CPUFault fault;
    uint32_t fault_pc;

    uint64_t instructions_executed;

    Memory memory;

} DioCPU;


CPUStatus cpu_v2_init(DioCPU *cpu);

CPUStatus cpu_v2_reset(DioCPU *cpu);

CPUStatus cpu_v2_step(DioCPU *cpu);

CPUStatus cpu_v2_run(
    DioCPU *cpu,
    uint64_t max_steps,
    uint64_t *steps_executed
);

const char *cpu_v2_fault_string(CPUFault fault);

const char *cpu_v2_state_string(CPUState state);

const char *cpu_v2_status_string(CPUStatus status);

#endif
