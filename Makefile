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
ACTION_TEST_BIN := sg_action_test.make
ACTION_TEST_OBJS := .sg_action_test.make.o .sg_action_under_test.make.o
ACTION_TEST_DEPS := $(ACTION_TEST_OBJS:.o=.d)
COMPOUND_TEST_BIN := sg_compound_test.make
COMPOUND_TEST_OBJS := .sg_compound_test.make.o \
	.sg_compound_under_test.make.o .sg_compound_action_under_test.make.o
COMPOUND_TEST_DEPS := $(COMPOUND_TEST_OBJS:.o=.d)
MOVER_LEASE_TEST_BIN := sg_mover_lease_test.make
MOVER_LEASE_TEST_OBJS := .sg_mover_lease_test.make.o \
	.sg_mover_lease_under_test.make.o
MOVER_LEASE_TEST_DEPS := $(MOVER_LEASE_TEST_OBJS:.o=.d)
MOVER_LEASE_TEST_ALL_ARTIFACTS := \
	sg_mover_lease_test.gnu sg_mover_lease_test.make \
	.sg_mover_lease_test.gnu.o .sg_mover_lease_test.gnu.d \
	.sg_mover_lease_under_test.gnu.o .sg_mover_lease_under_test.gnu.d \
	$(MOVER_LEASE_TEST_OBJS) $(MOVER_LEASE_TEST_DEPS)
COMPOUND_WORLD_TEST_BIN := sg_compound_world_test.make
COMPOUND_WORLD_TEST_OBJS := .sg_compound_world_test.make.o \
	.sg_compound_world_under_test.make.o \
	.sg_compound_world_q_shared_under_test.make.o
COMPOUND_WORLD_TEST_DEPS := $(COMPOUND_WORLD_TEST_OBJS:.o=.d)
COMPOUND_GEN_TEST_BIN := sg_compound_gen_test.make
COMPOUND_GEN_TEST_OBJS := .sg_compound_gen_test.make.o \
	.sg_compound_gen_under_test.make.o
COMPOUND_GEN_TEST_DEPS := $(COMPOUND_GEN_TEST_OBJS:.o=.d)
COMPOUND_GEN_TEST_ALL_ARTIFACTS := \
	sg_compound_gen_test sg_compound_gen_test.gnu sg_compound_gen_test.make \
	.sg_compound_gen_test.gnu.o .sg_compound_gen_test.gnu.d \
	.sg_compound_gen_under_test.gnu.o .sg_compound_gen_under_test.gnu.d \
	.sg_compound_gen_test.make.o .sg_compound_gen_test.make.d \
	.sg_compound_gen_under_test.make.o .sg_compound_gen_under_test.make.d
COMPOUND_PUBLICATION_TEST_BIN := sg_compound_publication_test.make
COMPOUND_PUBLICATION_TEST_OBJS := .sg_compound_publication_test.make.o \
	.sg_compound_publication_under_test.make.o
COMPOUND_PUBLICATION_TEST_DEPS := $(COMPOUND_PUBLICATION_TEST_OBJS:.o=.d)
COMPOUND_PUBLICATION_INTEGRATION_TEST := \
	tests/test_compound_publication_integration.py
COMPOUND_PUBLICATION_TEST_ALL_ARTIFACTS := \
	sg_compound_publication_test.gnu sg_compound_publication_test.make \
	.sg_compound_publication_test.gnu.o \
	.sg_compound_publication_test.gnu.d \
	.sg_compound_publication_under_test.gnu.o \
	.sg_compound_publication_under_test.gnu.d \
	$(COMPOUND_PUBLICATION_TEST_OBJS) \
	$(COMPOUND_PUBLICATION_TEST_DEPS)
IDENTITY_TEST_BIN := sg_identity_test.make
IDENTITY_TEST_OBJS := .sg_identity_test.make.o .sg_identity_under_test.make.o \
	.sg_crc32_under_test.make.o
IDENTITY_TEST_DEPS := $(IDENTITY_TEST_OBJS:.o=.d)
RUNE_WIRE_TEST_BIN := sg_rune_wire_test.make
RUNE_WIRE_TEST_OBJS := .sg_rune_wire_test.make.o \
	.sg_rune_wire_under_test.make.o .sg_rune_wire_action_under_test.make.o \
	.sg_rune_wire_crc_under_test.make.o
