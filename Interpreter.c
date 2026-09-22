// Myn, a simple scripting language, Thomas Führinger, 2025-04-04
// Based on MIN by Carsten Herting (slu4) at https://github.com/slu4coder/Minimal-UART-CPU-System
// compile: clang -std=c23

#include "Myn.h"
#include "Tokens.h"
#include "Utilities.h"
#include <inttypes.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#    include <direct.h>
#else
#    include <sys/stat.h>
#endif

#define SYMBOL_TYPE_VARIABLE 0
#define SYMBOL_TYPE_REFERENCE 1
#define SYMBOL_ARRAY_INITIAL_SIZE 30

#define HALT_NONE 0
#define HALT_CONTINUE 1
#define HALT_BREAK 2
#define HALT_RETURN 3

char* data_type_name[14] = { "none", "bool", "byte", "integer", "float", "complex", "time", "string", "callable", "stream", "dictionary", "object", "type", "other" };

extern char myn_error_message[MYN_ERROR_MESSAGE_MAX];

typedef struct {
    uint32_t key;
    int type;
    int level;     // call stack nesting level
    int reference; // -1 if holds it holds a value or else index of referenced value
    MynValue value;
} Symbol;

typedef struct {
    Symbol* symbol;
    size_t size;
    size_t used;
} SymbolArray;

typedef struct MynEnvironment {
    uint8_t* text;
    size_t length;
    uint8_t* pos;
    int level;
    int halt; // it is time to return from function or exit loop
    SymbolArray symbols;
} MynEnvironment;

// forward declarations
MynValue Expression_process(MynEnvironment* env);
void MathExpression_process(MynEnvironment* env, MynValue* result);
MynValue Script_lex(const uint8_t* script_text);
void ErrorMessage_set(const char* text, ...);
MynValue PrintStatement_process(MynEnvironment* env);
MynValue Block_execute(MynEnvironment* env);
MynValue Block_skip(MynEnvironment* env);
void Value_process(MynEnvironment* env, MynValue* value);
MynValue Call_execute(MynEnvironment* env, MynValue* callable_value);
MynValue Stream_write_string(MynEnvironment* env, FILE* stream);

#define TRY(expr) ({ MynValue r = expr; if (r.type == MYN_VALUE_TYPE_ERROR) return r; })
#define RETURN_ERROR(errtype, message, ...) ({ ErrorMessage_set( message __VA_OPT__(,)__VA_ARGS__); return (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = errtype }; })
#define RETURN_NONE() \
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE }

MynValue
MynValue_duplicate(MynValue* value) {
    MynValue new = *value;
    if (value->size > 0) {
        new.string = malloc(value->size);
        memcpy(new.string, value->string, value->size);
    }
    return new;
}

MynValue
MynValue_shadow(MynValue* value) {
    MynValue new = *value;
    new.size = 0;
    return new;
}

bool
MynValue_bytearray_add(MynValue* value, char what) {
    if (value->type != MYN_VALUE_TYPE_BYTE)
        return false;

    if (value->length == 0) {
        value->size = MYN_VALUE_ARRAY_INITIAL_COUNT;
        value->int_array = (int32_t*)malloc(value->size);
        if (value->int_array == nullptr)
            return false;
    } else if (value->length == value->size) {
        value->size *= 2;
        value->byte_array = (char*)realloc(value->int_array, value->size);
        if (value->byte_array == nullptr)
            return false;
    }
    *(value->byte_array + value->length++) = what;
    return true;
}

bool
MynValue_intarray_add(MynValue* value, int what) {
    if (value->type != MYN_VALUE_TYPE_INT)
        return false;

    if (value->length == 0) {
        value->size = MYN_VALUE_ARRAY_INITIAL_COUNT * sizeof(int32_t);
        value->int_array = (int32_t*)malloc(value->size);
        if (value->int_array == nullptr)
            return false;
    } else if (value->length * sizeof(int32_t) == value->size) {
        value->size *= 2;
        value->int_array = (int32_t*)realloc(value->int_array, value->size);
        if (value->int_array == nullptr)
            return false;
    }
    *(value->int_array + value->length++) = what;
    return true;
}

bool
MynValue_floatarray_add(MynValue* value, float what) {
    if (value->type != MYN_VALUE_TYPE_FLOAT)
        return false;

    if (value->length == 0) {
        value->size = MYN_VALUE_ARRAY_INITIAL_COUNT * sizeof(float);
        value->float_array = (float*)malloc(value->size);
        if (value->float_array == nullptr)
            return false;
    } else if (value->length * sizeof(float) == value->size) {
        value->size *= 2;
        value->float_array = (float*)realloc(value->float_array, value->size);
        if (value->float_array == nullptr)
            return false;
    }
    *(value->float_array + value->length++) = what;
    return true;
}

MynValue
MynValue_array_get(MynValue* array, size_t index) {
    if (index >= array->length)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Index out of bounds.");
    if (array->type == MYN_VALUE_TYPE_INT)
        return (MynValue) { .type = MYN_VALUE_TYPE_INT, .int_value = array->int_array[index] };
    else if (array->type == MYN_VALUE_TYPE_FLOAT)
        return (MynValue) { .type = MYN_VALUE_TYPE_FLOAT, .float_value = array->float_array[index] };
    else if (array->type == MYN_VALUE_TYPE_BYTE)
        return (MynValue) { .type = MYN_VALUE_TYPE_BYTE, .byte_value = array->byte_array[index] };
    else
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Unsupported array type.");
}

MynValue
MynValue_array_concatenate(MynValue* value, MynValue* what) {
    if (value->type != what->type)
        RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "Incompatible value types for additon assignment.");

    for (int i = 0; i < what->length; i++) {
        if (value->type == MYN_VALUE_TYPE_BYTE) {
            if (!MynValue_bytearray_add(value, what->byte_array[i]))
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append byte to array.");
        } else if (value->type == MYN_VALUE_TYPE_INT) {
            if (!MynValue_intarray_add(value, what->int_array[i]))
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append integer to array.");
        } else if (value->type == MYN_VALUE_TYPE_FLOAT) {
            if (!MynValue_floatarray_add(value, what->float_array[i]))
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append float to array.");
        } else
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append value to array.");
    }
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

bool
MynValue_str_set(MynValue* value, const char* to) {
    if (value->type != MYN_VALUE_TYPE_STR)
        return false;

    size_t length = strlen(to);
    if (length <= MYN_VALUE_SHORT_STRING) {
        if (value->size > 0)
            free(value->string);
        strcpy(value->short_string, to);
        value->size = 0;
    } else {
        if (value->string == nullptr) {
            value->size = length + 1;
            value->string = malloc(value->size);
            if (value->string == nullptr)
                return false;
        } else if (length + 1 > value->size) {
            value->size = length + 1;
            value->string = realloc(value->string, value->size);
            if (value->string == nullptr)
                return false;
        }
        strcpy(value->string, to);
    }
    value->length = length;
    return true;
}

MynValue
MynValue_str_add(MynValue* string, const MynValue* add_string) {
    if ((string->type != MYN_VALUE_TYPE_STR) || (add_string->type != MYN_VALUE_TYPE_STR))
        RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "String concatenation failed.");

    if ((string->length + add_string->length) <= MYN_VALUE_SHORT_STRING) {
        memcpy(string->short_string + string->length, add_string->short_string, add_string->length);
        string->short_string[string->length + add_string->length] = '\0';
    } else {
        if (string->length <= MYN_VALUE_SHORT_STRING) {
            string->size = string->length + add_string->length + 1;
            char* tmp = malloc(string->size);
            if (tmp == nullptr)
                RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "String concatenation failed.");
            strcpy(tmp, string->short_string);
            string->string = tmp;
        }
        if (string->size < string->length + add_string->length + 1) {
            string->size = string->length + add_string->length + 1;
            string->string = realloc(string->string, string->size);
            if (string->string == nullptr)
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Out of memory.");
        }
        if (add_string->length <= MYN_VALUE_SHORT_STRING)
            strcpy(string->string + string->length, add_string->short_string);
        else
            strcpy(string->string + string->length, add_string->string);
    }
    string->length += add_string->length;
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

const uint8_t*
MynValue_str_get(MynValue* value) {
    if (value->type != MYN_VALUE_TYPE_STR)
        return nullptr;
    return value->length <= MYN_VALUE_SHORT_STRING ? value->short_string : value->string;
}

void
MynValue_free(MynValue* value) {
    if (value->size > 0) {
        free(value->string);
        value->string = nullptr;
    }
    value->length = value->size = 0;
}

void
SymbolArray_init(SymbolArray* array, size_t initial_size) {
    array->symbol = malloc(initial_size * sizeof(Symbol));
    array->size = initial_size;
    array->used = 0;
}

int
SymbolArray_lookup(const SymbolArray* array, uint32_t key) {
    for (int i = array->used - 1; i >= 0; i--) { // pick latest first to reflect nesting
        if (array->symbol[i].key == key)
            return i;
    }
    return -1;
}

MynValue
SymbolArray_append(SymbolArray* array, const Symbol* symbol) {
    if (array->used == array->size) {
        array->size *= 2;
        array->symbol = realloc(array->symbol, array->size * sizeof(Symbol));
        if (array->symbol == nullptr)
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Out of memory.");
    }
    array->symbol[array->used++] = *symbol;
    return (MynValue) { .type = MYN_VALUE_TYPE_INT, .int_value = array->used - 1 };
}

