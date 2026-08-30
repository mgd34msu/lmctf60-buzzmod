/* Immutable publication of the accepted production host laws. */
#ifndef SG_HOST_LAW_PUBLICATION_H
#define SG_HOST_LAW_PUBLICATION_H

#include <stdint.h>

/* g_local.h exports `world` as a macro; it must not rewrite the authority
 * field or BSP API while these public types are included. */
#ifdef world
#define SG_HOST_LAW_RESTORE_WORLD_MACRO
#undef world
#endif
#include "sg_host_collision.h"
#include "sg_host_engine_pmove.h"
#include "sg_host_engine_runtime.h"
#include "sg_host_hook_law.h"
#include "sg_host_mechanism_law.h"
#ifdef SG_HOST_LAW_RESTORE_WORLD_MACRO
#define world (&g_edicts[0])
#undef SG_HOST_LAW_RESTORE_WORLD_MACRO
#endif

#define SG_HOST_LAW_PUBLICATION_VERSION UINT32_C(3)
#define SG_HOST_LAW_ELEMENT_NONE UINT32_MAX

typedef struct sg_host_law_publication_s sg_host_law_publication_t;
typedef struct sg_host_law_construction_s sg_host_law_construction_t;

/* Both engine-backed publications carry these exact upstream inputs for the
 * downstream model seal.  They do not claim a complete-model or RUNE
 * identity. */
typedef struct sg_host_law_view_s
{
	uint32_t version;
	uint32_t reserved;
	uint64_t collision_law_id;
	uint64_t pmove_law_id;
	uint64_t gravity_law_id;
	uint64_t hook_law_id;
	uint64_t mechanism_law_id;
	/* Exact SHA-256 of the retained engine-selected BSP bytes. */
	sg_bsp_content_identity_t bsp_identity;
	uint64_t bsp_bytes;
	/* Host-owned inputs to the downstream complete-model seal.  The
	 * complete-model identity remains absent until that later seal. */
	sg_host_static_identity_t static_identity;
	/* Present only on the legacy controller-backed test publication. */
	sg_rune_model_identity_t identity;
	sg_host_engine_pmove_abi_t pmove_abi;
	uint64_t pmove_behavior_fingerprint;
	float airaccelerate;
	float maxvelocity;
	uint32_t movement_flags;
	uint32_t physics_flags;
	uint32_t hook_fire_speed;
	uint32_t hook_pull_speed;
	uint32_t hook_initial_damage;
	uint32_t hook_attached_damage;
	uint32_t hook_health;
	sg_host_hook_law_t hook;
	sg_host_mechanism_law_t mechanism;
} sg_host_law_view_t;

typedef enum sg_host_law_status_e
{
	SG_HOST_LAW_OK = 0,
	SG_HOST_LAW_INVALID_ARGUMENT,
	SG_HOST_LAW_HOST_UNAVAILABLE,
	SG_HOST_LAW_UNSUPPORTED_PRODUCTION_LAW,
	SG_HOST_LAW_ALLOCATION_FAILED,
	SG_HOST_LAW_CORRUPT_PUBLICATION,
	SG_HOST_LAW_PRODUCTION_DRIFT,
	SG_HOST_LAW_EVALUATION_FAILED
} sg_host_law_status_t;

