#include "assembler.h"
#include "decoder.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define ASSEMBLER_MAX_LABELS 1024
#define ASSEMBLER_MAX_LABEL_LENGTH 63

typedef struct {
    char name[ASSEMBLER_MAX_LABEL_LENGTH + 1];
    uint32_t address;
} AssemblerLabel;

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

static int lookup_mnemonic(
    const char *name,
    InstructionOpcode *opcode
)
{
#define MATCH(name_text, opcode_value) \
    if (strcmp(name, name_text) == 0) { \
        *opcode = opcode_value; \
        return 1; \
    }

    MATCH("NOP", DIOCPU_OP_NOP)
    MATCH("HLT", DIOCPU_OP_HLT)
    MATCH("JMP", DIOCPU_OP_JMP)
    MATCH("CALL", DIOCPU_OP_CALL)
    MATCH("RET", DIOCPU_OP_RET)

    MATCH("LOADI", DIOCPU_OP_LOADI)
    MATCH("LOAD", DIOCPU_OP_LOAD)
    MATCH("STORE", DIOCPU_OP_STORE)
    MATCH("PUSH", DIOCPU_OP_PUSH)
    MATCH("POP", DIOCPU_OP_POP)

    MATCH("ADD", DIOCPU_OP_ADD)
    MATCH("SUB", DIOCPU_OP_SUB)
    MATCH("MUL", DIOCPU_OP_MUL)
    MATCH("MULS", DIOCPU_OP_MULS)
    MATCH("DIV", DIOCPU_OP_DIV)
    MATCH("DIVS", DIOCPU_OP_DIVS)

    MATCH("AND", DIOCPU_OP_AND)
    MATCH("OR", DIOCPU_OP_OR)
    MATCH("XOR", DIOCPU_OP_XOR)
    MATCH("NOT", DIOCPU_OP_NOT)

    MATCH("CMP", DIOCPU_OP_CMP)

    MATCH("JZ", DIOCPU_OP_JZ)
    MATCH("JNZ", DIOCPU_OP_JNZ)
    MATCH("JC", DIOCPU_OP_JC)
    MATCH("JNC", DIOCPU_OP_JNC)

#undef MATCH

    return 0;
}

static uint8_t instruction_length(InstructionOpcode opcode)
{
    switch (opcode) {

        case DIOCPU_OP_NOP:
        case DIOCPU_OP_HLT:
        case DIOCPU_OP_RET:
            return 1;

        case DIOCPU_OP_PUSH:
        case DIOCPU_OP_POP:
        case DIOCPU_OP_NOT:

        case DIOCPU_OP_ADD:
        case DIOCPU_OP_SUB:
        case DIOCPU_OP_MUL:
        case DIOCPU_OP_MULS:
        case DIOCPU_OP_DIV:
        case DIOCPU_OP_DIVS:
        case DIOCPU_OP_AND:
        case DIOCPU_OP_OR:
        case DIOCPU_OP_XOR:
        case DIOCPU_OP_CMP:

        case DIOCPU_OP_JZ:
        case DIOCPU_OP_JNZ:
        case DIOCPU_OP_JC:
        case DIOCPU_OP_JNC:
            return 2;

        case DIOCPU_OP_LOADI:
            return 4;

        case DIOCPU_OP_JMP:
        case DIOCPU_OP_CALL:
            return 4;

        case DIOCPU_OP_LOAD:
        case DIOCPU_OP_STORE:
            return 5;

        default:
            return 0;
    }
}


