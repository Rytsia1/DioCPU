; =========================================
; DioCPU V2 Integration Test
; =========================================

    ; Load values
    LOADI R1, 10
    LOADI R2, 20

    ; Arithmetic
    ADD R1, R2

    ; Data memory
    LOADI R3, 0x0100

    STORE R1, [0x0100]
    LOAD R4, [0x0100]

    ; Comparison and conditional branch
    CMP R4, R1
    JZ EQUAL

    LOADI R5, 0
    JMP FINISH

EQUAL:
    ; Function call
    CALL DOUBLE

    ; Stack operations
    PUSH R1
    POP R6

FINISH:
    HLT

; =========================================
; Subroutine
; =========================================

DOUBLE:
    LOADI R7, 2
    MUL R1, R7
    RET
