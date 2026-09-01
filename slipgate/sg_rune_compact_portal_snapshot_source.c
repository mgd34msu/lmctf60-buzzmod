#include "sg_rune_compact_portal_snapshot_source.h"

#include "sg_bsp_entity_semantics_publication.h"
#include "sg_host_law_owner.h"
#include "sg_rune_source_authority.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define PORTAL_SNAPSHOT_HASH_OFFSET UINT64_C(14695981039346656037)
#define PORTAL_SNAPSHOT_HASH_PRIME UINT64_C(1099511628211)

struct sg_rune_compact_portal_snapshot_source_s
{
	const sg_rune_compact_model_t *model;
	sg_rune_source_authority_t *authority;
	sg_rune_source_snapshot_t source_snapshot;
	sg_bsp_entity_semantics_t *semantics;
};

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static uint64_t HashByte(uint64_t hash, uint8_t value)
{
	return (hash ^ (uint64_t)value) * PORTAL_SNAPSHOT_HASH_PRIME;
}

static uint64_t HashBytes(uint64_t hash, const uint8_t *bytes, size_t count)
{
	size_t index;

	if (bytes == NULL && count != 0U)
		return 0U;
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
	return HashBytes(PORTAL_SNAPSHOT_HASH_OFFSET, (const uint8_t *)domain,
		strlen(domain));
}

static uint64_t HashFloat(uint64_t hash, float value)
{
	return HashU32(hash, FloatBits(value));
}

static uint64_t HashVec3(uint64_t hash, const sg_rune_vec3_t *vector)
{
	uint32_t axis;

	if (vector == NULL)
		return 0U;
	for (axis = 0U; axis < 3U; axis++)
		hash = HashFloat(hash, vector->value[axis]);
	return hash;
}

static uint64_t HashBounds(uint64_t hash, const sg_rune_bounds_t *bounds)
{
	if (bounds == NULL)
		return 0U;
	hash = HashVec3(hash, &bounds->mins);
	return hash != 0U ? HashVec3(hash, &bounds->maxs) : 0U;
}

/* Keep this byte-for-byte semantic identity law aligned with the compact
 * builder.  A production source is accepted only when it names the identity
 * carried by the already accepted artifact; a merely similar entity parse is
 * not a live authority. */
static uint64_t HashAngularMover(uint64_t hash,
	const sg_bsp_entity_angular_mover_t *mover)
{
	const uint8_t *schedule;
	size_t schedule_bytes;
	size_t active_bytes = 0U;

	if (mover == NULL)
		return 0U;
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
		return 0U;
	}
	return hash != 0U ? HashBytes(hash, schedule + active_bytes,
		schedule_bytes - active_bytes) : 0U;
}

static int EntitySemanticsId(const sg_bsp_content_identity_t *bsp_identity,
	const sg_bsp_entity_semantics_t *semantics, uint64_t *identity_out)
{
	uint64_t hash = BeginIdentity("lmctf.compact.entity-semantics.v1");
	uint32_t index;

	if (identity_out != NULL)
		*identity_out = 0U;
	if (bsp_identity == NULL || semantics == NULL || identity_out == NULL ||
		(semantics->entity_count != 0U && semantics->entities == NULL) ||
		(semantics->edge_count != 0U && semantics->edges == NULL) ||
		(semantics->string_bytes != 0U && semantics->strings == NULL))
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
		if (hash == 0U)
			return 0;
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
	if (hash == 0U)
		return 0;
	*identity_out = FinishIdentity(hash);
	return 1;
}

static uint64_t DeriveSourceSetId(const sg_bsp_content_identity_t *identity)
{
	uint64_t hash;

	if (identity == NULL)
		return 0U;
	hash = BeginIdentity("lmctf.compact.source-set.v1");
	hash = HashBytes(hash, identity->bytes, SG_BSP_CONTENT_ID_BYTES);
	hash = HashBytes(hash, SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID.bytes,
		SG_BSP_CONTENT_ID_BYTES);
	return hash != 0U ? FinishIdentity(hash) : 0U;
}

