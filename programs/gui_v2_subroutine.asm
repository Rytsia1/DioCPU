; ===================================================
; DioCPU V2 - Nested Subroutine & Stack Demonstration
; Demonstrates PUSH, POP, CALL, and RET across multiple
; nesting levels for the Stack Viewer.
; ===================================================

start:
    LOADI R1, 0x1111
    LOADI R2, 0x2222
    LOADI R3, 0x3333

    ; Push registers onto stack
    PUSH R1
    PUSH R2
    PUSH R3

    ; Call level 1 subroutine
    CALL func_alpha

    ; Restore stack in reverse order
    POP R6
    POP R5
    POP R4

    HLT

func_alpha:
    LOADI R7, 0xAAAA
    PUSH R7

    ; Nested call to level 2 subroutine
    CALL func_beta

    POP R8
    RET

func_beta:
    LOADI R9, 0xBBBB
    LOADI R10, 0x0005
    ADD R9, R10
    RET
