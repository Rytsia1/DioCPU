; =====================================
; DioCPU GUI V2 Cycle Animation Test
; =====================================

    LOADI R1, 10
    LOADI R2, 20

    ADD R1, R2

    CMP R1, R2

    JNZ NOT_EQUAL

    LOADI R3, 1
    JMP FINISH

NOT_EQUAL:
    LOADI R3, 0

FINISH:
    HLT