RUNE_WIRE_TEST_DEPS := $(RUNE_WIRE_TEST_OBJS:.o=.d)
SIDECAR_WIRE_TEST_BIN := sg_sidecar_wire_test.make
SIDECAR_WIRE_TEST_OBJS := .sg_sidecar_wire_test.make.o \
	.sg_sidecar_wire_under_test.make.o .sg_rune_wire_under_test.make.o \
	.sg_rune_wire_action_under_test.make.o .sg_rune_wire_crc_under_test.make.o
SIDECAR_WIRE_TEST_DEPS := $(SIDECAR_WIRE_TEST_OBJS:.o=.d)
SIDECAR_LOADER_TEST_BIN := sg_sidecar_loader_test.make
SIDECAR_LOADER_TEST_OBJS := .sg_sidecar_loader_test.make.o \
	.sg_sidecar_loader_under_test.make.o .sg_sidecar_wire_under_test.make.o \
	.sg_rune_wire_under_test.make.o .sg_rune_wire_action_under_test.make.o \
	.sg_rune_wire_crc_under_test.make.o
SIDECAR_LOADER_TEST_DEPS := $(SIDECAR_LOADER_TEST_OBJS:.o=.d)
SIDECAR_STORE_TEST_BIN := sg_sidecar_store_test.make
SIDECAR_STORE_TEST_OBJS := .sg_sidecar_store_test.make.o \
	.sg_sidecar_store_under_test.make.o .sg_sidecar_loader_under_test.make.o \
	.sg_sidecar_wire_under_test.make.o .sg_rune_wire_under_test.make.o \
	.sg_rune_wire_action_under_test.make.o .sg_rune_wire_crc_under_test.make.o
SIDECAR_STORE_TEST_DEPS := $(SIDECAR_STORE_TEST_OBJS:.o=.d)
DANGER_LEASE_TEST_BIN := sg_danger_lease_test.make
DANGER_LEASE_TEST_OBJS := .sg_danger_lease_test.make.o \
	.sg_danger_lease_under_test.make.o
DANGER_LEASE_TEST_DEPS := $(DANGER_LEASE_TEST_OBJS:.o=.d)
DANGER_POLICY_TEST_BIN := sg_danger_policy_test.make
DANGER_POLICY_TEST_OBJS := .sg_danger_policy_test.make.o \
	.sg_danger_policy_under_test.make.o
DANGER_POLICY_TEST_DEPS := $(DANGER_POLICY_TEST_OBJS:.o=.d)
DANGER_V3_TEST_BIN := sg_danger_v3_test.make
DANGER_V3_TEST_OBJS := .sg_danger_v3_test.make.o \
	.sg_danger_under_test.make.o .sg_rune_wire_under_test.make.o \
	.sg_rune_wire_action_under_test.make.o .sg_rune_wire_crc_under_test.make.o
DANGER_V3_TEST_DEPS := $(DANGER_V3_TEST_OBJS:.o=.d)
FIELDS_CANDIDATE_TEST_BIN := sg_fields_candidate_test.make
FIELDS_CANDIDATE_TEST_OBJS := .sg_fields_candidate_test.make.o \
	.sg_fields_candidate_under_test.make.o
FIELDS_CANDIDATE_TEST_DEPS := $(FIELDS_CANDIDATE_TEST_OBJS:.o=.d)
RUNE_LOADER_TEST_BIN := sg_rune_loader_test.make
RUNE_LOADER_TEST_OBJS := .sg_rune_loader_test.make.o \
	.sg_rune_loader_under_test.make.o .sg_rune_wire_under_test.make.o \
	.sg_rune_wire_action_under_test.make.o .sg_rune_wire_crc_under_test.make.o \
	.sg_compound_under_test.make.o
RUNE_LOADER_TEST_DEPS := $(RUNE_LOADER_TEST_OBJS:.o=.d)
RUNE_WRITER_TEST_BIN := sg_rune_writer_test.make
RUNE_WRITER_TEST_OBJS := .sg_rune_writer_test.make.o \
	.sg_rune_writer_under_test.make.o .sg_rune_wire_under_test.make.o \
	.sg_rune_wire_action_under_test.make.o .sg_rune_wire_crc_under_test.make.o \
	.sg_compound_under_test.make.o
