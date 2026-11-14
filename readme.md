# DioCPU — 16-Bit Architecture & Interactive Simulator

DioCPU is a custom 16-bit Harvard architecture computer system implemented in portable C11 with an interactive, animated visual simulator built using Raylib.

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
  - Interrupt Vector Table Register (`IVTR`): 18-bit base pointer (default: `0x2FC00`).
  - Flags Register (`FLAGS`): 16-bit status & control register (`ZF`: Zero, `CF`: Carry, `SF`: Sign, `OF`: Overflow, `IF`: Interrupt Enable).
- **ALU (Arithmetic Logic Unit)**:
  - Arithmetic: `ADD`, `ADDI`, `SUB`, `SUBI`, `INC`, `DEC`, `CMP`, `CMPI`, `MUL`, `MULS`, `DIV`, `DIVS`.
  - Logic & Shifts: `AND`, `OR`, `XOR`, `NOT`, `SHL`, `SHR`, `SAR`, `SHLI`, `SHRI`, `SARI`.
- **Stack & Interrupt Subsystem**:
  - Full-descending stack with 16-bit word push/pop and stack-frame access (`LOADSP`, `STORESP`).
  - 32-bit (4-byte) aligned return address storage for 18-bit `CALL`/`CALLR`/`RET` and `INT`/`IRET`.
  - 256-entry Interrupt Vector Table (IVT) supporting software traps (`INT`), maskable hardware IRQs (`CLI`/`STI`), and fault trapping.
  - Hardware boundary checks: automatic overflow and underflow protection.

---

## 2. Instruction Set Architecture (ISA)

See [docs/ISA_SPEC.md](docs/ISA_SPEC.md) and [docs/ARCHITECTURE_AUDIT.md](docs/ARCHITECTURE_AUDIT.md) for full encoding and architecture details.

| Category | Opcodes | Description |
| :--- | :--- | :--- |
| **System & Interrupts** | `NOP`, `HLT`, `CLI`, `STI`, `INT`, `IRET`, `SETIVT` | Execution control, interrupt masking, traps, and IVT configuration |
| **Data Transfer & Addressing** | `LOADI`, `MOV`, `LOAD`, `STORE`, `LOADR`, `STORER`, `LOADX`, `STOREX`, `LOADSP`, `STORESP`, `LEA` | Immediate, direct `[abs18]`, register-indirect `[Rb ± disp16]`, indexed `[Rb + Rc]`, and stack-relative `[SP ± disp16]` |
| **Arithmetic** | `ADD`, `ADDI`, `SUB`, `SUBI`, `INC`, `DEC`, `CMP`, `CMPI`, `MUL`, `MULS`, `DIV`, `DIVS` | Unsigned, signed, and immediate arithmetic operations |
| **Logic & Shifts** | `AND`, `OR`, `XOR`, `NOT`, `SHL`, `SHR`, `SAR`, `SHLI`, `SHRI`, `SARI` | Bitwise logic, logical shifts, and arithmetic shifts |
| **Stack** | `PUSH`, `POP`, `CALL`, `CALLR`, `RET` | LIFO stack operations and direct/indirect subroutine linkages |
| **Control Flow** | `JMP`, `JMPR`, `JZ`, `JNZ`, `JC`, `JNC`, `JS`, `JNS`, `JO`, `JNO`, `JL`, `JGE`, `JLE`, `JG` | Direct/indirect jumps and full unsigned/signed conditional branches |

---

## 3. Interactive GUI Architecture

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
3. **EXECUTE**: Commits operations to the core execution engine (`cpu_step()`), writes back results, and updates flags.

> [!NOTE]
> The GUI is designed as an instruction-level phase visualizer rather than a cycle-accurate hardware micro-operation engine. This design guarantees consistent, deterministic execution while providing maximum educational clarity.

---

## 6. Build and Run Instructions

### Prerequisites
- GCC (supporting C11: `-std=c11`)
- Raylib library (`-lraylib`)
- MSYS2 UCRT64 / MinGW-w64 on Windows

### Build All Executables & Run Tests
```bash
make all
```

### Run DioCPU CLI
```bash
# Assemble and run an assembly program
./diocpu.exe programs/fibonacci.asm
```

### Run DioCPU Interactive GUI
```bash
./diocpu_gui.exe
```

### Run Automated Test Suites
```bash
make test
```

---

## 7. Example Programs (`programs/`)

- [`programs/test.asm`](programs/test.asm): Default program demonstrating LOADI, STORE, LOAD, and ADD.
- [`programs/fibonacci.asm`](programs/fibonacci.asm): Computes Fibonacci sequence and writes results into Data Memory.
- [`programs/subroutine.asm`](programs/subroutine.asm): Demonstrates nested subroutines, stack push/pop, and call/ret.
- [`programs/math.asm`](programs/math.asm): Full ALU and FLAGS demonstration (Zero, Carry, Sign, Overflow).
- [`programs/fault_demo.asm`](programs/fault_demo.asm): Demonstrates Divide-by-Zero fault detection and automatic safe auto-stop.
- [`programs/error_demo.asm`](programs/error_demo.asm): Demonstrates assembler error line detection.

---

## 8. License

Created for educational exploration of computer architecture and assembly systems.
