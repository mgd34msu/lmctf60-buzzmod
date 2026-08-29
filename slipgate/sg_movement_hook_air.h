/* Hook and airborne capability consumer contract.
 *
 * Construction stays unavailable until production hook-visibility and host-law
 * publications exist. The synthetic feasibility catalog is deliberately not
 * an input to this API. */
#ifndef SG_MOVEMENT_HOOK_AIR_H
#define SG_MOVEMENT_HOOK_AIR_H

#include <stdint.h>

#include "../q_shared.h"
#include "sg_bsp_completeness_proof.h"
#include "sg_configuration_semantics.h"

#define SG_MOVEMENT_HOOK_AIR_INDEX_NONE UINT32_MAX
#define SG_MOVEMENT_HOOK_AIR_COMMAND_MAGNITUDE INT16_C(400)

/* These publications are owned by the visibility and host integrators. They
 * remain opaque here so a caller cannot manufacture acceptance counters or
 * substitute function pointers for production laws. */
typedef struct sg_hook_visibility_production_publication_s
	sg_hook_visibility_production_publication_t;
typedef struct sg_host_law_production_publication_s
	sg_host_law_production_publication_t;

typedef enum sg_movement_hook_air_error_code_e
{
	SG_MOVEMENT_HOOK_AIR_ERROR_NONE = 0,
	SG_MOVEMENT_HOOK_AIR_ERROR_INVALID_ARGUMENT,
	SG_MOVEMENT_HOOK_AIR_ERROR_INVALID_SOURCE,
	SG_MOVEMENT_HOOK_AIR_ERROR_IDENTITY_MISMATCH,
	SG_MOVEMENT_HOOK_AIR_ERROR_INCOMPLETE_BSP_PROOF,
	SG_MOVEMENT_HOOK_AIR_ERROR_INCOMPLETE_SEMANTICS_PROOF,
	SG_MOVEMENT_HOOK_AIR_ERROR_VISIBILITY_PREREQUISITE_UNAVAILABLE,
	SG_MOVEMENT_HOOK_AIR_ERROR_HOST_LAW_PREREQUISITE_UNAVAILABLE,
	SG_MOVEMENT_HOOK_AIR_ERROR_INVALID_PHASE,
	SG_MOVEMENT_HOOK_AIR_ERROR_HOST_DISAGREEMENT,
	SG_MOVEMENT_HOOK_AIR_ERROR_OVERFLOW,
	SG_MOVEMENT_HOOK_AIR_ERROR_OUT_OF_MEMORY
} sg_movement_hook_air_error_code_t;

typedef struct sg_movement_hook_air_error_s
{
	sg_movement_hook_air_error_code_t code;
	uint32_t source_index;
	sg_bsp_completeness_code_t bsp_code;
	sg_configuration_semantics_audit_code_t semantics_code;
} sg_movement_hook_air_error_t;

typedef enum sg_movement_hook_air_kind_e
{
	SG_MOVEMENT_HOOK_AIR_BOLT_FLIGHT = 0,
	SG_MOVEMENT_HOOK_AIR_ATTACH,
	SG_MOVEMENT_HOOK_AIR_PULL,
	SG_MOVEMENT_HOOK_AIR_RELEASE,
	SG_MOVEMENT_HOOK_AIR_COAST,
	SG_MOVEMENT_HOOK_AIR_CONTROL,
	SG_MOVEMENT_HOOK_AIR_FALL,
	SG_MOVEMENT_HOOK_AIR_LAND,
	SG_MOVEMENT_HOOK_AIR_RELAUNCH,
	SG_MOVEMENT_HOOK_AIR_KIND_COUNT
} sg_movement_hook_air_kind_t;

typedef enum sg_movement_hook_state_e
{
	SG_MOVEMENT_HOOK_FREE = 0,
	SG_MOVEMENT_HOOK_BOLT_IN_FLIGHT,
	SG_MOVEMENT_HOOK_ATTACHED,
	SG_MOVEMENT_HOOK_STATE_COUNT
} sg_movement_hook_state_t;

typedef enum sg_movement_air_direction_e
{
	SG_MOVEMENT_AIR_DIRECTION_NONE = 0,
	SG_MOVEMENT_AIR_DIRECTION_POSITIVE_X,
	SG_MOVEMENT_AIR_DIRECTION_NEGATIVE_X,
	SG_MOVEMENT_AIR_DIRECTION_POSITIVE_Y,
	SG_MOVEMENT_AIR_DIRECTION_NEGATIVE_Y,
	SG_MOVEMENT_AIR_DIRECTION_COUNT
} sg_movement_air_direction_t;

