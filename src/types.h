#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

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

#if _DEBUG
#define assert(expression)                                                                                  \
    do {                                                                                                    \
        if (!(expression)) {                                                                                \
            print_cpu_state(cpu);                                                                           \
            fflush(stdout);                                                                                 \
            fflush(stderr);                                                                                 \
            fprintf(stderr, "Assertion failed: %s, file %s, line %d\n", #expression, __FILE__, __LINE__);   \
            *((int *)0) = 0;                                                                                \
        }                                                                                                   \
    } while (0)
#else
#define assert(expression)
#endif


typedef struct CPU {
    union {
        struct {
            // Unbanked registers
            u32 r0;
            u32 r1;
            u32 r2;
            u32 r3;
            u32 r4;
            u32 r5;
            u32 r6;
            u32 r7;
            
            // Banked registers
            u32 r8;
            u32 r9;
            u32 r10;
            u32 r11;
            u32 r12;
            union {
                u32 r13;
                u32 sp;
            };
            union {
                u32 r14;
                u32 lr;
            };

            // Unbanked
            union {
                u32 r15;
                u32 pc;
            };
        };
        u32 r[16];
    };

    // FIQ Banked registers
    union {
        struct {
            u32 r8_fiq;
            u32 r9_fiq;
            u32 r10_fiq;
            u32 r11_fiq;
            u32 r12_fiq;
            u32 r13_fiq;
            u32 r14_fiq;
        };
        u32 r_fiq[8]; // Get using r_fiq[r_number - 8] to get the correct offset.
    };

    // Supervisor Banked registers
    union {
        struct {
            u32 r13_svc;
            u32 r14_svc;
        };
        u32 r_svc[2]; // Get using r_svc[r_number - 13] to get the correct offset.
    };

    // Abort Banked registers
    union {
        struct {
            u32 r13_abt;
            u32 r14_abt;
        };
        u32 r_abt[2]; // Get using r_abt[r_number - 13] to get the correct offset.
    };

    // IRQ Banked registers
    union {
        struct {
            u32 r13_irq;
            u32 r14_irq;
        };
        u32 r_irq[2]; // Get using r_irq[r_number - 13] to get the correct offset.
    };

    // Undefined Banked registers
    union {
        struct {
            u32 r13_und;
            u32 r14_und;
        };
        u32 r_und[2]; // Get using r_und[r_number - 13] to get the correct offset.
    };


    u32 cpsr; // Current Program Status Register
    
    // Saved Program Status Register
    u32 spsr_fiq;
    u32 spsr_irq;
    u32 spsr_svc;
    u32 spsr_abt;
    u32 spsr_und;
} CPU;

#define MODE_USER       (0b10000)
#define MODE_FIQ        (0b10001)
#define MODE_IRQ        (0b10010)
#define MODE_SUPERVISOR (0b10011)
#define MODE_ABORT      (0b10111)
#define MODE_UNDEFINED  (0b11011)
#define MODE_SYSTEM     (0b11111)

char *psr_mode[] = {
    [MODE_USER]         = "USER",
    [MODE_FIQ]          = "FIQ",
    [MODE_IRQ]          = "IRQ",
    [MODE_SUPERVISOR]   = "SUPERVISOR",
    [MODE_ABORT]        = "ABORT",
    [MODE_UNDEFINED]    = "UNDEFINED",
    [MODE_SYSTEM]       = "SYSTEM",
};


static void
num_to_binary_32(char *buffer, u32 num)
{
    int i = 0;
    while (i < 32) {
        buffer[i++] = '0' + ((num >> 31) & 1);
        num <<= 1;
    }
    buffer[i] = '\0';
}

void
print_cpu_state(CPU *cpu)
{
    printf("----------------\n");
    printf("Registers:\n");
    for (int i = 0; i < 16; i++) {
        printf("    r[%d] = 0x%X\n", i, cpu->r[i]);
    }
    printf("----------------\n");
    printf("PC = 0x%X\n", cpu->pc);

    char cpsr_buffer[33];
    num_to_binary_32(cpsr_buffer, cpu->cpsr);
    printf("CPSR = 0x%X = %s\n", cpu->cpsr, cpsr_buffer);

    printf("Condition flags: ");
    if ((cpu->cpsr >> 31) & 1) printf("N"); else printf("-");
    if ((cpu->cpsr >> 30) & 1) printf("Z"); else printf("-");
    if ((cpu->cpsr >> 29) & 1) printf("C"); else printf("-");
    if ((cpu->cpsr >> 28) & 1) printf("V"); else printf("-");
    
    printf("\n");
    printf("Control bits: ");
    if ((cpu->cpsr >> 7) & 1) printf("I"); else printf("-");
    if ((cpu->cpsr >> 6) & 1) printf("F"); else printf("-");
    if ((cpu->cpsr >> 5) & 1) printf("T"); else printf("-");

    printf("\n");
    printf("  Mode: %s: ", psr_mode[cpu->cpsr & 0b11111]);
    if ((cpu->cpsr >> 4) & 1) printf("1"); else printf("0");
    if ((cpu->cpsr >> 3) & 1) printf("1"); else printf("0");
    if ((cpu->cpsr >> 2) & 1) printf("1"); else printf("0");
    if ((cpu->cpsr >> 1) & 1) printf("1"); else printf("0");
    if ((cpu->cpsr >> 0) & 1) printf("1"); else printf("0");


    printf("\n");
    printf("----------------\n");
}

bool
in_privileged_mode(CPU *cpu)
{
    u8 mode = (cpu->cpsr & 0b11111);
    switch (mode) {
        case MODE_USER:
            return false;
        case MODE_FIQ:
        case MODE_IRQ:
        case MODE_SUPERVISOR:
        case MODE_ABORT:
        case MODE_UNDEFINED:
        case MODE_SYSTEM:
            return true;
        default: assert(!"Unknown mode");
    }

    return false; // Unreachable
}

bool
current_mode_has_spsr(CPU *cpu)
{
    u8 mode = (cpu->cpsr & 0b11111);
    switch (mode) {
        case MODE_USER:
        case MODE_SYSTEM:
            return false;
        case MODE_FIQ:
        case MODE_IRQ:
        case MODE_SUPERVISOR:
        case MODE_ABORT:
        case MODE_UNDEFINED:
            return true;
        default: assert(!"Unknown mode");
    }

    return false; // Unreachable
}

u32 *
get_spsr_current_mode(CPU *cpu)
{
    u8 mode = (cpu->cpsr & 0b11111);
    switch (mode) {
        case MODE_FIQ:          return &cpu->spsr_fiq;
        case MODE_SUPERVISOR:   return &cpu->spsr_svc;
        case MODE_ABORT:        return &cpu->spsr_abt;
        case MODE_IRQ:          return &cpu->spsr_irq;
        case MODE_UNDEFINED:    return &cpu->spsr_und;
        default: assert(!"User and System mode does not have SPSR");
    }

    return 0;
}

u32 *
get_register(CPU *cpu, u8 rn)
{
    u8 mode = (cpu->cpsr & 0b11111);
    switch (mode) {
        case MODE_USER:
        case MODE_SYSTEM:
        {
            return cpu->r + rn;
        } break;
        case MODE_FIQ: {
            if (rn <= 7 || rn == 15) {
                return cpu->r + rn;
            }

            return cpu->r_fiq + (rn - 8);
        } break;
        case MODE_SUPERVISOR: {
            if (rn <= 12 || rn == 15) {
                return cpu->r + rn;
            }

            return cpu->r_svc + (rn - 13);
        } break;
        case MODE_ABORT: {
            if (rn <= 12 || rn == 15) {
                return cpu->r + rn;
            }

            return cpu->r_abt + (rn - 13);
        } break;
        case MODE_IRQ: {
            if (rn <= 12 || rn == 15) {
                return cpu->r + rn;
            }

            return cpu->r_irq + (rn - 13);
        } break;
        case MODE_UNDEFINED: {
            if (rn <= 12 || rn == 15) {
                return cpu->r + rn;
            }

            return cpu->r_und + (rn - 13);
        } break;

        default: assert(!"Invalid mode");
    }

    assert(!"Invalid register number");
    return 0;
}


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
    u8 game_pak_rom_wait_state_1[32*MEGABYTE];
    u8 game_pak_rom_wait_state_2[32*MEGABYTE];
    u8 game_pak_ram[64*MEGABYTE];
    // 0E010000-0FFFFFFF   Not used

    // Unused Memory Area
    // 10000000-FFFFFFFF   Not used (upper 4bits of address bus unused)

} GBAMemory;


