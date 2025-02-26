#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <math.h>
#include "../include/raylib.h"
#include "types.h"


#ifdef _DEBUG_PRINT
    #ifdef _LINUX
        #define DEBUG_PRINT(format, ...) printf(format, ##__VA_ARGS__)
    #else
        #define DEBUG_PRINT(format, ...) printf(format, __VA_ARGS__)
    #endif
#else
#define DEBUG_PRINT(...)
#endif


CPU gba_cpu = {0};
CPU *cpu = &gba_cpu;

GBAMemory memory = {0};

static u8 paused = 0;


//
// Control Bits
//
#define CONTROL_BITS_MODE       ((cpu->cpsr >> 0) & 0b11111)    /* Mode bits */
#define CONTROL_BITS_T          ((cpu->cpsr >> 5) & 1)          /* State bit (in Thumb mode) */
#define CONTROL_BITS_F          ((cpu->cpsr >> 6) & 1)          /* FIQ disable */
#define CONTROL_BITS_I          ((cpu->cpsr >> 7) & 1)          /* IRQ disable */

#define IN_THUMB_MODE           CONTROL_BITS_T


static void
set_mode(u8 bits)
{
    cpu->cpsr = (cpu->cpsr & ((u32)~(0b11111))) | ((bits) & 0b11111);
}

static void
set_control_bit_T(u8 bit)
{
#if _DEBUG
    if (IN_THUMB_MODE && bit == 0) {
        DEBUG_PRINT("    Changing mode: THUMB -> ARM\n");
    } else if (!IN_THUMB_MODE && bit == 1) {
        DEBUG_PRINT("    Changing mode: ARM -> THUMB\n");
    }
#endif

    cpu->cpsr = ((cpu->cpsr & ~(1 << 5)) | ((bit) & 1) << 5);
}

static void
set_control_bit_F(u8 bit)
{
    cpu->cpsr = ((cpu->cpsr & ~(1 << 6)) | ((bit) & 1) << 6);
}

static void
set_control_bit_I(u8 bit)
{
    cpu->cpsr = ((cpu->cpsr & ~(1 << 7)) | ((bit) & 1) << 7);
}


//
// Codition Code Flags
//
#define CONDITION_V             ((cpu->cpsr >> 28) & 1)     /* Overflow */
#define CONDITION_C             ((cpu->cpsr >> 29) & 1)     /* Carry or borrow extended */
#define CONDITION_Z             ((cpu->cpsr >> 30) & 1)     /* Zero */
#define CONDITION_N             ((cpu->cpsr >> 31) & 1)     /* Negative or less than */

static void
set_condition_V(u8 bit)
{
    cpu->cpsr = ((cpu->cpsr & ~(1 << 28)) | ((bit) & 1) << 28);
}

static void
set_condition_C(u8 bit)
{
    cpu->cpsr = ((cpu->cpsr & ~(1 << 29)) | ((bit) & 1) << 29);
}

static void
set_condition_Z(u8 bit)
{
    cpu->cpsr = ((cpu->cpsr & ~(1 << 30)) | ((bit) & 1) << 30);
}

static void
set_condition_N(u8 bit)
{
    cpu->cpsr = ((cpu->cpsr & ~(1 << 31)) | ((bit) & 1) << 31);
}

static void
set_overflow_addition(u32 a, u32 b, u32 result)
{
    u8 bit = (((a >> 31) == 0) && ((b >> 31) == 0) && ((result >> 31) == 1)) ||
             (((a >> 31) == 1) && ((b >> 31) == 1) && ((result >> 31) == 0));
    set_condition_V(bit);
}

static void
set_overflow_subtract(u32 a, u32 b, u32 result)
{
    u8 bit = (((a >> 31) == 0) && ((b >> 31) == 1) && ((result >> 31) == 1)) ||
             (((a >> 31) == 1) && ((b >> 31) == 0) && ((result >> 31) == 0));
    set_condition_V(bit);
}

static int
load_cartridge_into_memory(char *filename)
{
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "[ERROR]: Could not load file \"%s\"\n", filename);
        return 1;
    } else {
        fseek(file, 0, SEEK_END);
        int size = ftell(file);
        
        assert(size <= 32*MEGABYTE);
        
        fseek(file, 0, SEEK_SET);
        fread(memory.game_pak_rom, size, 1, file);

        fclose(file);
    }

    return 0;
}

static int
load_bios_into_memory()
{
    char *filename = "src/gba_bios.bin";
    FILE *file = fopen(filename, "rb");
    if (file == NULL) {
        fprintf(stderr, "[ERROR]: Could not load file \"%s\"\n", filename);
        return 1;
    } else {
        fseek(file, 0, SEEK_END);
        int size = ftell(file);
        fseek(file, 0, SEEK_SET);

        assert(size == sizeof(memory.bios_system_rom));

        fread(memory.bios_system_rom, size, 1, file);
        
        fclose(file);
    }

    return 0;
}


// I/O Registers
#define IO_DISPCNT      ((u16 *)get_memory_at(cpu, &memory, 0x4000000))
#define IO_DISPSTAT     ((u16 *)get_memory_at(cpu, &memory, 0x4000004))
#define IO_VCOUNT       ((u16 *)get_memory_at(cpu, &memory, 0x4000006))
#define IO_BG0CNT       ((u16 *)get_memory_at(cpu, &memory, 0x4000008))
#define IO_BG1CNT       ((u16 *)get_memory_at(cpu, &memory, 0x400000A))
#define IO_BG2CNT       ((u16 *)get_memory_at(cpu, &memory, 0x400000C))
#define IO_BG3CNT       ((u16 *)get_memory_at(cpu, &memory, 0x400000E))
#define IO_BG0HOFS      ((u16 *)get_memory_at(cpu, &memory, 0x4000010))
#define IO_BG0VOFS      ((u16 *)get_memory_at(cpu, &memory, 0x4000012))
#define IO_BG1HOFS      ((u16 *)get_memory_at(cpu, &memory, 0x4000014))
#define IO_BG1VOFS      ((u16 *)get_memory_at(cpu, &memory, 0x4000016))
#define IO_BG2HOFS      ((u16 *)get_memory_at(cpu, &memory, 0x4000018))
#define IO_BG2VOFS      ((u16 *)get_memory_at(cpu, &memory, 0x400001A))
#define IO_BG3HOFS      ((u16 *)get_memory_at(cpu, &memory, 0x400001C))
#define IO_BG3VOFS      ((u16 *)get_memory_at(cpu, &memory, 0x400001E))
#define IO_BG2PA        ((u16 *)get_memory_at(cpu, &memory, 0x4000020))
#define IO_BG2PB        ((u16 *)get_memory_at(cpu, &memory, 0x4000022))
#define IO_BG2PC        ((u16 *)get_memory_at(cpu, &memory, 0x4000024))
#define IO_BG2PD        ((u16 *)get_memory_at(cpu, &memory, 0x4000026))
#define IO_BG2X         ((u32 *)get_memory_at(cpu, &memory, 0x4000028))
#define IO_BG2Y         ((u32 *)get_memory_at(cpu, &memory, 0x400002C))
#define IO_BG3PA        ((u16 *)get_memory_at(cpu, &memory, 0x4000030))
#define IO_BG3PB        ((u16 *)get_memory_at(cpu, &memory, 0x4000032))
#define IO_BG3PC        ((u16 *)get_memory_at(cpu, &memory, 0x4000034))
#define IO_BG3PD        ((u16 *)get_memory_at(cpu, &memory, 0x4000036))
#define IO_BG3X         ((u32 *)get_memory_at(cpu, &memory, 0x4000038))
#define IO_BG3Y         ((u32 *)get_memory_at(cpu, &memory, 0x400003C))
#define IO_WIN0H        ((u16 *)get_memory_at(cpu, &memory, 0x4000040))
#define IO_WIN1H        ((u16 *)get_memory_at(cpu, &memory, 0x4000042))
#define IO_WIN0V        ((u16 *)get_memory_at(cpu, &memory, 0x4000044))
#define IO_WIN1V        ((u16 *)get_memory_at(cpu, &memory, 0x4000046))
#define IO_WININ        ((u16 *)get_memory_at(cpu, &memory, 0x4000048))
#define IO_WINOUT       ((u16 *)get_memory_at(cpu, &memory, 0x400004A))
#define IO_MOSAIC       ((u16 *)get_memory_at(cpu, &memory, 0x400004C))
#define IO_BLDCNT       ((u16 *)get_memory_at(cpu, &memory, 0x4000050))
#define IO_BLDALPHA     ((u16 *)get_memory_at(cpu, &memory, 0x4000052))
#define IO_BLDY         ((u16 *)get_memory_at(cpu, &memory, 0x4000054))

#define VRAM_ADDRESS    (0x6000000)
#define VRAM            ((u16 *)get_memory_at(cpu, &memory, VRAM_ADDRESS))


typedef struct DisplayControlRegister {
    u8 video_mode;
    u8 gbc_mode;
    u8 bitmap_address;
    u8 hblank_processing;
    u8 sprite_dimension;
    u8 force_blank;
    u8 enable_bg0;
    u8 enable_bg1;
    u8 enable_bg2;
    u8 enable_bg3;
    u8 enable_oam;
    u8 enable_window_0;
    u8 enable_window_1;
    u8 enable_sprite_windows;
} DisplayControlRegister;

static void
parse_display_control_register(DisplayControlRegister *display_control_register, u16 reg)
{
    display_control_register->video_mode            = (reg >> 0) & 0b111;
    display_control_register->gbc_mode              = (reg >> 3) & 1;
    display_control_register->bitmap_address        = (reg >> 4) & 1;
    display_control_register->hblank_processing     = (reg >> 5) & 1;
    display_control_register->sprite_dimension      = (reg >> 6) & 1;
    display_control_register->force_blank           = (reg >> 7) & 1;
    display_control_register->enable_bg0            = (reg >> 8) & 1;
    display_control_register->enable_bg1            = (reg >> 9) & 1;
    display_control_register->enable_bg2            = (reg >> 10) & 1;
    display_control_register->enable_bg3            = (reg >> 11) & 1;
    display_control_register->enable_oam            = (reg >> 12) & 1;
    display_control_register->enable_window_0       = (reg >> 13) & 1;
    display_control_register->enable_window_1       = (reg >> 14) & 1;
    display_control_register->enable_sprite_windows = (reg >> 15) & 1;
}

typedef struct BackgroundControl {
    u8 priority;
    u8 address_character_tile_data;
    u8 mosaic_effect;
    u8 color_palette;
    u8 address_character_tile_map;
    u8 screen_over;
    u8 tile_map_size;
} BackgroundControl;

static void
parse_background_layer_configuration(BackgroundControl *background_control, u16 reg_io_background_control)
{
    background_control->priority                    = (reg_io_background_control >> 0) & 0b11;
    background_control->address_character_tile_data = (reg_io_background_control >> 2) & 0b11;
    background_control->mosaic_effect               = (reg_io_background_control >> 6) & 1;
    background_control->color_palette               = (reg_io_background_control >> 7) & 1;
    background_control->address_character_tile_map  = (reg_io_background_control >> 8) & 0b11111;
    background_control->screen_over                 = (reg_io_background_control >> 13) & 1;
    background_control->tile_map_size               = (reg_io_background_control >> 14) & 0b11;
}

static void
print_background_control(BackgroundControl *background_control, char *name)
{
    printf("%s:\n", name);
    printf("  priority = 0x%08X\n", background_control->priority);
    printf("  address_character_tile_data = 0x%08X\n", background_control->address_character_tile_data);
    printf("  mosaic_effect = 0x%08X\n", background_control->mosaic_effect);
    printf("  color_palette = 0x%08X\n", background_control->color_palette);
    printf("  address_character_tile_map = 0x%08X\n", background_control->address_character_tile_map);
    printf("  screen_over = 0x%08X\n", background_control->screen_over);
    printf("  tile_map_size = 0x%08X\n", background_control->tile_map_size);
}

