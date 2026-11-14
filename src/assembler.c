#include "assembler.h"
#include "decoder.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSEMBLER_MAX_LABELS 1024
#define ASSEMBLER_MAX_LABEL_LENGTH 63
#define ASSEMBLER_LINE_SCRATCH 512

typedef struct {
    char name[ASSEMBLER_MAX_LABEL_LENGTH + 1];
    uint32_t address;
} AssemblerLabel;

typedef enum {
    MEM_MODE_ABS18 = 0,
    MEM_MODE_REG_OFFSET,
    MEM_MODE_INDEXED,
    MEM_MODE_SP_OFFSET
} MemoryOperandMode;

typedef struct {
    MemoryOperandMode mode;
    uint8_t base_reg;
    uint8_t index_reg;
    int16_t offset16;
    uint32_t abs18;
} ParsedMemoryOperand;

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        text++;
    }

    char *end = text + strlen(text);

    while (end > text &&
           isspace((unsigned char)end[-1])) {
        end--;
    }

    *end = '\0';

    return text;
}

static void uppercase(char *text)
{
    while (*text) {
        *text = (char)toupper((unsigned char)*text);
        text++;
    }
}

static void remove_comment(char *line)
{
    for (char *p = line; *p; p++) {
        if (*p == ';' || *p == '#') {
            *p = '\0';
            break;
        }
    }
}

static char *next_line(char **cursor)
{
    if (cursor == NULL || *cursor == NULL) {
        return NULL;
    }

    char *line = *cursor;
    char *newline = strchr(line, '\n');

    if (newline != NULL) {
        *newline = '\0';
        *cursor = newline + 1;
    } else {
        *cursor = NULL;
    }

    size_t length = strlen(line);

    if (length > 0 && line[length - 1] == '\r') {
        line[length - 1] = '\0';
    }

    return line;
}

static int split_instruction(
    char *line,
    char **mnemonic,
    char **operands
)
{
    line = trim(line);

    if (*line == '\0') {
        return 0;
    }

    char *end = line;

    while (*end && !isspace((unsigned char)*end)) {
        end++;
    }

    if (*end == '\0') {
        *mnemonic = line;
        *operands = end;
    } else {
        *end = '\0';

        *mnemonic = line;
        *operands = trim(end + 1);
    }

    return 1;
}

static int valid_label_name(const char *name)
{
    if (name == NULL || *name == '\0') {
        return 0;
    }

    size_t length = strlen(name);

    if (length > ASSEMBLER_MAX_LABEL_LENGTH) {
        return 0;
    }

    if (!isalpha((unsigned char)name[0]) &&
        name[0] != '_') {
        return 0;
    }

    for (size_t i = 1; i < length; i++) {
        if (!isalnum((unsigned char)name[i]) &&
            name[i] != '_') {
            return 0;
        }
    }

    return 1;
}

static int find_label(
    const AssemblerLabel *labels,
    size_t label_count,
    const char *name
)
{
    for (size_t i = 0; i < label_count; i++) {
        if (strcmp(labels[i].name, name) == 0) {
            return (int)i;
        }
    }

    return -1;
}

static AssemblerStatus extract_label(
    char **line,
    AssemblerLabel *labels,
    size_t *label_count,
    uint32_t current_address,
    int add_label
)
{
    char *colon = strchr(*line, ':');

    if (colon == NULL) {
        *line = trim(*line);
        return ASSEMBLER_STATUS_OK;
    }

    *colon = '\0';

    char *name = trim(*line);

    if (!valid_label_name(name)) {
        return ASSEMBLER_STATUS_SYNTAX_ERROR;
    }

    if (add_label) {
        if (find_label(labels, *label_count, name) >= 0) {
            return ASSEMBLER_STATUS_DUPLICATE_LABEL;
        }

        if (*label_count >= ASSEMBLER_MAX_LABELS) {
            return ASSEMBLER_STATUS_TOO_MANY_LABELS;
        }

        strcpy(labels[*label_count].name, name);
        labels[*label_count].address = current_address;

        (*label_count)++;
    }

    *line = trim(colon + 1);

    return ASSEMBLER_STATUS_OK;
}

