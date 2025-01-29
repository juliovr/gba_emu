#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "types.h"


#ifdef _DEBUG
    #ifdef _LINUX
        #define DEBUG_PRINT(format, ...) printf(format, ##__VA_ARGS__)
    #else
        #define DEBUG_PRINT(format, ...) printf(format, __VA_ARGS__)
    #endif
#else
#define DEBUG_PRINT(...)
#endif


bool is_running = true;

CPU gba_cpu = {0};
CPU *cpu = &gba_cpu;

//
// Control Bits
//
#define CONTROL_BITS_MODE       ((cpu->cpsr >> 0) & 0b11111)    /* Mode bits */
#define CONTROL_BITS_T          ((cpu->cpsr >> 5) & 1)          /* State bit (in Thumb mode) */
#define CONTROL_BITS_F          ((cpu->cpsr >> 6) & 1)          /* FIQ disable */
#define CONTROL_BITS_I          ((cpu->cpsr >> 7) & 1)          /* IRQ disable */

#define IN_THUMB_MODE           CONTROL_BITS_T

static char *
get_current_instruction_encoding()
{
    if (IN_THUMB_MODE) {
        return "THUMB";
    } else {
        return "ARM";
    }
}

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


GBAMemory memory = {0};


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

static void
init_gba()
{
    memset(&gba_cpu, 0, sizeof(CPU));
    memset(&memory, 0, sizeof(GBAMemory));

    cpu->r13 = 0x03007F00;
    cpu->cpsr = 0x1F;

    load_bios_into_memory();

    // cpu->pc = 0x08000000;
    cpu->pc = 0;
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
        case INSTRUCTION_DEBUG_EXIT: strcpy(buffer, "INSTRUCTION_DEBUG_EXIT"); break;
    }
}

u32 current_instruction;

Instruction decoded_instruction;

static bool
should_execute_instruction(Condition condition)
{
#ifdef _DEBUG
    if (decoded_instruction.type == INSTRUCTION_DEBUG_EXIT) return true;
#endif

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
            fprintf(stderr, "Unexpected condition: %X\n", condition);
            char type_name[64];
            get_instruction_type_name(decoded_instruction.type, type_name);
            fprintf(stderr, "Address: 0x%X, Current instruction: 0x%X -> type = %s\n", decoded_instruction.address, current_instruction, type_name);

            print_cpu_state(cpu);

            exit(1);
        }
    }
}

/**
 * actual_bits_value: how many bits are used in value.
 */
static u32
left_shift_sign_extended(u32 value, u8 actual_bits_value, u8 shift)
{
    u8 sign = (value >> (actual_bits_value - 1)) & 1;
    u32 value_sign_extended = (-sign << (actual_bits_value)) | value;

    return (value_sign_extended << shift);
}