static void
init_gba()
{
    memset(&gba_cpu, 0, sizeof(CPU));
    memset(&memory, 0, sizeof(GBAMemory));

    cpu->r13 = 0x03007F00;
    cpu->cpsr = 0x1F;
    cpu->pc = 0;
    // cpu->pc = 0x08000000;

    load_bios_into_memory();


    //
    // Set default values for IO registers
    //

    // SOUNDBIAS - Sound PWM Control
    // This register controls the final sound output. The default setting is 0200h
    *(u16 *)get_memory_at(cpu, &memory, 0x04000088) = 0x0200;

    *IO_BG2PA = 0x0100;
    *IO_BG2PD = 0x0100;
    *IO_BG3PA = 0x0100;
    *IO_BG3PD = 0x0100;
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


static void
get_instruction_type_name(InstructionType type, char *buffer)
{
    switch (type) {
        case INSTRUCTION_NONE: strcpy(buffer, "INSTRUCTION_NONE"); break;
        case INSTRUCTION_B: strcpy(buffer, "INSTRUCTION_B"); break;
        case INSTRUCTION_BX: strcpy(buffer, "INSTRUCTION_BX"); break;
        case INSTRUCTION_AND: strcpy(buffer, "INSTRUCTION_AND"); break;
        case INSTRUCTION_EOR: strcpy(buffer, "INSTRUCTION_EOR"); break;
        case INSTRUCTION_SUB: strcpy(buffer, "INSTRUCTION_SUB"); break;
        case INSTRUCTION_RSB: strcpy(buffer, "INSTRUCTION_RSB"); break;
        case INSTRUCTION_ADD: strcpy(buffer, "INSTRUCTION_ADD"); break;
        case INSTRUCTION_ADC: strcpy(buffer, "INSTRUCTION_ADC"); break;
        case INSTRUCTION_SBC: strcpy(buffer, "INSTRUCTION_SBC"); break;
        case INSTRUCTION_RSC: strcpy(buffer, "INSTRUCTION_RSC"); break;
        case INSTRUCTION_TST: strcpy(buffer, "INSTRUCTION_TST"); break;
        case INSTRUCTION_TEQ: strcpy(buffer, "INSTRUCTION_TEQ"); break;
        case INSTRUCTION_CMP: strcpy(buffer, "INSTRUCTION_CMP"); break;
        case INSTRUCTION_CMN: strcpy(buffer, "INSTRUCTION_CMN"); break;
        case INSTRUCTION_ORR: strcpy(buffer, "INSTRUCTION_ORR"); break;
        case INSTRUCTION_MOV: strcpy(buffer, "INSTRUCTION_MOV"); break;
        case INSTRUCTION_BIC: strcpy(buffer, "INSTRUCTION_BIC"); break;
        case INSTRUCTION_MVN: strcpy(buffer, "INSTRUCTION_MVN"); break;
        case INSTRUCTION_MRS: strcpy(buffer, "INSTRUCTION_MRS"); break;
        case INSTRUCTION_MSR: strcpy(buffer, "INSTRUCTION_MSR"); break;
        case INSTRUCTION_MUL: strcpy(buffer, "INSTRUCTION_MUL"); break;
        case INSTRUCTION_MLA: strcpy(buffer, "INSTRUCTION_MLA"); break;
        case INSTRUCTION_MULL: strcpy(buffer, "INSTRUCTION_MULL"); break;
        case INSTRUCTION_MLAL: strcpy(buffer, "INSTRUCTION_MLAL"); break;
        case INSTRUCTION_LDR: strcpy(buffer, "INSTRUCTION_LDR"); break;
        case INSTRUCTION_STR: strcpy(buffer, "INSTRUCTION_STR"); break;
        case INSTRUCTION_LDRH_IMM: strcpy(buffer, "INSTRUCTION_LDRH_IMM"); break;
        case INSTRUCTION_STRH_IMM: strcpy(buffer, "INSTRUCTION_STRH_IMM"); break;
        case INSTRUCTION_LDRSB_IMM: strcpy(buffer, "INSTRUCTION_LDRSB_IMM"); break;
        case INSTRUCTION_LDRSH_IMM: strcpy(buffer, "INSTRUCTION_LDRSH_IMM"); break;
        case INSTRUCTION_LDRH: strcpy(buffer, "INSTRUCTION_LDRH"); break;
        case INSTRUCTION_STRH: strcpy(buffer, "INSTRUCTION_STRH"); break;
        case INSTRUCTION_LDRSB: strcpy(buffer, "INSTRUCTION_LDRSB"); break;
        case INSTRUCTION_LDRSH: strcpy(buffer, "INSTRUCTION_LDRSH"); break;
        case INSTRUCTION_LDM: strcpy(buffer, "INSTRUCTION_LDM"); break;
        case INSTRUCTION_STM: strcpy(buffer, "INSTRUCTION_STM"); break;
        case INSTRUCTION_SWP: strcpy(buffer, "INSTRUCTION_SWP"); break;
        case INSTRUCTION_SWI: strcpy(buffer, "INSTRUCTION_SWI"); break;
        case INSTRUCTION_CDP: strcpy(buffer, "INSTRUCTION_CDP"); break;
        case INSTRUCTION_STC: strcpy(buffer, "INSTRUCTION_STC"); break;
        case INSTRUCTION_LDC: strcpy(buffer, "INSTRUCTION_LDC"); break;
        case INSTRUCTION_MCR: strcpy(buffer, "INSTRUCTION_MCR"); break;
        case INSTRUCTION_MRC: strcpy(buffer, "INSTRUCTION_MRC"); break;
    }
}

u32 current_instruction;

Instruction decoded_instruction;
Instruction last_instruction;

static int
should_execute_instruction(Condition condition)
{
    switch (condition) {
        case CONDITION_EQ: return (CONDITION_Z == 1);
        case CONDITION_NE: return (CONDITION_Z == 0);
        case CONDITION_CS: return (CONDITION_C == 1);
        case CONDITION_CC: return (CONDITION_C == 0);
        case CONDITION_MI: return (CONDITION_N == 1);
        case CONDITION_PL: return (CONDITION_N == 0);
        case CONDITION_VS: return (CONDITION_V == 1);
        case CONDITION_VC: return (CONDITION_V == 0);
        case CONDITION_HI: return (CONDITION_C == 1 && CONDITION_Z == 0);
        case CONDITION_LS: return (CONDITION_C == 0 || CONDITION_Z == 1);
        case CONDITION_GE: return (CONDITION_N == CONDITION_V);
        case CONDITION_LT: return (CONDITION_N != CONDITION_V);
        case CONDITION_GT: return (CONDITION_Z == 0 && CONDITION_N == CONDITION_V);
        case CONDITION_LE: return (CONDITION_Z == 1 || CONDITION_N != CONDITION_V);
        case CONDITION_AL: return true; // Always
        default: {
            fprintf(stderr, "Unexpected condition: %08X\n", condition);
            char type_name[64];
            get_instruction_type_name(decoded_instruction.type, type_name);
            fprintf(stderr, "Address: 0x%08X, Current instruction: 0x%08X -> type = %s\n", decoded_instruction.address, current_instruction, type_name);

            print_cpu_state(cpu);

            exit(1);
        }
    }
}


void
thumb_execute()
{
    DEBUG_PRINT("0x%08X: 0x%08X %s, cpsr = 0x%08X, cycles = %lld\n", decoded_instruction.address, decoded_instruction.encoding, get_instruction_type_string(decoded_instruction.type), cpu->cpsr, cpu->cycles);

    switch (decoded_instruction.type) {
        case INSTRUCTION_MOVE_SHIFTED_REGISTER: {
            u32 shift = decoded_instruction.offset;
            u32 value = *get_register(cpu, decoded_instruction.rs);
            u32 *rd = get_register(cpu, decoded_instruction.rd);
            
            switch (decoded_instruction.op) {
                case THUMB_SHIFT_TYPE_LOGICAL_LEFT: { // LSL
                    if (shift == 0) {
                        *rd = value;
                    } else {
                        set_condition_C((value >> (32 - shift)) & 1);
                        *rd = value << shift;
                    }

                    set_condition_Z(*rd == 0);
                    set_condition_N((*rd >> 31) & 1);
                } break;

                case THUMB_SHIFT_TYPE_LOGICAL_RIGHT: { // LSR
                    if (shift == 0) {
                        set_condition_C((value >> 31) & 1);
                        *rd = 0;
                    } else {
                        set_condition_C((value >> (shift - 1)) & 1);
                        *rd = value >> shift;
                    }
                    
                    set_condition_Z(*rd == 0);
                    set_condition_N((*rd >> 31) & 1);
                } break;

                case THUMB_SHIFT_TYPE_ARITHMETIC_RIGHT: { // ASR
                    if (shift == 0) {
                        u8 msb = (value >> 31) & 1;
                        set_condition_C(msb);

                        if (msb == 0) {
                            *rd = 0;
                        } else {
                            *rd = 0xFFFFFFFF;
                        }
                    } else {
                        set_condition_C((value >> (shift - 1)) & 1);

                        u8 msb = (value >> 31) & 1;
                        u32 msb_replicated = (-msb << (32 - shift));
                        *rd = (value >> shift) | msb_replicated;
                    }

                    set_condition_Z(*rd == 0);
                    set_condition_N((*rd >> 31) & 1);
                } break;
            }

            cpu->cycles++;
        } break;
        case INSTRUCTION_ADD_SUBTRACT: {
            u32 first_value = *get_register(cpu, decoded_instruction.rs);
            u32 second_value = (decoded_instruction.I) ? decoded_instruction.rn : *get_register(cpu, decoded_instruction.rn);
            u32 result = 0;

            u32 *rd = get_register(cpu, decoded_instruction.rd);

            if (decoded_instruction.op) { // SUB
                result = first_value - second_value;

                set_condition_C(second_value <= first_value ? 1 : 0);
                set_overflow_subtract(first_value, second_value, result);
            } else { // ADD
                result = first_value + second_value;

                set_condition_C((result < second_value) ? 1 : 0);
                // set_condition_V(((*rd & 0x80000000) == 0) && ((result & 0x80000000) == 1));
                set_overflow_addition(first_value, second_value, result);
            }

            *rd = result;
            
            set_condition_Z(result == 0);
            set_condition_N((result >> 31) & 1);

            cpu->cycles++;
        } break;
        case INSTRUCTION_MOVE_COMPARE_ADD_SUBTRACT_IMMEDIATE: {
            u32 result = 0;
            u32 *rd = get_register(cpu, decoded_instruction.rd);

            switch (decoded_instruction.op) {
                case 0: { // MOV
                    result = decoded_instruction.offset;
                    *rd = result;
                } break;
                case 1: { // CMP
                    result = *rd - decoded_instruction.offset;

                    set_condition_C((u32)decoded_instruction.offset <= *rd ? 1 : 0);
                    set_overflow_subtract(*rd, decoded_instruction.offset, result);
                } break;
                case 2: { // ADD
                    result = *rd + decoded_instruction.offset;

                    set_condition_C((result < (u32)decoded_instruction.offset) ? 1 : 0);
                    // set_condition_V(((*rd & 0x80000000) == 0) && ((result & 0x80000000) == 1));
                    set_overflow_addition(*rd, decoded_instruction.offset, result);
                    
                    *rd = result;
                } break;
                case 3: { // SUB
                    result = *rd - decoded_instruction.offset;

                    set_condition_C((u32)decoded_instruction.offset <= *rd ? 1 : 0);
                    set_overflow_subtract(*rd, decoded_instruction.offset, result);
                    
                    *rd = result;
                } break;
            }

            set_condition_Z(result == 0);
            set_condition_N((result >> 31) & 1);

            cpu->cycles++;
        } break;
        case INSTRUCTION_ALU_OPERATIONS: {
            u32 *rd = get_register(cpu, decoded_instruction.rd);
            u32 *rs = get_register(cpu, decoded_instruction.rs);

            u32 result = 0;
            int store_result = false;

            switch (decoded_instruction.op) {
                case 0: { // AND
                    result = *rd & *rs;
                    store_result = true;
                    cpu->cycles++;
                } break;
                case 1: { // EOR
                    result = *rd ^ *rs;
                    store_result = true;
                    cpu->cycles++;
                } break;
                case 2: { // LSL
                    u8 rs_value = (u8)*rs;
                    if (rs_value == 0) {
                        store_result = false;
                    } else if (rs_value < 32) {
                        set_condition_C((*rd >> (32 - rs_value)) & 1);
                        result = *rd << *rs;
                        store_result = true;
                    } else if (rs_value == 32) {
                        set_condition_C(*rd & 1);
                        result = 0;
                        store_result = true;
                    } else {
                        set_condition_C(0);
                        result = 0;
                        store_result = true;
                    }
                    cpu->cycles += 2;
                } break;
                case 3: { // LSR
                    u8 rs_value = (u8)*rs;
                    if (rs_value == 0) {
                        store_result = false;
                    } else if (rs_value < 32) {
                        set_condition_C((*rd >> (rs_value - 1)) & 1);
                        result = *rd >> rs_value;
                        store_result = true;
                    } else if (rs_value == 32) {
                        set_condition_C((*rd >> 31) & 1);
                        result = 0;
                        store_result = true;
                    } else {
                        set_condition_C(0);
                        result = 0;
                        store_result = true;
                    }
                    cpu->cycles += 2;
                } break;
                case 4: { // ASR
                    u8 rs_value = (u8)*rs;
                    if (rs_value == 0) {
                        store_result = false;
                    } else if (rs_value < 32) {
                        set_condition_C((*rd >> (rs_value - 1)) & 1);

                        u8 msb = (*rd >> 31) & 1;
                        u32 msb_replicated = (-msb << (32 - rs_value));

                        result = (*rd >> rs_value) | msb_replicated;
                        store_result = true;
                    } else {
                        u8 sign = (*rd >> 31) & 1;
                        set_condition_C(sign);
                        if (sign == 0) {
                            result = 0;
                        } else {
                            result = 0xFFFFFFFF;
                        }

                        store_result = true;
                    }
                    cpu->cycles += 2;
                } break;
                case 5: { // ADC
                    result = (*rd + *rs + CONDITION_C);
                    store_result = true;

                    set_condition_C((result < *rd) ? 1 : 0); // TODO: check
                    // set_condition_V(((*rd & 0x80000000) == 0) && ((result & 0x80000000) == 1));
                    set_overflow_addition(*rd, *rs + CONDITION_C, result);
                    cpu->cycles++;
                } break;
                case 6: { // SBC
                    result = (*rd - *rs - ~(CONDITION_C));
                    store_result = true;

                    set_condition_C((result <= *rd) ? 1 : 0);
                    set_overflow_subtract(*rd, *rs - ~(CONDITION_C), result);
                    cpu->cycles++;
                } break;
                case 7: { // ROR
                    u8 rs_value = (u8)*rs;
                    if (rs_value == 0) {
                        store_result = false;
                    } else if ((rs_value & 0xF) == 0) {
                        set_condition_C((*rd >> 31) & 1);
                        store_result = false;
                    } else {
                        set_condition_C((*rd >> ((rs_value & 0xF) - 1)) & 1);

                        u8 shift = rs_value & 0xF;
                        u32 value_to_rotate = *rd & ((1 << shift) - 1);
                        u32 rotate_masked = value_to_rotate << (32 - shift);

                        result = (*rd >> shift) | rotate_masked;
                        store_result = true;
                    }
                    cpu->cycles += 2;
                } break;
                case 8: { // TST
                    result = *rd & *rs;
                    store_result = false;
                    cpu->cycles++;
                } break;
                case 9: { // NEG
                    result = 0 - *rs;
                    store_result = true;

                    set_condition_C((result <= *rd) ? 1 : 0);
                    set_overflow_subtract(0, *rs, result);
                    cpu->cycles++;
                } break;
                case 10: { // CMP
                    result = *rd - *rs;
                    store_result = false;

                    set_condition_C((result <= *rd) ? 1 : 0);
                    set_overflow_subtract(*rd, *rs, result);
                    cpu->cycles++;
                } break;
                case 11: { // CMN
                    result = *rd + *rs;
                    store_result = false;

                    set_condition_C((result < *rd) ? 1 : 0);
                    // set_condition_C(*rs <= *rd ? 1 : 0);
                    // set_condition_V(((*rd & 0x80000000) == 0) && ((result & 0x80000000) == 1)); // TODO: check
                    set_overflow_addition(*rd, *rs, result);
                    cpu->cycles++;
                } break;
                case 12: { // ORR
                    result = *rd | *rs;
                    store_result = true;
                    cpu->cycles++;
                } break;
                case 13: { // MUL
                    result = (u32)(*rd * *rs);
                    store_result = true;
                    cpu->cycles += 2;
                } break;
                case 14: { // BIC
                    result = *rd & ~*rs;
                    store_result = true;
                    cpu->cycles++;
                } break;
                case 15: { // MVN
                    result = ~*rs;
                    store_result = true;
                    cpu->cycles++;
                } break;
            }

            if (store_result) {
                *rd = result;
            }
            
            set_condition_N((result >> 31) & 1);
            set_condition_Z(result == 0);

        } break;
        case INSTRUCTION_HI_REGISTER_OPERATIONS_BRANCH_EXCHANGE: {
            // H1 and H2 are flags to use the register as a Hi register (in the range of 8-15).
            // H1 for rd; H2 for rs.
            u8 H1 = decoded_instruction.H1;
            u8 H2 = decoded_instruction.H2;
            u8 op = decoded_instruction.op;
            
            assert(!(H1 == 0 &&
                     H2 == 0 &&
                    (op == 0 || op == 1 || op == 2)));
            

            u8 rs_n = decoded_instruction.rs + (H2 * 8);
            u8 rd_n = decoded_instruction.rd + (H1 * 8);

            u32 *rs = get_register(cpu, rs_n);
            u32 *rd = get_register(cpu, rd_n);

            u32 result = 0;

            switch (op) {
                case 0: { // ADD
                    result = *rd + *rs;
                    
                    *rd = result;

                    cpu->cycles++;
                } break;
                case 1: { // CMP
                    result = *rd - *rs;
                    
                    set_condition_C((result <= *rs) ? 1 : 0);
                    set_overflow_subtract(*rd, *rs, result);
                    set_condition_Z(result == 0);
                    set_condition_N((result >> 31) & 1);
                    
                    cpu->cycles++;
                } break;
                case 2: { // MOV
                    result = *rs;
                    
                    *rd = result;
                    
                    cpu->cycles++;
                } break;
                case 3: { // BX
                    cpu->pc = *rs & (-2);
                    current_instruction = 0;

                    u8 thumb_mode = *rs & 1;
                    set_control_bit_T(thumb_mode);
                    
                    cpu->cycles += 3;
                } break;
            }
        } break;
        case INSTRUCTION_PC_RELATIVE_LOAD: {
            assert(decoded_instruction.rd != 15);
            u32 base = (cpu->pc & -4) + (decoded_instruction.offset << 2);
            u32 *address = (u32 *)get_memory_at(cpu, &memory, base);
            if (address != 0) *get_register(cpu, decoded_instruction.rd) = *address;

            cpu->cycles += 3;
        } break;
        case INSTRUCTION_LOAD_STORE_WITH_REGISTER_OFFSET: {
            u32 base = *get_register(cpu, decoded_instruction.rb) + *get_register(cpu, decoded_instruction.rm);
            if (base > *get_register(cpu, decoded_instruction.rb)) {
                // If the result overflow do not execute the instruction.

                if (decoded_instruction.L) {
                    if (decoded_instruction.B) { // LDRB
                        u8 *address = get_memory_at(cpu, &memory, base);
                        if (address != 0) *get_register(cpu, decoded_instruction.rd) = (u32)*address;
                    } else { // LDR
                        assert((base & 0b11) == 0);
                        u32 *address = (u32 *)get_memory_at(cpu, &memory, base);
                        if (address != 0) *get_register(cpu, decoded_instruction.rd) = *address;
                    }
                } else {
                    if (decoded_instruction.B) { // STRB
                        u8 *address = get_memory_at(cpu, &memory, base);
                        if (address != 0) *address = (u8)*get_register(cpu, decoded_instruction.rd);
                    } else { // STR
                        assert((base & 0b11) == 0);
                        u32 *address = (u32 *)get_memory_at(cpu, &memory, base);
                        if (address != 0) *address = *get_register(cpu, decoded_instruction.rd);
                    }
                }
            }

            if (decoded_instruction.L) {
                cpu->cycles += 3;
            } else {
                cpu->cycles += 2;
            }
        } break;
        case INSTRUCTION_LOAD_STORE_SIGN_EXTENDED_BYTE_HALFWORD: {
            u32 base = *get_register(cpu, decoded_instruction.rb) + *get_register(cpu, decoded_instruction.rm);
            assert((base & 1) == 0);

            u32 *rd = get_register(cpu, decoded_instruction.rd);

            u8 S = decoded_instruction.S;
            u8 H = decoded_instruction.H;

            if (S == 0 && H == 0) { // STRH
                assert((base & 1) == 0);
                u16 *address = (u16 *)get_memory_at(cpu, &memory, base);
                if (address != 0) *address = (u16)*rd;

                cpu->cycles += 2;
            } else if (S == 0 && H == 1) { // LDRH
                assert((base & 1) == 0);
                u16 *address = (u16 *)get_memory_at(cpu, &memory, base);
                if (address != 0) *rd = *address;

                cpu->cycles += 3;
            } else if (S == 1 && H == 0) { // LDRSB
                u8 *address = get_memory_at(cpu, &memory, base);
                if (address != 0) *rd = sign_extend(*address, 8);
                
                cpu->cycles += 3;
            } else { // LDRSH
                assert((base & 1) == 0);
                u16 *address = (u16 *)get_memory_at(cpu, &memory, base);
                if (address != 0) *rd = sign_extend(*address, 16);
                
                cpu->cycles += 3;
            }
        } break;
        case INSTRUCTION_LOAD_STORE_WITH_IMMEDIATE_OFFSET: {
            if (decoded_instruction.B) {
                u32 base = *get_register(cpu, decoded_instruction.rb) + (decoded_instruction.offset); // For Byte quantity does not multiply the offset.
                u8 *address = get_memory_at(cpu, &memory, base);
                if (address != 0) {
                    if (decoded_instruction.L) { // LDRB
                        *get_register(cpu, decoded_instruction.rd) = (u32)*address;
                    } else { // STRB
                        *address = (u8)*get_register(cpu, decoded_instruction.rd);
                    }
                }
            } else {
                u32 base = *get_register(cpu, decoded_instruction.rb) + (decoded_instruction.offset << 2);
                assert((base & 0b11) == 0);
                u32 *address = (u32 *)get_memory_at(cpu, &memory, base);
                if (address != 0) {
                    if (decoded_instruction.L) { // LDR
                        *get_register(cpu, decoded_instruction.rd) = *address;
                    } else { // STR
                        *address = *get_register(cpu, decoded_instruction.rd);
                    }
                }
            }

            if (decoded_instruction.L) {
                cpu->cycles += 3;
            } else {
                cpu->cycles += 2;
            }
        } break;
        case INSTRUCTION_LOAD_STORE_HALFWORD: {
            u32 base = *get_register(cpu, decoded_instruction.rb) + (decoded_instruction.offset << 1);
            assert((base & 1) == 0);
            u16 *address = (u16 *)get_memory_at(cpu, &memory, base);
            if (address != 0) {
                if (decoded_instruction.L) { // LDRH
                    *get_register(cpu, decoded_instruction.rd) = (u32)*address; // Cast to u32 to fill high bits with 0.
                } else { // STRH
                    *address = (u16)*get_register(cpu, decoded_instruction.rd);
                }
            }
            
            if (decoded_instruction.L) {
                cpu->cycles += 3;
            } else {
                cpu->cycles += 2;
            }
        } break;
        case INSTRUCTION_SP_RELATIVE_LOAD_STORE: {
            u32 base = cpu->sp + (decoded_instruction.offset << 2);
            assert((base & 0b11) == 0);
            
            u32 *address = (u32 *)get_memory_at(cpu, &memory, base);
            if (address != 0) {
                if (decoded_instruction.L) { // LDR
                    *get_register(cpu, decoded_instruction.rd) = *address;
                } else { // STR
                    *address = *get_register(cpu, decoded_instruction.rd);
                }
            }
            
            if (decoded_instruction.L) {
                cpu->cycles += 3;
            } else {
                cpu->cycles += 2;
            }
        } break;
        case INSTRUCTION_LOAD_ADDRESS: {
            assert(decoded_instruction.rd != 15);
            if (decoded_instruction.S) {
                // SP
                *get_register(cpu, decoded_instruction.rd) = cpu->sp + (decoded_instruction.value_8 << 2);
            } else {
                // PC
                *get_register(cpu, decoded_instruction.rd) = (cpu->pc & 0xFFFFFFFC) + (decoded_instruction.value_8 << 2);
            }

            cpu->cycles++;
        } break;
        case INSTRUCTION_ADD_OFFSET_TO_STACK_POINTER: {
            s8 sign = decoded_instruction.S ? -1 : 1;
            int offset = sign * (decoded_instruction.offset << 2);

            cpu->sp += offset;

            cpu->cycles++;
        } break;
        case INSTRUCTION_PUSH_POP_REGISTERS: {
            u8 register_list = (u8)decoded_instruction.register_list;
            // assert(register_list != 0);
            if (register_list == 0) {
                cpu->cycles++;
                goto exit_thumb_execute;
            }

            u32 sp = cpu->sp;
            
            u8 registers_set = 0;
            if (decoded_instruction.L) { // POP
                int register_index = 0;
                while (register_list) {
                    int register_index_set = register_list & 1;
                    if (register_index_set) {
                        registers_set++;

                        u32 *address = (u32 *)get_memory_at(cpu, &memory, sp);
                        if (address != 0) *get_register(cpu, (u8)register_index) = *address;

                        sp += 4;
                    }
                    
                    register_index++;
                    register_list >>= 1;
                }

                if (decoded_instruction.R) {
                    registers_set += 2;

                    u32 *address = (u32 *)get_memory_at(cpu, &memory, sp);
                    if (address != 0) {
                        cpu->pc = *address & 0xFFFFFFFE;
                        current_instruction = 0;
                        sp += 4;
                    }
                }
                
                cpu->sp = sp;

                cpu->cycles += registers_set + 2;
            } else { // PUSH
                // Instead of going from the bottom I'm going to insert the values in reverse order.
                int register_index = 7;
                if (decoded_instruction.R) {
                    registers_set++;

                    sp -= 4;

                    u32 *address = (u32 *)get_memory_at(cpu, &memory, sp);
                    if (address != 0) *address = *get_register(cpu, (u8)14); // LR register
                }

                while (register_list) {
                    int register_index_set = (register_list >> 7) & 1;
                    if (register_index_set) {
                        registers_set++;

                        sp -= 4;
                        
                        u32 *address = (u32 *)get_memory_at(cpu, &memory, sp);
                        if (address != 0) *address = *get_register(cpu, (u8)register_index);
                    }

                    register_index--;
                    register_list <<= 1;
                }

                cpu->sp = sp;

                cpu->cycles += registers_set + 1;
            }
        } break;
        case INSTRUCTION_MULTIPLE_LOAD_STORE: {
            u8 fixed_cycles = (decoded_instruction.L) ? 2 : 1;

            u32 *rb = get_register(cpu, decoded_instruction.rb);
            u32 base = *rb;
            u16 register_list = decoded_instruction.register_list;
            assert(register_list != 0);

            int register_index = 0;
            u8 registers_set = 0;
            while (register_list) {
                int register_index_set = register_list & 1;
                if (register_index_set) {
                    registers_set++;

                    u32 *address = (u32 *)get_memory_at(cpu, &memory, base);

                    if (address != 0) {
                        if (decoded_instruction.L) {
                            // LDMIA
                            *get_register(cpu, (u8)register_index) = *address;
                        } else {
                            // STMIA
                            *address = *get_register(cpu, (u8)register_index);
                        }
    
                        base += 4;
                    }
                }

                register_index++;
                register_list >>= 1;
            }

            *rb = base;

            cpu->cycles += registers_set + fixed_cycles;
        } break;
        case INSTRUCTION_CONDITIONAL_BRANCH: {
            Condition condition = (Condition)decoded_instruction.condition;
            int should_execute = should_execute_instruction(condition);

            if (should_execute) {
                u32 offset = left_shift_sign_extended(decoded_instruction.offset, 8, 1);
                cpu->pc += offset;
                current_instruction = 0;

                cpu->cycles += 3;
            } else {
                cpu->cycles++;
            }
        } break;
        case INSTRUCTION_SOFTWARE_INTERRUPT: {
            cpu->r14_svc = decoded_instruction.address + 2; // Next instruction
            cpu->spsr_svc = cpu->cpsr;

            set_mode(MODE_SUPERVISOR);
            set_control_bit_T(0); // Execute in ARM state
            set_control_bit_I(1); // Disable normal interrupts

            cpu->pc = 0x8;
            current_instruction = 0;

            cpu->cycles += 3;
        } break;
        case INSTRUCTION_UNCONDITIONAL_BRANCH: {
            u32 offset = left_shift_sign_extended(decoded_instruction.offset, 11, 1);
            cpu->pc += offset;
            current_instruction = 0;

            cpu->cycles += 3;
        } break;
        case INSTRUCTION_LONG_BRANCH_WITH_LINK: {
            if (decoded_instruction.H == 0) {
                // First part of the instruction
                u32 offset = left_shift_sign_extended(decoded_instruction.offset, 11, 12);
                cpu->lr = cpu->pc + offset;

                cpu->cycles++;
            } else {
                // Second part of the instruction
                cpu->pc = cpu->lr + ((u32)decoded_instruction.offset << 1);
                cpu->lr = (decoded_instruction.address + 2) | 1; // NOTE: the address of the instruction following the BL is placed in LR and bit 0 of LR is set.
                
                current_instruction = 0;

                cpu->cycles += 3;
            }

        } break;
    }

exit_thumb_execute:
    decoded_instruction = (Instruction){0};
}

void
thumb_decode()
{
    if (current_instruction == 0) return;

    if ((current_instruction & THUMB_INSTRUCTION_FORMAT_LONG_BRANCH_WITH_LINK) == THUMB_INSTRUCTION_FORMAT_LONG_BRANCH_WITH_LINK) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_LONG_BRANCH_WITH_LINK,
            .H = (current_instruction >> 11) & 1,
            .offset = current_instruction & 0x7FF,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_UNCONDITIONAL_BRANCH) == THUMB_INSTRUCTION_FORMAT_UNCONDITIONAL_BRANCH) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_UNCONDITIONAL_BRANCH,
            .offset = current_instruction & 0x7FF,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT) == THUMB_INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT) {
thumb_swi:
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_SOFTWARE_INTERRUPT,
            .value_8 = current_instruction & 0xFF,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_CONDITIONAL_BRANCH) == THUMB_INSTRUCTION_FORMAT_CONDITIONAL_BRANCH) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_CONDITIONAL_BRANCH,
            .offset = current_instruction & 0xFF,
            .condition = (current_instruction >> 8) & 0xF,
        };

        assert(decoded_instruction.condition != 0b1110);

        if (decoded_instruction.condition == 0b1111) {
            goto thumb_swi;
        }
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_MULTIPLE_LOAD_STORE) == THUMB_INSTRUCTION_FORMAT_MULTIPLE_LOAD_STORE) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_MULTIPLE_LOAD_STORE,
            .register_list = current_instruction & 0xFF,
            .rb = (current_instruction >> 8) & 7,
            .L = (current_instruction >> 11) & 1,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_PUSH_POP_REGISTERS) == THUMB_INSTRUCTION_FORMAT_PUSH_POP_REGISTERS) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_PUSH_POP_REGISTERS,
            .register_list = current_instruction & 0xFF,
            .R = (current_instruction >> 8) & 1,
            .L = (current_instruction >> 11) & 1,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_ADD_OFFSET_STACK_POINTER) == THUMB_INSTRUCTION_FORMAT_ADD_OFFSET_STACK_POINTER) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_ADD_OFFSET_TO_STACK_POINTER,
            .offset = current_instruction & 0x7F,
            .S = (current_instruction >> 7) & 1,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_LOAD_ADDRESS) == THUMB_INSTRUCTION_FORMAT_LOAD_ADDRESS) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_LOAD_ADDRESS,
            .value_8 = current_instruction & 0xFF,
            .rd = (current_instruction >> 8) & 7,
            .S = (current_instruction >> 11) & 1,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_SP_RELATIVE_LOAD_STORE) == THUMB_INSTRUCTION_FORMAT_SP_RELATIVE_LOAD_STORE) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_SP_RELATIVE_LOAD_STORE,
            .offset = current_instruction & 0xFF,
            .rd = (current_instruction >> 8) & 7,
            .L = (current_instruction >> 11) & 1,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_LOAD_STORE_HALFWORD) == THUMB_INSTRUCTION_FORMAT_LOAD_STORE_HALFWORD) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_LOAD_STORE_HALFWORD,
            .rd = (current_instruction >> 0) & 7,
            .rb = (current_instruction >> 3) & 7,
            .offset = (current_instruction >> 6) & 0x1F,
            .L = (current_instruction >> 11) & 1,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_LOAD_STORE_WITH_IMMEDIATE_OFFSET) == THUMB_INSTRUCTION_FORMAT_LOAD_STORE_WITH_IMMEDIATE_OFFSET) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_LOAD_STORE_WITH_IMMEDIATE_OFFSET,
            .rd = (current_instruction >> 0) & 7,
            .rb = (current_instruction >> 3) & 7,
            .offset = (current_instruction >> 6) & 0x1F,
            .L = (current_instruction >> 11) & 1,
            .B = (current_instruction >> 12) & 1,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_LOAD_STORE_SIGN_EXTENDED_BYTE_HALFWORD) == THUMB_INSTRUCTION_FORMAT_LOAD_STORE_SIGN_EXTENDED_BYTE_HALFWORD) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_LOAD_STORE_SIGN_EXTENDED_BYTE_HALFWORD,
            .rd = (current_instruction >> 0) & 7,
            .rb = (current_instruction >> 3) & 7,
            .rm = (current_instruction >> 6) & 7,
            .S = (current_instruction >> 10) & 1,
            .H = (current_instruction >> 11) & 1,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_LOAD_STORE_WITH_REGISTER_OFFSET) == THUMB_INSTRUCTION_FORMAT_LOAD_STORE_WITH_REGISTER_OFFSET) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_LOAD_STORE_WITH_REGISTER_OFFSET,
            .rd = (current_instruction >> 0) & 7,
            .rb = (current_instruction >> 3) & 7,
            .rm = (current_instruction >> 6) & 7,
            .B = (current_instruction >> 10) & 1,
            .L = (current_instruction >> 11) & 1,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_PC_RELATIVE_LOAD) == THUMB_INSTRUCTION_FORMAT_PC_RELATIVE_LOAD) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_PC_RELATIVE_LOAD,
            .offset = current_instruction & 0xFF,
            .rd = (current_instruction >> 8) & 7,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_HI_REGISTER_OPERATIONS_BRANCH_EXCHANGE) == THUMB_INSTRUCTION_FORMAT_HI_REGISTER_OPERATIONS_BRANCH_EXCHANGE) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_HI_REGISTER_OPERATIONS_BRANCH_EXCHANGE,
            .rd = (current_instruction >> 0) & 7,
            .rs = (current_instruction >> 3) & 7,
            .H2 = (current_instruction >> 6) & 1,
            .H1 = (current_instruction >> 7) & 1,
            .op = (current_instruction >> 8) & 0b11,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_ALU_OPERATIONS) == THUMB_INSTRUCTION_FORMAT_ALU_OPERATIONS) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_ALU_OPERATIONS,
            .rd = (current_instruction >> 0) & 7,
            .rs = (current_instruction >> 3) & 7,
            .op = (current_instruction >> 6) & 0xF,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_MOVE_COMPARE_ADD_SUBTRACT_IMMEDIATE) == THUMB_INSTRUCTION_FORMAT_MOVE_COMPARE_ADD_SUBTRACT_IMMEDIATE) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_MOVE_COMPARE_ADD_SUBTRACT_IMMEDIATE,
            .offset = current_instruction & 0xFF,
            .rd = (current_instruction >> 8) & 7,
            .op = (current_instruction >> 11) & 0b11,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_ADD_SUBTRACT) == THUMB_INSTRUCTION_FORMAT_ADD_SUBTRACT) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_ADD_SUBTRACT,
            .rd = (current_instruction >> 0) & 7,
            .rs = (current_instruction >> 3) & 7,
            .rn = (current_instruction >> 6) & 7,
            .op = (current_instruction >> 9) & 1,
            .I = (current_instruction >> 10) & 1,
        };
    }
    else if ((current_instruction & THUMB_INSTRUCTION_FORMAT_MOVE_SHIFTED_REGISTER) == THUMB_INSTRUCTION_FORMAT_MOVE_SHIFTED_REGISTER) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_MOVE_SHIFTED_REGISTER,
            .rd = (current_instruction >> 0) & 7,
            .rs = (current_instruction >> 3) & 7,
            .offset = (current_instruction >> 6) & 0x1F,
            .op = (current_instruction >> 11) & 0b11,
        };
    }
    else {
        fprintf(stderr, "Thumb instruction unknown: 0x%08X\n", current_instruction);
        exit(1);
    }

    decoded_instruction.address = cpu->pc - 2;
    decoded_instruction.encoding = current_instruction;
}

