#include <stdio.h>
#include <string.h>

#define main raylib_gui_v2_main
#include "gui.c"
#undef main

#define CHECK(condition)                                               \
    do {                                                               \
        if (!(condition)) {                                            \
            fprintf(stderr, "FAILED: %s:%d: %s\n",                      \
                    __FILE__, __LINE__, #condition);                   \
            return 0;                                                  \
        }                                                              \
    } while (0)

#define RUN_TEST(test_func)                                            \
    do {                                                               \
        if (!test_func()) {                                            \
            return 1;                                                  \
        }                                                              \
        printf("[PASS] %s\n", #test_func);                             \
        passed++;                                                      \
    } while (0)

static DioCPU test_cpu;
static GUIState test_gui;

static void setup_test_gui(void)
{
    cpu_init(&test_cpu);
    memset(&test_gui, 0, sizeof(test_gui));
    test_gui.auto_run = false;
    test_gui.phase_timer = 0.0f;
    test_gui.phase_interval = GUI_DEFAULT_PHASE_INTERVAL;
    test_gui.phase = GUI_PHASE_IDLE;
    test_gui.workspace_view = GUI_WORKSPACE_ASSEMBLY;
    test_gui.memory_view = GUI_MEMORY_VIEW_INSTRUCTION;
    test_gui.stack_follow_sp = true;
    test_gui.stack_view_base = test_cpu.sp;
    test_gui.disassembly_follow_pc = true;
}

static int test_gui_init_and_reset(void)
{
    setup_test_gui();

    CHECK(test_cpu.state == CPU_STATE_RUNNING);
    CHECK(test_cpu.pc == 0x00000);
    CHECK(test_cpu.sp == 0x3FFFE);
    CHECK(test_cpu.mar == 0x00000);
    CHECK(test_cpu.flags == 0x0000);
    CHECK(test_gui.phase == GUI_PHASE_IDLE);
    CHECK(test_gui.auto_run == false);

    /* Write dirty data into CPU state and memory */
    test_cpu.gpr[1] = 0xABCD;
    test_cpu.pc = 0x00100;
    test_cpu.flags = 0x000F;
    test_cpu.memory.instruction[0] = 0x10;
    test_cpu.memory.data[0] = 0x42;

    /* Execute non-destructive reset */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_RESET);

    CHECK(test_cpu.state == CPU_STATE_RUNNING);
    CHECK(test_cpu.pc == 0x00000);
    CHECK(test_cpu.sp == 0x3FFFE);
    CHECK(test_cpu.mar == 0x00000);
    CHECK(test_cpu.gpr[1] == 0x0000);
    CHECK(test_cpu.flags == 0x0000);
    CHECK(test_cpu.instructions_executed == 0);
    CHECK(test_gui.phase == GUI_PHASE_IDLE);

    /* Memory must be preserved */
    CHECK(test_cpu.memory.instruction[0] == 0x10);
    CHECK(test_cpu.memory.data[0] == 0x42);

    return 1;
}

static int test_gui_assembly_loading_and_disassembly_sync(void)
{
    setup_test_gui();

    /* Load valid test program */
    snprintf(test_gui.source_path, sizeof(test_gui.source_path), "programs/test.asm");
    CHECK(gui_load_assembly_program(&test_cpu, &test_gui) == true);

    CHECK(test_gui.assembly_valid == true);
    CHECK(test_gui.assembled_code_size == 21);
    CHECK(test_gui.assembled_instruction_count == 6);
    CHECK(test_gui.disassembly_count == 6);
    CHECK(test_gui.disassembly_follow_pc == true);
    CHECK(test_gui.source_loaded == true);
    CHECK(test_gui.source_line_count > 0);
    CHECK(test_gui.last_assembly_succeeded == true);

    /* Verify first disassembly entry is at 0x00000 */
    CHECK(gui_disassembly_rows[0].address == 0x00000);

    /* Load invalid program and verify error handling */
    snprintf(test_gui.source_path, sizeof(test_gui.source_path), "programs/error_demo.asm");
    CHECK(gui_load_assembly_program(&test_cpu, &test_gui) == false);
    CHECK(test_gui.last_assembly_succeeded == false);
    CHECK(test_gui.assembly_error_line == 2);

    /* Machine code from previous valid program must remain */
    CHECK(test_cpu.memory.instruction[0] == 0x10);

    return 1;
}

static int test_gui_phase_transitions_and_alu_preview(void)
{
    setup_test_gui();

    /* Program: LOADI R1, 10; LOADI R2, 20; ADD R1, R2; HLT */
    const char *prog = "LOADI R1, 10\nLOADI R2, 20\nADD R1, R2\nHLT\n";
    FILE *f = fopen("test_phase_temp.asm", "w");
    CHECK(f != NULL);
    fputs(prog, f);
    fclose(f);

    snprintf(test_gui.source_path, sizeof(test_gui.source_path), "test_phase_temp.asm");
    CHECK(gui_load_assembly_program(&test_cpu, &test_gui) == true);

    /* Step Instruction twice to complete both LOADI */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.gpr[1] == 10);

    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.gpr[2] == 20);

    /* Now at ADD R1, R2 instruction */
    /* Advance Phase: IDLE/EXECUTE -> FETCH */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_PHASE);
    CHECK(test_gui.phase == GUI_PHASE_FETCH);
    CHECK(test_cpu.pc == 0x00008); /* PC still at ADD */

    /* Advance Phase: FETCH -> DECODE */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_PHASE);
    CHECK(test_gui.phase == GUI_PHASE_DECODE);
    CHECK(test_gui.decoded.opcode == DIOCPU_OP_ADD);
    CHECK(test_gui.alu_preview_valid == true);
    CHECK(test_gui.alu_operation == ALU_ADD);
    CHECK(test_gui.alu_input_a == 10);
    CHECK(test_gui.alu_input_b == 20);
    CHECK(test_gui.alu_preview.result == 30);
    CHECK(test_cpu.gpr[1] == 10); /* Not yet committed */

    /* Advance Phase: DECODE -> EXECUTE */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_PHASE);
    CHECK(test_gui.phase == GUI_PHASE_EXECUTE);
    CHECK(test_cpu.gpr[1] == 30); /* Committed to Register File */
    CHECK(test_cpu.pc == 0x0000A);

    remove("test_phase_temp.asm");
    return 1;
}