MynValue
MynEnvironment_add_symbol(MynEnvironment* env, const char* variable, MynValue value) {
    const Symbol symbol = { .key = Span_hash32(&(Span) { .position = variable, .length = strlen(variable) }), .type = SYMBOL_TYPE_VARIABLE, .level = env->level, .value = value };
    return SymbolArray_append(&env->symbols, &symbol);
}

void
SymbolArray_trim(SymbolArray* array, int up_to_position) {
    while (array->used > up_to_position) {
        MynValue_free(&array->symbol[--array->used].value);
    }
}

void
SymbolArray_free(SymbolArray* array) {
    free(array->symbol);
    array->symbol = NULL;
    array->used = array->size = 0;
}

void // for debugging purposes
SymbolArray_print(const SymbolArray* array) {
    for (int i = 0; i < array->used; i++) {
        fprintf(stdout, "Symbol #%d 0x%x, level %d", i, array->symbol[i].key, array->symbol[i].level);

        switch (array->symbol[i].type) {

        case SYMBOL_TYPE_REFERENCE:
            fprintf(stdout, " -> reference to # 0x%x", (unsigned)array->symbol[array->symbol[i].reference].key);
            break;

        case SYMBOL_TYPE_VARIABLE:
            switch (array->symbol[i].value.type) {
            case MYN_VALUE_TYPE_INT:
                if (array->symbol[i].value.length == 0)
                    fprintf(stdout, " -> integer %d", array->symbol[i].value.int_value);
                else {
                    fprintf(stdout, " -> integer array ");
                    fprintf(stdout, "[");
                    for (int ii = 0; ii < array->symbol[i].value.length; ii++) {
                        fprintf(stdout, "%d", *(array->symbol[i].value.int_array + ii));
                        if (ii < array->symbol[i].value.length - 1)
                            fprintf(stdout, ", ");
                    }
                    fprintf(stdout, "]");
                }
                fflush(stdout);
                break;
            case MYN_VALUE_TYPE_FLOAT:
                if (array->symbol[i].value.length == 0)
                    fprintf(stdout, " -> float %f", array->symbol[i].value.float_value);
                else {
                    fprintf(stdout, " -> float array ");
                    fprintf(stdout, "[");
                    for (int ii = 0; ii < array->symbol[i].value.length; ii++) {
                        fprintf(stdout, "%f", *(array->symbol[i].value.float_array + ii));
                        if (ii < array->symbol[i].value.length - 1)
                            fprintf(stdout, ", ");
                    }
                    fprintf(stdout, "]");
                }
                fflush(stdout);
                break;
            case MYN_VALUE_TYPE_STR:
                fprintf(stdout, " -> string%s \"%s\"", array->symbol[i].value.length <= MYN_VALUE_SHORT_STRING ? " (short)" : "", MynValue_str_get(&array->symbol[i].value));
                break;
            }
        }
        fputc('\n', stdout);
    }
    fflush(stdout);
}

MynValue
TypeFunction_process(MynEnvironment* env) {
    MynValue argument = Expression_process(env);
    if (argument.type == MYN_VALUE_TYPE_ERROR)
        return argument;
    auto length = argument.length;
    MynValue_free(&argument);
    if (*env->pos++ != ')')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");
    return (MynValue) { .type = MYN_VALUE_TYPE_TYPE, .type_value = argument.type, .length = length };
}

MynValue
LenFunction_process(MynEnvironment* env) {
    MynValue argument = Expression_process(env);
    if (argument.type == MYN_VALUE_TYPE_ERROR)
        return argument;
    auto length = argument.length;
    MynValue_free(&argument);
    if (*env->pos++ != ')')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");
    return (MynValue) { .type = MYN_VALUE_TYPE_INT, .int_value = length };
}

MynValue
StrFunction_process(MynEnvironment* env) {
    MynValue argument = Expression_process(env);
    if (argument.type == MYN_VALUE_TYPE_ERROR)
        return argument;

    if (argument.type != MYN_VALUE_TYPE_INT) {
        MynValue_free(&argument);
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected an integer argument.");
    }
    int number = argument.int_value;

    if (*env->pos == ',') {
        env->pos++;
        argument = Expression_process(env);
        if (argument.type == MYN_VALUE_TYPE_ERROR)
            return argument;

        if (argument.type != MYN_VALUE_TYPE_STR) {
            MynValue_free(&argument);
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected a string as format argument.");
        }
    }

    if (*env->pos++ != ')')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");

    char* format = (argument.type == MYN_VALUE_TYPE_STR) ? MynValue_str_get(&argument) : nullptr;

    char buffer[12];
    sprintf(buffer, format ? format : "%d", number);
    MynValue str = { .type = MYN_VALUE_TYPE_STR };
    MynValue_str_set(&str, buffer);
    MynValue_free(&argument);
    return str;
}

MynValue
IntFunction_process(MynEnvironment* env) {
    MynValue argument = Expression_process(env);
    MynValue result;

    if (argument.type == MYN_VALUE_TYPE_ERROR)
        return argument;

    if (argument.type == MYN_VALUE_TYPE_STR) {
        if (*env->pos++ != ')')
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");
        result = (MynValue) { .type = MYN_VALUE_TYPE_INT, .int_value = atoi(MynValue_str_get(&argument)) };
        MynValue_free(&argument);
        return result;
    } else if (argument.type == MYN_VALUE_TYPE_FLOAT) {
        if (*env->pos++ != ')')
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");

        return (MynValue) { .type = MYN_VALUE_TYPE_INT, .int_value = (int)argument.float_value };
    } else if (argument.type == MYN_VALUE_TYPE_BYTE) {
        if (*env->pos++ != ')')
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");

        if (argument.length > 0) {
            result = (MynValue) { .type = MYN_VALUE_TYPE_INT, .float_value = (int)argument.byte_array[0] };
            MynValue_free(&argument);
        } else
            result = (MynValue) { .type = MYN_VALUE_TYPE_INT, .float_value = (int)argument.byte_value };
        return result;
    } else {
        MynValue_free(&argument);
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Invalid argument type.");
    }
}

MynValue // returns true if arguments reference the same value
IdenticalFunction_process(MynEnvironment* env) {
    RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Not yet implemented.");
}

MynValue
ByteFunction_process(MynEnvironment* env) {
    MynValue argument = Expression_process(env);
    if (argument.type == MYN_VALUE_TYPE_ERROR)
        return argument;

    if (argument.type == MYN_VALUE_TYPE_INT) {
        if (*env->pos++ != ')')
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");

        return (MynValue) { .type = MYN_VALUE_TYPE_INT, .byte_value = (uint8_t)argument.int_value };
    } else {
        MynValue_free(&argument);
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Invalid argument type.");
    }
}

MynValue
FloatFunction_process(MynEnvironment* env) {
    MynValue argument = Expression_process(env);
    MynValue result;
    if (argument.type == MYN_VALUE_TYPE_ERROR)
        return argument;

    if (argument.type == MYN_VALUE_TYPE_STR) {
        if (*env->pos++ != ')')
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");
        result = (MynValue) { .type = MYN_VALUE_TYPE_FLOAT, .float_value = atof(MynValue_str_get(&argument)) };
        MynValue_free(&argument);
        return result;
    } else if (argument.type == MYN_VALUE_TYPE_INT) {
        if (*env->pos++ != ')')
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");
        return (MynValue) { .type = MYN_VALUE_TYPE_FLOAT, .float_value = (float)argument.int_value };
    } else if (argument.type == MYN_VALUE_TYPE_BYTE) {
        if (*env->pos++ != ')')
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");

        if (argument.length > 0) {
            result = (MynValue) { .type = MYN_VALUE_TYPE_FLOAT, .float_value = (float)argument.byte_array[0] };
            MynValue_free(&argument);
        } else
            result = (MynValue) { .type = MYN_VALUE_TYPE_FLOAT, .float_value = (float)argument.byte_value };
        return result;
    } else {
        MynValue_free(&argument);
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Invalid argument type.");
    }
}

MynValue
RandomFunction_process(MynEnvironment* env) {
    MynValue argument = Expression_process(env);
    if (argument.type == MYN_VALUE_TYPE_ERROR)
        return argument;

    if (argument.type != MYN_VALUE_TYPE_FLOAT) {
        MynValue_free(&argument);
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected a float argument.");
    }
    if (*env->pos++ != ')')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");

    srand(time(NULL));
    float number = (float)rand() * argument.float_value / RAND_MAX;
    MynValue result = { .type = MYN_VALUE_TYPE_FLOAT, .float_value = number };

    return result;
}

MynValue
PeekFunction_process(MynEnvironment* env) {
    MynValue argument = { .type = MYN_VALUE_TYPE_NONE };
    MathExpression_process(env, &argument);
    if (argument.type == MYN_VALUE_TYPE_ERROR)
        return argument;
    if (argument.type != MYN_VALUE_TYPE_INT)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected an integer argument to Peek statement.");

    char byte = *((char*)argument.int_value);

    if (*env->pos++ != ')')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");
    return (MynValue) { .type = MYN_VALUE_TYPE_BYTE, .byte_value = byte };
}

