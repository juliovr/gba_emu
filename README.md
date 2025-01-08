# GBA emu
This is my attempt to make a GBA emulator without using any tutorials, only by reading documentation.

# Instruction pipeline
Fetch       Instruction fetched from memory
  |
  v
Decode      Decoding of registers used in instructions
  |
  v
Execute     Register(s) read from register bank
            Perform shift and ALU operations
            Write register(s) back to register bank

# References
- [GBATEK](https://problemkaputt.de/gbatek.htm)
- [ARM7TDMI Reference Manual](https://ww1.microchip.com/downloads/en/DeviceDoc/DDI0029G_7TDMI_R3_trm.pdf)
- [ARM Architecture Reference Manual](https://www.intel.com/programmable/technical-pdfs/654202.pdf)
- [ARM7TDMI Data Sheet](https://www.dwedit.org/files/ARM7TDMI.pdf)
