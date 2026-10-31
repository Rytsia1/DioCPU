#include "raylib.h"
#include "cpu.h"
#include "assembler.h"

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define SCREEN_WIDTH 1280
#define SCREEN_HEIGHT 800
#define MAX_INPUT_CHARS 128

// Warna antarmuka
#define COLOR_BG        (Color){20, 24, 32, 255}
#define COLOR_PANEL     (Color){32, 38, 50, 255}
#define COLOR_BORDER    (Color){55, 68, 86, 255}
#define COLOR_ACCENT    (Color){70, 170, 230, 255}
#define COLOR_TEXT      (Color){230, 235, 245, 255}
#define COLOR_MUTED     (Color){135, 148, 168, 255}
#define COLOR_HIGHLIGHT (Color){45, 100, 130, 255}

// Warna siklus instruksi (Fetch - Decode - Execute)
#define COLOR_FETCH     (Color){245, 185, 55, 255}   // Emas / Kuning hangat
#define COLOR_DECODE    (Color){70, 195, 245, 255}   // Biru muda / Cyan
#define COLOR_EXECUTE   (Color){60, 220, 130, 255}   // Hijau zamrud

// Enum fase siklus instruksi
typedef enum {
    PHASE_IDLE,
    PHASE_FETCH,
    PHASE_DECODE,
    PHASE_EXECUTE
} CyclePhase;

// Enum mode langkah (Langkah per fase atau langkah per instruksi penuh)
typedef enum {
    STEP_MODE_PHASE,
    STEP_MODE_INSTRUCTION
} StepMode;

// Enum tampilan tab panel kanan (Datapath vs Memory Hex)
typedef enum {
    VIEW_DATAPATH,
    VIEW_MEMORY
} RightPanelView;

// Sinyal kontrol CPU (Control Signals)
typedef struct {
    bool reg_write;
    bool alu_src_imm;
    const char *alu_op;
    bool branch;
    bool branch_taken;
    bool is_out;
    bool is_halt;
} ControlSignals;

// Struktur penyimpan status siklus instruksi & datapath
typedef struct {
    CyclePhase phase;
    uint16_t fetch_pc;
    uint8_t opcode;
    uint8_t operand1;
    uint8_t operand2;
    char disasm_text[64];
    char decode_desc[128];
    char execute_desc[128];
    int modified_reg;   // Register tujuan (-1 jika tidak ada)
    int source_reg;     // Register sumber (-1 jika tidak ada)
    ControlSignals ctrl;

    // Nilai ALU
    uint8_t alu_in_a;
    uint8_t alu_in_b;
    uint8_t alu_result;
    bool alu_active;
} InstructionCycleState;

// Reset siklus ke status awal
static void reset_cycle(InstructionCycleState *cycle)
{
    cycle->phase = PHASE_IDLE;
    cycle->fetch_pc = 0;
    cycle->opcode = 0;
    cycle->operand1 = 0;
    cycle->operand2 = 0;
    cycle->disasm_text[0] = '\0';
    snprintf(cycle->decode_desc, sizeof(cycle->decode_desc), "Menunggu instruksi...");
    snprintf(cycle->execute_desc, sizeof(cycle->execute_desc), "Belum dieksekusi.");
    cycle->modified_reg = -1;
    cycle->source_reg = -1;

    cycle->ctrl.reg_write = false;
    cycle->ctrl.alu_src_imm = false;
    cycle->ctrl.alu_op = "NONE";
    cycle->ctrl.branch = false;
    cycle->ctrl.branch_taken = false;
    cycle->ctrl.is_out = false;
    cycle->ctrl.is_halt = false;

    cycle->alu_in_a = 0;
    cycle->alu_in_b = 0;
    cycle->alu_result = 0;
    cycle->alu_active = false;
}

// Analisis instruksi untuk fase DECODE dan pembangkit sinyal kontrol
static void decode_instruction(
    uint8_t opcode,
    uint8_t op1,
    uint8_t op2,
    const CPU *cpu,
    InstructionCycleState *cycle
)
{
    cycle->modified_reg = -1;
    cycle->source_reg = -1;
    cycle->alu_active = false;

    cycle->ctrl.reg_write = false;
    cycle->ctrl.alu_src_imm = false;
    cycle->ctrl.alu_op = "NONE";
    cycle->ctrl.branch = false;
    cycle->ctrl.branch_taken = false;
    cycle->ctrl.is_out = false;
    cycle->ctrl.is_halt = false;

    switch (opcode)
    {
        case OP_LOAD:
            if (op1 < REGISTER_COUNT)
            {
                cycle->modified_reg = op1;
            }
            cycle->ctrl.reg_write = true;
            cycle->ctrl.alu_src_imm = true;
            cycle->ctrl.alu_op = "PASS";
            cycle->alu_active = true;
            cycle->alu_in_a = 0;
            cycle->alu_in_b = op2;
            cycle->alu_result = op2;

            snprintf(
                cycle->decode_desc, sizeof(cycle->decode_desc),
                "LOAD: Menyiapkan pengisian nilai immediate %u ke Register R%u",
                op2, op1
            );
            break;

        case OP_ADD:
            if (op1 < REGISTER_COUNT)
            {
                cycle->modified_reg = op1;
            }
            if (op2 < REGISTER_COUNT)
            {
                cycle->source_reg = op2;
            }
            cycle->ctrl.reg_write = true;
            cycle->ctrl.alu_src_imm = false;
            cycle->ctrl.alu_op = "ADD";
            cycle->alu_active = true;
            cycle->alu_in_a = (op1 < REGISTER_COUNT) ? cpu->registers[op1] : 0;
            cycle->alu_in_b = (op2 < REGISTER_COUNT) ? cpu->registers[op2] : 0;
            cycle->alu_result = (uint8_t)(cycle->alu_in_a + cycle->alu_in_b);

            snprintf(
                cycle->decode_desc, sizeof(cycle->decode_desc),
                "ADD: Menyiapkan ALU [ADD] R%u (%u) + R%u (%u)",
                op1, cycle->alu_in_a, op2, cycle->alu_in_b
            );
            break;

        case OP_SUB:
            if (op1 < REGISTER_COUNT)
            {
                cycle->modified_reg = op1;
            }
            if (op2 < REGISTER_COUNT)
            {
                cycle->source_reg = op2;
            }
            cycle->ctrl.reg_write = true;
            cycle->ctrl.alu_src_imm = false;
            cycle->ctrl.alu_op = "SUB";
            cycle->alu_active = true;
            cycle->alu_in_a = (op1 < REGISTER_COUNT) ? cpu->registers[op1] : 0;
            cycle->alu_in_b = (op2 < REGISTER_COUNT) ? cpu->registers[op2] : 0;
            cycle->alu_result = (uint8_t)(cycle->alu_in_a - cycle->alu_in_b);

            snprintf(
                cycle->decode_desc, sizeof(cycle->decode_desc),
                "SUB: Menyiapkan ALU [SUB] R%u (%u) - R%u (%u)",
                op1, cycle->alu_in_a, op2, cycle->alu_in_b
            );
            break;

        case OP_JMP:
            cycle->ctrl.branch = true;
            cycle->ctrl.branch_taken = true;
            snprintf(
                cycle->decode_desc, sizeof(cycle->decode_desc),
                "JMP: Menyiapkan lompatan tanpa syarat ke alamat 0x%02X",
                op1
            );
            break;

        case OP_JZ:
            cycle->ctrl.branch = true;
            cycle->ctrl.branch_taken = cpu->zero_flag;
            if (cpu->zero_flag)
            {
                snprintf(cycle->decode_desc, sizeof(cycle->decode_desc), "JZ: Zero Flag = 1 -> Bersiap melompat ke alamat 0x%02X", op1);
            }
            else
            {
                snprintf(cycle->decode_desc, sizeof(cycle->decode_desc), "JZ: Zero Flag = 0 -> Syarat tidak terpenuhi, lanjut PC+3");
            }
            break;

        case OP_JNZ:
            cycle->ctrl.branch = true;
            cycle->ctrl.branch_taken = !cpu->zero_flag;
            if (!cpu->zero_flag)
            {
                snprintf(cycle->decode_desc, sizeof(cycle->decode_desc), "JNZ: Zero Flag = 0 -> Bersiap melompat ke alamat 0x%02X", op1);
            }
            else
            {
                snprintf(cycle->decode_desc, sizeof(cycle->decode_desc), "JNZ: Zero Flag = 1 -> Syarat tidak terpenuhi, lanjut PC+3");
            }
            break;

        case OP_OUT:
            cycle->ctrl.is_out = true;
            if (op1 == OUT_MODE_REGISTER)
            {
                if (op2 < REGISTER_COUNT)
                {
                    cycle->source_reg = op2;
                }
                snprintf(
                    cycle->decode_desc, sizeof(cycle->decode_desc),
                    "OUT: Menyiapkan bus I/O dengan isi Register R%u (%u)",
                    op2, (op2 < REGISTER_COUNT) ? cpu->registers[op2] : 0
                );
            }
            else if (op1 == OUT_MODE_IMMEDIATE)
            {
                snprintf(
                    cycle->decode_desc, sizeof(cycle->decode_desc),
                    "OUT: Menyiapkan bus I/O dengan nilai langsung %u",
                    op2
                );
            }
            else
            {
                snprintf(cycle->decode_desc, sizeof(cycle->decode_desc), "OUT: Mode output tidak valid (0x%02X)", op1);
            }
            break;

        case OP_HLT:
            cycle->ctrl.is_halt = true;
            snprintf(cycle->decode_desc, sizeof(cycle->decode_desc), "HLT: Menyiapkan sinyal penghentian CPU (HALT)");
            break;

        default:
            snprintf(cycle->decode_desc, sizeof(cycle->decode_desc), "Opcode 0x%02X tidak dikenali oleh dekoder", opcode);
            break;
    }
}

