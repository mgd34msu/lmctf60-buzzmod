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
COMPOUND_TEST_BIN = sg_compound_test.gnu
COMPOUND_TEST_OBJS = .sg_compound_test.gnu.o \
	.sg_compound_under_test.gnu.o .sg_compound_action_under_test.gnu.o
COMPOUND_TEST_DEPS = $(COMPOUND_TEST_OBJS:.o=.d)
MOVER_LEASE_TEST_BIN = sg_mover_lease_test.gnu
MOVER_LEASE_TEST_OBJS = .sg_mover_lease_test.gnu.o \
	.sg_mover_lease_under_test.gnu.o
MOVER_LEASE_TEST_DEPS = $(MOVER_LEASE_TEST_OBJS:.o=.d)
MOVER_LEASE_TEST_ALL_ARTIFACTS = \
	sg_mover_lease_test.gnu sg_mover_lease_test.make \
	.sg_mover_lease_test.gnu.o .sg_mover_lease_test.gnu.d \
	.sg_mover_lease_under_test.gnu.o .sg_mover_lease_under_test.gnu.d \
	.sg_mover_lease_test.make.o .sg_mover_lease_test.make.d \
	.sg_mover_lease_under_test.make.o .sg_mover_lease_under_test.make.d
BUTTON_LIVE_TEST_BIN = sg_button_live_test.gnu
BUTTON_LIVE_TEST_OBJS = .sg_button_live_test.gnu.o \
	.sg_button_live_under_test.gnu.o
BUTTON_LIVE_TEST_DEPS = $(BUTTON_LIVE_TEST_OBJS:.o=.d)
BUTTON_LIVE_TEST_ALL_ARTIFACTS = \
	sg_button_live_test.gnu sg_button_live_test.make \
	.sg_button_live_test.gnu.o .sg_button_live_test.gnu.d \
	.sg_button_live_under_test.gnu.o .sg_button_live_under_test.gnu.d \
	.sg_button_live_test.make.o .sg_button_live_test.make.d \
	.sg_button_live_under_test.make.o .sg_button_live_under_test.make.d
BUTTON_GAME_TEST_BIN = sg_button_game_test.gnu
BUTTON_GAME_TEST_OBJS = .sg_button_game_test.gnu.o \
	.sg_button_game_live_under_test.gnu.o \
	.sg_button_game_move_under_test.gnu.o \
	.sg_door_approach_under_test.gnu.o \
	.sg_button_game_func_under_test.gnu.o \
	.sg_button_game_q_shared_under_test.gnu.o
BUTTON_GAME_TEST_DEPS = $(BUTTON_GAME_TEST_OBJS:.o=.d)
BUTTON_GAME_INTEGRATION_TEST = tests/test_button_game_integration.py
BUTTON_GAME_TEST_ALL_ARTIFACTS = \
	sg_button_game_test.gnu sg_button_game_test.make \
	.sg_button_game_test.gnu.o .sg_button_game_test.gnu.d \
	.sg_button_game_live_under_test.gnu.o \
	.sg_button_game_live_under_test.gnu.d \
	.sg_button_game_move_under_test.gnu.o \
	.sg_button_game_move_under_test.gnu.d \
	.sg_button_game_func_under_test.gnu.o \
	.sg_button_game_func_under_test.gnu.d \
	.sg_button_game_q_shared_under_test.gnu.o \
	.sg_button_game_q_shared_under_test.gnu.d \
	.sg_button_game_test.make.o .sg_button_game_test.make.d \
	.sg_button_game_live_under_test.make.o \
	.sg_button_game_live_under_test.make.d \
	.sg_button_game_move_under_test.make.o \
	.sg_button_game_move_under_test.make.d \
	.sg_button_game_func_under_test.make.o \
	.sg_button_game_func_under_test.make.d \
	.sg_button_game_q_shared_under_test.make.o \
	.sg_button_game_q_shared_under_test.make.d
COMPOUND_GUARD_TEST_BIN = sg_compound_guard_test.gnu
COMPOUND_GUARD_TEST_OBJS = .sg_compound_guard_test.gnu.o \
	.sg_compound_guard_under_test.gnu.o \
	.sg_compound_guard_mover_lease_under_test.gnu.o
COMPOUND_GUARD_TEST_DEPS = $(COMPOUND_GUARD_TEST_OBJS:.o=.d)
COMPOUND_GUARD_TEST_ALL_ARTIFACTS = \
	sg_compound_guard_test.gnu sg_compound_guard_test.make \
	.sg_compound_guard_test.gnu.o .sg_compound_guard_test.gnu.d \
	.sg_compound_guard_under_test.gnu.o \
	.sg_compound_guard_under_test.gnu.d \
	.sg_compound_guard_mover_lease_under_test.gnu.o \
	.sg_compound_guard_mover_lease_under_test.gnu.d \
	.sg_compound_guard_test.make.o .sg_compound_guard_test.make.d \
	.sg_compound_guard_under_test.make.o \
	.sg_compound_guard_under_test.make.d \
	.sg_compound_guard_mover_lease_under_test.make.o \
	.sg_compound_guard_mover_lease_under_test.make.d
COMPOUND_GUARD_GAME_TEST_BIN = sg_compound_guard_game_test.gnu
COMPOUND_GUARD_GAME_TEST_OBJS = .sg_compound_guard_game_test.gnu.o \
	.sg_compound_guard_game_under_test.gnu.o
COMPOUND_GUARD_GAME_TEST_DEPS = $(COMPOUND_GUARD_GAME_TEST_OBJS:.o=.d)
COMPOUND_GUARD_GAME_INTEGRATION_TEST = \
	tests/test_compound_guard_game_integration.py
COMPOUND_GUARD_GAME_TEST_ALL_ARTIFACTS = \
	sg_compound_guard_game_test.gnu sg_compound_guard_game_test.make \
	.sg_compound_guard_game_test.gnu.o \
	.sg_compound_guard_game_test.gnu.d \
	.sg_compound_guard_game_under_test.gnu.o \
	.sg_compound_guard_game_under_test.gnu.d \
	.sg_compound_guard_game_test.make.o \
	.sg_compound_guard_game_test.make.d \
	.sg_compound_guard_game_under_test.make.o \
	.sg_compound_guard_game_under_test.make.d
DECLARED_DOOR_GUARD_TEST_BIN = sg_declared_door_guard_test.gnu
DECLARED_DOOR_GUARD_TEST_OBJS = .sg_declared_door_guard_test.gnu.o \
	.sg_declared_door_guard_under_test.gnu.o
DECLARED_DOOR_GUARD_TEST_DEPS = $(DECLARED_DOOR_GUARD_TEST_OBJS:.o=.d)
DECLARED_DOOR_GUARD_INTEGRATION_TESTS = \
	tests/test_declared_door_guard_integration.py \
	tests/test_declared_door_guard_runtime_integration.py \
	tests/test_declared_door_guard_arach_integration.py
DECLARED_DOOR_GUARD_TEST_ALL_ARTIFACTS = \
	sg_declared_door_guard_test.gnu sg_declared_door_guard_test.make \
	.sg_declared_door_guard_test.gnu.o \
	.sg_declared_door_guard_test.gnu.d \
	.sg_declared_door_guard_under_test.gnu.o \
	.sg_declared_door_guard_under_test.gnu.d \
	.sg_declared_door_guard_test.make.o \
	.sg_declared_door_guard_test.make.d \
	.sg_declared_door_guard_under_test.make.o \
	.sg_declared_door_guard_under_test.make.d
COMPOUND_WORLD_TEST_BIN = sg_compound_world_test.gnu
COMPOUND_WORLD_TEST_OBJS = .sg_compound_world_test.gnu.o \
	.sg_compound_world_under_test.gnu.o \
	.sg_compound_world_q_shared_under_test.gnu.o
COMPOUND_WORLD_TEST_DEPS = $(COMPOUND_WORLD_TEST_OBJS:.o=.d)
COMPOUND_GEN_TEST_BIN = sg_compound_gen_test.gnu
COMPOUND_GEN_TEST_OBJS = .sg_compound_gen_test.gnu.o \
	.sg_compound_gen_under_test.gnu.o
COMPOUND_GEN_TEST_DEPS = $(COMPOUND_GEN_TEST_OBJS:.o=.d)
COMPOUND_GEN_TEST_ALL_ARTIFACTS = \
	sg_compound_gen_test sg_compound_gen_test.gnu sg_compound_gen_test.make \
	.sg_compound_gen_test.gnu.o .sg_compound_gen_test.gnu.d \
	.sg_compound_gen_under_test.gnu.o .sg_compound_gen_under_test.gnu.d \
	.sg_compound_gen_test.make.o .sg_compound_gen_test.make.d \
	.sg_compound_gen_under_test.make.o .sg_compound_gen_under_test.make.d
COMPOUND_PUBLICATION_TEST_BIN = sg_compound_publication_test.gnu
COMPOUND_PUBLICATION_TEST_OBJS = .sg_compound_publication_test.gnu.o \
	.sg_compound_publication_under_test.gnu.o
COMPOUND_PUBLICATION_TEST_DEPS = $(COMPOUND_PUBLICATION_TEST_OBJS:.o=.d)
COMPOUND_PUBLICATION_INTEGRATION_TEST = \
	tests/test_compound_publication_integration.py
MECHANISM_PUBLICATION_INTEGRATION_TEST = \
	tests/test_mechanism_publication_integration.py
COMPOUND_PUBLICATION_TEST_ALL_ARTIFACTS = \
	sg_compound_publication_test.gnu sg_compound_publication_test.make \
	.sg_compound_publication_test.gnu.o \
	.sg_compound_publication_test.gnu.d \
	.sg_compound_publication_under_test.gnu.o \
	.sg_compound_publication_under_test.gnu.d \
	.sg_compound_publication_test.make.o \
	.sg_compound_publication_test.make.d \
	.sg_compound_publication_under_test.make.o \
	.sg_compound_publication_under_test.make.d
IDENTITY_TEST_BIN = sg_identity_test.gnu
IDENTITY_TEST_OBJS = .sg_identity_test.gnu.o .sg_identity_under_test.gnu.o \
	.sg_crc32_under_test.gnu.o
IDENTITY_TEST_DEPS = $(IDENTITY_TEST_OBJS:.o=.d)
RUNE_CODEC_TEST_BIN = sg_rune_codec_test.gnu
RUNE_CODEC_TEST_OBJS = .sg_rune_codec_test.gnu.o \
	.sg_rune_codec_under_test.gnu.o \
	.sg_rune_action_under_test.gnu.o \
	.sg_rune_crc_under_test.gnu.o
RUNE_CODEC_TEST_DEPS = $(RUNE_CODEC_TEST_OBJS:.o=.d)
RUNE_CODEC_TEST_ALL_ARTIFACTS = \
	sg_rune_codec_test.gnu sg_rune_codec_test.make \
	.sg_rune_codec_test.gnu.o .sg_rune_codec_test.gnu.d \
	.sg_rune_codec_under_test.gnu.o .sg_rune_codec_under_test.gnu.d \
	.sg_rune_codec_test.make.o .sg_rune_codec_test.make.d \
	.sg_rune_codec_under_test.make.o .sg_rune_codec_under_test.make.d
RUNE_ARTIFACT_LOADER_TEST_BIN = sg_rune_artifact_loader_test.gnu
RUNE_ARTIFACT_LOADER_TEST_OBJS = .sg_rune_artifact_loader_test.gnu.o \
	.sg_rune_artifact_loader_under_test.gnu.o \
	.sg_rune_codec_under_test.gnu.o \
	.sg_rune_action_under_test.gnu.o \
	.sg_rune_crc_under_test.gnu.o
RUNE_ARTIFACT_LOADER_TEST_DEPS = $(RUNE_ARTIFACT_LOADER_TEST_OBJS:.o=.d)
RUNE_ARTIFACT_LOADER_TEST_ALL_ARTIFACTS = \
	sg_rune_artifact_loader_test.gnu sg_rune_artifact_loader_test.make \
	.sg_rune_artifact_loader_test.gnu.o .sg_rune_artifact_loader_test.gnu.d \
	.sg_rune_artifact_loader_under_test.gnu.o .sg_rune_artifact_loader_under_test.gnu.d \
	.sg_rune_artifact_loader_test.make.o .sg_rune_artifact_loader_test.make.d \
	.sg_rune_artifact_loader_under_test.make.o .sg_rune_artifact_loader_under_test.make.d
RUNE_ARTIFACT_WRITER_TEST_BIN = sg_rune_artifact_writer_test.gnu
RUNE_ARTIFACT_WRITER_TEST_OBJS = .sg_rune_artifact_writer_test.gnu.o \
	.sg_rune_artifact_writer_under_test.gnu.o \
	.sg_rune_codec_under_test.gnu.o \
	.sg_rune_action_under_test.gnu.o \
	.sg_rune_crc_under_test.gnu.o
RUNE_ARTIFACT_WRITER_TEST_DEPS = $(RUNE_ARTIFACT_WRITER_TEST_OBJS:.o=.d)
RUNE_ARTIFACT_WRITER_TEST_ALL_ARTIFACTS = \
	sg_rune_artifact_writer_test.gnu sg_rune_artifact_writer_test.make \
	.sg_rune_artifact_writer_test.gnu.o .sg_rune_artifact_writer_test.gnu.d \
	.sg_rune_artifact_writer_under_test.gnu.o .sg_rune_artifact_writer_under_test.gnu.d \
	.sg_rune_artifact_writer_test.make.o .sg_rune_artifact_writer_test.make.d \
	.sg_rune_artifact_writer_under_test.make.o .sg_rune_artifact_writer_under_test.make.d
RUNE_MECHANISM_PLAN_TEST_BIN = sg_rune_mechanism_plan_test.gnu
RUNE_MECHANISM_PLAN_TEST_OBJS = .sg_rune_mechanism_plan_test.gnu.o \
	.sg_rune_mechanism_plan_under_test.gnu.o \
	.sg_rune_codec_under_test.gnu.o \
	.sg_rune_action_under_test.gnu.o \
	.sg_rune_crc_under_test.gnu.o
RUNE_MECHANISM_PLAN_TEST_DEPS = $(RUNE_MECHANISM_PLAN_TEST_OBJS:.o=.d)
RUNE_MECHANISM_PLAN_TEST_ALL_ARTIFACTS = \
	sg_rune_mechanism_plan_test.gnu sg_rune_mechanism_plan_test.make \
	.sg_rune_mechanism_plan_test.gnu.o .sg_rune_mechanism_plan_test.gnu.d \
	.sg_rune_mechanism_plan_under_test.gnu.o .sg_rune_mechanism_plan_under_test.gnu.d \
	.sg_rune_mechanism_plan_test.make.o .sg_rune_mechanism_plan_test.make.d \
	.sg_rune_mechanism_plan_under_test.make.o .sg_rune_mechanism_plan_under_test.make.d