static int test_gui_memory_viewer_sync(void)
{
    setup_test_gui();

    /* Program: LOADI R1, 0x1234; STORE R1, [0x00100]; LOAD R2, [0x00100]; HLT */
    const char *prog =
        "LOADI R1, 0x1234\n"
        "STORE R1, [0x00100]\n"
        "LOAD R2, [0x00100]\n"
        "HLT\n";
    FILE *f = fopen("test_mem_temp.asm", "w");
    CHECK(f != NULL);
    fputs(prog, f);
    fclose(f);

    snprintf(test_gui.source_path, sizeof(test_gui.source_path), "test_mem_temp.asm");
    CHECK(gui_load_assembly_program(&test_cpu, &test_gui) == true);

    /* Execute LOADI R1, 0x1234 */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.gpr[1] == 0x1234);

    /* Execute STORE R1, [0x00100] */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.mar == 0x00100);
    /* Verify little-endian in data memory */
    CHECK(test_cpu.memory.data[0x00100] == 0x34);
    CHECK(test_cpu.memory.data[0x00101] == 0x12);

    /* Test memory navigation synchronization */
    test_gui.memory_view = GUI_MEMORY_VIEW_DATA;
    memory_go_to_cpu_pointer(&test_cpu, &test_gui);
    CHECK(test_gui.memory_base == 0x00100);

    /* Page navigation */
    memory_move_page(&test_gui, 1);
    CHECK(test_gui.memory_base == 0x00100 + GUI_MEMORY_PAGE_SIZE);
    memory_move_page(&test_gui, -1);
    CHECK(test_gui.memory_base == 0x00100);

    /* Execute LOAD R2, [0x00100] */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.gpr[2] == 0x1234);

    remove("test_mem_temp.asm");
    return 1;
}