static AssemblerStatus split_operands(
    char *text,
    unsigned int expected_count,
    char **operand_a,
    char **operand_b
)
{
    *operand_a = NULL;
    *operand_b = NULL;

    text = trim(text);

    if (expected_count == 0) {
        return (*text == '\0')
            ? ASSEMBLER_STATUS_OK
            : ASSEMBLER_STATUS_SYNTAX_ERROR;
    }

    if (*text == '\0') {
        return ASSEMBLER_STATUS_SYNTAX_ERROR;
    }

    char *comma = strchr(text, ',');

    if (expected_count == 1) {
        if (comma != NULL) {
            return ASSEMBLER_STATUS_SYNTAX_ERROR;
        }

        *operand_a = text;

        return ASSEMBLER_STATUS_OK;
    }

    if (expected_count == 2) {
        if (comma == NULL) {
            return ASSEMBLER_STATUS_SYNTAX_ERROR;
        }

        *comma = '\0';

        if (strchr(comma + 1, ',') != NULL) {
            return ASSEMBLER_STATUS_SYNTAX_ERROR;
        }

        *operand_a = trim(text);
        *operand_b = trim(comma + 1);

        if (**operand_a == '\0' || **operand_b == '\0') {
            return ASSEMBLER_STATUS_SYNTAX_ERROR;
        }

        return ASSEMBLER_STATUS_OK;
    }

    return ASSEMBLER_STATUS_SYNTAX_ERROR;
}

static AssemblerStatus parse_integer(
    const char *text,
    long *value
)
{
    if (text == NULL || *text == '\0' || value == NULL) {
        return ASSEMBLER_STATUS_INVALID_NUMBER;
    }

    errno = 0;

    char *end = NULL;
    long parsed = strtol(text, &end, 0);

    if (errno == ERANGE || end == text) {
        return ASSEMBLER_STATUS_INVALID_NUMBER;
    }

    while (isspace((unsigned char)*end)) {
        end++;
    }

    if (*end != '\0') {
        return ASSEMBLER_STATUS_INVALID_NUMBER;
    }

    *value = parsed;

    return ASSEMBLER_STATUS_OK;
}

static AssemblerStatus parse_register(
    const char *text,
    uint8_t *reg
)
{
    if (text == NULL ||
        (text[0] != 'R' && text[0] != 'r') ||
        text[1] == '\0') {
        return ASSEMBLER_STATUS_INVALID_REGISTER;
    }

    for (const char *p = text + 1; *p; p++) {
        if (!isdigit((unsigned char)*p)) {
            return ASSEMBLER_STATUS_INVALID_REGISTER;
        }
    }

    errno = 0;

    char *end = NULL;
    long value = strtol(text + 1, &end, 10);

    if (errno == ERANGE || *end != '\0' ||
        value < 0 || value > 15) {
        return ASSEMBLER_STATUS_INVALID_REGISTER;
    }

    *reg = (uint8_t)value;

    return ASSEMBLER_STATUS_OK;
}

static int is_register_token(const char *text)
{
    uint8_t dummy = 0;
    return parse_register(text, &dummy) == ASSEMBLER_STATUS_OK;
}

static int is_sp_token(const char *text)
{
    return text != NULL &&
           (text[0] == 'S' || text[0] == 's') &&
           (text[1] == 'P' || text[1] == 'p') &&
           text[2] == '\0';
}

static int is_label_identifier(const char *text)
{
    return valid_label_name(text);
}

static AssemblerStatus resolve_address(
    const char *text,
    const AssemblerLabel *labels,
    size_t label_count,
    uint32_t *address
)
{
    if (is_label_identifier(text)) {
        int index = find_label(labels, label_count, text);

        if (index < 0) {
            return ASSEMBLER_STATUS_UNDEFINED_LABEL;
        }

        if (labels[index].address > DIOCPU_MEMORY_MAX_ADDRESS) {
            return ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE;
        }

        *address = labels[index].address;

        return ASSEMBLER_STATUS_OK;
    }

    long value;

    AssemblerStatus status = parse_integer(text, &value);

    if (status != ASSEMBLER_STATUS_OK) {
        return status;
    }

    if (value < 0 || (uint32_t)value > DIOCPU_MEMORY_MAX_ADDRESS) {
        return ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE;
    }

    *address = (uint32_t)value;

    return ASSEMBLER_STATUS_OK;
}

