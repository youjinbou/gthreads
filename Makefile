CC               = gcc
YACC             = bison -d
LEX              = flex
TARGET          ?= test_detach

DEBUG           ?= -g -DDEBUG

CFLAGS          += -Wall -I $(srcdir)

genfiles         =
files            = list gthread
libfiles         = 

srcdir           = src/
libdir           = src/
objdir           = obj/
bindir           = bin/
testdir          = tests/

gensources       = $(addprefix $(gendir), $(addsuffix .c,$(genfiles)))
genheaders       = 
sources          = $(addprefix $(srcdir), $(addsuffix .c,$(files))) $(addprefix $(srcdir), $(addsuffix .c,$(libfiles)))
headers          = $(addprefix $(srcdir), $(addsuffix .h,$(files))) $(addprefix $(srcdir), $(addsuffix .h,$(libfiles)))
objects          = $(addprefix $(objdir), $(addsuffix .o,$(files))) $(addprefix $(objdir), $(addsuffix .o,$(genfiles))) $(addprefix $(objdir), $(addsuffix .o,$(libfiles)))
objects_dbg      = $(addsuffix .dbg.o,$(files)) $(addsuffix .o,$(genfiles)) $(addsuffix .o,$(libfiles))

.PHONY: all clean

all: $(bindir)$(TARGET)

clean:
	$(RM) $(objdir)*.o $(gensources) $(genheaders) $(bindir)*

#$(lib): $(objects)
#	$(AR) 

$(bindir)%:  $(testdir)%.o $(sources) $(headers) $(gensources) $(genheaders) $(objects)
	$(CC) $(CFLAGS) $(objects) $< -o $@

$(bindir)%.dbg: $(testdir)%.dbg.o $(sources) $(headers) $(gensources) $(genheaders) $(objects_dbg) 
	$(CC) $(CFLAGS) $(objects_dbg) $< -o $@

parser.tab.c parser.tab.h: parser.y
	$(YACC) $< 

lex.yy.c: lexer.lex
	$(LEX) $<

$(testdir)%.o: $(testdir)%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(testdir)%.dbg.o: $(testdir)%.c
	$(CC) $(CFLAGS) $(DEBUG) -c $< -o $@

$(objdir)%.o: $(srcdir)%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(objdir)%.dbg.o: $(srcdir)%.c
	$(CC) $(CFLAGS) $(DEBUG) -c $< -o $@
