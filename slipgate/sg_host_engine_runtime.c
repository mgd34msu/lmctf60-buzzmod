#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world

#include "sg_host_engine_runtime.h"
#include "sg_host_engine_runtime_private.h"
#include "sg_host_engine_pmove.h"
#include "sg_rune.h"

extern rune_t *SG_Rune(void);

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

extern game_import_t gi;
extern game_export_t globals;

#define SG_HOST_ENGINE_RUNTIME_STATE UINT32_C(0x45525433)
#define SG_HOST_ENGINE_RUNTIME_STATE_INVERSE UINT32_C(0xb8adbccc)
#define SG_HOST_RUNE_SHA256_HEX_BYTES 64U

typedef trace_t (*sg_host_engine_trace_function_t)(vec3_t start,
	vec3_t mins, vec3_t maxs, vec3_t end, edict_t *passent, int contentmask);
typedef int (*sg_host_engine_contents_function_t)(vec3_t point);

struct sg_host_engine_runtime_s
{
	uint32_t state;
	uint32_t state_inverse;
	const void *owner;
	sg_host_engine_trace_function_t trace;
	sg_host_engine_contents_function_t pointcontents;
	sg_host_pmove_function_t pmove;
	sg_level_identity_t level;
	char mapname[SG_LEVEL_IDENTITY_MAPNAME_BYTES];
	int accepted;
	const rune_t *accepted_rune;
	rune_artifact_t accepted_artifact;
	const sg_bsp_world_t *accepted_world;
	char accepted_rune_sha256[SG_HOST_RUNE_SHA256_HEX_BYTES + 1U];
	sg_rune_model_identity_t identity;
	sg_bsp_content_identity_t content_identity;
	uint64_t generation;
	uint64_t topology_revision;
	edict_t *subject;
	uint32_t subject_index;
	const gclient_t *subject_client;
	int subject_number;
};

typedef struct sg_host_engine_runtime_scope_s
{
	const sg_host_engine_runtime_t *runtime;
	int collision_failed;
} sg_host_engine_runtime_scope_t;

static sg_host_engine_runtime_scope_t *sg_host_engine_runtime_scope;

static int FiniteVector(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static int IdentityValid(const sg_rune_model_identity_t *identity)
{
	uint32_t axis;

	if (!identity || identity->bsp_content_id == 0U ||
		identity->bsp_content_id == UINT64_MAX ||
		identity->entity_semantics_id == 0U ||
		identity->entity_semantics_id == UINT64_MAX ||
		identity->physics_abi_id == 0U ||
		identity->physics_abi_id == UINT64_MAX ||
		identity->source_set_identity == 0U ||
		identity->source_set_identity == UINT64_MAX ||
		identity->schema_id == 0U || identity->schema_id == UINT64_MAX ||
		identity->producer_identity == 0U ||
		identity->producer_identity == UINT64_MAX)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!isfinite(identity->standing_hull.mins.value[axis]) ||
			!isfinite(identity->standing_hull.maxs.value[axis]) ||
			!isfinite(identity->crouching_hull.mins.value[axis]) ||
			!isfinite(identity->crouching_hull.maxs.value[axis]) ||
			identity->standing_hull.mins.value[axis] >=
				identity->standing_hull.maxs.value[axis] ||
			identity->crouching_hull.mins.value[axis] >=
				identity->crouching_hull.maxs.value[axis])
			return 0;
	}
	return isfinite(identity->physics.gravity) &&
		isfinite(identity->physics.ground_acceleration) &&
		isfinite(identity->physics.air_acceleration) &&
		isfinite(identity->physics.water_acceleration) &&
		isfinite(identity->physics.hook_acceleration) &&
		isfinite(identity->physics.external_acceleration) &&
		isfinite(identity->physics.water_drag) &&
		isfinite(identity->physics.max_velocity) &&
		identity->physics.gravity >= 0.0f &&
		identity->physics.ground_acceleration >= 0.0f &&
		identity->physics.air_acceleration >= 0.0f &&
		identity->physics.water_acceleration >= 0.0f &&
		identity->physics.hook_acceleration >= 0.0f &&
		identity->physics.external_acceleration >= 0.0f &&
		identity->physics.water_drag >= 0.0f &&
		identity->physics.max_velocity > 0.0f &&
		identity->physics.frame_ms != 0U &&
		identity->physics.substep_ms != 0U;
}

static int ContentIdentityValid(const sg_bsp_content_identity_t *identity)
{
	uint32_t index;
	int any = 0;

	if (!identity)
		return 0;
	for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
	{
		if (identity->bytes[index] != 0U)
			any = 1;
	}
	return any;
}

static int RuneSHA256Valid(const char *sha256)
{
	uint32_t index;

	if (!sha256 || strlen(sha256) != SG_HOST_RUNE_SHA256_HEX_BYTES)
		return 0;
	for (index = 0U; index < SG_HOST_RUNE_SHA256_HEX_BYTES; index++)
		if (!((sha256[index] >= '0' && sha256[index] <= '9') ||
			(sha256[index] >= 'a' && sha256[index] <= 'f')))
			return 0;
	return 1;
}

static uint64_t RuneSHA256Fingerprint(const char *sha256, uint64_t seed)
{
	uint32_t index;
	uint64_t value = seed;

	for (index = 0U; index < SG_HOST_RUNE_SHA256_HEX_BYTES; index++)
		value = (value ^ (uint8_t)sha256[index]) * UINT64_C(1099511628211);
	return value == 0U ? UINT64_C(1) : value;
}

static uint64_t FingerprintByte(uint64_t value, uint8_t octet)
{
	return (value ^ octet) * UINT64_C(1099511628211);
}

static uint64_t FingerprintU16(uint64_t value, uint16_t input)
{
	value = FingerprintByte(value, (uint8_t)input);
	return FingerprintByte(value, (uint8_t)(input >> 8U));
}

