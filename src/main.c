#include "cpu.h"

#include "assembler.h"
#include "control_unit.h"
#include "cycle_engine.h"
#include "debugger.h"
#include "microop.h"
#include "pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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
    printf("IVTR  : 0x%05X\n", (unsigned int)cpu->ivtr);

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
    if (cpu->pipeline.stats.cycles > 0u) {
        char pipe_buf[192];
        pipeline_format_summary(&cpu->pipeline, pipe_buf, sizeof(pipe_buf));
        printf("Pipeline summary     : %s\n", pipe_buf);
        printf("Pipeline breakdown   : data_stalls=%llu load_use=%llu ex_stalls=%llu mem_stalls=%llu ctrl_stalls=%llu flushes=%llu bubbles=%llu\n",
               (unsigned long long)cpu->pipeline.stats.data_hazard_stalls,
               (unsigned long long)cpu->pipeline.stats.load_use_stalls,
               (unsigned long long)cpu->pipeline.stats.structural_ex_stalls,
               (unsigned long long)cpu->pipeline.stats.structural_mem_stalls,
               (unsigned long long)cpu->pipeline.stats.control_stalls,
               (unsigned long long)cpu->pipeline.stats.control_hazard_flushes,
               (unsigned long long)cpu->pipeline.stats.bubbles);
    } else {
        printf("Micro-ops executed   : %llu\n",
               (unsigned long long)cpu->uop.microops_executed);
        printf("Clock cycles         : %llu (Stall: %llu, IRQ: %llu)\n",
               (unsigned long long)cpu->cycle.total_cycles,
               (unsigned long long)cpu->cycle.busy_stall_cycles,
               (unsigned long long)cpu->cycle.interrupt_cycles);
        printf("CPI / IPC            : %.2f / %.2f (Last instr: %llu cyc)\n",
               cycle_engine_cpi(&cpu->cycle, cpu->instructions_executed),
               cycle_engine_ipc(&cpu->cycle, cpu->instructions_executed),
               (unsigned long long)cpu->cycle.last_instruction_cycles);
    }
    printf("Interrupts serviced  : %u\n",
           (unsigned int)cpu->interrupt_count);

    if (cpu->datapath.valid) {
        char ctrl_buf[192];
        char uop_buf[192];
        control_unit_format_signals(&cpu->datapath.ctrl, ctrl_buf, sizeof(ctrl_buf));
        microop_format_sequence(&cpu->uop.sequence, uop_buf, sizeof(uop_buf));
        printf("Last IR              : %s @ 0x%05X\n",
               isa_opcode_name(cpu->datapath.ir.opcode),
               (unsigned int)cpu->datapath.ir_pc);
        printf("Last Control Signals : %s\n", ctrl_buf);
        printf("Last Micro-op Seq    : %s\n", uop_buf);
    }

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
    bool use_pipeline = false;
    bool forwarding_enabled = true;
    const char *program_path = NULL;

    if (argc == 2) {
        program_path = argv[1];
    } else if (argc == 3 && strcmp(argv[1], "--pipeline") == 0) {
        use_pipeline = true;
        forwarding_enabled = true;
        program_path = argv[2];
    } else if (argc == 3 && strcmp(argv[1], "--pipeline-no-fwd") == 0) {
        use_pipeline = true;
        forwarding_enabled = false;
        program_path = argv[2];
    } else {
        printf("Usage: diocpu.exe [--pipeline|--pipeline-no-fwd] <program.asm>\n");
        return 1;
    }

    char *source = read_source_file(program_path);

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
    CPUStatus cpu_status = CPU_STATUS_OK;

    if (use_pipeline) {
        cpu_set_pipeline_forwarding(&cpu, forwarding_enabled);
        cpu_status = cpu_pipeline_run(&cpu, 5000000, &steps);
        printf("Execution mode: Pipeline (Forwarding: %s)\n",
               forwarding_enabled ? "ON" : "OFF");
        printf("Execution result: %s\n", cpu_status_string(cpu_status));
        printf("Cycles in this run: %llu\n", (unsigned long long)steps);
    } else {
        cpu_status = cpu_run(&cpu, 1000000, &steps);
        printf("Execution result: %s\n", cpu_status_string(cpu_status));
        printf("Steps in this run: %llu\n", (unsigned long long)steps);
    }

    print_cpu_state(&cpu);


    if (cpu_status == CPU_STATUS_HALTED) {
        return 0;
    }

    if (cpu_status == CPU_STATUS_FAULTED) {
        return 2;
    }

    return 3;
}
