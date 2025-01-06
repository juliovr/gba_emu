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


typedef struct CPU {
    u32 r0;
    u32 r1;
    u32 r2;
    u32 r3;
    u32 r4;
    u32 r5;
    u32 r6;
    u32 r7;
    u32 r8;
    u32 r9;
    u32 r10;
    u32 r11;
    u32 r12;
    u32 r13;
    union {
        u32 r14;
        u32 lr;
    };
    union {
        u32 r15;
        u32 pc;
    };

    u32 cpsr; // Current Program Status Register
    // TODO: how to store the banked registers.
} CPU;

CPU cpu = {0};

//
// Control Bits
//
#define MODE_BITS                       ((cpu.cpsr >> 0) && 0b11111)
#define STATE_BIT                       ((cpu.cpsr >> 5) && 1)
#define FIQ_DISABLE                     ((cpu.cpsr >> 6) && 1)
#define IRQ_DISABLE                     ((cpu.cpsr >> 7) && 1)

//
// Codition Code Flags
//
#define CONDITION_OVERFLOW              ((cpu.cpsr >> 28) && 1)
#define CONDITION_CARRY                 ((cpu.cpsr >> 29) && 1)
#define CONDITION_ZERO                  ((cpu.cpsr >> 30) && 1)
#define CONDITION_NEGATIVE_OR_LESS_THAN ((cpu.cpsr >> 31) && 1)


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

GBAMemory memory = {0};

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



#define INSTRUCTION_FORMAT_DATA_PROCESSING                              (1 << 25)
#define INSTRUCTION_FORMAT_MULTIPLY                                     (0b0000000000000000000010010000)
#define INSTRUCTION_FORMAT_MULTIPLY_LONG                                (0b0000100000000000000010010000)
#define INSTRUCTION_FORMAT_SINGLE_DATA_SWAP                             (0b0001000000000000000010010000)
#define INSTRUCTION_FORMAT_BRANCH_AND_EXCHANGE                          (0b0001001011111111111100010000)
#define INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET       (0b0000000000000000000010010000)
#define INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_IMMEDIATE_OFFSET      (0b0000010000000000000010010000)
#define INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER                         (0b11 << 25)
#define INSTRUCTION_FORMAT_UNDEFINED                                    ((0b11 << 25) | (1 << 4))
#define INSTRUCTION_FORMAT_BLOCK_DATA_TRANSFER                          (1 << 27)
#define INSTRUCTION_FORMAT_BRANCH                                       (0b101 << 25)
#define INSTRUCTION_FORMAT_COPROCESSOR_DATA_TRANSFER                    (0b11 << 26)
#define INSTRUCTION_FORMAT_COPROCESSOR_DATA_OPERATION                   (0b111 << 25)
#define INSTRUCTION_FORMAT_COPROCESSOR_REGISTER_TRANSFER                ((0b111 << 25) | (1 << 4))
#define INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT                           (0b1111 << 24)


typedef enum InstructionType {
    INSTRUCTION_NONE,

    INSTRUCTION_B,
} InstructionType;

static void
decode_debug()
{
    u32 *instructions = (u32 *)memory.game_pak_rom;
    cpu.pc = 0;
    
    // TODO: Wrap in a while loop
    while (1) {
        u32 instruction = instructions[cpu.pc++]; // TODO: should this be u16 or u32?
        // cpu.pc++; // Because of pipelining should increment twice, but how do I execute this "skipped" instruction?
        
        if ((instruction & INSTRUCTION_FORMAT_BRANCH) == INSTRUCTION_FORMAT_BRANCH) {
            int L = instruction & (1 << 24);
            if (L) {
                // TODO: implement logic
                printf("BL\n");
            }

            int offset = instruction & 0xFFFFFF;
            printf("Instruction Branch: offset = %d\n", offset);

            cpu.pc++;
            cpu.pc += offset;
        } else if ((instruction & INSTRUCTION_FORMAT_DATA_PROCESSING) == INSTRUCTION_FORMAT_DATA_PROCESSING) {
            printf("Data processing: 0x%x\n", instruction);
        } else {
            fprintf(stderr, "Instruction unimplemented: 0x%x\n", instruction);
            return;
        }

        int x = 5;
    }
}

u32 current_instruction;

typedef struct Instruction {
    InstructionType type;
    int offset;
    int L;
} Instruction;

Instruction decoded_instruction;

static void
execute()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_NONE: return;

        case INSTRUCTION_B: {
            if (decoded_instruction.L) {
                cpu.lr = cpu.pc - 1;
            }

            cpu.pc += decoded_instruction.offset;
        } break;
    }

    decoded_instruction = (Instruction){0};
}