void
thumb_fetch()
{
    current_instruction = *(u16 *)get_memory_at(cpu, &memory, cpu->pc);
    cpu->pc += 2;
}


static void
process_branch()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_B: {
            if (decoded_instruction.L) {
                cpu->lr = cpu->pc - 4;
                assert(cpu->pc - 4 == decoded_instruction.address + 4);
            }

            u32 offset = left_shift_sign_extended(decoded_instruction.offset, 24, 2);
            cpu->pc += offset;

            current_instruction = 0;

            cpu->cycles += 3;
        } break;

        case INSTRUCTION_BX: {
            cpu->pc = *get_register(cpu, decoded_instruction.rn) & (-2); // NOTE: PC must be 16-bit align. This clears out the lsb (-2 is 0b1110).
            current_instruction = 0;

            u8 thumb_mode = *get_register(cpu, decoded_instruction.rn) & 1;
            set_control_bit_T(thumb_mode);

            cpu->cycles += 3;
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}

static void
process_data_processing()
{
    u8 extra_cpu_cycles = 0;

    u8 carry = 0;
    u32 second_operand = 0;
    if (decoded_instruction.I) {
        // Immediate with rotate right

        u8 imm = decoded_instruction.second_operand & 0xFF;
        u32 rotate = (decoded_instruction.second_operand >> 8) & 0xF;
        // NOTE: This value is zero extended to 32 bits, and then subject to a rotate right by twice the value in the rotate field.
        rotate *= 2;

        second_operand = rotate_right(imm, rotate, 32);
        if (rotate == 0) {
            carry = CONDITION_C;
        } else {
            carry = (second_operand >> 31) & 1;
        }
    } else {
        // From register

        u8 rm_n = decoded_instruction.second_operand & 0xF;
        u32 rm = *get_register(cpu, rm_n);
        u8 shift = (decoded_instruction.second_operand >> 4) & 0xFF;
        ShiftType shift_type = (ShiftType)((shift >> 1) & 0b11);
        if (shift & 1) {
            // Shift register
            extra_cpu_cycles++;

            u8 rs = (shift >> 4) & 0xF; // Register to the value to shift.
            u8 shift_value = (u8)(*get_register(cpu, rs));
            
            switch (shift_type) {
                case SHIFT_TYPE_LOGICAL_LEFT: {
                    if (shift_value == 0) {
                        second_operand = rm;
                        carry = CONDITION_C;
                    } else if (shift_value < 32) {
                        second_operand = rm << shift_value;
                        carry = (rm >> (32 - shift_value)) & 1;
                    } else if (shift_value == 32) {
                        second_operand = 0;
                        carry = rm & 1;
                    } else {
                        second_operand = 0;
                        carry = 0;
                    }
                } break;
                case SHIFT_TYPE_LOGICAL_RIGHT: {
                    if (shift_value == 0) {
                        second_operand = rm;
                        carry = CONDITION_C;
                    } else if (shift_value < 32) {
                        second_operand = rm >> shift_value;
                        carry = (rm >> (shift_value - 1)) & 1;
                    } else if (shift_value == 32) {
                        second_operand = 0;
                        carry = (rm >> 31) & 1;
                    } else {
                        second_operand = 0;
                        carry = 0;
                    }
                } break;
                case SHIFT_TYPE_ARITHMETIC_RIGHT: {
                    if (shift_value == 0) {
                        second_operand = rm;
                        carry = CONDITION_C;
                    } else if (shift_value < 32) {
                        second_operand = arithmetic_shift_right(rm, shift_value);
                        carry = (rm >> (shift_value - 1)) & 1;
                    } else {
                        if (((rm >> 31) & 1) == 0) {
                            second_operand = 0;
                            carry = (rm >> 31) & 1;
                        } else {
                            second_operand = 0xFFFFFFFF;
                            carry = (rm >> 31) & 1;
                        }
                    }
                } break;
                case SHIFT_TYPE_ROTATE_RIGHT: {
                    if (shift_value == 0) {
                        second_operand = rm;
                        carry = CONDITION_C;
                    } else if ((shift_value & 0xF) == 0) {
                        second_operand = rm;
                        carry = (rm >> 31) & 1;
                    } else {
                        second_operand = rotate_right(rm, shift_value & 0xF, 32);
                        carry = (rm >> ((shift_value & 0xF) - 1)) & 1;
                    }
                } break;
            }

        } else {
            // Shift immediate 5-bit value

            u8 shift_value = (shift >> 3) & 0b11111;
            
            switch (shift_type) {
                case SHIFT_TYPE_LOGICAL_LEFT: {
                    if (shift_value == 0) {
                        second_operand = rm;
                        carry = CONDITION_C;
                    } else {
                        second_operand = rm << shift_value;
                        carry = (rm >> (32 - shift_value)) & 1;
                    }
                } break;
                case SHIFT_TYPE_LOGICAL_RIGHT: {
                    if (shift_value == 0) {
                        second_operand = 0;
                        carry = (rm >> 31) & 1;
                    } else {
                        second_operand = rm >> shift_value;
                        carry = (rm >> (shift_value - 1)) & 1;
                    }
                } break;
                case SHIFT_TYPE_ARITHMETIC_RIGHT: {
                    if (shift_value == 0) {
                        if (((rm >> 31) & 1) == 0) {
                            second_operand = 0;
                            carry = (rm >> 31) & 1;
                        } else {
                            second_operand = 0xFFFFFFFF;
                            carry = (rm >> 31) & 1;
                        }
                    } else {
                        second_operand = arithmetic_shift_right(rm, shift_value);
                        carry = (rm >> (shift_value - 1)) & 1;
                    }
                } break;
                case SHIFT_TYPE_ROTATE_RIGHT: {
                    if (shift_value == 0) {
                        second_operand = (CONDITION_C << 31) | (rm >> 1);
                        carry = rm & 1;
                    } else {
                        second_operand = rotate_right(rm, shift_value, 32);
                        carry = (rm >> (shift_value - 1)) & 1;
                    }
                } break;
            }
        }
    }


    int store_result = false;
    u32 result = 0;

    u32 rn = *get_register(cpu, decoded_instruction.rn);
    u32 *rd = get_register(cpu, decoded_instruction.rd);
    
    switch (decoded_instruction.type) {
        case INSTRUCTION_ADD: {
            result = rn + second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
                // set_condition_V(((*rd & 0x80000000) == 0) && ((result & 0x80000000) == 1));
                set_overflow_addition(rn, second_operand, result);
            }
        } break;
        case INSTRUCTION_AND: {
            result = rn & second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(carry);
            }
        } break;
        case INSTRUCTION_EOR: {
            result = rn ^ second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(carry);
            }
        } break;
        case INSTRUCTION_SUB: {
            result = rn - second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(second_operand <= rn ? 1 : 0);
                set_overflow_subtract(rn, second_operand, result);
            }
        } break;
        case INSTRUCTION_RSB: {
            result = second_operand - rn;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(second_operand <= rn ? 1 : 0);
                set_overflow_subtract(second_operand, rn, result);
            }
        } break;
        case INSTRUCTION_ADC: {
            // result = rn & second_operand + carry;
            result = rn + second_operand + CONDITION_C;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
                // set_condition_V(((*rd & 0x80000000) == 0) && ((result & 0x80000000) == 1));
                set_overflow_addition(rn, second_operand + CONDITION_C, result);
            }
        } break;
        case INSTRUCTION_SBC: {
            // result = rn - second_operand + carry - 1;
            result = rn - second_operand - ~(CONDITION_C);
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(second_operand <= rn ? 1 : 0);
                set_overflow_subtract(rn, second_operand - ~(CONDITION_C), result);
            }
        } break;
        case INSTRUCTION_RSC: {
            // result = second_operand - rn + carry - 1;
            result = second_operand - rn - ~(CONDITION_C);
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(second_operand <= rn ? 1 : 0);
                set_overflow_subtract(second_operand, rn - ~(CONDITION_C), result);
            }
        } break;
        case INSTRUCTION_TST: {
            result = rn & second_operand;
            store_result = false;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(carry);
            }
        } break;
        case INSTRUCTION_TEQ: {
            result = rn ^ second_operand;
            store_result = false;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(carry);
            }
        } break;
        case INSTRUCTION_CMP: {
            result = rn - second_operand;
            store_result = false;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(second_operand <= rn ? 1 : 0);
                set_overflow_subtract(rn, second_operand, result);
            }
        } break;
        case INSTRUCTION_CMN: {
            result = rn + second_operand;
            store_result = false;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
                // set_condition_V(((*rd & 0x80000000) == 0) && ((result & 0x80000000) == 1));
                set_overflow_addition(rn, second_operand, result);
            }
        } break;
        case INSTRUCTION_ORR: {
            result = rn | second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(carry);
            }
        } break;
        case INSTRUCTION_MOV: {
            result = second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(carry);
            }
        } break;
        case INSTRUCTION_BIC: {
            result = rn & ~second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(carry);
            }
        } break;
        case INSTRUCTION_MVN: {
            result = ~second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(carry);
            }
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }

    if (store_result) {
        *rd = result;

        if (decoded_instruction.rd == 15) {
            assert(!"OK");
            current_instruction = 0;
            
            extra_cpu_cycles += 2;
        }
    }

    cpu->cycles += 1 + extra_cpu_cycles;
}

