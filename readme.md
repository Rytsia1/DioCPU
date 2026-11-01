# DioCPU V2 — 16-Bit Architecture & Interactive Simulator

DioCPU V2 is a 16-bit Harvard architecture computer system implemented in portable C11 with an interactive, animated visual simulator built using Raylib.

---

## 1. Architectural Specifications

- **Word Size**: 16-bit data words and registers.
- **Address Bus**: 18-bit addressing space ($2^{18} = 262,144$ bytes / $256\text{ KiB}$).
- **Memory Architecture**: Strict Harvard Architecture.
  - **Instruction Memory**: $256\text{ KiB}$ dedicated program memory.
  - **Data Memory**: $256\text{ KiB}$ dedicated data memory.
  - **Byte Order**: Little-Endian (low byte at address $N$, high byte at address $N+1$).
- **Register File**:
  - 16 General-Purpose Registers: `R0` through `R15` (16-bit each).
  - Program Counter (`PC`): 18-bit instruction address pointer.
  - Stack Pointer (`SP`): 18-bit stack pointer (top of memory: `0x3FFFE`).
  - Memory Address Register (`MAR`): 18-bit bus address register.
  - Flags Register (`FLAGS`): 16-bit status register (`ZF`: Zero, `CF`: Carry, `SF`: Sign, `OF`: Overflow).
- **ALU (Arithmetic Logic Unit)**:
  - Arithmetic: `ADD`, `SUB`, `CMP`, `MUL`, `MULS`, `DIV`, `DIVS`.
  - Logic: `AND`, `OR`, `XOR`, `NOT`.
- **Stack Subsystem**:
  - Full-descending stack with 16-bit word push/pop.
  - 32-bit (4-byte) aligned return address storage for 18-bit `CALL`/`RET`.
  - Hardware boundary checks: automatic overflow and underflow protection.

---

## 2. Instruction Set Architecture (ISA)

| Category | Opcodes | Description |
| :--- | :--- | :--- |
| **System** | `NOP`, `HLT` | No operation, halt execution engine |
| **Data Transfer** | `LOADI`, `LOAD`, `STORE` | Immediate load, memory load & store |
| **Arithmetic** | `ADD`, `SUB`, `CMP`, `MUL`, `MULS`, `DIV`, `DIVS` | Unsigned & signed arithmetic operations |
| **Logic** | `AND`, `OR`, `XOR`, `NOT` | Bitwise bit manipulation |
| **Stack** | `PUSH`, `POP`, `CALL`, `RET` | LIFO stack operations and subroutine linkages |
| **Control Flow** | `JMP`, `JZ`, `JNZ`, `JC`, `JNC`, `JS`, `JNS`, `JO`, `JNO` | Unconditional and flag-conditional jumps |

---

## 3. DioCPU V2 GUI Architecture

The interactive visual simulator provides an educational, real-time window into the CPU internals with 4 swappable workspaces:

### 1. Datapath & ALU Workspace
- Animated data buses showing operand movement from Register File to ALU.
- Real-time ALU preview during `DECODE` phase before commit.
- Dynamic color-coded writeback highlights when registers update.

### 2. Memory Viewer Workspace
- Dual view: Instruction Memory and Data Memory.
- Formatted Hexadecimal and ASCII inspection with 16-bit Little-Endian evaluation.
- Jump-to-PC and Jump-to-MAR tracking with page-up / page-down navigation.

### 3. Stack Viewer Workspace
- Visual memory stack showing 16-bit words.
- Active Stack Pointer (`SP`) tracking and stack capacity usage meter.
- Live event highlights for `PUSH`, `POP`, `CALL` (storing 18-bit return address), and `RET`.

### 4. Assembly & Disassembly Workspace
- Source code editor and viewer with line counting and scrolling.
- Integrated two-pass Assembler with line-accurate error highlighting.
- Real-time Disassembly view with active `PC` highlight bar.

---

## 4. Execution Controller & Keyboard Shortcuts

| Control | Shortcut | Function |
| :--- | :---: | :--- |
| **RUN / PAUSE** | `F5` | Toggle continuous automatic execution timer ($0.35\text{s}$ per phase) |
| **RESET CPU** | `F6` | Non-destructive CPU reset (restores registers, PC, SP, MAR, FLAGS while preserving program memory) |
| **SWITCH VIEW** | `F7` | Cycle through workspaces (Datapath $\to$ Memory $\to$ Stack $\to$ Assembly) |
| **STEP PHASE** | `F8` | Advance a single instruction phase (`FETCH` $\to$ `DECODE` $\to$ `EXECUTE`) |
| **STEP INSTR** | `F10` | Execute the entire current instruction atomically until completion |
| **FOLLOW POINTER** | `Home` | Center memory/stack viewer onto active `PC`, `MAR`, or `SP` |
| **SCROLL VIEWER** | `PgUp` / `PgDn` | Navigate older/newer memory or stack pages |

---

## 5. Design Notes & Simulation Boundary

The visual animation system simulates **instruction-level phases** (`FETCH`, `DECODE`, `EXECUTE`):
1. **FETCH**: Reads opcode and operand bytes from Instruction Memory at `PC`.
2. **DECODE**: Decodes instruction type, fetches register inputs, and prepares ALU/Stack preview.
3. **EXECUTE**: Commits operations to the core execution engine (`cpu_v2_step()`), writes back results, and updates flags.

> [!NOTE]
> The GUI is designed as an instruction-level phase visualizer rather than a cycle-accurate hardware micro-operation engine. This design guarantees consistent, deterministic execution while providing maximum educational clarity.

---

## 6. Build and Run Instructions

### Prerequisites
- GCC (supporting C11: `-std=c11`)
- Raylib library (`-lraylib`)
- MSYS2 UCRT64 / MinGW-w64 on Windows

### Build Everything
```bash
make all
```

### Run DioCPU V2 CLI
```bash
# Assemble and run an assembly file
./diocpu_v2.exe programs/gui_v2_fibonacci.asm
```

### Run DioCPU V2 Interactive GUI
```bash
./diocpu_v2_gui.exe
```

### Run All Unit and Integration Tests
```bash
make test
```

---

## 7. Example Programs (`programs/`)

- [`programs/gui_v2_test.asm`](programs/gui_v2_test.asm): Default program demonstrating LOADI, STORE, LOAD, and ADD.
- [`programs/gui_v2_fibonacci.asm`](programs/gui_v2_fibonacci.asm): Computes Fibonacci sequence and writes results into Data Memory.
- [`programs/gui_v2_subroutine.asm`](programs/gui_v2_subroutine.asm): Demonstrates nested subroutines, stack push/pop, and call/ret.
- [`programs/gui_v2_math.asm`](programs/gui_v2_math.asm): Full ALU and FLAGS demonstration (Zero, Carry, Sign, Overflow).
- [`programs/gui_v2_fault_demo.asm`](programs/gui_v2_fault_demo.asm): Demonstrates Divide-by-Zero fault detection and automatic safe auto-stop.
- [`programs/gui_v2_error.asm`](programs/gui_v2_error.asm): Demonstrates assembler error line detection.

---

## 8. License

Created for educational exploration of computer architecture and assembly systems.
