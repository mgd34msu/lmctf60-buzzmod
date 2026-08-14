######################################################################
# Linux Makefile for Quake2 v1.4
# Robert "Arras" LeBlanc <rjl@renaissoft.com>
#
# This Makefile is based on the "authoritative" Makefile released
# by Zoid <zoid@idsoftware.com> and refined somewhat by Jet
# <jet@poboxes.com>, and now finally refined a bit more by me to
# make it more useful to mod authors.
#
# To use this Makefile to build your own mod, follow the steps below:
#
# Step 1: Get the 03/14 source distribution from id Software
#         and copy the contents (*.c, *.h) to a working directory.
#
# Step 2: Copy this Makefile into the working directory.  If you have
#         a Redhat Linux distribution, comment out the LDFLAGS
#         definition below--you don't need "-ldl -lm", and in fact the
#         presence of these flags will cause your build to crash and
#         burn.  Slackware users can leave them untouched.
#
# Step 3: Copy any mod-related source files of yours into the working
#         directory.
#
# Step 4: If you developed your source files on a Win32 machine, type
#         "make stripcr" to get rid of any stray carriage returns that
#         may be lurking in your files.
#         MJD - If make complains that it cannot find a separator, then
#               your makefile has been contaminated, too!  Somehow, you
#               need to run stripcr on your makefile so you can use
#               the makefile!  Neat catch-22, there.  I suggest a little
#               set of utils called unix2dos and dos2unix.
#
# Step 5: Add your custom mod-related files to the C_OBJS list below,
#         so that the Makefile knows to build them.  For every *.c
#         file in your mod you should have a corresponding *.o file
#         listed under C_OBJS.  Don't list files that are already
#         part of id's sources (e.g. g_cmds.o, p_client.o, etc.),
#         they're already known to the Makefile; just list any *.c
#         files specific to your mod.
#         MJD - If you get a bunch of messages about a function
#         already being defined, and it shows you the same line
#         twice -- that means you put the same file in the list
#         of objects twice.  Oops.
#
# Step 6: Type "make dep" to build a list of dependencies based on
#         the total set of source files in the working directory.
#
# Step 7: Type "make" to build your mod.
######################################################################

PLATFORM := $(shell uname 2>/dev/null || echo Windows)
PLATFORM := $(patsubst CYGWIN%,Cygwin,$(PLATFORM))
PLATFORM := $(patsubst MSYS%,Cygwin,$(PLATFORM))
PLATFORM := $(patsubst MINGW%,Windows,$(PLATFORM))
PLATFORM := $(patsubst UCRT%,Windows,$(PLATFORM))
PLATFORM := $(patsubst CLANG%,Windows,$(PLATFORM))

# this nice line comes from the linux kernel makefile
ARCH := $(shell uname -m | sed -e s/i.86/i386/ \
	-e s/sun4u/sparc64/ -e s/arm.*/arm/ \
	-e s/sa110/arm/ -e s/alpha/axp/)

ifeq ($(PLATFORM),Windows)
	ifeq ($(patsubst MINGW32%,,$(shell uname 2>/dev/null)),)
		ARCH := x86
	endif
	ifeq ($(patsubst CLANG32%,,$(shell uname 2>/dev/null)),)
		ARCH := x86
	endif
	ifeq ($(ARCH),i386)
		ARCH := x86
	endif
endif

# On 64-bit OS use the command: 'setarch i386 make' after 'make clean'
# to obtain the 32-bit binary DLL on 64-bit Linux.

# on x64 machines do this preparation:
# sudo apt-get install ia32-libs
# sudo apt-get install libc6-dev-i386
# On Ubuntu 16.x use sudo apt install libc6-dev-i386
# this will let you build 32-bits on ia64 systems
#
ifndef REV
    REV := $(shell git rev-list HEAD | wc -l)
endif

ifndef VER
    VER := r$(REV)~$(shell git rev-parse --short HEAD)
endif

REVISION_HEADER = GitRevisionInfo.h
REVISION_TEMPLATE = GitRevisionInfo.tmpl
DEPEND_FILE = .depend
HOST_TEST_BIN = sg_hooks_test.gnu
HOST_TEST_OBJS = .sg_hooks_test.gnu.o .sg_hooks_under_test.gnu.o
HOST_TEST_DEPS = $(HOST_TEST_OBJS:.o=.d)
ACTION_TEST_BIN = sg_action_test.gnu
ACTION_TEST_OBJS = .sg_action_test.gnu.o .sg_action_under_test.gnu.o
ACTION_TEST_DEPS = $(ACTION_TEST_OBJS:.o=.d)
IDENTITY_TEST_BIN = sg_identity_test.gnu
IDENTITY_TEST_OBJS = .sg_identity_test.gnu.o .sg_identity_under_test.gnu.o \
	.sg_crc32_under_test.gnu.o
IDENTITY_TEST_DEPS = $(IDENTITY_TEST_OBJS:.o=.d)
RUNE_WIRE_TEST_BIN = sg_rune_wire_test.gnu
RUNE_WIRE_TEST_OBJS = .sg_rune_wire_test.gnu.o \
	.sg_rune_wire_under_test.gnu.o .sg_rune_wire_action_under_test.gnu.o \
	.sg_rune_wire_crc_under_test.gnu.o
