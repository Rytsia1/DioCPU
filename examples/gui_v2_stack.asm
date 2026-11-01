; =====================================
; DioCPU GUI V2 Stack & Call Test
; =====================================

    ; Test 1 & 3: PUSH registers
    LOADI R1, 0x1111
    LOADI R2, 0x2222
    LOADI R3, 0x3333

    PUSH R1
    PUSH R2
    PUSH R3

    ; Test 1: POP to different registers
    POP R4
    POP R5
    POP R6

    ; Test 2: Subroutine CALL and RET
    CALL SUBROUTINE
    HLT

SUBROUTINE:
    LOADI R7, 0xCAFE
    PUSH R7
    POP R8
    RET
