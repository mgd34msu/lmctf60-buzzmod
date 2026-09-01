#ifndef SG_RUNE_COMPACT_BUILDER_OWNER_H
#define SG_RUNE_COMPACT_BUILDER_OWNER_H

#include "sg_bsp_entity_semantics.h"
#include "sg_rune_compact_builder.h"
#include "sg_rune_compact_geometry.h"
#include "sg_rune_compact_mechanisms.h"

/* Host pose classification treats exact hull contact as occupied.  A carried
 * support pose therefore raises the player origin by one Q8 unit above the
 * mathematical hull bottom; all producers and replay validators share this
 * convention. */
#define SG_RUNE_COMPACT_SUPPORT_CLEARANCE_Q8 INT32_C(1)

typedef struct sg_rune_compact_builder_owner_view_s
{
	sg_rune_compact_identity_t identity;
	const sg_bsp_world_t *world;
	const sg_host_collision_authority_t *collision;
	const sg_host_law_view_t *host_law;
	const sg_rune_source_weapon_law_t *weapon_law;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_bsp_entity_semantics_t *entity_semantics;
	const sg_static_visibility_t *visibility;
} sg_rune_compact_builder_owner_view_t;

int SG_RuneCompactBuilderOwnerRead(
	const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out);
sg_host_law_result_t SG_RuneCompactBuilderOwnerPmove(
	const sg_rune_compact_builder_t *builder,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out);
sg_host_law_result_t SG_RuneCompactBuilderOwnerReplayFrame(
	const sg_rune_compact_builder_t *builder,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out,
	sg_host_pmove_error_t *error_out);
/* Resolve one canonical brush mover from the builder-owned, current entity
 * semantics and transform model-local Q8 vertices with collision's exact
 * angle-axis/origin law.  The transform itself never leaves this boundary.
 * world_bounds_out is the conservative AABB of the returned world vertices.
 * This is geometry evaluation only; a caller must still certify any movement
 * transition through SG_RuneCompactBuilderOwnerPmove and its collision scene. */
sg_host_law_result_t SG_RuneCompactBuilderOwnerTransformModelLocalQ8(
	const sg_rune_compact_builder_t *builder, uint32_t mover_entity_ordinal,
	const sg_rune_q8_vec3_t *local_vertices, uint32_t vertex_count,
	sg_rune_vec3_t *world_vertices_out, sg_rune_bounds_t *world_bounds_out);

/* Replays one local Q8 pose through collision's exact forward transform for
 * a current authenticated brush mover.  transform must be the canonical
 * finite transform carried by a successful mover result; this is a witness
 * rederivation helper, not a movement or collision certificate. */
sg_host_law_result_t SG_RuneCompactBuilderOwnerReplayLocalQ8Pose(
	const sg_rune_compact_builder_t *builder, uint32_t mover_entity_ordinal,
	const sg_host_collision_world_transform_t *transform,
	const sg_rune_q8_vec3_t *local_pose, sg_rune_vec3_t *world_pose_out);

/* The mover boundary distinguishes a portal-state condition from an actual
 * carried-support transport.  It deliberately accepts compact catalog
 * ordinals and returns host-derived facts; transforms and movement commands
 * do not cross this boundary. */
typedef enum sg_rune_compact_builder_mover_mode_e
{
	SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE = 0,
	SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT,
	SG_RUNE_COMPACT_BUILDER_MOVER_MODE_COUNT
} sg_rune_compact_builder_mover_mode_t;

typedef enum sg_rune_compact_builder_mover_failure_e
{
	SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE = 0,
	SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_BLOCKED,
	SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_CRUSHED,
	SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING,
	SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_COUNT
} sg_rune_compact_builder_mover_failure_t;

typedef enum sg_rune_compact_builder_support_pose_mode_e
{
	/* player_local_pose and support_local_pose are authenticated request data. */
	SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_EXPLICIT = 0,
	/* The host derives the sole canonical Q8 pose accepted for the supplied
	 * root under its own collision and hull law. */
	SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_CANONICAL,
	SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_MODE_COUNT
} sg_rune_compact_builder_support_pose_mode_t;