RUNE_MECHANISM_CATALOG_TEST_BIN = sg_rune_mechanism_catalog_test.gnu
RUNE_MECHANISM_CATALOG_TEST_OBJS = .sg_rune_mechanism_catalog_test.gnu.o \
	.sg_rune_mechanism_catalog_under_test.gnu.o
RUNE_MECHANISM_CATALOG_TEST_DEPS = \
	$(RUNE_MECHANISM_CATALOG_TEST_OBJS:.o=.d)
RUNE_MECHANISM_CATALOG_TEST_ALL_ARTIFACTS = \
	sg_rune_mechanism_catalog_test.gnu sg_rune_mechanism_catalog_test.make \
	.sg_rune_mechanism_catalog_test.gnu.o \
	.sg_rune_mechanism_catalog_test.gnu.d \
	.sg_rune_mechanism_catalog_under_test.gnu.o \
	.sg_rune_mechanism_catalog_under_test.gnu.d \
	.sg_rune_mechanism_catalog_test.make.o \
	.sg_rune_mechanism_catalog_test.make.d \
	.sg_rune_mechanism_catalog_under_test.make.o \
	.sg_rune_mechanism_catalog_under_test.make.d
RUNE_MECHANISM_EXECUTION_TEST_BIN = sg_rune_mechanism_execution_test.gnu
RUNE_MECHANISM_EXECUTION_TEST_OBJS = \
	.sg_rune_mechanism_execution_test.gnu.o \
	.sg_rune_mechanism_catalog_under_test.gnu.o \
	.sg_rune_binding_under_test.gnu.o .sg_rune_runtime_under_test.gnu.o \
	.sg_rune_action_under_test.gnu.o .sg_rune_crc_under_test.gnu.o \
	.sg_door_approach_under_test.gnu.o .sg_mover_lease_under_test.gnu.o \
	.sg_delayed_relay_dispatch_move_under_test.gnu.o \
	.sg_delayed_relay_dispatch_util_under_test.gnu.o \
	.sg_delayed_relay_dispatch_view_under_test.gnu.o \
	.sg_delayed_relay_dispatch_utils_under_test.gnu.o \
	.sg_delayed_relay_dispatch_trigger_under_test.gnu.o \
	.sg_delayed_relay_dispatch_button_under_test.gnu.o \
	.sg_delayed_relay_dispatch_q_shared_under_test.gnu.o
RUNE_MECHANISM_EXECUTION_TEST_DEPS = \
	$(RUNE_MECHANISM_EXECUTION_TEST_OBJS:.o=.d)
RUNE_MECHANISM_EXECUTION_TEST_ALL_ARTIFACTS = \
	sg_rune_mechanism_execution_test.gnu sg_rune_mechanism_execution_test.make \
	.sg_rune_mechanism_execution_test.gnu.o \
	.sg_rune_mechanism_execution_test.gnu.d \
	.sg_rune_mechanism_execution_test.make.o \
	.sg_rune_mechanism_execution_test.make.d \
	$(foreach flavor,gnu make, \
	.sg_delayed_relay_dispatch_move_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_move_under_test.$(flavor).d \
	.sg_delayed_relay_dispatch_util_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_util_under_test.$(flavor).d \
	.sg_delayed_relay_dispatch_view_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_view_under_test.$(flavor).d \
	.sg_delayed_relay_dispatch_utils_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_utils_under_test.$(flavor).d \
	.sg_delayed_relay_dispatch_trigger_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_trigger_under_test.$(flavor).d \
	.sg_delayed_relay_dispatch_button_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_button_under_test.$(flavor).d \
	.sg_delayed_relay_dispatch_q_shared_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_q_shared_under_test.$(flavor).d)
RUNE_BINDING_TEST_BIN = sg_rune_binding_test.gnu
RUNE_BINDING_TEST_OBJS = .sg_rune_binding_test.gnu.o \
	.sg_rune_binding_under_test.gnu.o \
	.sg_rune_runtime_under_test.gnu.o \
	.sg_rune_action_under_test.gnu.o \
	.sg_rune_crc_under_test.gnu.o
RUNE_BINDING_TEST_DEPS = $(RUNE_BINDING_TEST_OBJS:.o=.d)
RUNE_BINDING_TEST_ALL_ARTIFACTS = \
	sg_rune_binding_test.gnu sg_rune_binding_test.make \
	.sg_rune_binding_test.gnu.o .sg_rune_binding_test.gnu.d \
	.sg_rune_binding_under_test.gnu.o .sg_rune_binding_under_test.gnu.d \
	.sg_rune_runtime_under_test.gnu.o .sg_rune_runtime_under_test.gnu.d \
	.sg_rune_binding_test.make.o .sg_rune_binding_test.make.d \
	.sg_rune_binding_under_test.make.o .sg_rune_binding_under_test.make.d \
	.sg_rune_runtime_under_test.make.o .sg_rune_runtime_under_test.make.d
RUNE_ACCEPT_BIN = runeaccept.gnu
RUNE_ACCEPT_OBJS = .runeaccept.gnu.o \
	.sg_rune_file_under_test.gnu.o \
	.sg_rune_artifact_loader_under_test.gnu.o \
	.sg_rune_codec_under_test.gnu.o \
	.sg_rune_action_under_test.gnu.o \
	.sg_rune_crc_under_test.gnu.o
RUNE_ACCEPT_DEPS = $(RUNE_ACCEPT_OBJS:.o=.d)
RUNE_ACCEPT_ALL_ARTIFACTS = \
	runeaccept.gnu runeaccept.make \
	.runeaccept.gnu.o .runeaccept.gnu.d \
	.sg_rune_file_under_test.gnu.o \
	.sg_rune_file_under_test.gnu.d \
	.runeaccept.make.o .runeaccept.make.d \
	.sg_rune_file_under_test.make.o \
	.sg_rune_file_under_test.make.d
SIDECAR_WIRE_TEST_BIN = sg_sidecar_wire_test.gnu
SIDECAR_WIRE_TEST_OBJS = .sg_sidecar_wire_test.gnu.o \
	.sg_sidecar_wire_under_test.gnu.o .sg_rune_crc_under_test.gnu.o
SIDECAR_WIRE_TEST_DEPS = $(SIDECAR_WIRE_TEST_OBJS:.o=.d)
SIDECAR_LOADER_TEST_BIN = sg_sidecar_loader_test.gnu
SIDECAR_LOADER_TEST_OBJS = .sg_sidecar_loader_test.gnu.o \
	.sg_sidecar_loader_under_test.gnu.o .sg_sidecar_wire_under_test.gnu.o \
	.sg_rune_crc_under_test.gnu.o
SIDECAR_LOADER_TEST_DEPS = $(SIDECAR_LOADER_TEST_OBJS:.o=.d)
SIDECAR_STORE_TEST_BIN = sg_sidecar_store_test.gnu
SIDECAR_STORE_TEST_OBJS = .sg_sidecar_store_test.gnu.o \
	.sg_sidecar_store_under_test.gnu.o .sg_sidecar_loader_under_test.gnu.o \
	.sg_sidecar_wire_under_test.gnu.o .sg_rune_crc_under_test.gnu.o
SIDECAR_STORE_TEST_DEPS = $(SIDECAR_STORE_TEST_OBJS:.o=.d)
DANGER_LEASE_TEST_BIN = sg_danger_lease_test.gnu
DANGER_LEASE_TEST_OBJS = .sg_danger_lease_test.gnu.o \
	.sg_danger_lease_under_test.gnu.o
DANGER_LEASE_TEST_DEPS = $(DANGER_LEASE_TEST_OBJS:.o=.d)
DANGER_POLICY_TEST_BIN = sg_danger_policy_test.gnu
DANGER_POLICY_TEST_OBJS = .sg_danger_policy_test.gnu.o \
	.sg_danger_policy_under_test.gnu.o
DANGER_POLICY_TEST_DEPS = $(DANGER_POLICY_TEST_OBJS:.o=.d)
DANGER_TEST_BIN = sg_danger_test.gnu
DANGER_TEST_OBJS = .sg_danger_test.gnu.o .sg_danger_under_test.gnu.o \
	.sg_rune_runtime_under_test.gnu.o
DANGER_TEST_DEPS = $(DANGER_TEST_OBJS:.o=.d)
FIELDS_CANDIDATE_TEST_BIN = sg_fields_candidate_test.gnu
FIELDS_CANDIDATE_TEST_OBJS = .sg_fields_candidate_test.gnu.o \
	.sg_fields_candidate_under_test.gnu.o \
	.sg_caco_projection_under_test.gnu.o \
	.sg_goal_projection_under_test.gnu.o \
	.sg_snag_repair_under_test.gnu.o \
	.sg_rune_file_sha_under_test.gnu.o
FIELDS_CANDIDATE_TEST_DEPS = $(FIELDS_CANDIDATE_TEST_OBJS:.o=.d)
SNAG_REPAIR_TEST_BIN = sg_snag_repair_test.gnu
SNAG_REPAIR_TEST_OBJS = .sg_snag_repair_test.gnu.o \
	.sg_fields_candidate_under_test.gnu.o .sg_snag_repair_under_test.gnu.o \
	.sg_rune_file_sha_under_test.gnu.o
SNAG_REPAIR_TEST_DEPS = $(SNAG_REPAIR_TEST_OBJS:.o=.d)
SNAG_REPAIR_PYTHON_TEST = tests/test_snagrepair.py
SNAG_REPAIR_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_snag_repair_test.$(flavor) \
	.sg_snag_repair_test.$(flavor).o .sg_snag_repair_test.$(flavor).d \
	.sg_snag_repair_under_test.$(flavor).o \
	.sg_snag_repair_under_test.$(flavor).d \
	.sg_rune_file_sha_under_test.$(flavor).o \
	.sg_rune_file_sha_under_test.$(flavor).d)
SPECTATOR_SOUND_TEST_BIN = sg_spectator_sound_test.gnu
SPECTATOR_SOUND_TEST_OBJS = .sg_spectator_sound_test.gnu.o \
	.sg_spectator_sound_net_under_test.gnu.o
SPECTATOR_SOUND_TEST_DEPS = $(SPECTATOR_SOUND_TEST_OBJS:.o=.d)
SPECTATOR_SOUND_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_spectator_sound_test.$(flavor) \
	.sg_spectator_sound_test.$(flavor).o \
	.sg_spectator_sound_test.$(flavor).d \
	.sg_spectator_sound_net_under_test.$(flavor).o \
	.sg_spectator_sound_net_under_test.$(flavor).d)
HUMAN_SPEED_TEST_BIN = sg_human_speed_test.gnu
HUMAN_SPEED_TEST_OBJS = .sg_human_speed_test.gnu.o \
	.sg_human_speed_under_test.gnu.o \
	.sg_human_speed_pmove_under_test.gnu.o \
	.sg_human_speed_q_shared_under_test.gnu.o
HUMAN_SPEED_TEST_DEPS = $(HUMAN_SPEED_TEST_OBJS:.o=.d)
HUMAN_SPEED_INTEGRATION_TEST = tests/test_human_speed_integration.py
HUMAN_SPEED_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_human_speed_test.$(flavor) \
	.sg_human_speed_test.$(flavor).o .sg_human_speed_test.$(flavor).d \
	.sg_human_speed_under_test.$(flavor).o \
	.sg_human_speed_under_test.$(flavor).d \
	.sg_human_speed_pmove_under_test.$(flavor).o \
	.sg_human_speed_pmove_under_test.$(flavor).d \
	.sg_human_speed_q_shared_under_test.$(flavor).o \
	.sg_human_speed_q_shared_under_test.$(flavor).d)
DOOR_APPROACH_TEST_BIN = sg_door_approach_test.gnu
DOOR_APPROACH_TEST_OBJS = .sg_door_approach_test.gnu.o \
	.sg_door_approach_under_test.gnu.o
DOOR_APPROACH_TEST_DEPS = $(DOOR_APPROACH_TEST_OBJS:.o=.d)
DOOR_APPROACH_INTEGRATION_TEST = tests/test_door_approach_integration.py
DOOR_APPROACH_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_door_approach_test.$(flavor) \
	.sg_door_approach_test.$(flavor).o \
	.sg_door_approach_test.$(flavor).d \
	.sg_door_approach_under_test.$(flavor).o \
	.sg_door_approach_under_test.$(flavor).d)
DEFENSE_SHIFT_TEST_BIN = sg_defense_shift_test.gnu
DEFENSE_SHIFT_TEST_OBJS = .sg_defense_shift_test.gnu.o \
	.sg_defense_shift_under_test.gnu.o
DEFENSE_SHIFT_TEST_DEPS = $(DEFENSE_SHIFT_TEST_OBJS:.o=.d)
DEFENSE_SHIFT_INTEGRATION_TEST = tests/test_defense_shift_integration.py
DEFENSE_COMBAT_INTEGRATION_TEST = tests/test_defense_combat_integration.py
DEFENSE_SHIFT_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_defense_shift_test.$(flavor) \
	.sg_defense_shift_test.$(flavor).o .sg_defense_shift_test.$(flavor).d \
	.sg_defense_shift_under_test.$(flavor).o \
	.sg_defense_shift_under_test.$(flavor).d)
DEFENSE_SUPPLY_TEST_BIN = sg_defense_supply_test.gnu
DEFENSE_SUPPLY_TEST_OBJS = .sg_defense_supply_test.gnu.o \
	.sg_defense_supply_under_test.gnu.o
DEFENSE_SUPPLY_TEST_DEPS = $(DEFENSE_SUPPLY_TEST_OBJS:.o=.d)
DEFENSE_SUPPLY_INTEGRATION_TEST = tests/test_defender_supply_integration.py
DEFENSE_SUPPLY_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_defense_supply_test.$(flavor) \
	.sg_defense_supply_test.$(flavor).o .sg_defense_supply_test.$(flavor).d \
	.sg_defense_supply_under_test.$(flavor).o \
	.sg_defense_supply_under_test.$(flavor).d)
STRIKE_ADAPTER_TEST_BIN = sg_strike_adapter_test.gnu
STRIKE_ADAPTER_TEST_OBJS = .sg_strike_adapter_test.gnu.o \
	.sg_strike_under_test.gnu.o .sg_strike_adapter_under_test.gnu.o
STRIKE_ADAPTER_TEST_DEPS = $(STRIKE_ADAPTER_TEST_OBJS:.o=.d)
STRIKE_ADAPTER_INTEGRATION_TEST = tests/test_strike_integration.py
STRIKE_ADAPTER_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_strike_adapter_test.$(flavor) \
	.sg_strike_adapter_test.$(flavor).o .sg_strike_adapter_test.$(flavor).d \
	.sg_strike_under_test.$(flavor).o .sg_strike_under_test.$(flavor).d \
	.sg_strike_adapter_under_test.$(flavor).o \
	.sg_strike_adapter_under_test.$(flavor).d)
ITEM_COMMITMENT_TEST_BIN = sg_item_commitment_test.gnu
ITEM_COMMITMENT_TEST_OBJS = .sg_item_commitment_test.gnu.o \
	.sg_item_commitment_under_test.gnu.o slipgate/sg_pickup_target.o