// Analisis hasil instruksi untuk fase EXECUTE
static void describe_execution(
    uint8_t opcode,
    uint8_t op1,
    uint8_t op2,
    const CPU *cpu_before,
    const CPU *cpu_after,
    char *exec_desc,
    size_t desc_size
)
{
    switch (opcode)
    {
        case OP_LOAD:
            snprintf(
                exec_desc, desc_size,
                "R%u diisi %u | PC: 0x%02X -> 0x%02X",
                op1, (op1 < REGISTER_COUNT) ? cpu_after->registers[op1] : 0,
                cpu_before->pc, cpu_after->pc
            );
            break;

        case OP_ADD:
            snprintf(
                exec_desc, desc_size,
                "ALU ADD: Hasil = %u | ZF: %d, CF: %d | PC -> 0x%02X",
                (op1 < REGISTER_COUNT) ? cpu_after->registers[op1] : 0,
                cpu_after->zero_flag, cpu_after->carry_flag,
                cpu_after->pc
            );
            break;

        case OP_SUB:
            snprintf(
                exec_desc, desc_size,
                "ALU SUB: Hasil = %u | ZF: %d, CF: %d | PC -> 0x%02X",
                (op1 < REGISTER_COUNT) ? cpu_after->registers[op1] : 0,
                cpu_after->zero_flag, cpu_after->carry_flag,
                cpu_after->pc
            );
            break;

        case OP_JMP:
            snprintf(
                exec_desc, desc_size,
                "Lompatan berhasil | PC: 0x%02X -> 0x%02X",
                cpu_before->pc, cpu_after->pc
            );
            break;

        case OP_JZ:
            if (cpu_before->zero_flag)
            {
                snprintf(exec_desc, desc_size, "Lompat diambil (ZF=1) | PC: 0x%02X -> 0x%02X", cpu_before->pc, cpu_after->pc);
            }
            else
            {
                snprintf(exec_desc, desc_size, "Lompat diabaikan (ZF=0) | PC lanjut ke 0x%02X", cpu_after->pc);
            }
            break;

        case OP_JNZ:
            if (!cpu_before->zero_flag)
            {
                snprintf(exec_desc, desc_size, "Lompat diambil (ZF=0) | PC: 0x%02X -> 0x%02X", cpu_before->pc, cpu_after->pc);
            }
            else
            {
                snprintf(exec_desc, desc_size, "Lompat diabaikan (ZF=1) | PC lanjut ke 0x%02X", cpu_after->pc);
            }
            break;

        case OP_OUT:
            {
                unsigned int val = (op1 == OUT_MODE_REGISTER && op2 < REGISTER_COUNT)
                    ? cpu_before->registers[op2]
                    : op2;
                snprintf(exec_desc, desc_size, "Output tercetak: %u | PC lanjut ke 0x%02X", val, cpu_after->pc);
            }
            break;

        case OP_HLT:
            snprintf(exec_desc, desc_size, "CPU HALTED | Eksekusi instruksi selesai");
            break;

        default:
            snprintf(exec_desc, desc_size, "Instruksi selesai | PC: 0x%02X", cpu_after->pc);
            break;
    }
}