u8 *
get_memory_at(CPU *cpu, GBAMemory *gba_memory, u32 at)
{
    // General Internal Memory
    if (at <= 0x00003FFF) return (gba_memory->bios_system_rom + (at - 0x00000000));
    if (at <= 0x01FFFFFF) assert(!"Invalid memory");
    if (at <= 0x0203FFFF) return (gba_memory->ewram + (at - 0x02000000));
    if (at <= 0x02FFFFFF) assert(!"Invalid memory");
    if (at <= 0x03007FFF) return (gba_memory->iwram + (at - 0x03000000));
    if (at <= 0x03FFFFFF) assert(!"Invalid memory");
    if (at <= 0x040003FE) return (gba_memory->io_registers + (at - 0x04000000));
    if (at <= 0x04FFFFFF) assert(!"Invalid memory");

    // Internal Display Memory
    if (at <= 0x050003FF) return (gba_memory->bg_obj_palette_ram + (at - 0x05000000));
    if (at <= 0x05FFFFFF) assert(!"Invalid memory");
    if (at <= 0x06017FFF) return (gba_memory->vram + (at - 0x06000000));
    if (at <= 0x06FFFFFF) assert(!"Invalid memory");
    if (at <= 0x070003FF) return (gba_memory->oam_obj_attributes + (at - 0x07000000));
    if (at <= 0x07FFFFFF) assert(!"Invalid memory");

    // External Memory (Game Pak)
    if (at <= 0x09FFFFFF) return (gba_memory->game_pak_rom + (at - 0x08000000));
    if (at <= 0x0BFFFFFF) return (gba_memory->game_pak_rom_wait_state_1 + (at - 0x0A000000));
    if (at <= 0x0DFFFFFF) return (gba_memory->game_pak_rom_wait_state_2 + (at - 0x0C000000));
    if (at <= 0x0E00FFFF) return (gba_memory->game_pak_ram + (at - 0x0E000000));


    assert(!"Invalid memory");
    return 0;
}

