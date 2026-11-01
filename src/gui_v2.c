#include "raylib.h"

#include "cpu_v2.h"
#include "alu.h"
#include "assembler.h"
#include "decoder.h"
#include "memory.h"
#include "stack.h"

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

/* Color aliases requested by GUI-V2 specifications */
#define FETCH_COLOR       COLOR_YELLOW
#define DECODE_COLOR      COLOR_CYAN
#define EXECUTE_COLOR     COLOR_GREEN
#define FAULT_COLOR       COLOR_RED
#define PANEL_BG          COLOR_PANEL
#define TEXT_COLOR        COLOR_TEXT
#define ACCENT_COLOR      COLOR_ACCENT

/* =========================================
   MEMORY VIEWER CONFIGURATION
   ========================================= */

#define GUI_MEMORY_BYTES_PER_ROW 16
#define GUI_MEMORY_ROWS 8

#define GUI_MEMORY_PAGE_SIZE \
    (GUI_MEMORY_BYTES_PER_ROW * GUI_MEMORY_ROWS)

#define GUI_MEMORY_MAX_BASE \
    (DIOCPU_MEMORY_SIZE - GUI_MEMORY_PAGE_SIZE)

#define GUI_MEMORY_VIEW_INSTRUCTION 0
#define GUI_MEMORY_VIEW_DATA        1

#define GUI_WORKSPACE_DATAPATH 0
#define GUI_WORKSPACE_MEMORY   1
#define GUI_WORKSPACE_STACK    2

#define GUI_STACK_VIEW_WORDS 10

#define GUI_STACK_VIEW_BYTES \
    (GUI_STACK_VIEW_WORDS * 2u)

#define GUI_STACK_EVENT_DURATION 2.5f

/* =========================================
   STACK EVENT TYPES
   ========================================= */

typedef enum {

    GUI_STACK_EVENT_NONE = 0,

    GUI_STACK_EVENT_PUSH,
    GUI_STACK_EVENT_POP,

    GUI_STACK_EVENT_CALL,
    GUI_STACK_EVENT_RET

} GUIStackEventType;

/* =========================================
   GUI STATE & TRACKING
   ========================================= */

typedef enum {
    GUI_PHASE_IDLE = 0,
    GUI_PHASE_FETCH,
    GUI_PHASE_DECODE,
    GUI_PHASE_EXECUTE
} GUIPhase;

#define GUI_FETCH    GUI_PHASE_FETCH
#define GUI_DECODE   GUI_PHASE_DECODE
#define GUI_EXECUTE  GUI_PHASE_EXECUTE

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
       WORKSPACE & MEMORY VIEWER
       ================================= */

    int workspace_view;

    uint32_t memory_base;

    int memory_view;

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


    /* =========================================
       ALU VISUALIZATION
       ========================================= */

    bool alu_preview_valid;
    bool alu_result_visible;
    bool alu_instruction_invalid;
    bool alu_execution_failed;

    bool alu_is_unary;
    bool alu_is_compare;
    bool alu_is_division;

    ALUOperation alu_operation;
    ALUResult alu_preview;

    uint8_t alu_reg_a;
    uint8_t alu_reg_b;

    uint16_t alu_input_a;
    uint16_t alu_input_b;

    uint16_t alu_reg_a_before;
    uint16_t alu_reg_b_before;

    uint16_t alu_reg_a_after;
    uint16_t alu_reg_b_after;

    uint16_t alu_flags_before;
    uint16_t alu_flags_after;

    float datapath_animation_time;


    /* =========================================
       STACK VIEWER
       ========================================= */

    uint32_t stack_view_base;

    bool stack_follow_sp;


    /* =========================================
       STACK EVENT TRACKING
       ========================================= */

    GUIStackEventType stack_event_type;

    bool stack_event_pending;
    bool stack_event_success;

    uint8_t stack_event_register;

    uint32_t stack_event_old_sp;
    uint32_t stack_event_new_sp;

    uint32_t stack_event_address;
    uint32_t stack_event_return_address;

    uint16_t stack_event_value;
    uint16_t stack_event_high_value;

    float stack_event_timer;

} GUIState;

static const uint16_t FLAG_MASKS[4] = {
    CPU_FLAG_ZF,
    CPU_FLAG_CF,
    CPU_FLAG_SF,
    CPU_FLAG_OF
};

/* =========================================
   ALU MAPPING & STATUS HELPERS
   ========================================= */

static bool gui_get_alu_operation(
    InstructionOpcode opcode,
    ALUOperation *operation
)
{
    if (operation == NULL) {
        return false;
    }

    switch (opcode) {

        case DIOCPU_OP_ADD:
            *operation = ALU_ADD;
            return true;

        case DIOCPU_OP_SUB:
            *operation = ALU_SUB;
            return true;

        case DIOCPU_OP_MUL:
            *operation = ALU_MUL;
            return true;

        case DIOCPU_OP_MULS:
            *operation = ALU_MULS;
            return true;

        case DIOCPU_OP_DIV:
            *operation = ALU_DIV;
            return true;

        case DIOCPU_OP_DIVS:
            *operation = ALU_DIVS;
            return true;

        case DIOCPU_OP_AND:
            *operation = ALU_AND;
            return true;

        case DIOCPU_OP_OR:
            *operation = ALU_OR;
            return true;

        case DIOCPU_OP_XOR:
            *operation = ALU_XOR;
            return true;

        case DIOCPU_OP_NOT:
            *operation = ALU_NOT;
            return true;

        case DIOCPU_OP_CMP:
            *operation = ALU_CMP;
            return true;

        default:
            return false;
    }
}

static const char *gui_alu_operation_name(
    ALUOperation operation
)
{
    switch (operation) {

        case ALU_ADD:  return "ADD";
        case ALU_SUB:  return "SUB";
        case ALU_MUL:  return "MUL";
        case ALU_MULS: return "MULS";
        case ALU_DIV:  return "DIV";
        case ALU_DIVS: return "DIVS";
        case ALU_AND:  return "AND";
        case ALU_OR:   return "OR";
        case ALU_XOR:  return "XOR";
        case ALU_NOT:  return "NOT";
        case ALU_CMP:  return "CMP";

        default:
            return "UNKNOWN";
    }
}

static const char *gui_alu_status_string(ALUStatus status)
{
    switch (status) {

        case ALU_STATUS_OK:
            return "OK";

        case ALU_STATUS_DIVIDE_BY_ZERO:
            return "Divide by zero";

        case ALU_STATUS_ARITHMETIC_OVERFLOW:
            return "Arithmetic overflow";

        case ALU_STATUS_INVALID_OPERATION:
            return "Invalid ALU operation";

        default:
            return "Unknown ALU status";
    }
}

static void clear_alu_visualization(GUIState *gui)
{
    gui->alu_preview_valid = false;
    gui->alu_result_visible = false;
    gui->alu_instruction_invalid = false;
    gui->alu_execution_failed = false;

    gui->alu_is_unary = false;
    gui->alu_is_compare = false;
    gui->alu_is_division = false;

    gui->alu_operation = ALU_ADD;
    gui->alu_preview = (ALUResult){0};

    gui->alu_reg_a = 0;
    gui->alu_reg_b = 0;

    gui->alu_input_a = 0;
    gui->alu_input_b = 0;

    gui->alu_reg_a_before = 0;
    gui->alu_reg_b_before = 0;

    gui->alu_reg_a_after = 0;
    gui->alu_reg_b_after = 0;

    gui->alu_flags_before = 0;
    gui->alu_flags_after = 0;
}

