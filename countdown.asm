; Program countdown Dio-8

LOAD R0, 3
LOAD R1, 1

LOOP:
    SUB R0, R1
    JNZ LOOP

HLT
