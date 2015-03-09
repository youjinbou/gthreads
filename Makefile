CC               = g++ -g
YACC             = bison -d
LEX              = flex
TARGET          ?= main

CFLAGS          += -Wall

genfiles         =
files            = list green_thread
libfiles         = 

gensources       = $(addsuffix .c,$(genfiles)) 
genheaders       = 
sources          = $(addsuffix .c,$(files)) $(addsuffix .c,$(libfiles))
headers          = $(addsuffix .h,$(files)) $(addsuffix .h,$(libfiles))
objects          = $(addsuffix .o,$(files)) $(addsuffix .o,$(genfiles)) $(addsuffix .o,$(libfiles))


.PHONY: all clean

all: $(TARGET)

clean:
	$(RM) *.o $(gensources) $(genheaders) $(TARGET)

$(TARGET): $(sources) $(headers) $(gensources) $(genheaders) $(objects) 
	$(CC) $(CFLAGS) $(objects) -o $(TARGET)

parser.tab.c parser.tab.h: parser.y
	$(YACC) $< 

lex.yy.c: lexer.lex
	$(LEX) $<