// Menjalankan satu fase maju dalam siklus Fetch-Decode-Execute
static void step_cycle_phase(
    CPU *cpu,
    InstructionCycleState *cycle,
    uint16_t *last_pc,
    bool *has_executed,
    uint8_t *last_out_val,
    bool *has_output
)
{
    if (cpu->halted)
    {
        return;
    }

    if (cycle->phase == PHASE_IDLE || cycle->phase == PHASE_EXECUTE)
    {
        // === FASE 1: FETCH ===
        cycle->fetch_pc = cpu->pc;
        cycle->opcode = cpu->memory[cpu->pc];
        cycle->operand1 = cpu->memory[cpu->pc + 1];
        cycle->operand2 = cpu->memory[cpu->pc + 2];

        disassemble_instruction(cpu->memory, cpu->pc, cycle->disasm_text, sizeof(cycle->disasm_text));
        cycle->modified_reg = -1;
        cycle->source_reg = -1;
        cycle->phase = PHASE_FETCH;
    }
    else if (cycle->phase == PHASE_FETCH)
    {
        // === FASE 2: DECODE ===
        decode_instruction(
            cycle->opcode,
            cycle->operand1,
            cycle->operand2,
            cpu,
            cycle
        );
        cycle->phase = PHASE_DECODE;
    }
    else if (cycle->phase == PHASE_DECODE)
    {
        // === FASE 3: EXECUTE ===
        CPU cpu_before = *cpu;

        // Tangkap nilai OUT jika instruksi adalah OUT
        if (cycle->opcode == OP_OUT)
        {
            if (cycle->operand1 == OUT_MODE_REGISTER && cycle->operand2 < REGISTER_COUNT)
            {
                *last_out_val = cpu->registers[cycle->operand2];
                *has_output = true;
            }
            else if (cycle->operand1 == OUT_MODE_IMMEDIATE)
            {
                *last_out_val = cycle->operand2;
                *has_output = true;
            }
        }

        *last_pc = cpu->pc;
        *has_executed = true;

        cpu_step(cpu);

        describe_execution(
            cycle->opcode,
            cycle->operand1,
            cycle->operand2,
            &cpu_before,
            cpu,
            cycle->execute_desc,
            sizeof(cycle->execute_desc)
        );

        cycle->phase = PHASE_EXECUTE;
    }
}

// Helper untuk merakit dan memuat file ASM secara aman ke CPU
static bool do_assemble_and_load(
    CPU *cpu,
    const char *path,
    InstructionCycleState *cycle,
    char *status_msg,
    size_t status_size,
    Color *status_color
)
{
    if (assemble_and_load(cpu, path))
    {
        reset_cycle(cycle);
        snprintf(status_msg, status_size, "Loaded: %s", path);
        *status_color = GREEN;
        return true;
    }
    else
    {
        snprintf(status_msg, status_size, "Error assembling: %s", path);
        *status_color = RED;
        return false;
    }
}

// Draw a button and return true when clicked
static bool draw_button(
    Rectangle bounds,
    const char *label,
    Color base_color
)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, bounds);

    Color color = hovered
        ? (Color){65, 85, 110, 255}
        : base_color;

    DrawRectangleRec(bounds, color);
    DrawRectangleLinesEx(bounds, 1, COLOR_BORDER);

    int text_width = MeasureText(label, 16);

    DrawText(
        label,
        (int)(bounds.x + (bounds.width - text_width) / 2),
        (int)(bounds.y + 12),
        16,
        COLOR_TEXT
    );

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

// Helper menggambar garis bus beranimasi dengan partikel data yang bergerak
static void draw_animated_bus(
    Vector2 start,
    Vector2 end,
    Color base_color,
    Color active_color,
    bool is_active,
    float time
)
{
    DrawLineEx(start, end, is_active ? 3.0f : 2.0f, is_active ? active_color : base_color);

    if (is_active)
    {
        float dx = end.x - start.x;
        float dy = end.y - start.y;
        float length = sqrtf(dx * dx + dy * dy);

        if (length > 0.0f)
        {
            float speed = 75.0f; // Kecepatan gerak partikel bus
            float offset = fmodf(time * speed, 24.0f);

            for (float d = offset; d < length; d += 24.0f)
            {
                float t = d / length;
                Vector2 pos = (Vector2){
                    start.x + t * dx,
                    start.y + t * dy
                };
                DrawCircleV(pos, 3.5f, WHITE);
            }
        }
    }
}

// Draw CPU registers and flags with highlight on modified register
static void draw_register_panel(
    const CPU *cpu,
    uint8_t last_out_val,
    bool has_output,
    int modified_reg,
    CyclePhase current_phase
)
{
    DrawRectangle(40, 160, 380, 290, COLOR_PANEL);
    DrawRectangleLines(40, 160, 380, 290, COLOR_BORDER);

    DrawText("CPU REGISTERS", 60, 175, 19, COLOR_ACCENT);

    for (int i = 0; i < REGISTER_COUNT; i++)
    {
        int column = i % 2;
        int row = i / 2;

        int x = 65 + column * 165;
        int y = 210 + row * 62;

        bool is_modified = (current_phase == PHASE_EXECUTE && modified_reg == i);

        if (is_modified)
        {
            DrawRectangle(x - 5, y - 2, 140, 52, (Color){25, 75, 45, 255});
            DrawRectangleLines(x - 5, y - 2, 140, 52, COLOR_EXECUTE);
        }

        DrawText(
            TextFormat("R%d", i),
            x, y, 17, is_modified ? COLOR_EXECUTE : COLOR_MUTED
        );

        DrawText(
            TextFormat("%03u", (unsigned int)cpu->registers[i]),
            x, y + 20, 23, is_modified ? GREEN : COLOR_TEXT
        );
    }

    DrawLine(60, 345, 400, 345, COLOR_BORDER);

    bool pc_highlight = (current_phase == PHASE_FETCH);

    DrawText(
        TextFormat("PC: 0x%04X", (unsigned int)cpu->pc),
        65, 360, 17, pc_highlight ? COLOR_FETCH : COLOR_TEXT
    );

    DrawText(
        TextFormat("SP: 0x%02X", (unsigned int)cpu->sp),
        210, 360, 17, COLOR_TEXT
    );

    if (has_output)
    {
        DrawText(
            TextFormat("OUT: %u", (unsigned int)last_out_val),
            310, 360, 17, COLOR_ACCENT
        );
    }
    else
    {
        DrawText("OUT: --", 310, 360, 17, COLOR_MUTED);
    }

    DrawText(
        TextFormat("ZF: %d", cpu->zero_flag),
        65, 400, 17, COLOR_TEXT
    );

    DrawText(
        TextFormat("CF: %d", cpu->carry_flag),
        170, 400, 17, COLOR_TEXT
    );

    const char *status = cpu->halted ? "HALTED" : "READY";

    DrawText(
        status,
        270, 400, 17,
        cpu->halted ? RED : GREEN
    );
}