static int test_gui_stack_viewer_sync(void)
{
    setup_test_gui();

    /* Program:
       LOADI R1, 0x55AA
       PUSH R1
       CALL TEST_SUB
       POP R2
       HLT
       TEST_SUB:
       LOADI R3, 0xCAFE
       RET
    */
    const char *prog =
        "LOADI R1, 0x55AA\n"
        "PUSH R1\n"
        "CALL TEST_SUB\n"
        "POP R2\n"
        "HLT\n"
        "TEST_SUB:\n"
        "LOADI R3, 0xCAFE\n"
        "RET\n";
    FILE *f = fopen("test_stack_temp.asm", "w");
    CHECK(f != NULL);
    fputs(prog, f);
    fclose(f);

    snprintf(test_gui.source_path, sizeof(test_gui.source_path), "test_stack_temp.asm");
    CHECK(gui_load_assembly_program(&test_cpu, &test_gui) == true);

    /* Step LOADI R1 */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.sp == 0x3FFFE);

    /* Step PUSH R1 */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.sp == 0x3FFFC);
    CHECK(test_gui.stack_event_type == GUI_STACK_EVENT_PUSH);
    CHECK(test_gui.stack_event_value == 0x55AA);

    /* Step CALL TEST_SUB (pushes 4-byte 18-bit return address) */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.sp == 0x3FFF8);
    CHECK(test_gui.stack_event_type == GUI_STACK_EVENT_CALL);

    /* Step LOADI R3 */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.gpr[3] == 0xCAFE);

    /* Step RET */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.sp == 0x3FFFC);
    CHECK(test_gui.stack_event_type == GUI_STACK_EVENT_RET);

    /* Step POP R2 */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.sp == 0x3FFFE);
    CHECK(test_cpu.gpr[2] == 0x55AA);
    CHECK(test_gui.stack_event_type == GUI_STACK_EVENT_POP);

    remove("test_stack_temp.asm");
    return 1;
}

static int test_gui_branch_and_flags_highlight(void)
{
    setup_test_gui();

    /* Program:
       LOADI R1, 5
       LOADI R2, 5
       CMP R1, R2
       JZ TARGET_ZERO
       LOADI R3, 99
       HLT
       TARGET_ZERO:
       LOADI R3, 42
       HLT
    */
    const char *prog =
        "LOADI R1, 5\n"
        "LOADI R2, 5\n"
        "CMP R1, R2\n"
        "JZ TARGET_ZERO\n"
        "LOADI R3, 99\n"
        "HLT\n"
        "TARGET_ZERO:\n"
        "LOADI R3, 42\n"
        "HLT\n";
    FILE *f = fopen("test_branch_temp.asm", "w");
    CHECK(f != NULL);
    fputs(prog, f);
    fclose(f);

    snprintf(test_gui.source_path, sizeof(test_gui.source_path), "test_branch_temp.asm");
    CHECK(gui_load_assembly_program(&test_cpu, &test_gui) == true);

    /* LOADI R1, 5 */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    /* LOADI R2, 5 */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);

    /* CMP R1, R2 -> Zero Flag should be set */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK((test_cpu.flags & CPU_FLAG_ZF) != 0);

    /* Verify state tracking registers highlight */
    update_state_tracking(&test_gui, &test_cpu, 0.016f);
    CHECK(test_gui.flag_flash[0] > 0.0f); /* ZF is index 0 */

    /* JZ TARGET_ZERO -> Branch taken */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);

    /* Next instruction should be LOADI R3, 42 */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.gpr[3] == 42);

    /* Step HLT */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.state == CPU_STATE_HALTED);

    remove("test_branch_temp.asm");
    return 1;
}

