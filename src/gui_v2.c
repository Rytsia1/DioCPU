#include "raylib.h"

#include "cpu_v2.h"
#include "assembler.h"
#include "decoder.h"
#include "memory.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

/* =========================================
   WINDOW CONFIGURATION
   ========================================= */

#define SCREEN_WIDTH 1600
#define SCREEN_HEIGHT 1000

#define FPS 60

/* =========================================
   COLOR PALETTE
   ========================================= */

#define COLOR_BACKGROUND  (Color){18, 22, 30, 255}
#define COLOR_PANEL       (Color){30, 36, 47, 255}
#define COLOR_PANEL_ALT   (Color){38, 45, 58, 255}
#define COLOR_BORDER      (Color){60, 72, 90, 255}

#define COLOR_TEXT        (Color){230, 235, 245, 255}
#define COLOR_MUTED       (Color){135, 148, 168, 255}
#define COLOR_ACCENT      (Color){75, 175, 235, 255}

#define COLOR_GREEN       (Color){60, 220, 130, 255}
#define COLOR_YELLOW      (Color){245, 185, 55, 255}
#define COLOR_RED         (Color){235, 75, 75, 255}
#define COLOR_CYAN        (Color){70, 195, 245, 255}

/* Color aliases requested by GUI-V2-3 specification */
#define FETCH_COLOR       COLOR_YELLOW
#define DECODE_COLOR      COLOR_CYAN
#define EXECUTE_COLOR     COLOR_GREEN
#define FAULT_COLOR       COLOR_RED
#define PANEL_BG          COLOR_PANEL
#define TEXT_COLOR        COLOR_TEXT
#define ACCENT_COLOR      COLOR_ACCENT

/* =========================================
   GUI STATE & TRACKING
   ========================================= */

typedef enum {
    GUI_PHASE_IDLE = 0,
    GUI_PHASE_FETCH,
    GUI_PHASE_DECODE,
    GUI_PHASE_EXECUTE
} GUIPhase;

typedef enum {
    MEMORY_VIEW_INSTRUCTION = 0,
    MEMORY_VIEW_DATA,
    MEMORY_VIEW_STACK
} MemoryView;

#define CHANGE_HIGHLIGHT_DURATION 0.85f

#define GUI_MAX_INSTRUCTION_BYTES 5

typedef struct {

    /* =================================
       GENERAL GUI INFORMATION
       ================================= */

    char status_message[180];

    Color status_color;

    bool initialization_failed;


    /* =================================
       EXECUTION PHASE
       ================================= */

    GUIPhase phase;

    uint32_t instruction_pc;

    uint8_t fetched_opcode;

    uint8_t fetched_bytes[GUI_MAX_INSTRUCTION_BYTES];

    uint8_t fetched_byte_count;

    bool fetch_valid;

    DecodedInstruction decoded;

    DecoderStatus decode_status;

    CPUStatus last_cpu_status;


    /* =================================
       AUTOMATIC EXECUTION
       ================================= */

    bool auto_run;

    float phase_timer;

    float phase_interval;


    /* =================================
       MEMORY VIEWER
       ================================= */

    MemoryView memory_view;

    uint32_t memory_base;

    uint32_t program_size;

    uint32_t program_list_start;


    /* =================================
       ASSEMBLY INPUT
       ================================= */

    char source_path[256];

    bool path_focused;


    /* =================================
       REGISTER CHANGE TRACKING
       ================================= */

    uint16_t previous_registers[CPU_GPR_COUNT];

    uint16_t old_register_values[CPU_GPR_COUNT];

    float register_flash[CPU_GPR_COUNT];


    /* =================================
       FLAGS CHANGE TRACKING
       ================================= */

    uint16_t previous_flags;

    bool old_flag_values[4];

    float flag_flash[4];

    bool snapshot_initialized;

} GUIState;

static const uint16_t FLAG_MASKS[4] = {
    CPU_FLAG_ZF,
    CPU_FLAG_CF,
    CPU_FLAG_SF,
    CPU_FLAG_OF
};

/* =========================================
   STATE TRACKING FUNCTIONS
   ========================================= */

static void initialize_state_tracking(
    GUIState *gui,
    const DioCPU *cpu
)
{
    memcpy(
        gui->previous_registers,
        cpu->gpr,
        sizeof(cpu->gpr)
    );

    gui->previous_flags = cpu->flags;

    for (unsigned int i = 0; i < CPU_GPR_COUNT; i++) {

        gui->old_register_values[i] = cpu->gpr[i];

        gui->register_flash[i] = 0.0f;
    }

    for (unsigned int i = 0; i < 4; i++) {

        gui->old_flag_values[i] =
            (cpu->flags & FLAG_MASKS[i]) != 0;

        gui->flag_flash[i] = 0.0f;
    }

    gui->snapshot_initialized = true;
}

static void update_state_tracking(
    GUIState *gui,
    const DioCPU *cpu,
    float delta_time
)
{
    if (!gui->snapshot_initialized) {

        initialize_state_tracking(gui, cpu);

        return;
    }


    /* =====================================
       REGISTER COMPARISON
       ===================================== */

    for (unsigned int i = 0;
         i < CPU_GPR_COUNT;
         i++) {

        if (gui->register_flash[i] > 0.0f) {

            gui->register_flash[i] -= delta_time;

            if (gui->register_flash[i] < 0.0f) {
                gui->register_flash[i] = 0.0f;
            }
        }

        uint16_t current_value = cpu->gpr[i];

        uint16_t previous_value =
            gui->previous_registers[i];


        if (current_value != previous_value) {

            gui->old_register_values[i] =
                previous_value;

            gui->previous_registers[i] =
                current_value;

            gui->register_flash[i] =
                CHANGE_HIGHLIGHT_DURATION;
        }
    }


    /* =====================================
       FLAGS COMPARISON
       ===================================== */

    for (unsigned int i = 0; i < 4; i++) {

        if (gui->flag_flash[i] > 0.0f) {

            gui->flag_flash[i] -= delta_time;

            if (gui->flag_flash[i] < 0.0f) {
                gui->flag_flash[i] = 0.0f;
            }
        }

        bool current_value =
            (cpu->flags & FLAG_MASKS[i]) != 0;

        bool previous_value =
            (gui->previous_flags & FLAG_MASKS[i]) != 0;


        if (current_value != previous_value) {

            gui->old_flag_values[i] =
                previous_value;

            gui->flag_flash[i] =
                CHANGE_HIGHLIGHT_DURATION;
        }
    }

    gui->previous_flags = cpu->flags;
}