ITEM_COMMITMENT_TEST_DEPS = $(ITEM_COMMITMENT_TEST_OBJS:.o=.d)
ITEM_COMMITMENT_INTEGRATION_TEST = tests/test_item_commitment_integration.py
ITEM_COMMITMENT_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_item_commitment_test.$(flavor) \
	.sg_item_commitment_test.$(flavor).o .sg_item_commitment_test.$(flavor).d \
	.sg_item_commitment_under_test.$(flavor).o \
	.sg_item_commitment_under_test.$(flavor).d)
HOOK_DIAGNOSTICS_TEST_BIN = sg_hook_diagnostics_test.gnu
HOOK_DIAGNOSTICS_TEST_OBJS = .sg_hook_diagnostics_test.gnu.o \
	.sg_hook_diagnostics_under_test.gnu.o
HOOK_DIAGNOSTICS_TEST_DEPS = $(HOOK_DIAGNOSTICS_TEST_OBJS:.o=.d)
HOOK_DIAGNOSTICS_INTEGRATION_TEST = tests/test_hook_diagnostics_integration.py
HOOK_DIAGNOSTICS_CONSUMER_TEST = tests/test_hook_diagnostic_consumers.py
ROLE_TELEMETRY_CONSUMER_TEST = tests/test_role_telemetry_consumers.py
HOOK_EVENTS_TEST = tests/test_hookevents.py
HOOK_DIAGNOSTICS_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_hook_diagnostics_test.$(flavor) \
	.sg_hook_diagnostics_test.$(flavor).o \
	.sg_hook_diagnostics_test.$(flavor).d \
	.sg_hook_diagnostics_under_test.$(flavor).o \
	.sg_hook_diagnostics_under_test.$(flavor).d)
RUN_HANDOFF_TEST_BIN = sg_run_handoff_test.gnu
RUN_HANDOFF_TEST_OBJS = .sg_run_handoff_test.gnu.o \
	.sg_run_handoff_descend_under_test.gnu.o \
	.sg_run_handoff_pmove_under_test.gnu.o \
	.sg_run_handoff_q_shared_under_test.gnu.o
RUN_HANDOFF_TEST_DEPS = $(RUN_HANDOFF_TEST_OBJS:.o=.d)
RUN_HANDOFF_INTEGRATION_TEST = tests/test_run_handoff_integration.py
RUN_HANDOFF_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_run_handoff_test.$(flavor) \
	.sg_run_handoff_test.$(flavor).o .sg_run_handoff_test.$(flavor).d \
	.sg_run_handoff_descend_under_test.$(flavor).o \
	.sg_run_handoff_descend_under_test.$(flavor).d \
	.sg_run_handoff_pmove_under_test.$(flavor).o \
	.sg_run_handoff_pmove_under_test.$(flavor).d \
	.sg_run_handoff_q_shared_under_test.$(flavor).o \
	.sg_run_handoff_q_shared_under_test.$(flavor).d)
RUNE_INSTALL_TEST_BIN = sg_rune_install_test.gnu
RUNE_INSTALL_TEST_OBJS = .sg_rune_install_test.gnu.o \
	.sg_rune_install_under_test.gnu.o .sg_rune_stream_under_test.gnu.o \
	.sg_rune_artifact_writer_under_test.gnu.o \
	.sg_rune_codec_under_test.gnu.o .sg_rune_action_under_test.gnu.o \
	.sg_rune_crc_under_test.gnu.o
RUNE_INSTALL_TEST_DEPS = $(RUNE_INSTALL_TEST_OBJS:.o=.d)
RUNE_PROOF_TEST_BIN = sg_rune_proof_test.gnu
RUNE_PROOF_TEST_OBJS = .sg_rune_proof_test.gnu.o \
	.sg_rune_proof_under_test.gnu.o
RUNE_PROOF_TEST_DEPS = $(RUNE_PROOF_TEST_OBJS:.o=.d)
RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN = sg_rune_objective_diagnostics_test.gnu
RUNE_OBJECTIVE_DIAGNOSTICS_TEST_OBJS = \
	.sg_rune_objective_diagnostics_test.gnu.o
RUNE_OBJECTIVE_DIAGNOSTICS_TEST_DEPS = \
	$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_OBJS:.o=.d)
RUNE_OBJECTIVE_DIAGNOSTICS_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_rune_objective_diagnostics_test.$(flavor) \
	.sg_rune_objective_diagnostics_test.$(flavor).o \
	.sg_rune_objective_diagnostics_test.$(flavor).d)
REPLAY_TEST_BIN = sg_replay_test.gnu
REPLAY_TEST_OBJS = .sg_replay_test.gnu.o .sg_replay_under_test.gnu.o
REPLAY_TEST_DEPS = $(REPLAY_TEST_OBJS:.o=.d)
DROP_LIVE_TEST_BIN = sg_drop_live_test.gnu
DROP_LIVE_TEST_OBJS = .sg_drop_live_test.gnu.o \
	.sg_drop_live_under_test.gnu.o .sg_drop_live_replay_under_test.gnu.o
DROP_LIVE_TEST_DEPS = $(DROP_LIVE_TEST_OBJS:.o=.d)
SWIM_LIVE_TEST_BIN = sg_swim_live_test.gnu
SWIM_LIVE_TEST_OBJS = .sg_swim_live_test.gnu.o \
	.sg_swim_live_under_test.gnu.o .sg_swim_live_replay_under_test.gnu.o
SWIM_LIVE_TEST_DEPS = $(SWIM_LIVE_TEST_OBJS:.o=.d)
COMPOUND_SWIM_LIVE_TEST_BIN = sg_compound_swim_live_test.gnu
COMPOUND_SWIM_LIVE_TEST_OBJS = .sg_compound_swim_live_test.gnu.o \
	.sg_compound_swim_live_under_test.gnu.o \
	.sg_compound_swim_live_compound_under_test.gnu.o \
	.sg_compound_swim_live_action_under_test.gnu.o \
	.sg_compound_swim_live_replay_under_test.gnu.o
COMPOUND_SWIM_LIVE_TEST_DEPS = $(COMPOUND_SWIM_LIVE_TEST_OBJS:.o=.d)
COMPOUND_SWIM_LIVE_INTEGRATION_TEST = \
	tests/test_compound_swim_live_integration.py
COMPOUND_SWIM_LIVE_TEST_ALL_ARTIFACTS = \
	sg_compound_swim_live_test.gnu sg_compound_swim_live_test.make \
	.sg_compound_swim_live_test.gnu.o \
	.sg_compound_swim_live_test.gnu.d \
	.sg_compound_swim_live_under_test.gnu.o \
	.sg_compound_swim_live_under_test.gnu.d \
	.sg_compound_swim_live_compound_under_test.gnu.o \
	.sg_compound_swim_live_compound_under_test.gnu.d \
	.sg_compound_swim_live_action_under_test.gnu.o \
	.sg_compound_swim_live_action_under_test.gnu.d \
	.sg_compound_swim_live_replay_under_test.gnu.o \
	.sg_compound_swim_live_replay_under_test.gnu.d \
	.sg_compound_swim_live_test.make.o \
	.sg_compound_swim_live_test.make.d \
	.sg_compound_swim_live_under_test.make.o \
	.sg_compound_swim_live_under_test.make.d \
	.sg_compound_swim_live_compound_under_test.make.o \
	.sg_compound_swim_live_compound_under_test.make.d \
	.sg_compound_swim_live_action_under_test.make.o \
	.sg_compound_swim_live_action_under_test.make.d \
	.sg_compound_swim_live_replay_under_test.make.o \
	.sg_compound_swim_live_replay_under_test.make.d
HOOK_LIVE_TEST_BIN = sg_hook_live_test.gnu
HOOK_LIVE_TEST_OBJS = .sg_hook_live_test.gnu.o \
	.sg_hook_live_under_test.gnu.o .sg_hook_live_replay_under_test.gnu.o
HOOK_LIVE_TEST_DEPS = $(HOOK_LIVE_TEST_OBJS:.o=.d)
HOOK_INTEGRATION_TEST = tests/test_hook_live_integration.py
HOOK_DISCIPLINE_TEST_BIN = sg_hook_discipline_test.gnu
HOOK_DISCIPLINE_TEST_OBJS = .sg_hook_discipline_test.gnu.o \
	.sg_hook_discipline_under_test.gnu.o
HOOK_DISCIPLINE_TEST_DEPS = $(HOOK_DISCIPLINE_TEST_OBJS:.o=.d)
HOOK_DISCIPLINE_TEST_ALL_ARTIFACTS = \
	sg_hook_discipline_test.gnu sg_hook_discipline_test.make \
	.sg_hook_discipline_test.gnu.o .sg_hook_discipline_test.gnu.d \
	.sg_hook_discipline_under_test.gnu.o \
	.sg_hook_discipline_under_test.gnu.d \
	.sg_hook_discipline_test.make.o .sg_hook_discipline_test.make.d \
	.sg_hook_discipline_under_test.make.o \
	.sg_hook_discipline_under_test.make.d
RUNE_NAMING_TEST = tests/test_rune_naming.py
RELEASE_WORKFLOW_TEST = tests/test_release_workflow.py
DESLOP_AUDIT = tools/deslop_audit.py
DESLOP_AUDIT_TEST = tests/test_deslop_audit.py
SOURCE_SIZE_BUDGET = tools/source-size-budget.json
RUNE_PYTHON_TESTS = tests/test_rune_contracts.py \
	tests/test_rune_artifact.py \
	tests/test_sidecario.py \
	tests/test_rune_tool_readers.py
RUNGEN_TEST = tests/test_runegen_gate.py
RUNE_CORPUS_CONTROLLER_TEST = tests/test_rune_corpus_controller.py
BSPMECHANISMS_TEST = tests/test_bspmechanisms.py
WAVELOOP_PROCESS_TEST = tests/test_waveloop_process_scope.py
TEMP_FLAG_DIAGNOSTIC_TEST = tests/test_no_temp_flag_diagnostics.py
CARRIER_RETURN_TEST = tests/test_carrier_return_progress.py
COMBAT_AIM_TEST = tests/test_combat_aim_envelope.py
OFFENSE_FLAG_PICKUP_TEST = tests/test_offense_flag_pickup_recovery.py
ROTATOR_SWEEP_TEST_BIN = sg_rotator_sweep_test.gnu
ROTATOR_SWEEP_TEST_OBJS = .sg_rotator_sweep_test.gnu.o .sg_rotator_sweep_under_test.gnu.o \
	.sg_rotator_sweep_q_shared_under_test.gnu.o
ROTATOR_SWEEP_TEST_DEPS = $(ROTATOR_SWEEP_TEST_OBJS:.o=.d)
MOVER_SUBJECT_SWEEP_TEST_BIN = sg_mover_subject_sweep_test.gnu
MOVER_SUBJECT_SWEEP_TEST_OBJS = \
	.sg_mover_subject_sweep_test.gnu.o \
	.sg_mover_subject_sweep_oracle_under_test.gnu.o \
	.sg_door_approach_under_test.gnu.o \
	.sg_mover_subject_sweep_util_under_test.gnu.o \
	.sg_mover_subject_sweep_view_under_test.gnu.o \
	.sg_mover_subject_sweep_q_shared_under_test.gnu.o
MOVER_SUBJECT_SWEEP_TEST_DEPS = \
	$(MOVER_SUBJECT_SWEEP_TEST_OBJS:.o=.d)
MOVER_SUBJECT_SWEEP_TEST_ALL_ARTIFACTS = \
	sg_mover_subject_sweep_test.gnu sg_mover_subject_sweep_test.make \
	.sg_mover_subject_sweep_test.gnu.o \
	.sg_mover_subject_sweep_test.gnu.d \
	.sg_mover_subject_sweep_oracle_under_test.gnu.o \
	.sg_mover_subject_sweep_oracle_under_test.gnu.d \
	.sg_mover_subject_sweep_util_under_test.gnu.o \
	.sg_mover_subject_sweep_util_under_test.gnu.d \
	.sg_mover_subject_sweep_view_under_test.gnu.o \
	.sg_mover_subject_sweep_view_under_test.gnu.d \
	.sg_mover_subject_sweep_q_shared_under_test.gnu.o \
	.sg_mover_subject_sweep_q_shared_under_test.gnu.d \
	.sg_mover_subject_sweep_test.make.o \
	.sg_mover_subject_sweep_test.make.d \
	.sg_mover_subject_sweep_oracle_under_test.make.o \
	.sg_mover_subject_sweep_oracle_under_test.make.d \
	.sg_mover_subject_sweep_util_under_test.make.o \
	.sg_mover_subject_sweep_util_under_test.make.d \
	.sg_mover_subject_sweep_view_under_test.make.o \
	.sg_mover_subject_sweep_view_under_test.make.d \
	.sg_mover_subject_sweep_q_shared_under_test.make.o \
	.sg_mover_subject_sweep_q_shared_under_test.make.d
COMPOUND_SWIM_ORACLE_TEST_BIN = sg_compound_swim_oracle_test.gnu
COMPOUND_SWIM_ORACLE_TEST_OBJS = \
	.sg_compound_swim_oracle_test.gnu.o \
	.sg_compound_swim_oracle_oracle_under_test.gnu.o \
	.sg_compound_swim_oracle_rune_timing_under_test.gnu.o \
	.sg_compound_swim_oracle_replay_under_test.gnu.o \
	.sg_compound_swim_oracle_compound_under_test.gnu.o \
	.sg_compound_swim_oracle_world_under_test.gnu.o \
	.sg_compound_swim_oracle_q_shared_under_test.gnu.o
COMPOUND_SWIM_ORACLE_TEST_DEPS = \
	$(COMPOUND_SWIM_ORACLE_TEST_OBJS:.o=.d)
COMPOUND_SWIM_ORACLE_TEST_ALL_ARTIFACTS = \
	sg_compound_swim_oracle_test.gnu sg_compound_swim_oracle_test.make \
	.sg_compound_swim_oracle_test.gnu.o \
	.sg_compound_swim_oracle_test.gnu.d \
	.sg_compound_swim_oracle_oracle_under_test.gnu.o \
	.sg_compound_swim_oracle_oracle_under_test.gnu.d \
	.sg_compound_swim_oracle_rune_timing_under_test.gnu.o \
	.sg_compound_swim_oracle_rune_timing_under_test.gnu.d \
	.sg_compound_swim_oracle_replay_under_test.gnu.o \
	.sg_compound_swim_oracle_replay_under_test.gnu.d \
	.sg_compound_swim_oracle_compound_under_test.gnu.o \
	.sg_compound_swim_oracle_compound_under_test.gnu.d \
	.sg_compound_swim_oracle_world_under_test.gnu.o \
	.sg_compound_swim_oracle_world_under_test.gnu.d \
	.sg_compound_swim_oracle_q_shared_under_test.gnu.o \
	.sg_compound_swim_oracle_q_shared_under_test.gnu.d \
	.sg_compound_swim_oracle_test.make.o \
	.sg_compound_swim_oracle_test.make.d \
	.sg_compound_swim_oracle_oracle_under_test.make.o \
	.sg_compound_swim_oracle_oracle_under_test.make.d \
	.sg_compound_swim_oracle_rune_timing_under_test.make.o \
	.sg_compound_swim_oracle_rune_timing_under_test.make.d \
	.sg_compound_swim_oracle_replay_under_test.make.o \
	.sg_compound_swim_oracle_replay_under_test.make.d \
	.sg_compound_swim_oracle_compound_under_test.make.o \
	.sg_compound_swim_oracle_compound_under_test.make.d \
	.sg_compound_swim_oracle_world_under_test.make.o \
	.sg_compound_swim_oracle_world_under_test.make.d \
	.sg_compound_swim_oracle_q_shared_under_test.make.o \
	.sg_compound_swim_oracle_q_shared_under_test.make.d
