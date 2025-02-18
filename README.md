# GBA emu
This is my attempt to make a GBA emulator without using any tutorials, only by reading documentation.

## Instruction pipeline
```
Fetch       Instruction fetched from memory
  |
  v
Decode      Decoding of registers used in instructions
  |
  v
Execute     Register(s) read from register bank
            Perform shift and ALU operations
            Write register(s) back to register bank
```

## Condition flags
I had many errors dealing with the differences between C and V flags. Here are the key differences:
- C flag: Deals with unsigned arithmetic. It is set when there is an overflow in an unsigned sense (carry out of the register). In other words, if the new value does not fit in the register size, this flag is set.
- V flag: Deals with signed arithmetic. See https://teaching.idallen.com/dat2343/10f/notes/040_overflow.txt

## References
- [GBATEK](https://problemkaputt.de/gbatek.htm)
- [CowBite](https://www.cs.rit.edu/~tjh8300/CowBite/CowBiteSpec.htm)
- [ARM7TDMI Reference Manual](https://ww1.microchip.com/downloads/en/DeviceDoc/DDI0029G_7TDMI_R3_trm.pdf)
- [ARM Architecture Reference Manual](https://www.intel.com/programmable/technical-pdfs/654202.pdf)
- [ARM7TDMI Data Sheet](https://www.dwedit.org/files/ARM7TDMI.pdf)
- [GBA Architecture](https://www.copetti.org/writings/consoles/game-boy-advance/)
- [BIOS File](https://archive.org/details/gba_bios_202206)
- [ARM Simulator to validate how instructions change the Status Register](https://cpulator.01xz.net/?sys=arm)