typedef struct sg_rune_compact_builder_mover_request_s
{
	sg_rune_compact_builder_mover_mode_t mode;
	/* PORTAL_STATE normally certifies the selected mover alone.  A nonzero
	 * team_portal instead binds the stock TEAM master below; the host derives
	 * every member itself, so a caller cannot omit a blocking panel. */
	int team_portal;
	uint32_t team_master_entity_ordinal;
	uint32_t mover_entity_ordinal;
	sg_rune_compact_mechanism_authority_state_t source_state;
	sg_rune_compact_mechanism_authority_state_t destination_state;
	/* Indexes the authenticated geometry source-surface catalog. */
	uint32_t source_surface_ordinal;
	/* PORTAL_STATE selects one catalog portal; CARRIED_SUPPORT sets this to
	 * SG_RUNE_COMPACT_INDEX_NONE. */
	uint32_t portal_ordinal;
	sg_rune_compact_cell_index_t entry_cell;
	sg_rune_compact_cell_index_t exit_cell;
	/* A train names its authenticated source and destination path endpoints.
	 * Other movers set both to SG_RUNE_COMPACT_INDEX_NONE. */
	uint32_t source_endpoint_entity_ordinal;
	uint32_t destination_endpoint_entity_ordinal;
	/* The selected source-path-corner -> destination-path-corner TARGET
	 * fanout.  It makes the concrete train branch part of the host proof;
	 * all non-trains set SG_RUNE_COMPACT_INDEX_NONE. */
	uint32_t route_fanout_ordinal;
	sg_rune_compact_builder_support_pose_mode_t support_pose_mode;
	/* Caller-owned storage for exact binary32 transforms of the authenticated
	 * catalog root.  PORTAL_STATE requires both arrays; carried-support callers
	 * may omit both after asking only for pose certification. */
	sg_rune_vec3_t *source_world_vertices_out;
	sg_rune_vec3_t *destination_world_vertices_out;
	uint32_t world_vertex_capacity;
	/* The player hull origin in the mover model's Q8-local frame.  It is used
	 * only for explicit CARRIED_SUPPORT. */
	sg_rune_q8_vec3_t player_local_pose;
	/* Authenticated contact point on source_surface_ordinal in the same local
	 * Q8 frame.  It is mandatory for explicit CARRIED_SUPPORT. */
	sg_rune_q8_vec3_t support_local_pose;
	sg_rune_stance_t stance;
} sg_rune_compact_builder_mover_request_t;

typedef struct sg_rune_compact_builder_mover_result_s
{
	sg_rune_compact_builder_mover_mode_t mode;
	int team_portal;
	/* Echoes the authenticated canonical TEAM root only when team_portal is
	 * true; selected mover_model/surface remain the requested panel's payload
	 * provenance. */
	uint32_t team_master_entity_ordinal;
	/* A non-applicable catalog root is a normal candidate miss.  A caller may
	 * use the remaining proof fields only when applicable is true and failure
	 * is NONE. */
	int applicable;
	sg_rune_compact_mechanism_authority_state_t source_state;
	sg_rune_compact_mechanism_authority_state_t destination_state;
	sg_rune_stance_t stance;
	uint32_t mover_model;
	uint32_t source_surface_ordinal;
	uint32_t portal_ordinal;
	uint32_t source_endpoint_entity_ordinal;
	uint32_t destination_endpoint_entity_ordinal;
	uint32_t route_fanout_ordinal;
	sg_rune_compact_cell_index_t entry_cell;
	sg_rune_compact_cell_index_t exit_cell;
	sg_rune_q8_vec3_t approach_witness;
	sg_rune_q8_vec3_t entry_witness;
	sg_rune_q8_vec3_t exit_witness;
	sg_rune_q8_vec3_t source_player_local;
	sg_rune_q8_vec3_t destination_player_local;
	sg_rune_q8_vec3_t source_support_local;
	sg_rune_q8_vec3_t destination_support_local;
	/* Exact binary32 endpoint values.  A downstream serializer may make Q8
	 * witnesses only after proving exact representability. */
	sg_rune_vec3_t source_player_world;
	sg_rune_vec3_t destination_player_world;
	sg_rune_vec3_t source_support_world;
	sg_rune_vec3_t destination_support_world;
	/* Exact collision-law origin/basis transforms that produced the carried
	 * endpoint world poses. They are meaningful only for certified
	 * CARRIED_SUPPORT results and never require reader-side trigonometry. */
	sg_host_collision_world_transform_t source_mover_transform;
	sg_host_collision_world_transform_t destination_mover_transform;
	sg_rune_bounds_t source_surface_world_bounds;
	sg_rune_bounds_t destination_surface_world_bounds;
	uint32_t source_vertex_count;
	uint64_t elapsed_ms;
	/* Carried support is publishable only when this oracle proved both endpoint
	 * contacts in addition to the static sweep.  Portal-state results leave
	 * these clear because they carry no player hull. */
	/* PORTAL_STATE records occupancy of one strict interior portal patch at
	 * each authenticated endpoint.  A successful portal certificate requires
	 * these to differ, proving a real collision/connectivity state change. */
	int source_portal_blocked;
	int destination_portal_blocked;
	int start_supported;
	int end_supported;
	int swept_static_clear;
	sg_rune_compact_builder_mover_failure_t failure;
} sg_rune_compact_builder_mover_result_t;

/* Revalidates the builder and geometry identities, source-surface/model
 * ownership, portal incidence (for PORTAL_STATE), and the stock mover
 * schedule/carry law (for CARRIED_SUPPORT).  Any non-NONE failure is a hard
 * no-certification result, never a fallback transport. */
sg_host_law_result_t SG_RuneCompactBuilderOwnerMoverTransport(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_builder_mover_request_t *request,
	sg_rune_compact_builder_mover_result_t *result_out);

#if defined(SG_COMPACT_BUILDER_TEST_HOOKS)
/* Test-only allocation-boundary fault injection for the private train-route
 * traversal.  It is absent from production builds. */
void SG_RuneCompactBuilderTestFailNextTrainRouteAllocation(void);
#endif

#endif