RUNE_DOOR_SCOPE_TEST_BIN = sg_rune_door_scope_test.gnu
RUNE_DOOR_SCOPE_TEST_OBJS = .sg_rune_door_scope_test.gnu.o \
	.sg_rune_door_scope_under_test.gnu.o
RUNE_DOOR_SCOPE_TEST_DEPS = $(RUNE_DOOR_SCOPE_TEST_OBJS:.o=.d)
RUNE_DOOR_SCOPE_TEST_ALL_ARTIFACTS = \
	sg_rune_door_scope_test.gnu sg_rune_door_scope_test.make \
	.sg_rune_door_scope_test.gnu.o .sg_rune_door_scope_test.gnu.d \
	.sg_rune_door_scope_under_test.gnu.o \
	.sg_rune_door_scope_under_test.gnu.d \
	.sg_rune_door_scope_test.make.o .sg_rune_door_scope_test.make.d \
	.sg_rune_door_scope_under_test.make.o \
	.sg_rune_door_scope_under_test.make.d
ENTFILE_TEST_BIN = g_entfile_path_test.gnu
ENTFILE_TEST_OBJS = .g_entfile_path_test.gnu.o
ENTFILE_TEST_DEPS = $(ENTFILE_TEST_OBJS:.o=.d)
MAPLIST_ROTATION_TEST_BIN = maplist_rotation_test.gnu
MAPLIST_ROTATION_TEST_ALL_ARTIFACTS = \
	maplist_rotation_test.gnu maplist_rotation_test.make
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
	sg_compound_test.gnu sg_compound_test.make \
	.sg_compound_test.gnu.o .sg_compound_test.gnu.d \
	.sg_compound_under_test.gnu.o .sg_compound_under_test.gnu.d \
	.sg_compound_action_under_test.gnu.o \
	.sg_compound_action_under_test.gnu.d \
	.sg_compound_test.make.o .sg_compound_test.make.d \
	.sg_compound_under_test.make.o .sg_compound_under_test.make.d \
	.sg_compound_action_under_test.make.o \
	.sg_compound_action_under_test.make.d \
	sg_compound_world_test.gnu sg_compound_world_test.make \
	.sg_compound_world_test.gnu.o .sg_compound_world_test.gnu.d \
	.sg_compound_world_under_test.gnu.o \
	.sg_compound_world_under_test.gnu.d \
	.sg_compound_world_q_shared_under_test.gnu.o \
	.sg_compound_world_q_shared_under_test.gnu.d \
	.sg_compound_world_test.make.o .sg_compound_world_test.make.d \
	.sg_compound_world_under_test.make.o \
	.sg_compound_world_under_test.make.d \
	.sg_compound_world_q_shared_under_test.make.o \
	.sg_compound_world_q_shared_under_test.make.d \
	sg_identity_test sg_identity_test.gnu sg_identity_test.make \
	.sg_identity_test.gnu.o .sg_identity_test.gnu.d \
	.sg_identity_under_test.gnu.o .sg_identity_under_test.gnu.d \
	.sg_crc32_under_test.gnu.o .sg_crc32_under_test.gnu.d \
	.sg_identity_test.make.o .sg_identity_test.make.d \
	.sg_identity_under_test.make.o .sg_identity_under_test.make.d \
	.sg_crc32_under_test.make.o .sg_crc32_under_test.make.d \
	.sg_rune_action_under_test.gnu.o \
	.sg_rune_action_under_test.gnu.d \
	.sg_rune_crc_under_test.gnu.o \
	.sg_rune_crc_under_test.gnu.d \
	.sg_rune_action_under_test.make.o \
	.sg_rune_action_under_test.make.d \
	.sg_rune_crc_under_test.make.o \
	.sg_rune_crc_under_test.make.d \
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
	sg_danger_test.gnu sg_danger_test.make \
	.sg_danger_test.gnu.o .sg_danger_test.gnu.d \
	.sg_danger_under_test.gnu.o .sg_danger_under_test.gnu.d \
	.sg_rune_runtime_under_test.gnu.o .sg_rune_runtime_under_test.gnu.d \
	.sg_danger_test.make.o .sg_danger_test.make.d \
	.sg_danger_under_test.make.o .sg_danger_under_test.make.d \
	sg_fields_candidate_test.gnu sg_fields_candidate_test.make \
	.sg_fields_candidate_test.gnu.o .sg_fields_candidate_test.gnu.d \
	.sg_fields_candidate_under_test.gnu.o .sg_fields_candidate_under_test.gnu.d \
	.sg_caco_projection_under_test.gnu.o .sg_caco_projection_under_test.gnu.d \
	.sg_goal_projection_under_test.gnu.o .sg_goal_projection_under_test.gnu.d \
	.sg_fields_candidate_test.make.o .sg_fields_candidate_test.make.d \
	.sg_fields_candidate_under_test.make.o .sg_fields_candidate_under_test.make.d \
	.sg_caco_projection_under_test.make.o .sg_caco_projection_under_test.make.d \
	.sg_goal_projection_under_test.make.o .sg_goal_projection_under_test.make.d \
	sg_rune_install_test.gnu sg_rune_install_test.make \
	.sg_rune_install_test.gnu.o .sg_rune_install_test.gnu.d \
	.sg_rune_install_under_test.gnu.o .sg_rune_install_under_test.gnu.d \
	.sg_rune_stream_under_test.gnu.o .sg_rune_stream_under_test.gnu.d \
	.sg_rune_install_test.make.o .sg_rune_install_test.make.d \
	.sg_rune_install_under_test.make.o .sg_rune_install_under_test.make.d \
	.sg_rune_stream_under_test.make.o .sg_rune_stream_under_test.make.d \
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
	sg_drop_live_test.gnu sg_drop_live_test.make \
	.sg_drop_live_test.gnu.o .sg_drop_live_test.gnu.d \
	.sg_drop_live_under_test.gnu.o .sg_drop_live_under_test.gnu.d \
	.sg_drop_live_replay_under_test.gnu.o \
	.sg_drop_live_replay_under_test.gnu.d \
	.sg_drop_live_test.make.o .sg_drop_live_test.make.d \
	.sg_drop_live_under_test.make.o .sg_drop_live_under_test.make.d \
	.sg_drop_live_replay_under_test.make.o \
	.sg_drop_live_replay_under_test.make.d \
	sg_swim_live_test.gnu sg_swim_live_test.make \
	.sg_swim_live_test.gnu.o .sg_swim_live_test.gnu.d \
	.sg_swim_live_under_test.gnu.o .sg_swim_live_under_test.gnu.d \
	.sg_swim_live_replay_under_test.gnu.o \
	.sg_swim_live_replay_under_test.gnu.d \
	.sg_swim_live_test.make.o .sg_swim_live_test.make.d \
	.sg_swim_live_under_test.make.o .sg_swim_live_under_test.make.d \
	.sg_swim_live_replay_under_test.make.o \
	.sg_swim_live_replay_under_test.make.d \
	sg_rotator_sweep_test.gnu sg_rotator_sweep_test.make \
	.sg_rotator_sweep_test.gnu.o .sg_rotator_sweep_test.gnu.d \
	.sg_rotator_sweep_under_test.gnu.o .sg_rotator_sweep_under_test.gnu.d \
	.sg_rotator_sweep_q_shared_under_test.gnu.o .sg_rotator_sweep_q_shared_under_test.gnu.d \
	.sg_rotator_sweep_test.make.o .sg_rotator_sweep_test.make.d \
	.sg_rotator_sweep_under_test.make.o .sg_rotator_sweep_under_test.make.d \
	.sg_rotator_sweep_q_shared_under_test.make.o .sg_rotator_sweep_q_shared_under_test.make.d \
	g_entfile_path_test.gnu g_entfile_path_test.make \
	.g_entfile_path_test.gnu.o .g_entfile_path_test.gnu.d \
	.g_entfile_path_test.make.o .g_entfile_path_test.make.d

# This is for native build
CFLAGS=-std=c11 -O3 -DARCH="$(ARCH)" -DSTDC_HEADERS -DVER='"$(VER)"'
CPPFLAGS += -I.
# This is for 32-bit build on 64-bit host
ifeq ($(ARCH),i386)
CFLAGS =-m32 -std=c11 -O3 -DARCH="$(ARCH)" -DSTDC_HEADERS -DVER='"$(VER)"' -I/usr/include
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
		 sg_action.o sg_crc32.o sg_identity.o slipgate/sg_rune_codec.o slipgate/sg_rune_artifact_loader.o slipgate/sg_rune_artifact_writer.o slipgate/sg_rune_file.o slipgate/sg_rune_stream.o slipgate/sg_rune_mechanism_catalog.o slipgate/sg_rune_mechanism_plan.o slipgate/sg_rune_runtime.o slipgate/sg_rune_binding.o sg_sidecar_wire.o sg_sidecar_loader.o sg_sidecar_store.o sg_rune_install.o sg_rune_proof.o sg_replay.o sg_compound.o slipgate/sg_mover_lease.o slipgate/sg_button_live.o slipgate/sg_compound_guard.o slipgate/sg_compound_guard_game.o slipgate/sg_declared_door_guard.o slipgate/sg_compound_world.o slipgate/sg_compound_gen.o slipgate/sg_compound_publication.o slipgate/sg_rune_door_scope.o sg_drop_live.o sg_swim_live.o sg_hook_live.o sg_oracle.o sg_rune.o sg_arach.o slipgate/sg_localization.o slipgate/sg_pickup_target.o sg_fields.o sg_caco.o sg_combat.o \
		 sg_cvars.o sg_hooks.o sg_util.o sg_client.o slipgate/sg_pov_identity.o slipgate/sg_human_speed.o slipgate/sg_door_approach.o slipgate/sg_defense_shift.o slipgate/sg_defense_supply.o slipgate/sg_strike.o slipgate/sg_strike_adapter.o slipgate/sg_hook_diagnostics.o slipgate/sg_snag_repair.o sg_clock.o sg_danger.o sg_danger_lease.o sg_danger_policy.o sg_weights.o sg_tilt.o sg_lead.o sg_move.o slipgate/sg_feeler_probe.o sg_price.o sg_descend.o slipgate/sg_traversal_transition.o sg_goal.o \
		 sg_chat.o sg_net.o sg_persona.o

######################################################################
# End of user-customizable section - you shouldn't have to touch
# anything below this point.
# MJD - With the exception of if you want massive debugging turned
# on or not...
######################################################################

# Game-related objects
G_OBJS = g_ai.o g_cmds.o g_combat.o g_func.o g_items.o g_main.o g_maplist.o \
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

POVLOCK_TEST_BIN = povlock_test.gnu
POVLOCK_TEST_OBJS = .povlock_test.gnu.o .povlock_under_test.gnu.o \
	.povlock_endframe_under_test.gnu.o
POVLOCK_TEST_DEPS = $(POVLOCK_TEST_OBJS:.o=.d)
POV_SESSION_TEST_BIN = pov_session_production_test.gnu
POV_SESSION_TEST_OBJS = .pov_session_production_test.gnu.o \
	.pov_session_chase_under_test.gnu.o \
	.pov_session_client_under_test.gnu.o \
	.pov_session_identity_under_test.gnu.o
POV_SESSION_TEST_DEPS = $(POV_SESSION_TEST_OBJS:.o=.d)
POVLOCK_DISPATCH_TEST = tests/test_povlock_dispatch.py
POV_SUPERVISOR_BIN = tools/pov-supervisor
POV_SUPERVISOR_TEST_BIN = pov_supervisor_unit.gnu
POV_SUPERVISOR_TEST = tests/test_pov_supervisor.py
POV_ITERATE_SELECTION_TEST = tests/test_iterate2_pov_selection.py
POV_SUPERVISOR_ALL_ARTIFACTS = tools/pov-supervisor pov_supervisor_unit.gnu \
	pov_supervisor_unit.make

.PHONY: all dep host-test action-test compound-test mover-lease-test \
	povlock-test pov-session-production-test pov-supervisor-test \
	button-live-test button-game-test \
	compound-guard-test compound-guard-game-test declared-door-guard-test \
	compound-world-test \
	compound-gen-test compound-publication-test \
	identity-test rune-codec-test rune-artifact-loader-test \
	rune-artifact-writer-test rune-mechanism-plan-test \
	rune-mechanism-catalog-test rune-mechanism-execution-test rune-binding-test \
	rune-accept-tool \
	rune-naming-test rune-artifact-test rune-corpus-controller-test \
	deslop-test \
	sidecar-wire-test sidecar-loader-test sidecar-store-test \
	danger-lease-test danger-policy-test danger-test fields-candidate-test snag-repair-test \
	spectator-sound-test human-speed-test defense-shift-test \
	door-approach-test \
	item-commitment-test hook-diagnostics-test \
	run-handoff-test \
	rune-install-test rune-proof-test rune-objective-diagnostics-test \
	replay-test hook-discipline-test \
	drop-live-test swim-live-test compound-swim-live-test rotator-sweep-test \
	mover-subject-sweep-test entfile-test maplist-rotation-test \
	compound-swim-oracle-test rune-door-scope-test \
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

slipgate/sg_compound_world.o: slipgate/sg_compound_world.c \
		slipgate/sg_compound_world.h slipgate/sg_util.h g_local.h
slipgate/sg_mover_lease.o: slipgate/sg_mover_lease.c \
		slipgate/sg_mover_lease.h
slipgate/sg_button_live.o: slipgate/sg_button_live.c \
		slipgate/sg_button_live.h slipgate/sg_mover_lease.h
slipgate/sg_compound_guard.o: slipgate/sg_compound_guard.c \
		slipgate/sg_compound_guard.h slipgate/sg_mover_lease.h
slipgate/sg_compound_guard_game.o: slipgate/sg_compound_guard_game.c \
		slipgate/sg_compound_guard_game.h slipgate/sg_compound_guard.h \
		slipgate/sg_mover_lease.h slipgate/sg_bot.h slipgate/sg_local.h \
		g_local.h
slipgate/sg_declared_door_guard.o: slipgate/sg_declared_door_guard.c \
		slipgate/sg_declared_door_guard.h slipgate/sg_compound_guard.h \
		slipgate/sg_mover_lease.h slipgate/sg_bot.h slipgate/sg_local.h \
		g_local.h
