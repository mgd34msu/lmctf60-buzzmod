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
HOST_LAW_PUBLICATION_TEST := tests/run_sg_host_law_publication_test.sh
ACTION_TEST_BIN := sg_action_test.make
ACTION_TEST_OBJS := .sg_action_test.make.o .sg_action_under_test.make.o
ACTION_TEST_DEPS := $(ACTION_TEST_OBJS:.o=.d)
COMPOUND_TEST_BIN := sg_compound_test.make
COMPOUND_TEST_OBJS := .sg_compound_test.make.o \
	.sg_compound_under_test.make.o .sg_compound_action_under_test.make.o
COMPOUND_TEST_DEPS := $(COMPOUND_TEST_OBJS:.o=.d)
MOVER_LEASE_TEST_BIN := sg_mover_lease_test.make
MOVER_LEASE_TEST_OBJS := .sg_mover_lease_test.make.o .sg_mover_lease_under_test.make.o
MOVER_LEASE_TEST_DEPS := $(MOVER_LEASE_TEST_OBJS:.o=.d)
MOVER_LEASE_TEST_ALL_ARTIFACTS := \
	sg_mover_lease_test.gnu sg_mover_lease_test.make \
	.sg_mover_lease_test.gnu.o .sg_mover_lease_test.gnu.d \
	.sg_mover_lease_under_test.gnu.o .sg_mover_lease_under_test.gnu.d \
	$(MOVER_LEASE_TEST_OBJS) $(MOVER_LEASE_TEST_DEPS)
WATER_FOREST_TEST_BIN := sg_water_forest_test.make
WATER_FOREST_TEST_OBJS := .sg_water_forest_test.make.o \
	.sg_water_forest_under_test.make.o
WATER_FOREST_TEST_DEPS := $(WATER_FOREST_TEST_OBJS:.o=.d)
WATER_FOREST_TEST_ALL_ARTIFACTS := \
	sg_water_forest_test.gnu sg_water_forest_test.make \
	.sg_water_forest_test.gnu.o .sg_water_forest_test.gnu.d \
	.sg_water_forest_under_test.gnu.o .sg_water_forest_under_test.gnu.d \
	$(WATER_FOREST_TEST_OBJS) $(WATER_FOREST_TEST_DEPS)
BUTTON_LIVE_TEST_BIN := sg_button_live_test.make
BUTTON_LIVE_TEST_OBJS := .sg_button_live_test.make.o .sg_button_live_under_test.make.o
BUTTON_LIVE_TEST_DEPS := $(BUTTON_LIVE_TEST_OBJS:.o=.d)
BUTTON_LIVE_TEST_ALL_ARTIFACTS := \
	sg_button_live_test.gnu sg_button_live_test.make \
	.sg_button_live_test.gnu.o .sg_button_live_test.gnu.d \
	.sg_button_live_under_test.gnu.o .sg_button_live_under_test.gnu.d \
	$(BUTTON_LIVE_TEST_OBJS) $(BUTTON_LIVE_TEST_DEPS)
TRAIN_GATE_LIVE_TEST_BIN := sg_train_gate_live_test.make
TRAIN_GATE_LIVE_TEST_OBJS := .sg_train_gate_live_test.make.o \
	.sg_train_gate_live_under_test.make.o
TRAIN_GATE_LIVE_TEST_DEPS := $(TRAIN_GATE_LIVE_TEST_OBJS:.o=.d)
MECHANISM_TIMELINE_TEST_BIN := sg_mechanism_timeline_test.make
MECHANISM_TIMELINE_TEST_OBJS := .sg_mechanism_timeline_test.make.o \
	.sg_mechanism_timeline_under_test.make.o
MECHANISM_TIMELINE_TEST_DEPS := $(MECHANISM_TIMELINE_TEST_OBJS:.o=.d)
RELAY_WALL_TRANSACTION_TEST_BIN := sg_relay_wall_transaction_test.make
RELAY_WALL_TRANSACTION_TEST_OBJS := .sg_relay_wall_transaction_test.make.o \
	.sg_relay_wall_transaction_under_test.make.o \
	.sg_relay_wall_transaction_timeline_under_test.make.o
RELAY_WALL_TRANSACTION_TEST_DEPS := \
	$(RELAY_WALL_TRANSACTION_TEST_OBJS:.o=.d)
RELAY_WALL_OBJECTIVE_TEST_BIN := sg_relay_wall_objective_test.make
RELAY_WALL_OBJECTIVE_TEST_OBJS := .sg_relay_wall_objective_test.make.o \
	.sg_relay_wall_objective_under_test.make.o
RELAY_WALL_OBJECTIVE_TEST_DEPS := $(RELAY_WALL_OBJECTIVE_TEST_OBJS:.o=.d)
SHOOT_DOOR_LIVE_TEST_BIN := sg_shoot_door_live_test.make
SHOOT_DOOR_LIVE_TEST_OBJS := .sg_shoot_door_live_test.make.o \
	.sg_shoot_door_live_under_test.make.o
SHOOT_DOOR_LIVE_TEST_DEPS := $(SHOOT_DOOR_LIVE_TEST_OBJS:.o=.d)
BUTTON_GAME_TEST_BIN := sg_button_game_test.make
BUTTON_GAME_TEST_OBJS := .sg_button_game_test.make.o \
	.sg_button_game_live_under_test.make.o \
	.sg_button_game_move_under_test.make.o \
	.sg_door_approach_under_test.make.o \
	.sg_button_game_func_under_test.make.o \
	.sg_button_game_q_shared_under_test.make.o
BUTTON_GAME_TEST_DEPS := $(BUTTON_GAME_TEST_OBJS:.o=.d)
BUTTON_GAME_INTEGRATION_TEST := tests/test_button_game_integration.py
BUTTON_GAME_TEST_ALL_ARTIFACTS := \
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
	$(BUTTON_GAME_TEST_OBJS) $(BUTTON_GAME_TEST_DEPS)
COMPOUND_GUARD_TEST_BIN := sg_compound_guard_test.make
COMPOUND_GUARD_TEST_OBJS := .sg_compound_guard_test.make.o \
	.sg_compound_guard_under_test.make.o \
	.sg_compound_guard_mover_lease_under_test.make.o
COMPOUND_GUARD_TEST_DEPS := $(COMPOUND_GUARD_TEST_OBJS:.o=.d)
COMPOUND_GUARD_TEST_ALL_ARTIFACTS := \
	sg_compound_guard_test.gnu sg_compound_guard_test.make \
	.sg_compound_guard_test.gnu.o .sg_compound_guard_test.gnu.d \
	.sg_compound_guard_under_test.gnu.o \
	.sg_compound_guard_under_test.gnu.d \
	.sg_compound_guard_mover_lease_under_test.gnu.o \
	.sg_compound_guard_mover_lease_under_test.gnu.d \
	$(COMPOUND_GUARD_TEST_OBJS) $(COMPOUND_GUARD_TEST_DEPS)
COMPOUND_GUARD_GAME_TEST_BIN := sg_compound_guard_game_test.make
COMPOUND_GUARD_GAME_TEST_OBJS := .sg_compound_guard_game_test.make.o \
	.sg_compound_guard_game_under_test.make.o \
	.sg_compound_hook_game_lifecycle_under_test.make.o
COMPOUND_GUARD_GAME_TEST_DEPS := $(COMPOUND_GUARD_GAME_TEST_OBJS:.o=.d)
COMPOUND_GUARD_GAME_INTEGRATION_TEST := \
	tests/test_compound_guard_game_integration.py
COMPOUND_GUARD_GAME_TEST_ALL_ARTIFACTS := \
	sg_compound_guard_game_test.gnu sg_compound_guard_game_test.make \
	.sg_compound_guard_game_test.gnu.o \
	.sg_compound_guard_game_test.gnu.d \
	.sg_compound_guard_game_under_test.gnu.o \
	.sg_compound_guard_game_under_test.gnu.d \
	.sg_compound_hook_game_lifecycle_under_test.gnu.o \
	.sg_compound_hook_game_lifecycle_under_test.gnu.d \
	$(COMPOUND_GUARD_GAME_TEST_OBJS) \
	$(COMPOUND_GUARD_GAME_TEST_DEPS)
DECLARED_DOOR_GUARD_TEST_BIN := sg_declared_door_guard_test.make
DECLARED_DOOR_GUARD_TEST_OBJS := .sg_declared_door_guard_test.make.o \
	.sg_declared_door_guard_under_test.make.o
DECLARED_DOOR_GUARD_TEST_DEPS := $(DECLARED_DOOR_GUARD_TEST_OBJS:.o=.d)
DECLARED_DOOR_GUARD_INTEGRATION_TESTS := \
	tests/test_declared_door_guard_integration.py \
	tests/test_declared_door_guard_runtime_integration.py \
	tests/test_declared_door_guard_arach_integration.py
DECLARED_DOOR_GUARD_TEST_ALL_ARTIFACTS := \
	sg_declared_door_guard_test.gnu sg_declared_door_guard_test.make \
	.sg_declared_door_guard_test.gnu.o \
	.sg_declared_door_guard_test.gnu.d \
	.sg_declared_door_guard_under_test.gnu.o \
	.sg_declared_door_guard_under_test.gnu.d \
	$(DECLARED_DOOR_GUARD_TEST_OBJS) \
	$(DECLARED_DOOR_GUARD_TEST_DEPS)
COMPOUND_WORLD_TEST_BIN := sg_compound_world_test.make
COMPOUND_WORLD_TEST_OBJS := .sg_compound_world_test.make.o \
	.sg_compound_world_under_test.make.o \
	.sg_compound_world_q_shared_under_test.make.o
COMPOUND_WORLD_TEST_DEPS := $(COMPOUND_WORLD_TEST_OBJS:.o=.d)
COMPOUND_GEN_TEST_BIN := sg_compound_gen_test.make
COMPOUND_GEN_TEST_OBJS := .sg_compound_gen_test.make.o \
	.sg_compound_gen_under_test.make.o
COMPOUND_GEN_TEST_DEPS := $(COMPOUND_GEN_TEST_OBJS:.o=.d)
COMPOUND_GEN_GAME_TEST_BIN := sg_compound_gen_game_test.make
COMPOUND_GEN_GAME_TEST_OBJS := .sg_compound_gen_game_test.make.o \
	.sg_compound_gen_game_under_test.make.o \
	.sg_compound_gen_under_test.make.o
COMPOUND_GEN_GAME_TEST_DEPS := $(COMPOUND_GEN_GAME_TEST_OBJS:.o=.d)
COMPOUND_GEN_TEST_ALL_ARTIFACTS := \
	sg_compound_gen_test sg_compound_gen_test.gnu sg_compound_gen_test.make \
	sg_compound_gen_game_test.gnu sg_compound_gen_game_test.make \
	.sg_compound_gen_test.gnu.o .sg_compound_gen_test.gnu.d \
	.sg_compound_gen_under_test.gnu.o .sg_compound_gen_under_test.gnu.d \
	.sg_compound_gen_test.make.o .sg_compound_gen_test.make.d \
	.sg_compound_gen_under_test.make.o .sg_compound_gen_under_test.make.d \
	.sg_compound_gen_game_test.gnu.o .sg_compound_gen_game_test.gnu.d \
	.sg_compound_gen_game_under_test.gnu.o \
	.sg_compound_gen_game_under_test.gnu.d \
	.sg_compound_gen_game_test.make.o .sg_compound_gen_game_test.make.d \
	.sg_compound_gen_game_under_test.make.o \
	.sg_compound_gen_game_under_test.make.d
COMPOUND_PUBLICATION_TEST_BIN := sg_compound_publication_test.make
COMPOUND_PUBLICATION_CASE_STEMS := compound_publication_fixture \
	compound_publication_core_cases compound_hook_publication_cases
COMPOUND_PUBLICATION_CASE_MAKE_OBJS := \
	$(addprefix .sg_,$(addsuffix .make.o,$(COMPOUND_PUBLICATION_CASE_STEMS)))
COMPOUND_PUBLICATION_TEST_OBJS := .sg_compound_publication_test.make.o \
	$(COMPOUND_PUBLICATION_CASE_MAKE_OBJS) \
	.sg_compound_publication_under_test.make.o \
	.sg_compound_publication_build_under_test.make.o \
	.sg_compound_action_publication_for_publication_test.make.o
COMPOUND_PUBLICATION_TEST_DEPS := $(COMPOUND_PUBLICATION_TEST_OBJS:.o=.d)
COMPOUND_PUBLICATION_INTEGRATION_TEST := \
	tests/test_compound_publication_integration.py
COMPOUND_ACTION_INTEGRATION_TEST := tests/test_compound_action_contracts.sh
MECHANISM_PUBLICATION_INTEGRATION_TEST := \
	tests/test_mechanism_publication_integration.py
COMPOUND_PUBLICATION_ARTIFACT_STEMS := compound_publication_test \
	$(COMPOUND_PUBLICATION_CASE_STEMS) compound_publication_under_test \
	compound_publication_build_under_test \
	compound_action_publication_for_publication_test
COMPOUND_PUBLICATION_TEST_ALL_ARTIFACTS := \
	sg_compound_publication_test.gnu sg_compound_publication_test.make \
	$(foreach flavor,gnu make,$(foreach stem, \
		$(COMPOUND_PUBLICATION_ARTIFACT_STEMS), \
		.sg_$(stem).$(flavor).o .sg_$(stem).$(flavor).d))
IDENTITY_TEST_BIN := sg_identity_test.make
IDENTITY_TEST_OBJS := .sg_identity_test.make.o .sg_identity_under_test.make.o \
	.sg_crc32_under_test.make.o
IDENTITY_TEST_DEPS := $(IDENTITY_TEST_OBJS:.o=.d)
RUNE_CODEC_TEST_BIN := sg_rune_codec_test.make
RUNE_CODEC_TEST_OBJS := .sg_rune_codec_test.make.o \
	.sg_rune_codec_under_test.make.o \
	.sg_rune_action_under_test.make.o \
	.sg_rune_crc_under_test.make.o
RUNE_CODEC_TEST_DEPS := $(RUNE_CODEC_TEST_OBJS:.o=.d)
RUNE_CODEC_TEST_ALL_ARTIFACTS := \
	sg_rune_codec_test.gnu sg_rune_codec_test.make \
	.sg_rune_codec_test.gnu.o .sg_rune_codec_test.gnu.d \
	.sg_rune_codec_under_test.gnu.o .sg_rune_codec_under_test.gnu.d \
	.sg_rune_codec_test.make.o .sg_rune_codec_test.make.d \
	.sg_rune_codec_under_test.make.o .sg_rune_codec_under_test.make.d
RUNE_ARTIFACT_LOADER_TEST_BIN := sg_rune_artifact_loader_test.make
RUNE_ARTIFACT_LOADER_TEST_OBJS := .sg_rune_artifact_loader_test.make.o \
	.sg_rune_artifact_loader_under_test.make.o \
	.sg_rune_codec_under_test.make.o \
	.sg_rune_action_under_test.make.o \
	.sg_rune_crc_under_test.make.o
RUNE_ARTIFACT_LOADER_TEST_DEPS := $(RUNE_ARTIFACT_LOADER_TEST_OBJS:.o=.d)
RUNE_ARTIFACT_LOADER_TEST_ALL_ARTIFACTS := \
	sg_rune_artifact_loader_test.gnu sg_rune_artifact_loader_test.make \
	.sg_rune_artifact_loader_test.gnu.o .sg_rune_artifact_loader_test.gnu.d \
	.sg_rune_artifact_loader_under_test.gnu.o .sg_rune_artifact_loader_under_test.gnu.d \
	.sg_rune_artifact_loader_test.make.o .sg_rune_artifact_loader_test.make.d \
	.sg_rune_artifact_loader_under_test.make.o .sg_rune_artifact_loader_under_test.make.d
RUNE_ARTIFACT_WRITER_TEST_BIN := sg_rune_artifact_writer_test.make
RUNE_ARTIFACT_WRITER_TEST_OBJS := .sg_rune_artifact_writer_test.make.o \
	.sg_rune_artifact_writer_under_test.make.o \
	.sg_rune_codec_under_test.make.o \
	.sg_rune_action_under_test.make.o \
	.sg_rune_crc_under_test.make.o
RUNE_ARTIFACT_WRITER_TEST_DEPS := $(RUNE_ARTIFACT_WRITER_TEST_OBJS:.o=.d)
RUNE_ARTIFACT_WRITER_TEST_ALL_ARTIFACTS := \
	sg_rune_artifact_writer_test.gnu sg_rune_artifact_writer_test.make \
	.sg_rune_artifact_writer_test.gnu.o .sg_rune_artifact_writer_test.gnu.d \
	.sg_rune_artifact_writer_under_test.gnu.o .sg_rune_artifact_writer_under_test.gnu.d \
	.sg_rune_artifact_writer_test.make.o .sg_rune_artifact_writer_test.make.d \
	.sg_rune_artifact_writer_under_test.make.o .sg_rune_artifact_writer_under_test.make.d
RUNE_MECHANISM_PLAN_TEST_BIN := sg_rune_mechanism_plan_test.make
RUNE_MECHANISM_PLAN_TEST_OBJS := .sg_rune_mechanism_plan_test.make.o \
	.sg_rune_mechanism_plan_under_test.make.o \
	.sg_train_station_plan_under_test.make.o \
	.sg_rune_codec_under_test.make.o \
	.sg_rune_action_under_test.make.o \
	.sg_rune_crc_under_test.make.o
RUNE_MECHANISM_PLAN_TEST_DEPS := $(RUNE_MECHANISM_PLAN_TEST_OBJS:.o=.d)
RUNE_MECHANISM_PLAN_TEST_ALL_ARTIFACTS := \
	sg_rune_mechanism_plan_test.gnu sg_rune_mechanism_plan_test.make \
	sg_train_station_plan_test.gnu sg_train_station_plan_test.make \
	sg_train_station_transaction_test.gnu \
	sg_train_station_transaction_test.make \
	sg_train_station_game_test.gnu sg_train_station_game_test.make \
	.sg_rune_mechanism_plan_test.gnu.o .sg_rune_mechanism_plan_test.gnu.d \
	.sg_rune_mechanism_plan_under_test.gnu.o .sg_rune_mechanism_plan_under_test.gnu.d \
	.sg_train_station_plan_under_test.gnu.o .sg_train_station_plan_under_test.gnu.d \
	.sg_rune_mechanism_plan_test.make.o .sg_rune_mechanism_plan_test.make.d \
	.sg_rune_mechanism_plan_under_test.make.o .sg_rune_mechanism_plan_under_test.make.d \
	.sg_train_station_plan_under_test.make.o .sg_train_station_plan_under_test.make.d
RUNE_MECHANISM_CATALOG_TEST_BIN := sg_rune_mechanism_catalog_test.make
RUNE_MECHANISM_CATALOG_TEST_OBJS := .sg_rune_mechanism_catalog_test.make.o \
	.sg_rune_mechanism_catalog_under_test.make.o \
	.sg_train_station_plan_under_test.make.o
RUNE_MECHANISM_CATALOG_TEST_DEPS := \
	$(RUNE_MECHANISM_CATALOG_TEST_OBJS:.o=.d)
RUNE_MECHANISM_CATALOG_TEST_ALL_ARTIFACTS := \
	sg_rune_mechanism_catalog_test.gnu sg_rune_mechanism_catalog_test.make \
	.sg_rune_mechanism_catalog_test.gnu.o \
	.sg_rune_mechanism_catalog_test.gnu.d \
	.sg_rune_mechanism_catalog_under_test.gnu.o \
	.sg_rune_mechanism_catalog_under_test.gnu.d \
	.sg_train_station_plan_under_test.gnu.o \
	.sg_train_station_plan_under_test.gnu.d \
	.sg_rune_mechanism_catalog_test.make.o \
	.sg_rune_mechanism_catalog_test.make.d \
	.sg_rune_mechanism_catalog_under_test.make.o \
	.sg_rune_mechanism_catalog_under_test.make.d \
	.sg_train_station_plan_under_test.make.o \
	.sg_train_station_plan_under_test.make.d
RUNE_MECHANISM_EXECUTION_TEST_BIN := sg_rune_mechanism_execution_test.make
RUNE_MECHANISM_EXECUTION_TEST_OBJS := \
	.sg_rune_mechanism_execution_test.make.o \
	.sg_rune_mechanism_catalog_under_test.make.o \
	.sg_rune_binding_under_test.make.o .sg_rune_runtime_under_test.make.o \
	.sg_train_station_plan_under_test.make.o \
	.sg_rune_codec_under_test.make.o \
	.sg_rune_action_under_test.make.o .sg_rune_crc_under_test.make.o \
	.sg_door_approach_under_test.make.o .sg_mover_lease_under_test.make.o \
	.sg_delayed_relay_dispatch_move_under_test.make.o \
	.sg_delayed_relay_dispatch_util_under_test.make.o \
	.sg_delayed_relay_dispatch_view_under_test.make.o \
	.sg_game_utils_under_test.make.o \
	.sg_relay_wall_game_under_test.make.o \
	.sg_relay_wall_live_under_test.make.o \
	.sg_relay_wall_ticket_under_test.make.o \
	.sg_relay_wall_transaction_live_under_test.make.o \
	.sg_relay_wall_timeline_live_under_test.make.o \
	.sg_timed_vault_runtime_under_test.make.o \
	.sg_timed_vault_game_under_test.make.o \
	.sg_timed_vault_transaction_under_test.make.o \
	.sg_delayed_relay_dispatch_trigger_under_test.make.o \
	.sg_delayed_relay_dispatch_button_under_test.make.o \
	.sg_rune_mechanism_execution_link_stubs.make.o \
	.sg_q_shared_under_test.make.o
RUNE_MECHANISM_EXECUTION_TEST_DEPS := \
	$(RUNE_MECHANISM_EXECUTION_TEST_OBJS:.o=.d)
RUNE_MECHANISM_EXECUTION_TEST_ALL_ARTIFACTS := \
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
	.sg_game_utils_under_test.$(flavor).o \
	.sg_game_utils_under_test.$(flavor).d \
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
	.sg_q_shared_under_test.$(flavor).o \
	.sg_q_shared_under_test.$(flavor).d)
RUNE_BINDING_TEST_BIN := sg_rune_binding_test.make
RUNE_BINDING_TEST_OBJS := .sg_rune_binding_test.make.o \
	.sg_rune_binding_under_test.make.o \
	.sg_train_station_plan_under_test.make.o \
	.sg_rune_runtime_under_test.make.o \
	.sg_rune_codec_under_test.make.o \
	.sg_rune_action_under_test.make.o \
	.sg_rune_crc_under_test.make.o
RUNE_BINDING_TEST_DEPS := $(RUNE_BINDING_TEST_OBJS:.o=.d)
RUNE_BINDING_TEST_ALL_ARTIFACTS := \
	sg_rune_binding_test.gnu sg_rune_binding_test.make \
	.sg_rune_binding_test.gnu.o .sg_rune_binding_test.gnu.d \
	.sg_rune_binding_under_test.gnu.o .sg_rune_binding_under_test.gnu.d \
	.sg_train_station_plan_under_test.gnu.o .sg_train_station_plan_under_test.gnu.d \
	.sg_rune_runtime_under_test.gnu.o .sg_rune_runtime_under_test.gnu.d \
	.sg_rune_binding_test.make.o .sg_rune_binding_test.make.d \
	.sg_rune_binding_under_test.make.o .sg_rune_binding_under_test.make.d \
	.sg_train_station_plan_under_test.make.o .sg_train_station_plan_under_test.make.d \
	.sg_rune_runtime_under_test.make.o .sg_rune_runtime_under_test.make.d