RUNE_WRITER_TEST_DEPS := $(RUNE_WRITER_TEST_OBJS:.o=.d)
RUNE_INSTALL_TEST_BIN := sg_rune_install_test.make
RUNE_INSTALL_TEST_OBJS := .sg_rune_install_test.make.o \
	.sg_rune_install_under_test.make.o .sg_rune_writer_under_test.make.o \
	.sg_rune_wire_under_test.make.o .sg_rune_wire_action_under_test.make.o \
	.sg_rune_wire_crc_under_test.make.o .sg_compound_under_test.make.o
RUNE_INSTALL_TEST_DEPS := $(RUNE_INSTALL_TEST_OBJS:.o=.d)
RUNE_PROOF_TEST_BIN := sg_rune_proof_test.make
RUNE_PROOF_TEST_OBJS := .sg_rune_proof_test.make.o \
	.sg_rune_proof_under_test.make.o
RUNE_PROOF_TEST_DEPS := $(RUNE_PROOF_TEST_OBJS:.o=.d)
REPLAY_TEST_BIN := sg_replay_test.make
REPLAY_TEST_OBJS := .sg_replay_test.make.o .sg_replay_under_test.make.o
REPLAY_TEST_DEPS := $(REPLAY_TEST_OBJS:.o=.d)
DROP_LIVE_TEST_BIN := sg_drop_live_test.make
DROP_LIVE_TEST_OBJS := .sg_drop_live_test.make.o \
	.sg_drop_live_under_test.make.o .sg_drop_live_replay_under_test.make.o
DROP_LIVE_TEST_DEPS := $(DROP_LIVE_TEST_OBJS:.o=.d)
SWIM_LIVE_TEST_BIN := sg_swim_live_test.make
SWIM_LIVE_TEST_OBJS := .sg_swim_live_test.make.o \
	.sg_swim_live_under_test.make.o .sg_swim_live_replay_under_test.make.o
SWIM_LIVE_TEST_DEPS := $(SWIM_LIVE_TEST_OBJS:.o=.d)
HOOK_LIVE_TEST_BIN := sg_hook_live_test.make
HOOK_LIVE_TEST_OBJS := .sg_hook_live_test.make.o \
	.sg_hook_live_under_test.make.o .sg_hook_live_replay_under_test.make.o
HOOK_LIVE_TEST_DEPS := $(HOOK_LIVE_TEST_OBJS:.o=.d)
HOOK_INTEGRATION_TEST := tests/test_hook_live_integration.py
ROTATOR_SWEEP_TEST_BIN := sg_rotator_sweep_test.make
ROTATOR_SWEEP_TEST_OBJS := .sg_rotator_sweep_test.make.o .sg_rotator_sweep_under_test.make.o \
	.sg_rotator_sweep_q_shared_under_test.make.o
ROTATOR_SWEEP_TEST_DEPS := $(ROTATOR_SWEEP_TEST_OBJS:.o=.d)
COMPOUND_SWIM_ORACLE_TEST_BIN := sg_compound_swim_oracle_test.make
COMPOUND_SWIM_ORACLE_TEST_OBJS := \
	.sg_compound_swim_oracle_test.make.o \
	.sg_compound_swim_oracle_oracle_under_test.make.o \
	.sg_compound_swim_oracle_replay_under_test.make.o \
	.sg_compound_swim_oracle_compound_under_test.make.o \
	.sg_compound_swim_oracle_world_under_test.make.o \
	.sg_compound_swim_oracle_q_shared_under_test.make.o
COMPOUND_SWIM_ORACLE_TEST_DEPS := \
	$(COMPOUND_SWIM_ORACLE_TEST_OBJS:.o=.d)
COMPOUND_SWIM_ORACLE_TEST_ALL_ARTIFACTS := \
	sg_compound_swim_oracle_test.gnu sg_compound_swim_oracle_test.make \
	.sg_compound_swim_oracle_test.gnu.o \
	.sg_compound_swim_oracle_test.gnu.d \
	.sg_compound_swim_oracle_oracle_under_test.gnu.o \
	.sg_compound_swim_oracle_oracle_under_test.gnu.d \
	.sg_compound_swim_oracle_replay_under_test.gnu.o \
	.sg_compound_swim_oracle_replay_under_test.gnu.d \
	.sg_compound_swim_oracle_compound_under_test.gnu.o \
	.sg_compound_swim_oracle_compound_under_test.gnu.d \
	.sg_compound_swim_oracle_world_under_test.gnu.o \
	.sg_compound_swim_oracle_world_under_test.gnu.d \
	.sg_compound_swim_oracle_q_shared_under_test.gnu.o \
	.sg_compound_swim_oracle_q_shared_under_test.gnu.d \
	$(COMPOUND_SWIM_ORACLE_TEST_OBJS) \
	$(COMPOUND_SWIM_ORACLE_TEST_DEPS)