RUNE_WIRE_TEST_DEPS = $(RUNE_WIRE_TEST_OBJS:.o=.d)
SIDECAR_WIRE_TEST_BIN = sg_sidecar_wire_test.gnu
SIDECAR_WIRE_TEST_OBJS = .sg_sidecar_wire_test.gnu.o \
	.sg_sidecar_wire_under_test.gnu.o .sg_rune_wire_under_test.gnu.o \
	.sg_rune_wire_action_under_test.gnu.o .sg_rune_wire_crc_under_test.gnu.o
SIDECAR_WIRE_TEST_DEPS = $(SIDECAR_WIRE_TEST_OBJS:.o=.d)
SIDECAR_LOADER_TEST_BIN = sg_sidecar_loader_test.gnu
SIDECAR_LOADER_TEST_OBJS = .sg_sidecar_loader_test.gnu.o \
	.sg_sidecar_loader_under_test.gnu.o .sg_sidecar_wire_under_test.gnu.o \
	.sg_rune_wire_under_test.gnu.o .sg_rune_wire_action_under_test.gnu.o \
	.sg_rune_wire_crc_under_test.gnu.o
SIDECAR_LOADER_TEST_DEPS = $(SIDECAR_LOADER_TEST_OBJS:.o=.d)
SIDECAR_STORE_TEST_BIN = sg_sidecar_store_test.gnu
SIDECAR_STORE_TEST_OBJS = .sg_sidecar_store_test.gnu.o \
	.sg_sidecar_store_under_test.gnu.o .sg_sidecar_loader_under_test.gnu.o \
	.sg_sidecar_wire_under_test.gnu.o .sg_rune_wire_under_test.gnu.o \
	.sg_rune_wire_action_under_test.gnu.o .sg_rune_wire_crc_under_test.gnu.o
SIDECAR_STORE_TEST_DEPS = $(SIDECAR_STORE_TEST_OBJS:.o=.d)
DANGER_LEASE_TEST_BIN = sg_danger_lease_test.gnu
DANGER_LEASE_TEST_OBJS = .sg_danger_lease_test.gnu.o \
	.sg_danger_lease_under_test.gnu.o
DANGER_LEASE_TEST_DEPS = $(DANGER_LEASE_TEST_OBJS:.o=.d)
DANGER_POLICY_TEST_BIN = sg_danger_policy_test.gnu
DANGER_POLICY_TEST_OBJS = .sg_danger_policy_test.gnu.o \
	.sg_danger_policy_under_test.gnu.o
DANGER_POLICY_TEST_DEPS = $(DANGER_POLICY_TEST_OBJS:.o=.d)
DANGER_V3_TEST_BIN = sg_danger_v3_test.gnu
DANGER_V3_TEST_OBJS = .sg_danger_v3_test.gnu.o \
	.sg_danger_under_test.gnu.o .sg_rune_wire_under_test.gnu.o \
	.sg_rune_wire_action_under_test.gnu.o .sg_rune_wire_crc_under_test.gnu.o
DANGER_V3_TEST_DEPS = $(DANGER_V3_TEST_OBJS:.o=.d)
FIELDS_CANDIDATE_TEST_BIN = sg_fields_candidate_test.gnu
FIELDS_CANDIDATE_TEST_OBJS = .sg_fields_candidate_test.gnu.o \
	.sg_fields_candidate_under_test.gnu.o
FIELDS_CANDIDATE_TEST_DEPS = $(FIELDS_CANDIDATE_TEST_OBJS:.o=.d)
RUNE_LOADER_TEST_BIN = sg_rune_loader_test.gnu
RUNE_LOADER_TEST_OBJS = .sg_rune_loader_test.gnu.o \
	.sg_rune_loader_under_test.gnu.o .sg_rune_wire_under_test.gnu.o \
	.sg_rune_wire_action_under_test.gnu.o .sg_rune_wire_crc_under_test.gnu.o
RUNE_LOADER_TEST_DEPS = $(RUNE_LOADER_TEST_OBJS:.o=.d)
RUNE_WRITER_TEST_BIN = sg_rune_writer_test.gnu
RUNE_WRITER_TEST_OBJS = .sg_rune_writer_test.gnu.o \
	.sg_rune_writer_under_test.gnu.o .sg_rune_wire_under_test.gnu.o \
	.sg_rune_wire_action_under_test.gnu.o .sg_rune_wire_crc_under_test.gnu.o
RUNE_WRITER_TEST_DEPS = $(RUNE_WRITER_TEST_OBJS:.o=.d)
RUNE_INSTALL_TEST_BIN = sg_rune_install_test.gnu
RUNE_INSTALL_TEST_OBJS = .sg_rune_install_test.gnu.o \
	.sg_rune_install_under_test.gnu.o .sg_rune_writer_under_test.gnu.o \
	.sg_rune_wire_under_test.gnu.o .sg_rune_wire_action_under_test.gnu.o \
	.sg_rune_wire_crc_under_test.gnu.o
