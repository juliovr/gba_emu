#ifndef CPU_H
#define CPU_H

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

    u64 cycles;
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

#endif // CPU_H