static void
process_psr_transfer()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_MRS: {
            if (decoded_instruction.P) {
                *get_register(cpu, decoded_instruction.rd) = *(get_spsr_current_mode(cpu));
            } else {
                *get_register(cpu, decoded_instruction.rd) = cpu->cpsr;
            }
        } break;
        case INSTRUCTION_MSR: {
            u32 value;
            if (decoded_instruction.I) {
                u8 imm = decoded_instruction.source_operand & 0xFF;
                u32 rotate = (decoded_instruction.source_operand >> 8) & 0xF;
                // NOTE: This value is zero extended to 32 bits, and then subject to a rotate right by twice the value in the rotate field.
                rotate *= 2;

                value = rotate_right(imm, rotate, 8);
            } else {
                value = *get_register(cpu, decoded_instruction.rm);
            }

            u32 field_mask = decoded_instruction.mask;
            if (decoded_instruction.P == 0) {
                if (in_privileged_mode(cpu)) {
                    if (((field_mask >> 0) & 1)) {
                        cpu->cpsr = cpu->cpsr & 0xFFFFFF00;
                        cpu->cpsr |=   (value & 0x000000FF);
                    }
                    if (((field_mask >> 1) & 1)) {
                        cpu->cpsr = cpu->cpsr & 0xFFFF00FF;
                        cpu->cpsr |=   (value & 0x0000FF00);
                    }
                    if (((field_mask >> 2) & 1)) {
                        cpu->cpsr = cpu->cpsr & 0xFF00FFFF;
                        cpu->cpsr |=   (value & 0x00FF0000);
                    }
                    if (((field_mask >> 3) & 1)) {
                        cpu->cpsr = cpu->cpsr & 0x00FFFFFF;
                        cpu->cpsr |=   (value & 0xFF000000);
                    }
                }
            } else {
                if (current_mode_has_spsr(cpu)) {
                    u32 *sr = get_spsr_current_mode(cpu);

                    if (((field_mask >> 0) & 1)) {
                        *sr =     *sr & 0xFFFFFF00;
                        *sr |= (value & 0x000000FF);
                    }
                    if (((field_mask >> 1) & 1)) {
                        *sr =     *sr & 0xFFFF00FF;
                        *sr |= (value & 0x0000FF00);
                    }
                    if (((field_mask >> 2) & 1)) {
                        *sr =     *sr & 0xFF00FFFF;
                        *sr |= (value & 0x00FF0000);
                    }
                    if (((field_mask >> 3) & 1)) {
                        *sr =     *sr & 0x00FFFFFF;
                        *sr |= (value & 0xFF000000);
                    }
                }

            }


            // u32 *sr = &cpu->cpsr;
            // if (decoded_instruction.P) {
            //     sr = get_spsr_current_mode(cpu);
            // }

            // if (decoded_instruction.I) {
            //     u8 imm = decoded_instruction.source_operand & 0xFF;
            //     u32 rotate = (decoded_instruction.source_operand >> 8) & 0xF;
            //     // NOTE: This value is zero extended to 32 bits, and then subject to a rotate right by twice the value in the rotate field.
            //     rotate *= 2;

            //     u32 value = rotate_right(imm, rotate, 8);
            //     *sr = value;
            // } else {
            //     *sr = *get_register(cpu, decoded_instruction.rm);
            // }
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }

    cpu->cycles++;
}