RUNE_INSTALL_TEST_DEPS = $(RUNE_INSTALL_TEST_OBJS:.o=.d)
RUNE_PROOF_TEST_BIN = sg_rune_proof_test.gnu
RUNE_PROOF_TEST_OBJS = .sg_rune_proof_test.gnu.o \
	.sg_rune_proof_under_test.gnu.o
RUNE_PROOF_TEST_DEPS = $(RUNE_PROOF_TEST_OBJS:.o=.d)
REPLAY_TEST_BIN = sg_replay_test.gnu
REPLAY_TEST_OBJS = .sg_replay_test.gnu.o .sg_replay_under_test.gnu.o
REPLAY_TEST_DEPS = $(REPLAY_TEST_OBJS:.o=.d)
SWIM_LIVE_TEST_BIN = sg_swim_live_test.gnu
SWIM_LIVE_TEST_OBJS = .sg_swim_live_test.gnu.o \
	.sg_swim_live_under_test.gnu.o .sg_swim_live_replay_under_test.gnu.o
SWIM_LIVE_TEST_DEPS = $(SWIM_LIVE_TEST_OBJS:.o=.d)
ENTFILE_TEST_BIN = g_entfile_path_test.gnu
ENTFILE_TEST_OBJS = .g_entfile_path_test.gnu.o
ENTFILE_TEST_DEPS = $(ENTFILE_TEST_OBJS:.o=.d)
ENGINE_SNAPSHOT_TEST = tests/test_engine_snapshot_name.sh
HOST_TEST_ALL_ARTIFACTS = sg_hooks_test sg_hooks_test.gnu sg_hooks_test.make \
	.sg_hooks_test.gnu.o .sg_hooks_test.gnu.d \
	.sg_hooks_under_test.gnu.o .sg_hooks_under_test.gnu.d \
	.sg_hooks_test.make.o .sg_hooks_test.make.d \
	.sg_hooks_under_test.make.o .sg_hooks_under_test.make.d \
	sg_action_test sg_action_test.gnu sg_action_test.make \
	.sg_action_test.gnu.o .sg_action_test.gnu.d \
	.sg_action_under_test.gnu.o .sg_action_under_test.gnu.d \
	.sg_action_test.make.o .sg_action_test.make.d \
	.sg_action_under_test.make.o .sg_action_under_test.make.d \
	sg_identity_test sg_identity_test.gnu sg_identity_test.make \
	.sg_identity_test.gnu.o .sg_identity_test.gnu.d \
	.sg_identity_under_test.gnu.o .sg_identity_under_test.gnu.d \
	.sg_crc32_under_test.gnu.o .sg_crc32_under_test.gnu.d \
	.sg_identity_test.make.o .sg_identity_test.make.d \
	.sg_identity_under_test.make.o .sg_identity_under_test.make.d \
	.sg_crc32_under_test.make.o .sg_crc32_under_test.make.d \
	sg_rune_wire_test.gnu sg_rune_wire_test.make \
	.sg_rune_wire_test.gnu.o .sg_rune_wire_test.gnu.d \
	.sg_rune_wire_under_test.gnu.o .sg_rune_wire_under_test.gnu.d \
	.sg_rune_wire_action_under_test.gnu.o \
	.sg_rune_wire_action_under_test.gnu.d \
	.sg_rune_wire_crc_under_test.gnu.o \
	.sg_rune_wire_crc_under_test.gnu.d \
	.sg_rune_wire_test.make.o .sg_rune_wire_test.make.d \
	.sg_rune_wire_under_test.make.o .sg_rune_wire_under_test.make.d \
	.sg_rune_wire_action_under_test.make.o \
	.sg_rune_wire_action_under_test.make.d \
	.sg_rune_wire_crc_under_test.make.o \
	.sg_rune_wire_crc_under_test.make.d \
	sg_sidecar_wire_test.gnu sg_sidecar_wire_test.make \
	.sg_sidecar_wire_test.gnu.o .sg_sidecar_wire_test.gnu.d \
	.sg_sidecar_wire_under_test.gnu.o .sg_sidecar_wire_under_test.gnu.d \
	.sg_sidecar_wire_test.make.o .sg_sidecar_wire_test.make.d \
	.sg_sidecar_wire_under_test.make.o .sg_sidecar_wire_under_test.make.d \
	sg_sidecar_loader_test.gnu sg_sidecar_loader_test.make \
	.sg_sidecar_loader_test.gnu.o .sg_sidecar_loader_test.gnu.d \
	.sg_sidecar_loader_under_test.gnu.o .sg_sidecar_loader_under_test.gnu.d \
	.sg_sidecar_loader_test.make.o .sg_sidecar_loader_test.make.d \
	.sg_sidecar_loader_under_test.make.o .sg_sidecar_loader_under_test.make.d \
	sg_sidecar_store_test.gnu sg_sidecar_store_test.make \
	.sg_sidecar_store_test.gnu.o .sg_sidecar_store_test.gnu.d \
	.sg_sidecar_store_under_test.gnu.o .sg_sidecar_store_under_test.gnu.d \
	.sg_sidecar_store_test.make.o .sg_sidecar_store_test.make.d \
	.sg_sidecar_store_under_test.make.o .sg_sidecar_store_under_test.make.d \
	sg_danger_lease_test.gnu sg_danger_lease_test.make \
	.sg_danger_lease_test.gnu.o .sg_danger_lease_test.gnu.d \
	.sg_danger_lease_under_test.gnu.o .sg_danger_lease_under_test.gnu.d \
	.sg_danger_lease_test.make.o .sg_danger_lease_test.make.d \
	.sg_danger_lease_under_test.make.o .sg_danger_lease_under_test.make.d \
	sg_danger_policy_test.gnu sg_danger_policy_test.make \
	.sg_danger_policy_test.gnu.o .sg_danger_policy_test.gnu.d \
	.sg_danger_policy_under_test.gnu.o .sg_danger_policy_under_test.gnu.d \
	.sg_danger_policy_test.make.o .sg_danger_policy_test.make.d \
	.sg_danger_policy_under_test.make.o .sg_danger_policy_under_test.make.d \
	sg_danger_v3_test.gnu sg_danger_v3_test.make \
	.sg_danger_v3_test.gnu.o .sg_danger_v3_test.gnu.d \
	.sg_danger_under_test.gnu.o .sg_danger_under_test.gnu.d \
	.sg_danger_v3_test.make.o .sg_danger_v3_test.make.d \
	.sg_danger_under_test.make.o .sg_danger_under_test.make.d \
	sg_fields_candidate_test.gnu sg_fields_candidate_test.make \
	.sg_fields_candidate_test.gnu.o .sg_fields_candidate_test.gnu.d \
	.sg_fields_candidate_under_test.gnu.o .sg_fields_candidate_under_test.gnu.d \
	.sg_fields_candidate_test.make.o .sg_fields_candidate_test.make.d \
	.sg_fields_candidate_under_test.make.o .sg_fields_candidate_under_test.make.d \
	sg_rune_loader_test.gnu sg_rune_loader_test.make \
	.sg_rune_loader_test.gnu.o .sg_rune_loader_test.gnu.d \
	.sg_rune_loader_under_test.gnu.o .sg_rune_loader_under_test.gnu.d \
	.sg_rune_loader_test.make.o .sg_rune_loader_test.make.d \
	.sg_rune_loader_under_test.make.o .sg_rune_loader_under_test.make.d \
	sg_rune_writer_test.gnu sg_rune_writer_test.make \
	.sg_rune_writer_test.gnu.o .sg_rune_writer_test.gnu.d \
	.sg_rune_writer_under_test.gnu.o .sg_rune_writer_under_test.gnu.d \
	.sg_rune_writer_test.make.o .sg_rune_writer_test.make.d \
	.sg_rune_writer_under_test.make.o .sg_rune_writer_under_test.make.d \
	sg_rune_install_test.gnu sg_rune_install_test.make \
	.sg_rune_install_test.gnu.o .sg_rune_install_test.gnu.d \
	.sg_rune_install_under_test.gnu.o .sg_rune_install_under_test.gnu.d \
	.sg_rune_install_test.make.o .sg_rune_install_test.make.d \
	.sg_rune_install_under_test.make.o .sg_rune_install_under_test.make.d \
	sg_rune_proof_test.gnu sg_rune_proof_test.make \
	.sg_rune_proof_test.gnu.o .sg_rune_proof_test.gnu.d \
	.sg_rune_proof_under_test.gnu.o .sg_rune_proof_under_test.gnu.d \
	.sg_rune_proof_test.make.o .sg_rune_proof_test.make.d \
	.sg_rune_proof_under_test.make.o .sg_rune_proof_under_test.make.d \
	sg_replay_test.gnu sg_replay_test.make \
	.sg_replay_test.gnu.o .sg_replay_test.gnu.d \
	.sg_replay_under_test.gnu.o .sg_replay_under_test.gnu.d \
	.sg_replay_test.make.o .sg_replay_test.make.d \
	.sg_replay_under_test.make.o .sg_replay_under_test.make.d \
	sg_swim_live_test.gnu sg_swim_live_test.make \
	.sg_swim_live_test.gnu.o .sg_swim_live_test.gnu.d \
	.sg_swim_live_under_test.gnu.o .sg_swim_live_under_test.gnu.d \
	.sg_swim_live_replay_under_test.gnu.o \
	.sg_swim_live_replay_under_test.gnu.d \
	.sg_swim_live_test.make.o .sg_swim_live_test.make.d \
	.sg_swim_live_under_test.make.o .sg_swim_live_under_test.make.d \
	.sg_swim_live_replay_under_test.make.o \
	.sg_swim_live_replay_under_test.make.d \
	g_entfile_path_test.gnu g_entfile_path_test.make \
	.g_entfile_path_test.gnu.o .g_entfile_path_test.gnu.d \
	.g_entfile_path_test.make.o .g_entfile_path_test.make.d