RUNE_ACCEPT_BIN := runeaccept.make
RUNE_ACCEPT_OBJS := .runeaccept.make.o \
	.sg_rune_file_under_test.make.o \
	.sg_rune_v2_content_identity_under_test.make.o \
	.sg_rune_v2_exact_snapshot_under_test.make.o \
	.sg_rune_artifact_loader_under_test.make.o \
	.sg_rune_codec_under_test.make.o \
	.sg_rune_action_under_test.make.o \
	.sg_rune_crc_under_test.make.o
RUNE_ACCEPT_DEPS := $(RUNE_ACCEPT_OBJS:.o=.d)
RUNE_ACCEPT_ALL_ARTIFACTS := \
	runeaccept.gnu runeaccept.make \
	.runeaccept.gnu.o .runeaccept.gnu.d \
	.sg_rune_file_under_test.gnu.o \
	.sg_rune_file_under_test.gnu.d \
	.sg_rune_v2_content_identity_under_test.gnu.o \
	.sg_rune_v2_content_identity_under_test.gnu.d \
	.sg_rune_v2_exact_snapshot_under_test.gnu.o \
	.sg_rune_v2_exact_snapshot_under_test.gnu.d \
	.runeaccept.make.o .runeaccept.make.d \
	.sg_rune_file_under_test.make.o \
	.sg_rune_file_under_test.make.d \
	.sg_rune_v2_content_identity_under_test.make.o \
	.sg_rune_v2_content_identity_under_test.make.d \
	.sg_rune_v2_exact_snapshot_under_test.make.o \
	.sg_rune_v2_exact_snapshot_under_test.make.d
SIDECAR_WIRE_TEST_BIN := sg_sidecar_wire_test.make
SIDECAR_WIRE_TEST_OBJS := .sg_sidecar_wire_test.make.o \
	.sg_sidecar_wire_under_test.make.o .sg_rune_crc_under_test.make.o
SIDECAR_WIRE_TEST_DEPS := $(SIDECAR_WIRE_TEST_OBJS:.o=.d)
SIDECAR_LOADER_TEST_BIN := sg_sidecar_loader_test.make
SIDECAR_LOADER_TEST_OBJS := .sg_sidecar_loader_test.make.o \
	.sg_sidecar_loader_under_test.make.o .sg_sidecar_wire_under_test.make.o \
	.sg_rune_crc_under_test.make.o
SIDECAR_LOADER_TEST_DEPS := $(SIDECAR_LOADER_TEST_OBJS:.o=.d)
SIDECAR_STORE_TEST_BIN := sg_sidecar_store_test.make
SIDECAR_STORE_TEST_OBJS := .sg_sidecar_store_test.make.o \
	.sg_sidecar_store_under_test.make.o .sg_sidecar_loader_under_test.make.o \
	.sg_sidecar_wire_under_test.make.o .sg_rune_crc_under_test.make.o
SIDECAR_STORE_TEST_DEPS := $(SIDECAR_STORE_TEST_OBJS:.o=.d)
DANGER_LEASE_TEST_BIN := sg_danger_lease_test.make
DANGER_LEASE_TEST_OBJS := .sg_danger_lease_test.make.o \
	.sg_danger_lease_under_test.make.o
DANGER_LEASE_TEST_DEPS := $(DANGER_LEASE_TEST_OBJS:.o=.d)
DANGER_POLICY_TEST_BIN := sg_danger_policy_test.make
DANGER_POLICY_TEST_OBJS := .sg_danger_policy_test.make.o \
	.sg_danger_policy_under_test.make.o
DANGER_POLICY_TEST_DEPS := $(DANGER_POLICY_TEST_OBJS:.o=.d)
DANGER_TEST_BIN := sg_danger_test.make
DANGER_TEST_OBJS := .sg_danger_test.make.o .sg_danger_under_test.make.o \
	.sg_rune_runtime_under_test.make.o
DANGER_TEST_DEPS := $(DANGER_TEST_OBJS:.o=.d)
FIELDS_CANDIDATE_TEST_BIN := sg_fields_candidate_test.make
FIELDS_CANDIDATE_TEST_OBJS := .sg_fields_candidate_test.make.o \
	.sg_caco_lifecycle_test.make.o .sg_game_utils_under_test.make.o \
	.sg_q_shared_under_test.make.o .sg_fields_candidate_under_test.make.o \
	.sg_action_under_test.make.o \
	.sg_caco_projection_under_test.make.o \
	.sg_goal_projection_under_test.make.o
FIELDS_CANDIDATE_TEST_DEPS := $(FIELDS_CANDIDATE_TEST_OBJS:.o=.d)
STALL_CENSUS_PYTHON_TEST := tests/test_stallcensus.py
SPECTATOR_SOUND_TEST_BIN := sg_spectator_sound_test.make
SPECTATOR_SOUND_TEST_OBJS := .sg_spectator_sound_test.make.o \
	.sg_spectator_sound_net_under_test.make.o
SPECTATOR_SOUND_TEST_DEPS := $(SPECTATOR_SOUND_TEST_OBJS:.o=.d)
SPECTATOR_SOUND_TEST_ALL_ARTIFACTS := \
	$(foreach flavor,gnu make,sg_spectator_sound_test.$(flavor) \
	.sg_spectator_sound_test.$(flavor).o \
	.sg_spectator_sound_test.$(flavor).d \
	.sg_spectator_sound_net_under_test.$(flavor).o \
	.sg_spectator_sound_net_under_test.$(flavor).d)
HUMAN_SPEED_TEST_BIN := sg_human_speed_test.make
HUMAN_SPEED_TEST_OBJS := .sg_human_speed_test.make.o \
	.sg_human_speed_under_test.make.o \
	.sg_human_speed_pmove_under_test.make.o \
	.sg_human_speed_q_shared_under_test.make.o
HUMAN_SPEED_TEST_DEPS := $(HUMAN_SPEED_TEST_OBJS:.o=.d)
HUMAN_SPEED_INTEGRATION_TEST := tests/test_human_speed_integration.py
HUMAN_TRACE_TESTS := tests/test_humantrace.py \
	tests/test_human_trace_integration.py \
	tests/test_human_trace_v3_integration.py
HUMAN_TRACE_HOOK_TEST_BIN := sg_human_trace_hook_test.make
HUMAN_TRACE_HOOK_TEST_SOURCE := tests/sg_human_trace_hook_test.c
HUMAN_TRACE_HOOK_TEST_ALL_ARTIFACTS := \
	sg_human_trace_hook_test.gnu sg_human_trace_hook_test.make
HUMAN_SPEED_TEST_ALL_ARTIFACTS := \
	sg_human_speed_test.gnu sg_human_speed_test.make \
	.sg_human_speed_test.gnu.o .sg_human_speed_test.gnu.d \
	.sg_human_speed_under_test.gnu.o .sg_human_speed_under_test.gnu.d \
	.sg_human_speed_pmove_under_test.gnu.o \
	.sg_human_speed_pmove_under_test.gnu.d \
	.sg_human_speed_q_shared_under_test.gnu.o \
	.sg_human_speed_q_shared_under_test.gnu.d \
	$(HUMAN_SPEED_TEST_OBJS) $(HUMAN_SPEED_TEST_DEPS)
DOOR_APPROACH_TEST_BIN := sg_door_approach_test.make
DOOR_APPROACH_TEST_OBJS := .sg_door_approach_test.make.o \
	.sg_door_approach_under_test.make.o
DOOR_APPROACH_TEST_DEPS := $(DOOR_APPROACH_TEST_OBJS:.o=.d)
DOOR_APPROACH_INTEGRATION_TEST := tests/test_door_approach_integration.py
DOOR_APPROACH_TEST_ALL_ARTIFACTS := \
	$(foreach flavor,gnu make,sg_door_approach_test.$(flavor) \
	.sg_door_approach_test.$(flavor).o \
	.sg_door_approach_test.$(flavor).d \
	.sg_door_approach_under_test.$(flavor).o \
	.sg_door_approach_under_test.$(flavor).d)
DEFENSE_SHIFT_TEST_BIN := sg_defense_shift_test.make
DEFENSE_SHIFT_TEST_OBJS := .sg_defense_shift_test.make.o \
	.sg_defense_shift_under_test.make.o
DEFENSE_SHIFT_TEST_DEPS := $(DEFENSE_SHIFT_TEST_OBJS:.o=.d)
DEFENSE_SHIFT_INTEGRATION_TEST := tests/test_defense_shift_integration.py
DEFENSE_COMBAT_INTEGRATION_TEST := tests/test_defense_combat_integration.py
DEFENSE_SHIFT_TEST_ALL_ARTIFACTS := \
	$(foreach flavor,gnu make,sg_defense_shift_test.$(flavor) \
	.sg_defense_shift_test.$(flavor).o .sg_defense_shift_test.$(flavor).d \
	.sg_defense_shift_under_test.$(flavor).o \
	.sg_defense_shift_under_test.$(flavor).d)
DEFENSE_SUPPLY_TEST_BIN := sg_defense_supply_test.make
DEFENSE_SUPPLY_TEST_OBJS := .sg_defense_supply_test.make.o \
	.sg_defense_supply_under_test.make.o
DEFENSE_SUPPLY_TEST_DEPS := $(DEFENSE_SUPPLY_TEST_OBJS:.o=.d)
DEFENSE_SUPPLY_INTEGRATION_TEST := tests/test_defender_supply_integration.py
DEFENSE_SUPPLY_TEST_ALL_ARTIFACTS := \
	$(foreach flavor,gnu make,sg_defense_supply_test.$(flavor) \
	.sg_defense_supply_test.$(flavor).o .sg_defense_supply_test.$(flavor).d \
	.sg_defense_supply_under_test.$(flavor).o \
	.sg_defense_supply_under_test.$(flavor).d)
STRIKE_ADAPTER_TEST_BIN := sg_strike_adapter_test.make
STRIKE_ADAPTER_TEST_OBJS := .sg_strike_adapter_test.make.o \
	.sg_strike_under_test.make.o .sg_strike_adapter_under_test.make.o
STRIKE_ADAPTER_TEST_DEPS := $(STRIKE_ADAPTER_TEST_OBJS:.o=.d)
STRIKE_ADAPTER_INTEGRATION_TEST := tests/test_strike_integration.py
STRIKE_ADAPTER_TEST_ALL_ARTIFACTS := \
	$(foreach flavor,gnu make,sg_strike_adapter_test.$(flavor) \
	.sg_strike_adapter_test.$(flavor).o .sg_strike_adapter_test.$(flavor).d \
	.sg_strike_under_test.$(flavor).o .sg_strike_under_test.$(flavor).d \
	.sg_strike_adapter_under_test.$(flavor).o \
	.sg_strike_adapter_under_test.$(flavor).d)
ITEM_COMMITMENT_TEST_BIN := sg_item_commitment_test.make
ITEM_COMMITMENT_TEST_OBJS := .sg_item_commitment_test.make.o \
	.sg_item_commitment_under_test.make.o slipgate/sg_pickup_target.o
ITEM_COMMITMENT_TEST_DEPS := $(ITEM_COMMITMENT_TEST_OBJS:.o=.d)
ITEM_COMMITMENT_INTEGRATION_TEST := tests/test_item_commitment_integration.py
ITEM_COMMITMENT_TEST_ALL_ARTIFACTS := \
	$(foreach flavor,gnu make,sg_item_commitment_test.$(flavor) \
	.sg_item_commitment_test.$(flavor).o .sg_item_commitment_test.$(flavor).d \
	.sg_item_commitment_under_test.$(flavor).o \
	.sg_item_commitment_under_test.$(flavor).d)
HOOK_DIAGNOSTICS_TEST_BIN := sg_hook_diagnostics_test.make
HOOK_DIAGNOSTICS_TEST_OBJS := .sg_hook_diagnostics_test.make.o \
	.sg_hook_diagnostics_under_test.make.o
HOOK_DIAGNOSTICS_TEST_DEPS := $(HOOK_DIAGNOSTICS_TEST_OBJS:.o=.d)
HOOK_DIAGNOSTICS_INTEGRATION_TEST := tests/test_hook_diagnostics_integration.py
HOOK_DIAGNOSTICS_CONSUMER_TEST := tests/test_hook_diagnostic_consumers.py
ROLE_TELEMETRY_CONSUMER_TEST := tests/test_role_telemetry_consumers.py
HOOK_EVENTS_TEST := tests/test_hookevents.py
HOOK_DIAGNOSTICS_TEST_ALL_ARTIFACTS := \
	$(foreach flavor,gnu make,sg_hook_diagnostics_test.$(flavor) \
	.sg_hook_diagnostics_test.$(flavor).o \
	.sg_hook_diagnostics_test.$(flavor).d \
	.sg_hook_diagnostics_under_test.$(flavor).o \
	.sg_hook_diagnostics_under_test.$(flavor).d)
RUN_HANDOFF_TEST_BIN := sg_run_handoff_test.make
RUN_HANDOFF_TEST_OBJS := .sg_run_handoff_test.make.o \
	.sg_run_handoff_descend_under_test.make.o \
	.sg_push_live_under_test.make.o \
	.sg_run_handoff_pmove_under_test.make.o \
	.sg_run_handoff_q_shared_under_test.make.o
RUN_HANDOFF_TEST_DEPS := $(RUN_HANDOFF_TEST_OBJS:.o=.d)
RUN_HANDOFF_INTEGRATION_TEST := tests/test_run_handoff_integration.py
RUN_HANDOFF_TEST_ALL_ARTIFACTS := \
	$(foreach flavor,gnu make,sg_run_handoff_test.$(flavor) \
	.sg_run_handoff_test.$(flavor).o .sg_run_handoff_test.$(flavor).d \
	.sg_run_handoff_descend_under_test.$(flavor).o \
	.sg_run_handoff_descend_under_test.$(flavor).d \
	.sg_run_handoff_pmove_under_test.$(flavor).o \
	.sg_run_handoff_pmove_under_test.$(flavor).d \
	.sg_run_handoff_q_shared_under_test.$(flavor).o \
	.sg_run_handoff_q_shared_under_test.$(flavor).d)
RUNE_INSTALL_TEST_BIN := sg_rune_install_test.make
RUNE_INSTALL_TEST_OBJS := .sg_rune_install_test.make.o \
	.sg_rune_install_under_test.make.o .sg_rune_stream_under_test.make.o \
	.sg_rune_artifact_writer_under_test.make.o \
	.sg_rune_codec_under_test.make.o .sg_rune_action_under_test.make.o \
	.sg_rune_crc_under_test.make.o
RUNE_INSTALL_TEST_DEPS := $(RUNE_INSTALL_TEST_OBJS:.o=.d)
RUNE_PROOF_TEST_BIN := sg_rune_proof_test.make
RUNE_PROOF_TEST_OBJS := .sg_rune_proof_test.make.o \
	.sg_rune_proof_under_test.make.o
RUNE_PROOF_TEST_DEPS := $(RUNE_PROOF_TEST_OBJS:.o=.d)
RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN := sg_rune_objective_diagnostics_test.make
RUNE_OBJECTIVE_DIAGNOSTICS_TEST_OBJS := \
	.sg_rune_objective_diagnostics_test.make.o
RUNE_OBJECTIVE_DIAGNOSTICS_TEST_DEPS := \
	$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_OBJS:.o=.d)
RUNE_OBJECTIVE_DIAGNOSTICS_TEST_ALL_ARTIFACTS := \
	$(foreach flavor,gnu make,sg_rune_objective_diagnostics_test.$(flavor) \
	.sg_rune_objective_diagnostics_test.$(flavor).o \
	.sg_rune_objective_diagnostics_test.$(flavor).d)
REPLAY_TEST_BIN := sg_replay_test.make
REPLAY_TEST_OBJS := .sg_replay_test.make.o .sg_replay_under_test.make.o
REPLAY_TEST_DEPS := $(REPLAY_TEST_OBJS:.o=.d)
CHAIN_HOOK_REPLAY_TEST_BIN := sg_chain_hook_replay_test.make
CHAIN_HOOK_REPLAY_TEST_OBJS := .sg_chain_hook_replay_test.make.o \
	.sg_chain_hook_replay_under_test.make.o \
	.sg_chain_hook_replay_replay_under_test.make.o
CHAIN_HOOK_REPLAY_TEST_DEPS := $(CHAIN_HOOK_REPLAY_TEST_OBJS:.o=.d)
CHAIN_HOOK_GAME_INTEGRATION_TEST := tests/test_chain_hook_game_integration.py
DROP_LIVE_TEST_BIN := sg_drop_live_test.make
DROP_LIVE_TEST_OBJS := .sg_drop_live_test.make.o \
	.sg_drop_live_under_test.make.o .sg_drop_live_replay_under_test.make.o
DROP_LIVE_TEST_DEPS := $(DROP_LIVE_TEST_OBJS:.o=.d)
SWIM_LIVE_TEST_BIN := sg_swim_live_test.make
SWIM_LIVE_TEST_OBJS := .sg_swim_live_test.make.o \
	.sg_swim_live_under_test.make.o .sg_swim_live_replay_under_test.make.o
SWIM_LIVE_TEST_DEPS := $(SWIM_LIVE_TEST_OBJS:.o=.d)
COMPOUND_SWIM_LIVE_TEST_BIN := sg_compound_swim_live_test.make
COMPOUND_SWIM_LIVE_TEST_OBJS := .sg_compound_swim_live_test.make.o \
	.sg_compound_swim_live_under_test.make.o \
	.sg_compound_swim_live_compound_under_test.make.o \
	.sg_compound_swim_live_action_under_test.make.o \
	.sg_compound_swim_live_replay_under_test.make.o
COMPOUND_SWIM_LIVE_TEST_DEPS := $(COMPOUND_SWIM_LIVE_TEST_OBJS:.o=.d)
COMPOUND_SWIM_LIVE_INTEGRATION_TEST := \
	tests/test_compound_swim_live_integration.py
COMPOUND_SWIM_GAME_TEST_BIN := sg_compound_swim_game_test.make
COMPOUND_SWIM_GAME_TEST_OBJS := .sg_compound_swim_game_test.make.o \
	.sg_compound_swim_game_under_test.make.o $(filter-out \
	.sg_compound_swim_live_test.make.o,$(COMPOUND_SWIM_LIVE_TEST_OBJS))
COMPOUND_SWIM_GAME_TEST_DEPS := $(COMPOUND_SWIM_GAME_TEST_OBJS:.o=.d)
COMPOUND_SWIM_GAME_INTEGRATION_TEST := tests/test_compound_swim_game_integration.py
COMPOUND_SWIM_LIVE_TEST_ALL_ARTIFACTS := \
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
	$(COMPOUND_SWIM_LIVE_TEST_OBJS) \
	$(COMPOUND_SWIM_LIVE_TEST_DEPS)
COMPOUND_SWIM_GAME_TEST_ALL_ARTIFACTS := \
	sg_compound_swim_game_test.gnu sg_compound_swim_game_test.make \
	$(COMPOUND_SWIM_GAME_TEST_OBJS) $(COMPOUND_SWIM_GAME_TEST_DEPS) \
	.sg_compound_swim_game_test.gnu.o .sg_compound_swim_game_test.gnu.d \
	.sg_compound_swim_game_under_test.gnu.o \
	.sg_compound_swim_game_under_test.gnu.d
COMPOUND_DROP_LIVE_TEST_BIN := sg_compound_drop_live_test.make
COMPOUND_DROP_LIVE_TEST_OBJS := .sg_compound_drop_live_test.make.o \
	.sg_compound_drop_live_under_test.make.o \
	.sg_compound_drop_live_finish_under_test.make.o \
	.sg_compound_drop_live_drop_under_test.make.o \
	.sg_compound_drop_live_compound_under_test.make.o \
	.sg_compound_drop_live_action_under_test.make.o \
	.sg_compound_drop_live_replay_under_test.make.o
COMPOUND_DROP_LIVE_TEST_DEPS := $(COMPOUND_DROP_LIVE_TEST_OBJS:.o=.d)
COMPOUND_DROP_GAME_TEST_BIN := sg_compound_drop_game_test.make
COMPOUND_DROP_GAME_TEST_OBJS := .sg_compound_drop_game_test.make.o \
	.sg_compound_drop_game_under_test.make.o
COMPOUND_DROP_GAME_TEST_DEPS := $(COMPOUND_DROP_GAME_TEST_OBJS:.o=.d)
COMPOUND_DROP_FANOUT_TEST_BIN := sg_compound_drop_fanout_game_test.make
COMPOUND_DROP_FANOUT_TEST_OBJS := \
	.sg_compound_drop_fanout_game_test.make.o \
	.sg_compound_drop_game_under_test.make.o
COMPOUND_DROP_FANOUT_TEST_DEPS := \
	$(COMPOUND_DROP_FANOUT_TEST_OBJS:.o=.d)
COMPOUND_DROP_TRANSITION_TEST_BIN := sg_compound_drop_transition_test.make
COMPOUND_DROP_TRANSITION_TEST_OBJS := \
	.sg_compound_drop_transition_test.make.o \
	.sg_compound_drop_transition_under_test.make.o \
	.sg_push_live_under_test.make.o
COMPOUND_DROP_TRANSITION_TEST_DEPS := \
	$(COMPOUND_DROP_TRANSITION_TEST_OBJS:.o=.d)
COMPOUND_DROP_TEST_ALL_ARTIFACTS := \
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
COMPOUND_HOOK_LIVE_TEST_BIN := sg_compound_hook_live_test.make
COMPOUND_HOOK_LIVE_TEST_OBJS := .sg_compound_hook_live_test.make.o \
	.sg_compound_hook_live_fixture.make.o \
	.sg_compound_hook_live_safety_test.make.o \
	.sg_compound_hook_live_under_test.make.o \
	.sg_compound_hook_live_finish_under_test.make.o \
	.sg_compound_hook_live_compound_under_test.make.o \
	.sg_compound_hook_live_action_under_test.make.o \
	.sg_compound_hook_live_replay_under_test.make.o \
	.sg_compound_hook_live_hook_under_test.make.o \
	.sg_compound_hook_live_publication_under_test.make.o
COMPOUND_HOOK_LIVE_TEST_DEPS := $(COMPOUND_HOOK_LIVE_TEST_OBJS:.o=.d)
COMPOUND_HOOK_GAME_TEST_BIN := sg_compound_hook_game_test.make
COMPOUND_HOOK_GAME_TEST_OBJS := .sg_compound_hook_game_test.make.o \
	.sg_compound_hook_game_under_test.make.o \
	.sg_compound_hook_game_lifecycle_under_test.make.o