// Draw the dynamic Program Disassembly panel
static void draw_program_panel(
    const CPU *cpu,
    const InstructionCycleState *cycle,
    uint16_t last_pc,
    bool has_executed
)
{
    DrawRectangle(40, 465, 380, 250, COLOR_PANEL);
    DrawRectangleLines(40, 465, 380, 250, COLOR_BORDER);

    DrawText("PROGRAM DISASSEMBLY", 60, 480, 18, COLOR_ACCENT);

    // Kumpulkan alamat instruksi yang terpasang di memori
    uint16_t addresses[64];
    int count = 0;

    for (uint16_t addr = 0; addr + INSTRUCTION_SIZE <= MEMORY_SIZE && count < 64; addr += INSTRUCTION_SIZE)
    {
        addresses[count++] = addr;
        if (cpu->memory[addr] == OP_HLT)
        {
            break;
        }
    }

    if (count == 0)
    {
        addresses[0] = 0;
        count = 1;
    }

    uint16_t target_pc = (cycle->phase != PHASE_IDLE) ? cycle->fetch_pc : cpu->pc;

    int current_idx = 0;
    for (int i = 0; i < count; i++)
    {
        if (addresses[i] == target_pc)
        {
            current_idx = i;
            break;
        }
    }

    // Auto-scroll agar instruksi aktif selalu terlihat di panel
    const int max_visible = 7;
    int start_idx = 0;
    if (current_idx >= max_visible - 2)
    {
        start_idx = current_idx - (max_visible - 3);
    }
    if (start_idx + max_visible > count && count >= max_visible)
    {
        start_idx = count - max_visible;
    }
    if (start_idx < 0)
    {
        start_idx = 0;
    }

    for (int i = start_idx; i < count && (i - start_idx) < max_visible; i++)
    {
        uint16_t addr = addresses[i];
        int y = 510 + (i - start_idx) * 26;

        bool active = (!cpu->halted && target_pc == addr) ||
                      (cpu->halted && has_executed && addr == last_pc);

        Color line_border = COLOR_ACCENT;
        Color line_bg = COLOR_HIGHLIGHT;
        Color text_color = SKYBLUE;

        if (active)
        {
            if (cycle->phase == PHASE_FETCH)
            {
                line_bg = (Color){70, 55, 15, 255};
                line_border = COLOR_FETCH;
                text_color = COLOR_FETCH;
            }
            else if (cycle->phase == PHASE_DECODE)
            {
                line_bg = (Color){20, 60, 85, 255};
                line_border = COLOR_DECODE;
                text_color = COLOR_DECODE;
            }
            else if (cycle->phase == PHASE_EXECUTE)
            {
                line_bg = (Color){20, 70, 40, 255};
                line_border = COLOR_EXECUTE;
                text_color = COLOR_EXECUTE;
            }

            DrawRectangle(50, y - 2, 360, 24, line_bg);
            DrawRectangleLines(50, y - 2, 360, 24, line_border);
        }
        else
        {
            text_color = COLOR_TEXT;
        }

        char disasm[64];
        disassemble_instruction(cpu->memory, addr, disasm, sizeof(disasm));

        DrawText(
            disasm,
            60, y + 2, 16,
            text_color
        );
    }
}

// Draw the Instruction Cycle (Fetch - Decode - Execute) visualizer
static void draw_cycle_panel(
    const CPU *cpu,
    const InstructionCycleState *cycle
)
{
    DrawRectangle(440, 160, 800, 145, COLOR_PANEL);
    DrawRectangleLines(440, 160, 800, 145, COLOR_BORDER);

    DrawText("INSTRUCTION CYCLE", 460, 172, 18, COLOR_ACCENT);

    const char *phase_title = "IDLE";
    Color phase_color = COLOR_MUTED;

    if (cycle->phase == PHASE_FETCH)
    {
        phase_title = "1. FETCH";
        phase_color = COLOR_FETCH;
    }
    else if (cycle->phase == PHASE_DECODE)
    {
        phase_title = "2. DECODE";
        phase_color = COLOR_DECODE;
    }
    else if (cycle->phase == PHASE_EXECUTE)
    {
        phase_title = "3. EXECUTE";
        phase_color = COLOR_EXECUTE;
    }

    DrawText(
        TextFormat("PHASE: %s", phase_title),
        1070, 174, 15, phase_color
    );

    // Card 1: FETCH
    Rectangle card_fetch = (Rectangle){455, 198, 230, 60};
    bool fetch_active = (cycle->phase == PHASE_FETCH);
    bool fetch_done = (cycle->phase > PHASE_FETCH);

    Color fetch_bg = fetch_active
        ? (Color){60, 50, 18, 255}
        : (fetch_done ? (Color){30, 42, 38, 255} : (Color){25, 30, 40, 255});
    Color fetch_border = fetch_active
        ? COLOR_FETCH
        : (fetch_done ? (Color){80, 140, 100, 255} : COLOR_BORDER);

    DrawRectangleRec(card_fetch, fetch_bg);
    DrawRectangleLinesEx(card_fetch, fetch_active ? 2 : 1, fetch_border);

    DrawText("1. FETCH", 470, 204, 15, fetch_active ? COLOR_FETCH : (fetch_done ? GREEN : COLOR_TEXT));

    if (cycle->phase != PHASE_IDLE)
    {
        DrawText(
            TextFormat("PC: 0x%02X | Bytes: [%02X %02X %02X]", cycle->fetch_pc, cycle->opcode, cycle->operand1, cycle->operand2),
            470, 228, 13, COLOR_TEXT
        );
    }
    else
    {
        DrawText(TextFormat("PC: 0x%02X | Siap baca byte", cpu->pc), 470, 228, 13, COLOR_MUTED);
    }

    // Panah 1
    DrawText("->", 698, 218, 20, (cycle->phase >= PHASE_DECODE) ? COLOR_ACCENT : COLOR_MUTED);

    // Card 2: DECODE
    Rectangle card_decode = (Rectangle){725, 198, 230, 60};
    bool decode_active = (cycle->phase == PHASE_DECODE);
    bool decode_done = (cycle->phase > PHASE_DECODE);

    Color decode_bg = decode_active
        ? (Color){18, 48, 68, 255}
        : (decode_done ? (Color){30, 42, 38, 255} : (Color){25, 30, 40, 255});
    Color decode_border = decode_active
        ? COLOR_DECODE
        : (decode_done ? (Color){80, 140, 100, 255} : COLOR_BORDER);

    DrawRectangleRec(card_decode, decode_bg);
    DrawRectangleLinesEx(card_decode, decode_active ? 2 : 1, decode_border);

    DrawText("2. DECODE", 740, 204, 15, decode_active ? COLOR_DECODE : (decode_done ? GREEN : COLOR_TEXT));

    if (cycle->phase >= PHASE_DECODE)
    {
        DrawText(TextFormat("Instr: %s (0x%02X)", cycle->disasm_text, cycle->opcode), 740, 228, 13, COLOR_TEXT);
    }
    else
    {
        DrawText("Menunggu Fetch...", 740, 228, 13, COLOR_MUTED);
    }

    // Panah 2
    DrawText("->", 968, 218, 20, (cycle->phase >= PHASE_EXECUTE) ? COLOR_ACCENT : COLOR_MUTED);

    // Card 3: EXECUTE
    Rectangle card_execute = (Rectangle){995, 198, 230, 60};
    bool exec_active = (cycle->phase == PHASE_EXECUTE);

    Color exec_bg = exec_active
        ? (Color){20, 60, 36, 255}
        : (Color){25, 30, 40, 255};
    Color exec_border = exec_active
        ? COLOR_EXECUTE
        : COLOR_BORDER;

    DrawRectangleRec(card_execute, exec_bg);
    DrawRectangleLinesEx(card_execute, exec_active ? 2 : 1, exec_border);

    DrawText("3. EXECUTE", 1010, 204, 15, exec_active ? COLOR_EXECUTE : COLOR_TEXT);

    if (cycle->phase == PHASE_EXECUTE)
    {
        DrawText(
            TextFormat("Hasil: %s", cpu->halted ? "HALTED" : "COMPLETED"),
            1010, 228, 13, cpu->halted ? RED : GREEN
        );
    }
    else
    {
        DrawText("Menunggu Decode...", 1010, 228, 13, COLOR_MUTED);
    }

    // Banner penjelasan di bawah kartu
    Rectangle banner_rect = (Rectangle){455, 266, 770, 30};
    DrawRectangleRec(banner_rect, (Color){24, 28, 38, 255});
    DrawRectangleLines(banner_rect.x, banner_rect.y, banner_rect.width, banner_rect.height, COLOR_BORDER);

    if (cycle->phase == PHASE_IDLE)
    {
        DrawText("Siap. Tekan STEP untuk memulai siklus instruksi.", 465, 273, 13, COLOR_MUTED);
    }
    else if (cycle->phase == PHASE_FETCH)
    {
        DrawText(
            TextFormat("[FETCH] Membaca instruksi '%s' dari alamat 0x%02X di memori.", cycle->disasm_text, cycle->fetch_pc),
            465, 273, 13, COLOR_FETCH
        );
    }
    else if (cycle->phase == PHASE_DECODE)
    {
        DrawText(
            TextFormat("[DECODE] %s", cycle->decode_desc),
            465, 273, 13, COLOR_DECODE
        );
    }
    else if (cycle->phase == PHASE_EXECUTE)
    {
        DrawText(
            TextFormat("[EXECUTE] %s", cycle->execute_desc),
            465, 273, 13, COLOR_EXECUTE
        );
    }
}