static void
process_multiply()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_MUL: {
            assert(!"Implement");
        } break;
        case INSTRUCTION_MLA: {
            assert(!"Implement");
        } break;
        case INSTRUCTION_MULL: {
            assert(!"Implement");
        } break;
        case INSTRUCTION_MLAL: {
            assert(!"Implement");
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}

#define UPDATE_BASE_OFFSET()            \
    do {                                \
        if (decoded_instruction.U) {    \
            base += offset;             \
        } else {                        \
            base -= offset;             \
        }                               \
    } while (0)

static void
process_single_data_transfer()
{
    u32 base = *get_register(cpu, decoded_instruction.rn);
    u32 offset = 0;

    if (decoded_instruction.I) {
        // Offset is in register

        u8 carry;
        u32 rm = *get_register(cpu, decoded_instruction.offset & 0xF);
        u8 shift = (decoded_instruction.offset >> 4) & 0xFF;
        u8 shift_type = (ShiftType)((shift >> 1) & 0b11);
        if (shift & 1) {
            // From register
            assert(!"The manual does not specify this as valid addressing mode. ARM Architecture Reference Manual, page A5-19");

            u8 rs = (shift >> 4) & 0xF ; // Register to the value to shift.
            offset = apply_shift(rm, (u8)(*get_register(cpu, rs) & 0xF), shift_type, &carry);
        } else {
            // Shift immediate 5-bit value

            u8 shift_value = (shift >> 3) & 0b11111;
            // offset = (u16)apply_shift(offset_register, shift_amount, shift_type, &carry);
            switch (shift_type) {
                case SHIFT_TYPE_LOGICAL_LEFT: {
                    offset = rm << shift_value;
                } break;
                case SHIFT_TYPE_LOGICAL_RIGHT: {
                    if (shift_value == 0) {
                        offset = 0;
                    } else {
                        offset = rm >> shift_value;
                    }
                } break;
                case SHIFT_TYPE_ARITHMETIC_RIGHT: {
                    if (shift_value == 0) {
                        if (((rm >> 31) & 1) == 1) {
                            offset = 0xFFFFFFFF;
                        } else {
                            offset = 0;
                        }
                    } else {
                        offset = arithmetic_shift_right(rm, shift_value);
                    }
                } break;
                case SHIFT_TYPE_ROTATE_RIGHT: {
                    if (shift_value == 0) {
                        offset = (CONDITION_C << 31) | (rm >> 1);
                    } else {
                        offset = rotate_right(rm, shift_value, 32);
                    }
                } break;
            }
        }

    } else {
        // Immediate value

        offset = (u16)decoded_instruction.offset;
    }

    u8 P = decoded_instruction.P;
    u8 B = decoded_instruction.B;
    u32 *rd = get_register(cpu, decoded_instruction.rd);
    
    switch (decoded_instruction.type) {
        case INSTRUCTION_LDR: {
            if (B) {
                u8 *address;
                if (P) {
                    UPDATE_BASE_OFFSET();
                    address = get_memory_at(cpu, &memory, base);

                    if (decoded_instruction.W) {
                        *get_register(cpu, decoded_instruction.rn) = base;
                    }
                } else {
                    address = get_memory_at(cpu, &memory, base);
                    UPDATE_BASE_OFFSET();
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
                
                if (address != 0) *rd = *address;
            } else {
                u32 value = 0;
                u32 *address;
                if (P) {
                    UPDATE_BASE_OFFSET();
                    address = (u32 *)get_memory_at(cpu, &memory, base);

                    if (decoded_instruction.W) {
                        *get_register(cpu, decoded_instruction.rn) = base;
                    }
                } else {
                    address = (u32 *)get_memory_at(cpu, &memory, base);
                    UPDATE_BASE_OFFSET();
                    *get_register(cpu, decoded_instruction.rn) = base;
                }

                if (address != 0) {
                    u8 rotate_value = 8 * (base & 0b11);
                    value = rotate_right(*address, rotate_value, 32);
    
                    if (*rd == 15) {
                        cpu->pc = value & 0xFFFFFFFC; // NOTE: From "ARM Architecture Reference Manual"
    
                        // PC written, so it has to branch to that instruction and invalidate whatever the pre-fetched was.
                        current_instruction = 0;
    
                        cpu->cycles += 2; // 2 Extra cycles on LDR PC
                    } else {
                        *rd = value;
                    }
                }
            }
            
            cpu->cycles += 3;
        } break;
        case INSTRUCTION_STR: {
            if (B) {
                u8 *address;
                if (P) {
                    UPDATE_BASE_OFFSET();
                    address = get_memory_at(cpu, &memory, base);

                    if (decoded_instruction.W) {
                        *get_register(cpu, decoded_instruction.rn) = base;
                    }
                } else {
                    address = get_memory_at(cpu, &memory, base);
                    UPDATE_BASE_OFFSET();
                    *get_register(cpu, decoded_instruction.rn) = base;
                }

                if (address != 0) *address = (*rd & 0xFF);
            } else {
                u32 *address;
                if (P) {
                    UPDATE_BASE_OFFSET();
                    address = (u32 *)get_memory_at(cpu, &memory, base);

                    if (decoded_instruction.W) {
                        *get_register(cpu, decoded_instruction.rn) = base;
                    }
                } else {
                    address = (u32 *)get_memory_at(cpu, &memory, base);
                    UPDATE_BASE_OFFSET();
                    *get_register(cpu, decoded_instruction.rn) = base;
                }

                if (address != 0) *address = *rd;
            }

            cpu->cycles += 2;

        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}

static void
process_halfword_and_signed_data_transfer()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_LDRH: {
            assert(decoded_instruction.rd != 15);

            u32 base = *get_register(cpu, decoded_instruction.rn);
            u32 offset;
            if (decoded_instruction.I) {
                offset = decoded_instruction.offset;
            } else {
                offset = *get_register(cpu, decoded_instruction.rm);
            }

            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();

                u8 *address = get_memory_at(cpu, &memory, base);
                if (address != 0) *get_register(cpu, decoded_instruction.rd) = *((u16 *)address);

                if (decoded_instruction.W) {
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
            } else {
                u8 *address = get_memory_at(cpu, &memory, base);
                if (address != 0) *get_register(cpu, decoded_instruction.rd) = *((u16 *)address);

                UPDATE_BASE_OFFSET();
                *get_register(cpu, decoded_instruction.rn) = base;
            }

            cpu->cycles += 3;

        } break;
        case INSTRUCTION_STRH: {
            u32 base = *get_register(cpu, decoded_instruction.rn);
            u32 offset;
            if (decoded_instruction.I) {
                offset = decoded_instruction.offset;
            } else {
                offset = *get_register(cpu, decoded_instruction.rm);
            }

            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();

                u8 *address = get_memory_at(cpu, &memory, base);
                if (address != 0) *((u16 *)address) = (u16)*get_register(cpu, decoded_instruction.rd);

                if (decoded_instruction.W) {
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
            } else {
                u8 *address = get_memory_at(cpu, &memory, base);
                if (address != 0) *((u16 *)address) = (u16)*get_register(cpu, decoded_instruction.rd);

                UPDATE_BASE_OFFSET();
                *get_register(cpu, decoded_instruction.rn) = base;
            }
            
            cpu->cycles += 2;

        } break;
        case INSTRUCTION_LDRSB: {
            assert(decoded_instruction.rd != 15);
            
            u32 base = *get_register(cpu, decoded_instruction.rn);
            u32 offset;
            if (decoded_instruction.I) {
                offset = decoded_instruction.offset;
            } else {
                offset = *get_register(cpu, decoded_instruction.rm);
            }


            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();

                u8 *address = get_memory_at(cpu, &memory, base);
                if (address != 0) {
                    u8 value = *address;
                    u8 sign = (value >> 7) & 1;
                    u32 value_sign_extended = (((u32)-sign) << 8) | value;
    
                    *get_register(cpu, decoded_instruction.rd) = value_sign_extended;
                    
                    if (decoded_instruction.W) {
                        *get_register(cpu, decoded_instruction.rn) = base;
                    }
                }

            } else {
                u8 *address = get_memory_at(cpu, &memory, base);
                if (address != 0) {
                    u8 value = *address;
                    u8 sign = (value >> 7) & 1;
                    u32 value_sign_extended = (((u32)-sign) << 8) | value;
    
                    *get_register(cpu, decoded_instruction.rd) = value_sign_extended;
    
                    UPDATE_BASE_OFFSET();
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
            }

            cpu->cycles += 3;

        } break;
        case INSTRUCTION_LDRSH: {
            assert(decoded_instruction.rd != 15);
            
            u32 base = *get_register(cpu, decoded_instruction.rn);
            u32 offset;
            if (decoded_instruction.I) {
                offset = decoded_instruction.offset;
            } else {
                offset = *get_register(cpu, decoded_instruction.rm);
            }


            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();

                u8 *address = get_memory_at(cpu, &memory, base);
                if (address != 0) {
                    u16 value = *((u16 *)address);
                    u8 sign = (value >> 15) & 1;
                    u32 value_sign_extended = (((u32)-sign) << 16) | value;
    
                    *get_register(cpu, decoded_instruction.rd) = value_sign_extended;
    
                    if (decoded_instruction.W) {
                        *get_register(cpu, decoded_instruction.rn) = base;
                    }
                }
            } else {
                u8 *address = get_memory_at(cpu, &memory, base);
                if (address != 0) {
                    u16 value = *((u16 *)address);
                    u8 sign = (value >> 15) & 1;
                    u32 value_sign_extended = (((u32)-sign) << 16) | value;
    
                    *get_register(cpu, decoded_instruction.rd) = value_sign_extended;
    
                    UPDATE_BASE_OFFSET();
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
            }

            cpu->cycles += 3;

        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}

#undef UPDATE_BASE_OFFSET


static void
process_block_data_transfer()
{
    if (decoded_instruction.S) {
        assert(!"Not handled");
    }

    u8 P = decoded_instruction.P;
    switch (decoded_instruction.type) {
        case INSTRUCTION_LDM: {
            u32 base_address = *get_register(cpu, decoded_instruction.rn);
            u16 register_list = decoded_instruction.register_list;
            assert(register_list != 0);

            int register_index = (decoded_instruction.U) ? 0 : 15;
            u8 registers_set = 0;
            while (register_list) {
                if (decoded_instruction.U) {
                    // Increment
                    int register_index_set = register_list & 1;
                    if (register_index_set) {
                        registers_set++;

                        u32 *address;
                        if (P) {
                            base_address += 4;
                            address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            
                            if (decoded_instruction.W) {
                                *get_register(cpu, decoded_instruction.rn) = base_address;
                            }
                        } else {
                            address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            base_address += 4;
                            *get_register(cpu, decoded_instruction.rn) = base_address;
                        }

                        if (register_index == 15) {
                            assert(!"Check if I have to use the P flag (I think I do)");
                            u32 value = *(u32 *)get_memory_at(cpu, &memory, base_address);
                            if (value != 0) {
                                cpu->pc = value & 0xFFFFFFFC;
                                current_instruction = 0;
                            }

                            base_address += 4;

                            assert("Add cpu cycles");
                        } else {
                            u32 value = 0;
                            if (address != 0) {
                                value = *address;
                            }

                            *get_register(cpu, (u8)register_index) = value;
                        }
                    }

                    register_index++;
                    register_list >>= 1;
                } else {
                    // Decrement
                    assert(!"Implemented checking the manual, but when reach this point, let's re-check the implementation (just in case)");
                    assert(!"Add cpu cycles");
                    int register_index_set = (register_list >> 15) & 1;
                    if (register_index_set) {
                        u32 *address;
                        if (P) {
                            base_address -= 4;
                            address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            
                            if (decoded_instruction.W) {
                                *get_register(cpu, decoded_instruction.rn) = base_address;
                            }
                        } else {
                            address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            base_address -= 4;
                            *get_register(cpu, decoded_instruction.rn) = base_address;
                        }

                        if (register_index == 15) {
                            u32 value = *(u32 *)get_memory_at(cpu, &memory, base_address);
                            cpu->pc = value & 0xFFFFFFFC;
                            current_instruction = 0;

                            base_address -= 4;
                        } else {
                            if (address != 0) *get_register(cpu, (u8)register_index) = *address;
                        }
                    }

                    register_index--;
                    register_list <<= 1;
                }

            }

            cpu->cycles += registers_set + 2;

        } break;

        case INSTRUCTION_STM: {
            u32 base_address = *get_register(cpu, decoded_instruction.rn);
            u16 register_list = decoded_instruction.register_list;
            assert(register_list != 0);

            u8 P = decoded_instruction.P;

            int register_index = (decoded_instruction.U) ? 0 : 15;
            u8 registers_set = 0;

            while (register_list) {
                if (decoded_instruction.U) {
                    // Increment
                    int register_index_set = register_list & 1;
                    if (register_index_set) {
                        registers_set++;

                        u32 *address;
                        if (P) {
                            base_address += 4;
                            address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            
                            if (decoded_instruction.W) {
                                *get_register(cpu, decoded_instruction.rn) = base_address;
                            }
                        } else {
                            address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            base_address += 4;
                            *get_register(cpu, decoded_instruction.rn) = base_address;
                        }

                        if (address != 0) *address = *get_register(cpu, (u8)register_index);
                    }

                    register_index++;
                    register_list >>= 1;
                } else {
                    // Decrement
                    int register_index_set = (register_list >> 15) & 1;
                    if (register_index_set) {
                        registers_set++;

                        u32 *address;
                        if (P) {
                            base_address -= 4;
                            address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            
                            if (decoded_instruction.W) {
                                *get_register(cpu, decoded_instruction.rn) = base_address;
                            }
                        } else {
                            address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            base_address -= 4;
                            *get_register(cpu, decoded_instruction.rn) = base_address;
                        }

                        if (address != 0) *address = *get_register(cpu, (u8)register_index);
                    }

                    register_index--;
                    register_list <<= 1;
                }
            }
            
            cpu->cycles += registers_set + 1;
            
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}
static void
process_single_data_swap()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_SWP: {
            u32 *rn = get_register(cpu, decoded_instruction.rn);
            u32 *rm = get_register(cpu, decoded_instruction.rm);
            u32 *rd = get_register(cpu, decoded_instruction.rd);
            
            if (decoded_instruction.B) {
                u8 *address = get_memory_at(cpu, &memory, *rn);
                if (address != 0) {
                    u8 temp = *address;
                    *address = (u8)*rm;
                    *rd = temp;
                }
            } else {
                u32 *address = (u32 *)get_memory_at(cpu, &memory, *rn);
                if (address != 0) {
                    int rotate_value = 8 * (*rn & 0b11);
                    u32 temp = rotate_right(*address, rotate_value, 32);
    
                    *address = *rm;
                    *rd = temp;
                }
            }

            cpu->cycles += 4;
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}

static void
process_software_interrupt()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_SWI: {
            assert(!"Implement");
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}

static void
process_coprocessor_data_operations()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_CDP: {
            assert(!"Implement");
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}

static void
process_coprocessor_data_transfers()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_STC: {
            assert(!"Implement");
        } break;
        case INSTRUCTION_LDC: {
            assert(!"Implement");
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}

static void
process_coprocessor_register_transfers()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_MCR: {
            assert(!"Implement");
        } break;
        case INSTRUCTION_MRC: {
            assert(!"Implement");
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}

u32 *last_register_compared = 0;

static int found = 0;

void
execute()
{
    if (decoded_instruction.type == INSTRUCTION_NONE) goto exit_execute;

    if (decoded_instruction.address == 0x000003CC) {
        found = 1;
    }
    
    if (IN_THUMB_MODE) {
        thumb_execute();
        return;
    }

    if (!should_execute_instruction(decoded_instruction.condition)) {
        DEBUG_PRINT("0x%08X: 0x%08X %s, cpsr = 0x%08X, cycles = %lld... Skipped\n", decoded_instruction.address, decoded_instruction.encoding, get_instruction_type_string(decoded_instruction.type), cpu->cpsr, cpu->cycles);
        cpu->cycles++;
        
        goto exit_execute;
    }
    
    DEBUG_PRINT("0x%08X: 0x%08X %s, cpsr = 0x%08X, cycles = %lld\n", decoded_instruction.address, decoded_instruction.encoding, get_instruction_type_string(decoded_instruction.type), cpu->cpsr, cpu->cycles);

    InstructionCategory category = instruction_categories[decoded_instruction.type];
    switch (category) {
        case INSTRUCTION_CATEGORY_BRANCH: {
            process_branch();
        } break;
        case INSTRUCTION_CATEGORY_DATA_PROCESSING: {
            process_data_processing();
        } break;
        case INSTRUCTION_CATEGORY_PSR_TRANSFER: {
            process_psr_transfer();
        } break;
        case INSTRUCTION_CATEGORY_MULTIPLY: {
            process_multiply();
        } break;
        case INSTRUCTION_CATEGORY_SINGLE_DATA_TRANSFER: {
            process_single_data_transfer();
        } break;
        case INSTRUCTION_CATEGORY_HALFWORD_AND_SIGNED_DATA_TRANSFER: {
            process_halfword_and_signed_data_transfer();
        } break;
        case INSTRUCTION_CATEGORY_BLOCK_DATA_TRANSFER: {
            process_block_data_transfer();
        } break;
        case INSTRUCTION_CATEGORY_SINGLE_DATA_SWAP: {
            process_single_data_swap();
        } break;
        case INSTRUCTION_CATEGORY_SOFTWARE_INTERRUPT: {
            process_software_interrupt();
        } break;
        case INSTRUCTION_CATEGORY_COPROCESSOR_DATA_OPERATIONS: {
            process_coprocessor_data_operations();
        } break;
        case INSTRUCTION_CATEGORY_COPROCESSOR_DATA_TRANSFERS: {
            process_coprocessor_data_transfers();
        } break;
        case INSTRUCTION_CATEGORY_COPROCESSOR_REGISTER_TRANSFERS: {
            process_coprocessor_register_transfers();
        } break;
    }

exit_execute:
    last_instruction = decoded_instruction;
    decoded_instruction = (Instruction){0};
}


void
decode()
{
    if (IN_THUMB_MODE) {
        thumb_decode();
        return;
    }

    if (current_instruction == 0) return;

    if ((current_instruction & INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT) == INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_SWI,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_COPROCESSOR_REGISTER_TRANSFER) == INSTRUCTION_FORMAT_COPROCESSOR_REGISTER_TRANSFER) {
        u8 L = (current_instruction >> 20) & 1;
        InstructionType type = 0;
        switch (L) {
            case 0: type = INSTRUCTION_MCR; break;
            case 1: type = INSTRUCTION_MRC; break;
        }

        decoded_instruction = (Instruction) {
            .type = type,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_COPROCESSOR_DATA_OPERATION) == INSTRUCTION_FORMAT_COPROCESSOR_DATA_OPERATION) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_CDP,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_COPROCESSOR_DATA_TRANSFER) == INSTRUCTION_FORMAT_COPROCESSOR_DATA_TRANSFER) {
        u8 L = (current_instruction >> 20) & 1;
        InstructionType type = 0;
        switch (L) {
            case 0: type = INSTRUCTION_STC; break;
            case 1: type = INSTRUCTION_LDC; break;
        }

        decoded_instruction = (Instruction) {
            .type = type,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_BRANCH) == INSTRUCTION_FORMAT_BRANCH) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_B,
            .offset = current_instruction & 0xFFFFFF,
            .L = (u8)((current_instruction >> 24) & 1),
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_BLOCK_DATA_TRANSFER) == INSTRUCTION_FORMAT_BLOCK_DATA_TRANSFER) {
        int opcode = (current_instruction >> 20) & 1;
        InstructionType type = 0;
        switch (opcode) {
            case 0: type = INSTRUCTION_STM; break;
            case 1: type = INSTRUCTION_LDM; break;
        }

        decoded_instruction = (Instruction) {
            .type = type,
            .P = (current_instruction >> 24) & 1,
            .U = (current_instruction >> 23) & 1,
            .S = (current_instruction >> 22) & 1,
            .W = (current_instruction >> 21) & 1,
            .L = (current_instruction >> 20) & 1,
            .rn = (current_instruction >> 16) & 0xF,
            .register_list = current_instruction & 0xFFFF,
        };

        if (decoded_instruction.S) {
            decoded_instruction.W = 0; // NOTE: Setting bit 21 (the W bit) has UNPREDICTABLE results, so let's force to 0.
        }

        // NOTE: R15 should not be used as the base register in any LDM or STM instruction.
        assert(decoded_instruction.rn != 15);

        // NOTE: Any subset of the registers, or all the registers, may be specified. The only restriction is that the register list should not be empty.
        assert(decoded_instruction.register_list > 0);
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER) == INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER) {
        int opcode = (current_instruction >> 20) & 1;
        InstructionType type = 0;
        switch (opcode) {
            case 0: type = INSTRUCTION_STR; break;
            case 1: type = INSTRUCTION_LDR; break;
        }

        decoded_instruction = (Instruction) {
            .type = type,
            .I = (current_instruction >> 25) & 1,
            .P = (current_instruction >> 24) & 1,
            .U = (current_instruction >> 23) & 1,
            .B = (current_instruction >> 22) & 1,
            .W = (current_instruction >> 21) & 1,
            .L = (current_instruction >> 20) & 1,
            .rn = (current_instruction >> 16) & 0xF,
            .rd = (current_instruction >> 12) & 0xF,
            .offset = current_instruction & 0xFFF,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_IMMEDIATE_OFFSET) == INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_IMMEDIATE_OFFSET) {
        if ((current_instruction >> 25) & 1) {
            // HALFWORD_DATA_TRANSFER does not have the 25-bit set; it should be a DATA_PROCESSING instruction.
            goto data_processing;
        }
        
        u8 H = (current_instruction >> 5) & 1;
        u8 S = (current_instruction >> 6) & 1;

        if (S == 0 && H == 0) goto SWP;

        u8 L = (current_instruction >> 20) & 1;
        InstructionType type = 0;
        if (S == 0 && H == 1) {
            if (L) {
                // load
                type = INSTRUCTION_LDRH;
            } else {
                type = INSTRUCTION_STRH;
            }
        } else if (S == 1 && H == 0) {
            type = INSTRUCTION_LDRSB;
        } else {
            type = INSTRUCTION_LDRSH;
        }

        decoded_instruction = (Instruction) {
            .type = type,
            .offset = ((current_instruction >> 4) & 0xF0) | (current_instruction & 0xF),
            .H = H,
            .S = S,
            .rd = (current_instruction >> 12) & 0xF,
            .rn = (current_instruction >> 16) & 0xF,
            .L = L,
            .I = (current_instruction >> 22) & 1,
            .W = (current_instruction >> 21) & 1,
            .U = (current_instruction >> 23) & 1,
            .P = (current_instruction >> 24) & 1,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET) == INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET) {
        if ((current_instruction >> 25) & 1) {
            // HALFWORD_DATA_TRANSFER does not have the 25-bit set; it should be a DATA_PROCESSING instruction.
            goto data_processing;
        }

        u8 H = (current_instruction >> 5) & 1;
        u8 S = (current_instruction >> 6) & 1;

        if (S == 0 && H == 0) goto SWP;

        u8 L = (current_instruction >> 20) & 1;

        // if (L == 0 && S == 1) assert(!"Bad flags");

        InstructionType type = 0;
        if (S == 0 && H == 1) {
            if (L) {
                // load
                type = INSTRUCTION_LDRH;
            } else {
                type = INSTRUCTION_STRH;
            }
        } else if (S == 1 && H == 0) {
            type = INSTRUCTION_LDRSB;
        } else {
            type = INSTRUCTION_LDRSH;
        }

        decoded_instruction = (Instruction) {
            .type = type,
            .rm = current_instruction & 0xF,
            .H = H,
            .S = S,
            .rd = (current_instruction >> 12) & 0xF,
            .rn = (current_instruction >> 16) & 0xF,
            .L = L,
            .W = (current_instruction >> 21) & 1,
            .U = (current_instruction >> 23) & 1,
            .P = (current_instruction >> 24) & 1,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_BRANCH_AND_EXCHANGE) == INSTRUCTION_FORMAT_BRANCH_AND_EXCHANGE) {
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_BX,
            .rn = (current_instruction & 0xF),
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_SINGLE_DATA_SWAP) == INSTRUCTION_FORMAT_SINGLE_DATA_SWAP) {
SWP:
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_SWP,
            .rm = current_instruction & 0xF,
            .rd = (current_instruction >> 12) & 0xF,
            .rn = (current_instruction >> 16) & 0xF,
            .B = (current_instruction >> 22) & 1,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_MULTIPLY_LONG) == INSTRUCTION_FORMAT_MULTIPLY_LONG) {
        u8 A = (current_instruction >> 21) & 1;
        InstructionType type = 0;
        switch (A) {
            case 0: type = INSTRUCTION_MULL; break;
            case 1: type = INSTRUCTION_MLAL; break;
        }

        decoded_instruction = (Instruction) {
            .type = type,
            .rm = current_instruction & 0xF,
            .rs = (current_instruction >> 8) & 0xF,
            .rdlo = (current_instruction >> 12) & 0xF,
            .rdhi = (current_instruction >> 16) & 0xF,
            .S = (current_instruction >> 20) & 1,
            .A = A,
            .U = (current_instruction >> 22) & 1,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_MULTIPLY) == INSTRUCTION_FORMAT_MULTIPLY) {
        u8 A = (current_instruction >> 21) & 1;
        InstructionType type = 0;
        switch (A) {
            case 0: type = INSTRUCTION_MUL; break;
            case 1: type = INSTRUCTION_MLA; break;
        }

        decoded_instruction = (Instruction) {
            .type = type,
            .rm = current_instruction & 0xF,
            .rs = (current_instruction >> 8) & 0xF,
            .rn = (current_instruction >> 12) & 0xF,
            .rd = (current_instruction >> 16) & 0xF,
            .S = (current_instruction >> 20) & 1,
            .A = A,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_DATA_PROCESSING) == INSTRUCTION_FORMAT_DATA_PROCESSING) {
data_processing:
        int opcode = (current_instruction >> 21) & 0b1111;
        InstructionType type = 0;
        switch (opcode) {
            case 0b0000: type = INSTRUCTION_AND; break;
            case 0b0001: type = INSTRUCTION_EOR; break;
            case 0b0010: type = INSTRUCTION_SUB; break;
            case 0b0011: type = INSTRUCTION_RSB; break;
            case 0b0100: type = INSTRUCTION_ADD; break;
            case 0b0101: type = INSTRUCTION_ADC; break;
            case 0b0110: type = INSTRUCTION_SBC; break;
            case 0b0111: type = INSTRUCTION_RSC; break;
            case 0b1000: type = INSTRUCTION_TST; break;
            case 0b1001: type = INSTRUCTION_TEQ; break;
            case 0b1010: type = INSTRUCTION_CMP; break;
            case 0b1011: type = INSTRUCTION_CMN; break;
            case 0b1100: type = INSTRUCTION_ORR; break;
            case 0b1101: type = INSTRUCTION_MOV; break;
            case 0b1110: type = INSTRUCTION_BIC; break;
            case 0b1111: type = INSTRUCTION_MVN; break;
        }

        u8 S = (current_instruction >> 20) & 1;
        decoded_instruction = (Instruction) {
            .type = type,
            .S = S, // Set condition codes
            .I = (current_instruction >> 25) & 1, // Immediate operand
            .rn = (current_instruction >> 16) & 0xF, // Source register
            .rd = (current_instruction >> 12) & 0xF, // Destination register
            .second_operand = current_instruction & ((1 << 12) - 1),
        };

        if (S == 0 && (type == INSTRUCTION_TST ||
                       type == INSTRUCTION_TEQ ||
                       type == INSTRUCTION_CMP ||
                       type == INSTRUCTION_CMN))
        {
            u8 special_type = (current_instruction >> 16) & 0b111111;
            switch (special_type) {
                case 0b001111: {
                    decoded_instruction = (Instruction) {
                        .type = INSTRUCTION_MRS,
                        .P = (current_instruction >> 22) & 1,
                        .rd = (current_instruction >> 12) & 0xF,
                    };
                } break;
                case 0b101001: {
                    decoded_instruction = (Instruction) {
                        .type = INSTRUCTION_MSR,
                        .P = (current_instruction >> 22) & 1,
                        .rm = current_instruction & 0xF,
                        .mask = (current_instruction >> 16) & 0xF,
                    };
                } break;
                case 0b101000: {
                    decoded_instruction = (Instruction) {
                        .type = INSTRUCTION_MSR,
                        .P = (current_instruction >> 22) & 1,
                        .source_operand = current_instruction & 0xFFF,
                        .I = 1, // To be recognized as immediate
                        .mask = (current_instruction >> 16) & 0xF,
                    };
                } break;
                default: {
                    // If does not meet the requirements to be the previous instructions, just keep the original one and set the S flag.
                    // NOTE: An assembler should always set the S flag for these instructions even if this is not specified in the mnemonic.
                    decoded_instruction.S = 1;
                }
            }
        }


    } else {
        fprintf(stderr, "Instruction unknown: 0x%08X\n", current_instruction);
        exit(1);
    }


    decoded_instruction.condition = (current_instruction >> 28) & 0xF;
    decoded_instruction.address = cpu->pc - 4;
    decoded_instruction.encoding = current_instruction;

    current_instruction = 0;
}

