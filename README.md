# Myn

Simple, light-weight Python style scripting language  
Based on the MIN programming language by Carsten Herting (slu4) at  
https://github.com/slu4coder/Minimal-UART-CPU-System
  

* Nothing to install - just one small executable file
* Nothing to learn - common instructions in Python like syntax
* Easy to embed - a few hundred lines of pure C code, dependent only on the standard library
* All the essentials for scripting purposes
  
  <br/>
Example code:

```
print "Hello World.myn"
```
  
<br/>
USAGE: 
```
myn "Hello World.myn"
```
  
or compile it into object code:

```
mynce "Hello World.myn"
```
  
and run
```
myn "Hello World.mync"
```

## Features

* Python-style indentation

* if-elif-else, do-while-continue-break, print, fn-pr-return

* `str()`, `int()`, `len()`, `random()`, `type()`

* string, integer, float, byte, bool and stream data types

* local and global variables and 1-dimensional arrays

* functions with parameters and C-style referencing

* `'A'` replacing `ord("A")`
  

## Examples


```
fn fibonacci(n)
    if n < 0
        print "Incorrect input"
    elif n == 0
        return 0
    elif n == 1 or n == 2
        return 1
    else
        return fibonacci(n-1) + fibonacci(n-2)

stream_write_string STDOUT, "Enter a number: "
input = stream_read_string(STDIN, 6)
n = int(input)
print "Fibonacci(", str(n), ") = ", fibonacci(n), "\n" # print is short for: stream_write_string STDOUT,
```
  
Instructions and built-in funcions can be written in lowercase or - as I prefer for readability - uppercase with function names in CamelCase:
```
FN Fibonacci(n)
    IF n < 0
        Print "Incorrect input"
    ELIF n == 0
        RETURN 0
    ELIF n == 1 OR n == 2
        RETURN 1
    ELSE
        RETURN Fibonacci(n-1) + Fibonacci(n-2)

StreamWriteString STDOUT, "Enter a number: "
input = StreamReadString(STDIN, 6)
n = Int(input)
Print "Fibonacci(", Str(n), ") = ", Fibonacci(n), "\n"
```
  
Arrays

```
i = 0; b = [2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31] # integer array
DO
    i += 1

    IF i % 2 == 0
        CONTINUE
    IF i > 7
        BREAK

    Print b[i], " "

WHILE i < Len(b)


a = ['\x31', '2', '\x33'] # byte array

FOR i = 0 TO Len(a) - 1
    Print "a[", i, "]  = ", a[i], "\n"

```
  
Pass parameters by reference (to avoid copying)

```
a = 1; b = 2

FN Modify(x, &y) # pass by reference
    Print "2: x, y ", x, ", ", y, "\n"
    IF Type(y) != INTEGER
        Print "Expected y to be an integer but it is a ", Type(y), "!\n"
        RETURN
    x = 3; y = 4
    Print "3: x, y ", x, ", ", y, "\n"

Print "1: a, b ", a, ", ", b, "\n"
Modify(a, b)
Print "4: a, b ", a, ", ", b, "\n"
```
  
Procedures vs functions

```
# procedure
PR PrintThis x
    Print x, "\n"

# function
FN Add(a, b)
    RETURN a + b

PrintThis "Hi!"
Print "1 + 2 = ", Add(1, 2), "\n"
```
  
  
This is how you would embed the interpreter:

```
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
```