// Draw the Animated CPU Datapath Visualization (GUI-7)
static void draw_datapath_panel(
    const CPU *cpu,
    const InstructionCycleState *cycle,
    uint8_t last_out_val,
    bool has_output,
    float time
)
{
    DrawRectangle(440, 350, 800, 365, COLOR_PANEL);
    DrawRectangleLines(440, 350, 800, 365, COLOR_BORDER);

    // Titik sambungan komponen datapath
    // Row 1: PC -> Memory -> Decoder -> Control Unit
    Rectangle rect_pc = (Rectangle){460, 370, 100, 52};
    Rectangle rect_mem = (Rectangle){600, 370, 130, 52};
    Rectangle rect_dec = (Rectangle){770, 370, 160, 52};
    Rectangle rect_ctrl = (Rectangle){970, 370, 250, 52};

    // Row 2: Register File, ALU, Flags, Output
    Rectangle rect_rf = (Rectangle){460, 455, 240, 155};
    Rectangle rect_alu = (Rectangle){740, 455, 200, 155};
    Rectangle rect_flags = (Rectangle){980, 455, 240, 70};
    Rectangle rect_io = (Rectangle){980, 540, 240, 70};

    // Sinyal aktif bus
    bool bus_pc_mem = (cycle->phase == PHASE_FETCH);
    bool bus_mem_dec = (cycle->phase == PHASE_FETCH || cycle->phase == PHASE_DECODE);
    bool bus_dec_ctrl = (cycle->phase == PHASE_DECODE);
    bool bus_rf_alu = (cycle->phase == PHASE_EXECUTE && cycle->alu_active && !cycle->ctrl.alu_src_imm);
    bool bus_alu_rf = (cycle->phase == PHASE_EXECUTE && cycle->ctrl.reg_write);
    bool bus_ctrl_branch = (cycle->phase == PHASE_EXECUTE && cycle->ctrl.branch && cycle->ctrl.branch_taken);
    bool bus_rf_io = (cycle->phase == PHASE_EXECUTE && cycle->ctrl.is_out);

    // Gambar Garis Bus Data & Kontrol Beranimasi
    // 1. PC -> Memory Bus (Address Bus)
    draw_animated_bus(
        (Vector2){rect_pc.x + rect_pc.width, rect_pc.y + 26},
        (Vector2){rect_mem.x, rect_mem.y + 26},
        COLOR_BORDER, COLOR_FETCH, bus_pc_mem, time
    );

    // 2. Memory -> Instruction Decoder Bus (Data Bus)
    draw_animated_bus(
        (Vector2){rect_mem.x + rect_mem.width, rect_mem.y + 26},
        (Vector2){rect_dec.x, rect_dec.y + 26},
        COLOR_BORDER, COLOR_FETCH, bus_mem_dec, time
    );

    // 3. Decoder -> Control Unit Bus
    draw_animated_bus(
        (Vector2){rect_dec.x + rect_dec.width, rect_dec.y + 26},
        (Vector2){rect_ctrl.x, rect_ctrl.y + 26},
        COLOR_BORDER, COLOR_DECODE, bus_dec_ctrl, time
    );

    // 4. Register File -> ALU (Operand Bus)
    draw_animated_bus(
        (Vector2){rect_rf.x + rect_rf.width, rect_rf.y + 60},
        (Vector2){rect_alu.x, rect_alu.y + 60},
        COLOR_BORDER, COLOR_DECODE, bus_rf_alu, time
    );

    // 5. ALU Result -> Register File (Writeback Bus)
    draw_animated_bus(
        (Vector2){rect_alu.x, rect_alu.y + 110},
        (Vector2){rect_rf.x + rect_rf.width, rect_rf.y + 110},
        COLOR_BORDER, COLOR_EXECUTE, bus_alu_rf, time
    );

    // 6. ALU -> Flags Bus
    draw_animated_bus(
        (Vector2){rect_alu.x + rect_alu.width, rect_alu.y + 40},
        (Vector2){rect_flags.x, rect_flags.y + 35},
        COLOR_BORDER, COLOR_EXECUTE, (cycle->phase == PHASE_EXECUTE && cycle->alu_active), time
    );

    // 7. Register / Immediate -> I/O Output Bus
    draw_animated_bus(
        (Vector2){rect_alu.x + rect_alu.width, rect_alu.y + 110},
        (Vector2){rect_io.x, rect_io.y + 35},
        COLOR_BORDER, COLOR_ACCENT, bus_rf_io, time
    );

    // 8. Branch Feedback Bus (Control -> PC)
    if (bus_ctrl_branch)
    {
        Vector2 p1 = (Vector2){rect_ctrl.x + 40, rect_ctrl.y};
        Vector2 p2 = (Vector2){rect_ctrl.x + 40, rect_ctrl.y - 12};
        Vector2 p3 = (Vector2){rect_pc.x + 50, rect_pc.y - 12};
        Vector2 p4 = (Vector2){rect_pc.x + 50, rect_pc.y};

        DrawLineEx(p1, p2, 2.5f, COLOR_FETCH);
        DrawLineEx(p2, p3, 2.5f, COLOR_FETCH);
        DrawLineEx(p3, p4, 2.5f, COLOR_FETCH);
        DrawCircleV(p4, 4.0f, COLOR_FETCH);
    }

    // ==========================================
    // GAMBAR BLOK-BLOK KOMPONEN HARDWARE DIO-8
    // ==========================================

    // 1. Program Counter (PC)
    bool pc_act = (cycle->phase == PHASE_FETCH);
    DrawRectangleRec(rect_pc, pc_act ? (Color){55, 45, 18, 255} : (Color){26, 32, 42, 255});
    DrawRectangleLinesEx(rect_pc, pc_act ? 2 : 1, pc_act ? COLOR_FETCH : COLOR_BORDER);
    DrawText("PROG COUNTER", (int)rect_pc.x + 6, (int)rect_pc.y + 8, 11, pc_act ? COLOR_FETCH : COLOR_MUTED);
    DrawText(TextFormat("PC: 0x%02X", (cycle->phase != PHASE_IDLE) ? cycle->fetch_pc : cpu->pc), (int)rect_pc.x + 10, (int)rect_pc.y + 25, 17, COLOR_TEXT);

    // 2. Instruction Memory (RAM 256B)
    bool mem_act = (cycle->phase == PHASE_FETCH);
    DrawRectangleRec(rect_mem, mem_act ? (Color){55, 45, 18, 255} : (Color){26, 32, 42, 255});
    DrawRectangleLinesEx(rect_mem, mem_act ? 2 : 1, mem_act ? COLOR_FETCH : COLOR_BORDER);
    DrawText("INSTR MEMORY", (int)rect_mem.x + 8, (int)rect_mem.y + 8, 11, mem_act ? COLOR_FETCH : COLOR_MUTED);
    DrawText(TextFormat("[%02X %02X %02X]", cycle->opcode, cycle->operand1, cycle->operand2), (int)rect_mem.x + 8, (int)rect_mem.y + 26, 14, COLOR_TEXT);

    // 3. Instruction Decoder
    bool dec_act = (cycle->phase == PHASE_DECODE);
    DrawRectangleRec(rect_dec, dec_act ? (Color){18, 48, 68, 255} : (Color){26, 32, 42, 255});
    DrawRectangleLinesEx(rect_dec, dec_act ? 2 : 1, dec_act ? COLOR_DECODE : COLOR_BORDER);
    DrawText("INSTRUCTION DECODER", (int)rect_dec.x + 8, (int)rect_dec.y + 8, 11, dec_act ? COLOR_DECODE : COLOR_MUTED);
    DrawText(TextFormat("Op: 0x%02X (%s)", cycle->opcode, cycle->disasm_text), (int)rect_dec.x + 8, (int)rect_dec.y + 26, 13, COLOR_TEXT);

    // 4. Control Unit
    bool ctrl_act = (cycle->phase == PHASE_DECODE || cycle->phase == PHASE_EXECUTE);
    DrawRectangleRec(rect_ctrl, ctrl_act ? (Color){28, 42, 60, 255} : (Color){26, 32, 42, 255});
    DrawRectangleLinesEx(rect_ctrl, ctrl_act ? 2 : 1, ctrl_act ? COLOR_ACCENT : COLOR_BORDER);
    DrawText("CONTROL UNIT", (int)rect_ctrl.x + 10, (int)rect_ctrl.y + 8, 11, ctrl_act ? COLOR_ACCENT : COLOR_MUTED);
    DrawText(
        TextFormat("ALUOp:%s | RegW:%d | Br:%d", cycle->ctrl.alu_op, cycle->ctrl.reg_write ? 1 : 0, cycle->ctrl.branch ? 1 : 0),
        (int)rect_ctrl.x + 10, (int)rect_ctrl.y + 26, 12, COLOR_TEXT
    );

    // 5. Register File (R0 - R3)
    DrawRectangleRec(rect_rf, (Color){26, 32, 42, 255});
    DrawRectangleLinesEx(rect_rf, 1, COLOR_BORDER);
    DrawText("REGISTER FILE (R0 - R3)", (int)rect_rf.x + 12, (int)rect_rf.y + 10, 13, COLOR_ACCENT);

    for (int i = 0; i < REGISTER_COUNT; i++)
    {
        int rx = (int)rect_rf.x + 12 + (i % 2) * 110;
        int ry = (int)rect_rf.y + 35 + (i / 2) * 52;

        bool is_dest = (cycle->phase == PHASE_EXECUTE && cycle->modified_reg == i);
        bool is_src = (cycle->phase >= PHASE_DECODE && cycle->source_reg == i);

        Color reg_bg = (Color){34, 42, 54, 255};
        Color reg_bdr = COLOR_BORDER;

        if (is_dest)
        {
            reg_bg = (Color){25, 75, 45, 255};
            reg_bdr = COLOR_EXECUTE;
        }
        else if (is_src)
        {
            reg_bg = (Color){18, 50, 70, 255};
            reg_bdr = COLOR_DECODE;
        }

        DrawRectangle(rx, ry, 100, 44, reg_bg);
        DrawRectangleLines(rx, ry, 100, 44, reg_bdr);

        DrawText(TextFormat("R%d", i), rx + 8, ry + 6, 13, is_dest ? COLOR_EXECUTE : (is_src ? COLOR_DECODE : COLOR_MUTED));
        DrawText(TextFormat("%03u", (unsigned int)cpu->registers[i]), rx + 38, ry + 16, 18, is_dest ? GREEN : COLOR_TEXT);
    }

    // 6. Arithmetic Logic Unit (ALU)
    bool alu_act = (cycle->phase == PHASE_EXECUTE && cycle->alu_active);
    DrawRectangleRec(rect_alu, alu_act ? (Color){20, 60, 36, 255} : (Color){26, 32, 42, 255});
    DrawRectangleLinesEx(rect_alu, alu_act ? 2 : 1, alu_act ? COLOR_EXECUTE : COLOR_BORDER);

    DrawText("ARITHMETIC LOGIC UNIT", (int)rect_alu.x + 10, (int)rect_alu.y + 10, 13, alu_act ? COLOR_EXECUTE : COLOR_ACCENT);

    DrawText(TextFormat("Input A : %03u", cycle->alu_in_a), (int)rect_alu.x + 14, (int)rect_alu.y + 40, 14, COLOR_TEXT);
    DrawText(TextFormat("Input B : %03u", cycle->alu_in_b), (int)rect_alu.x + 14, (int)rect_alu.y + 64, 14, COLOR_TEXT);
    DrawText(TextFormat("OP      : [%s]", cycle->ctrl.alu_op), (int)rect_alu.x + 14, (int)rect_alu.y + 88, 14, alu_act ? COLOR_EXECUTE : COLOR_MUTED);

    DrawLine((int)rect_alu.x + 12, (int)rect_alu.y + 112, (int)rect_alu.x + 185, (int)rect_alu.y + 112, COLOR_BORDER);
    DrawText(TextFormat("RESULT  : %03u", cycle->alu_result), (int)rect_alu.x + 14, (int)rect_alu.y + 122, 17, alu_act ? GREEN : COLOR_MUTED);

    // 7. Status Flags (ZF / CF)
    DrawRectangleRec(rect_flags, (Color){26, 32, 42, 255});
    DrawRectangleLinesEx(rect_flags, 1, COLOR_BORDER);
    DrawText("STATUS FLAGS", (int)rect_flags.x + 10, (int)rect_flags.y + 10, 12, COLOR_ACCENT);

    DrawText(TextFormat("Zero Flag  (ZF) : %d", cpu->zero_flag), (int)rect_flags.x + 12, (int)rect_flags.y + 28, 14, cpu->zero_flag ? COLOR_FETCH : COLOR_TEXT);
    DrawText(TextFormat("Carry Flag (CF) : %d", cpu->carry_flag), (int)rect_flags.x + 12, (int)rect_flags.y + 46, 14, cpu->carry_flag ? COLOR_FETCH : COLOR_TEXT);

    // 8. Output / Peripherals
    DrawRectangleRec(rect_io, (Color){26, 32, 42, 255});
    DrawRectangleLinesEx(rect_io, 1, (cycle->ctrl.is_out && cycle->phase == PHASE_EXECUTE) ? COLOR_ACCENT : COLOR_BORDER);
    DrawText("I/O OUTPUT (OUT)", (int)rect_io.x + 10, (int)rect_io.y + 10, 12, COLOR_ACCENT);

    if (has_output)
    {
        DrawText(TextFormat("LATEST : %u", (unsigned int)last_out_val), (int)rect_io.x + 12, (int)rect_io.y + 34, 20, COLOR_ACCENT);
    }
    else
    {
        DrawText("LATEST : --", (int)rect_io.x + 12, (int)rect_io.y + 34, 18, COLOR_MUTED);
    }

    // Legend Bus di bawah panel
    DrawText("Bus Legend: [Gold: Address/Fetch] | [Cyan: Instruction/Decode] | [Green: ALU & Writeback Data]", 460, 690, 12, COLOR_MUTED);
}