static int SnapshotMatchesModel(const sg_rune_compact_model_t *model,
	const sg_rune_source_snapshot_t *snapshot,
	const sg_host_collision_authority_t *collision)
{
	const sg_level_identity_t *level;

	if (model == NULL || snapshot == NULL || collision == NULL ||
		collision->world == NULL || model->identity.entity_semantics_id == 0U ||
		snapshot->version != SG_RUNE_SOURCE_AUTHORITY_VERSION ||
		snapshot->reserved != 0U || snapshot->publication_generation == 0U ||
		snapshot->publication_generation_complement !=
			~snapshot->publication_generation)
		return 0;
	level = &snapshot->level_identity;
	return memcmp(level->bsp_sha256, model->identity.bsp_sha256,
			SG_BSP_CONTENT_ID_BYTES) == 0 &&
		memcmp(collision->content_identity.bytes, model->identity.bsp_sha256,
			SG_BSP_CONTENT_ID_BYTES) == 0 &&
		level->bsp_bytes == model->identity.bsp_bytes &&
		level->bsp_checksum == model->identity.bsp_checksum &&
		level->entity_crc32 == model->identity.entity_crc32;
}

static int SourceLive(const sg_rune_compact_portal_snapshot_source_t *source,
	const sg_rune_compact_model_t *model,
	const sg_host_collision_authority_t **collision_out)
{
	const sg_host_collision_authority_t *collision = NULL;
	sg_host_law_result_t host_result;
	uint64_t semantic_identity;

	if (collision_out != NULL)
		*collision_out = NULL;
	if (source == NULL || model == NULL || source->model != model ||
		source->authority == NULL || source->semantics == NULL ||
		SG_RuneSourceAuthorityCurrent(source->authority) != SG_RUNE_SOURCE_OK ||
		SG_HostLawProductionAuthorityCurrent(
			&source->source_snapshot.host_authority).status != SG_HOST_LAW_OK)
		return 0;
	host_result = SG_HostLawProductionCollisionAuthority(&collision);
	if (host_result.status != SG_HOST_LAW_OK || collision == NULL ||
		!SnapshotMatchesModel(model, &source->source_snapshot, collision) ||
		!EntitySemanticsId(&collision->content_identity, source->semantics,
			&semantic_identity) ||
		semantic_identity != model->identity.entity_semantics_id)
		return 0;
	if (collision_out != NULL)
		*collision_out = collision;
	return 1;
}

