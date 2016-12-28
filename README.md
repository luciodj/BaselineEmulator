PIC16F54 (baseline PIC) Emulator 
==
(PIC32 project using xc32 compiler)

This is a simple Instruction Emulator for the (PIC16F54) baseline original (12-bit) architecture.

* For debugging purposes, logging is provided on a serial port (UART1)
* Stricter - Timing can be enabled to limit execution speed to Tcy provided by TMR1
* No attempt was made at optimising the code execution
* Testing of correct instruction execution has been extremely limited (see .ROM binary file - edit using any text editor)

## Linked Projects
* [A cycle by cycle emulator (in Python)](https://github.com/luciodj/CycleByCyclePIC)