# This is for native build
CFLAGS=-O3 -DARCH="$(ARCH)" -DSTDC_HEADERS -DVER='"$(VER)"'
# This is for 32-bit build on 64-bit host
ifeq ($(ARCH),i386)
CFLAGS =-m32 -O3 -DARCH="$(ARCH)" -DSTDC_HEADERS -DVER='"$(VER)"' -I/usr/include
endif

######################################################################
# C_OBJS (Custom Objects): Mod authors should use this group below to
# specify any custom files required by their mods that are not
# included in id's sources.  e.g. if your mod requires the addition
# of custom files "foo.c" and "bar.c", you'd add:
#
# C_OBJS = foo.o bar.o
#
# Leave this empty if you just want to build id's default gamei386.so.
######################################################################
C_OBJS = g_menu.o g_replace.o g_runes.o g_ctffunc.o \
         g_skins.o g_tourney.o plasma.o ui_text.o ui_layout.o ui_boards.o \
		 p_observer.o g_chase.o p_stats.o \
		 stdlog.o gslog.o bat.o g_vote.o \
		 ctf_file_io.o ctf_sqlite_core.o ctf_sqlite_player.o ctf_sqlite_unidb.o sqlite3.o \
		 sg_action.o sg_crc32.o sg_identity.o sg_rune_wire.o sg_sidecar_wire.o sg_sidecar_loader.o sg_sidecar_store.o sg_rune_loader.o sg_rune_writer.o sg_rune_install.o sg_rune_proof.o sg_replay.o sg_swim_live.o sg_oracle.o sg_rune.o sg_arach.o sg_fields.o sg_caco.o sg_combat.o \
		 sg_cvars.o sg_hooks.o sg_util.o sg_client.o sg_clock.o sg_danger.o sg_danger_lease.o sg_danger_policy.o sg_weights.o sg_tilt.o sg_lead.o sg_move.o sg_price.o sg_descend.o sg_goal.o \
		 sg_chat.o sg_net.o sg_persona.o