static void
decode()
{
    if (current_instruction == 0) return;

    // if ((current_instruction & INSTRUCTION_FORMAT_BRANCH) == INSTRUCTION_FORMAT_BRANCH) {
    //     decoded_instruction = (Instruction) {
    //         .type = INSTRUCTION_B,
    //         .offset = current_instruction & 0xFFFFFF,
    //         .L = current_instruction & (1 << 24),
    //     };
    // } else if ((current_instruction & INSTRUCTION_FORMAT_DATA_PROCESSING) == INSTRUCTION_FORMAT_DATA_PROCESSING) {
    //     printf("Data processing: 0x%x\n", current_instruction);
    // } else {
    //     fprintf(stderr, "Instruction unimplemented: 0x%x\n", current_instruction);
    //     return;
    // }

    if ((current_instruction &INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT) == INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT) {
        printf("INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_COPROCESSOR_REGISTER_TRANSFER) == INSTRUCTION_FORMAT_COPROCESSOR_REGISTER_TRANSFER) {
        printf("INSTRUCTION_FORMAT_COPROCESSOR_REGISTER_TRANSFER: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_COPROCESSOR_DATA_OPERATION) == INSTRUCTION_FORMAT_COPROCESSOR_DATA_OPERATION) {
        printf("INSTRUCTION_FORMAT_COPROCESSOR_DATA_OPERATION: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_COPROCESSOR_DATA_TRANSFER) == INSTRUCTION_FORMAT_COPROCESSOR_DATA_TRANSFER) {
        printf("INSTRUCTION_FORMAT_COPROCESSOR_DATA_TRANSFER: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_BRANCH) == INSTRUCTION_FORMAT_BRANCH) {
        printf("INSTRUCTION_FORMAT_BRANCH: 0x%x\n", current_instruction);
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_B,
            .offset = current_instruction & 0xFFFFFF,
            .L = current_instruction & (1 << 24),
        };
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_BLOCK_DATA_TRANSFER) == INSTRUCTION_FORMAT_BLOCK_DATA_TRANSFER) {
        printf("INSTRUCTION_FORMAT_BLOCK_DATA_TRANSFER: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_UNDEFINED) == INSTRUCTION_FORMAT_UNDEFINED) {
        printf("INSTRUCTION_FORMAT_UNDEFINED: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER) == INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER) {
        printf("INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_IMMEDIATE_OFFSET) == INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_IMMEDIATE_OFFSET) {
        printf("INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_IMMEDIATE_OFFSET: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET) == INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET) {
        printf("INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_BRANCH_AND_EXCHANGE) == INSTRUCTION_FORMAT_BRANCH_AND_EXCHANGE) {
        printf("INSTRUCTION_FORMAT_BRANCH_AND_EXCHANGE: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_SINGLE_DATA_SWAP) == INSTRUCTION_FORMAT_SINGLE_DATA_SWAP) {
        printf("INSTRUCTION_FORMAT_SINGLE_DATA_SWAP: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_MULTIPLY_LONG) == INSTRUCTION_FORMAT_MULTIPLY_LONG) {
        printf("INSTRUCTION_FORMAT_MULTIPLY_LONG: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_MULTIPLY) == INSTRUCTION_FORMAT_MULTIPLY) {
        printf("INSTRUCTION_FORMAT_MULTIPLY: 0x%x\n", current_instruction);
    }
    else if ((current_instruction &INSTRUCTION_FORMAT_DATA_PROCESSING) == INSTRUCTION_FORMAT_DATA_PROCESSING) {
        printf("INSTRUCTION_FORMAT_DATA_PROCESSING: 0x%x\n", current_instruction);
    } else {
        fprintf(stderr, "Instruction unknown: 0x%x\n", current_instruction);
        exit(1);
    }

    current_instruction = 0;
}

static void
fetch()
{
    current_instruction = ((u32 *)memory.game_pak_rom)[cpu.pc++];
}

static void
process_instructions()
{
    u32 *instructions = (u32 *)memory.game_pak_rom;

    // Emulating pipelining.
    while (1) {
        // TODO: in the future, check if this separation works (because of the order they are executed).
        execute();
        decode();
        fetch();
    }
}

#if 0
static void
process_instructions()
{
    u32 *instructions = (u32 *)memory.game_pak_rom;

    while (1) {
        // Fetch
        current_instruction = instructions[cpu.pc++];

        // Decode
        if ((current_instruction & INSTRUCTION_FORMAT_BRANCH) == INSTRUCTION_FORMAT_BRANCH) {
            decoded_instruction = (Instruction) {
                .type = INSTRUCTION_B,
                .offset = current_instruction & 0xFFFFFF,
                .L = current_instruction & (1 << 24),
            };
        } else if ((current_instruction & INSTRUCTION_FORMAT_DATA_PROCESSING) == INSTRUCTION_FORMAT_DATA_PROCESSING) {
            printf("Data processing\n");
        } else {
            fprintf(stderr, "Instruction unimplemented: 0x%x\n", current_instruction);
            return;
        }

        // Execute
        switch (decoded_instruction.type) {
            case INSTRUCTION_B: {
                if (decoded_instruction.L) {
                    cpu.lr = cpu.pc - 1;
                }

                cpu.pc += decoded_instruction.offset;
            } break;
        }

        decoded_instruction = (Instruction){0};
    }
}
#endif

int main()
{
    char *filename = "../Donkey Kong Country 2.gba";
    int error = load_cartridge_into_memory(filename);
    if (error) {
        exit(1);
    }
    
    CartridgeHeader *header = (CartridgeHeader *)memory.game_pak_rom;
    printf("fixed_value = 0x%x, expected = 0x96\n", header->fixed_value);

    process_instructions();
    // decode_debug();

    return 0;
}
