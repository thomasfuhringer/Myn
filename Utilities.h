// Utility library, Thomas Führinger, 2025

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef DEBUG
#    define DBG_(fmt, ...) fprintf(stderr, "--*> %s:%d: %s: " fmt "%c", __FILE__, __LINE__, __func__, __VA_ARGS__)
#    define DBG(...) DBG_(__VA_ARGS__, '\n')
#    define ASSERT(expr) \
        ((expr) ? (void)0 : (void)(fprintf(stderr, "%s:%d: %s: Assertion '%s' failed.\n", __FILE__, __LINE__, __FUNCTION__, #expr), abort()))
#else
#    define DBG(...)
#    define ASSERT(...) ((void)0)
#endif

#define TRY(expr) ({ MynValue r = expr; if (r.type == MYN_VALUE_TYPE_ERROR) return r; })
#define RETURN_ERROR(errtype, message, ...) ({ ErrorMessage_set( message __VA_OPT__(,)__VA_ARGS__); return (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = errtype }; })
#define RETURN_NONE() \
    return (MynValue) { .type = MYN_VALUE_TYPE_NONE }

typedef union {
    uint32_t integer;
    float flt;
    char byte[4];
} ValueAsBytes;

typedef struct { // Points to a span of memory.
    const uint8_t* position;
    size_t length;
} Span;

char* File_load(const char* file_name, size_t* length);
bool String_save(const char* string, const char* file_name);
bool Span_save(Span span, const char* file_name);
int Span_compare_span(const Span* span, const Span* to);
int Span_compare_str(const Span* span, const char* to);
void Span_print(const Span* span);
uint32_t Span_hash32(const Span* span);
void ErrorMessage_set(const char* text, ...);