slipgate/sg_rune_runtime.o: slipgate/sg_rune_runtime.c \
		slipgate/sg_rune.h slipgate/sg_rune_contract.h q_shared.h
slipgate/sg_rune_binding.o: slipgate/sg_rune_binding.c \
		slipgate/sg_rune_binding.h slipgate/sg_rune.h slipgate/sg_crc32.h \
		slipgate/sg_rune_mechanism_catalog.h \
		slipgate/sg_action_contract.generated.h q_shared.h
slipgate/sg_rune_file.o: slipgate/sg_rune_file.c \
		slipgate/sg_rune_file.h slipgate/sg_rune_artifact_loader.h \
		slipgate/sg_rune_codec.h slipgate/sg_rune.h q_shared.h
slipgate/sg_rune_stream.o: slipgate/sg_rune_stream.c \
		slipgate/sg_rune_stream.h slipgate/sg_rune_artifact_writer.h \
		slipgate/sg_rune_codec.h slipgate/sg_rune.h q_shared.h
slipgate/sg_rune_mechanism_plan.o: slipgate/sg_rune_mechanism_plan.c \
		slipgate/sg_rune_mechanism_plan.h \
		slipgate/sg_rune_mechanism_catalog.h slipgate/sg_rune.h \
		slipgate/sg_crc32.h q_shared.h
slipgate/sg_compound_gen.o: slipgate/sg_compound_gen.c \
		slipgate/sg_compound_gen.h slipgate/sg_rune.h q_shared.h
slipgate/sg_compound_publication.o: slipgate/sg_compound_publication.c \
		slipgate/sg_compound_publication.h slipgate/sg_compound_world.h \
		slipgate/sg_local.h slipgate/sg_rune.h g_local.h
slipgate/sg_rune_door_scope.o: slipgate/sg_rune_door_scope.c \
		slipgate/sg_rune_door_scope.h

.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SHLIBCFLAGS) -o $@ -c $<

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

$(RUNE_CODEC_TEST_BIN): $(RUNE_CODEC_TEST_OBJS)
	$(CC) -o $@ $(RUNE_CODEC_TEST_OBJS) $(LDFLAGS)

$(RUNE_ARTIFACT_LOADER_TEST_BIN): $(RUNE_ARTIFACT_LOADER_TEST_OBJS)
	$(CC) -o $@ $(RUNE_ARTIFACT_LOADER_TEST_OBJS) $(LDFLAGS)

$(RUNE_ARTIFACT_WRITER_TEST_BIN): $(RUNE_ARTIFACT_WRITER_TEST_OBJS)
	$(CC) -o $@ $(RUNE_ARTIFACT_WRITER_TEST_OBJS) $(LDFLAGS)

$(RUNE_MECHANISM_PLAN_TEST_BIN): $(RUNE_MECHANISM_PLAN_TEST_OBJS)
	$(CC) -o $@ $(RUNE_MECHANISM_PLAN_TEST_OBJS) $(LDFLAGS)

$(RUNE_MECHANISM_CATALOG_TEST_BIN): $(RUNE_MECHANISM_CATALOG_TEST_OBJS)
	$(CC) -o $@ $(RUNE_MECHANISM_CATALOG_TEST_OBJS) $(LDFLAGS)

$(RUNE_MECHANISM_EXECUTION_TEST_BIN): $(RUNE_MECHANISM_EXECUTION_TEST_OBJS)
	$(CC) -Wl,--gc-sections -Wl,--wrap=G_UseTargets -o $@ \
		$(RUNE_MECHANISM_EXECUTION_TEST_OBJS) $(LDFLAGS)

$(RUNE_BINDING_TEST_BIN): $(RUNE_BINDING_TEST_OBJS)
	$(CC) -o $@ $(RUNE_BINDING_TEST_OBJS) $(LDFLAGS)

$(RUNE_ACCEPT_BIN): $(RUNE_ACCEPT_OBJS)
	$(CC) -o $@ $(RUNE_ACCEPT_OBJS) $(LDFLAGS)

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

$(DANGER_TEST_BIN): $(DANGER_TEST_OBJS)
	$(CC) -o $@ $(DANGER_TEST_OBJS) $(LDFLAGS)

