#include "sg_rune_compact_builder.h"
#include "sg_rune_compact_builder_owner.h"
#include "sg_rune_compact_weapon_catalog.h"

#include "sg_host_law_publication_private.h"
#include "sg_rune_compact_binary32.h"
#include "sg_rune_compact_localize.h"
#include "sg_rune_source_authority.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(SG_COMPACT_BUILDER_TEST_HOOKS)
static int train_route_fail_next_allocation;

void SG_RuneCompactBuilderTestFailNextTrainRouteAllocation(void)
{
	train_route_fail_next_allocation = 1;
}
#endif

#define SG_RUNE_COMPACT_BUILDER_STATE UINT64_C(0x434255494c444552)
#define SG_RUNE_COMPACT_BUILDER_SCHEMA_ID UINT64_C(0x434f4d5041435402)
#define SG_RUNE_COMPACT_BUILDER_CONSTRUCTION_ID UINT64_C(0x4253504649454c02)
#define SG_RUNE_COMPACT_BUILDER_PRODUCER_ID SG_RUNE_COMPACT_WEAPON_PRODUCER_ID
#define SG_RUNE_COMPACT_BUILDER_HASH_OFFSET UINT64_C(14695981039346656037)
#define SG_RUNE_COMPACT_BUILDER_HASH_PRIME UINT64_C(1099511628211)

struct sg_rune_compact_builder_s
{
	uint64_t state;
	uint64_t state_inverse;
	const struct sg_rune_compact_builder_s *self;
	sg_rune_compact_identity_t identity;
	sg_host_law_view_t host_law;
	sg_host_law_pmove_evaluator_t *pmove_evaluator;
	sg_rune_source_authority_t *source_authority;
	sg_bsp_world_t *world;
	sg_host_collision_authority_t collision;
	sg_configuration_space_t *configuration;
	sg_configuration_semantics_t *semantics;
	sg_bsp_entity_semantics_t *entity_semantics;
	sg_static_visibility_t *visibility;
	sg_rune_source_snapshot_t source_snapshot;
	char *entity_text;
	sg_rune_source_entity_record_t *entity_records;
	size_t entity_text_bytes;
	size_t entity_record_count;
	sg_rune_weapon_profile_t *weapon_profiles;
	sg_weapon_profile_t *resolved_weapon_profiles;
	uint32_t weapon_profile_count;
};

static void SetError(sg_rune_compact_builder_error_t *error,
	sg_rune_compact_builder_error_code_t code, uint32_t record,
	uint64_t expected, uint64_t observed)
{
	if (!error)
		return;
	error->code = code;
	error->record = record;
	error->expected = expected;
	error->observed = observed;
}

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

/* All persisted mover poses use the same staged binary32 law as the static
 * validator.  The host transform owner supplies the canonical matrix; this
 * wrapper owns only the shared arithmetic and keeps the builder's existing
 * finite-output admission at the boundary. */
static int TransformWithStagedBinary32Law(
	const sg_host_collision_transform_t *transform, const float local[3],
	float world_out[3])
{
	sg_host_collision_world_transform_t world_transform;
	uint32_t axis;

	if (transform == NULL || local == NULL || world_out == NULL ||
		!SG_HostCollisionWorldTransform(transform, &world_transform) ||
		!SG_RuneCompactBinary32TransformPoint(local, world_transform.origin,
			(const float (*)[3])world_transform.axis, world_out))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(world_out[axis]))
			return 0;
	return 1;
}

static uint64_t HashByte(uint64_t hash, uint8_t value)
{
	return (hash ^ (uint64_t)value) * SG_RUNE_COMPACT_BUILDER_HASH_PRIME;
}

static uint64_t HashBytes(uint64_t hash, const uint8_t *bytes, size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++)
		hash = HashByte(hash, bytes[index]);
	return hash;
}

static uint64_t HashU32(uint64_t hash, uint32_t value)
{
	uint32_t shift;

	for (shift = 0U; shift < 32U; shift += 8U)
		hash = HashByte(hash, (uint8_t)(value >> shift));
	return hash;
}

static uint64_t HashU64(uint64_t hash, uint64_t value)
{
	uint32_t shift;

	for (shift = 0U; shift < 64U; shift += 8U)
		hash = HashByte(hash, (uint8_t)(value >> shift));
	return hash;
}

static uint64_t FinishIdentity(uint64_t hash)
{
	if (hash == 0U)
		return UINT64_C(1);
	if (hash == UINT64_MAX)
		return UINT64_MAX - UINT64_C(1);
	return hash;
}

static uint64_t BeginIdentity(const char *domain)
{
	return HashBytes(SG_RUNE_COMPACT_BUILDER_HASH_OFFSET,
		(const uint8_t *)domain, strlen(domain));
}

static uint64_t HashFloat(uint64_t hash, float value)
{
	return HashU32(hash, FloatBits(value));
}

static uint64_t HashVec3(uint64_t hash, const sg_rune_vec3_t *vector)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		hash = HashFloat(hash, vector->value[axis]);
	return hash;
}

static uint64_t HashBounds(uint64_t hash, const sg_rune_bounds_t *bounds)
{
	hash = HashVec3(hash, &bounds->mins);
	return HashVec3(hash, &bounds->maxs);
}

static int SizeMultiply(size_t left, size_t right, size_t *result_out)
{
	if (result_out == NULL || (right != 0U && left > SIZE_MAX / right))
		return 0;
	*result_out = left * right;
	return 1;
}

/* Keep the builder's currentness witness coupled to every spawn-resolved
 * angular fact that can select a pusher transform or motion schedule. */
static uint64_t HashAngularMover(uint64_t hash,
	const sg_bsp_entity_angular_mover_t *mover)
{
	const uint8_t *schedule;
	size_t schedule_bytes;
	size_t active_bytes = 0U;

	if (mover == NULL)
		return hash;
	hash = HashU32(hash, (uint32_t)mover->kind);
	hash = HashU32(hash, mover->flags);
	schedule = (const uint8_t *)&mover->schedule;
	schedule_bytes = sizeof(mover->schedule);
	switch (mover->kind) {
	case SG_BSP_ENTITY_ANGULAR_MOVER_NONE:
		break;
	case SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR:
		hash = HashVec3(hash, &mover->schedule.finite_door.inactive_angles);
		hash = HashVec3(hash, &mover->schedule.finite_door.active_angles);
		hash = HashVec3(hash, &mover->schedule.finite_door.axis);
		hash = HashVec3(hash,
			&mover->schedule.finite_door.angular_displacement);
		hash = HashFloat(hash, mover->schedule.finite_door.speed);
		hash = HashFloat(hash, mover->schedule.finite_door.acceleration);
		hash = HashFloat(hash, mover->schedule.finite_door.deceleration);
		hash = HashU32(hash, mover->schedule.finite_door.frame_ms);
		active_bytes = sizeof(mover->schedule.finite_door);
		break;
	case SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR:
		hash = HashVec3(hash,
			&mover->schedule.continuous_rotator.initial_angles);
		hash = HashVec3(hash, &mover->schedule.continuous_rotator.axis);
		hash = HashVec3(hash,
			&mover->schedule.continuous_rotator.angular_velocity);
		hash = HashVec3(hash,
			&mover->schedule.continuous_rotator.frame_angular_delta);
		hash = HashFloat(hash, mover->schedule.continuous_rotator.speed);
		hash = HashU32(hash, mover->schedule.continuous_rotator.frame_ms);
		active_bytes = sizeof(mover->schedule.continuous_rotator);
		break;
	case SG_BSP_ENTITY_ANGULAR_MOVER_KIND_COUNT:
		break;
	}
	return HashBytes(hash, schedule + active_bytes,
		schedule_bytes - active_bytes);
}

static int HostHullMatches(const sg_rune_hull_profile_t *left,
	const sg_rune_hull_profile_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (FloatBits(left->mins.value[axis]) !=
				FloatBits(right->mins.value[axis]) ||
			FloatBits(left->maxs.value[axis]) !=
				FloatBits(right->maxs.value[axis]))
			return 0;
	return 1;
}

static int HostPhysicsMatches(const sg_rune_physics_parameters_t *left,
	const sg_rune_physics_parameters_t *right)
{
	return FloatBits(left->gravity) == FloatBits(right->gravity) &&
		FloatBits(left->ground_acceleration) ==
			FloatBits(right->ground_acceleration) &&
		FloatBits(left->air_acceleration) ==
			FloatBits(right->air_acceleration) &&
		FloatBits(left->water_acceleration) ==
			FloatBits(right->water_acceleration) &&
		FloatBits(left->hook_acceleration) ==
			FloatBits(right->hook_acceleration) &&
		FloatBits(left->external_acceleration) ==
			FloatBits(right->external_acceleration) &&
		FloatBits(left->water_drag) == FloatBits(right->water_drag) &&
		FloatBits(left->max_velocity) == FloatBits(right->max_velocity) &&
		left->frame_ms == right->frame_ms && left->substep_ms == right->substep_ms;
}

static int StaticIdentityMatches(const sg_host_static_identity_t *left,
	const sg_host_static_identity_t *right)
{
	return memcmp(left->bsp_identity.bytes, right->bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) == 0 &&
		left->bsp_bytes == right->bsp_bytes &&
		left->engine_checksum == right->engine_checksum &&
		left->entity_crc32 == right->entity_crc32 &&
		left->host_physics_epoch == right->host_physics_epoch &&
		left->reserved == right->reserved &&
		left->physics_abi_id == right->physics_abi_id &&
		HostHullMatches(&left->standing_hull, &right->standing_hull) &&
		HostHullMatches(&left->crouching_hull, &right->crouching_hull) &&
		HostPhysicsMatches(&left->physics, &right->physics);
}

static int ContentIdentityPresent(const uint8_t bytes[32])
{
	uint32_t index;

	for (index = 0U; index < 32U; index++)
		if (bytes[index] != 0U)
			return 1;
	return 0;
}

static int StaticWeaponLawValid(
	const sg_rune_source_weapon_law_t *law)
{
	return law &&
		law->weapon_balance_compiled == (uint8_t)SG_WEAPON_BALANCE_COMPILED &&
		law->weapon_balance_enabled <= 1U &&
		law->rail_match_active <= 1U && law->deathmatch_active <= 1U &&
		law->fast_switch_enabled <= 1U &&
		law->reserved[0] == 0U && law->reserved[1] == 0U &&
		law->reserved[2] == 0U &&
		(law->weapon_balance_enabled == 0U ||
			law->weapon_balance_compiled == 1U);
}

static int HostGeometryMatches(
	const sg_host_law_construction_geometry_t *left,
	const sg_host_law_construction_geometry_t *right)
{
	return memcmp(left->bsp_identity.bytes, right->bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) == 0 &&
		left->bsp_bytes == right->bsp_bytes &&
		left->engine_checksum == right->engine_checksum &&
		left->entity_bytes == right->entity_bytes &&
		left->plane_count == right->plane_count &&
		left->node_count == right->node_count &&
		left->texinfo_count == right->texinfo_count &&
		left->leaf_count == right->leaf_count &&
		left->leaf_brush_count == right->leaf_brush_count &&
		left->model_count == right->model_count &&
		left->brush_count == right->brush_count &&
		left->brush_side_count == right->brush_side_count;
}

static int HostLawAuthorityMatches(const sg_host_law_view_t *left,
	const sg_host_law_view_t *right)
{
	return left->version == right->version &&
		left->reserved == right->reserved &&
		memcmp(left->bsp_identity.bytes, right->bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) == 0 &&
		left->bsp_bytes == right->bsp_bytes &&
		StaticIdentityMatches(&left->static_identity,
			&right->static_identity) &&
		left->collision_law_id == right->collision_law_id &&
		left->pmove_law_id == right->pmove_law_id &&
		left->gravity_law_id == right->gravity_law_id &&
		left->hook_law_id == right->hook_law_id &&
		left->mechanism_law_id == right->mechanism_law_id;
}

static int HostAuthorityValid(const sg_host_law_construction_view_t *host)
{
	const sg_host_static_identity_t *identity = &host->host_static_identity;

	return host->version == SG_HOST_LAW_PUBLICATION_VERSION &&
		host->current == 1U && host->level_generation != 0U &&
		ContentIdentityPresent(identity->bsp_identity.bytes) &&
		identity->bsp_bytes != 0U && identity->bsp_bytes <= SIZE_MAX &&
		identity->physics_abi_id != 0U &&
		memcmp(identity->bsp_identity.bytes, host->geometry.bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) == 0 &&
		identity->bsp_bytes == host->geometry.bsp_bytes &&
		identity->engine_checksum == host->geometry.engine_checksum &&
		host->laws.version == SG_HOST_LAW_PUBLICATION_VERSION &&
		memcmp(host->laws.bsp_identity.bytes, identity->bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) == 0 &&
		host->laws.bsp_bytes == identity->bsp_bytes &&
		StaticIdentityMatches(&host->laws.static_identity, identity) &&
		host->geometry.entity_bytes != 0U && host->geometry.model_count != 0U &&
		host->geometry.leaf_count != 0U && host->geometry.plane_count != 0U &&
		(host->geometry.brush_count == 0U) ==
			(host->geometry.brush_side_count == 0U) &&
		host->laws.collision_law_id != 0U &&
		host->laws.pmove_law_id != 0U && host->laws.gravity_law_id != 0U &&
		host->laws.hook_law_id != 0U && host->laws.mechanism_law_id != 0U;
}

static int HostAuthorityMatches(
	const sg_host_law_construction_view_t *left,
	const sg_host_law_construction_view_t *right)
{
	return left->version == right->version && left->current == right->current &&
		left->level_generation == right->level_generation &&
		StaticIdentityMatches(&left->host_static_identity,
			&right->host_static_identity) &&
		HostGeometryMatches(&left->geometry, &right->geometry) &&
		HostLawAuthorityMatches(&left->laws, &right->laws);
}

static int SourceSnapshotMatchesHost(
	const sg_rune_source_snapshot_t *snapshot,
	const sg_host_law_construction_view_t *host)
{
	const sg_level_identity_t *level = &snapshot->level_identity;
	const sg_host_static_identity_t *identity = &host->host_static_identity;

	return snapshot->version == SG_RUNE_SOURCE_AUTHORITY_VERSION &&
		snapshot->reserved == 0U &&
		snapshot->publication_generation != 0U &&
		snapshot->publication_generation_complement ==
			~snapshot->publication_generation &&
		level->bsp_checksum == identity->engine_checksum &&
		level->entity_crc32 == identity->entity_crc32 &&
		level->host_physics_id == identity->host_physics_epoch &&
		level->bsp_bytes == identity->bsp_bytes &&
		memcmp(level->bsp_sha256, identity->bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) == 0 &&
		snapshot->host_authority.version ==
			SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION &&
		snapshot->host_authority.reserved == 0U &&
		snapshot->host_authority.epoch != 0U &&
		snapshot->host_authority.epoch_complement ==
			~snapshot->host_authority.epoch &&
		HostLawAuthorityMatches(&snapshot->host_authority.view,
			&host->laws) && StaticWeaponLawValid(&snapshot->weapon_law);
}

static int SourceSnapshotsEqual(const sg_rune_source_snapshot_t *left,
	const sg_rune_source_snapshot_t *right)
{
	return memcmp(left, right, sizeof(*left)) == 0;
}

static sg_rune_compact_builder_error_code_t SourceErrorCode(
	sg_rune_source_status_t status)
{
	if (status == SG_RUNE_SOURCE_ALLOCATION_FAILED)
		return SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY;
	if (status == SG_RUNE_SOURCE_WEAPON_UNAVAILABLE ||
		status == SG_RUNE_SOURCE_WEAPON_DRIFT)
		return SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_AUTHORITY;
	return SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUTHORITY;
}

static int WorldIdentityMatches(const sg_host_law_construction_view_t *host,
	const sg_bsp_world_t *world)
{
	return SG_BspWorldSourceIdentityCurrent(world) &&
		memcmp(world->content_identity.bytes,
			host->host_static_identity.bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) == 0 &&
		world->source_size == (size_t)host->host_static_identity.bsp_bytes &&
		world->engine_checksum == host->host_static_identity.engine_checksum &&
		world->entity_byte_count == host->geometry.entity_bytes &&
		world->model_count == host->geometry.model_count &&
		world->leaf_count == host->geometry.leaf_count &&
		world->plane_count == host->geometry.plane_count &&
		world->node_count == host->geometry.node_count &&
		world->texinfo_count == host->geometry.texinfo_count &&
		world->leaf_brush_count == host->geometry.leaf_brush_count &&
		world->brush_count == host->geometry.brush_count &&
		world->brush_side_count == host->geometry.brush_side_count;
}

static uint64_t DeriveBspContentId(const sg_bsp_content_identity_t *identity)
{
	uint64_t hash = BeginIdentity("lmctf.compact.bsp-content.v1");

	hash = HashBytes(hash, identity->bytes, SG_BSP_CONTENT_ID_BYTES);
	return FinishIdentity(hash);
}

static uint64_t DeriveSourceSetId(const sg_bsp_content_identity_t *identity)
{
	uint64_t hash = BeginIdentity("lmctf.compact.source-set.v1");

	hash = HashBytes(hash, identity->bytes, SG_BSP_CONTENT_ID_BYTES);
	hash = HashBytes(hash, SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID.bytes,
		SG_BSP_CONTENT_ID_BYTES);
	return FinishIdentity(hash);
}

static int EntitySemanticsId(const sg_bsp_content_identity_t *bsp_identity,
	const sg_bsp_entity_semantics_t *semantics, uint64_t *identity_out)
{
	uint64_t hash = BeginIdentity("lmctf.compact.entity-semantics.v1");
	uint32_t index;

	if (!bsp_identity || !semantics || !identity_out ||
		(semantics->entity_count != 0U && !semantics->entities) ||
		(semantics->edge_count != 0U && !semantics->edges) ||
		(semantics->string_bytes != 0U && !semantics->strings))
		return 0;
	hash = HashBytes(hash, bsp_identity->bytes, SG_BSP_CONTENT_ID_BYTES);
	hash = HashBytes(hash, SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID.bytes,
		SG_BSP_CONTENT_ID_BYTES);
	hash = HashU64(hash, semantics->source_set_identity);
	hash = HashU64(hash, semantics->world.source_set_identity);
	hash = HashU32(hash, semantics->world.source_entity_ordinal);
	hash = HashU32(hash, semantics->world.flags);
	hash = HashFloat(hash, semantics->world.gravity);
	hash = HashU32(hash, semantics->entity_count);
	for (index = 0U; index < semantics->entity_count; index++) {
		const sg_bsp_entity_semantic_t *entity = &semantics->entities[index];

		hash = HashU64(hash, entity->source_set_identity);
		hash = HashU32(hash, entity->source_entity_ordinal);
		hash = HashU32(hash, entity->canonical_ordinal);
		hash = HashU32(hash, entity->classname);
		hash = HashU32(hash, entity->targetname);
		hash = HashU32(hash, entity->required_item);
		hash = HashU32(hash, entity->spawned_classname);
		hash = HashU32(hash, entity->destination_map);
		hash = HashU32(hash, entity->bsp_model);
		hash = HashU32(hash, entity->flags);
		hash = HashU32(hash, (uint32_t)entity->landmark_kind);
		hash = HashU32(hash, (uint32_t)entity->mechanism_kind);
		hash = HashU32(hash, (uint32_t)entity->mechanism_role);
		hash = HashU32(hash, (uint32_t)entity->physics_kind);
		hash = HashVec3(hash, &entity->origin);
		hash = HashVec3(hash, &entity->angles);
		hash = HashVec3(hash, &entity->move_direction);
		hash = HashVec3(hash, &entity->move_origin);
		hash = HashVec3(hash, &entity->move_angles);
		hash = HashAngularMover(hash, &entity->angular_mover);
		hash = HashBounds(hash, &entity->bounds);
		hash = HashFloat(hash, entity->delay_ms);
		hash = HashFloat(hash, entity->dwell_ms);
		hash = HashFloat(hash, entity->pause_ms);
		hash = HashFloat(hash, entity->speed);
		hash = HashFloat(hash, entity->acceleration);
		hash = HashFloat(hash, entity->deceleration);
		hash = HashFloat(hash, entity->lip);
		hash = HashFloat(hash, entity->height);
		hash = HashFloat(hash, entity->distance);
		hash = HashFloat(hash, entity->gravity);
		hash = HashFloat(hash, entity->random);
		hash = HashU32(hash, (uint32_t)entity->damage);
		hash = HashU32(hash, (uint32_t)entity->count);
		hash = HashU32(hash, (uint32_t)entity->health);
		hash = HashU32(hash, (uint32_t)entity->style);
		hash = HashU32(hash, entity->spawnflags);
	}
	hash = HashU32(hash, semantics->edge_count);
	for (index = 0U; index < semantics->edge_count; index++) {
		const sg_bsp_entity_semantic_edge_t *edge = &semantics->edges[index];

		hash = HashU32(hash, edge->source);
		hash = HashU32(hash, edge->destination);
		hash = HashU32(hash, (uint32_t)edge->kind);
		hash = HashU32(hash, edge->name);
		hash = HashU32(hash, edge->fanout_ordinal);
	}
	hash = HashU32(hash, semantics->string_bytes);
	hash = HashBytes(hash, (const uint8_t *)semantics->strings,
		semantics->string_bytes);
	*identity_out = FinishIdentity(hash);
	return 1;
}