MynValue
PokeStatement_process(MynEnvironment* env) {
    MynValue argument = { .type = MYN_VALUE_TYPE_NONE };
    MathExpression_process(env, &argument);
    if (argument.type == MYN_VALUE_TYPE_ERROR)
        return argument;
    if (argument.type != MYN_VALUE_TYPE_INT)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected an integer as first argument to Poke statement.");
    char* address = (char*)argument.int_value;

    if (*env->pos++ != ',')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ','.");
    MathExpression_process(env, &argument);
    if (argument.type == MYN_VALUE_TYPE_ERROR)
        return argument;
    if (argument.type != MYN_VALUE_TYPE_BYTE)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected a byte value as second argument to Poke statement.");

    *address = argument.byte_value;

    if (*env->pos++ != ')')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

MynValue
FileExistsFunction_process(MynEnvironment* env) {
    MynValue file_name = Expression_process(env);
    if (file_name.type == MYN_VALUE_TYPE_ERROR)
        return file_name;
    if (file_name.type != MYN_VALUE_TYPE_STR)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a string as file name.");
    if (*env->pos++ != ')')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");

#ifdef _WIN32
    DWORD dwAttrib = GetFileAttributes((char*)MynValue_str_get(&file_name));
    MynValue result = { .type = MYN_VALUE_TYPE_BOOL, .bool_value = (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY)) };
#else
    struct stat buffer;
    MynValue result = { .type = MYN_VALUE_TYPE_BOOL, .bool_value = stat((char*)MynValue_str_get(&file_name), &buffer) == 0 };
#endif

    MynValue_free(&file_name);
    return result;
}

MynValue
FileOpenFunction_process(MynEnvironment* env) {
    MynValue file_name = Expression_process(env);
    if (file_name.type == MYN_VALUE_TYPE_ERROR)
        return file_name;
    if (file_name.type != MYN_VALUE_TYPE_STR)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a string as file name.");
    if (*env->pos++ != ',')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ','.");

    MynValue writeable = Expression_process(env);
    if (writeable.type == MYN_VALUE_TYPE_ERROR)
        return writeable;
    if (writeable.type != MYN_VALUE_TYPE_BOOL)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a bool as 'writeable' argument.");
    if (*env->pos++ != ')')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");

    FILE* file = fopen((char*)MynValue_str_get(&file_name), writeable.bool_value ? "a+" : "r");
    MynValue_free(&file_name);

    if (file == NULL)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot open file.");

    return (MynValue) { .type = MYN_VALUE_TYPE_STREAM, .subtype = MYN_VALUE_TYPE_STREAM_FILE, .file_stream = file };
}

