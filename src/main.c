#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


#define KILOBYTE (1024)
#define MEGABYTE (1024*1024)

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;


// typedef struct CPU {

// } CPU;

// TODO: is it necessary to make fields for the "Not used" data to make the load simpler?
typedef struct GBAMemory {
    // General Internal Memory
    u8 bios_system_rom[16*KILOBYTE];
    // 00004000-01FFFFFF   Not used
    u8 ewram[256*KILOBYTE];
    // 02040000-02FFFFFF   Not used
    u8 iwram[32*KILOBYTE];
    // 03008000-03FFFFFF   Not used
    u8 io_registers[1*KILOBYTE];
    // 04000400-04FFFFFF   Not used

    // Internal Display Memory
    u8 bg_obj_palette_ram[1*KILOBYTE];
    // 05000400-05FFFFFF   Not used
    u8 vram[96*KILOBYTE];
    // 06018000-06FFFFFF   Not used
    u8 oam_obj_attributes[1*KILOBYTE];
    // 07000400-07FFFFFF   Not used

    // External Memory (Game Pak)
    u8 game_pak_rom[32*MEGABYTE];
    // 0A000000-0BFFFFFF   Game Pak ROM/FlashROM (max 32MB) - Wait State 1
    // 0C000000-0DFFFFFF   Game Pak ROM/FlashROM (max 32MB) - Wait State 2
    u8 game_pak_ram[64*MEGABYTE];
    // 0E010000-0FFFFFFF   Not used

    // Unused Memory Area
    // 10000000-FFFFFFFF   Not used (upper 4bits of address bus unused)

} GBAMemory;

GBAMemory memory;

static int
load_cartridge_into_memory(char *filename)
{
    FILE *file;
    if (fopen_s(&file, filename, "rb")) {
        fprintf(stderr, "[ERROR]: Could not load file \"%s\"\n", filename);
        return 1;
    } else {
        fseek(file, 0, SEEK_END);
        int size = ftell(file);
        fseek(file, 0, SEEK_SET);

        fread(memory.game_pak_rom, size, 1, file);
        
        fclose(file);
    }

    return 0;
}


/*
 * Example for rom_entry_point:
 * cond branch_instruction L                     offset
 * 1110                101 0   000000000000000000101110
 */
typedef struct CartridgeHeader {
    u32 rom_entry_point;    // Where the entry point is. Usually is a branch instruction ("B <label>")
    u8 nintendo_logo[156];
    char game_title[12];
    char game_code[4];      // Short-name for the game
    char marker_code[2];
    u8 fixed_value;         // must be 96h, required!
    u8 main_unit_code;      // 00h for current GBA models
    u8 device_type;         // usually 00h
    u8 reserved_1[7];
    u8 software_version;    // usually 00h
    u8 complement_check;
    u8 reserved_2[2];
    
    // Additional Multiboot Header Entries
    u32 ram_entry_point;
    u8 boot_mode;
    u8 slave_id_number;
    u8 not_used[26];
    u32 joybus_entry_point;
} CartridgeHeader;



#define INSTRUCTION_BRANCH 0b1010000000000000000000000000

static void
decode(u8 *content)
{
    // TODO: Wrap in a while loop
    u32 *instruction = (u32 *)content; // TODO: should this be u16 or u32?
    if ((*instruction & INSTRUCTION_BRANCH) == INSTRUCTION_BRANCH) {
        int L = *instruction & (1 << 24);
        if (L) {
            // TODO: implement logic
            printf("BL\n");
        }

        int offset = *instruction & 0xFFFFFF;
        printf("Instruction Branch: offset = %d\n", offset);

        
    }
}

int main()
{
    char *filename = "../Donkey Kong Country 2.gba";
    int error = load_cartridge_into_memory(filename);
    if (error) {
        exit(1);
    }
    
    CartridgeHeader *header = (CartridgeHeader *)memory.game_pak_rom;
    printf("fixed_value = 0x%x, expected = 0x96\n", header->fixed_value);

    decode(memory.game_pak_rom);

    return 0;
}
