# DioCPU V2

DioCPU V2 is a custom 16-bit CPU architecture implemented in C.

## Architecture

- 16-bit general-purpose registers (R0–R15)
- 18-bit address space
- Harvard architecture
- 256 KiB Instruction Memory
- 256 KiB Data Memory
- Little Endian memory representation
- 16-bit ALU
- 18-bit Program Counter, Stack Pointer, and Memory Address Register
- 16-bit FLAGS register

## Core Components

| Component           | Responsibility                              |
| ------------------- | ------------------------------------------- |
| Register File       | General-purpose and architectural registers |
| ALU                 | Arithmetic and logic operations             |
| Memory System       | Instruction and data memory                 |
| Stack System        | PUSH, POP, CALL, RET                        |
| Instruction Decoder | Machine-code decoding                       |
| Assembler           | Assembly-to-machine-code translation        |
| Debugger            | Disassembly and instruction inspection      |
| Execution Engine    | Fetch, Decode, Execute, and fault handling  |

## Build

```bash
make cpu-v2
```

## Run Tests

```bash
make cpu-v2-test
```

## Run an Assembly Program

```bash
./diocpu_v2.exe examples/integration.asm
```

## Execution Model

DioCPU uses a Fetch–Decode–Execute execution model.

Instructions are fetched from Instruction Memory, decoded into structured operands, and executed using the corresponding CPU subsystems.

## Fault Handling

The CPU supports the following fault categories:

- Invalid opcode
- Invalid instruction
- Memory access fault
- Divide by zero
- Arithmetic overflow
- Stack overflow
- Stack underflow
- Instruction fetch fault

A fault transitions the CPU into the FAULTED state. The faulting instruction address is preserved.

## Version

DioCPU V2 — 16-bit CPU implementation.