static void ReportPhase(const sg_rune_compact_builder_input_t *input,
	const char *phase, uint32_t done, uint32_t total)
{
	if (input != NULL && input->progress != NULL)
		input->progress(input->progress_context, phase, done, total);
}

typedef struct progress_link_s
{
	sg_rune_compact_builder_progress_fn progress;
	void *context;
} progress_link_t;

static void ConfigurationProgress(void *context, uint32_t done,
	uint32_t total)
{
	const progress_link_t *link = context;

	if (link->progress != NULL)
		link->progress(link->context, "configuration", done, total);
}

static int InputShapeValid(const sg_rune_compact_builder_input_t *input,
	sg_rune_compact_builder_t **builder_out)
{
	return input && input->construction && builder_out && !*builder_out;
}

static int FloatToQ8(float value, int32_t *q8_out)
{
	const double scaled = (double)value * 8.0;

	if (!q8_out || !isfinite(value) || scaled < (double)INT32_MIN ||
		scaled > (double)INT32_MAX || scaled != floor(scaled))
		return 0;
	*q8_out = (int32_t)scaled;
	return 1;
}

static int CompactHull(const sg_rune_hull_profile_t *source,
	sg_rune_compact_hull_t *destination)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (!FloatToQ8(source->mins.value[axis],
				&destination->mins.value[axis]) ||
			!FloatToQ8(source->maxs.value[axis],
				&destination->maxs.value[axis]))
			return 0;
	return 1;
}

static void ConfigurationIdentity(
	const sg_host_law_construction_view_t *host, uint64_t source_set_identity,
	uint64_t entity_semantics_id, sg_rune_model_identity_t *identity_out)
{
	memset(identity_out, 0, sizeof(*identity_out));
	identity_out->bsp_content_id =
		DeriveBspContentId(&host->host_static_identity.bsp_identity);
	identity_out->entity_semantics_id = entity_semantics_id;
	identity_out->physics_abi_id = host->host_static_identity.physics_abi_id;
	identity_out->source_set_identity = source_set_identity;
	identity_out->schema_id = SG_RUNE_COMPACT_BUILDER_SCHEMA_ID;
	identity_out->producer_identity = SG_RUNE_COMPACT_BUILDER_PRODUCER_ID;
	identity_out->standing_hull = host->host_static_identity.standing_hull;
	identity_out->crouching_hull = host->host_static_identity.crouching_hull;
	identity_out->physics = host->host_static_identity.physics;
}

static int CompactIdentity(const sg_host_law_construction_view_t *host,
	const sg_bsp_world_t *world, uint64_t entity_semantics_id,
	uint64_t weapon_law_id, uint64_t weapon_profile_catalog_id,
	uint32_t entity_count,
	sg_rune_compact_identity_t *identity_out)
{
	const sg_host_static_identity_t *source = &host->host_static_identity;

	memset(identity_out, 0, sizeof(*identity_out));
	memcpy(identity_out->bsp_sha256, source->bsp_identity.bytes,
		SG_BSP_CONTENT_ID_BYTES);
	identity_out->bsp_bytes = source->bsp_bytes;
	identity_out->bsp_checksum = source->engine_checksum;
	identity_out->entity_crc32 = source->entity_crc32;
	identity_out->entity_semantics_id = entity_semantics_id;
	identity_out->physics_abi_id = source->physics_abi_id;
	identity_out->collision_law_id = host->laws.collision_law_id;
	identity_out->pmove_law_id = host->laws.pmove_law_id;
	identity_out->gravity_law_id = host->laws.gravity_law_id;
	identity_out->hook_law_id = host->laws.hook_law_id;
	identity_out->mechanism_law_id = host->laws.mechanism_law_id;
	identity_out->weapon_law_id = weapon_law_id;
	identity_out->weapon_profile_catalog_id = weapon_profile_catalog_id;
	identity_out->construction_id = SG_RUNE_COMPACT_BUILDER_CONSTRUCTION_ID;
	identity_out->schema_id = SG_RUNE_COMPACT_BUILDER_SCHEMA_ID;
	identity_out->producer_identity = SG_RUNE_COMPACT_BUILDER_PRODUCER_ID;
	identity_out->source_counts.model_count = world->model_count;
	identity_out->source_counts.leaf_count = world->leaf_count;
	identity_out->source_counts.area_count = world->area_count;
	identity_out->source_counts.plane_count = world->plane_count;
	identity_out->source_counts.brush_count = world->brush_count;
	identity_out->source_counts.brush_side_count = world->brush_side_count;
	identity_out->source_counts.entity_count = entity_count;
	identity_out->physics.gravity_bits = FloatBits(source->physics.gravity);
	identity_out->physics.ground_acceleration_bits =
		FloatBits(source->physics.ground_acceleration);
	identity_out->physics.air_acceleration_bits =
		FloatBits(source->physics.air_acceleration);
	identity_out->physics.water_acceleration_bits =
		FloatBits(source->physics.water_acceleration);
	identity_out->physics.hook_acceleration_bits =
		FloatBits(source->physics.hook_acceleration);
	identity_out->physics.external_acceleration_bits =
		FloatBits(source->physics.external_acceleration);
	identity_out->physics.water_drag_bits =
		FloatBits(source->physics.water_drag);
	identity_out->physics.max_velocity_bits =
		FloatBits(source->physics.max_velocity);
	identity_out->physics.frame_ms = source->physics.frame_ms;
	identity_out->physics.substep_ms = source->physics.substep_ms;
	return CompactHull(&source->standing_hull, &identity_out->standing_hull) &&
		CompactHull(&source->crouching_hull, &identity_out->crouching_hull);
}

static sg_rune_weapon_response_family_mask_t CanonicalResponseMask(
	const sg_weapon_profile_t *profile)
{
#define RESPONSE_BIT(family) SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family)
	if (!profile)
		return 0U;
	switch (profile->id) {
	case SG_WEAPON_PROFILE_BLASTER:
		if (profile->family != SG_WEAPON_FAMILY_STRAIGHT_PROJECTILE ||
			(profile->effects & SG_WEAPON_EFFECT_PROJECTILE) == 0U)
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT);
	case SG_WEAPON_PROFILE_SHOTGUN:
	case SG_WEAPON_PROFILE_SUPER_SHOTGUN:
		if (profile->family != SG_WEAPON_FAMILY_SPREAD ||
			(profile->effects & (SG_WEAPON_EFFECT_HITSCAN |
				SG_WEAPON_EFFECT_SPREAD)) !=
				(SG_WEAPON_EFFECT_HITSCAN | SG_WEAPON_EFFECT_SPREAD))
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_HITSCAN) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE);
	case SG_WEAPON_PROFILE_MACHINEGUN:
	case SG_WEAPON_PROFILE_CHAINGUN:
		if (profile->family != SG_WEAPON_FAMILY_HITSCAN ||
			(profile->effects & (SG_WEAPON_EFFECT_HITSCAN |
				SG_WEAPON_EFFECT_SPREAD)) !=
				(SG_WEAPON_EFFECT_HITSCAN | SG_WEAPON_EFFECT_SPREAD))
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_HITSCAN) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD);
	case SG_WEAPON_PROFILE_GRENADE_LAUNCHER:
	case SG_WEAPON_PROFILE_HAND_GRENADE:
		if (profile->family != SG_WEAPON_FAMILY_GRENADE_BOUNCE ||
			(profile->effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_SPLASH | SG_WEAPON_EFFECT_BOUNCE)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPLASH |
				 SG_WEAPON_EFFECT_BOUNCE))
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE);
	case SG_WEAPON_PROFILE_ROCKET_LAUNCHER:
		if (profile->family != SG_WEAPON_FAMILY_ROCKET_SPLASH ||
			(profile->effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_SPLASH)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPLASH))
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH);
	case SG_WEAPON_PROFILE_HYPERBLASTER:
		if (profile->family != SG_WEAPON_FAMILY_HYPERBLASTER ||
			(profile->effects & SG_WEAPON_EFFECT_PROJECTILE) == 0U)
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER);
	case SG_WEAPON_PROFILE_RAILGUN:
		if (profile->family != SG_WEAPON_FAMILY_HITSCAN ||
			(profile->effects & (SG_WEAPON_EFFECT_HITSCAN |
				SG_WEAPON_EFFECT_PENETRATION)) !=
				(SG_WEAPON_EFFECT_HITSCAN | SG_WEAPON_EFFECT_PENETRATION))
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_RAIL);
	case SG_WEAPON_PROFILE_BFG:
		if (profile->family != SG_WEAPON_FAMILY_BFG ||
			(profile->effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_SPLASH | SG_WEAPON_EFFECT_SPECIAL)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPLASH |
				 SG_WEAPON_EFFECT_SPECIAL))
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_BFG) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_SPECIAL);
	case SG_WEAPON_PROFILE_PLASMA_REFLECT:
		if (profile->family != SG_WEAPON_FAMILY_PLASMA_REFLECT ||
			(profile->effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_SPLASH | SG_WEAPON_EFFECT_BOUNCE)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPLASH |
				 SG_WEAPON_EFFECT_BOUNCE))
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_SPECIAL);
	case SG_WEAPON_PROFILE_PLASMA_SPREAD:
		if (profile->family != SG_WEAPON_FAMILY_PLASMA_SPREAD ||
			(profile->effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_SPLASH | SG_WEAPON_EFFECT_SPREAD)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPLASH |
				 SG_WEAPON_EFFECT_SPREAD))
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH) |
			RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_SPECIAL);
	case SG_WEAPON_PROFILE_HOOK:
		if (profile->family != SG_WEAPON_FAMILY_SPECIAL ||
			(profile->effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_SPECIAL)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPECIAL))
			return 0U;
		return RESPONSE_BIT(SG_RUNE_WEAPON_RESPONSE_SPECIAL);
	case SG_WEAPON_PROFILE_COUNT:
		break;
	}
#undef RESPONSE_BIT
	return 0U;
}

static int ResolveWeaponProfiles(
	const sg_rune_source_weapon_law_t *static_law,
	uint64_t physics_abi_id, sg_rune_compact_builder_t *builder,
	uint64_t *weapon_law_id_out, uint64_t *weapon_profile_catalog_id_out,
	sg_rune_compact_builder_error_t *error)
{
	size_t catalog_count = SG_WeaponProfileCount();
	uint32_t index;

	if (!static_law || !StaticWeaponLawValid(static_law) ||
		!weapon_law_id_out || !weapon_profile_catalog_id_out ||
		!SG_WeaponProfileCatalogValid() ||
		catalog_count != (size_t)SG_WEAPON_PROFILE_COUNT - 1U ||
		catalog_count == 0U ||
		catalog_count > SG_RUNE_COMPACT_MAX_WEAPON_PROFILES ||
		catalog_count > UINT32_MAX) {
		SetError(error, SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_PROFILE, 0U,
			(uint64_t)SG_WEAPON_PROFILE_COUNT - 1U, (uint64_t)catalog_count);
		return 0;
	}
	builder->weapon_profile_count = (uint32_t)catalog_count;
	builder->weapon_profiles = calloc(catalog_count,
		sizeof(*builder->weapon_profiles));
	builder->resolved_weapon_profiles = calloc(catalog_count,
		sizeof(*builder->resolved_weapon_profiles));
	if (!builder->weapon_profiles || !builder->resolved_weapon_profiles) {
		SetError(error, SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY, 0U,
			(uint64_t)catalog_count, 0U);
		return 0;
	}
	if (!SG_RuneCompactWeaponProfilesResolve(static_law, physics_abi_id,
		builder->resolved_weapon_profiles, builder->weapon_profile_count)) {
		SetError(error, SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_PROFILE,
			0U, 1U, 0U);
		return 0;
	}
	for (index = 0U; index < builder->weapon_profile_count; index++) {
		const sg_weapon_profile_id_t source =
			(sg_weapon_profile_id_t)(index + 1U);
		sg_rune_weapon_response_family_mask_t mask;

		if (builder->resolved_weapon_profiles[index].id != source ||
			builder->resolved_weapon_profiles[index].build_identity !=
				SG_RUNE_COMPACT_BUILDER_PRODUCER_ID ||
			builder->resolved_weapon_profiles[index].physics_abi_id !=
				physics_abi_id) {
			SetError(error, SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_PROFILE,
				index, (uint64_t)source, 0U);
			return 0;
		}
		mask = CanonicalResponseMask(&builder->resolved_weapon_profiles[index]);
		if (mask == 0U ||
			(mask & ~SG_RUNE_WEAPON_RESPONSE_FAMILIES_ALL) != 0U) {
			SetError(error, SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_PROFILE,
				index, (uint64_t)source, 0U);
			return 0;
		}
		builder->weapon_profiles[index].source_profile = (uint32_t)source;
		builder->weapon_profiles[index].response_families = mask;
		builder->weapon_profiles[index].projectile_count_min =
			builder->resolved_weapon_profiles[index].projectile_count_min;
		builder->weapon_profiles[index].projectile_count_max =
			builder->resolved_weapon_profiles[index].projectile_count_max;
		builder->weapon_profiles[index].auxiliary_trace_count =
			builder->resolved_weapon_profiles[index].auxiliary_trace_count;
		builder->weapon_profiles[index].direct_response_count =
			FloatBits(builder->resolved_weapon_profiles[index].direct_damage) ==
			FloatBits(builder->resolved_weapon_profiles[index].direct_damage_max) ?
				2U : 1U;
	}
	if (!SG_RuneCompactWeaponLawIdentity(static_law,
		builder->resolved_weapon_profiles, builder->weapon_profile_count,
		weapon_law_id_out)) {
		SetError(error, SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_PROFILE,
			builder->weapon_profile_count, 1U, 0U);
		return 0;
	}
	if (!SG_RuneCompactWeaponProfileCatalogId(builder->weapon_profiles,
		builder->weapon_profile_count, weapon_profile_catalog_id_out)) {
		SetError(error, SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_PROFILE,
			builder->weapon_profile_count, 1U, 0U);
		return 0;
	}
	return 1;
}

static int BuilderValid(const sg_rune_compact_builder_t *builder)
{
	return builder && builder->state == SG_RUNE_COMPACT_BUILDER_STATE &&
		builder->state_inverse == ~SG_RUNE_COMPACT_BUILDER_STATE &&
		builder->self == builder && builder->world && builder->configuration &&
		builder->semantics && builder->entity_semantics && builder->visibility &&
		builder->pmove_evaluator && builder->source_authority &&
		builder->entity_text && builder->entity_text_bytes != 0U &&
		builder->entity_records && builder->entity_record_count != 0U &&
		builder->weapon_profiles && builder->resolved_weapon_profiles &&
		builder->weapon_profile_count != 0U;
}

static int BuilderEntitySemanticsCurrent(
	const sg_rune_compact_builder_t *builder, uint64_t *observed_out)
{
	uint64_t observed;

	if (builder == NULL || builder->world == NULL ||
		builder->entity_semantics == NULL ||
		!EntitySemanticsId(&builder->world->content_identity,
			builder->entity_semantics, &observed))
		return 0;
	if (observed_out != NULL)
		*observed_out = observed;
	return observed == builder->identity.entity_semantics_id;
}

