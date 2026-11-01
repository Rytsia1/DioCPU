; =====================================
; DIOCPU V2 TEST PROGRAM
; =====================================

start:

    LOADI R1, 0x1234
    LOADI R2, 0x0005

    STORE R1, [0x00010]

    LOAD R3, [0x00010]

    ADD R3, R2

    HLT