u8 *
get_memory_at_without_exit(CPU *cpu, GBAMemory *gba_memory, u32 at)
{
    // General Internal Memory
    if (at <= 0x00003FFF) return (gba_memory->bios_system_rom + (at - 0x00000000));
    if (at <= 0x01FFFFFF) return 0;
    if (at <= 0x0203FFFF) return (gba_memory->ewram + (at - 0x02000000));
    if (at <= 0x02FFFFFF) return 0;
    if (at <= 0x03007FFF) return (gba_memory->iwram + (at - 0x03000000));
    if (at <= 0x03FFFFFF) return 0;
    if (at <= 0x040003FE) return (gba_memory->io_registers + (at - 0x04000000));
    if (at <= 0x04FFFFFF) return 0;

    // Internal Display Memory
    if (at <= 0x050003FF) return (gba_memory->bg_obj_palette_ram + (at - 0x05000000));
    if (at <= 0x05FFFFFF) return 0;
    if (at <= 0x06017FFF) return (gba_memory->vram + (at - 0x06000000));
    if (at <= 0x06FFFFFF) return 0;
    if (at <= 0x070003FF) return (gba_memory->oam_obj_attributes + (at - 0x07000000));
    if (at <= 0x07FFFFFF) return 0;

    // External Memory (Game Pak)
    if (at <= 0x09FFFFFF) return (gba_memory->game_pak_rom + (at - 0x08000000));
    if (at <= 0x0BFFFFFF) return (gba_memory->game_pak_rom_wait_state_1 + (at - 0x0A000000));
    if (at <= 0x0DFFFFFF) return (gba_memory->game_pak_rom_wait_state_2 + (at - 0x0C000000));
    if (at <= 0x0E00FFFF) return (gba_memory->game_pak_ram + (at - 0x0E000000));


    return 0;
}