static int Build(
	const sg_rune_compact_builder_input_t *input,
	sg_rune_compact_builder_t **builder_out,
	sg_rune_compact_builder_error_t *error_out, int development_audit)
{
	sg_rune_compact_builder_t *builder = NULL;
	sg_host_law_construction_view_t host;
	sg_host_law_construction_view_t final_host;
	sg_host_static_identity_t copied_identity;
	sg_host_law_result_t host_result;
	sg_bsp_error_t bsp_error;
	sg_host_collision_error_t collision_error;
	sg_rune_model_identity_t configuration_identity;
	sg_configuration_error_t configuration_error;
	progress_link_t progress_link;
	sg_configuration_limits_t default_configuration_limits;
	const sg_configuration_limits_t *configuration_limits;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_limits_t default_semantics_limits;
	const sg_configuration_semantics_limits_t *semantics_limits;
	sg_bsp_entity_semantics_error_t entity_error;
	sg_bsp_entity_semantics_audit_result_t entity_audit;
	sg_bsp_entity_semantics_binding_t entity_binding;
	sg_bsp_entity_semantics_source_t entity_source;
	sg_static_visibility_error_t visibility_error;
	sg_static_visibility_limits_t default_visibility_limits;
	const sg_static_visibility_limits_t *visibility_limits;
	sg_static_visibility_audit_result_t visibility_audit;
	sg_rune_source_authority_t *source_authority = NULL;
	sg_rune_source_snapshot_t final_source_snapshot;
	sg_rune_source_status_t source_status;
	char *final_entity_text = NULL;
	sg_rune_source_entity_record_t *final_entity_records = NULL;
	uint8_t *bsp_bytes = NULL;
	size_t bsp_size = 0U;
	size_t copied_size = 0U;
	size_t entity_text_bytes = 0U;
	size_t entity_record_count = 0U;
	size_t entity_records_bytes;
	uint64_t source_set_identity;
	uint64_t entity_semantics_id = 0U;
	uint64_t weapon_law_id;
	uint64_t weapon_profile_catalog_id;
	int ok = 0;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!InputShapeValid(input, builder_out)) {
		SetError(error_out, SG_RUNE_COMPACT_BUILDER_ERROR_INVALID_ARGUMENT,
			0U, 1U, 0U);
		return 0;
	}
	configuration_limits = input->configuration_limits;
	if (!configuration_limits) {
		SG_ConfigurationDefaultLimits(&default_configuration_limits);
		configuration_limits = &default_configuration_limits;
	}
	semantics_limits = input->semantics_limits;
	if (!semantics_limits) {
		SG_ConfigurationSemanticsDefaultLimits(&default_semantics_limits);
		semantics_limits = &default_semantics_limits;
	}
	visibility_limits = input->visibility_limits;
	if (!visibility_limits) {
		SG_StaticVisibilityDefaultLimits(&default_visibility_limits);
		visibility_limits = &default_visibility_limits;
	}
	memset(&host, 0, sizeof(host));
	host_result = SG_HostLawConstructionRead(input->construction, &host);
	if (host_result.status != SG_HOST_LAW_OK || !HostAuthorityValid(&host)) {
		SetError(error_out,
			host_result.status == SG_HOST_LAW_ALLOCATION_FAILED ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
			host_result.status == SG_HOST_LAW_OK ?
				SG_RUNE_COMPACT_BUILDER_ERROR_IDENTITY_MISMATCH :
				SG_RUNE_COMPACT_BUILDER_ERROR_HOST_AUTHORITY,
				host_result.element, SG_HOST_LAW_OK, host_result.status);
		return 0;
	}
	source_status = SG_RuneSourceAuthorityAcquire(&source_authority);
	if (source_status != SG_RUNE_SOURCE_OK) {
		SetError(error_out, SourceErrorCode(source_status), 0U,
			SG_RUNE_SOURCE_OK, (uint64_t)source_status);
		goto done;
	}
	source_status = SG_RuneSourceAuthoritySizes(source_authority,
		&entity_text_bytes, &entity_record_count);
	if (source_status != SG_RUNE_SOURCE_OK || entity_text_bytes == 0U ||
		entity_record_count == 0U || entity_text_bytes > UINT32_MAX ||
		entity_record_count > UINT32_MAX ||
		entity_record_count > SIZE_MAX /
			sizeof(*builder->entity_records)) {
		SetError(error_out,
			source_status == SG_RUNE_SOURCE_OK ?
				SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUTHORITY :
				SourceErrorCode(source_status),
			0U, 1U, (uint64_t)entity_record_count);
		goto done;
	}
	entity_records_bytes = entity_record_count *
		sizeof(*builder->entity_records);
	host_result = SG_HostLawConstructionOwnerCopyBsp(input->construction,
		NULL, 0U, &bsp_size, NULL);
	if (host_result.status != SG_HOST_LAW_OK || bsp_size == 0U) {
		SetError(error_out,
			host_result.status == SG_HOST_LAW_ALLOCATION_FAILED ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_HOST_AUTHORITY,
			host_result.element, 1U, (uint64_t)bsp_size);
		goto done;
	}
	if ((uint64_t)bsp_size != host.host_static_identity.bsp_bytes) {
		SetError(error_out, SG_RUNE_COMPACT_BUILDER_ERROR_IDENTITY_MISMATCH,
			0U, host.host_static_identity.bsp_bytes, (uint64_t)bsp_size);
		goto done;
	}
	bsp_bytes = malloc(bsp_size);
	builder = calloc(1U, sizeof(*builder));
	if (builder) {
		builder->entity_text = malloc(entity_text_bytes);
		builder->entity_records = malloc(entity_records_bytes);
		builder->entity_text_bytes = entity_text_bytes;
		builder->entity_record_count = entity_record_count;
	}
	final_entity_text = malloc(entity_text_bytes);
	final_entity_records = malloc(entity_records_bytes);
	if (!bsp_bytes || !builder || !builder->entity_text ||
		!builder->entity_records || !final_entity_text ||
		!final_entity_records) {
		SetError(error_out, SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY, 0U,
			(uint64_t)bsp_size, 0U);
		goto done;
	}
	memset(&builder->source_snapshot, 0, sizeof(builder->source_snapshot));
	source_status = SG_RuneSourceAuthorityCopy(source_authority,
		&builder->source_snapshot, builder->entity_text,
		builder->entity_text_bytes, builder->entity_records,
		builder->entity_record_count);
	if (source_status != SG_RUNE_SOURCE_OK ||
		builder->source_snapshot.entity_text_bytes != entity_text_bytes ||
		builder->source_snapshot.entity_record_count != entity_record_count ||
		builder->entity_text[entity_text_bytes - 1U] != '\0' ||
		!SourceSnapshotMatchesHost(&builder->source_snapshot, &host)) {
		SetError(error_out,
			source_status == SG_RUNE_SOURCE_OK ?
				SG_RUNE_COMPACT_BUILDER_ERROR_IDENTITY_MISMATCH :
				SourceErrorCode(source_status),
			0U, SG_RUNE_SOURCE_OK, (uint64_t)source_status);
		goto done;
	}
	memset(&copied_identity, 0, sizeof(copied_identity));
	host_result = SG_HostLawConstructionOwnerCopyBsp(input->construction,
		bsp_bytes, bsp_size, &copied_size, &copied_identity);
	if (host_result.status != SG_HOST_LAW_OK || copied_size != bsp_size ||
		!StaticIdentityMatches(&copied_identity,
			&host.host_static_identity)) {
		SetError(error_out,
			host_result.status == SG_HOST_LAW_ALLOCATION_FAILED ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_HOST_AUTHORITY,
			host_result.element, (uint64_t)bsp_size, (uint64_t)copied_size);
		goto done;
	}
	memset(&bsp_error, 0, sizeof(bsp_error));
	if (!SG_BspWorldLoadMemory(bsp_bytes, copied_size, &builder->world,
			&bsp_error)) {
		SetError(error_out,
			bsp_error.code == SG_BSP_ERROR_OUT_OF_MEMORY ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_BSP_LOAD,
			bsp_error.record, SG_BSP_ERROR_NONE, (uint64_t)bsp_error.code);
		goto done;
	}
	if (!WorldIdentityMatches(&host, builder->world)) {
		SetError(error_out, SG_RUNE_COMPACT_BUILDER_ERROR_IDENTITY_MISMATCH,
			0U, 1U, 0U);
		goto done;
	}
	source_set_identity =
		DeriveSourceSetId(&host.host_static_identity.bsp_identity);
	memset(&entity_error, 0, sizeof(entity_error));
	ReportPhase(input, "entities", 0U, 0U);
	if (!SG_BspEntitySemanticsBuildEffective(builder->world,
			builder->entity_text, builder->entity_text_bytes,
			builder->entity_records, builder->entity_record_count,
			source_set_identity, &builder->entity_semantics, &entity_error)) {
		SetError(error_out,
			entity_error.code == SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_SEMANTICS,
			entity_error.entity_ordinal, SG_BSP_ENTITY_SEMANTICS_ERROR_NONE,
			(uint64_t)entity_error.code);
		goto done;
	}
	if (builder->entity_semantics->entity_count == 0U ||
		!EntitySemanticsId(&builder->world->content_identity,
			builder->entity_semantics, &entity_semantics_id)) {
		SetError(error_out, SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_SEMANTICS,
			0U, 1U, builder->entity_semantics->entity_count);
		goto done;
	}
	ConfigurationIdentity(&host, source_set_identity, entity_semantics_id,
		&configuration_identity);
	memset(&collision_error, 0, sizeof(collision_error));
	if (!SG_HostCollisionInit(&builder->collision, builder->world,
			&configuration_identity, &collision_error)) {
		SetError(error_out, SG_RUNE_COMPACT_BUILDER_ERROR_CONFIGURATION, 0U,
			SG_HOST_COLLISION_ERROR_NONE, (uint64_t)collision_error);
		goto done;
	}
	memset(&entity_binding, 0, sizeof(entity_binding));
	memcpy(entity_binding.source_identity.bytes,
		builder->world->content_identity.bytes, SG_BSP_CONTENT_ID_BYTES);
	entity_binding.source_set_identity = source_set_identity;
	entity_binding.schema_identity = SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID;
	memset(&entity_source, 0, sizeof(entity_source));
	entity_source.selected_entity_text = builder->entity_text;
	entity_source.selected_entity_text_bytes = builder->entity_text_bytes;
	entity_source.survivors = builder->entity_records;
	entity_source.survivor_count = builder->entity_record_count;
	memset(&entity_audit, 0, sizeof(entity_audit));
	if (development_audit &&
		!SG_BspEntitySemanticsAuditEffective(&builder->collision,
			&entity_binding, &entity_source, builder->entity_semantics,
			&entity_audit)) {
		SetError(error_out,
			entity_audit.code == SG_BSP_ENTITY_SEMANTICS_AUDIT_OUT_OF_MEMORY ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUDIT,
			entity_audit.record, SG_BSP_ENTITY_SEMANTICS_AUDIT_OK,
			(uint64_t)entity_audit.code);
		goto done;
	}
	memset(&configuration_error, 0, sizeof(configuration_error));
	ReportPhase(input, "configuration", 0U, 0U);
	progress_link.progress = input->progress;
	progress_link.context = input->progress_context;
	if (!SG_ConfigurationBuildWithProgress(&builder->collision,
			configuration_limits, ConfigurationProgress, &progress_link,
			&builder->configuration, &configuration_error)) {
		SetError(error_out,
			configuration_error.code == SG_CONFIGURATION_ERROR_OUT_OF_MEMORY ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_CONFIGURATION,
			configuration_error.source_index, SG_CONFIGURATION_ERROR_NONE,
			(uint64_t)configuration_error.code);
		goto done;
	}
	memset(&semantics_error, 0, sizeof(semantics_error));
	ReportPhase(input, "semantics", 0U, 0U);
	if (!SG_ConfigurationSemanticsBuild(&builder->collision,
			builder->configuration, semantics_limits,
			&builder->semantics, &semantics_error)) {
		SetError(error_out,
			semantics_error.code ==
				SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_SEMANTICS,
			semantics_error.source_index, SG_CONFIGURATION_SEMANTICS_ERROR_NONE,
			(uint64_t)semantics_error.code);
		goto done;
	}
	memset(&visibility_error, 0, sizeof(visibility_error));
	ReportPhase(input, "visibility", 0U, 0U);
	if (!SG_StaticVisibilityBuild(&builder->collision, builder->configuration,
			builder->semantics, visibility_limits, &builder->visibility,
			&visibility_error)) {
		SetError(error_out,
			visibility_error.code == SG_STATIC_VISIBILITY_ERROR_OUT_OF_MEMORY ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_VISIBILITY,
			visibility_error.source_index, SG_STATIC_VISIBILITY_ERROR_NONE,
			(uint64_t)visibility_error.code);
		goto done;
	}
	memset(&visibility_audit, 0, sizeof(visibility_audit));
	if (development_audit &&
		!SG_StaticVisibilityAudit(&builder->collision, builder->configuration,
			builder->semantics, builder->visibility, &visibility_audit)) {
		SetError(error_out,
			visibility_audit.code == SG_STATIC_VISIBILITY_AUDIT_OUT_OF_MEMORY ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_VISIBILITY_AUDIT,
			visibility_audit.record, SG_STATIC_VISIBILITY_AUDIT_OK,
			(uint64_t)visibility_audit.code);
		goto done;
	}
	if (!ResolveWeaponProfiles(&builder->source_snapshot.weapon_law,
			host.host_static_identity.physics_abi_id,
			builder, &weapon_law_id, &weapon_profile_catalog_id, error_out))
		goto done;
	if (!CompactIdentity(&host, builder->world, entity_semantics_id,
			weapon_law_id, weapon_profile_catalog_id,
			builder->entity_semantics->entity_count,
			&builder->identity)) {
		SetError(error_out, SG_RUNE_COMPACT_BUILDER_ERROR_IDENTITY_MISMATCH,
			0U, 1U, 0U);
		goto done;
	}
	memset(&final_host, 0, sizeof(final_host));
	host_result = SG_HostLawConstructionRead(input->construction, &final_host);
	if (host_result.status != SG_HOST_LAW_OK ||
		!HostAuthorityValid(&final_host) ||
		!HostAuthorityMatches(&host, &final_host) ||
		!WorldIdentityMatches(&final_host, builder->world)) {
		SetError(error_out,
			host_result.status == SG_HOST_LAW_ALLOCATION_FAILED ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_HOST_AUTHORITY,
			host_result.element, SG_HOST_LAW_OK, host_result.status);
		goto done;
	}
	memset(&final_source_snapshot, 0, sizeof(final_source_snapshot));
	source_status = SG_RuneSourceAuthorityCopy(source_authority,
		&final_source_snapshot, final_entity_text, entity_text_bytes,
		final_entity_records, entity_record_count);
	if (source_status != SG_RUNE_SOURCE_OK ||
		!SourceSnapshotsEqual(&builder->source_snapshot,
			&final_source_snapshot) ||
		memcmp(builder->entity_text, final_entity_text,
			entity_text_bytes) != 0 ||
		memcmp(builder->entity_records, final_entity_records,
			entity_records_bytes) != 0) {
		SetError(error_out,
			source_status == SG_RUNE_SOURCE_OK ?
				(memcmp(&builder->source_snapshot.weapon_law,
					&final_source_snapshot.weapon_law,
					sizeof(builder->source_snapshot.weapon_law)) == 0 ?
					SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUTHORITY :
					SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_AUTHORITY) :
				SourceErrorCode(source_status),
			0U, SG_RUNE_SOURCE_OK, (uint64_t)source_status);
		goto done;
	}
	source_status = SG_RuneSourceAuthorityCurrent(source_authority);
	if (source_status != SG_RUNE_SOURCE_OK) {
		SetError(error_out, SourceErrorCode(source_status), 0U,
			SG_RUNE_SOURCE_OK, (uint64_t)source_status);
		goto done;
	}
	builder->host_law = final_host.laws;
	host_result = SG_HostLawConstructionOwnerPmoveEvaluatorAcquire(
		input->construction, &builder->pmove_evaluator);
	if (host_result.status != SG_HOST_LAW_OK) {
		SetError(error_out,
			host_result.status == SG_HOST_LAW_ALLOCATION_FAILED ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_HOST_AUTHORITY,
			0U, SG_HOST_LAW_OK, (uint64_t)host_result.status);
		goto done;
	}
	builder->source_authority = source_authority;
	source_authority = NULL;
	builder->state = SG_RUNE_COMPACT_BUILDER_STATE;
	builder->state_inverse = ~SG_RUNE_COMPACT_BUILDER_STATE;
	builder->self = builder;
	*builder_out = builder;
	builder = NULL;
	ok = 1;

done:
	SG_RuneSourceAuthorityDestroy(source_authority);
	free(final_entity_records);
	free(final_entity_text);
	free(bsp_bytes);
	SG_RuneCompactBuilderDestroy(builder);
	return ok;
}

int SG_RuneCompactBuilderBuild(
	const sg_rune_compact_builder_input_t *input,
	sg_rune_compact_builder_t **builder_out,
	sg_rune_compact_builder_error_t *error_out)
{
	return Build(input, builder_out, error_out, 0);
}

int SG_RuneCompactBuilderBuildDevelopmentAudit(
	const sg_rune_compact_builder_input_t *input,
	sg_rune_compact_builder_t **builder_out,
	sg_rune_compact_builder_error_t *error_out)
{
	return Build(input, builder_out, error_out, 1);
}

int SG_RuneCompactBuilderRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_view_t *view_out)
{
	if (!BuilderValid(builder) || !view_out)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = builder->identity;
	view_out->weapon_profiles = builder->weapon_profiles;
	view_out->resolved_weapon_profiles = builder->resolved_weapon_profiles;
	view_out->weapon_profile_count = builder->weapon_profile_count;
	return 1;
}

int SG_RuneCompactBuilderOwnerRead(
	const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	if (!BuilderValid(builder) || !view_out ||
		!BuilderEntitySemanticsCurrent(builder, NULL) ||
		SG_HostLawPmoveEvaluatorCurrent(builder->pmove_evaluator).status !=
			SG_HOST_LAW_OK ||
		SG_RuneSourceAuthorityCurrent(builder->source_authority) !=
			SG_RUNE_SOURCE_OK)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = builder->identity;
	view_out->world = builder->world;
	view_out->collision = &builder->collision;
	view_out->host_law = &builder->host_law;
	view_out->weapon_law = &builder->source_snapshot.weapon_law;
	view_out->configuration = builder->configuration;
	view_out->semantics = builder->semantics;
	view_out->entity_semantics = builder->entity_semantics;
	view_out->visibility = builder->visibility;
	return 1;
}

static sg_host_law_result_t BuilderOwnerResult(sg_host_law_status_t status,
	sg_host_law_field_t field, uint32_t element, uint64_t expected,
	uint64_t observed)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	result.field = field;
	result.element = element;
	result.expected_bits = expected;
	result.observed_bits = observed;
	return result;
}

static sg_host_law_result_t BuilderOwnerCurrent(
	const sg_rune_compact_builder_t *builder)
{
	sg_host_law_result_t result;
	sg_rune_source_status_t source_status;
	uint64_t entity_semantics_id = 0U;

	if (!BuilderValid(builder))
		return BuilderOwnerResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_ENTITY_SEMANTICS, SG_HOST_LAW_ELEMENT_NONE,
			1U, 0U);
	if (!BuilderEntitySemanticsCurrent(builder, &entity_semantics_id))
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_ENTITY_SEMANTICS, SG_HOST_LAW_ELEMENT_NONE,
			builder->identity.entity_semantics_id, entity_semantics_id);
	result = SG_HostLawPmoveEvaluatorCurrent(builder->pmove_evaluator);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	source_status = SG_RuneSourceAuthorityCurrent(builder->source_authority);
	if (source_status != SG_RUNE_SOURCE_OK)
		return BuilderOwnerResult(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_ENTITY_SEMANTICS, SG_HOST_LAW_ELEMENT_NONE,
			SG_RUNE_SOURCE_OK, (uint64_t)source_status);
	return BuilderOwnerResult(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE,
		SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
}

static int MoverInitialTransform(const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity,
	sg_host_collision_transform_t *transform_out);

sg_host_law_result_t SG_RuneCompactBuilderOwnerTransformModelLocalQ8(
	const sg_rune_compact_builder_t *builder, uint32_t mover_entity_ordinal,
	const sg_rune_q8_vec3_t *local_vertices, uint32_t vertex_count,
	sg_rune_vec3_t *world_vertices_out, sg_rune_bounds_t *world_bounds_out)
{
	const sg_bsp_entity_semantic_t *entity;
	sg_host_collision_transform_t transform;
	sg_rune_bounds_t bounds;
	sg_host_law_result_t result;
	uint64_t entity_semantics_id = 0U;
	uint32_t vertex;

	if (local_vertices == NULL || vertex_count == 0U ||
		world_vertices_out == NULL || world_bounds_out == NULL)
		return BuilderOwnerResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, mover_entity_ordinal, 1U, 0U);
	result = BuilderOwnerCurrent(builder);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (mover_entity_ordinal >= builder->identity.source_counts.entity_count ||
		builder->entity_semantics->entity_count !=
			builder->identity.source_counts.entity_count ||
		builder->entity_semantics->entities == NULL ||
		!EntitySemanticsId(&builder->world->content_identity,
			builder->entity_semantics, &entity_semantics_id) ||
		entity_semantics_id != builder->identity.entity_semantics_id)
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_ENTITY_SEMANTICS, mover_entity_ordinal,
			builder->identity.entity_semantics_id, entity_semantics_id);
	entity = &builder->entity_semantics->entities[mover_entity_ordinal];
	if (entity->canonical_ordinal != mover_entity_ordinal ||
		entity->source_set_identity == 0U || entity->source_set_identity !=
			builder->entity_semantics->source_set_identity ||
		(entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) == 0U ||
		entity->bsp_model == SG_BSP_ENTITY_MODEL_NONE ||
		entity->bsp_model == SG_HOST_COLLISION_MODEL_WORLD ||
		entity->bsp_model >= builder->identity.source_counts.model_count ||
		entity->bsp_model >= builder->world->model_count)
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_ENTITY_SEMANTICS, mover_entity_ordinal,
			builder->identity.source_counts.model_count, entity->bsp_model);
	/* func_door_rotating clears map angles and may start at its moved endpoint.
	 * Never expose its raw parsed angles as a model pose: only its spawn-resolved
	 * finite schedule owns the initial transform.  A continuous rotator has an
	 * initial pose but no finite destination for this endpoint query. */
	if (!MoverInitialTransform(builder, entity, &transform))
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_MECHANISM_LAW, mover_entity_ordinal, 1U, 0U);
	for (vertex = 0U; vertex < vertex_count; vertex++)
	{
		float local[3];
		float world[3];
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
			local[axis] = (float)local_vertices[vertex].value[axis] * 0.125f;
		if (!TransformWithStagedBinary32Law(&transform, local, world))
			return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
				SG_HOST_LAW_FIELD_COLLISION_LAW, vertex, 1U, 0U);
		for (axis = 0U; axis < 3U; axis++)
		{
			world_vertices_out[vertex].value[axis] = world[axis];
			if (vertex == 0U)
				bounds.mins.value[axis] = bounds.maxs.value[axis] = world[axis];
			else
			{
				bounds.mins.value[axis] = fminf(bounds.mins.value[axis],
					world[axis]);
				bounds.maxs.value[axis] = fmaxf(bounds.maxs.value[axis],
					world[axis]);
			}
		}
	}
	result = BuilderOwnerCurrent(builder);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	*world_bounds_out = bounds;
	return BuilderOwnerResult(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE,
		SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
}

