; ===================================================
; DioCPU V2 - Fibonacci Sequence Generator
; Computes first 10 Fibonacci numbers and writes them
; into Data Memory starting at address [0x00100].
; ===================================================

start:
    ; Initialize registers
    LOADI R1, 0          ; F(0) = 0
    LOADI R2, 1          ; F(1) = 1
    LOADI R3, 10         ; Loop counter (10 iterations)
    LOADI R4, 1          ; Decrement step
    LOADI R5, 0x0100     ; Data Memory destination pointer

    ; Store initial two values
    STORE R1, [0x0100]
    STORE R2, [0x0102]

fib_loop:
    ; Calculate next Fibonacci number: R6 = R1 + R2
    LOADI R6, 0
    ADD R6, R1
    ADD R6, R2

    ; Advance pointer and store result
    LOADI R7, 2
    ADD R5, R7
    STORE R6, [0x0104]   ; Store current result

    ; Shift sequence: R1 = R2, R2 = R6
    LOADI R1, 0
    ADD R1, R2
    LOADI R2, 0
    ADD R2, R6

    ; Decrement counter: R3 = R3 - 1
    SUB R3, R4
    JNZ fib_loop

    HLT