static void prepare_alu_visualization(
    const DioCPU *cpu,
    GUIState *gui
)
{
    clear_alu_visualization(gui);

    ALUOperation operation;

    if (!gui_get_alu_operation(
            gui->decoded.opcode,
            &operation
        )) {

        /* Instruksi ini tidak menggunakan ALU. */
        return;
    }

    uint8_t reg_a = gui->decoded.reg_a;
    uint8_t reg_b = gui->decoded.reg_b;

    if (reg_a >= CPU_GPR_COUNT ||
        reg_b >= CPU_GPR_COUNT) {

        gui->alu_instruction_invalid = true;
        return;
    }

    gui->alu_operation = operation;

    gui->alu_reg_a = reg_a;
    gui->alu_reg_b = reg_b;

    gui->alu_is_unary =
        gui->decoded.opcode == DIOCPU_OP_NOT;

    gui->alu_is_compare =
        gui->decoded.opcode == DIOCPU_OP_CMP;

    gui->alu_is_division =
        gui->decoded.opcode == DIOCPU_OP_DIV ||
        gui->decoded.opcode == DIOCPU_OP_DIVS;

    /* Ambil operand dari Register File. */

    gui->alu_input_a = cpu->gpr[reg_a];

    gui->alu_input_b = gui->alu_is_unary
        ? 0
        : cpu->gpr[reg_b];

    /* Simpan kondisi sebelum eksekusi. */

    gui->alu_reg_a_before = cpu->gpr[reg_a];
    gui->alu_reg_b_before = cpu->gpr[reg_b];

    gui->alu_flags_before = cpu->flags;

    /*
     * DIV dan DIVS membutuhkan dua register tujuan
     * yang berbeda: quotient ke RA dan remainder ke RB.
     */

    if (gui->alu_is_division && reg_a == reg_b) {

        gui->alu_instruction_invalid = true;
        gui->alu_preview_valid = true;

        return;
    }

    /*
     * Preview hanya untuk visualisasi.
     * Tidak mengubah GPR atau FLAGS CPU.
     */

    gui->alu_preview = alu_execute(
        operation,
        gui->alu_input_a,
        gui->alu_input_b,
        cpu->flags
    );

    gui->alu_preview_valid = true;
}

static void complete_alu_visualization(
    const DioCPU *cpu,
    GUIState *gui,
    CPUStatus execution_status
)
{
    if (!gui->alu_preview_valid) {
        return;
    }

    gui->alu_result_visible = true;

    gui->alu_execution_failed =
        execution_status != CPU_STATUS_OK;

    /*
     * Ambil kondisi aktual setelah cpu_v2_step().
     */

    gui->alu_reg_a_after =
        cpu->gpr[gui->alu_reg_a];

    gui->alu_reg_b_after =
        cpu->gpr[gui->alu_reg_b];

    gui->alu_flags_after = cpu->flags;
}

/* =========================================
   MEMORY VIEWER HELPERS
   ========================================= */

static void normalize_memory_base(GUIState *gui)
{
    gui->memory_base -=
        gui->memory_base % GUI_MEMORY_PAGE_SIZE;

    if (gui->memory_base > GUI_MEMORY_MAX_BASE) {
        gui->memory_base = GUI_MEMORY_MAX_BASE;
    }
}

static uint8_t gui_memory_read_byte(
    const DioCPU *cpu,
    int memory_view,
    uint32_t address
)
{
    if (address >= DIOCPU_MEMORY_SIZE) {
        return 0;
    }

    if (memory_view == GUI_MEMORY_VIEW_INSTRUCTION) {
        return cpu->memory.instruction[address];
    }

    return cpu->memory.data[address];
}

static uint32_t get_memory_selected_address(
    const DioCPU *cpu,
    int memory_view
)
{
    if (memory_view == GUI_MEMORY_VIEW_INSTRUCTION) {
        return cpu->pc;
    }

    return cpu->mar;
}

static void memory_go_to_cpu_pointer(
    const DioCPU *cpu,
    GUIState *gui
)
{
    uint32_t address = get_memory_selected_address(
        cpu,
        gui->memory_view
    );

    if (address >= DIOCPU_MEMORY_SIZE) {
        address = DIOCPU_MEMORY_MAX_ADDRESS;
    }

    gui->memory_base =
        (address / GUI_MEMORY_PAGE_SIZE) *
        GUI_MEMORY_PAGE_SIZE;

    normalize_memory_base(gui);
}

static void memory_move_page(
    GUIState *gui,
    int direction
)
{
    normalize_memory_base(gui);

    if (direction < 0) {

        if (gui->memory_base >= GUI_MEMORY_PAGE_SIZE) {
            gui->memory_base -= GUI_MEMORY_PAGE_SIZE;
        } else {
            gui->memory_base = 0;
        }

    } else if (direction > 0) {

        if (gui->memory_base <=
            GUI_MEMORY_MAX_BASE - GUI_MEMORY_PAGE_SIZE) {

            gui->memory_base += GUI_MEMORY_PAGE_SIZE;
        }
    }
}