static int MoverModeValid(sg_rune_compact_builder_mover_mode_t mode)
{
	return mode == SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE ||
		mode == SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT;
}

static int MoverStateValid(sg_rune_compact_mechanism_authority_state_t state)
{
	return state == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE ||
		state == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
}

static int SupportPoseModeValid(
	sg_rune_compact_builder_support_pose_mode_t mode)
{
	return mode == SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_EXPLICIT ||
		mode == SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_CANONICAL;
}

static int SpanValid(uint32_t first, uint32_t count, uint32_t capacity)
{
	return first <= capacity && count <= capacity - first;
}

static int MoverEntity(const sg_rune_compact_builder_t *builder,
	uint32_t mover_entity_ordinal, const sg_bsp_entity_semantic_t **entity_out,
	sg_host_law_result_t *error_out)
{
	uint64_t entity_semantics_id = 0U;
	const sg_bsp_entity_semantic_t *entity;

	if (entity_out == NULL || error_out == NULL)
		return 0;
	*entity_out = NULL;
	*error_out = BuilderOwnerCurrent(builder);
	if (error_out->status != SG_HOST_LAW_OK)
		return 0;
	if (mover_entity_ordinal >= builder->identity.source_counts.entity_count ||
		builder->entity_semantics->entity_count !=
			builder->identity.source_counts.entity_count ||
		builder->entity_semantics->entities == NULL ||
		!EntitySemanticsId(&builder->world->content_identity,
			builder->entity_semantics, &entity_semantics_id) ||
		entity_semantics_id != builder->identity.entity_semantics_id)
	{
		*error_out = BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_ENTITY_SEMANTICS, mover_entity_ordinal,
			builder->identity.entity_semantics_id, entity_semantics_id);
		return 0;
	}
	entity = &builder->entity_semantics->entities[mover_entity_ordinal];
	if (entity->canonical_ordinal != mover_entity_ordinal ||
		entity->source_set_identity == 0U || entity->source_set_identity !=
			builder->entity_semantics->source_set_identity ||
		(entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) == 0U ||
		entity->bsp_model == SG_BSP_ENTITY_MODEL_NONE ||
		entity->bsp_model == SG_HOST_COLLISION_MODEL_WORLD ||
		entity->bsp_model >= builder->identity.source_counts.model_count ||
		entity->bsp_model >= builder->world->model_count)
	{
		*error_out = BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_ENTITY_SEMANTICS, mover_entity_ordinal,
			builder->identity.source_counts.model_count, entity->bsp_model);
		return 0;
	}
	*entity_out = entity;
	return 1;
}

static void EntityBaseTransform(const sg_bsp_entity_semantic_t *entity,
	sg_host_collision_transform_t *transform_out)
{
	memset(transform_out, 0, sizeof(*transform_out));
	memcpy(transform_out->origin, entity->origin.value,
		sizeof(transform_out->origin));
	memcpy(transform_out->angles, entity->angles.value,
		sizeof(transform_out->angles));
}

static int CollisionWorldTransformCanonical(
	const sg_host_collision_world_transform_t *transform)
{
	uint32_t local_axis;
	uint32_t world_axis;

	if (!transform)
		return 0;
	for (world_axis = 0U; world_axis < 3U; world_axis++) {
		if (!isfinite(transform->origin[world_axis]) ||
			(transform->origin[world_axis] == 0.0f &&
				FloatBits(transform->origin[world_axis]) != 0U))
			return 0;
		for (local_axis = 0U; local_axis < 3U; local_axis++)
			if (!isfinite(transform->axis[local_axis][world_axis]) ||
				(transform->axis[local_axis][world_axis] == 0.0f &&
					FloatBits(transform->axis[local_axis][world_axis]) != 0U))
				return 0;
	}
	return 1;
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerReplayLocalQ8Pose(
	const sg_rune_compact_builder_t *builder, uint32_t mover_entity_ordinal,
	const sg_host_collision_world_transform_t *transform,
	const sg_rune_q8_vec3_t *local_pose, sg_rune_vec3_t *world_pose_out)
{
	const sg_bsp_entity_semantic_t *entity;
	sg_host_law_result_t result;
	float local[3];
	float world[3];
	uint32_t world_axis;

	if (!transform || !local_pose || !world_pose_out)
		return BuilderOwnerResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, mover_entity_ordinal, 1U, 0U);
	if (!MoverEntity(builder, mover_entity_ordinal, &entity, &result))
		return result;
	if (!CollisionWorldTransformCanonical(transform))
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, mover_entity_ordinal, 1U, 0U);
	for (world_axis = 0U; world_axis < 3U; world_axis++)
		local[world_axis] = (float)local_pose->value[world_axis] * 0.125f;
	if (!SG_RuneCompactBinary32TransformPoint(local, transform->origin,
		(const float (*)[3])transform->axis, world))
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, mover_entity_ordinal, 1U, 0U);
	for (world_axis = 0U; world_axis < 3U; world_axis++)
		if (!isfinite(world[world_axis]))
			return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
				SG_HOST_LAW_FIELD_COLLISION_LAW, mover_entity_ordinal, 1U, 0U);
	result = BuilderOwnerCurrent(builder);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	for (world_axis = 0U; world_axis < 3U; world_axis++)
		world_pose_out->value[world_axis] = world[world_axis];
	return BuilderOwnerResult(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE,
		SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
}

static const sg_bsp_entity_angular_mover_t *FiniteAngularDoor(
	const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity)
{
	const sg_bsp_entity_angular_mover_t *mover;

	if (builder == NULL || entity == NULL ||
		entity->canonical_ordinal >= builder->entity_semantics->entity_count)
		return NULL;
	if (!SG_BspEntitySemanticHasFiniteAngularDoor(entity))
		return NULL;
	mover = SG_BspEntitySemanticsAngularMover(builder->entity_semantics,
		entity->canonical_ordinal);
	if (mover == NULL ||
		mover->kind != SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR ||
		mover->schedule.finite_door.frame_ms !=
			builder->host_law.mechanism.frame_ms)
		return NULL;
	return mover;
}

static const sg_bsp_entity_angular_mover_t *ContinuousAngularRotator(
	const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity)
{
	const sg_bsp_entity_angular_mover_t *mover;
	uint32_t axis;

	if (builder == NULL || entity == NULL ||
		entity->canonical_ordinal >= builder->entity_semantics->entity_count)
		return NULL;
	mover = SG_BspEntitySemanticsAngularMover(builder->entity_semantics,
		entity->canonical_ordinal);
	if (mover == NULL || mover->kind !=
		SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR ||
		mover->schedule.continuous_rotator.frame_ms !=
			builder->host_law.mechanism.frame_ms ||
		!isfinite(mover->schedule.continuous_rotator.speed) ||
		mover->schedule.continuous_rotator.speed <= 0.0f)
		return NULL;
	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(mover->schedule.continuous_rotator.initial_angles.value[axis]) ||
			!isfinite(mover->schedule.continuous_rotator.axis.value[axis]) ||
			!isfinite(mover->schedule.continuous_rotator.angular_velocity.value[axis]) ||
			!isfinite(mover->schedule.continuous_rotator.frame_angular_delta.value[axis]))
			return NULL;
	return mover;
}

/* A continuous rotator has no terminal endpoint.  A carried-support record
 * therefore certifies exactly one active server frame, from its published
 * initial phase to initial + frame_angular_delta, rather than inventing a
 * map-time phase or a finite destination. */
static int ContinuousRotatorFrameTransforms(
	const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity,
	sg_rune_compact_mechanism_authority_state_t source_state,
	sg_rune_compact_mechanism_authority_state_t destination_state,
	sg_host_collision_transform_t *source_out,
	sg_host_collision_transform_t *destination_out)
{
	const sg_bsp_entity_angular_mover_t *mover =
		ContinuousAngularRotator(builder, entity);
	uint32_t axis;

	if (mover == NULL || source_out == NULL || destination_out == NULL ||
		(source_state != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE &&
			source_state != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE) ||
		destination_state != SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE)
		return 0;
	EntityBaseTransform(entity, source_out);
	for (axis = 0U; axis < 3U; axis++) {
		source_out->angles[axis] =
			mover->schedule.continuous_rotator.initial_angles.value[axis];
		destination_out->origin[axis] = source_out->origin[axis];
		destination_out->angles[axis] = source_out->angles[axis] +
			mover->schedule.continuous_rotator.frame_angular_delta.value[axis];
		if (!isfinite(destination_out->origin[axis]) ||
			!isfinite(destination_out->angles[axis]))
			return 0;
	}
	return 1;
}

static int AngularDoorEndpoint(const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity,
	sg_rune_compact_mechanism_authority_state_t state,
	sg_host_collision_transform_t *transform_out)
{
	const sg_bsp_entity_angular_mover_t *mover = FiniteAngularDoor(builder,
		entity);
	const sg_rune_vec3_t *angles;

	if (mover == NULL || !MoverStateValid(state) || transform_out == NULL)
		return 0;
	angles = state == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE ?
		&mover->schedule.finite_door.active_angles :
		&mover->schedule.finite_door.inactive_angles;
	EntityBaseTransform(entity, transform_out);
	memcpy(transform_out->angles, angles->value, sizeof(transform_out->angles));
	return isfinite(transform_out->origin[0]) &&
		isfinite(transform_out->origin[1]) &&
		isfinite(transform_out->origin[2]) && isfinite(transform_out->angles[0]) &&
		isfinite(transform_out->angles[1]) && isfinite(transform_out->angles[2]);
}

static int MoverInitialTransform(const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity,
	sg_host_collision_transform_t *transform_out)
{
	const sg_bsp_entity_angular_mover_t *angular;

	if (builder == NULL || entity == NULL || transform_out == NULL)
		return 0;
	/* func_door_secret follows stock's two-leg motion rather than a single
	 * base-to-active transform.  Until that schedule is authenticated, even
	 * the generic local-Q8 query must not accidentally publish its map pose as
	 * a mover-state fact. */
	if (entity->mechanism_role == SG_MECH_NODE_SECRET_DOOR)
		return 0;
	angular = SG_BspEntitySemanticsAngularMover(builder->entity_semantics,
		entity->canonical_ordinal);
	if (entity->mechanism_kind == SG_RUNE_MECHANISM_ROTATOR) {
		if (angular == NULL || angular->kind !=
			SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR)
			return 0;
		return AngularDoorEndpoint(builder, entity,
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE, transform_out);
	}
	EntityBaseTransform(entity, transform_out);
	return isfinite(transform_out->origin[0]) &&
		isfinite(transform_out->origin[1]) &&
		isfinite(transform_out->origin[2]) && isfinite(transform_out->angles[0]) &&
		isfinite(transform_out->angles[1]) && isfinite(transform_out->angles[2]);
}

static int FloatQ8Exact(float value, int32_t *q8_out)
{
	double scaled;

	if (q8_out == NULL || !isfinite(value))
		return 0;
	scaled = (double)value * 8.0;
	if (scaled < (double)INT32_MIN || scaled > (double)INT32_MAX ||
		trunc(scaled) != scaled)
		return 0;
	*q8_out = (int32_t)scaled;
	return 1;
}

static int LinearMoverDistance(const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity, float *distance_out)
{
	const sg_bsp_model_t *model;
	const sg_host_mechanism_law_t *law = &builder->host_law.mechanism;
	float lip;
	float distance;
	uint32_t axis;

	if (!builder || !entity || !distance_out || !SG_HostMechanismLawValid(law))
		return 0;
	if (entity->mechanism_kind != SG_RUNE_MECHANISM_DOOR &&
		entity->mechanism_kind != SG_RUNE_MECHANISM_BUTTON)
		return 0;
	if (entity->bsp_model >= builder->world->model_count ||
		builder->world->models == NULL)
		return 0;
	model = &builder->world->models[entity->bsp_model];
	lip = (entity->flags & SG_BSP_ENTITY_LIP_DEFINED) != 0U ? entity->lip :
		(entity->mechanism_kind == SG_RUNE_MECHANISM_BUTTON ?
			law->button_default_lip : law->door_default_lip);
	distance = -lip;
	for (axis = 0U; axis < 3U; axis++)
	{
		const float size = model->maxs.value[axis] - model->mins.value[axis];

		if (!isfinite(size) || !isfinite(entity->move_direction.value[axis]))
			return 0;
		distance += fabsf(entity->move_direction.value[axis]) * size;
	}
	if (!isfinite(distance))
		return 0;
	*distance_out = distance;
	return 1;
}

static int LinearMoverEndpoint(const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity, int active,
	sg_host_collision_transform_t *transform_out)
{
	float distance;
	uint32_t axis;

	if (!builder || !entity || !transform_out)
		return 0;
	EntityBaseTransform(entity, transform_out);
	/* SP_func_door/SP_func_water swap pos1 and pos2 for START_OPEN after
	 * calculating the map-origin endpoint.  Authority INACTIVE names that
	 * post-spawn physical state, so its transform is the moved endpoint. */
	if (entity->mechanism_kind == SG_RUNE_MECHANISM_DOOR &&
		(entity->spawnflags & UINT32_C(1)) != 0U)
		active = !active;
	if (!active)
		return 1;
	if (entity->mechanism_kind == SG_RUNE_MECHANISM_LIFT)
		/* SP_func_plat's active/top endpoint is the source-map origin. */
		return 1;
	if (!LinearMoverDistance(builder, entity, &distance))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		transform_out->origin[axis] = entity->origin.value[axis] + distance *
			entity->move_direction.value[axis];
	return isfinite(transform_out->origin[0]) &&
		isfinite(transform_out->origin[1]) && isfinite(transform_out->origin[2]);
}

static int TrainEndpointTransform(const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity, uint32_t endpoint_ordinal,
	sg_host_collision_transform_t *transform_out)
{
	const sg_bsp_entity_semantic_t *endpoint;
	uint32_t axis;

	if (endpoint_ordinal >= builder->entity_semantics->entity_count ||
		entity->bsp_model >= builder->world->model_count)
		return 0;
	endpoint = &builder->entity_semantics->entities[endpoint_ordinal];
	if (endpoint->canonical_ordinal != endpoint_ordinal ||
		endpoint->mechanism_role != SG_MECH_NODE_PATH_CORNER)
		return 0;
	EntityBaseTransform(entity, transform_out);
	for (axis = 0U; axis < 3U; axis++)
		transform_out->origin[axis] = endpoint->origin.value[axis] -
			builder->world->models[entity->bsp_model].mins.value[axis];
	return isfinite(transform_out->origin[0]) &&
		isfinite(transform_out->origin[1]) && isfinite(transform_out->origin[2]);
}

static int MoverStateTransform(const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity,
	sg_rune_compact_mechanism_authority_state_t state,
	uint32_t endpoint_ordinal, sg_host_collision_transform_t *transform_out)
{
	if (!MoverStateValid(state) || !builder || !entity || !transform_out)
		return 0;
	/* func_door_secret is a two-leg stock path.  Until an authenticated
	 * intermediate transform is represented, it cannot use this one-axis
	 * endpoint evaluator. */
	if (entity->mechanism_role == SG_MECH_NODE_SECRET_DOOR)
		return 0;
	if (FiniteAngularDoor(builder, entity) != NULL)
		return AngularDoorEndpoint(builder, entity, state, transform_out);
	if (entity->mechanism_kind == SG_RUNE_MECHANISM_TRAIN)
		return TrainEndpointTransform(builder, entity, endpoint_ordinal,
			transform_out);
	if (entity->mechanism_kind == SG_RUNE_MECHANISM_LIFT &&
		state == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE)
	{
		const sg_host_mechanism_law_t *law = &builder->host_law.mechanism;
		const float lip = (entity->flags & SG_BSP_ENTITY_LIP_DEFINED) != 0U ?
			entity->lip : law->platform_default_lip;
		const float distance = (entity->flags & SG_BSP_ENTITY_HEIGHT_DEFINED) != 0U ?
			entity->height : builder->world->models[entity->bsp_model].maxs.value[2] -
				builder->world->models[entity->bsp_model].mins.value[2] - lip;

		if (!SG_HostMechanismLawValid(law) || !isfinite(distance) ||
			distance < 0.0f)
			return 0;
		EntityBaseTransform(entity, transform_out);
		transform_out->origin[2] -= distance;
		return isfinite(transform_out->origin[2]);
	}
	return LinearMoverEndpoint(builder, entity,
		state == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE,
		transform_out);
}

static int SurfaceMatchesMover(const sg_rune_compact_geometry_view_t *geometry,
	uint32_t ordinal, uint32_t model, const sg_rune_compact_source_surface_t **surface_out,
	const sg_rune_q8_vec3_t **vertices_out)
{
	const sg_rune_compact_source_surface_t *surface;

	if (surface_out == NULL || vertices_out == NULL || geometry == NULL ||
		ordinal >= geometry->source_surface_count ||
		geometry->source_surfaces == NULL ||
		geometry->source_surface_vertices == NULL)
		return 0;
	surface = &geometry->source_surfaces[ordinal];
	if (surface->frame != SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL ||
		surface->source.model != model || surface->vertices.count < 3U ||
		!SpanValid(surface->vertices.first, surface->vertices.count,
			geometry->source_surface_vertex_count))
		return 0;
	*surface_out = surface;
	*vertices_out = geometry->source_surface_vertices + surface->vertices.first;
	return 1;
}

static int TransformSurface(const sg_rune_compact_builder_t *builder,
	uint32_t model, const sg_host_collision_transform_t *transform,
	const sg_rune_q8_vec3_t *local_vertices, uint32_t count,
	sg_rune_vec3_t *vertices_out, sg_rune_bounds_t *bounds_out)
{
	sg_rune_bounds_t bounds;
	uint32_t vertex;

	(void)model;

	if (!builder || !transform || !local_vertices || count == 0U ||
		!bounds_out)
		return 0;
	for (vertex = 0U; vertex < count; vertex++)
	{
		float local[3];
		float world[3];
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
			local[axis] = (float)local_vertices[vertex].value[axis] * 0.125f;
		if (!TransformWithStagedBinary32Law(transform, local, world))
			return 0;
		for (axis = 0U; axis < 3U; axis++)
		{
			if (vertices_out)
				vertices_out[vertex].value[axis] = world[axis];
			if (vertex == 0U)
				bounds.mins.value[axis] = bounds.maxs.value[axis] = world[axis];
			else
			{
				bounds.mins.value[axis] = fminf(bounds.mins.value[axis], world[axis]);
				bounds.maxs.value[axis] = fmaxf(bounds.maxs.value[axis], world[axis]);
			}
		}
	}
	*bounds_out = bounds;
	return 1;
}

