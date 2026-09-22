// Myn scripting language, Thomas Führinger, 2025

#pragma once

#define TOK_INTEGER 'i'
#define TOK_BYTE 'b'
#define TOK_FLOAT 'd'
#define TOK_STRING 's'
#define TOK_NONE 'n'
#define TOK_TRUE 't'
#define TOK_FALSE 'f'
#define TOK_FUNCTION 'F'
#define TOK_PROCEDURE 'P'
#define TOK_VARIABLE 'V'
#define TOK_REFERENCE '&'
#define TOK_ASSIGN ':'
// #define TOK_FUNCTIONCALL 'C'
// #define TOK_PROCEDURECALL 'c'
#define TOK_TYPE 'y'
#define TOK_LEN 'L'
// #define TOK_CALL 'c'
#define TOK_IF 'I'
#define TOK_ELSE 'E'
#define TOK_ELIF 'e'
#define TOK_WHILE 'W'
#define TOK_DO 'D'
#define TOK_DO_WHILE 'w'
#define TOK_FOR 'R'
#define TOK_TO 'o'
#define TOK_BREAK 'B'
#define TOK_CONTINUE 'T'
#define TOK_RETURN 'r'
#define TOK_SEMICOLON ';'
#define TOK_BLOCK_END '#'
#define TOK_EQUAL '='
#define TOK_UNEQUAL '!'
#define TOK_COMMA ','
#define TOK_LESSTHAN '<'
#define TOK_LESSTHANEQ 'l'
#define TOK_GREATERTHAN '>'
#define TOK_GREATERTHANEQ 'g'
#define TOK_IDENTICAL 'q'
#define TOK_PLUS '+'
#define TOK_PLUS_EQUAL 'p'
#define TOK_MINUS '-'
#define TOK_MINUS_EQUAL 'm'
#define TOK_MULTIPLY '*'
#define TOK_DIVIDE '/'
#define TOK_POWER_OF '^'
#define TOK_MODULO '%'
#define TOK_AND 'A'
#define TOK_OR 'O'
#define TOK_NOT 'N'
#define TOK_LPAREN '('
#define TOK_RPAREN ')'
#define TOK_LBRACKET '['
#define TOK_RBRACKET ']'
#define TOK_STR 'S'
#define TOK_INT 'Z'
#define TOK_FLT 'U'
#define TOK_BYT 'Y'
#define TOK_RANDOM 'M'

// { "none", "bool", "byte", "integer", "float", "complex", "time", "string", "callable", "stream", "dictionary", "object", "type", "other" };
#define TOK_BOOL_TYPE 128
#define TOK_BYTE_TYPE 129
#define TOK_INTEGER_TYPE 130
#define TOK_FLOAT_TYPE 131
#define TOK_STRING_TYPE 134
#define TOK_CALLABLE_TYPE 136
#define TOK_STREAM_TYPE 137
#define TOK_DICTIONARY_TYPE 138
#define TOK_OBJECT_TYPE 139
#define TOK_TYPE_TYPE 140

#define TOK_PRINT 141
#define TOK_PEEK 142
#define TOK_POKE 143
#define TOK_FILE_OPEN 144
#define TOK_FILE_CLOSE 145
#define TOK_FILE_COPY 146
#define TOK_FILE_DELETE 147
#define TOK_FILE_RENAME 148
#define TOK_FILE_EXISTS 149
#define TOK_STREAM_READ_BYTE 150
#define TOK_STREAM_READ_STRING 151
#define TOK_STREAM_WRITE_BYTE 152
#define TOK_STREAM_WRITE_STRING 153
#define TOK_STREAM_POS_MOVE 154