######################################################################
# End of user-customizable section - you shouldn't have to touch
# anything below this point.
# MJD - With the exception of if you want massive debugging turned
# on or not...
######################################################################

# Game-related objects
G_OBJS = g_ai.o g_cmds.o g_combat.o g_func.o g_items.o g_main.o \
         g_misc.o g_monster.o g_phys.o g_save.o g_spawn.o g_svcmds.o \
         g_target.o g_trigger.o g_turret.o g_utils.o g_weapon.o



# Monster-related objects
M_OBJS = m_actor.o m_berserk.o m_boss2.o m_boss3.o m_boss31.o \
         m_boss32.o m_brain.o m_chick.o m_flash.o m_flipper.o \
         m_float.o m_flyer.o m_gladiator.o m_gunner.o m_hover.o \
         m_infantry.o m_insane.o m_medic.o m_move.o m_mutant.o \
         m_parasite.o m_soldier.o m_supertank.o m_tank.o

# Player-related objects
P_OBJS = p_client.o p_hud.o p_trail.o p_view.o p_weapon.o

# Quake2-related objects
Q_OBJS = q_shared.o

# Lithium II ZBot detection object (uncomment the appropriate binary)
L_OBJS =
#L_OBJS = l2zbot/Linux_x86/zbotcheck.o
#L_OBJS = l2zbot/Linux_AXP/zbotcheck.o

# Note that the mod author's Custom Objects (C_OBJS) are built first,
# which should speed up the detection of compilation errors; if they
# build properly, the rest of id's code should build without complaint,
# at least until link time :)
#
OBJS = $(C_OBJS) $(G_OBJS) $(M_OBJS) $(P_OBJS) $(Q_OBJS)

TARGET = game$(ARCH)-lmctf-$(VER).so

CC = gcc -std=c11

SHELL = /bin/sh
#for Windows or when we don't know the OS.
LIBTOOL = ldd
CFLAGS += -g -Wall

# Windows / MinGW (incl. MSYS2 MinGW)
ifeq ($(PLATFORM),Windows)
TARGET = game$(ARCH)-lmctf-$(VER).dll
endif

# MSYS2 / Cygwin
ifeq ($(PLATFORM),Cygwin)
TARGET = game$(ARCH)-lmctf-$(VER).dll
CFLAGS += -DLINUX
LDFLAGS = -ldl -lm
endif

# flavors of Linux
ifeq ($(shell uname),Linux)
CFLAGS += -DLINUX
LDFLAGS = -ldl -lm
ifeq ("$(wildcard /etc/alpine-release)","")
LIBTOOL = ldd -r
endif
endif

# OS X wants to be Linux and FreeBSD too.
ifeq ($(shell uname),Darwin)
CFLAGS += -DLINUX
LDFLAGS = -ldl -lm
LIBTOOL = otool
endif

# Linker flags for building a shared library (*.so).
#
# Redhat Linux users don't need -ldl or -lm...
# MJD - I'm not sure I buy this.  My Slackware system works fine without
# linking in the DynaLink and Math libs.  It may be more of a function of
# library version than RHS/Slackware...  Try both!
#LDFLAGS =

# but Slackware people do
#LDFLAGS = -ldl -lm

SHLIBCFLAGS = -fPIC
SHLIBLDFLAGS = -shared

######################################################################
# Targets
######################################################################

