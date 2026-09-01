/* Authenticated runtime localization into exact-bound RUNE phase space. */
#ifndef SG_CELL_PHASE_LOCALIZATION_H
#define SG_CELL_PHASE_LOCALIZATION_H

#include <stddef.h>
#include <stdint.h>

/* g_local.h exports `world` as a macro; it must not rewrite the BSP authority
 * declarations reached through the public localization types. */
#ifdef world
#define SG_CELL_PHASE_RESTORE_WORLD_MACRO
#undef world
#endif
#include "sg_configuration_semantics.h"
#include "sg_destination.h"
#include "sg_host_law_owner.h"
#include "sg_localization_runtime.h"
#ifdef SG_CELL_PHASE_RESTORE_WORLD_MACRO
#define world (&g_edicts[0])
#undef SG_CELL_PHASE_RESTORE_WORLD_MACRO
#endif

/* Bindings are in the same canonical order as semantic regions. The artifact
 * integration layer supplies this relation; neither input implies it. */
typedef struct sg_localization_region_binding_s
{
	uint64_t semantic_region_id;
	sg_rune_cell_ref_t rune_cell;
	uint32_t runtime_region;
	uint32_t reserved;
} sg_localization_region_binding_t;

typedef struct sg_localization_workspace_s
{
	uint32_t *cell_region_offsets;
	size_t cell_region_offset_capacity;
	uint32_t *region_indices;
	size_t region_index_capacity;
	uint32_t *region_runtime_cells;
	size_t region_runtime_cell_capacity;
	uint32_t *region_runtime_regions;
	size_t region_runtime_region_capacity;
	uint32_t *cell_portal_offsets;
	size_t cell_portal_offset_capacity;
	uint32_t *cell_portal_cursors;
	size_t cell_portal_cursor_capacity;
	/* Two entries per undirected configuration portal. */
	uint32_t *portal_indices;
	size_t portal_index_capacity;
	uint32_t *stance_overlap_offsets;
	size_t stance_overlap_offset_capacity;
	uint32_t *stance_overlap_cursors;
	size_t stance_overlap_cursor_capacity;
	/* Two entries per standing/crouching overlap. */
	uint32_t *stance_overlap_indices;
	size_t stance_overlap_index_capacity;
	uint32_t *phase_transition_offsets;
	size_t phase_transition_offset_capacity;
	uint32_t *phase_transition_cursors;
	size_t phase_transition_cursor_capacity;
	uint32_t *phase_transition_indices;
	size_t phase_transition_index_capacity;
	uint32_t *phase_kernel_offsets;
	size_t phase_kernel_offset_capacity;
	uint32_t *phase_kernel_cursors;
	size_t phase_kernel_cursor_capacity;
	uint32_t *phase_kernel_indices;
	size_t phase_kernel_index_capacity;
} sg_localization_workspace_t;

/* Runtime-safe view of the phase basis frozen into one accepted RUNE model.
 * Offline phase construction and audit own how these records are produced;
 * localization retains only the immutable runtime artifact and its exact
 * identity. */
typedef struct sg_localization_phase_view_s
{
	const sg_rune_model_t *model;
	const sg_rune_phase_basis_t *phases;
	const sg_rune_phase_transition_t *transitions;
	sg_rune_model_identity_t identity;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint32_t cell_count;
	uint32_t phase_count;
	uint32_t transition_count;
	uint32_t region_count;
	sg_rune_model_flags_t flags;
	sg_rune_completeness_t completeness;
} sg_localization_phase_view_t;

/* Prepared once after an audited configuration, semantics set, and runtime
 * snapshot have been bound. The caller snapshot is consumed only during
 * preparation; runtime phase coordinates come from the retained immutable
 * model view. */
typedef struct sg_cell_phase_locator_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	sg_localization_phase_view_t phase_view;
	const uint32_t *cell_region_offsets;
	const uint32_t *region_indices;
	const uint32_t *region_runtime_cells;
	const uint32_t *region_runtime_regions;
	const uint32_t *cell_portal_offsets;
	const uint32_t *portal_indices;
	const uint32_t *stance_overlap_offsets;
	const uint32_t *stance_overlap_indices;
	const uint32_t *phase_transition_offsets;
	const uint32_t *phase_transition_indices;
	const uint32_t *phase_kernel_offsets;
	const uint32_t *phase_kernel_indices;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint32_t configuration_cell_count;
	uint32_t semantic_region_count;
	uint32_t runtime_cell_count;
	uint32_t runtime_phase_count;
	uint32_t configuration_portal_count;
	uint32_t configuration_stance_overlap_count;
	uint32_t runtime_phase_transition_count;
	uint32_t runtime_kernel_count;
	uint64_t prepare_cell_steps;
	uint64_t prepare_region_steps;
	uint64_t prepare_binding_checks;
	uint64_t prepare_runtime_cell_comparisons;
	uint64_t prepare_portal_steps;
	uint64_t prepare_portal_adjacency_steps;
	uint64_t prepare_phase_transition_steps;
	uint64_t prepare_transition_lookup_comparisons;
	uint64_t prepare_kernel_steps;
	uint64_t prepare_kernel_lookup_comparisons;
} sg_cell_phase_locator_t;

