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
HOST_TEST_DEPS = $(HOST_TEST_OBJS:.o=.d)
ACTION_TEST_DEPS = $(ACTION_TEST_OBJS:.o=.d)
COMPOUND_TEST_DEPS = $(COMPOUND_TEST_OBJS:.o=.d)
MOVER_LEASE_TEST_DEPS = $(MOVER_LEASE_TEST_OBJS:.o=.d)
WATER_FOREST_TEST_DEPS = $(WATER_FOREST_TEST_OBJS:.o=.d)
BUTTON_LIVE_TEST_DEPS = $(BUTTON_LIVE_TEST_OBJS:.o=.d)
TRAIN_GATE_LIVE_TEST_DEPS = $(TRAIN_GATE_LIVE_TEST_OBJS:.o=.d)
MECHANISM_TIMELINE_TEST_DEPS = $(MECHANISM_TIMELINE_TEST_OBJS:.o=.d)
RELAY_WALL_TRANSACTION_TEST_DEPS = \
	$(RELAY_WALL_TRANSACTION_TEST_OBJS:.o=.d)
RELAY_WALL_OBJECTIVE_TEST_DEPS = $(RELAY_WALL_OBJECTIVE_TEST_OBJS:.o=.d)
SHOOT_DOOR_LIVE_TEST_DEPS = $(SHOOT_DOOR_LIVE_TEST_OBJS:.o=.d)
BUTTON_GAME_TEST_OBJS = .sg_button_game_func_under_test.gnu.o \
	.sg_button_game_q_shared_under_test.gnu.o
BUTTON_GAME_TEST_DEPS = $(BUTTON_GAME_TEST_OBJS:.o=.d)
BUTTON_GAME_TEST_ALL_ARTIFACTS = .sg_button_game_live_under_test.gnu.d \
	.sg_button_game_move_under_test.gnu.d .sg_button_game_func_under_test.gnu.o \
	.sg_button_game_func_under_test.gnu.d \
	.sg_button_game_q_shared_under_test.gnu.o \
	.sg_button_game_q_shared_under_test.gnu.d \
	.sg_button_game_live_under_test.make.o .sg_button_game_live_under_test.make.d \
	.sg_button_game_move_under_test.make.o .sg_button_game_move_under_test.make.d \
	.sg_button_game_func_under_test.make.o .sg_button_game_func_under_test.make.d \
	.sg_button_game_q_shared_under_test.make.o \
	.sg_button_game_q_shared_under_test.make.d
COMPOUND_GUARD_TEST_DEPS = $(COMPOUND_GUARD_TEST_OBJS:.o=.d)
COMPOUND_GUARD_TEST_ALL_ARTIFACTS = \
	.sg_compound_guard_mover_lease_under_test.gnu.d \
	.sg_compound_guard_mover_lease_under_test.make.o \
	.sg_compound_guard_mover_lease_under_test.make.d
COMPOUND_GUARD_GAME_TEST_DEPS = $(COMPOUND_GUARD_GAME_TEST_OBJS:.o=.d)
DECLARED_DOOR_GUARD_TEST_DEPS = $(DECLARED_DOOR_GUARD_TEST_OBJS:.o=.d)
COMPOUND_WORLD_TEST_OBJS = .sg_compound_world_q_shared_under_test.gnu.o
COMPOUND_WORLD_TEST_DEPS = $(COMPOUND_WORLD_TEST_OBJS:.o=.d)
COMPOUND_GEN_TEST_DEPS = $(COMPOUND_GEN_TEST_OBJS:.o=.d)
COMPOUND_GEN_GAME_TEST_DEPS = $(COMPOUND_GEN_GAME_TEST_OBJS:.o=.d)
COMPOUND_GEN_TEST_ALL_ARTIFACTS = sg_compound_gen_test
COMPOUND_PUBLICATION_CASE_STEMS = compound_publication_fixture \
	compound_publication_core_cases compound_hook_publication_cases
COMPOUND_PUBLICATION_CASE_GNU_OBJS = \
	$(addprefix .sg_,$(addsuffix .gnu.o,$(COMPOUND_PUBLICATION_CASE_STEMS)))
COMPOUND_PUBLICATION_TEST_DEPS = $(COMPOUND_PUBLICATION_TEST_OBJS:.o=.d)
COMPOUND_PUBLICATION_ARTIFACT_STEMS = compound_publication_test \
	$(COMPOUND_PUBLICATION_CASE_STEMS) compound_publication_under_test \
	compound_publication_build_under_test \
	compound_action_publication_for_publication_test
COMPOUND_PUBLICATION_TEST_ALL_ARTIFACTS = $(foreach flavor,gnu make,$(foreach \
	stem, $(COMPOUND_PUBLICATION_ARTIFACT_STEMS), .sg_$(stem).$(flavor).o \
	.sg_$(stem).$(flavor).d))
IDENTITY_TEST_DEPS = $(IDENTITY_TEST_OBJS:.o=.d)
RUNE_CODEC_TEST_OBJS = .sg_rune_crc_under_test.gnu.o
RUNE_CODEC_TEST_DEPS = $(RUNE_CODEC_TEST_OBJS:.o=.d)
RUNE_ARTIFACT_LOADER_TEST_OBJS = .sg_rune_crc_under_test.gnu.o
RUNE_ARTIFACT_LOADER_TEST_DEPS = $(RUNE_ARTIFACT_LOADER_TEST_OBJS:.o=.d)
RUNE_ARTIFACT_WRITER_TEST_OBJS = .sg_rune_crc_under_test.gnu.o
RUNE_ARTIFACT_WRITER_TEST_DEPS = $(RUNE_ARTIFACT_WRITER_TEST_OBJS:.o=.d)
RUNE_MECHANISM_PLAN_TEST_OBJS = .sg_rune_crc_under_test.gnu.o
RUNE_MECHANISM_PLAN_TEST_DEPS = $(RUNE_MECHANISM_PLAN_TEST_OBJS:.o=.d)
RUNE_MECHANISM_CATALOG_TEST_DEPS = \
	$(RUNE_MECHANISM_CATALOG_TEST_OBJS:.o=.d)
RUNE_MECHANISM_EXECUTION_TEST_OBJS = .sg_rune_crc_under_test.gnu.o \
	.sg_delayed_relay_dispatch_view_under_test.gnu.o \
	.sg_game_utils_under_test.gnu.o \
	.sg_delayed_relay_dispatch_trigger_under_test.gnu.o \
	.sg_q_shared_under_test.gnu.o
RUNE_MECHANISM_EXECUTION_TEST_DEPS = \
	$(RUNE_MECHANISM_EXECUTION_TEST_OBJS:.o=.d)
RUNE_MECHANISM_EXECUTION_TEST_ALL_ARTIFACTS = $(foreach flavor,gnu make, \
	.sg_delayed_relay_dispatch_move_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_move_under_test.$(flavor).d \
	.sg_delayed_relay_dispatch_util_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_util_under_test.$(flavor).d \
	.sg_delayed_relay_dispatch_view_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_view_under_test.$(flavor).d \
	.sg_game_utils_under_test.$(flavor).o .sg_game_utils_under_test.$(flavor).d \
	.sg_relay_wall_game_under_test.$(flavor).o \
	.sg_relay_wall_game_under_test.$(flavor).d \
	.sg_relay_wall_live_under_test.$(flavor).o \
	.sg_relay_wall_live_under_test.$(flavor).d \
	.sg_relay_wall_ticket_under_test.$(flavor).o \
	.sg_relay_wall_ticket_under_test.$(flavor).d \
	.sg_relay_wall_transaction_live_under_test.$(flavor).o \
	.sg_relay_wall_transaction_live_under_test.$(flavor).d \
	.sg_relay_wall_timeline_live_under_test.$(flavor).o \
	.sg_relay_wall_timeline_live_under_test.$(flavor).d \
	.sg_timed_vault_runtime_under_test.$(flavor).o \
	.sg_timed_vault_runtime_under_test.$(flavor).d \
	.sg_timed_vault_game_under_test.$(flavor).o \
	.sg_timed_vault_game_under_test.$(flavor).d \
	.sg_timed_vault_transaction_under_test.$(flavor).o \
	.sg_timed_vault_transaction_under_test.$(flavor).d \
	.sg_delayed_relay_dispatch_trigger_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_trigger_under_test.$(flavor).d \
	.sg_delayed_relay_dispatch_button_under_test.$(flavor).o \
	.sg_delayed_relay_dispatch_button_under_test.$(flavor).d \
	.sg_rune_mechanism_execution_link_stubs.$(flavor).o \
	.sg_rune_mechanism_execution_link_stubs.$(flavor).d \
	.sg_q_shared_under_test.$(flavor).o .sg_q_shared_under_test.$(flavor).d)