#define INSTRUCTION_FORMAT_DATA_PROCESSING                              (0)
#define INSTRUCTION_FORMAT_MULTIPLY                                     (0b0000000000000000000010010000)
#define INSTRUCTION_FORMAT_MULTIPLY_LONG                                (0b0000100000000000000010010000)
#define INSTRUCTION_FORMAT_SINGLE_DATA_SWAP                             (0b0001000000000000000010010000)
#define INSTRUCTION_FORMAT_BRANCH_AND_EXCHANGE                          (0b0001001011111111111100010000)
#define INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET       (0b10010000)
#define INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_IMMEDIATE_OFFSET      (0b0000010000000000000010010000)
#define INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER                         (1 << 26)
#define INSTRUCTION_FORMAT_UNDEFINED                                    ((0b11 << 25) | (1 << 4))
#define INSTRUCTION_FORMAT_BLOCK_DATA_TRANSFER                          (1 << 27)
#define INSTRUCTION_FORMAT_BRANCH                                       (0b101 << 25)
#define INSTRUCTION_FORMAT_COPROCESSOR_DATA_TRANSFER                    (0b11 << 26)
#define INSTRUCTION_FORMAT_COPROCESSOR_DATA_OPERATION                   (0b111 << 25)
#define INSTRUCTION_FORMAT_COPROCESSOR_REGISTER_TRANSFER                ((0b111 << 25) | (1 << 4))
#define INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT                           (0b1111 << 24)


//
// Thumb Instructions
//
#define THUMB_INSTRUCTION_FORMAT_MOVE_SHIFTED_REGISTER                  (0)
#define THUMB_INSTRUCTION_FORMAT_ADD_SUBTRACT                           (0b11 << 11)
#define THUMB_INSTRUCTION_FORMAT_MOVE_COMPARE_ADD_SUBTRACT_IMMEDIATE    (1 << 13)
#define THUMB_INSTRUCTION_FORMAT_ALU_OPERATIONS                         (1 << 14)
#define THUMB_INSTRUCTION_FORMAT_HI_REGISTER_OPERATIONS_BRANCH_EXCHANGE (0b10001 << 10)
#define THUMB_INSTRUCTION_FORMAT_PC_RELATIVE_LOAD                       (0b1001 << 11)
#define THUMB_INSTRUCTION_FORMAT_LOAD_STORE_WITH_REGISTER_OFFSET        (0b101000 << 9)
#define THUMB_INSTRUCTION_FORMAT_LOAD_STORE_SIGN_EXTENDED_BYTE_HALFWORD (0b101001 << 9)
#define THUMB_INSTRUCTION_FORMAT_LOAD_STORE_WITH_IMMEDIATE_OFFSET       (0b11 << 13)
#define THUMB_INSTRUCTION_FORMAT_LOAD_STORE_HALFWORD                    (1 << 15)
#define THUMB_INSTRUCTION_FORMAT_SP_RELATIVE_LOAD_STORE                 (0b1001 << 12)
#define THUMB_INSTRUCTION_FORMAT_LOAD_ADDRESS                           (0b101 << 13)
#define THUMB_INSTRUCTION_FORMAT_ADD_OFFSET_STACK_POINTER               (0b1011 << 12)
#define THUMB_INSTRUCTION_FORMAT_PUSH_POP_REGISTERS                     (0b101101 << 10)
#define THUMB_INSTRUCTION_FORMAT_MULTIPLE_LOAD_STORE                    (0b11 << 14)
#define THUMB_INSTRUCTION_FORMAT_CONDITIONAL_BRANCH                     (0b1101 << 12)
#define THUMB_INSTRUCTION_FORMAT_SOFTWARE_INTERRUPT                     (0b11011111 << 8)
#define THUMB_INSTRUCTION_FORMAT_UNCONDITIONAL_BRANCH                   (0b111 << 13)
#define THUMB_INSTRUCTION_FORMAT_LONG_BRANCH_WITH_LINK                  (0b1111 << 12)



typedef enum ShiftType {
    SHIFT_TYPE_LOGICAL_LEFT     = 0b00,
    SHIFT_TYPE_LOGICAL_RIGHT    = 0b01,
    SHIFT_TYPE_ARITHMETIC_RIGHT = 0b10,
    SHIFT_TYPE_ROTATE_RIGHT     = 0b11,
} ShiftType;