typedef struct sg_localization_observation_s
{
	uint8_t authenticated;
	uint8_t reserved[3];
	sg_localization_observation_kind_t kind;
	sg_rune_stance_t stance;
	sg_localization_subject_t subject;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t frame_sequence;
	uint64_t observed_at_ms;
	uint64_t authenticated_at_ms;
	float position[3];
	float velocity[3];
	pmove_state_t host_state;
} sg_localization_observation_t;

typedef struct sg_localization_environment_s
{
	uint8_t authenticated;
	uint8_t reserved[7];
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t frame_sequence;
	uint64_t sampled_at_ms;
	uint64_t authenticated_at_ms;
	const sg_host_pmove_request_t *pmove_request;
	sg_host_pmove_substep_t *replay_substeps;
	size_t replay_substep_capacity;
	sg_host_pmove_trace_t *replay_traces;
	size_t replay_trace_capacity;
} sg_localization_environment_t;

/* The runtime retains the exact engine physics/collision publication accepted
 * at construction. Every localization operation revalidates it through the
 * owner. Moving-mechanism continuity remains unavailable until opaque
 * mechanism, entity-semantics, mover, and transform-timeline publications are
 * accepted. */
typedef struct sg_cell_phase_runtime_s
{
	const sg_cell_phase_locator_t *locator;
	sg_host_law_runtime_authority_t host_authority;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint8_t prepared;
	uint8_t mover_authority_ready;
	uint8_t reserved[6];
} sg_cell_phase_runtime_t;

typedef struct sg_localized_player_state_s sg_localized_player_state_t;

typedef struct sg_localization_request_s
{
	sg_localization_subject_t expected_subject;
	uint64_t now_ms;
	uint64_t minimum_frame_sequence;
	uint64_t max_observation_age_ms;
	/* A prior state requests continuity proof. The distance only bounds
	 * numeric-drift recovery outside exact published cells; exact path
	 * continuity has no caller-selected distance cap. Temporary absence uses
	 * the duration. */
	const sg_localized_player_state_t *previous;
	float maximum_recovery_distance;
	uint32_t reserved;
	uint64_t maximum_temporary_absence_ms;
} sg_localization_request_t;

struct sg_localized_player_state_s
{
	sg_destination_pose_t field_pose;
	sg_localization_subject_t subject;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t frame_sequence;
	uint64_t localized_at_ms;
	uint64_t phase_started_at_ms;
	uint64_t phase_elapsed_ms;
	uint64_t time_quantum_index;
	uint32_t configuration_cell;
	uint32_t semantic_region;
	uint32_t runtime_region;
	sg_rune_stance_t stance;
	sg_rune_motion_t motion;
	sg_rune_support_t support;
	sg_rune_medium_t medium;
	sg_rune_void_relation_t void_relation;
	sg_rune_reference_frame_t reference_frame;
	sg_rune_mechanism_ref_t mover;
	float phase_velocity[3];
	float reference_velocity[3];
	uint8_t water_level;
	uint8_t reserved[3];
	sg_host_collision_contents_t water_type;
	uint32_t support_model_index;
	uint64_t support_instance_id;
	pmove_state_t host_state;
	uint8_t host_state_valid;
	uint8_t reserved3[3];
	sg_localization_recovery_t recovery;
	uint32_t portal_candidates_examined;
	uint32_t phase_transition_candidates_examined;
	uint32_t kernel_candidates_examined;
	uint32_t reserved2;
};

int SG_CellPhaseLocatorPrepare(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_localization_region_binding_t *bindings,
	size_t binding_count, sg_localization_workspace_t *workspace,
	sg_cell_phase_locator_t *locator_out,
	sg_localization_status_t *status_out);

int SG_CellPhaseRuntimePrepare(const sg_cell_phase_locator_t *locator,
	sg_cell_phase_runtime_t *runtime_out,
	sg_localization_status_t *status_out);

/* Production callers may retain a runtime and its latest output only while
 * the complete owner-issued authority chain remains current.  These queries
 * do not localize, allocate, or refresh stale storage. */
int SG_CellPhaseRuntimeCurrent(const sg_cell_phase_runtime_t *runtime);
int SG_CellPhaseLocalizedStateCurrent(
	const sg_cell_phase_runtime_t *runtime,
	const sg_localization_subject_t *subject,
	const sg_localized_player_state_t *state);

int SG_CellPhaseLocalize(const sg_cell_phase_runtime_t *runtime,
	const sg_localization_request_t *request,
	const sg_localization_observation_t *observation,
	const sg_localization_environment_t *environment,
	sg_localized_player_state_t *state_out,
	sg_localization_status_t *status_out);

const char *SG_LocalizationStatusString(sg_localization_status_t status);

#endif /* SG_CELL_PHASE_LOCALIZATION_H */
