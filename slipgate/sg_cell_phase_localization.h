/* Authenticated runtime localization into exact-bound RUNE phase space. */
#ifndef SG_CELL_PHASE_LOCALIZATION_H
#define SG_CELL_PHASE_LOCALIZATION_H

#include <stddef.h>
#include <stdint.h>

#include "sg_configuration_semantics.h"
#include "sg_destination_field.h"

#define SG_LOCALIZATION_SUPPORT_MODEL_NONE UINT32_MAX

typedef enum sg_localization_status_e
{
	SG_LOCALIZATION_OK = 0,
	SG_LOCALIZATION_INVALID_ARGUMENT,
	SG_LOCALIZATION_INVALID_BINDING,
	SG_LOCALIZATION_CAPACITY,
	SG_LOCALIZATION_UNAUTHENTICATED,
	SG_LOCALIZATION_IDENTITY_MISMATCH,
	SG_LOCALIZATION_STALE,
	SG_LOCALIZATION_NONFINITE,
	SG_LOCALIZATION_SOLID,
	SG_LOCALIZATION_OUTSIDE_CONFIGURATION,
	SG_LOCALIZATION_NO_SEMANTIC_REGION,
	SG_LOCALIZATION_AMBIGUOUS_INPUT,
	SG_LOCALIZATION_MOVER_UNBOUND,
	SG_LOCALIZATION_NO_PHASE,
	SG_LOCALIZATION_RECOVERY_PARAMETER,
	SG_LOCALIZATION_RECOVERY_REJECTED,
	SG_LOCALIZATION_RESET_REQUIRED
} sg_localization_status_t;

typedef enum sg_localization_observation_kind_e
{
	SG_LOCALIZATION_OBSERVATION_PRESENT = 0,
	SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT,
	SG_LOCALIZATION_OBSERVATION_DEAD,
	SG_LOCALIZATION_OBSERVATION_TELEPORTED,
	SG_LOCALIZATION_OBSERVATION_NEW_SPAWN,
	SG_LOCALIZATION_OBSERVATION_KIND_COUNT
} sg_localization_observation_kind_t;

typedef enum sg_localization_recovery_e
{
	SG_LOCALIZATION_RECOVERY_NONE = 0,
	SG_LOCALIZATION_RECOVERY_EXACT_CONTINUITY,
	SG_LOCALIZATION_RECOVERY_NUMERIC_DRIFT,
	SG_LOCALIZATION_RECOVERY_TEMPORARY_ABSENCE
} sg_localization_recovery_t;

typedef struct sg_localization_subject_s
{
	uint32_t client_id;
	uint32_t reserved;
	uint64_t spawn_generation;
} sg_localization_subject_t;

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

/* Prepared once after an audited configuration, semantics set, and runtime
 * snapshot have been bound. All referenced storage remains caller-owned. */
typedef struct sg_cell_phase_locator_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_rune_runtime_snapshot_t *snapshot;
	const uint32_t *cell_region_offsets;
	const uint32_t *region_indices;
	const uint32_t *region_runtime_cells;
	const uint32_t *region_runtime_regions;
	const uint32_t *cell_portal_offsets;
	const uint32_t *portal_indices;
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
	uint64_t phase_started_at_ms;
	float position[3];
	float velocity[3];
} sg_localization_observation_t;

typedef struct sg_localization_mover_s
{
	uint8_t authenticated;
	uint8_t reserved[7];
	uint64_t sampled_at_ms;
	uint64_t instance_id;
	uint32_t model_index;
	uint32_t reserved2;
	sg_rune_mechanism_ref_t mechanism;
	float velocity[3];
} sg_localization_mover_t;

typedef struct sg_localization_environment_s
{
	uint8_t authenticated;
	uint8_t reserved[7];
	uint64_t sampled_at_ms;
	const sg_host_collision_scene_t *scene;
	const sg_localization_mover_t *movers;
	size_t mover_count;
} sg_localization_environment_t;

typedef struct sg_localized_player_state_s sg_localized_player_state_t;

typedef struct sg_localization_request_s
{
	sg_localization_subject_t expected_subject;
	uint64_t now_ms;
	uint64_t minimum_frame_sequence;
	uint64_t max_observation_age_ms;
	/* A prior state requests continuity proof. Present observations use the
	 * geometric distance; temporary absence uses the duration. */
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

int SG_CellPhaseLocalize(const sg_cell_phase_locator_t *locator,
	const sg_localization_request_t *request,
	const sg_localization_observation_t *observation,
	const sg_localization_environment_t *environment,
	sg_localized_player_state_t *state_out,
	sg_localization_status_t *status_out);

const char *SG_LocalizationStatusString(sg_localization_status_t status);

#endif /* SG_CELL_PHASE_LOCALIZATION_H */
