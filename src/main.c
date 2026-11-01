#include "cpu.h"

#include "assembler.h"
#include "debugger.h"

#include <stdio.h>
#include <stdlib.h>


static char *read_source_file(const char *filename)
{
    FILE *file = fopen(filename, "rb");

    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    long file_size = ftell(file);

    if (file_size < 0) {
        fclose(file);
        return NULL;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    char *buffer = malloc((size_t)file_size + 1u);

    if (buffer == NULL) {
        fclose(file);
        return NULL;
    }

    size_t bytes_read = fread(
        buffer,
        1,
        (size_t)file_size,
        file
    );

    if (bytes_read != (size_t)file_size) {
        free(buffer);
        fclose(file);
        return NULL;
    }

    buffer[bytes_read] = '\0';

    fclose(file);

    return buffer;
}


static void print_cpu_state(const DioCPU *cpu)
{
    printf("\n========== CPU STATE ==========\n");

    printf("State : %s\n", cpu_state_string(cpu->state));

    printf("PC    : 0x%05X\n", (unsigned int)cpu->pc);
    printf("SP    : 0x%05X\n", (unsigned int)cpu->sp);
    printf("MAR   : 0x%05X\n", (unsigned int)cpu->mar);

    printf("FLAGS : 0x%04X\n", (unsigned int)cpu->flags);

    printf("\nGeneral Purpose Registers:\n");

    for (unsigned int i = 0; i < CPU_GPR_COUNT; i++) {

        printf(
            "R%-2u = 0x%04X (%u)\n",
            i,
            (unsigned int)cpu->gpr[i],
            (unsigned int)cpu->gpr[i]
        );
    }

    printf("\nInstructions executed: %llu\n",
           (unsigned long long)cpu->instructions_executed);

    if (cpu->state == CPU_STATE_FAULTED) {

        printf(
            "Fault      : %s\n",
            cpu_fault_string(cpu->fault)
        );

        printf(
            "Fault PC   : 0x%05X\n",
            (unsigned int)cpu->fault_pc
        );
    }

    printf("===============================\n");
}


int main(int argc, char **argv)
{
    if (argc != 2) {
        printf("Usage: diocpu.exe <program.asm>\n");
        return 1;
    }

    char *source = read_source_file(argv[1]);

    if (source == NULL) {
        fprintf(stderr, "Failed to read source file.\n");
        return 1;
    }

    static DioCPU cpu;

    if (cpu_init(&cpu) != CPU_STATUS_OK) {
        fprintf(stderr, "CPU initialization failed.\n");
        free(source);
        return 1;
    }

    AssemblerResult assembly_result;

    AssemblerStatus assembly_status = assembler_assemble(
        source,
        &cpu.memory,
        &assembly_result
    );

    free(source);

    if (assembly_status != ASSEMBLER_STATUS_OK) {

        fprintf(
            stderr,
            "Assembly failed at line %u: %s\n",
            (unsigned int)assembly_result.error_line,
            assembler_status_string(assembly_status)
        );

        return 1;
    }

    if (assembly_result.code_size == 0) {
        fprintf(stderr, "Cannot execute an empty program.\n");
        return 1;
    }

    printf("Assembly successful.\n");

    printf(
        "Program size: %u bytes\n",
        (unsigned int)assembly_result.code_size
    );

    printf(
        "Instruction count: %u\n",
        (unsigned int)assembly_result.instruction_count
    );


    /*
     * Display disassembled machine code
     * before execution.
     */

    printf("\n========== DISASSEMBLY ==========\n");

    DebuggerResult debugger_result;

    DebuggerStatus debugger_status = debugger_disassemble(
        &cpu.memory,
        0,
        assembly_result.code_size,
        stdout,
        &debugger_result
    );

    if (debugger_status != DEBUGGER_STATUS_OK) {
        fprintf(stderr, "Disassembly failed.\n");
        return 1;
    }


    /*
     * Reset architectural state without
     * removing the assembled program.
     */

    cpu_reset(&cpu);


    /*
     * Execute the program.
     */

    printf("\n========== EXECUTION ==========\n");

    uint64_t steps = 0;

    CPUStatus cpu_status = cpu_run(
        &cpu,
        1000000,
        &steps
    );

    printf(
        "Execution result: %s\n",
        cpu_status_string(cpu_status)
    );

    printf("Steps in this run: %llu\n",
           (unsigned long long)steps);

    print_cpu_state(&cpu);


    if (cpu_status == CPU_STATUS_HALTED) {
        return 0;
    }

    if (cpu_status == CPU_STATUS_FAULTED) {
        return 2;
    }

    return 3;
}
