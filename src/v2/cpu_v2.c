#include "cpu_v2.h"
#include <stddef.h>

void cpu_v2_init(CpuV2 *cpu)
{
    if (cpu == NULL)
    {
        return;
    }

    for (int i = 0; i < V2_REGISTER_COUNT; i++)
    {
        cpu->r[i] = 0;
    }

    cpu->pc = 0;
    cpu->sp = V2_INITIAL_SP;
    cpu->mar = 0;
    cpu->flags = 0;
    cpu->status = CPU_STATUS_RUNNING;

    cpu->fault.type = FAULT_NONE;
    cpu->fault.address = 0;
}

void cpu_v2_reset(CpuV2 *cpu)
{
    cpu_v2_init(cpu);
}

bool cpu_v2_set_register(CpuV2 *cpu, uint8_t reg_index, uint16_t value)
{
    if (cpu == NULL)
    {
        return false;
    }

    if (reg_index >= V2_REGISTER_COUNT)
    {
        cpu_v2_trigger_fault(cpu, FAULT_INVALID_REGISTER, cpu->pc);
        return false;
    }

    cpu->r[reg_index] = value;
    return true;
}

bool cpu_v2_get_register(const CpuV2 *cpu, uint8_t reg_index, uint16_t *value)
{
    if (cpu == NULL || value == NULL)
    {
        return false;
    }

    if (reg_index >= V2_REGISTER_COUNT)
    {
        return false;
    }

    *value = cpu->r[reg_index];
    return true;
}

void cpu_v2_set_flag(CpuV2 *cpu, uint16_t flag_mask, bool value)
{
    if (cpu == NULL)
    {
        return;
    }

    if (value)
    {
        cpu->flags |= flag_mask;
    }
    else
    {
        cpu->flags &= (uint16_t)(~flag_mask);
    }
}

bool cpu_v2_get_flag(const CpuV2 *cpu, uint16_t flag_mask)
{
    if (cpu == NULL)
    {
        return false;
    }

    return (cpu->flags & flag_mask) != 0;
}

bool cpu_v2_set_pc(CpuV2 *cpu, uint32_t address)
{
    if (cpu == NULL)
    {
        return false;
    }

    if (address > V2_MAX_ADDRESS)
    {
        cpu_v2_trigger_fault(cpu, FAULT_OUT_OF_BOUNDS, address);
        return false;
    }

    cpu->pc = address & V2_ADDRESS_MASK;
    return true;
}

bool cpu_v2_set_sp(CpuV2 *cpu, uint32_t address)
{
    if (cpu == NULL)
    {
        return false;
    }

    if (address > V2_MAX_ADDRESS)
    {
        cpu_v2_trigger_fault(cpu, FAULT_OUT_OF_BOUNDS, address);
        return false;
    }

    cpu->sp = address & V2_ADDRESS_MASK;
    return true;
}

bool cpu_v2_set_mar(CpuV2 *cpu, uint32_t address)
{
    if (cpu == NULL)
    {
        return false;
    }

    if (address > V2_MAX_ADDRESS)
    {
        cpu_v2_trigger_fault(cpu, FAULT_OUT_OF_BOUNDS, address);
        return false;
    }

    cpu->mar = address & V2_ADDRESS_MASK;
    return true;
}

void cpu_v2_trigger_fault(CpuV2 *cpu, FaultType type, uint32_t address)
{
    if (cpu == NULL)
    {
        return;
    }

    cpu->status = CPU_STATUS_FAULTED;
    cpu->fault.type = type;
    cpu->fault.address = address;
}

const char *cpu_v2_fault_name(FaultType type)
{
    switch (type)
    {
        case FAULT_NONE:             return "NONE";
        case FAULT_INVALID_REGISTER: return "INVALID_REGISTER";
        case FAULT_INVALID_OPCODE:   return "INVALID_OPCODE";
        case FAULT_OUT_OF_BOUNDS:    return "OUT_OF_BOUNDS";
        case FAULT_ALIGNMENT_ERROR:  return "ALIGNMENT_ERROR";
        default:                     return "UNKNOWN_FAULT";
    }
}
