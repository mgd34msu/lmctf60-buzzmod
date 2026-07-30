######################################################################
# Botlib for LMCTF BuzzMod.
#
# The bot AI itself -- routing, movement, goals, chat -- lives here, in
# a separate shared library the game module loads at runtime. Source is
# the Q3 botlib backported to Quake II; be_interface_q2.c is the adapter
# between the two.
#
# Built with -O0 deliberately: the upstream project does the same, and
# the routing code has been seen to misbehave under optimisation.
######################################################################
ARCH := $(shell uname -m | sed -e s/i.86/i386/)

# Overridable so CI can cross-build the Windows library with mingw:
#   make CC="x86_64-w64-mingw32-gcc -std=gnu17" TARGET=botlib.dll PLATFLAGS=-DWIN32
# CC is assigned, not ?=, because make predefines it: "?=" would leave the
# built-in "cc". A command-line CC= still overrides this, which is what the
# cross-build relies on.
TARGET ?= botlib.so
CC = gcc
PLATFLAGS ?= -DLINUX -fPIC

# -std=gnu17: this is 2001 code, and C23 turns "false" into a keyword, which
# the Q3 headers use as an enum constant.
# -DDEBUG unlocks the library's own navigation diagnostics. They are gated at
# runtime on the bot_developer libvar as well, so this costs nothing on a
# server that leaves it at 0.
CFLAGS = -std=gnu17 -O0 -g -DBOTLIB -DDEBUG -DSTDC_HEADERS -w $(PLATFLAGS)
LDFLAGS = -shared -lm

OBJS = be_aas_bspq2.o be_aas_cluster.o be_aas_debug.o be_aas_entity.o \
       be_aas_file.o be_aas_main.o be_aas_move.o be_aas_optimize.o \
       be_aas_reach.o be_aas_route.o be_aas_routealt.o be_aas_sample.o \
       be_ai_char.o be_ai_chat.o be_ai_gen.o be_ai_goal.o be_ai_move.o \
       be_ai_weap.o be_ai_weight.o be_ea.o be_interface.o \
       be_interface_q2.o l_crc.o l_libvar.o l_log.o l_memory.o \
       l_precomp.o l_script.o l_struct.o standalone.o \
       l_utils.o q_shared.o

all: $(TARGET)

# The botlib keeps the Q3 directory layout it was written against, so its
# includes ("../game_q3/q_shared.h") resolve without editing 100+ files.
# Two objects come from outside this directory.
q_shared.o: ../../q_shared.c
	$(CC) $(CFLAGS) -I. -I../bspc -o $@ -c $<

l_utils.o: ../bspc/l_utils.c
	$(CC) $(CFLAGS) -I. -I../bspc -o $@ -c $<

%.o: %.c
	$(CC) $(CFLAGS) -I. -I../bspc -o $@ -c $<

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS)

clean:
	@rm -f $(OBJS) $(TARGET)