/* =========================================
   INSTRUCTION FORMATTING HELPER
   ========================================= */

static const char *get_opcode_name(InstructionOpcode opcode)
{
    switch (opcode) {
        case DIOCPU_OP_NOP:   return "NOP";
        case DIOCPU_OP_HLT:   return "HLT";
        case DIOCPU_OP_JMP:   return "JMP";
        case DIOCPU_OP_CALL:  return "CALL";
        case DIOCPU_OP_RET:   return "RET";

        case DIOCPU_OP_LOADI: return "LOADI";
        case DIOCPU_OP_LOAD:  return "LOAD";
        case DIOCPU_OP_STORE: return "STORE";
        case DIOCPU_OP_PUSH:  return "PUSH";
        case DIOCPU_OP_POP:   return "POP";

        case DIOCPU_OP_ADD:   return "ADD";
        case DIOCPU_OP_SUB:   return "SUB";
        case DIOCPU_OP_MUL:   return "MUL";
        case DIOCPU_OP_MULS:  return "MULS";
        case DIOCPU_OP_DIV:   return "DIV";
        case DIOCPU_OP_DIVS:  return "DIVS";

        case DIOCPU_OP_AND:   return "AND";
        case DIOCPU_OP_OR:    return "OR";
        case DIOCPU_OP_XOR:   return "XOR";
        case DIOCPU_OP_NOT:   return "NOT";

        case DIOCPU_OP_CMP:   return "CMP";

        case DIOCPU_OP_JZ:    return "JZ";
        case DIOCPU_OP_JNZ:   return "JNZ";
        case DIOCPU_OP_JC:    return "JC";
        case DIOCPU_OP_JNC:   return "JNC";

        default:              return "UNKNOWN";
    }
}

static void format_instruction(
    const DecodedInstruction *instruction,
    char *buffer,
    size_t size
)
{
    if (buffer == NULL || size == 0) {
        return;
    }

    const char *name = get_opcode_name(instruction->opcode);

    switch (instruction->format) {
        case INSTRUCTION_FORMAT_NONE:
            snprintf(buffer, size, "%s", name);
            break;

        case INSTRUCTION_FORMAT_REGISTER:
            snprintf(buffer, size, "%s R%u", name, (unsigned int)instruction->reg_a);
            break;

        case INSTRUCTION_FORMAT_REGISTER_REGISTER:
            snprintf(
                buffer,
                size,
                "%s R%u, R%u",
                name,
                (unsigned int)instruction->reg_a,
                (unsigned int)instruction->reg_b
            );
            break;

        case INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16:
            snprintf(
                buffer,
                size,
                "%s R%u, 0x%04X",
                name,
                (unsigned int)instruction->reg_a,
                (unsigned int)instruction->immediate16
            );
            break;

        case INSTRUCTION_FORMAT_REGISTER_ABSOLUTE18:
            snprintf(
                buffer,
                size,
                "%s R%u, [0x%05X]",
                name,
                (unsigned int)instruction->reg_a,
                (unsigned int)instruction->absolute_address
            );
            break;

        case INSTRUCTION_FORMAT_ABSOLUTE18:
            snprintf(
                buffer,
                size,
                "%s 0x%05X",
                name,
                (unsigned int)instruction->absolute_address
            );
            break;

        case INSTRUCTION_FORMAT_RELATIVE8:
        {
            int64_t target =
                (int64_t)instruction->next_pc +
                (int64_t)instruction->relative_offset;

            snprintf(
                buffer,
                size,
                "%s %+d -> 0x%05X",
                name,
                (int)instruction->relative_offset,
                (unsigned int)target
            );
            break;
        }

        default:
            snprintf(buffer, size, "%s ???", name);
            break;
    }
}

/* =========================================
   STEP 2 — RESET CYCLE
   ========================================= */

static void reset_gui_cycle(GUIState *gui)
{
    gui->phase = GUI_PHASE_IDLE;

    gui->instruction_pc = 0;

    gui->fetched_opcode = 0;

    gui->fetched_byte_count = 0;

    gui->fetch_valid = false;

    memset(
        gui->fetched_bytes,
        0,
        sizeof(gui->fetched_bytes)
    );

    gui->decoded = (DecodedInstruction){0};

    gui->decode_status = DECODER_STATUS_OK;

    gui->last_cpu_status = CPU_STATUS_OK;

    gui->auto_run = false;

    gui->phase_timer = 0.0f;

    snprintf(
        gui->status_message,
        sizeof(gui->status_message),
        "Ready. Waiting for instruction."
    );

    gui->status_color = COLOR_GREEN;
}

/* =========================================
   STEP 3 — FETCH PHASE
   ========================================= */

static void phase_fetch(
    DioCPU *cpu,
    GUIState *gui
)
{
    gui->instruction_pc = cpu->pc;

    gui->fetched_opcode = 0;

    gui->fetched_byte_count = 0;

    gui->fetch_valid = false;

    memset(
        gui->fetched_bytes,
        0,
        sizeof(gui->fetched_bytes)
    );

    /*
     * Fetch opcode from Instruction Memory.
     */

    MemoryStatus status = memory_instruction_read8(
        &cpu->memory,
        cpu->pc,
        &gui->fetched_opcode
    );

    if (status != MEMORY_STATUS_OK) {

        gui->decode_status =
            DECODER_STATUS_INSTRUCTION_FETCH_FAULT;

        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "FETCH FAILED: Invalid instruction address."
        );

        gui->status_color = COLOR_RED;

    } else {

        gui->fetch_valid = true;

        gui->fetched_bytes[0] = gui->fetched_opcode;

        gui->fetched_byte_count = 1;

        gui->decode_status = DECODER_STATUS_OK;

        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "FETCH: Opcode 0x%02X from address 0x%05X.",
            gui->fetched_opcode,
            (unsigned int)cpu->pc
        );

        gui->status_color = COLOR_YELLOW;
    }

    gui->phase = GUI_PHASE_FETCH;
}