RUNE_DOOR_SCOPE_TEST_BIN := sg_rune_door_scope_test.make
RUNE_DOOR_SCOPE_TEST_OBJS := .sg_rune_door_scope_test.make.o \
	.sg_rune_door_scope_under_test.make.o
RUNE_DOOR_SCOPE_TEST_DEPS := $(RUNE_DOOR_SCOPE_TEST_OBJS:.o=.d)
RUNE_DOOR_SCOPE_TEST_ALL_ARTIFACTS := \
	sg_rune_door_scope_test.gnu sg_rune_door_scope_test.make \
	.sg_rune_door_scope_test.gnu.o .sg_rune_door_scope_test.gnu.d \
	.sg_rune_door_scope_under_test.gnu.o \
	.sg_rune_door_scope_under_test.gnu.d \
	$(RUNE_DOOR_SCOPE_TEST_OBJS) $(RUNE_DOOR_SCOPE_TEST_DEPS)
ENTFILE_TEST_BIN := g_entfile_path_test.make
ENTFILE_TEST_OBJS := .g_entfile_path_test.make.o
ENTFILE_TEST_DEPS := $(ENTFILE_TEST_OBJS:.o=.d)
ENGINE_SNAPSHOT_TEST := tests/test_engine_snapshot_name.sh
HOST_TEST_ALL_ARTIFACTS := sg_hooks_test sg_hooks_test.gnu sg_hooks_test.make \
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
CFLAGS += -DSG_ACCEPT_DROP
ifneq ($(SG_ACCEPT_DROP_LEGACY_A),)
CFLAGS += -DSG_ACCEPT_DROP_LEGACY_A=$(SG_ACCEPT_DROP_LEGACY_A)
endif
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
	sg_action.o \
	sg_crc32.o \
	sg_identity.o \
	sg_rune_wire.o \
	sg_sidecar_wire.o \
	sg_sidecar_loader.o \
	sg_sidecar_store.o \
	sg_rune_loader.o \
	sg_rune_writer.o \
	sg_rune_install.o \
	sg_rune_proof.o \
	sg_replay.o \
	sg_compound.o \
	slipgate/sg_mover_lease.o \
	slipgate/sg_compound_world.o \
	slipgate/sg_compound_gen.o \
	slipgate/sg_compound_publication.o \
	slipgate/sg_rune_door_scope.o \
	sg_drop_live.o \
	sg_accept_drop.o \
	sg_swim_live.o \
	sg_hook_live.o \
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
	sg_danger_lease.o \
	sg_danger_policy.o \
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

.PHONY: all default host-test action-test compound-test mover-lease-test \
	compound-world-test \
	compound-gen-test compound-publication-test \
	identity-test rune-wire-test \
	sidecar-wire-test sidecar-loader-test sidecar-store-test \
	danger-lease-test danger-policy-test danger-v3-test fields-candidate-test \
	rune-loader-test \
	rune-writer-test rune-install-test rune-proof-test replay-test \
	drop-live-test swim-live-test rotator-sweep-test entfile-test \
	compound-swim-oracle-test rune-door-scope-test \
	snapshot-test clean strip FORCE

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

slipgate/sg_compound_world.o: slipgate/sg_compound_world.c \
		slipgate/sg_compound_world.h slipgate/sg_util.h g_local.h
slipgate/sg_mover_lease.o: slipgate/sg_mover_lease.c \
		slipgate/sg_mover_lease.h
slipgate/sg_compound_gen.o: slipgate/sg_compound_gen.c \
		slipgate/sg_compound_gen.h slipgate/sg_rune.h q_shared.h
slipgate/sg_compound_publication.o: slipgate/sg_compound_publication.c \
		slipgate/sg_compound_publication.h slipgate/sg_compound_world.h \
		slipgate/sg_local.h slipgate/sg_rune.h g_local.h
slipgate/sg_rune_door_scope.o: slipgate/sg_rune_door_scope.c \
		slipgate/sg_rune_door_scope.h