RUNE_BINDING_TEST_OBJS = .sg_rune_crc_under_test.gnu.o
RUNE_BINDING_TEST_DEPS = $(RUNE_BINDING_TEST_OBJS:.o=.d)
RUNE_COMPACT_READER_SOURCES = tools/runecompactread.c
RUNE_COMPACT_READER_HEADERS = $(wildcard *.h slipgate/*.h)
RUNE_ACCEPT_OBSOLETE_ALL_ARTIFACTS = runeaccept.gnu runeaccept.make \
	.runeaccept.gnu.o .runeaccept.gnu.d .runeaccept.make.o .runeaccept.make.d
DANGER_LEASE_TEST_DEPS = $(DANGER_LEASE_TEST_OBJS:.o=.d)
DANGER_POLICY_TEST_DEPS = $(DANGER_POLICY_TEST_OBJS:.o=.d)
DANGER_TEST_DEPS = $(DANGER_TEST_OBJS:.o=.d)
FIELDS_CANDIDATE_TEST_OBJS = .sg_game_utils_under_test.gnu.o .sg_q_shared_under_test.gnu.o
FIELDS_CANDIDATE_TEST_DEPS = $(FIELDS_CANDIDATE_TEST_OBJS:.o=.d)
TACTIC_POLICY_TEST_DEPS = $(TACTIC_POLICY_TEST_OBJS:.o=.d)
TACTIC_POLICY_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_tactic_policy_test.$(flavor) \
	.sg_tactic_policy_test.$(flavor).o \
	.sg_tactic_policy_test.$(flavor).d \
	.sg_tactic_policy_under_test.$(flavor).o \
	.sg_tactic_policy_under_test.$(flavor).d)
SPECTATOR_SOUND_TEST_OBJS = .sg_spectator_sound_net_under_test.gnu.o
SPECTATOR_SOUND_TEST_DEPS = $(SPECTATOR_SOUND_TEST_OBJS:.o=.d)
SPECTATOR_SOUND_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_spectator_sound_test.$(flavor) \
	.sg_spectator_sound_test.$(flavor).o \
	.sg_spectator_sound_test.$(flavor).d \
	.sg_spectator_sound_net_under_test.$(flavor).o \
	.sg_spectator_sound_net_under_test.$(flavor).d)
HUMAN_SPEED_TEST_OBJS = .sg_human_speed_pmove_under_test.gnu.o \
	.sg_human_speed_q_shared_under_test.gnu.o
HUMAN_SPEED_TEST_DEPS = $(HUMAN_SPEED_TEST_OBJS:.o=.d)
HUMAN_TRACE_IO_TEST_BIN = sg_human_trace_io_test.gnu
HUMAN_TRACE_HOOK_TEST_ALL_ARTIFACTS = sg_human_trace_io_test.gnu sg_human_trace_io_test.make
HUMAN_SPEED_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_human_speed_test.$(flavor) \
	.sg_human_speed_test.$(flavor).o .sg_human_speed_test.$(flavor).d \
	.sg_human_speed_under_test.$(flavor).o \
	.sg_human_speed_under_test.$(flavor).d \
	.sg_human_speed_pmove_under_test.$(flavor).o \
	.sg_human_speed_pmove_under_test.$(flavor).d \
	.sg_human_speed_q_shared_under_test.$(flavor).o \
	.sg_human_speed_q_shared_under_test.$(flavor).d)
DOOR_APPROACH_TEST_DEPS = $(DOOR_APPROACH_TEST_OBJS:.o=.d)
DOOR_APPROACH_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_door_approach_test.$(flavor) \
	.sg_door_approach_test.$(flavor).o \
	.sg_door_approach_test.$(flavor).d \
	.sg_door_approach_under_test.$(flavor).o \
	.sg_door_approach_under_test.$(flavor).d)
DEFENSE_SHIFT_TEST_DEPS = $(DEFENSE_SHIFT_TEST_OBJS:.o=.d)
DEFENSE_SHIFT_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_defense_shift_test.$(flavor) \
	.sg_defense_shift_test.$(flavor).o .sg_defense_shift_test.$(flavor).d \
	.sg_defense_shift_under_test.$(flavor).o \
	.sg_defense_shift_under_test.$(flavor).d)
DEFENSE_SUPPLY_TEST_DEPS = $(DEFENSE_SUPPLY_TEST_OBJS:.o=.d)
DEFENSE_SUPPLY_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_defense_supply_test.$(flavor) \
	.sg_defense_supply_test.$(flavor).o .sg_defense_supply_test.$(flavor).d \
	.sg_defense_supply_under_test.$(flavor).o \
	.sg_defense_supply_under_test.$(flavor).d)
STRIKE_ADAPTER_TEST_DEPS = $(STRIKE_ADAPTER_TEST_OBJS:.o=.d)
STRIKE_ADAPTER_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_strike_adapter_test.$(flavor) \
	.sg_strike_adapter_test.$(flavor).o .sg_strike_adapter_test.$(flavor).d \
	.sg_strike_under_test.$(flavor).o .sg_strike_under_test.$(flavor).d \
	.sg_strike_adapter_under_test.$(flavor).o \
	.sg_strike_adapter_under_test.$(flavor).d)
ITEM_COMMITMENT_TEST_DEPS = $(ITEM_COMMITMENT_TEST_OBJS:.o=.d)
ITEM_COMMITMENT_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_item_commitment_test.$(flavor) \
	.sg_item_commitment_test.$(flavor).o .sg_item_commitment_test.$(flavor).d \
	.sg_item_commitment_under_test.$(flavor).o \
	.sg_item_commitment_under_test.$(flavor).d)
HOOK_DIAGNOSTICS_TEST_DEPS = $(HOOK_DIAGNOSTICS_TEST_OBJS:.o=.d)
HOOK_DIAGNOSTICS_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_hook_diagnostics_test.$(flavor) \
	.sg_hook_diagnostics_test.$(flavor).o \
	.sg_hook_diagnostics_test.$(flavor).d \
	.sg_hook_diagnostics_under_test.$(flavor).o \
	.sg_hook_diagnostics_under_test.$(flavor).d)
RUN_HANDOFF_TEST_OBJS = .sg_run_handoff_pmove_under_test.gnu.o \
	.sg_run_handoff_q_shared_under_test.gnu.o
RUN_HANDOFF_TEST_DEPS = $(RUN_HANDOFF_TEST_OBJS:.o=.d)
RUN_HANDOFF_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_run_handoff_test.$(flavor) \
	.sg_run_handoff_test.$(flavor).o .sg_run_handoff_test.$(flavor).d \
	.sg_run_handoff_descend_under_test.$(flavor).o \
	.sg_run_handoff_descend_under_test.$(flavor).d \
	.sg_run_handoff_pmove_under_test.$(flavor).o \
	.sg_run_handoff_pmove_under_test.$(flavor).d \
	.sg_run_handoff_q_shared_under_test.$(flavor).o \
	.sg_run_handoff_q_shared_under_test.$(flavor).d)
RUNE_INSTALL_TEST_OBJS = .sg_rune_crc_under_test.gnu.o
RUNE_INSTALL_TEST_DEPS = $(RUNE_INSTALL_TEST_OBJS:.o=.d)
RUNE_PROOF_TEST_DEPS = $(RUNE_PROOF_TEST_OBJS:.o=.d)
RUNE_OBJECTIVE_DIAGNOSTICS_TEST_DEPS = \
	$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_OBJS:.o=.d)
RUNE_OBJECTIVE_DIAGNOSTICS_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_rune_objective_diagnostics_test.$(flavor) \
	.sg_rune_objective_diagnostics_test.$(flavor).o \
	.sg_rune_objective_diagnostics_test.$(flavor).d)
REPLAY_TEST_DEPS = $(REPLAY_TEST_OBJS:.o=.d)
CHAIN_HOOK_REPLAY_TEST_DEPS = $(CHAIN_HOOK_REPLAY_TEST_OBJS:.o=.d)
DROP_LIVE_TEST_DEPS = $(DROP_LIVE_TEST_OBJS:.o=.d)
SWIM_LIVE_TEST_DEPS = $(SWIM_LIVE_TEST_OBJS:.o=.d)
COMPOUND_SWIM_LIVE_TEST_DEPS = $(COMPOUND_SWIM_LIVE_TEST_OBJS:.o=.d)
COMPOUND_SWIM_GAME_TEST_OBJS = $(filter-out \
	.sg_compound_swim_live_test.gnu.o,$(COMPOUND_SWIM_LIVE_TEST_OBJS))
COMPOUND_SWIM_GAME_TEST_DEPS = $(COMPOUND_SWIM_GAME_TEST_OBJS:.o=.d)
COMPOUND_SWIM_LIVE_TEST_ALL_ARTIFACTS = \
	.sg_compound_swim_live_compound_under_test.gnu.d \
	.sg_compound_swim_live_action_under_test.gnu.d \
	.sg_compound_swim_live_replay_under_test.gnu.d \
	.sg_compound_swim_live_compound_under_test.make.o \
	.sg_compound_swim_live_compound_under_test.make.d \
	.sg_compound_swim_live_action_under_test.make.o \
	.sg_compound_swim_live_action_under_test.make.d \
	.sg_compound_swim_live_replay_under_test.make.o \
	.sg_compound_swim_live_replay_under_test.make.d
COMPOUND_SWIM_GAME_TEST_ALL_ARTIFACTS = $(COMPOUND_SWIM_GAME_TEST_OBJS) \
	$(COMPOUND_SWIM_GAME_TEST_DEPS)
COMPOUND_DROP_LIVE_TEST_DEPS = $(COMPOUND_DROP_LIVE_TEST_OBJS:.o=.d)
COMPOUND_DROP_GAME_TEST_DEPS = $(COMPOUND_DROP_GAME_TEST_OBJS:.o=.d)
COMPOUND_DROP_FANOUT_TEST_DEPS = \
	$(COMPOUND_DROP_FANOUT_TEST_OBJS:.o=.d)
COMPOUND_DROP_TRANSITION_TEST_DEPS = \
	$(COMPOUND_DROP_TRANSITION_TEST_OBJS:.o=.d)
COMPOUND_DROP_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_compound_drop_live_test.$(flavor) \
	.sg_compound_drop_live_test.$(flavor).o \
	.sg_compound_drop_live_test.$(flavor).d \
	.sg_compound_drop_live_under_test.$(flavor).o \
	.sg_compound_drop_live_under_test.$(flavor).d \
	.sg_compound_drop_live_finish_under_test.$(flavor).o \
	.sg_compound_drop_live_finish_under_test.$(flavor).d \
	.sg_compound_drop_live_drop_under_test.$(flavor).o \
	.sg_compound_drop_live_drop_under_test.$(flavor).d \
	.sg_compound_drop_live_compound_under_test.$(flavor).o \
	.sg_compound_drop_live_compound_under_test.$(flavor).d \
	.sg_compound_drop_live_action_under_test.$(flavor).o \
	.sg_compound_drop_live_action_under_test.$(flavor).d \
	.sg_compound_drop_live_replay_under_test.$(flavor).o \
	.sg_compound_drop_live_replay_under_test.$(flavor).d \
	sg_compound_drop_game_test.$(flavor) \
	.sg_compound_drop_game_test.$(flavor).o \
	.sg_compound_drop_game_test.$(flavor).d \
	.sg_compound_drop_game_under_test.$(flavor).o \
	.sg_compound_drop_game_under_test.$(flavor).d \
	sg_compound_drop_fanout_game_test.$(flavor) \
	.sg_compound_drop_fanout_game_test.$(flavor).o \
	.sg_compound_drop_fanout_game_test.$(flavor).d \
	sg_compound_drop_transition_test.$(flavor) \
	.sg_compound_drop_transition_test.$(flavor).o \
	.sg_compound_drop_transition_test.$(flavor).d \
	.sg_compound_drop_transition_under_test.$(flavor).o \
	.sg_compound_drop_transition_under_test.$(flavor).d)
COMPOUND_HOOK_LIVE_TEST_DEPS = $(COMPOUND_HOOK_LIVE_TEST_OBJS:.o=.d)
COMPOUND_HOOK_GAME_TEST_DEPS = $(COMPOUND_HOOK_GAME_TEST_OBJS:.o=.d)
COMPOUND_HOOK_GAME_EVENTS_TEST_DEPS = \
	$(COMPOUND_HOOK_GAME_EVENTS_TEST_OBJS:.o=.d)
COMPOUND_HOOK_GAME_EVENTS_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_compound_hook_game_events_test.$(flavor) \
	.sg_compound_hook_game_events_test.$(flavor).o \
	.sg_compound_hook_game_events_test.$(flavor).d \
	.sg_compound_hook_game_events_under_test.$(flavor).o \
	.sg_compound_hook_game_events_under_test.$(flavor).d)
COMPOUND_HOOK_TEST_ALL_ARTIFACTS = \
	$(foreach flavor,gnu make,sg_compound_hook_live_test.$(flavor) \
	.sg_compound_hook_live_test.$(flavor).o \
	.sg_compound_hook_live_test.$(flavor).d \
	.sg_compound_hook_live_fixture.$(flavor).o \
	.sg_compound_hook_live_fixture.$(flavor).d \
	.sg_compound_hook_live_safety_test.$(flavor).o \
	.sg_compound_hook_live_safety_test.$(flavor).d \
	.sg_compound_hook_live_under_test.$(flavor).o \
	.sg_compound_hook_live_under_test.$(flavor).d \
	.sg_compound_hook_live_finish_under_test.$(flavor).o \
	.sg_compound_hook_live_finish_under_test.$(flavor).d \
	.sg_compound_hook_live_compound_under_test.$(flavor).o \
	.sg_compound_hook_live_compound_under_test.$(flavor).d \
	.sg_compound_hook_live_action_under_test.$(flavor).o \
	.sg_compound_hook_live_action_under_test.$(flavor).d \
	.sg_compound_hook_live_replay_under_test.$(flavor).o \
	.sg_compound_hook_live_replay_under_test.$(flavor).d \
	.sg_compound_hook_live_hook_under_test.$(flavor).o \
	.sg_compound_hook_live_hook_under_test.$(flavor).d \
	.sg_compound_hook_live_publication_under_test.$(flavor).o \
	.sg_compound_hook_live_publication_under_test.$(flavor).d \
	sg_compound_hook_game_test.$(flavor) \
	.sg_compound_hook_game_test.$(flavor).o \
	.sg_compound_hook_game_test.$(flavor).d \
	.sg_compound_hook_game_under_test.$(flavor).o \
	.sg_compound_hook_game_under_test.$(flavor).d \
	.sg_compound_hook_game_lifecycle_under_test.$(flavor).o \
	.sg_compound_hook_game_lifecycle_under_test.$(flavor).d)
HOOK_LIVE_TEST_DEPS = $(HOOK_LIVE_TEST_OBJS:.o=.d)
ROCKETJUMP_LIVE_TEST_DEPS = $(ROCKETJUMP_LIVE_TEST_OBJS:.o=.d)
PUSH_LIVE_TEST_OBJS = .sg_push_falling_under_test.gnu.o
PUSH_LIVE_TEST_DEPS = $(PUSH_LIVE_TEST_OBJS:.o=.d)
ROCKETJUMP_CADENCE_TEST_DEPS = $(ROCKETJUMP_CADENCE_TEST_OBJS:.o=.d)
ROCKETJUMP_GAME_TEST_OBJS = .sg_rocketjump_game_q_shared_under_test.gnu.o
ROCKETJUMP_GAME_TEST_DEPS = $(ROCKETJUMP_GAME_TEST_OBJS:.o=.d)
ROCKETJUMP_TEST_ALL_ARTIFACTS = $(ROCKETJUMP_LIVE_TEST_OBJS) \
	$(ROCKETJUMP_LIVE_TEST_DEPS) $(PUSH_LIVE_TEST_OBJS) $(PUSH_LIVE_TEST_DEPS) \
	$(ROCKETJUMP_CADENCE_TEST_OBJS) $(ROCKETJUMP_CADENCE_TEST_DEPS) \
	$(ROCKETJUMP_GAME_TEST_OBJS) $(ROCKETJUMP_GAME_TEST_DEPS)
HOOK_DISCIPLINE_TEST_DEPS = $(HOOK_DISCIPLINE_TEST_OBJS:.o=.d)
ROTATOR_SWEEP_TEST_OBJS = .sg_rotator_sweep_q_shared_under_test.gnu.o
ROTATOR_SWEEP_TEST_DEPS = $(ROTATOR_SWEEP_TEST_OBJS:.o=.d)
MOVER_SUBJECT_SWEEP_TEST_OBJS = .sg_mover_subject_sweep_view_under_test.gnu.o \
	.sg_mover_subject_sweep_q_shared_under_test.gnu.o \
	.sg_mover_subject_sweep_pmove_under_test.gnu.o
MOVER_SUBJECT_SWEEP_TEST_DEPS = \
	$(MOVER_SUBJECT_SWEEP_TEST_OBJS:.o=.d)
MOVER_SUBJECT_SWEEP_TEST_ALL_ARTIFACTS = \
	.sg_mover_subject_sweep_oracle_under_test.gnu.d \
	.sg_mover_subject_sweep_util_under_test.gnu.d \
	.sg_mover_subject_sweep_view_under_test.gnu.o \
	.sg_mover_subject_sweep_view_under_test.gnu.d \
	.sg_mover_subject_sweep_q_shared_under_test.gnu.o \
	.sg_mover_subject_sweep_q_shared_under_test.gnu.d \
	.sg_mover_subject_sweep_pmove_under_test.gnu.o \
	.sg_mover_subject_sweep_pmove_under_test.gnu.d \
	.sg_mover_subject_sweep_oracle_under_test.make.o \
	.sg_mover_subject_sweep_oracle_under_test.make.d \
	.sg_mover_subject_sweep_util_under_test.make.o \
	.sg_mover_subject_sweep_util_under_test.make.d \
	.sg_mover_subject_sweep_view_under_test.make.o \
	.sg_mover_subject_sweep_view_under_test.make.d \
	.sg_mover_subject_sweep_q_shared_under_test.make.o \
	.sg_mover_subject_sweep_q_shared_under_test.make.d
COMPOUND_ORACLE_FIXTURE_STEMS = compound_oracle_fake_game \
	compound_oracle_fake_host compound_oracle_fixture \
	compound_swim_oracle_preopen_cases \
	compound_swim_oracle_recovery_cases compound_declared_oracle_cases \
	compound_hook_oracle_scenario
COMPOUND_ORACLE_FIXTURE_GNU_OBJS = \
	$(addprefix .sg_,$(addsuffix .gnu.o,$(COMPOUND_ORACLE_FIXTURE_STEMS)))
COMPOUND_ORACLE_ARTIFACT_STEMS = compound_swim_oracle_test \
	compound_hook_oracle_test compound_hook_oracle_fixture \
	$(COMPOUND_ORACLE_FIXTURE_STEMS) \
	compound_swim_oracle_oracle_under_test \
	compound_swim_oracle_rune_timing_under_test \
	compound_swim_oracle_replay_under_test \
	compound_swim_oracle_compound_under_test \
	compound_swim_oracle_world_under_test \
	compound_swim_oracle_q_shared_under_test \
	mover_subject_sweep_util_under_test
COMPOUND_ORACLE_ALL_ARTIFACTS = $(foreach flavor,gnu make,$(foreach stem, \
	$(COMPOUND_ORACLE_ARTIFACT_STEMS), .sg_$(stem).$(flavor).o \
	.sg_$(stem).$(flavor).d))
COMPOUND_SWIM_ORACLE_TEST_OBJS = .sg_compound_swim_oracle_q_shared_under_test.gnu.o
COMPOUND_SWIM_ORACLE_TEST_DEPS = \
	$(COMPOUND_SWIM_ORACLE_TEST_OBJS:.o=.d)
COMPOUND_SWIM_ORACLE_TEST_ALL_ARTIFACTS = \
	$(COMPOUND_ORACLE_ALL_ARTIFACTS)
COMPOUND_HOOK_ORACLE_TEST_OBJS = .sg_compound_swim_oracle_q_shared_under_test.gnu.o
COMPOUND_HOOK_ORACLE_TEST_DEPS = \
	$(COMPOUND_HOOK_ORACLE_TEST_OBJS:.o=.d)
COMPOUND_HOOK_ORACLE_TEST_ALL_ARTIFACTS = \
	$(COMPOUND_ORACLE_ALL_ARTIFACTS)
RUNE_DOOR_SCOPE_TEST_DEPS = $(RUNE_DOOR_SCOPE_TEST_OBJS:.o=.d)
ENTFILE_TEST_BIN = g_entfile_path_test.gnu
ENTFILE_TEST_OBJS = .g_entfile_path_test.gnu.o
ENTFILE_TEST_DEPS = $(ENTFILE_TEST_OBJS:.o=.d)
MAPLIST_ROTATION_TEST_BIN = maplist_rotation_test.gnu
MAPLIST_ROTATION_TEST_ALL_ARTIFACTS = \
	maplist_rotation_test.gnu maplist_rotation_test.make
ENGINE_SNAPSHOT_TEST = tests/test_engine_snapshot_name.sh
HOST_TEST_ALL_ARTIFACTS = sg_hooks_test sg_action_test \
	.sg_compound_action_under_test.gnu.d .sg_compound_action_under_test.make.o \
	.sg_compound_action_under_test.make.d \
	.sg_compound_world_q_shared_under_test.gnu.o \
	.sg_compound_world_q_shared_under_test.gnu.d \
	.sg_compound_world_q_shared_under_test.make.o \
	.sg_compound_world_q_shared_under_test.make.d sg_identity_test \
	.sg_rune_action_under_test.gnu.d .sg_rune_crc_under_test.gnu.o \
	.sg_rune_crc_under_test.gnu.d .sg_rune_action_under_test.make.o \
	.sg_rune_action_under_test.make.d .sg_rune_crc_under_test.make.o \
	.sg_rune_crc_under_test.make.d .sg_caco_projection_under_test.gnu.d \
	.sg_goal_projection_under_test.gnu.d .sg_caco_projection_under_test.make.o \
	.sg_caco_projection_under_test.make.d .sg_goal_projection_under_test.make.o \
	.sg_goal_projection_under_test.make.d .sg_drop_live_replay_under_test.gnu.d \
	.sg_drop_live_replay_under_test.make.o .sg_drop_live_replay_under_test.make.d \
	.sg_swim_live_replay_under_test.gnu.d .sg_swim_live_replay_under_test.make.o \
	.sg_swim_live_replay_under_test.make.d \
	.sg_rotator_sweep_q_shared_under_test.gnu.o \
	.sg_rotator_sweep_q_shared_under_test.gnu.d \
	.sg_rotator_sweep_q_shared_under_test.make.o \
	.sg_rotator_sweep_q_shared_under_test.make.d g_entfile_path_test.gnu \
	g_entfile_path_test.make .g_entfile_path_test.gnu.o \
	.g_entfile_path_test.gnu.d .g_entfile_path_test.make.o \
	.g_entfile_path_test.make.d

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
C_OBJS = g_menu.o g_replace.o g_runes.o g_ctffunc.o g_skins.o g_tourney.o \
	plasma.o ui_text.o ui_layout.o ui_boards.o p_observer.o g_chase.o p_stats.o \
	stdlog.o gslog.o bat.o g_vote.o ctf_file_io.o ctf_sqlite_core.o \
	ctf_sqlite_player.o ctf_sqlite_unidb.o sqlite3.o slipgate/sg_bot_cvars.o \
	slipgate/sg_bot_util.o slipgate/sg_bot_persona.o slipgate/sg_bot_roster.o \
	slipgate/sg_tactic_controller.o slipgate/sg_bites.o slipgate/sg_rune_cx.o \
	slipgate/sg_configuration_cells.o slipgate/sg_configuration_semantics.o \
	slipgate/sg_rune_cx_build.o slipgate/sg_rune_movement_build.o \
	slipgate/sg_rune_mechanisms.o slipgate/sg_rune_hook.o slipgate/sg_rune_entities.o \
	slipgate/sg_rune_vis.o slipgate/sg_rune_fire_build.o slipgate/sg_rune_generate.o \
	slipgate/sg_rune_game_generate.o \
	slipgate/sg_rune_analytic.o slipgate/sg_rune_movement.o \
	slipgate/sg_rune_artifact.o slipgate/sg_rune_bsp.o slipgate/sg_rune_crc.o \
	slipgate/sg_rune_trace.o slipgate/sg_rune_law.o slipgate/sg_bot_host.o \
	slipgate/sg_rune_locate.o slipgate/sg_rune_field.o slipgate/sg_rune_flight.o \
	slipgate/sg_rune_fire.o slipgate/sg_rune_level.o slipgate/sg_bot_frame.o \
	slipgate/sg_bot_orders.o slipgate/sg_bot_callout.o slipgate/sg_bot_combat.o \
	slipgate/sg_bot_items.o slipgate/sg_bot_weapons.o

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
LIBTOOL = true
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
ifneq ($(PLATFORM),Windows)
LIBTOOL = ldd -r
endif
endif
endif

# OS X wants to be Linux and FreeBSD too.
ifeq ($(shell uname),Darwin)
CFLAGS += -DLINUX
LDFLAGS = -ldl -lm
LIBTOOL = otool
endif

# A Windows module (MSYS or a MinGW cross build) has no libdl.
ifeq ($(PLATFORM),Windows)
LDFLAGS = -lm -lpthread
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

# Exact configuration construction is an offline game-module concern. The
# shipped module retains the compact artifact reader/runtime service only.
RUNE_COMPACT_GENERATOR_OFFLINE_OBJS = \
	slipgate/sg_configuration_cells.o \
	slipgate/sg_configuration_semantics.o \
	slipgate/sg_rune_cx_build.o \
	slipgate/sg_rune_movement_build.o \
	slipgate/sg_rune_mechanisms.o \
	slipgate/sg_rune_hook.o \
	slipgate/sg_rune_entities.o \
	slipgate/sg_rune_vis.o \
	slipgate/sg_rune_fire_build.o \
	slipgate/sg_rune_generate.o \
	slipgate/sg_rune_game_generate.o
RUNE_COMPACT_GENERATOR_RUNTIME_OBJS = $(OBJS)
RUNE_COMPACT_GENERATOR_OBJS = $(RUNE_COMPACT_GENERATOR_RUNTIME_OBJS)

# A slipgate header change rebuilds every slipgate object: the record
# layouts in these headers are shared by the runtime and the generator, and
# an object built against an old layout reads garbage silently.
$(filter slipgate/%.o,$(OBJS) $(RUNE_COMPACT_GENERATOR_OFFLINE_OBJS)): $(wildcard slipgate/*.h)

ifneq ($(filter Windows Cygwin,$(PLATFORM)),)
.PHONY: rune-compact-generator

# The runtime module is what a bare make builds; the generator has its own
# target and is never the default.
.DEFAULT_GOAL := all

rune-compact-generator:
	@echo "rune-compact-generator is unavailable in Windows game builds" >&2
	@false
else
RUNE_COMPACT_GENERATOR_TARGET = game$(ARCH)-lmctf-$(VER)-rune-generator.so

.PHONY: rune-compact-generator rune-compact-generator-requirements

rune-compact-generator: $(RUNE_COMPACT_GENERATOR_TARGET)

# The generator no longer has any external library dependency; this target
# is kept as a no-op so callers that still invoke it as a prerequisite are
# unaffected.
rune-compact-generator-requirements:
	@:

$(RUNE_COMPACT_GENERATOR_TARGET): $(RUNE_COMPACT_GENERATOR_OBJS)
	$(CC) $(CFLAGS) $(SHLIBLDFLAGS) -o $@ $^ $(LDFLAGS)
endif

######################################################################
# Targets
######################################################################

POVLOCK_TEST_OBJS = .povlock_endframe_under_test.gnu.o
POVLOCK_TEST_DEPS = $(POVLOCK_TEST_OBJS:.o=.d)
POV_SESSION_TEST_OBJS = .pov_session_chase_under_test.gnu.o \
	.pov_session_client_under_test.gnu.o .pov_session_identity_under_test.gnu.o
POV_SESSION_TEST_DEPS = $(POV_SESSION_TEST_OBJS:.o=.d)
POV_SUPERVISOR_BIN = tools/pov-supervisor
POV_SUPERVISOR_ALL_ARTIFACTS = tools/pov-supervisor


.PHONY: tactic-controller-test
tactic-controller-test: tests/run_sg_tactic_controller_test.sh \
		tests/sg_tactic_controller_test.c \
		slipgate/sg_tactic_controller.c slipgate/sg_tactic_controller.h
	sh tests/run_sg_tactic_controller_test.sh


all: dep $(TARGET)
runtime-module-path:
	@printf '%s\n' "$(TARGET)"
host-law-runtime-link-check: dep $(TARGET) $(HOST_LAW_RUNTIME_LINK_CHECK)
	sh $(HOST_LAW_RUNTIME_LINK_CHECK) $(TARGET)
FORCE:
$(REVISION_HEADER): $(REVISION_TEMPLATE) FORCE
	@echo "Generating $@..." >&2
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

# These production objects live below slipgate/, while makedepend emits their
# targets without that directory prefix.  Keep the real object paths tied to
# the generated wire contract so an incremental build cannot mix contracts.


.c.o:
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SHLIBCFLAGS) -o $@ -c $<

$(TARGET):	$(OBJS) $(L_OBJS)
		$(CC) $(CFLAGS) $(SHLIBLDFLAGS) -o $@ $(OBJS) $(L_OBJS) $(LDFLAGS)
		$(LIBTOOL) $@



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

$(RUNE_COMPACT_READER_BIN): $(RUNE_COMPACT_READER_SOURCES) \
		$(RUNE_COMPACT_READER_HEADERS)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Wpedantic \
		-Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes \
		-Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align -I. \
		-o $@ $(RUNE_COMPACT_READER_SOURCES) $(LDFLAGS) -lm




$(DANGER_LEASE_TEST_BIN): $(DANGER_LEASE_TEST_OBJS)
	$(CC) -o $@ $(DANGER_LEASE_TEST_OBJS) $(LDFLAGS)

$(DANGER_POLICY_TEST_BIN): $(DANGER_POLICY_TEST_OBJS)
	$(CC) -o $@ $(DANGER_POLICY_TEST_OBJS) $(LDFLAGS)

$(DANGER_TEST_BIN): $(DANGER_TEST_OBJS)
	$(CC) -o $@ $(DANGER_TEST_OBJS) $(LDFLAGS)

$(FIELDS_CANDIDATE_TEST_BIN): $(FIELDS_CANDIDATE_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(FIELDS_CANDIDATE_TEST_OBJS) $(LDFLAGS)

$(TACTIC_POLICY_TEST_BIN): $(TACTIC_POLICY_TEST_OBJS)
	$(CC) -o $@ $(TACTIC_POLICY_TEST_OBJS) $(LDFLAGS)

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

$(ROCKETJUMP_LIVE_TEST_BIN): $(ROCKETJUMP_LIVE_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(ROCKETJUMP_LIVE_TEST_OBJS) $(LDFLAGS)

$(PUSH_LIVE_TEST_BIN): $(PUSH_LIVE_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(PUSH_LIVE_TEST_OBJS) $(LDFLAGS)

$(ROCKETJUMP_CADENCE_TEST_BIN): $(ROCKETJUMP_CADENCE_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(ROCKETJUMP_CADENCE_TEST_OBJS) $(LDFLAGS)

$(ROCKETJUMP_GAME_TEST_BIN): $(ROCKETJUMP_GAME_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(ROCKETJUMP_GAME_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_TEST_BIN): $(COMPOUND_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_TEST_OBJS) $(LDFLAGS)

$(MOVER_LEASE_TEST_BIN): $(MOVER_LEASE_TEST_OBJS)
	$(CC) -o $@ $(MOVER_LEASE_TEST_OBJS) $(LDFLAGS)

$(WATER_FOREST_TEST_BIN): $(WATER_FOREST_TEST_OBJS)
	$(CC) -o $@ $(WATER_FOREST_TEST_OBJS) $(LDFLAGS)

$(BUTTON_LIVE_TEST_BIN): $(BUTTON_LIVE_TEST_OBJS)
	$(CC) -o $@ $(BUTTON_LIVE_TEST_OBJS) $(LDFLAGS)

$(TRAIN_GATE_LIVE_TEST_BIN): $(TRAIN_GATE_LIVE_TEST_OBJS)
	$(CC) -o $@ $(TRAIN_GATE_LIVE_TEST_OBJS) $(LDFLAGS)

$(MECHANISM_TIMELINE_TEST_BIN): $(MECHANISM_TIMELINE_TEST_OBJS)
	$(CC) -o $@ $(MECHANISM_TIMELINE_TEST_OBJS) $(LDFLAGS)

$(RELAY_WALL_TRANSACTION_TEST_BIN): $(RELAY_WALL_TRANSACTION_TEST_OBJS)
	$(CC) -o $@ $(RELAY_WALL_TRANSACTION_TEST_OBJS) $(LDFLAGS)

$(RELAY_WALL_OBJECTIVE_TEST_BIN): $(RELAY_WALL_OBJECTIVE_TEST_OBJS)
	$(CC) -o $@ $(RELAY_WALL_OBJECTIVE_TEST_OBJS) $(LDFLAGS)

$(SHOOT_DOOR_LIVE_TEST_BIN): $(SHOOT_DOOR_LIVE_TEST_OBJS)
	$(CC) -o $@ $(SHOOT_DOOR_LIVE_TEST_OBJS) $(LDFLAGS)

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

$(COMPOUND_GEN_GAME_TEST_BIN): $(COMPOUND_GEN_GAME_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_GEN_GAME_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_PUBLICATION_TEST_BIN): $(COMPOUND_PUBLICATION_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_PUBLICATION_TEST_OBJS) $(LDFLAGS) -lm

$(RUNE_INSTALL_TEST_BIN): $(RUNE_INSTALL_TEST_OBJS)
	$(CC) -o $@ $(RUNE_INSTALL_TEST_OBJS) $(LDFLAGS)
$(RUNE_PROOF_TEST_BIN): $(RUNE_PROOF_TEST_OBJS)
	$(CC) -o $@ $(RUNE_PROOF_TEST_OBJS) $(LDFLAGS)

$(REPLAY_TEST_BIN): $(REPLAY_TEST_OBJS)
	$(CC) -o $@ $(REPLAY_TEST_OBJS) $(LDFLAGS)

$(CHAIN_HOOK_REPLAY_TEST_BIN): $(CHAIN_HOOK_REPLAY_TEST_OBJS)
	$(CC) -o $@ $(CHAIN_HOOK_REPLAY_TEST_OBJS) $(LDFLAGS)
$(DROP_LIVE_TEST_BIN): $(DROP_LIVE_TEST_OBJS)
	$(CC) -o $@ $(DROP_LIVE_TEST_OBJS) $(LDFLAGS)

$(SWIM_LIVE_TEST_BIN): $(SWIM_LIVE_TEST_OBJS)
	$(CC) -o $@ $(SWIM_LIVE_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_SWIM_LIVE_TEST_BIN): $(COMPOUND_SWIM_LIVE_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_SWIM_LIVE_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_SWIM_GAME_TEST_BIN): $(COMPOUND_SWIM_GAME_TEST_OBJS)
	$(CC) -o $@ $(COMPOUND_SWIM_GAME_TEST_OBJS) $(LDFLAGS)
$(COMPOUND_DROP_LIVE_TEST_BIN): $(COMPOUND_DROP_LIVE_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(COMPOUND_DROP_LIVE_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_DROP_GAME_TEST_BIN): $(COMPOUND_DROP_GAME_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(COMPOUND_DROP_GAME_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_DROP_FANOUT_TEST_BIN): $(COMPOUND_DROP_FANOUT_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_DROP_FANOUT_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_DROP_TRANSITION_TEST_BIN): $(COMPOUND_DROP_TRANSITION_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_DROP_TRANSITION_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_HOOK_LIVE_TEST_BIN): $(COMPOUND_HOOK_LIVE_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_HOOK_LIVE_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_HOOK_GAME_TEST_BIN): $(COMPOUND_HOOK_GAME_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_HOOK_GAME_TEST_OBJS) $(LDFLAGS)

$(COMPOUND_HOOK_GAME_EVENTS_TEST_BIN): \
		$(COMPOUND_HOOK_GAME_EVENTS_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_HOOK_GAME_EVENTS_TEST_OBJS) $(LDFLAGS)

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

$(COMPOUND_HOOK_ORACLE_TEST_BIN): $(COMPOUND_HOOK_ORACLE_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_HOOK_ORACLE_TEST_OBJS) $(LDFLAGS)

$(RUNE_DOOR_SCOPE_TEST_BIN): $(RUNE_DOOR_SCOPE_TEST_OBJS)
	$(CC) -o $@ $(RUNE_DOOR_SCOPE_TEST_OBJS) $(LDFLAGS)

$(ENTFILE_TEST_BIN): $(ENTFILE_TEST_OBJS)
	$(CC) -o $@ $(ENTFILE_TEST_OBJS) $(LDFLAGS)



















.sg_push_falling_under_test.gnu.o: p_view.c g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -Wno-unused-parameter \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<






.sg_rocketjump_game_q_shared_under_test.gnu.o: q_shared.c \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections \
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


















.sg_compound_world_q_shared_under_test.gnu.o: q_shared.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<














.sg_rune_crc_under_test.gnu.o: $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<















.sg_delayed_relay_dispatch_view_under_test.gnu.o: p_view.c \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -Wno-unused-parameter \
		-ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_game_utils_under_test.gnu.o: g_utils.c \
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


.sg_q_shared_under_test.gnu.o: q_shared.c \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<




























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


























































.sg_rotator_sweep_q_shared_under_test.gnu.o: q_shared.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
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

.sg_mover_subject_sweep_pmove_under_test.gnu.o: \
		tests/support/yq2_pmove.c q_shared.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -DDEDICATED_ONLY \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<










.sg_compound_swim_oracle_q_shared_under_test.gnu.o: \
		q_shared.c $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
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


.sg_spectator_sound_net_under_test.gnu.o: \
		g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

povlock-test: $(POVLOCK_TEST_BIN)
	@echo "[TEST] $<"
	@./$(POVLOCK_TEST_BIN)

$(POVLOCK_TEST_BIN): $(POVLOCK_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(POVLOCK_TEST_OBJS) $(LDFLAGS)



.povlock_endframe_under_test.gnu.o: p_view.c g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-unused-parameter -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

pov-session-production-test: $(POV_SESSION_TEST_BIN)
	@echo "[TEST] $<"
	@./$(POV_SESSION_TEST_BIN)

$(POV_SESSION_TEST_BIN): $(POV_SESSION_TEST_OBJS)
	$(CC) -Wl,--gc-sections -o $@ $(POV_SESSION_TEST_OBJS) $(LDFLAGS)


.pov_session_chase_under_test.gnu.o: g_chase.c g_local.h $(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.pov_session_client_under_test.gnu.o: p_client.c g_local.h g_tourney.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-unused-parameter -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.pov_session_identity_under_test.gnu.o: \
		g_local.h slipgate/sg_bot.h \
		$(REVISION_HEADER)
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<































runegen-test: tests/run_runegen_test.sh tools/runegen.sh \
		tools/rune-corpus-maps.txt
	sh tests/run_runegen_test.sh


rune-compact-test: runegen-test







action-test: $(ACTION_TEST_BIN)
	./$(ACTION_TEST_BIN)

# Linux tools-only supervisor: intentionally absent from Visual Studio projects.
pov-supervisor-test: $(POV_SUPERVISOR_BIN)
	./$(POV_SUPERVISOR_TEST_BIN)

$(POV_SUPERVISOR_BIN): tools/pov-supervisor.c tools/pov-spawn-linux.c tools/pov-spawn-linux.h
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Itools -o $@ \
		tools/pov-supervisor.c tools/pov-spawn-linux.c


button-live-test: $(BUTTON_LIVE_TEST_BIN)
	./$(BUTTON_LIVE_TEST_BIN)

train-gate-live-test: $(TRAIN_GATE_LIVE_TEST_BIN)
	./$(TRAIN_GATE_LIVE_TEST_BIN)

mechanism-timeline-test: $(MECHANISM_TIMELINE_TEST_BIN)
	./$(MECHANISM_TIMELINE_TEST_BIN)

relay-wall-transaction-test: $(RELAY_WALL_TRANSACTION_TEST_BIN)
	./$(RELAY_WALL_TRANSACTION_TEST_BIN)

relay-wall-objective-test: $(RELAY_WALL_OBJECTIVE_TEST_BIN)
	./$(RELAY_WALL_OBJECTIVE_TEST_BIN)













shoot-door-live-test: $(SHOOT_DOOR_LIVE_TEST_BIN)
	./$(SHOOT_DOOR_LIVE_TEST_BIN)

button-game-test: $(BUTTON_GAME_TEST_BIN)
	./$(BUTTON_GAME_TEST_BIN)

compound-test: $(COMPOUND_TEST_BIN)
	./$(COMPOUND_TEST_BIN)

mover-lease-test: $(MOVER_LEASE_TEST_BIN)
	./$(MOVER_LEASE_TEST_BIN)

water-forest-test: $(WATER_FOREST_TEST_BIN)
	./$(WATER_FOREST_TEST_BIN)

compound-guard-test: $(COMPOUND_GUARD_TEST_BIN)
	./$(COMPOUND_GUARD_TEST_BIN)

compound-guard-game-test: $(COMPOUND_GUARD_GAME_TEST_BIN)
	./$(COMPOUND_GUARD_GAME_TEST_BIN)

declared-door-guard-test: $(DECLARED_DOOR_GUARD_TEST_BIN)
	./$(DECLARED_DOOR_GUARD_TEST_BIN)

compound-world-test: $(COMPOUND_WORLD_TEST_BIN)
	./$(COMPOUND_WORLD_TEST_BIN)

compound-gen-test: $(COMPOUND_GEN_TEST_BIN) $(COMPOUND_GEN_GAME_TEST_BIN)
	./$(COMPOUND_GEN_TEST_BIN)
	./$(COMPOUND_GEN_GAME_TEST_BIN)

compound-publication-test: $(COMPOUND_PUBLICATION_TEST_BIN)
	./$(COMPOUND_PUBLICATION_TEST_BIN)

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




danger-lease-test: $(DANGER_LEASE_TEST_BIN)
	./$(DANGER_LEASE_TEST_BIN)

danger-policy-test: $(DANGER_POLICY_TEST_BIN)
	./$(DANGER_POLICY_TEST_BIN)

danger-test: $(DANGER_TEST_BIN)
	./$(DANGER_TEST_BIN)

fields-candidate-test: $(FIELDS_CANDIDATE_TEST_BIN)
	./$(FIELDS_CANDIDATE_TEST_BIN)

tactic-policy-test: $(TACTIC_POLICY_TEST_BIN)
	./$(TACTIC_POLICY_TEST_BIN)

human-speed-test: $(HUMAN_SPEED_TEST_BIN)
	./$(HUMAN_SPEED_TEST_BIN)

human-trace-test:
	tmp=$$(mktemp -d); \
		trap 'rm -f "$$tmp/humantrace-tracehook.jsonl"; rmdir "$$tmp"' \
			EXIT HUP INT TERM; \
		./$(HUMAN_TRACE_HOOK_TEST_BIN) "$$tmp"; \
		./$(HUMAN_TRACE_HOOK_TEST_BIN) "$$tmp" json-identity; \
		./$(HUMAN_TRACE_HOOK_TEST_BIN) "$$tmp" collection-alloc; \
		./$(HUMAN_TRACE_HOOK_TEST_BIN) "$$tmp" writefail

door-approach-test: $(DOOR_APPROACH_TEST_BIN)
	./$(DOOR_APPROACH_TEST_BIN)

defense-shift-test: $(DEFENSE_SHIFT_TEST_BIN)
	./$(DEFENSE_SHIFT_TEST_BIN)

defense-supply-test: $(DEFENSE_SUPPLY_TEST_BIN)
	./$(DEFENSE_SUPPLY_TEST_BIN)

strike-adapter-test: $(STRIKE_ADAPTER_TEST_BIN)
	./$(STRIKE_ADAPTER_TEST_BIN)

item-commitment-test: $(ITEM_COMMITMENT_TEST_BIN)
	./$(ITEM_COMMITMENT_TEST_BIN)

hook-diagnostics-test: $(HOOK_DIAGNOSTICS_TEST_BIN)
	./$(HOOK_DIAGNOSTICS_TEST_BIN)
run-handoff-test: $(RUN_HANDOFF_TEST_BIN)
	./$(RUN_HANDOFF_TEST_BIN)
rune-install-test: $(RUNE_INSTALL_TEST_BIN)
	./$(RUNE_INSTALL_TEST_BIN)
rune-proof-test: $(RUNE_PROOF_TEST_BIN)
	./$(RUNE_PROOF_TEST_BIN)
rune-objective-diagnostics-test:
	./$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN)

replay-test: $(REPLAY_TEST_BIN)
	./$(REPLAY_TEST_BIN)
chain-hook-replay-test: $(CHAIN_HOOK_REPLAY_TEST_BIN)
	./$(CHAIN_HOOK_REPLAY_TEST_BIN)


swim-live-test: $(SWIM_LIVE_TEST_BIN)
	./$(SWIM_LIVE_TEST_BIN)

compound-swim-live-test: $(COMPOUND_SWIM_LIVE_TEST_BIN)
	./$(COMPOUND_SWIM_LIVE_TEST_BIN)

compound-swim-game-test: $(COMPOUND_SWIM_GAME_TEST_BIN)
	./$(COMPOUND_SWIM_GAME_TEST_BIN)
compound-drop-live-test: $(COMPOUND_DROP_LIVE_TEST_BIN)
	./$(COMPOUND_DROP_LIVE_TEST_BIN)

compound-drop-game-test: $(COMPOUND_DROP_GAME_TEST_BIN) \
		$(COMPOUND_DROP_FANOUT_TEST_BIN)
	./$(COMPOUND_DROP_GAME_TEST_BIN)
	./$(COMPOUND_DROP_FANOUT_TEST_BIN)

compound-drop-transition-test: $(COMPOUND_DROP_TRANSITION_TEST_BIN)
	./$(COMPOUND_DROP_TRANSITION_TEST_BIN)

compound-hook-live-test: $(COMPOUND_HOOK_LIVE_TEST_BIN)
	./$(COMPOUND_HOOK_LIVE_TEST_BIN)

compound-hook-game-test: $(COMPOUND_HOOK_GAME_TEST_BIN)
	./$(COMPOUND_HOOK_GAME_TEST_BIN)

compound-hook-game-events-test: $(COMPOUND_HOOK_GAME_EVENTS_TEST_BIN)
	./$(COMPOUND_HOOK_GAME_EVENTS_TEST_BIN)

hook-live-test: $(HOOK_LIVE_TEST_BIN)
	./$(HOOK_LIVE_TEST_BIN)

hook-discipline-test: $(HOOK_DISCIPLINE_TEST_BIN)
	./$(HOOK_DISCIPLINE_TEST_BIN)

rotator-sweep-test: $(ROTATOR_SWEEP_TEST_BIN)
	./$(ROTATOR_SWEEP_TEST_BIN)

mover-subject-sweep-test: $(MOVER_SUBJECT_SWEEP_TEST_BIN)
	./$(MOVER_SUBJECT_SWEEP_TEST_BIN)

compound-swim-oracle-test: $(COMPOUND_SWIM_ORACLE_TEST_BIN)
	./$(COMPOUND_SWIM_ORACLE_TEST_BIN)

compound-hook-oracle-test: $(COMPOUND_HOOK_ORACLE_TEST_BIN)
	./$(COMPOUND_HOOK_ORACLE_TEST_BIN)

rune-door-scope-test: $(RUNE_DOOR_SCOPE_TEST_BIN)
	./$(RUNE_DOOR_SCOPE_TEST_BIN)


entfile-test: $(ENTFILE_TEST_BIN)
	./$(ENTFILE_TEST_BIN)

maplist-rotation-test: $(MAPLIST_ROTATION_TEST_BIN)
	./$(MAPLIST_ROTATION_TEST_BIN)

snapshot-test:
	./$(ENGINE_SNAPSHOT_TEST)

dep:


stripcr:	.
		@echo "Stripping carriage returns from source files..."
	 	@for f in *.[ch]; do \
		  cat $$f | tr -d '\015' > .stripcr; \
		  mv .stripcr $$f; \
		done; \
		rm -f .stripcr

distclean:
		@echo "Deleting everything that can be rebuilt..."

ifeq (,$(filter distclean,$(MAKECMDGOALS)))
ifeq ($(DEPEND_FILE),$(wildcard $(DEPEND_FILE)))
include
endif
-include $(HOST_TEST_DEPS)
-include $(POVLOCK_TEST_DEPS)
-include $(POV_SESSION_TEST_DEPS)
-include $(ACTION_TEST_DEPS)
-include $(COMPOUND_TEST_DEPS)
-include $(MOVER_LEASE_TEST_DEPS)
-include $(WATER_FOREST_TEST_DEPS)
-include $(BUTTON_LIVE_TEST_DEPS)
-include $(TRAIN_GATE_LIVE_TEST_DEPS)
-include $(MECHANISM_TIMELINE_TEST_DEPS)
-include $(RELAY_WALL_TRANSACTION_TEST_DEPS)
-include $(RELAY_WALL_OBJECTIVE_TEST_DEPS)
-include $(SHOOT_DOOR_LIVE_TEST_DEPS)
-include $(BUTTON_GAME_TEST_DEPS)
-include $(COMPOUND_GUARD_TEST_DEPS)
-include $(COMPOUND_GUARD_GAME_TEST_DEPS)
-include $(DECLARED_DOOR_GUARD_TEST_DEPS)
-include $(COMPOUND_WORLD_TEST_DEPS)
-include $(COMPOUND_GEN_TEST_DEPS)
-include $(COMPOUND_GEN_GAME_TEST_DEPS)
-include $(COMPOUND_PUBLICATION_TEST_DEPS)
-include $(IDENTITY_TEST_DEPS)
-include $(RUNE_CODEC_TEST_DEPS)
-include $(RUNE_ARTIFACT_LOADER_TEST_DEPS)
-include $(RUNE_ARTIFACT_WRITER_TEST_DEPS)
-include $(RUNE_MECHANISM_PLAN_TEST_DEPS)
-include $(RUNE_MECHANISM_CATALOG_TEST_DEPS)
-include $(RUNE_MECHANISM_EXECUTION_TEST_DEPS)
-include $(RUNE_BINDING_TEST_DEPS)
-include $(DANGER_LEASE_TEST_DEPS)
-include $(DANGER_POLICY_TEST_DEPS)
-include $(DANGER_TEST_DEPS)
-include $(FIELDS_CANDIDATE_TEST_DEPS)
-include $(TACTIC_POLICY_TEST_DEPS)
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
-include $(CHAIN_HOOK_REPLAY_TEST_DEPS)
-include $(DROP_LIVE_TEST_DEPS)
-include $(SWIM_LIVE_TEST_DEPS)
-include $(COMPOUND_SWIM_LIVE_TEST_DEPS)
-include $(ROCKETJUMP_LIVE_TEST_DEPS)
-include $(PUSH_LIVE_TEST_DEPS)
-include $(ROCKETJUMP_CADENCE_TEST_DEPS)
-include $(ROCKETJUMP_GAME_TEST_DEPS)
-include $(COMPOUND_DROP_LIVE_TEST_DEPS)
-include $(COMPOUND_DROP_GAME_TEST_DEPS)
-include $(COMPOUND_DROP_FANOUT_TEST_DEPS)
-include $(COMPOUND_DROP_TRANSITION_TEST_DEPS)
-include $(COMPOUND_HOOK_LIVE_TEST_DEPS)
-include $(COMPOUND_HOOK_GAME_TEST_DEPS)
-include $(COMPOUND_HOOK_GAME_EVENTS_TEST_DEPS)
-include $(HOOK_LIVE_TEST_DEPS)
-include $(HOOK_DISCIPLINE_TEST_DEPS)
-include $(ROTATOR_SWEEP_TEST_DEPS)
-include $(MOVER_SUBJECT_SWEEP_TEST_DEPS)
-include $(COMPOUND_SWIM_ORACLE_TEST_DEPS)
-include $(COMPOUND_HOOK_ORACLE_TEST_DEPS)
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

# The era-4 cell builder on one map: counts and timing.
CELLSDUMP_BIN = cellsdump.gnu
CELLSDUMP_SRCS = slipgate/sg_configuration_cells.c \
	slipgate/sg_configuration_semantics.c slipgate/sg_rune_cx.c \
	slipgate/sg_rune_cx_build.c slipgate/sg_rune_movement.c \
	slipgate/sg_rune_movement_build.c slipgate/sg_rune_flight.c \
	slipgate/sg_rune_analytic.c slipgate/sg_rune_artifact.c \
	slipgate/sg_rune_mechanisms.c slipgate/sg_rune_hook.c \
	slipgate/sg_rune_vis.c slipgate/sg_rune_fire.c slipgate/sg_rune_fire_build.c \
	slipgate/sg_rune_locate.c slipgate/sg_rune_bsp.c slipgate/sg_rune_trace.c \
	slipgate/sg_rune_law.c slipgate/sg_rune_crc.c slipgate/sg_rune_entities.c

$(CELLSDUMP_BIN): tools/cellsdump.c $(CELLSDUMP_SRCS)
	$(CC) -std=c11 -O2 -g -Wall -Wextra -I. tools/cellsdump.c \
		$(CELLSDUMP_SRCS) -lm -o $@
.PHONY: cellsdump
cellsdump: $(CELLSDUMP_BIN)

.PHONY: rune-analytic-test
rune-analytic-test: tests/run_sg_rune_analytic_test.sh tests/sg_rune_analytic_test.c \
		slipgate/sg_rune_analytic.c slipgate/sg_rune_analytic.h
	sh tests/run_sg_rune_analytic_test.sh

.PHONY: rune-movement-test
rune-movement-test: tests/run_sg_rune_movement_test.sh tests/sg_rune_movement_test.c \
		slipgate/sg_rune_movement.c slipgate/sg_rune_movement.h \
		slipgate/sg_rune_analytic.c slipgate/sg_rune_analytic.h
	sh tests/run_sg_rune_movement_test.sh

.PHONY: rune-runtime-test
rune-runtime-test: tests/run_sg_rune_runtime_test.sh tests/sg_rune_runtime_test.c \
		slipgate/sg_rune_locate.c slipgate/sg_rune_field.c slipgate/sg_rune_flight.c \
		slipgate/sg_rune_artifact.c slipgate/sg_rune_cx.c slipgate/sg_rune_movement.c \
		slipgate/sg_rune_movement_build.c slipgate/sg_rune_analytic.c
	sh tests/run_sg_rune_runtime_test.sh

.PHONY: rune-artifact-test
rune-artifact-test: tests/run_sg_rune_artifact_test.sh tests/sg_rune_artifact_test.c \
		slipgate/sg_rune_artifact.c slipgate/sg_rune_artifact.h \
		slipgate/sg_rune_cx.c slipgate/sg_rune_cx.h \
		slipgate/sg_rune_movement.c slipgate/sg_rune_analytic.c
	sh tests/run_sg_rune_artifact_test.sh

# Era-4 field check: load an artifact, locate two points, walk the route.
FIELDCHECK_BIN = fieldcheck
.PHONY: fieldcheck
fieldcheck: $(FIELDCHECK_BIN)
$(FIELDCHECK_BIN): tools/fieldcheck.c slipgate/sg_rune_locate.c \
		slipgate/sg_rune_field.c slipgate/sg_rune_artifact.c \
		slipgate/sg_rune_cx.c slipgate/sg_rune_movement.c \
		slipgate/sg_rune_analytic.c slipgate/sg_rune_law.c slipgate/sg_rune_crc.c
	$(CC) -std=c11 -O2 -g -Wall -Wextra -I. tools/fieldcheck.c \
		slipgate/sg_rune_locate.c slipgate/sg_rune_field.c \
		slipgate/sg_rune_artifact.c slipgate/sg_rune_cx.c \
		slipgate/sg_rune_movement.c slipgate/sg_rune_analytic.c \
		slipgate/sg_rune_law.c slipgate/sg_rune_crc.c slipgate/sg_rune_flight.c -lm -o $@
