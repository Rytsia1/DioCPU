; ===================================================
; DioCPU V2 - Full ALU & Flags Demonstration
; Demonstrates arithmetic, logic, and bitwise operations
; for the Datapath & ALU Viewer.
; ===================================================

start:
    ; 1. Arithmetic & Zero Flag (ZF)
    LOADI R1, 100
    LOADI R2, 100
    SUB R1, R2           ; R1 = 0, ZF = 1

    ; 2. Carry Flag (CF) via Addition Overflow
    LOADI R3, 0xFFFF
    LOADI R4, 1
    ADD R3, R4           ; R3 = 0, CF = 1, ZF = 1

    ; 3. Sign Flag (SF)
    LOADI R5, 5
    LOADI R6, 10
    SUB R5, R6           ; R5 = 0xFFFB (-5), SF = 1

    ; 4. Multiplication & Division
    LOADI R7, 25
    LOADI R8, 4
    MUL R7, R8           ; R7 = 100

    LOADI R9, 10
    DIV R7, R9           ; R7 = 10

    ; 5. Logic Operations
    LOADI R10, 0x00FF
    LOADI R11, 0x0F0F
    AND R10, R11         ; R10 = 0x000F

    LOADI R12, 0xF000
    OR R10, R12          ; R10 = 0xF00F

    LOADI R13, 0xFFFF
    XOR R10, R13         ; R10 = 0x0FF0

    NOT R10              ; R10 = 0xF00F

    HLT