MynValue
FileCloseStatement_process(MynEnvironment* env) {
    MynValue stream = Expression_process(env);
    if (stream.type == MYN_VALUE_TYPE_ERROR)
        return stream;
    if ((stream.type != MYN_VALUE_TYPE_STREAM) || (stream.subtype != MYN_VALUE_TYPE_STREAM_FILE))
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a file stream as argument.");

    if (fclose(stream.file_stream) == EOF)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Could not close file stream.");
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

MynValue
FileCopyStatement_process(MynEnvironment* env) {
    MynValue file_name = Expression_process(env);
    if (file_name.type == MYN_VALUE_TYPE_ERROR)
        return file_name;
    if (file_name.type != MYN_VALUE_TYPE_STR)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a string as file name.");

    if (*env->pos++ != ',')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ','.");

    MynValue file_name_dest = Expression_process(env);
    if (file_name_dest.type == MYN_VALUE_TYPE_ERROR)
        return file_name;
    if (file_name_dest.type != MYN_VALUE_TYPE_STR)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a string as destination file name.");

    char buffer[4096];
    FILE* stream_read = fopen((char*)MynValue_str_get(&file_name), "r");
    FILE* stream_write = fopen((char*)MynValue_str_get(&file_name_dest), "w");

    while (!feof(stream_read)) {
        size_t bytes = fread(buffer, 1, sizeof(buffer), stream_read);
        if (bytes) {
            fwrite(buffer, 1, bytes, stream_write);
        }
    }

    fclose(stream_read);
    fclose(stream_write);
    MynValue_free(&file_name);
    MynValue_free(&file_name_dest);
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

MynValue
FileDeleteStatement_process(MynEnvironment* env) {
    MynValue file_name = Expression_process(env);
    if (file_name.type == MYN_VALUE_TYPE_ERROR)
        return file_name;
    if (file_name.type != MYN_VALUE_TYPE_STR)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a string as file name.");

    if (remove((char*)MynValue_str_get(&file_name)) != 0)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Could not delete file.");
    MynValue_free(&file_name);
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

MynValue
FileRenameStatement_process(MynEnvironment* env) {
    MynValue file_name = Expression_process(env);
    if (file_name.type == MYN_VALUE_TYPE_ERROR)
        return file_name;
    if (file_name.type != MYN_VALUE_TYPE_STR)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a string as file name.");

    if (*env->pos++ != ',')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ','.");

    MynValue file_name_dest = Expression_process(env);
    if (file_name_dest.type == MYN_VALUE_TYPE_ERROR)
        return file_name;
    if (file_name_dest.type != MYN_VALUE_TYPE_STR)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a string as destination file name.");

    if (rename((char*)MynValue_str_get(&file_name), (char*)MynValue_str_get(&file_name_dest)) != 0)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Could not delete file.");
    MynValue_free(&file_name);
    MynValue_free(&file_name_dest);
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

MynValue
StreamReadByteFunction_process(MynEnvironment* env) {
    MynValue stream = Expression_process(env);
    if (stream.type == MYN_VALUE_TYPE_ERROR)
        return stream;
    if ((stream.type != MYN_VALUE_TYPE_STREAM) || (stream.subtype != MYN_VALUE_TYPE_STREAM_FILE))
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a file stream as argument.");
    if (*env->pos++ != ')')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");

    int result = fgetc(stream.file_stream);
    if (result == EOF)
        return (MynValue) { .type = MYN_VALUE_TYPE_OTHER, .subtype = MYN_VALUE_TYPE_OTHER_EOF };
    else
        return (MynValue) { .type = MYN_VALUE_TYPE_BYTE, .byte_value = result };
}

MynValue
StreamReadStringFunction_process(MynEnvironment* env) {
    MynValue stream = Expression_process(env);
    if (stream.type == MYN_VALUE_TYPE_ERROR)
        return stream;
    if ((stream.type != MYN_VALUE_TYPE_STREAM) || (stream.subtype != MYN_VALUE_TYPE_STREAM_FILE))
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a file stream as argument.");

    if (*env->pos++ != ',')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ','.");

    MynValue max = Expression_process(env);
    if (max.type == MYN_VALUE_TYPE_ERROR)
        return max;
    if (max.type != MYN_VALUE_TYPE_INT)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected an integer as maximum.");
    if (*env->pos++ != ')')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ')'.");

    char buffer[max.int_value + 1];
    char* success = fgets(buffer, max.int_value + 1, stream.file_stream);
    if (success == NULL)
        return (MynValue) { .type = MYN_VALUE_TYPE_OTHER, .subtype = MYN_VALUE_TYPE_OTHER_EOF };
    else {
        MynValue result = { .type = MYN_VALUE_TYPE_STR };
        MynValue_str_set(&result, buffer);
        return result;
    }
}

MynValue
StreamWriteByteStatement_process(MynEnvironment* env) {
    MynValue stream = Expression_process(env);
    if (stream.type == MYN_VALUE_TYPE_ERROR)
        return stream;
    if ((stream.type != MYN_VALUE_TYPE_STREAM) || (stream.subtype != MYN_VALUE_TYPE_STREAM_FILE))
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a file stream as argument.");
    if (*env->pos++ != ',')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ','.");

    MynValue byte = Expression_process(env);
    if (byte.type == MYN_VALUE_TYPE_ERROR)
        return byte;
    if (byte.type != MYN_VALUE_TYPE_BYTE)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a byte as data.");

    int result;
    if (byte.length > 0) {
        for (int i = 0; i < byte.length; i++) {
            result = fputc(byte.byte_array[i], stream.file_stream);
            if (result == EOF)
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Could not write byte");
        }
        return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
    } else {
        result = fputc(byte.byte_value, stream.file_stream);
        if (result == EOF)
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Could not write byte");
        else
            return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
    }
}

MynValue
StreamWriteStringStatement_process(MynEnvironment* env) {
    MynValue stream = Expression_process(env);
    if (stream.type == MYN_VALUE_TYPE_ERROR)
        return stream;
    if ((stream.type != MYN_VALUE_TYPE_STREAM) || (stream.subtype != MYN_VALUE_TYPE_STREAM_FILE))
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a file stream as argument.");
    if (*env->pos++ != ',')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ','.");

    return Stream_write_string(env, stream.file_stream);
}

MynValue
StreamPositionMoveStatement_process(MynEnvironment* env) {
    MynValue stream = Expression_process(env);
    if (stream.type == MYN_VALUE_TYPE_ERROR)
        return stream;
    if ((stream.type != MYN_VALUE_TYPE_STREAM) || (stream.subtype != MYN_VALUE_TYPE_STREAM_FILE))
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a file stream as argument.");
    if (*env->pos++ != ',')
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected ','.");

    MynValue offset = Expression_process(env);
    if (offset.type == MYN_VALUE_TYPE_ERROR)
        return offset;
    if (offset.type != MYN_VALUE_TYPE_INT)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected an integer as offset.");

    int result;
    if (offset.int_value == 0)
        result = fseek(stream.file_stream, 0, SEEK_SET);
    else
        result = fseek(stream.file_stream, offset.int_value, SEEK_CUR);

    if (result != 0)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Could not set the position");
    else
        return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

MynValue
WhileStatement_process(MynEnvironment* env) {
    MynValue condition;
    MynValue result;
    uint8_t* loop = env->pos;

RepeatWhile:
    condition = Expression_process(env);

    if (condition.type == MYN_VALUE_TYPE_ERROR)
        return condition;

    if (condition.type != MYN_VALUE_TYPE_BOOL)
        RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "Invalid condition for while statement");

    if (condition.bool_value && env->halt < HALT_BREAK) {
        result = Block_execute(env);

        if (result.type == MYN_VALUE_TYPE_ERROR)
            return result;
        else
            MynValue_free(&result);

        env->pos = loop;
        if (env->halt == HALT_CONTINUE)
            env->halt = HALT_NONE;
        goto RepeatWhile;
    } else
        Block_skip(env);

    if (env->halt < HALT_RETURN)
        env->halt = HALT_NONE;

    return result;
}

MynValue
DoStatement_process(MynEnvironment* env) {
    MynValue condition = { .type = MYN_VALUE_TYPE_BOOL, .bool_value = true };
    MynValue result;
    uint8_t* loop = env->pos;

RepeatDo:
    result = Block_execute(env);
    if (result.type == MYN_VALUE_TYPE_ERROR)
        return result;

    if (*env->pos++ != TOK_DO_WHILE)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected 'while'.");

    condition = Expression_process(env);
    if (condition.type == MYN_VALUE_TYPE_ERROR)
        return condition;

    if (condition.type != MYN_VALUE_TYPE_BOOL)
        RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "Invalid condition for while statement");

    if (condition.bool_value && env->halt < HALT_BREAK) {
        env->pos = loop;
        MynValue_free(&result);
        if (env->halt == HALT_CONTINUE)
            env->halt = HALT_NONE;
        goto RepeatDo;
    }
    if (env->halt < HALT_RETURN)
        env->halt = HALT_NONE;
    return result;
}

MynValue
ForStatement_process(MynEnvironment* env) {
    MynValue result = { .type = MYN_VALUE_TYPE_NONE };
    MynValue value = { .type = MYN_VALUE_TYPE_NONE };
    int limit;

    uint8_t c = *env->pos++;
    if (c != TOK_VARIABLE)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a variable.");

    ValueAsBytes hash = { .byte[0] = env->pos[3], .byte[1] = env->pos[2], .byte[2] = env->pos[1], .byte[3] = env->pos[0] };
    int counter_index = SymbolArray_lookup(&env->symbols, hash.integer);
    env->pos += 4;

    c = *env->pos++;
    if (c != TOK_ASSIGN)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected an assignemnt.");

    MathExpression_process(env, &value);
    if (value.type == MYN_VALUE_TYPE_ERROR)
        return value;
    if (value.type != MYN_VALUE_TYPE_INT)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Invalid data type for counter variable");

    if (counter_index != -1) {
        env->symbols.symbol[counter_index].value = value;
    } else {
        Symbol counter = { .key = hash.integer, .type = SYMBOL_TYPE_VARIABLE, .level = env->level, .value = value };
        MynValue new_index = SymbolArray_append(&env->symbols, &counter);
        if (new_index.type == MYN_VALUE_TYPE_ERROR)
            return new_index;
        counter_index = new_index.int_value;
    }

    c = *env->pos++;
    if (c != TOK_TO)
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected a TO statement.");

    value.type = MYN_VALUE_TYPE_NONE;
    MathExpression_process(env, &value);
    if (value.type != MYN_VALUE_TYPE_INT)
        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Invalid data type for index");

    limit = value.int_value;
    uint8_t* loop = env->pos;
    while (env->symbols.symbol[counter_index].value.int_value <= limit) {
        MynValue_free(&result);
        result = Block_execute(env);
        if (result.type == MYN_VALUE_TYPE_ERROR)
            return result;

        env->symbols.symbol[counter_index].value.int_value++;
        env->pos = loop;
    }
    Block_skip(env);
    env->symbols.symbol[counter_index].value.int_value--;
    return result;
}

MynValue
BreakStatement_process(MynEnvironment* env) {
    env->halt = HALT_BREAK;
    MynValue result = { .type = MYN_VALUE_TYPE_NONE };
    return result;
}

MynValue
ContinueStatement_process(MynEnvironment* env) {
    env->halt = HALT_CONTINUE;
    MynValue result = { .type = MYN_VALUE_TYPE_NONE };
    return result;
}

MynValue
IfStatement_process(MynEnvironment* env) {
    MynValue condition = Expression_process(env);
    if (condition.type == MYN_VALUE_TYPE_ERROR)
        return condition;

    if (condition.type != MYN_VALUE_TYPE_BOOL)
        RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "Invalid condition for if statement");

    if (condition.bool_value) {
        MynValue result = Block_execute(env);
        if (*env->pos == TOK_ELSE) {
            env->pos++;
            Block_skip(env);
        }
        return result;
    } else {
        Block_skip(env);
        while (*env->pos == TOK_ELIF) {
            env->pos++;
            condition = Expression_process(env);
            if (condition.type == MYN_VALUE_TYPE_ERROR)
                return condition;
            if (condition.type != MYN_VALUE_TYPE_BOOL)
                RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "Invalid condition for ELIF statement");
            if (condition.bool_value) {
                return Block_execute(env);
            }
            Block_skip(env);
        }
        if (*env->pos == TOK_ELSE) {
            env->pos++;
            return Block_execute(env);
        }
    }
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

MynValue
Stream_write_string(MynEnvironment* env, FILE* stream) {
    MynValue argument;
    do {
        if (*env->pos == ',')
            env->pos++;

        argument = Expression_process(env);
        if (argument.type == MYN_VALUE_TYPE_ERROR)
            return argument;
        if (argument.type == MYN_VALUE_TYPE_NONE)
            continue;
        if (argument.type == MYN_VALUE_TYPE_STR) {
            fprintf(stream, "%s", MynValue_str_get(&argument));
            fflush(stream);
        } else if (argument.type == MYN_VALUE_TYPE_BOOL) {
            if (argument.bool_value)
                fprintf(stream, "TRUE");
            else
                fprintf(stream, "FALSE");
        } else if (argument.type == MYN_VALUE_TYPE_INT) {
            if (argument.length == 0)
                fprintf(stream, "%d", argument.int_value);
            else {
                fprintf(stream, "[");
                for (int i = 0; i < argument.length; i++) {
                    fprintf(stream, "%d", *(argument.int_array + i));
                    if (i < argument.length - 1)
                        fprintf(stream, ", ");
                }
                fprintf(stream, "]");
            }
            fflush(stream);
        } else if (argument.type == MYN_VALUE_TYPE_BYTE) {
            if (argument.length == 0)
                fprintf(stream, "%hhu", argument.byte_value);
            else {
                fprintf(stream, "[");
                for (int i = 0; i < argument.length; i++) {
                    fprintf(stream, "%hhu", *(argument.byte_array + i));
                    if (i < argument.length - 1)
                        fprintf(stream, ", ");
                }
                fprintf(stream, "]");
            }
            fflush(stream);
        } else if (argument.type == MYN_VALUE_TYPE_FLOAT) {
            if (argument.length == 0)
                fprintf(stream, "%f", argument.float_value);
            else {
                fprintf(stream, "[");
                for (int i = 0; i < argument.length; i++) {
                    fprintf(stream, "%f", *(argument.float_array + i));
                    if (i < argument.length - 1)
                        fprintf(stream, ", ");
                }
                fprintf(stream, "]");
            }
            fflush(stream);
        } else if (argument.type == MYN_VALUE_TYPE_TYPE) {
            fprintf(stream, data_type_name[argument.type_value]);
            if ((argument.length > 0) && (argument.type_value != MYN_VALUE_TYPE_STR))
                fprintf(stream, " array");
        } else {
            MynValue_free(&argument);
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Invalid argument for print statement");
        }
        MynValue_free(&argument);
    } while (*env->pos == ',');

    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

MynValue
PrintStatement_process(MynEnvironment* env) {
    return Stream_write_string(env, stdout);
}

void
MathTerm_process(MynEnvironment* env, MynValue* result) {
    if (result->type == MYN_VALUE_TYPE_NONE) {
        Value_process(env, result);
        if (result->type == MYN_VALUE_TYPE_ERROR)
            return;
    }
    char c = *env->pos;
    MynValue factor = *result;
    while ((factor.type != MYN_VALUE_TYPE_ERROR) && (c == '*' || c == '/' || c == TOK_MODULO) && (env->pos - env->text < env->length)) {
        env->pos++;
        if ((result->type == MYN_VALUE_TYPE_INT) || (result->type == MYN_VALUE_TYPE_BYTE) || (result->type == MYN_VALUE_TYPE_FLOAT)) {
            Value_process(env, &factor);
            if (factor.type == MYN_VALUE_TYPE_ERROR) {
                *result = factor;
                return;
            }
            if (c == TOK_MODULO) {
                int r = 0;
                if ((result->type == MYN_VALUE_TYPE_INT) || (factor.type == MYN_VALUE_TYPE_INT)) {
                    r = result->int_value % factor.int_value;
                } // ToDo: return an error if arguments are not int
                result->int_value = r;
                result->type = MYN_VALUE_TYPE_INT;
                c = *env->pos;
                continue;
            }
            if ((factor.type == MYN_VALUE_TYPE_INT) || (factor.type == MYN_VALUE_TYPE_BYTE) || (factor.type == MYN_VALUE_TYPE_FLOAT)) {
                double r = result->type == MYN_VALUE_TYPE_INT ? (double)result->int_value : (result->type == MYN_VALUE_TYPE_FLOAT ? (double)result->float_value : (double)result->byte_value);
                double f = factor.type == MYN_VALUE_TYPE_INT ? (double)factor.int_value : (factor.type == MYN_VALUE_TYPE_FLOAT ? (double)factor.float_value : (double)factor.byte_value);

                if (c == '*')
                    r *= f;
                else
                    r /= f;

                if ((result->type == MYN_VALUE_TYPE_FLOAT) || (factor.type == MYN_VALUE_TYPE_FLOAT)) {
                    result->float_value = r;
                    result->type = MYN_VALUE_TYPE_FLOAT;
                } else if ((result->type == MYN_VALUE_TYPE_INT) || (factor.type == MYN_VALUE_TYPE_INT)) {
                    result->int_value = r;
                    result->type = MYN_VALUE_TYPE_INT;
                } else {
                    result->byte_value = r;
                    result->type = MYN_VALUE_TYPE_BYTE;
                }

                c = *env->pos;
                continue;
            }
        }
        ErrorMessage_set("Operation not possible.");
        *result = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_TYPE };
        return;
    }
}

void
BooleanFactor_process(MynEnvironment* env, MynValue* result) {
    uint8_t c = *env->pos;
    bool invert = false;
    if (c == TOK_NOT) {
        invert = true;
        env->pos++;
    };

    MathExpression_process(env, result);
    if (result->type == MYN_VALUE_TYPE_ERROR)
        return;

    c = *env->pos;
    MynValue factor = { .type = MYN_VALUE_TYPE_NONE };
    while ((factor.type != MYN_VALUE_TYPE_ERROR) && ((c == TOK_EQUAL) || (c == TOK_UNEQUAL) || (c == TOK_LESSTHAN) || (c == TOK_LESSTHANEQ) || (c == TOK_GREATERTHAN) || (c == TOK_GREATERTHAN)) && (env->pos - env->text < env->length)) {

        if ((result->type == MYN_VALUE_TYPE_INT) || (result->type == MYN_VALUE_TYPE_BYTE) || (result->type == MYN_VALUE_TYPE_FLOAT)) {
            env->pos++;
            MathExpression_process(env, &factor);
            if ((factor.type != MYN_VALUE_TYPE_INT) && (factor.type != MYN_VALUE_TYPE_BYTE) && (factor.type != MYN_VALUE_TYPE_FLOAT)) {
                ErrorMessage_set("Comparison not possible.");
                *result = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_TYPE };
                return;
            }
            double r = result->type == MYN_VALUE_TYPE_INT ? (double)result->int_value : (result->type == MYN_VALUE_TYPE_FLOAT ? (double)result->float_value : (double)result->byte_value);
            double f = factor.type == MYN_VALUE_TYPE_INT ? (double)factor.int_value : (factor.type == MYN_VALUE_TYPE_FLOAT ? (double)factor.float_value : (double)factor.byte_value);

            result->type = MYN_VALUE_TYPE_BOOL;
            if (c == TOK_EQUAL)
                result->bool_value = (r == f);
            if (c == TOK_UNEQUAL)
                result->bool_value = (r != f);
            if (c == TOK_LESSTHAN)
                result->bool_value = (r < f);
            if (c == TOK_LESSTHANEQ)
                result->bool_value = (r <= f);
            if (c == TOK_GREATERTHAN)
                result->bool_value = (r > f);
            if (c == TOK_GREATERTHANEQ)
                result->bool_value = (r >= f);
        } else if ((result->type == MYN_VALUE_TYPE_BOOL) && ((c == TOK_EQUAL) || (c == TOK_UNEQUAL))) {
            env->pos++;
            factor = Expression_process(env);
            if (factor.type != MYN_VALUE_TYPE_BOOL) {
                ErrorMessage_set("Comparison not possible.");
                *result = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_TYPE };
                return;
            }

            if (c == TOK_EQUAL)
                result->bool_value = (result->bool_value == factor.bool_value);
            if (c == TOK_UNEQUAL)
                result->bool_value = (result->bool_value != factor.bool_value);
        } else if ((result->type == MYN_VALUE_TYPE_TYPE) && ((c == TOK_EQUAL) || (c == TOK_UNEQUAL))) {
            env->pos++;
            factor = Expression_process(env);
            if (factor.type == MYN_VALUE_TYPE_ERROR) {
                *result = factor;
                return;
            }
            if (factor.type != MYN_VALUE_TYPE_TYPE) {
                ErrorMessage_set("Comparison not possible.");
                *result = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_TYPE };
                return;
            }
            result->type = MYN_VALUE_TYPE_BOOL;
            if (c == TOK_EQUAL)
                result->bool_value = (result->type_value == factor.type_value);
            if (c == TOK_UNEQUAL)
                result->bool_value = (result->type_value != factor.type_value);

        } else {
            ErrorMessage_set("Comparison not possible.");
            *result = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_TYPE };
            return;
        }
        c = *env->pos;
    }

    if ((invert) && (result->type != MYN_VALUE_TYPE_ERROR))
        if (result->type == MYN_VALUE_TYPE_BOOL)
            result->bool_value = !result->bool_value;
        else {
            ErrorMessage_set("Trying to logically invert a non-boolean value.");
            *result = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_TYPE };
            return;
        }
}