/* =========================================
   STEP 4 — DECODE PHASE
   ========================================= */

static void phase_decode(
    DioCPU *cpu,
    GUIState *gui
)
{
    if (!gui->fetch_valid) {

        gui->decode_status =
            DECODER_STATUS_INSTRUCTION_FETCH_FAULT;

        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "DECODE FAILED: Fetch was unsuccessful."
        );

        gui->status_color = COLOR_RED;

        gui->phase = GUI_PHASE_DECODE;

        return;
    }


    /*
     * Decode instruction using actual decoder.
     */

    gui->decode_status = decoder_decode(
        &cpu->memory,
        gui->instruction_pc,
        &gui->decoded
    );

    if (gui->decode_status != DECODER_STATUS_OK) {

        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "DECODE FAILED: Decoder status %d.",
            (int)gui->decode_status
        );

        gui->status_color = COLOR_RED;

        gui->phase = GUI_PHASE_DECODE;

        return;
    }


    /*
     * Capture complete instruction bytes.
     */

    gui->fetched_byte_count = gui->decoded.length;

    for (uint8_t i = 0;
         i < gui->decoded.length && i < GUI_MAX_INSTRUCTION_BYTES;
         i++) {

        gui->fetched_bytes[i] =
            cpu->memory.instruction[
                gui->instruction_pc + i
            ];
    }


    /*
     * Format decoded instruction.
     */

    char instruction_text[100];

    format_instruction(
        &gui->decoded,
        instruction_text,
        sizeof(instruction_text)
    );


    snprintf(
        gui->status_message,
        sizeof(gui->status_message),
        "DECODE: %s | Length: %u byte(s).",
        instruction_text,
        (unsigned int)gui->decoded.length
    );

    gui->status_color = COLOR_CYAN;

    gui->phase = GUI_PHASE_DECODE;
}

/* =========================================
   STEP 5 — EXECUTE PHASE
   ========================================= */

static void phase_execute(
    DioCPU *cpu,
    GUIState *gui
)
{
    /*
     * Execute instruction through actual CPU engine.
     */

    gui->last_cpu_status = cpu_v2_step(cpu);


    if (cpu->state == CPU_STATE_FAULTED) {

        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "FAULT: %s at 0x%05X",
            cpu_v2_fault_string(cpu->fault),
            (unsigned int)cpu->fault_pc
        );

        gui->status_color = COLOR_RED;

        gui->auto_run = false;

    } else if (cpu->state == CPU_STATE_HALTED) {

        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "CPU HALTED. Execution completed."
        );

        gui->status_color = COLOR_YELLOW;

        gui->auto_run = false;

    } else {

        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "EXECUTE: Instruction completed. Next PC: 0x%05X.",
            (unsigned int)cpu->pc
        );

        gui->status_color = COLOR_GREEN;
    }

    gui->phase = GUI_PHASE_EXECUTE;
}

/* =========================================
   STEP 6 — PHASE CONTROLLER
   ========================================= */

static void advance_cpu_phase(
    DioCPU *cpu,
    GUIState *gui
)
{
    if (cpu->state != CPU_STATE_RUNNING) {

        gui->auto_run = false;

        return;
    }


    switch (gui->phase) {

        case GUI_PHASE_IDLE:
        case GUI_PHASE_EXECUTE:

            phase_fetch(cpu, gui);

            break;


        case GUI_PHASE_FETCH:

            phase_decode(cpu, gui);

            break;


        case GUI_PHASE_DECODE:

            phase_execute(cpu, gui);

            break;


        default:

            reset_gui_cycle(gui);

            break;
    }
}

/* =========================================
   STEP 7 — SINGLE INSTRUCTION STEP
   ========================================= */

static void execute_one_instruction(
    DioCPU *cpu,
    GUIState *gui
)
{
    if (cpu->state != CPU_STATE_RUNNING) {
        return;
    }


    /*
     * If the previous instruction has completed,
     * begin the next instruction.
     */

    if (gui->phase == GUI_PHASE_EXECUTE) {
        gui->phase = GUI_PHASE_IDLE;
    }


    /*
     * Complete the remaining phases.
     */

    unsigned int transitions = 0;

    while (
        cpu->state == CPU_STATE_RUNNING &&
        gui->phase != GUI_PHASE_EXECUTE &&
        transitions < 3
    ) {

        advance_cpu_phase(cpu, gui);

        transitions++;
    }
}

/* =========================================
   LOAD ASSEMBLY PROGRAM
   ========================================= */

static void load_assembly_program(DioCPU *cpu, GUIState *gui)
{
    FILE *fp = fopen(gui->source_path, "rb");
    if (!fp) {
        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "Could not open source file: %.140s",
            gui->source_path
        );
        gui->status_color = COLOR_RED;
        return;
    }

    fseek(fp, 0, SEEK_END);
    long fsize = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (fsize < 0 || fsize > 65536) {
        fclose(fp);
        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "File size invalid or too large."
        );
        gui->status_color = COLOR_RED;
        return;
    }

    char *source = (char *)malloc((size_t)fsize + 1);
    if (!source) {
        fclose(fp);
        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "Memory allocation failed."
        );
        gui->status_color = COLOR_RED;
        return;
    }

    size_t read_bytes = fread(source, 1, (size_t)fsize, fp);
    source[read_bytes] = '\0';
    fclose(fp);

    cpu_v2_reset(cpu);

    AssemblerResult result = {0};
    AssemblerStatus status = assembler_assemble(source, &cpu->memory, &result);
    free(source);

    if (status != ASSEMBLER_STATUS_OK) {
        snprintf(
            gui->status_message,
            sizeof(gui->status_message),
            "Assemble error line %u: %s",
            (unsigned int)result.error_line,
            assembler_status_string(status)
        );
        gui->status_color = COLOR_RED;
        return;
    }

    gui->program_size = result.code_size;
    reset_gui_cycle(gui);
    initialize_state_tracking(gui, cpu);

    snprintf(
        gui->status_message,
        sizeof(gui->status_message),
        "Program loaded: %u bytes (%u instructions). Ready.",
        (unsigned int)result.code_size,
        (unsigned int)result.instruction_count
    );
    gui->status_color = COLOR_GREEN;
}