int SG_RuneCompactPortalSnapshotSourcePrepare(
	const sg_rune_compact_model_t *model,
	sg_rune_compact_portal_snapshot_source_t **source_out)
{
	sg_rune_compact_portal_snapshot_source_t *source = NULL;
	sg_rune_source_authority_t *authority = NULL;
	sg_rune_source_snapshot_t snapshot;
	const sg_host_collision_authority_t *collision = NULL;
	sg_bsp_entity_semantics_t *semantics = NULL;
	sg_bsp_entity_semantics_error_t semantics_error;
	sg_bsp_entity_semantics_audit_result_t audit;
	sg_bsp_entity_semantics_binding_t binding;
	sg_bsp_entity_semantics_source_t semantic_source;
	char *text = NULL;
	sg_rune_source_entity_record_t *records = NULL;
	size_t text_bytes = 0U;
	size_t record_count = 0U;
	uint64_t source_set_identity;
	uint64_t semantic_identity;
	int success = 0;

	if (source_out == NULL || *source_out != NULL || model == NULL ||
		model->identity.entity_semantics_id == 0U)
		return 0;
	*source_out = NULL;
	if (SG_RuneSourceAuthorityAcquire(&authority) != SG_RUNE_SOURCE_OK ||
		SG_RuneSourceAuthoritySizes(authority, &text_bytes, &record_count) !=
			SG_RUNE_SOURCE_OK || text_bytes == 0U || text_bytes > UINT32_MAX ||
		record_count > UINT32_MAX || record_count >
			SIZE_MAX / sizeof(*records))
		goto done;
	text = malloc(text_bytes);
	if (record_count != 0U)
		records = malloc(record_count * sizeof(*records));
	if (text == NULL || (record_count != 0U && records == NULL))
		goto done;
	memset(&snapshot, 0, sizeof(snapshot));
	if (SG_RuneSourceAuthorityCopy(authority, &snapshot, text, text_bytes,
		records, record_count) != SG_RUNE_SOURCE_OK ||
		snapshot.entity_text_bytes != text_bytes ||
		snapshot.entity_record_count != record_count ||
		text[text_bytes - 1U] != '\0' ||
		SG_HostLawProductionAuthorityCurrent(&snapshot.host_authority).status !=
			SG_HOST_LAW_OK ||
		SG_HostLawProductionCollisionAuthority(&collision).status !=
			SG_HOST_LAW_OK || collision == NULL ||
		!SnapshotMatchesModel(model, &snapshot, collision))
		goto done;
	source_set_identity = DeriveSourceSetId(&collision->content_identity);
	if (source_set_identity == 0U)
		goto done;
	memset(&semantics_error, 0, sizeof(semantics_error));
	if (!SG_BspEntitySemanticsBuildEffective(collision->world, text, text_bytes,
		records, record_count, source_set_identity, &semantics,
		&semantics_error))
		goto done;
	memset(&binding, 0, sizeof(binding));
	memcpy(binding.source_identity.bytes, collision->content_identity.bytes,
		SG_BSP_CONTENT_ID_BYTES);
	binding.source_set_identity = source_set_identity;
	binding.schema_identity = SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID;
	semantic_source.selected_entity_text = text;
	semantic_source.selected_entity_text_bytes = text_bytes;
	semantic_source.survivors = records;
	semantic_source.survivor_count = record_count;
	memset(&audit, 0, sizeof(audit));
	if (!SG_BspEntitySemanticsAuditEffective(collision, &binding,
		&semantic_source, semantics, &audit) ||
		!EntitySemanticsId(&collision->content_identity, semantics,
			&semantic_identity) ||
		semantic_identity != model->identity.entity_semantics_id ||
		SG_RuneSourceAuthorityCurrent(authority) != SG_RUNE_SOURCE_OK ||
		SG_HostLawProductionAuthorityCurrent(&snapshot.host_authority).status !=
			SG_HOST_LAW_OK)
		goto done;
	source = calloc(1U, sizeof(*source));
	if (source == NULL)
		goto done;
	source->model = model;
	source->authority = authority;
	source->source_snapshot = snapshot;
	source->semantics = semantics;
	authority = NULL;
	semantics = NULL;
	*source_out = source;
	source = NULL;
	success = 1;

done:
	free(records);
	free(text);
	SG_BspEntitySemanticsDestroy(semantics);
	SG_RuneSourceAuthorityDestroy(authority);
	SG_RuneCompactPortalSnapshotSourceDestroy(source);
	return success;
}

int SG_RuneCompactPortalSnapshotSourceCurrent(
	const sg_rune_compact_portal_snapshot_source_t *source,
	const sg_rune_compact_model_t *model)
{
	return SourceLive(source, model, NULL);
}

const sg_bsp_entity_semantics_t *
SG_RuneCompactPortalSnapshotSourceEffectiveSemantics(
	const sg_rune_compact_portal_snapshot_source_t *source)
{
	return source != NULL ? source->semantics : NULL;
}

void SG_RuneCompactPortalSnapshotSourceDestroy(
	sg_rune_compact_portal_snapshot_source_t *source)
{
	if (source == NULL)
		return;
	SG_BspEntitySemanticsDestroy(source->semantics);
	SG_RuneSourceAuthorityDestroy(source->authority);
	free(source);
}
