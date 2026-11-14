# DioCPU Instruction Set Architecture (ISA) Specification — M1

## 1. Architectural State

- **Word Size:** 16-bit (`uint16_t`)
- **Address Bus:** 18-bit (`0x00000` – `0x3FFFF`, 256 KiB)
- **Memory Model:** Strict Harvard Architecture (separate 256 KiB Instruction Memory and 256 KiB Data Memory, Little-Endian)
- **Data Memory Regions:**
  - `0x00000` – `0x2FBFF`: General Data Memory
  - `0x2FC00` – `0x2FFFF`: Default Interrupt Vector Table (256 entries × 4 bytes, configurable via `SETIVT`)
  - `0x30000` – `0x3FFFF`: Protected Stack Segment (`SP` initializes to `0x3FFFE`, full-descending)
- **Registers:**
  - `R0`–`R15`: 16 × 16-bit General-Purpose Registers
  - `PC`: 18-bit Program Counter (init `0x00000`)
  - `SP`: 18-bit Stack Pointer (init `0x3FFFE`)
  - `MAR`: 18-bit Memory Address Register (init `0x00000`)
  - `IVTR`: 18-bit Interrupt Vector Table Base Register (init `0x2FC00`)
  - `FLAGS`: 16-bit Status & Control Register:
    - Bit 0 (`0x0001`): `ZF` — Zero Flag
    - Bit 1 (`0x0002`): `CF` — Carry / Borrow Flag
    - Bit 2 (`0x0004`): `SF` — Sign Flag
    - Bit 3 (`0x0008`): `OF` — Signed Overflow Flag
    - Bit 4 (`0x0010`): `IF` — Interrupt Enable Flag (`STI`/`CLI`, cleared on interrupt entry, restored on `IRET`)

---

## 2. Instruction Formats

| Format | Size | Byte Layout |
| :--- | :---: | :--- |
| `NONE` | 1B | `[op]` |
| `REGISTER` | 2B | `[op] [0x0 \| Ra]` |
| `REGISTER_REGISTER` | 2B | `[op] [Ra<<4 \| Rb]` |
| `RELATIVE8` | 2B | `[op] [rel8]` |
| `IMMEDIATE8` | 2B | `[op] [imm8]` |
| `REGISTER_IMMEDIATE8` | 3B | `[op] [0x0 \| Ra] [imm8]` |
| `REGISTER_INDEXED` | 3B | `[op] [Ra<<4 \| Rb] [0x0 \| Rc]` |
| `REGISTER_IMMEDIATE16` | 4B | `[op] [0x0 \| Ra] [imm16_lo] [imm16_hi]` |
| `ABSOLUTE18` | 4B | `[op] [addr_lo] [addr_mid] [addr_hi(0..3)]` |
| `REGISTER_INDIRECT_OFFSET` | 4B | `[op] [Ra<<4 \| Rb] [off16_lo] [off16_hi]` |
| `REGISTER_SP_OFFSET` | 4B | `[op] [0x0 \| Ra] [off16_lo] [off16_hi]` |
| `REGISTER_ABSOLUTE18` | 5B | `[op] [0x0 \| Ra] [addr_lo] [addr_mid] [addr_hi(0..3)]` |

---

## 3. Opcode Map

### System & Interrupts (`0x00–0x0B`)
| Opcode | Mnemonic | Format | Description |
| :---: | :--- | :--- | :--- |
| `0x00` | `NOP` | `NONE` | No operation |
| `0x01` | `HLT` | `NONE` | Halt CPU (`CPU_STATE_HALTED`) |
| `0x02` | `JMP abs18` | `ABSOLUTE18` | Unconditional jump to `abs18` |
| `0x03` | `CALL abs18` | `ABSOLUTE18` | Push 32-bit `next_pc` to stack, jump to `abs18` |
| `0x04` | `RET` | `NONE` | Pop 32-bit return address from stack into `PC` |
| `0x05` | `IRET` | `NONE` | Pop 16-bit `FLAGS` then 32-bit `PC` from stack |
| `0x06` | `CLI` | `NONE` | Clear `IF` (`FLAGS &= ~IF`) |
| `0x07` | `STI` | `NONE` | Set `IF` (`FLAGS |= IF`) |
| `0x08` | `INT imm8` | `IMMEDIATE8` | Software interrupt to vector `imm8` |
| `0x09` | `SETIVT abs18` | `ABSOLUTE18` | Set `IVTR = abs18` |
| `0x0A` | `JMPR Ra` | `REGISTER` | Indirect jump to `R[Ra]` (`JMP Ra` alias) |
| `0x0B` | `CALLR Ra` | `REGISTER` | Indirect call to `R[Ra]` (`CALL Ra` alias) |

