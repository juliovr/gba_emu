#ifndef MEMORY_H
#define MEMORY_H

typedef struct GBAMemory {
    // General Internal Memory
    u8 bios_system_rom[16*KILOBYTE];
    u8 ewram[256*KILOBYTE];
    u8 iwram[32*KILOBYTE];
    u8 io_registers[1*KILOBYTE];

    // Internal Display Memory
    u8 bg_obj_palette_ram[1*KILOBYTE];
    u8 vram[96*KILOBYTE];
    u8 oam_obj_attributes[1*KILOBYTE];

    // External Memory (Game Pak)
    u8 game_pak_rom[32*MEGABYTE];
    u8 game_pak_ram[64*MEGABYTE];
} GBAMemory;


u8 *
get_memory_at(CPU *cpu, GBAMemory *gba_memory, u32 at)
{
    // General Internal Memory
    if (at <= 0x00003FFF) return (gba_memory->bios_system_rom + (at - 0x00000000));
    if (at <= 0x01FFFFFF) assert(!"Invalid memory");

    if (at <= 0x0203FFFF) return (gba_memory->ewram + (at - 0x02000000));
    if (at <= 0x02FFFFFF) {
        assert(!"Check this");
        at &= 0xFFFFF;
        at %= 0x40000;

        return (gba_memory->ewram + at);
    }

    if (at <= 0x03007FFF) return (gba_memory->iwram + (at - 0x03000000));
    if (at <= 0x03FFFFFF) {
        at &= 0xFFFF;
        at %= 0x8000;

        return (gba_memory->iwram + at);
    }

    if (at <= 0x040003FE) return (gba_memory->io_registers + (at - 0x04000000));
    if (at <= 0x04FFFFFF) {
        if (at == 0x04000410) {
            return 0;
        }

        assert(!"The word at 0x04000800 (only!) is mirrored every 0x10000 bytes from 0x04000000 - 0x04FFFFFF.");
    }

    // Internal Display Memory
    if (at <= 0x050003FF) return (gba_memory->bg_obj_palette_ram + (at - 0x05000000));
    if (at <= 0x05FFFFFF) {
        assert(!"Check this");
        at &= 0xFFF;
        at %= 0x400;

        return (gba_memory->bg_obj_palette_ram + at);
    }

    if (at <= 0x06017FFF) return (gba_memory->vram + (at - 0x06000000));
    if (at <= 0x06FFFFFF) {
        assert(!"Check this");
        at &= 0xFFFFF;
        at %= 0x20000;

        return (gba_memory->vram + at);
    }

    if (at <= 0x070003FF) return (gba_memory->oam_obj_attributes + (at - 0x07000000));
    if (at <= 0x07FFFFFF) {
        // assert(!"Check this");
        at &= 0xFFF;
        at %= 0x400;

        return (gba_memory->oam_obj_attributes + at);
    }

    // External Memory (Game Pak)
    if (at <= 0x09FFFFFF) return (gba_memory->game_pak_rom + (at - 0x08000000));
    if (at <= 0x0BFFFFFF) {
        at -= 0x0A000000;

        return (gba_memory->game_pak_rom + at);
    }
    if (at <= 0x0DFFFFFF) {
        at -= 0x0C000000;

        return (gba_memory->game_pak_rom + at);
    }
    
    if (at <= 0x0E00FFFF) return (gba_memory->game_pak_ram + (at - 0x0E000000));


    assert(!"Invalid memory");
    return 0;
}

#endif // MEMORY_H