static int Q8PointOnHorizontalSurface(const sg_rune_q8_vec3_t *vertices,
	uint32_t count, const sg_rune_q8_vec3_t *point)
{
	int sign = 0;
	uint32_t vertex;

	if (!vertices || !point || count < 3U)
		return 0;
	for (vertex = 0U; vertex < count; vertex++)
	{
		const sg_rune_q8_vec3_t *left = &vertices[vertex];
		const sg_rune_q8_vec3_t *right = &vertices[(vertex + 1U) % count];
		const int64_t cross =
			((int64_t)right->value[0] - left->value[0]) *
				((int64_t)point->value[1] - left->value[1]) -
			((int64_t)right->value[1] - left->value[1]) *
				((int64_t)point->value[0] - left->value[0]);

		if (left->value[2] != point->value[2])
			return 0;
		if (cross != 0)
		{
			const int next_sign = cross < 0 ? -1 : 1;

			if (sign != 0 && sign != next_sign)
				return 0;
			sign = next_sign;
		}
	}
	return sign != 0;
}

static int CanonicalSupportPose(const sg_rune_q8_vec3_t *vertices,
	uint32_t count, const sg_rune_hull_profile_t *hull,
	sg_rune_q8_vec3_t *support_out, sg_rune_q8_vec3_t *player_out)
{
	int64_t total[2] = { 0, 0 };
	int32_t hull_offset;
	uint32_t vertex;
	uint32_t axis;

	if (!vertices || count < 3U || !hull || !support_out || !player_out ||
		!FloatQ8Exact(-hull->mins.value[2], &hull_offset))
		return 0;
	for (vertex = 0U; vertex < count; vertex++)
		for (axis = 0U; axis < 2U; axis++)
		{
			const int64_t value = vertices[vertex].value[axis];

			if ((value > 0 && total[axis] > INT64_MAX - value) ||
				(value < 0 && total[axis] < INT64_MIN - value))
				return 0;
			total[axis] += value;
		}
	/* A convex root contains its real centroid, but the centroid need not be a
	 * Q8 lattice point.  Test the deterministic floor/ceiling lattice points
	 * around it instead of rejecting a perfectly usable polygon.  The caller's
	 * subsequent host collision classification performs the actual hull erosion
	 * and support test. */
	for (axis = 0U; axis < 4U; axis++)
	{
		const int64_t denominator = (int64_t)count;
		int64_t coordinate[2];
		uint32_t coordinate_axis;

		for (coordinate_axis = 0U; coordinate_axis < 2U; coordinate_axis++)
		{
			int64_t floor_value = total[coordinate_axis] / denominator;

			if (total[coordinate_axis] < 0 &&
				total[coordinate_axis] % denominator != 0)
				floor_value--;
			coordinate[coordinate_axis] = floor_value +
				((axis >> coordinate_axis) & 1U);
			if (coordinate[coordinate_axis] < INT32_MIN ||
				coordinate[coordinate_axis] > INT32_MAX)
				return 0;
			support_out->value[coordinate_axis] =
				(int32_t)coordinate[coordinate_axis];
		}
		support_out->value[2] = vertices[0].value[2];
		if (!Q8PointOnHorizontalSurface(vertices, count, support_out))
			continue;
		*player_out = *support_out;
		{
			const int64_t player_z = (int64_t)support_out->value[2] +
				hull_offset + SG_RUNE_COMPACT_SUPPORT_CLEARANCE_Q8;

			if (player_z < INT32_MIN || player_z > INT32_MAX)
				continue;
			player_out->value[2] = (int32_t)player_z;
		}
		return 1;
	}
	return 0;
}

static void Q8ToFloatVector(const sg_rune_q8_vec3_t *q8, float value[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		value[axis] = (float)q8->value[axis] * 0.125f;
}

static int TransformLocalPose(const sg_rune_compact_builder_t *builder,
	uint32_t model, const sg_host_collision_transform_t *transform,
	const sg_rune_q8_vec3_t *local, sg_rune_vec3_t *world_out)
{
	float local_float[3];

	(void)builder;
	(void)model;

	if (!local || !world_out)
		return 0;
	Q8ToFloatVector(local, local_float);
	return TransformWithStagedBinary32Law(transform, local_float,
		world_out->value);
}

static int MoverFinalPoseValid(const sg_rune_compact_builder_t *builder,
	uint32_t model, const sg_host_collision_transform_t *transform,
	const sg_rune_vec3_t *player_world, sg_rune_stance_t stance)
{
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;
	sg_host_collision_pose_t pose;

	if (!builder || !transform || !player_world)
		return 0;
	memset(&instance, 0, sizeof(instance));
	instance.instance_id = model;
	instance.model_index = model;
	instance.transform = *transform;
	scene.instances = &instance;
	scene.instance_count = 1U;
	return SG_HostCollisionClassifyPose(&builder->collision, &scene,
		player_world->value, stance, &pose) && pose.valid;
}

static int SupportWorldPoseValid(const sg_rune_compact_builder_t *builder,
	uint32_t model, const sg_host_collision_transform_t *transform,
	const sg_rune_vec3_t *player_world, sg_rune_stance_t stance)
{
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;
	sg_host_collision_pose_t pose;

	if (!builder || !transform || !player_world)
		return 0;
	memset(&instance, 0, sizeof(instance));
	instance.instance_id = model;
	instance.model_index = model;
	instance.transform = *transform;
	scene.instances = &instance;
	scene.instance_count = 1U;
	return SG_HostCollisionClassifyPose(&builder->collision, &scene,
		player_world->value, stance, &pose) && pose.valid && pose.supported &&
		pose.support.instance_id == instance.instance_id;
}

static int SupportPoseValid(const sg_rune_compact_builder_t *builder,
	uint32_t model, const sg_host_collision_transform_t *transform,
	const sg_rune_q8_vec3_t *player_local, sg_rune_stance_t stance,
	sg_rune_vec3_t *player_world_out)
{
	return TransformLocalPose(builder, model, transform, player_local,
		player_world_out) && SupportWorldPoseValid(builder, model, transform,
		player_world_out, stance);
}

typedef struct projected_polygon_s
{
	const sg_rune_vec3_t *world_vertices;
	const sg_rune_q8_vec3_t *q8_vertices;
	uint32_t vertex_count;
	uint32_t horizontal_axis;
	uint32_t vertical_axis;
} projected_polygon_t;

static long double ProjectedCoordinate(const projected_polygon_t *polygon,
	uint32_t vertex, uint32_t axis)
{
	if (polygon->world_vertices != NULL)
		return (long double)polygon->world_vertices[vertex].value[axis];
	return (long double)polygon->q8_vertices[vertex].value[axis] * 0.125L;
}

static int PortalProjectionAxes(const sg_rune_q8_vec3_t *vertices,
	uint32_t vertex_count, uint32_t *horizontal_axis_out,
	uint32_t *vertical_axis_out)
{
	long double normal[3] = { 0.0L, 0.0L, 0.0L };
	uint32_t dropped_axis = 0U;
	uint32_t vertex;
	uint32_t axis;

	if (vertices == NULL || vertex_count < 3U ||
		horizontal_axis_out == NULL || vertical_axis_out == NULL)
		return 0;
	for (vertex = 0U; vertex < vertex_count; vertex++) {
		const sg_rune_q8_vec3_t *current = &vertices[vertex];
		const sg_rune_q8_vec3_t *next = &vertices[(vertex + 1U) % vertex_count];
		const long double current_x = (long double)current->value[0];
		const long double current_y = (long double)current->value[1];
		const long double current_z = (long double)current->value[2];
		const long double next_x = (long double)next->value[0];
		const long double next_y = (long double)next->value[1];
		const long double next_z = (long double)next->value[2];

		normal[0] += (current_y - next_y) * (current_z + next_z);
		normal[1] += (current_z - next_z) * (current_x + next_x);
		normal[2] += (current_x - next_x) * (current_y + next_y);
	}
	for (axis = 1U; axis < 3U; axis++)
		if (fabsl(normal[axis]) > fabsl(normal[dropped_axis]))
			dropped_axis = axis;
	if (normal[dropped_axis] == 0.0L)
		return 0;
	*horizontal_axis_out = (dropped_axis + 1U) % 3U;
	*vertical_axis_out = (dropped_axis + 2U) % 3U;
	return 1;
}

static int ProjectedPolygonPositiveArea(const projected_polygon_t *polygon)
{
	long double doubled_area = 0.0L;
	uint32_t vertex;

	if (polygon == NULL || polygon->vertex_count < 3U ||
		(polygon->world_vertices == NULL) == (polygon->q8_vertices == NULL))
		return 0;
	for (vertex = 0U; vertex < polygon->vertex_count; vertex++) {
		const uint32_t next = (vertex + 1U) % polygon->vertex_count;

		doubled_area += ProjectedCoordinate(polygon, vertex,
			polygon->horizontal_axis) * ProjectedCoordinate(polygon, next,
			polygon->vertical_axis) - ProjectedCoordinate(polygon, next,
			polygon->horizontal_axis) * ProjectedCoordinate(polygon, vertex,
			polygon->vertical_axis);
	}
	return isfinite(doubled_area) && doubled_area != 0.0L;
}

static int ProjectedPolygonsOverlapOnEdge(
	const projected_polygon_t *left, const projected_polygon_t *right,
	const projected_polygon_t *edge_owner, uint32_t edge)
{
	const uint32_t next = (edge + 1U) % edge_owner->vertex_count;
	const long double edge_x = ProjectedCoordinate(edge_owner, next,
		edge_owner->horizontal_axis) - ProjectedCoordinate(edge_owner, edge,
		edge_owner->horizontal_axis);
	const long double edge_y = ProjectedCoordinate(edge_owner, next,
		edge_owner->vertical_axis) - ProjectedCoordinate(edge_owner, edge,
		edge_owner->vertical_axis);
	const long double edge_length = hypotl(edge_x, edge_y);
	long double left_min = 0.0L;
	long double left_max = 0.0L;
	long double right_min = 0.0L;
	long double right_max = 0.0L;
	long double axis_x;
	long double axis_y;
	uint32_t vertex;

	if (!isfinite(edge_length) || edge_length == 0.0L)
		return 1;
	axis_x = -edge_y / edge_length;
	axis_y = edge_x / edge_length;
	for (vertex = 0U; vertex < left->vertex_count; vertex++) {
		const long double projection = axis_x * ProjectedCoordinate(left,
			vertex, left->horizontal_axis) + axis_y * ProjectedCoordinate(left,
			vertex, left->vertical_axis);

		if (!isfinite(projection))
			return 0;
		if (vertex == 0U)
			left_min = left_max = projection;
		else {
			left_min = fminl(left_min, projection);
			left_max = fmaxl(left_max, projection);
		}
	}
	for (vertex = 0U; vertex < right->vertex_count; vertex++) {
		const long double projection = axis_x * ProjectedCoordinate(right,
			vertex, right->horizontal_axis) + axis_y * ProjectedCoordinate(right,
			vertex, right->vertical_axis);

		if (!isfinite(projection))
			return 0;
		if (vertex == 0U)
			right_min = right_max = projection;
		else {
			right_min = fminl(right_min, projection);
			right_max = fmaxl(right_max, projection);
		}
	}
	return fminl(left_max, right_max) > fmaxl(left_min, right_min);
}

/* Collision proves that a mover volume closes the world portal.  This
 * projected positive-area test separately binds that proof to the exact
 * authenticated model-local source surface selected by the transition.  The
 * projection intentionally tolerates the offset faces of thick movers while
 * refusing another surface elsewhere in the same bmodel. */
static int PortalSurfacePositiveAreaOverlap(
	const sg_rune_compact_geometry_view_t *geometry, uint32_t portal_ordinal,
	const sg_rune_vec3_t *surface_vertices, uint32_t surface_vertex_count,
	int *overlap_out)
{
	const sg_rune_compact_portal_t *portal;
	const sg_rune_compact_facet_t *facet;
	projected_polygon_t portal_polygon;
	projected_polygon_t surface_polygon;
	uint32_t horizontal_axis;
	uint32_t vertical_axis;
	uint32_t edge;

	if (geometry == NULL || surface_vertices == NULL || overlap_out == NULL ||
		geometry->portals == NULL || geometry->facets == NULL ||
		geometry->vertices == NULL || portal_ordinal >= geometry->portal_count ||
		surface_vertex_count < 3U)
		return 0;
	*overlap_out = 0;
	portal = &geometry->portals[portal_ordinal];
	if (portal->facet.value >= geometry->facet_count)
		return 0;
	facet = &geometry->facets[portal->facet.value];
	if (facet->kind != SG_RUNE_COMPACT_FACET_POLYGON ||
		facet->vertices.count < 3U || !SpanValid(facet->vertices.first,
			facet->vertices.count, geometry->vertex_count))
		return 0;
	if (!PortalProjectionAxes(geometry->vertices + facet->vertices.first,
		facet->vertices.count, &horizontal_axis, &vertical_axis))
		return 0;
	memset(&portal_polygon, 0, sizeof(portal_polygon));
	portal_polygon.q8_vertices = geometry->vertices + facet->vertices.first;
	portal_polygon.vertex_count = facet->vertices.count;
	portal_polygon.horizontal_axis = horizontal_axis;
	portal_polygon.vertical_axis = vertical_axis;
	memset(&surface_polygon, 0, sizeof(surface_polygon));
	surface_polygon.world_vertices = surface_vertices;
	surface_polygon.vertex_count = surface_vertex_count;
	surface_polygon.horizontal_axis = horizontal_axis;
	surface_polygon.vertical_axis = vertical_axis;
	if (!ProjectedPolygonPositiveArea(&portal_polygon) ||
		!ProjectedPolygonPositiveArea(&surface_polygon))
		return 1;
	for (edge = 0U; edge < portal_polygon.vertex_count; edge++)
		if (!ProjectedPolygonsOverlapOnEdge(&portal_polygon, &surface_polygon,
			&portal_polygon, edge))
			return 1;
	for (edge = 0U; edge < surface_polygon.vertex_count; edge++)
		if (!ProjectedPolygonsOverlapOnEdge(&portal_polygon, &surface_polygon,
			&surface_polygon, edge))
			return 1;
	*overlap_out = 1;
	return 1;
}

static int PolygonHasPositiveArea3D(const sg_rune_vec3_t *vertices,
	uint32_t vertex_count)
{
	uint32_t vertex;

	if (vertices == NULL || vertex_count < 3U)
		return 0;
	for (vertex = 1U; vertex + 1U < vertex_count; vertex++) {
		long double first[3];
		long double second[3];
		long double cross[3];
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++) {
			first[axis] = (long double)vertices[vertex].value[axis] -
				(long double)vertices[0].value[axis];
			second[axis] = (long double)vertices[vertex + 1U].value[axis] -
				(long double)vertices[0].value[axis];
		}
		cross[0] = first[1] * second[2] - first[2] * second[1];
		cross[1] = first[2] * second[0] - first[0] * second[2];
		cross[2] = first[0] * second[1] - first[1] * second[0];
		if (isfinite(cross[0]) && isfinite(cross[1]) && isfinite(cross[2]) &&
			(cross[0] != 0.0L || cross[1] != 0.0L || cross[2] != 0.0L))
			return 1;
	}
	return 0;
}

static int SelectedBrushSideBoundsPortalNormal(const sg_bsp_world_t *world,
	const sg_bsp_brush_t *brush, uint32_t selected_brush_side,
	const sg_rune_vec3_t *portal_vertices, uint32_t portal_vertex_count,
	const sg_rune_vec3_t *occupied_vertices, uint32_t occupied_vertex_count,
	int *bounds_out)
{
	long double portal_normal[3] = { 0.0L, 0.0L, 0.0L };
	long double occupied_center[3] = { 0.0L, 0.0L, 0.0L };
	long double nearest_distance = HUGE_VALL;
	long double selected_distance = HUGE_VALL;
	uint32_t vertex;
	uint32_t side_offset;
	uint32_t axis;

	if (world == NULL || brush == NULL || portal_vertices == NULL ||
		occupied_vertices == NULL || bounds_out == NULL ||
		portal_vertex_count < 3U || occupied_vertex_count < 3U)
		return 0;
	*bounds_out = 0;
	for (vertex = 0U; vertex < portal_vertex_count; vertex++) {
		const uint32_t next = (vertex + 1U) % portal_vertex_count;
		const long double current_x = portal_vertices[vertex].value[0];
		const long double current_y = portal_vertices[vertex].value[1];
		const long double current_z = portal_vertices[vertex].value[2];
		const long double next_x = portal_vertices[next].value[0];
		const long double next_y = portal_vertices[next].value[1];
		const long double next_z = portal_vertices[next].value[2];

		portal_normal[0] += (current_y - next_y) * (current_z + next_z);
		portal_normal[1] += (current_z - next_z) * (current_x + next_x);
		portal_normal[2] += (current_x - next_x) * (current_y + next_y);
	}
	if (portal_normal[0] == 0.0L && portal_normal[1] == 0.0L &&
		portal_normal[2] == 0.0L)
		return 0;
	for (vertex = 0U; vertex < occupied_vertex_count; vertex++)
		for (axis = 0U; axis < 3U; axis++)
			occupied_center[axis] +=
				(long double)occupied_vertices[vertex].value[axis];
	for (axis = 0U; axis < 3U; axis++)
		occupied_center[axis] /= (long double)occupied_vertex_count;
	for (side_offset = 0U; side_offset < brush->side_count; side_offset++) {
		const uint32_t side_index = brush->first_side + side_offset;
		const sg_bsp_brush_side_t *side = &world->brush_sides[side_index];
		const sg_bsp_plane_t *plane;
		long double clearance;
		long double direction = 0.0L;
		long double distance;

		if (side->plane >= world->plane_count)
			return 0;
		plane = &world->planes[side->plane];
		clearance = (long double)plane->distance;
		for (axis = 0U; axis < 3U; axis++) {
			if (!isfinite(plane->normal.value[axis]))
				return 0;
			clearance -= occupied_center[axis] *
				(long double)plane->normal.value[axis];
			direction += portal_normal[axis] *
				(long double)plane->normal.value[axis];
		}
		if (!isfinite(plane->distance) || !isfinite(clearance) ||
			!isfinite(direction) || clearance < 0.0L)
			return 0;
		if (direction == 0.0L)
			continue;
		distance = fabsl(clearance / direction);
		if (!isfinite(distance))
			return 0;
		if (distance < nearest_distance)
			nearest_distance = distance;
		if (side_index == selected_brush_side)
			selected_distance = distance;
	}
	*bounds_out = isfinite(nearest_distance) &&
		selected_distance == nearest_distance;
	return 1;
}

/* The projected polygon binds a source face to the portal footprint.  This
 * second certificate binds its exact BSP brush to the occupied depth: the
 * portal must retain positive area after clipping against that brush in the
 * selected mover state.  A parallel face from another volume cannot borrow a
 * model-wide collision result. */