$(FIELDS_CANDIDATE_TEST_BIN): $(FIELDS_CANDIDATE_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(FIELDS_CANDIDATE_TEST_OBJS) $(LDFLAGS)

$(SNAG_REPAIR_TEST_BIN): $(SNAG_REPAIR_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(SNAG_REPAIR_TEST_OBJS) $(LDFLAGS)

$(HUMAN_SPEED_TEST_BIN): $(HUMAN_SPEED_TEST_OBJS)
	$(CC) -o $@ $(HUMAN_SPEED_TEST_OBJS) $(LDFLAGS)

$(DOOR_APPROACH_TEST_BIN): $(DOOR_APPROACH_TEST_OBJS)
	$(CC) -o $@ $(DOOR_APPROACH_TEST_OBJS) $(LDFLAGS)

$(DEFENSE_SHIFT_TEST_BIN): $(DEFENSE_SHIFT_TEST_OBJS)
	$(CC) -o $@ $(DEFENSE_SHIFT_TEST_OBJS) $(LDFLAGS)

$(DEFENSE_SUPPLY_TEST_BIN): $(DEFENSE_SUPPLY_TEST_OBJS)
	$(CC) -o $@ $(DEFENSE_SUPPLY_TEST_OBJS) $(LDFLAGS)

$(STRIKE_ADAPTER_TEST_BIN): $(STRIKE_ADAPTER_TEST_OBJS)
	$(CC) -o $@ $(STRIKE_ADAPTER_TEST_OBJS) $(LDFLAGS)

$(ITEM_COMMITMENT_TEST_BIN): $(ITEM_COMMITMENT_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(ITEM_COMMITMENT_TEST_OBJS) $(LDFLAGS)

$(HOOK_DIAGNOSTICS_TEST_BIN): $(HOOK_DIAGNOSTICS_TEST_OBJS)
	$(CC) -o $@ $(HOOK_DIAGNOSTICS_TEST_OBJS) $(LDFLAGS)

$(RUN_HANDOFF_TEST_BIN): $(RUN_HANDOFF_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(RUN_HANDOFF_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_TEST_BIN): $(COMPOUND_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_TEST_OBJS) $(LDFLAGS)

$(MOVER_LEASE_TEST_BIN): $(MOVER_LEASE_TEST_OBJS)
	$(CC) -o $@ $(MOVER_LEASE_TEST_OBJS) $(LDFLAGS)

$(BUTTON_LIVE_TEST_BIN): $(BUTTON_LIVE_TEST_OBJS)
	$(CC) -o $@ $(BUTTON_LIVE_TEST_OBJS) $(LDFLAGS)

$(BUTTON_GAME_TEST_BIN): $(BUTTON_GAME_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(BUTTON_GAME_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_GUARD_TEST_BIN): $(COMPOUND_GUARD_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_GUARD_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_GUARD_GAME_TEST_BIN): $(COMPOUND_GUARD_GAME_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_GUARD_GAME_TEST_OBJS) $(LDFLAGS)

$(DECLARED_DOOR_GUARD_TEST_BIN): $(DECLARED_DOOR_GUARD_TEST_OBJS)
	$(CC) -o $@ $(DECLARED_DOOR_GUARD_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_WORLD_TEST_BIN): $(COMPOUND_WORLD_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(COMPOUND_WORLD_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_GEN_TEST_BIN): $(COMPOUND_GEN_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_GEN_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_PUBLICATION_TEST_BIN): $(COMPOUND_PUBLICATION_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_PUBLICATION_TEST_OBJS) $(LDFLAGS) -lm

$(RUNE_INSTALL_TEST_BIN): $(RUNE_INSTALL_TEST_OBJS)
	$(CC) -o $@ $(RUNE_INSTALL_TEST_OBJS) $(LDFLAGS)

$(RUNE_PROOF_TEST_BIN): $(RUNE_PROOF_TEST_OBJS)
	$(CC) -o $@ $(RUNE_PROOF_TEST_OBJS) $(LDFLAGS)

$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN): \
		$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ \
		$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_OBJS) $(LDFLAGS)

$(REPLAY_TEST_BIN): $(REPLAY_TEST_OBJS)
	$(CC) -o $@ $(REPLAY_TEST_OBJS) $(LDFLAGS)

$(DROP_LIVE_TEST_BIN): $(DROP_LIVE_TEST_OBJS)
	$(CC) -o $@ $(DROP_LIVE_TEST_OBJS) $(LDFLAGS)

$(SWIM_LIVE_TEST_BIN): $(SWIM_LIVE_TEST_OBJS)
	$(CC) -o $@ $(SWIM_LIVE_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_SWIM_LIVE_TEST_BIN): $(COMPOUND_SWIM_LIVE_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_SWIM_LIVE_TEST_OBJS) $(LDFLAGS)

$(HOOK_LIVE_TEST_BIN): $(HOOK_LIVE_TEST_OBJS)
	$(CC) -o $@ $(HOOK_LIVE_TEST_OBJS) $(LDFLAGS)

$(HOOK_DISCIPLINE_TEST_BIN): $(HOOK_DISCIPLINE_TEST_OBJS)
	$(CC) -o $@ $(HOOK_DISCIPLINE_TEST_OBJS) $(LDFLAGS)

$(ROTATOR_SWEEP_TEST_BIN): $(ROTATOR_SWEEP_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(ROTATOR_SWEEP_TEST_OBJS) $(LDFLAGS)

$(MOVER_SUBJECT_SWEEP_TEST_BIN): $(MOVER_SUBJECT_SWEEP_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ \
		$(MOVER_SUBJECT_SWEEP_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_SWIM_ORACLE_TEST_BIN): $(COMPOUND_SWIM_ORACLE_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_SWIM_ORACLE_TEST_OBJS) $(LDFLAGS)

$(RUNE_DOOR_SCOPE_TEST_BIN): $(RUNE_DOOR_SCOPE_TEST_OBJS)
	$(CC) -o $@ $(RUNE_DOOR_SCOPE_TEST_OBJS) $(LDFLAGS)

$(ENTFILE_TEST_BIN): $(ENTFILE_TEST_OBJS)
	$(CC) -o $@ $(ENTFILE_TEST_OBJS) $(LDFLAGS)

.sg_action_test.gnu.o: tests/sg_action_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_button_live_test.gnu.o: tests/sg_button_live_test.c \
		slipgate/sg_button_live.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_button_live_under_test.gnu.o: slipgate/sg_button_live.c \
		slipgate/sg_button_live.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_button_game_test.gnu.o: tests/sg_button_game_test.c \
		slipgate/sg_button_live.h slipgate/sg_move.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_button_game_live_under_test.gnu.o: slipgate/sg_button_live.c \
		slipgate/sg_button_live.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_button_game_move_under_test.gnu.o: slipgate/sg_move.c \
		slipgate/sg_button_live.h slipgate/sg_move.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_button_game_func_under_test.gnu.o: g_func.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-unused-parameter -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_button_game_q_shared_under_test.gnu.o: q_shared.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_action_under_test.gnu.o: slipgate/sg_action.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_test.gnu.o: tests/sg_compound_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_under_test.gnu.o: slipgate/sg_compound.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_action_under_test.gnu.o: slipgate/sg_action.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_lease_test.gnu.o: tests/sg_mover_lease_test.c \
		slipgate/sg_mover_lease.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_lease_under_test.gnu.o: slipgate/sg_mover_lease.c \
		slipgate/sg_mover_lease.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_guard_test.gnu.o: tests/sg_compound_guard_test.c \
		slipgate/sg_compound_guard.h slipgate/sg_mover_lease.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_guard_under_test.gnu.o: slipgate/sg_compound_guard.c \
		slipgate/sg_compound_guard.h slipgate/sg_mover_lease.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_guard_mover_lease_under_test.gnu.o: \
		slipgate/sg_mover_lease.c slipgate/sg_mover_lease.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_guard_game_test.gnu.o: \
		tests/sg_compound_guard_game_test.c \
		slipgate/sg_compound_guard_game.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -DSG_COMPOUND_GUARD_GAME_TEST -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_guard_game_under_test.gnu.o: \
		slipgate/sg_compound_guard_game.c \
		slipgate/sg_compound_guard_game.h slipgate/sg_compound_guard.h \
		slipgate/sg_mover_lease.h slipgate/sg_bot.h slipgate/sg_local.h \
		g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -DSG_COMPOUND_GUARD_GAME_TEST -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_declared_door_guard_test.gnu.o: \
		tests/sg_declared_door_guard_test.c \
		slipgate/sg_declared_door_guard.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_declared_door_guard_under_test.gnu.o: \
		slipgate/sg_declared_door_guard.c \
		slipgate/sg_declared_door_guard.h slipgate/sg_compound_guard.h \
		slipgate/sg_mover_lease.h slipgate/sg_bot.h slipgate/sg_local.h \
		g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_world_test.gnu.o: tests/sg_compound_world_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_world_under_test.gnu.o: slipgate/sg_compound_world.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_world_q_shared_under_test.gnu.o: q_shared.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_gen_test.gnu.o: tests/sg_compound_gen_test.c \
		slipgate/sg_compound_gen.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_gen_under_test.gnu.o: slipgate/sg_compound_gen.c \
		slipgate/sg_compound_gen.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_publication_test.gnu.o: \
		tests/sg_compound_publication_test.c \
		slipgate/sg_compound_publication.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_publication_under_test.gnu.o: \
		slipgate/sg_compound_publication.c \
		slipgate/sg_compound_publication.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_identity_test.gnu.o: tests/sg_identity_test.c slipgate/sg_chat.h \
		slipgate/sg_chat_random.h slipgate/sg_ear_random.h \
		slipgate/sg_route_jitter.h slipgate/sg_callout_policy.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_identity_under_test.gnu.o: slipgate/sg_identity.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_crc32_under_test.gnu.o: slipgate/sg_crc32.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_action_under_test.gnu.o: slipgate/sg_action.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_crc_under_test.gnu.o: slipgate/sg_crc32.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_codec_test.gnu.o: tests/sg_rune_codec_test.c \
		slipgate/sg_rune_codec.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_codec_under_test.gnu.o: slipgate/sg_rune_codec.c \
		slipgate/sg_rune_codec.h \
		slipgate/sg_action_contract.generated.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_artifact_loader_test.gnu.o: tests/sg_rune_artifact_loader_test.c \
		slipgate/sg_rune_artifact_loader.h slipgate/sg_rune_codec.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_artifact_loader_under_test.gnu.o: slipgate/sg_rune_artifact_loader.c \
		slipgate/sg_rune_artifact_loader.h slipgate/sg_rune_codec.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_artifact_writer_test.gnu.o: tests/sg_rune_artifact_writer_test.c \
		slipgate/sg_rune_artifact_writer.h slipgate/sg_rune_codec.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_artifact_writer_under_test.gnu.o: slipgate/sg_rune_artifact_writer.c \
		slipgate/sg_rune_artifact_writer.h slipgate/sg_rune_codec.h \
		slipgate/sg_crc32.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_mechanism_plan_test.gnu.o: tests/sg_rune_mechanism_plan_test.c \
		slipgate/sg_rune_mechanism_plan.h slipgate/sg_rune_codec.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_mechanism_plan_under_test.gnu.o: \
		slipgate/sg_rune_mechanism_plan.c \
		slipgate/sg_rune_mechanism_plan.h \
		slipgate/sg_rune_mechanism_catalog.h slipgate/sg_rune.h \
		slipgate/sg_crc32.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_mechanism_catalog_test.gnu.o: \
		tests/sg_rune_mechanism_catalog_test.c \
		slipgate/sg_rune_mechanism_catalog.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_mechanism_execution_test.gnu.o: \
		tests/sg_rune_mechanism_execution_test.c \
		slipgate/sg_rune_binding.h slipgate/sg_rune_mechanism_catalog.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_delayed_relay_dispatch_move_under_test.gnu.o: slipgate/sg_move.c \
		slipgate/sg_move.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_delayed_relay_dispatch_util_under_test.gnu.o: slipgate/sg_util.c \
		slipgate/sg_util.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_delayed_relay_dispatch_view_under_test.gnu.o: p_view.c \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -Wno-unused-parameter \
		-ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_delayed_relay_dispatch_utils_under_test.gnu.o: g_utils.c \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_delayed_relay_dispatch_trigger_under_test.gnu.o: g_trigger.c \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -Wno-unused-parameter \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_delayed_relay_dispatch_button_under_test.gnu.o: \
		slipgate/sg_button_live.c slipgate/sg_button_live.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_delayed_relay_dispatch_q_shared_under_test.gnu.o: q_shared.c \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_rune_mechanism_catalog_under_test.gnu.o: \
		slipgate/sg_rune_mechanism_catalog.c \
		slipgate/sg_rune_mechanism_catalog.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_binding_test.gnu.o: tests/sg_rune_binding_test.c \
		slipgate/sg_rune_binding.h slipgate/sg_rune.h \
		slipgate/sg_rune_mechanism_catalog.h slipgate/sg_crc32.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_binding_under_test.gnu.o: slipgate/sg_rune_binding.c \
		slipgate/sg_rune_binding.h slipgate/sg_rune.h \
		slipgate/sg_rune_mechanism_catalog.h slipgate/sg_crc32.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.runeaccept.gnu.o: tools/runeaccept.c slipgate/sg_rune_file.h \
		slipgate/sg_rune_codec.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_file_under_test.gnu.o: \
		slipgate/sg_rune_file.c slipgate/sg_rune_file.h \
		slipgate/sg_rune_artifact_loader.h slipgate/sg_rune.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_wire_test.gnu.o: tests/sg_sidecar_wire_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_wire_under_test.gnu.o: slipgate/sg_sidecar_wire.c \
		slipgate/sg_sidecar_wire.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_loader_test.gnu.o: tests/sg_sidecar_loader_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_loader_under_test.gnu.o: slipgate/sg_sidecar_loader.c \
		slipgate/sg_sidecar_loader.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_store_test.gnu.o: tests/sg_sidecar_store_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_store_under_test.gnu.o: slipgate/sg_sidecar_store.c \
		slipgate/sg_sidecar_store.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

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

.sg_danger_test.gnu.o: tests/sg_danger_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_under_test.gnu.o: slipgate/sg_danger.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_runtime_under_test.gnu.o: slipgate/sg_rune_runtime.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_fields_candidate_test.gnu.o: tests/sg_fields_candidate_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_fields_candidate_under_test.gnu.o: slipgate/sg_fields.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -DSG_FIELDS_TEST -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_snag_repair_test.gnu.o: tests/sg_snag_repair_test.c \
		slipgate/sg_snag_repair.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_snag_repair_under_test.gnu.o: slipgate/sg_snag_repair.c \
		slipgate/sg_snag_repair.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_file_sha_under_test.gnu.o: slipgate/sg_rune_file.c \
		slipgate/sg_rune_file.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_caco_projection_under_test.gnu.o: slipgate/sg_caco.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -DSG_CACO_TEST -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_goal_projection_under_test.gnu.o: slipgate/sg_goal.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -DSG_GOAL_TEST -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_human_speed_test.gnu.o: tests/sg_human_speed_test.c \
		slipgate/sg_human_speed.h tests/support/yq2_pmove.c \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_human_speed_under_test.gnu.o: slipgate/sg_human_speed.c \
		slipgate/sg_human_speed.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_human_speed_pmove_under_test.gnu.o: tests/support/yq2_pmove.c \
		q_shared.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -DDEDICATED_ONLY -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_human_speed_q_shared_under_test.gnu.o: q_shared.c q_shared.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_defense_shift_test.gnu.o: tests/sg_defense_shift_test.c \
		slipgate/sg_defense_shift.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_door_approach_test.gnu.o: tests/sg_door_approach_test.c \
		slipgate/sg_door_approach.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_door_approach_under_test.gnu.o: slipgate/sg_door_approach.c \
		slipgate/sg_door_approach.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_defense_shift_under_test.gnu.o: slipgate/sg_defense_shift.c \
		slipgate/sg_defense_shift.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_defense_supply_test.gnu.o: tests/sg_defense_supply_test.c \
		slipgate/sg_defense_supply.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_defense_supply_under_test.gnu.o: slipgate/sg_defense_supply.c \
		slipgate/sg_defense_supply.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_strike_adapter_test.gnu.o: tests/sg_strike_adapter_test.c \
		slipgate/sg_strike_adapter.h slipgate/sg_strike.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_strike_under_test.gnu.o: slipgate/sg_strike.c \
		slipgate/sg_strike.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_strike_adapter_under_test.gnu.o: slipgate/sg_strike_adapter.c \
		slipgate/sg_strike_adapter.h slipgate/sg_strike.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_item_commitment_test.gnu.o: tests/sg_item_commitment_test.c \
		slipgate/sg_lead.h slipgate/sg_rune_handoff_policy.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_item_commitment_under_test.gnu.o: slipgate/sg_lead.c \
		slipgate/sg_lead.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_diagnostics_test.gnu.o: tests/sg_hook_diagnostics_test.c \
		slipgate/sg_hook_diagnostics.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_diagnostics_under_test.gnu.o: slipgate/sg_hook_diagnostics.c \
		slipgate/sg_hook_diagnostics.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_run_handoff_test.gnu.o: tests/sg_run_handoff_test.c \
		slipgate/sg_descend.h tests/support/yq2_pmove.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_run_handoff_descend_under_test.gnu.o: slipgate/sg_descend.c \
		slipgate/sg_descend.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_run_handoff_pmove_under_test.gnu.o: tests/support/yq2_pmove.c \
		q_shared.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -DDEDICATED_ONLY \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_run_handoff_q_shared_under_test.gnu.o: q_shared.c q_shared.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_install_test.gnu.o: tests/sg_rune_install_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_install_under_test.gnu.o: slipgate/sg_rune_install.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_stream_under_test.gnu.o: slipgate/sg_rune_stream.c \
		slipgate/sg_rune_stream.h slipgate/sg_rune_artifact_writer.h \
		slipgate/sg_rune_codec.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_proof_test.gnu.o: tests/sg_rune_proof_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_proof_under_test.gnu.o: slipgate/sg_rune_proof.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_objective_diagnostics_test.gnu.o: \
		tests/sg_rune_objective_diagnostics_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_replay_test.gnu.o: tests/sg_replay_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_replay_under_test.gnu.o: slipgate/sg_replay.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_drop_live_test.gnu.o: tests/sg_drop_live_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_drop_live_under_test.gnu.o: slipgate/sg_drop_live.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_drop_live_replay_under_test.gnu.o: slipgate/sg_replay.c $(REVISION_HEADER)
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

.sg_compound_swim_live_test.gnu.o: \
		tests/sg_compound_swim_live_test.c \
		slipgate/sg_compound_swim_live.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_live_under_test.gnu.o: \
		slipgate/sg_compound_swim_live.c \
		slipgate/sg_compound_swim_live.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_live_compound_under_test.gnu.o: \
		slipgate/sg_compound.c slipgate/sg_compound.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_live_action_under_test.gnu.o: \
		slipgate/sg_action.c slipgate/sg_action.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_live_replay_under_test.gnu.o: \
		slipgate/sg_replay.c slipgate/sg_replay.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_live_test.gnu.o: tests/sg_hook_live_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_live_under_test.gnu.o: slipgate/sg_hook_live.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_live_replay_under_test.gnu.o: slipgate/sg_replay.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_discipline_test.gnu.o: tests/sg_hook_discipline_test.c \
		slipgate/sg_hook_discipline.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_discipline_under_test.gnu.o: slipgate/sg_hook_discipline.c \
		slipgate/sg_hook_discipline.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rotator_sweep_test.gnu.o: tests/sg_rotator_sweep_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rotator_sweep_under_test.gnu.o: slipgate/sg_oracle.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rotator_sweep_q_shared_under_test.gnu.o: q_shared.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_test.gnu.o: \
		tests/sg_mover_subject_sweep_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_oracle_under_test.gnu.o: \
		slipgate/sg_oracle.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_util_under_test.gnu.o: \
		slipgate/sg_util.c slipgate/sg_util.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections \
		-DSG_ImmutableSupport=SG_MoverSubjectSweepRealImmutableSupport \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_view_under_test.gnu.o: p_view.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -Wno-unused-parameter \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_q_shared_under_test.gnu.o: \
		q_shared.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_test.gnu.o: \
		tests/sg_compound_swim_oracle_test.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_oracle_under_test.gnu.o: \
		slipgate/sg_oracle.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_rune_timing_under_test.gnu.o: \
		slipgate/sg_rune.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -DSG_RUNE_TIMING_TEST -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_replay_under_test.gnu.o: \
		slipgate/sg_replay.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_compound_under_test.gnu.o: \
		slipgate/sg_compound.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_world_under_test.gnu.o: \
		slipgate/sg_compound_world.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_q_shared_under_test.gnu.o: \
		q_shared.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_door_scope_test.gnu.o: \
		tests/sg_rune_door_scope_test.c slipgate/sg_rune_door_scope.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_door_scope_under_test.gnu.o: \
		slipgate/sg_rune_door_scope.c slipgate/sg_rune_door_scope.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.g_entfile_path_test.gnu.o: tests/g_entfile_path_test.c g_entfile_path.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-pedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

$(MAPLIST_ROTATION_TEST_BIN): tests/maplist_rotation_test.c g_maplist.c \
		g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -Wpedantic -I. \
		-o $@ tests/maplist_rotation_test.c g_maplist.c $(LDFLAGS)

spectator-sound-test: $(SPECTATOR_SOUND_TEST_BIN)
	@echo "[TEST] $<"
	@./$(SPECTATOR_SOUND_TEST_BIN)

$(SPECTATOR_SOUND_TEST_BIN): $(SPECTATOR_SOUND_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(SPECTATOR_SOUND_TEST_OBJS) $(LDFLAGS)

.sg_spectator_sound_test.gnu.o: tests/sg_spectator_sound_test.c \
		slipgate/sg_sound_policy.h \
		g_local.h slipgate/sg_net.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_spectator_sound_net_under_test.gnu.o: slipgate/sg_net.c \
		g_local.h slipgate/sg_net.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

povlock-test: $(POVLOCK_TEST_BIN) $(POVLOCK_DISPATCH_TEST)
	@echo "[TEST] $<"
	@./$(POVLOCK_TEST_BIN)
	@python3 -B $(POVLOCK_DISPATCH_TEST)

$(POVLOCK_TEST_BIN): $(POVLOCK_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(POVLOCK_TEST_OBJS) $(LDFLAGS)

.povlock_test.gnu.o: tests/povlock_test.c g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.povlock_under_test.gnu.o: g_chase.c g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.povlock_endframe_under_test.gnu.o: p_view.c g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-unused-parameter -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

pov-session-production-test: $(POV_SESSION_TEST_BIN)
	@echo "[TEST] $<"
	@./$(POV_SESSION_TEST_BIN)

$(POV_SESSION_TEST_BIN): $(POV_SESSION_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(POV_SESSION_TEST_OBJS) $(LDFLAGS)

.pov_session_production_test.gnu.o: tests/pov_session_production_test.c \
		g_local.h g_tourney.h slipgate/sg_bot.h \
		slipgate/sg_pov_identity.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.pov_session_chase_under_test.gnu.o: g_chase.c g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.pov_session_client_under_test.gnu.o: p_client.c g_local.h g_tourney.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-unused-parameter -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.pov_session_identity_under_test.gnu.o: slipgate/sg_pov_identity.c \
		g_local.h slipgate/sg_bot.h slipgate/sg_pov_identity.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

host-test: $(HOST_TEST_BIN) $(ACTION_TEST_BIN) $(COMPOUND_TEST_BIN) \
		$(MOVER_LEASE_TEST_BIN) $(BUTTON_LIVE_TEST_BIN) \
		$(BUTTON_GAME_TEST_BIN) $(COMPOUND_GUARD_TEST_BIN) \
		$(COMPOUND_GUARD_GAME_TEST_BIN) \
		$(COMPOUND_GUARD_GAME_INTEGRATION_TEST) \
		$(DECLARED_DOOR_GUARD_TEST_BIN) \
		$(DECLARED_DOOR_GUARD_INTEGRATION_TESTS) \
		$(COMPOUND_WORLD_TEST_BIN) $(COMPOUND_GEN_TEST_BIN) \
		$(COMPOUND_PUBLICATION_TEST_BIN) \
		$(COMPOUND_PUBLICATION_INTEGRATION_TEST) \
		$(MECHANISM_PUBLICATION_INTEGRATION_TEST) \
		$(IDENTITY_TEST_BIN) \
		$(RUNE_CODEC_TEST_BIN) \
		$(RUNE_ARTIFACT_LOADER_TEST_BIN) $(RUNE_ARTIFACT_WRITER_TEST_BIN) \
		$(RUNE_MECHANISM_PLAN_TEST_BIN) \
		$(RUNE_MECHANISM_CATALOG_TEST_BIN) \
		$(RUNE_MECHANISM_EXECUTION_TEST_BIN) $(RUNE_BINDING_TEST_BIN) \
		$(RUNE_INSTALL_TEST_BIN) \
		$(RUNE_ACCEPT_BIN) \
		$(SIDECAR_WIRE_TEST_BIN) $(SIDECAR_LOADER_TEST_BIN) \
		$(SIDECAR_STORE_TEST_BIN) \
		$(RUNE_NAMING_TEST) \
		$(RELEASE_WORKFLOW_TEST) \
		$(DESLOP_AUDIT) $(DESLOP_AUDIT_TEST) $(SOURCE_SIZE_BUDGET) \
		$(RUNE_PYTHON_TESTS) \
		$(RUNGEN_TEST) \
		$(RUNE_CORPUS_CONTROLLER_TEST) \
		$(BSPMECHANISMS_TEST) \
		$(WAVELOOP_PROCESS_TEST) \
		$(TEMP_FLAG_DIAGNOSTIC_TEST) \
		$(DANGER_LEASE_TEST_BIN) $(DANGER_POLICY_TEST_BIN) \
		$(DANGER_TEST_BIN) $(FIELDS_CANDIDATE_TEST_BIN) \
		$(SNAG_REPAIR_TEST_BIN) $(SNAG_REPAIR_PYTHON_TEST) \
		$(SPECTATOR_SOUND_TEST_BIN) \
		$(HUMAN_SPEED_TEST_BIN) $(HUMAN_SPEED_INTEGRATION_TEST) \
		$(DOOR_APPROACH_TEST_BIN) $(DOOR_APPROACH_INTEGRATION_TEST) \
		$(DEFENSE_SHIFT_TEST_BIN) $(DEFENSE_SHIFT_INTEGRATION_TEST) \
		$(DEFENSE_SUPPLY_TEST_BIN) $(DEFENSE_SUPPLY_INTEGRATION_TEST) \
		$(STRIKE_ADAPTER_TEST_BIN) $(STRIKE_ADAPTER_INTEGRATION_TEST) \
		$(DEFENSE_COMBAT_INTEGRATION_TEST) \
		$(CARRIER_RETURN_TEST) $(COMBAT_AIM_TEST) \
		$(OFFENSE_FLAG_PICKUP_TEST) tests/botfill_selector_test.py \
		$(ITEM_COMMITMENT_TEST_BIN) $(ITEM_COMMITMENT_INTEGRATION_TEST) \
		$(HOOK_DIAGNOSTICS_TEST_BIN) \
		$(HOOK_DIAGNOSTICS_INTEGRATION_TEST) \
		$(HOOK_DIAGNOSTICS_CONSUMER_TEST) $(ROLE_TELEMETRY_CONSUMER_TEST) \
		$(HOOK_EVENTS_TEST) \
		$(RUN_HANDOFF_TEST_BIN) $(RUN_HANDOFF_INTEGRATION_TEST) \
		$(RUNE_PROOF_TEST_BIN) \
		$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN) \
		$(REPLAY_TEST_BIN) $(DROP_LIVE_TEST_BIN) $(SWIM_LIVE_TEST_BIN) \
		$(COMPOUND_SWIM_LIVE_TEST_BIN) \
		$(COMPOUND_SWIM_LIVE_INTEGRATION_TEST) \
		$(HOOK_LIVE_TEST_BIN) $(HOOK_DISCIPLINE_TEST_BIN) \
		$(HOOK_INTEGRATION_TEST) \
		$(ROTATOR_SWEEP_TEST_BIN) $(MOVER_SUBJECT_SWEEP_TEST_BIN) \
		$(COMPOUND_SWIM_ORACLE_TEST_BIN) \
		$(RUNE_DOOR_SCOPE_TEST_BIN) $(ENTFILE_TEST_BIN) \
		$(MAPLIST_ROTATION_TEST_BIN) \
		$(POVLOCK_TEST_BIN) $(POV_SESSION_TEST_BIN) \
		$(POVLOCK_DISPATCH_TEST) $(POV_SUPERVISOR_TEST_BIN) \
		$(POV_SUPERVISOR_BIN) $(POV_SUPERVISOR_TEST) \
		$(POV_ITERATE_SELECTION_TEST)
	./$(HOST_TEST_BIN)
	./$(ACTION_TEST_BIN)
	./$(COMPOUND_TEST_BIN)
	./$(MOVER_LEASE_TEST_BIN)
	./$(BUTTON_LIVE_TEST_BIN)
	./$(BUTTON_GAME_TEST_BIN)
	python3 $(BUTTON_GAME_INTEGRATION_TEST)
	./$(COMPOUND_GUARD_TEST_BIN)
	./$(COMPOUND_GUARD_GAME_TEST_BIN)
	python3 $(COMPOUND_GUARD_GAME_INTEGRATION_TEST)
	./$(DECLARED_DOOR_GUARD_TEST_BIN)
	@set -e; for test in $(DECLARED_DOOR_GUARD_INTEGRATION_TESTS); do \
		python3 "$$test"; done
	./$(COMPOUND_WORLD_TEST_BIN)
	./$(COMPOUND_GEN_TEST_BIN)
	./$(COMPOUND_PUBLICATION_TEST_BIN)
	python3 $(COMPOUND_PUBLICATION_INTEGRATION_TEST)
	python3 $(MECHANISM_PUBLICATION_INTEGRATION_TEST)
	./$(IDENTITY_TEST_BIN)
	./$(RUNE_CODEC_TEST_BIN)
	./$(RUNE_ARTIFACT_LOADER_TEST_BIN)
	./$(RUNE_ARTIFACT_WRITER_TEST_BIN)
	./$(RUNE_MECHANISM_PLAN_TEST_BIN)
	./$(RUNE_MECHANISM_CATALOG_TEST_BIN)
	./$(RUNE_MECHANISM_EXECUTION_TEST_BIN)
	./$(RUNE_BINDING_TEST_BIN)
	./$(RUNE_INSTALL_TEST_BIN)
	./$(SIDECAR_WIRE_TEST_BIN)
	./$(SIDECAR_LOADER_TEST_BIN)
	./$(SIDECAR_STORE_TEST_BIN)
	python3 $(RUNE_NAMING_TEST)
	python3 $(RELEASE_WORKFLOW_TEST)
	python3 -m unittest tests.test_rune_contracts tests.test_rune_artifact \
		tests.test_sidecario tests.test_rune_tool_readers
	python3 $(RUNGEN_TEST)
	python3 -m unittest tests.test_rune_corpus_controller
	python3 -B $(BSPMECHANISMS_TEST)
	python3 $(WAVELOOP_PROCESS_TEST)
	python3 $(TEMP_FLAG_DIAGNOSTIC_TEST)
	./$(DANGER_LEASE_TEST_BIN)
	./$(DANGER_POLICY_TEST_BIN)
	./$(DANGER_TEST_BIN)
	./$(FIELDS_CANDIDATE_TEST_BIN)
	./$(SNAG_REPAIR_TEST_BIN)
	python3 -B $(SNAG_REPAIR_PYTHON_TEST)
	./$(SPECTATOR_SOUND_TEST_BIN)
	./$(HUMAN_SPEED_TEST_BIN)
	python3 -B $(HUMAN_SPEED_INTEGRATION_TEST)
	./$(DOOR_APPROACH_TEST_BIN)
	python3 -B $(DOOR_APPROACH_INTEGRATION_TEST)
	./$(DEFENSE_SHIFT_TEST_BIN)
	python3 -B $(DEFENSE_SHIFT_INTEGRATION_TEST)
	./$(DEFENSE_SUPPLY_TEST_BIN)
	python3 -B $(DEFENSE_SUPPLY_INTEGRATION_TEST)
	./$(STRIKE_ADAPTER_TEST_BIN)
	python3 -B $(STRIKE_ADAPTER_INTEGRATION_TEST)
	python3 -B $(DEFENSE_COMBAT_INTEGRATION_TEST)
	python3 -B $(CARRIER_RETURN_TEST)
	python3 -B $(COMBAT_AIM_TEST)
	python3 -B -m unittest tests.test_offense_flag_pickup_recovery \
		tests.botfill_selector_test tests.test_flag_state
	./$(ITEM_COMMITMENT_TEST_BIN)
	python3 -B $(ITEM_COMMITMENT_INTEGRATION_TEST)
	./$(HOOK_DIAGNOSTICS_TEST_BIN)
	python3 -B $(HOOK_DIAGNOSTICS_INTEGRATION_TEST)
	python3 -B $(HOOK_DIAGNOSTICS_CONSUMER_TEST)
	python3 -B $(ROLE_TELEMETRY_CONSUMER_TEST)
	python3 -B $(HOOK_EVENTS_TEST)
	./$(RUN_HANDOFF_TEST_BIN)
	python3 -B $(RUN_HANDOFF_INTEGRATION_TEST)
	./$(RUNE_PROOF_TEST_BIN)
	./$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN)
	./$(REPLAY_TEST_BIN)
	./$(DROP_LIVE_TEST_BIN)
	sh tests/sg_drop_begin_wiring_test.sh
	./$(SWIM_LIVE_TEST_BIN)
	./$(COMPOUND_SWIM_LIVE_TEST_BIN)
	python3 $(COMPOUND_SWIM_LIVE_INTEGRATION_TEST)
	./$(HOOK_LIVE_TEST_BIN)
	./$(HOOK_DISCIPLINE_TEST_BIN)
	python3 $(HOOK_INTEGRATION_TEST)
	./$(ROTATOR_SWEEP_TEST_BIN)
	./$(MOVER_SUBJECT_SWEEP_TEST_BIN)
	./$(COMPOUND_SWIM_ORACLE_TEST_BIN)
	./$(RUNE_DOOR_SCOPE_TEST_BIN)
	./$(ENTFILE_TEST_BIN)
	./$(MAPLIST_ROTATION_TEST_BIN)
	./$(POVLOCK_TEST_BIN)
	python3 -B $(POVLOCK_DISPATCH_TEST)
	./$(POV_SESSION_TEST_BIN)
	./$(POV_SUPERVISOR_TEST_BIN)
	python3 -B $(POV_SUPERVISOR_TEST)
	python3 -B $(POV_ITERATE_SELECTION_TEST)
	./$(ENGINE_SNAPSHOT_TEST)
	python3 -B $(DESLOP_AUDIT_TEST)
	python3 -B $(DESLOP_AUDIT)

deslop-test: $(DESLOP_AUDIT) $(DESLOP_AUDIT_TEST) \
		$(SOURCE_SIZE_BUDGET)
	python3 -B $(DESLOP_AUDIT_TEST)
	python3 -B $(DESLOP_AUDIT)

action-test: $(ACTION_TEST_BIN)
	./$(ACTION_TEST_BIN)

# Linux tools-only supervisor: intentionally absent from Visual Studio projects.
pov-supervisor-test: $(POV_SUPERVISOR_TEST_BIN) $(POV_SUPERVISOR_BIN) \
		$(POV_SUPERVISOR_TEST) $(POV_ITERATE_SELECTION_TEST)
	./$(POV_SUPERVISOR_TEST_BIN)
	python3 -B $(POV_SUPERVISOR_TEST)
	python3 -B $(POV_ITERATE_SELECTION_TEST)

$(POV_SUPERVISOR_BIN): tools/pov-supervisor.c tools/pov-spawn-linux.c tools/pov-spawn-linux.h
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Itools -o $@ \
		tools/pov-supervisor.c tools/pov-spawn-linux.c

$(POV_SUPERVISOR_TEST_BIN): tests/pov_supervisor_unit.c tools/pov-spawn-linux.c tools/pov-spawn-linux.h
	$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -DPOV_TESTING \
		-Itools -o $@ tests/pov_supervisor_unit.c tools/pov-spawn-linux.c

button-live-test: $(BUTTON_LIVE_TEST_BIN)
	./$(BUTTON_LIVE_TEST_BIN)

button-game-test: $(BUTTON_GAME_TEST_BIN)
	./$(BUTTON_GAME_TEST_BIN)
	python3 $(BUTTON_GAME_INTEGRATION_TEST)

rune-naming-test: $(RUNE_NAMING_TEST)
	python3 $(RUNE_NAMING_TEST)

rune-artifact-test: $(RUNE_PYTHON_TESTS)
	python3 -m unittest tests.test_rune_contracts tests.test_rune_artifact \
		tests.test_sidecario tests.test_rune_tool_readers

rune-corpus-controller-test: $(RUNE_CORPUS_CONTROLLER_TEST) \
		tools/rune_corpus_controller.py tools/RUNE_CORPUS_CONTROLLER.md \
		tools/rune-corpus-maps.txt
	python3 -m unittest tests.test_rune_corpus_controller

compound-test: $(COMPOUND_TEST_BIN)
	./$(COMPOUND_TEST_BIN)

mover-lease-test: $(MOVER_LEASE_TEST_BIN)
	./$(MOVER_LEASE_TEST_BIN)

compound-guard-test: $(COMPOUND_GUARD_TEST_BIN)
	./$(COMPOUND_GUARD_TEST_BIN)

compound-guard-game-test: $(COMPOUND_GUARD_GAME_TEST_BIN) \
		$(COMPOUND_GUARD_GAME_INTEGRATION_TEST)
	./$(COMPOUND_GUARD_GAME_TEST_BIN)
	python3 $(COMPOUND_GUARD_GAME_INTEGRATION_TEST)

declared-door-guard-test: $(DECLARED_DOOR_GUARD_TEST_BIN) \
		$(DECLARED_DOOR_GUARD_INTEGRATION_TESTS)
	./$(DECLARED_DOOR_GUARD_TEST_BIN)
	@set -e; for test in $(DECLARED_DOOR_GUARD_INTEGRATION_TESTS); do \
		python3 "$$test"; done

compound-world-test: $(COMPOUND_WORLD_TEST_BIN)
	./$(COMPOUND_WORLD_TEST_BIN)

compound-gen-test: $(COMPOUND_GEN_TEST_BIN)
	./$(COMPOUND_GEN_TEST_BIN)

compound-publication-test: $(COMPOUND_PUBLICATION_TEST_BIN) \
		$(COMPOUND_PUBLICATION_INTEGRATION_TEST) \
		$(MECHANISM_PUBLICATION_INTEGRATION_TEST)
	./$(COMPOUND_PUBLICATION_TEST_BIN)
	python3 $(COMPOUND_PUBLICATION_INTEGRATION_TEST)
	python3 $(MECHANISM_PUBLICATION_INTEGRATION_TEST)

identity-test: $(IDENTITY_TEST_BIN)
	./$(IDENTITY_TEST_BIN)

rune-codec-test: $(RUNE_CODEC_TEST_BIN)
	./$(RUNE_CODEC_TEST_BIN)

rune-artifact-loader-test: $(RUNE_ARTIFACT_LOADER_TEST_BIN)
	./$(RUNE_ARTIFACT_LOADER_TEST_BIN)

rune-artifact-writer-test: $(RUNE_ARTIFACT_WRITER_TEST_BIN)
	./$(RUNE_ARTIFACT_WRITER_TEST_BIN)

rune-mechanism-plan-test: $(RUNE_MECHANISM_PLAN_TEST_BIN)
	./$(RUNE_MECHANISM_PLAN_TEST_BIN)

rune-mechanism-catalog-test: $(RUNE_MECHANISM_CATALOG_TEST_BIN)
	./$(RUNE_MECHANISM_CATALOG_TEST_BIN)

rune-mechanism-execution-test: $(RUNE_MECHANISM_EXECUTION_TEST_BIN)
	./$(RUNE_MECHANISM_EXECUTION_TEST_BIN)

rune-binding-test: $(RUNE_BINDING_TEST_BIN)
	./$(RUNE_BINDING_TEST_BIN)

rune-accept-tool: $(RUNE_ACCEPT_BIN)

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

danger-test: $(DANGER_TEST_BIN)
	./$(DANGER_TEST_BIN)

fields-candidate-test: $(FIELDS_CANDIDATE_TEST_BIN)
	./$(FIELDS_CANDIDATE_TEST_BIN)

snag-repair-test: $(SNAG_REPAIR_TEST_BIN) $(SNAG_REPAIR_PYTHON_TEST)
	./$(SNAG_REPAIR_TEST_BIN)
	python3 -B $(SNAG_REPAIR_PYTHON_TEST)

human-speed-test: $(HUMAN_SPEED_TEST_BIN) $(HUMAN_SPEED_INTEGRATION_TEST)
	./$(HUMAN_SPEED_TEST_BIN)
	python3 -B $(HUMAN_SPEED_INTEGRATION_TEST)

door-approach-test: $(DOOR_APPROACH_TEST_BIN)
	./$(DOOR_APPROACH_TEST_BIN)

defense-shift-test: $(DEFENSE_SHIFT_TEST_BIN) $(DEFENSE_SHIFT_INTEGRATION_TEST) \
		$(DEFENSE_COMBAT_INTEGRATION_TEST)
	./$(DEFENSE_SHIFT_TEST_BIN)
	python3 -B $(DEFENSE_SHIFT_INTEGRATION_TEST)
	python3 -B $(DEFENSE_COMBAT_INTEGRATION_TEST)

defense-supply-test: $(DEFENSE_SUPPLY_TEST_BIN) $(DEFENSE_SUPPLY_INTEGRATION_TEST)
	./$(DEFENSE_SUPPLY_TEST_BIN)
	python3 -B $(DEFENSE_SUPPLY_INTEGRATION_TEST)

strike-adapter-test: $(STRIKE_ADAPTER_TEST_BIN) $(STRIKE_ADAPTER_INTEGRATION_TEST)
	./$(STRIKE_ADAPTER_TEST_BIN)
	python3 -B $(STRIKE_ADAPTER_INTEGRATION_TEST)

item-commitment-test: $(ITEM_COMMITMENT_TEST_BIN) \
		$(ITEM_COMMITMENT_INTEGRATION_TEST)
	./$(ITEM_COMMITMENT_TEST_BIN)
	python3 -B $(ITEM_COMMITMENT_INTEGRATION_TEST)

hook-diagnostics-test: $(HOOK_DIAGNOSTICS_TEST_BIN) \
		$(HOOK_DIAGNOSTICS_INTEGRATION_TEST) \
		$(HOOK_DIAGNOSTICS_CONSUMER_TEST) $(HOOK_EVENTS_TEST)
	./$(HOOK_DIAGNOSTICS_TEST_BIN)
	python3 -B $(HOOK_DIAGNOSTICS_INTEGRATION_TEST)
	python3 -B $(HOOK_DIAGNOSTICS_CONSUMER_TEST)
	python3 -B $(HOOK_EVENTS_TEST)

run-handoff-test: $(RUN_HANDOFF_TEST_BIN) $(RUN_HANDOFF_INTEGRATION_TEST)
	./$(RUN_HANDOFF_TEST_BIN)
	python3 -B $(RUN_HANDOFF_INTEGRATION_TEST)

rune-install-test: $(RUNE_INSTALL_TEST_BIN)
	./$(RUNE_INSTALL_TEST_BIN)

rune-proof-test: $(RUNE_PROOF_TEST_BIN)
	./$(RUNE_PROOF_TEST_BIN)

rune-objective-diagnostics-test: $(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN)
	./$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN)

replay-test: $(REPLAY_TEST_BIN)
	./$(REPLAY_TEST_BIN)

drop-live-test: $(DROP_LIVE_TEST_BIN)
	./$(DROP_LIVE_TEST_BIN)
	sh tests/sg_drop_begin_wiring_test.sh

swim-live-test: $(SWIM_LIVE_TEST_BIN)
	./$(SWIM_LIVE_TEST_BIN)

compound-swim-live-test: $(COMPOUND_SWIM_LIVE_TEST_BIN) \
		$(COMPOUND_SWIM_LIVE_INTEGRATION_TEST)
	./$(COMPOUND_SWIM_LIVE_TEST_BIN)
	python3 $(COMPOUND_SWIM_LIVE_INTEGRATION_TEST)

hook-live-test: $(HOOK_LIVE_TEST_BIN)
	./$(HOOK_LIVE_TEST_BIN)

hook-integration-test:
	python3 $(HOOK_INTEGRATION_TEST)

hook-discipline-test: $(HOOK_DISCIPLINE_TEST_BIN)
	./$(HOOK_DISCIPLINE_TEST_BIN)

rotator-sweep-test: $(ROTATOR_SWEEP_TEST_BIN)
	./$(ROTATOR_SWEEP_TEST_BIN)

mover-subject-sweep-test: $(MOVER_SUBJECT_SWEEP_TEST_BIN)
	./$(MOVER_SUBJECT_SWEEP_TEST_BIN)

compound-swim-oracle-test: $(COMPOUND_SWIM_ORACLE_TEST_BIN)
	./$(COMPOUND_SWIM_ORACLE_TEST_BIN)

rune-door-scope-test: $(RUNE_DOOR_SCOPE_TEST_BIN)
	./$(RUNE_DOOR_SCOPE_TEST_BIN)

entfile-test: $(ENTFILE_TEST_BIN)
	./$(ENTFILE_TEST_BIN)

maplist-rotation-test: $(MAPLIST_ROTATION_TEST_BIN)
	./$(MAPLIST_ROTATION_TEST_BIN)

snapshot-test:
	./$(ENGINE_SNAPSHOT_TEST)

dep: $(DEPEND_FILE)

$(DEPEND_FILE): $(OBJS:.o=.c) GNUmakefile FORCE | $(REVISION_HEADER)
	@echo "Updating dependencies..."
	@set -e; \
	tmp="$@.tmp.$$$$"; \
	trap 'rm -f "$$tmp"' EXIT HUP INT TERM; \
	$(CC) $(CPPFLAGS) -MM $(filter-out slipgate/sg_compound_world.c \
		slipgate/sg_mover_lease.c \
		slipgate/sg_compound_guard.c \
		slipgate/sg_compound_guard_game.c \
		slipgate/sg_declared_door_guard.c \
		slipgate/sg_compound_gen.c \
		slipgate/sg_compound_publication.c \
		slipgate/sg_rune_door_scope.c \
		slipgate/sg_door_approach.c \
		slipgate/sg_snag_repair.c, \
		$(OBJS:.o=.c)) > "$$tmp"; \
	$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_compound_world.o \
		slipgate/sg_compound_world.c >> "$$tmp"; \
	$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_mover_lease.o \
		slipgate/sg_mover_lease.c >> "$$tmp"; \
	$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_compound_guard.o \
		slipgate/sg_compound_guard.c >> "$$tmp"; \
	$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_compound_guard_game.o \
		slipgate/sg_compound_guard_game.c >> "$$tmp"; \
	$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_declared_door_guard.o \
		slipgate/sg_declared_door_guard.c >> "$$tmp"; \
	$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_compound_gen.o \
		slipgate/sg_compound_gen.c >> "$$tmp"; \
	$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_compound_publication.o \
		slipgate/sg_compound_publication.c >> "$$tmp"; \
	$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_rune_door_scope.o \
		slipgate/sg_rune_door_scope.c >> "$$tmp"; \
	$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_door_approach.o \
		slipgate/sg_door_approach.c >> "$$tmp"; \
	$(CC) $(CPPFLAGS) -MM -MT slipgate/sg_snag_repair.o \
		slipgate/sg_snag_repair.c >> "$$tmp"; \
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
		@rm -f $(OBJS) $(OBJS:.o=.d) $(REVISION_HEADER) \
			$(REVISION_HEADER).tmp.* \
			$(DEPEND_FILE).tmp.* $(HOST_TEST_ALL_ARTIFACTS) \
			$(MAPLIST_ROTATION_TEST_ALL_ARTIFACTS) \
			$(POV_SUPERVISOR_ALL_ARTIFACTS) \
			$(COMPOUND_GEN_TEST_ALL_ARTIFACTS) \
			$(COMPOUND_PUBLICATION_TEST_ALL_ARTIFACTS) \
			$(RUNE_CODEC_TEST_ALL_ARTIFACTS) \
			$(RUNE_ARTIFACT_LOADER_TEST_ALL_ARTIFACTS) \
			$(RUNE_ARTIFACT_WRITER_TEST_ALL_ARTIFACTS) \
			$(RUNE_MECHANISM_PLAN_TEST_ALL_ARTIFACTS) \
			$(RUNE_MECHANISM_CATALOG_TEST_ALL_ARTIFACTS) \
			$(RUNE_MECHANISM_EXECUTION_TEST_ALL_ARTIFACTS) \
			$(RUNE_BINDING_TEST_ALL_ARTIFACTS) \
			$(RUNE_ACCEPT_ALL_ARTIFACTS) \
			$(COMPOUND_SWIM_LIVE_TEST_ALL_ARTIFACTS) \
			$(COMPOUND_SWIM_ORACLE_TEST_ALL_ARTIFACTS) \
			$(MOVER_LEASE_TEST_ALL_ARTIFACTS) \
			$(BUTTON_LIVE_TEST_ALL_ARTIFACTS) \
			$(BUTTON_GAME_TEST_ALL_ARTIFACTS) \
			$(COMPOUND_GUARD_TEST_ALL_ARTIFACTS) \
			$(COMPOUND_GUARD_GAME_TEST_ALL_ARTIFACTS) \
			$(DECLARED_DOOR_GUARD_TEST_ALL_ARTIFACTS) \
			$(MOVER_SUBJECT_SWEEP_TEST_ALL_ARTIFACTS) \
			$(SNAG_REPAIR_TEST_ALL_ARTIFACTS) \
			$(SPECTATOR_SOUND_TEST_ALL_ARTIFACTS) \
			$(HUMAN_SPEED_TEST_ALL_ARTIFACTS) \
			$(DOOR_APPROACH_TEST_ALL_ARTIFACTS) \
			$(DEFENSE_SHIFT_TEST_ALL_ARTIFACTS) \
			$(DEFENSE_SUPPLY_TEST_ALL_ARTIFACTS) \
			$(STRIKE_ADAPTER_TEST_ALL_ARTIFACTS) \
			$(ITEM_COMMITMENT_TEST_ALL_ARTIFACTS) \
			$(HOOK_DIAGNOSTICS_TEST_ALL_ARTIFACTS) \
			$(HOOK_DISCIPLINE_TEST_ALL_ARTIFACTS) \
			$(RUN_HANDOFF_TEST_ALL_ARTIFACTS) \
			$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_ALL_ARTIFACTS) \
			$(RUNE_DOOR_SCOPE_TEST_ALL_ARTIFACTS) *.orig ~* core
distclean:	clean
		@echo "Deleting everything that can be rebuilt..."
		@rm -f $(TARGET) $(DEPEND_FILE)

ifeq (,$(filter clean distclean,$(MAKECMDGOALS)))
ifeq ($(DEPEND_FILE),$(wildcard $(DEPEND_FILE)))
include $(DEPEND_FILE)
endif
-include $(HOST_TEST_DEPS)
-include $(POVLOCK_TEST_DEPS)
-include $(POV_SESSION_TEST_DEPS)
-include $(ACTION_TEST_DEPS)
-include $(COMPOUND_TEST_DEPS)
-include $(MOVER_LEASE_TEST_DEPS)
-include $(BUTTON_LIVE_TEST_DEPS)
-include $(BUTTON_GAME_TEST_DEPS)
-include $(COMPOUND_GUARD_TEST_DEPS)
-include $(COMPOUND_GUARD_GAME_TEST_DEPS)
-include $(DECLARED_DOOR_GUARD_TEST_DEPS)
-include $(COMPOUND_WORLD_TEST_DEPS)
-include $(COMPOUND_GEN_TEST_DEPS)
-include $(COMPOUND_PUBLICATION_TEST_DEPS)
-include $(IDENTITY_TEST_DEPS)
-include $(RUNE_CODEC_TEST_DEPS)
-include $(RUNE_ARTIFACT_LOADER_TEST_DEPS)
-include $(RUNE_ARTIFACT_WRITER_TEST_DEPS)
-include $(RUNE_MECHANISM_PLAN_TEST_DEPS)
-include $(RUNE_MECHANISM_CATALOG_TEST_DEPS)
-include $(RUNE_MECHANISM_EXECUTION_TEST_DEPS)
-include $(RUNE_BINDING_TEST_DEPS)
-include $(RUNE_ACCEPT_DEPS)
-include $(SIDECAR_WIRE_TEST_DEPS)
-include $(SIDECAR_LOADER_TEST_DEPS)
-include $(SIDECAR_STORE_TEST_DEPS)
-include $(DANGER_LEASE_TEST_DEPS)
-include $(DANGER_POLICY_TEST_DEPS)
-include $(DANGER_TEST_DEPS)
-include $(FIELDS_CANDIDATE_TEST_DEPS)
-include $(SNAG_REPAIR_TEST_DEPS)
-include $(SPECTATOR_SOUND_TEST_DEPS)
-include $(HUMAN_SPEED_TEST_DEPS)
-include $(DOOR_APPROACH_TEST_DEPS)
-include $(DEFENSE_SHIFT_TEST_DEPS)
-include $(DEFENSE_SUPPLY_TEST_DEPS)
-include $(STRIKE_ADAPTER_TEST_DEPS)
-include $(ITEM_COMMITMENT_TEST_DEPS)
-include $(HOOK_DIAGNOSTICS_TEST_DEPS)
-include $(RUN_HANDOFF_TEST_DEPS)
-include $(RUNE_INSTALL_TEST_DEPS)
-include $(RUNE_PROOF_TEST_DEPS)
-include $(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_DEPS)
-include $(REPLAY_TEST_DEPS)
-include $(DROP_LIVE_TEST_DEPS)
-include $(SWIM_LIVE_TEST_DEPS)
-include $(COMPOUND_SWIM_LIVE_TEST_DEPS)
-include $(HOOK_LIVE_TEST_DEPS)
-include $(HOOK_DISCIPLINE_TEST_DEPS)
-include $(ROTATOR_SWEEP_TEST_DEPS)
-include $(MOVER_SUBJECT_SWEEP_TEST_DEPS)
-include $(COMPOUND_SWIM_ORACLE_TEST_DEPS)
-include $(RUNE_DOOR_SCOPE_TEST_DEPS)
-include $(ENTFILE_TEST_DEPS)
endif

# The SQLite amalgamation is third-party and does not build clean under our
# -Wall, so give it its own rule. THREADSAFE=0 because the game module is
# single-threaded and it saves linking pthreads.
SQLITE_CFLAGS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION=1 \
                -DSQLITE_DEFAULT_MEMSTATUS=0 -w

sqlite3.o: sqlite3.c
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) $(SQLITE_CFLAGS) -o $@ -c $<
