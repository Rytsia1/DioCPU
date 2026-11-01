#include "raylib.h"
#include "cpu_v2.h"

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

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

/* =========================================
   GUI STATE
   ========================================= */

typedef struct {
    char status_message[160];
    Color status_color;
    bool initialization_failed;
} GUIState;


/* =========================================
   BASIC UI COMPONENTS
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

    int text_width = MeasureText(label, 15);

    DrawText(
        label,
        (int)(bounds.x +
            (bounds.width - text_width) / 2),
        (int)(bounds.y + 11),
        15,
        COLOR_TEXT
    );

    return hovered &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}


/* =========================================
   HEADER
   ========================================= */

static void draw_header(const GUIState *gui)
{
    DrawText(
        "DioCPU V2",
        22,
        20,
        30,
        COLOR_ACCENT
    );

    DrawText(
        "16-BIT CPU ARCHITECTURE SIMULATOR",
        22,
        57,
        13,
        COLOR_MUTED
    );

    DrawText(
        gui->status_message,
        650,
        32,
        14,
        gui->status_color
    );

    DrawText(
        "GUI DEVELOPMENT",
        1370,
        36,
        12,
        COLOR_MUTED
    );
}


/* =========================================
   CPU STATUS
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
        20, 100, 295, 80,
        "PROGRAM COUNTER",
        pc_text,
        COLOR_YELLOW
    );

    draw_metric_card(
        335, 100, 295, 80,
        "STACK POINTER",
        sp_text,
        COLOR_CYAN
    );

    draw_metric_card(
        650, 100, 295, 80,
        "MEMORY ADDRESS REGISTER",
        mar_text,
        COLOR_CYAN
    );

    draw_metric_card(
        965, 100, 295, 80,
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
        1280, 100, 300, 80,
        "CPU STATE",
        state_text,
        get_cpu_state_color(cpu->state)
    );
}


/* =========================================
   GENERAL PURPOSE REGISTER PANEL
   ========================================= */

static void draw_register_panel(const DioCPU *cpu)
{
    draw_panel(
        20,
        205,
        760,
        390,
        "GENERAL PURPOSE REGISTER FILE"
    );

    for (unsigned int i = 0;
         i < CPU_GPR_COUNT;
         i++) {

        int column = (int)(i % 4);
        int row = (int)(i / 4);

        int x = 38 + column * 183;
        int y = 250 + row * 78;

        DrawRectangle(
            x,
            y,
            165,
            62,
            COLOR_PANEL_ALT
        );

        DrawRectangleLines(
            x,
            y,
            165,
            62,
            COLOR_BORDER
        );

        DrawText(
            TextFormat("R%u", i),
            x + 12,
            y + 8,
            14,
            COLOR_MUTED
        );

        DrawText(
            TextFormat(
                "0x%04X",
                (unsigned int)cpu->gpr[i]
            ),
            x + 12,
            y + 30,
            21,
            COLOR_TEXT
        );
    }

    DrawText(
        "16 GENERAL PURPOSE REGISTERS | 16-BIT EACH",
        38,
        568,
        12,
        COLOR_MUTED
    );
}


/* =========================================
   FLAGS PANEL
   ========================================= */

static void draw_flag_card(
    int x,
    int y,
    const char *name,
    const char *description,
    bool enabled
)
{
    Color state_color = enabled
        ? COLOR_GREEN
        : COLOR_MUTED;

    DrawRectangle(
        x,
        y,
        165,
        90,
        COLOR_PANEL_ALT
    );

    DrawRectangleLines(
        x,
        y,
        165,
        90,
        enabled ? COLOR_GREEN : COLOR_BORDER
    );

    DrawText(
        name,
        x + 13,
        y + 10,
        20,
        state_color
    );

    DrawText(
        enabled ? "SET (1)" : "CLEAR (0)",
        x + 13,
        y + 38,
        13,
        state_color
    );

    DrawText(
        description,
        x + 13,
        y + 65,
        10,
        COLOR_MUTED
    );
}


static void draw_flags_panel(const DioCPU *cpu)
{
    draw_panel(
        810,
        405,
        770,
        190,
        "STATUS FLAGS"
    );

    draw_flag_card(
        830, 455,
        "ZF",
        "Zero Flag",
        (cpu->flags & CPU_FLAG_ZF) != 0
    );

    draw_flag_card(
        1010, 455,
        "CF",
        "Carry Flag",
        (cpu->flags & CPU_FLAG_CF) != 0
    );

    draw_flag_card(
        1190, 455,
        "SF",
        "Sign Flag",
        (cpu->flags & CPU_FLAG_SF) != 0
    );

    draw_flag_card(
        1370, 455,
        "OF",
        "Overflow Flag",
        (cpu->flags & CPU_FLAG_OF) != 0
    );
}