.PHONY: all dep host-test action-test identity-test rune-wire-test \
	sidecar-wire-test sidecar-loader-test sidecar-store-test \
	danger-lease-test danger-policy-test danger-v3-test fields-candidate-test \
	rune-loader-test \
	rune-writer-test rune-install-test rune-proof-test replay-test \
	swim-live-test entfile-test \
	snapshot-test stripcr clean distclean FORCE

all: dep $(TARGET)

FORCE:

$(REVISION_HEADER): $(REVISION_TEMPLATE) FORCE
	@echo "Generating $@..."
	@set -e; \
	tmp="$@.tmp.$$$$"; \
	trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
	sed -e 's/\$$//g' \
	    -e 's/WCLOGCOUNT+2/$(REV)/g' \
	    -e 's/WCREV=7/$(VER)/g' \
	    -e 's/WCNOW=%Y/$(shell date +%Y)/g' \
	    "$<" > "$$tmp"; \
	if test -r "$@" && cmp -s "$$tmp" "$@"; then \
		rm -f "$$tmp"; \
	else \
		mv -f "$$tmp" "$@"; \
	fi; \
	trap - EXIT HUP INT TERM

# Dependency files do not exist in a fresh checkout.  This explicit edge is
# therefore the synchronization contract: no object compile may start until
# the complete generated header has been atomically installed.
$(OBJS): $(REVISION_HEADER)

.c.o:
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -o $@ -c $<

$(TARGET):	$(OBJS) $(L_OBJS)
		$(CC) $(CFLAGS) $(SHLIBLDFLAGS) -o $@ $(OBJS) $(L_OBJS) $(LDFLAGS)
		$(LIBTOOL) $@

.sg_hooks_test.gnu.o: slipgate/sg_hooks_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-DSG_HOST_TEST -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hooks_under_test.gnu.o: slipgate/sg_hooks.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-DSG_HOST_TEST -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

$(HOST_TEST_BIN): $(HOST_TEST_OBJS)
	$(CC) -o $@ $(HOST_TEST_OBJS) $(LDFLAGS)

$(ACTION_TEST_BIN): $(ACTION_TEST_OBJS)
	$(CC) -o $@ $(ACTION_TEST_OBJS) $(LDFLAGS)

$(IDENTITY_TEST_BIN): $(IDENTITY_TEST_OBJS)
	$(CC) -o $@ $(IDENTITY_TEST_OBJS) $(LDFLAGS)

$(RUNE_WIRE_TEST_BIN): $(RUNE_WIRE_TEST_OBJS)
	$(CC) -o $@ $(RUNE_WIRE_TEST_OBJS) $(LDFLAGS)

$(SIDECAR_WIRE_TEST_BIN): $(SIDECAR_WIRE_TEST_OBJS)
	$(CC) -o $@ $(SIDECAR_WIRE_TEST_OBJS) $(LDFLAGS)

$(SIDECAR_LOADER_TEST_BIN): $(SIDECAR_LOADER_TEST_OBJS)
	$(CC) -o $@ $(SIDECAR_LOADER_TEST_OBJS) $(LDFLAGS)

$(SIDECAR_STORE_TEST_BIN): $(SIDECAR_STORE_TEST_OBJS)
	$(CC) -o $@ $(SIDECAR_STORE_TEST_OBJS) $(LDFLAGS)

$(DANGER_LEASE_TEST_BIN): $(DANGER_LEASE_TEST_OBJS)
	$(CC) -o $@ $(DANGER_LEASE_TEST_OBJS) $(LDFLAGS)

$(DANGER_POLICY_TEST_BIN): $(DANGER_POLICY_TEST_OBJS)
	$(CC) -o $@ $(DANGER_POLICY_TEST_OBJS) $(LDFLAGS)

$(DANGER_V3_TEST_BIN): $(DANGER_V3_TEST_OBJS)
	$(CC) -o $@ $(DANGER_V3_TEST_OBJS) $(LDFLAGS)