static uint64_t FingerprintU32(uint64_t value, uint32_t input)
{
	uint32_t index;

	for (index = 0U; index < 4U; index++)
		value = FingerprintByte(value, (uint8_t)(input >> (index * 8U)));
	return value;
}

static uint64_t FingerprintI16(uint64_t value, int16_t input)
{
	return FingerprintU16(value, (uint16_t)input);
}

static uint64_t FingerprintI32(uint64_t value, int32_t input)
{
	return FingerprintU32(value, (uint32_t)input);
}

static uint64_t FingerprintFloat(uint64_t value, float input)
{
	uint32_t bits;

	memcpy(&bits, &input, sizeof(bits));
	return FingerprintU32(value, bits);
}

static uint64_t FingerprintVector(uint64_t value, const float input[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		value = FingerprintFloat(value, input[axis]);
	return value;
}

static uint64_t FingerprintBytes(uint64_t value, const uint8_t *input,
	size_t bytes)
{
	size_t index;

	if (!input && bytes != 0U)
		return 0U;
	for (index = 0U; index < bytes; index++)
		value = FingerprintByte(value, input[index]);
	return value;
}

static uint64_t FingerprintSeed(uint64_t value, const rune_seed_t *seed)
{
	value = FingerprintVector(value, seed->origin);
	value = FingerprintI16(value, seed->area_hint);
	return FingerprintI16(value, seed->flags);
}

static uint64_t FingerprintLink(uint64_t value, const rune_link_t *link)
{
	value = FingerprintI32(value, (int32_t)link->from);
	value = FingerprintI32(value, (int32_t)link->to);
	value = FingerprintByte(value, link->action);
	value = FingerprintByte(value, link->provenance);
	value = FingerprintByte(value, link->min_speed);
	value = FingerprintByte(value, link->heading);
	value = FingerprintByte(value, link->heading_slack);
	value = FingerprintByte(value, link->exit_speed);
	value = FingerprintI16(value, link->cost_ms);
	value = FingerprintVector(value, link->anchor);
	value = FingerprintVector(value, link->mechanism_anchor);
	value = FingerprintU16(value, link->sweep_clear_ms);
	value = FingerprintByte(value, link->mode);
	return FingerprintU32(value, link->mechanism_plan);
}

static uint64_t FingerprintMechanismNode(uint64_t value,
	const rune_mechanism_node_t *node)
{
	uint32_t axis;

	value = FingerprintU32(value, node->key);
	value = FingerprintU16(value, node->kind);
	value = FingerprintU16(value, node->flags);
	value = FingerprintU32(value, node->classname_offset);
	value = FingerprintU32(value, node->target_offset);
	value = FingerprintU32(value, node->targetname_offset);
	value = FingerprintU32(value, node->killtarget_offset);
	value = FingerprintU32(value, node->owner_key);
	value = FingerprintU32(value, node->team_master_key);
	value = FingerprintU32(value, node->spawnflags);
	value = FingerprintU16(value, node->touch_callback);
	value = FingerprintU16(value, node->use_callback);
	value = FingerprintU16(value, node->think_callback);
	value = FingerprintU16(value, node->blocked_callback);
	value = FingerprintI32(value, node->delay_ms);
	value = FingerprintI32(value, node->wait_ms);
	value = FingerprintU32(value, node->speed_q8);
	value = FingerprintU32(value, node->accel_q8);
	value = FingerprintU32(value, node->decel_q8);
	for (axis = 0U; axis < 3U; axis++)
		value = FingerprintI16(value, node->absmin_q8[axis]);
	for (axis = 0U; axis < 3U; axis++)
		value = FingerprintI16(value, node->absmax_q8[axis]);
	value = FingerprintU32(value, node->path_target_offset);
	return FingerprintVector(value, node->push_velocity);
}

static uint64_t FingerprintMechanismEdge(uint64_t value,
	const rune_mechanism_edge_t *edge)
{
	value = FingerprintU32(value, edge->from_key);
	value = FingerprintU32(value, edge->to_key);
	value = FingerprintU16(value, edge->kind);
	value = FingerprintU16(value, edge->ordinal);
	return FingerprintU32(value, edge->delay_ms);
}

static uint64_t FingerprintMechanismPlan(uint64_t value,
	const rune_mechanism_plan_t *plan)
{
	value = FingerprintU32(value, plan->entry_key);
	value = FingerprintU32(value, plan->mover_key);
	value = FingerprintU32(value, plan->first_edge);
	value = FingerprintU32(value, plan->num_edges);
	value = FingerprintU16(value, plan->controller_kind);
	value = FingerprintU16(value, plan->flags);
	value = FingerprintU16(value, plan->expected_members);
	value = FingerprintU32(value, plan->cooldown_ms);
	return FingerprintU32(value, plan->closure_crc32);
}

/* The encoded SHA is the immutable file generation.  This second fingerprint
 * is deliberately over the live graph arrays as well: changing topology
 * while leaving all public counts and a stale encoded digest untouched must
 * invalidate the owner binding. */
static uint64_t RuneTopologyFingerprint(const rune_t *rune, uint64_t seed)
{
	const rune_artifact_t *artifact;
	uint32_t index;
	uint64_t value = seed;

	if (!rune || !SG_RunePublishedShapeValid(rune) ||
		rune->artifact.num_seeds == 0U || !rune->seeds || !rune->first_link ||
		!rune->linked_seed ||
		(rune->artifact.num_links != 0U &&
			(!rune->links || !rune->next_link)) ||
		(rune->artifact.num_mechanism_nodes != 0U && !rune->mechanism_nodes) ||
		(rune->artifact.num_mechanism_edges != 0U && !rune->mechanism_edges) ||
		(rune->artifact.num_mechanism_plans != 0U && !rune->mechanism_plans) ||
		!rune->mechanism_strings)
		return 0U;
	artifact = &rune->artifact;
	/* Encode every field in a fixed little-endian order.  Native padding and
	 * host integer layout are not part of the accepted topology identity. */
	value = FingerprintU32(value, artifact->num_seeds);
	for (index = 0U; index < artifact->num_seeds; index++)
	{
		value = FingerprintSeed(value, &rune->seeds[index]);
		value = FingerprintI32(value, (int32_t)rune->first_link[index]);
		value = FingerprintByte(value, rune->linked_seed[index]);
	}
	value = FingerprintU32(value, artifact->num_links);
	for (index = 0U; index < artifact->num_links; index++)
	{
		value = FingerprintLink(value, &rune->links[index]);
		value = FingerprintI32(value, (int32_t)rune->next_link[index]);
	}
	value = FingerprintU32(value, artifact->num_mechanism_nodes);
	for (index = 0U; index < artifact->num_mechanism_nodes; index++)
		value = FingerprintMechanismNode(value, &rune->mechanism_nodes[index]);
	value = FingerprintU32(value, artifact->num_mechanism_edges);
	for (index = 0U; index < artifact->num_mechanism_edges; index++)
		value = FingerprintMechanismEdge(value, &rune->mechanism_edges[index]);
	value = FingerprintU32(value, artifact->num_mechanism_plans);
	for (index = 0U; index < artifact->num_mechanism_plans; index++)
		value = FingerprintMechanismPlan(value, &rune->mechanism_plans[index]);
	value = FingerprintU32(value, artifact->string_bytes);
	value = FingerprintBytes(value, rune->mechanism_strings,
		(size_t)artifact->string_bytes);
	return value == 0U ? UINT64_C(1) : value;
}

static int RuntimeShapeValid(const sg_host_engine_runtime_t *runtime)
{
	return runtime && runtime->state == SG_HOST_ENGINE_RUNTIME_STATE &&
		runtime->state_inverse == SG_HOST_ENGINE_RUNTIME_STATE_INVERSE &&
		runtime->owner == (const void *)&gi && runtime->trace &&
		runtime->pointcontents && runtime->pmove && runtime->mapname[0] != '\0';
}

static int LevelEqual(const sg_level_identity_t *left,
	const sg_level_identity_t *right)
{
	return left && right && left->bsp_checksum == right->bsp_checksum &&
		left->entity_crc32 == right->entity_crc32 &&
		left->host_physics_id == right->host_physics_id &&
		memcmp(left->mapname, right->mapname,
			SG_LEVEL_IDENTITY_MAPNAME_BYTES) == 0;
}

static int RuntimeCallbacksCurrent(const sg_host_engine_runtime_t *runtime)
{
	return RuntimeShapeValid(runtime) &&
		runtime->trace == (sg_host_engine_trace_function_t)gi.trace &&
		runtime->pointcontents ==
			(sg_host_engine_contents_function_t)gi.pointcontents &&
		runtime->pmove == (sg_host_pmove_function_t)gi.Pmove;
}

static int RuntimeSubjectCurrent(const sg_host_engine_runtime_t *runtime)
{
	const edict_t *subject;

	if (!RuntimeShapeValid(runtime) || !runtime->subject ||
		!globals.edicts || globals.num_edicts <= 0 ||
		runtime->subject_index >= (uint32_t)globals.num_edicts)
		return 0;
	subject = &globals.edicts[runtime->subject_index];
	return subject == runtime->subject && subject->inuse && subject->client &&
		subject->client->pers.connected && subject->classname &&
		strcmp(subject->classname, "player") == 0 &&
		subject->s.number == runtime->subject_number &&
		subject->client == runtime->subject_client;
}

static void RuntimeClearSubject(sg_host_engine_runtime_t *runtime)
{
	runtime->subject = NULL;
	runtime->subject_index = 0U;
	runtime->subject_client = NULL;
	runtime->subject_number = 0;
}

sg_host_engine_runtime_status_t SG_HostEngineRuntimeBegin(
	const char *mapname, sg_host_engine_runtime_t **runtime_out)
{
	sg_host_engine_runtime_t *runtime;
	sg_level_identity_t level_identity;
	sg_identity_status_t identity_status;

	if (!runtime_out || *runtime_out || !mapname)
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	*runtime_out = NULL;
	identity_status = SG_LevelIdentitySnapshot(mapname, &level_identity);
	if (identity_status != SG_IDENTITY_OK)
		return identity_status == SG_IDENTITY_INVALID_ARGUMENT ||
			identity_status == SG_IDENTITY_INVALID_MAPNAME ?
			SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT :
			SG_HOST_ENGINE_RUNTIME_LEVEL_UNAVAILABLE;
	/* The identity provider is an owner boundary too.  Do not let a provider
	 * that returned a different committed map silently bind callbacks to this
	 * level's runtime. */
	if (strcmp(level_identity.mapname, mapname) != 0)
		return SG_HOST_ENGINE_RUNTIME_LEVEL_UNAVAILABLE;
	if (!gi.trace || !gi.pointcontents || !gi.Pmove)
		return SG_HOST_ENGINE_RUNTIME_HOST_UNAVAILABLE;
	runtime = calloc(1U, sizeof(*runtime));
	if (!runtime)
		return SG_HOST_ENGINE_RUNTIME_ALLOCATION_FAILED;
	runtime->state = SG_HOST_ENGINE_RUNTIME_STATE;
	runtime->state_inverse = SG_HOST_ENGINE_RUNTIME_STATE_INVERSE;
	runtime->owner = (const void *)&gi;
	runtime->trace = (sg_host_engine_trace_function_t)gi.trace;
	runtime->pointcontents =
		(sg_host_engine_contents_function_t)gi.pointcontents;
	runtime->pmove = (sg_host_pmove_function_t)gi.Pmove;
	runtime->level = level_identity;
	memcpy(runtime->mapname, level_identity.mapname, sizeof(runtime->mapname));
	*runtime_out = runtime;
	return SG_HOST_ENGINE_RUNTIME_OK;
}

static uint64_t ContentIdentityWord(const sg_bsp_content_identity_t *identity)
{
	uint64_t value = 0U;
	uint32_t index;

	for (index = 0U; index < sizeof(value); index++)
		value = (value << 8U) | identity->bytes[index];
	return value;
}

static int ActiveArtifactMatchesLevel(const rune_artifact_t *artifact,
	const sg_level_identity_t *level_identity, const char *mapname)
{
	return artifact && level_identity && mapname &&
		artifact->identity.bsp_checksum == level_identity->bsp_checksum &&
		artifact->identity.entity_crc32 == level_identity->entity_crc32 &&
		artifact->identity.host_physics_id == level_identity->host_physics_id &&
		memcmp(artifact->identity.map_name, mapname,
			sizeof(artifact->identity.map_name)) == 0;
}

/* Convert the native RUNE artifact to the one host identity used by both the
 * static construction and live-engine backends.  Every field is derived from
 * the active artifact or retained BSP; no caller can nominate a replacement
 * model identity. */
static int HostIdentityFromActiveArtifact(const rune_artifact_t *artifact,
	const sg_bsp_world_t *world, sg_rune_model_identity_t *identity_out)
{
	sg_rune_hull_profile_t standing_hull;
	sg_rune_hull_profile_t crouching_hull;
	uint32_t axis;

	if (!artifact || !world || !identity_out || !ContentIdentityValid(
		&world->content_identity))
		return 0;
	memset(identity_out, 0, sizeof(*identity_out));
	if (!SG_HostEnginePhysicsLaw(&identity_out->physics))
		return 0;
	identity_out->bsp_content_id = ContentIdentityWord(&world->content_identity);
	identity_out->entity_semantics_id = ((uint64_t)artifact->identity.entity_crc32 <<
		32U) | artifact->identity.host_physics_id;
	identity_out->physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	identity_out->source_set_identity =
		((uint64_t)artifact->action_contract_crc32 << 32U) |
		artifact->mechanism_contract_crc32;
	identity_out->schema_id = ((uint64_t)artifact->magic << 32U) |
		artifact->route_contract;
	identity_out->producer_identity =
		((uint64_t)artifact->header_crc32 << 32U) | artifact->payload_crc32;
	if (!SG_HostEngineHullProfiles(&standing_hull, &crouching_hull))
		return 0;
	identity_out->standing_hull = standing_hull;
	identity_out->crouching_hull = crouching_hull;
	/* Gravity, velocity ceiling, and frame timing come from the live game
	 * owner.  The retained RUNE must match them bit-for-bit; it never supplies
	 * those values to the publication. */
	if (memcmp(&artifact->identity.gravity,
			&identity_out->physics.gravity, sizeof(artifact->identity.gravity)) != 0 ||
		memcmp(&artifact->identity.maxvelocity,
			&identity_out->physics.max_velocity,
			sizeof(artifact->identity.maxvelocity)) != 0 ||
		artifact->identity.airaccelerate != 0.0f ||
		artifact->identity.physics_flags != SG_HOST_ENGINE_PHYSICS_FLAGS ||
		artifact->identity.server_frame_ms != identity_out->physics.frame_ms ||
		artifact->identity.pmove_substep_ms != identity_out->physics.substep_ms)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(identity_out->standing_hull.mins.value[axis]) ||
			!isfinite(identity_out->standing_hull.maxs.value[axis]) ||
			!isfinite(identity_out->crouching_hull.mins.value[axis]) ||
			!isfinite(identity_out->crouching_hull.maxs.value[axis]))
			return 0;
	return IdentityValid(identity_out);
}