void
fetch()
{
    if (IN_THUMB_MODE) {
        thumb_fetch();
    } else {
        current_instruction = *(u32 *)get_memory_at(cpu, &memory, cpu->pc);
        cpu->pc += 4;
    }
}



#define CYCLES_HDRAW            960
#define CYCLES_HBLANK           272
#define CYCLES_SCANLINE         (CYCLES_HDRAW + CYCLES_HBLANK)
#define CYCLES_VDRAW            160*CYCLES_SCANLINE
#define CYCLES_VBLANK           68*CYCLES_SCANLINE
#define MAX_SCANLINE            228
#define CPU_CYCLES_PER_FRAME    (280896)

static u8 current_scanline;

static void
set_lcd_io()
{
    u32 cycles_current_frame = (cpu->cycles % CPU_CYCLES_PER_FRAME);
    u32 cycles_current_scanline = (cycles_current_frame / CYCLES_SCANLINE);

    u8 scanline = cycles_current_scanline % MAX_SCANLINE;
    if (scanline != current_scanline) {
        current_scanline = scanline;
        *IO_VCOUNT = current_scanline;
    }
    

    *IO_DISPSTAT &= 0xFFFC; // Clear last 2 bits
    
    if (cycles_current_frame > CYCLES_VDRAW && cycles_current_frame <= CYCLES_VBLANK) {
        *IO_DISPSTAT |= 1; // Set bit 0
    }

    if (cycles_current_scanline > CYCLES_HDRAW && cycles_current_scanline <= CYCLES_HBLANK) {
        *IO_DISPSTAT |= 0b10; // Set bit 1
    }
}


