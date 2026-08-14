### LMCTF Makefile ###

-include .config

ifndef CPU
    CPU := $(shell uname -m | sed -e s/i.86/i386/ -e s/amd64/x86_64/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/ -e s/alpha/axp/)
endif

ifndef REV
    REV := $(shell git rev-list HEAD | wc -l)
endif

ifndef VER
    VER := r$(REV)~$(shell git rev-parse --short HEAD)
endif

REVISION_HEADER := GitRevisionInfo.h
REVISION_TEMPLATE := GitRevisionInfo.tmpl
HOST_TEST_BIN := sg_hooks_test.make
HOST_TEST_OBJS := .sg_hooks_test.make.o .sg_hooks_under_test.make.o
HOST_TEST_DEPS := $(HOST_TEST_OBJS:.o=.d)
HOST_TEST_ALL_ARTIFACTS := sg_hooks_test sg_hooks_test.gnu sg_hooks_test.make \
	.sg_hooks_test.gnu.o .sg_hooks_test.gnu.d \
	.sg_hooks_under_test.gnu.o .sg_hooks_under_test.gnu.d \
	.sg_hooks_test.make.o .sg_hooks_test.make.d \
	.sg_hooks_under_test.make.o .sg_hooks_under_test.make.d

CC ?= gcc
WINDRES ?= windres
STRIP ?= strip
RM ?= rm -f

CFLAGS ?= -DVER='"$(VER)"' -std=c11 -O0 -fno-strict-aliasing -g -Wall -MMD $(INCLUDES)
LDFLAGS ?= -shared

ifdef CONFIG_WINDOWS
    LDFLAGS += -mconsole
    LDFLAGS += -Wl,--nxcompat,--dynamicbase
else
    CFLAGS += -fPIC -fvisibility=hidden
    LDFLAGS += -Wl,--no-undefined
endif

CFLAGS += -O3 -g -Wall
LDFLAGS +=

HEADERS := \
	bat.h \
	game.h \
	g_ctffunc.h \
	g_local.h \
	g_menu.h \
	g_skins.h \
	gslog.h \
	g_tourney.h \
	g_vote.h \
	m_actor.h \
	m_berserk.h \
	m_boss2.h \
	m_boss31.h \
	m_boss32.h \
	m_brain.h \
	m_chick.h \
	m_flipper.h \
	m_float.h \
	m_flyer.h \
	m_gladiator.h \
	m_gunner.h \
	m_hover.h \
	m_infantry.h \
	m_insane.h \
	m_medic.h \
	m_mutant.h \
	m_parasite.h \
	m_player.h \
	m_rider.h \
	m_soldier.h \
	m_supertank.h \
	m_tank.h \
	plasma.h \
	p_stats.h \
	q_shared.h \
	stdlog.h

OBJS := \
	bat.o \
	ctf_file_io.o \
	ctf_sqlite_core.o \
	ctf_sqlite_player.o \
	ctf_sqlite_unidb.o \
	sqlite3.o \
	g_ai.o \
	g_chase.o \
	g_cmds.o \
	g_combat.o \
	g_ctffunc.o \
	g_func.o \
	g_items.o \
	g_main.o \
	g_menu.o \
	g_misc.o \
	g_monster.o \
	g_phys.o \
	g_replace.o \
	g_runes.o \
	g_save.o \
	g_skins.o \
	gslog.o \
	g_spawn.o \
	g_svcmds.o \
	g_target.o \
	g_tourney.o \
	g_trigger.o \
	g_turret.o \
	g_utils.o \
	g_vote.o \
	g_weapon.o \
	m_actor.o \
	m_berserk.o \
	m_boss2.o \
	m_boss31.o \
	m_boss32.o \
	m_boss3.o \
	m_brain.o \
	m_chick.o \
	m_flash.o \
	m_flipper.o \
	m_float.o \
	m_flyer.o \
	m_gladiator.o \
	m_gunner.o \
	m_hover.o \
	m_infantry.o \
	m_insane.o \
	m_medic.o \
	m_move.o \
	m_mutant.o \
	m_parasite.o \
	m_soldier.o \
	m_supertank.o \
	m_tank.o \
	p_client.o \
	p_hud.o \
	plasma.o \
	p_observer.o \
	p_stats.o \
	p_trail.o \
	p_view.o \
	p_weapon.o \
	q_shared.o \
	sg_oracle.o \
	sg_rune.o \
	sg_arach.o \
	sg_fields.o \
	sg_caco.o \
	sg_combat.o \
	sg_cvars.o \
	sg_hooks.o \
	sg_util.o \
	sg_client.o \
	sg_clock.o \
	sg_danger.o \
	sg_weights.o \
	sg_tilt.o \
	sg_lead.o \
	sg_move.o \
	sg_price.o \
	sg_descend.o \
	sg_goal.o \
	sg_chat.o \
	sg_net.o \
	sg_persona.o \
	stdlog.o \
	ui_text.o \
	ui_layout.o \
	ui_boards.o