typedef enum sg_host_law_field_e
{
	SG_HOST_LAW_FIELD_NONE = 0,
	SG_HOST_LAW_FIELD_VERSION,
	SG_HOST_LAW_FIELD_COLLISION_LAW,
	SG_HOST_LAW_FIELD_PMOVE_LAW,
	SG_HOST_LAW_FIELD_PMOVE_ABI,
	SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR,
	SG_HOST_LAW_FIELD_GRAVITY_LAW,
	SG_HOST_LAW_FIELD_HOOK_LAW,
	SG_HOST_LAW_FIELD_MECHANISM_LAW,
	SG_HOST_LAW_FIELD_BSP_CONTENT,
	SG_HOST_LAW_FIELD_ENTITY_SEMANTICS,
	SG_HOST_LAW_FIELD_PHYSICS_ABI,
	SG_HOST_LAW_FIELD_SCHEMA,
	SG_HOST_LAW_FIELD_SOURCE_SET,
	SG_HOST_LAW_FIELD_PRODUCER,
	SG_HOST_LAW_FIELD_STANDING_HULL_MINS,
	SG_HOST_LAW_FIELD_STANDING_HULL_MAXS,
	SG_HOST_LAW_FIELD_CROUCHING_HULL_MINS,
	SG_HOST_LAW_FIELD_CROUCHING_HULL_MAXS,
	SG_HOST_LAW_FIELD_GRAVITY,
	SG_HOST_LAW_FIELD_GROUND_ACCELERATION,
	SG_HOST_LAW_FIELD_AIR_ACCELERATION,
	SG_HOST_LAW_FIELD_WATER_ACCELERATION,
	SG_HOST_LAW_FIELD_HOOK_ACCELERATION,
	SG_HOST_LAW_FIELD_EXTERNAL_ACCELERATION,
	SG_HOST_LAW_FIELD_WATER_DRAG,
	SG_HOST_LAW_FIELD_MODEL_MAX_VELOCITY,
	SG_HOST_LAW_FIELD_FRAME_MS,
	SG_HOST_LAW_FIELD_SUBSTEP_MS,
	SG_HOST_LAW_FIELD_AIRACCELERATE,
	SG_HOST_LAW_FIELD_MAXVELOCITY,
	SG_HOST_LAW_FIELD_MOVEMENT_FLAGS,
	SG_HOST_LAW_FIELD_PHYSICS_FLAGS,
	SG_HOST_LAW_FIELD_HOOK_FIRE_SPEED,
	SG_HOST_LAW_FIELD_HOOK_PULL_SPEED,
	SG_HOST_LAW_FIELD_HOOK_INITIAL_DAMAGE,
	SG_HOST_LAW_FIELD_HOOK_ATTACHED_DAMAGE,
	SG_HOST_LAW_FIELD_HOOK_HEALTH,
	SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY,
	SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS
} sg_host_law_field_t;

typedef struct sg_host_law_result_s
{
	sg_host_law_status_t status;
	sg_host_law_field_t field;
	uint32_t element;
	uint32_t reserved;
	uint64_t expected_bits;
	uint64_t observed_bits;
} sg_host_law_result_t;

/* Pointer-free facts copied from the handle-owned loader parse.  These are
 * descriptive metadata only; collision and BSP storage never cross the
 * construction boundary. */
typedef struct sg_host_law_construction_geometry_s
{
	sg_bsp_content_identity_t bsp_identity;
	uint64_t bsp_bytes;
	uint32_t engine_checksum;
	uint32_t entity_bytes;
	uint32_t plane_count;
	uint32_t node_count;
	uint32_t texinfo_count;
	uint32_t leaf_count;
	uint32_t leaf_brush_count;
	uint32_t model_count;
	uint32_t brush_count;
	uint32_t brush_side_count;
} sg_host_law_construction_geometry_t;

typedef struct sg_host_law_construction_view_s
{
	uint32_t version;
	uint32_t current;
	uint64_t level_generation;
	/* This is the complete identity authenticated by the live host at this
	 * layer: BSP SHA/size/checksum and engine physics.  Downstream entity,
	 * schema, source-set, and producer identities are deliberately absent. */
	sg_host_static_identity_t host_static_identity;
	sg_host_law_construction_geometry_t geometry;
	sg_host_law_view_t laws;
} sg_host_law_construction_view_t;

sg_host_law_result_t SG_HostLawPublicationRead(
	const sg_host_law_publication_t *publication,
	sg_host_law_view_t *view_out);
sg_host_law_result_t SG_HostLawPublicationMatch(
	const sg_host_law_publication_t *publication,
	const sg_host_law_view_t *expected);
sg_host_law_result_t SG_HostLawPublicationRevalidateProduction(
	const sg_host_law_publication_t *publication);

/* Borrow the collision authority owned by this publication.  The returned
 * view is read-only and is valid only until the publication is destroyed;
 * callers must reacquire it after every level transition. */
sg_host_law_result_t SG_HostLawPublicationCollisionAuthority(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_authority_t **authority_out);

/* Execute the captured laws.  None of these functions accepts a callback. */
sg_host_law_result_t SG_HostLawPublicationCollisionTrace(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out);
sg_host_law_result_t SG_HostLawPublicationPmove(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out);
/* An issued construction handle owns its loader-parsed BSP and sealed law
 * state.  Consumers never pair it with a raw publication or authority. */
sg_host_law_result_t SG_HostLawConstructionCurrent(
	const sg_host_law_construction_t *construction);
sg_host_law_result_t SG_HostLawConstructionRead(
	const sg_host_law_construction_t *construction,
	sg_host_law_construction_view_t *view_out);
/* Collision operations remain construction-bound so neither an authority nor
 * its owned BSP arrays can be substituted or mutated by a consumer. */