void
BooleanExpression_process(MynEnvironment* env, MynValue* result) {
    BooleanFactor_process(env, result);

    uint8_t c = *env->pos;
    MynValue factor = { .type = MYN_VALUE_TYPE_NONE };
    while ((factor.type != MYN_VALUE_TYPE_ERROR) && (c == TOK_OR || c == TOK_AND) && (env->pos - env->text < env->length)) {
        env->pos++;
        BooleanFactor_process(env, &factor);

        if ((result->type != MYN_VALUE_TYPE_BOOL) || (factor.type != MYN_VALUE_TYPE_BOOL)) {
            ErrorMessage_set("Cannot perform Boolean expression.");
            *result = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_TYPE };
        }

        if (factor.type != MYN_VALUE_TYPE_ERROR) {
            if (c == TOK_OR)
                result->bool_value = result->bool_value || factor.bool_value;
            else
                result->bool_value = result->bool_value && factor.bool_value;
        } else
            break;
        c = *env->pos;
    }
}

void
MathExpression_process(MynEnvironment* env, MynValue* result) {
    MathTerm_process(env, result);
    if (result->type == MYN_VALUE_TYPE_ERROR)
        return;

    char c = *env->pos;
    MynValue term = { .type = MYN_VALUE_TYPE_NONE };
    while ((term.type != MYN_VALUE_TYPE_ERROR) && (c == '+' || (c == '-')) && (env->pos - env->text < env->length)) {
        env->pos++;
        if ((result->type == MYN_VALUE_TYPE_INT) || (result->type == MYN_VALUE_TYPE_BYTE) || (result->type == MYN_VALUE_TYPE_FLOAT)) {
            term.type = MYN_VALUE_TYPE_NONE;
            MathTerm_process(env, &term);
            if (term.type == MYN_VALUE_TYPE_ERROR)
                return;
            if ((term.type == MYN_VALUE_TYPE_INT) || (term.type == MYN_VALUE_TYPE_BYTE) || (term.type == MYN_VALUE_TYPE_FLOAT)) {
                double r = result->type == MYN_VALUE_TYPE_INT ? (double)result->int_value : (result->type == MYN_VALUE_TYPE_FLOAT ? (double)result->float_value : (double)result->byte_value);
                double t = term.type == MYN_VALUE_TYPE_INT ? (double)term.int_value : (term.type == MYN_VALUE_TYPE_FLOAT ? (double)term.float_value : (double)term.byte_value);

                if (c == '+')
                    r += t;
                else
                    r -= t;

                if ((result->type == MYN_VALUE_TYPE_FLOAT) || (term.type == MYN_VALUE_TYPE_FLOAT)) {
                    result->float_value = r;
                    result->type = MYN_VALUE_TYPE_FLOAT;
                } else if ((result->type == MYN_VALUE_TYPE_INT) || (term.type == MYN_VALUE_TYPE_INT)) {
                    result->int_value = r;
                    result->type = MYN_VALUE_TYPE_INT;
                } else {
                    result->byte_value = r;
                    result->type = MYN_VALUE_TYPE_BYTE;
                }

                c = *env->pos;
                continue;
            }
        }
        ErrorMessage_set("Operation not possible.");
        *result = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_TYPE };
    }
}