static unsigned int operand_count(InstructionOpcode opcode)
{
    switch (opcode) {

        case DIOCPU_OP_NOP:
        case DIOCPU_OP_HLT:
        case DIOCPU_OP_RET:
            return 0;

        case DIOCPU_OP_PUSH:
        case DIOCPU_OP_POP:
        case DIOCPU_OP_NOT:
        case DIOCPU_OP_JMP:
        case DIOCPU_OP_CALL:
        case DIOCPU_OP_JZ:
        case DIOCPU_OP_JNZ:
        case DIOCPU_OP_JC:
        case DIOCPU_OP_JNC:
            return 1;

        case DIOCPU_OP_LOADI:
        case DIOCPU_OP_LOAD:
        case DIOCPU_OP_STORE:

        case DIOCPU_OP_ADD:
        case DIOCPU_OP_SUB:
        case DIOCPU_OP_MUL:
        case DIOCPU_OP_MULS:
        case DIOCPU_OP_DIV:
        case DIOCPU_OP_DIVS:
        case DIOCPU_OP_AND:
        case DIOCPU_OP_OR:
        case DIOCPU_OP_XOR:
        case DIOCPU_OP_CMP:
            return 2;

        default:
            return 0;
    }
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


static AssemblerStatus parse_memory_address(
    char *text,
    const AssemblerLabel *labels,
    size_t label_count,
    uint32_t *address
)
{
    size_t length = strlen(text);

    if (length < 3 ||
        text[0] != '[' ||
        text[length - 1] != ']') {
        return ASSEMBLER_STATUS_SYNTAX_ERROR;
    }

    text[length - 1] = '\0';

    char *inside = trim(text + 1);

    if (*inside == '\0') {
        return ASSEMBLER_STATUS_SYNTAX_ERROR;
    }

    return resolve_address(
        inside,
        labels,
        label_count,
        address
    );
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

        (void)operands;

        uppercase(mnemonic);

        InstructionOpcode opcode;

        if (!lookup_mnemonic(mnemonic, &opcode)) {
            *error_line = line_number;
            return ASSEMBLER_STATUS_UNKNOWN_MNEMONIC;
        }

        uint8_t length = instruction_length(opcode);

        if (length == 0) {
            *error_line = line_number;
            return ASSEMBLER_STATUS_UNKNOWN_MNEMONIC;
        }

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

    InstructionOpcode opcode;

    if (!lookup_mnemonic(mnemonic, &opcode)) {
        return ASSEMBLER_STATUS_UNKNOWN_MNEMONIC;
    }

    uint32_t instruction_pc = *pc;

    char *a = NULL;
    char *b = NULL;

    AssemblerStatus status = split_operands(
        operand_text,
        operand_count(opcode),
        &a,
        &b
    );

    if (status != ASSEMBLER_STATUS_OK) {
        return status;
    }

    emit_byte(image, pc, (uint8_t)opcode);

    uint8_t reg_a;
    uint8_t reg_b;

    uint32_t address;
    int32_t offset;

    long immediate;


    switch (opcode) {

        case DIOCPU_OP_NOP:
        case DIOCPU_OP_HLT:
        case DIOCPU_OP_RET:
            break;


        case DIOCPU_OP_PUSH:
        case DIOCPU_OP_POP:
        case DIOCPU_OP_NOT:

            status = parse_register(a, &reg_a);

            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }

            emit_byte(image, pc, reg_a);

            break;


        case DIOCPU_OP_ADD:
        case DIOCPU_OP_SUB:
        case DIOCPU_OP_MUL:
        case DIOCPU_OP_MULS:
        case DIOCPU_OP_DIV:
        case DIOCPU_OP_DIVS:
        case DIOCPU_OP_AND:
        case DIOCPU_OP_OR:
        case DIOCPU_OP_XOR:
        case DIOCPU_OP_CMP:

            status = parse_register(a, &reg_a);

            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }

            status = parse_register(b, &reg_b);

            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }

            emit_byte(
                image,
                pc,
                (uint8_t)((reg_a << 4u) | reg_b)
            );

            break;


        case DIOCPU_OP_LOADI:

            status = parse_register(a, &reg_a);

            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }

            status = parse_integer(b, &immediate);

            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }

            if (immediate < -32768 || immediate > 65535) {
                return ASSEMBLER_STATUS_OPERAND_OUT_OF_RANGE;
            }

            {
                uint16_t value = (uint16_t)immediate;

                emit_byte(image, pc, reg_a);

                emit_byte(
                    image,
                    pc,
                    (uint8_t)(value & 0xFFu)
                );

                emit_byte(
                    image,
                    pc,
                    (uint8_t)(value >> 8u)
                );
            }

            break;


        case DIOCPU_OP_LOAD:
        case DIOCPU_OP_STORE:

            status = parse_register(a, &reg_a);

            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }

            status = parse_memory_address(
                b,
                labels,
                label_count,
                &address
            );

            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }

            emit_byte(image, pc, reg_a);
            emit_address18(image, pc, address);

            break;


        case DIOCPU_OP_JMP:
        case DIOCPU_OP_CALL:

            status = resolve_address(
                a,
                labels,
                label_count,
                &address
            );

            if (status != ASSEMBLER_STATUS_OK) {
                return status;
            }

            emit_address18(image, pc, address);

            break;


        case DIOCPU_OP_JZ:
        case DIOCPU_OP_JNZ:
        case DIOCPU_OP_JC:
        case DIOCPU_OP_JNC:

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