COMPOUND_HOOK_GAME_TEST_DEPS := $(COMPOUND_HOOK_GAME_TEST_OBJS:.o=.d)
COMPOUND_HOOK_GAME_INTEGRATION_TEST := tests/test_compound_hook_game_integration.py
COMPOUND_HOOK_GAME_EVENTS_TEST_BIN := sg_compound_hook_game_events_test.make
COMPOUND_HOOK_GAME_EVENTS_TEST_OBJS := \
	.sg_compound_hook_game_events_test.make.o \
	.sg_compound_hook_game_events_under_test.make.o
COMPOUND_HOOK_GAME_EVENTS_TEST_DEPS := \
	$(COMPOUND_HOOK_GAME_EVENTS_TEST_OBJS:.o=.d)
COMPOUND_HOOK_GAME_EVENTS_ALL_ARTIFACTS := \
	$(foreach flavor,gnu make,sg_compound_hook_game_events_test.$(flavor) \
	.sg_compound_hook_game_events_test.$(flavor).o \
	.sg_compound_hook_game_events_test.$(flavor).d \
	.sg_compound_hook_game_events_under_test.$(flavor).o \
	.sg_compound_hook_game_events_under_test.$(flavor).d)
COMPOUND_HOOK_TEST_ALL_ARTIFACTS := \
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
HOOK_LIVE_TEST_BIN := sg_hook_live_test.make
HOOK_LIVE_TEST_OBJS := .sg_hook_live_test.make.o \
	.sg_hook_live_under_test.make.o .sg_hook_live_replay_under_test.make.o
HOOK_LIVE_TEST_DEPS := $(HOOK_LIVE_TEST_OBJS:.o=.d)
ROCKETJUMP_LIVE_TEST_BIN := sg_rocketjump_live_test.make
ROCKETJUMP_LIVE_TEST_OBJS := .sg_rocketjump_live_test.make.o \
	.sg_rocketjump_live_under_test.make.o
ROCKETJUMP_LIVE_TEST_DEPS := $(ROCKETJUMP_LIVE_TEST_OBJS:.o=.d)
PUSH_LIVE_TEST_BIN := sg_push_live_test.make
PUSH_LIVE_TEST_OBJS := .sg_push_live_test.make.o \
	.sg_push_live_under_test.make.o .sg_push_falling_under_test.make.o
PUSH_LIVE_TEST_DEPS := $(PUSH_LIVE_TEST_OBJS:.o=.d)
PUSH_GAME_INTEGRATION_TEST := tests/test_push_game_integration.py
TRAIN_GATE_GAME_INTEGRATION_TEST := tests/test_train_gate_game_integration.py
SHOOT_DOOR_GAME_INTEGRATION_TEST := tests/test_shoot_door_game_integration.py
ROCKETJUMP_CADENCE_TEST_BIN := sg_rocketjump_cadence_test.make
ROCKETJUMP_CADENCE_TEST_OBJS := .sg_rocketjump_cadence_test.make.o \
	.sg_rocketjump_cadence_under_test.make.o
ROCKETJUMP_CADENCE_TEST_DEPS := $(ROCKETJUMP_CADENCE_TEST_OBJS:.o=.d)
ROCKETJUMP_GAME_TEST_BIN := sg_rocketjump_game_test.make
ROCKETJUMP_GAME_TEST_OBJS := .sg_rocketjump_game_test.make.o \
	.sg_rocketjump_game_under_test.make.o \
	.sg_rocketjump_game_live_under_test.make.o \
	.sg_rocketjump_game_q_shared_under_test.make.o
ROCKETJUMP_GAME_TEST_DEPS := $(ROCKETJUMP_GAME_TEST_OBJS:.o=.d)
ROCKETJUMP_TEST_ALL_ARTIFACTS := \
	sg_rocketjump_live_test.gnu sg_rocketjump_live_test.make \
	sg_push_live_test.gnu sg_push_live_test.make \
	sg_rocketjump_cadence_test.gnu sg_rocketjump_cadence_test.make \
	sg_rocketjump_game_test.gnu sg_rocketjump_game_test.make \
	$(ROCKETJUMP_LIVE_TEST_OBJS) $(ROCKETJUMP_LIVE_TEST_DEPS) \
	$(PUSH_LIVE_TEST_OBJS) $(PUSH_LIVE_TEST_DEPS) \
	$(ROCKETJUMP_CADENCE_TEST_OBJS) $(ROCKETJUMP_CADENCE_TEST_DEPS) \
	$(ROCKETJUMP_GAME_TEST_OBJS) $(ROCKETJUMP_GAME_TEST_DEPS)
HOOK_INTEGRATION_TEST := tests/test_hook_live_integration.py
HOOK_DISCIPLINE_TEST_BIN := sg_hook_discipline_test.make
HOOK_DISCIPLINE_TEST_OBJS := .sg_hook_discipline_test.make.o \
	.sg_hook_discipline_under_test.make.o
HOOK_DISCIPLINE_TEST_DEPS := $(HOOK_DISCIPLINE_TEST_OBJS:.o=.d)
HOOK_DISCIPLINE_TEST_ALL_ARTIFACTS := \
	sg_hook_discipline_test.gnu sg_hook_discipline_test.make \
	.sg_hook_discipline_test.gnu.o .sg_hook_discipline_test.gnu.d \
	.sg_hook_discipline_under_test.gnu.o \
	.sg_hook_discipline_under_test.gnu.d \
	.sg_hook_discipline_test.make.o .sg_hook_discipline_test.make.d \
	.sg_hook_discipline_under_test.make.o \
	.sg_hook_discipline_under_test.make.d
RUNE_NAMING_TEST := tests/test_rune_naming.py
RELEASE_WORKFLOW_TEST := tests/test_release_workflow.py
PROJECT_COMPLETION_PLAN_TEST := tests/test_project_completion_plan.py
DESLOP_AUDIT := tools/deslop_audit.py
DESLOP_AUDIT_TEST := tests/test_deslop_audit.py
SOURCE_SIZE_BUDGET := tools/source-size-budget.json
RUNE_PYTHON_TESTS := tests/test_rune_contracts.py \
	tests/test_rune_artifact.py \
	tests/test_sidecario.py \
	tests/test_rune_tool_readers.py \
	tests/test_lmctf58_rune_accept.py \
	tests/test_rune_water_overflow_failfast.py \
	tests/test_rune_pair_preflight.py
RUNGEN_TEST := tests/test_runegen_gate.py
RUNGEN_PAIR_TEST := tests/test_runegen_pair.py
RUNGEN_PAIR_TOOL := tools/runegen_pair.py
BOTKIN_TEST := tests/test_botkin_cli.py
FILM_PYTHON ?= $(HOME)/.venvs/slipgate-film/bin/python
SHEET_CLI_TEST := tests/test_fightsheet_cli_status.py
RUNE_CORPUS_CONTROLLER_TEST := tests/test_rune_corpus_controller.py
RUNE_CORPUS_FINALIZER_TEST := tests/test_rune_corpus_finalizer.py
RUNE_GENERATOR_CONFIG_TEST := tests/test_rune_generator_config.py
BUILD_PYTHON_RUNTIME_TEST := tests/test_build_python_runtime.py
FLEET_RUNNER_TEST := tests/test_fleet_runner.py
FLEET_RUNNER_LIVE_TEST := tests/test_fleet_runner_live.py
ROUTE_ONLY_EVIDENCE_TEST := tests/test_route_only_evidence.py
ROUTE_ONLY_MATCH_CONFIG_TEST := tests/test_route_only_match_config.py
SERVER_BUNDLE_TEST := tests/test_server_bundle.py
CHAIN_HOOK_FRONTIER_INTEGRATION_TEST := \
	tests/test_chain_hook_frontier_integration.py
HOOK_SURFACE_VOLUME_INTEGRATION_TEST := \
	tests/test_hook_surface_volume_integration.py
BSPMECHANISMS_TEST := tests/test_bspmechanisms.py
WAVELOOP_PROCESS_TEST := tests/test_waveloop_process_scope.py
TEMP_FLAG_DIAGNOSTIC_TEST := tests/test_no_temp_flag_diagnostics.py
CARRIER_RETURN_TEST := tests/test_carrier_return_progress.py
COMBAT_AIM_TEST := tests/test_combat_aim_envelope.py
COMBAT_LAND_LEAD_TEST := tests/test_combat_land_lead.py
OFFENSE_FLAG_PICKUP_TEST := tests/test_offense_flag_pickup_recovery.py
ROTATOR_SWEEP_TEST_BIN := sg_rotator_sweep_test.make
ROTATOR_SWEEP_TEST_OBJS := .sg_rotator_sweep_test.make.o \
	.sg_mover_subject_sweep_oracle_under_test.make.o \
	.sg_rotator_sweep_under_test.make.o \
	.sg_rotator_sweep_q_shared_under_test.make.o
ROTATOR_SWEEP_TEST_DEPS := $(ROTATOR_SWEEP_TEST_OBJS:.o=.d)
MOVER_SUBJECT_SWEEP_TEST_BIN := sg_mover_subject_sweep_test.make
MOVER_SUBJECT_SWEEP_TEST_OBJS := \
	.sg_mover_subject_sweep_test.make.o \
	.sg_mover_subject_sweep_oracle_under_test.make.o \
	.sg_rotator_sweep_under_test.make.o \
	.sg_door_approach_under_test.make.o \
	.sg_mover_subject_sweep_util_under_test.make.o \
	.sg_replay_under_test.make.o \
	.sg_mover_subject_sweep_view_under_test.make.o \
	.sg_mover_subject_sweep_q_shared_under_test.make.o \
	.sg_mover_subject_sweep_pmove_under_test.make.o \
	slipgate/sg_timed_vault_egress.o \
	slipgate/sg_timed_vault_egress_game.o
MOVER_SUBJECT_SWEEP_TEST_DEPS := \
	$(MOVER_SUBJECT_SWEEP_TEST_OBJS:.o=.d)
MOVER_SUBJECT_SWEEP_TEST_ALL_ARTIFACTS := \
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
	.sg_mover_subject_sweep_pmove_under_test.make.o \
	.sg_mover_subject_sweep_pmove_under_test.make.d \
	$(MOVER_SUBJECT_SWEEP_TEST_OBJS) \
	$(MOVER_SUBJECT_SWEEP_TEST_DEPS)
COMPOUND_ORACLE_FIXTURE_STEMS := compound_oracle_fake_game \
	compound_oracle_fake_host compound_oracle_fixture \
	compound_swim_oracle_preopen_cases \
	compound_swim_oracle_recovery_cases compound_declared_oracle_cases \
	compound_hook_oracle_scenario
COMPOUND_ORACLE_FIXTURE_MAKE_OBJS := \
	$(addprefix .sg_,$(addsuffix .make.o,$(COMPOUND_ORACLE_FIXTURE_STEMS)))
COMPOUND_ORACLE_ARTIFACT_STEMS := compound_swim_oracle_test \
	compound_hook_oracle_test compound_hook_oracle_fixture \
	$(COMPOUND_ORACLE_FIXTURE_STEMS) \
	compound_swim_oracle_oracle_under_test \
	compound_swim_oracle_rune_timing_under_test \
	compound_swim_oracle_replay_under_test \
	compound_swim_oracle_compound_under_test \
	compound_swim_oracle_world_under_test \
	compound_swim_oracle_q_shared_under_test \
	mover_subject_sweep_util_under_test
COMPOUND_ORACLE_ALL_ARTIFACTS := \
	sg_compound_swim_oracle_test.gnu sg_compound_swim_oracle_test.make \
	sg_compound_hook_oracle_test.gnu sg_compound_hook_oracle_test.make \
	$(foreach flavor,gnu make,$(foreach stem, \
		$(COMPOUND_ORACLE_ARTIFACT_STEMS), \
		.sg_$(stem).$(flavor).o .sg_$(stem).$(flavor).d))
COMPOUND_SWIM_ORACLE_TEST_BIN := sg_compound_swim_oracle_test.make
COMPOUND_SWIM_ORACLE_TEST_OBJS := \
	.sg_compound_swim_oracle_test.make.o \
	.sg_compound_oracle_fake_game.make.o .sg_compound_oracle_fake_host.make.o \
	slipgate/sg_chain_hook_replay.o slipgate/sg_hook_oracle.o \
	.sg_compound_oracle_fixture.make.o \
	.sg_compound_swim_oracle_preopen_cases.make.o \
	.sg_compound_swim_oracle_recovery_cases.make.o \
	.sg_compound_declared_oracle_cases.make.o \
	.sg_compound_swim_oracle_oracle_under_test.make.o \
	.sg_rotator_sweep_under_test.make.o \
	.sg_compound_swim_oracle_rune_timing_under_test.make.o \
	.sg_compound_swim_oracle_replay_under_test.make.o \
	.sg_compound_swim_oracle_compound_under_test.make.o \
	.sg_compound_swim_oracle_world_under_test.make.o \
	.sg_rocketjump_live_under_test.make.o \
	.sg_compound_swim_oracle_q_shared_under_test.make.o \
	.sg_mover_subject_sweep_util_under_test.make.o
COMPOUND_SWIM_ORACLE_TEST_DEPS := \
	$(COMPOUND_SWIM_ORACLE_TEST_OBJS:.o=.d)
COMPOUND_SWIM_ORACLE_TEST_ALL_ARTIFACTS := \
	$(COMPOUND_ORACLE_ALL_ARTIFACTS)
COMPOUND_HOOK_ORACLE_TEST_BIN := sg_compound_hook_oracle_test.make
COMPOUND_HOOK_ORACLE_TEST_OBJS := \
	.sg_compound_hook_oracle_test.make.o .sg_compound_hook_oracle_fixture.make.o \
	.sg_compound_hook_oracle_scenario.make.o \
	.sg_compound_oracle_fake_game.make.o .sg_compound_oracle_fake_host.make.o \
	slipgate/sg_chain_hook_replay.o slipgate/sg_hook_oracle.o \
	.sg_compound_oracle_fixture.make.o \
	.sg_compound_swim_oracle_oracle_under_test.make.o \
	.sg_rotator_sweep_under_test.make.o \
	.sg_compound_swim_oracle_rune_timing_under_test.make.o \
	.sg_compound_swim_oracle_replay_under_test.make.o \
	.sg_compound_swim_oracle_compound_under_test.make.o \
	.sg_compound_swim_oracle_world_under_test.make.o \
	.sg_compound_swim_oracle_q_shared_under_test.make.o \
	.sg_mover_subject_sweep_util_under_test.make.o
COMPOUND_HOOK_ORACLE_TEST_DEPS := \
	$(COMPOUND_HOOK_ORACLE_TEST_OBJS:.o=.d)
COMPOUND_HOOK_ORACLE_TEST_ALL_ARTIFACTS := \
	$(COMPOUND_ORACLE_ALL_ARTIFACTS)
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
MAPLIST_ROTATION_TEST_BIN := maplist_rotation_test.make
MAPLIST_ROTATION_TEST_ALL_ARTIFACTS := \
	maplist_rotation_test.gnu maplist_rotation_test.make
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
	.sg_danger_test.make.o .sg_danger_test.make.d \
	.sg_danger_under_test.make.o .sg_danger_under_test.make.d \
	.sg_rune_runtime_under_test.make.o .sg_rune_runtime_under_test.make.d \
	sg_fields_candidate_test.gnu sg_fields_candidate_test.make \
	.sg_fields_candidate_test.gnu.o .sg_fields_candidate_test.gnu.d \
	.sg_caco_lifecycle_test.gnu.o .sg_caco_lifecycle_test.gnu.d \
	.sg_fields_candidate_under_test.gnu.o .sg_fields_candidate_under_test.gnu.d \
	.sg_caco_projection_under_test.gnu.o .sg_caco_projection_under_test.gnu.d \
	.sg_goal_projection_under_test.gnu.o .sg_goal_projection_under_test.gnu.d \
	.sg_fields_candidate_test.make.o .sg_fields_candidate_test.make.d \
	.sg_caco_lifecycle_test.make.o .sg_caco_lifecycle_test.make.d \
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

CC ?= gcc
WINDRES ?= windres
STRIP ?= strip
RM ?= rm -f

CPPFLAGS += -I.
CFLAGS ?= -DVER='"$(VER)"' -std=c11 -O0 -fno-strict-aliasing -g -Wall -MMD
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
	g_maplist.o \
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
	slipgate/sg_rune_codec.o \
	slipgate/sg_rune_artifact_loader.o \
	slipgate/sg_rune_artifact_writer.o \
	slipgate/sg_rune_v2_content_identity.o \
	slipgate/sg_rune_v2_exact_snapshot.o \
	slipgate/sg_rune_file.o \
	slipgate/sg_rune_stream.o \
	slipgate/sg_rune_mechanism_catalog.o \
	slipgate/sg_rune_mechanism_plan.o \
	slipgate/sg_train_station_plan.o \
	slipgate/sg_train_station_candidate.o \
	slipgate/sg_train_station_candidate_game.o \
	slipgate/sg_train_station_board_path.o \
	slipgate/sg_train_station_transaction.o \
	slipgate/sg_train_station_game.o \
	slipgate/sg_rune_runtime.o \
	slipgate/sg_rune_binding.o \
	slipgate/sg_rune_learning.o \
	slipgate/sg_rune_learning_game.o \
	slipgate/sg_rune_authority_game.o \
	slipgate/sg_rune_update_source.o \
	slipgate/sg_water_forest.o \
	sg_sidecar_wire.o \
	sg_sidecar_loader.o \
	sg_sidecar_store.o \
	sg_rune_install.o \
	sg_rune_proof.o \
	sg_replay.o \
	slipgate/sg_chain_hook_replay.o slipgate/sg_hook_oracle.o slipgate/sg_hook_game.o \
	slipgate/sg_rune_hook_frontier.o slipgate/sg_rune_late_path.o \
	slipgate/sg_rune_topology.o slipgate/sg_rune_topology_game.o \
	slipgate/sg_rune_reverse_boundary.o \
	slipgate/sg_rune_seed_game.o sg_compound.o \
	slipgate/sg_mover_lease.o \
	slipgate/sg_button_live.o \
	slipgate/sg_mechanism_timeline.o \
	slipgate/sg_relay_wall_transaction.o \
	slipgate/sg_delayed_use_ticket.o \
	slipgate/sg_relay_wall_live.o \
	slipgate/sg_relay_wall_game.o \
	slipgate/sg_relay_wall_objective.o \
	slipgate/sg_relay_wall_objective_game.o \
	slipgate/sg_timed_vault_transaction.o \
	slipgate/sg_timed_vault_game.o \
	slipgate/sg_timed_vault_game_runtime.o \
	slipgate/sg_timed_vault_egress.o \
	slipgate/sg_timed_vault_egress_game.o \
	slipgate/sg_compound_guard.o \
	slipgate/sg_compound_guard_game.o \
	slipgate/sg_compound_swim_live.o \
	slipgate/sg_compound_swim_game.o \
	slipgate/sg_declared_door_guard.o \
	slipgate/sg_compound_world.o \
	slipgate/sg_compound_gen.o \
	slipgate/sg_compound_gen_game.o \
	slipgate/sg_compound_action_gen.o \
	slipgate/sg_compound_publication.o \
	slipgate/sg_compound_publication_build.o \
	slipgate/sg_compound_action_publication.o \
	slipgate/sg_compound_drop_live.o \
	slipgate/sg_compound_drop_live_finish.o \
	slipgate/sg_compound_drop_game.o \
	slipgate/sg_compound_hook_live.o \
	slipgate/sg_compound_hook_live_finish.o \
	slipgate/sg_compound_hook_game.o \
	slipgate/sg_compound_hook_game_lifecycle.o \
	slipgate/sg_compound_hook_game_events.o \
	slipgate/sg_rune_door_scope.o \
	slipgate/sg_rune_door_scope_game.o \
	slipgate/sg_rune_door_frontier.o \
	sg_drop_live.o \
	sg_swim_live.o \
	sg_hook_live.o \
	slipgate/sg_rocketjump_live.o \
	slipgate/sg_rocketjump_cadence.o \
	slipgate/sg_rocketjump_game.o \
	slipgate/sg_push_live.o \
	slipgate/sg_push_game.o \
	slipgate/sg_train_gate_live.o \
	slipgate/sg_train_gate_game.o \
	slipgate/sg_shoot_door_live.o \
	slipgate/sg_shoot_door_game.o \
	sg_oracle.o \
	slipgate/sg_oracle_rotator.o \
	sg_rune.o \
	sg_arach.o \
	slipgate/sg_localization.o \
	slipgate/sg_pickup_target.o \
	sg_fields.o \
	sg_caco.o sg_combat.o slipgate/sg_combat_land_lead.o \
	sg_cvars.o \
	sg_hooks.o \
	sg_util.o \
	sg_client.o \
	slipgate/sg_client_ownership.o \
	slipgate/sg_pov_identity.o \
	slipgate/sg_human_speed.o \
	slipgate/sg_human_trace.o \
	slipgate/sg_door_approach.o \
	slipgate/sg_defense_shift.o \
	slipgate/sg_defense_supply.o \
	slipgate/sg_strike.o \
	slipgate/sg_strike_adapter.o \
	slipgate/sg_hook_diagnostics.o \
	sg_clock.o \
	sg_danger.o \
	sg_danger_lease.o \
	sg_danger_policy.o \
	sg_weights.o \
	sg_tilt.o \
	sg_lead.o \
	sg_move.o slipgate/sg_feeler_probe.o \
	sg_price.o \
	sg_descend.o slipgate/sg_traversal_transition.o \
	sg_goal.o \
	slipgate/sg_belief.o \
	slipgate/sg_destination.o \
	slipgate/sg_rune_dynamics_model.o \
	slipgate/sg_rune_dynamics_geometry.o \
	slipgate/sg_rune_field_contract.o \
	slipgate/sg_field_attractor.o \
	slipgate/sg_bsp_world.o \
	slipgate/sg_host_collision.o \
	slipgate/sg_bsp_entity_semantics.o \
	slipgate/sg_bsp_entity_semantics_audit_expected.o \
	slipgate/sg_bsp_entity_semantics_publication.o \
	slipgate/sg_rune_model.o \
	slipgate/sg_strategy.o \
	slipgate/sg_strategy_caller.o \
	slipgate/sg_strategy_runtime_bridge.o \
	slipgate/sg_host_pmove.o \
	slipgate/sg_host_engine_pmove.o \
	slipgate/sg_host_engine_runtime.o \
	slipgate/sg_host_engine_parity.o \
	slipgate/sg_host_hook_law.o \
	slipgate/sg_host_mechanism_law.o \
	slipgate/sg_host_law_owner.o \
	slipgate/sg_host_law_publication.o \
	slipgate/sg_weapon_effect_profile.o \
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

POVLOCK_TEST_BIN := povlock_test.make
POVLOCK_TEST_OBJS := .povlock_test.make.o .povlock_under_test.make.o \
	.povlock_endframe_under_test.make.o
POVLOCK_TEST_DEPS := $(POVLOCK_TEST_OBJS:.o=.d)
POV_SESSION_TEST_BIN := pov_session_production_test.make
POV_SESSION_TEST_OBJS := .pov_session_production_test.make.o \
	.pov_session_chase_under_test.make.o \
	.pov_session_client_under_test.make.o \
	.pov_session_identity_under_test.make.o