typedef enum ThumbShiftType {
    THUMB_SHIFT_TYPE_LOGICAL_LEFT       = 0,
    THUMB_SHIFT_TYPE_LOGICAL_RIGHT      = 1,
    THUMB_SHIFT_TYPE_ARITHMETIC_RIGHT   = 2,
} ThumbShiftType;

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



    //
    // Thumb instructions
    //

    INSTRUCTION_MOVE_SHIFTED_REGISTER,
    INSTRUCTION_ADD_SUBTRACT,
    INSTRUCTION_MOVE_COMPARE_ADD_SUBTRACT_IMMEDIATE,
    INSTRUCTION_ALU_OPERATIONS,
    INSTRUCTION_HI_REGISTER_OPERATIONS_BRANCH_EXCHANGE,
    INSTRUCTION_PC_RELATIVE_LOAD,
    INSTRUCTION_LOAD_STORE_WITH_REGISTER_OFFSET,
    INSTRUCTION_LOAD_STORE_SIGN_EXTENDED_BYTE_HALFWORD,
    INSTRUCTION_LOAD_STORE_WITH_IMMEDIATE_OFFSET,
    INSTRUCTION_LOAD_STORE_HALFWORD,
    INSTRUCTION_SP_RELATIVE_LOAD_STORE,
    INSTRUCTION_LOAD_ADDRESS,
    INSTRUCTION_ADD_OFFSET_TO_STACK_POINTER,
    INSTRUCTION_PUSH_POP_REGISTERS,
    INSTRUCTION_MULTIPLE_LOAD_STORE,
    INSTRUCTION_CONDITIONAL_BRANCH,
    INSTRUCTION_SOFTWARE_INTERRUPT,
    INSTRUCTION_UNCONDITIONAL_BRANCH,
    INSTRUCTION_LONG_BRANCH_WITH_LINK,
} InstructionType;