$(FIELDS_CANDIDATE_TEST_BIN): $(FIELDS_CANDIDATE_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(FIELDS_CANDIDATE_TEST_OBJS) $(LDFLAGS)

$(RUNE_LOADER_TEST_BIN): $(RUNE_LOADER_TEST_OBJS)
	$(CC) -o $@ $(RUNE_LOADER_TEST_OBJS) $(LDFLAGS)

$(RUNE_WRITER_TEST_BIN): $(RUNE_WRITER_TEST_OBJS)
	$(CC) -o $@ $(RUNE_WRITER_TEST_OBJS) $(LDFLAGS)

$(RUNE_INSTALL_TEST_BIN): $(RUNE_INSTALL_TEST_OBJS)
	$(CC) -o $@ $(RUNE_INSTALL_TEST_OBJS) $(LDFLAGS)

$(RUNE_PROOF_TEST_BIN): $(RUNE_PROOF_TEST_OBJS)
	$(CC) -o $@ $(RUNE_PROOF_TEST_OBJS) $(LDFLAGS)

$(REPLAY_TEST_BIN): $(REPLAY_TEST_OBJS)
	$(CC) -o $@ $(REPLAY_TEST_OBJS) $(LDFLAGS)

$(SWIM_LIVE_TEST_BIN): $(SWIM_LIVE_TEST_OBJS)
	$(CC) -o $@ $(SWIM_LIVE_TEST_OBJS) $(LDFLAGS)

$(ENTFILE_TEST_BIN): $(ENTFILE_TEST_OBJS)
	$(CC) -o $@ $(ENTFILE_TEST_OBJS) $(LDFLAGS)

.sg_action_test.gnu.o: tests/sg_action_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_action_under_test.gnu.o: slipgate/sg_action.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_identity_test.gnu.o: tests/sg_identity_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_identity_under_test.gnu.o: slipgate/sg_identity.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_crc32_under_test.gnu.o: slipgate/sg_crc32.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_wire_test.gnu.o: tests/sg_rune_wire_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_wire_under_test.gnu.o: slipgate/sg_rune_wire.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_wire_action_under_test.gnu.o: slipgate/sg_action.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_wire_crc_under_test.gnu.o: slipgate/sg_crc32.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_wire_test.gnu.o: tests/sg_sidecar_wire_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_wire_under_test.gnu.o: slipgate/sg_sidecar_wire.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_loader_test.gnu.o: tests/sg_sidecar_loader_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_loader_under_test.gnu.o: slipgate/sg_sidecar_loader.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_store_test.gnu.o: tests/sg_sidecar_store_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_store_under_test.gnu.o: slipgate/sg_sidecar_store.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_lease_test.gnu.o: tests/sg_danger_lease_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_lease_under_test.gnu.o: slipgate/sg_danger_lease.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_policy_test.gnu.o: tests/sg_danger_policy_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_policy_under_test.gnu.o: slipgate/sg_danger_policy.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_v3_test.gnu.o: tests/sg_danger_v3_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_under_test.gnu.o: slipgate/sg_danger.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_fields_candidate_test.gnu.o: tests/sg_fields_candidate_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_fields_candidate_under_test.gnu.o: slipgate/sg_fields.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -DSG_FIELDS_TEST -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_loader_test.gnu.o: tests/sg_rune_loader_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_loader_under_test.gnu.o: slipgate/sg_rune_loader.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_writer_test.gnu.o: tests/sg_rune_writer_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_writer_under_test.gnu.o: slipgate/sg_rune_writer.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_install_test.gnu.o: tests/sg_rune_install_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_install_under_test.gnu.o: slipgate/sg_rune_install.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_proof_test.gnu.o: tests/sg_rune_proof_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_proof_under_test.gnu.o: slipgate/sg_rune_proof.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_replay_test.gnu.o: tests/sg_replay_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_replay_under_test.gnu.o: slipgate/sg_replay.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_swim_live_test.gnu.o: tests/sg_swim_live_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_swim_live_under_test.gnu.o: slipgate/sg_swim_live.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_swim_live_replay_under_test.gnu.o: slipgate/sg_replay.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.g_entfile_path_test.gnu.o: tests/g_entfile_path_test.c g_entfile_path.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-pedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

host-test: $(HOST_TEST_BIN) $(ACTION_TEST_BIN) $(IDENTITY_TEST_BIN) \
		$(RUNE_WIRE_TEST_BIN) $(SIDECAR_WIRE_TEST_BIN) \
		$(SIDECAR_LOADER_TEST_BIN) $(SIDECAR_STORE_TEST_BIN) \
		$(DANGER_LEASE_TEST_BIN) $(DANGER_POLICY_TEST_BIN) \
		$(DANGER_V3_TEST_BIN) $(FIELDS_CANDIDATE_TEST_BIN) \
		$(RUNE_LOADER_TEST_BIN) $(RUNE_WRITER_TEST_BIN) \
		$(RUNE_INSTALL_TEST_BIN) $(RUNE_PROOF_TEST_BIN) \
		$(REPLAY_TEST_BIN) $(SWIM_LIVE_TEST_BIN) \
		$(ENTFILE_TEST_BIN)
	./$(HOST_TEST_BIN)
	./$(ACTION_TEST_BIN)
	./$(IDENTITY_TEST_BIN)
	./$(RUNE_WIRE_TEST_BIN)
	./$(SIDECAR_WIRE_TEST_BIN)
	./$(SIDECAR_LOADER_TEST_BIN)
	./$(SIDECAR_STORE_TEST_BIN)
	./$(DANGER_LEASE_TEST_BIN)
	./$(DANGER_POLICY_TEST_BIN)
	./$(DANGER_V3_TEST_BIN)
	./$(FIELDS_CANDIDATE_TEST_BIN)
	./$(RUNE_LOADER_TEST_BIN)
	./$(RUNE_WRITER_TEST_BIN)
	./$(RUNE_INSTALL_TEST_BIN)
	./$(RUNE_PROOF_TEST_BIN)
	./$(REPLAY_TEST_BIN)
	./$(SWIM_LIVE_TEST_BIN)
	./$(ENTFILE_TEST_BIN)
	./$(ENGINE_SNAPSHOT_TEST)

action-test: $(ACTION_TEST_BIN)
	./$(ACTION_TEST_BIN)

identity-test: $(IDENTITY_TEST_BIN)
	./$(IDENTITY_TEST_BIN)

rune-wire-test: $(RUNE_WIRE_TEST_BIN)
	./$(RUNE_WIRE_TEST_BIN)

sidecar-wire-test: $(SIDECAR_WIRE_TEST_BIN)
	./$(SIDECAR_WIRE_TEST_BIN)

sidecar-loader-test: $(SIDECAR_LOADER_TEST_BIN)
	./$(SIDECAR_LOADER_TEST_BIN)

sidecar-store-test: $(SIDECAR_STORE_TEST_BIN)
	./$(SIDECAR_STORE_TEST_BIN)

danger-lease-test: $(DANGER_LEASE_TEST_BIN)
	./$(DANGER_LEASE_TEST_BIN)

danger-policy-test: $(DANGER_POLICY_TEST_BIN)
	./$(DANGER_POLICY_TEST_BIN)

danger-v3-test: $(DANGER_V3_TEST_BIN)
	./$(DANGER_V3_TEST_BIN)

fields-candidate-test: $(FIELDS_CANDIDATE_TEST_BIN)
	./$(FIELDS_CANDIDATE_TEST_BIN)

rune-loader-test: $(RUNE_LOADER_TEST_BIN)
	./$(RUNE_LOADER_TEST_BIN)

rune-writer-test: $(RUNE_WRITER_TEST_BIN)
	./$(RUNE_WRITER_TEST_BIN)

rune-install-test: $(RUNE_INSTALL_TEST_BIN)
	./$(RUNE_INSTALL_TEST_BIN)

rune-proof-test: $(RUNE_PROOF_TEST_BIN)
	./$(RUNE_PROOF_TEST_BIN)

replay-test: $(REPLAY_TEST_BIN)
	./$(REPLAY_TEST_BIN)

swim-live-test: $(SWIM_LIVE_TEST_BIN)
	./$(SWIM_LIVE_TEST_BIN)

entfile-test: $(ENTFILE_TEST_BIN)
	./$(ENTFILE_TEST_BIN)

snapshot-test:
	./$(ENGINE_SNAPSHOT_TEST)

dep: $(DEPEND_FILE)

$(DEPEND_FILE): $(OBJS:.o=.c) GNUmakefile FORCE | $(REVISION_HEADER)
	@echo "Updating dependencies..."
	@set -e; \
	tmp="$@.tmp.$$$$"; \
	trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
	$(CC) -MM $(OBJS:.o=.c) > "$$tmp"; \
	if test -r "$@" && cmp -s "$$tmp" "$@"; then \
		rm -f "$$tmp"; \
	else \
		mv -f "$$tmp" "$@"; \
	fi; \
	trap - EXIT HUP INT TERM

stripcr:	.
		@echo "Stripping carriage returns from source files..."
	 	@for f in *.[ch]; do \
		  cat $$f | tr -d '\015' > .stripcr; \
		  mv .stripcr $$f; \
		done; \
		rm -f .stripcr

clean:
		@echo "Deleting temporary files..."
		@rm -f $(OBJS) $(REVISION_HEADER) $(REVISION_HEADER).tmp.* \
			$(DEPEND_FILE).tmp.* $(HOST_TEST_ALL_ARTIFACTS) *.orig ~* core

distclean:	clean
		@echo "Deleting everything that can be rebuilt..."
		@rm -f $(TARGET) $(DEPEND_FILE)

ifeq (,$(filter clean distclean,$(MAKECMDGOALS)))
ifeq ($(DEPEND_FILE),$(wildcard $(DEPEND_FILE)))
include $(DEPEND_FILE)
endif
-include $(HOST_TEST_DEPS)
-include $(ACTION_TEST_DEPS)
-include $(IDENTITY_TEST_DEPS)
-include $(RUNE_WIRE_TEST_DEPS)
-include $(SIDECAR_WIRE_TEST_DEPS)
-include $(SIDECAR_LOADER_TEST_DEPS)
-include $(SIDECAR_STORE_TEST_DEPS)
-include $(DANGER_LEASE_TEST_DEPS)
-include $(DANGER_POLICY_TEST_DEPS)
-include $(DANGER_V3_TEST_DEPS)
-include $(FIELDS_CANDIDATE_TEST_DEPS)
-include $(RUNE_LOADER_TEST_DEPS)
-include $(RUNE_WRITER_TEST_DEPS)
-include $(RUNE_INSTALL_TEST_DEPS)
-include $(RUNE_PROOF_TEST_DEPS)
-include $(REPLAY_TEST_DEPS)
-include $(SWIM_LIVE_TEST_DEPS)
-include $(ENTFILE_TEST_DEPS)
endif

# The SQLite amalgamation is third-party and does not build clean under our
# -Wall, so give it its own rule. THREADSAFE=0 because the game module is
# single-threaded and it saves linking pthreads.
SQLITE_CFLAGS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION=1 \
                -DSQLITE_DEFAULT_MEMSTATUS=0 -w

sqlite3.o: sqlite3.c
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) $(SQLITE_CFLAGS) -o $@ -c $<
