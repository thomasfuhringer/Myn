// Utilities, Thomas Führinger, 2025-04-04

#include "Utilities.h"
#include "Myn.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char myn_error_message[MYN_ERROR_MESSAGE_MAX] = {};

void
ErrorMessage_set(const char* restrict text, ...) {
    va_list args;
    va_start(args, text);
    snprintf(myn_error_message, MYN_ERROR_MESSAGE_MAX, text, args);
    va_end(args);
}

int
Span_compare_span(const Span* span, const Span* to) {
    if (span->length != to->length)
        return span->length - to->length;
    return memcmp(span->position, to->position, span->length);
}

int
Span_compare_str(const Span* span, const char* to) {
    if (span->length == 0)
        if (*to == '\0')
            return 0;
        else
            return 1;

    const char* sp = span->position;
    while ((sp < span->position + span->length) && *to) {
        if (*sp != *to)
            break;
        sp++;
        to++;
    }
    if ((sp == span->position + span->length) && (*to == '\0'))
        return 0;
    else
        return *(unsigned char*)sp - *(unsigned char*)to;
}

#define FNV_OFFSET_32 2166136261
#define FNV_PRIME_32 16777619

uint32_t
Span_hash32(const Span* span) {
    if (span->length == 0)
        return 0;

    uint32_t hash = FNV_OFFSET_32;
    for (const char* p = span->position; p < span->position + span->length; p++) {
        hash ^= (uint32_t)(unsigned char)(*p);
        hash *= FNV_PRIME_32;
    }
    return hash;
}

void // for debugging only
Span_print(const Span* span) {
    char buffer[] = "-----------------";
    memcpy(buffer, span->position, span->length);
    printf("StrSpan: %s\n", buffer);
}

char*
File_load(const char* file_name, size_t* length) {
    FILE* file_stream = fopen(file_name, "rb");
    char* data = nullptr;

    if (file_stream) {
        fseek(file_stream, 0, SEEK_END);
        *length = ftell(file_stream);
        fseek(file_stream, 0, SEEK_SET);
        data = malloc(*length + 1);
        if (data) {
            fread(data, 1, *length, file_stream);
            data[*length] = '\0';
        }
        fclose(file_stream);
        return data;
    }
    return nullptr;
}

bool
String_save(const char* string, const char* file_name) {
    FILE* file_stream = fopen(file_name, "w");
    if (file_stream != nullptr) {
        fputs(string, file_stream);
        fclose(file_stream);
        return true;
    }
    return false;
}

// Return 32-bit FNV-1a hash for key (see https://en.wikipedia.org/wiki/Fowler–Noll–Vo_hash_function)
#define FNV_OFFSET_64 14695981039346656037UL
#define FNV_PRIME_64 1099511628211UL

static uint64_t
hash_key(const char* key) {
    uint64_t hash = FNV_OFFSET_64;
    for (const char* p = key; *p; p++) {
        hash ^= (uint64_t)(unsigned char)(*p);
        hash *= FNV_PRIME_64;
    }
    return hash;
}