/* =========================================
   CPU INFORMATION PANEL
   ========================================= */

static void draw_cpu_information(const DioCPU *cpu)
{
    draw_panel(
        810,
        205,
        770,
        180,
        "EXECUTION STATUS"
    );

    DrawText(
        TextFormat(
            "Instructions executed: %llu",
            (unsigned long long)cpu->instructions_executed
        ),
        835,
        260,
        17,
        COLOR_TEXT
    );

    DrawText(
        TextFormat(
            "CPU State: %s",
            cpu_v2_state_string(cpu->state)
        ),
        835,
        292,
        17,
        get_cpu_state_color(cpu->state)
    );

    DrawText(
        TextFormat(
            "Fault: %s",
            cpu_v2_fault_string(cpu->fault)
        ),
        835,
        324,
        15,
        cpu->fault == CPU_FAULT_NONE
            ? COLOR_MUTED
            : COLOR_RED
    );

    DrawText(
        TextFormat(
            "Fault PC: 0x%05X",
            (unsigned int)cpu->fault_pc
        ),
        835,
        350,
        14,
        COLOR_MUTED
    );
}


/* =========================================
   MEMORY ARCHITECTURE PANEL
   ========================================= */

static void draw_memory_card(
    int x,
    int y,
    const char *title,
    const char *capacity,
    const char *description
)
{
    DrawRectangle(
        x,
        y,
        340,
        110,
        COLOR_PANEL_ALT
    );

    DrawRectangleLines(
        x,
        y,
        340,
        110,
        COLOR_BORDER
    );

    DrawText(
        title,
        x + 15,
        y + 12,
        16,
        COLOR_ACCENT
    );

    DrawText(
        capacity,
        x + 15,
        y + 39,
        22,
        COLOR_TEXT
    );

    DrawText(
        description,
        x + 15,
        y + 78,
        12,
        COLOR_MUTED
    );
}


static void draw_architecture_panel(void)
{
    draw_panel(
        20,
        620,
        1560,
        350,
        "MEMORY & ARCHITECTURE"
    );

    draw_memory_card(
        40,
        675,
        "INSTRUCTION MEMORY",
        "256 KiB",
        "Stores machine instructions"
    );

    draw_memory_card(
        400,
        675,
        "DATA MEMORY",
        "256 KiB",
        "Stores program data"
    );


    DrawText(
        "HARVARD ARCHITECTURE",
        800,
        690,
        19,
        COLOR_CYAN
    );

    DrawText(
        "Instruction and Data Memory are separated.",
        800,
        722,
        14,
        COLOR_TEXT
    );

    DrawText(
        "Address width: 18-bit",
        800,
        750,
        14,
        COLOR_TEXT
    );

    DrawText(
        "Data width: 16-bit",
        800,
        778,
        14,
        COLOR_TEXT
    );


    DrawText(
        "STACK REGION",
        1200,
        690,
        16,
        COLOR_ACCENT
    );

    DrawText(
        "Bottom: 0x30000",
        1200,
        722,
        14,
        COLOR_TEXT
    );

    DrawText(
        "Top: 0x3FFFF",
        1200,
        750,
        14,
        COLOR_TEXT
    );

    DrawText(
        "Initial SP: 0x3FFFE",
        1200,
        778,
        14,
        COLOR_TEXT
    );


    DrawText(
        "DioCPU V2 | CPU Dashboard | Press F6 to reset CPU",
        40,
        925,
        12,
        COLOR_MUTED
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
        "DioCPU V2 - CPU Dashboard"
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


    Rectangle reset_button = {
        1390, 30, 180, 45
    };


    while (!WindowShouldClose()) {

        /*
         * Reset CPU.
         * Memory contents are preserved.
         */

        bool reset_pressed =
            IsKeyPressed(KEY_F6) ||
            draw_button(
                reset_button,
                "RESET CPU"
            );

        if (reset_pressed &&
            !gui.initialization_failed) {

            CPUStatus status = cpu_v2_reset(&cpu);

            if (status == CPU_STATUS_OK) {

                snprintf(
                    gui.status_message,
                    sizeof(gui.status_message),
                    "CPU reset successfully. Memory preserved."
                );

                gui.status_color = COLOR_GREEN;

            } else {

                snprintf(
                    gui.status_message,
                    sizeof(gui.status_message),
                    "CPU reset failed."
                );

                gui.status_color = COLOR_RED;
            }
        }


        BeginDrawing();

        ClearBackground(COLOR_BACKGROUND);

        draw_header(&gui);

        draw_cpu_dashboard(&cpu);

        draw_register_panel(&cpu);

        draw_cpu_information(&cpu);

        draw_flags_panel(&cpu);

        draw_architecture_panel();

        draw_button(
            reset_button,
            "RESET CPU"
        );

        EndDrawing();
    }


    CloseWindow();

    return 0;
}
