#ifndef CPU_V2_H
#define CPU_V2_H

#include <stdbool.h>
#include <stdint.h>

#define V2_REGISTER_COUNT   16
#define V2_ADDRESS_MASK     0x0003FFFFu   // 18-bit address mask (256 KB)
#define V2_MAX_ADDRESS      0x0003FFFFu
#define V2_INITIAL_SP       0x0003FFFEu   // Top of memory, word-aligned

// Flags bitmask (16-bit register)
#define FLAG_ZF             (1u << 0)     // Zero Flag
#define FLAG_CF             (1u << 1)     // Carry Flag
#define FLAG_SF             (1u << 2)     // Sign Flag
#define FLAG_OF             (1u << 3)     // Overflow Flag

typedef enum {
    CPU_STATUS_RUNNING,
    CPU_STATUS_HALTED,
    CPU_STATUS_FAULTED
} CpuStatus;

typedef enum {
    FAULT_NONE = 0,
    FAULT_INVALID_REGISTER,
    FAULT_INVALID_OPCODE,
    FAULT_OUT_OF_BOUNDS,
    FAULT_ALIGNMENT_ERROR
} FaultType;

typedef struct {
    FaultType type;
    uint32_t address;
} CpuFault;

typedef struct {
    uint16_t r[V2_REGISTER_COUNT];  // R0 - R15 (16-bit)
    uint32_t pc;                   // Program Counter (18-bit)
    uint32_t sp;                   // Stack Pointer (18-bit, initial 0x3FFFE)
    uint32_t mar;                  // Memory Address Register (18-bit)
    uint16_t flags;                // FLAGS register (16-bit)
    CpuStatus status;              // RUNNING, HALTED, FAULTED
    CpuFault fault;                // Fault information
} CpuV2;

// Fungsi Lifecycle & State
void cpu_v2_init(CpuV2 *cpu);
void cpu_v2_reset(CpuV2 *cpu);

// Register File Operations
bool cpu_v2_set_register(CpuV2 *cpu, uint8_t reg_index, uint16_t value);
bool cpu_v2_get_register(const CpuV2 *cpu, uint8_t reg_index, uint16_t *value);

// Flags Operations (Bitmask Access)
void cpu_v2_set_flag(CpuV2 *cpu, uint16_t flag_mask, bool value);
bool cpu_v2_get_flag(const CpuV2 *cpu, uint16_t flag_mask);

// Address Registers (18-bit clamping / validation)
bool cpu_v2_set_pc(CpuV2 *cpu, uint32_t address);
bool cpu_v2_set_sp(CpuV2 *cpu, uint32_t address);
bool cpu_v2_set_mar(CpuV2 *cpu, uint32_t address);

// Fault Handling
void cpu_v2_trigger_fault(CpuV2 *cpu, FaultType type, uint32_t address);
const char *cpu_v2_fault_name(FaultType type);

#endif // CPU_V2_H
