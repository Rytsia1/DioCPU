# DioCPU — M0 Architecture Audit & Upgrade Baseline

**Scope:** Phase 1 — M0 (Architecture Audit)  
**Goal:** Establish an accurate baseline of the existing DioCPU codebase, document architectural gaps and coupling risks, and define the upgrade path toward M1–M7 without breaking backward compatibility.

---

## 1. Existing System Inventory

| Module | Files | Responsibility |
| :--- | :--- | :--- |
| **Core CPU** | `src/cpu.h`, `src/cpu.c` | `DioCPU` state (`gpr[16]`, `pc`, `sp`, `mar`, `flags`, `state`, `fault`, `fault_pc`, `instructions_executed`), atomic `cpu_step()`, `cpu_run()`, fault handling |
| **ALU** | `src/alu.h`, `src/alu.c` | 16-bit arithmetic (`ADD`, `SUB`, `MUL`, `MULS`, `DIV`, `DIVS`, `CMP`) and bitwise (`AND`, `OR`, `XOR`, `NOT`), `ZF`/`CF`/`SF`/`OF` flag calculation |
| **Memory** | `src/memory.h`, `src/memory.c` | Strict Harvard 256 KiB (`0x40000`) `instruction[]` and `data[]`, little-endian 16-bit data access |
| **Stack** | `src/stack.h`, `src/stack.c` | Full-descending stack in `0x30000..0x3FFFF` (`SP` init `0x3FFFE`), 16-bit push/pop, 32-bit `CALL`/`RET` return address |
| **Decoder** | `src/decoder.h`, `src/decoder.c` | Variable-length instruction fetch & decode (1–5 bytes) into `DecodedInstruction` |
| **Assembler** | `src/assembler.h`, `src/assembler.c` | Two-pass assembler with labels, hex/dec/signed immediates, line-accurate error reporting |
| **Debugger** | `src/debugger.h`, `src/debugger.c` | Linear disassembler formatting PC, raw bytes, mnemonic, and operands |
| **CLI & GUI** | `src/main.c`, `src/gui.c` | CLI runner (`diocpu.exe`) and Raylib visual simulator (`diocpu_gui.exe`) |

---

## 2. Baseline Verification

All 8 baseline test suites compile cleanly with `-Wall -Wextra -Wpedantic -Werror -std=c11` and pass:
- `tests_cpu`: 8/8
- `tests_alu`: 25/25
- `tests_memory`: 7/7
- `tests_stack`: 9/9
- `tests_decoder`: 11/11
- `tests_assembler`: 7/7
- `tests_debugger`: 4/4
- `tests_gui_integration`: 8/8

---

## 3. Audit Findings & Technical Debt

### 3.1 Documentation vs. Implementation Drift
- `readme.md` lists `JS`, `JNS`, `JO`, and `JNO` in the ISA table, but these opcodes are absent from `decoder.h`, `decoder.c`, `cpu.c`, `assembler.c`, `debugger.c`, and `gui.c`.

### 3.2 `FLAGS` Bit Masking Risk (`alu.c` vs. System Flags)
- `alu_execute()` ends with `flags &= CPU_FLAGS_DEFINED_MASK;` (`0x000F`).
- `tests_alu.c` (`test_reserved_flags_zero`) explicitly asserts that `alu_execute()` zeroes bits 4–15.
- **Action:** Keep `alu_execute()` returning only `0x000F`, and have `cpu.c` merge ALU condition bits onto `cpu->flags` while preserving system bits (`CPU_FLAG_IF`, etc.) in bits 4–15.

### 3.3 Addressing Mode & ISA Gaps
- No register-to-register `MOV` (programs like `programs/fibonacci.asm` work around this with `LOADI Rx, 0; ADD Rx, Ry`).
- No register-indirect (`[Rb]`, `[Rb + disp]`), indexed (`[Rb + Rc]`), or indirect control flow (`JMP Ra`, `CALL Ra`).
- No immediate ALU operations (`ADDI`, `SUBI`, `CMPI`) or shifts (`SHL`, `SHR`, `SAR`).
- `valid_general_data_address()` blocks all `LOAD`/`STORE` accesses to `>= 0x30000`, preventing stack-frame local access (`[SP + disp]`). Dedicated stack-relative instructions (`LOADSP`/`STORESP`) are needed so general-data protection (`test_stack_memory_protection`) stays intact while stack frames become addressable.

### 3.4 Missing Interrupt & Exception Architecture
- `raise_fault()` unconditionally transitions the CPU to `CPU_STATE_FAULTED` with no exception vector table, trap/software interrupt (`INT`), interrupt masking (`CLI`/`STI`), or return-from-interrupt (`IRET`).
- Existing data tests write to `0x00000` and `0x00100`, so the default Interrupt Vector Table (IVT) must avoid low data memory (e.g., default `IVTR = 0x2FC00`, top 1 KiB of general data space `0x2FC00..0x2FFFF`, 256 × 4-byte entries).
- Unregistered fault vectors must continue transitioning to `CPU_STATE_FAULTED` for 100% backward compatibility.

### 3.5 ISA Metadata Duplication Across 6 Modules
- Opcode names, operand counts, instruction lengths, and formats are manually duplicated across `decoder.h`, `decoder.c`, `cpu.c`, `assembler.c`, `debugger.c`, and `gui.c`.
- **Action:** Introduce a centralized ISA descriptor table in `decoder.h`/`decoder.c` (`ISAInstructionInfo`) as the single source of truth before expanding the ISA in M1 and adding datapath/micro-op models in M2–M4.

### 3.6 Atomic Execution Coupling (Prep for Phase 2: M2–M4)
- `cpu_step()` executes an entire instruction atomically, and `gui.c` simulates `FETCH`/`DECODE`/`EXECUTE` visually around `cpu_step()`.
- **Action:** Keep `cpu_step()` backward-compatible for M1 and existing tests, while structuring M1 cleanly so M2 (Datapath & Control Unit), M3 (Micro-operation Engine), and M4 (Cycle-Accurate Engine) can decompose each instruction format into explicit control signals and micro-ops.