void
StringExpression_process(MynEnvironment* env, MynValue* result) {
    if (result->type == MYN_VALUE_TYPE_NONE) {
        result->type = MYN_VALUE_TYPE_STR;
        uint8_t* p = env->pos;
        if (*p == TOK_STRING) {
            while (*++env->pos)
                ;
            MynValue_str_set(result, p + 1);
            env->pos++;
        } else if (*p == TOK_VARIABLE) {
            ValueAsBytes hash = { .byte[0] = env->pos[3], .byte[1] = env->pos[2], .byte[2] = env->pos[1], .byte[3] = env->pos[0] };
            env->pos += 4;
            int index = SymbolArray_lookup(&env->symbols, hash.integer);
            if (index != -1) {
                Symbol symbol = env->symbols.symbol[index];
                if (symbol.type == SYMBOL_TYPE_REFERENCE)
                    symbol = env->symbols.symbol[symbol.reference];

                if (symbol.value.type == MYN_VALUE_TYPE_CALLABLE)
                    *result = Call_execute(env, &symbol.value);
                else
                    *result = MynValue_duplicate(&symbol.value);
            } else {
                ErrorMessage_set("Variable not defined.");
                *result = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_TYPE };
                return;
            }

        } else {
            ErrorMessage_set("Could not process string");
            *result = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_SYNTAX };
        }
    }

    if (*env->pos == '+') {
        env->pos++;
        MynValue add_string = (MynValue) { .type = MYN_VALUE_TYPE_NONE };
        StringExpression_process(env, &add_string);
        if (add_string.type == MYN_VALUE_TYPE_ERROR) {
            // MynValue_free(&add_string);
            return;
        }
        auto r = MynValue_str_add(result, &add_string);
        MynValue_free(&add_string);
        if (r.type == MYN_VALUE_TYPE_ERROR)
            *result = r;
    }
}

MynValue
StrAdditionAssignment_process(MynEnvironment* env, int symbol_index) {
    MynValue add_string = Expression_process(env);
    if (add_string.type == MYN_VALUE_TYPE_ERROR)
        return add_string;
    if (add_string.type != MYN_VALUE_TYPE_STR)
        RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "Trying to add assingn a non-string to a string.");

    auto result = MynValue_str_add(&env->symbols.symbol[symbol_index].value, &add_string);
    MynValue_free(&add_string);
    return result;
}

MynValue
ArrayAdditionAssignment_process(MynEnvironment* env, int symbol_index) {
    MynValue add_array = Expression_process(env);
    if (add_array.type == MYN_VALUE_TYPE_ERROR)
        return add_array;

    if (add_array.length > 0) {
        auto result = MynValue_array_concatenate(&env->symbols.symbol[symbol_index].value, &add_array);
        MynValue_free(&add_array);
        return result;
    } else {
        MynValue rv = { .type = MYN_VALUE_TYPE_NONE };
        MathExpression_process(env, &rv);
        if (rv.type == MYN_VALUE_TYPE_ERROR)
            return rv;

        if (rv.type == MYN_VALUE_TYPE_BYTE) {
            if (!MynValue_bytearray_add(&env->symbols.symbol[symbol_index].value, rv.byte_value))
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append byte to array.");
        } else if (rv.type == MYN_VALUE_TYPE_INT) {
            if (!MynValue_intarray_add(&env->symbols.symbol[symbol_index].value, rv.int_value))
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append integer to array.");
        } else if (rv.type == MYN_VALUE_TYPE_FLOAT) {
            if (!MynValue_floatarray_add(&env->symbols.symbol[symbol_index].value, rv.float_value))
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append float to array.");
        } else
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append value to array.");
        return rv;
    }
}

MynValue
Array_process(MynEnvironment* env) {
    MynValue result = {};
    bool is_first = true;

    while (*env->pos != ']') {
        if (*env->pos == ',')
            env->pos++;

        MynValue rv = { .type = MYN_VALUE_TYPE_NONE };
        MathExpression_process(env, &rv);
        if (rv.type == MYN_VALUE_TYPE_ERROR)
            return rv;

        if (is_first) {
            result.type = rv.type;
            is_first = false;
        }

        if (rv.type == MYN_VALUE_TYPE_BYTE) {
            if (!MynValue_bytearray_add(&result, rv.byte_value))
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append byte to array.");
        } else if (rv.type == MYN_VALUE_TYPE_INT) {
            if (!MynValue_intarray_add(&result, rv.int_value))
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append integer to array.");
        } else if (rv.type == MYN_VALUE_TYPE_FLOAT) {
            if (!MynValue_floatarray_add(&result, rv.float_value))
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append float to array.");
        } else
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Cannot append value to array.");
    }
    env->pos++;
    return result;
}

MynValue
ArrayExpression_process(MynEnvironment* env, MynValue* value) {
    MynValue result = Array_process(env);
    if (result.type == MYN_VALUE_TYPE_ERROR) {
        return result;
    }
    while (*env->pos == '+') {
        if (*++env->pos == '[') {
            env->pos++;
            MynValue addend = Array_process(env);
            if (addend.type == MYN_VALUE_TYPE_ERROR) {
                MynValue_free(&result);
                return addend;
            }
            for (size_t i = 0; i < addend.length; i++) {
                if (!MynValue_intarray_add(&result, addend.int_array[i]))
                    RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Array extension failed.");
            }
        } else {
            MynValue_free(&result);
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Array extension failed.");
        }
    }
    return result;
}

MynValue // read the next expression and assign the value to the variable "key" (and the given element if it is an array)
Assignment_process(MynEnvironment* env, uint32_t key, int element) {
    MynValue expression = Expression_process(env);
    if (expression.type == MYN_VALUE_TYPE_ERROR)
        return expression;

    int symbol_index = SymbolArray_lookup(&env->symbols, key);
    if (symbol_index != -1) {
        Symbol symbol = env->symbols.symbol[symbol_index];

        if (symbol.level == env->level) {
            if (symbol.type == SYMBOL_TYPE_REFERENCE) {
                symbol_index = symbol.reference;
                symbol = env->symbols.symbol[symbol_index];
            }
            if (symbol.value.type != expression.type) {
                MynValue_free(&expression);
                RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "Wrong data type assigned.");
            } else {
                if (element == -1)
                    env->symbols.symbol[symbol_index].value = expression; // ToDo: free current value first
                else {
                    if (env->symbols.symbol[symbol_index].value.length <= element)
                        RETURN_ERROR(MYN_ERROR_TYPE_RANGE, "Element index out of range.");

                    if (env->symbols.symbol[symbol_index].value.type == MYN_VALUE_TYPE_BYTE)
                        env->symbols.symbol[symbol_index].value.byte_array[element] = expression.byte_value;
                    if (env->symbols.symbol[symbol_index].value.type == MYN_VALUE_TYPE_INT)
                        env->symbols.symbol[symbol_index].value.int_array[element] = expression.int_value;
                    if (env->symbols.symbol[symbol_index].value.type == MYN_VALUE_TYPE_FLOAT)
                        env->symbols.symbol[symbol_index].value.float_array[element] = expression.float_value;
                }
                return expression;
            }
        }
    }
    const Symbol symbol = { .key = key, .type = SYMBOL_TYPE_VARIABLE, .level = env->level, .value = expression };
    MynValue new_index = SymbolArray_append(&env->symbols, &symbol);
    if (new_index.type == MYN_VALUE_TYPE_ERROR)
        return new_index;
    return expression.size == 0 ? expression : (MynValue) { .type = MYN_VALUE_TYPE_NONE }; // allow chaining of assignments for simple values
}

MynValue
ModificationAssignment_process(MynEnvironment* env, int symbol_index, int element, bool addition) {

    Symbol symbol = env->symbols.symbol[symbol_index];
    if (symbol.type == SYMBOL_TYPE_REFERENCE) {
        symbol_index = symbol.reference;
        symbol = env->symbols.symbol[symbol_index];
    }

    if ((symbol.value.type == MYN_VALUE_TYPE_STR) && addition)
        return StrAdditionAssignment_process(env, symbol_index);

    if ((element != -1) && (env->symbols.symbol[symbol_index].value.length <= element))
        RETURN_ERROR(MYN_ERROR_TYPE_RANGE, "Element index out of range.");

    if ((symbol.value.length > 0) && (element == -1) && addition)
        return ArrayAdditionAssignment_process(env, symbol_index);

    if ((symbol.value.type != MYN_VALUE_TYPE_BYTE) && (symbol.value.type != MYN_VALUE_TYPE_INT) && (symbol.value.type != MYN_VALUE_TYPE_FLOAT))
        RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "Wrong data type for assignment.");

    MynValue expression = Expression_process(env);
    if (expression.type == MYN_VALUE_TYPE_ERROR)
        return expression;

    if (symbol.value.type != expression.type) {
        MynValue_free(&expression);
        RETURN_ERROR(MYN_ERROR_TYPE_TYPE, "Wrong data type assigned.");
    } else {
        if (addition) {
            if (symbol.value.type == MYN_VALUE_TYPE_BYTE)
                if (element == -1)
                    env->symbols.symbol[symbol_index].value.byte_value += expression.byte_value;
                else
                    env->symbols.symbol[symbol_index].value.byte_array[element] += expression.byte_value;
            else if (symbol.value.type == MYN_VALUE_TYPE_INT)
                if (element == -1)
                    env->symbols.symbol[symbol_index].value.int_value += expression.int_value;
                else
                    env->symbols.symbol[symbol_index].value.int_array[element] += expression.int_value;
            else if (symbol.value.type == MYN_VALUE_TYPE_FLOAT)
                env->symbols.symbol[symbol_index].value.float_value += expression.float_value;
        } else {
            if (symbol.value.type == MYN_VALUE_TYPE_BYTE)
                env->symbols.symbol[symbol_index].value.byte_value -= expression.byte_value;
            else if (symbol.value.type == MYN_VALUE_TYPE_INT)
                env->symbols.symbol[symbol_index].value.int_value -= expression.int_value;
            else if (symbol.value.type == MYN_VALUE_TYPE_FLOAT)
                env->symbols.symbol[symbol_index].value.float_value -= expression.float_value;
        }
    }
    return env->symbols.symbol[symbol_index].value;
}