sg_host_law_result_t SG_HostLawConstructionCollisionTrace(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out);
sg_host_law_result_t SG_HostLawConstructionPointContents(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float point[3],
	sg_host_collision_contents_t *contents_out);
sg_host_law_result_t SG_HostLawConstructionClassifyPose(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out);
sg_host_law_result_t SG_HostLawConstructionTransition(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float end[3], sg_rune_stance_t stance,
	sg_host_collision_transition_t *transition_out);
sg_host_law_result_t SG_HostLawConstructionPmove(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out);
/* SG_HostPmoveReplayFrame owns the fixed frame/substep loop.  This wrapper
 * copy-outs its complete authenticated substep and trace chronology without
 * exposing the captured Pmove callback.  A capacity error is retryable with a
 * larger caller-owned workspace. */
sg_host_law_result_t SG_HostLawConstructionReplayFrame(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out);
void SG_HostLawConstructionDestroy(sg_host_law_construction_t *construction);
sg_host_law_result_t SG_HostLawPublicationHookPullVelocity(
	const sg_host_law_publication_t *publication, const vec3_t start,
	const vec3_t bite, vec3_t velocity, int *rope_length_out);
sg_host_law_result_t SG_HostLawPublicationHookMuzzle(
	const sg_host_law_publication_t *publication, const float origin[3],
	float viewheight, int hand, const float forward[3], const float right[3],
	float start_out[3]);
#ifdef SG_HOST_LAW_TESTING
sg_host_law_result_t SG_HostLawPublicationHookStep(
	const sg_host_law_publication_t *publication,
	const sg_host_hook_observation_t *observation,
	sg_host_hook_step_t *step_out);
/* Spawn/link and trace the bolt through the publication-owned collision
 * authority before applying the immediate first-hit/attach/abort chronology.
 * The request contains only owner phase/button and trace endpoints; target,
 * sky, team, and hit fields are derived internally. */
sg_host_law_result_t SG_HostLawPublicationHookFire(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_scene_t *scene,
	const sg_host_hook_fire_request_t *request,
	sg_host_hook_step_t *step_out);
#endif

sg_host_law_result_t SG_HostLawPublicationMoveSchedule(
	const sg_host_law_publication_t *publication, float distance, float speed,
	float accel, float decel, int current_entity,
	sg_host_mechanism_move_result_t *result_out);
sg_host_law_result_t SG_HostLawPublicationDoorStep(
	const sg_host_law_publication_t *publication,
	sg_host_mechanism_door_event_t event, uint32_t flags, int state,
	float wait_seconds, uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_transition_t *result_out);
sg_host_law_result_t SG_HostLawPublicationDoorStepEx(
	const sg_host_law_publication_t *publication,
	sg_host_mechanism_door_event_t event, uint32_t flags, int state,
	float wait_seconds, uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_blocker_kind_t blocker_kind, uint32_t damage,
	sg_host_mechanism_transition_t *result_out);
sg_host_law_result_t SG_HostLawPublicationPlatformStep(
	const sg_host_law_publication_t *publication,
	sg_host_mechanism_platform_event_t event, int state, uint64_t now_ms,
	uint64_t debounce_until_ms, sg_host_mechanism_transition_t *result_out);
sg_host_law_result_t SG_HostLawPublicationPlatformStepEx(
	const sg_host_law_publication_t *publication,
	sg_host_mechanism_platform_event_t event, int state, uint64_t now_ms,
	uint64_t debounce_until_ms, sg_host_mechanism_blocker_kind_t blocker_kind,
	uint32_t damage, sg_host_mechanism_transition_t *result_out);
sg_host_law_result_t SG_HostLawPublicationTriggerStep(
	const sg_host_law_publication_t *publication, int already_triggered,
	float wait_seconds, uint64_t now_ms,
	sg_host_mechanism_transition_t *result_out);
sg_host_law_result_t SG_HostLawPublicationTrainStep(
	const sg_host_law_publication_t *publication,
	sg_host_mechanism_train_event_t event, uint32_t flags, float wait_seconds,
	int state, int has_target, int has_current_target,
	sg_host_mechanism_blocker_kind_t blocker_kind, uint32_t damage,
	uint64_t now_ms, uint64_t debounce_until_ms,
	sg_host_mechanism_transition_t *result_out);

const char *SG_HostLawStatusString(sg_host_law_status_t status);
const char *SG_HostLawFieldString(sg_host_law_field_t field);

#endif /* SG_HOST_LAW_PUBLICATION_H */
