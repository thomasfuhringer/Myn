// Lexer for Myn Programming Language, Thomas Führinger, 2025

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
#define SYMBOL_ARRAY_INITIAL_SIZE 30
#define SYMBOL_TYPE_FUNCTION 0
#define SYMBOL_TYPE_PROCEDURE 1

#define EXPECT(c, script) (if (!(*script.position++ == c)) \
        RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '%c' in line %d.", c, script.line);)

extern char myn_error_message[];

static const char g_special_char[] = { '#', '\\', '\'', 'n', 'r', 't', 'e', '0' };
static const char g_special_char_id[] = { 35, 34, 39, 10, 13, 9, 27, 0 };

typedef struct {
    uint32_t hash;
    uint32_t index;
    uint8_t type;
} Symbol;

typedef struct {
    const uint8_t* text;
    const uint8_t* position;
    int line;
    int indent;
} Script;

typedef struct {
    uint8_t* text;
    size_t size;
    size_t position;
} Code;

void
Code_add(Code* code, uint8_t byte) {
    if (code->position == code->size) {
        code->size *= 2;
        code->text = realloc(code->text, code->size);
    }
    code->text[code->position++] = byte;
}

void
Code_add_identifier(Code* code, const Span* identifier) {
    ValueAsBytes hash = { .integer = Span_hash32(identifier) };
    Code_add(code, hash.byte[3]); // big endian
    Code_add(code, hash.byte[2]);
    Code_add(code, hash.byte[1]);
    Code_add(code, hash.byte[0]);
}

void
Code_add_integer(Code* code, const int32_t value) {
    ValueAsBytes val = { .integer = value };
    Code_add(code, val.byte[3]);
    Code_add(code, val.byte[2]);
    Code_add(code, val.byte[1]);
    Code_add(code, val.byte[0]);
}

void
Code_add_float(Code* code, const float value) {
    ValueAsBytes val = { .flt = value };
    Code_add(code, val.byte[3]);
    Code_add(code, val.byte[2]);
    Code_add(code, val.byte[1]);
    Code_add(code, val.byte[0]);
}

static bool
Char_is_digit(char c) {
    return (c >= '0' && c <= '9');
}

static bool
Char_is_alpha(char c) {
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'));
}

static bool
Char_is_alphanumeric(char c) {
    return (Char_is_digit(c) || Char_is_alpha(c) || c == '_');
}

char
Char_translate(char c) {
    char translated = '\0';
    for (size_t i = 0; i <= sizeof(g_special_char); i++) {
        if (c == g_special_char[i])
            translated = g_special_char_id[i];
    }
    return translated;
}

void // Moves position indicator to next non-whitespace character.
Script_skip_whitespace(Script* script) {
    char c = *script->position;
    while ((c == ' ') || (c == '\t') || (c == '\r') || (c == ':') || (c == ';') || (c == '#')) {
        if (c == '#') // and ignores line comments
            while (c != '\n' && c != '\0') {
                c = *(++script->position);
            }
        else
            script->position++;
        c = *script->position;
    }
}

Span // Peek at alpha-numeric string (identifier).
Script_next_identifier(Script* script) {
    Span span = { .position = script->position, span.length = 0 };
    const uint8_t* p = script->position;
    while (Char_is_alphanumeric(*p++))
        span.length++;
    return span;
}