POV_SESSION_TEST_DEPS := $(POV_SESSION_TEST_OBJS:.o=.d)
POVLOCK_DISPATCH_TEST := tests/test_povlock_dispatch.py
POV_SUPERVISOR_BIN := tools/pov-supervisor
POV_SUPERVISOR_TEST_BIN := pov_supervisor_unit.make
POV_SUPERVISOR_TEST := tests/test_pov_supervisor.py
POV_ITERATE_SELECTION_TEST := tests/test_iterate2_pov_selection.py
RUNE_PAIR_PREFLIGHT_DEPS := tools/rune_pair_preflight.py tools/runeio.py \
	tools/rune_contracts_generated.py
POV_SUPERVISOR_ALL_ARTIFACTS := tools/pov-supervisor pov_supervisor_unit.gnu \
	pov_supervisor_unit.make

.PHONY: all default host-test action-test compound-test mover-lease-test \
	water-forest-test \
	povlock-test pov-session-production-test pov-supervisor-test \
	button-live-test mechanism-timeline-test relay-wall-transaction-test \
	relay-wall-objective-test \
	delayed-use-ticket-test relay-wall-live-test \
	timed-vault-transaction-test timed-vault-game-test \
	timed-vault-runtime-test timed-vault-egress-test train-station-plan-test \
	train-station-candidate-test train-station-candidate-game-test \
	train-station-board-path-test \
	train-station-transaction-test train-station-game-test \
	button-game-test \
	compound-guard-test compound-guard-game-test declared-door-guard-test \
	compound-world-test \
	compound-gen-test compound-publication-test \
	identity-test rune-codec-test rune-artifact-loader-test \
	rune-artifact-writer-test rune-mechanism-plan-test \
	rune-mechanism-catalog-test rune-mechanism-execution-test rune-binding-test \
	rune-accept-tool \
	rune-naming-test rune-artifact-test rune-corpus-controller-test \
	rune-generator-config-test \
	rune-v2-contract-test rune-v2-exact-snapshot-test \
	rune-v2-independent-reader-test rune-v2-belief-test \
	rune-v2-perception-evidence-test rune-v2-configuration-space-test \
	ground-capability-publication-test \
	weapon-effect-profile-test hook-visibility-catalog-test \
	static-affordance-catalog-publication-test \
	bsp-entity-semantics-publication-test \
	host-law-publication-test \
	project-completion-plan-test \
	fleet-runner-test route-only-match-test server-bundle-test \
	runegen-test botkin-test sheet-cli-test \
	deslop-test \
	sidecar-wire-test sidecar-loader-test sidecar-store-test \
	danger-lease-test danger-policy-test danger-test fields-candidate-test \
	spectator-sound-test human-speed-test defense-shift-test \
	door-approach-test \
	item-commitment-test hook-diagnostics-test \
	run-handoff-test \
	rune-install-test rune-proof-test rune-objective-diagnostics-test \
	rune-late-path-test rune-topology-test rune-reverse-boundary-test \
	human-hook-ownership-test \
	replay-test chain-hook-replay-test hook-discipline-test \
	drop-live-test swim-live-test compound-swim-live-test \
	push-game-integration-test train-gate-game-integration-test \
	shoot-door-game-integration-test \
	compound-swim-game-test rotator-sweep-test \
	compound-drop-live-test compound-drop-game-test \
	compound-drop-transition-test compound-hook-live-test \
	compound-hook-game-test compound-hook-game-events-test rotator-sweep-test \
	mover-subject-sweep-test entfile-test maplist-rotation-test \
	compound-swim-oracle-test compound-hook-oracle-test rune-door-scope-test \
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

# These production objects live below slipgate/, while makedepend emits their
# targets without that directory prefix.  Keep the real object paths tied to
# the generated wire contract so an incremental build cannot mix contracts.
slipgate/sg_rune_artifact_writer.o slipgate/sg_rune_codec.o \
	slipgate/sg_rune_authority_game.o slipgate/sg_rune_binding.o \
	slipgate/sg_rune_file.o slipgate/sg_rune_late_path.o \
	slipgate/sg_rune_mechanism_plan.o slipgate/sg_compound_gen_game.o: \
	slipgate/sg_action_contract.generated.h

slipgate/sg_compound_world.o: slipgate/sg_compound_world.c \
		slipgate/sg_compound_world.h slipgate/sg_util.h g_local.h
slipgate/sg_mover_lease.o: slipgate/sg_mover_lease.c \
		slipgate/sg_mover_lease.h
slipgate/sg_button_live.o: slipgate/sg_button_live.c \
		slipgate/sg_button_live.h slipgate/sg_mover_lease.h
slipgate/sg_bsp_world.o: slipgate/sg_bsp_world.c slipgate/sg_bsp_world.h
slipgate/sg_host_collision.o: slipgate/sg_host_collision.c \
		slipgate/sg_host_collision.h slipgate/sg_bsp_world.h \
		slipgate/sg_rune_model.h
slipgate/sg_bsp_entity_semantics.o: slipgate/sg_bsp_entity_semantics.c \
		slipgate/sg_bsp_entity_semantics.h \
		slipgate/sg_bsp_entity_semantics_storage_internal.h
slipgate/sg_bsp_entity_semantics_audit_expected.o: \
		slipgate/sg_bsp_entity_semantics_audit_expected.c \
		slipgate/sg_bsp_entity_semantics_audit_internal.h
slipgate/sg_bsp_entity_semantics_publication.o: \
		slipgate/sg_bsp_entity_semantics_publication.c \
		slipgate/sg_bsp_entity_semantics_publication.h \
		slipgate/sg_bsp_entity_semantics_audit_internal.h \
		slipgate/sg_bsp_entity_semantics_storage_internal.h
slipgate/sg_compound_guard.o: slipgate/sg_compound_guard.c \
		slipgate/sg_compound_guard.h slipgate/sg_mover_lease.h
slipgate/sg_compound_guard_game.o: slipgate/sg_compound_guard_game.c \
		slipgate/sg_compound_guard_game.h slipgate/sg_compound_guard.h \
		slipgate/sg_mover_lease.h slipgate/sg_bot.h slipgate/sg_local.h \
		g_local.h
slipgate/sg_compound_gen_game.o: slipgate/sg_compound_gen_game.c \
		slipgate/sg_compound_gen_game.h slipgate/sg_compound_gen.h \
		slipgate/sg_compound_world.h slipgate/sg_local.h g_local.h
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
		slipgate/sg_rune_codec.h slipgate/sg_rune.h \
		slipgate/sg_rune_v2_content_identity.h \
		slipgate/sg_rune_v2_exact_snapshot.h q_shared.h
slipgate/sg_rune_v2_content_identity.o: \
		slipgate/sg_rune_v2_content_identity.c \
		slipgate/sg_rune_v2_content_identity.h slipgate/sg_rune_v2_wire.h
slipgate/sg_rune_v2_exact_snapshot.o: \
		slipgate/sg_rune_v2_exact_snapshot.c \
		slipgate/sg_rune_v2_exact_snapshot.h \
		slipgate/sg_rune_v2_exact_snapshot_private.h \
		slipgate/sg_rune_v2_content_identity.h \
		slipgate/sg_rune_v2_artifact_publication_internal.h
slipgate/sg_rune_stream.o: slipgate/sg_rune_stream.c \
		slipgate/sg_rune_stream.h slipgate/sg_rune_artifact_writer.h \
		slipgate/sg_rune_codec.h slipgate/sg_rune.h q_shared.h
slipgate/sg_rune_mechanism_plan.o: slipgate/sg_rune_mechanism_plan.c \
		slipgate/sg_rune_mechanism_plan.h \
		slipgate/sg_rune_mechanism_catalog.h slipgate/sg_rune.h \
		slipgate/sg_crc32.h q_shared.h
slipgate/sg_train_station_plan.o: slipgate/sg_train_station_plan.c \
		slipgate/sg_train_station_plan.h \
		slipgate/sg_rune_mechanism_catalog.h slipgate/sg_rune.h q_shared.h
slipgate/sg_train_station_candidate.o: \
		slipgate/sg_train_station_candidate.c \
		slipgate/sg_train_station_candidate.h \
		slipgate/sg_train_station_plan.h \
		slipgate/sg_rune_mechanism_plan.h
slipgate/sg_train_station_candidate_game.o: \
		slipgate/sg_train_station_candidate_game.c \
		slipgate/sg_train_station_candidate_game.h \
		slipgate/sg_train_station_candidate.h slipgate/sg_local.h g_local.h
slipgate/sg_train_station_board_path.o: \
		slipgate/sg_train_station_board_path.c \
		slipgate/sg_train_station_board_path.h q_shared.h
slipgate/sg_train_station_transaction.o: \
		slipgate/sg_train_station_transaction.c \
		slipgate/sg_train_station_transaction.h
slipgate/sg_train_station_game.o: slipgate/sg_train_station_game.c \
		slipgate/sg_train_station_game.h \
		slipgate/sg_train_station_transaction.h \
		slipgate/sg_rune_binding.h slipgate/sg_rune_mechanism_catalog.h \
		slipgate/sg_train_station_plan.h slipgate/sg_bot.h g_local.h
slipgate/sg_compound_gen.o: slipgate/sg_compound_gen.c \
		slipgate/sg_compound_gen.h slipgate/sg_rune.h q_shared.h
slipgate/sg_compound_action_gen.o: slipgate/sg_compound_action_gen.c \
		slipgate/sg_compound_action_gen.h slipgate/sg_compound.h \
		slipgate/sg_action.h slipgate/sg_rune.h q_shared.h
slipgate/sg_compound_publication.o: slipgate/sg_compound_publication.c \
		slipgate/sg_compound_publication.h \
		slipgate/sg_compound_publication_internal.h \
		slipgate/sg_compound_world.h \
		slipgate/sg_local.h slipgate/sg_rune.h g_local.h
slipgate/sg_compound_publication_build.o: \
		slipgate/sg_compound_publication_build.c \
		slipgate/sg_compound_publication.h \
		slipgate/sg_compound_publication_internal.h \
		slipgate/sg_compound_world.h slipgate/sg_local.h slipgate/sg_util.h \
		slipgate/sg_rune.h g_local.h
slipgate/sg_compound_action_publication.o: \
		slipgate/sg_compound_action_publication.c \
		slipgate/sg_compound_action_publication.h \
		slipgate/sg_compound_publication.h slipgate/sg_compound.h \
		slipgate/sg_replay.h slipgate/sg_rune.h q_shared.h
slipgate/sg_host_pmove.o: slipgate/sg_host_pmove.c \
		slipgate/sg_host_pmove.h slipgate/sg_host_collision.h \
		slipgate/sg_rune_model.h q_shared.h
slipgate/sg_host_engine_pmove.o: slipgate/sg_host_engine_pmove.c \
		slipgate/sg_host_engine_pmove.h slipgate/sg_host_pmove.h \
		game.h q_shared.h
slipgate/sg_host_engine_runtime.o: slipgate/sg_host_engine_runtime.c \
		slipgate/sg_host_engine_runtime.h \
		slipgate/sg_host_engine_runtime_private.h \
		slipgate/sg_host_pmove.h slipgate/sg_host_collision.h \
		slipgate/sg_bsp_world.h slipgate/sg_identity.h \
		slipgate/sg_destination.h game.h q_shared.h
slipgate/sg_host_engine_parity.o: slipgate/sg_host_engine_parity.c \
		slipgate/sg_host_engine_parity.h slipgate/sg_host_engine_pmove.h \
		game.h q_shared.h
slipgate/sg_host_hook_law.o: slipgate/sg_host_hook_law.c \
		slipgate/sg_host_hook_law.h slipgate/sg_host_pmove.h \
		slipgate/sg_weapon_host_constants.h q_shared.h
slipgate/sg_host_mechanism_law.o: slipgate/sg_host_mechanism_law.c \
		slipgate/sg_host_mechanism_law.h
slipgate/sg_host_law_owner.o: slipgate/sg_host_law_owner.c \
		slipgate/sg_host_law_owner.h slipgate/sg_host_law_publication.h \
		slipgate/sg_host_law_owner_internal.h \
		slipgate/sg_host_law_publication_private.h \
		slipgate/sg_host_engine_runtime_private.h \
		slipgate/sg_host_collision.h slipgate/sg_bsp_world.h \
		slipgate/sg_identity.h game.h q_shared.h
slipgate/sg_host_law_publication.o: slipgate/sg_host_law_publication.c \
		slipgate/sg_host_law_publication.h \
		slipgate/sg_host_law_publication_private.h \
		slipgate/sg_host_engine_runtime_private.h \
		slipgate/sg_host_collision.h \
		slipgate/sg_host_pmove.h slipgate/sg_host_engine_pmove.h \
		slipgate/sg_host_engine_parity.h slipgate/sg_host_hook_law.h \
		slipgate/sg_host_mechanism_law.h slipgate/sg_weapon_host_constants.h \
		game.h q_shared.h
slipgate/sg_rune_door_scope.o: slipgate/sg_rune_door_scope.c \
		slipgate/sg_rune_door_scope.h
-include $(OBJS:.o=.d)
-include $(POVLOCK_TEST_DEPS)
-include $(POV_SESSION_TEST_DEPS)
-include $(HOST_TEST_DEPS)
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
-include $(RUNE_ACCEPT_DEPS)
-include $(SIDECAR_WIRE_TEST_DEPS)
-include $(SIDECAR_LOADER_TEST_DEPS)
-include $(SIDECAR_STORE_TEST_DEPS)
-include $(DANGER_LEASE_TEST_DEPS)
-include $(DANGER_POLICY_TEST_DEPS)
-include $(DANGER_TEST_DEPS)
-include $(FIELDS_CANDIDATE_TEST_DEPS)
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

%.o: %.c
	$(E) [CC] $@
	$(Q)$(CC) -c $(CPPFLAGS) $(CFLAGS) -o $@ $<

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

$(RUNE_CODEC_TEST_BIN): $(RUNE_CODEC_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_CODEC_TEST_OBJS) $(LIBS)

$(RUNE_ARTIFACT_LOADER_TEST_BIN): $(RUNE_ARTIFACT_LOADER_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_ARTIFACT_LOADER_TEST_OBJS) $(LIBS)

$(RUNE_ARTIFACT_WRITER_TEST_BIN): $(RUNE_ARTIFACT_WRITER_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_ARTIFACT_WRITER_TEST_OBJS) $(LIBS)

$(RUNE_MECHANISM_PLAN_TEST_BIN): $(RUNE_MECHANISM_PLAN_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_MECHANISM_PLAN_TEST_OBJS) $(LIBS)

$(RUNE_MECHANISM_CATALOG_TEST_BIN): $(RUNE_MECHANISM_CATALOG_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_MECHANISM_CATALOG_TEST_OBJS) $(LIBS)