void SG_HostEngineRuntimeOwnerClearAcceptance(
	sg_host_engine_runtime_t *runtime)
{
	if (!RuntimeShapeValid(runtime))
		return;
	runtime->accepted = 0;
	runtime->accepted_rune = NULL;
	memset(&runtime->accepted_artifact, 0, sizeof(runtime->accepted_artifact));
	runtime->accepted_world = NULL;
	memset(runtime->accepted_rune_sha256, 0,
		sizeof(runtime->accepted_rune_sha256));
	memset(&runtime->identity, 0, sizeof(runtime->identity));
	memset(&runtime->content_identity, 0, sizeof(runtime->content_identity));
	runtime->generation = 0U;
	runtime->topology_revision = 0U;
	RuntimeClearSubject(runtime);
}

sg_host_engine_runtime_status_t SG_HostEngineRuntimeOwnerInstallActiveRune(
	sg_host_engine_runtime_t *runtime)
{
	rune_t *active;
	const rune_artifact_t *artifact;
	const sg_bsp_world_t *world;
	sg_rune_model_identity_t identity;

	if (!RuntimeShapeValid(runtime))
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	SG_HostEngineRuntimeOwnerClearAcceptance(runtime);
	world = SG_HostLawOwnerRetainedWorld();
	if (!world || !world->source_bytes ||
		world->source_size == 0U || !SG_BspWorldSourceIdentityCurrent(world))
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	if (!RuntimeCallbacksCurrent(runtime))
		return SG_HOST_ENGINE_RUNTIME_DRIFT;
	if (!SG_HostEngineRuntimeCurrent(runtime))
		return SG_HOST_ENGINE_RUNTIME_LEVEL_UNAVAILABLE;
	active = SG_Rune();
	artifact = SG_RuneArtifact(active);
	if (!active || !artifact || !SG_RunePublishedShapeValid(active) ||
		!SG_RunePhysicsCompatible(active))
		return SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY;
	if (!ActiveArtifactMatchesLevel(artifact, &runtime->level,
		runtime->mapname))
		return SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY;
	if (!RuneSHA256Valid(active->encoded_sha256))
		return SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY;
	if (world->engine_checksum != runtime->level.bsp_checksum)
		return SG_HOST_ENGINE_RUNTIME_INVALID_CONTENT_ID;
	if (!HostIdentityFromActiveArtifact(artifact, world, &identity))
		return SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY;
	runtime->accepted_rune = active;
	runtime->accepted_artifact = *artifact;
	runtime->accepted_world = world;
	memcpy(runtime->accepted_rune_sha256, active->encoded_sha256,
		sizeof(runtime->accepted_rune_sha256));
	runtime->identity = identity;
	runtime->content_identity = world->content_identity;
	runtime->generation = RuneSHA256Fingerprint(active->encoded_sha256,
		UINT64_C(1469598103934665603));
	runtime->topology_revision = RuneTopologyFingerprint(active,
		UINT64_C(1099511628211));
	if (runtime->generation == 0U || runtime->topology_revision == 0U)
	{
		SG_HostEngineRuntimeOwnerClearAcceptance(runtime);
		return SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY;
	}
	runtime->accepted = 1;
	return SG_HOST_ENGINE_RUNTIME_OK;
}