static u32 current_frame = 0;

static void
run()
{
    // Emulating pipelining.
    // while (is_running) {
    //     execute();
    //     decode();
    //     fetch();
    // }

    while (cpu->cycles / CPU_CYCLES_PER_FRAME <= current_frame) {
        
        execute();
        set_lcd_io();
        
        decode();
        fetch();
        if (paused) return;
    }
    
    current_frame++;

}

// Video
#define SCREEN_WIDTH            (240)
#define SCREEN_HEIGHT           (160)
#define SCREEN_SIZE             (SCREEN_WIDTH*SCREEN_HEIGHT)
#define SCALE                   (10)    /* Pixel scale */
#define WINDOW_WIDTH            (SCREEN_WIDTH*SCALE)
#define WINDOW_HEIGHT           (SCREEN_HEIGHT*SCALE)

// Audio
#define MAX_SAMPLES             512
#define MAX_SAMPLES_PER_UPDATE  4096
#define SAMPLE_RATE             44100
#define SAMPLE_SIZE             16
#define NUMBER_OF_CHANNELS      2

static float frequency = 440.0f;
// Index for audio rendering
static float sine_idx = 0.0f;

// Audio input processing callback
static void AudioInputCallback(void *buffer, unsigned int frames)
{
    float incr = frequency/SAMPLE_RATE;
    short *d = (short *)buffer;

    for (unsigned int i = 0; i < frames; i++)
    {
        d[i] = (short)(32000.0f*sinf(2*PI*sine_idx));
        sine_idx += incr;
        if (sine_idx > 1.0f) {
            sine_idx -= 1.0f;
        }
    }
}


