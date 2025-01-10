#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <assert.h>


#define KILOBYTE (1024)
#define MEGABYTE (1024*1024)

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;


#define true 1
#define false 0
typedef int bool;


#ifdef _DEBUG
#define DEBUG_PRINT(format, ...) printf(format, __VA_ARGS__)
#else
#define DEBUG_PRINT(...)
#endif

typedef struct CPU {
    union {
        struct {
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
        };
        u32 r[16];
    };

    u32 cpsr; // Current Program Status Register
    // TODO: how to store the banked registers.
} CPU;

CPU cpu = {0};

int pc_incremented = 0;

//
// Control Bits
//
#define CONTROL_BITS_MODE       ((cpu.cpsr >> 0) & 0b11111)    /* Mode bits */
#define CONTROL_BITS_T          ((cpu.cpsr >> 5) & 1)          /* State bit */
#define CONTROL_BITS_F          ((cpu.cpsr >> 6) & 1)          /* FIQ disable */
#define CONTROL_BITS_I          ((cpu.cpsr >> 7) & 1)          /* IRQ disable */

//
// Codition Code Flags
//
#define CONDITION_V             ((cpu.cpsr >> 28) & 1)     /* Overflow */
#define CONDITION_C             ((cpu.cpsr >> 29) & 1)     /* Carry or borrow extended */
#define CONDITION_Z             ((cpu.cpsr >> 30) & 1)     /* Zero */
#define CONDITION_N             ((cpu.cpsr >> 31) & 1)     /* Negative or less than */

#define SET_CONDITION_V(bit)    (cpu.cpsr = ((cpu.cpsr & ~(1 << 28)) | ((bit) & 1) << 28))
#define SET_CONDITION_C(bit)    (cpu.cpsr = ((cpu.cpsr & ~(1 << 29)) | ((bit) & 1) << 29))
#define SET_CONDITION_Z(bit)    (cpu.cpsr = ((cpu.cpsr & ~(1 << 30)) | ((bit) & 1) << 30))
#define SET_CONDITION_N(bit)    (cpu.cpsr = ((cpu.cpsr & ~(1 << 31)) | ((bit) & 1) << 31))


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

static u8 *
get_memory_region_at(int at)
{
    // General Internal Memory
    if (at <= 0x00003FFF) return (memory.bios_system_rom + (at - 0x00000000));
    if (at <= 0x01FFFFFF) assert(!"Invalid memory");
    if (at <= 0x0203FFFF) return (memory.ewram + (at - 0x02000000));
    if (at <= 0x02FFFFFF) assert(!"Invalid memory");
    if (at <= 0x03007FFF) return (memory.iwram + (at - 0x03000000));
    if (at <= 0x03FFFFFF) assert(!"Invalid memory");
    if (at <= 0x040003FE) return (memory.io_registers + (at - 0x04000000));
    if (at <= 0x04FFFFFF) assert(!"Invalid memory");

    // Internal Display Memory
    if (at <= 0x050003FF) return (memory.bg_obj_palette_ram + (at - 0x05000000));
    if (at <= 0x05FFFFFF) assert(!"Invalid memory");
    if (at <= 0x06017FFF) return (memory.vram + (at - 0x06000000));
    if (at <= 0x06FFFFFF) assert(!"Invalid memory");
    if (at <= 0x070003FF) return (memory.oam_obj_attributes + (at - 0x07000000));
    if (at <= 0x07FFFFFF) assert(!"Invalid memory");

    // External Memory (Game Pak)
    if (at <= 0x09FFFFFF) return (memory.game_pak_rom + (at - 0x08000000));
    if (at <= 0x0BFFFFFF) assert(!"game_pak not handle");
    if (at <= 0x0DFFFFFF) assert(!"game_pak not handle");
    if (at <= 0x0E00FFFF) return (memory.game_pak_ram + (at - 0x0E000000));


    assert(!"Invalid memory");
    return 0;
}


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


#define INSTRUCTION_FORMAT_DATA_PROCESSING                              (0)
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


typedef enum ShiftType {
    SHIFT_TYPE_LOGICAL_LEFT     = 0b00,
    SHIFT_TYPE_LOGICAL_RIGHT    = 0b01,
    SHIFT_TYPE_ARITHMETIC_RIGHT = 0b10,
    SHIFT_TYPE_ROTATE_RIGHT     = 0b11,
} ShiftType;

