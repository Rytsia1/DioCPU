#ifndef DIOCPU_DEBUGGER_H
#define DIOCPU_DEBUGGER_H

#include "decoder.h"

#include <stdio.h>

typedef enum {
    DEBUGGER_STATUS_OK = 0,
    DEBUGGER_STATUS_INVALID_ARGUMENT,
    DEBUGGER_STATUS_ADDRESS_RANGE,
    DEBUGGER_STATUS_INCOMPLETE_INSTRUCTION,
    DEBUGGER_STATUS_DECODE_ERROR,
    DEBUGGER_STATUS_OUTPUT_ERROR
} DebuggerStatus;


typedef struct {
    uint32_t next_pc;
    uint32_t instruction_count;
    uint32_t fault_pc;

    DecoderStatus decoder_status;
} DebuggerResult;


DebuggerStatus debugger_disassemble(
    const Memory *memory,
    uint32_t start_pc,
    uint32_t byte_count,
    FILE *output,
    DebuggerResult *result
);

#endif
