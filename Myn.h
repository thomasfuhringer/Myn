// Myn scripting language, Thomas Führinger, 2025
// Version 0.9

#pragma once
#define DEBUG
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define MYN_TABWITH 4
#define MYN_MAGIC_NO_1 6
#define MYN_MAGIC_NO_2 14
#define MYN_VERSION 1

#define MYN_VALUE_TYPE_NONE 0
#define MYN_VALUE_TYPE_BOOL 1
#define MYN_VALUE_TYPE_BYTE 2
#define MYN_VALUE_TYPE_INT 3
#define MYN_VALUE_TYPE_FLOAT 4
#define MYN_VALUE_TYPE_COMPLEX 5
#define MYN_VALUE_TYPE_TIME 6
#define MYN_VALUE_TYPE_STR 7
#define MYN_VALUE_TYPE_CALLABLE 8
#define MYN_VALUE_TYPE_STREAM 9
#define MYN_VALUE_TYPE_DICTIONARY 10
#define MYN_VALUE_TYPE_OBJECT 11
#define MYN_VALUE_TYPE_TYPE 12
#define MYN_VALUE_TYPE_OTHER 13
#define MYN_VALUE_TYPE_ERROR 255

#define MYN_VALUE_TYPE_CALLABLE_PROCEDURE 0
#define MYN_VALUE_TYPE_CALLABLE_FUNCTION 1
#define MYN_VALUE_TYPE_STREAM_FILE 0
#define MYN_VALUE_TYPE_OTHER_EOF 0

#define MYN_VALUE_ARRAY_INITIAL_COUNT 12
constexpr size_t MYN_VALUE_SHORT_STRING = sizeof(char*) - 1;

#define MYN_ERROR_TYPE_OTHER 0
#define MYN_ERROR_TYPE_SYNTAX 3
#define MYN_ERROR_TYPE_TYPE 4
#define MYN_ERROR_TYPE_INDENT 5
#define MYN_ERROR_TYPE_RANGE 6
#define MYN_ERROR_TYPE_MEMORY 7
#define MYN_ERROR_TYPE_VALUE 8
#define MYN_ERROR_MESSAGE_MAX 127

typedef struct {
    uint8_t type; // MYN_VALUE_TYPE
    uint8_t subtype;

    union {
        uint16_t length; // if > 0 it is an array
        uint16_t error_type;
    };

    uint32_t size; // buffer size of allocated memory

    union {
        int32_t int_value;
        uint8_t byte_value;
        float float_value;
        int32_t* int_array;
        float* float_array;
        uint8_t* byte_array;
        uint8_t* string;
        uint8_t short_string[MYN_VALUE_SHORT_STRING + 1];
        bool bool_value;
        int64_t time_value; // miliseconds since 1970-01-01
        uint32_t position;  // for callable
        uint16_t type_value;
        FILE* file_stream;
    };
} MynValue;

typedef struct MynEnvironment MynEnvironment;

MynValue MynRun(const char* file_name);
MynEnvironment* Myn_initialize(void);
MynValue MynEnvironment_add_symbol(MynEnvironment* environment, const char*, MynValue value);
MynValue Myn_finalize(MynEnvironment* environment);
MynValue MynScript_evaluate(const uint8_t* code, MynEnvironment*);
void MynValue_free(MynValue* value);
MynValue MynFile_lex(const char* file_name);

extern char myn_error_message[MYN_ERROR_MESSAGE_MAX];