typedef enum Condition {
    CONDITION_EQ = 0b0000,
    CONDITION_NE = 0b0001,
    CONDITION_CS = 0b0010,
    CONDITION_CC = 0b0011,
    CONDITION_MI = 0b0100,
    CONDITION_PL = 0b0101,
    CONDITION_VS = 0b0110,
    CONDITION_VC = 0b0111,
    CONDITION_HI = 0b1000,
    CONDITION_LS = 0b1001,
    CONDITION_GE = 0b1010,
    CONDITION_LT = 0b1011,
    CONDITION_GT = 0b1100,
    CONDITION_LE = 0b1101,
    CONDITION_AL = 0b1110,
} Condition;

typedef enum InstructionType {
    INSTRUCTION_NONE,

    // Branch
    INSTRUCTION_B,
    INSTRUCTION_BX,

    // Data Processing
    INSTRUCTION_AND,
    INSTRUCTION_EOR,
    INSTRUCTION_SUB,
    INSTRUCTION_RSB,
    INSTRUCTION_ADD,
    INSTRUCTION_ADC,
    INSTRUCTION_SBC,
    INSTRUCTION_RSC,
    INSTRUCTION_TST,
    INSTRUCTION_TEQ,
    INSTRUCTION_CMP,
    INSTRUCTION_CMN,
    INSTRUCTION_ORR,
    INSTRUCTION_MOV,
    INSTRUCTION_BIC,
    INSTRUCTION_MVN,

    // PSR Transfer
    INSTRUCTION_MRS,
    INSTRUCTION_MSR,

    // Multiply
    INSTRUCTION_MUL,
    INSTRUCTION_MLA,
    INSTRUCTION_MULL,
    INSTRUCTION_MLAL,

    // Single Data Transfer
    INSTRUCTION_LDR,
    INSTRUCTION_STR,

    // Halfword and signed data transfer
    
    // TODO: Used to differentiate the instructions with immediate offset against register offset. Maybe change this later.
    INSTRUCTION_LDRH_IMM,
    INSTRUCTION_STRH_IMM,
    INSTRUCTION_LDRSB_IMM,
    INSTRUCTION_LDRSH_IMM,
    
    INSTRUCTION_LDRH,
    INSTRUCTION_STRH,
    INSTRUCTION_LDRSB,
    INSTRUCTION_LDRSH,

    // Block Data Transfer
    INSTRUCTION_LDM,
    INSTRUCTION_STM,

    // Single Data Swap
    INSTRUCTION_SWP,

    // Software Interrupt
    INSTRUCTION_SWI,

    // Coprocessor Data Operations
    INSTRUCTION_CDP,

    // Coprocessor Data Transfers
    INSTRUCTION_STC,
    INSTRUCTION_LDC,

    // Coprocessor Register Transfers
    INSTRUCTION_MCR,
    INSTRUCTION_MRC,
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
    Condition condition;
    int offset;
    u8 L;
    u8 S;
    u8 rn;
    u8 I;
    u8 P;
    u8 U;
    u8 W;
    u8 B;
    u8 A;
    u8 H;
    u16 second_operand;
    u8 rd;
    u8 rm;
    u8 rs;
    u8 rdhi;
    u8 rdlo;
    u16 source_operand;
} Instruction;

Instruction decoded_instruction;

static bool
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
        case CONDITION_LS: return (CONDITION_C == 0 && CONDITION_Z == 1);
        case CONDITION_GE: return (CONDITION_N == CONDITION_V);
        case CONDITION_LT: return (CONDITION_N != CONDITION_V);
        case CONDITION_GT: return (CONDITION_Z == 0 && CONDITION_N == CONDITION_V);
        case CONDITION_LE: return (CONDITION_Z == 1 || CONDITION_N != CONDITION_V);
        case CONDITION_AL: return true; // Always
        default: {
            fprintf(stderr, "Unexpected condition: %x\n", condition);
            exit(1);
        }
    }
}

static int
apply_shift(u32 value, u8 shift, ShiftType shift_type, u8 *carry)
{
    switch (shift_type) {
        case SHIFT_TYPE_LOGICAL_LEFT: {
            *carry = (value >> (32 - shift)) & 1;

            return value << shift;
        } break;
        case SHIFT_TYPE_LOGICAL_RIGHT: {
            *carry = (value >> (shift - 1) & 1);
            
            return value >> shift;
        } break;
        case SHIFT_TYPE_ARITHMETIC_RIGHT: {
            *carry = (value >> (shift - 1) & 1);
            
            u8 msb = (value >> 31) & 1;
            u32 msb_replicated = (-msb << (32 - shift));

            return (value >> shift) | msb_replicated;
        } break;
        case SHIFT_TYPE_ROTATE_RIGHT: {
            *carry = (value >> (shift - 1) & 1);
            
            u32 value_to_rotate = value & ((1 << shift) - 1);
            u32 rotate_masked = value_to_rotate << (32 - shift);

            return (value >> shift) | rotate_masked;
        } break;
    }

    return value;
}