$(RUNE_MECHANISM_EXECUTION_TEST_BIN): $(RUNE_MECHANISM_EXECUTION_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -Wl,--wrap=G_UseTargets -o $@ \
		$(RUNE_MECHANISM_EXECUTION_TEST_OBJS) $(LIBS)

$(RUNE_BINDING_TEST_BIN): $(RUNE_BINDING_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_BINDING_TEST_OBJS) $(LIBS)

$(RUNE_ACCEPT_BIN): $(RUNE_ACCEPT_OBJS)
	$(E) [TOOL-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_ACCEPT_OBJS) $(LIBS)

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

$(DANGER_TEST_BIN): $(DANGER_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(DANGER_TEST_OBJS) $(LIBS)

$(FIELDS_CANDIDATE_TEST_BIN): $(FIELDS_CANDIDATE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(FIELDS_CANDIDATE_TEST_OBJS) $(LIBS)

$(HUMAN_SPEED_TEST_BIN): $(HUMAN_SPEED_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(HUMAN_SPEED_TEST_OBJS) $(LIBS)

$(HUMAN_TRACE_HOOK_TEST_BIN): $(HUMAN_TRACE_HOOK_TEST_SOURCE) \
		slipgate/sg_human_trace.c \
		slipgate/sg_rune_v2_content_identity.c \
		slipgate/sg_rune_v2_content_identity.h \
		slipgate/sg_rune_v2_wire.h
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -Wpedantic -I. \
		-DSG_HUMAN_TRACE_WRAP_FWRITE \
		-o $@ $(filter %.c,$^) $(LIBS) -Wl,--wrap=fwrite

$(DOOR_APPROACH_TEST_BIN): $(DOOR_APPROACH_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(DOOR_APPROACH_TEST_OBJS) $(LIBS)

$(DEFENSE_SHIFT_TEST_BIN): $(DEFENSE_SHIFT_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(DEFENSE_SHIFT_TEST_OBJS) $(LIBS)

$(DEFENSE_SUPPLY_TEST_BIN): $(DEFENSE_SUPPLY_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(DEFENSE_SUPPLY_TEST_OBJS) $(LIBS)

$(STRIKE_ADAPTER_TEST_BIN): $(STRIKE_ADAPTER_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(STRIKE_ADAPTER_TEST_OBJS) $(LIBS)

$(ITEM_COMMITMENT_TEST_BIN): $(ITEM_COMMITMENT_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(ITEM_COMMITMENT_TEST_OBJS) $(LIBS)

$(HOOK_DIAGNOSTICS_TEST_BIN): $(HOOK_DIAGNOSTICS_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(HOOK_DIAGNOSTICS_TEST_OBJS) $(LIBS)

$(RUN_HANDOFF_TEST_BIN): $(RUN_HANDOFF_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(RUN_HANDOFF_TEST_OBJS) $(LIBS)

$(ROCKETJUMP_LIVE_TEST_BIN): $(ROCKETJUMP_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(ROCKETJUMP_LIVE_TEST_OBJS) $(LIBS)

$(PUSH_LIVE_TEST_BIN): $(PUSH_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(PUSH_LIVE_TEST_OBJS) $(LIBS)

$(ROCKETJUMP_CADENCE_TEST_BIN): $(ROCKETJUMP_CADENCE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(ROCKETJUMP_CADENCE_TEST_OBJS) $(LIBS)

$(ROCKETJUMP_GAME_TEST_BIN): $(ROCKETJUMP_GAME_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(ROCKETJUMP_GAME_TEST_OBJS) $(LIBS)

$(COMPOUND_TEST_BIN): $(COMPOUND_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_TEST_OBJS) $(LIBS)

$(MOVER_LEASE_TEST_BIN): $(MOVER_LEASE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(MOVER_LEASE_TEST_OBJS) $(LIBS)

$(WATER_FOREST_TEST_BIN): $(WATER_FOREST_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(WATER_FOREST_TEST_OBJS) $(LIBS)

$(BUTTON_LIVE_TEST_BIN): $(BUTTON_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(BUTTON_LIVE_TEST_OBJS) $(LIBS)

$(TRAIN_GATE_LIVE_TEST_BIN): $(TRAIN_GATE_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(TRAIN_GATE_LIVE_TEST_OBJS) $(LIBS)

$(MECHANISM_TIMELINE_TEST_BIN): $(MECHANISM_TIMELINE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(MECHANISM_TIMELINE_TEST_OBJS) $(LIBS)

$(RELAY_WALL_TRANSACTION_TEST_BIN): $(RELAY_WALL_TRANSACTION_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RELAY_WALL_TRANSACTION_TEST_OBJS) $(LIBS)

$(RELAY_WALL_OBJECTIVE_TEST_BIN): $(RELAY_WALL_OBJECTIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RELAY_WALL_OBJECTIVE_TEST_OBJS) $(LIBS)

$(SHOOT_DOOR_LIVE_TEST_BIN): $(SHOOT_DOOR_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(SHOOT_DOOR_LIVE_TEST_OBJS) $(LIBS)

$(BUTTON_GAME_TEST_BIN): $(BUTTON_GAME_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(BUTTON_GAME_TEST_OBJS) $(LIBS)

$(COMPOUND_GUARD_TEST_BIN): $(COMPOUND_GUARD_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_GUARD_TEST_OBJS) $(LIBS)

$(COMPOUND_GUARD_GAME_TEST_BIN): $(COMPOUND_GUARD_GAME_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_GUARD_GAME_TEST_OBJS) $(LIBS)

$(DECLARED_DOOR_GUARD_TEST_BIN): $(DECLARED_DOOR_GUARD_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(DECLARED_DOOR_GUARD_TEST_OBJS) $(LIBS)

$(COMPOUND_WORLD_TEST_BIN): $(COMPOUND_WORLD_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(COMPOUND_WORLD_TEST_OBJS) $(LIBS)

$(COMPOUND_GEN_TEST_BIN): $(COMPOUND_GEN_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_GEN_TEST_OBJS) $(LIBS)

$(COMPOUND_GEN_GAME_TEST_BIN): $(COMPOUND_GEN_GAME_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_GEN_GAME_TEST_OBJS) $(LIBS)

$(COMPOUND_PUBLICATION_TEST_BIN): $(COMPOUND_PUBLICATION_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_PUBLICATION_TEST_OBJS) $(LIBS)

$(RUNE_INSTALL_TEST_BIN): $(RUNE_INSTALL_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_INSTALL_TEST_OBJS) $(LIBS)
$(RUNE_PROOF_TEST_BIN): $(RUNE_PROOF_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(RUNE_PROOF_TEST_OBJS) $(LIBS)

$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN): \
		$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_OBJS) slipgate/sg_rune_late_path.c \
		slipgate/sg_rune_reverse_boundary.c slipgate/sg_rune_topology.c \
		slipgate/sg_action.c tests/sg_rune_objective_diagnostics_link_stubs.c
	$(E) [TEST-LD] $@
	$(Q)$(CC) $(CFLAGS) -std=c11 -I. -Wl,--gc-sections -o $@ \
		$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_OBJS) \
		slipgate/sg_rune_late_path.c slipgate/sg_rune_reverse_boundary.c \
		slipgate/sg_rune_topology.c slipgate/sg_action.c \
		tests/sg_rune_objective_diagnostics_link_stubs.c $(LIBS)
$(REPLAY_TEST_BIN): $(REPLAY_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(REPLAY_TEST_OBJS) $(LIBS)

$(CHAIN_HOOK_REPLAY_TEST_BIN): $(CHAIN_HOOK_REPLAY_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(CHAIN_HOOK_REPLAY_TEST_OBJS) $(LIBS)

$(DROP_LIVE_TEST_BIN): $(DROP_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(DROP_LIVE_TEST_OBJS) $(LIBS)

$(SWIM_LIVE_TEST_BIN): $(SWIM_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(SWIM_LIVE_TEST_OBJS) $(LIBS)

$(COMPOUND_SWIM_LIVE_TEST_BIN): $(COMPOUND_SWIM_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_SWIM_LIVE_TEST_OBJS) $(LIBS)

$(COMPOUND_SWIM_GAME_TEST_BIN): $(COMPOUND_SWIM_GAME_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(COMPOUND_SWIM_GAME_TEST_OBJS) $(LIBS)
$(COMPOUND_DROP_LIVE_TEST_BIN): $(COMPOUND_DROP_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(COMPOUND_DROP_LIVE_TEST_OBJS) $(LIBS)

$(COMPOUND_DROP_GAME_TEST_BIN): $(COMPOUND_DROP_GAME_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(COMPOUND_DROP_GAME_TEST_OBJS) $(LIBS)

$(COMPOUND_DROP_FANOUT_TEST_BIN): $(COMPOUND_DROP_FANOUT_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_DROP_FANOUT_TEST_OBJS) $(LIBS)

$(COMPOUND_DROP_TRANSITION_TEST_BIN): $(COMPOUND_DROP_TRANSITION_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_DROP_TRANSITION_TEST_OBJS) $(LIBS)

$(COMPOUND_HOOK_LIVE_TEST_BIN): $(COMPOUND_HOOK_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_HOOK_LIVE_TEST_OBJS) $(LIBS)

$(COMPOUND_HOOK_GAME_TEST_BIN): $(COMPOUND_HOOK_GAME_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_HOOK_GAME_TEST_OBJS) $(LIBS)

$(COMPOUND_HOOK_GAME_EVENTS_TEST_BIN): \
		$(COMPOUND_HOOK_GAME_EVENTS_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_HOOK_GAME_EVENTS_TEST_OBJS) $(LIBS)

$(HOOK_LIVE_TEST_BIN): $(HOOK_LIVE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(HOOK_LIVE_TEST_OBJS) $(LIBS)

$(HOOK_DISCIPLINE_TEST_BIN): $(HOOK_DISCIPLINE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -o $@ $(HOOK_DISCIPLINE_TEST_OBJS) $(LIBS)

$(ROTATOR_SWEEP_TEST_BIN): $(ROTATOR_SWEEP_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(ROTATOR_SWEEP_TEST_OBJS) $(LIBS)

$(MOVER_SUBJECT_SWEEP_TEST_BIN): $(MOVER_SUBJECT_SWEEP_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ \
		$(MOVER_SUBJECT_SWEEP_TEST_OBJS) $(LIBS)

$(COMPOUND_SWIM_ORACLE_TEST_BIN): $(COMPOUND_SWIM_ORACLE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_SWIM_ORACLE_TEST_OBJS) $(LIBS)

$(COMPOUND_HOOK_ORACLE_TEST_BIN): $(COMPOUND_HOOK_ORACLE_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ \
		$(COMPOUND_HOOK_ORACLE_TEST_OBJS) $(LIBS)

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

.sg_button_live_test.make.o: tests/sg_button_live_test.c \
		slipgate/sg_button_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_water_forest_test.make.o: tests/sg_water_forest_test.c \
		slipgate/sg_water_forest.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_water_forest_under_test.make.o: slipgate/sg_water_forest.c \
		slipgate/sg_water_forest.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_button_live_under_test.make.o: slipgate/sg_button_live.c \
		slipgate/sg_button_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_train_gate_live_test.make.o: tests/sg_train_gate_live_test.c \
		slipgate/sg_train_gate_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_train_gate_live_under_test.make.o: slipgate/sg_train_gate_live.c \
		slipgate/sg_train_gate_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mechanism_timeline_test.make.o: tests/sg_mechanism_timeline_test.c \
		slipgate/sg_mechanism_timeline.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mechanism_timeline_under_test.make.o: slipgate/sg_mechanism_timeline.c \
		slipgate/sg_mechanism_timeline.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_relay_wall_transaction_test.make.o: \
		tests/sg_relay_wall_transaction_test.c \
		slipgate/sg_relay_wall_transaction.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_relay_wall_transaction_under_test.make.o: \
		slipgate/sg_relay_wall_transaction.c \
		slipgate/sg_relay_wall_transaction.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_relay_wall_transaction_timeline_under_test.make.o: \
		slipgate/sg_mechanism_timeline.c slipgate/sg_mechanism_timeline.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_relay_wall_objective_test.make.o: tests/sg_relay_wall_objective_test.c \
		slipgate/sg_relay_wall_objective.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_relay_wall_objective_under_test.make.o: \
		slipgate/sg_relay_wall_objective.c \
		slipgate/sg_relay_wall_objective.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_shoot_door_live_test.make.o: tests/sg_shoot_door_live_test.c \
		slipgate/sg_shoot_door_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_shoot_door_live_under_test.make.o: slipgate/sg_shoot_door_live.c \
		slipgate/sg_shoot_door_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rocketjump_live_test.make.o: tests/sg_rocketjump_live_test.c \
		slipgate/sg_rocketjump_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rocketjump_live_under_test.make.o: slipgate/sg_rocketjump_live.c \
		slipgate/sg_rocketjump_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_push_live_test.make.o: tests/sg_push_live_test.c \
		slipgate/sg_push_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_push_live_under_test.make.o: slipgate/sg_push_live.c \
		slipgate/sg_push_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_push_falling_under_test.make.o: p_view.c g_local.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -Wno-unused-parameter \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rocketjump_cadence_test.make.o: tests/sg_rocketjump_cadence_test.c \
		slipgate/sg_rocketjump_cadence.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rocketjump_cadence_under_test.make.o: \
		slipgate/sg_rocketjump_cadence.c \
		slipgate/sg_rocketjump_cadence.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rocketjump_game_test.make.o: tests/sg_rocketjump_game_test.c \
		slipgate/sg_rocketjump_game.h slipgate/sg_rocketjump_live.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_rocketjump_game_under_test.make.o: slipgate/sg_rocketjump_game.c \
		slipgate/sg_rocketjump_game.h slipgate/sg_rocketjump_live.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_rocketjump_game_live_under_test.make.o: \
		slipgate/sg_rocketjump_live.c slipgate/sg_rocketjump_live.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rocketjump_game_q_shared_under_test.make.o: q_shared.c \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_button_game_test.make.o: tests/sg_button_game_test.c \
		slipgate/sg_button_live.h slipgate/sg_move.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_button_game_live_under_test.make.o: slipgate/sg_button_live.c \
		slipgate/sg_button_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_button_game_move_under_test.make.o: slipgate/sg_move.c \
		slipgate/sg_button_live.h slipgate/sg_move.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_button_game_func_under_test.make.o: g_func.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-unused-parameter -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_button_game_q_shared_under_test.make.o: q_shared.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

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

.sg_compound_guard_test.make.o: tests/sg_compound_guard_test.c \
		slipgate/sg_compound_guard.h slipgate/sg_mover_lease.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_guard_under_test.make.o: slipgate/sg_compound_guard.c \
		slipgate/sg_compound_guard.h slipgate/sg_mover_lease.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_guard_mover_lease_under_test.make.o: \
		slipgate/sg_mover_lease.c slipgate/sg_mover_lease.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_guard_game_test.make.o: \
		tests/sg_compound_guard_game_test.c \
		slipgate/sg_compound_guard_game.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -DSG_COMPOUND_GUARD_GAME_TEST -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_guard_game_under_test.make.o: \
		slipgate/sg_compound_guard_game.c \
		slipgate/sg_compound_guard_game.h slipgate/sg_compound_guard.h \
		slipgate/sg_mover_lease.h slipgate/sg_bot.h slipgate/sg_local.h \
		g_local.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -DSG_COMPOUND_GUARD_GAME_TEST -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_declared_door_guard_test.make.o: \
		tests/sg_declared_door_guard_test.c \
		slipgate/sg_declared_door_guard.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_declared_door_guard_under_test.make.o: \
		slipgate/sg_declared_door_guard.c \
		slipgate/sg_declared_door_guard.h slipgate/sg_compound_guard.h \
		slipgate/sg_mover_lease.h slipgate/sg_bot.h slipgate/sg_local.h \
		g_local.h $(REVISION_HEADER)
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

.sg_compound_gen_game_test.make.o: tests/sg_compound_gen_game_test.c \
		slipgate/sg_compound_gen_game.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_gen_game_under_test.make.o: slipgate/sg_compound_gen_game.c \
		slipgate/sg_compound_gen_game.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_publication_test.make.o: \
		tests/sg_compound_publication_test.c \
		tests/sg_compound_publication_fixture.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

$(COMPOUND_PUBLICATION_CASE_MAKE_OBJS): .sg_%.make.o: tests/sg_%.c \
		tests/sg_compound_publication_fixture.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -Itests -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_publication_under_test.make.o: \
		slipgate/sg_compound_publication.c \
		slipgate/sg_compound_publication.h \
		slipgate/sg_compound_publication_internal.h slipgate/sg_util.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_publication_build_under_test.make.o: \
		slipgate/sg_compound_publication_build.c \
		slipgate/sg_compound_publication.h \
		slipgate/sg_compound_publication_internal.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_action_publication_for_publication_test.make.o: \
		slipgate/sg_compound_action_publication.c \
		slipgate/sg_compound_action_publication.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_identity_test.make.o: tests/sg_identity_test.c slipgate/sg_chat.h \
		slipgate/sg_chat_random.h slipgate/sg_ear_random.h \
		slipgate/sg_route_jitter.h slipgate/sg_callout_policy.h \
		$(REVISION_HEADER)
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

.sg_rune_action_under_test.make.o: slipgate/sg_action.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_crc_under_test.make.o: slipgate/sg_crc32.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_codec_test.make.o: tests/sg_rune_codec_test.c \
		slipgate/sg_rune_codec.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_codec_under_test.make.o: slipgate/sg_rune_codec.c \
		slipgate/sg_rune_codec.h \
		slipgate/sg_action_contract.generated.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_artifact_loader_test.make.o: tests/sg_rune_artifact_loader_test.c \
		slipgate/sg_rune_artifact_loader.h slipgate/sg_rune_codec.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_artifact_loader_under_test.make.o: slipgate/sg_rune_artifact_loader.c \
		slipgate/sg_rune_artifact_loader.h slipgate/sg_rune_codec.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_artifact_writer_test.make.o: tests/sg_rune_artifact_writer_test.c \
		slipgate/sg_rune_artifact_writer.h slipgate/sg_rune_codec.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_artifact_writer_under_test.make.o: slipgate/sg_rune_artifact_writer.c \
		slipgate/sg_rune_artifact_writer.h slipgate/sg_rune_codec.h \
		slipgate/sg_crc32.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_mechanism_plan_test.make.o: tests/sg_rune_mechanism_plan_test.c \
		slipgate/sg_rune_mechanism_plan.h slipgate/sg_rune_codec.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_mechanism_plan_under_test.make.o: \
		slipgate/sg_rune_mechanism_plan.c \
		slipgate/sg_rune_mechanism_plan.h \
		slipgate/sg_rune_mechanism_catalog.h slipgate/sg_rune.h \
		slipgate/sg_crc32.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_train_station_plan_under_test.make.o: \
		slipgate/sg_train_station_plan.c slipgate/sg_train_station_plan.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_mechanism_catalog_test.make.o: \
		tests/sg_rune_mechanism_catalog_test.c \
		slipgate/sg_rune_mechanism_catalog.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_mechanism_execution_test.make.o: \
		tests/sg_rune_mechanism_execution_test.c \
		slipgate/sg_rune_binding.h slipgate/sg_rune_mechanism_catalog.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_mechanism_execution_link_stubs.make.o: \
		tests/sg_rune_mechanism_execution_link_stubs.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_delayed_relay_dispatch_move_under_test.make.o: slipgate/sg_move.c \
		slipgate/sg_move.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -DSG_RUNE_MECHANISM_EXECUTION_TEST -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_delayed_relay_dispatch_util_under_test.make.o: slipgate/sg_util.c \
		slipgate/sg_util.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_delayed_relay_dispatch_view_under_test.make.o: p_view.c \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -Wno-unused-parameter \
		-ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_game_utils_under_test.make.o: g_utils.c \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_relay_wall_game_under_test.make.o: slipgate/sg_relay_wall_game.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_relay_wall_live_under_test.make.o: slipgate/sg_relay_wall_live.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_relay_wall_ticket_under_test.make.o: slipgate/sg_delayed_use_ticket.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_relay_wall_transaction_live_under_test.make.o: \
		slipgate/sg_relay_wall_transaction.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_relay_wall_timeline_live_under_test.make.o: slipgate/sg_mechanism_timeline.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_timed_vault_runtime_under_test.make.o: slipgate/sg_timed_vault_game_runtime.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_timed_vault_game_under_test.make.o: slipgate/sg_timed_vault_game.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_timed_vault_transaction_under_test.make.o: \
		slipgate/sg_timed_vault_transaction.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_delayed_relay_dispatch_trigger_under_test.make.o: g_trigger.c \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -Wno-unused-parameter \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_delayed_relay_dispatch_button_under_test.make.o: \
		slipgate/sg_button_live.c slipgate/sg_button_live.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_q_shared_under_test.make.o: q_shared.c \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_rune_mechanism_catalog_under_test.make.o: \
		slipgate/sg_rune_mechanism_catalog.c \
		slipgate/sg_rune_mechanism_catalog.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_binding_test.make.o: tests/sg_rune_binding_test.c \
		slipgate/sg_rune_binding.h slipgate/sg_rune.h \
		slipgate/sg_rune_mechanism_catalog.h slipgate/sg_crc32.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_binding_under_test.make.o: slipgate/sg_rune_binding.c \
		slipgate/sg_rune_binding.h slipgate/sg_rune.h \
		slipgate/sg_rune_mechanism_catalog.h slipgate/sg_crc32.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.runeaccept.make.o: tools/runeaccept.c slipgate/sg_rune_file.h \
		slipgate/sg_action_contract.generated.h $(REVISION_HEADER)
	$(E) [TOOL-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_file_under_test.make.o: \
		slipgate/sg_rune_file.c slipgate/sg_rune_file.h \
		slipgate/sg_rune_artifact_loader.h slipgate/sg_rune.h \
		slipgate/sg_rune_v2_content_identity.h \
		slipgate/sg_rune_v2_exact_snapshot.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_v2_content_identity_under_test.make.o: \
		slipgate/sg_rune_v2_content_identity.c \
		slipgate/sg_rune_v2_content_identity.h \
		slipgate/sg_rune_v2_wire.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_v2_exact_snapshot_under_test.make.o: \
		slipgate/sg_rune_v2_exact_snapshot.c \
		slipgate/sg_rune_v2_exact_snapshot.h \
		slipgate/sg_rune_v2_exact_snapshot_private.h \
		slipgate/sg_rune_v2_content_identity.h \
		slipgate/sg_rune_v2_artifact_publication_internal.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_wire_test.make.o: tests/sg_sidecar_wire_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_wire_under_test.make.o: slipgate/sg_sidecar_wire.c \
		slipgate/sg_sidecar_wire.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_loader_test.make.o: tests/sg_sidecar_loader_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_loader_under_test.make.o: slipgate/sg_sidecar_loader.c \
		slipgate/sg_sidecar_loader.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_store_test.make.o: tests/sg_sidecar_store_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_sidecar_store_under_test.make.o: slipgate/sg_sidecar_store.c \
		slipgate/sg_sidecar_store.h \
		$(REVISION_HEADER)
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

.sg_door_approach_test.make.o: tests/sg_door_approach_test.c \
		slipgate/sg_door_approach.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_door_approach_under_test.make.o: slipgate/sg_door_approach.c \
		slipgate/sg_door_approach.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_test.make.o: tests/sg_danger_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_danger_under_test.make.o: slipgate/sg_danger.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rune_runtime_under_test.make.o: slipgate/sg_rune_runtime.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
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

.sg_caco_projection_under_test.make.o: slipgate/sg_caco.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -DSG_CACO_TEST \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_caco_lifecycle_test.make.o: tests/sg_caco_lifecycle_test.c \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_goal_projection_under_test.make.o: slipgate/sg_goal.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -DSG_GOAL_TEST \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_human_speed_test.make.o: tests/sg_human_speed_test.c \
		slipgate/sg_human_speed.h tests/support/yq2_pmove.c \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_human_speed_under_test.make.o: slipgate/sg_human_speed.c \
		slipgate/sg_human_speed.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_human_speed_pmove_under_test.make.o: tests/support/yq2_pmove.c \
		q_shared.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -DDEDICATED_ONLY \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_human_speed_q_shared_under_test.make.o: q_shared.c q_shared.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_defense_shift_test.make.o: tests/sg_defense_shift_test.c \
		slipgate/sg_defense_shift.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_defense_shift_under_test.make.o: slipgate/sg_defense_shift.c \
		slipgate/sg_defense_shift.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_defense_supply_test.make.o: tests/sg_defense_supply_test.c \
		slipgate/sg_defense_supply.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_defense_supply_under_test.make.o: slipgate/sg_defense_supply.c \
		slipgate/sg_defense_supply.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_strike_adapter_test.make.o: tests/sg_strike_adapter_test.c \
		slipgate/sg_strike_adapter.h slipgate/sg_strike.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_strike_under_test.make.o: slipgate/sg_strike.c \
		slipgate/sg_strike.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_strike_adapter_under_test.make.o: slipgate/sg_strike_adapter.c \
		slipgate/sg_strike_adapter.h slipgate/sg_strike.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_item_commitment_test.make.o: tests/sg_item_commitment_test.c \
		slipgate/sg_lead.h slipgate/sg_rune_handoff_policy.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_item_commitment_under_test.make.o: slipgate/sg_lead.c \
		slipgate/sg_lead.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_diagnostics_test.make.o: tests/sg_hook_diagnostics_test.c \
		slipgate/sg_hook_diagnostics.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_diagnostics_under_test.make.o: slipgate/sg_hook_diagnostics.c \
		slipgate/sg_hook_diagnostics.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_run_handoff_test.make.o: tests/sg_run_handoff_test.c \
		slipgate/sg_descend.h tests/support/yq2_pmove.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_run_handoff_descend_under_test.make.o: slipgate/sg_descend.c \
		slipgate/sg_descend.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_run_handoff_pmove_under_test.make.o: tests/support/yq2_pmove.c \
		q_shared.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -DDEDICATED_ONLY \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_run_handoff_q_shared_under_test.make.o: q_shared.c q_shared.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP \
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

.sg_rune_stream_under_test.make.o: slipgate/sg_rune_stream.c \
		slipgate/sg_rune_stream.h slipgate/sg_rune_artifact_writer.h \
		slipgate/sg_rune_codec.h $(REVISION_HEADER)
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

.sg_rune_objective_diagnostics_test.make.o: \
		tests/sg_rune_objective_diagnostics_test.c slipgate/sg_rune.c \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

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

.sg_chain_hook_replay_test.make.o: \
		tests/sg_chain_hook_replay_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_chain_hook_replay_under_test.make.o: \
		slipgate/sg_chain_hook_replay.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_chain_hook_replay_replay_under_test.make.o: \
		slipgate/sg_replay.c $(REVISION_HEADER)
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

.sg_compound_swim_live_test.make.o: \
		tests/sg_compound_swim_live_test.c \
		slipgate/sg_compound_swim_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_live_under_test.make.o: \
		slipgate/sg_compound_swim_live.c \
		slipgate/sg_compound_swim_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_live_compound_under_test.make.o: \
		slipgate/sg_compound.c slipgate/sg_compound.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_live_action_under_test.make.o: \
		slipgate/sg_action.c slipgate/sg_action.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_live_replay_under_test.make.o: \
		slipgate/sg_replay.c slipgate/sg_replay.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_game_test.make.o: \
		tests/sg_compound_swim_game_test.c \
		slipgate/sg_compound_swim_game.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_game_under_test.make.o: \
		slipgate/sg_compound_swim_game.c \
		slipgate/sg_compound_swim_game.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<
.sg_compound_drop_live_test.make.o: tests/sg_compound_drop_live_test.c \
		slipgate/sg_compound_drop_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_live_under_test.make.o: slipgate/sg_compound_drop_live.c \
		slipgate/sg_compound_drop_live.h \
		slipgate/sg_compound_drop_live_internal.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_live_finish_under_test.make.o: \
		slipgate/sg_compound_drop_live_finish.c \
		slipgate/sg_compound_drop_live.h \
		slipgate/sg_compound_drop_live_internal.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_live_drop_under_test.make.o: slipgate/sg_drop_live.c \
		slipgate/sg_drop_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_live_compound_under_test.make.o: slipgate/sg_compound.c \
		slipgate/sg_compound.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_live_action_under_test.make.o: slipgate/sg_action.c \
		slipgate/sg_action.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_live_replay_under_test.make.o: slipgate/sg_replay.c \
		slipgate/sg_replay.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_game_test.make.o: tests/sg_compound_drop_game_test.c \
		slipgate/sg_compound_drop_game.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_fanout_game_test.make.o: \
		tests/sg_compound_drop_fanout_game_test.c \
		slipgate/sg_compound_drop_game.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_game_under_test.make.o: slipgate/sg_compound_drop_game.c \
		slipgate/sg_compound_drop_game.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_transition_test.make.o: \
		tests/sg_compound_drop_transition_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_drop_transition_under_test.make.o: \
		slipgate/sg_traversal_transition.c \
		slipgate/sg_traversal_transition.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_live_test.make.o: tests/sg_compound_hook_live_test.c \
		tests/sg_compound_hook_live_fixture.h \
		slipgate/sg_compound_hook_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_game_test.make.o: tests/sg_compound_hook_game_test.c \
		slipgate/sg_compound_hook_game.h \
		slipgate/sg_compound_hook_game_events.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_game_under_test.make.o: \
		slipgate/sg_compound_hook_game.c \
		slipgate/sg_compound_hook_game.h \
		slipgate/sg_compound_hook_game_events.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_game_lifecycle_under_test.make.o: \
		slipgate/sg_compound_hook_game_lifecycle.c \
		slipgate/sg_compound_hook_game.h slipgate/sg_bot.h \
		slipgate/sg_local.h g_local.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_game_events_test.make.o: \
		tests/sg_compound_hook_game_events_test.c \
		tests/sg_compound_hook_game_events_fixture.h \
		slipgate/sg_compound_hook_game_events.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) \
		-DSG_COMPOUND_HOOK_GAME_EVENTS_TEST -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_game_events_under_test.make.o: \
		slipgate/sg_compound_hook_game_events.c \
		tests/sg_compound_hook_game_events_fixture.h \
		slipgate/sg_compound_hook_game_events.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) \
		-DSG_COMPOUND_HOOK_GAME_EVENTS_TEST -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_live_fixture.make.o: \
		tests/sg_compound_hook_live_fixture.c \
		tests/sg_compound_hook_live_fixture.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_live_safety_test.make.o: \
		tests/sg_compound_hook_live_safety_test.c \
		tests/sg_compound_hook_live_fixture.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_live_under_test.make.o: \
		slipgate/sg_compound_hook_live.c \
		slipgate/sg_compound_hook_live.h \
		slipgate/sg_compound_hook_live_internal.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_live_finish_under_test.make.o: \
		slipgate/sg_compound_hook_live_finish.c \
		slipgate/sg_compound_hook_live_internal.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_live_compound_under_test.make.o: slipgate/sg_compound.c \
		slipgate/sg_compound.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_live_action_under_test.make.o: slipgate/sg_action.c \
		slipgate/sg_action.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_live_replay_under_test.make.o: slipgate/sg_replay.c \
		slipgate/sg_replay.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_live_hook_under_test.make.o: slipgate/sg_hook_live.c \
		slipgate/sg_hook_live.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_live_publication_under_test.make.o: \
		slipgate/sg_compound_action_publication.c \
		slipgate/sg_compound_action_publication.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

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

.sg_hook_discipline_test.make.o: tests/sg_hook_discipline_test.c \
		slipgate/sg_hook_discipline.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_hook_discipline_under_test.make.o: slipgate/sg_hook_discipline.c \
		slipgate/sg_hook_discipline.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rotator_sweep_test.make.o: tests/sg_rotator_sweep_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rotator_sweep_under_test.make.o: slipgate/sg_oracle_rotator.c \
		slipgate/sg_oracle_internal.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_rotator_sweep_q_shared_under_test.make.o: q_shared.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_test.make.o: \
		tests/sg_mover_subject_sweep_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_oracle_under_test.make.o: \
		slipgate/sg_oracle.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_util_under_test.make.o: \
		slipgate/sg_util.c slipgate/sg_util.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections \
		-DSG_ImmutableSupport=SG_MoverSubjectSweepRealImmutableSupport \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_view_under_test.make.o: p_view.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -Wno-unused-parameter \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_q_shared_under_test.make.o: \
		q_shared.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_mover_subject_sweep_pmove_under_test.make.o: \
		tests/support/yq2_pmove.c q_shared.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -DDEDICATED_ONLY \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_test.make.o: \
		tests/sg_compound_swim_oracle_test.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_oracle_test.make.o: \
		tests/sg_compound_hook_oracle_test.c \
		tests/sg_compound_hook_oracle_fixture.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections \
		-Itests -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_hook_oracle_fixture.make.o: \
		tests/sg_compound_hook_oracle_fixture.c \
		tests/sg_compound_hook_oracle_fixture.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections \
		-Itests -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

$(COMPOUND_ORACLE_FIXTURE_MAKE_OBJS): .sg_%.make.o: tests/sg_%.c \
		tests/sg_compound_oracle_fixture.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -Itests -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_oracle_under_test.make.o: \
		slipgate/sg_oracle.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.sg_compound_swim_oracle_rune_timing_under_test.make.o: \
		slipgate/sg_rune.c $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes \
		-ffunction-sections -fdata-sections -DSG_RUNE_TIMING_TEST \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

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

$(MAPLIST_ROTATION_TEST_BIN): tests/maplist_rotation_test.c g_maplist.c \
		g_local.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o $@ tests/maplist_rotation_test.c \
		g_maplist.c $(LIBS)

spectator-sound-test: $(SPECTATOR_SOUND_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(SPECTATOR_SOUND_TEST_BIN)

$(SPECTATOR_SOUND_TEST_BIN): $(SPECTATOR_SOUND_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(SPECTATOR_SOUND_TEST_OBJS) $(LIBS)

.sg_spectator_sound_test.make.o: tests/sg_spectator_sound_test.c \
		slipgate/sg_sound_policy.h \
		g_local.h slipgate/sg_net.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

.sg_spectator_sound_net_under_test.make.o: slipgate/sg_net.c \
		g_local.h slipgate/sg_net.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -Wno-strict-prototypes -ffunction-sections \
		-fdata-sections -I. -MMD -MP -MF $(patsubst %.o,%.d,$@) \
		-c -o $@ $<

povlock-test: $(POVLOCK_TEST_BIN) $(POVLOCK_DISPATCH_TEST)
	$(E) [TEST] $<
	$(Q)./$(POVLOCK_TEST_BIN)
	$(Q)python3 -B $(POVLOCK_DISPATCH_TEST)

$(POVLOCK_TEST_BIN): $(POVLOCK_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(POVLOCK_TEST_OBJS) $(LIBS)

.povlock_test.make.o: tests/povlock_test.c g_local.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.povlock_under_test.make.o: g_chase.c g_local.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.povlock_endframe_under_test.make.o: p_view.c g_local.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-unused-parameter -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

pov-session-production-test: $(POV_SESSION_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(POV_SESSION_TEST_BIN)

$(POV_SESSION_TEST_BIN): $(POV_SESSION_TEST_OBJS)
	$(E) [TEST-LD] $@
	$(Q)$(CC) -Wl,--gc-sections -o $@ $(POV_SESSION_TEST_OBJS) $(LIBS)

.pov_session_production_test.make.o: tests/pov_session_production_test.c \
		g_local.h g_tourney.h slipgate/sg_bot.h \
		slipgate/sg_pov_identity.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.pov_session_chase_under_test.make.o: g_chase.c g_local.h $(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.pov_session_client_under_test.make.o: p_client.c g_local.h g_tourney.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -Wno-unused-parameter -ffunction-sections -fdata-sections \
		-I. -MMD -MP -MF $(patsubst %.o,%.d,$@) -c -o $@ $<

.pov_session_identity_under_test.make.o: slipgate/sg_pov_identity.c \
		g_local.h slipgate/sg_bot.h slipgate/sg_pov_identity.h \
		$(REVISION_HEADER)
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -ffunction-sections -fdata-sections -I. -MMD -MP \
		-MF $(patsubst %.o,%.d,$@) -c -o $@ $<

host-test: $(HOST_TEST_BIN) $(ACTION_TEST_BIN) $(COMPOUND_TEST_BIN) \
		rune-v2-contract-test \
		$(MOVER_LEASE_TEST_BIN) $(WATER_FOREST_TEST_BIN) \
		train-station-candidate-test train-station-candidate-game-test \
		train-station-board-path-test \
		train-station-transaction-test train-station-game-test \
		$(BUTTON_LIVE_TEST_BIN) $(SHOOT_DOOR_LIVE_TEST_BIN) \
		$(BUTTON_GAME_TEST_BIN) $(COMPOUND_GUARD_TEST_BIN) \
		$(COMPOUND_GUARD_GAME_TEST_BIN) \
		$(COMPOUND_GUARD_GAME_INTEGRATION_TEST) \
		$(DECLARED_DOOR_GUARD_TEST_BIN) \
		$(DECLARED_DOOR_GUARD_INTEGRATION_TESTS) \
		$(COMPOUND_WORLD_TEST_BIN) $(COMPOUND_GEN_TEST_BIN) \
		$(COMPOUND_PUBLICATION_TEST_BIN) \
		$(COMPOUND_PUBLICATION_INTEGRATION_TEST) \
		$(COMPOUND_ACTION_INTEGRATION_TEST) \
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
		$(PROJECT_COMPLETION_PLAN_TEST) \
		$(DESLOP_AUDIT) $(DESLOP_AUDIT_TEST) $(SOURCE_SIZE_BUDGET) \
		$(RUNE_PYTHON_TESTS) \
		$(RUNGEN_TEST) \
		$(RUNGEN_PAIR_TEST) \
		$(BOTKIN_TEST) $(SHEET_CLI_TEST) \
		$(RUNE_CORPUS_CONTROLLER_TEST) $(RUNE_GENERATOR_CONFIG_TEST) \
		tools/rune.cfg \
		$(BUILD_PYTHON_RUNTIME_TEST) \
		$(FLEET_RUNNER_TEST) $(FLEET_RUNNER_LIVE_TEST) \
		tools/fleet-runner.py tools/fleet_runner_live.py tools/topmaps.txt \
		route-only-match-test \
		$(SERVER_BUNDLE_TEST) tools/server_bundle.py \
		$(BSPMECHANISMS_TEST) \
		$(WAVELOOP_PROCESS_TEST) \
		$(TEMP_FLAG_DIAGNOSTIC_TEST) \
		$(DANGER_LEASE_TEST_BIN) $(DANGER_POLICY_TEST_BIN) \
		$(DANGER_TEST_BIN) $(FIELDS_CANDIDATE_TEST_BIN) \
		$(STALL_CENSUS_PYTHON_TEST) \
		$(SPECTATOR_SOUND_TEST_BIN) tests/test_spectator_limit.py \
		$(HUMAN_SPEED_TEST_BIN) $(HUMAN_SPEED_INTEGRATION_TEST) \
		$(HUMAN_TRACE_TESTS) $(HUMAN_TRACE_HOOK_TEST_BIN) \
		$(DOOR_APPROACH_TEST_BIN) $(DOOR_APPROACH_INTEGRATION_TEST) \
		$(DEFENSE_SHIFT_TEST_BIN) $(DEFENSE_SHIFT_INTEGRATION_TEST) \
		$(DEFENSE_SUPPLY_TEST_BIN) $(DEFENSE_SUPPLY_INTEGRATION_TEST) \
		$(STRIKE_ADAPTER_TEST_BIN) $(STRIKE_ADAPTER_INTEGRATION_TEST) \
		$(DEFENSE_COMBAT_INTEGRATION_TEST) \
		$(CARRIER_RETURN_TEST) $(COMBAT_AIM_TEST) $(COMBAT_LAND_LEAD_TEST) \
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
		$(ROCKETJUMP_LIVE_TEST_BIN) $(ROCKETJUMP_CADENCE_TEST_BIN) \
		$(PUSH_LIVE_TEST_BIN) \
		$(PUSH_GAME_INTEGRATION_TEST) \
		$(ROCKETJUMP_GAME_TEST_BIN) \
		$(COMPOUND_SWIM_LIVE_INTEGRATION_TEST) \
		$(COMPOUND_SWIM_GAME_TEST_BIN) \
		$(COMPOUND_SWIM_GAME_INTEGRATION_TEST) \
		$(COMPOUND_DROP_LIVE_TEST_BIN) $(COMPOUND_DROP_GAME_TEST_BIN) \
		$(COMPOUND_DROP_FANOUT_TEST_BIN) \
		$(COMPOUND_DROP_TRANSITION_TEST_BIN) \
		$(COMPOUND_HOOK_LIVE_TEST_BIN) \
		$(COMPOUND_HOOK_GAME_TEST_BIN) \
		$(COMPOUND_HOOK_GAME_INTEGRATION_TEST) \
		$(COMPOUND_HOOK_GAME_EVENTS_TEST_BIN) \
		$(HOOK_LIVE_TEST_BIN) $(HOOK_DISCIPLINE_TEST_BIN) \
		$(HOOK_INTEGRATION_TEST) $(HOOK_SURFACE_VOLUME_INTEGRATION_TEST) \
		$(ROTATOR_SWEEP_TEST_BIN) $(MOVER_SUBJECT_SWEEP_TEST_BIN) \
		$(COMPOUND_SWIM_ORACLE_TEST_BIN) \
		$(COMPOUND_HOOK_ORACLE_TEST_BIN) \
		$(RUNE_DOOR_SCOPE_TEST_BIN) $(ENTFILE_TEST_BIN) \
		$(MAPLIST_ROTATION_TEST_BIN) \
		$(POVLOCK_TEST_BIN) $(POV_SESSION_TEST_BIN) \
		$(POVLOCK_DISPATCH_TEST) $(POV_SUPERVISOR_TEST_BIN) \
		$(POV_SUPERVISOR_BIN) $(POV_SUPERVISOR_TEST) \
		$(POV_ITERATE_SELECTION_TEST)
	$(E) [TEST] $<
	$(Q)./$(HOST_TEST_BIN)
	$(Q)./$(ACTION_TEST_BIN)
	$(Q)./$(COMPOUND_TEST_BIN)
	$(Q)./$(MOVER_LEASE_TEST_BIN)
	$(Q)./$(WATER_FOREST_TEST_BIN)
	$(Q)./$(BUTTON_LIVE_TEST_BIN)
	$(Q)./$(SHOOT_DOOR_LIVE_TEST_BIN)
	$(Q)./$(BUTTON_GAME_TEST_BIN)
	$(Q)python3 $(BUTTON_GAME_INTEGRATION_TEST)
	$(Q)./$(COMPOUND_GUARD_TEST_BIN)
	$(Q)./$(COMPOUND_GUARD_GAME_TEST_BIN)
	$(Q)python3 $(COMPOUND_GUARD_GAME_INTEGRATION_TEST)
	$(Q)./$(DECLARED_DOOR_GUARD_TEST_BIN)
	$(Q)set -e; for test in $(DECLARED_DOOR_GUARD_INTEGRATION_TESTS); do \
		python3 "$$test"; done
	$(Q)./$(COMPOUND_WORLD_TEST_BIN)
	$(Q)./$(COMPOUND_GEN_TEST_BIN)
	$(Q)./$(COMPOUND_PUBLICATION_TEST_BIN)
	$(Q)python3 $(COMPOUND_PUBLICATION_INTEGRATION_TEST)
	$(Q)sh $(COMPOUND_ACTION_INTEGRATION_TEST)
	$(Q)python3 $(MECHANISM_PUBLICATION_INTEGRATION_TEST)
	$(Q)./$(IDENTITY_TEST_BIN)
	$(Q)./$(RUNE_CODEC_TEST_BIN)
	$(Q)./$(RUNE_ARTIFACT_LOADER_TEST_BIN)
	$(Q)./$(RUNE_ARTIFACT_WRITER_TEST_BIN)
	$(Q)./$(RUNE_MECHANISM_PLAN_TEST_BIN)
	$(Q)./$(RUNE_MECHANISM_CATALOG_TEST_BIN)
	$(Q)./$(RUNE_MECHANISM_EXECUTION_TEST_BIN)
	$(Q)./$(RUNE_BINDING_TEST_BIN)
	$(Q)./$(RUNE_INSTALL_TEST_BIN)
	$(Q)./$(SIDECAR_WIRE_TEST_BIN)
	$(Q)./$(SIDECAR_LOADER_TEST_BIN)
	$(Q)./$(SIDECAR_STORE_TEST_BIN)
	$(Q)python3 $(RUNE_NAMING_TEST)
	$(Q)python3 $(RELEASE_WORKFLOW_TEST)
	$(Q)python3 -m unittest tests.test_rune_contracts tests.test_rune_artifact \
		tests.test_sidecario tests.test_rune_tool_readers \
		tests.test_lmctf58_rune_accept \
		tests.test_rune_water_overflow_failfast \
		tests.test_rune_pair_preflight
	$(Q)python3 $(RUNGEN_TEST)
	$(Q)python3 -m unittest tests.test_runegen_pair
	$(Q)python3 $(BOTKIN_TEST)
	$(Q)$(FILM_PYTHON) -B $(SHEET_CLI_TEST)
	$(Q)python3 -m unittest tests.test_rune_corpus_controller \
		tests.test_rune_corpus_finalizer \
		tests.test_rune_generator_config \
		tests.test_build_python_runtime
	$(Q)python3 -B $(BSPMECHANISMS_TEST)
	$(Q)python3 $(WAVELOOP_PROCESS_TEST)
	$(Q)python3 $(TEMP_FLAG_DIAGNOSTIC_TEST)
	$(Q)./$(DANGER_LEASE_TEST_BIN)
	$(Q)./$(DANGER_POLICY_TEST_BIN)
	$(Q)./$(DANGER_TEST_BIN)
	$(Q)./$(FIELDS_CANDIDATE_TEST_BIN)
	$(Q)python3 -B $(STALL_CENSUS_PYTHON_TEST)
	$(Q)./$(SPECTATOR_SOUND_TEST_BIN) && python3 -B tests/test_spectator_limit.py
	$(Q)./$(HUMAN_SPEED_TEST_BIN)
	$(Q)python3 -B $(HUMAN_SPEED_INTEGRATION_TEST)
	$(Q)SG_HUMAN_TRACE_TEST_BINARY=$(HUMAN_TRACE_HOOK_TEST_BIN) \
		python3 -B -m unittest tests.test_humantrace \
		tests.test_human_trace_integration \
		tests.test_human_trace_v3_integration
	$(Q)tmp=$$(mktemp -d); \
		trap 'rm -f "$$tmp/humantrace-tracehook.jsonl"; rmdir "$$tmp"' \
			EXIT HUP INT TERM; \
		./$(HUMAN_TRACE_HOOK_TEST_BIN) "$$tmp"
	$(Q)./$(DOOR_APPROACH_TEST_BIN)
	$(Q)python3 -B $(DOOR_APPROACH_INTEGRATION_TEST)
	$(Q)./$(DEFENSE_SHIFT_TEST_BIN)
	$(Q)python3 -B $(DEFENSE_SHIFT_INTEGRATION_TEST)
	$(Q)./$(DEFENSE_SUPPLY_TEST_BIN)
	$(Q)python3 -B $(DEFENSE_SUPPLY_INTEGRATION_TEST)
	$(Q)./$(STRIKE_ADAPTER_TEST_BIN)
	$(Q)python3 -B $(STRIKE_ADAPTER_INTEGRATION_TEST)
	$(Q)python3 -B $(DEFENSE_COMBAT_INTEGRATION_TEST)
	$(Q)python3 -B $(CARRIER_RETURN_TEST)
	$(Q)python3 -B $(COMBAT_AIM_TEST)
	$(Q)python3 -B $(COMBAT_LAND_LEAD_TEST)
	$(Q)python3 -B -m unittest tests.test_offense_flag_pickup_recovery \
		tests.botfill_selector_test tests.test_flag_state
	$(Q)./$(ITEM_COMMITMENT_TEST_BIN)
	$(Q)python3 -B $(ITEM_COMMITMENT_INTEGRATION_TEST)
	$(Q)./$(HOOK_DIAGNOSTICS_TEST_BIN)
	$(Q)python3 -B $(HOOK_DIAGNOSTICS_INTEGRATION_TEST)
	$(Q)python3 -B $(HOOK_DIAGNOSTICS_CONSUMER_TEST)
	$(Q)python3 -B $(ROLE_TELEMETRY_CONSUMER_TEST)
	$(Q)python3 -B $(HOOK_EVENTS_TEST)
	$(Q)./$(RUN_HANDOFF_TEST_BIN)
	$(Q)python3 -B $(RUN_HANDOFF_INTEGRATION_TEST)
	$(Q)./$(RUNE_PROOF_TEST_BIN)
	$(Q)./$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN)
	$(Q)./$(REPLAY_TEST_BIN)
	$(Q)./$(DROP_LIVE_TEST_BIN)
	$(Q)sh tests/sg_drop_begin_wiring_test.sh
	$(Q)./$(SWIM_LIVE_TEST_BIN)
	$(Q)./$(COMPOUND_SWIM_LIVE_TEST_BIN)
	$(Q)./$(ROCKETJUMP_LIVE_TEST_BIN)
	$(Q)./$(PUSH_LIVE_TEST_BIN)
	$(Q)python3 -B $(PUSH_GAME_INTEGRATION_TEST)
	$(Q)./$(ROCKETJUMP_CADENCE_TEST_BIN)
	$(Q)./$(ROCKETJUMP_GAME_TEST_BIN)
	$(Q)python3 $(COMPOUND_SWIM_LIVE_INTEGRATION_TEST)
	$(Q)./$(COMPOUND_SWIM_GAME_TEST_BIN)
	$(Q)python3 $(COMPOUND_SWIM_GAME_INTEGRATION_TEST)
	$(Q)./$(COMPOUND_DROP_LIVE_TEST_BIN)
	$(Q)./$(COMPOUND_DROP_GAME_TEST_BIN)
	$(Q)./$(COMPOUND_DROP_FANOUT_TEST_BIN)
	$(Q)./$(COMPOUND_DROP_TRANSITION_TEST_BIN)
	$(Q)./$(COMPOUND_HOOK_LIVE_TEST_BIN)
	$(Q)./$(COMPOUND_HOOK_GAME_TEST_BIN)
	$(Q)python3 -B $(COMPOUND_HOOK_GAME_INTEGRATION_TEST)
	$(Q)./$(COMPOUND_HOOK_GAME_EVENTS_TEST_BIN)
	$(Q)./$(HOOK_LIVE_TEST_BIN)
	$(Q)./$(HOOK_DISCIPLINE_TEST_BIN)
	$(Q)python3 $(HOOK_INTEGRATION_TEST)
	$(Q)python3 -B $(HOOK_SURFACE_VOLUME_INTEGRATION_TEST)
	$(Q)./$(ROTATOR_SWEEP_TEST_BIN)
	$(Q)./$(MOVER_SUBJECT_SWEEP_TEST_BIN)
	$(Q)./$(COMPOUND_SWIM_ORACLE_TEST_BIN)
	$(Q)./$(COMPOUND_HOOK_ORACLE_TEST_BIN)
	$(Q)./$(RUNE_DOOR_SCOPE_TEST_BIN)
	$(Q)./$(ENTFILE_TEST_BIN)
	$(Q)./$(MAPLIST_ROTATION_TEST_BIN)
	$(Q)./$(POVLOCK_TEST_BIN)
	$(Q)python3 -B $(POVLOCK_DISPATCH_TEST)
	$(Q)./$(POV_SESSION_TEST_BIN)
	$(Q)./$(POV_SUPERVISOR_TEST_BIN)
	$(Q)python3 -B $(POV_SUPERVISOR_TEST)
	$(Q)python3 -B $(POV_ITERATE_SELECTION_TEST)
	$(Q)./$(ENGINE_SNAPSHOT_TEST)
	$(Q)python3 -B $(DESLOP_AUDIT_TEST)
	$(Q)python3 -B $(DESLOP_AUDIT)
	$(Q)python3 -B $(PROJECT_COMPLETION_PLAN_TEST)

project-completion-plan-test: $(PROJECT_COMPLETION_PLAN_TEST) \
		PROJECT-COMPLETION-PLAN.md
	$(E) [TEST] project completion plan
	$(Q)python3 -B $(PROJECT_COMPLETION_PLAN_TEST)

weapon-effect-profile-test: tests/run_sg_weapon_effect_profile_test.sh \
		tests/sg_weapon_effect_profile_test.c \
		tests/sg_weapon_effect_profile_correction_test.c \
		tests/test_weapon_effect_profile_source_parity.py \
		slipgate/sg_weapon_effect_profile.c \
		slipgate/sg_weapon_effect_profile.h slipgate/sg_weapon_contract.h \
		slipgate/sg_weapon_host_constants.h slipgate/sg_rune_model.c \
		g_combat.c g_weapon.c p_weapon.c plasma.c plasma.h
	$(E) [TEST] weapon effect profiles
	$(Q)sh tests/run_sg_weapon_effect_profile_test.sh

host-law-publication-test: $(HOST_LAW_PUBLICATION_TEST) \
		tests/sg_host_law_publication_test.c \
		slipgate/sg_host_law_publication.c \
		slipgate/sg_host_law_publication.h \
		slipgate/sg_host_law_publication_private.h \
		slipgate/sg_host_law_owner.c slipgate/sg_host_law_owner.h \
		slipgate/sg_host_law_owner_internal.h \
		slipgate/sg_host_engine_pmove.c slipgate/sg_host_engine_pmove.h \
		slipgate/sg_host_engine_runtime.c \
		slipgate/sg_host_engine_runtime.h \
		slipgate/sg_host_engine_runtime_private.h \
		slipgate/sg_host_engine_parity.c slipgate/sg_host_engine_parity.h \
		slipgate/sg_host_hook_law.c slipgate/sg_host_hook_law.h \
		slipgate/sg_host_mechanism_law.c slipgate/sg_host_mechanism_law.h \
		slipgate/sg_host_pmove.c \
		slipgate/sg_host_collision.c slipgate/sg_bsp_world.c \
		slipgate/sg_rune_model.c tests/support/yq2_pmove.c q_shared.c \
		slipgate/sg_host_collision.h slipgate/sg_host_pmove.h \
		slipgate/sg_weapon_host_constants.h
	$(E) [TEST] host law publication
	$(Q)sh $(HOST_LAW_PUBLICATION_TEST)

rune-v2-exact-snapshot-test: tests/run_sg_rune_v2_exact_snapshot_test.sh \
		tests/sg_rune_v2_content_identity_test.c \
		tests/sg_rune_v2_content_identity_probe.c \
		tests/test_sg_rune_v2_content_identity.py \
		slipgate/sg_rune_v2_content_identity.c \
		slipgate/sg_rune_v2_content_identity.h \
		slipgate/sg_rune_v2_exact_snapshot.c \
		slipgate/sg_rune_v2_exact_snapshot.h \
		slipgate/sg_rune_v2_exact_snapshot_private.h
	$(E) [TEST] exact RUNE v2 snapshots
	$(Q)sh tests/run_sg_rune_v2_exact_snapshot_test.sh

hook-visibility-catalog-test: \
		tests/run_sg_hook_visibility_catalog_test.sh \
		tests/sg_hook_visibility_catalog_test.c \
		tests/sg_hook_visibility_feasibility_fixture.c \
		tests/sg_hook_visibility_feasibility_fixture.h \
		slipgate/sg_hook_visibility_catalog.c \
		slipgate/sg_hook_visibility_catalog.h \
		slipgate/sg_hook_visibility_feasibility.c \
		slipgate/sg_hook_visibility_feasibility.h \
		slipgate/sg_hook_visibility_feasibility_internal.h \
		slipgate/sg_hook_visibility_feasibility_family.c \
		slipgate/sg_hook_visibility_feasibility_events.c \
		slipgate/sg_hook_visibility_feasibility_partition.c \
		slipgate/sg_hook_visibility_feasibility_proof.c \
		slipgate/sg_hook_visibility_feasibility_verifier_digest.c \
		slipgate/sg_hook_visibility_feasibility_audit.c \
		slipgate/sg_hook_visibility_feasibility_audit_family.c \
		slipgate/sg_hook_visibility_feasibility_audit_events.c \
		slipgate/sg_hook_visibility_feasibility_audit_tiling.c
	$(E) [TEST] hook visibility catalog
	$(Q)sh tests/run_sg_hook_visibility_catalog_test.sh

static-affordance-catalog-publication-test: \
		tests/run_sg_static_affordance_catalog_test.sh \
		tests/sg_static_affordance_catalog_test.c \
		tests/sg_hook_visibility_feasibility_fixture.c \
		tests/sg_hook_visibility_feasibility_fixture.h \
		tests/sg_weapon_static_affordance_fixture.h \
		slipgate/sg_static_affordance_catalog.c \
		slipgate/sg_static_affordance_catalog.h \
		slipgate/sg_weapon_static_affordance.c \
		slipgate/sg_weapon_static_affordance.h \
		slipgate/sg_static_visibility_publication.c \
		slipgate/sg_static_visibility_publication.h \
		slipgate/sg_hook_visibility_catalog.c \
		slipgate/sg_hook_visibility_catalog.h
	$(E) [TEST] static affordance catalog publication
	$(Q)sh tests/run_sg_static_affordance_catalog_test.sh

bsp-entity-semantics-publication-test: \
		tests/run_sg_bsp_entity_semantics_publication_test.sh \
		tests/sg_bsp_entity_semantics_publication_test.c \
		slipgate/sg_bsp_entity_semantics_publication.c \
		slipgate/sg_bsp_entity_semantics_publication.h \
		slipgate/sg_bsp_entity_semantics_audit_expected.c \
		slipgate/sg_bsp_entity_semantics_audit_internal.h \
		slipgate/sg_bsp_entity_semantics_storage_internal.h \
		slipgate/sg_bsp_entity_semantics.c \
		slipgate/sg_bsp_entity_semantics.h \
		slipgate/sg_host_collision.c slipgate/sg_host_collision.h \
		slipgate/sg_bsp_world.c slipgate/sg_bsp_world.h
	$(E) [TEST] BSP entity semantics publication
	$(Q)sh tests/run_sg_bsp_entity_semantics_publication_test.sh

rune-v2-contract-test: rune-v2-exact-snapshot-test \
		rune-v2-independent-reader-test rune-v2-belief-test \
		rune-v2-perception-evidence-test rune-v2-configuration-space-test \
		ground-capability-publication-test \
		weapon-effect-profile-test hook-visibility-catalog-test \
		static-affordance-catalog-publication-test \
		bsp-entity-semantics-publication-test \
		host-law-publication-test \
		tests/sg_rune_v2_artifact_contract_test.c \
		tests/sg_rune_runtime_contract_test.c \
		tests/sg_rune_model_contract_test.c slipgate/sg_rune_model.c \
		tests/sg_bsp_world_test.c slipgate/sg_bsp_world.c \
		tests/run_sg_bsp_entity_semantics_test.sh \
		tests/sg_bsp_entity_semantics_test.c \
		slipgate/sg_bsp_entity_semantics.c \
		slipgate/sg_bsp_entity_semantics.h \
		tests/run_sg_static_visibility_test.sh \
		tests/sg_static_visibility_test.c \
		slipgate/sg_static_visibility.c \
		slipgate/sg_static_visibility.h \
		tests/run_sg_bsp_completeness_proof_test.sh \
		tests/sg_bsp_completeness_proof_test.c \
		tests/sg_bsp_completeness_portal_index_scaling_test.c \
		tests/sg_bsp_completeness_world_guard_test.c \
		tests/sg_bsp_completeness_deep_traversal_test.c \
		slipgate/sg_bsp_completeness_proof.c \
		slipgate/sg_bsp_completeness_proof.h \
		slipgate/sg_bsp_completeness_internal.h \
		slipgate/sg_bsp_completeness_core.c \
		slipgate/sg_bsp_completeness_coverage.c \
		slipgate/sg_bsp_completeness_lattice.c \
		slipgate/sg_bsp_completeness_portal.c \
		slipgate/sg_bsp_completeness_portal_index.c \
		slipgate/sg_bsp_completeness_region.c \
		slipgate/sg_bsp_completeness_state.c \
		slipgate/sg_bsp_completeness_traversal.c \
		tests/run_sg_rune_v2_artifact_semantic_test.sh \
		tests/sg_rune_v2_artifact_semantic_test.c \
		slipgate/sg_rune_v2_artifact_semantic.c \
		slipgate/sg_rune_v2_artifact_semantic.h \
		tests/run_sg_cell_phase_localization_test.sh \
		tests/sg_cell_phase_localization_test.c \
		slipgate/sg_cell_phase_localization.c \
		slipgate/sg_cell_phase_localization.h \
		tests/run_sg_host_collision_test.sh \
		tests/sg_host_collision_test.c slipgate/sg_host_collision.c \
		slipgate/sg_host_pmove.c \
		tests/sg_rune_v2_codec_test.c slipgate/sg_rune_v2_codec.c \
		tests/sg_rune_v2_artifact_loader_test.c \
		slipgate/sg_rune_v2_artifact_loader.c \
		tests/run_sg_rune_v2_artifact_publication_test.sh \
		tests/sg_rune_v2_artifact_publication_test.c \
		tests/support/sg_rune_v2_artifact_publication_faults.h \
		slipgate/sg_rune_v2_artifact_publication.c \
		slipgate/sg_rune_v2_artifact_publication_manifest.c \
		slipgate/sg_rune_v2_artifact_publication_io.c \
		tests/run_sg_strategy_test.sh tests/sg_strategy_test.c \
		slipgate/sg_strategy.c slipgate/sg_strategy_contract.h \
		tests/run_sg_destination_test.sh tests/sg_destination_test.c \
		slipgate/sg_destination.c slipgate/sg_destination.h \
		tests/run_sg_rune_dynamics_model_test.sh \
		tests/sg_rune_dynamics_model_test.c \
		tests/test_sg_rune_dynamics_rank_reference.py \
		slipgate/sg_rune_dynamics_model.c \
		slipgate/sg_rune_dynamics_geometry.c \
		slipgate/sg_rune_field_contract.c \
		slipgate/sg_rune_dynamics_model.h \
		slipgate/sg_rune_dynamics_model_internal.h \
		tests/run_sg_field_attractor_test.sh \
		tests/sg_field_attractor_test.c \
		slipgate/sg_field_attractor.c slipgate/sg_field_attractor.h \
		tests/run_sg_strategy_caller_test.sh \
		tests/sg_strategy_caller_test.c \
		tests/test_strategy_caller_integration.py \
		slipgate/sg_strategy_caller.c slipgate/sg_strategy_caller.h \
		slipgate/sg_strategy_runtime_bridge.c slipgate/sg_strategy_runtime_bridge.h \
		tests/support/yq2_pmove.c q_shared.c
	$(E) [TEST] RUNE v2 contracts
	$(Q)set -e; \
	tmp=$$(mktemp -d); \
	trap 'rm -r "$$tmp"' EXIT HUP INT TERM; \
	strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes'; \
	$(CC) $$strict -I. tests/sg_rune_v2_artifact_contract_test.c -o "$$tmp/artifact"; \
	"$$tmp/artifact"; \
	$(CC) $$strict -I. tests/sg_rune_runtime_contract_test.c \
		slipgate/sg_weapon_effect_profile.c slipgate/sg_rune_model.c -lm \
		-o "$$tmp/runtime"; \
	"$$tmp/runtime"; \
	$(CC) $$strict -Wcast-align -I. tests/sg_rune_model_contract_test.c \
		slipgate/sg_rune_model.c -lm -o "$$tmp/model"; \
	"$$tmp/model"; \
	$(CC) $$strict -Wcast-align -I. tests/sg_bsp_world_test.c \
		slipgate/sg_bsp_world.c -lm -o "$$tmp/bsp"; \
	"$$tmp/bsp"; \
	sh tests/run_sg_bsp_entity_semantics_test.sh; \
	sh tests/run_sg_static_visibility_test.sh; \
	sh tests/run_sg_bsp_completeness_proof_test.sh; \
	sh tests/run_sg_rune_v2_artifact_semantic_test.sh; \
	sh tests/run_sg_cell_phase_localization_test.sh; \
	sh tests/run_sg_host_collision_test.sh; \
	$(CC) $$strict -Wcast-align -I. tests/sg_rune_v2_codec_test.c \
		slipgate/sg_rune_v2_codec.c slipgate/sg_rune_model.c -lm \
		-o "$$tmp/codec"; \
	"$$tmp/codec"; \
	$(CC) $$strict -Wcast-align -I. \
		tests/sg_rune_v2_artifact_loader_test.c \
		slipgate/sg_rune_v2_artifact_loader.c \
		slipgate/sg_rune_v2_codec.c slipgate/sg_rune_model.c -lm \
		-o "$$tmp/loader"; \
	"$$tmp/loader"; \
	sh tests/run_sg_rune_v2_artifact_publication_test.sh; \
	sh tests/run_sg_strategy_test.sh; \
	sh tests/run_sg_strategy_caller_test.sh; \
	python3 -B tests/test_strategy_caller_integration.py; \
	sh tests/run_sg_destination_test.sh; \
	$(CC) $$strict -Wcast-align -I. -c slipgate/sg_destination.c \
		-o "$$tmp/destination.o"; \
	sh tests/run_sg_rune_dynamics_model_test.sh; \
	sh tests/run_sg_field_attractor_test.sh; \
	$(CC) $$strict -Wcast-align -I. -c \
		slipgate/sg_rune_dynamics_model.c -o "$$tmp/dynamics.o"; \
	$(CC) $$strict -Wcast-align -I. -c \
		slipgate/sg_rune_dynamics_geometry.c -o "$$tmp/geometry.o"; \
	$(CC) $$strict -Wcast-align -I. -c \
		slipgate/sg_rune_field_contract.c -o "$$tmp/field-contract.o"; \
	$(CC) $$strict -Wcast-align -I. -c \
		slipgate/sg_field_attractor.c -o "$$tmp/field-attractor.o"

ground-capability-publication-test: \
		tests/run_sg_ground_capability_publication_test.sh \
		tests/sg_ground_capability_publication_test.c \
		tests/sg_ground_capability_test.c \
		slipgate/sg_ground_capability_publication.c \
		slipgate/sg_ground_capability_publication.h \
		slipgate/sg_ground_capability.c slipgate/sg_ground_capability.h
	$(E) [TEST] ground capability publication
	$(Q)sh tests/run_sg_ground_capability_publication_test.sh

rune-v2-belief-test: tests/run_sg_belief_test.sh \
		tests/test_belief_life_identity_contract.py tests/sg_belief_test.c \
		g_local.h p_client.c slipgate/sg_belief.c slipgate/sg_belief_contract.h \
		slipgate/sg_rune_v2_content_identity.c \
		slipgate/sg_rune_v2_content_identity.h
	$(E) [TEST] RUNE v2 phase-space beliefs
	$(Q)sh tests/run_sg_belief_test.sh

rune-v2-perception-evidence-test: \
		tests/run_sg_perception_evidence_test.sh \
		tests/sg_perception_evidence_test.c \
		slipgate/sg_perception_evidence.c \
		slipgate/sg_perception_evidence.h \
		slipgate/sg_belief.c slipgate/sg_belief_contract.h \
		slipgate/sg_rune_v2_content_identity.c \
		slipgate/sg_rune_v2_content_identity.h
	$(E) [TEST] RUNE v2 perception evidence
	$(Q)sh tests/run_sg_perception_evidence_test.sh

rune-v2-configuration-space-test: tests/run_sg_configuration_space_test.sh \
		tests/sg_configuration_space_test.c \
		slipgate/sg_configuration_lattice.c \
		slipgate/sg_configuration_space.c \
		slipgate/sg_configuration_audit.c
	$(E) [TEST] RUNE v2 positive-volume configuration space
	$(Q)sh tests/run_sg_configuration_space_test.sh

deslop-test: $(DESLOP_AUDIT) $(DESLOP_AUDIT_TEST) \
		$(SOURCE_SIZE_BUDGET)
	$(E) [TEST] deslop
	$(Q)python3 -B $(DESLOP_AUDIT_TEST)
	$(Q)python3 -B $(DESLOP_AUDIT)

runegen-test: tools/runegen.sh $(RUNGEN_PAIR_TOOL) $(RUNGEN_TEST) \
		$(RUNGEN_PAIR_TEST) tools/runeio.py tools/rune_contracts_generated.py
	$(E) [TEST] runegen pair
	$(Q)python3 $(RUNGEN_TEST)
	$(Q)python3 -m unittest tests.test_runegen_pair

botkin-test: tools/botkin.py $(BOTKIN_TEST)
	$(E) [TEST] botkin observer
	$(Q)python3 $(BOTKIN_TEST)

sheet-cli-test: $(SHEET_CLI_TEST)
	$(Q)$(FILM_PYTHON) -B $(SHEET_CLI_TEST)

action-test: $(ACTION_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(ACTION_TEST_BIN)

# Linux tools-only supervisor: intentionally absent from Visual Studio projects.
pov-supervisor-test: $(POV_SUPERVISOR_TEST_BIN) $(POV_SUPERVISOR_BIN) \
		$(POV_SUPERVISOR_TEST) $(POV_ITERATE_SELECTION_TEST) \
		$(RUNE_PAIR_PREFLIGHT_DEPS)
	$(E) [TEST] $<
	$(Q)./$(POV_SUPERVISOR_TEST_BIN)
	$(Q)python3 -B $(POV_SUPERVISOR_TEST)
	$(Q)python3 -B $(POV_ITERATE_SELECTION_TEST)

$(POV_SUPERVISOR_BIN): tools/pov-supervisor.c tools/pov-spawn-linux.c tools/pov-spawn-linux.h
	$(E) [TEST-CC] $@
	$(Q)$(CC) -std=c11 -O2 -Wall -Wextra -Werror -Wpedantic -Itools -o $@ \
		tools/pov-supervisor.c tools/pov-spawn-linux.c

$(POV_SUPERVISOR_TEST_BIN): tests/pov_supervisor_unit.c tools/pov-spawn-linux.c tools/pov-spawn-linux.h
	$(E) [TEST-CC] $@
	$(Q)$(CC) -std=c11 -O1 -g -Wall -Wextra -Werror -Wpedantic -DPOV_TESTING \
		-Itools -o $@ tests/pov_supervisor_unit.c tools/pov-spawn-linux.c

rune-naming-test: $(RUNE_NAMING_TEST)
	$(E) [TEST] $<
	$(Q)python3 $(RUNE_NAMING_TEST)

rune-v2-independent-reader-test: tools/runev2read.c tools/runev2read.py \
		tools/runev2makecheck.c \
		tests/sg_rune_v2_codec_probe.c tests/sg_rune_v2_fixture_writer.c \
		tests/test_rune_v2_independent_readers.py \
		tests/test_rune_v2_make_independent_reader.py \
		tests/run_rune_v2_make_independent_reader_test.sh \
		tests/support/sg_rune_v2_fixture.h \
		slipgate/sg_rune_v2_artifact_loader.c slipgate/sg_rune_v2_codec.c \
		slipgate/sg_rune_model.c slipgate/sg_rune_v2_exact_snapshot.c \
		slipgate/sg_rune_v2_content_identity.c
	$(E) [TEST] RUNE v2 independent readers
	$(Q)set -e; \
	tmp=$$(mktemp -d); \
	trap 'rm -r "$$tmp"' EXIT HUP INT TERM; \
	strict='-std=c11 -Wall -Wextra -Werror -Wpedantic -Wconversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes'; \
	$(CC) $$strict tools/runev2read.c -lm -o "$$tmp/reader"; \
	$(CC) $$strict -Wcast-align -I. tests/sg_rune_v2_codec_probe.c \
		slipgate/sg_rune_v2_artifact_loader.c slipgate/sg_rune_v2_codec.c \
		slipgate/sg_rune_model.c -lm -o "$$tmp/probe"; \
	$(CC) $$strict -Wcast-align -I. tests/sg_rune_v2_fixture_writer.c \
		slipgate/sg_rune_v2_codec.c slipgate/sg_rune_model.c -lm \
		-o "$$tmp/writer"; \
	RUNE_V2_C_READER="$$tmp/reader" RUNE_V2_CODEC_PROBE="$$tmp/probe" \
		RUNE_V2_FIXTURE_WRITER="$$tmp/writer" \
		python3 -B tests/test_rune_v2_independent_readers.py; \
	sanitize="$$strict -O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined"; \
	clang $$sanitize tools/runev2read.c -lm -o "$$tmp/reader-san"; \
	clang $$sanitize -Wcast-align -I. tests/sg_rune_v2_codec_probe.c \
		slipgate/sg_rune_v2_artifact_loader.c slipgate/sg_rune_v2_codec.c \
		slipgate/sg_rune_model.c -lm -o "$$tmp/probe-san"; \
	clang $$sanitize -Wcast-align -I. tests/sg_rune_v2_fixture_writer.c \
		slipgate/sg_rune_v2_codec.c slipgate/sg_rune_model.c -lm \
		-o "$$tmp/writer-san"; \
	clang --analyze $$strict tools/runev2read.c -o "$$tmp/analyzer.plist"; \
	if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1; then \
		x86_64-w64-mingw32-gcc $$strict -c tools/runev2read.c \
			-o "$$tmp/reader-windows.o"; \
	fi; \
	ASAN_OPTIONS='detect_leaks=1:halt_on_error=1' \
	UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
	RUNE_V2_C_READER="$$tmp/reader-san" \
	RUNE_V2_CODEC_PROBE="$$tmp/probe-san" \
	RUNE_V2_FIXTURE_WRITER="$$tmp/writer-san" \
		python3 -B tests/test_rune_v2_independent_readers.py; \
	sh tests/run_rune_v2_make_independent_reader_test.sh "$(CC)"

rune-artifact-test: $(RUNE_PYTHON_TESTS)
	$(E) [TEST] rune artifact
	$(Q)python3 -m unittest tests.test_rune_contracts tests.test_rune_artifact \
		tests.test_sidecario tests.test_rune_tool_readers \
		tests.test_lmctf58_rune_accept \
		tests.test_rune_water_overflow_failfast \
		tests.test_rune_pair_preflight

push-game-integration-test: $(PUSH_GAME_INTEGRATION_TEST)
	$(E) [TEST] push game integration
	$(Q)python3 -B $(PUSH_GAME_INTEGRATION_TEST)

train-gate-game-integration-test: $(TRAIN_GATE_GAME_INTEGRATION_TEST)
	$(E) [TEST] train gate game integration
	$(Q)python3 -B $(TRAIN_GATE_GAME_INTEGRATION_TEST)

shoot-door-game-integration-test: $(SHOOT_DOOR_GAME_INTEGRATION_TEST)
	$(E) [TEST] shoot door game integration
	$(Q)python3 -B $(SHOOT_DOOR_GAME_INTEGRATION_TEST)

rune-corpus-controller-test: $(RUNE_CORPUS_CONTROLLER_TEST) \
		$(RUNE_CORPUS_FINALIZER_TEST) \
		$(BUILD_PYTHON_RUNTIME_TEST) tools/build_python_runtime.py \
		tools/rune_corpus_controller.py tools/rune_corpus_finalizer.py \
		tools/rune_corpus_policy.py tools/RUNE_CORPUS_CONTROLLER.md \
		tools/rune-corpus-maps.txt
	$(E) [TEST] RUNE corpus controller
	$(Q)python3 -m unittest tests.test_rune_corpus_controller \
		tests.test_rune_corpus_finalizer \
		tests.test_build_python_runtime

rune-generator-config-test: $(RUNE_GENERATOR_CONFIG_TEST) tools/rune.cfg
	$(E) [TEST] RUNE generator config
	$(Q)python3 -m unittest tests.test_rune_generator_config

fleet-runner-test: $(FLEET_RUNNER_TEST) $(FLEET_RUNNER_LIVE_TEST) \
		$(ROUTE_ONLY_EVIDENCE_TEST) $(ROUTE_ONLY_MATCH_CONFIG_TEST) \
		$(SERVER_BUNDLE_TEST) tools/fleet-runner.py tools/fleet_runner_live.py \
		tools/route-only-match.cfg tools/route-only-maplist.txt \
		tools/server_bundle.py tools/topmaps.txt tools/rune-corpus-maps.txt
	$(E) [TEST] persistent fleet runner
	$(Q)python3 -B -m unittest tests.test_fleet_runner tests.test_fleet_runner_live

route-only-match-test: $(ROUTE_ONLY_EVIDENCE_TEST) $(ROUTE_ONLY_MATCH_CONFIG_TEST) \
		$(FLEET_RUNNER_LIVE_TEST) $(SERVER_BUNDLE_TEST) \
		tools/fleet-runner.py tools/fleet_runner_live.py \
		tools/route-only-match.cfg tools/route-only-maplist.txt tools/server_bundle.py
	$(E) [TEST] route-only ordinary match evidence
	$(Q)python3 -B -m unittest tests.test_fleet_runner_live tests.test_route_only_evidence \
		tests.test_route_only_match_config tests.test_server_bundle

server-bundle-test: $(SERVER_BUNDLE_TEST) tools/server_bundle.py \
		tools/fleet-runner.py tools/topmaps.txt tools/rune-corpus-maps.txt
	$(E) [TEST] immutable server bundle
	$(Q)python3 -B -m unittest tests.test_server_bundle

compound-test: $(COMPOUND_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_TEST_BIN)

mover-lease-test: $(MOVER_LEASE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(MOVER_LEASE_TEST_BIN)

water-forest-test: $(WATER_FOREST_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(WATER_FOREST_TEST_BIN)

button-live-test: $(BUTTON_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(BUTTON_LIVE_TEST_BIN)

train-gate-live-test: $(TRAIN_GATE_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(TRAIN_GATE_LIVE_TEST_BIN)

mechanism-timeline-test: $(MECHANISM_TIMELINE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(MECHANISM_TIMELINE_TEST_BIN)

relay-wall-transaction-test: $(RELAY_WALL_TRANSACTION_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RELAY_WALL_TRANSACTION_TEST_BIN)

relay-wall-objective-test: $(RELAY_WALL_OBJECTIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RELAY_WALL_OBJECTIVE_TEST_BIN)

delayed-use-ticket-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_delayed_use_ticket_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o sg_delayed_use_ticket_test.make \
		tests/sg_delayed_use_ticket_test.c slipgate/sg_delayed_use_ticket.c \
		$(LIBS)
	$(E) [TEST] sg_delayed_use_ticket_test.make
	$(Q)./sg_delayed_use_ticket_test.make

relay-wall-live-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_relay_wall_live_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o sg_relay_wall_live_test.make \
		tests/sg_relay_wall_live_test.c slipgate/sg_relay_wall_live.c \
		slipgate/sg_relay_wall_transaction.c \
		slipgate/sg_mechanism_timeline.c slipgate/sg_delayed_use_ticket.c \
		$(LIBS)
	$(E) [TEST] sg_relay_wall_live_test.make
	$(Q)./sg_relay_wall_live_test.make

timed-vault-transaction-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_timed_vault_transaction_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o sg_timed_vault_transaction_test.make \
		tests/sg_timed_vault_transaction_test.c \
		slipgate/sg_timed_vault_transaction.c $(LIBS)
	$(E) [TEST] sg_timed_vault_transaction_test.make
	$(Q)./sg_timed_vault_transaction_test.make

train-station-plan-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_train_station_plan_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o sg_train_station_plan_test.make \
		tests/sg_train_station_plan_test.c \
		slipgate/sg_train_station_plan.c $(LIBS)
	$(E) [TEST] sg_train_station_plan_test.make
	$(Q)./sg_train_station_plan_test.make

train-station-candidate-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_train_station_candidate_test.make
	$(Q)$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror \
		-Wpedantic -I. -o sg_train_station_candidate_test.make \
		tests/sg_train_station_candidate_test.c \
		slipgate/sg_train_station_candidate.c \
		slipgate/sg_train_station_plan.c $(LIBS)
	$(E) [TEST] sg_train_station_candidate_test.make
	$(Q)./sg_train_station_candidate_test.make
	$(Q)python3 -B -m unittest tests.test_train_station_generation_integration

train-station-candidate-game-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_train_station_candidate_game_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-Wl,--gc-sections -o sg_train_station_candidate_game_test.make \
		tests/sg_train_station_candidate_game_test.c \
		slipgate/sg_train_station_candidate_game.c \
		slipgate/sg_train_station_board_path.c \
		slipgate/sg_train_station_candidate.c \
		slipgate/sg_train_station_plan.c $(LIBS)
	$(E) [TEST] sg_train_station_candidate_game_test.make
	$(Q)./sg_train_station_candidate_game_test.make

train-station-board-path-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_train_station_board_path_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o sg_train_station_board_path_test.make \
		tests/sg_train_station_board_path_test.c \
		slipgate/sg_train_station_board_path.c $(LIBS)
	$(E) [TEST] sg_train_station_board_path_test.make
	$(Q)./sg_train_station_board_path_test.make

train-station-transaction-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_train_station_transaction_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o sg_train_station_transaction_test.make \
		tests/sg_train_station_transaction_test.c \
		slipgate/sg_train_station_transaction.c $(LIBS)
	$(E) [TEST] sg_train_station_transaction_test.make
	$(Q)./sg_train_station_transaction_test.make

train-station-game-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_train_station_game_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o sg_train_station_game_test.make \
		tests/sg_train_station_game_test.c \
		slipgate/sg_train_station_game.c \
		slipgate/sg_train_station_board_path.c \
		slipgate/sg_train_station_transaction.c $(LIBS)
	$(E) [TEST] sg_train_station_game_test.make
	$(Q)./sg_train_station_game_test.make
	$(E) [TEST] train station game integration
	$(Q)python3 -B tests/test_train_station_game_integration.py

timed-vault-game-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_timed_vault_game_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o sg_timed_vault_game_test.make \
		tests/sg_timed_vault_game_test.c slipgate/sg_timed_vault_game.c \
		slipgate/sg_timed_vault_transaction.c \
		slipgate/sg_delayed_use_ticket.c $(LIBS)
	$(E) [TEST] sg_timed_vault_game_test.make
	$(Q)./sg_timed_vault_game_test.make

timed-vault-runtime-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_timed_vault_game_runtime_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -ffunction-sections -fdata-sections -I. \
		-Wl,--gc-sections -o sg_timed_vault_game_runtime_test.make \
		tests/sg_timed_vault_game_runtime_test.c \
		slipgate/sg_timed_vault_game_runtime.c \
		slipgate/sg_timed_vault_game.c \
		slipgate/sg_timed_vault_transaction.c \
		slipgate/sg_delayed_use_ticket.c $(LIBS)
	$(E) [TEST] sg_timed_vault_game_runtime_test.make
	$(Q)./sg_timed_vault_game_runtime_test.make

timed-vault-egress-test: $(REVISION_HEADER)
	$(E) [TEST-CC] sg_timed_vault_egress_test.make
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o sg_timed_vault_egress_test.make \
		tests/sg_timed_vault_egress_test.c \
		slipgate/sg_timed_vault_egress.c -lm
	$(Q)./sg_timed_vault_egress_test.make

shoot-door-live-test: $(SHOOT_DOOR_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(SHOOT_DOOR_LIVE_TEST_BIN)

button-game-test: $(BUTTON_GAME_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(BUTTON_GAME_TEST_BIN)
	$(Q)python3 $(BUTTON_GAME_INTEGRATION_TEST)

compound-guard-test: $(COMPOUND_GUARD_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_GUARD_TEST_BIN)

compound-guard-game-test: $(COMPOUND_GUARD_GAME_TEST_BIN) \
		$(COMPOUND_GUARD_GAME_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_GUARD_GAME_TEST_BIN)
	$(Q)python3 $(COMPOUND_GUARD_GAME_INTEGRATION_TEST)

declared-door-guard-test: $(DECLARED_DOOR_GUARD_TEST_BIN) \
		$(DECLARED_DOOR_GUARD_INTEGRATION_TESTS)
	$(E) [TEST] $<
	$(Q)./$(DECLARED_DOOR_GUARD_TEST_BIN)
	$(Q)set -e; for test in $(DECLARED_DOOR_GUARD_INTEGRATION_TESTS); do \
		python3 "$$test"; done

compound-world-test: $(COMPOUND_WORLD_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_WORLD_TEST_BIN)

compound-gen-test: $(COMPOUND_GEN_TEST_BIN) $(COMPOUND_GEN_GAME_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_GEN_TEST_BIN)
	$(Q)./$(COMPOUND_GEN_GAME_TEST_BIN)

compound-action-test: $(COMPOUND_ACTION_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)sh $(COMPOUND_ACTION_INTEGRATION_TEST)

compound-publication-test: $(COMPOUND_PUBLICATION_TEST_BIN) \
		$(COMPOUND_PUBLICATION_INTEGRATION_TEST) \
		$(MECHANISM_PUBLICATION_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_PUBLICATION_TEST_BIN)
	$(Q)python3 $(COMPOUND_PUBLICATION_INTEGRATION_TEST)
	$(Q)python3 $(MECHANISM_PUBLICATION_INTEGRATION_TEST)

identity-test: $(IDENTITY_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(IDENTITY_TEST_BIN)

rune-codec-test: $(RUNE_CODEC_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_CODEC_TEST_BIN)

rune-artifact-loader-test: $(RUNE_ARTIFACT_LOADER_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_ARTIFACT_LOADER_TEST_BIN)

rune-artifact-writer-test: $(RUNE_ARTIFACT_WRITER_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_ARTIFACT_WRITER_TEST_BIN)

rune-mechanism-plan-test: $(RUNE_MECHANISM_PLAN_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_MECHANISM_PLAN_TEST_BIN)

rune-mechanism-catalog-test: $(RUNE_MECHANISM_CATALOG_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_MECHANISM_CATALOG_TEST_BIN)

rune-mechanism-execution-test: $(RUNE_MECHANISM_EXECUTION_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_MECHANISM_EXECUTION_TEST_BIN)

rune-binding-test: $(RUNE_BINDING_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_BINDING_TEST_BIN)

rune-accept-tool: $(RUNE_ACCEPT_BIN)

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

danger-test: $(DANGER_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(DANGER_TEST_BIN)

fields-candidate-test: $(FIELDS_CANDIDATE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(FIELDS_CANDIDATE_TEST_BIN)

human-speed-test: $(HUMAN_SPEED_TEST_BIN) $(HUMAN_SPEED_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(HUMAN_SPEED_TEST_BIN)
	$(Q)python3 -B $(HUMAN_SPEED_INTEGRATION_TEST)

human-trace-test: $(HUMAN_TRACE_TESTS) $(HUMAN_TRACE_HOOK_TEST_BIN)
	$(E) [TEST] $<
	$(Q)SG_HUMAN_TRACE_TEST_BINARY=$(HUMAN_TRACE_HOOK_TEST_BIN) \
		python3 -B -m unittest tests.test_humantrace \
		tests.test_human_trace_integration \
		tests.test_human_trace_v3_integration
	$(Q)tmp=$$(mktemp -d); \
		trap 'rm -f "$$tmp/humantrace-tracehook.jsonl"; rmdir "$$tmp"' \
			EXIT HUP INT TERM; \
		./$(HUMAN_TRACE_HOOK_TEST_BIN) "$$tmp"; \
		./$(HUMAN_TRACE_HOOK_TEST_BIN) "$$tmp" writefail

door-approach-test: $(DOOR_APPROACH_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(DOOR_APPROACH_TEST_BIN)

defense-shift-test: $(DEFENSE_SHIFT_TEST_BIN) $(DEFENSE_SHIFT_INTEGRATION_TEST) \
		$(DEFENSE_COMBAT_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(DEFENSE_SHIFT_TEST_BIN)
	$(Q)python3 -B $(DEFENSE_SHIFT_INTEGRATION_TEST)
	$(Q)python3 -B $(DEFENSE_COMBAT_INTEGRATION_TEST)

defense-supply-test: $(DEFENSE_SUPPLY_TEST_BIN) $(DEFENSE_SUPPLY_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(DEFENSE_SUPPLY_TEST_BIN)
	$(Q)python3 -B $(DEFENSE_SUPPLY_INTEGRATION_TEST)

strike-adapter-test: $(STRIKE_ADAPTER_TEST_BIN) $(STRIKE_ADAPTER_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(STRIKE_ADAPTER_TEST_BIN)
	$(Q)python3 -B $(STRIKE_ADAPTER_INTEGRATION_TEST)

item-commitment-test: $(ITEM_COMMITMENT_TEST_BIN) \
		$(ITEM_COMMITMENT_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(ITEM_COMMITMENT_TEST_BIN)
	$(Q)python3 -B $(ITEM_COMMITMENT_INTEGRATION_TEST)

hook-diagnostics-test: $(HOOK_DIAGNOSTICS_TEST_BIN) \
		$(HOOK_DIAGNOSTICS_INTEGRATION_TEST) \
		$(HOOK_DIAGNOSTICS_CONSUMER_TEST) $(HOOK_EVENTS_TEST)
	$(E) [TEST] $<
	$(Q)./$(HOOK_DIAGNOSTICS_TEST_BIN)
	$(Q)python3 -B $(HOOK_DIAGNOSTICS_INTEGRATION_TEST)
	$(Q)python3 -B $(HOOK_DIAGNOSTICS_CONSUMER_TEST)
	$(Q)python3 -B $(HOOK_EVENTS_TEST)
run-handoff-test: $(RUN_HANDOFF_TEST_BIN) $(RUN_HANDOFF_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(RUN_HANDOFF_TEST_BIN)
	$(Q)python3 -B $(RUN_HANDOFF_INTEGRATION_TEST)
rune-install-test: $(RUNE_INSTALL_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_INSTALL_TEST_BIN)
rune-proof-test: $(RUNE_PROOF_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_PROOF_TEST_BIN)
rune-objective-diagnostics-test: $(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_BIN)
rune-late-path-test: tests/sg_rune_late_path_test.c slipgate/sg_rune_late_path.c
	$(Q)$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -Wpedantic \
		-I. -o sg_rune_late_path_test.make $^
	$(Q)./sg_rune_late_path_test.make
rune-topology-test: tests/sg_rune_topology_test.c \
	slipgate/sg_rune_topology.c slipgate/sg_rune_topology_game.c
	$(Q)$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -Wpedantic \
		-I. -o sg_rune_topology_test.make $^ && ./sg_rune_topology_test.make
rune-reverse-boundary-test: tests/sg_rune_reverse_boundary_test.c \
		slipgate/sg_rune_reverse_boundary.c slipgate/sg_rune_topology.c
	$(Q)$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -Wpedantic \
		-I. -o sg_rune_reverse_boundary_test.make $^ $(LIBS)
	$(Q)./sg_rune_reverse_boundary_test.make
rune-update-test: $(CHAIN_HOOK_FRONTIER_INTEGRATION_TEST)
	$(Q)$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -Wpedantic -I. \
		-o sg_rune_update_source_test.make \
		tests/sg_rune_update_source_test.c \
		slipgate/sg_rune_update_source.c
	$(Q)./sg_rune_update_source_test.make
	$(Q)$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -Wpedantic -I. \
		-o sg_rune_learning_test.make tests/sg_rune_learning_test.c \
		slipgate/sg_rune_learning.c -lm
	$(Q)./sg_rune_learning_test.make
	$(Q)$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -Wpedantic -I. \
		-o sg_rune_learning_game_test.make \
		tests/sg_rune_learning_game_test.c \
		slipgate/sg_rune_learning_game.c -lm
	$(Q)./sg_rune_learning_game_test.make
	$(Q)$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -Wpedantic \
		-ffunction-sections -fdata-sections -I. -Wl,--gc-sections \
		-o sg_rune_hook_nomination_test.make \
		tests/sg_rune_hook_nomination_test.c \
		slipgate/sg_rune_hook_frontier.c sg_action.c q_shared.c -lm
	$(Q)./sg_rune_hook_nomination_test.make
	$(Q)python3 -B -m unittest tests.test_runelearn \
		tests.test_rune_update_integration
	$(Q)python3 -B $(CHAIN_HOOK_FRONTIER_INTEGRATION_TEST)
human-hook-ownership-test: tests/sg_human_hook_ownership_test.c \
		slipgate/sg_client_ownership.c tests/test_human_hook_ownership_integration.py
	$(Q)$(CC) $(CFLAGS) -std=c11 -Wall -Wextra -Werror -Wpedantic -I. \
		-o sg_human_hook_ownership_test.make \
		tests/sg_human_hook_ownership_test.c slipgate/sg_client_ownership.c
	$(Q)./sg_human_hook_ownership_test.make
	$(Q)python3 -B tests/test_human_hook_ownership_integration.py
replay-test: $(REPLAY_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(REPLAY_TEST_BIN)
chain-hook-replay-test: $(CHAIN_HOOK_REPLAY_TEST_BIN) \
	$(CHAIN_HOOK_GAME_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(CHAIN_HOOK_REPLAY_TEST_BIN)
	$(Q)python3 -B $(CHAIN_HOOK_GAME_INTEGRATION_TEST)
drop-live-test: $(DROP_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(DROP_LIVE_TEST_BIN)
	$(Q)sh tests/sg_drop_begin_wiring_test.sh
swim-live-test: $(SWIM_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(SWIM_LIVE_TEST_BIN)
compound-swim-live-test: $(COMPOUND_SWIM_LIVE_TEST_BIN) \
		$(COMPOUND_SWIM_LIVE_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_SWIM_LIVE_TEST_BIN)
	$(Q)python3 $(COMPOUND_SWIM_LIVE_INTEGRATION_TEST)

compound-swim-game-test: $(COMPOUND_SWIM_GAME_TEST_BIN) \
		$(COMPOUND_SWIM_GAME_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_SWIM_GAME_TEST_BIN)
	$(Q)python3 $(COMPOUND_SWIM_GAME_INTEGRATION_TEST)
compound-drop-live-test: $(COMPOUND_DROP_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_DROP_LIVE_TEST_BIN)

compound-drop-game-test: $(COMPOUND_DROP_GAME_TEST_BIN) \
		$(COMPOUND_DROP_FANOUT_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_DROP_GAME_TEST_BIN)
	$(Q)./$(COMPOUND_DROP_FANOUT_TEST_BIN)

compound-drop-transition-test: $(COMPOUND_DROP_TRANSITION_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_DROP_TRANSITION_TEST_BIN)

compound-hook-live-test: $(COMPOUND_HOOK_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_HOOK_LIVE_TEST_BIN)

compound-hook-game-test: $(COMPOUND_HOOK_GAME_TEST_BIN) \
		$(COMPOUND_HOOK_GAME_INTEGRATION_TEST)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_HOOK_GAME_TEST_BIN)
	$(Q)python3 -B $(COMPOUND_HOOK_GAME_INTEGRATION_TEST)

compound-hook-game-events-test: $(COMPOUND_HOOK_GAME_EVENTS_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_HOOK_GAME_EVENTS_TEST_BIN)

hook-live-test: $(HOOK_LIVE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(HOOK_LIVE_TEST_BIN)

hook-integration-test:
	$(E) [TEST] $(HOOK_INTEGRATION_TEST)
	$(Q)python3 $(HOOK_INTEGRATION_TEST)

hook-discipline-test: $(HOOK_DISCIPLINE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(HOOK_DISCIPLINE_TEST_BIN)

rotator-sweep-test: $(ROTATOR_SWEEP_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(ROTATOR_SWEEP_TEST_BIN)

mover-subject-sweep-test: $(MOVER_SUBJECT_SWEEP_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(MOVER_SUBJECT_SWEEP_TEST_BIN)

compound-swim-oracle-test: $(COMPOUND_SWIM_ORACLE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_SWIM_ORACLE_TEST_BIN)

compound-hook-oracle-test: $(COMPOUND_HOOK_ORACLE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(COMPOUND_HOOK_ORACLE_TEST_BIN)

rune-door-scope-test: $(RUNE_DOOR_SCOPE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(RUNE_DOOR_SCOPE_TEST_BIN)

rune-door-frontier-test: tests/sg_rune_door_frontier_test.c \
		tests/test_rune_door_frontier_integration.py \
		slipgate/sg_rune_door_frontier.c slipgate/sg_rune_door_frontier.h
	$(E) [TEST-CC] $@
	$(Q)$(CC) $(filter-out -MMD,$(CFLAGS)) -std=c11 -Wall -Wextra \
		-Werror -Wpedantic -I. -o /tmp/sg_rune_door_frontier_test.make \
		tests/sg_rune_door_frontier_test.c \
		slipgate/sg_rune_door_frontier.c -lm
	$(E) [TEST] $@
	$(Q)/tmp/sg_rune_door_frontier_test.make
	$(Q)python3 -B tests/test_rune_door_frontier_integration.py

entfile-test: $(ENTFILE_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(ENTFILE_TEST_BIN)

maplist-rotation-test: $(MAPLIST_ROTATION_TEST_BIN)
	$(E) [TEST] $<
	$(Q)./$(MAPLIST_ROTATION_TEST_BIN)

snapshot-test:
	$(E) [TEST] $(ENGINE_SNAPSHOT_TEST)
	$(Q)./$(ENGINE_SNAPSHOT_TEST)

clean:
	$(E) [CLEAN]
	$(Q)$(RM) *.o *.d $(OBJS) $(OBJS:.o=.d) $(TARGET) $(REVISION_HEADER) \
		$(REVISION_HEADER).tmp.* $(HOST_TEST_ALL_ARTIFACTS) \
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
		$(COMPOUND_SWIM_GAME_TEST_ALL_ARTIFACTS) \
		$(ROCKETJUMP_TEST_ALL_ARTIFACTS) \
		$(COMPOUND_DROP_TEST_ALL_ARTIFACTS) \
		$(COMPOUND_HOOK_TEST_ALL_ARTIFACTS) \
		$(COMPOUND_HOOK_GAME_EVENTS_ALL_ARTIFACTS) \
		$(COMPOUND_SWIM_ORACLE_TEST_ALL_ARTIFACTS) \
		$(COMPOUND_HOOK_ORACLE_TEST_ALL_ARTIFACTS) \
		$(MOVER_LEASE_TEST_ALL_ARTIFACTS) \
		$(WATER_FOREST_TEST_ALL_ARTIFACTS) \
		$(BUTTON_LIVE_TEST_ALL_ARTIFACTS) \
		$(BUTTON_GAME_TEST_ALL_ARTIFACTS) \
		$(COMPOUND_GUARD_TEST_ALL_ARTIFACTS) \
		$(COMPOUND_GUARD_GAME_TEST_ALL_ARTIFACTS) \
		$(DECLARED_DOOR_GUARD_TEST_ALL_ARTIFACTS) \
		$(MOVER_SUBJECT_SWEEP_TEST_ALL_ARTIFACTS) \
		$(SPECTATOR_SOUND_TEST_ALL_ARTIFACTS) \
		$(HUMAN_SPEED_TEST_ALL_ARTIFACTS) \
		$(HUMAN_TRACE_HOOK_TEST_ALL_ARTIFACTS) \
		$(DOOR_APPROACH_TEST_ALL_ARTIFACTS) \
		$(DEFENSE_SHIFT_TEST_ALL_ARTIFACTS) \
		$(DEFENSE_SUPPLY_TEST_ALL_ARTIFACTS) \
		$(STRIKE_ADAPTER_TEST_ALL_ARTIFACTS) \
		$(ITEM_COMMITMENT_TEST_ALL_ARTIFACTS) \
		$(HOOK_DIAGNOSTICS_TEST_ALL_ARTIFACTS) \
		$(HOOK_DISCIPLINE_TEST_ALL_ARTIFACTS) \
		$(RUN_HANDOFF_TEST_ALL_ARTIFACTS) \
		$(RUNE_OBJECTIVE_DIAGNOSTICS_TEST_ALL_ARTIFACTS) \
		$(RUNE_DOOR_SCOPE_TEST_ALL_ARTIFACTS) sg_rune_late_path_test.make \
		sg_human_hook_ownership_test.make \
		sg_rune_update_source_test.make \
		sg_rune_topology_test.make sg_rune_learning_test.make \
		sg_rune_learning_game_test.make sg_rune_hook_nomination_test.make

strip: $(TARGET)
	$(E) [STRIP]
	$(Q)$(STRIP) $(TARGET)

# Third-party SQLite amalgamation: own rule, warnings off, single-threaded.
SQLITE_CFLAGS = -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION=1 \
                -DSQLITE_DEFAULT_MEMSTATUS=0 -w

sqlite3.o: sqlite3.c
	$(CC) $(CFLAGS) $(SHLIBCFLAGS) $(SQLITE_CFLAGS) -o $@ -c $<