static sg_host_law_status_t PortalSourceBrushPositiveAreaOverlap(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_view_t *geometry, uint32_t portal_ordinal,
	const sg_rune_compact_source_surface_t *surface,
	const sg_host_collision_transform_t *transform, int *overlap_out)
{
	const sg_rune_compact_portal_t *portal;
	const sg_rune_compact_facet_t *facet;
	const sg_bsp_brush_t *brush;
	sg_host_collision_world_transform_t world_transform;
	sg_rune_vec3_t *storage = NULL;
	const sg_rune_vec3_t *current;
	sg_rune_vec3_t *other;
	sg_rune_vec3_t *scratch_a;
	sg_rune_vec3_t *scratch_b;
	size_t polygon_bytes;
	size_t storage_bytes;
	uint32_t capacity;
	uint32_t current_count;
	uint32_t side_offset;
	uint32_t vertex;
	sg_host_law_status_t status = SG_HOST_LAW_EVALUATION_FAILED;
	int selected_side_bounds_portal;

	if (builder == NULL || builder->world == NULL || geometry == NULL ||
		surface == NULL || transform == NULL || overlap_out == NULL ||
		geometry->portals == NULL || geometry->facets == NULL ||
		geometry->vertices == NULL || portal_ordinal >= geometry->portal_count ||
		builder->world->brushes == NULL || builder->world->brush_sides == NULL ||
		builder->world->planes == NULL ||
		surface->source.brush >= builder->world->brush_count)
		return status;
	*overlap_out = 0;
	portal = &geometry->portals[portal_ordinal];
	if (portal->facet.value >= geometry->facet_count)
		return status;
	facet = &geometry->facets[portal->facet.value];
	if (facet->kind != SG_RUNE_COMPACT_FACET_POLYGON ||
		facet->vertices.count < 3U || !SpanValid(facet->vertices.first,
			facet->vertices.count, geometry->vertex_count))
		return status;
	brush = &builder->world->brushes[surface->source.brush];
	if (brush->first_side > builder->world->brush_side_count ||
		brush->side_count > builder->world->brush_side_count - brush->first_side ||
		brush->side_count == 0U ||
		surface->source.brush_side < brush->first_side ||
		surface->source.brush_side >= brush->first_side + brush->side_count ||
		surface->source.plane !=
			builder->world->brush_sides[surface->source.brush_side].plane)
		return status;
	if (((sg_host_collision_contents_t)brush->contents &
		SG_HOST_MASK_PLAYER_SOLID) == 0U) {
		status = SG_HOST_LAW_OK;
		goto done;
	}
	if (facet->vertices.count > UINT32_MAX - brush->side_count)
		return SG_HOST_LAW_ALLOCATION_FAILED;
	capacity = facet->vertices.count + brush->side_count;
	if (!SizeMultiply((size_t)capacity, sizeof(*storage), &polygon_bytes) ||
		!SizeMultiply(polygon_bytes, 3U, &storage_bytes))
		return SG_HOST_LAW_ALLOCATION_FAILED;
	storage = malloc(storage_bytes);
	if (storage == NULL)
		return SG_HOST_LAW_ALLOCATION_FAILED;
	scratch_a = storage + capacity;
	scratch_b = scratch_a + capacity;
	if (!SG_HostCollisionWorldTransform(transform, &world_transform))
		goto done;
	for (vertex = 0U; vertex < facet->vertices.count; vertex++) {
		float delta[3];
		uint32_t local_axis;
		uint32_t world_axis;

		for (world_axis = 0U; world_axis < 3U; world_axis++)
			delta[world_axis] = (float)geometry->vertices[
				facet->vertices.first + vertex].value[world_axis] * 0.125f -
				world_transform.origin[world_axis];
		for (local_axis = 0U; local_axis < 3U; local_axis++) {
			float coordinate = 0.0f;

			for (world_axis = 0U; world_axis < 3U; world_axis++)
				coordinate += delta[world_axis] *
					world_transform.axis[local_axis][world_axis];
			if (!isfinite(coordinate))
				goto done;
			storage[vertex].value[local_axis] = coordinate;
		}
	}
	current = storage;
	other = scratch_a;
	current_count = facet->vertices.count;
	for (side_offset = 0U; side_offset < brush->side_count; side_offset++) {
		const sg_bsp_brush_side_t *side = &builder->world->brush_sides[
			brush->first_side + side_offset];
		const sg_bsp_plane_t *plane;
		float previous[3];
		float previous_distance;
		int previous_inside;
		uint32_t output_count = 0U;

		if (side->plane >= builder->world->plane_count)
			goto done;
		if (current_count == 0U) {
			status = SG_HOST_LAW_OK;
			goto done;
		}
		plane = &builder->world->planes[side->plane];
		memcpy(previous, current[current_count - 1U].value, sizeof(previous));
		previous_distance = previous[0] * plane->normal.value[0] +
			previous[1] * plane->normal.value[1] +
			previous[2] * plane->normal.value[2] - plane->distance;
		if (!isfinite(previous_distance))
			goto done;
		previous_inside = previous_distance <= 0.0f;
		for (vertex = 0U; vertex < current_count; vertex++) {
			const float *point = current[vertex].value;
			const float distance = point[0] * plane->normal.value[0] +
				point[1] * plane->normal.value[1] +
				point[2] * plane->normal.value[2] - plane->distance;
			const int inside = distance <= 0.0f;

			if (!isfinite(distance))
				goto done;
			if (inside != previous_inside) {
				const float denominator = previous_distance - distance;
				const float fraction = previous_distance / denominator;
				uint32_t axis;

				if (!isfinite(denominator) || denominator == 0.0f ||
					!isfinite(fraction) || output_count >= capacity)
					goto done;
				for (axis = 0U; axis < 3U; axis++) {
					other[output_count].value[axis] = previous[axis] + fraction *
						(point[axis] - previous[axis]);
					if (!isfinite(other[output_count].value[axis]))
						goto done;
				}
				output_count++;
			}
			if (inside) {
				if (output_count >= capacity)
					goto done;
				other[output_count++] = current[vertex];
			}
			memcpy(previous, point, sizeof(previous));
			previous_distance = distance;
			previous_inside = inside;
		}
		current = other;
		other = other == scratch_a ? scratch_b : scratch_a;
		current_count = output_count;
	}
	if (!PolygonHasPositiveArea3D(current, current_count)) {
		status = SG_HOST_LAW_OK;
		goto done;
	}
	if (!SelectedBrushSideBoundsPortalNormal(builder->world, brush,
		surface->source.brush_side, storage, facet->vertices.count, current,
		current_count, &selected_side_bounds_portal))
		goto done;
	*overlap_out = selected_side_bounds_portal;
	status = SG_HOST_LAW_OK;
done:
	free(storage);
	return status;
}

/* Collision owns the inverse transform and BSP brush clipping.  This owner
 * only translates authenticated Q8 portal vertices into its exact binary32
 * input and preserves allocation/evaluation failures as host-law failures. */
static sg_host_law_status_t PortalModelPositiveAreaOverlap(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_view_t *geometry, uint32_t portal_ordinal,
	uint32_t model, const sg_host_collision_transform_t *transform,
	int *blocked_out)
{
	const sg_rune_compact_portal_t *portal;
	const sg_rune_compact_facet_t *facet;
	sg_rune_vec3_t *world_vertices = NULL;
	size_t vertex_bytes;
	uint32_t axis;
	uint32_t vertex;
	int blocked;
	sg_host_law_status_t status = SG_HOST_LAW_EVALUATION_FAILED;

	if (blocked_out == NULL || builder == NULL || transform == NULL ||
		geometry == NULL || geometry->portals == NULL ||
		geometry->facets == NULL || geometry->vertices == NULL ||
		portal_ordinal >= geometry->portal_count)
		return status;
	*blocked_out = 0;
	portal = &geometry->portals[portal_ordinal];
	if (portal->facet.value >= geometry->facet_count)
		return status;
	facet = &geometry->facets[portal->facet.value];
	if (facet->kind != SG_RUNE_COMPACT_FACET_POLYGON ||
		facet->vertices.count < 3U || !SpanValid(facet->vertices.first,
			facet->vertices.count, geometry->vertex_count))
		return status;
	if (!SizeMultiply((size_t)facet->vertices.count,
		sizeof(*world_vertices), &vertex_bytes))
		return SG_HOST_LAW_ALLOCATION_FAILED;
	world_vertices = malloc(vertex_bytes);
	if (world_vertices == NULL)
		return SG_HOST_LAW_ALLOCATION_FAILED;
	for (vertex = 0U; vertex < facet->vertices.count; vertex++)
		for (axis = 0U; axis < 3U; axis++)
			world_vertices[vertex].value[axis] = (float)geometry->vertices[
				facet->vertices.first + vertex].value[axis] * 0.125f;
	if (SG_HostCollisionModelPositiveAreaPolygonOverlap(&builder->collision,
		model, transform, world_vertices, facet->vertices.count,
		SG_HOST_MASK_PLAYER_SOLID, &blocked)) {
		*blocked_out = blocked;
		status = SG_HOST_LAW_OK;
	}
	free(world_vertices);
	return status;
}

static int TrainEndpointReachable(const sg_bsp_entity_semantics_t *semantics,
	uint32_t train, uint32_t endpoint, int *reachable_out,
	sg_host_law_status_t *failure_out)
{
	uint8_t *visited;
	uint32_t *pending;
	uint32_t head = 0U;
	uint32_t tail = 0U;
	size_t visited_bytes;
	size_t pending_bytes;
	int found = 0;
	int graph_valid = 1;

	if (reachable_out == NULL || failure_out == NULL)
		return 0;
	*reachable_out = 0;
	*failure_out = SG_HOST_LAW_OK;
	if (semantics == NULL || semantics->entities == NULL ||
		(semantics->edge_count != 0U && semantics->edges == NULL) ||
		semantics->entity_count == 0U || train >= semantics->entity_count ||
		endpoint >= semantics->entity_count) {
		*failure_out = SG_HOST_LAW_EVALUATION_FAILED;
		return 0;
	}
	if (!SizeMultiply((size_t)semantics->entity_count, sizeof(*visited),
		&visited_bytes) || !SizeMultiply((size_t)semantics->entity_count,
		sizeof(*pending), &pending_bytes)) {
		*failure_out = SG_HOST_LAW_ALLOCATION_FAILED;
		return 0;
	}
#if defined(SG_COMPACT_BUILDER_TEST_HOOKS)
	if (train_route_fail_next_allocation) {
		train_route_fail_next_allocation = 0;
		*failure_out = SG_HOST_LAW_ALLOCATION_FAILED;
		return 0;
	}
#endif
	visited = calloc(1U, visited_bytes);
	pending = malloc(pending_bytes);
	if (visited == NULL || pending == NULL) {
		free(pending);
		free(visited);
		*failure_out = SG_HOST_LAW_ALLOCATION_FAILED;
		return 0;
	}
	visited[train] = 1U;
	pending[tail++] = train;
	while (head < tail) {
		const uint32_t source = pending[head++];
		uint32_t edge;

		for (edge = 0U; edge < semantics->edge_count; edge++) {
			const sg_bsp_entity_semantic_edge_t *candidate =
				&semantics->edges[edge];
			uint32_t destination;

			if (candidate->source != source ||
				candidate->kind != SG_MECH_EDGE_TARGET)
				continue;
			if (candidate->destination >= semantics->entity_count) {
				graph_valid = 0;
				goto done;
			}
			destination = candidate->destination;
			if (semantics->entities[destination].canonical_ordinal != destination ||
				semantics->entities[destination].mechanism_role !=
					SG_MECH_NODE_PATH_CORNER) {
				graph_valid = 0;
				goto done;
			}
			if (destination == endpoint) {
				found = 1;
				goto done;
			}
			if (!visited[destination]) {
				visited[destination] = 1U;
				pending[tail++] = destination;
			}
		}
	}
done:
	free(pending);
	free(visited);
	if (!graph_valid) {
		*failure_out = SG_HOST_LAW_EVALUATION_FAILED;
		return 0;
	}
	*reachable_out = found;
	return 1;
}

static int TargetEndpointFanout(const sg_bsp_entity_semantics_t *semantics,
	uint32_t source, uint32_t destination, uint32_t fanout)
{
	uint32_t edge;
	uint32_t matches = 0U;

	for (edge = 0U; edge < semantics->edge_count; edge++) {
		const sg_bsp_entity_semantic_edge_t *candidate =
			&semantics->edges[edge];

		if (candidate->source != source ||
			candidate->kind != SG_MECH_EDGE_TARGET ||
			candidate->fanout_ordinal != fanout)
			continue;
		if (candidate->destination != destination || matches != 0U)
			return 0;
		matches++;
	}
	return matches == 1U;
}

static int TrainRouteValid(const sg_bsp_entity_semantics_t *semantics,
	uint32_t train, uint32_t source, uint32_t destination, uint32_t fanout,
	int *valid_out, sg_host_law_status_t *failure_out)
{
	int reachable;

	if (valid_out == NULL || failure_out == NULL)
		return 0;
	*valid_out = 0;
	*failure_out = SG_HOST_LAW_OK;
	if (semantics == NULL || semantics->entities == NULL ||
		(semantics->edge_count != 0U && semantics->edges == NULL) ||
		semantics->entity_count == 0U) {
		*failure_out = SG_HOST_LAW_EVALUATION_FAILED;
		return 0;
	}
	if (train >= semantics->entity_count ||
		source >= semantics->entity_count || destination >= semantics->entity_count ||
		semantics->entities[train].canonical_ordinal != train ||
		semantics->entities[train].mechanism_kind != SG_RUNE_MECHANISM_TRAIN ||
		semantics->entities[train].mechanism_role != SG_MECH_NODE_TRAIN ||
		semantics->entities[source].canonical_ordinal != source ||
		semantics->entities[source].mechanism_role != SG_MECH_NODE_PATH_CORNER ||
		semantics->entities[destination].canonical_ordinal != destination ||
		semantics->entities[destination].mechanism_role != SG_MECH_NODE_PATH_CORNER ||
		fanout == SG_RUNE_COMPACT_INDEX_NONE)
		return 1;
	/* A train may already be at any path_corner reached through the finite
	 * TARGET graph.  Traverse only authenticated path_corner edges, with a
	 * visited set bounded by the actual entity set, then bind the selected next
	 * edge's fanout exactly. */
	if (!TrainEndpointReachable(semantics, train, source, &reachable,
		failure_out))
		return 0;
	*valid_out = reachable && TargetEndpointFanout(semantics, source,
		destination, fanout);
	return 1;
}

typedef struct mover_schedule_input_s
{
	int angular;
	float delta[3];
	float distance;
	float speed;
	float accel;
	float decel;
} mover_schedule_input_t;

static int MoverScheduleInput(const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity,
	const sg_host_collision_transform_t *source,
	const sg_host_collision_transform_t *destination,
	mover_schedule_input_t *input_out)
{
	const sg_bsp_entity_angular_mover_t *angular_door;
	uint32_t axis;

	if (!builder || !entity || !source || !destination || !input_out)
		return 0;
	memset(input_out, 0, sizeof(*input_out));
	angular_door = FiniteAngularDoor(builder, entity);
	if (angular_door != NULL) {
		input_out->angular = 1;
		for (axis = 0U; axis < 3U; axis++) {
			input_out->delta[axis] = destination->angles[axis] -
				source->angles[axis];
			if (!isfinite(input_out->delta[axis]))
				return 0;
		}
		input_out->speed = angular_door->schedule.finite_door.speed;
		return isfinite(input_out->speed) && input_out->speed > 0.0f;
	}
	for (axis = 0U; axis < 3U; axis++)
		input_out->delta[axis] = destination->origin[axis] -
			source->origin[axis];
	input_out->distance = sqrtf(input_out->delta[0] * input_out->delta[0] +
		input_out->delta[1] * input_out->delta[1] +
		input_out->delta[2] * input_out->delta[2]);
	if (!isfinite(input_out->distance))
		return 0;
	switch (entity->mechanism_kind)
	{
	case SG_RUNE_MECHANISM_DOOR:
		input_out->speed = entity->speed != 0.0f ? entity->speed :
			builder->host_law.mechanism.door_default_speed;
		input_out->accel = entity->acceleration != 0.0f ?
			entity->acceleration : input_out->speed;
		input_out->decel = entity->deceleration != 0.0f ?
			entity->deceleration : input_out->speed;
		break;
	case SG_RUNE_MECHANISM_BUTTON:
		input_out->speed = entity->speed != 0.0f ? entity->speed :
			builder->host_law.mechanism.button_default_speed;
		input_out->accel = entity->acceleration != 0.0f ?
			entity->acceleration : input_out->speed;
		input_out->decel = entity->deceleration != 0.0f ?
			entity->deceleration : input_out->speed;
		break;
	case SG_RUNE_MECHANISM_LIFT:
		input_out->speed = entity->speed != 0.0f ? entity->speed * 0.1f :
			builder->host_law.mechanism.platform_default_speed;
		input_out->accel = entity->acceleration != 0.0f ?
			entity->acceleration * 0.1f :
			builder->host_law.mechanism.platform_default_accel;
		input_out->decel = entity->deceleration != 0.0f ?
			entity->deceleration * 0.1f :
			builder->host_law.mechanism.platform_default_decel;
		break;
	case SG_RUNE_MECHANISM_TRAIN:
		input_out->speed = entity->speed != 0.0f ? entity->speed :
			builder->host_law.mechanism.train_default_speed;
		input_out->accel = input_out->speed;
		input_out->decel = input_out->speed;
		break;
	default:
		return 0;
	}
	return isfinite(input_out->speed) && isfinite(input_out->accel) &&
		isfinite(input_out->decel);
}

static int MoverScheduleRun(const sg_rune_compact_builder_t *builder,
	const mover_schedule_input_t *input, int current_entity,
	sg_host_mechanism_move_frame_fn linear_frame, void *linear_context,
	sg_host_mechanism_angle_frame_fn angular_frame, void *angular_context,
	uint64_t *elapsed_out)
{
	sg_host_mechanism_move_result_t schedule;

	if (!builder || !input || !elapsed_out)
		return 0;
	if (input->angular) {
		if (linear_frame != NULL ||
			(angular_frame != NULL && !SG_HostMechanismAngleMoveFrames(
				&builder->host_law.mechanism, input->delta, input->speed,
				current_entity, angular_frame, angular_context, &schedule)) ||
			(angular_frame == NULL && !SG_HostMechanismAngleMoveSchedule(
				&builder->host_law.mechanism, input->delta, input->speed,
				current_entity, &schedule)) || !schedule.valid)
			return 0;
	}
	else if ((linear_frame != NULL && !SG_HostMechanismMoveFrames(
				&builder->host_law.mechanism, input->distance, input->speed,
				input->accel, input->decel, current_entity, linear_frame,
				linear_context,
				&schedule)) ||
		(angular_frame != NULL || (linear_frame == NULL &&
			!SG_HostMechanismMoveSchedule(
				&builder->host_law.mechanism, input->distance, input->speed,
				input->accel, input->decel, current_entity, &schedule))) ||
		!schedule.valid)
		return 0;
	*elapsed_out = schedule.completion_ms;
	return 1;
}

static int MoverSchedule(const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity,
	const sg_host_collision_transform_t *source,
	const sg_host_collision_transform_t *destination,
	sg_host_mechanism_move_frame_fn linear_frame, void *linear_context,
	sg_host_mechanism_angle_frame_fn angular_frame, void *angular_context,
	uint64_t *elapsed_out)
{
	mover_schedule_input_t input;

	if (!MoverScheduleInput(builder, entity, source, destination, &input))
		return 0;
	/* Doors/buttons/plats are armed from an activator's dispatch, while the
	 * catalog's train route callback runs as the current pusher entity. */
	return MoverScheduleRun(builder, &input,
		entity->mechanism_kind == SG_RUNE_MECHANISM_TRAIN,
		input.angular ? NULL : linear_frame,
		input.angular ? NULL : linear_context,
		input.angular ? angular_frame : NULL,
		input.angular ? angular_context : NULL, elapsed_out);
}

typedef struct team_portal_member_s
{
	uint32_t entity_ordinal;
	const sg_bsp_entity_semantic_t *entity;
	sg_host_collision_transform_t source;
	sg_host_collision_transform_t destination;
	mover_schedule_input_t schedule;
} team_portal_member_t;

