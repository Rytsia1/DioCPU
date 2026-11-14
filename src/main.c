#include "cpu.h"

#include "assembler.h"
#include "branch_predictor.h"
#include "cache.h"
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
        printf("Pipeline breakdown   : data_stalls=%llu load_use=%llu ex_stalls=%llu mem_stalls=%llu icache_stalls=%llu dcache_stalls=%llu ctrl_stalls=%llu flushes=%llu bubbles=%llu\n",
               (unsigned long long)cpu->pipeline.stats.data_hazard_stalls,
               (unsigned long long)cpu->pipeline.stats.load_use_stalls,
               (unsigned long long)cpu->pipeline.stats.structural_ex_stalls,
               (unsigned long long)cpu->pipeline.stats.structural_mem_stalls,
               (unsigned long long)cpu->pipeline.stats.icache_stall_cycles,
               (unsigned long long)cpu->pipeline.stats.dcache_stall_cycles,
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
    if (cpu->cache.enabled) {
        char ic_buf[192];
        char dc_buf[192];
        cache_format_stats(&cpu->cache.icache, "I-Cache", ic_buf, sizeof(ic_buf));
        cache_format_stats(&cpu->cache.dcache, "D-Cache", dc_buf, sizeof(dc_buf));
        printf("%s\n", ic_buf);
        printf("%s\n", dc_buf);
    }
    if (cpu->pipeline.bp.config.mode != BP_MODE_NONE) {
        char bp_buf[192];
        bp_format_stats(&cpu->pipeline.bp, bp_buf, sizeof(bp_buf));
        printf("%s\n", bp_buf);
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


static bool parse_bp_mode(const char *arg, BranchPredictorMode *out_mode)
{
    if (arg == NULL || out_mode == NULL) {
        return false;
    }

    if (strcmp(arg, "none") == 0) {
        *out_mode = BP_MODE_NONE;
        return true;
    }
    if (strcmp(arg, "btfnt") == 0) {
        *out_mode = BP_MODE_STATIC_BTFNT;
        return true;
    }
    if (strcmp(arg, "1bit") == 0) {
        *out_mode = BP_MODE_1BIT;
        return true;
    }
    if (strcmp(arg, "2bit") == 0) {
        *out_mode = BP_MODE_2BIT;
        return true;
    }
    if (strcmp(arg, "gshare") == 0) {
        *out_mode = BP_MODE_GSHARE;
        return true;
    }

    return false;
}


int main(int argc, char **argv)
{
    bool use_pipeline = false;
    bool forwarding_enabled = true;
    bool cache_enabled = false;
    BranchPredictorMode bp_mode = BP_MODE_NONE;
    const char *program_path = NULL;

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (strcmp(arg, "--cache") == 0) {
            cache_enabled = true;
        } else if (strcmp(arg, "--pipeline") == 0) {
            use_pipeline = true;
            forwarding_enabled = true;
        } else if (strcmp(arg, "--pipeline-no-fwd") == 0) {
            use_pipeline = true;
            forwarding_enabled = false;
        } else if (strcmp(arg, "--pipeline-cache") == 0) {
            use_pipeline = true;
            forwarding_enabled = true;
            cache_enabled = true;
        } else if (strncmp(arg, "--bp=", 5) == 0) {
            if (!parse_bp_mode(arg + 5, &bp_mode)) {
                printf("Unknown branch predictor mode: %s\n", arg + 5);
                return 1;
            }
            if (bp_mode != BP_MODE_NONE) {
                use_pipeline = true;
            }
        } else if (strcmp(arg, "--bp") == 0 && (i + 1) < argc) {
            if (!parse_bp_mode(argv[++i], &bp_mode)) {
                printf("Unknown branch predictor mode: %s\n", argv[i]);
                return 1;
            }
            if (bp_mode != BP_MODE_NONE) {
                use_pipeline = true;
            }
        } else if (arg[0] != '-' && program_path == NULL) {
            program_path = arg;
        } else {
            printf("Usage: diocpu.exe [--cache] [--pipeline|--pipeline-no-fwd|--pipeline-cache] [--bp=<none|btfnt|1bit|2bit|gshare>] <program.asm>\n");
            return 1;
        }
    }

    if (program_path == NULL) {
        printf("Usage: diocpu.exe [--cache] [--pipeline|--pipeline-no-fwd|--pipeline-cache] [--bp=<none|btfnt|1bit|2bit|gshare>] <program.asm>\n");
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

    cpu_set_cache_enabled(&cpu, cache_enabled);

    if (use_pipeline) {
        cpu_set_pipeline_forwarding(&cpu, forwarding_enabled);
        cpu_set_branch_predictor_mode(&cpu, bp_mode);
        cpu_status = cpu_pipeline_run(&cpu, 5000000, &steps);
        printf("Execution mode: Pipeline (Forwarding: %s, Cache: %s, BP: %s)\n",
               forwarding_enabled ? "ON" : "OFF",
               cache_enabled ? "ON" : "OFF",
               bp_mode_name(bp_mode));
        printf("Execution result: %s\n", cpu_status_string(cpu_status));
        printf("Cycles in this run: %llu\n", (unsigned long long)steps);
    } else {
        cpu_status = cpu_run(&cpu, 1000000, &steps);
        if (cache_enabled) {
            printf("Execution mode: Single/Micro-op (Cache: ON)\n");
        }
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