static u32
get_second_operand(u8 *carry)
{
    int second_operand;
    if (decoded_instruction.I) {
        u8 imm = decoded_instruction.second_operand & 0xFF;
        u8 rotate = (decoded_instruction.second_operand >> 8) & 0xF;

        second_operand = apply_shift(imm, rotate, SHIFT_TYPE_ROTATE_RIGHT, carry);
    } else {
        int rm = decoded_instruction.second_operand & 0xF;
        u8 shift = (decoded_instruction.second_operand >> 4) & 0xFF;
        u8 shift_type = (ShiftType)((shift >> 1) & 0b11);
        if (shift & 1) {
            // Shift register
            u8 rs = (shift >> 4) & 0xF ; // Register to the value to shift.
            second_operand = apply_shift(cpu.r[rm], (u8)(cpu.r[rs] & 0xF), shift_type, carry);
        } else {
            // Shift amount
            u8 shift_amount = (shift >> 3) & 0b11111;
            second_operand = apply_shift(cpu.r[rm], shift_amount, shift_type, carry);
        }
    }

    return second_operand;
}

#define DATA_PROCESSING(expression)                                 \
    u8 carry = 0;                                                   \
    /*u32 second_operand = get_second_operand(&carry);*/                \
    u32 result = (expression);                                      \
                                                                    \
    if (decoded_instruction.S && decoded_instruction.rd != 15) {    \
        SET_CONDITION_C(carry);                                     \
        SET_CONDITION_Z(result == 0);                               \
        SET_CONDITION_N(result >> 31);                              \
    }

