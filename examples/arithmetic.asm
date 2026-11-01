; Simple arithmetic program

START:
    LOADI R1, 100
    LOADI R2, 200

    ADD R1, R2

    CMP R1, R2
    JZ EQUAL

    LOADI R3, 0
    JMP END

EQUAL:
    LOADI R3, 1

END:
    HLT