static AssemblerStatus parse_memory_operand(
    const char *text,
    const AssemblerLabel *labels,
    size_t label_count,
    int resolve_symbols,
    ParsedMemoryOperand *out
)
{
    char buf[ASSEMBLER_LINE_SCRATCH];
    size_t len = strlen(text);

    if (len >= sizeof(buf) || len < 3 ||
        text[0] != '[' || text[len - 1] != ']') {
        return ASSEMBLER_STATUS_SYNTAX_ERROR;
    }

    memcpy(buf, text + 1, len - 2);
    buf[len - 2] = '\0';

    char *inside = trim(buf);
    if (*inside == '\0') {
        return ASSEMBLER_STATUS_SYNTAX_ERROR;
    }

    char *plus = strchr(inside, '+');
    char *minus = strchr(inside, '-');
    char *op_pos = NULL;
    int sign = 1;

    if (plus != NULL && minus != NULL) {
        return ASSEMBLER_STATUS_SYNTAX_ERROR;
    }
    if (plus != NULL) {
        op_pos = plus;
        sign = 1;
    } else if (minus != NULL) {
        op_pos = minus;
        sign = -1;
    }

    memset(out, 0, sizeof(*out));

    if (op_pos == NULL) {
        uint8_t reg = 0;

        if (is_sp_token(inside)) {
            out->mode = MEM_MODE_SP_OFFSET;
            out->offset16 = 0;
            return ASSEMBLER_STATUS_OK;
        }

        if (parse_register(inside, &reg) == ASSEMBLER_STATUS_OK) {
            out->mode = MEM_MODE_REG_OFFSET;
            out->base_reg = reg;
            out->offset16 = 0;
            return ASSEMBLER_STATUS_OK;
        }

        out->mode = MEM_MODE_ABS18;
        if (!resolve_symbols) {
            return ASSEMBLER_STATUS_OK;
        }
        return resolve_address(inside, labels, label_count, &out->abs18);
    }

    *op_pos = '\0';
    char *left = trim(inside);
    char *right = trim(op_pos + 1);

    if (*left == '\0' || *right == '\0') {
        return ASSEMBLER_STATUS_SYNTAX_ERROR;
    }

    uint8_t base = 0;
    uint8_t idx = 0;

    if (is_sp_token(left)) {
        long val = 0;
        AssemblerStatus st = parse_integer(right, &val);
        if (st != ASSEMBLER_STATUS_OK) {
            return st;
        }
        val *= sign;
        if (val < -32768 || val > 32767) {
            return ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE;
        }
        out->mode = MEM_MODE_SP_OFFSET;
        out->offset16 = (int16_t)val;
        return ASSEMBLER_STATUS_OK;
    }

    if (parse_register(left, &base) == ASSEMBLER_STATUS_OK) {
        if (sign == 1 && parse_register(right, &idx) == ASSEMBLER_STATUS_OK) {
            out->mode = MEM_MODE_INDEXED;
            out->base_reg = base;
            out->index_reg = idx;
            return ASSEMBLER_STATUS_OK;
        }

        long val = 0;
        AssemblerStatus st = parse_integer(right, &val);
        if (st != ASSEMBLER_STATUS_OK) {
            return st;
        }
        val *= sign;
        if (val < -32768 || val > 32767) {
            return ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE;
        }
        out->mode = MEM_MODE_REG_OFFSET;
        out->base_reg = base;
        out->offset16 = (int16_t)val;
        return ASSEMBLER_STATUS_OK;
    }

    return ASSEMBLER_STATUS_SYNTAX_ERROR;
}

