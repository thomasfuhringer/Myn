// Myn interpreter, Thomas Führinger, 2025

#include "Myn.h"
#include <stdio.h>

int
main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: myn <filename>\n");
        return 1;
    }

    MynValue result = MynRun(argv[1]);
    if (result.type == MYN_VALUE_TYPE_ERROR) {
        fprintf(stderr, "ERROR: %s\n", myn_error_message);
        return 1;
    }
    return 0;
}
