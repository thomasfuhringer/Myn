# Myn Makefile

CC     = clang
# CC     = /opt/homebrew/opt/llvm/bin/clang # Mac
CFLAGS = -std=c23 -Os -Wall

PROG =	myn
OBJS =	Myn.o Lexer.o Interpreter.o Utilities.o

all: ${PROG} mynce embed_myn

${PROG}: ${OBJS}
	${CC} ${CFLAGS} ${OBJS} -o ${PROG}

mynce: Mynce.o Lexer.o Utilities.o
	${CC} ${CFLAGS} Mynce.o Lexer.o Utilities.o -o mynce

embed_myn: EmbedMyn.o Lexer.o Interpreter.o Utilities.o
	${CC} ${CFLAGS} EmbedMyn.o Lexer.o Interpreter.o Utilities.o -o embed_myn

Utilities.o: Utilities.c Utilities.h
	$(CC) $(CFLAGS) -c Utilities.c -o Utilities.o

Lexer.o: Lexer.c Myn.h Utilities.h Tokens.h
	$(CC) $(CFLAGS) -c Lexer.c -o Lexer.o

Interpreter.o: Interpreter.c Myn.h Utilities.h Tokens.h
	$(CC) $(CFLAGS) -c Interpreter.c -o Interpreter.o

Myn.o: Myn.c Myn.h Utilities.h Lexer.c Interpreter.c
	$(CC) $(CFLAGS) -c Myn.c -o Myn.o

Mynce.o: Mynce.c Myn.h Utilities.h Lexer.c Interpreter.c
	$(CC) $(CFLAGS) -c Mynce.c -o Mynce.o

EmbedMyn.o: EmbedMyn.c Myn.h Utilities.h Lexer.c Interpreter.c
	$(CC) $(CFLAGS) -c EmbedMyn.c -o EmbedMyn.o

test: ${PROG}
	./myn Test.myn

clean:
	rm -f *.o

run: myn
	./myn "Hello World.myn"
