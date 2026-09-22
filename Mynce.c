// Byte code compiler for Myn programming language, Thomas Führinger, 2026

#include "Myn.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int
main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: mynce <filename>\n");
        return 1;
    }

    char* dot = strrchr(argv[1], '.');
    if (dot == nullptr) {
        printf("File must end in '.myn'\n");
        return 1;
    }
    if (!!strcmp(strrchr(argv[1], '.'), ".myn")) {
        printf("File must end in '.myn'\n");
        return 1;
    }

    auto len = strlen(argv[1]);
    char* output_file_name = malloc(len + 2);
    strcpy(output_file_name, argv[1]);
    *(output_file_name + len) = 'c';
    *(output_file_name + len + 1) = '\0';

    MynValue result = MynFile_lex(argv[1]);
    if (result.type == MYN_VALUE_TYPE_ERROR) {
        fprintf(stderr, "ERROR: %s\n", myn_error_message);
        return 1;
    }

    FILE* file_stream = fopen(output_file_name, "w");
    free(output_file_name);
    if (file_stream != nullptr) {
        fputc(MYN_MAGIC_NO_1, file_stream);
        fputc(MYN_MAGIC_NO_2, file_stream);
        fputc(MYN_VERSION, file_stream);
        fwrite(result.string, 1, result.length, file_stream);
        fclose(file_stream);
        return 0;
    }
    printf("Error saving byte code'\n");
    return 1;
}