/* =========================================
   BASIC UI DRAWING HELPERS
   ========================================= */

static void draw_panel(
    int x,
    int y,
    int width,
    int height,
    const char *title
)
{
    DrawRectangle(
        x,
        y,
        width,
        height,
        COLOR_PANEL
    );

    DrawRectangleLines(
        x,
        y,
        width,
        height,
        COLOR_BORDER
    );

    DrawText(
        title,
        x + 16,
        y + 13,
        18,
        COLOR_ACCENT
    );
}

static void draw_metric_card(
    int x,
    int y,
    int width,
    int height,
    const char *label,
    const char *value,
    Color value_color
)
{
    DrawRectangle(
        x,
        y,
        width,
        height,
        COLOR_PANEL
    );

    DrawRectangleLines(
        x,
        y,
        width,
        height,
        COLOR_BORDER
    );

    DrawText(
        label,
        x + 15,
        y + 12,
        13,
        COLOR_MUTED
    );

    DrawText(
        value,
        x + 15,
        y + 38,
        23,
        value_color
    );
}

static bool draw_button(
    Rectangle bounds,
    const char *label
)
{
    Vector2 mouse = GetMousePosition();

    bool hovered = CheckCollisionPointRec(
        mouse,
        bounds
    );

    Color background = hovered
        ? (Color){55, 85, 110, 255}
        : COLOR_PANEL_ALT;

    DrawRectangleRec(
        bounds,
        background
    );

    DrawRectangleLinesEx(
        bounds,
        1,
        hovered ? COLOR_ACCENT : COLOR_BORDER
    );

    int text_width = MeasureText(label, 14);

    DrawText(
        label,
        (int)(bounds.x +
            (bounds.width - text_width) / 2),
        (int)(bounds.y + 12),
        14,
        COLOR_TEXT
    );

    return hovered &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* =========================================
   HEADER & INTERACTIVE CONTROLS
   ========================================= */

static void draw_header(DioCPU *cpu, GUIState *gui)
{
    DrawText(
        "DioCPU V2",
        22,
        18,
        28,
        COLOR_ACCENT
    );

    DrawText(
        "16-BIT ARCHITECTURE | FETCH-DECODE-EXECUTE VISUALIZER",
        22,
        52,
        12,
        COLOR_MUTED
    );

    DrawText(
        gui->status_message,
        410,
        28,
        14,
        gui->status_color
    );

    /* Buttons: RUN/PAUSE, STEP PHASE, STEP INSTR, RESET, LOAD ASM */

    Rectangle run_btn = {960, 20, 110, 42};
    const char *run_label = gui->auto_run ? "PAUSE (F5)" : "RUN (F5)";
    if (draw_button(run_btn, run_label)) {
        if (cpu->state == CPU_STATE_RUNNING) {
            gui->auto_run = !gui->auto_run;
            gui->phase_timer = 0.0f;
        }
    }

    Rectangle step_phase_btn = {1080, 20, 115, 42};
    if (draw_button(step_phase_btn, "PHASE (F8)")) {
        gui->auto_run = false;
        advance_cpu_phase(cpu, gui);
    }

    Rectangle step_instr_btn = {1205, 20, 115, 42};
    if (draw_button(step_instr_btn, "INSTR (F10)")) {
        gui->auto_run = false;
        execute_one_instruction(cpu, gui);
    }

    Rectangle reset_btn = {1330, 20, 110, 42};
    if (draw_button(reset_btn, "RESET (F6)")) {
        cpu_v2_reset(cpu);
        reset_gui_cycle(gui);
    }

    Rectangle load_btn = {1450, 20, 130, 42};
    if (draw_button(load_btn, "LOAD ASM")) {
        load_assembly_program(cpu, gui);
    }
}

/* =========================================
   CPU DASHBOARD
   ========================================= */

static Color get_cpu_state_color(CPUState state)
{
    switch (state) {
        case CPU_STATE_RUNNING:
            return COLOR_GREEN;
        case CPU_STATE_HALTED:
            return COLOR_YELLOW;
        case CPU_STATE_FAULTED:
            return COLOR_RED;
        default:
            return COLOR_MUTED;
    }
}

static void draw_cpu_dashboard(const DioCPU *cpu)
{
    char pc_text[32];
    char sp_text[32];
    char mar_text[32];
    char flags_text[32];

    snprintf(
        pc_text,
        sizeof(pc_text),
        "0x%05X",
        (unsigned int)cpu->pc
    );

    snprintf(
        sp_text,
        sizeof(sp_text),
        "0x%05X",
        (unsigned int)cpu->sp
    );

    snprintf(
        mar_text,
        sizeof(mar_text),
        "0x%05X",
        (unsigned int)cpu->mar
    );

    snprintf(
        flags_text,
        sizeof(flags_text),
        "0x%04X",
        (unsigned int)cpu->flags
    );

    draw_metric_card(
        20, 85, 295, 75,
        "PROGRAM COUNTER",
        pc_text,
        COLOR_YELLOW
    );

    draw_metric_card(
        335, 85, 295, 75,
        "STACK POINTER",
        sp_text,
        COLOR_CYAN
    );

    draw_metric_card(
        650, 85, 295, 75,
        "MEMORY ADDRESS REGISTER",
        mar_text,
        COLOR_CYAN
    );

    draw_metric_card(
        965, 85, 295, 75,
        "FLAGS REGISTER",
        flags_text,
        COLOR_ACCENT
    );

    char state_text[40];
    snprintf(
        state_text,
        sizeof(state_text),
        "%s",
        cpu_v2_state_string(cpu->state)
    );

    draw_metric_card(
        1280, 85, 300, 75,
        "CPU STATE",
        state_text,
        get_cpu_state_color(cpu->state)
    );
}

/* =========================================
   STEP 9 — INSTRUCTION CYCLE PANEL
   ========================================= */

static void draw_cycle_panel(
    const DioCPU *cpu,
    const GUIState *gui
)
{
    draw_panel(
        430,
        175,
        1150,
        145,
        "INSTRUCTION CYCLE"
    );

    const int x_positions[] = {
        455, 820, 1185
    };

    const Color phase_colors[] = {
        FETCH_COLOR,
        DECODE_COLOR,
        EXECUTE_COLOR
    };

    const char *phase_names[] = {
        "1. FETCH",
        "2. DECODE",
        "3. EXECUTE"
    };

    for (int i = 0; i < 3; i++) {

        GUIPhase phase = (GUIPhase)(i + 1);

        bool active = gui->phase == phase;

        bool completed = gui->phase > phase;

        Color color = active
            ? phase_colors[i]
            : (completed ? COLOR_GREEN : COLOR_BORDER);

        Color background = active
            ? (Color){45, 50, 58, 255}
            : PANEL_BG;

        Rectangle card = {
            (float)x_positions[i],
            220,
            325,
            75
        };

        DrawRectangleRec(card, background);

        DrawRectangleLinesEx(
            card,
            active ? 2.0f : 1.0f,
            color
        );

        DrawText(
            phase_names[i],
            (int)card.x + 15,
            (int)card.y + 8,
            17,
            color
        );


        /* =========================
           FETCH CARD
           ========================= */

        if (i == 0) {

            if (gui->phase == GUI_PHASE_IDLE) {

                DrawText(
                    TextFormat(
                        "PC: %05X | Waiting...",
                        (unsigned int)cpu->pc
                    ),
                    (int)card.x + 15,
                    (int)card.y + 43,
                    13,
                    COLOR_MUTED
                );

            } else if (gui->fetch_valid) {

                DrawText(
                    TextFormat(
                        "PC: %05X | OPCODE: %02X",
                        (unsigned int)gui->instruction_pc,
                        gui->fetched_opcode
                    ),
                    (int)card.x + 15,
                    (int)card.y + 43,
                    13,
                    TEXT_COLOR
                );

            } else {

                DrawText(
                    "Instruction fetch failed.",
                    (int)card.x + 15,
                    (int)card.y + 43,
                    13,
                    FAULT_COLOR
                );
            }
        }


        /* =========================
           DECODE CARD
           ========================= */

        else if (i == 1) {

            if (gui->phase < GUI_PHASE_DECODE) {

                DrawText(
                    "Waiting for Fetch...",
                    (int)card.x + 15,
                    (int)card.y + 43,
                    13,
                    COLOR_MUTED
                );

            } else if (
                gui->decode_status == DECODER_STATUS_OK &&
                gui->fetch_valid
            ) {

                char text[100];

                format_instruction(
                    &gui->decoded,
                    text,
                    sizeof(text)
                );

                DrawText(
                    text,
                    (int)card.x + 15,
                    (int)card.y + 43,
                    13,
                    TEXT_COLOR
                );

            } else {

                DrawText(
                    "Invalid instruction.",
                    (int)card.x + 15,
                    (int)card.y + 43,
                    13,
                    FAULT_COLOR
                );
            }
        }


        /* =========================
           EXECUTE CARD
           ========================= */

        else {

            if (gui->phase < GUI_PHASE_EXECUTE) {

                DrawText(
                    "Waiting for Decode...",
                    (int)card.x + 15,
                    (int)card.y + 43,
                    13,
                    COLOR_MUTED
                );

            } else if (
                cpu->state == CPU_STATE_FAULTED
            ) {

                DrawText(
                    "EXECUTION FAULT",
                    (int)card.x + 15,
                    (int)card.y + 43,
                    13,
                    FAULT_COLOR
                );

            } else if (
                cpu->state == CPU_STATE_HALTED
            ) {

                DrawText(
                    "CPU HALTED",
                    (int)card.x + 15,
                    (int)card.y + 43,
                    13,
                    FETCH_COLOR
                );

            } else {

                DrawText(
                    "Instruction completed.",
                    (int)card.x + 15,
                    (int)card.y + 43,
                    13,
                    COLOR_GREEN
                );
            }
        }
    }


    /* =========================
       RAW INSTRUCTION BYTES
       ========================= */

    if (gui->fetched_byte_count > 0) {

        char bytes_text[64] = {0};

        size_t offset = 0;

        for (uint8_t i = 0;
             i < gui->fetched_byte_count;
             i++) {

            int written = snprintf(
                bytes_text + offset,
                sizeof(bytes_text) - offset,
                "%02X ",
                gui->fetched_bytes[i]
            );

            if (written < 0 ||
                (size_t)written >= sizeof(bytes_text) - offset) {
                break;
            }

            offset += (size_t)written;
        }

        DrawText(
            TextFormat(
                "INSTRUCTION BYTES: %s",
                bytes_text
            ),
            455,
            301,
            12,
            COLOR_MUTED
        );
    }
}

/* =========================================
   STEP 10 — DATAPATH VISUALIZER
   ========================================= */

static const char *get_control_description(
    const GUIState *gui
)
{
    switch (gui->phase) {

        case GUI_PHASE_FETCH:
            return "Fetching instruction";

        case GUI_PHASE_DECODE:
            return "Decoding operands";

        case GUI_PHASE_EXECUTE:
            return "Executing operation";

        default:
            return "Waiting for instruction";
    }
}

static void draw_component(
    Rectangle bounds,
    const char *title,
    const char *subtitle,
    bool active,
    Color active_color
)
{
    Color bg = active ? (Color){45, 55, 75, 255} : COLOR_PANEL_ALT;
    Color border = active ? active_color : COLOR_BORDER;

    DrawRectangleRec(bounds, bg);
    DrawRectangleLinesEx(bounds, active ? 2.0f : 1.0f, border);

    DrawText(
        title,
        (int)bounds.x + 12,
        (int)bounds.y + 10,
        15,
        active ? active_color : COLOR_TEXT
    );

    DrawText(
        subtitle,
        (int)bounds.x + 12,
        (int)bounds.y + 35,
        12,
        COLOR_MUTED
    );
}

static void draw_datapath(
    const DioCPU *cpu,
    const GUIState *gui
)
{
    draw_panel(
        430,
        335,
        1150,
        270,
        "CPU DATAPATH"
    );

    bool fetching = (gui->phase == GUI_PHASE_FETCH);
    bool decoding = (gui->phase == GUI_PHASE_DECODE);
    bool executing = (gui->phase == GUI_PHASE_EXECUTE);

    /* Upper row: PC, Instruction Memory, Instruction Decoder, Control Unit */

    Rectangle pc_box = {455, 385, 200, 65};
    draw_component(
        pc_box,
        "PROGRAM COUNTER",
        TextFormat("PC: 0x%05X", (unsigned int)cpu->pc),
        fetching,
        COLOR_YELLOW
    );

    Rectangle imem_box = {690, 385, 220, 65};
    draw_component(
        imem_box,
        "INSTRUCTION MEM",
        TextFormat("Read: 0x%02X", gui->fetched_opcode),
        fetching,
        COLOR_YELLOW
    );

    Rectangle dec_box = {945, 385, 220, 65};
    draw_component(
        dec_box,
        "INSTRUCTION DECODER",
        decoding ? TextFormat("Len: %u byte(s)", (unsigned int)gui->decoded.length) : "Standby",
        decoding,
        COLOR_CYAN
    );

    Rectangle control_box = {1200, 385, 355, 65};
    draw_component(
        control_box,
        "CONTROL UNIT",
        get_control_description(gui),
        decoding || executing,
        ACCENT_COLOR
    );

    /* Lower row: Register File, ALU Engine, Data Memory / Stack */

    Rectangle rf_box = {550, 480, 260, 65};
    draw_component(
        rf_box,
        "REGISTER FILE",
        "16 GPRs (R0-R15)",
        executing,
        COLOR_GREEN
    );

    Rectangle alu_box = {850, 480, 260, 65};
    draw_component(
        alu_box,
        "ALU ENGINE",
        "Arithmetic & Logic",
        executing,
        COLOR_GREEN
    );

    Rectangle dmem_box = {1150, 480, 260, 65};
    draw_component(
        dmem_box,
        "DATA MEMORY / STACK",
        TextFormat("MAR: 0x%05X | SP: 0x%05X", (unsigned int)cpu->mar, (unsigned int)cpu->sp),
        executing,
        COLOR_ACCENT
    );

    /* Datapath connecting bus lines */

    Color pc_bus_color = fetching ? COLOR_YELLOW : COLOR_BORDER;
    DrawLineEx((Vector2){655, 417}, (Vector2){690, 417}, 2.0f, pc_bus_color);

    Color mem_dec_color = (fetching || decoding) ? COLOR_CYAN : COLOR_BORDER;
    DrawLineEx((Vector2){910, 417}, (Vector2){945, 417}, 2.0f, mem_dec_color);

    Color dec_ctrl_color = decoding ? COLOR_CYAN : COLOR_BORDER;
    DrawLineEx((Vector2){1165, 417}, (Vector2){1200, 417}, 2.0f, dec_ctrl_color);

    Color exec_bus_color = executing ? COLOR_GREEN : COLOR_BORDER;
    DrawLineEx((Vector2){810, 512}, (Vector2){850, 512}, 2.0f, exec_bus_color);
    DrawLineEx((Vector2){1110, 512}, (Vector2){1150, 512}, 2.0f, exec_bus_color);

    DrawText(
        "ACTIVE BUSES HIGHLIGHT ACCORDING TO CYCLE PHASE (FETCH = YELLOW, DECODE = CYAN, EXECUTE = GREEN)",
        455,
        570,
        12,
        COLOR_MUTED
    );
}

/* =========================================
   PROGRAM / DISASSEMBLY PANEL
   ========================================= */

static void draw_program_panel(
    const DioCPU *cpu,
    const GUIState *gui
)
{
    draw_panel(
        20,
        175,
        390,
        430,
        "PROGRAM INSTRUCTIONS"
    );

    if (gui->program_size == 0) {
        DrawText(
            "No program loaded in memory.",
            40,
            240,
            14,
            COLOR_MUTED
        );
        DrawText(
            "Click LOAD ASM or press F6 to reset.",
            40,
            268,
            13,
            COLOR_MUTED
        );
        return;
    }

    uint32_t current_pc = 0;
    int line_index = 0;
    const int max_display_lines = 11;

    while (current_pc < gui->program_size && line_index < max_display_lines) {

        DecodedInstruction instr = {0};
        DecoderStatus status = decoder_decode(&cpu->memory, current_pc, &instr);

        if (status != DECODER_STATUS_OK) {
            break;
        }

        int y_pos = 225 + line_index * 32;
        bool is_current = (current_pc == cpu->pc);

        if (is_current) {
            DrawRectangle(
                35,
                y_pos - 4,
                360,
                28,
                (Color){45, 75, 110, 255}
            );
            DrawRectangleLines(
                35,
                y_pos - 4,
                360,
                28,
                COLOR_CYAN
            );
        }

        DrawText(
            is_current ? ">" : " ",
            42,
            y_pos + 2,
            14,
            COLOR_YELLOW
        );

        DrawText(
            TextFormat("%05X:", (unsigned int)current_pc),
            58,
            y_pos + 2,
            13,
            is_current ? COLOR_YELLOW : COLOR_MUTED
        );

        char instr_text[80];
        format_instruction(&instr, instr_text, sizeof(instr_text));

        DrawText(
            instr_text,
            125,
            y_pos + 2,
            14,
            is_current ? COLOR_TEXT : (Color){200, 205, 215, 255}
        );

        current_pc += instr.length;
        line_index++;
    }
}

/* =========================================
   GENERAL PURPOSE REGISTER PANEL
   ========================================= */

static void draw_register_panel(
    const DioCPU *cpu,
    const GUIState *gui
)
{
    draw_panel(
        20,
        620,
        760,
        325,
        "GENERAL PURPOSE REGISTER FILE"
    );

    for (unsigned int i = 0;
         i < CPU_GPR_COUNT;
         i++) {

        int column = (int)(i % 4);
        int row = (int)(i / 4);

        int x = 38 + column * 183;
        int y = 660 + row * 66;

        bool changed =
            gui->register_flash[i] > 0.0f;

        Color background = changed
            ? (Color){25, 75, 45, 255}
            : COLOR_PANEL_ALT;

        Color border = changed
            ? COLOR_GREEN
            : COLOR_BORDER;

        Color value_color = changed
            ? COLOR_GREEN
            : COLOR_TEXT;

        DrawRectangle(
            x,
            y,
            165,
            58,
            background
        );

        DrawRectangleLinesEx(
            (Rectangle){
                (float)x,
                (float)y,
                165,
                58
            },
            changed ? 2.0f : 1.0f,
            border
        );

        DrawText(
            TextFormat("R%u", i),
            x + 12,
            y + 6,
            13,
            COLOR_MUTED
        );

        DrawText(
            TextFormat(
                "0x%04X",
                (unsigned int)cpu->gpr[i]
            ),
            x + 12,
            y + 24,
            19,
            value_color
        );

        if (changed) {
            DrawText(
                TextFormat(
                    "WAS: %04X",
                    (unsigned int)gui->old_register_values[i]
                ),
                x + 95,
                y + 38,
                9,
                COLOR_GREEN
            );
        }
    }
}

/* =========================================
   MEMORY PANEL
   ========================================= */

static void draw_memory_panel(
    const DioCPU *cpu,
    GUIState *gui
)
{
    draw_panel(
        795,
        620,
        440,
        325,
        "MEMORY VIEWER"
    );

    /* Tab switcher for memory views */
    Rectangle tab_instr = {815, 660, 120, 28};
    Rectangle tab_data  = {945, 660, 120, 28};
    Rectangle tab_stack = {1075, 660, 120, 28};

    if (draw_button(tab_instr, "INSTR")) {
        gui->memory_view = MEMORY_VIEW_INSTRUCTION;
    }
    if (draw_button(tab_data, "DATA")) {
        gui->memory_view = MEMORY_VIEW_DATA;
    }
    if (draw_button(tab_stack, "STACK")) {
        gui->memory_view = MEMORY_VIEW_STACK;
    }

    /* Active tab indicator */
    Rectangle active_tab = (gui->memory_view == MEMORY_VIEW_INSTRUCTION)
        ? tab_instr
        : ((gui->memory_view == MEMORY_VIEW_DATA) ? tab_data : tab_stack);

    DrawRectangleLinesEx(active_tab, 2.0f, COLOR_ACCENT);

    uint32_t view_base = 0;
    const char *region_name = "";

    if (gui->memory_view == MEMORY_VIEW_INSTRUCTION) {
        view_base = (cpu->pc >= 16) ? ((cpu->pc - 16) & ~0x0Fu) : 0;
        region_name = "Instruction Memory (256 KiB)";
    } else if (gui->memory_view == MEMORY_VIEW_DATA) {
        view_base = (cpu->mar & ~0x0Fu);
        region_name = "Data Memory (256 KiB)";
    } else {
        view_base = (cpu->sp & ~0x0Fu);
        region_name = "Stack Region (0x30000-0x3FFFF)";
    }

    DrawText(region_name, 815, 698, 12, COLOR_MUTED);

    /* Render 6 rows of 8 bytes = 48 bytes */
    for (int r = 0; r < 6; r++) {

        uint32_t row_addr = view_base + (uint32_t)(r * 8);
        int y_row = 724 + r * 30;

        DrawText(
            TextFormat("%05X:", (unsigned int)row_addr),
            815,
            y_row,
            13,
            COLOR_ACCENT
        );

        for (int c = 0; c < 8; c++) {

            uint32_t byte_addr = row_addr + (uint32_t)c;
            uint8_t byte_val = 0;

            if (gui->memory_view == MEMORY_VIEW_INSTRUCTION) {
                if (byte_addr <= DIOCPU_MEMORY_MAX_ADDRESS) {
                    byte_val = cpu->memory.instruction[byte_addr];
                }
            } else {
                if (byte_addr <= DIOCPU_MEMORY_MAX_ADDRESS) {
                    byte_val = cpu->memory.data[byte_addr];
                }
            }

            bool is_pc_or_sp = (gui->memory_view == MEMORY_VIEW_INSTRUCTION && byte_addr == cpu->pc) ||
                               (gui->memory_view == MEMORY_VIEW_STACK && byte_addr == cpu->sp);

            Color byte_color = is_pc_or_sp ? COLOR_YELLOW : ((byte_val != 0) ? COLOR_TEXT : COLOR_MUTED);

            DrawText(
                TextFormat("%02X", byte_val),
                885 + c * 38,
                y_row,
                13,
                byte_color
            );
        }
    }
}

/* =========================================
   CPU INFORMATION & FLAGS
   ========================================= */

static void draw_flag_card(
    int x,
    int y,
    int width,
    int height,
    const char *name,
    const char *description,
    bool enabled,
    bool changed,
    bool old_value
)
{
    Color state_color = enabled
        ? COLOR_GREEN
        : COLOR_MUTED;

    Color border_color = changed
        ? COLOR_YELLOW
        : (enabled ? COLOR_GREEN : COLOR_BORDER);

    Color background = changed
        ? (Color){65, 55, 25, 255}
        : COLOR_PANEL_ALT;

    DrawRectangle(
        x,
        y,
        width,
        height,
        background
    );

    DrawRectangleLinesEx(
        (Rectangle){
            (float)x,
            (float)y,
            (float)width,
            (float)height
        },
        changed ? 2.0f : 1.0f,
        border_color
    );

    DrawText(
        name,
        x + 10,
        y + 6,
        17,
        changed ? COLOR_YELLOW : state_color
    );

    DrawText(
        enabled ? "1" : "0",
        x + width - 20,
        y + 6,
        17,
        state_color
    );

    if (changed) {
        DrawText(
            TextFormat("%d -> %d", old_value ? 1 : 0, enabled ? 1 : 0),
            x + 10,
            y + 36,
            11,
            COLOR_YELLOW
        );
    } else {
        DrawText(
            description,
            x + 10,
            y + 36,
            11,
            COLOR_MUTED
        );
    }
}

static void draw_cpu_information(
    const DioCPU *cpu,
    const GUIState *gui
)
{
    draw_panel(
        1250,
        620,
        330,
        325,
        "CPU STATUS & FLAGS"
    );

    DrawText(
        TextFormat(
            "Instructions executed: %llu",
            (unsigned long long)cpu->instructions_executed
        ),
        1270,
        665,
        14,
        COLOR_TEXT
    );

    DrawText(
        TextFormat(
            "CPU State: %s",
            cpu_v2_state_string(cpu->state)
        ),
        1270,
        690,
        14,
        get_cpu_state_color(cpu->state)
    );

    DrawText(
        TextFormat(
            "Fault: %s",
            cpu_v2_fault_string(cpu->fault)
        ),
        1270,
        715,
        13,
        cpu->fault == CPU_FAULT_NONE
            ? COLOR_MUTED
            : COLOR_RED
    );

    DrawText(
        TextFormat(
            "Fault PC: 0x%05X",
            (unsigned int)cpu->fault_pc
        ),
        1270,
        738,
        13,
        COLOR_MUTED
    );

    /* Flags 2x2 grid */

    draw_flag_card(
        1270, 770, 140, 60,
        "ZF",
        "Zero Flag",
        (cpu->flags & CPU_FLAG_ZF) != 0,
        gui->flag_flash[0] > 0.0f,
        gui->old_flag_values[0]
    );

    draw_flag_card(
        1425, 770, 140, 60,
        "CF",
        "Carry Flag",
        (cpu->flags & CPU_FLAG_CF) != 0,
        gui->flag_flash[1] > 0.0f,
        gui->old_flag_values[1]
    );

    draw_flag_card(
        1270, 845, 140, 60,
        "SF",
        "Sign Flag",
        (cpu->flags & CPU_FLAG_SF) != 0,
        gui->flag_flash[2] > 0.0f,
        gui->old_flag_values[2]
    );

    draw_flag_card(
        1425, 845, 140, 60,
        "OF",
        "Overflow Flag",
        (cpu->flags & CPU_FLAG_OF) != 0,
        gui->flag_flash[3] > 0.0f,
        gui->old_flag_values[3]
    );

    DrawText(
        "FLAGS: ZF | CF | SF | OF",
        1270,
        918,
        11,
        COLOR_MUTED
    );
}

/* =========================================
   STATUS BAR (FOOTER)
   ========================================= */

static void draw_status_bar(
    const DioCPU *cpu,
    const GUIState *gui
)
{
    (void)cpu;

    DrawRectangle(
        20,
        955,
        1560,
        35,
        COLOR_PANEL
    );

    DrawRectangleLines(
        20,
        955,
        1560,
        35,
        COLOR_BORDER
    );

    DrawText(
        TextFormat(
            "SHORTCUTS: F5: %s | F8: Step Phase | F10: Step Instr | F6: Reset CPU",
            gui->auto_run ? "Pause" : "Run"
        ),
        35,
        965,
        13,
        COLOR_MUTED
    );

    DrawText(
        TextFormat(
            "ANIMATION: %s (Interval: %.2fs) | PHASE: %s",
            gui->auto_run ? "RUNNING" : "PAUSED",
            gui->phase_interval,
            gui->phase == GUI_PHASE_FETCH ? "FETCH" :
            (gui->phase == GUI_PHASE_DECODE ? "DECODE" :
            (gui->phase == GUI_PHASE_EXECUTE ? "EXECUTE" : "IDLE"))
        ),
        1100,
        965,
        13,
        gui->auto_run ? COLOR_GREEN : COLOR_CYAN
    );
}

/* =========================================
   MAIN
   ========================================= */

int main(void)
{
    InitWindow(
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        "DioCPU V2 - Fetch-Decode-Execute Animation Visualizer"
    );

    SetTargetFPS(FPS);

    /*
     * CPU is static because Memory contains
     * two 256 KiB arrays.
     */

    static DioCPU cpu;

    GUIState gui = {0};

    CPUStatus init_status = cpu_v2_init(&cpu);

    if (init_status != CPU_STATUS_OK) {

        gui.initialization_failed = true;

        gui.status_color = COLOR_RED;

        snprintf(
            gui.status_message,
            sizeof(gui.status_message),
            "CPU initialization failed."
        );

    } else {

        gui.status_color = COLOR_GREEN;

        snprintf(
            gui.status_message,
            sizeof(gui.status_message),
            "CPU initialized successfully."
        );
    }

    gui.phase_interval = 0.35f;

    gui.memory_view = MEMORY_VIEW_INSTRUCTION;

    snprintf(
        gui.source_path,
        sizeof(gui.source_path),
        "examples/gui_v2_cycle.asm"
    );

    reset_gui_cycle(&gui);

    initialize_state_tracking(&gui, &cpu);

    load_assembly_program(&cpu, &gui);


    while (!WindowShouldClose()) {

        float delta_time = GetFrameTime();


        /* =================================
           KEYBOARD SHORTCUTS
           ================================= */

        if (IsKeyPressed(KEY_F8)) {

            gui.auto_run = false;

            advance_cpu_phase(&cpu, &gui);
        }

        if (IsKeyPressed(KEY_F10)) {

            gui.auto_run = false;

            execute_one_instruction(&cpu, &gui);
        }

        if (IsKeyPressed(KEY_F5)) {

            if (cpu.state == CPU_STATE_RUNNING) {

                gui.auto_run = !gui.auto_run;

                gui.phase_timer = 0.0f;
            }
        }

        if (IsKeyPressed(KEY_F6)) {

            cpu_v2_reset(&cpu);

            reset_gui_cycle(&gui);
        }


        /* =================================
           DRAWING
           ================================= */

        BeginDrawing();

        ClearBackground(COLOR_BACKGROUND);


        /* Header and interactive buttons */

        draw_header(&cpu, &gui);


        /* =================================
           AUTOMATIC EXECUTION
           ================================= */

        if (gui.auto_run &&
            cpu.state == CPU_STATE_RUNNING) {

            gui.phase_timer += delta_time;

            if (gui.phase_timer >= gui.phase_interval) {

                gui.phase_timer = 0.0f;

                advance_cpu_phase(&cpu, &gui);
            }
        }


        if (cpu.state != CPU_STATE_RUNNING) {

            gui.auto_run = false;
        }


        /* =================================
           STATE CHANGE TRACKING
           ================================= */

        update_state_tracking(
            &gui,
            &cpu,
            delta_time
        );


        /* =================================
           DRAW CPU PANELS
           ================================= */

        draw_status_bar(&cpu, &gui);

        draw_cpu_dashboard(&cpu);

        draw_register_panel(&cpu, &gui);

        draw_cycle_panel(&cpu, &gui);

        draw_datapath(&cpu, &gui);

        draw_program_panel(&cpu, &gui);

        draw_memory_panel(&cpu, &gui);

        draw_cpu_information(&cpu, &gui);


        EndDrawing();
    }


    CloseWindow();

    return 0;
}