static int text_height = 30;
static int text_drawn = 0;

#ifdef _DEBUG
#ifdef _LINUX
    #define DRAW_TEXT(format, ...)                                          \
        do {                                                                \
            char text[100];                                                 \
            snprintf(text, 100, format, ##__VA_ARGS__);                     \
            DrawText(text, 10, text_drawn*text_height, text_height, GREEN); \
            text_drawn++;                                                   \
        } while (0)
#else
    #define DRAW_TEXT(format, ...)                                          \
        do {                                                                \
            char text[100];                                                 \
            snprintf(text, 100, format, __VA_ARGS__);                       \
            DrawText(text, 10, text_drawn*text_height, text_height, GREEN); \
            text_drawn++;                                                   \
        } while (0)
#endif
#else
    #define DRAW_TEXT(...)
#endif



#define VIDEO_BUFFER_SIZE SCREEN_SIZE

static void
fill_video_buffer(u32 *buffer)
{
    if ((*IO_DISPCNT >> 7) & 1) {
        // Force blank

        for (int i = 0; i < VIDEO_BUFFER_SIZE; ++i) {
            buffer[i] = (WHITE.r << 24) |
                        (WHITE.g << 16) |
                        (WHITE.b << 8) |
                        (WHITE.a << 0);
        }
    } else if ((*IO_DISPCNT & 0b111) == 3) {
        // Mode 3

        for (int i = 0; i < VIDEO_BUFFER_SIZE; ++i) {
            u16 pixel = VRAM[i];
            u8 r = ((pixel >> 0)  & 0x1F) << 3;
            u8 g = ((pixel >> 5)  & 0x1F) << 3;
            u8 b = ((pixel >> 10) & 0x1F) << 3;
            u8 a = 0xFF;

            buffer[i] = (r << 24) |
                        (g << 16) |
                        (b << 8) |
                        (a << 0);
        }
    }
}


int main(int argc, char *argv[])
{
    init_gba();
    
    // char *filename = "Donkey Kong Country 2.gba";
    char *filename = "gba-plane.gba";
    int error = load_cartridge_into_memory(filename);
    if (error) {
        exit(1);
    }
    
    // CartridgeHeader *header = (CartridgeHeader *)memory.game_pak_rom;
    // printf("fixed_value = 0x%08X, expected = 0x96\n", header->fixed_value);


    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, filename);
    // SetTargetFPS(60);

    u32 *video_buffer = (u32 *)malloc(VIDEO_BUFFER_SIZE*sizeof(u32));

    // Main loop
    while (!WindowShouldClose()) {
        text_drawn = 0;

        if (IsKeyPressed(KEY_P)) {
            paused = !paused;
        }

        if (!paused) {
            run();
        }
        

        BackgroundControl bg0cnt = {0};
        BackgroundControl bg1cnt = {0};
        BackgroundControl bg2cnt = {0};
        BackgroundControl bg3cnt = {0};
        parse_background_layer_configuration(&bg0cnt, *IO_BG0CNT);
        parse_background_layer_configuration(&bg1cnt, *IO_BG1CNT);
        parse_background_layer_configuration(&bg2cnt, *IO_BG2CNT);
        parse_background_layer_configuration(&bg3cnt, *IO_BG3CNT);

        
        //
        // Draw buffer
        //
        fill_video_buffer(video_buffer);
#if 1
        BeginDrawing();
            for (int i = 0; i < SCREEN_HEIGHT; i++) {
                for (int j = 0; j < SCREEN_WIDTH; j++) {
                    int index = (i * SCREEN_WIDTH) + j;
                    u32 rgba = video_buffer[index];
                    Color color = (Color) {
                        .r = (rgba >> 24) & 0xFF,
                        .g = (rgba >> 16) & 0xFF,
                        .b = (rgba >> 8) & 0xFF,
                        .a = (rgba >> 0) & 0xFF,
                    };
                    
                    float x = (float)j*SCALE;
                    float y = (float)i*SCALE;
                    float width = SCALE;
                    float height = SCALE;

                    Rectangle pixel = { x, y, width, height };
                    DrawRectangleRec(pixel, color);
                }
            }

            if (paused) {
                DrawText("Paused", (int)(WINDOW_WIDTH*0.5), (int)(WINDOW_HEIGHT*0.5), 40, GREEN);
            }

            DRAW_TEXT("Cycles = %lld", cpu->cycles);
            DRAW_TEXT("Frame = %d", current_frame);
            DRAW_TEXT("GetFPS() = %d", GetFPS());

            DRAW_TEXT("IO_DISPCNT = 0x%08X", *IO_DISPCNT);
            DRAW_TEXT("IO_BG0CNT = 0x%08X", *IO_BG0CNT);
            DRAW_TEXT("IO_BG1CNT = 0x%08X", *IO_BG1CNT);
            DRAW_TEXT("IO_BG2CNT = 0x%08X", *IO_BG2CNT);
            DRAW_TEXT("IO_BG3CNT = 0x%08X", *IO_BG3CNT);

            // CPU state
            DRAW_TEXT("");
            DRAW_TEXT(" r0: 0x%08X     r1: 0x%08X     r2: 0x%08X     r3: 0x%08X", cpu->r0, cpu->r1, cpu->r2, cpu->r3);
            DRAW_TEXT(" r4: 0x%08X     r5: 0x%08X     r6: 0x%08X     r7: 0x%08X", cpu->r4, cpu->r5, cpu->r6, cpu->r7);
            DRAW_TEXT(" r8: 0x%08X     r9: 0x%08X    r10: 0x%08X    r11: 0x%08X", cpu->r8, cpu->r9, cpu->r10, cpu->r11);
            DRAW_TEXT("r12: 0x%08X    r13: 0x%08X    r14: 0x%08X    r15: 0x%08X", cpu->r12, cpu->r13, cpu->r14, cpu->r15);
            DRAW_TEXT("CPSR: 0x%08X", cpu->cpsr);
            DRAW_TEXT("0x%08X: 0x%08X", decoded_instruction.address, decoded_instruction.encoding);

            if (found) {
                DRAW_TEXT("FOUND");
            }

        EndDrawing();
#endif

    }

#ifdef _DEBUG
    print_cpu_state(cpu);

    printf("Exit OK\n");
#endif

    CloseWindow();

    return 0;
}