sg_host_engine_runtime_status_t SG_HostEngineRuntimeOwnerBindActiveSubject(
	sg_host_engine_runtime_t *runtime, uint32_t subject_index)
{
	edict_t *subject;

	if (!RuntimeShapeValid(runtime))
		return SG_HOST_ENGINE_RUNTIME_NOT_ACCEPTED;
	/* A failed rebind must not leave the preceding bot as the active subject. */
	RuntimeClearSubject(runtime);
	if (!runtime->accepted ||
		!RuntimeCallbacksCurrent(runtime) || !SG_HostEngineRuntimeCurrent(runtime))
		return SG_HOST_ENGINE_RUNTIME_NOT_ACCEPTED;
	if (!globals.edicts || globals.num_edicts <= 0 ||
		subject_index >= (uint32_t)globals.num_edicts)
		return SG_HOST_ENGINE_RUNTIME_HOST_UNAVAILABLE;
	subject = &globals.edicts[subject_index];
	if (!subject->inuse || !subject->client || !subject->client->pers.connected ||
		!subject->classname || strcmp(subject->classname, "player") != 0 ||
		subject->s.number <= 0 ||
		(uint32_t)subject->s.number != subject_index)
		return SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT;
	runtime->subject = subject;
	runtime->subject_index = subject_index;
	runtime->subject_client = subject->client;
	runtime->subject_number = subject->s.number;
	return SG_HOST_ENGINE_RUNTIME_OK;
}