typedef uint32_t sg_movement_hook_air_flags_t;
enum
{
	SG_MOVEMENT_HOOK_AIR_DIRECTIONAL = UINT32_C(1) << 0,
	SG_MOVEMENT_HOOK_AIR_VISIBILITY_PROVEN = UINT32_C(1) << 1,
	SG_MOVEMENT_HOOK_AIR_HOST_PMOVE_PROVEN = UINT32_C(1) << 2,
	SG_MOVEMENT_HOOK_AIR_BODY_SWEEP_PROVEN = UINT32_C(1) << 3,
	SG_MOVEMENT_HOOK_AIR_SOURCE_SUPPORTED = UINT32_C(1) << 4,
	SG_MOVEMENT_HOOK_AIR_SOURCE_AIRBORNE = UINT32_C(1) << 5,
	SG_MOVEMENT_HOOK_AIR_DESTINATION_SUPPORTED = UINT32_C(1) << 6,
	SG_MOVEMENT_HOOK_AIR_DESTINATION_AIRBORNE = UINT32_C(1) << 7,
	SG_MOVEMENT_HOOK_AIR_VOID_ADJACENT = UINT32_C(1) << 8,
	SG_MOVEMENT_HOOK_AIR_STANCE_CROUCHING = UINT32_C(1) << 9
};

typedef struct sg_movement_hook_air_phase_binding_s
{
	uint32_t semantic_region;
	uint32_t phase;
} sg_movement_hook_air_phase_binding_t;

/* A production visibility partition owns the exact player-origin polytope and
 * control fiber. semantic_region trims that polytope to audited configuration
 * semantics. No axis-aligned witness box is promoted to proof authority. */
typedef struct sg_movement_hook_air_visibility_partition_ref_s
{
	uint64_t publication_identity;
	uint32_t partition;
	uint32_t semantic_region;
} sg_movement_hook_air_visibility_partition_ref_t;

typedef struct sg_movement_hook_air_sources_s
{
	const sg_host_collision_authority_t *collision;
	const sg_host_collision_scene_t *scene;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_hook_visibility_production_publication_t *visibility;
	const sg_host_law_production_publication_t *host_laws;
	const sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	const sg_movement_hook_air_phase_binding_t *bindings;
	uint32_t binding_count;
} sg_movement_hook_air_sources_t;

typedef struct sg_movement_hook_air_fact_s
{
	uint32_t order;
	sg_movement_hook_air_kind_t kind;
	sg_movement_hook_state_t source_hook_state;
	sg_movement_hook_state_t destination_hook_state;
	sg_movement_air_direction_t air_direction;
	uint32_t source_cell;
	uint32_t destination_cell;
	uint32_t source_region;
	uint32_t destination_region;
	uint32_t source_phase;
	uint32_t destination_phase;
	sg_movement_hook_air_visibility_partition_ref_t visibility_partition;
	uint64_t surface_id;
	sg_rune_vec3_t source_witness;
	sg_rune_vec3_t muzzle_witness;
	sg_rune_vec3_t attachment_witness;
	sg_rune_vec3_t destination_witness;
	sg_rune_vec3_t source_velocity;
	sg_rune_vec3_t observed_velocity;
	sg_rune_vec3_t command_vector;
	uint32_t rope_length;
	/* Completed player-body frames before the host attachment event. The
	 * attachment frame belongs to attach/pull chronology and is not replayed as
	 * outbound flight. */
	uint32_t preattach_frames;
	sg_rune_kernel_parameters_t parameters;
	sg_movement_hook_air_flags_t flags;
} sg_movement_hook_air_fact_t;

typedef struct sg_movement_hook_air_set_s
{
	sg_rune_model_identity_t identity;
	sg_movement_hook_air_fact_t *facts;
	uint32_t fact_count;
	uint32_t covered_regions;
	uint64_t host_pmove_frames;
} sg_movement_hook_air_set_t;

int SG_MovementHookAirBuild(const sg_movement_hook_air_sources_t *sources,
	sg_movement_hook_air_set_t **set_out,
	sg_movement_hook_air_error_t *error_out);
void SG_MovementHookAirDestroy(sg_movement_hook_air_set_t *set);
const char *SG_MovementHookAirErrorString(
	sg_movement_hook_air_error_code_t code);

/* Exact fixed-yaw world-axis commands used by the eventual production
 * constructor. Quake's yaw-zero right vector points toward negative Y. */
int SG_MovementHookAirCommandForDirection(
	sg_movement_air_direction_t direction, usercmd_t *command_out,
	sg_rune_vec3_t *world_vector_out);

/* Convert bolt travel to completed outbound player-body frames before the
 * attachment event. The cadence frame containing the impact is the attachment
 * frame, matching SG_HookReplayBegin's flight_ms - one-frame contract. */
int SG_MovementHookAirFlightFrameCount(float distance, float bolt_speed,
	uint32_t frame_ms, uint32_t *frames_out);

#endif
