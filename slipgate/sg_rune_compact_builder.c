#include "sg_rune_compact_builder.h"
#include "sg_rune_compact_builder_owner.h"

#include "sg_configuration_audit.h"
#include "sg_host_law_publication_private.h"
#include "sg_rune_source_authority.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SG_RUNE_COMPACT_BUILDER_STATE UINT64_C(0x434255494c444552)
#define SG_RUNE_COMPACT_BUILDER_SCHEMA_ID UINT64_C(0x434f4d5041435402)
#define SG_RUNE_COMPACT_BUILDER_CONSTRUCTION_ID UINT64_C(0x4253504649454c02)
#define SG_RUNE_COMPACT_BUILDER_PRODUCER_ID UINT64_C(0x53474255494c4401)
#define SG_RUNE_COMPACT_BUILDER_HASH_OFFSET UINT64_C(14695981039346656037)
#define SG_RUNE_COMPACT_BUILDER_HASH_PRIME UINT64_C(1099511628211)

struct sg_rune_compact_builder_s
{
	uint64_t state;
	uint64_t state_inverse;
	const struct sg_rune_compact_builder_s *self;
	sg_rune_compact_identity_t identity;
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

static uint64_t HashU16(uint64_t hash, uint16_t value)
{
	hash = HashByte(hash, (uint8_t)value);
	return HashByte(hash, (uint8_t)(value >> 8));
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

static int InputShapeValid(const sg_rune_compact_builder_input_t *input,
	sg_rune_compact_builder_t **builder_out)
{
	return input && input->construction && builder_out && !*builder_out;
}

static uint64_t HashWeaponProfile(uint64_t hash,
	const sg_weapon_profile_t *profile)
{
#define HASH_WEAPON_U16(field) hash = HashU16(hash, profile->field)
#define HASH_WEAPON_U32(field) hash = HashU32(hash, (uint32_t)profile->field)
#define HASH_WEAPON_FLOAT(field) hash = HashFloat(hash, profile->field)
	HASH_WEAPON_U32(id);
	HASH_WEAPON_U32(family);
	HASH_WEAPON_U32(effects);
	HASH_WEAPON_FLOAT(projectile_speed);
	HASH_WEAPON_FLOAT(projectile_speed_max);
	HASH_WEAPON_FLOAT(ray_distance);
	HASH_WEAPON_FLOAT(projectile_retire_distance);
	HASH_WEAPON_FLOAT(projectile_half_extent);
	HASH_WEAPON_FLOAT(launch_vertical_speed);
	HASH_WEAPON_FLOAT(launch_jitter);
	HASH_WEAPON_FLOAT(gravity_scale);
	HASH_WEAPON_FLOAT(horizontal_spread);
	HASH_WEAPON_FLOAT(vertical_spread);
	HASH_WEAPON_FLOAT(yaw_spread_degrees);
	HASH_WEAPON_FLOAT(splash_radius);
	HASH_WEAPON_FLOAT(splash_radius_max);
	HASH_WEAPON_FLOAT(secondary_splash_radius);
	HASH_WEAPON_FLOAT(direct_damage);
	HASH_WEAPON_FLOAT(direct_damage_max);
	HASH_WEAPON_FLOAT(splash_damage);
	HASH_WEAPON_FLOAT(splash_damage_max);
	HASH_WEAPON_FLOAT(secondary_splash_damage);
	HASH_WEAPON_FLOAT(periodic_ray_damage);
	HASH_WEAPON_FLOAT(periodic_ray_radius);
	HASH_WEAPON_FLOAT(periodic_ray_distance);
	HASH_WEAPON_FLOAT(self_damage_scale);
	HASH_WEAPON_FLOAT(teammate_risk_scale);
	HASH_WEAPON_FLOAT(auxiliary_trace_damage);
	HASH_WEAPON_FLOAT(auxiliary_horizontal_spread);
	HASH_WEAPON_FLOAT(auxiliary_vertical_spread);
	HASH_WEAPON_U32(windup_ms);
	HASH_WEAPON_U32(cook_ms);
	HASH_WEAPON_U32(fuse_ms);
	HASH_WEAPON_U32(projectile_lifetime_ms);
	HASH_WEAPON_U32(cadence_ms);
	HASH_WEAPON_U32(periodic_ray_interval_ms);
	HASH_WEAPON_U16(projectile_count_min);
	HASH_WEAPON_U16(projectile_count_max);
	HASH_WEAPON_U16(auxiliary_trace_count);
	HASH_WEAPON_U16(hook_initial_damage);
	HASH_WEAPON_U16(hook_attached_damage);
	HASH_WEAPON_U16(hook_health);
	HASH_WEAPON_U16(hook_pull_speed);
	HASH_WEAPON_U32(splash.kernel);
	HASH_WEAPON_U32(splash.owner);
	HASH_WEAPON_FLOAT(splash.owner_scale);
	HASH_WEAPON_U32(secondary_splash.kernel);
	HASH_WEAPON_U32(secondary_splash.owner);
	HASH_WEAPON_FLOAT(secondary_splash.owner_scale);
	HASH_WEAPON_U16(ammo.ready_minimum);
	HASH_WEAPON_U16(ammo.live_fire_minimum);
	HASH_WEAPON_U16(ammo.debit);
	HASH_WEAPON_U16(ammo.debit_maximum);
	HASH_WEAPON_U16(ammo.infinite_ammo_debit);
	HASH_WEAPON_U16(timing.activate_last_frame);
	HASH_WEAPON_U16(timing.fire_last_frame);
	HASH_WEAPON_U16(timing.idle_last_frame);
	HASH_WEAPON_U16(timing.deactivate_first_frame);
	HASH_WEAPON_U16(timing.deactivate_last_frame);
	HASH_WEAPON_U16(timing.first_effect_frame);
	HASH_WEAPON_U16(timing.last_effect_frame);
	HASH_WEAPON_U32(timing.effect_interval_ms);
	HASH_WEAPON_U32(timing.resolved_switch_in_ms);
	HASH_WEAPON_U32(timing.resolved_switch_out_ms);
	HASH_WEAPON_U32(timing.activation_frame_step);
	HASH_WEAPON_U32(timing.fast_switch_bypasses_activation);
	HASH_WEAPON_U32(timing.fast_switch_bypasses_deactivation);
	HASH_WEAPON_U32(damage_dependency);
	HASH_WEAPON_U32(cadence_kind);
	HASH_WEAPON_U32(requires_live_trace);
	HASH_WEAPON_U32(supports_occluded_impact);
	HASH_WEAPON_U32(auxiliary_traces_penetrate);
#undef HASH_WEAPON_FLOAT
#undef HASH_WEAPON_U32
#undef HASH_WEAPON_U16
	return hash;
}

static void WeaponLawInput(
	const sg_rune_source_weapon_law_t *static_law,
	uint64_t physics_abi_id, sg_weapon_law_input_t *law_out)
{
	memset(law_out, 0, sizeof(*law_out));
	law_out->build_identity = SG_RUNE_COMPACT_BUILDER_PRODUCER_ID;
	law_out->physics_abi_id = physics_abi_id;
	law_out->weapon_balance_compiled = static_law->weapon_balance_compiled;
	law_out->weapon_balance_enabled = static_law->weapon_balance_enabled;
	law_out->rail_match_active = static_law->rail_match_active;
	law_out->deathmatch_active = static_law->deathmatch_active;
	law_out->fast_switch_enabled = static_law->fast_switch_enabled;
}

static uint64_t WeaponLawIdentity(
	const sg_rune_source_weapon_law_t *static_law,
	const sg_weapon_profile_t *profiles, uint32_t count)
{
	uint64_t hash = BeginIdentity("lmctf.compact.weapon-law.v1");
	uint32_t index;

	hash = HashU32(hash, static_law->weapon_balance_compiled);
	hash = HashU32(hash, static_law->weapon_balance_enabled);
	hash = HashU32(hash, static_law->rail_match_active);
	hash = HashU32(hash, static_law->deathmatch_active);
	hash = HashU32(hash, static_law->fast_switch_enabled);
	hash = HashU32(hash, count);
	for (index = 0U; index < count; index++)
		hash = HashWeaponProfile(hash, &profiles[index]);
	return FinishIdentity(hash);
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
	uint64_t weapon_law_id, uint32_t entity_count,
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
	uint64_t *weapon_law_id_out, sg_rune_compact_builder_error_t *error)
{
	sg_weapon_law_input_t weapon_law;
	size_t catalog_count = SG_WeaponProfileCount();
	uint32_t index;

	if (!static_law || !StaticWeaponLawValid(static_law) ||
		!weapon_law_id_out || !SG_WeaponProfileCatalogValid() ||
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
	WeaponLawInput(static_law, physics_abi_id, &weapon_law);
	for (index = 0U; index < builder->weapon_profile_count; index++) {
		const sg_weapon_profile_id_t source =
			(sg_weapon_profile_id_t)(index + 1U);
		sg_rune_weapon_response_family_mask_t mask;

		if (!SG_WeaponProfileResolve(source, &weapon_law,
				&builder->resolved_weapon_profiles[index]) ||
			builder->resolved_weapon_profiles[index].id != source ||
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
	}
	*weapon_law_id_out = WeaponLawIdentity(static_law,
		builder->resolved_weapon_profiles, builder->weapon_profile_count);
	return 1;
}

static int BuilderValid(const sg_rune_compact_builder_t *builder)
{
	return builder && builder->state == SG_RUNE_COMPACT_BUILDER_STATE &&
		builder->state_inverse == ~SG_RUNE_COMPACT_BUILDER_STATE &&
		builder->self == builder && builder->world && builder->configuration &&
		builder->semantics && builder->entity_semantics && builder->visibility &&
		builder->entity_text && builder->entity_text_bytes != 0U &&
		builder->entity_records && builder->entity_record_count != 0U &&
		builder->weapon_profiles && builder->resolved_weapon_profiles &&
		builder->weapon_profile_count != 0U;
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
	sg_configuration_limits_t default_configuration_limits;
	const sg_configuration_limits_t *configuration_limits;
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_limits_t default_semantics_limits;
	const sg_configuration_semantics_limits_t *semantics_limits;
	sg_configuration_semantics_audit_result_t semantics_audit;
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
	uint64_t entity_semantics_id;
	uint64_t weapon_law_id;
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
	if (!SG_ConfigurationBuild(&builder->collision,
			configuration_limits, &builder->configuration,
			&configuration_error)) {
		SetError(error_out,
			configuration_error.code == SG_CONFIGURATION_ERROR_OUT_OF_MEMORY ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_CONFIGURATION,
			configuration_error.source_index, SG_CONFIGURATION_ERROR_NONE,
			(uint64_t)configuration_error.code);
		goto done;
	}
	memset(&configuration_audit, 0, sizeof(configuration_audit));
	if (development_audit &&
		!SG_ConfigurationAudit(&builder->collision, builder->configuration,
			&configuration_audit)) {
		SetError(error_out,
			configuration_audit.code == SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_CONFIGURATION_AUDIT,
			configuration_audit.record, SG_CONFIGURATION_AUDIT_OK,
			(uint64_t)configuration_audit.code);
		goto done;
	}
	memset(&semantics_error, 0, sizeof(semantics_error));
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
	memset(&semantics_audit, 0, sizeof(semantics_audit));
	if (development_audit &&
		!SG_ConfigurationSemanticsAudit(&builder->collision,
			builder->configuration, builder->semantics, &semantics_audit)) {
		SetError(error_out,
			semantics_audit.code ==
				SG_CONFIGURATION_SEMANTICS_AUDIT_OUT_OF_MEMORY ?
				SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY :
				SG_RUNE_COMPACT_BUILDER_ERROR_SEMANTICS_AUDIT,
			semantics_audit.record, SG_CONFIGURATION_SEMANTICS_AUDIT_OK,
			(uint64_t)semantics_audit.code);
		goto done;
	}
	memset(&visibility_error, 0, sizeof(visibility_error));
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
			builder, &weapon_law_id, error_out))
		goto done;
	if (!CompactIdentity(&host, builder->world, entity_semantics_id,
			weapon_law_id, builder->entity_semantics->entity_count,
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
	if (!BuilderValid(builder) || !view_out)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->world = builder->world;
	view_out->collision = &builder->collision;
	view_out->configuration = builder->configuration;
	view_out->semantics = builder->semantics;
	view_out->entity_semantics = builder->entity_semantics;
	view_out->visibility = builder->visibility;
	return 1;
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