int SG_HostEngineRuntimeCurrent(const sg_host_engine_runtime_t *runtime)
{
	sg_level_identity_t current;

	if (!RuntimeCallbacksCurrent(runtime) ||
		SG_LevelIdentitySnapshot(runtime->mapname, &current) != SG_IDENTITY_OK ||
		!LevelEqual(&runtime->level, &current))
		return 0;
	return 1;
}

int SG_HostEngineRuntimeAccepted(const sg_host_engine_runtime_t *runtime)
{
	const rune_t *active;
	const rune_artifact_t *artifact;

	if (!RuntimeShapeValid(runtime) || !runtime->accepted ||
		!runtime->accepted_rune || !runtime->accepted_world ||
		!runtime->accepted_world->source_bytes ||
		runtime->accepted_world->source_size == 0U ||
		!IdentityValid(&runtime->identity) ||
		!ContentIdentityValid(&runtime->content_identity) ||
		runtime->generation == 0U || runtime->topology_revision == 0U ||
		!RuntimeCallbacksCurrent(runtime) || !SG_HostEngineRuntimeCurrent(runtime) ||
		!RuneSHA256Valid(runtime->accepted_rune_sha256))
		return 0;
	/* Re-read all live owner state.  A copied artifact or a caller's stale
	 * snapshot is never sufficient to keep the runtime admitted. */
	active = SG_Rune();
	artifact = SG_RuneArtifact(active);
	if (!active || active != runtime->accepted_rune || !artifact ||
		!SG_RuneArtifactsEqual(artifact, &runtime->accepted_artifact) ||
		strcmp(active->encoded_sha256, runtime->accepted_rune_sha256) != 0 ||
		!SG_RunePhysicsCompatible(active) ||
		!ActiveArtifactMatchesLevel(artifact, &runtime->level, runtime->mapname) ||
		RuneTopologyFingerprint(active, UINT64_C(1099511628211)) !=
			runtime->topology_revision ||
		!SG_BspWorldSourceIdentityCurrent(runtime->accepted_world) ||
		runtime->accepted_world->engine_checksum != runtime->level.bsp_checksum ||
		memcmp(&runtime->accepted_world->content_identity,
			&runtime->content_identity, sizeof(runtime->content_identity)) != 0)
		return 0;
	return 1;
}

static int TraceArgumentsValid(const float start[3], const float mins[3],
	const float maxs[3], const float end[3])
{
	return FiniteVector(start) && FiniteVector(end) &&
		(!mins || FiniteVector(mins)) && (!maxs || FiniteVector(maxs));
}

static int EntityCurrent(const edict_t *entity, uint32_t *index_out)
{
	int index;

	if (!entity || !index_out || !globals.edicts || globals.num_edicts <= 0)
		return 0;
	/* The engine uses the level world edict (slot zero) as a legitimate trace
	 * and ground identity.  It has no client and is not an ordinary numbered
	 * mover, so authenticate it by the owner array address rather than by the
	 * nonzero entity-number rule used below. */
	if (entity == &globals.edicts[0])
	{
		*index_out = 0U;
		return 1;
	}
	for (index = 0; index < globals.num_edicts; index++)
		if (&globals.edicts[index] == entity)
		{
			if (!entity->inuse || entity->s.number <= 0 ||
				(uint32_t)entity->s.number != (uint32_t)index)
				return 0;
			*index_out = (uint32_t)index;
			return 1;
		}
	return 0;
}

