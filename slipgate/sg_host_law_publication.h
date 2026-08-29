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
#include "sg_host_pmove.h"
#ifdef SG_HOST_LAW_RESTORE_WORLD_MACRO
#define world (&g_edicts[0])
#undef SG_HOST_LAW_RESTORE_WORLD_MACRO
#endif

#define SG_HOST_LAW_PUBLICATION_VERSION UINT32_C(2)
#define SG_HOST_LAW_ELEMENT_NONE UINT32_MAX

typedef struct sg_host_law_publication_s sg_host_law_publication_t;

/*
 * The view is deliberately made from the canonical model identity.  The
 * identity contains the BSP, entity semantics, physics ABI, source set,
 * producer, hulls, and every physics parameter consumed by the adapters.
 * There is no reduced identity which a caller could accidentally compare in
 * place of the model identity.
 */
typedef struct sg_host_law_view_s
{
	uint32_t version;
	uint32_t reserved;
	uint64_t collision_law_id;
	uint64_t pmove_law_id;
	uint64_t gravity_law_id;
	uint64_t hook_law_id;
	uint64_t mechanism_law_id;
	sg_rune_model_identity_t identity;
	float airaccelerate;
	float maxvelocity;
	uint32_t movement_flags;
	uint32_t physics_flags;
	uint32_t hook_fire_speed;
	uint32_t hook_pull_speed;
	uint32_t hook_initial_damage;
	uint32_t hook_attached_damage;
	uint32_t hook_health;
	uint32_t action_contract_crc32;
	uint32_t mechanism_contract_crc32;
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
	SG_HOST_LAW_FIELD_ACTION_CONTRACT,
	SG_HOST_LAW_FIELD_MECHANISM_CONTRACT
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

/*
 * Issue only from an initialized collision authority.  The authority owns
 * the canonical identity and binds the immutable BSP; callers cannot provide
 * a replacement collision, Pmove, or hook callback through this API.
 */
sg_host_law_result_t SG_HostLawPublicationIssue(
	const sg_host_collision_authority_t *authority,
	sg_host_law_publication_t **publication_out);

sg_host_law_result_t SG_HostLawPublicationRead(
	const sg_host_law_publication_t *publication,
	sg_host_law_view_t *view_out);
sg_host_law_result_t SG_HostLawPublicationMatch(
	const sg_host_law_publication_t *publication,
	const sg_host_law_view_t *expected);
sg_host_law_result_t SG_HostLawPublicationRevalidateProduction(
	const sg_host_law_publication_t *publication);

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
sg_host_law_result_t SG_HostLawPublicationHookPullVelocity(
	const sg_host_law_publication_t *publication, const vec3_t start,
	const vec3_t bite, vec3_t velocity, int *rope_length_out);

void SG_HostLawPublicationDestroy(sg_host_law_publication_t *publication);

const char *SG_HostLawStatusString(sg_host_law_status_t status);
const char *SG_HostLawFieldString(sg_host_law_field_t field);

#endif /* SG_HOST_LAW_PUBLICATION_H */