void
thumb_execute()
{
    if (decoded_instruction.type == INSTRUCTION_NONE) goto exit_thumb_execute;
    
    switch (decoded_instruction.type) {
        case INSTRUCTION_MOVE_SHIFTED_REGISTER: {
            DEBUG_PRINT("INSTRUCTION_MOVE_SHIFTED_REGISTER, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            u8 rd = decoded_instruction.rd;
            int shift = decoded_instruction.offset;
            u16 value = (u16)(*get_register(cpu, decoded_instruction.rs));
            u8 carry = 0;
            u16 result = 0;
            
            switch (decoded_instruction.op) {
                case THUMB_SHIFT_TYPE_LOGICAL_LEFT: {
                    carry = (value >> (16 - shift)) & 1;

                    result = value << shift;
                } break;

                case THUMB_SHIFT_TYPE_LOGICAL_RIGHT: {
                    carry = (value >> (shift - 1)) & 1;

                    result = value >> shift;
                } break;

                case THUMB_SHIFT_TYPE_ARITHMETIC_RIGHT: {
                    carry = (value >> (shift - 1)) & 1;

                    u8 msb = (value >> 15) & 1;
                    u16 msb_replicated = (-msb << (16 - shift));

                    result = (value >> shift) | msb_replicated;
                } break;
            }

            u32 *rd_register = get_register(cpu, rd);
            *rd_register = result;

            u8 overflow = (result < value) ? 1 : 0;

            set_condition_V(overflow);
            set_condition_C(carry);
            set_condition_Z(result == 0);
            set_condition_N(result >> 15);
        } break;
        case INSTRUCTION_ADD_SUBTRACT: {
            DEBUG_PRINT("INSTRUCTION_ADD_SUBTRACT, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            u16 first_value = (u16)(*get_register(cpu, decoded_instruction.rs));
            u16 second_value = (u16)((decoded_instruction.I) ? decoded_instruction.rn : *get_register(cpu, decoded_instruction.rn));
            u16 result = 0;
            if (decoded_instruction.op) {
                result = (u16)(first_value - second_value);
            } else {
                result = (u16)(first_value + second_value);
            }

            u32 *rd_register = get_register(cpu, decoded_instruction.rd);
            *rd_register = result;

            u8 overflow = (result < first_value) ? 1 : 0;

            set_condition_V(overflow);
            // set_condition_C(carry); // TODO: do this
            set_condition_Z(result == 0);
            set_condition_N(result >> 15);
        } break;
        case INSTRUCTION_MOVE_COMPARE_ADD_SUBTRACT_IMMEDIATE: {
            DEBUG_PRINT("INSTRUCTION_MOVE_COMPARE_ADD_SUBTRACT_IMMEDIATE, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            u32 result = 0;
            u32 old_value = *get_register(cpu, decoded_instruction.rd);

            switch (decoded_instruction.op) {
                case 0: { // MOV
                    result = decoded_instruction.offset;
                    *get_register(cpu, decoded_instruction.rd) = result;
                } break;
                case 1: { // CMP
                    result = *get_register(cpu, decoded_instruction.rd) - decoded_instruction.offset;

                    set_condition_C((u32)decoded_instruction.offset <= *get_register(cpu, decoded_instruction.rd) ? 1 : 0);
                    set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
                } break;
                case 2: { // ADD
                    result = *get_register(cpu, decoded_instruction.rd) + decoded_instruction.offset;
                    *get_register(cpu, decoded_instruction.rd) = result;

                    set_condition_C((result < (u32)decoded_instruction.offset) ? 1 : 0);
                    set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
                } break;
                case 3: { // SUB
                    result = *get_register(cpu, decoded_instruction.rd) - decoded_instruction.offset;
                    *get_register(cpu, decoded_instruction.rd) = result;

                    set_condition_C((u32)decoded_instruction.offset <= *get_register(cpu, decoded_instruction.rd) ? 1 : 0);
                    set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
                } break;
            }

            set_condition_Z(result == 0);
            set_condition_N(result >> 31);
        } break;
        case INSTRUCTION_ALU_OPERATIONS: {
            DEBUG_PRINT("INSTRUCTION_ALU_OPERATIONS, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            u8 rd = decoded_instruction.rd;
            u8 rs = decoded_instruction.rs;

            u32 result = 0;
            bool store_result = false;

            switch (decoded_instruction.op) {
                case 0: { // AND
                    result = *get_register(cpu, rd) & *get_register(cpu, rs);
                    store_result = true;
                } break;
                case 1: { // EOR
                    result = *get_register(cpu, rd) ^ *get_register(cpu, rs);
                    store_result = true;
                } break;
                case 2: { // LSL
                    result = *get_register(cpu, rd) << *get_register(cpu, rs);
                    store_result = true;

                    if (*get_register(cpu, rs)) {
                        set_condition_C((result >> 31) & 1);
                    }
                } break;
                case 3: { // LSR
                    if (*get_register(cpu, rs)) {
                        set_condition_C((*get_register(cpu, rd) >> (*get_register(cpu, rs) - 1)) & 1);
                    }

                    result = *get_register(cpu, rd) >> *get_register(cpu, rs);
                    store_result = true;
                } break;
                case 4: { // ASR
                    if (*get_register(cpu, rs)) {
                        set_condition_C((*get_register(cpu, rd) >> (*get_register(cpu, rs) - 1)) & 1);
                    }

                    u8 shift = *get_register(cpu, rs) & 0xFF;
                    u8 msb = (*get_register(cpu, rd) >> 31) & 1;
                    u32 msb_replicated = (-msb << (32 - shift));

                    result = (*get_register(cpu, rd) >> shift) | msb_replicated;
                    store_result = true;
                } break;
                case 5: { // ADC
                    result = (*get_register(cpu, rd) + *get_register(cpu, rs) + CONDITION_C);
                    store_result = true;

                    set_condition_C(result < *get_register(cpu, rd)); // TODO: should the carry be set in the same way as overflow?
                    set_condition_V(result < *get_register(cpu, rd));
                } break;
                case 6: { // SBC
                    result = (*get_register(cpu, rd) - *get_register(cpu, rs) - !(CONDITION_C));
                    store_result = true;

                    set_condition_C(result > *get_register(cpu, rd)); // TODO: should the carry be set in the same way as overflow?
                    set_condition_V(result > *get_register(cpu, rd));
                } break;
                case 7: { // ROR
                    if ((*get_register(cpu, rs) & 0xF) == 0) {
                        set_condition_C((*get_register(cpu, rd) >> 31) & 1);
                    } else {
                        set_condition_C((*get_register(cpu, rd) >> (((*get_register(cpu, rs) & 0xF) - 1)) & 1));
                    }

                    u8 shift = *get_register(cpu, rs) & 0xFF;
                    u32 value_to_rotate = *get_register(cpu, rd) & ((1 << shift) - 1);
                    u32 rotate_masked = value_to_rotate << (32 - shift);

                    result = (*get_register(cpu, rd) >> shift) | rotate_masked;
                    store_result = true;
                } break;
                case 8: { // TST
                    result = *get_register(cpu, rd) & *get_register(cpu, rs);
                    store_result = false;
                } break;
                case 9: { // NEG
                    result = (u32)(-(s32)*get_register(cpu, rs));
                    store_result = true;

                    set_condition_C(result > *get_register(cpu, rd)); // TODO: should the carry be set in the same way as overflow?
                    set_condition_V(result > *get_register(cpu, rd));
                } break;
                case 10: { // CMP
                    result = *get_register(cpu, rd) - *get_register(cpu, rs);
                    store_result = false;

                    set_condition_C(result > *get_register(cpu, rd)); // TODO: should the carry be set in the same way as overflow?
                    set_condition_V(result > *get_register(cpu, rd));
                } break;
                case 11: { // CMN
                    result = *get_register(cpu, rd) + *get_register(cpu, rs);
                    store_result = false;

                    set_condition_C(result < *get_register(cpu, rd)); // TODO: should the carry be set in the same way as overflow?
                    set_condition_V(result < *get_register(cpu, rd));
                } break;
                case 12: { // ORR
                    result = *get_register(cpu, rd) | *get_register(cpu, rs);
                    store_result = true;
                } break;
                case 13: { // MUL
                    result = *get_register(cpu, rd) * *get_register(cpu, rs);
                    store_result = true;
                } break;
                case 14: { // BIC
                    result = *get_register(cpu, rd) & !*get_register(cpu, rs);
                    store_result = true;
                } break;
                case 15: { // MVN
                    result = !*get_register(cpu, rs);
                    store_result = true;
                } break;
            }

            if (store_result) {
                *get_register(cpu, rd) = result;
            }
            
            set_condition_N((result >> 31) & 1);
            set_condition_Z(result == 0);

        } break;
        case INSTRUCTION_HI_REGISTER_OPERATIONS_BRANCH_EXCHANGE: {
            DEBUG_PRINT("INSTRUCTION_HI_REGISTER_OPERATIONS_BRANCH_EXCHANGE, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            // H1 and H2 are flags to use the register as a Hi register (in the range of 8-15).
            // H1 for rd; H2 for rs.
            u8 H1 = decoded_instruction.H1;
            u8 H2 = decoded_instruction.H2;
            u8 op = decoded_instruction.op;
            
            assert(!(H1 == 0 &&
                     H2 == 0 &&
                    (op == 0 || op == 1 || op == 2)));
            

            u8 rs = decoded_instruction.rs + (H2 * 8);
            u8 rd = decoded_instruction.rd + (H1 * 8);

            switch (op) {
                case 0: { // ADD
                    *get_register(cpu, rd) += *get_register(cpu, rs);
                } break;
                case 1: { // CMP
                    u32 old_value = *get_register(cpu, rd);
                    u32 result = *get_register(cpu, rd) - *get_register(cpu, rs);
                    
                    u8 overflow = (result < old_value) ? 1 : 0;
                    
                    set_condition_V(overflow);
                    // set_condition_C(carry); // TODO: do this
                    set_condition_Z(result == 0);
                    set_condition_N(result >> 31); // TODO: check the shift
                } break;
                case 2: { // MOV
                    *get_register(cpu, rd) = *get_register(cpu, rs);
                } break;
                case 3: { // BX
                    cpu->pc = *get_register(cpu, rs) & (-2);

                    u8 thumb_mode = *get_register(cpu, rs) & 1;
                    set_control_bit_T(thumb_mode);

                    current_instruction = 0;
                } break;
            }
        } break;
        case INSTRUCTION_PC_RELATIVE_LOAD: {
            DEBUG_PRINT("INSTRUCTION_PC_RELATIVE_LOAD, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            u32 base = (((cpu->pc - 2) & -2) + (decoded_instruction.offset << 2));   // TODO: in mGBA this computes the value, but the data sheet says the PC is 4 bytes ahead of this instruction.
                                                                                    // Let's see why this works.
            // u32 base = ((cpu->pc & -2) + (decoded_instruction.offset << 2));
            u32 *address = (u32 *)get_memory_at(cpu, &memory, base);

            *get_register(cpu, decoded_instruction.rd) = *address;
        } break;
        case INSTRUCTION_LOAD_STORE_WITH_REGISTER_OFFSET: {
            DEBUG_PRINT("INSTRUCTION_LOAD_STORE_WITH_REGISTER_OFFSET, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            u32 base = *get_register(cpu, decoded_instruction.rb) + *get_register(cpu, decoded_instruction.rm);
            if (base > *get_register(cpu, decoded_instruction.rb)) {
                // If the result overflow does not execute the instruction.

                if (decoded_instruction.L) {
                    if (decoded_instruction.B) {
                        u8 *address = get_memory_at(cpu, &memory, base);
                        *get_register(cpu, decoded_instruction.rd) = (u32)*address;
                    } else {
                        u32 *address = (u32 *)get_memory_at(cpu, &memory, base);
                        *get_register(cpu, decoded_instruction.rd) = *address;
                    }
                } else {
                    if (decoded_instruction.B) {
                        u8 *address = get_memory_at(cpu, &memory, base);
                        *address = (u8)*get_register(cpu, decoded_instruction.rd);
                    } else {
                        u32 *address = (u32 *)get_memory_at(cpu, &memory, base);
                        *address = *get_register(cpu, decoded_instruction.rd);
                    }
                }
            }
        } break;
        case INSTRUCTION_LOAD_STORE_SIGN_EXTENDED_BYTE_HALFWORD: {
            DEBUG_PRINT("INSTRUCTION_LOAD_STORE_SIGN_EXTENDED_BYTE_HALFWORD, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            assert(!"Implement");
        } break;
        case INSTRUCTION_LOAD_STORE_WITH_IMMEDIATE_OFFSET: {
            DEBUG_PRINT("INSTRUCTION_LOAD_STORE_WITH_IMMEDIATE_OFFSET, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            u32 base = *get_register(cpu, decoded_instruction.rb) + (decoded_instruction.offset << 2);
            if (decoded_instruction.B) {
                u8 *address = get_memory_at(cpu, &memory, base);
                if (decoded_instruction.L) {
                    *get_register(cpu, decoded_instruction.rd) = (u32)*address;
                } else {
                    *address = (u8)*get_register(cpu, decoded_instruction.rd);
                }
            } else {
                u32 *address = (u32 *)get_memory_at(cpu, &memory, base);
                if (decoded_instruction.L) {
                    *get_register(cpu, decoded_instruction.rd) = *address;
                } else {
                    *address = *get_register(cpu, decoded_instruction.rd);
                }
            }

        } break;
        case INSTRUCTION_LOAD_STORE_HALFWORD: {
            DEBUG_PRINT("INSTRUCTION_LOAD_STORE_HALFWORD, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            u32 base = *get_register(cpu, decoded_instruction.rb) + (decoded_instruction.offset << 1);
            u16 *address = (u16 *)get_memory_at(cpu, &memory, base);
            if (decoded_instruction.L) {
                *get_register(cpu, decoded_instruction.rd) = (u32)*address; // Cast to u32 to fill high bits with 0.
            } else {
                *address = (u16)*get_register(cpu, decoded_instruction.rd);
            }
        } break;
        case INSTRUCTION_SP_RELATIVE_LOAD_STORE: {
            DEBUG_PRINT("INSTRUCTION_SP_RELATIVE_LOAD_STORE, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            u32 base = cpu->sp + (decoded_instruction.offset << 2);
            u32 *address = (u32 *)get_memory_at(cpu, &memory, base);
            if (decoded_instruction.L) {
                *get_register(cpu, decoded_instruction.rd) = *address;
            } else {
                *address = *get_register(cpu, decoded_instruction.rd);
            }
        } break;
        case INSTRUCTION_LOAD_ADDRESS: {
            DEBUG_PRINT("INSTRUCTION_LOAD_ADDRESS, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            assert(!"Implement");
        } break;
        case INSTRUCTION_ADD_OFFSET_TO_STACK_POINTER: {
            DEBUG_PRINT("INSTRUCTION_ADD_OFFSET_TO_STACK_POINTER, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            s8 sign = decoded_instruction.S ? -1 : 1;
            int offset = sign * (decoded_instruction.offset << 2);

            cpu->sp += offset;
        } break;
        case INSTRUCTION_PUSH_POP_REGISTERS: {
            DEBUG_PRINT("INSTRUCTION_PUSH_POP_REGISTERS, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            u8 register_list = (u8)decoded_instruction.register_list;
            u32 sp = cpu->sp;

            if (decoded_instruction.R) {
                if (decoded_instruction.L) {
                    // Load
                    u32 *address = (u32 *)get_memory_at(cpu, &memory, sp);
                    cpu->pc = *address;

                    sp += 4;
                } else {
                    // Store
                    sp -= 4;

                    u32 *address = (u32 *)get_memory_at(cpu, &memory, sp);
                    *address = cpu->lr;
                }
            }

            int register_index = 7;
            while (register_list) {
                bool register_index_set = (register_list >> 7) & 1;
                if (register_index_set) {
                    if (decoded_instruction.L) {
                        // Load
                        u32 *address = (u32 *)get_memory_at(cpu, &memory, sp);
                        *get_register(cpu, (u8)register_index) = *address;

                        sp += 4;
                    } else {
                        // Store
                        sp -= 4;

                        u32 *address = (u32 *)get_memory_at(cpu, &memory, sp);
                        *address = *get_register(cpu, (u8)register_index);
                    }
                }

                register_index--;
                register_list <<= 1;
            }

            cpu->sp = sp;
        } break;
        case INSTRUCTION_MULTIPLE_LOAD_STORE: {
            DEBUG_PRINT("INSTRUCTION_MULTIPLE_LOAD_STORE, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            assert(!"Implement");
        } break;
        case INSTRUCTION_CONDITIONAL_BRANCH: {
            DEBUG_PRINT("INSTRUCTION_CONDITIONAL_BRANCH, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            Condition condition = (Condition)decoded_instruction.condition;
            bool should_execute = should_execute_instruction(condition);

            if (should_execute) {
                u32 offset = left_shift_sign_extended(decoded_instruction.offset, 8, 1);
                cpu->pc += offset;

                current_instruction = 0;
            }
        } break;
        case INSTRUCTION_SOFTWARE_INTERRUPT: {
            DEBUG_PRINT("INSTRUCTION_SOFTWARE_INTERRUPT, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            cpu->r14_svc = decoded_instruction.address + 2; // Next instruction
            cpu->spsr_svc = cpu->cpsr;

            set_mode(MODE_SUPERVISOR);
            set_control_bit_T(0); // Execute in ARM state
            set_control_bit_I(1); // Disable normal interrupts

            cpu->pc = 0x8;

            current_instruction = 0;
        } break;
        case INSTRUCTION_UNCONDITIONAL_BRANCH: {
            DEBUG_PRINT("INSTRUCTION_UNCONDITIONAL_BRANCH, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            u32 offset = left_shift_sign_extended(decoded_instruction.offset, 11, 1);
            cpu->pc += offset;

            current_instruction = 0;
        } break;
        case INSTRUCTION_LONG_BRANCH_WITH_LINK: {
            DEBUG_PRINT("INSTRUCTION_LONG_BRANCH_WITH_LINK, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            if (decoded_instruction.H == 0) {
                // First part of the instruction
                u32 offset = left_shift_sign_extended(decoded_instruction.offset, 11, 12);
                cpu->lr = cpu->pc + offset;

            } else {
                // Second part of the instruction
                cpu->pc = cpu->lr + ((u32)decoded_instruction.offset << 1);
                cpu->lr = (decoded_instruction.address + 2) | 1; // NOTE: the address of the instruction following the BL is placed in LR and bit 0 of LR is set.
                
                current_instruction = 0;
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
        fprintf(stderr, "Thumb instruction unknown: 0x%X\n", current_instruction);
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
            DEBUG_PRINT("INSTRUCTION_B, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            if (decoded_instruction.L) {
                cpu->lr = cpu->pc - 4;
            }

            u32 offset = left_shift_sign_extended(decoded_instruction.offset, 24, 2);
            cpu->pc += offset;

            current_instruction = 0;
        } break;

        case INSTRUCTION_BX: {
            DEBUG_PRINT("INSTRUCTION_BX, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            cpu->pc = *get_register(cpu, decoded_instruction.rn) & (-2); // NOTE: PC must be 16-bit align. This clears out the lsb (-2 is 0b1110).

            u8 thumb_mode = *get_register(cpu, decoded_instruction.rn) & 1;
            set_control_bit_T(thumb_mode);

            current_instruction = 0;
        } break;

        default: {
            // print_cpu_state(cpu);
            assert(!"Invalid instruction type for category");
        }
    }
}


static u32
rotate_right(u32 value, u32 shift)
{
    u32 value_to_rotate = value & ((1 << shift) - 1);
    u32 rotate_masked = value_to_rotate << (32 - shift);

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

            return rotate_right(value, shift);
        } break;
    }

    return value;
}

static void
process_data_processing()
{
    u8 carry = 0; // TODO: if the set_condition_C below works well, get rid of this variable.
    u32 second_operand;
    ShiftType shift_type;
    u32 shift_value = 0;
    if (decoded_instruction.I) {
        u8 imm = decoded_instruction.second_operand & 0xFF;
        u32 rotate = (decoded_instruction.second_operand >> 8) & 0xF;
        // NOTE: This value is zero extended to 32 bits, and then subject to a rotate right by twice the value in the rotate field.
        rotate *= 2;

        shift_type = SHIFT_TYPE_ROTATE_RIGHT;
        shift_value = rotate;

        second_operand = apply_shift(imm, rotate, shift_type, &carry);
    } else {
        u8 rm = decoded_instruction.second_operand & 0xF;
        u8 shift = (decoded_instruction.second_operand >> 4) & 0xFF;
        shift_type = (ShiftType)((shift >> 1) & 0b11);
        if (shift & 1) {
            // Shift register
            u8 rs = (shift >> 4) & 0xF; // Register to the value to shift.
            shift_value = (u8)(*get_register(cpu, rs) & 0xF);
            second_operand = apply_shift(*get_register(cpu, rm), shift_value, shift_type, &carry);
        } else {
            // Shift amount
            shift_value = (shift >> 3) & 0b11111;
            second_operand = apply_shift(*get_register(cpu, rm), shift_value, shift_type, &carry);
        }
    }


    bool store_result = false;
    u32 result = 0;

    u32 rn = *get_register(cpu, decoded_instruction.rn);
    
    switch (decoded_instruction.type) {
        case INSTRUCTION_ADD: {
            DEBUG_PRINT("INSTRUCTION_ADD, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            result = rn + second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
                set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
            }
        } break;
        case INSTRUCTION_AND: {
            DEBUG_PRINT("INSTRUCTION_AND, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            result = rn & second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
            }
        } break;
        case INSTRUCTION_EOR: {
            DEBUG_PRINT("INSTRUCTION_EOR, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            result = rn ^ second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
            }
        } break;
        case INSTRUCTION_SUB: {
            DEBUG_PRINT("INSTRUCTION_SUB, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            result = rn - second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(second_operand <= rn ? 1 : 0);
                set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
            }
        } break;
        case INSTRUCTION_RSB: {
            DEBUG_PRINT("INSTRUCTION_RSB, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            result = second_operand - rn;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(second_operand <= rn ? 1 : 0);
                set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
            }
        } break;
        case INSTRUCTION_ADC: {
            DEBUG_PRINT("INSTRUCTION_ADC, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            result = rn & second_operand + carry;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
                set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
            }
        } break;
        case INSTRUCTION_SBC: {
            DEBUG_PRINT("INSTRUCTION_SBC, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            result = rn - second_operand + carry - 1;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(second_operand <= rn ? 1 : 0);
                set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
            }
        } break;
        case INSTRUCTION_RSC: {
            DEBUG_PRINT("INSTRUCTION_RSC, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            result = second_operand - rn + carry - 1;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(second_operand <= rn ? 1 : 0);
                set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
            }
        } break;
        case INSTRUCTION_TST: {
            DEBUG_PRINT("INSTRUCTION_TST, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            result = rn & second_operand;
            store_result = false;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
            }
        } break;
        case INSTRUCTION_TEQ: {
            DEBUG_PRINT("INSTRUCTION_TEQ, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            result = rn ^ second_operand;
            store_result = false;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
            }
        } break;
        case INSTRUCTION_CMP: {
            DEBUG_PRINT("INSTRUCTION_CMP, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            result = rn - second_operand;
            store_result = false;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C(second_operand <= rn ? 1 : 0);
                set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
            }
        } break;
        case INSTRUCTION_CMN: {
            DEBUG_PRINT("INSTRUCTION_CMN, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            result = rn + second_operand;
            store_result = false;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
                set_condition_V((*get_register(cpu, decoded_instruction.rd) & 0x80000000) != (result & 0x80000000));
            }
        } break;
        case INSTRUCTION_ORR: {
            DEBUG_PRINT("INSTRUCTION_ORR, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            result = rn | second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
            }
        } break;
        case INSTRUCTION_MOV: {
            DEBUG_PRINT("INSTRUCTION_MOV, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            result = second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
            }
        } break;
        case INSTRUCTION_BIC: {
            DEBUG_PRINT("INSTRUCTION_BIC, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            result = rn & !second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
            }
        } break;
        case INSTRUCTION_MVN: {
            DEBUG_PRINT("INSTRUCTION_MVN, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            result = !second_operand;
            store_result = true;

            if (decoded_instruction.S == 1 && decoded_instruction.rd == 15) {
                cpu->cpsr = *get_spsr_current_mode(cpu);
            } else if (decoded_instruction.S == 1) {
                set_condition_Z(result == 0);
                set_condition_N(result >> 31);
                set_condition_C((result < second_operand) ? 1 : 0);
            }
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }

    if (store_result) {
        *get_register(cpu, decoded_instruction.rd) = result;
    }
}

static void
process_psr_transfer()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_MRS: {
            DEBUG_PRINT("INSTRUCTION_MRS, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            u32 sr = cpu->cpsr;
            if (decoded_instruction.P) {
                sr = *(get_spsr_current_mode(cpu));
            }

            *get_register(cpu, decoded_instruction.rd) = sr;
        } break;
        case INSTRUCTION_MSR: {
            DEBUG_PRINT("INSTRUCTION_MSR, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            u32 *sr = &cpu->cpsr;
            if (decoded_instruction.P) {
                sr = get_spsr_current_mode(cpu);
            }

            if (decoded_instruction.I) {
                u8 carry = 0;
                
                u8 imm = decoded_instruction.source_operand & 0xFF;
                u32 rotate = (decoded_instruction.source_operand >> 8) & 0xF;
                // NOTE: This value is zero extended to 32 bits, and then subject to a rotate right by twice the value in the rotate field.
                rotate *= 2;

                u32 value = apply_shift(imm, rotate, SHIFT_TYPE_ROTATE_RIGHT, &carry);
                *sr = value;
            } else {
                *sr = *get_register(cpu, decoded_instruction.rm);
            }
        } break;

        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}

static void
process_multiply()
{
    switch (decoded_instruction.type) {
        case INSTRUCTION_MUL: {
            DEBUG_PRINT("INSTRUCTION_MUL, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
        } break;
        case INSTRUCTION_MLA: {
            DEBUG_PRINT("INSTRUCTION_MLA, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
        } break;
        case INSTRUCTION_MULL: {
            DEBUG_PRINT("INSTRUCTION_MULL, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
        } break;
        case INSTRUCTION_MLAL: {
            DEBUG_PRINT("INSTRUCTION_MLAL, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

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
    u16 offset;

    if (decoded_instruction.I) {
        u8 carry;
        u32 offset_register = *get_register(cpu, decoded_instruction.offset & 0xF);
        u8 shift = (decoded_instruction.offset >> 4) & 0xFF;
        u8 shift_type = (ShiftType)((shift >> 1) & 0b11);
        if (shift & 1) {
            // Shift register
            u8 rs = (shift >> 4) & 0xF ; // Register to the value to shift.
            offset = (u16)apply_shift(offset_register, (u8)(*get_register(cpu, rs) & 0xF), shift_type, &carry);
        } else {
            // Shift amount
            u8 shift_amount = (shift >> 3) & 0b11111;
            offset = (u16)apply_shift(offset_register, shift_amount, shift_type, &carry);
        }

    } else {
        offset = (u16)decoded_instruction.offset;
    }
    
    switch (decoded_instruction.type) {
        case INSTRUCTION_LDR: {
            DEBUG_PRINT("INSTRUCTION_LDR, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());
            
            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();
                u8 *address = get_memory_at(cpu, &memory, base);
                u8 rotate_value = 8 * (base & 0b11);
                
                // Store data
                if (decoded_instruction.B) {
                    *get_register(cpu, decoded_instruction.rd) = rotate_right((u32)(*address), rotate_value);
                } else {
                    *get_register(cpu, decoded_instruction.rd) = rotate_right(*((u32 *)address), rotate_value);
                }

                if (decoded_instruction.W) {
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
            } else {
                u8 *address = get_memory_at(cpu, &memory, base);
                u8 rotate_value = 8 * (base & 0b11);

                // Store data
                if (decoded_instruction.B) {
                    *get_register(cpu, decoded_instruction.rd) = rotate_right((u32)(*address), rotate_value);
                } else {
                    *get_register(cpu, decoded_instruction.rd) = rotate_right(*((u32 *)address), rotate_value);
                }

                UPDATE_BASE_OFFSET();
                *get_register(cpu, decoded_instruction.rn) = base;
            }

            if (decoded_instruction.rd == 15) {
                *get_register(cpu, decoded_instruction.rd) &= 0xFFFFFFFC; // NOTE: From "ARM Architecture Reference Manual"

                // PC written, so it has to branch to that instruction and invalidate whatever the pre-fetched was.
                current_instruction = 0;
            }
            
        } break;
        case INSTRUCTION_STR: {
            DEBUG_PRINT("INSTRUCTION_STR, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();
                u8 *address = get_memory_at(cpu, &memory, base);

                // Store data
                if (decoded_instruction.B) {
                    *((u32 *)address) = (u8)*get_register(cpu, decoded_instruction.rd);
                } else {
                    *((u32 *)address) = *get_register(cpu, decoded_instruction.rd);
                }

                if (decoded_instruction.W) {
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
            } else {
                u8 *address = get_memory_at(cpu, &memory, base);

                // Store data
                if (decoded_instruction.B) {
                    *((u32 *)address) = (u8)*get_register(cpu, decoded_instruction.rd);
                } else {
                    *((u32 *)address) = *get_register(cpu, decoded_instruction.rd);
                }

                UPDATE_BASE_OFFSET();
                *get_register(cpu, decoded_instruction.rn) = base;
            }

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
        // Halfword and signed data transfer
        case INSTRUCTION_LDRH_IMM: {
            DEBUG_PRINT("INSTRUCTION_LDRH_IMM, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
        } break;
        case INSTRUCTION_STRH_IMM: {
            DEBUG_PRINT("INSTRUCTION_STRH_IMM, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
        } break;
        case INSTRUCTION_LDRSB_IMM: {
            DEBUG_PRINT("INSTRUCTION_LDRSB_IMM, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
        } break;
        case INSTRUCTION_LDRSH_IMM: {
            DEBUG_PRINT("INSTRUCTION_LDRSH_IMM, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
        } break;

        case INSTRUCTION_LDRH: {
            DEBUG_PRINT("INSTRUCTION_LDRH, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            int base = *get_register(cpu, decoded_instruction.rn);
            int offset = *get_register(cpu, decoded_instruction.rm);

            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();

                u8 *address = get_memory_at(cpu, &memory, base);
                *get_register(cpu, decoded_instruction.rd) = *((u16 *)address);

                if (decoded_instruction.W) {
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
            } else {
                u8 *address = get_memory_at(cpu, &memory, base);
                *get_register(cpu, decoded_instruction.rd) = *((u16 *)address);

                UPDATE_BASE_OFFSET();
                *get_register(cpu, decoded_instruction.rn) = base;
            }

        } break;
        case INSTRUCTION_STRH: {
            DEBUG_PRINT("INSTRUCTION_STRH, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            int base = *get_register(cpu, decoded_instruction.rn);
            int offset = *get_register(cpu, decoded_instruction.rm);

            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();

                u8 *address = get_memory_at(cpu, &memory, base);
                *((u16 *)address) = (u16)*get_register(cpu, decoded_instruction.rd);

                if (decoded_instruction.W) {
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
            } else {
                u8 *address = get_memory_at(cpu, &memory, base);
                *((u16 *)address) = (u16)*get_register(cpu, decoded_instruction.rd);

                UPDATE_BASE_OFFSET();
                *get_register(cpu, decoded_instruction.rn) = base;
            }

        } break;
        case INSTRUCTION_LDRSB: {
            DEBUG_PRINT("INSTRUCTION_LDRSB, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            int base = *get_register(cpu, decoded_instruction.rn);
            int offset = *get_register(cpu, decoded_instruction.rm);

            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();

                u8 *memory_region = get_memory_at(cpu, &memory, base);
                u8 value = *memory_region;
                u8 sign = (value >> 7) & 1;
                u32 value_sign_extended = (((u32)-sign) << 8) | value;

                *get_register(cpu, decoded_instruction.rd) = value_sign_extended;

                if (decoded_instruction.W) {
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
            } else {
                u8 *memory_region = get_memory_at(cpu, &memory, base);
                u8 value = *memory_region;
                u8 sign = (value >> 7) & 1;
                u32 value_sign_extended = (((u32)-sign) << 8) | value;

                *get_register(cpu, decoded_instruction.rd) = value_sign_extended;

                UPDATE_BASE_OFFSET();
                *get_register(cpu, decoded_instruction.rn) = base;
            }

        } break;
        case INSTRUCTION_LDRSH: {
            DEBUG_PRINT("INSTRUCTION_LDRSH, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            int base = *get_register(cpu, decoded_instruction.rn);
            int offset = *get_register(cpu, decoded_instruction.rm);

            if (decoded_instruction.P) {
                UPDATE_BASE_OFFSET();

                u8 *memory_region = get_memory_at(cpu, &memory, base);
                u16 value = *((u16 *)memory_region);
                u8 sign = (value >> 15) & 1;
                u32 value_sign_extended = (((u32)-sign) << 16) | value;

                *get_register(cpu, decoded_instruction.rd) = value_sign_extended;

                if (decoded_instruction.W) {
                    *get_register(cpu, decoded_instruction.rn) = base;
                }
            } else {
                u8 *memory_region = get_memory_at(cpu, &memory, base);
                u16 value = *((u16 *)memory_region);
                u8 sign = (value >> 15) & 1;
                u32 value_sign_extended = (((u32)-sign) << 16) | value;

                *get_register(cpu, decoded_instruction.rd) = value_sign_extended;

                UPDATE_BASE_OFFSET();
                *get_register(cpu, decoded_instruction.rn) = base;
            }

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
    switch (decoded_instruction.type) {
        case INSTRUCTION_LDM: {
            s32 base_address = *get_register(cpu, decoded_instruction.rn);
            u16 register_list = decoded_instruction.register_list;
            assert(register_list != 0);

            int register_index = (decoded_instruction.U) ? 0 : 15;
            while (register_list) {
                if (decoded_instruction.U) {
                    // Increment
                    bool register_index_set = register_list & 1;
                    if (register_index_set) {
                        if (decoded_instruction.P) {
                            base_address++;

                            u32 *address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            *get_register(cpu, (u8)register_index) = *address;
                        } else {
                            u32 *address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            *get_register(cpu, (u8)register_index) = *address;

                            base_address++;
                        }
                    }

                    register_index++;
                    register_list >>= 1;
                } else {
                    // Decrement
                    bool register_index_set = (register_list >> 15) & 1;
                    if (register_index_set) {
                        if (decoded_instruction.P) {
                            base_address--;

                            u32 *address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            *get_register(cpu, (u8)register_index) = *address;
                        } else {
                            u32 *address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            *get_register(cpu, (u8)register_index) = *address;

                            base_address--;
                        }
                    }

                    register_index--;
                    register_list <<= 1;
                }

            }

            if (decoded_instruction.W) {
                *get_register(cpu, decoded_instruction.rn) = base_address;
            }
        } break;

        case INSTRUCTION_STM: {
            DEBUG_PRINT("INSTRUCTION_STM, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            u32 base_address = *get_register(cpu, decoded_instruction.rn);
            u16 register_list = decoded_instruction.register_list;
            assert(register_list != 0);

            int register_index = (decoded_instruction.U) ? 0 : 15;
            while (register_list) {
                if (decoded_instruction.U) {
                    // Increment
                    bool register_index_set = register_list & 1;
                    if (register_index_set) {
                        if (decoded_instruction.P) {
                            base_address += 4;

                            u32 *address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            *address = *get_register(cpu, (u8)register_index);
                        } else {
                            u32 *address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            *address = *get_register(cpu, (u8)register_index);

                            base_address += 4;
                        }
                    }

                    register_index++;
                    register_list >>= 1;
                } else {
                    // Decrement
                    bool register_index_set = (register_list >> 15) & 1;
                    if (register_index_set) {
                        if (decoded_instruction.P) {
                            base_address -= 4;

                            u32 *address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            *address = *get_register(cpu, (u8)register_index);
                        } else {
                            u32 *address = (u32 *)get_memory_at(cpu, &memory, base_address);
                            *address = *get_register(cpu, (u8)register_index);

                            base_address -= 4;
                        }
                    }

                    register_index--;
                    register_list <<= 1;
                }

            }

            if (decoded_instruction.W) {
                *get_register(cpu, decoded_instruction.rn) = base_address;
            }
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
            DEBUG_PRINT("INSTRUCTION_SWP, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
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
            DEBUG_PRINT("INSTRUCTION_SWI, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

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
            DEBUG_PRINT("INSTRUCTION_CDP... Not implemented for now, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            // assert(!"Implement");
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
            DEBUG_PRINT("INSTRUCTION_STC, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
        } break;
        case INSTRUCTION_LDC: {
            DEBUG_PRINT("INSTRUCTION_LDC, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

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
            DEBUG_PRINT("INSTRUCTION_MCR, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
        } break;
        case INSTRUCTION_MRC: {
            DEBUG_PRINT("INSTRUCTION_MRC, instruction = 0x%X, address = 0x%X, instruction_set = %s\n", decoded_instruction.encoding, decoded_instruction.address, get_current_instruction_encoding());

            assert(!"Implement");
        } break;


        default: {
            assert(!"Invalid instruction type for category");
        }
    }
}


void
execute()
{
    if (IN_THUMB_MODE) {
        thumb_execute();
        return;
    }

    if (decoded_instruction.type == INSTRUCTION_NONE) goto exit_execute;
    if (!should_execute_instruction(decoded_instruction.condition)) {
        // printf("Condition %d...Skipped\n", decoded_instruction.condition);
        goto exit_execute;
    }

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
        
#ifdef _DEBUG
        case INSTRUCTION_CATEGORY_DEBUG: {
            if (decoded_instruction.type == INSTRUCTION_DEBUG_EXIT) {
                is_running = false;
            }
        } break;
#endif // _DEBUG
    }

exit_execute:
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
        
#ifdef _DEBUG
        if ((current_instruction >> 28) & 0xF) {
            decoded_instruction.type = INSTRUCTION_DEBUG_EXIT;
        }
#endif
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

        // NOTE: R15 should not be used as the base register in any LDM or STM instruction.
        assert(decoded_instruction.rn != 15);

        // NOTE: Any subset of the registers, or all the registers, may be specified. The only restriction is that the register list should not be empty.
        assert(decoded_instruction.register_list > 0);

        // NOTE: S set means it is executed in privilege mode.
        assert(decoded_instruction.S == 0);
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
            // TODO: check the docs when writing into R15 (PC) register.
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
                    };
                } break;
                case 0b101000: {
                    decoded_instruction = (Instruction) {
                        .type = INSTRUCTION_MSR,
                        .P = (current_instruction >> 22) & 1,
                        .source_operand = current_instruction & 0xFFF,
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
        fprintf(stderr, "Instruction unknown: 0x%X\n", current_instruction);
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


static void
process_instructions()
{
    // Emulating pipelining.
    while (is_running) {
        // TODO: in the future, check if this separation works (because of the order they are executed).
        execute();
        decode();
        fetch();
    }
}







// ==================================================
// Part of the new header framework

// ARM lack of a no-op instruciton.
Instruction no_op = {
    .type = INSTRUCTION_NONE,
};

Instruction exit_instruction = {
    .type = INSTRUCTION_DEBUG_EXIT,
};

typedef struct TestCartridge {
    // Instruction instructions[256];
    // int count;
    u32 instructions[256];
    int count;
} TestCartridge;

void add_instruction(TestCartridge *cartridge, Instruction instruction)
{
    // cartridge->instructions[cartridge->count++] = instruction;

    u32 encoding = instruction.condition << 28;

    switch (instruction.type) {
        case INSTRUCTION_B: {
            encoding |= 0b101 << 25;
            encoding |= instruction.L << 24;
            encoding |= instruction.offset & 0xFFF;
        } break;
        case INSTRUCTION_AND: {
            encoding |= (instruction.I & 1) << 25;
            encoding |= (instruction.S & 1) << 20;
            encoding |= (instruction.rn & 0xF) << 16;
            encoding |= (instruction.rd & 0xF) << 12;
            encoding |= (instruction.second_operand & 0xFFF);
        } break;
        case INSTRUCTION_NONE: {
            encoding = 0;
        } break;
        case INSTRUCTION_DEBUG_EXIT: {
            encoding = (u32)-1;
        } break;
    }

    cartridge->instructions[cartridge->count++] = encoding;
}

void load_test_cartridge_into_memory(TestCartridge *cartridge)
{
    for (int i = 0; i < cartridge->count; i++) {
        *(((u32 *)memory.game_pak_rom) + i) = cartridge->instructions[i];
    }
}
// ==================================================

TestCartridge cartridge = {0};

void init_B()
{
    Instruction instruction = {
        .type = INSTRUCTION_B,
        .condition = CONDITION_AL,
        .L = 0,
        .offset = 1,
    };

    add_instruction(&cartridge, instruction);
    add_instruction(&cartridge, no_op);
    add_instruction(&cartridge, no_op);
    add_instruction(&cartridge, no_op);
    add_instruction(&cartridge, no_op);
    add_instruction(&cartridge, no_op);
    add_instruction(&cartridge, exit_instruction);
    load_test_cartridge_into_memory(&cartridge);
}

void eval_B()
{
    /* Order of increments (because of prefetching):
     * 1. Before executing B, pc = 2.
     * 2. After executing B, pc = 6.
     * 3. Next fetch, pc = 7.
     * 4. Decoding exit_instruction, next fetch, pc = 8.
     * 5. Set is_running flag to false, one last fetching, pc = 9.
     */
    CPU expected = {
        .pc = 9,
    };

    printf("PC = %d, expected = %d\n", cpu->pc, expected.pc);
}

void init_AND()
{
    u8 rotate = 0;
    u8 imm = 2;
    Instruction instruction = {
        .type = INSTRUCTION_AND,
        .condition = CONDITION_AL,
        .I = 1,
        .S = 1,
        .rn = 0,
        .rd = 1,
        .second_operand = ((rotate << 8) | imm) & 0xFFF,
    };

    cpu->r0 = 7;
    
    add_instruction(&cartridge, instruction);
    add_instruction(&cartridge, exit_instruction);
    add_instruction(&cartridge, no_op);
    add_instruction(&cartridge, no_op);
    load_test_cartridge_into_memory(&cartridge);
}

void eval_AND()
{
    CPU expected = {
        .r0 = 7,
        .r1 = 2,
        .cpsr = 0,
    };

    printf("r0 = %d, expected = %d\n", cpu->r0, expected.r0);
    printf("r1 = %d, expected = %d\n", cpu->r1, expected.r1);
    printf("cpsr = %d, expected = %d\n", cpu->cpsr, expected.cpsr);
}

void run_tests()
{
    // init_gba();
    // init_B();
    // process_instructions();
    // eval_B();


    // TODO: replicate the above test with this instructions and try to come up with a framework to add the remaining instruction.
    init_gba();
    init_AND();
    process_instructions();
    eval_AND();
}

/*
Do something like this:
TestDefinition test = {
    .cartridge = 
    .init = init_B,

};

DEFINE_TEST(test_B, test_B);
*/


int main(int argc, char *argv[])
{
#if 1
    init_gba();
    
    char *filename = "Donkey Kong Country 2.gba";
    int error = load_cartridge_into_memory(filename);
    if (error) {
        exit(1);
    }
    
    // CartridgeHeader *header = (CartridgeHeader *)memory.game_pak_rom;
    // printf("fixed_value = 0x%X, expected = 0x96\n", header->fixed_value);

    process_instructions();

    print_cpu_state(cpu);

    printf("Exit OK\n");
#else
// #include "test_B.c"
    init_gba();

    run_tests();
#endif

    return 0;
}
