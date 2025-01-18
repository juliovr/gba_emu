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

char *psr_mode[] = {
    [0b10000] = "USER",
    [0b10001] = "FIQ",
    [0b10010] = "IRQ",
    [0b10011] = "SUPERVISOR",
    [0b10111] = "ABORT",
    [0b11011] = "UNDEFINED",
    [0b11111] = "SYSTEM",
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
print_cpu_state(CPU cpu)
{
    printf("----------------\n");
    printf("Registers:\n");
    for (int i = 0; i < 16; i++) {
        printf("    r[%d] = %d\n", i, cpu.r[i]);
    }
    printf("----------------\n");
    printf("PC = 0x%x\n", cpu.pc);

    char cpsr_buffer[33];
    num_to_binary_32(cpsr_buffer, cpu.cpsr);
    printf("%s\n", cpsr_buffer);

    printf("Condition flags: ");
    if ((cpu.cpsr >> 31) & 1) printf("N"); else printf("-");
    if ((cpu.cpsr >> 30) & 1) printf("Z"); else printf("-");
    if ((cpu.cpsr >> 29) & 1) printf("C"); else printf("-");
    if ((cpu.cpsr >> 28) & 1) printf("V"); else printf("-");
    
    printf("\n");
    printf("Control bits: ");
    if ((cpu.cpsr >> 7) & 1) printf("I"); else printf("-");
    if ((cpu.cpsr >> 6) & 1) printf("F"); else printf("-");
    if ((cpu.cpsr >> 5) & 1) printf("T"); else printf("-");

    printf("\n");
    printf("  Mode: %s: ", psr_mode[cpu.cpsr & 0b11111]);
    if ((cpu.cpsr >> 4) & 1) printf("1"); else printf("0");
    if ((cpu.cpsr >> 3) & 1) printf("1"); else printf("0");
    if ((cpu.cpsr >> 2) & 1) printf("1"); else printf("0");
    if ((cpu.cpsr >> 1) & 1) printf("1"); else printf("0");
    if ((cpu.cpsr >> 0) & 1) printf("1"); else printf("0");


    printf("\n");
    printf("----------------\n");
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
    // 0A000000-0BFFFFFF   Game Pak ROM/FlashROM (max 32MB) - Wait State 1
    // 0C000000-0DFFFFFF   Game Pak ROM/FlashROM (max 32MB) - Wait State 2
    u8 game_pak_ram[64*MEGABYTE];
    // 0E010000-0FFFFFFF   Not used

    // Unused Memory Area
    // 10000000-FFFFFFFF   Not used (upper 4bits of address bus unused)

} GBAMemory;


u8 *
get_memory_at(CPU cpu, GBAMemory *gba_memory, u32 at)
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
    if (at <= 0x0BFFFFFF) assert(!"game_pak Wait State 1 not handled");
    if (at <= 0x0DFFFFFF) assert(!"game_pak Wait State 2 not handled");
    if (at <= 0x0E00FFFF) return (gba_memory->game_pak_ram + (at - 0x0E000000));


    assert(!"Invalid memory");
    return 0;
}

u32
get_instruction_at(GBAMemory *gba_memory, u32 pc)
{
    return *(u32 *)(gba_memory->game_pak_rom + (pc - 0x08000000));
}


#define INSTRUCTION_FORMAT_DATA_PROCESSING                              (0)
#define INSTRUCTION_FORMAT_MULTIPLY                                     (0b0000000000000000000010010000)
#define INSTRUCTION_FORMAT_MULTIPLY_LONG                                (0b0000100000000000000010010000)
#define INSTRUCTION_FORMAT_SINGLE_DATA_SWAP                             (0b0001000000000000000010010000)
#define INSTRUCTION_FORMAT_BRANCH_AND_EXCHANGE                          (0b0001001011111111111100010000)
#define INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_REGISTER_OFFSET       (0b0000000000000000000010010000)
#define INSTRUCTION_FORMAT_HALFWORD_DATA_TRANSFER_IMMEDIATE_OFFSET      (0b0000010000000000000010010000)
#define INSTRUCTION_FORMAT_SINGLE_DATA_TRANSFER                         (1 << 26)
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

    INSTRUCTION_UNKNOWN,

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
    
    INSTRUCTION_DEBUG_EXIT,
} InstructionType;

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

    [INSTRUCTION_DEBUG_EXIT] = INSTRUCTION_CATEGORY_DEBUG,
};

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
} Instruction;


#endif