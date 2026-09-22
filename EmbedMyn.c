// Myn embedded example, Thomas Führinger, 2025

#include "Myn.h"
#include <stdio.h>

int
main(int argc, char* argv[]) {
    MynValue result;
    int x = 3;
    char expression[] = "2 * x";

    MynEnvironment* environment = Myn_initialize();
    MynValue x_value = { .type = MYN_VALUE_TYPE_INT, .int_value = x };
    result = MynEnvironment_add_symbol(environment, "x", x_value);
    if (result.type == MYN_VALUE_TYPE_ERROR) {
        fprintf(stderr, "ERROR: %s\n", myn_error_message);
        return 1;
    }
    result = MynScript_evaluate(expression, environment);
    if (result.type == MYN_VALUE_TYPE_ERROR) {
        fprintf(stderr, "ERROR: %s\n", myn_error_message);
        return 1;
    }

    printf("Expression '%s' evaluated to '%d' with x = 3.\n", expression, result.int_value);
    result = Myn_finalize(environment);
    return 0;
}
