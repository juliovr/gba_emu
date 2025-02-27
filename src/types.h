#ifndef TYPES_H
#define TYPES_H

#include <stdint.h>

#define KILOBYTE (1024)
#define MEGABYTE (1024*1024)

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;


#define true 1
#define false 0

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

#endif // TYPES_H