static int TraceConvert(const trace_t *source,
	sg_host_collision_trace_t *destination)
{
	uint32_t entity_index;

	if (!source || !destination || !isfinite(source->fraction) ||
		source->fraction < 0.0f || source->fraction > 1.0f ||
		!FiniteVector(source->endpos) || !FiniteVector(source->plane.normal) ||
		!isfinite(source->plane.dist) || source->contents < 0)
		return 0;
	memset(destination, 0, sizeof(*destination));
	destination->allsolid = source->allsolid ? 1 : 0;
	destination->startsolid = source->startsolid ? 1 : 0;
	destination->fraction = source->fraction;
	memcpy(destination->end, source->endpos, sizeof(destination->end));
	memcpy(destination->plane.normal, source->plane.normal,
		sizeof(destination->plane.normal));
	destination->plane.distance = source->plane.dist;
	destination->plane.type = source->plane.type;
	destination->contents = (sg_host_collision_contents_t)source->contents;
	destination->texinfo = SG_HOST_COLLISION_TEXINFO_NONE;
	destination->surface_flags = source->surface ? source->surface->flags : 0;
	destination->model_index = SG_HOST_COLLISION_MODEL_WORLD;
	destination->instance_id = 0U;
	if (source->ent)
	{
		if (!EntityCurrent(source->ent, &entity_index))
			return 0;
		if (entity_index == 0U)
		{
			/* WORLD is represented by model zero in the normalized API even
			 * though the engine world edict commonly carries model index 1. */
			destination->model_index = SG_HOST_COLLISION_MODEL_WORLD;
			destination->instance_id = 0U;
		}
		else
		{
			if (source->ent->s.modelindex <= 0)
				return 0;
			destination->model_index = (uint32_t)source->ent->s.modelindex;
			destination->instance_id = (uint64_t)entity_index;
		}
	}
	return 1;
}

int SG_HostEngineRuntimeTrace(const sg_host_engine_runtime_t *runtime,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out)
{
	float start_copy[3];
	float mins_copy[3];
	float maxs_copy[3];
	float end_copy[3];
	trace_t trace;

	if (!SG_HostEngineRuntimeAccepted(runtime) ||
		!RuntimeSubjectCurrent(runtime) ||
		!TraceArgumentsValid(start, mins, maxs, end) || !trace_out)
		return 0;
	memcpy(start_copy, start, sizeof(start_copy));
	memcpy(end_copy, end, sizeof(end_copy));
	if (mins)
		memcpy(mins_copy, mins, sizeof(mins_copy));
	if (maxs)
		memcpy(maxs_copy, maxs, sizeof(maxs_copy));
	trace = runtime->trace(start_copy, mins ? mins_copy : NULL,
		maxs ? maxs_copy : NULL, end_copy, runtime->subject, (int)mask);
	return TraceConvert(&trace, trace_out);
}

static sg_host_hook_target_kind_t RuntimeTargetKind(edict_t *entity)
{
	if (!entity)
		return SG_HOST_HOOK_TARGET_NONE;
	if (entity->classname && strcmp(entity->classname, "bodyque") == 0)
		return SG_HOST_HOOK_TARGET_BODYQUE;
	/* Use the same validator and ordering as hook_touch.  Its deathmatch and
	 * team rules are the human hook's definition of an attachable player. */
	if (ctf_validateplayer(entity, CTF_TEAM_ANYTEAM))
		return SG_HOST_HOOK_TARGET_PLAYER;
	if (entity == &globals.edicts[0] || (entity->classname &&
		strcmp(entity->classname, "worldspawn") == 0))
		return SG_HOST_HOOK_TARGET_WORLD;
	if (entity->classname && strncmp(entity->classname, "func", 4) == 0)
		return SG_HOST_HOOK_TARGET_FUNC;
	if (entity->classname && strncmp(entity->classname, "info_flag", 9) == 0)
		return SG_HOST_HOOK_TARGET_INFO_FLAG;
	return SG_HOST_HOOK_TARGET_OTHER;
}

int SG_HostEngineRuntimeHookTrace(const sg_host_engine_runtime_t *runtime,
	const float start[3], const float end[3], sg_host_collision_contents_t mask,
	sg_host_hook_collision_t *collision_out)
{
	trace_t trace;
	float start_copy[3];
	float end_copy[3];
	sg_host_collision_trace_t normalized;
	uint32_t entity_index;
	edict_t *target;

	if (!collision_out || !FiniteVector(start) || !FiniteVector(end) ||
		!SG_HostEngineRuntimeAccepted(runtime) ||
		!RuntimeSubjectCurrent(runtime))
		return 0;
	memset(collision_out, 0, sizeof(*collision_out));
	memcpy(start_copy, start, sizeof(start_copy));
	memcpy(end_copy, end, sizeof(end_copy));
	trace = runtime->trace(start_copy, NULL, NULL, end_copy,
		runtime->subject, (int)mask);
	if (!TraceConvert(&trace, &normalized))
		return 0;
	if (trace.fraction >= 1.0f && !trace.startsolid && !trace.allsolid)
		return 1;
	/* A hit must retain the engine's exact target pointer.  Treating a
	 * normalized model number as an entity class would allow a caller to turn
	 * any mover into a valid FUNC target. */
	if (!trace.ent || !EntityCurrent(trace.ent, &entity_index))
		return 0;
	target = trace.ent;
	collision_out->hit = 1;
	collision_out->owner_hit = target == runtime->subject;
	collision_out->sky = (trace.surface &&
		(trace.surface->flags & SG_HOST_SURFACE_SKY)) != 0;
	collision_out->trace_epsilon_applied = 1;
	if (collision_out->owner_hit)
		return 1;
	collision_out->target_kind = RuntimeTargetKind(target);
	collision_out->target_identity = entity_index == 0U ?
		runtime->identity.bsp_content_id : (uint64_t)entity_index;
	collision_out->target_dead = target->deadflag != DEAD_NO;
	/* hook_touch tests other->client directly after its target whitelist. */
	if (target->client)
		collision_out->same_team = runtime->subject->client->ctf.teamnum ==
			target->client->ctf.teamnum;
	/* T_Damage is deliberately not performed by this query.  The hook state
	 * machine consumes this owner-derived record, and a live weapon owner is
	 * responsible for reporting a post-damage death on its next touch. */
	return 1;
}