### Data Transfer & Addressing Modes (`0x10–0x1C`)
| Opcode | Mnemonic | Format | Description |
| :---: | :--- | :--- | :--- |
| `0x10` | `LOADI Ra, imm16` | `REGISTER_IMMEDIATE16` | `Ra = imm16` |
| `0x11` | `LOAD Ra, [abs18]` | `REGISTER_ABSOLUTE18` | `Ra = Data16[abs18]` |
| `0x12` | `STORE Ra, [abs18]` | `REGISTER_ABSOLUTE18` | `Data16[abs18] = Ra` |
| `0x13` | `PUSH Ra` | `REGISTER` | `SP -= 2; Data16[SP] = Ra` |
| `0x14` | `POP Ra` | `REGISTER` | `Ra = Data16[SP]; SP += 2` |
| `0x15` | `MOV Ra, Rb` | `REGISTER_REGISTER` | `Ra = Rb` |
| `0x16` | `LOADR Ra, [Rb + disp16]` | `REGISTER_INDIRECT_OFFSET` | `Ra = Data16[Rb + disp16]` (`LOAD Ra, [Rb]` / `LOAD Ra, [Rb + disp]`) |
| `0x17` | `STORER Ra, [Rb + disp16]` | `REGISTER_INDIRECT_OFFSET` | `Data16[Rb + disp16] = Ra` (`STORE Ra, [Rb]` / `STORE Ra, [Rb + disp]`) |
| `0x18` | `LOADX Ra, [Rb + Rc]` | `REGISTER_INDEXED` | `Ra = Data16[Rb + Rc]` (`LOAD Ra, [Rb + Rc]`) |
| `0x19` | `STOREX Ra, [Rb + Rc]` | `REGISTER_INDEXED` | `Data16[Rb + Rc] = Ra` (`STORE Ra, [Rb + Rc]`) |
| `0x1A` | `LOADSP Ra, [SP + disp16]` | `REGISTER_SP_OFFSET` | `Ra = Stack16[SP + disp16]` (`LOAD Ra, [SP + disp]`) |
| `0x1B` | `STORESP Ra, [SP + disp16]` | `REGISTER_SP_OFFSET` | `Stack16[SP + disp16] = Ra` (`STORE Ra, [SP + disp]`) |
| `0x1C` | `LEA Ra, [Rb + disp16]` | `REGISTER_INDIRECT_OFFSET` | `Ra = (uint16_t)(Rb + disp16)` |

### Arithmetic (`0x20–0x29`)
| Opcode | Mnemonic | Format | Description |
| :---: | :--- | :--- | :--- |
| `0x20` | `ADD Ra, Rb` | `REGISTER_REGISTER` | `Ra = Ra + Rb` |
| `0x21` | `SUB Ra, Rb` | `REGISTER_REGISTER` | `Ra = Ra - Rb` |
| `0x22` | `MUL Ra, Rb` | `REGISTER_REGISTER` | `Ra = (uint16_t)(Ra * Rb)` |
| `0x23` | `MULS Ra, Rb` | `REGISTER_REGISTER` | `Ra = (int16_t)(Ra * Rb)` |
| `0x24` | `DIV Ra, Rb` | `REGISTER_REGISTER` | `Ra = Ra / Rb`, `Rb = Ra % Rb` (`Ra != Rb`) |
| `0x25` | `DIVS Ra, Rb` | `REGISTER_REGISTER` | Signed `DIV` (`Ra != Rb`) |
| `0x26` | `ADDI Ra, imm16` | `REGISTER_IMMEDIATE16` | `Ra = Ra + imm16` (`ADD Ra, imm16` alias) |
| `0x27` | `SUBI Ra, imm16` | `REGISTER_IMMEDIATE16` | `Ra = Ra - imm16` (`SUB Ra, imm16` alias) |
| `0x28` | `INC Ra` | `REGISTER` | `Ra = Ra + 1` |
| `0x29` | `DEC Ra` | `REGISTER` | `Ra = Ra - 1` |