char *
get_instruction_type_string(InstructionType type)
{
    switch (type) {
        case INSTRUCTION_NONE: return "INSTRUCTION_NONE";
        case INSTRUCTION_B: return "INSTRUCTION_B";
        case INSTRUCTION_BX: return "INSTRUCTION_BX";
        case INSTRUCTION_AND: return "INSTRUCTION_AND";
        case INSTRUCTION_EOR: return "INSTRUCTION_EOR";
        case INSTRUCTION_SUB: return "INSTRUCTION_SUB";
        case INSTRUCTION_RSB: return "INSTRUCTION_RSB";
        case INSTRUCTION_ADD: return "INSTRUCTION_ADD";
        case INSTRUCTION_ADC: return "INSTRUCTION_ADC";
        case INSTRUCTION_SBC: return "INSTRUCTION_SBC";
        case INSTRUCTION_RSC: return "INSTRUCTION_RSC";
        case INSTRUCTION_TST: return "INSTRUCTION_TST";
        case INSTRUCTION_TEQ: return "INSTRUCTION_TEQ";
        case INSTRUCTION_CMP: return "INSTRUCTION_CMP";
        case INSTRUCTION_CMN: return "INSTRUCTION_CMN";
        case INSTRUCTION_ORR: return "INSTRUCTION_ORR";
        case INSTRUCTION_MOV: return "INSTRUCTION_MOV";
        case INSTRUCTION_BIC: return "INSTRUCTION_BIC";
        case INSTRUCTION_MVN: return "INSTRUCTION_MVN";
        case INSTRUCTION_MRS: return "INSTRUCTION_MRS";
        case INSTRUCTION_MSR: return "INSTRUCTION_MSR";
        case INSTRUCTION_MUL: return "INSTRUCTION_MUL";
        case INSTRUCTION_MLA: return "INSTRUCTION_MLA";
        case INSTRUCTION_MULL: return "INSTRUCTION_MULL";
        case INSTRUCTION_MLAL: return "INSTRUCTION_MLAL";
        case INSTRUCTION_LDR: return "INSTRUCTION_LDR";
        case INSTRUCTION_STR: return "INSTRUCTION_STR";
        case INSTRUCTION_LDRH_IMM: return "INSTRUCTION_LDRH_IMM";
        case INSTRUCTION_STRH_IMM: return "INSTRUCTION_STRH_IMM";
        case INSTRUCTION_LDRSB_IMM: return "INSTRUCTION_LDRSB_IMM";
        case INSTRUCTION_LDRSH_IMM: return "INSTRUCTION_LDRSH_IMM";
        case INSTRUCTION_LDRH: return "INSTRUCTION_LDRH";
        case INSTRUCTION_STRH: return "INSTRUCTION_STRH";
        case INSTRUCTION_LDRSB: return "INSTRUCTION_LDRSB";
        case INSTRUCTION_LDRSH: return "INSTRUCTION_LDRSH";
        case INSTRUCTION_LDM: return "INSTRUCTION_LDM";
        case INSTRUCTION_STM: return "INSTRUCTION_STM";
        case INSTRUCTION_SWP: return "INSTRUCTION_SWP";
        case INSTRUCTION_SWI: return "INSTRUCTION_SWI";
        case INSTRUCTION_CDP: return "INSTRUCTION_CDP";
        case INSTRUCTION_STC: return "INSTRUCTION_STC";
        case INSTRUCTION_LDC: return "INSTRUCTION_LDC";
        case INSTRUCTION_MCR: return "INSTRUCTION_MCR";
        case INSTRUCTION_MRC: return "INSTRUCTION_MRC";
        case INSTRUCTION_MOVE_SHIFTED_REGISTER: return "INSTRUCTION_MOVE_SHIFTED_REGISTER";
        case INSTRUCTION_ADD_SUBTRACT: return "INSTRUCTION_ADD_SUBTRACT";
        case INSTRUCTION_MOVE_COMPARE_ADD_SUBTRACT_IMMEDIATE: return "INSTRUCTION_MOVE_COMPARE_ADD_SUBTRACT_IMMEDIATE";
        case INSTRUCTION_ALU_OPERATIONS: return "INSTRUCTION_ALU_OPERATIONS";
        case INSTRUCTION_HI_REGISTER_OPERATIONS_BRANCH_EXCHANGE: return "INSTRUCTION_HI_REGISTER_OPERATIONS_BRANCH_EXCHANGE";
        case INSTRUCTION_PC_RELATIVE_LOAD: return "INSTRUCTION_PC_RELATIVE_LOAD";
        case INSTRUCTION_LOAD_STORE_WITH_REGISTER_OFFSET: return "INSTRUCTION_LOAD_STORE_WITH_REGISTER_OFFSET";
        case INSTRUCTION_LOAD_STORE_SIGN_EXTENDED_BYTE_HALFWORD: return "INSTRUCTION_LOAD_STORE_SIGN_EXTENDED_BYTE_HALFWORD";
        case INSTRUCTION_LOAD_STORE_WITH_IMMEDIATE_OFFSET: return "INSTRUCTION_LOAD_STORE_WITH_IMMEDIATE_OFFSET";
        case INSTRUCTION_LOAD_STORE_HALFWORD: return "INSTRUCTION_LOAD_STORE_HALFWORD";
        case INSTRUCTION_SP_RELATIVE_LOAD_STORE: return "INSTRUCTION_SP_RELATIVE_LOAD_STORE";
        case INSTRUCTION_LOAD_ADDRESS: return "INSTRUCTION_LOAD_ADDRESS";
        case INSTRUCTION_ADD_OFFSET_TO_STACK_POINTER: return "INSTRUCTION_ADD_OFFSET_TO_STACK_POINTER";
        case INSTRUCTION_PUSH_POP_REGISTERS: return "INSTRUCTION_PUSH_POP_REGISTERS";
        case INSTRUCTION_MULTIPLE_LOAD_STORE: return "INSTRUCTION_MULTIPLE_LOAD_STORE";
        case INSTRUCTION_CONDITIONAL_BRANCH: return "INSTRUCTION_CONDITIONAL_BRANCH";
        case INSTRUCTION_SOFTWARE_INTERRUPT: return "INSTRUCTION_SOFTWARE_INTERRUPT";
        case INSTRUCTION_UNCONDITIONAL_BRANCH: return "INSTRUCTION_UNCONDITIONAL_BRANCH";
        case INSTRUCTION_LONG_BRANCH_WITH_LINK: return "INSTRUCTION_LONG_BRANCH_WITH_LINK";
    }

    return "UNKNOWN";
}