// Draw all 256 bytes of memory with phase-aware highlighting
static void draw_memory_panel(
    const CPU *cpu,
    const InstructionCycleState *cycle,
    uint16_t last_pc,
    bool has_executed
)
{
    DrawRectangle(440, 350, 800, 365, COLOR_PANEL);
    DrawRectangleLines(440, 350, 800, 365, COLOR_BORDER);

    DrawText("MEMORY VIEWER", 460, 360, 18, COLOR_ACCENT);
    DrawText("256 BYTES | HEXADECIMAL", 1040, 364, 12, COLOR_MUTED);

    for (int row = 0; row < 16; row++)
    {
        int y = 390 + row * 19;

        DrawText(
            TextFormat("%04X:", row * 16),
            460, y + 2, 13, COLOR_MUTED
        );

        for (int column = 0; column < 16; column++)
        {
            int address = row * 16 + column;
            int x = 510 + column * 45;

            bool is_current_cycle = (cycle->phase != PHASE_IDLE &&
                address >= cycle->fetch_pc &&
                address < cycle->fetch_pc + INSTRUCTION_SIZE);

            bool is_prev_instruction = (cpu->halted && has_executed &&
                address >= last_pc &&
                address < last_pc + INSTRUCTION_SIZE);

            Color cell_color = (Color){40, 48, 62, 255};
            Color border_color = COLOR_BORDER;

            if (is_current_cycle)
            {
                if (cycle->phase == PHASE_FETCH)
                {
                    cell_color = (Color){90, 75, 20, 255};
                    border_color = COLOR_FETCH;
                }
                else if (cycle->phase == PHASE_DECODE)
                {
                    cell_color = (Color){25, 75, 100, 255};
                    border_color = COLOR_DECODE;
                }
                else if (cycle->phase == PHASE_EXECUTE)
                {
                    cell_color = (Color){25, 85, 45, 255};
                    border_color = COLOR_EXECUTE;
                }
            }
            else if (is_prev_instruction)
            {
                cell_color = COLOR_HIGHLIGHT;
            }

            DrawRectangle(x, y, 38, 17, cell_color);
            DrawRectangleLines(x, y, 38, 17, border_color);

            DrawText(
                TextFormat("%02X", cpu->memory[address]),
                x + 8, y + 2, 13, COLOR_TEXT
            );
        }
    }
}

