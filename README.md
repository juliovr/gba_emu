# GBA emu
This is my attempt to make a GBA emulator without using any tutorials, only by reading documentation.

## Motivation and scope
I want to prove myself I can make a (partial) emulation of a hardware by only reading the documentation available, walking away from tutorials hell. I choose GBA because I played with it a lot when I was a kid (specially Pokemon), so it is a perfect personal motivator.

I'm not trying to make a whole GBA emulator (there already exists [mGBA](https://mgba.io/)), but to be able to simulate a great deal of the CPU and display some image. For this I use a demo-game I made a while ago (following GBADEV) using Mode 3 (video mode to treat VRAM as a bitmap, the easiest mode).


## Thinks encountered while working

### Instruction pipeline
ARM7TDMI is a pipelined processor with following steps:
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

At the beginning I thought it would not be important for the emulation perspective, but it is important when the CPU does branches calculation because it accounts for the prefetched-incremented PC.
So, the easiest thing I came up with was to execute the stages in "reverse order", i.e. call `execute()`, `decode()` and `fetch()`. Doing so, when reaches `execute()`, the PC is already 2 instructions ahead, exactly what the ARM specification states when doing branches or any other calculation that involves the PC.

### Condition flags
I had many errors dealing with the differences between C and V flags. Here are the key differences:
- C flag: Deals with unsigned arithmetic. It is set when there is an overflow in an unsigned sense (carry out of the register). In other words, if the new value does not fit in the register size, this flag is set.
- V flag: Deals with signed arithmetic. See https://teaching.idallen.com/dat2343/10f/notes/040_overflow.txt

### From BIOS to the game
I did not intended to reverse engineered the BIOS, so I download one from internet (link in references).
The main problem was that once I implemented a big portion of ARM instructions, the game does not boot up. In fact, the emulation just run into an infinite loop.
This was a headache that got me stuck for several days and even looking for the dissasembly I could not figured out how to make it work. One day, while viewing the recommendations on youtube, there was a video on reverse engineering with GHIDRA, so I thought maybe I could use it (nothing to lose, right?).
Fortunately, It was a lifesaver. The output were more clear than other tools. There wasn't just one problem, but the one that got stuck in an infinite loop was related to screen synchronization (the check if all scanlines were done). So, after implement the interrupts to update the internal registers of the count of scanlines, the game finally boot up.


## References
- [GBATEK](https://problemkaputt.de/gbatek.htm)
- [CowBite](https://www.cs.rit.edu/~tjh8300/CowBite/CowBiteSpec.htm)
- [ARM7TDMI Reference Manual](https://ww1.microchip.com/downloads/en/DeviceDoc/DDI0029G_7TDMI_R3_trm.pdf)
- [ARM Architecture Reference Manual](https://www.intel.com/programmable/technical-pdfs/654202.pdf)
- [ARM7TDMI Data Sheet](https://www.dwedit.org/files/ARM7TDMI.pdf)
- [GBA Architecture](https://www.copetti.org/writings/consoles/game-boy-advance/)
- [BIOS File](https://archive.org/details/gba_bios_202206)
- [GBADEV: tutorials to make games](https://gbadev.net/tonc/hardware.html)
- [ARM Simulator to validate how instructions change the Status Register](https://cpulator.01xz.net/?sys=arm)
- [GHIDRA](https://ghidra-sre.org/)