typedef enum DataProcessingTypes {
    DATA_PROCESSING_LOGICAL,
    DATA_PROCESSING_ARITHMETIC,
} DataProcessingTypes;

InstructionType data_processing_types[] = {
    [INSTRUCTION_AND] = DATA_PROCESSING_LOGICAL,
    [INSTRUCTION_EOR] = DATA_PROCESSING_LOGICAL,
    [INSTRUCTION_SUB] = DATA_PROCESSING_ARITHMETIC,
    [INSTRUCTION_RSB] = DATA_PROCESSING_ARITHMETIC,
    [INSTRUCTION_ADD] = DATA_PROCESSING_ARITHMETIC,
    [INSTRUCTION_ADC] = DATA_PROCESSING_ARITHMETIC,
    [INSTRUCTION_SBC] = DATA_PROCESSING_ARITHMETIC,
    [INSTRUCTION_RSC] = DATA_PROCESSING_ARITHMETIC,
    [INSTRUCTION_TST] = DATA_PROCESSING_LOGICAL,
    [INSTRUCTION_TEQ] = DATA_PROCESSING_LOGICAL,
    [INSTRUCTION_CMP] = DATA_PROCESSING_ARITHMETIC,
    [INSTRUCTION_CMN] = DATA_PROCESSING_ARITHMETIC,
    [INSTRUCTION_ORR] = DATA_PROCESSING_LOGICAL,
    [INSTRUCTION_MOV] = DATA_PROCESSING_LOGICAL,
    [INSTRUCTION_BIC] = DATA_PROCESSING_LOGICAL,
    [INSTRUCTION_MVN] = DATA_PROCESSING_LOGICAL,
};

typedef enum InstructionCategory {
    INSTRUCTION_CATEGORY_BRANCH,
    INSTRUCTION_CATEGORY_DATA_PROCESSING,
    INSTRUCTION_CATEGORY_PSR_TRANSFER,
    INSTRUCTION_CATEGORY_MULTIPLY,
    INSTRUCTION_CATEGORY_SINGLE_DATA_TRANSFER,
    INSTRUCTION_CATEGORY_HALFWORD_AND_SIGNED_DATA_TRANSFER,
    INSTRUCTION_CATEGORY_BLOCK_DATA_TRANSFER,
    INSTRUCTION_CATEGORY_SINGLE_DATA_SWAP,
    INSTRUCTION_CATEGORY_SOFTWARE_INTERRUPT,
    INSTRUCTION_CATEGORY_COPROCESSOR_DATA_OPERATIONS,
    INSTRUCTION_CATEGORY_COPROCESSOR_DATA_TRANSFERS,
    INSTRUCTION_CATEGORY_COPROCESSOR_REGISTER_TRANSFERS,

    INSTRUCTION_CATEGORY_DEBUG,
} InstructionCategory;