-include $(OBJS:.o=.d)
-include $(HOST_TEST_DEPS)
-include $(ACTION_TEST_DEPS)
-include $(COMPOUND_TEST_DEPS)
-include $(MOVER_LEASE_TEST_DEPS)
-include $(COMPOUND_WORLD_TEST_DEPS)
-include $(COMPOUND_GEN_TEST_DEPS)
-include $(COMPOUND_PUBLICATION_TEST_DEPS)
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
-include $(DROP_LIVE_TEST_DEPS)
-include $(SWIM_LIVE_TEST_DEPS)
-include $(HOOK_LIVE_TEST_DEPS)
-include $(ROTATOR_SWEEP_TEST_DEPS)
-include $(COMPOUND_SWIM_ORACLE_TEST_DEPS)
-include $(RUNE_DOOR_SCOPE_TEST_DEPS)
-include $(ENTFILE_TEST_DEPS)

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

$(ACTION_TEST_BIN): $(ACTION_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(ACTION_TEST_OBJS) $(LIBS)

$(IDENTITY_TEST_BIN): $(IDENTITY_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(IDENTITY_TEST_OBJS) $(LIBS)

$(RUNE_WIRE_TEST_BIN): $(RUNE_WIRE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_WIRE_TEST_OBJS) $(LIBS)

$(SIDECAR_WIRE_TEST_BIN): $(SIDECAR_WIRE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(SIDECAR_WIRE_TEST_OBJS) $(LIBS)

$(SIDECAR_LOADER_TEST_BIN): $(SIDECAR_LOADER_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(SIDECAR_LOADER_TEST_OBJS) $(LIBS)

$(SIDECAR_STORE_TEST_BIN): $(SIDECAR_STORE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(SIDECAR_STORE_TEST_OBJS) $(LIBS)

$(DANGER_LEASE_TEST_BIN): $(DANGER_LEASE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(DANGER_LEASE_TEST_OBJS) $(LIBS)

$(DANGER_POLICY_TEST_BIN): $(DANGER_POLICY_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(DANGER_POLICY_TEST_OBJS) $(LIBS)

$(DANGER_V3_TEST_BIN): $(DANGER_V3_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(DANGER_V3_TEST_OBJS) $(LIBS)

$(FIELDS_CANDIDATE_TEST_BIN): $(FIELDS_CANDIDATE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(FIELDS_CANDIDATE_TEST_OBJS) $(LIBS)

$(COMPOUND_TEST_BIN): $(COMPOUND_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_TEST_OBJS) $(LIBS)

$(MOVER_LEASE_TEST_BIN): $(MOVER_LEASE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(MOVER_LEASE_TEST_OBJS) $(LIBS)

$(COMPOUND_WORLD_TEST_BIN): $(COMPOUND_WORLD_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(COMPOUND_WORLD_TEST_OBJS) $(LIBS)

$(COMPOUND_GEN_TEST_BIN): $(COMPOUND_GEN_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_GEN_TEST_OBJS) $(LIBS)

$(COMPOUND_PUBLICATION_TEST_BIN): $(COMPOUND_PUBLICATION_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_PUBLICATION_TEST_OBJS) $(LIBS)

$(RUNE_LOADER_TEST_BIN): $(RUNE_LOADER_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_LOADER_TEST_OBJS) $(LIBS)

$(RUNE_WRITER_TEST_BIN): $(RUNE_WRITER_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_WRITER_TEST_OBJS) $(LIBS)

$(RUNE_INSTALL_TEST_BIN): $(RUNE_INSTALL_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_INSTALL_TEST_OBJS) $(LIBS)

$(RUNE_PROOF_TEST_BIN): $(RUNE_PROOF_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_PROOF_TEST_OBJS) $(LIBS)

$(REPLAY_TEST_BIN): $(REPLAY_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(REPLAY_TEST_OBJS) $(LIBS)

$(DROP_LIVE_TEST_BIN): $(DROP_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(DROP_LIVE_TEST_OBJS) $(LIBS)

$(SWIM_LIVE_TEST_BIN): $(SWIM_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(SWIM_LIVE_TEST_OBJS) $(LIBS)

$(HOOK_LIVE_TEST_BIN): $(HOOK_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(HOOK_LIVE_TEST_OBJS) $(LIBS)

$(ROTATOR_SWEEP_TEST_BIN): $(ROTATOR_SWEEP_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(ROTATOR_SWEEP_TEST_OBJS) $(LIBS)

$(COMPOUND_SWIM_ORACLE_TEST_BIN): $(COMPOUND_SWIM_ORACLE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_SWIM_ORACLE_TEST_OBJS) $(LIBS)

$(RUNE_DOOR_SCOPE_TEST_BIN): $(RUNE_DOOR_SCOPE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_DOOR_SCOPE_TEST_OBJS) $(LIBS)

$(ENTFILE_TEST_BIN): $(ENTFILE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(ENTFILE_TEST_OBJS) $(LIBS)

.sg_action_test.make.o: tests/sg_action_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_action_under_test.make.o: slipgate/sg_action.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_test.make.o: tests/sg_compound_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_under_test.make.o: slipgate/sg_compound.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_action_under_test.make.o: slipgate/sg_action.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_lease_test.make.o: tests/sg_mover_lease_test.c \
		slipgate/sg_mover_lease.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_lease_under_test.make.o: slipgate/sg_mover_lease.c \
		slipgate/sg_mover_lease.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_world_test.make.o: tests/sg_compound_world_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_world_under_test.make.o: slipgate/sg_compound_world.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_world_q_shared_under_test.make.o: q_shared.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_gen_test.make.o: tests/sg_compound_gen_test.c \
		slipgate/sg_compound_gen.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_gen_under_test.make.o: slipgate/sg_compound_gen.c \
		slipgate/sg_compound_gen.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_publication_test.make.o: \
		tests/sg_compound_publication_test.c \
		slipgate/sg_compound_publication.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_publication_under_test.make.o: \
		slipgate/sg_compound_publication.c \
		slipgate/sg_compound_publication.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_identity_test.make.o: tests/sg_identity_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_identity_under_test.make.o: slipgate/sg_identity.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_crc32_under_test.make.o: slipgate/sg_crc32.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_wire_test.make.o: tests/sg_rune_wire_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_wire_under_test.make.o: slipgate/sg_rune_wire.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_wire_action_under_test.make.o: slipgate/sg_action.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_wire_crc_under_test.make.o: slipgate/sg_crc32.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_wire_test.make.o: tests/sg_sidecar_wire_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_wire_under_test.make.o: slipgate/sg_sidecar_wire.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_loader_test.make.o: tests/sg_sidecar_loader_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_loader_under_test.make.o: slipgate/sg_sidecar_loader.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_store_test.make.o: tests/sg_sidecar_store_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_store_under_test.make.o: slipgate/sg_sidecar_store.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_lease_test.make.o: tests/sg_danger_lease_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_lease_under_test.make.o: slipgate/sg_danger_lease.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_policy_test.make.o: tests/sg_danger_policy_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_policy_under_test.make.o: slipgate/sg_danger_policy.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_v3_test.make.o: tests/sg_danger_v3_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_under_test.make.o: slipgate/sg_danger.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_fields_candidate_test.make.o: tests/sg_fields_candidate_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_fields_candidate_under_test.make.o: slipgate/sg_fields.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -DSG_FIELDS_TEST -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_loader_test.make.o: tests/sg_rune_loader_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_loader_under_test.make.o: slipgate/sg_rune_loader.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_writer_test.make.o: tests/sg_rune_writer_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_writer_under_test.make.o: slipgate/sg_rune_writer.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_install_test.make.o: tests/sg_rune_install_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_install_under_test.make.o: slipgate/sg_rune_install.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_proof_test.make.o: tests/sg_rune_proof_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_proof_under_test.make.o: slipgate/sg_rune_proof.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_replay_test.make.o: tests/sg_replay_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_replay_under_test.make.o: slipgate/sg_replay.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_drop_live_test.make.o: tests/sg_drop_live_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_drop_live_under_test.make.o: slipgate/sg_drop_live.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_drop_live_replay_under_test.make.o: slipgate/sg_replay.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_swim_live_test.make.o: tests/sg_swim_live_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_swim_live_under_test.make.o: slipgate/sg_swim_live.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_swim_live_replay_under_test.make.o: slipgate/sg_replay.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_live_test.make.o: tests/sg_hook_live_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_live_under_test.make.o: slipgate/sg_hook_live.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_live_replay_under_test.make.o: slipgate/sg_replay.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rotator_sweep_test.make.o: tests/sg_rotator_sweep_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rotator_sweep_under_test.make.o: slipgate/sg_oracle.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rotator_sweep_q_shared_under_test.make.o: q_shared.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_test.make.o: \
		tests/sg_compound_swim_oracle_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_oracle_under_test.make.o: \
		slipgate/sg_oracle.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_replay_under_test.make.o: \
		slipgate/sg_replay.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_compound_under_test.make.o: \
		slipgate/sg_compound.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_world_under_test.make.o: \
		slipgate/sg_compound_world.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_q_shared_under_test.make.o: \
		q_shared.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_door_scope_test.make.o: \
		tests/sg_rune_door_scope_test.c slipgate/sg_rune_door_scope.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_door_scope_under_test.make.o: \
		slipgate/sg_rune_door_scope.c slipgate/sg_rune_door_scope.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.g_entfile_path_test.make.o: tests/g_entfile_path_test.c g_entfile_path.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -pedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

host-test: $(HOST_TEST_BIN) $(ACTION_TEST_BIN) $(COMPOUND_TEST_BIN) \
		$(MOVER_LEASE_TEST_BIN) \
		$(COMPOUND_WORLD_TEST_BIN) $(COMPOUND_GEN_TEST_BIN) \
		$(COMPOUND_PUBLICATION_TEST_BIN) \
		$(COMPOUND_PUBLICATION_INTEGRATION_TEST) \
		$(IDENTITY_TEST_BIN) \
		$(RUNE_WIRE_TEST_BIN) $(SIDECAR_WIRE_TEST_BIN) \
		$(SIDECAR_LOADER_TEST_BIN) $(SIDECAR_STORE_TEST_BIN) \
		$(DANGER_LEASE_TEST_BIN) $(DANGER_POLICY_TEST_BIN) \
		$(DANGER_V3_TEST_BIN) $(FIELDS_CANDIDATE_TEST_BIN) \
		$(RUNE_LOADER_TEST_BIN) $(RUNE_WRITER_TEST_BIN) \
		$(RUNE_INSTALL_TEST_BIN) $(RUNE_PROOF_TEST_BIN) \
		$(REPLAY_TEST_BIN) $(DROP_LIVE_TEST_BIN) $(SWIM_LIVE_TEST_BIN) \
		$(HOOK_LIVE_TEST_BIN) $(HOOK_INTEGRATION_TEST) \
		$(ROTATOR_SWEEP_TEST_BIN) $(COMPOUND_SWIM_ORACLE_TEST_BIN) \
		$(RUNE_DOOR_SCOPE_TEST_BIN) $(ENTFILE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(HOST_TEST_BIN)
	$(Q)./$(ACTION_TEST_BIN)
	$(Q)./$(COMPOUND_TEST_BIN)
	$(Q)./$(MOVER_LEASE_TEST_BIN)
	$(Q)./$(COMPOUND_WORLD_TEST_BIN)
	$(Q)./$(COMPOUND_GEN_TEST_BIN)
	$(Q)./$(COMPOUND_PUBLICATION_TEST_BIN)
	$(Q)python3 $(COMPOUND_PUBLICATION_INTEGRATION_TEST)
	$(Q)./$(IDENTITY_TEST_BIN)
	$(Q)./$(RUNE_WIRE_TEST_BIN)
	$(Q)./$(SIDECAR_WIRE_TEST_BIN)
	$(Q)./$(SIDECAR_LOADER_TEST_BIN)
	$(Q)./$(SIDECAR_STORE_TEST_BIN)
	$(Q)./$(DANGER_LEASE_TEST_BIN)
	$(Q)./$(DANGER_POLICY_TEST_BIN)
	$(Q)./$(DANGER_V3_TEST_BIN)
	$(Q)./$(FIELDS_CANDIDATE_TEST_BIN)
	$(Q)./$(RUNE_LOADER_TEST_BIN)
	$(Q)./$(RUNE_WRITER_TEST_BIN)
	$(Q)./$(RUNE_INSTALL_TEST_BIN)
	$(Q)./$(RUNE_PROOF_TEST_BIN)
	$(Q)./$(REPLAY_TEST_BIN)
	$(Q)./$(DROP_LIVE_TEST_BIN)
	$(Q)sh tests/sg_drop_begin_wiring_test.sh
	$(Q)./$(SWIM_LIVE_TEST_BIN)
	$(Q)./$(HOOK_LIVE_TEST_BIN)
	$(Q)python3 $(HOOK_INTEGRATION_TEST)
	$(Q)./$(ROTATOR_SWEEP_TEST_BIN)
	$(Q)./$(COMPOUND_SWIM_ORACLE_TEST_BIN)
	$(Q)./$(RUNE_DOOR_SCOPE_TEST_BIN)
	$(Q)./$(ENTFILE_TEST_BIN)
	$(Q)./$(ENGINE_SNAPSHOT_TEST)

action-test: $(ACTION_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(ACTION_TEST_BIN)

compound-test: $(COMPOUND_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_TEST_BIN)

mover-lease-test: $(MOVER_LEASE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(MOVER_LEASE_TEST_BIN)

compound-world-test: $(COMPOUND_WORLD_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_WORLD_TEST_BIN)

compound-gen-test: $(COMPOUND_GEN_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_GEN_TEST_BIN)

compound-publication-test: $(COMPOUND_PUBLICATION_TEST_BIN) \
		$(COMPOUND_PUBLICATION_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_PUBLICATION_TEST_BIN)
	$(Q)python3 $(COMPOUND_PUBLICATION_INTEGRATION_TEST)

identity-test: $(IDENTITY_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(IDENTITY_TEST_BIN)

rune-wire-test: $(RUNE_WIRE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_WIRE_TEST_BIN)

sidecar-wire-test: $(SIDECAR_WIRE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(SIDECAR_WIRE_TEST_BIN)

sidecar-loader-test: $(SIDECAR_LOADER_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(SIDECAR_LOADER_TEST_BIN)

sidecar-store-test: $(SIDECAR_STORE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(SIDECAR_STORE_TEST_BIN)

danger-lease-test: $(DANGER_LEASE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(DANGER_LEASE_TEST_BIN)

danger-policy-test: $(DANGER_POLICY_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(DANGER_POLICY_TEST_BIN)

danger-v3-test: $(DANGER_V3_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(DANGER_V3_TEST_BIN)

fields-candidate-test: $(FIELDS_CANDIDATE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(FIELDS_CANDIDATE_TEST_BIN)

rune-loader-test: $(RUNE_LOADER_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_LOADER_TEST_BIN)

rune-writer-test: $(RUNE_WRITER_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_WRITER_TEST_BIN)

rune-install-test: $(RUNE_INSTALL_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_INSTALL_TEST_BIN)

rune-proof-test: $(RUNE_PROOF_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_PROOF_TEST_BIN)

replay-test: $(REPLAY_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(REPLAY_TEST_BIN)

drop-live-test: $(DROP_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(DROP_LIVE_TEST_BIN)
	$(Q)sh tests/sg_drop_begin_wiring_test.sh

swim-live-test: $(SWIM_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(SWIM_LIVE_TEST_BIN)

hook-live-test: $(HOOK_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(HOOK_LIVE_TEST_BIN)

hook-integration-test:
	$(E) [TEST] $(HOOK_INTEGRATION_TEST)
	$(Q)python3 $(HOOK_INTEGRATION_TEST)

rotator-sweep-test: $(ROTATOR_SWEEP_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(ROTATOR_SWEEP_TEST_BIN)

compound-swim-oracle-test: $(COMPOUND_SWIM_ORACLE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_SWIM_ORACLE_TEST_BIN)

rune-door-scope-test: $(RUNE_DOOR_SCOPE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_DOOR_SCOPE_TEST_BIN)

entfile-test: $(ENTFILE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(ENTFILE_TEST_BIN)

snapshot-test:
	$(E) [TEST] $(ENGINE_SNAPSHOT_TEST)
	$(Q)./$(ENGINE_SNAPSHOT_TEST)

clean:
	$(E) [CLEAN]
	$(Q)$(RM) *.o *.d $(OBJS) $(OBJS:.o=.d) $(TARGET) $(REVISION_HEADER) \
		$(REVISION_HEADER).tmp.* $(HOST_TEST_ALL_ARTIFACTS) \
		$(COMPOUND_GEN_TEST_ALL_ARTIFACTS) \
		$(COMPOUND_PUBLICATION_TEST_ALL_ARTIFACTS) \
		$(COMPOUND_SWIM_ORACLE_TEST_ALL_ARTIFACTS) \
		$(MOVER_LEASE_TEST_ALL_ARTIFACTS) \
		$(RUNE_DOOR_SCOPE_TEST_ALL_ARTIFACTS)

strip: $(TARGET)
	$(E) [STRIP]
	$(Q)$(STRIP) $(TARGET)


# Third-party SQLite amalgamation: own rule, warnings off, single-threaded.
SQLITE_CFLAGS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION=1 \
                -DSQLITE_DEFAULT_MEMSTATUS=0 -w

sqlite3.o: sqlite3.c
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) $(SQLITE_CFLAGS) -o $@ -c $<