### Bitwise, Shifts & Compare (`0x30–0x41`)
| Opcode | Mnemonic | Format | Description |
| :---: | :--- | :--- | :--- |
| `0x30` | `AND Ra, Rb` | `REGISTER_REGISTER` | `Ra = Ra & Rb` |
| `0x31` | `OR Ra, Rb` | `REGISTER_REGISTER` | `Ra = Ra \| Rb` |
| `0x32` | `XOR Ra, Rb` | `REGISTER_REGISTER` | `Ra = Ra ^ Rb` |
| `0x33` | `NOT Ra` | `REGISTER` | `Ra = ~Ra` |
| `0x34` | `SHL Ra, Rb` | `REGISTER_REGISTER` | Logical shift left by `Rb` |
| `0x35` | `SHR Ra, Rb` | `REGISTER_REGISTER` | Logical shift right by `Rb` |
| `0x36` | `SAR Ra, Rb` | `REGISTER_REGISTER` | Arithmetic shift right by `Rb` |
| `0x37` | `SHLI Ra, imm8` | `REGISTER_IMMEDIATE8` | Logical shift left by `imm8` (`SHL Ra, imm8` alias) |
| `0x38` | `SHRI Ra, imm8` | `REGISTER_IMMEDIATE8` | Logical shift right by `imm8` (`SHR Ra, imm8` alias) |
| `0x39` | `SARI Ra, imm8` | `REGISTER_IMMEDIATE8` | Arithmetic shift right by `imm8` (`SAR Ra, imm8` alias) |
| `0x40` | `CMP Ra, Rb` | `REGISTER_REGISTER` | `Ra - Rb` (updates `FLAGS` only) |
| `0x41` | `CMPI Ra, imm16` | `REGISTER_IMMEDIATE16` | `Ra - imm16` (`CMP Ra, imm16` alias) |

### Conditional Branches (`0x50–0x5B`, `RELATIVE8`)
| Opcode | Mnemonic | Condition |
| :---: | :--- | :--- |
| `0x50` | `JZ rel8` | `ZF == 1` |
| `0x51` | `JNZ rel8` | `ZF == 0` |
| `0x52` | `JC rel8` | `CF == 1` |
| `0x53` | `JNC rel8` | `CF == 0` |
| `0x54` | `JS rel8` | `SF == 1` |
| `0x55` | `JNS rel8` | `SF == 0` |
| `0x56` | `JO rel8` | `OF == 1` |
| `0x57` | `JNO rel8` | `OF == 0` |
| `0x58` | `JL rel8` | `SF != OF` |
| `0x59` | `JGE rel8` | `SF == OF` |
| `0x5A` | `JLE rel8` | `ZF == 1 \|\| SF != OF` |
| `0x5B` | `JG rel8` | `ZF == 0 && SF == OF` |

---

## 4. Interrupt & Exception Model

- **IVT Layout:** 256 entries × 4 bytes (32-bit little-endian) starting at `IVTR` (default `0x2FC00`).
  - Entry value `0x00000000` = unregistered / not present.
  - Entry value `(0x80000000 | handler_pc)` or non-zero `handler_pc <= 0x3FFFF` = present handler at `entry & 0x3FFFF`.
- **Reserved Exception Vectors (`0x01–0x08`):**
  - `0x01`: Invalid Opcode
  - `0x02`: Invalid Instruction
  - `0x03`: Memory Access Fault
  - `0x04`: Divide by Zero
  - `0x05`: Arithmetic Overflow
  - `0x06`: Stack Overflow
  - `0x07`: Stack Underflow
  - `0x08`: Instruction Fetch Fault
  - If an exception vector has a registered handler (and nesting depth `< 16` and not a stack fault preventing push), CPU traps to the handler; otherwise CPU enters `CPU_STATE_FAULTED`.
- **Interrupt Entry Sequence:**
  1. Push 32-bit return `PC` (`next_pc` for `INT`, current `pc` for IRQ/fault).
  2. Push 16-bit `FLAGS`.
  3. Clear `IF` (`FLAGS &= ~CPU_FLAG_IF`).
  4. Set `PC = handler_pc`.
- **`IRET` Sequence:**
  1. Pop 16-bit `FLAGS`.
  2. Pop 32-bit `PC`.