static void
execute()
{
    if (decoded_instruction.type == INSTRUCTION_NONE) return;
    if (!should_execute_instruction(decoded_instruction.condition)) {
        // printf("Condition %d...Skipped\n", decoded_instruction.condition);
        return;
    }

    switch (decoded_instruction.type) {
        // Branch
        case INSTRUCTION_B: {
            DEBUG_PRINT("INSTRUCTION_B\n");
            if (decoded_instruction.L) {
                cpu.lr = cpu.pc - 1;
            }

            cpu.pc += (decoded_instruction.offset << 2);
            current_instruction = 0;
        } break;

        // Data processing
        case INSTRUCTION_ADD: {
            DEBUG_PRINT("INSTRUCTION_ADD\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] + get_second_operand(&carry));
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_AND: {
            DEBUG_PRINT("INSTRUCTION_AND\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] & get_second_operand(&carry));
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_EOR: {
            DEBUG_PRINT("INSTRUCTION_EOR\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] ^ get_second_operand(&carry));
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_SUB: {
            DEBUG_PRINT("INSTRUCTION_SUB\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] - get_second_operand(&carry));
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_RSB: {
            DEBUG_PRINT("INSTRUCTION_RSB\n");
            DATA_PROCESSING(get_second_operand(&carry) - cpu.r[decoded_instruction.rn]);
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_ADC: {
            DEBUG_PRINT("INSTRUCTION_ADC\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] + get_second_operand(&carry) + carry);
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_SBC: {
            DEBUG_PRINT("INSTRUCTION_SBC\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] - get_second_operand(&carry) + carry - 1);
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_RSC: {
            DEBUG_PRINT("INSTRUCTION_RSC\n");
            DATA_PROCESSING(get_second_operand(&carry) - cpu.r[decoded_instruction.rn] + carry - 1);
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_TST: {
            DEBUG_PRINT("INSTRUCTION_TST\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] & get_second_operand(&carry));
        } break;
        case INSTRUCTION_TEQ: {
            DEBUG_PRINT("INSTRUCTION_TEQ\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] ^ get_second_operand(&carry));
        } break;
        case INSTRUCTION_CMP: {
            DEBUG_PRINT("INSTRUCTION_CMP\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] - get_second_operand(&carry));
        } break;
        case INSTRUCTION_CMN: {
            DEBUG_PRINT("INSTRUCTION_CMN\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] + get_second_operand(&carry));
        } break;
        case INSTRUCTION_ORR: {
            DEBUG_PRINT("INSTRUCTION_ORR\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] | get_second_operand(&carry));
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_MOV: {
            DEBUG_PRINT("INSTRUCTION_MOV\n");
            DATA_PROCESSING(get_second_operand(&carry));
            // u8 carry = 0;                                                   
            // /*u32 second_operand = get_second_operand(&carry);*/                
            // u32 result = (get_second_operand(&carry));                                      
                                                                            
            // if (decoded_instruction.S && decoded_instruction.rd != 15) {    
            //     SET_CONDITION_C(carry);                                     
            //     SET_CONDITION_Z(result == 0);                               
            //     SET_CONDITION_N(result >> 31);                              
            // }
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_BIC: {
            DEBUG_PRINT("INSTRUCTION_BIC\n");
            DATA_PROCESSING(cpu.r[decoded_instruction.rn] & !get_second_operand(&carry));
            
            cpu.r[decoded_instruction.rd] = result;
        } break;
        case INSTRUCTION_MVN: {
            DEBUG_PRINT("INSTRUCTION_MVN\n");
            DATA_PROCESSING(!get_second_operand(&carry));
            
            cpu.r[decoded_instruction.rd] = result;
        } break;

        // PSR Transfer
        case INSTRUCTION_MRS: {
            DEBUG_PRINT("INSTRUCTION_MRS\n");
            int x = 5;
        } break;
        case INSTRUCTION_MSR: {
            DEBUG_PRINT("INSTRUCTION_MSR\n");
            int x = 5;
        } break;

        // Multiply
        case INSTRUCTION_MUL: {
            DEBUG_PRINT("INSTRUCTION_MUL\n");
            int x = 5;
        } break;
        case INSTRUCTION_MLA: {
            DEBUG_PRINT("INSTRUCTION_MLA\n");
            int x = 5;
        } break;
        case INSTRUCTION_MULL: {
            DEBUG_PRINT("INSTRUCTION_MULL\n");
            int x = 5;
        } break;
        case INSTRUCTION_MLAL: {
            DEBUG_PRINT("INSTRUCTION_MLAL\n");
            int x = 5;
        } break;

        // Single Data Transfer
        case INSTRUCTION_LDR: {
            DEBUG_PRINT("INSTRUCTION_LDR\n");
            int x = 5;
        } break;
        case INSTRUCTION_STR: {
            DEBUG_PRINT("INSTRUCTION_STR\n");
            int x = 5;
        } break;



        // Halfword and signed data transfer
#define UPDATE_BASE_OFFSET()            \
    do {                                \
        if (decoded_instruction.U) {    \
            base += offset;             \
        } else {                        \
            base -= offset;             \
        }                               \
    } while (0)


        case INSTRUCTION_LDRH_IMM: {
            DEBUG_PRINT("INSTRUCTION_LDRH_IMM\n");
            int x = 5;
        } break;
        case INSTRUCTION_STRH_IMM: {
            DEBUG_PRINT("INSTRUCTION_STRH_IMM\n");
            int x = 5;
        } break;
        case INSTRUCTION_LDRSB_IMM: {
            DEBUG_PRINT("INSTRUCTION_LDRSB_IMM\n");
            int x = 5;
        } break;
        case INSTRUCTION_LDRSH_IMM: {
            DEBUG_PRINT("INSTRUCTION_LDRSH_IMM\n");
            int x = 5;
        } break;
        
        case INSTRUCTION_LDRH: {
            DEBUG_PRINT("INSTRUCTION_LDRH\n");

            int base = cpu.r[decoded_instruction.rn];
            int offset = cpu.r[decoded_instruction.rm];
            
            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();

                u8 *memory_region = get_memory_region_at(base);
                cpu.r[decoded_instruction.rd] = *((u16 *)memory_region);

                if (decoded_instruction.W) {
                    cpu.r[decoded_instruction.rn] = base;
                }
            } else {
                u8 *memory_region = get_memory_region_at(base);
                cpu.r[decoded_instruction.rd] = *((u16 *)memory_region);

                UPDATE_BASE_OFFSET();
                cpu.r[decoded_instruction.rn] = base;
            }


        } break;
        case INSTRUCTION_STRH: {
            DEBUG_PRINT("INSTRUCTION_STRH\n");
            int x = 5;
        } break;
        case INSTRUCTION_LDRSB: {
            DEBUG_PRINT("INSTRUCTION_LDRSB\n");
            
            int base = cpu.r[decoded_instruction.rn];
            int offset = cpu.r[decoded_instruction.rm];
            
            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();

                u8 *memory_region = get_memory_region_at(base);
                u8 value = *memory_region;
                u8 sign = (value >> 7) & 1;
                u32 value_sign_extended = (-sign << 31) | value;

                cpu.r[decoded_instruction.rd] = value_sign_extended;

                if (decoded_instruction.W) {
                    cpu.r[decoded_instruction.rn] = base;
                }
            } else {
                u8 *memory_region = get_memory_region_at(base);
                u8 value = *memory_region;
                u8 sign = (value >> 7) & 1;
                u32 value_sign_extended = (-sign << 31) | value;

                cpu.r[decoded_instruction.rd] = value_sign_extended;

                UPDATE_BASE_OFFSET();
                cpu.r[decoded_instruction.rn] = base;
            }
        } break;
        case INSTRUCTION_LDRSH: {
            DEBUG_PRINT("INSTRUCTION_LDRSH\n");
            int x = 5;
        } break;

#undef UPDATE_BASE_OFFSET



        // Block Data Transfer
        case INSTRUCTION_LDM: {
            DEBUG_PRINT("INSTRUCTION_LDM\n");
            u32 address = cpu.r[decoded_instruction.rn];
        } break;

        case INSTRUCTION_STM: {
            DEBUG_PRINT("INSTRUCTION_STM\n");
            u32 address = cpu.r[decoded_instruction.rn];
        } break;

        // Single Data Swap
        case INSTRUCTION_SWP: {
            DEBUG_PRINT("INSTRUCTION_SWP\n");
            int x = 5;
        } break;

        // Software Interrupt
        case INSTRUCTION_SWI: {
            DEBUG_PRINT("INSTRUCTION_SWI\n");
            int x = 5;
        } break;

        // Coprocessor Data Operations
        case INSTRUCTION_CDP: {
            DEBUG_PRINT("INSTRUCTION_CDP\n");
            int x = 5;
        } break;

        // Coprocessor Data Transfers
        case INSTRUCTION_STC: {
            DEBUG_PRINT("INSTRUCTION_STC\n");
            int x = 5;
        } break;
        case INSTRUCTION_LDC: {
            DEBUG_PRINT("INSTRUCTION_LDC\n");
            int x = 5;
        } break;

        // Coprocessor Register Transfers
        case INSTRUCTION_MCR: {
            DEBUG_PRINT("INSTRUCTION_MCR\n");
            int x = 5;
        } break;
        case INSTRUCTION_MRC: {
            DEBUG_PRINT("INSTRUCTION_MRC\n");
            int x = 5;
        } break;
    }

    decoded_instruction = (Instruction){0};
}


static void
decode()
{
    if (current_instruction == 0) return;

    if ((current_instruction & INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT) == INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT) {
        // printf("INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT: 0x%x\n", current_instruction);

        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_SWI,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_COPROCESSOR_REGISTER_TRANSFER) == INSTRUCTION_FORMAT_COPROCESSOR_REGISTER_TRANSFER) {
        // printf("INSTRUCTION_FORMAT_COPROCESSOR_REGISTER_TRANSFER: 0x%x\n", current_instruction);
        
        // TODO: Not handle at the moment. See if this is for multicable support.
        u8 L = (current_instruction >> 20) & 1;
        InstructionType type = 0;
        switch (L) {
            case 0: INSTRUCTION_MCR; break;
            case 1: INSTRUCTION_MRC; break;
        }

        decoded_instruction = (Instruction) {
            .type = type,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_COPROCESSOR_DATA_OPERATION) == INSTRUCTION_FORMAT_COPROCESSOR_DATA_OPERATION) {
        // printf("INSTRUCTION_FORMAT_COPROCESSOR_DATA_OPERATION: 0x%x\n", current_instruction);

        // TODO: Not handle at the moment. See if this is for multicable support.
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_CDP,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_COPROCESSOR_DATA_TRANSFER) == INSTRUCTION_FORMAT_COPROCESSOR_DATA_TRANSFER) {
        // printf("INSTRUCTION_FORMAT_COPROCESSOR_DATA_TRANSFER: 0x%x\n", current_instruction);

        // TODO: Not handle at the moment. See if this is for multicable support.
        u8 L = (current_instruction >> 20) & 1;
        InstructionType type = 0;
        switch (L) {
            case 0: INSTRUCTION_STC; break;
            case 1: INSTRUCTION_LDC; break;
        }

        decoded_instruction = (Instruction) {
            .type = type,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_BRANCH) == INSTRUCTION_FORMAT_BRANCH) {
        // printf("INSTRUCTION_FORMAT_BRANCH: 0x%x\n", current_instruction);
        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_B,
            .offset = current_instruction & 0xFFFFFF,
            .L = (u8)((current_instruction >> 24) & 1),
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_BLOCK_DATA_TRANSFER) == INSTRUCTION_FORMAT_BLOCK_DATA_TRANSFER) {
        // printf("INSTRUCTION_FORMAT_BLOCK_DATA_TRANSFER: 0x%x\n", current_instruction);

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
            .second_operand = current_instruction & 0xFFFF,
        };
    }
    // else if ((current_instruction &INSTRUCTION_FORMAT_UNDEFINED) == INSTRUCTION_FORMAT_UNDEFINED) {
    //     printf("INSTRUCTION_FORMAT_UNDEFINED: 0x%x\n", current_instruction);
    // }
    else if ((current_instruction & INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER) == INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER) {
        // printf("INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER: 0x%x\n", current_instruction);

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
        // printf("INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_IMMEDIATE_OFFSET: 0x%x\n", current_instruction);

        u8 H = (current_instruction >> 5) & 1;
        u8 S = (current_instruction >> 6) & 1;

        if (S == 0 && H == 0) goto SWP;

        u8 L = (current_instruction >> 20) & 1;
        InstructionType type = 0;
        if (S == 0 && H == 1) {
            if (L) {
                // load
                type = INSTRUCTION_LDRH_IMM;
            } else {
                type = INSTRUCTION_STRH_IMM;
            }
        } else if (S == 1 && H == 0) {
            type = INSTRUCTION_LDRSB_IMM;
        } else {
            type = INSTRUCTION_LDRSH_IMM;
        }

        decoded_instruction = (Instruction) {
            .type = type,
            .offset = ((current_instruction >> 4) & 0xF0) | (current_instruction & 0xF),
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
    else if ((current_instruction & INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET) == INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET) {
        // printf("INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET: 0x%x\n", current_instruction);

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
        // printf("INSTRUCTION_FORMAT_BRANCH_AND_EXCHANGE: 0x%x\n", current_instruction);

        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_BX,
            .rn = (current_instruction & 0xF),
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_SINGLE_DATA_SWAP) == INSTRUCTION_FORMAT_SINGLE_DATA_SWAP) {
SWP:
        // printf("INSTRUCTION_FORMAT_SINGLE_DATA_SWAP: 0x%x\n", current_instruction);

        decoded_instruction = (Instruction) {
            .type = INSTRUCTION_SWP,
            .rm = current_instruction & 0xF,
            .rd = (current_instruction >> 12) & 0xF,
            .rn = (current_instruction >> 16) & 0xF,
            .B = (current_instruction >> 22) & 1,
        };
    }
    else if ((current_instruction & INSTRUCTION_FORMAT_MULTIPLY_LONG) == INSTRUCTION_FORMAT_MULTIPLY_LONG) {
        // printf("INSTRUCTION_FORMAT_MULTIPLY_LONG: 0x%x\n", current_instruction);

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
        // printf("INSTRUCTION_FORMAT_MULTIPLY: 0x%x\n", current_instruction);
        
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
        // printf("INSTRUCTION_FORMAT_DATA_PROCESSING: 0x%x\n", current_instruction);
        
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
                    };
                } break;
                case 0b101000: {
                    decoded_instruction = (Instruction) {
                        .type = INSTRUCTION_MSR,
                        .P = (current_instruction >> 22) & 1,
                        .source_operand = current_instruction & 0xFFF,
                    };
                } break;
            }
        }
        else
        {
            decoded_instruction = (Instruction) {
                .type = type,
                .S = S, // Set condition codes
                .I = (current_instruction >> 25) & 1, // Immediate operand
                .rn = (current_instruction >> 16) & 0xF, // Source register
                .rd = (current_instruction >> 12) & 0xF, // Destination register
                // TODO: check the docs when writing into R15 (PC) register.
                .second_operand = current_instruction & ((1 << 12) - 1),
            };
        }

    } else {
        fprintf(stderr, "Instruction unknown: 0x%x\n", current_instruction);
        exit(1);
    }


    decoded_instruction.condition = (current_instruction >> 28) & 0xF;;

    current_instruction = 0;
}

static void
fetch()
{
    current_instruction = ((u32 *)memory.game_pak_rom)[cpu.pc++];
    pc_incremented++;
}

static void
process_instructions()
{
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