int main(void)
{
    InitWindow(
        SCREEN_WIDTH,
        SCREEN_HEIGHT,
        "Dio-8 | 8-bit CPU Simulator, Datapath & Cycle Visualizer"
    );

    SetTargetFPS(60);

    CPU cpu;
    cpu_init(&cpu);

    InstructionCycleState cycle;
    reset_cycle(&cycle);

    char file_path[MAX_INPUT_CHARS] = "programs/countdown.asm";
    int letter_count = (int)strlen(file_path);
    bool input_box_focused = false;

    char status_msg[128] = "Ready";
    Color status_color = GREEN;

    // Load program awal
    if (!do_assemble_and_load(&cpu, file_path, &cycle, status_msg, sizeof(status_msg), &status_color))
    {
        strncpy(file_path, "countdown.asm", MAX_INPUT_CHARS);
        file_path[MAX_INPUT_CHARS - 1] = '\0';
        letter_count = (int)strlen(file_path);
        do_assemble_and_load(&cpu, file_path, &cycle, status_msg, sizeof(status_msg), &status_color);
    }

    bool run_mode = false;
    bool has_executed = false;
    bool has_output = false;
    uint8_t last_out_val = 0;

    uint16_t last_pc = 0;

    StepMode step_mode = STEP_MODE_PHASE;
    RightPanelView right_view = VIEW_DATAPATH;

    float step_timer = 0.0f;
    const float phase_interval = 0.35f;

    Rectangle file_box = (Rectangle){440, 95, 290, 42};

    while (!WindowShouldClose())
    {
        float delta_time = GetFrameTime();
        float current_time = (float)GetTime();
        Vector2 mouse = GetMousePosition();

        // Cek fokus text box input file ASM
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
        {
            input_box_focused = CheckCollisionPointRec(mouse, file_box);
        }

        // Tangani pengetikan nama file saat text box aktif
        if (input_box_focused)
        {
            int key = GetCharPressed();
            while (key > 0)
            {
                if ((key >= 32) && (key <= 126) && (letter_count < MAX_INPUT_CHARS - 1))
                {
                    file_path[letter_count] = (char)key;
                    letter_count++;
                    file_path[letter_count] = '\0';
                }
                key = GetCharPressed();
            }

            if (IsKeyPressed(KEY_BACKSPACE) && letter_count > 0)
            {
                letter_count--;
                file_path[letter_count] = '\0';
            }
        }

        BeginDrawing();

        ClearBackground(COLOR_BG);

        // Header
        DrawText(
            "DIO-8 CPU SIMULATOR",
            40, 35, 30, COLOR_TEXT
        );

        DrawText(
            "CPU DATAPATH & INSTRUCTION CYCLE VISUALIZER",
            405, 45, 15, COLOR_MUTED
        );

        // Tombol STEP
        bool step_clicked = draw_button(
            (Rectangle){40, 95, 85, 42},
            "STEP",
            COLOR_PANEL
        );

        // Tombol Toggle Mode Step (PHASE / INSTR)
        const char *mode_label = (step_mode == STEP_MODE_PHASE) ? "MODE: PHASE" : "MODE: INSTR";
        bool mode_clicked = draw_button(
            (Rectangle){135, 95, 115, 42},
            mode_label,
            (step_mode == STEP_MODE_PHASE) ? (Color){38, 55, 75, 255} : COLOR_PANEL
        );

        if (mode_clicked)
        {
            step_mode = (step_mode == STEP_MODE_PHASE) ? STEP_MODE_INSTRUCTION : STEP_MODE_PHASE;
        }

        // Tombol RUN / PAUSE
        bool run_clicked = draw_button(
            (Rectangle){260, 95, 85, 42},
            run_mode ? "PAUSE" : "RUN",
            COLOR_ACCENT
        );

        // Tombol RESET
        bool reset_clicked = draw_button(
            (Rectangle){355, 95, 75, 42},
            "RESET",
            COLOR_PANEL
        );

        // Text box input file ASM
        DrawRectangleRec(file_box, (Color){26, 32, 42, 255});
        DrawRectangleLinesEx(file_box, 1, input_box_focused ? COLOR_ACCENT : COLOR_BORDER);

        DrawText("ASM FILE PATH:", 440, 75, 13, COLOR_MUTED);
        DrawText(file_path, (int)(file_box.x + 10), (int)(file_box.y + 13), 15, COLOR_TEXT);

        if (input_box_focused && (((int)(current_time * 2.0f)) % 2 == 0))
        {
            int cursor_x = (int)(file_box.x + 10 + MeasureText(file_path, 15));
            DrawText("|", cursor_x, (int)(file_box.y + 11), 17, COLOR_ACCENT);
        }

        // Tombol ASSEMBLE
        bool assemble_clicked = draw_button(
            (Rectangle){740, 95, 105, 42},
            "ASSEMBLE",
            COLOR_PANEL
        );

        // Tombol COUNTDOWN
        bool countdown_clicked = draw_button(
            (Rectangle){855, 95, 115, 42},
            "COUNTDOWN",
            COLOR_PANEL
        );

        // Tombol HELLO
        bool hello_clicked = draw_button(
            (Rectangle){980, 95, 85, 42},
            "HELLO",
            COLOR_PANEL
        );

        // Status bar
        DrawText(status_msg, 740, 75, 13, status_color);

        // Aksi tombol COUNTDOWN
        if (countdown_clicked)
        {
            strncpy(file_path, "programs/countdown.asm", MAX_INPUT_CHARS);
            file_path[MAX_INPUT_CHARS - 1] = '\0';
            letter_count = (int)strlen(file_path);
            if (do_assemble_and_load(&cpu, file_path, &cycle, status_msg, sizeof(status_msg), &status_color))
            {
                run_mode = false;
                has_executed = false;
                last_pc = 0;
                step_timer = 0.0f;
                has_output = false;
                last_out_val = 0;
            }
        }

        // Aksi tombol HELLO
        if (hello_clicked)
        {
            strncpy(file_path, "programs/hello.asm", MAX_INPUT_CHARS);
            file_path[MAX_INPUT_CHARS - 1] = '\0';
            letter_count = (int)strlen(file_path);
            if (do_assemble_and_load(&cpu, file_path, &cycle, status_msg, sizeof(status_msg), &status_color))
            {
                run_mode = false;
                has_executed = false;
                last_pc = 0;
                step_timer = 0.0f;
                has_output = false;
                last_out_val = 0;
            }
        }

        // Aksi ASSEMBLE atau Enter di text box
        if (assemble_clicked || (input_box_focused && IsKeyPressed(KEY_ENTER)))
        {
            if (do_assemble_and_load(&cpu, file_path, &cycle, status_msg, sizeof(status_msg), &status_color))
            {
                run_mode = false;
                has_executed = false;
                last_pc = 0;
                step_timer = 0.0f;
                has_output = false;
                last_out_val = 0;
            }
        }

        // Aksi RESET
        if (reset_clicked)
        {
            do_assemble_and_load(&cpu, file_path, &cycle, status_msg, sizeof(status_msg), &status_color);
            run_mode = false;
            has_executed = false;
            last_pc = 0;
            step_timer = 0.0f;
            has_output = false;
            last_out_val = 0;
        }

        // Aksi tombol STEP
        if (step_clicked && !cpu.halted)
        {
            run_mode = false;

            if (step_mode == STEP_MODE_PHASE)
            {
                step_cycle_phase(&cpu, &cycle, &last_pc, &has_executed, &last_out_val, &has_output);
            }
            else
            {
                do {
                    step_cycle_phase(&cpu, &cycle, &last_pc, &has_executed, &last_out_val, &has_output);
                } while (cycle.phase != PHASE_EXECUTE && !cpu.halted);
            }
        }

        // Toggle Continuous RUN
        if (run_clicked && !cpu.halted)
        {
            run_mode = !run_mode;
            step_timer = 0.0f;
        }

        // Automatic continuous execution
        if (run_mode && !cpu.halted)
        {
            step_timer += delta_time;

            if (step_timer >= phase_interval)
            {
                step_timer = 0.0f;
                step_cycle_phase(&cpu, &cycle, &last_pc, &has_executed, &last_out_val, &has_output);
            }
        }

        if (cpu.halted)
        {
            run_mode = false;
        }

        // ==========================================
        // RENDER PANEL-PANEL UTAMA
        // ==========================================

        // Sisi Kiri
        draw_register_panel(&cpu, last_out_val, has_output, cycle.modified_reg, cycle.phase);
        draw_program_panel(&cpu, &cycle, last_pc, has_executed);

        // Sisi Kanan: Panel Siklus Instruksi
        draw_cycle_panel(&cpu, &cycle);

        // Sisi Kanan: Tab Switcher (CPU DATAPATH vs MEMORY VIEWER)
        Rectangle tab_datapath = (Rectangle){440, 315, 175, 28};
        Rectangle tab_memory = (Rectangle){625, 315, 175, 28};

        if (draw_button(tab_datapath, "CPU DATAPATH", (right_view == VIEW_DATAPATH) ? (Color){45, 65, 90, 255} : COLOR_PANEL))
        {
            right_view = VIEW_DATAPATH;
        }

        if (draw_button(tab_memory, "MEMORY (HEX)", (right_view == VIEW_MEMORY) ? (Color){45, 65, 90, 255} : COLOR_PANEL))
        {
            right_view = VIEW_MEMORY;
        }

        // Tampilkan Tampilan Panel yang Aktif
        if (right_view == VIEW_DATAPATH)
        {
            draw_datapath_panel(&cpu, &cycle, last_out_val, has_output, current_time);
        }
        else
        {
            draw_memory_panel(&cpu, &cycle, last_pc, has_executed);
        }

        // Footer
        DrawText(
            "Dio-8 | Educational 8-bit CPU Simulator, Datapath & Cycle Visualizer",
            40, 735, 15, COLOR_MUTED
        );

        DrawText(
            "Controls: STEP (maju 1 fase/instruksi) | MODE (Phase/Instruction) | TAB (Datapath/Memory) | RUN/PAUSE | RESET",
            40, 760, 13, COLOR_MUTED
        );

        EndDrawing();
    }

    CloseWindow();

    return 0;
}