MynValue
Script_lex(const uint8_t* script_text) {
    Script script = { .text = script_text, .position = script_text, .line = 1 };
    auto len = strlen((char*)script_text) / 2; // initial size
    Code code = { .text = malloc(len), .size = len };
    bool do_block_is_open = false;
    ValueAsBytes hash;
    bool procedure_def = false;

    MynValue result = { .type = MYN_VALUE_TYPE_NONE };
    while (*script.position != '\0' && result.type != MYN_VALUE_TYPE_ERROR) {
        Script_skip_whitespace(&script);
        char c = *script.position;

        if (c == '\n') {
            if (procedure_def) {
                Code_add(&code, ')');
                procedure_def = false;
            }

            c = *(++script.position);
            script.line++;
            int indent_chars = 0;
            int indent_level = 0;
            while ((c == ' ') || (c == '\t')) {
                int indent_increment = 1 * (c == ' ') + MYN_TABWITH * (c == '\t');
                indent_chars += indent_increment;
                indent_level = indent_chars / MYN_TABWITH;
                c = *(++script.position);
            }

            if (c == '\n') // blank line
                continue;
            if (indent_level > script.indent)
                RETURN_ERROR(MYN_ERROR_TYPE_INDENT, "Indentation too big, line %d", script.line);
            if (indent_level < script.indent) {
                while (indent_level < script.indent) {
                    Code_add(&code, TOK_BLOCK_END);
                    script.indent--;
                }
            }

        } else if ((c == '<') && (script.position[1] == '=')) {
            Code_add(&code, TOK_LESSTHANEQ);
            script.position += 2;
        } else if ((c == '>') && (script.position[1] == '=')) {
            Code_add(&code, TOK_GREATERTHANEQ);
            script.position += 2;
        } else if ((c == '=') && (script.position[1] == '=')) {
            Code_add(&code, TOK_EQUAL);
            script.position += 2;
        } else if ((c == '!') && (script.position[1] == '=')) {
            Code_add(&code, TOK_UNEQUAL);
            script.position += 2;
        } else if ((c == '+') && (script.position[1] == '=')) {
            Code_add(&code, TOK_PLUS_EQUAL);
            script.position += 2;
        } else if ((c == '-') && (script.position[1] == '=')) {
            Code_add(&code, TOK_MINUS_EQUAL);
            script.position += 2;
        } else if ((c == '*') && (script.position[1] == '*')) {
            Code_add(&code, TOK_POWER_OF);
            script.position += 2;
        } else if (c == '=') {
            Code_add(&code, TOK_ASSIGN);
            script.position++;
        } else if (c == '&') {
            Code_add(&code, TOK_REFERENCE);
            script.position++;
        } else if ((c == '<') || (c == '>') || (c == ',') || (c == '(') || (c == ')') || (c == '[') || (c == ']') || (c == '+') || (c == '-') || (c == '*') || (c == '/') || (c == '%') || (c == '^') || (c == '<') || (c == '>')) {
            Code_add(&code, c);
            script.position++;
        } else if (Char_is_digit(c)) {
            int32_t integer = 0;
            float decimal = 0;
            while (Char_is_digit(c) || (c == '.') || (c == '\'')) {
                if (c == '.') {
                    decimal = 1;
                } else if (c != '\'') {
                    integer = 10 * integer + c - '0';
                    decimal *= 10;
                }
                c = *(++script.position);
            }
            if (decimal) {
                Code_add(&code, TOK_FLOAT);
                float flt = (float)integer / decimal;
                Code_add_float(&code, flt);
            } else {
                Code_add(&code, TOK_INTEGER);
                Code_add_integer(&code, integer);
            }
        } else if (c == '\'') {
            Code_add(&code, TOK_BYTE);
            c = *(++script.position);

            if (c == '\\') {
                c = *(++script.position);
                script.position++;
                if (c == 'x') {
                    char hex[3] = { script.position[0], script.position[1], '\0' };
                    c = strtol(hex, nullptr, 16);
                    script.position += 2;
                } else {
                    if (c == '0')
                        c = '\0';
                    else {
                        c = Char_translate(c);
                        if (c == '\0') {
                            RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Could not process escape character.");
                        }
                    }
                }
            } else
                script.position++;
            Code_add(&code, c);
            if (*(script.position++) != '\'')
                RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Could not process byte assignment.");

        } else if (c == '"') {
            Code_add(&code, TOK_STRING);
            while ((c = *(++script.position)) != '"') {
                if ((c == '\n') || (c == '\0'))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "String not terminated");

                char translated = c;
                if (c == '\\') {
                    translated = Char_translate(*(++script.position));
                    if (translated == '\0') {
                        RETURN_ERROR(MYN_ERROR_TYPE_OTHER, "Could not process escape character.");
                    }
                }
                Code_add(&code, translated);
            }
            script.position++;
            Code_add(&code, '\0'); // string terminator

        } else if (Char_is_alphanumeric(c)) {
            Span key = Script_next_identifier(&script);
            if ((Span_compare_str(&key, "print") == 0) || (Span_compare_str(&key, "Print") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_PRINT);
            } else if ((Span_compare_str(&key, "None") == 0) || (Span_compare_str(&key, "NONE") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_NONE);
            } else if ((Span_compare_str(&key, "True") == 0) || (Span_compare_str(&key, "TRUE") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_TRUE);
            } else if ((Span_compare_str(&key, "False") == 0) || (Span_compare_str(&key, "FALSE") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_FALSE);
            } else if ((Span_compare_str(&key, "and") == 0) || (Span_compare_str(&key, "AND") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_AND);
            } else if ((Span_compare_str(&key, "or") == 0) || (Span_compare_str(&key, "OR") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_OR);
            } else if ((Span_compare_str(&key, "not") == 0) || (Span_compare_str(&key, "NOT") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_NOT);
            } else if ((Span_compare_str(&key, "if") == 0) || (Span_compare_str(&key, "IF") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_IF);
                Script_skip_whitespace(&script);
                script.indent++;
            } else if ((Span_compare_str(&key, "else") == 0) || (Span_compare_str(&key, "ELSE") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_ELSE);
                Script_skip_whitespace(&script);
                script.indent++;
            } else if ((Span_compare_str(&key, "elif") == 0) || (Span_compare_str(&key, "ELIF") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_ELIF);
                Script_skip_whitespace(&script);
                script.indent++;
            } else if ((Span_compare_str(&key, "while") == 0) || (Span_compare_str(&key, "WHILE") == 0)) {
                script.position += key.length;
                if (!do_block_is_open) {
                    Code_add(&code, TOK_WHILE);
                    Script_skip_whitespace(&script);
                    script.indent++;
                } else {
                    Code_add(&code, TOK_DO_WHILE);
                    do_block_is_open = false;
                }
            } else if ((Span_compare_str(&key, "do") == 0) || (Span_compare_str(&key, "DO") == 0)) {
                if (do_block_is_open)
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Do statement repeated inline %d.", script.line);
                script.position += key.length;
                Code_add(&code, TOK_DO);
                Script_skip_whitespace(&script);
                script.indent++;
                do_block_is_open = true;
            } else if ((Span_compare_str(&key, "for") == 0) || (Span_compare_str(&key, "FOR") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_FOR);
                Script_skip_whitespace(&script);
                script.indent++;
            } else if ((Span_compare_str(&key, "to") == 0) || (Span_compare_str(&key, "TO") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_TO);
            } else if ((Span_compare_str(&key, "break") == 0) || (Span_compare_str(&key, "BREAK") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_BREAK);
            } else if ((Span_compare_str(&key, "continue") == 0) || (Span_compare_str(&key, "CONTINUE") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_CONTINUE);
            } else if ((Span_compare_str(&key, "return") == 0) || (Span_compare_str(&key, "RETURN") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_RETURN);
                Script_skip_whitespace(&script);
                if ((*script.position == '\n') || (*script.position == '\0'))
                    Code_add(&code, TOK_NONE);
            } else if ((Span_compare_str(&key, "type") == 0) || (Span_compare_str(&key, "Type") == 0) || (Span_compare_str(&key, "TYPE") == 0)) {
                script.position += key.length;
                Script_skip_whitespace(&script);
                if ((*script.position == '(')) {
                    Code_add(&code, (char)TOK_TYPE);
                    script.position++;
                } else
                    Code_add(&code, (char)TOK_TYPE_TYPE);
            } else if ((Span_compare_str(&key, "byte") == 0) || (Span_compare_str(&key, "Byte") == 0) || (Span_compare_str(&key, "BYTE") == 0)) {
                script.position += key.length;
                Script_skip_whitespace(&script);
                if ((*script.position == '(')) {
                    Code_add(&code, (char)TOK_BYT);
                    script.position++;
                } else
                    Code_add(&code, (char)TOK_BYTE_TYPE);
            } else if ((Span_compare_str(&key, "bool") == 0) || (Span_compare_str(&key, "BOOL") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_BOOL_TYPE);
            } else if ((Span_compare_str(&key, "integer") == 0) || (Span_compare_str(&key, "INTEGER") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_INTEGER_TYPE);
            } else if ((Span_compare_str(&key, "stream") == 0) || (Span_compare_str(&key, "STREAM") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_STREAM_TYPE);
            } else if ((Span_compare_str(&key, "float") == 0) || (Span_compare_str(&key, "FLOAT") == 0)) {
                script.position += key.length;
                Script_skip_whitespace(&script);
                if ((*script.position == '(')) {
                    Code_add(&code, (char)TOK_FLT);
                    script.position++;
                } else
                    Code_add(&code, (char)TOK_FLOAT_TYPE);
            } else if ((Span_compare_str(&key, "string") == 0) || (Span_compare_str(&key, "STRING") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_STRING_TYPE);
            } else if ((Span_compare_str(&key, "len") == 0) || (Span_compare_str(&key, "Len") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_LEN);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "str") == 0) || (Span_compare_str(&key, "Str") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_STR);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "int") == 0) || (Span_compare_str(&key, "Int") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_INT);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "identical") == 0) || (Span_compare_str(&key, "Identical") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_IDENTICAL);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "random") == 0) || (Span_compare_str(&key, "Random") == 0)) {
                script.position += key.length;
                Code_add(&code, TOK_RANDOM);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "peek") == 0) || (Span_compare_str(&key, "Peek") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_PEEK);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "poke") == 0) || (Span_compare_str(&key, "Poke") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_POKE);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "file_open") == 0) || (Span_compare_str(&key, "FileOpen") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_FILE_OPEN);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "file_close") == 0) || (Span_compare_str(&key, "FileClose") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_FILE_CLOSE);
            } else if ((Span_compare_str(&key, "file_copy") == 0) || (Span_compare_str(&key, "FileCopy") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_FILE_COPY);
            } else if ((Span_compare_str(&key, "file_delete") == 0) || (Span_compare_str(&key, "FileDelete") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_FILE_DELETE);
            } else if ((Span_compare_str(&key, "file_rename") == 0) || (Span_compare_str(&key, "FileRename") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_FILE_RENAME);
            } else if ((Span_compare_str(&key, "stream_read_byte") == 0) || (Span_compare_str(&key, "StreamReadByte") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_STREAM_READ_BYTE);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "stream_read_string") == 0) || (Span_compare_str(&key, "StreamReadString") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_STREAM_READ_STRING);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "file_exists") == 0) || (Span_compare_str(&key, "FileExists") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_FILE_EXISTS);
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
            } else if ((Span_compare_str(&key, "stream_write_byte") == 0) || (Span_compare_str(&key, "StreamWriteByte") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_STREAM_WRITE_BYTE);
            } else if ((Span_compare_str(&key, "stream_write_string") == 0) || (Span_compare_str(&key, "StreamWriteString") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_STREAM_WRITE_STRING);
            } else if ((Span_compare_str(&key, "stream_position_move") == 0) || (Span_compare_str(&key, "StreamPositionMove") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_STREAM_POS_MOVE);
            } else if ((Span_compare_str(&key, "fn") == 0) || (Span_compare_str(&key, "FN") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_FUNCTION);
                Script_skip_whitespace(&script);
                key = Script_next_identifier(&script);
                Code_add_identifier(&code, &key);
                script.position += key.length;
                Script_skip_whitespace(&script);
                if (!(*script.position++ == '('))
                    RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Expected character '(' in line %d.", script.line);
                script.indent++;
            } else if ((Span_compare_str(&key, "pr") == 0) || (Span_compare_str(&key, "PR") == 0)) {
                script.position += key.length;
                Code_add(&code, (char)TOK_PROCEDURE);
                Script_skip_whitespace(&script);
                key = Script_next_identifier(&script);
                Code_add_identifier(&code, &key);
                script.position += key.length;
                script.indent++;
                procedure_def = true;
            } else {
                script.position += key.length;
                Code_add(&code, TOK_VARIABLE);
                Code_add_identifier(&code, &key);
                if (*script.position == '(') // function call
                    script.position++;
            }
        } else
            RETURN_ERROR(MYN_ERROR_TYPE_SYNTAX, "Could not process character '%c'.", c);
    }

    return (MynValue) { .type = MYN_VALUE_TYPE_STR, .string = code.text, .length = code.position };
}

MynValue
MynFile_lex(const char* file_name) {
    size_t length;
    uint8_t* script_code = (uint8_t*)File_load(file_name, &length);

    if (script_code != nullptr) {
        MynValue result = Script_lex(script_code);
        free(script_code);
        return result;
    } else {
        DBG("Could not find script file '%s'.", file_name);
        ErrorMessage_set("Could not find script file '%s'.", file_name);
        return (MynValue) { .type = MYN_VALUE_TYPE_ERROR, .error_type = MYN_ERROR_TYPE_OTHER };
    };
}