static AssemblerStatus resolve_instruction_form(
    const char *mnemonic,
    const char *operand_text,
    const ISAInstructionInfo **out_info
)
{
    const ISAInstructionInfo *base = isa_lookup_mnemonic(mnemonic);
    if (base == NULL) {
        return ASSEMBLER_STATUS_UNKNOWN_MNEMONIC;
    }

    char scratch[ASSEMBLER_LINE_SCRATCH];
    size_t op_len = strlen(operand_text);
    if (op_len >= sizeof(scratch)) {
        return ASSEMBLER_STATUS_SYNTAX_ERROR;
    }
    memcpy(scratch, operand_text, op_len + 1u);

    char *a = NULL;
    char *b = NULL;

    /* Check overloaded mnemonics that select addressing mode by operand shape. */
    if (base->opcode == DIOCPU_OP_JMP || base->opcode == DIOCPU_OP_CALL) {
        AssemblerStatus st = split_operands(scratch, 1, &a, &b);
        if (st != ASSEMBLER_STATUS_OK) {
            return st;
        }
        if (is_register_token(a)) {
            InstructionOpcode target = (base->opcode == DIOCPU_OP_JMP)
                ? DIOCPU_OP_JMPR
                : DIOCPU_OP_CALLR;
            *out_info = isa_lookup_opcode(target);
            return ASSEMBLER_STATUS_OK;
        }
        *out_info = base;
        return ASSEMBLER_STATUS_OK;
    }

    if (base->opcode == DIOCPU_OP_LOAD || base->opcode == DIOCPU_OP_STORE) {
        AssemblerStatus st = split_operands(scratch, 2, &a, &b);
        if (st != ASSEMBLER_STATUS_OK) {
            return st;
        }
        ParsedMemoryOperand mem;
        st = parse_memory_operand(b, NULL, 0, 0, &mem);
        if (st != ASSEMBLER_STATUS_OK) {
            return st;
        }
        int is_load = (base->opcode == DIOCPU_OP_LOAD);
        InstructionOpcode chosen = base->opcode;

        switch (mem.mode) {
            case MEM_MODE_ABS18:
                chosen = is_load ? DIOCPU_OP_LOAD : DIOCPU_OP_STORE;
                break;
            case MEM_MODE_REG_OFFSET:
                chosen = is_load ? DIOCPU_OP_LOADR : DIOCPU_OP_STORER;
                break;
            case MEM_MODE_INDEXED:
                chosen = is_load ? DIOCPU_OP_LOADX : DIOCPU_OP_STOREX;
                break;
            case MEM_MODE_SP_OFFSET:
                chosen = is_load ? DIOCPU_OP_LOADSP : DIOCPU_OP_STORESP;
                break;
        }
        *out_info = isa_lookup_opcode(chosen);
        return ASSEMBLER_STATUS_OK;
    }

    if (base->opcode == DIOCPU_OP_ADD ||
        base->opcode == DIOCPU_OP_SUB ||
        base->opcode == DIOCPU_OP_CMP ||
        base->opcode == DIOCPU_OP_SHL ||
        base->opcode == DIOCPU_OP_SHR ||
        base->opcode == DIOCPU_OP_SAR) {
        AssemblerStatus st = split_operands(scratch, 2, &a, &b);
        if (st != ASSEMBLER_STATUS_OK) {
            return st;
        }
        if (!is_register_token(b)) {
            InstructionOpcode imm_op = base->opcode;
            if (base->opcode == DIOCPU_OP_ADD) imm_op = DIOCPU_OP_ADDI;
            else if (base->opcode == DIOCPU_OP_SUB) imm_op = DIOCPU_OP_SUBI;
            else if (base->opcode == DIOCPU_OP_CMP) imm_op = DIOCPU_OP_CMPI;
            else if (base->opcode == DIOCPU_OP_SHL) imm_op = DIOCPU_OP_SHLI;
            else if (base->opcode == DIOCPU_OP_SHR) imm_op = DIOCPU_OP_SHRI;
            else if (base->opcode == DIOCPU_OP_SAR) imm_op = DIOCPU_OP_SARI;
            *out_info = isa_lookup_opcode(imm_op);
            return ASSEMBLER_STATUS_OK;
        }
        *out_info = base;
        return ASSEMBLER_STATUS_OK;
    }

    *out_info = base;
    return ASSEMBLER_STATUS_OK;
}

static AssemblerStatus resolve_branch_offset(
    const char *text,
    const AssemblerLabel *labels,
    size_t label_count,
    uint32_t instruction_pc,
    int32_t *offset
)
{
    long value;

    if (is_label_identifier(text)) {
        int index = find_label(labels, label_count, text);

        if (index < 0) {
            return ASSEMBLER_STATUS_UNDEFINED_LABEL;
        }

        value = (long)labels[index].address -
                (long)(instruction_pc + 2u);

    } else {
        AssemblerStatus status = parse_integer(text, &value);

        if (status != ASSEMBLER_STATUS_OK) {
            return status;
        }
    }

    if (value < -128 || value > 127) {
        return ASSEMBLER_STATUS_BRANCH_OUT_OF_RANGE;
    }

    *offset = (int32_t)value;

    return ASSEMBLER_STATUS_OK;
}