MynValue
ReturnStatement_process(MynEnvironment* env) {
    MynValue result = Expression_process(env);
    env->halt = HALT_RETURN;
    return result;
}

MynValue
Call_execute(MynEnvironment* env, MynValue* callable_value) {
    uint8_t* subroutine_pos = env->text + callable_value->position;
    int symbol_stack_position = env->symbols.used;
    env->level++;

    // parse the arguments
    while (*subroutine_pos != ')') {
        if (*env->pos == ',') {
            env->pos++;
            subroutine_pos++;
        }

        if (*subroutine_pos == TOK_REFERENCE) {
            subroutine_pos++;
            if (*subroutine_pos != TOK_VARIABLE)
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected a variable to reference.");

            ValueAsBytes argument_hash = { .byte[0] = subroutine_pos[4], .byte[1] = subroutine_pos[3], .byte[2] = subroutine_pos[2], .byte[3] = subroutine_pos[1] };
            subroutine_pos += 5;

            if (*env->pos != TOK_VARIABLE)
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected a variable to be passed as reference.");
            ValueAsBytes variable_hash = { .byte[0] = env->pos[4], .byte[1] = env->pos[3], .byte[2] = env->pos[2], .byte[3] = env->pos[1] };
            env->pos += 5;
            int variable_index = SymbolArray_lookup(&env->symbols, variable_hash.integer);
            if (variable_index == -1)
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Could not find variable.");

            // if variable passed is a reference itself, reference to the referenced value
            Symbol variable = env->symbols.symbol[variable_index];
            if (variable.type == SYMBOL_TYPE_REFERENCE) {
                variable_index = variable.reference;
            }

            const Symbol symbol = { .key = argument_hash.integer, .type = SYMBOL_TYPE_REFERENCE, .level = env->level, .reference = variable_index };
            MynValue new_index = SymbolArray_append(&env->symbols, &symbol);
            if (new_index.type == MYN_VALUE_TYPE_ERROR)
                return new_index;

        } else if (*subroutine_pos == TOK_VARIABLE) {
            ValueAsBytes argument_hash = { .byte[0] = subroutine_pos[4], .byte[1] = subroutine_pos[3], .byte[2] = subroutine_pos[2], .byte[3] = subroutine_pos[1] };
            subroutine_pos += 5;

            MynValue expression = Assignment_process(env, argument_hash.integer, -1);
            if (expression.type == MYN_VALUE_TYPE_ERROR)
                return expression;
        } else
            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Expected a variable or reference as argument.");
    }

    // execute the function
    uint8_t* return_pos = env->pos + (callable_value->subtype == MYN_VALUE_TYPE_CALLABLE_PROCEDURE ? 0 : 1); // account for ')' added in procedure definition by lexer
    env->pos = subroutine_pos + 1;

    MynValue result = Block_execute(env);
    SymbolArray_trim(&env->symbols, symbol_stack_position);
    env->level--;
    env->halt = HALT_NONE;
    env->pos = return_pos;
    if (result.type == MYN_VALUE_TYPE_ERROR)
        return result;
    if (callable_value->subtype == MYN_VALUE_TYPE_CALLABLE_PROCEDURE) {
        MynValue_free(&result);
        RETURN_NONE();
    }
    return result;
}

void
Value_process(MynEnvironment* env, MynValue* value) {
    uint8_t c = *env->pos++;
    if (c == ')') {
        env->pos--;
        *value = (MynValue) { .type = MYN_VALUE_TYPE_NONE };
        return;
    }
    int sign = 1;
    if (c == '-') {
        sign = -1;
        c = *env->pos++;
    } else if (c == '+')
        c = *env->pos++;

    if (c == '(') {
        *value = (MynValue) { .type = MYN_VALUE_TYPE_NONE };
        MathExpression_process(env, value);
        if (*env->pos++ != ')') {
            ErrorMessage_set("Expected ')'.");
            *value = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_OTHER };
        }

    } else if ((c == TOK_BYTE_TYPE) || (c == TOK_INTEGER_TYPE) || (c == TOK_FLOAT_TYPE) || (c == TOK_STRING_TYPE) || (c == TOK_BOOL_TYPE) || (c == TOK_STREAM_TYPE) || (c == TOK_TYPE_TYPE)) {
        *value = (MynValue) { .type = MYN_VALUE_TYPE_TYPE, .type_value = c - 127 }; // translate TOK_X_TYPE to MYN_VALUE_TYPE_X
    } else if (c == TOK_TYPE) {
        *value = TypeFunction_process(env);
    } else if (c == TOK_VARIABLE) {
        ValueAsBytes hash = { .byte[0] = env->pos[3], .byte[1] = env->pos[2], .byte[2] = env->pos[1], .byte[3] = env->pos[0] };
        env->pos += 4;
        int index = SymbolArray_lookup(&env->symbols, hash.integer);
        if (index == -1) {
            if (*env->pos == TOK_ASSIGN) { // new symbol
                env->pos++;
                *value = Assignment_process(env, hash.integer, -1);
            } else {
                ErrorMessage_set("Variable not defined.");
                *value = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_TYPE };
                return;
            }

        } else {
            Symbol symbol = env->symbols.symbol[index];

            if (symbol.value.type == MYN_VALUE_TYPE_CALLABLE) {
                *value = Call_execute(env, &symbol.value);
            } else {
                // Check if it is an assignement
                int element_index = -1;

                if (*env->pos == '[') {
                    if (symbol.value.length == 0) {
                        ErrorMessage_set("Trying to index a scalar.");
                        *value = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_OTHER };
                        return;
                    }

                    env->pos++;
                    MynValue element_index_mv = { .type = MYN_VALUE_TYPE_NONE };
                    MathExpression_process(env, &element_index_mv);
                    if (element_index_mv.type == MYN_VALUE_TYPE_ERROR) {
                        *value = element_index_mv;
                        return;
                    }
                    if (element_index_mv.type != MYN_VALUE_TYPE_INT) {
                        ErrorMessage_set("Invalid data type for element index");
                        *value = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_OTHER };
                        return;
                    }

                    if (*env->pos++ != ']') {
                        ErrorMessage_set("Expected ']'");
                        *value = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_OTHER };
                        return;
                    }
                    element_index = element_index_mv.int_value;
                }

                if (*env->pos == TOK_ASSIGN) {
                    env->pos++;
                    *value = Assignment_process(env, hash.integer, element_index);
                } else if (*env->pos == TOK_PLUS_EQUAL) {
                    env->pos++;
                    *value = ModificationAssignment_process(env, index, element_index, true);
                } else if (*env->pos == TOK_MINUS_EQUAL) {
                    env->pos++;
                    *value = ModificationAssignment_process(env, index, element_index, false);
                } else if (element_index == -1) {
                    if (symbol.type == SYMBOL_TYPE_REFERENCE) {
                        index = symbol.reference;
                        symbol = env->symbols.symbol[index];
                    }
                    *value = MynValue_duplicate(&symbol.value);
                } else
                    *value = MynValue_array_get(&symbol.value, element_index);
            }
        }
    } else if (c == TOK_INTEGER) {
        ValueAsBytes val = { .byte[3] = env->pos[0], .byte[2] = env->pos[1], .byte[1] = env->pos[2], .byte[0] = env->pos[3] };
        *value = (MynValue) { .type = MYN_VALUE_TYPE_INT, .int_value = val.integer };
        env->pos += 4;
    } else if (c == TOK_FLOAT) {
        ValueAsBytes val = { .byte[3] = env->pos[0], .byte[2] = env->pos[1], .byte[1] = env->pos[2], .byte[0] = env->pos[3] };
        *value = (MynValue) { .type = MYN_VALUE_TYPE_FLOAT, .float_value = val.flt };
        env->pos += 4;
    } else if (c == TOK_BYTE) {
        *value = (MynValue) { .type = MYN_VALUE_TYPE_BYTE, .byte_value = env->pos[0] };
        env->pos += 1;
    } else if (c == TOK_TRUE) {
        *value = (MynValue) { .type = MYN_VALUE_TYPE_BOOL, .bool_value = true };
    } else if (c == TOK_FALSE) {
        *value = (MynValue) { .type = MYN_VALUE_TYPE_BOOL, .bool_value = false };
    } else if (c == TOK_LEN) {
        *value = LenFunction_process(env);
    } else if (c == TOK_STR) {
        *value = StrFunction_process(env);
    } else if (c == TOK_INT) {
        *value = IntFunction_process(env);
    } else if (c == TOK_FLT) {
        *value = FloatFunction_process(env);
    } else if (c == TOK_IDENTICAL) {
        *value = IdenticalFunction_process(env);
    } else if (c == TOK_RANDOM) {
        *value = RandomFunction_process(env);
    } else if (c == TOK_PEEK) {
        *value = PeekFunction_process(env);
    } else if (c == TOK_FILE_OPEN) {
        *value = FileOpenFunction_process(env);
    } else if (c == TOK_FILE_EXISTS) {
        *value = FileExistsFunction_process(env);
    } else if (c == TOK_STREAM_READ_BYTE) {
        *value = StreamReadByteFunction_process(env);
    } else if (c == TOK_STREAM_READ_STRING) {
        *value = StreamReadStringFunction_process(env);
    } else {
        ErrorMessage_set("Could not process token '%X' at position %d.", c, --env->pos - env->text);
        *value = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_SYNTAX };
    }

    if ((sign == -1) && (value->type != MYN_VALUE_TYPE_ERROR)) {
        if (value->type == MYN_VALUE_TYPE_INT)
            value->int_value *= sign;
        else if (value->type == MYN_VALUE_TYPE_FLOAT)
            value->float_value *= sign;
        else {
            ErrorMessage_set("Could not invert value.");
            *value = (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_SYNTAX };
        }
    }
}