ifdef CONFIG_VARIABLE_SERVER_FPS
    CFLAGS += -DUSE_FPS=1
endif

ifdef CONFIG_WINDOWS
    TARGET ?= game$(CPU)-lmctf-$(VER).dll
else
    LIBS += -lm
    TARGET ?= game$(CPU)-lmctf-$(VER).so
endif

all: $(TARGET)

default: all

.PHONY: all default host-test clean strip FORCE

FORCE:

# Define V=1 to show command line.
ifdef V
    Q :=
    E := @true
else
    Q := @
    E := @echo
endif

$(REVISION_HEADER): $(REVISION_TEMPLATE) FORCE
	$(E) [GEN] $@
	$(Q)set -e; \
	tmp="$@.tmp.$$$$"; \
	trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
	sed -e 's/\$$//g' \
	    -e 's/WCLOGCOUNT+2/$(REV)/g' \
	    -e 's/WCREV=7/$(VER)/g' \
	    -e 's/WCNOW=%Y/$(shell date +%Y)/g' \
	    "$<" > "$$tmp"; \
	if test -r "$@" && cmp -s "$$tmp" "$@"; then \
		$(RM) "$$tmp"; \
	else \
		mv -f "$$tmp" "$@"; \
	fi; \
	trap - EXIT HUP INT TERM

# The generated dependency files are absent on the first build, so retain an
# explicit prerequisite for the generated header on every object.
$(OBJS): $(REVISION_HEADER)

-include $(OBJS:.o=.d)
-include $(HOST_TEST_DEPS)

%.o: %.c
	$(E) [CC] $@
	$(Q)$(CC) -c $(CFLAGS) -o $@ $<

%.o: %.rc
	$(E) [RC] $@
	$(Q)$(WINDRES) $(RCFLAGS) -o $@ $<

$(TARGET): $(OBJS)
	$(E) [LD] $@
	$(Q)$(CC) -o $@ $^ $(LDFLAGS) $(LIBS)

.sg_hooks_test.make.o: slipgate/sg_hooks_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-DSG_HOST_TEST -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hooks_under_test.make.o: slipgate/sg_hooks.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-DSG_HOST_TEST -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

$(HOST_TEST_BIN): $(HOST_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(HOST_TEST_OBJS) $(LIBS)

host-test: $(HOST_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(HOST_TEST_BIN)

clean:
	$(E) [CLEAN]
	$(Q)$(RM) *.o *.d $(TARGET) $(REVISION_HEADER) \
		$(REVISION_HEADER).tmp.* $(HOST_TEST_ALL_ARTIFACTS)

strip: $(TARGET)
	$(E) [STRIP]
	$(Q)$(STRIP) $(TARGET)


# Third-party SQLite amalgamation: own rule, warnings off, single-threaded.
SQLITE_CFLAGS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION=1 \
                -DSQLITE_DEFAULT_MEMSTATUS=0 -w

sqlite3.o: sqlite3.c
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) $(SQLITE_CFLAGS) -o $@ -c $<
