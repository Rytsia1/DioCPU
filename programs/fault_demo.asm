; ===================================================
; DioCPU V2 - Fault Handling Demonstration
; Demonstrates Divide-by-Zero fault detection and
; automatic safe halting for the GUI controller.
; ===================================================

start:
    LOADI R1, 100
    LOADI R2, 0

    ; Dividing by zero triggers CPU_FAULT_DIVIDE_BY_ZERO
    DIV R1, R2

    ; The instructions below will NOT be reached
    LOADI R3, 0xFFFF
    HLT