static AssemblerStatus first_pass(
    char *source,
    AssemblerLabel *labels,
    size_t *label_count,
    uint32_t *code_size,
    uint32_t *instruction_count,
    uint32_t *error_line
)
{
    char *cursor = source;

    uint32_t pc = 0;
    uint32_t line_number = 0;

    char *line;

    while ((line = next_line(&cursor)) != NULL) {

        line_number++;

        remove_comment(line);

        char *work = trim(line);

        if (*work == '\0') {
            continue;
        }

        AssemblerStatus status = extract_label(
            &work,
            labels,
            label_count,
            pc,
            1
        );

        if (status != ASSEMBLER_STATUS_OK) {
            *error_line = line_number;
            return status;
        }

        if (*work == '\0') {
            continue;
        }

        char *mnemonic;
        char *operands;

        if (!split_instruction(work, &mnemonic, &operands)) {
            *error_line = line_number;
            return ASSEMBLER_STATUS_SYNTAX_ERROR;
        }

        uppercase(mnemonic);

        const ISAInstructionInfo *info = NULL;
        status = resolve_instruction_form(mnemonic, operands, &info);

        if (status != ASSEMBLER_STATUS_OK || info == NULL) {
            *error_line = line_number;
            return (status != ASSEMBLER_STATUS_OK)
                ? status
                : ASSEMBLER_STATUS_UNKNOWN_MNEMONIC;
        }

        uint8_t length = info->length;

        if (pc > DIOCPU_MEMORY_SIZE - length) {
            *error_line = line_number;
            return ASSEMBLER_STATUS_PROGRAM_TOO_LARGE;
        }

        pc += length;

        (*instruction_count)++;
    }

    *code_size = pc;

    return ASSEMBLER_STATUS_OK;
}

static void emit_byte(
    uint8_t *image,
    uint32_t *pc,
    uint8_t value
)
{
    image[*pc] = value;
    (*pc)++;
}

static void emit_uint16(
    uint8_t *image,
    uint32_t *pc,
    uint16_t value
)
{
    emit_byte(image, pc, (uint8_t)(value & 0xFFu));
    emit_byte(image, pc, (uint8_t)(value >> 8u));
}

static void emit_address18(
    uint8_t *image,
    uint32_t *pc,
    uint32_t address
)
{
    emit_byte(image, pc, (uint8_t)(address & 0xFFu));

    emit_byte(
        image,
        pc,
        (uint8_t)((address >> 8u) & 0xFFu)
    );

    emit_byte(
        image,
        pc,
        (uint8_t)((address >> 16u) & 0x03u)
    );
}