static bool draw_memory_control(
    Rectangle bounds,
    const char *label,
    bool active
)
{
    Vector2 mouse = GetMousePosition();

    bool hovered = CheckCollisionPointRec(
        mouse,
        bounds
    );

    Color background = active
        ? (Color){35, 75, 95, 255}
        : (hovered
            ? (Color){55, 85, 110, 255}
            : COLOR_PANEL_ALT);

    Color border = active || hovered
        ? COLOR_CYAN
        : COLOR_BORDER;

    DrawRectangleRec(
        bounds,
        background
    );

    DrawRectangleLinesEx(
        bounds,
        active ? 2.0f : 1.0f,
        border
    );

    int text_width = MeasureText(label, 12);

    DrawText(
        label,
        (int)(bounds.x +
            (bounds.width - text_width) / 2.0f),
        (int)(bounds.y +
            (bounds.height - 12.0f) / 2.0f),
        12,
        COLOR_TEXT
    );

    return hovered &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

/* =========================================
   STACK VIEWER HELPERS & EVENT TRACKING
   ========================================= */

static bool gui_stack_sp_valid(uint32_t sp)
{
    return
        sp >= DIOCPU_STACK_BOTTOM &&
        sp <= DIOCPU_STACK_INITIAL_SP &&
        (sp % 2u) == 0u;
}

static bool gui_stack_read_word(
    const DioCPU *cpu,
    uint32_t address,
    uint16_t *value
)
{
    if (cpu == NULL || value == NULL) {
        return false;
    }

    if (address < DIOCPU_STACK_BOTTOM ||
        address + 1u > DIOCPU_STACK_TOP) {

        return false;
    }

    uint8_t low =
        cpu->memory.data[address];

    uint8_t high =
        cpu->memory.data[address + 1u];

    *value =
        (uint16_t)low |
        ((uint16_t)high << 8u);

    return true;
}

static uint32_t gui_stack_used_bytes(
    const DioCPU *cpu
)
{
    if (!gui_stack_sp_valid(cpu->sp)) {
        return 0;
    }

    return DIOCPU_STACK_INITIAL_SP - cpu->sp;
}

static uint32_t gui_stack_free_bytes(
    const DioCPU *cpu
)
{
    if (!gui_stack_sp_valid(cpu->sp)) {
        return 0;
    }

    return cpu->sp - DIOCPU_STACK_BOTTOM;
}

static uint32_t gui_stack_used_words(
    const DioCPU *cpu
)
{
    return gui_stack_used_bytes(cpu) / 2u;
}

static uint32_t gui_stack_capacity_words(void)
{
    return (
        DIOCPU_STACK_INITIAL_SP -
        DIOCPU_STACK_BOTTOM
    ) / 2u;
}

static void gui_stack_follow_pointer(
    const DioCPU *cpu,
    GUIState *gui
)
{
    if (!gui_stack_sp_valid(cpu->sp)) {
        return;
    }

    gui->stack_follow_sp = true;

    gui->stack_view_base = cpu->sp;
}

static void gui_stack_view_older(
    GUIState *gui
)
{
    gui->stack_follow_sp = false;

    uint32_t maximum =
        DIOCPU_STACK_INITIAL_SP;

    if (gui->stack_view_base <=
        maximum - GUI_STACK_VIEW_BYTES) {

        gui->stack_view_base += GUI_STACK_VIEW_BYTES;

    } else {

        gui->stack_view_base = maximum;
    }

    gui->stack_view_base -=
        gui->stack_view_base % 2u;
}

static void gui_stack_view_newer(
    const DioCPU *cpu,
    GUIState *gui
)
{
    if (!gui_stack_sp_valid(cpu->sp)) {
        return;
    }

    gui->stack_follow_sp = false;

    if (gui->stack_view_base <=
        cpu->sp + GUI_STACK_VIEW_BYTES) {

        gui->stack_view_base = cpu->sp;

    } else {

        gui->stack_view_base -= GUI_STACK_VIEW_BYTES;
    }

    gui->stack_view_base -=
        gui->stack_view_base % 2u;
}

static void prepare_stack_event(
    const DioCPU *cpu,
    GUIState *gui
)
{
    InstructionOpcode opcode = gui->decoded.opcode;

    GUIStackEventType event_type =
        GUI_STACK_EVENT_NONE;

    switch (opcode) {

        case DIOCPU_OP_PUSH:
            event_type = GUI_STACK_EVENT_PUSH;
            break;

        case DIOCPU_OP_POP:
            event_type = GUI_STACK_EVENT_POP;
            break;

        case DIOCPU_OP_CALL:
            event_type = GUI_STACK_EVENT_CALL;
            break;

        case DIOCPU_OP_RET:
            event_type = GUI_STACK_EVENT_RET;
            break;

        default:
            gui->stack_event_pending = false;
            return;
    }

    gui->stack_event_type = event_type;

    gui->stack_event_pending = true;

    gui->stack_event_success = false;

    gui->stack_event_old_sp = cpu->sp;
    gui->stack_event_new_sp = cpu->sp;

    gui->stack_event_register = gui->decoded.reg_a;

    gui->stack_event_address = cpu->sp;

    gui->stack_event_value = 0;
    gui->stack_event_high_value = 0;

    gui->stack_event_return_address = 0;

    /*
     * PUSH
     */

    if (opcode == DIOCPU_OP_PUSH) {

        gui->stack_event_address =
            cpu->sp >= DIOCPU_STACK_BOTTOM + 2u
                ? cpu->sp - 2u
                : cpu->sp;

        if (gui->decoded.reg_a < CPU_GPR_COUNT) {

            gui->stack_event_value =
                cpu->gpr[gui->decoded.reg_a];
        }
    }

    /*
     * POP
     */

    else if (opcode == DIOCPU_OP_POP) {

        gui->stack_event_address = cpu->sp;

        (void)gui_stack_read_word(
            cpu,
            cpu->sp,
            &gui->stack_event_value
        );
    }

    /*
     * CALL
     */

    else if (opcode == DIOCPU_OP_CALL) {

        gui->stack_event_address =
            cpu->sp >= DIOCPU_STACK_BOTTOM + 4u
                ? cpu->sp - 4u
                : cpu->sp;

        gui->stack_event_return_address =
            gui->decoded.next_pc;

        gui->stack_event_value =
            (uint16_t)(
                gui->stack_event_return_address & 0xFFFFu
            );

        gui->stack_event_high_value =
            (uint16_t)(
                gui->stack_event_return_address >> 16u
            );
    }

    /*
     * RET
     */

    else if (opcode == DIOCPU_OP_RET) {

        gui->stack_event_address = cpu->sp;

        (void)gui_stack_read_word(
            cpu,
            cpu->sp,
            &gui->stack_event_value
        );

        (void)gui_stack_read_word(
            cpu,
            cpu->sp + 2u,
            &gui->stack_event_high_value
        );

        gui->stack_event_return_address =
            (uint32_t)gui->stack_event_value |
            ((uint32_t)gui->stack_event_high_value << 16u);
    }
}

static void complete_stack_event(
    const DioCPU *cpu,
    GUIState *gui,
    CPUStatus execution_status
)
{
    if (!gui->stack_event_pending) {
        return;
    }

    gui->stack_event_pending = false;

    gui->stack_event_new_sp = cpu->sp;

    gui->stack_event_success =
        execution_status == CPU_STATUS_OK &&
        cpu->sp != gui->stack_event_old_sp;

    gui->stack_event_timer =
        GUI_STACK_EVENT_DURATION;

    /*
     * Setelah operasi stack berhasil,
     * ikuti posisi SP terbaru.
     */

    gui->stack_follow_sp = true;

    gui->stack_view_base = cpu->sp;
}

static void clear_stack_event(GUIState *gui)
{
    gui->stack_event_type = GUI_STACK_EVENT_NONE;

    gui->stack_event_pending = false;

    gui->stack_event_success = false;

    gui->stack_event_timer = 0.0f;

    gui->stack_event_old_sp = 0;
    gui->stack_event_new_sp = 0;

    gui->stack_event_address = 0;
    gui->stack_event_return_address = 0;

    gui->stack_event_value = 0;
    gui->stack_event_high_value = 0;

    gui->stack_event_register = 0;
}

static bool draw_stack_control(
    Rectangle bounds,
    const char *label,
    bool active
)
{
    Vector2 mouse = GetMousePosition();

    bool hovered = CheckCollisionPointRec(
        mouse,
        bounds
    );

    Color background = active
        ? (Color){35, 75, 95, 255}
        : (hovered
            ? (Color){55, 85, 110, 255}
            : COLOR_PANEL_ALT);

    Color border =
        active || hovered
            ? COLOR_CYAN
            : COLOR_BORDER;

    DrawRectangleRec(
        bounds,
        background
    );

    DrawRectangleLinesEx(
        bounds,
        active ? 2.0f : 1.0f,
        border
    );

    int text_width = MeasureText(label, 12);

    DrawText(
        label,
        (int)(
            bounds.x +
            (bounds.width - text_width) / 2.0f
        ),
        (int)(
            bounds.y +
            (bounds.height - 12.0f) / 2.0f
        ),
        12,
        COLOR_TEXT
    );

    return hovered &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static const char *gui_stack_event_name(
    GUIStackEventType event
)
{
    switch (event) {

        case GUI_STACK_EVENT_PUSH:
            return "PUSH";

        case GUI_STACK_EVENT_POP:
            return "POP";

        case GUI_STACK_EVENT_CALL:
            return "CALL";

        case GUI_STACK_EVENT_RET:
            return "RET";

        default:
            return "NONE";
    }
}

static void draw_stack_event_information(
    const GUIState *gui,
    int x,
    int y
)
{
    if (gui->stack_event_type == GUI_STACK_EVENT_NONE) {

        DrawText(
            "Waiting for stack instruction...",
            x,
            y,
            13,
            COLOR_MUTED
        );

        return;
    }

    Color event_color = gui->stack_event_success
        ? COLOR_GREEN
        : COLOR_RED;

    const char *result_text =
        gui->stack_event_success
            ? "COMMITTED"
            : "FAILED";

    DrawText(
        TextFormat(
            "LAST EVENT: %s",
            gui_stack_event_name(gui->stack_event_type)
        ),
        x,
        y,
        13,
        COLOR_CYAN
    );

    DrawText(
        result_text,
        x + 300,
        y,
        13,
        event_color
    );

    switch (gui->stack_event_type) {

        case GUI_STACK_EVENT_PUSH:

            DrawText(
                TextFormat(
                    "R%u = 0x%04X -> [0x%05X]",
                    (unsigned int)gui->stack_event_register,
                    (unsigned int)gui->stack_event_value,
                    (unsigned int)gui->stack_event_address
                ),
                x,
                y + 22,
                12,
                COLOR_TEXT
            );

            break;

        case GUI_STACK_EVENT_POP:

            DrawText(
                TextFormat(
                    "[0x%05X] = 0x%04X -> R%u",
                    (unsigned int)gui->stack_event_address,
                    (unsigned int)gui->stack_event_value,
                    (unsigned int)gui->stack_event_register
                ),
                x,
                y + 22,
                12,
                COLOR_TEXT
            );

            break;

        case GUI_STACK_EVENT_CALL:

            DrawText(
                TextFormat(
                    "Return PC 0x%05X saved at 0x%05X - 0x%05X",
                    (unsigned int)gui->stack_event_return_address,
                    (unsigned int)gui->stack_event_address,
                    (unsigned int)(gui->stack_event_address + 3u)
                ),
                x,
                y + 22,
                12,
                COLOR_TEXT
            );

            break;

        case GUI_STACK_EVENT_RET:

            DrawText(
                TextFormat(
                    "Return address loaded: 0x%05X from 0x%05X",
                    (unsigned int)gui->stack_event_return_address,
                    (unsigned int)gui->stack_event_address
                ),
                x,
                y + 22,
                12,
                COLOR_TEXT
            );

            break;

        default:
            break;
    }
}

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

    clear_alu_visualization(gui);

    normalize_memory_base(gui);

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
    clear_alu_visualization(gui);

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
     * Prepare ALU preview when instruction uses ALU.
     */

    prepare_alu_visualization(cpu, gui);


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

    prepare_stack_event(cpu, gui);

    gui->last_cpu_status = cpu_v2_step(cpu);

    complete_stack_event(
        cpu,
        gui,
        gui->last_cpu_status
    );

    complete_alu_visualization(
        cpu,
        gui,
        gui->last_cpu_status
    );


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
    clear_stack_event(gui);
    gui_stack_follow_pointer(cpu, gui);

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
        "16-BIT ARCHITECTURE | INTERACTIVE SIMULATOR & MEMORY VIEWER",
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
        clear_stack_event(gui);
        gui_stack_follow_pointer(cpu, gui);
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
   INSTRUCTION CYCLE PANEL
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
        430,
        330,
        760,
        275,
        "GENERAL PURPOSE REGISTER FILE"
    );

    for (unsigned int i = 0;
         i < CPU_GPR_COUNT;
         i++) {

        int column = (int)(i % 4);
        int row = (int)(i / 4);

        int x = 448 + column * 183;
        int y = 370 + row * 56;

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
            50,
            background
        );

        DrawRectangleLinesEx(
            (Rectangle){
                (float)x,
                (float)y,
                165,
                50
            },
            changed ? 2.0f : 1.0f,
            border
        );

        DrawText(
            TextFormat("R%u", i),
            x + 10,
            y + 4,
            12,
            COLOR_MUTED
        );

        DrawText(
            TextFormat(
                "0x%04X",
                (unsigned int)cpu->gpr[i]
            ),
            x + 10,
            y + 20,
            17,
            value_color
        );

        if (changed) {
            DrawText(
                TextFormat(
                    "WAS: %04X",
                    (unsigned int)gui->old_register_values[i]
                ),
                x + 95,
                y + 32,
                9,
                COLOR_GREEN
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
        16,
        changed ? COLOR_YELLOW : state_color
    );

    DrawText(
        enabled ? "1" : "0",
        x + width - 20,
        y + 6,
        16,
        state_color
    );

    if (changed) {
        DrawText(
            TextFormat("%d -> %d", old_value ? 1 : 0, enabled ? 1 : 0),
            x + 10,
            y + 32,
            11,
            COLOR_YELLOW
        );
    } else {
        DrawText(
            description,
            x + 10,
            y + 32,
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
        1210,
        330,
        370,
        275,
        "CPU STATUS & FLAGS"
    );

    DrawText(
        TextFormat(
            "Instructions executed: %llu",
            (unsigned long long)cpu->instructions_executed
        ),
        1230,
        370,
        13,
        COLOR_TEXT
    );

    DrawText(
        TextFormat(
            "CPU State: %s",
            cpu_v2_state_string(cpu->state)
        ),
        1230,
        392,
        13,
        get_cpu_state_color(cpu->state)
    );

    DrawText(
        TextFormat(
            "Fault: %s",
            cpu_v2_fault_string(cpu->fault)
        ),
        1230,
        414,
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
        1230,
        434,
        13,
        COLOR_MUTED
    );

    /* Flags 2x2 grid */

    draw_flag_card(
        1230, 460, 160, 52,
        "ZF",
        "Zero Flag",
        (cpu->flags & CPU_FLAG_ZF) != 0,
        gui->flag_flash[0] > 0.0f,
        gui->old_flag_values[0]
    );

    draw_flag_card(
        1405, 460, 160, 52,
        "CF",
        "Carry Flag",
        (cpu->flags & CPU_FLAG_CF) != 0,
        gui->flag_flash[1] > 0.0f,
        gui->old_flag_values[1]
    );

    draw_flag_card(
        1230, 520, 160, 52,
        "SF",
        "Sign Flag",
        (cpu->flags & CPU_FLAG_SF) != 0,
        gui->flag_flash[2] > 0.0f,
        gui->old_flag_values[2]
    );

    draw_flag_card(
        1405, 520, 160, 52,
        "OF",
        "Overflow Flag",
        (cpu->flags & CPU_FLAG_OF) != 0,
        gui->flag_flash[3] > 0.0f,
        gui->old_flag_values[3]
    );

    DrawText(
        "STATUS FLAGS REGISTER (16-BIT)",
        1230,
        580,
        11,
        COLOR_MUTED
    );
}

/* =========================================
   ANIMATED DATAPATH & ALU VISUALIZATION
   ========================================= */

static void draw_datapath_node(
    Rectangle bounds,
    const char *title,
    const char *line1,
    const char *line2,
    bool active
)
{
    Color background = active
        ? (Color){35, 65, 82, 255}
        : COLOR_PANEL_ALT;

    Color border = active
        ? COLOR_CYAN
        : COLOR_BORDER;

    DrawRectangleRec(bounds, background);

    DrawRectangleLinesEx(
        bounds,
        active ? 2.0f : 1.0f,
        border
    );

    DrawText(
        title,
        (int)bounds.x + 12,
        (int)bounds.y + 9,
        13,
        active ? COLOR_CYAN : COLOR_ACCENT
    );

    DrawText(
        line1,
        (int)bounds.x + 12,
        (int)bounds.y + 32,
        13,
        COLOR_TEXT
    );

    DrawText(
        line2,
        (int)bounds.x + 12,
        (int)bounds.y + 52,
        12,
        COLOR_MUTED
    );
}

static void draw_bus_arrow(
    float start_x,
    float end_x,
    float y,
    bool active,
    float progress
)
{
    Color bus_color = active
        ? COLOR_CYAN
        : COLOR_BORDER;

    DrawLineEx(
        (Vector2){start_x, y},
        (Vector2){end_x - 10.0f, y},
        active ? 3.0f : 1.5f,
        bus_color
    );

    DrawTriangle(
        (Vector2){end_x, y},
        (Vector2){end_x - 12.0f, y - 7.0f},
        (Vector2){end_x - 12.0f, y + 7.0f},
        bus_color
    );

    /*
     * Titik bergerak hanya muncul
     * pada jalur yang sedang aktif.
     */

    if (active) {

        float dot_x =
            start_x +
            ((end_x - start_x) * progress);

        DrawCircle(
            (int)dot_x,
            (int)y,
            6,
            COLOR_YELLOW
        );
    }
}

static void format_fetched_bytes(
    const GUIState *gui,
    char *output,
    size_t output_size
)
{
    if (output == NULL || output_size == 0) {
        return;
    }

    output[0] = '\0';

    if (!gui->fetch_valid ||
        gui->fetched_byte_count == 0) {

        snprintf(
            output,
            output_size,
            "Waiting for instruction"
        );

        return;
    }

    size_t used = 0;

    for (uint8_t i = 0;
         i < gui->fetched_byte_count;
         i++) {

        int written = snprintf(
            output + used,
            output_size - used,
            "%02X ",
            (unsigned int)gui->fetched_bytes[i]
        );

        if (written < 0 ||
            (size_t)written >= output_size - used) {

            break;
        }

        used += (size_t)written;
    }
}

static void draw_datapath_visualization(
    const DioCPU *cpu,
    const GUIState *gui,
    Rectangle bounds
)
{
    int x = (int)bounds.x;
    int y = (int)bounds.y;

    draw_panel(
        x,
        y,
        (int)bounds.width,
        (int)bounds.height,
        "CPU DATAPATH & ALU VISUALIZATION"
    );

    bool fetching =
        gui->phase == GUI_FETCH;

    bool decoding =
        gui->phase == GUI_DECODE;

    bool executing =
        gui->phase == GUI_EXECUTE;

    float progress = gui->datapath_animation_time;

    if (progress < 0.0f || progress > 1.0f) {
        progress = 0.0f;
    }

    /*
     * =====================================
     * TOP ROW: INSTRUCTION FETCH & DECODE
     * =====================================
     */

    char pc_text[48];
    char memory_text[64];
    char bytes_text[64];

    snprintf(
        pc_text,
        sizeof(pc_text),
        "PC: 0x%05X",
        (unsigned int)gui->instruction_pc
    );

    if (gui->fetch_valid) {

        snprintf(
            memory_text,
            sizeof(memory_text),
            "Opcode: 0x%02X",
            (unsigned int)gui->fetched_opcode
        );

    } else {

        snprintf(
            memory_text,
            sizeof(memory_text),
            "Waiting for fetch"
        );
    }

    format_fetched_bytes(
        gui,
        bytes_text,
        sizeof(bytes_text)
    );


    Rectangle pc_node = {
        (float)(x + 20),
        (float)(y + 50),
        170,
        72
    };

    Rectangle memory_node = {
        (float)(x + 250),
        (float)(y + 50),
        220,
        72
    };

    Rectangle ir_node = {
        (float)(x + 545),
        (float)(y + 50),
        190,
        72
    };

    Rectangle decoder_node = {
        (float)(x + 800),
        (float)(y + 50),
        250,
        72
    };

    Rectangle control_node = {
        (float)(x + 1120),
        (float)(y + 50),
        420,
        72
    };


    draw_datapath_node(
        pc_node,
        "PROGRAM COUNTER",
        pc_text,
        "Instruction address",
        fetching
    );

    draw_datapath_node(
        memory_node,
        "INSTRUCTION MEMORY",
        memory_text,
        "Read machine code",
        fetching
    );

    draw_datapath_node(
        ir_node,
        "INSTRUCTION REGISTER",
        bytes_text,
        "Fetched instruction bytes",
        fetching
    );

    draw_datapath_node(
        decoder_node,
        "INSTRUCTION DECODER",
        gui->decode_status == DECODER_STATUS_OK
            ? "Instruction decoded"
            : "Waiting for decode",
        "Opcode and operands",
        decoding
    );

    const char *phase_name = "IDLE";

    if (fetching) {
        phase_name = "FETCH";
    } else if (decoding) {
        phase_name = "DECODE";
    } else if (executing) {
        phase_name = "EXECUTE";
    }

    draw_datapath_node(
        control_node,
        "CONTROL UNIT",
        TextFormat("ACTIVE PHASE: %s", phase_name),
        gui->alu_preview_valid
            ? "ALU operation detected"
            : "No ALU operation detected",
        fetching || decoding || executing
    );


    /*
     * Instruction flow arrows.
     */

    draw_bus_arrow(
        (float)(x + 192),
        (float)(x + 242),
        (float)(y + 86),
        fetching,
        progress
    );

    draw_bus_arrow(
        (float)(x + 472),
        (float)(x + 537),
        (float)(y + 86),
        fetching,
        progress
    );

    draw_bus_arrow(
        (float)(x + 737),
        (float)(x + 792),
        (float)(y + 86),
        decoding,
        progress
    );

    draw_bus_arrow(
        (float)(x + 1052),
        (float)(x + 1112),
        (float)(y + 86),
        decoding,
        progress
    );


    /*
     * =====================================
     * BOTTOM ROW: REGISTER FILE AND ALU
     * =====================================
     */

    char register_a_text[64];
    char register_b_text[64];

    char alu_line1[100];
    char alu_line2[100];

    char writeback_line1[100];
    char writeback_line2[100];


    /*
     * Register File input values.
     */

    if (gui->alu_preview_valid) {

        snprintf(
            register_a_text,
            sizeof(register_a_text),
            "A: R%u = 0x%04X",
            (unsigned int)gui->alu_reg_a,
            (unsigned int)gui->alu_input_a
        );

        if (gui->alu_is_unary) {

            snprintf(
                register_b_text,
                sizeof(register_b_text),
                "B: Not required"
            );

        } else {

            snprintf(
                register_b_text,
                sizeof(register_b_text),
                "B: R%u = 0x%04X",
                (unsigned int)gui->alu_reg_b,
                (unsigned int)gui->alu_input_b
            );
        }

    } else {

        snprintf(
            register_a_text,
            sizeof(register_a_text),
            "Waiting for operands"
        );

        snprintf(
            register_b_text,
            sizeof(register_b_text),
            "No ALU instruction"
        );
    }


    /*
     * ALU information.
     */

    if (!gui->alu_preview_valid) {

        snprintf(
            alu_line1,
            sizeof(alu_line1),
            "No ALU operation"
        );

        snprintf(
            alu_line2,
            sizeof(alu_line2),
            "Waiting for instruction"
        );

    } else if (gui->alu_instruction_invalid) {

        snprintf(
            alu_line1,
            sizeof(alu_line1),
            "Invalid register combination"
        );

        snprintf(
            alu_line2,
            sizeof(alu_line2),
            "Operation cannot execute"
        );

    } else {

        snprintf(
            alu_line1,
            sizeof(alu_line1),
            "%s | A=0x%04X B=0x%04X",
            gui_alu_operation_name(gui->alu_operation),
            (unsigned int)gui->alu_input_a,
            (unsigned int)gui->alu_input_b
        );

        if (!gui->alu_result_visible) {

            snprintf(
                alu_line2,
                sizeof(alu_line2),
                "Result waiting for EXECUTE"
            );

        } else if (
            gui->alu_preview.status != ALU_STATUS_OK
        ) {

            snprintf(
                alu_line2,
                sizeof(alu_line2),
                "ALU ERROR: %s",
                gui_alu_status_string(
                    gui->alu_preview.status
                )
            );

        } else if (gui->alu_execution_failed) {

            snprintf(
                alu_line2,
                sizeof(alu_line2),
                "CPU did not commit the result"
            );

        } else {

            snprintf(
                alu_line2,
                sizeof(alu_line2),
                "RESULT: 0x%04X",
                (unsigned int)gui->alu_preview.result
            );
        }
    }


    /*
     * Register Writeback information.
     */

    if (!gui->alu_preview_valid) {

        snprintf(
            writeback_line1,
            sizeof(writeback_line1),
            "Waiting for ALU result"
        );

        snprintf(
            writeback_line2,
            sizeof(writeback_line2),
            "No register writeback"
        );

    } else if (gui->alu_instruction_invalid) {

        snprintf(
            writeback_line1,
            sizeof(writeback_line1),
            "Invalid instruction"
        );

        snprintf(
            writeback_line2,
            sizeof(writeback_line2),
            "No register changes"
        );

    } else if (!gui->alu_result_visible) {

        snprintf(
            writeback_line1,
            sizeof(writeback_line1),
            "Waiting for execution"
        );

        snprintf(
            writeback_line2,
            sizeof(writeback_line2),
            "Writeback pending"
        );

    } else if (gui->alu_execution_failed) {

        snprintf(
            writeback_line1,
            sizeof(writeback_line1),
            "EXECUTION FAILED"
        );

        snprintf(
            writeback_line2,
            sizeof(writeback_line2),
            "Register changes not committed"
        );

    } else if (gui->alu_is_compare) {

        snprintf(
            writeback_line1,
            sizeof(writeback_line1),
            "CMP: FLAGS updated"
        );

        snprintf(
            writeback_line2,
            sizeof(writeback_line2),
            "No GPR writeback"
        );

    } else if (gui->alu_is_division) {

        snprintf(
            writeback_line1,
            sizeof(writeback_line1),
            "R%u = 0x%04X (quotient)",
            (unsigned int)gui->alu_reg_a,
            (unsigned int)gui->alu_reg_a_after
        );

        snprintf(
            writeback_line2,
            sizeof(writeback_line2),
            "R%u = 0x%04X (remainder)",
            (unsigned int)gui->alu_reg_b,
            (unsigned int)gui->alu_reg_b_after
        );

    } else {

        snprintf(
            writeback_line1,
            sizeof(writeback_line1),
            "R%u: 0x%04X",
            (unsigned int)gui->alu_reg_a,
            (unsigned int)gui->alu_reg_a_after
        );

        snprintf(
            writeback_line2,
            sizeof(writeback_line2),
            "Previous: 0x%04X",
            (unsigned int)gui->alu_reg_a_before
        );
    }


    Rectangle register_node = {
        (float)(x + 20),
        (float)(y + 175),
        370,
        95
    };

    Rectangle alu_node = {
        (float)(x + 580),
        (float)(y + 175),
        380,
        95
    };

    Rectangle writeback_node = {
        (float)(x + 1160),
        (float)(y + 175),
        380,
        95
    };


    draw_datapath_node(
        register_node,
        "REGISTER FILE",
        register_a_text,
        register_b_text,
        decoding
    );

    draw_datapath_node(
        alu_node,
        "ARITHMETIC LOGIC UNIT",
        alu_line1,
        alu_line2,
        executing
    );

    draw_datapath_node(
        writeback_node,
        "REGISTER WRITEBACK",
        writeback_line1,
        writeback_line2,
        executing && gui->alu_result_visible
    );


    /*
     * Register File -> ALU.
     */

    draw_bus_arrow(
        (float)(x + 392),
        (float)(x + 572),
        (float)(y + 222),
        decoding && gui->alu_preview_valid,
        progress
    );


    /*
     * ALU -> Register Writeback.
     */

    draw_bus_arrow(
        (float)(x + 962),
        (float)(x + 1152),
        (float)(y + 222),
        executing &&
            gui->alu_result_visible &&
            !gui->alu_execution_failed &&
            gui->alu_preview.status == ALU_STATUS_OK,
        progress
    );


    /*
     * =====================================
     * FLAGS & SHORTCUT HINT
     * =====================================
     */

    uint16_t displayed_flags =
        gui->alu_result_visible
            ? gui->alu_flags_after
            : (gui->alu_preview_valid
                ? gui->alu_flags_before
                : cpu->flags);

    DrawText(
        TextFormat(
            "FLAGS: ZF=%u   CF=%u   SF=%u   OF=%u",
            (displayed_flags & CPU_FLAG_ZF) != 0,
            (displayed_flags & CPU_FLAG_CF) != 0,
            (displayed_flags & CPU_FLAG_SF) != 0,
            (displayed_flags & CPU_FLAG_OF) != 0
        ),
        x + 25,
        y + 295,
        15,
        COLOR_YELLOW
    );

    DrawText(
        "F7: OPEN MEMORY VIEWER",
        x + 25,
        y + 316,
        11,
        COLOR_MUTED
    );

    DrawText(
        "16-BIT DATA PATH | 18-BIT ADDRESS PATH",
        x + 1150,
        y + 295,
        12,
        COLOR_MUTED
    );
}

static void draw_datapath(
    const DioCPU *cpu,
    const GUIState *gui
)
{
    draw_datapath_visualization(
        cpu,
        gui,
        (Rectangle){20, 620, 1560, 335}
    );
}

/* =========================================
   STEP 6 — MEMORY VIEWER (INSTRUCTION & DATA)
   ========================================= */

static void draw_memory_viewer(
    const DioCPU *cpu,
    GUIState *gui,
    Rectangle bounds
)
{
    int x = (int)bounds.x;
    int y = (int)bounds.y;

    if (gui->memory_view != GUI_MEMORY_VIEW_INSTRUCTION &&
        gui->memory_view != GUI_MEMORY_VIEW_DATA) {

        gui->memory_view = GUI_MEMORY_VIEW_INSTRUCTION;
    }

    normalize_memory_base(gui);

    bool instruction_view =
        gui->memory_view == GUI_MEMORY_VIEW_INSTRUCTION;

    const char *title = instruction_view
        ? "INSTRUCTION MEMORY VIEWER"
        : "DATA MEMORY VIEWER";

    draw_panel(
        x,
        y,
        (int)bounds.width,
        (int)bounds.height,
        title
    );

    /*
     * =====================================
     * MEMORY VIEW CONTROLS
     * =====================================
     */

    if (draw_memory_control(
            (Rectangle){
                (float)(x + 730),
                (float)(y + 9),
                140,
                30
            },
            "INSTRUCTION",
            instruction_view
        )) {

        gui->memory_view =
            GUI_MEMORY_VIEW_INSTRUCTION;

        memory_go_to_cpu_pointer(cpu, gui);
    }

    if (draw_memory_control(
            (Rectangle){
                (float)(x + 880),
                (float)(y + 9),
                100,
                30
            },
            "DATA",
            !instruction_view
        )) {

        gui->memory_view =
            GUI_MEMORY_VIEW_DATA;

        memory_go_to_cpu_pointer(cpu, gui);

        instruction_view = false;
    }

    if (draw_memory_control(
            (Rectangle){
                (float)(x + 990),
                (float)(y + 9),
                80,
                30
            },
            "< PREV",
            false
        )) {

        memory_move_page(gui, -1);
    }

    if (draw_memory_control(
            (Rectangle){
                (float)(x + 1080),
                (float)(y + 9),
                80,
                30
            },
            "NEXT >",
            false
        )) {

        memory_move_page(gui, 1);
    }

    if (draw_memory_control(
            (Rectangle){
                (float)(x + 1170),
                (float)(y + 9),
                180,
                30
            },
            instruction_view ? "FOLLOW PC" : "FOLLOW MAR",
            false
        )) {

        memory_go_to_cpu_pointer(cpu, gui);
    }

    /*
     * Update selected view after button interaction.
     */

    instruction_view =
        gui->memory_view == GUI_MEMORY_VIEW_INSTRUCTION;

    uint32_t selected_address =
        get_memory_selected_address(
            cpu,
            gui->memory_view
        );

    /*
     * =====================================
     * MEMORY INFORMATION
     * =====================================
     */

    uint32_t page_number =
        gui->memory_base / GUI_MEMORY_PAGE_SIZE + 1u;

    uint32_t total_pages =
        DIOCPU_MEMORY_SIZE / GUI_MEMORY_PAGE_SIZE;

    DrawText(
        TextFormat(
            "BASE: 0x%05X    END: 0x%05X    PAGE: %u / %u",
            (unsigned int)gui->memory_base,
            (unsigned int)(
                gui->memory_base +
                GUI_MEMORY_PAGE_SIZE - 1u
            ),
            (unsigned int)page_number,
            (unsigned int)total_pages
        ),
        x + 22,
        y + 49,
        13,
        COLOR_TEXT
    );

    DrawText(
        TextFormat(
            "%s: 0x%05X",
            instruction_view ? "PC" : "MAR",
            (unsigned int)selected_address
        ),
        x + 1100,
        y + 49,
        13,
        COLOR_YELLOW
    );

    /*
     * =====================================
     * COLUMN HEADERS
     * =====================================
     */

    DrawText(
        "ADDRESS",
        x + 22,
        y + 78,
        11,
        COLOR_MUTED
    );

    DrawText(
        "BYTES (HEX)",
        x + 110,
        y + 78,
        11,
        COLOR_MUTED
    );

    DrawText(
        "ASCII",
        x + 620,
        y + 78,
        11,
        COLOR_MUTED
    );

    if (!instruction_view) {

        DrawText(
            "16-BIT WORDS (LITTLE ENDIAN)",
            x + 835,
            y + 78,
            11,
            COLOR_MUTED
        );
    }

    /*
     * =====================================
     * MEMORY ROWS
     * =====================================
     */

    for (unsigned int row = 0;
         row < GUI_MEMORY_ROWS;
         row++) {

        uint32_t row_address =
            gui->memory_base +
            row * GUI_MEMORY_BYTES_PER_ROW;

        int row_y =
            y + 99 + (int)row * 27;

        /*
         * Address label.
         */

        DrawText(
            TextFormat(
                "0x%05X",
                (unsigned int)row_address
            ),
            x + 22,
            row_y + 4,
            13,
            COLOR_CYAN
        );

        /*
         * ASCII representation.
         */

        char ascii[GUI_MEMORY_BYTES_PER_ROW + 1];

        /*
         * Read and draw each byte.
         */

        for (unsigned int column = 0;
             column < GUI_MEMORY_BYTES_PER_ROW;
             column++) {

            uint32_t address =
                row_address + column;

            uint8_t value = gui_memory_read_byte(
                cpu,
                gui->memory_view,
                address
            );

            /*
             * Convert byte into ASCII.
             * Non-printable characters become '.'.
             */

            ascii[column] =
                (value >= 32 && value <= 126)
                    ? (char)value
                    : '.';

            bool selected =
                address == selected_address ||
                (!instruction_view &&
                 selected_address < DIOCPU_MEMORY_MAX_ADDRESS &&
                 address == selected_address + 1u);

            Color cell_background = selected
                ? (instruction_view
                    ? (Color){75, 65, 25, 255}
                    : (Color){25, 70, 90, 255})
                : ((column % 2u == 0u)
                    ? COLOR_PANEL_ALT
                    : COLOR_PANEL);

            Color cell_border = selected
                ? COLOR_YELLOW
                : COLOR_BORDER;

            int cell_x =
                x + 110 + (int)column * 30;

            Rectangle cell = {
                (float)cell_x,
                (float)row_y,
                26,
                23
            };

            DrawRectangleRec(
                cell,
                cell_background
            );

            DrawRectangleLinesEx(
                cell,
                selected ? 1.5f : 0.7f,
                cell_border
            );

            DrawText(
                TextFormat("%02X", (unsigned int)value),
                cell_x + 3,
                row_y + 5,
                12,
                selected ? COLOR_YELLOW : COLOR_TEXT
            );
        }

        ascii[GUI_MEMORY_BYTES_PER_ROW] = '\0';

        /*
         * ASCII column.
         */

        DrawText(
            ascii,
            x + 620,
            row_y + 5,
            13,
            COLOR_MUTED
        );

        /*
         * =================================
         * DATA MEMORY WORD REPRESENTATION
         * =================================
         */

        if (!instruction_view) {

            for (unsigned int word = 0;
                 word < 8;
                 word++) {

                uint32_t address =
                    row_address + word * 2u;

                uint8_t low = gui_memory_read_byte(
                    cpu,
                    GUI_MEMORY_VIEW_DATA,
                    address
                );

                uint8_t high = gui_memory_read_byte(
                    cpu,
                    GUI_MEMORY_VIEW_DATA,
                    address + 1u
                );

                uint16_t value =
                    (uint16_t)low |
                    ((uint16_t)high << 8u);

                bool selected =
                    selected_address == address ||
                    selected_address == address + 1u;

                int word_x =
                    x + 835 + (int)word * 82;

                Rectangle word_cell = {
                    (float)word_x,
                    (float)row_y,
                    72,
                    23
                };

                DrawRectangleRec(
                    word_cell,
                    selected
                        ? (Color){65, 55, 25, 255}
                        : COLOR_PANEL_ALT
                );

                DrawRectangleLinesEx(
                    word_cell,
                    selected ? 1.5f : 0.7f,
                    selected ? COLOR_YELLOW : COLOR_BORDER
                );

                DrawText(
                    TextFormat(
                        "%04X",
                        (unsigned int)value
                    ),
                    word_x + 10,
                    row_y + 5,
                    13,
                    selected ? COLOR_YELLOW : COLOR_TEXT
                );
            }
        }
    }

    /*
     * =====================================
     * FOOTER
     * =====================================
     */

    DrawText(
        instruction_view
            ? "F7: BACK TO DATAPATH | I: Instruction | D: Data | PgUp/PgDn: Page | Home: PC | PC is highlighted in yellow"
            : "F7: BACK TO DATAPATH | I: Instruction | D: Data | PgUp/PgDn: Page | Home: MAR | MAR and next byte are highlighted",
        x + 22,
        y + 316,
        12,
        COLOR_MUTED
    );

    DrawText(
        "READ ONLY VIEW",
        x + 1400,
        y + 316,
        11,
        COLOR_GREEN
    );
}

/* =========================================
   STACK VIEWER
   ========================================= */

static void draw_stack_viewer(
    const DioCPU *cpu,
    GUIState *gui,
    Rectangle bounds
)
{
    int x = (int)bounds.x;
    int y = (int)bounds.y;

    bool sp_valid = gui_stack_sp_valid(cpu->sp);

    /*
     * Follow SP automatically when enabled.
     */

    if (gui->stack_follow_sp && sp_valid) {

        gui->stack_view_base = cpu->sp;
    }

    /*
     * Ensure page alignment.
     */

    gui->stack_view_base -=
        gui->stack_view_base % 2u;

    /*
     * Clamp view base to stack bounds.
     */

    if (sp_valid) {

        if (gui->stack_view_base < cpu->sp) {
            gui->stack_view_base = cpu->sp;
        }

        if (
            gui->stack_view_base >
            DIOCPU_STACK_INITIAL_SP
        ) {

            gui->stack_view_base =
                DIOCPU_STACK_INITIAL_SP;
        }
    }

    draw_panel(
        x,
        y,
        (int)bounds.width,
        (int)bounds.height,
        "STACK MEMORY VIEWER"
    );

    /*
     * =====================================
     * CONTROLS
     * =====================================
     */

    if (draw_stack_control(
            (Rectangle){
                (float)(x + 1050),
                (float)(y + 9),
                130,
                30
            },
            "FOLLOW SP",
            gui->stack_follow_sp
        )) {

        gui_stack_follow_pointer(cpu, gui);
    }

    if (draw_stack_control(
            (Rectangle){
                (float)(x + 1190),
                (float)(y + 9),
                90,
                30
            },
            "OLDER >",
            false
        )) {

        gui_stack_view_older(gui);
    }

    if (draw_stack_control(
            (Rectangle){
                (float)(x + 1290),
                (float)(y + 9),
                90,
                30
            },
            "< NEWER",
            false
        )) {

        gui_stack_view_newer(cpu, gui);
    }

    /*
     * =====================================
     * STACK STATISTICS
     * =====================================
     */

    if (sp_valid) {

        uint32_t used_bytes =
            gui_stack_used_bytes(cpu);

        uint32_t free_bytes =
            gui_stack_free_bytes(cpu);

        uint32_t used_words =
            gui_stack_used_words(cpu);

        uint32_t capacity_words =
            gui_stack_capacity_words();

        DrawText(
            TextFormat(
                "SP: 0x%05X",
                (unsigned int)cpu->sp
            ),
            x + 22,
            y + 49,
            14,
            COLOR_YELLOW
        );

        DrawText(
            TextFormat(
                "USED: %u / %u WORDS",
                (unsigned int)used_words,
                (unsigned int)capacity_words
            ),
            x + 245,
            y + 49,
            13,
            COLOR_TEXT
        );

        DrawText(
            TextFormat(
                "USED BYTES: %u",
                (unsigned int)used_bytes
            ),
            x + 570,
            y + 49,
            13,
            COLOR_CYAN
        );

        DrawText(
            TextFormat(
                "FREE BYTES: %u",
                (unsigned int)free_bytes
            ),
            x + 790,
            y + 49,
            13,
            COLOR_GREEN
        );

    } else {

        DrawText(
            "INVALID STACK POINTER",
            x + 22,
            y + 49,
            14,
            COLOR_RED
        );
    }

    /*
     * =====================================
     * EVENT INFORMATION
     * =====================================
     */

    draw_stack_event_information(
        gui,
        x + 22,
        y + 78
    );

    /*
     * =====================================
     * STACK TABLE HEADERS
     * =====================================
     */

    int table_y = y + 130;

    DrawText(
        "ADDRESS",
        x + 22,
        table_y,
        11,
        COLOR_MUTED
    );

    DrawText(
        "LOW BYTE",
        x + 180,
        table_y,
        11,
        COLOR_MUTED
    );

    DrawText(
        "HIGH BYTE",
        x + 320,
        table_y,
        11,
        COLOR_MUTED
    );

    DrawText(
        "16-BIT VALUE",
        x + 500,
        table_y,
        11,
        COLOR_MUTED
    );

    DrawText(
        "STACK STATUS",
        x + 690,
        table_y,
        11,
        COLOR_MUTED
    );

    /*
     * =====================================
     * STACK ROWS
     * =====================================
     */

    for (unsigned int i = 0;
         i < GUI_STACK_VIEW_WORDS;
         i++) {

        uint32_t address =
            gui->stack_view_base + i * 2u;

        int row_y =
            table_y + 18 + (int)i * 17;

        if (address + 1u > DIOCPU_STACK_TOP) {
            break;
        }

        uint8_t low =
            cpu->memory.data[address];

        uint8_t high =
            cpu->memory.data[address + 1u];

        uint16_t value =
            (uint16_t)low |
            ((uint16_t)high << 8u);

        bool active =
            sp_valid &&
            address >= cpu->sp &&
            address + 1u < DIOCPU_STACK_INITIAL_SP;

        bool is_top =
            active && address == cpu->sp;

        bool event_address =
            gui->stack_event_timer > 0.0f &&
            address == gui->stack_event_address;

        bool event_high_address =
            gui->stack_event_timer > 0.0f &&
            gui->stack_event_type == GUI_STACK_EVENT_CALL &&
            address == gui->stack_event_address + 2u;

        Color background = is_top
            ? (Color){65, 55, 25, 255}
            : ((active)
                ? COLOR_PANEL_ALT
                : COLOR_PANEL);

        Color border = is_top
            ? COLOR_YELLOW
            : (event_address || event_high_address
                ? COLOR_CYAN
                : COLOR_BORDER);

        DrawRectangle(
            x + 20,
            row_y,
            950,
            15,
            background
        );

        DrawRectangleLines(
            x + 20,
            row_y,
            950,
            15,
            border
        );

        /*
         * ADDRESS
         */

        DrawText(
            TextFormat(
                "0x%05X",
                (unsigned int)address
            ),
            x + 22,
            row_y + 2,
            11,
            COLOR_CYAN
        );

        /*
         * LOW BYTE
         */

        DrawText(
            TextFormat(
                "%02X",
                (unsigned int)low
            ),
            x + 190,
            row_y + 2,
            11,
            COLOR_TEXT
        );

        /*
         * HIGH BYTE
         */

        DrawText(
            TextFormat(
                "%02X",
                (unsigned int)high
            ),
            x + 335,
            row_y + 2,
            11,
            COLOR_TEXT
        );

        /*
         * 16-BIT WORD
         */

        DrawText(
            TextFormat(
                "0x%04X",
                (unsigned int)value
            ),
            x + 515,
            row_y + 2,
            11,
            COLOR_YELLOW
        );

        /*
         * STATUS
         */

        const char *status = "INACTIVE";

        Color status_color = COLOR_MUTED;

        if (is_top) {

            status = "TOP OF STACK";
            status_color = COLOR_YELLOW;

        } else if (active) {

            status = "ACTIVE";
            status_color = COLOR_GREEN;
        }

        if (event_address) {

            if (gui->stack_event_type == GUI_STACK_EVENT_PUSH) {
                status = "PUSHED";
                status_color = COLOR_GREEN;
            }

            if (gui->stack_event_type == GUI_STACK_EVENT_CALL) {
                status = "RETURN LOW";
                status_color = COLOR_CYAN;
            }
        }

        if (event_high_address) {

            status = "RETURN HIGH";
            status_color = COLOR_CYAN;
        }

        DrawText(
            status,
            x + 690,
            row_y + 2,
            11,
            status_color
        );
    }

    /*
     * =====================================
     * EMPTY STACK MESSAGE / INFO BOX
     * =====================================
     */

    if (sp_valid && cpu->sp == DIOCPU_STACK_INITIAL_SP) {

        DrawText(
            "STACK IS EMPTY",
            x + 1000,
            y + 165,
            18,
            COLOR_MUTED
        );

        DrawText(
            "Initial SP is at 0x3FFFE.\nPushing 16-bit values decrements SP by 2.\nCalling subroutines pushes 32-bit return address (4 bytes).",
            x + 1000,
            y + 195,
            12,
            COLOR_MUTED
        );

    } else if (sp_valid) {

        DrawText(
            "STACK MEMORY REGION",
            x + 1000,
            y + 140,
            14,
            COLOR_CYAN
        );

        DrawText(
            TextFormat("Current SP: 0x%05X\nInitial SP: 0x%05X\nStack Bottom: 0x%05X",
                (unsigned int)cpu->sp,
                (unsigned int)DIOCPU_STACK_INITIAL_SP,
                (unsigned int)DIOCPU_STACK_BOTTOM),
            x + 1000,
            y + 165,
            12,
            COLOR_TEXT
        );

        DrawText(
            "Stack grows downwards towards lower addresses.\nLow byte is stored at address N, high byte at N+1.",
            x + 1000,
            y + 225,
            12,
            COLOR_MUTED
        );
    }

    /*
     * =====================================
     * FOOTER
     * =====================================
     */

    DrawText(
        "STACK GROWS DOWNWARD | 16-BIT PUSH/POP | 32-BIT CALL/RET",
        x + 22,
        y + 316,
        12,
        COLOR_MUTED
    );

    DrawText(
        "F7: NEXT WORKSPACE",
        x + 1320,
        y + 316,
        11,
        COLOR_ACCENT
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
        960,
        1560,
        32,
        COLOR_PANEL
    );

    DrawRectangleLines(
        20,
        960,
        1560,
        32,
        COLOR_BORDER
    );

    DrawText(
        TextFormat(
            "SHORTCUTS: F5: %s | F8: Step Phase | F10: Step Instr | F7: %s | F6: Reset CPU",
            gui->auto_run ? "Pause" : "Run",
            gui->workspace_view == GUI_WORKSPACE_DATAPATH ? "Memory View" :
            (gui->workspace_view == GUI_WORKSPACE_MEMORY ? "Stack View" : "Datapath View")
        ),
        35,
        969,
        13,
        COLOR_MUTED
    );

    DrawText(
        TextFormat(
            "WORKSPACE: %s | ANIMATION: %s | PHASE: %s",
            gui->workspace_view == GUI_WORKSPACE_DATAPATH ? "DATAPATH" :
            (gui->workspace_view == GUI_WORKSPACE_MEMORY ? "MEMORY" : "STACK"),
            gui->auto_run ? "RUNNING" : "PAUSED",
            gui->phase == GUI_PHASE_FETCH ? "FETCH" :
            (gui->phase == GUI_PHASE_DECODE ? "DECODE" :
            (gui->phase == GUI_PHASE_EXECUTE ? "EXECUTE" : "IDLE"))
        ),
        1050,
        969,
        13,
        gui->workspace_view == GUI_WORKSPACE_MEMORY ? COLOR_CYAN :
        (gui->workspace_view == GUI_WORKSPACE_STACK ? COLOR_YELLOW :
        (gui->auto_run ? COLOR_GREEN : COLOR_YELLOW))
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
        "DioCPU V2 - Interactive Simulator, Datapath & Memory Viewer"
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

    gui.workspace_view = GUI_WORKSPACE_DATAPATH;

    gui.memory_view = GUI_MEMORY_VIEW_INSTRUCTION;

    gui.memory_base = 0;

    gui.stack_follow_sp = true;

    gui.stack_view_base = cpu.sp;

    gui.stack_event_type = GUI_STACK_EVENT_NONE;

    gui.phase_interval = 0.35f;

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

        if (gui.stack_event_timer > 0.0f) {

            gui.stack_event_timer -= delta_time;

            if (gui.stack_event_timer <= 0.0f) {

                clear_stack_event(&gui);
            }
        }


        /* =================================
           ANIMATION TIMER
           ================================= */

        gui.datapath_animation_time += delta_time * 0.8f;

        while (gui.datapath_animation_time >= 1.0f) {
            gui.datapath_animation_time -= 1.0f;
        }


        /* =================================
           WORKSPACE VIEW CONTROL
           ================================= */

        if (IsKeyPressed(KEY_F7)) {

            gui.workspace_view =
                (gui.workspace_view + 1) % 3;

            if (
                gui.workspace_view ==
                GUI_WORKSPACE_DATAPATH
            ) {

                snprintf(
                    gui.status_message,
                    sizeof(gui.status_message),
                    "Datapath Viewer opened."
                );

            } else if (
                gui.workspace_view ==
                GUI_WORKSPACE_MEMORY
            ) {

                memory_go_to_cpu_pointer(
                    &cpu,
                    &gui
                );

                snprintf(
                    gui.status_message,
                    sizeof(gui.status_message),
                    "Memory Viewer opened."
                );

            } else if (
                gui.workspace_view ==
                GUI_WORKSPACE_STACK
            ) {

                gui_stack_follow_pointer(
                    &cpu,
                    &gui
                );

                snprintf(
                    gui.status_message,
                    sizeof(gui.status_message),
                    "Stack Viewer opened."
                );
            }

            gui.status_color = COLOR_CYAN;
        }


        /* =================================
           MEMORY NAVIGATION SHORTCUTS
           ================================= */

        if (gui.workspace_view == GUI_WORKSPACE_MEMORY) {

            if (IsKeyPressed(KEY_PAGE_UP)) {
                memory_move_page(&gui, -1);
            }

            if (IsKeyPressed(KEY_PAGE_DOWN)) {
                memory_move_page(&gui, 1);
            }

            if (IsKeyPressed(KEY_HOME)) {
                memory_go_to_cpu_pointer(&cpu, &gui);
            }

            if (IsKeyPressed(KEY_I)) {

                gui.memory_view =
                    GUI_MEMORY_VIEW_INSTRUCTION;

                memory_go_to_cpu_pointer(&cpu, &gui);
            }

            if (IsKeyPressed(KEY_D)) {

                gui.memory_view =
                    GUI_MEMORY_VIEW_DATA;

                memory_go_to_cpu_pointer(&cpu, &gui);
            }
        }


        /* =================================
           STACK NAVIGATION SHORTCUTS
           ================================= */

        if (
            gui.workspace_view ==
            GUI_WORKSPACE_STACK
        ) {

            if (IsKeyPressed(KEY_HOME)) {

                gui_stack_follow_pointer(
                    &cpu,
                    &gui
                );
            }

            if (IsKeyPressed(KEY_PAGE_UP)) {

                gui_stack_view_older(
                    &gui
                );
            }

            if (IsKeyPressed(KEY_PAGE_DOWN)) {

                gui_stack_view_newer(
                    &cpu,
                    &gui
                );
            }
        }


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

            clear_stack_event(&gui);

            gui_stack_follow_pointer(
                &cpu,
                &gui
            );
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

        draw_program_panel(&cpu, &gui);

        draw_cycle_panel(&cpu, &gui);

        draw_register_panel(&cpu, &gui);

        draw_cpu_information(&cpu, &gui);

        if (
            gui.workspace_view ==
            GUI_WORKSPACE_DATAPATH
        ) {

            draw_datapath(
                &cpu,
                &gui
            );

        } else if (
            gui.workspace_view ==
            GUI_WORKSPACE_MEMORY
        ) {

            draw_memory_viewer(
                &cpu,
                &gui,
                (Rectangle){20, 620, 1560, 335}
            );

        } else if (
            gui.workspace_view ==
            GUI_WORKSPACE_STACK
        ) {

            draw_stack_viewer(
                &cpu,
                &gui,
                (Rectangle){20, 620, 1560, 335}
            );
        }


        EndDrawing();
    }


    CloseWindow();

    return 0;
}
