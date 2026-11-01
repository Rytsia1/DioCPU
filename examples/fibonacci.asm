; =====================================
; DioCPU Example Program
; Simple Fibonacci-like calculation
; =====================================

    LOADI R1, 0
    LOADI R2, 1

    LOADI R3, 10

LOOP:
    ADD R1, R2
    SUB R3, R4

    JNZ LOOP

    HLT