int SG_HostEngineRuntimePointContents(
	const sg_host_engine_runtime_t *runtime, const float point[3],
	sg_host_collision_contents_t *contents_out)
{
	float point_copy[3];
	int contents;

	if (!SG_HostEngineRuntimeAccepted(runtime) || !FiniteVector(point) ||
		!contents_out)
		return 0;
	memcpy(point_copy, point, sizeof(point_copy));
	contents = runtime->pointcontents(point_copy);
	if (contents < 0)
		return 0;
	*contents_out = (sg_host_collision_contents_t)contents;
	return 1;
}

static trace_t RuntimePmoveTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end)
{
	trace_t trace;
	sg_host_collision_trace_t host_trace;
	sg_host_engine_runtime_scope_t *scope = sg_host_engine_runtime_scope;

	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	if (!scope || !RuntimeSubjectCurrent(scope->runtime) ||
		!TraceArgumentsValid(start, mins, maxs, end))
	{
		if (scope)
			scope->collision_failed = 1;
		trace.allsolid = true;
		trace.startsolid = true;
		trace.fraction = 0.0f;
		VectorCopy(start, trace.endpos);
		trace.contents = CONTENTS_SOLID;
		return trace;
	}
	/* Keep the engine trace object intact inside the private Pmove scope.  In
	 * particular, `ent` is the engine's authenticated ground/mover identity;
	 * reconstructing a normalized trace here would turn every support into
	 * WORLD and make grounded movement observably wrong. */
	trace = scope->runtime->trace(start, mins, maxs, end,
		scope->runtime->subject, (int)SG_HOST_MASK_PLAYER_SOLID);
	if (!TraceConvert(&trace, &host_trace))
	{
		scope->collision_failed = 1;
		memset(&trace, 0, sizeof(trace));
		trace.fraction = 0.0f;
		VectorCopy(start, trace.endpos);
		trace.allsolid = true;
		trace.startsolid = true;
		trace.contents = CONTENTS_SOLID;
	}
	return trace;
}

static int RuntimePmoveContents(vec3_t point)
{
	sg_host_collision_contents_t contents;

	if (!sg_host_engine_runtime_scope ||
		!SG_HostEngineRuntimePointContents(sg_host_engine_runtime_scope->runtime,
			point, &contents))
	{
		if (sg_host_engine_runtime_scope)
			sg_host_engine_runtime_scope->collision_failed = 1;
		return CONTENTS_SOLID;
	}
	return (int)contents;
}

static int HookGravityActive(const sg_host_pmove_request_t *request)
{
	return request && request->hook_law_id == SG_HOST_PMOVE_HOOK_LAW_ID &&
		request->hook_attached == 1U &&
		request->hook_length < SG_HOST_PMOVE_HOOK_LENGTH_GRAVITY_ZERO;
}

static int RequestGravity(const sg_host_pmove_request_t *request,
	short map_gravity, short *gravity_out)
{
	if (!request || !gravity_out || request->hook_attached > 1U)
		return 0;
	if (request->hook_law_id != 0U &&
		request->hook_law_id != SG_HOST_PMOVE_HOOK_LAW_ID)
		return 0;
	if (request->hook_law_id == 0U && request->hook_attached != 0U)
		return 0;
	*gravity_out = HookGravityActive(request) ? 0 : map_gravity;
	return 1;
}

static int HullMatchesIdentity(const sg_rune_model_identity_t *identity,
	const pmove_t *pmove, short expected_gravity)
{
	const sg_rune_hull_profile_t *hull = (pmove->s.pm_flags & PMF_DUCKED) ?
		&identity->crouching_hull : &identity->standing_hull;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (pmove->mins[axis] != hull->mins.value[axis] ||
			pmove->maxs[axis] != hull->maxs.value[axis])
			return 0;
	return pmove->s.gravity == expected_gravity;
}