static AssemblerStatus encode_instruction(
    char *line,
    const AssemblerLabel *labels,
    size_t label_count,
    uint8_t *image,
    uint32_t *pc
)
{
    char *mnemonic;
    char *operand_text;

    if (!split_instruction(line, &mnemonic, &operand_text)) {
        return ASSEMBLER_STATUS_SYNTAX_ERROR;
    }

    uppercase(mnemonic);

    const ISAInstructionInfo *info = NULL;
    AssemblerStatus status = resolve_instruction_form(mnemonic, operand_text, &info);
    if (status != ASSEMBLER_STATUS_OK || info == NULL) {
        return (status != ASSEMBLER_STATUS_OK)
            ? status
            : ASSEMBLER_STATUS_UNKNOWN_MNEMONIC;
    }

    uint32_t instruction_pc = *pc;

    char *a = NULL;
    char *b = NULL;

    status = split_operands(
        operand_text,
        info->operand_count,
        &a,
        &b
    );

    if (status != ASSEMBLER_STATUS_OK) {
        return status;
    }

    emit_byte(image, pc, (uint8_t)info->opcode);

    uint8_t reg_a = 0;
    uint8_t reg_b = 0;
    uint32_t address = 0;
    int32_t offset = 0;
    long immediate = 0;

    switch (info->format) {

        case INSTRUCTION_FORMAT_NONE:
            break;

        case INSTRUCTION_FORMAT_REGISTER:
            status = parse_register(a, &reg_a);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            emit_byte(image, pc, reg_a);
            break;

        case INSTRUCTION_FORMAT_REGISTER_REGISTER:
            status = parse_register(a, &reg_a);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            status = parse_register(b, &reg_b);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            emit_byte(image, pc, (uint8_t)((reg_a << 4u) | reg_b));
            break;

        case INSTRUCTION_FORMAT_REGISTER_IMMEDIATE16:
            status = parse_register(a, &reg_a);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            if (info->opcode == DIOCPU_OP_LOADI && is_label_identifier(b)) {
                uint32_t label_addr = 0;
                status = resolve_address(b, labels, label_count, &label_addr);
                if (status != ASSEMBLER_STATUS_OK) {
                    return status;
                }
                if (label_addr > 0xFFFFu) {
                    return ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE;
                }
                immediate = (long)label_addr;
            } else {
                status = parse_integer(b, &immediate);
                if (status != ASSEMBLER_STATUS_OK) {
                    return status;
                }
                if (immediate < -32768 || immediate > 65535) {
                    return ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE;
                }
            }
            emit_byte(image, pc, reg_a);
            emit_uint16(image, pc, (uint16_t)immediate);
            break;

        case INSTRUCTION_FORMAT_REGISTER_ABSOLUTE18:
        {
            status = parse_register(a, &reg_a);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            ParsedMemoryOperand mem;
            status = parse_memory_operand(b, labels, label_count, 1, &mem);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            if (mem.mode != MEM_MODE_ABS18) {
                return ASSEMBLER_STATUS_SYNTAX_ERROR;
            }
            emit_byte(image, pc, reg_a);
            emit_address18(image, pc, mem.abs18);
            break;
        }

        case INSTRUCTION_FORMAT_ABSOLUTE18:
            status = resolve_address(a, labels, label_count, &address);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            emit_address18(image, pc, address);
            break;

        case INSTRUCTION_FORMAT_RELATIVE8:
            status = resolve_branch_offset(
                a,
                labels,
                label_count,
                instruction_pc,
                &offset
            );
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            emit_byte(image, pc, (uint8_t)offset);
            break;

        case INSTRUCTION_FORMAT_IMMEDIATE8:
            status = parse_integer(a, &immediate);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            if (immediate < 0 || immediate > 255) {
                return ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE;
            }
            emit_byte(image, pc, (uint8_t)immediate);
            break;

        case INSTRUCTION_FORMAT_REGISTER_IMMEDIATE8:
            status = parse_register(a, &reg_a);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            status = parse_integer(b, &immediate);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            if (immediate < 0 || immediate > 255) {
                return ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE;
            }
            emit_byte(image, pc, reg_a);
            emit_byte(image, pc, (uint8_t)immediate);
            break;

        case INSTRUCTION_FORMAT_REGISTER_INDIRECT_OFFSET:
        {
            status = parse_register(a, &reg_a);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            ParsedMemoryOperand mem;
            status = parse_memory_operand(b, labels, label_count, 1, &mem);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            if (mem.mode != MEM_MODE_REG_OFFSET) {
                return ASSEMBLER_STATUS_SYNTAX_ERROR;
            }
            emit_byte(image, pc, (uint8_t)((reg_a << 4u) | mem.base_reg));
            emit_uint16(image, pc, (uint16_t)mem.offset16);
            break;
        }

        case INSTRUCTION_FORMAT_REGISTER_INDEXED:
        {
            status = parse_register(a, &reg_a);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            ParsedMemoryOperand mem;
            status = parse_memory_operand(b, labels, label_count, 1, &mem);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            if (mem.mode != MEM_MODE_INDEXED) {
                return ASSEMBLER_STATUS_SYNTAX_ERROR;
            }
            emit_byte(image, pc, (uint8_t)((reg_a << 4u) | mem.base_reg));
            emit_byte(image, pc, mem.index_reg);
            break;
        }

        case INSTRUCTION_FORMAT_REGISTER_SP_OFFSET:
        {
            status = parse_register(a, &reg_a);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            ParsedMemoryOperand mem;
            status = parse_memory_operand(b, labels, label_count, 1, &mem);
            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }
            if (mem.mode != MEM_MODE_SP_OFFSET) {
                return ASSEMBLER_STATUS_SYNTAX_ERROR;
            }
            emit_byte(image, pc, reg_a);
            emit_uint16(image, pc, (uint16_t)mem.offset16);
            break;
        }

        default:
            return ASSEMBLER_STATUS_UNKNOWN_MNEMONIC;
    }

    return ASSEMBLER_STATUS_OK;
}