MynValue // Passes ownership of return value!
Expression_process(MynEnvironment* env) {
    uint8_t c = *env->pos++;
    MynValue value = { .type = MYN_VALUE_TYPE_NONE };

    //  ToDo: refactor to implement array and string comparison
    if (c == '[')
        return ArrayExpression_process(env, nullptr);
    if (c == TOK_STRING) {
        env->pos--;
        StringExpression_process(env, &value);
        return value;
    }

    if (c == TOK_PRINT)
        return PrintStatement_process(env);
    if (c == TOK_FILE_CLOSE)
        return FileCloseStatement_process(env);
    if (c == TOK_FILE_COPY)
        return FileCopyStatement_process(env);
    if (c == TOK_FILE_DELETE)
        return FileDeleteStatement_process(env);
    if (c == TOK_FILE_RENAME)
        return FileRenameStatement_process(env);
    if (c == TOK_POKE)
        return PokeStatement_process(env);
    if ((c == TOK_FUNCTION) || (c == TOK_PROCEDURE)) {
        // assign a value to the function/procedure symbol
        ValueAsBytes hash = { .byte[0] = env->pos[3], .byte[1] = env->pos[2], .byte[2] = env->pos[1], .byte[3] = env->pos[0] };
        env->pos += 4;
        MynValue value = { .type = MYN_VALUE_TYPE_CALLABLE, .subtype = c == TOK_FUNCTION ? MYN_VALUE_TYPE_CALLABLE_FUNCTION : MYN_VALUE_TYPE_CALLABLE_PROCEDURE, .position = env->pos - env->text };
        const Symbol symbol = { .key = hash.integer, .type = SYMBOL_TYPE_VARIABLE, .level = env->level, .value = value };
        MynValue new_index = SymbolArray_append(&env->symbols, &symbol);
        return Block_skip(env);
    }

    if (c == TOK_RETURN)
        return ReturnStatement_process(env);
    if (c == TOK_BREAK)
        return BreakStatement_process(env);
    if (c == TOK_CONTINUE)
        return ContinueStatement_process(env);
    if (c == TOK_IF)
        return IfStatement_process(env);
    if (c == TOK_WHILE)
        return WhileStatement_process(env);
    if (c == TOK_FOR)
        return ForStatement_process(env);
    if (c == TOK_DO)
        return DoStatement_process(env);
    if (c == '[')
        return ArrayExpression_process(env, nullptr);
    if (c == TOK_STREAM_WRITE_BYTE)
        return StreamWriteByteStatement_process(env);
    if (c == TOK_STREAM_WRITE_STRING)
        return StreamWriteStringStatement_process(env);
    if (c == TOK_STREAM_POS_MOVE)
        return StreamPositionMoveStatement_process(env);

    if (c == TOK_NONE)
        return (MynValue) { .type = MYN_VALUE_TYPE_NONE };

    if (c == TOK_NOT) {
        env->pos--;
        BooleanExpression_process(env, &value);
        return value;
    }

    env->pos--;
    Value_process(env, &value);
    if ((value.type == MYN_VALUE_TYPE_ERROR) || (value.type == MYN_VALUE_TYPE_NONE))
        return value;

    if (value.length > 0) {
        return value;
    }
    BooleanExpression_process(env, &value);
    return value;
}

MynValue
Block_skip(MynEnvironment* env) { // skip to the position after the TOK_BLOCK_END of the current block
    int level = 1;
    uint8_t c;
    while ((level > 0) && (env->pos < env->text + env->length)) {
        c = *env->pos++;
        if ((c == TOK_VARIABLE) || (c == TOK_INTEGER) || (c == TOK_FLOAT))
            env->pos += 4;

        if (c == TOK_BYTE)
            env->pos++;

        if (c == TOK_STRING)
            while (*env->pos++)
                ;

        if ((c == TOK_IF) || (c == TOK_ELSE) || (c == TOK_ELIF) || (c == TOK_DO) || (c == TOK_WHILE) || (c == TOK_FOR))
            level++;
        if ((c == TOK_FUNCTION) || (c == TOK_PROCEDURE)) {
            level++;
            env->pos += 4;
        }

        if (c == TOK_BLOCK_END)
            level--;
    }
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

MynValue
Block_execute(MynEnvironment* env) {
    MynValue result = { .type = MYN_VALUE_TYPE_NONE };

    while (env->pos < env->text + env->length && result.type != MYN_VALUE_TYPE_ERROR && *env->pos != TOK_BLOCK_END) {
        if (env->halt == HALT_NONE) {
            MynValue_free(&result); // result from previous expression
            result = Expression_process(env);
        } else {
            Block_skip(env);
            env->pos--; // Move pointer to TOK_BLOCK_END
        }
    }
    if (*env->pos == TOK_BLOCK_END)
        env->pos++;

    env->level--;
    return result;
}

MynValue
Code_interpret(MynValue* bytecode, MynEnvironment* env) {
    env->text = bytecode->string, env->pos = bytecode->string, env->length = bytecode->length;
    return Block_execute(env);
}

MynEnvironment*
Myn_initialize(void) {
    MynEnvironment* env = (MynEnvironment*)calloc(1, sizeof(MynEnvironment));
    *env = (MynEnvironment) { .halt = HALT_NONE };
    SymbolArray_init(&env->symbols, SYMBOL_ARRAY_INITIAL_SIZE);

    MynValue value = { .type = MYN_VALUE_TYPE_STREAM, .subtype = MYN_VALUE_TYPE_STREAM_FILE, .file_stream = stdin };
    Symbol symbol = { .key = 0x4D44B3FD, .type = SYMBOL_TYPE_VARIABLE, .level = env->level, .value = value }; // STDIN
    MynValue new_index = SymbolArray_append(&env->symbols, &symbol);

    value.file_stream = stdout;
    symbol = (Symbol) { .key = 0xD92AC866, .type = SYMBOL_TYPE_VARIABLE, .level = env->level, .value = value }; // STDOUT
    new_index = SymbolArray_append(&env->symbols, &symbol);

    return env;
}

MynValue
Myn_finalize(MynEnvironment* env) {
    SymbolArray_trim(&env->symbols, 0);
    free(env);
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE };
}

MynValue
MynScript_evaluate(const uint8_t* script_code, MynEnvironment* environment) {
    MynEnvironment* env = environment;
    if (environment == nullptr)
        env = Myn_initialize();

    MynValue lex_result = Script_lex(script_code);
    if (lex_result.type == MYN_VALUE_TYPE_ERROR)
        return lex_result;

    MynValue interpreter_result = Code_interpret(&lex_result, env);
    MynValue_free(&lex_result);
    if (environment == nullptr)
        Myn_finalize(env);

    return interpreter_result;
}

MynValue
MynRun(const char* file_name) {
    size_t length;
    uint8_t* script_code = (uint8_t*)File_load(file_name, &length);

    if (script_code) {
        MynValue result;
        if (strcmp(strrchr(file_name, '.'), ".mync") == 0) {
            if ((script_code[0] != MYN_MAGIC_NO_1) || (script_code[1] != MYN_MAGIC_NO_2) || ((script_code[2] & 0x3F) > MYN_VERSION) || ((script_code[2] & 0xC0) != 0)) {
                free(script_code);
                ErrorMessage_set("Not a Myn version %d bytecode file!", MYN_VERSION);
                return (MynValue) { .type = MYN_VALUE_TYPE_ERROR };
            }
            MynEnvironment* environment = Myn_initialize();
            result = Code_interpret(&(MynValue) { .type = MYN_VALUE_TYPE_STR, .string = script_code + 3, .length = length - 3 }, environment);
            Myn_finalize(environment);
        } else
            result = MynScript_evaluate(script_code, nullptr);
        free(script_code);
        return result;
    } else {
        ErrorMessage_set("Could not find script file '%s'.", file_name);
        return (MynValue) { .type = MYN_VALUE_TYPE_ERROR };
    };
}