static int test_gui_controller_lifecycle(void)
{
    setup_test_gui();

    const char *prog = "LOADI R1, 10\nLOADI R2, 20\nHLT\n";
    FILE *f = fopen("test_ctl_temp.asm", "w");
    CHECK(f != NULL);
    fputs(prog, f);
    fclose(f);

    snprintf(test_gui.source_path, sizeof(test_gui.source_path), "test_ctl_temp.asm");
    CHECK(gui_load_assembly_program(&test_cpu, &test_gui) == true);

    /* Start auto-run */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_RUN_PAUSE);
    CHECK(test_gui.auto_run == true);

    /* Pass time < interval */
    update_auto_run(&test_cpu, &test_gui, 0.20f);
    CHECK(test_gui.phase == GUI_PHASE_IDLE);

    /* Reach interval -> advances to FETCH */
    update_auto_run(&test_cpu, &test_gui, 0.16f);
    CHECK(test_gui.phase == GUI_PHASE_FETCH);

    /* Pause midway */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_RUN_PAUSE);
    CHECK(test_gui.auto_run == false);
    CHECK(test_gui.phase == GUI_PHASE_FETCH);

    /* Step phase manually while paused */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_PHASE);
    CHECK(test_gui.phase == GUI_PHASE_DECODE);

    /* Resume auto run until halt */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_RUN_PAUSE);
    CHECK(test_gui.auto_run == true);

    for (int i = 0; i < 20; i++) {
        update_auto_run(&test_cpu, &test_gui, 0.35f);
        if (test_cpu.state == CPU_STATE_HALTED) {
            break;
        }
    }

    CHECK(test_cpu.state == CPU_STATE_HALTED);
    CHECK(test_gui.auto_run == false); /* Auto-stopped */
    CHECK(test_cpu.gpr[1] == 10);
    CHECK(test_cpu.gpr[2] == 20);

    /* Further stepping while halted must do nothing */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_PHASE);
    CHECK(test_cpu.state == CPU_STATE_HALTED);
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.state == CPU_STATE_HALTED);

    remove("test_ctl_temp.asm");
    return 1;
}

static int test_gui_fault_handling_and_auto_stop(void)
{
    setup_test_gui();

    const char *prog = "LOADI R1, 10\nLOADI R2, 0\nDIV R1, R2\nHLT\n";
    FILE *f = fopen("test_fault_temp.asm", "w");
    CHECK(f != NULL);
    fputs(prog, f);
    fclose(f);

    snprintf(test_gui.source_path, sizeof(test_gui.source_path), "test_fault_temp.asm");
    CHECK(gui_load_assembly_program(&test_cpu, &test_gui) == true);

    /* Run until fault occurs */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_RUN_PAUSE);
    CHECK(test_gui.auto_run == true);

    for (int i = 0; i < 20; i++) {
        update_auto_run(&test_cpu, &test_gui, 0.35f);
        if (test_cpu.state == CPU_STATE_FAULTED) {
            break;
        }
    }

    CHECK(test_cpu.state == CPU_STATE_FAULTED);
    CHECK(test_cpu.fault == CPU_FAULT_DIVIDE_BY_ZERO);
    CHECK(test_gui.auto_run == false);

    /* Stepping while faulted must do nothing */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_PHASE);
    CHECK(test_cpu.state == CPU_STATE_FAULTED);
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_STEP_INSTRUCTION);
    CHECK(test_cpu.state == CPU_STATE_FAULTED);

    /* RUN command while faulted must be rejected */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_RUN_PAUSE);
    CHECK(test_gui.auto_run == false);

    /* Reset recovers CPU */
    process_gui_command(&test_cpu, &test_gui, GUI_COMMAND_RESET);
    CHECK(test_cpu.state == CPU_STATE_RUNNING);
    CHECK(test_cpu.fault == CPU_FAULT_NONE);

    remove("test_fault_temp.asm");
    return 1;
}

int main(void)
{
    unsigned int passed = 0;

    printf("=== Running DioCPU GUI Integration Test Suite ===\n\n");

    RUN_TEST(test_gui_init_and_reset);
    RUN_TEST(test_gui_assembly_loading_and_disassembly_sync);
    RUN_TEST(test_gui_phase_transitions_and_alu_preview);
    RUN_TEST(test_gui_memory_viewer_sync);
    RUN_TEST(test_gui_stack_viewer_sync);
    RUN_TEST(test_gui_branch_and_flags_highlight);
    RUN_TEST(test_gui_controller_lifecycle);
    RUN_TEST(test_gui_fault_handling_and_auto_stop);

    printf("\nGUI Integration Tests: %u passed.\n", passed);

    return 0;
}