InstructionCategory instruction_categories[] = {
    [INSTRUCTION_B] = INSTRUCTION_CATEGORY_BRANCH,
    [INSTRUCTION_BX] = INSTRUCTION_CATEGORY_BRANCH,

    [INSTRUCTION_AND] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_EOR] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_SUB] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_RSB] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_ADD] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_ADC] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_SBC] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_RSC] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_TST] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_TEQ] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_CMP] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_CMN] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_ORR] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_MOV] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_BIC] = INSTRUCTION_CATEGORY_DATA_PROCESSING,
    [INSTRUCTION_MVN] = INSTRUCTION_CATEGORY_DATA_PROCESSING,

    [INSTRUCTION_MRS] = INSTRUCTION_CATEGORY_PSR_TRANSFER,
    [INSTRUCTION_MSR] = INSTRUCTION_CATEGORY_PSR_TRANSFER,

    [INSTRUCTION_MUL] = INSTRUCTION_CATEGORY_MULTIPLY,
    [INSTRUCTION_MLA] = INSTRUCTION_CATEGORY_MULTIPLY,
    [INSTRUCTION_MULL] = INSTRUCTION_CATEGORY_MULTIPLY,
    [INSTRUCTION_MLAL] = INSTRUCTION_CATEGORY_MULTIPLY,

    [INSTRUCTION_LDR] = INSTRUCTION_CATEGORY_SINGLE_DATA_TRANSFER,
    [INSTRUCTION_STR] = INSTRUCTION_CATEGORY_SINGLE_DATA_TRANSFER,

    [INSTRUCTION_LDRH_IMM] = INSTRUCTION_CATEGORY_HALFWORD_AND_SIGNED_DATA_TRANSFER,
    [INSTRUCTION_STRH_IMM] = INSTRUCTION_CATEGORY_HALFWORD_AND_SIGNED_DATA_TRANSFER,
    [INSTRUCTION_LDRSB_IMM] = INSTRUCTION_CATEGORY_HALFWORD_AND_SIGNED_DATA_TRANSFER,
    [INSTRUCTION_LDRSH_IMM] = INSTRUCTION_CATEGORY_HALFWORD_AND_SIGNED_DATA_TRANSFER,
    [INSTRUCTION_LDRH] = INSTRUCTION_CATEGORY_HALFWORD_AND_SIGNED_DATA_TRANSFER,
    [INSTRUCTION_STRH] = INSTRUCTION_CATEGORY_HALFWORD_AND_SIGNED_DATA_TRANSFER,
    [INSTRUCTION_LDRSB] = INSTRUCTION_CATEGORY_HALFWORD_AND_SIGNED_DATA_TRANSFER,
    [INSTRUCTION_LDRSH] = INSTRUCTION_CATEGORY_HALFWORD_AND_SIGNED_DATA_TRANSFER,

    [INSTRUCTION_LDM] = INSTRUCTION_CATEGORY_BLOCK_DATA_TRANSFER,
    [INSTRUCTION_STM] = INSTRUCTION_CATEGORY_BLOCK_DATA_TRANSFER,

    [INSTRUCTION_SWP] = INSTRUCTION_CATEGORY_SINGLE_DATA_SWAP,

    [INSTRUCTION_SWI] = INSTRUCTION_CATEGORY_SOFTWARE_INTERRUPT,

    [INSTRUCTION_CDP] = INSTRUCTION_CATEGORY_COPROCESSOR_DATA_OPERATIONS,

    [INSTRUCTION_STC] = INSTRUCTION_CATEGORY_COPROCESSOR_DATA_TRANSFERS,
    [INSTRUCTION_LDC] = INSTRUCTION_CATEGORY_COPROCESSOR_DATA_TRANSFERS,

    [INSTRUCTION_MCR] = INSTRUCTION_CATEGORY_COPROCESSOR_REGISTER_TRANSFERS,
    [INSTRUCTION_MRC] = INSTRUCTION_CATEGORY_COPROCESSOR_REGISTER_TRANSFERS,
};

typedef struct Instruction {
    InstructionType type;
    Condition condition;
    int offset;
    u8 L;
    u8 S;
    union {
        u8 rn;
        u8 rb;
    };
    u8 I;
    u8 P;
    u8 U;
    u8 W;
    u8 B;
    u8 A;
    u8 H;
    union {
        u16 second_operand;
        u16 register_list;
    };
    u8 rd;
    u8 rm;
    u8 rs;
    u8 rdhi;
    u8 rdlo;
    u16 source_operand;
    u8 value_8;
    u8 R;
    u8 H1;
    u8 H2;
    u8 op;
    u32 mask;

    u32 address;
#ifdef _DEBUG
    u32 encoding;
#endif
} Instruction;


void fetch();
void decode();
void execute();

void thumb_fetch();
void thumb_decode();
void thumb_execute();



static u32
rotate_right(u32 value, u32 shift, u8 bits)
{
    if (shift == 0) return value;

    u32 value_to_rotate = value & ((1 << shift) - 1);
    u32 rotate_masked = value_to_rotate << (bits - shift);

    return (value >> shift) | rotate_masked;
}

static u32
apply_shift(u32 value, u32 shift, ShiftType shift_type, u8 *carry)
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

            return rotate_right(value, shift, 32);
        } break;
    }

    return value;
}

#endif