static int TeamPortalDoorMemberValid(const sg_rune_compact_builder_t *builder,
	const sg_bsp_entity_semantic_t *entity, uint32_t ordinal)
{
	if (builder == NULL || entity == NULL || ordinal >=
		builder->entity_semantics->entity_count ||
		entity->canonical_ordinal != ordinal ||
		entity->mechanism_role != SG_MECH_NODE_DOOR_MASTER ||
		entity->mechanism_role == SG_MECH_NODE_SECRET_DOOR ||
		(entity->mechanism_kind != SG_RUNE_MECHANISM_DOOR &&
			!SG_BspEntitySemanticHasFiniteAngularDoor(entity)) ||
		(entity->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) == 0U ||
		entity->bsp_model == SG_BSP_ENTITY_MODEL_NONE ||
		entity->bsp_model == SG_HOST_COLLISION_MODEL_WORLD ||
		entity->bsp_model >= builder->world->model_count)
		return 0;
	return 1;
}

static int TeamPortalMemberDistance(const sg_rune_compact_builder_t *builder,
	const team_portal_member_t *member, float *distance_out)
{
	float squared = 0.0f;
	uint32_t axis;

	if (builder == NULL || member == NULL || member->entity == NULL ||
		distance_out == NULL)
		return 0;
	if (!member->schedule.angular) {
		/* Think_CalcMoveSpeed uses moveinfo.distance, not the magnitude
		 * reconstructed after VectorMA.  Reuse the endpoint's stock distance
		 * derivation so oblique doors retain the engine's operation order. */
		if (!LinearMoverDistance(builder, member->entity, distance_out))
			return 0;
		*distance_out = fabsf(*distance_out);
		return isfinite(*distance_out);
	}
	for (axis = 0U; axis < 3U; axis++) {
		squared += member->schedule.delta[axis] * member->schedule.delta[axis];
		if (!isfinite(squared))
			return 0;
	}
	*distance_out = (float)sqrt((double)squared);
	return isfinite(*distance_out);
}

/* The engine's G_FindTeams creates one captain followed by every edict with
 * the same team key.  Effective semantics records that exact post-filtered
 * relation as direct MEMBER -> MASTER TEAM edges.  The group query derives
 * its whole set from those edges; callers name only the root and one catalog
 * panel whose transformed vertices they need for candidate construction. */
static sg_host_law_status_t TeamPortalCertification(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_builder_mover_request_t *request,
	sg_rune_compact_builder_mover_result_t *result, int *applicable_out)
{
	const sg_bsp_entity_semantics_t *semantics;
	team_portal_member_t *members = NULL;
	uint32_t member_count = 0U;
	uint32_t member_capacity;
	uint32_t team_name = SG_BSP_ENTITY_STRING_NONE;
	uint32_t index;
	size_t member_bytes;
	float min_distance;
	float group_time;
	uint64_t elapsed = 0U;
	sg_host_law_status_t status = SG_HOST_LAW_EVALUATION_FAILED;
	int selected_found = 0;

	if (applicable_out == NULL || builder == NULL || geometry == NULL ||
		request == NULL || result == NULL)
		return status;
	*applicable_out = 0;
	semantics = builder->entity_semantics;
	if (request->team_master_entity_ordinal >= semantics->entity_count ||
		!TeamPortalDoorMemberValid(builder, &semantics->entities[
			request->team_master_entity_ordinal],
			request->team_master_entity_ordinal))
		return SG_HOST_LAW_OK;
	member_capacity = semantics->entity_count;
	if (!SizeMultiply((size_t)member_capacity, sizeof(*members),
		&member_bytes))
		return SG_HOST_LAW_ALLOCATION_FAILED;
	members = calloc(member_capacity, sizeof(*members));
	if (members == NULL)
		return SG_HOST_LAW_ALLOCATION_FAILED;
	members[member_count].entity_ordinal = request->team_master_entity_ordinal;
	members[member_count].entity = &semantics->entities[
		request->team_master_entity_ordinal];
	member_count++;
	for (index = 0U; index < semantics->edge_count; index++) {
		const sg_bsp_entity_semantic_edge_t *edge = &semantics->edges[index];
		const sg_bsp_entity_semantic_t *member;

		if (edge->kind != SG_MECH_EDGE_TEAM ||
			edge->destination != request->team_master_entity_ordinal)
			continue;
		if (edge->source >= semantics->entity_count ||
			edge->source == request->team_master_entity_ordinal ||
			edge->fanout_ordinal != 0U || edge->name ==
			SG_BSP_ENTITY_STRING_NONE || (team_name !=
			SG_BSP_ENTITY_STRING_NONE && edge->name != team_name) ||
			member_count >= member_capacity) {
			status = SG_HOST_LAW_EVALUATION_FAILED;
			goto done;
		}
		team_name = edge->name;
		member = &semantics->entities[edge->source];
		if (!TeamPortalDoorMemberValid(builder, member, edge->source)) {
			status = SG_HOST_LAW_OK;
			goto done;
		}
		members[member_count].entity_ordinal = edge->source;
		members[member_count].entity = member;
		member_count++;
	}
	/* A team-root query is deliberately distinct from a single-panel query. */
	if (member_count < 2U)
	{
		status = SG_HOST_LAW_OK;
		goto done;
	}
	for (index = 0U; index < member_count; index++) {
		team_portal_member_t *member = &members[index];
		sg_host_law_status_t overlap_status;
		int source_blocked;
		int destination_blocked;

		if (!MoverStateTransform(builder, member->entity, request->source_state,
			SG_RUNE_COMPACT_INDEX_NONE, &member->source) ||
			!MoverStateTransform(builder, member->entity,
				request->destination_state, SG_RUNE_COMPACT_INDEX_NONE,
				&member->destination) || !MoverScheduleInput(builder,
				member->entity, &member->source, &member->destination,
				&member->schedule)) {
			status = SG_HOST_LAW_EVALUATION_FAILED;
			goto done;
		}
		overlap_status = PortalModelPositiveAreaOverlap(builder, geometry,
			request->portal_ordinal, member->entity->bsp_model, &member->source,
			&source_blocked);
		if (overlap_status != SG_HOST_LAW_OK) {
			status = overlap_status;
			goto done;
		}
		overlap_status = PortalModelPositiveAreaOverlap(builder, geometry,
			request->portal_ordinal, member->entity->bsp_model,
			&member->destination, &destination_blocked);
		if (overlap_status != SG_HOST_LAW_OK) {
			status = overlap_status;
			goto done;
		}
		result->source_portal_blocked |= source_blocked;
		result->destination_portal_blocked |= destination_blocked;
		if (member->entity_ordinal == request->mover_entity_ordinal)
			selected_found = 1;
	}
	if (!selected_found || result->source_portal_blocked ==
		result->destination_portal_blocked) {
		status = SG_HOST_LAW_OK;
		goto done;
	}
	if (!TeamPortalMemberDistance(builder, &members[0], &min_distance) ||
		!isfinite(members[0].schedule.speed) ||
		members[0].schedule.speed <= 0.0f || min_distance <= 0.0f) {
		status = SG_HOST_LAW_EVALUATION_FAILED;
		goto done;
	}
	/* This is Think_CalcMoveSpeed's operation order: min / master speed,
	 * then every member distance/time and its proportional accel/decel scale. */
	for (index = 1U; index < member_count; index++) {
		float distance = 0.0f;

		if (!TeamPortalMemberDistance(builder, &members[index], &distance)) {
			status = SG_HOST_LAW_EVALUATION_FAILED;
			goto done;
		}
		if (distance < min_distance)
			min_distance = distance;
	}
	group_time = min_distance / members[0].schedule.speed;
	if (!isfinite(group_time) || group_time <= 0.0f) {
		status = SG_HOST_LAW_EVALUATION_FAILED;
		goto done;
	}
	for (index = 0U; index < member_count; index++) {
		mover_schedule_input_t schedule = members[index].schedule;
		float distance = 0.0f;
		float new_speed;
		float ratio;
		uint64_t member_elapsed;

		if (!TeamPortalMemberDistance(builder, &members[index], &distance) ||
			!isfinite(schedule.speed) || schedule.speed <= 0.0f)
		{
			status = SG_HOST_LAW_EVALUATION_FAILED;
			goto done;
		}
		new_speed = distance / group_time;
		ratio = new_speed / schedule.speed;
		if (!isfinite(new_speed) || !isfinite(ratio) || new_speed <= 0.0f) {
			status = SG_HOST_LAW_EVALUATION_FAILED;
			goto done;
		}
		if (!schedule.angular) {
			if (schedule.accel == schedule.speed)
				schedule.accel = new_speed;
			else
				schedule.accel *= ratio;
			if (schedule.decel == schedule.speed)
				schedule.decel = new_speed;
			else
				schedule.decel *= ratio;
			if (!isfinite(schedule.accel) || !isfinite(schedule.decel)) {
				status = SG_HOST_LAW_EVALUATION_FAILED;
				goto done;
			}
		}
		schedule.speed = new_speed;
		if (!MoverScheduleRun(builder, &schedule, index == 0U, NULL, NULL,
			NULL, NULL, &member_elapsed)) {
			status = SG_HOST_LAW_EVALUATION_FAILED;
			goto done;
		}
		if (member_elapsed > elapsed)
			elapsed = member_elapsed;
	}
	result->team_master_entity_ordinal = request->team_master_entity_ordinal;
	result->elapsed_ms = elapsed;
	*applicable_out = 1;
	status = SG_HOST_LAW_OK;
done:
	free(members);
	return status;
}

static int StanceValid(sg_rune_stance_t stance)
{
	return stance == SG_RUNE_STANCE_STANDING ||
		stance == SG_RUNE_STANCE_CROUCHING;
}

static const sg_rune_hull_profile_t *MoverHull(
	const sg_rune_compact_builder_t *builder, sg_rune_stance_t stance)
{
	if (!builder)
		return NULL;
	if (stance == SG_RUNE_STANCE_STANDING)
		return &builder->collision.identity.standing_hull;
	if (stance == SG_RUNE_STANCE_CROUCHING)
		return &builder->collision.identity.crouching_hull;
	return NULL;
}

static int Q8PoseOnSurface(const sg_rune_q8_vec3_t *vertices,
	uint32_t vertex_count, const sg_rune_hull_profile_t *hull,
	const sg_rune_q8_vec3_t *support, const sg_rune_q8_vec3_t *player)
{
	int32_t hull_offset;

	if (!vertices || !hull || !support || !player ||
		!FloatQ8Exact(-hull->mins.value[2], &hull_offset) ||
		!Q8PointOnHorizontalSurface(vertices, vertex_count, support) ||
		player->value[0] != support->value[0] ||
		player->value[1] != support->value[1])
		return 0;
	return (int64_t)player->value[2] == (int64_t)support->value[2] +
		hull_offset + SG_RUNE_COMPACT_SUPPORT_CLEARANCE_Q8;
}

static int LocalizeWorldPose(const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_vec3_t *world, sg_rune_stance_t stance,
	sg_rune_compact_cell_index_t *cell_out)
{
	sg_rune_compact_model_t model;
	sg_rune_compact_location_t location;

	if (!geometry || !world || !cell_out || !geometry->cells)
		return 0;
	memset(&model, 0, sizeof(model));
	model.identity = geometry->identity;
	model.cells = geometry->cells;
	model.cell_count = geometry->cell_count;
	model.facets = geometry->facets;
	model.facet_count = geometry->facet_count;
	model.incidences = geometry->incidences;
	model.incidence_count = geometry->incidence_count;
	model.cell_incidences = geometry->cell_incidences;
	model.cell_incidence_count = geometry->cell_incidence_count;
	/* This is the accepted compact localizer, including its exact binary32
	 * halfspace relation and open/closed ownership tie rule. */
	if (SG_RuneCompactLocalizeBinary32(&model, world, &location) !=
		SG_RUNE_COMPACT_LOCALIZE_OK ||
		(stance == SG_RUNE_STANCE_STANDING &&
			(location.valid_stances & SG_RUNE_STANCE_VALID_STANDING) == 0U) ||
		(stance == SG_RUNE_STANCE_CROUCHING &&
			(location.valid_stances & SG_RUNE_STANCE_VALID_CROUCHING) == 0U))
		return 0;
	*cell_out = location.cell;
	return 1;
}

static int PortalRequestValid(const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_builder_mover_request_t *request,
	sg_rune_compact_builder_mover_result_t *result_out)
{
	const sg_rune_compact_portal_t *portal;
	const sg_rune_compact_facet_t *facet;
	const sg_rune_compact_incidence_t *negative;
	const sg_rune_compact_incidence_t *positive;
	const sg_rune_q8_vec3_t *vertices;

	if (!geometry || !request || !result_out ||
		request->portal_ordinal >= geometry->portal_count ||
		!geometry->portals || !geometry->facets || !geometry->incidences ||
		!geometry->vertices)
		return 0;
	portal = &geometry->portals[request->portal_ordinal];
	if (portal->facet.value >= geometry->facet_count ||
		portal->negative_incidence.value >= geometry->incidence_count ||
		portal->positive_incidence.value >= geometry->incidence_count)
		return 0;
	facet = &geometry->facets[portal->facet.value];
	if (facet->kind != SG_RUNE_COMPACT_FACET_POLYGON ||
		facet->vertices.count < 3U || !SpanValid(facet->vertices.first,
			facet->vertices.count, geometry->vertex_count))
		return 0;
	negative = &geometry->incidences[portal->negative_incidence.value];
	positive = &geometry->incidences[portal->positive_incidence.value];
	if (negative->cell.value >= geometry->cell_count ||
		positive->cell.value >= geometry->cell_count ||
		!((request->entry_cell.value == negative->cell.value &&
			request->exit_cell.value == positive->cell.value) ||
		(request->entry_cell.value == positive->cell.value &&
			request->exit_cell.value == negative->cell.value)))
		return 0;
	vertices = geometry->vertices + facet->vertices.first;
	result_out->portal_ordinal = request->portal_ordinal;
	result_out->entry_cell = request->entry_cell;
	result_out->exit_cell = request->exit_cell;
	result_out->approach_witness = vertices[0];
	result_out->entry_witness = vertices[1];
	result_out->exit_witness = vertices[2];
	return 1;
}

typedef struct mover_push_carry_s
{
	const sg_rune_compact_builder_t *builder;
	const sg_bsp_entity_semantic_t *entity;
	uint32_t model;
	sg_rune_stance_t stance;
	float direction[3];
	sg_rune_vec3_t player_world;
	sg_rune_vec3_t support_world;
	sg_host_collision_transform_t transform;
	sg_rune_compact_builder_mover_failure_t failure;
	int host_error;
} mover_push_carry_t;

static int MoverPushCarryFrame(void *opaque, float distance)
{
	mover_push_carry_t *carry = opaque;
	float move[3];
	float next[3];
	sg_host_collision_transform_t next_transform;
	uint32_t axis;

	if (!carry || !isfinite(distance) || distance < 0.0f)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!SG_HostMechanismPushDisplacement(
			carry->direction[axis] * distance, &move[axis]))
		{
			carry->host_error = 1;
			return 0;
		}
		next[axis] = carry->player_world.value[axis] + move[axis];
		if (!isfinite(next[axis]))
		{
			carry->host_error = 1;
			return 0;
		}
	}
	/* SV_Push moves the rider and calls SV_TestEntityPosition at the resulting
	 * point.  It does not trace the rider path, so an intermediate obstacle is
	 * not a publication-time blocker when the final supported pose is free. */
	next_transform = carry->transform;
	for (axis = 0U; axis < 3U; axis++)
		next_transform.origin[axis] += move[axis];
	if (!MoverFinalPoseValid(carry->builder, carry->model, &next_transform,
		&(sg_rune_vec3_t){ { next[0], next[1], next[2] } }, carry->stance))
	{
		carry->failure = carry->entity->mechanism_kind ==
			SG_RUNE_MECHANISM_LIFT ?
			SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_CRUSHED :
			SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_BLOCKED;
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		carry->player_world.value[axis] = next[axis];
		carry->support_world.value[axis] += move[axis];
		carry->transform.origin[axis] += move[axis];
		if (!isfinite(carry->support_world.value[axis]) ||
			!isfinite(carry->transform.origin[axis]))
		{
			carry->host_error = 1;
			return 0;
		}
	}
	return 1;
}

static int MoverPushCarryAngularFrame(void *opaque,
	const float angular_delta[3])
{
	mover_push_carry_t *carry = opaque;
	float move[3] = { 0.0f, 0.0f, 0.0f };
	float next_player[3];
	float next_support[3];
	sg_host_collision_transform_t next_transform;
	uint32_t axis;

	if (carry == NULL || angular_delta == NULL)
		return 0;
	if (!SG_HostCollisionPusherCarry(&carry->transform, move, angular_delta,
		carry->player_world.value, next_player) ||
		!SG_HostCollisionPusherCarry(&carry->transform, move, angular_delta,
		carry->support_world.value, next_support))
	{
		carry->host_error = 1;
		return 0;
	}
	/* Match SV_Push's final SV_TestEntityPosition check; no swept rider trace. */
	next_transform = carry->transform;
	for (axis = 0U; axis < 3U; axis++)
		next_transform.angles[axis] += angular_delta[axis];
	if (!MoverFinalPoseValid(carry->builder, carry->model, &next_transform,
		&(sg_rune_vec3_t){ { next_player[0], next_player[1], next_player[2] } },
		carry->stance))
	{
		carry->failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_BLOCKED;
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++) {
		carry->player_world.value[axis] = next_player[axis];
		carry->support_world.value[axis] = next_support[axis];
		carry->transform.angles[axis] = next_transform.angles[axis];
		if (!isfinite(carry->transform.angles[axis])) {
			carry->host_error = 1;
			return 0;
		}
	}
	return 1;
}

