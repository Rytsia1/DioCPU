#include <stdio.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "cpu.h"
#include "assembler.h"

void print_cpu_state(const CPU *cpu) {
    printf("PC: 0x%02X | ", cpu->pc);

    for (int i = 0; i < REGISTER_COUNT; i++) {
        printf("R%d: %3u | ", i, cpu->registers[i]);
    }

    printf("ZF: %d | CF: %d | HALT: %d\n",
           cpu->zero_flag,
           cpu->carry_flag,
           cpu->halted);
}

void print_memory(const CPU *cpu) {
    const uint16_t bytes_per_row = 16;

    printf("\n=== Memory Viewer ===\n");

    // Cetak header kolom
    printf("ADDR  ");

    for (uint16_t column = 0; column < bytes_per_row; column++) {
        printf("%02X ", column);
    }

    printf("\n");

    // Cetak seluruh isi memory
    for (uint16_t address = 0;
         address < MEMORY_SIZE;
         address += bytes_per_row) {

        // Cetak alamat awal setiap baris
        printf("%04X: ", address);

        // Cetak 16 byte dalam hexadecimal
        for (uint16_t column = 0;
             column < bytes_per_row;
             column++) {

            printf("%02X ", cpu->memory[address + column]);
        }

        printf("\n");
    }
}

void print_instruction(const CPU *cpu, uint16_t address) {
    char buffer[64];
    disassemble_instruction(cpu->memory, address, buffer, sizeof(buffer));
    printf("%s", buffer);
}

int main(int argc, char *argv[])
{
    const char *filename = "programs/countdown.asm";

    if (argc > 1)
    {
        filename = argv[1];
    }

    CPU cpu;
    cpu_init(&cpu);

    if (!assemble_and_load(&cpu, filename))
    {
        // Jika tidak diberikan argumen dan default pertama gagal, coba countdown.asm di root
        if (argc == 1 && assemble_and_load(&cpu, "countdown.asm"))
        {
            filename = "countdown.asm";
        }
        else
        {
            printf("Error: Gagal merakit file %s\n", filename);
            return 1;
        }
    }

    // Pastikan PC dan status CPU bersih sebelum dijalankan
    cpu.pc = 0;
    cpu.halted = false;

    printf("\n=== Dio-8 Debugger ===\n\n");

    printf("Initial CPU State:\n");
    print_cpu_state(&cpu);

    print_memory(&cpu);

    printf("\nExecution Trace:\n");

    while (!cpu.halted) {
        uint16_t current_pc = cpu.pc;

        printf("\nExecuting: ");
        print_instruction(&cpu, current_pc);
        printf("\n");

        cpu_step(&cpu);

        print_cpu_state(&cpu);
    }

    printf("\nCPU execution finished.\n");

    return 0;
}