static AssemblerStatus second_pass(
    char *source,
    const AssemblerLabel *labels,
    size_t label_count,
    uint8_t *image,
    uint32_t *error_line
)
{
    char *cursor = source;

    uint32_t pc = 0;
    uint32_t line_number = 0;

    char *line;

    while ((line = next_line(&cursor)) != NULL) {

        line_number++;

        remove_comment(line);

        char *work = trim(line);

        if (*work == '\0') {
            continue;
        }

        AssemblerStatus status = extract_label(
            &work,
            (AssemblerLabel *)labels,
            &(size_t){label_count},
            pc,
            0
        );

        if (status != ASSEMBLER_STATUS_OK) {
            *error_line = line_number;
            return status;
        }

        if (*work == '\0') {
            continue;
        }

        status = encode_instruction(
            work,
            labels,
            label_count,
            image,
            &pc
        );

        if (status != ASSEMBLER_STATUS_OK) {
            *error_line = line_number;
            return status;
        }
    }

    return ASSEMBLER_STATUS_OK;
}

AssemblerStatus assembler_assemble(
    const char *source,
    Memory *memory,
    AssemblerResult *result
)
{
    if (result == NULL) {
        return ASSEMBLER_STATUS_INVALID_ARGUMENT;
    }

    *result = (AssemblerResult){0};

    if (source == NULL || memory == NULL) {
        result->status = ASSEMBLER_STATUS_INVALID_ARGUMENT;
        return result->status;
    }

    size_t source_length = strlen(source);

    char *first_source = malloc(source_length + 1u);
    char *second_source = malloc(source_length + 1u);

    AssemblerLabel *labels = calloc(
        ASSEMBLER_MAX_LABELS,
        sizeof(AssemblerLabel)
    );

    uint8_t *image = calloc(
        DIOCPU_MEMORY_SIZE,
        sizeof(uint8_t)
    );

    if (first_source == NULL ||
        second_source == NULL ||
        labels == NULL ||
        image == NULL) {

        free(first_source);
        free(second_source);
        free(labels);
        free(image);

        result->status = ASSEMBLER_STATUS_OUT_OF_MEMORY;

        return result->status;
    }

    memcpy(first_source, source, source_length + 1u);
    memcpy(second_source, source, source_length + 1u);

    size_t label_count = 0;

    uint32_t code_size = 0;
    uint32_t instruction_count = 0;
    uint32_t error_line = 0;

    AssemblerStatus status = first_pass(
        first_source,
        labels,
        &label_count,
        &code_size,
        &instruction_count,
        &error_line
    );

    if (status == ASSEMBLER_STATUS_OK) {
        status = second_pass(
            second_source,
            labels,
            label_count,
            image,
            &error_line
        );
    }

    if (status == ASSEMBLER_STATUS_OK) {

        /*
         * Commit only after the entire program
         * has been assembled successfully.
         *
         * Data Memory remains unchanged.
         */

        memcpy(
            memory->instruction,
            image,
            DIOCPU_MEMORY_SIZE
        );

        result->code_size = code_size;
        result->instruction_count = instruction_count;
        result->error_line = 0;
    }

    result->status = status;
    result->error_line = error_line;

    free(first_source);
    free(second_source);
    free(labels);
    free(image);

    return status;
}

const char *assembler_status_string(AssemblerStatus status)
{
    switch (status) {

        case ASSEMBLER_STATUS_OK:
            return "Success";

        case ASSEMBLER_STATUS_INVALID_ARGUMENT:
            return "Invalid argument";

        case ASSEMBLER_STATUS_OUT_OF_MEMORY:
            return "Out of memory";

        case ASSEMBLER_STATUS_SYNTAX_ERROR:
            return "Syntax error";

        case ASSEMBLER_STATUS_UNKNOWN_MNEMONIC:
            return "Unknown mnemonic";

        case ASSEMBLER_STATUS_DUPLICATE_LABEL:
            return "Duplicate label";

        case ASSEMBLER_STATUS_UNDEFINED_LABEL:
            return "Undefined label";

        case ASSEMBLER_STATUS_INVALID_REGISTER:
            return "Invalid register";

        case ASSEMBLER_STATUS_INVALID_NUMBER:
            return "Invalid number";

        case ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE:
            return "Operand out of range";

        case ASSEMBLER_STATUS_BRANCH_OUT_OF_RANGE:
            return "Branch out of range";

        case ASSEMBLER_STATUS_PROGRAM_TOO_LARGE:
            return "Program too large";

        case ASSEMBLER_STATUS_TOO_MANY_LABELS:
            return "Too many labels";

        default:
            return "Unknown assembler error";
    }
}