int SG_HostEngineRuntimePmove(const sg_host_engine_runtime_t *runtime,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	const sg_rune_model_identity_t *identity;
	sg_host_engine_runtime_scope_t scope;
	pmove_state_t previous;
	pmove_state_t state;
	pmove_t pm;
	short gravity;
	short effective_gravity;
	uint32_t steps;
	uint32_t step;

	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	if (!result_out || !request || request->state.pm_type != PM_NORMAL)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	if (!SG_HostEngineRuntimeAccepted(runtime))
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
		return 0;
	}
	if (!RuntimeSubjectCurrent(runtime))
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
		return 0;
	}
	if (sg_host_engine_runtime_scope)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_REENTRANT;
		return 0;
	}
	identity = &runtime->identity;
	if (identity->physics.substep_ms == 0U ||
		identity->physics.substep_ms > UCHAR_MAX ||
		identity->physics.frame_ms == 0U ||
		identity->physics.frame_ms % identity->physics.substep_ms != 0U)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_UNSUPPORTED_TIMING;
		return 0;
	}
	if (!isfinite(identity->physics.gravity) || identity->physics.gravity < 0.0f ||
		identity->physics.gravity > (float)SHRT_MAX ||
		truncf(identity->physics.gravity) != identity->physics.gravity)
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_UNSUPPORTED_GRAVITY;
		return 0;
	}
	if (!RequestGravity(request, (short)identity->physics.gravity,
		&effective_gravity))
	{
		if (error_out)
			*error_out = SG_HOST_PMOVE_ERROR_IDENTITY_MISMATCH;
		return 0;
	}
	gravity = effective_gravity;
	steps = identity->physics.frame_ms / identity->physics.substep_ms;
	state = request->state;
	previous = request->previous_state;
	memset(result_out, 0, sizeof(*result_out));
	memset(&scope, 0, sizeof(scope));
	scope.runtime = runtime;
	sg_host_engine_runtime_scope = &scope;
	memset(&pm, 0, sizeof(pm));
	for (step = 0U; step < steps; step++)
	{
		memset(&pm, 0, sizeof(pm));
		pm.s = state;
		pm.s.gravity = gravity;
		pm.cmd = request->command;
		pm.cmd.msec = (byte)identity->physics.substep_ms;
		pm.snapinitial = memcmp(&previous, &pm.s, sizeof(pm.s)) != 0;
		pm.trace = RuntimePmoveTrace;
		pm.pointcontents = RuntimePmoveContents;
		if (!RuntimeCallbacksCurrent(runtime))
			break;
		runtime->pmove(&pm);
		if (scope.collision_failed || !HullMatchesIdentity(identity, &pm,
			effective_gravity))
			break;
		state = pm.s;
		previous = pm.s;
	}
	sg_host_engine_runtime_scope = NULL;
	if (step != steps)
	{
		if (error_out)
			*error_out = scope.collision_failed ? SG_HOST_PMOVE_ERROR_COLLISION :
				SG_HOST_PMOVE_ERROR_HOST_UNAVAILABLE;
		return 0;
	}
	result_out->state = pm.s;
	memcpy(result_out->mins, pm.mins, sizeof(result_out->mins));
	memcpy(result_out->maxs, pm.maxs, sizeof(result_out->maxs));
	memcpy(result_out->view_angles, pm.viewangles,
		sizeof(result_out->view_angles));
	result_out->view_height = pm.viewheight;
	result_out->grounded = pm.groundentity != NULL;
	if (pm.groundentity)
	{
		uint32_t ground_index;

		if (!EntityCurrent(pm.groundentity, &ground_index))
		{
			if (error_out)
				*error_out = SG_HOST_PMOVE_ERROR_COLLISION;
			return 0;
		}
		result_out->support_model_index = ground_index == 0U ?
			SG_HOST_COLLISION_MODEL_WORLD :
			(uint32_t)pm.groundentity->s.modelindex;
		if (ground_index != 0U && pm.groundentity->s.modelindex <= 0)
		{
			if (error_out)
				*error_out = SG_HOST_PMOVE_ERROR_COLLISION;
			return 0;
		}
		result_out->support_instance_id = (uint64_t)ground_index;
	}
	for (step = 0U; step < 3U; step++)
	{
		result_out->origin[step] = pm.s.origin[step] * 0.125f;
		result_out->velocity[step] = pm.s.velocity[step] * 0.125f;
	}
	result_out->water_type = pm.watertype;
	result_out->water_level = pm.waterlevel;
	result_out->evaluated_steps = steps;
	result_out->elapsed_ms = identity->physics.frame_ms;
	result_out->gravity = (float)effective_gravity;
	result_out->physics_abi_id = identity->physics_abi_id;
	result_out->gravity_law_id = HookGravityActive(request) ?
		SG_HOST_PMOVE_HOOK_LAW_ID : UINT64_C(0);
	return 1;
}

const sg_rune_model_identity_t *SG_HostEngineRuntimeIdentity(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) ? &runtime->identity : NULL;
}

const sg_bsp_content_identity_t *SG_HostEngineRuntimeContentIdentity(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) ? &runtime->content_identity :
		NULL;
}

uint64_t SG_HostEngineRuntimeGeneration(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) ? runtime->generation : 0U;
}

uint64_t SG_HostEngineRuntimeTopologyRevision(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) ?
		runtime->topology_revision : 0U;
}

uint64_t SG_HostEngineRuntimeSubjectInstance(
	const sg_host_engine_runtime_t *runtime)
{
	return SG_HostEngineRuntimeAccepted(runtime) &&
		RuntimeSubjectCurrent(runtime) ? (uint64_t)runtime->subject_index : 0U;
}

void SG_HostEngineRuntimeDestroy(sg_host_engine_runtime_t *runtime)
{
	if (!runtime)
		return;
	runtime->accepted = 0;
	runtime->state = 0U;
	runtime->state_inverse = 0U;
	runtime->owner = NULL;
	runtime->trace = NULL;
	runtime->pointcontents = NULL;
	runtime->pmove = NULL;
	runtime->accepted_rune = NULL;
	runtime->accepted_world = NULL;
	runtime->subject = NULL;
	runtime->subject_client = NULL;
	free(runtime);
}

const char *SG_HostEngineRuntimeStatusString(
	sg_host_engine_runtime_status_t status)
{
	switch (status)
	{
	case SG_HOST_ENGINE_RUNTIME_OK: return "ok";
	case SG_HOST_ENGINE_RUNTIME_INVALID_ARGUMENT: return "invalid argument";
	case SG_HOST_ENGINE_RUNTIME_HOST_UNAVAILABLE: return "host unavailable";
	case SG_HOST_ENGINE_RUNTIME_LEVEL_UNAVAILABLE: return "level unavailable";
	case SG_HOST_ENGINE_RUNTIME_NOT_ACCEPTED: return "not accepted";
	case SG_HOST_ENGINE_RUNTIME_DRIFT: return "runtime drift";
	case SG_HOST_ENGINE_RUNTIME_INVALID_IDENTITY: return "invalid identity";
	case SG_HOST_ENGINE_RUNTIME_INVALID_CONTENT_ID:
		return "invalid BSP content identity";
	case SG_HOST_ENGINE_RUNTIME_EVALUATION_FAILED:
		return "evaluation failed";
	case SG_HOST_ENGINE_RUNTIME_ALLOCATION_FAILED:
		return "allocation failed";
	default: return "unknown runtime status";
	}
}