static int SameRuneVec3(const sg_rune_vec3_t *left, const sg_rune_vec3_t *right)
{
	uint32_t axis;

	if (left == NULL || right == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (FloatBits(left->value[axis]) != FloatBits(right->value[axis]))
			return 0;
	return 1;
}

static void MoverResultInitialize(
	sg_rune_compact_builder_mover_result_t *result,
	const sg_rune_compact_builder_mover_request_t *request)
{
	memset(result, 0, sizeof(*result));
	result->mode = request->mode;
	result->team_portal = request->team_portal;
	result->team_master_entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	result->source_state = request->source_state;
	result->destination_state = request->destination_state;
	result->stance = request->stance;
	result->mover_model = SG_RUNE_COMPACT_INDEX_NONE;
	result->source_surface_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	result->portal_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	result->source_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	result->destination_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	result->route_fanout_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	result->entry_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	result->exit_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	result->failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE;
}

static sg_host_law_result_t MoverFinish(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_builder_mover_result_t *result,
	sg_rune_compact_builder_mover_result_t *result_out)
{
	sg_host_law_result_t current = BuilderOwnerCurrent(builder);

	if (current.status != SG_HOST_LAW_OK)
		return current;
	*result_out = *result;
	return BuilderOwnerResult(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE,
		SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerMoverTransport(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_builder_mover_request_t *request,
	sg_rune_compact_builder_mover_result_t *result_out)
{
	sg_rune_compact_geometry_view_t geometry_view;
	sg_rune_compact_builder_mover_result_t result;
	sg_host_collision_transform_t source_transform;
	sg_host_collision_transform_t destination_transform;
	const sg_bsp_entity_semantic_t *entity;
	const sg_bsp_entity_angular_mover_t *angular_door;
	const sg_bsp_entity_angular_mover_t *continuous_rotator;
	const sg_rune_compact_source_surface_t *surface;
	const sg_rune_q8_vec3_t *vertices;
	const sg_rune_hull_profile_t *hull;
	sg_host_law_result_t host_result;
	mover_push_carry_t carry;
	uint32_t model;
	uint32_t vertex_count;
	uint32_t axis;
	float delta[3];
	float distance;
	int train_route_valid;
	int team_portal_applicable;
	int source_surface_portal_overlap;
	int destination_surface_portal_overlap;
	int source_brush_portal_overlap;
	int destination_brush_portal_overlap;
	sg_host_law_status_t train_route_failure;
	sg_host_law_status_t portal_overlap_status;

	if (!builder || !geometry || !request || !result_out ||
		!MoverModeValid(request->mode) ||
		!MoverStateValid(request->source_state) ||
		!MoverStateValid(request->destination_state) ||
		(request->team_portal != 0 && request->team_portal != 1))
		return BuilderOwnerResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_MECHANISM_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	host_result = BuilderOwnerCurrent(builder);
	if (host_result.status != SG_HOST_LAW_OK)
		return host_result;
	memset(&geometry_view, 0, sizeof(geometry_view));
	if (!SG_RuneCompactGeometryRead(geometry, &geometry_view) ||
		!SG_RuneCompactIdentityMatches(&geometry_view.identity,
			&builder->identity))
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_SCHEMA, SG_HOST_LAW_ELEMENT_NONE,
			builder->identity.schema_id, geometry_view.identity.schema_id);
	MoverResultInitialize(&result, request);
	if (!MoverEntity(builder, request->mover_entity_ordinal, &entity,
		&host_result))
		return host_result;
	model = entity->bsp_model;
	angular_door = FiniteAngularDoor(builder, entity);
	continuous_rotator = ContinuousAngularRotator(builder, entity);
	if (!SurfaceMatchesMover(&geometry_view, request->source_surface_ordinal,
		model, &surface, &vertices))
		return MoverFinish(builder, &result, result_out);
	vertex_count = surface->vertices.count;
	if ((request->source_world_vertices_out == NULL) !=
		(request->destination_world_vertices_out == NULL) ||
		(request->source_world_vertices_out != NULL &&
			request->world_vertex_capacity < vertex_count) ||
		(request->mode == SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE &&
			request->source_world_vertices_out == NULL))
		return BuilderOwnerResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, request->mover_entity_ordinal,
			vertex_count, request->world_vertex_capacity);
	result.mover_model = model;
	result.source_surface_ordinal = request->source_surface_ordinal;
	result.source_vertex_count = vertex_count;
	if (request->mode == SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE)
	{
		if ((entity->mechanism_kind != SG_RUNE_MECHANISM_DOOR &&
			entity->mechanism_kind != SG_RUNE_MECHANISM_BUTTON &&
			angular_door == NULL) ||
			entity->mechanism_role == SG_MECH_NODE_SECRET_DOOR ||
			request->source_endpoint_entity_ordinal !=
				SG_RUNE_COMPACT_INDEX_NONE ||
			request->destination_endpoint_entity_ordinal !=
				SG_RUNE_COMPACT_INDEX_NONE ||
			request->route_fanout_ordinal != SG_RUNE_COMPACT_INDEX_NONE ||
			!PortalRequestValid(&geometry_view, request, &result))
			return MoverFinish(builder, &result, result_out);
		if (!MoverStateTransform(builder, entity, request->source_state,
			SG_RUNE_COMPACT_INDEX_NONE, &source_transform) ||
			!MoverStateTransform(builder, entity, request->destination_state,
			SG_RUNE_COMPACT_INDEX_NONE, &destination_transform) ||
			!TransformSurface(builder, model, &source_transform, vertices,
				vertex_count, request->source_world_vertices_out,
				&result.source_surface_world_bounds) ||
			!TransformSurface(builder, model, &destination_transform, vertices,
				vertex_count, request->destination_world_vertices_out,
				&result.destination_surface_world_bounds) ||
			!PortalSurfacePositiveAreaOverlap(&geometry_view,
				request->portal_ordinal, request->source_world_vertices_out,
				vertex_count, &source_surface_portal_overlap) ||
			!PortalSurfacePositiveAreaOverlap(&geometry_view,
				request->portal_ordinal, request->destination_world_vertices_out,
				vertex_count, &destination_surface_portal_overlap))
			return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
				SG_HOST_LAW_FIELD_MECHANISM_LAW,
				request->mover_entity_ordinal, 1U, 0U);
		portal_overlap_status = PortalSourceBrushPositiveAreaOverlap(builder,
			&geometry_view, request->portal_ordinal, surface, &source_transform,
			&source_brush_portal_overlap);
		if (portal_overlap_status != SG_HOST_LAW_OK)
			return BuilderOwnerResult(portal_overlap_status,
				SG_HOST_LAW_FIELD_COLLISION_LAW,
				request->mover_entity_ordinal, 1U, 0U);
		portal_overlap_status = PortalSourceBrushPositiveAreaOverlap(builder,
			&geometry_view, request->portal_ordinal, surface,
			&destination_transform, &destination_brush_portal_overlap);
		if (portal_overlap_status != SG_HOST_LAW_OK)
			return BuilderOwnerResult(portal_overlap_status,
				SG_HOST_LAW_FIELD_COLLISION_LAW,
				request->mover_entity_ordinal, 1U, 0U);
		if (request->team_portal) {
			portal_overlap_status = TeamPortalCertification(builder,
				&geometry_view, request, &result, &team_portal_applicable);
			if (portal_overlap_status != SG_HOST_LAW_OK)
				return BuilderOwnerResult(portal_overlap_status,
					SG_HOST_LAW_FIELD_COLLISION_LAW,
					request->mover_entity_ordinal, 1U, 0U);
			if (!team_portal_applicable)
				return MoverFinish(builder, &result, result_out);
		}
		else {
			portal_overlap_status = PortalModelPositiveAreaOverlap(builder,
				&geometry_view, request->portal_ordinal, model, &source_transform,
				&result.source_portal_blocked);
			if (portal_overlap_status != SG_HOST_LAW_OK)
				return BuilderOwnerResult(portal_overlap_status,
					SG_HOST_LAW_FIELD_COLLISION_LAW,
					request->mover_entity_ordinal, 1U, 0U);
			portal_overlap_status = PortalModelPositiveAreaOverlap(builder,
				&geometry_view, request->portal_ordinal, model,
				&destination_transform, &result.destination_portal_blocked);
			if (portal_overlap_status != SG_HOST_LAW_OK)
				return BuilderOwnerResult(portal_overlap_status,
					SG_HOST_LAW_FIELD_COLLISION_LAW,
					request->mover_entity_ordinal, 1U, 0U);
		}
		/* A root that overlaps at both endpoint states did not change this
		 * portal's collision connectivity.  Preserve per-state occupancy so
		 * transitions bind direction without reconstructing collision law. */
		if (result.source_portal_blocked == result.destination_portal_blocked)
			return MoverFinish(builder, &result, result_out);
		if ((result.source_portal_blocked && (!source_surface_portal_overlap ||
				!source_brush_portal_overlap)) ||
			(result.destination_portal_blocked &&
				(!destination_surface_portal_overlap ||
					!destination_brush_portal_overlap)))
			return MoverFinish(builder, &result, result_out);
		if (!request->team_portal && !MoverSchedule(builder, entity,
			&source_transform, &destination_transform, NULL, NULL, NULL, NULL,
			&result.elapsed_ms))
			return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
				SG_HOST_LAW_FIELD_MECHANISM_LAW,
				request->mover_entity_ordinal, 1U, 0U);
		result.applicable = 1;
		return MoverFinish(builder, &result, result_out);
	}
	if (!StanceValid(request->stance) ||
		!SupportPoseModeValid(request->support_pose_mode) ||
		request->team_portal != 0 ||
		request->portal_ordinal != SG_RUNE_COMPACT_INDEX_NONE)
		return MoverFinish(builder, &result, result_out);
	/* SV_Push carries groundentity riders for every MOVETYPE_PUSH brush.  The
	 * entity semantics boundary has already reduced the candidates to these
	 * authenticated pusher classes; func_water is a DOOR here because stock
	 * SP_func_water hands it to door_use after spawn. */
	if (entity->mechanism_kind != SG_RUNE_MECHANISM_DOOR &&
		entity->mechanism_kind != SG_RUNE_MECHANISM_BUTTON &&
		entity->mechanism_kind != SG_RUNE_MECHANISM_LIFT &&
		entity->mechanism_kind != SG_RUNE_MECHANISM_TRAIN &&
		angular_door == NULL && continuous_rotator == NULL)
		return MoverFinish(builder, &result, result_out);
	if (entity->mechanism_kind == SG_RUNE_MECHANISM_TRAIN)
	{
		if (request->source_endpoint_entity_ordinal ==
				SG_RUNE_COMPACT_INDEX_NONE ||
			request->destination_endpoint_entity_ordinal ==
				SG_RUNE_COMPACT_INDEX_NONE)
			return MoverFinish(builder, &result, result_out);
		if (!TrainRouteValid(builder->entity_semantics,
			entity->canonical_ordinal,
			request->source_endpoint_entity_ordinal,
			request->destination_endpoint_entity_ordinal,
			request->route_fanout_ordinal, &train_route_valid,
			&train_route_failure))
			return BuilderOwnerResult(train_route_failure,
				SG_HOST_LAW_FIELD_ENTITY_SEMANTICS,
				request->mover_entity_ordinal, 1U, 0U);
		if (!train_route_valid)
			return MoverFinish(builder, &result, result_out);
		result.source_endpoint_entity_ordinal =
			request->source_endpoint_entity_ordinal;
		result.destination_endpoint_entity_ordinal =
			request->destination_endpoint_entity_ordinal;
		result.route_fanout_ordinal = request->route_fanout_ordinal;
	}
	else if (request->source_endpoint_entity_ordinal !=
			SG_RUNE_COMPACT_INDEX_NONE || request->destination_endpoint_entity_ordinal !=
			SG_RUNE_COMPACT_INDEX_NONE || request->route_fanout_ordinal !=
			SG_RUNE_COMPACT_INDEX_NONE)
		return MoverFinish(builder, &result, result_out);
	if (!((continuous_rotator != NULL &&
		ContinuousRotatorFrameTransforms(builder, entity, request->source_state,
			request->destination_state, &source_transform, &destination_transform)) ||
		(continuous_rotator == NULL &&
			MoverStateTransform(builder, entity, request->source_state,
				request->source_endpoint_entity_ordinal, &source_transform) &&
			MoverStateTransform(builder, entity, request->destination_state,
				request->destination_endpoint_entity_ordinal,
				&destination_transform))) ||
		!TransformSurface(builder, model, &source_transform, vertices,
			vertex_count, request->source_world_vertices_out,
			&result.source_surface_world_bounds))
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_MECHANISM_LAW, request->mover_entity_ordinal,
			1U, 0U);
	hull = MoverHull(builder, request->stance);
	if (hull == NULL)
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_PMOVE_LAW, request->mover_entity_ordinal, 1U, 0U);
	if (request->support_pose_mode ==
		SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_CANONICAL)
	{
		if (!CanonicalSupportPose(vertices, vertex_count, hull,
			&result.source_support_local, &result.source_player_local))
			return MoverFinish(builder, &result, result_out);
	}
	else
	{
		result.source_support_local = request->support_local_pose;
		result.source_player_local = request->player_local_pose;
		if (!Q8PoseOnSurface(vertices, vertex_count, hull,
			&result.source_support_local, &result.source_player_local))
		{
			result.applicable = 1;
			result.failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING;
			return MoverFinish(builder, &result, result_out);
		}
	}
	result.destination_support_local = result.source_support_local;
	result.destination_player_local = result.source_player_local;
	if (!SupportPoseValid(builder, model, &source_transform,
		&result.source_player_local, request->stance, &result.source_player_world) ||
		!TransformLocalPose(builder, model, &source_transform,
			&result.source_support_local, &result.source_support_world))
	{
		result.applicable = 1;
		result.failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING;
		return MoverFinish(builder, &result, result_out);
	}
	result.applicable = 1;
	result.start_supported = 1;
	if (!LocalizeWorldPose(&geometry_view, &result.source_player_world,
		request->stance, &result.entry_cell))
	{
		result.failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING;
		return MoverFinish(builder, &result, result_out);
	}
	memset(&carry, 0, sizeof(carry));
	carry.builder = builder;
	carry.entity = entity;
	carry.model = model;
	carry.stance = request->stance;
	carry.player_world = result.source_player_world;
	carry.support_world = result.source_support_world;
	carry.transform = source_transform;
	if (continuous_rotator != NULL) {
		if (!MoverPushCarryAngularFrame(&carry,
			continuous_rotator->schedule.continuous_rotator.frame_angular_delta.value))
			goto carry_failed;
		result.elapsed_ms = continuous_rotator->schedule.continuous_rotator.frame_ms;
	}
	else {
		for (axis = 0U; axis < 3U; axis++) {
			delta[axis] = destination_transform.origin[axis] -
				source_transform.origin[axis];
			if (!isfinite(delta[axis]))
				return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
					SG_HOST_LAW_FIELD_MECHANISM_LAW,
					request->mover_entity_ordinal, 1U, 0U);
		}
		distance = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] +
			delta[2] * delta[2]);
		if (!isfinite(distance))
			return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
				SG_HOST_LAW_FIELD_MECHANISM_LAW,
				request->mover_entity_ordinal, 1U, 0U);
		if (distance != 0.0f)
			for (axis = 0U; axis < 3U; axis++)
				carry.direction[axis] = delta[axis] / distance;
		if (!MoverSchedule(builder, entity, &source_transform,
			&destination_transform, MoverPushCarryFrame, &carry,
			MoverPushCarryAngularFrame, &carry, &result.elapsed_ms))
			goto carry_failed;
	}
	result.swept_static_clear = 1;
	result.destination_player_world = carry.player_world;
	result.destination_support_world = carry.support_world;
	if (angular_door != NULL || continuous_rotator != NULL) {
		sg_rune_vec3_t replayed_player;
		sg_rune_vec3_t replayed_support;

		if (!TransformLocalPose(builder, model, &carry.transform,
			&result.destination_player_local, &replayed_player) ||
			!TransformLocalPose(builder, model, &carry.transform,
			&result.destination_support_local, &replayed_support) ||
			!SameRuneVec3(&replayed_player, &carry.player_world) ||
			!SameRuneVec3(&replayed_support, &carry.support_world)) {
			result.failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING;
			return MoverFinish(builder, &result, result_out);
		}
	}
	if (!((angular_door != NULL || continuous_rotator != NULL) ?
		MoverFinalPoseValid(builder, model, &carry.transform,
			&result.destination_player_world, request->stance) :
		SupportWorldPoseValid(builder, model, &carry.transform,
			&result.destination_player_world, request->stance)) ||
		!TransformSurface(builder, model, &carry.transform, vertices,
			vertex_count, request->destination_world_vertices_out,
			&result.destination_surface_world_bounds) ||
		!LocalizeWorldPose(&geometry_view, &result.destination_player_world,
			request->stance, &result.exit_cell))
	{
		result.failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING;
		return MoverFinish(builder, &result, result_out);
	}
	/* Publish only the exact state transforms which produced the endpoint
	 * poses above.  The destination is terminal SV_Push accumulation, not the
	 * nominal endpoint transform: its binary32 operation order is part of the
	 * transport proof. */
	if (!SG_HostCollisionWorldTransform(&source_transform,
		&result.source_mover_transform) ||
		!SG_HostCollisionWorldTransform(&carry.transform,
			&result.destination_mover_transform))
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW,
			request->mover_entity_ordinal, 1U, 0U);
	result.end_supported = 1;
	return MoverFinish(builder, &result, result_out);

carry_failed:
	if (carry.host_error)
		return BuilderOwnerResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, request->mover_entity_ordinal,
			1U, 0U);
	result.failure = carry.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE ?
		SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_BLOCKED : carry.failure;
	return MoverFinish(builder, &result, result_out);
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerPmove(
	const sg_rune_compact_builder_t *builder,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out)
{
	if (!BuilderValid(builder)) {
		sg_host_law_result_t result;

		memset(&result, 0, sizeof(result));
		result.status = SG_HOST_LAW_INVALID_ARGUMENT;
		result.field = SG_HOST_LAW_FIELD_PMOVE_LAW;
		result.element = SG_HOST_LAW_ELEMENT_NONE;
		return result;
	}
	return SG_HostLawPmoveEvaluatorRun(builder->pmove_evaluator,
		&builder->collision, scene, request, result_out, error_out);
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerReplayFrame(
	const sg_rune_compact_builder_t *builder,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out,
	sg_host_pmove_error_t *error_out)
{
	if (!BuilderValid(builder)) {
		sg_host_law_result_t result;

		memset(&result, 0, sizeof(result));
		result.status = SG_HOST_LAW_INVALID_ARGUMENT;
		result.field = SG_HOST_LAW_FIELD_PMOVE_LAW;
		result.element = SG_HOST_LAW_ELEMENT_NONE;
		return result;
	}
	return SG_HostLawPmoveEvaluatorReplayFrame(builder->pmove_evaluator,
		&builder->collision, scene, request, workspace, replay_out, error_out);
}

void SG_RuneCompactBuilderDestroy(sg_rune_compact_builder_t *builder)
{
	if (!builder)
		return;
	if (builder->state != 0U && !BuilderValid(builder))
		return;
	builder->state = 0U;
	builder->state_inverse = 0U;
	builder->self = NULL;
	SG_HostLawPmoveEvaluatorDestroy(builder->pmove_evaluator);
	builder->pmove_evaluator = NULL;
	SG_RuneSourceAuthorityDestroy(builder->source_authority);
	builder->source_authority = NULL;
	free(builder->resolved_weapon_profiles);
	free(builder->weapon_profiles);
	free(builder->entity_records);
	free(builder->entity_text);
	SG_StaticVisibilityDestroy(builder->visibility);
	SG_BspEntitySemanticsDestroy(builder->entity_semantics);
	SG_ConfigurationSemanticsDestroy(builder->semantics);
	SG_ConfigurationDestroy(builder->configuration);
	SG_BspWorldDestroy(builder->world);
	free(builder);
}

const char *SG_RuneCompactBuilderErrorString(
	sg_rune_compact_builder_error_code_t code)
{
	switch (code) {
	case SG_RUNE_COMPACT_BUILDER_ERROR_NONE: return "none";
	case SG_RUNE_COMPACT_BUILDER_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_BUILDER_ERROR_HOST_AUTHORITY:
		return "host authority rejected";
	case SG_RUNE_COMPACT_BUILDER_ERROR_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_RUNE_COMPACT_BUILDER_ERROR_BSP_LOAD: return "BSP load failed";
	case SG_RUNE_COMPACT_BUILDER_ERROR_CONFIGURATION:
		return "configuration build failed";
	case SG_RUNE_COMPACT_BUILDER_ERROR_CONFIGURATION_AUDIT:
		return "configuration audit failed";
	case SG_RUNE_COMPACT_BUILDER_ERROR_SEMANTICS:
		return "configuration semantics build failed";
	case SG_RUNE_COMPACT_BUILDER_ERROR_SEMANTICS_AUDIT:
		return "configuration semantics audit failed";
	case SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_SEMANTICS:
		return "entity semantics build failed";
	case SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUDIT:
		return "entity semantics audit failed";
	case SG_RUNE_COMPACT_BUILDER_ERROR_VISIBILITY:
		return "visibility build failed";
	case SG_RUNE_COMPACT_BUILDER_ERROR_VISIBILITY_AUDIT:
		return "visibility audit failed";
	case SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUTHORITY:
		return "runtime entity source authority unavailable";
	case SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_AUTHORITY:
		return "weapon source authority unavailable";
	case SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_PROFILE:
		return "weapon profile resolution failed";
	case SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_RUNE_COMPACT_BUILDER_ERROR_CODE_COUNT: break;
	}
	return "unknown compact builder error";
}
