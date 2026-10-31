#include "cpu.h"

#include <stdio.h>
#include <string.h>

void cpu_init(CPU *cpu) {
    memset(cpu, 0, sizeof(*cpu));

    cpu->sp = MEMORY_SIZE - 1;
}

bool cpu_step(CPU *cpu) {
    if (cpu->halted) {
        return false;
    }

    // Validate that the complete instruction fits in memory.
    if (cpu->pc + INSTRUCTION_SIZE > MEMORY_SIZE) {
        cpu->halted = true;
        return false;
    }

    // Fetch
    uint8_t opcode = cpu->memory[cpu->pc];
    uint8_t operand1 = cpu->memory[cpu->pc + 1];
    uint8_t operand2 = cpu->memory[cpu->pc + 2];

    // Advance PC before execution.
    cpu->pc += INSTRUCTION_SIZE;

    // Decode and Execute
    switch (opcode) {
        case OP_LOAD:
            if (operand1 >= REGISTER_COUNT) {
                cpu->halted = true;
                return false;
            }

            cpu->registers[operand1] = operand2;
            cpu->zero_flag = (operand2 == 0);

            break;

        case OP_ADD:
            if (operand1 >= REGISTER_COUNT ||
                operand2 >= REGISTER_COUNT) {
                cpu->halted = true;
                return false;
            }

            {
                uint16_t result =
                    (uint16_t)cpu->registers[operand1] +
                    (uint16_t)cpu->registers[operand2];

                cpu->carry_flag = (result > 0xFF);

                cpu->registers[operand1] = (uint8_t)result;
                cpu->zero_flag =
                    (cpu->registers[operand1] == 0);
            }

            break;

        case OP_SUB:
            if (operand1 >= REGISTER_COUNT ||
                operand2 >= REGISTER_COUNT) {
                cpu->halted = true;
                return false;
            }

            {
                uint8_t a = cpu->registers[operand1];
                uint8_t b = cpu->registers[operand2];

                cpu->carry_flag = (a < b);

                cpu->registers[operand1] = a - b;
                cpu->zero_flag =
                    (cpu->registers[operand1] == 0);
            }

            break;

        case OP_JMP:
            cpu->pc = operand1;
            break;

        case OP_JZ:
            if (cpu->zero_flag) {
                cpu->pc = operand1;
            }

            break;

        case OP_JNZ:
            if (!cpu->zero_flag) {
                cpu->pc = operand1;
            }

            break;

        case OP_OUT:
        {
            if (operand1 == OUT_MODE_REGISTER)
            {
                if (operand2 >= REGISTER_COUNT)
                {
                    printf("Error: Invalid register R%u\n", operand2);
                    cpu->halted = true;
                    return false;
                }

                printf("Output: %u\n", cpu->registers[operand2]);
            }
            else if (operand1 == OUT_MODE_IMMEDIATE)
            {
                printf("Output: %u\n", operand2);
            }
            else
            {
                printf("Error: Invalid OUT mode %u\n", operand1);
                cpu->halted = true;
                return false;
            }

            break;
        }

        case OP_HLT:
            cpu->halted = true;
            return false;

        default:
            cpu->halted = true;
            return false;
    }

    return true;
}

void cpu_run(CPU *cpu) {
    while (!cpu->halted) {
        if (!cpu_step(cpu)) {
            break;
        }
    }
}
