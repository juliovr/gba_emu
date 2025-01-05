#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>


typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;


typedef struct FileContent {
    int size;
    u8 *content;
} FileContent;

static FileContent
read_entire_file(char *filename)
{
    FileContent result = {0};
    FILE *file;
    if (fopen_s(&file, filename, "rb") == 0) {
        fseek(file, 0, SEEK_END);
        result.size = ftell(file);
        fseek(file, 0, SEEK_SET);

        result.content = (u8 *)malloc(result.size);
        fread(result.content, result.size, 1, file);
        
        fclose(file);
    } else {
        fprintf(stderr, "[ERROR]: Could not load file \"%s\"\n", filename);
    }

    return result;
}

int main()
{
    char *filename = "../Donkey Kong Country 2.gba";
    FileContent game = read_entire_file(filename);
    printf("Game size = %d\n", game.size);
    if (game.size == 0) {
        exit(1);
    }
    

    return 0;
}
