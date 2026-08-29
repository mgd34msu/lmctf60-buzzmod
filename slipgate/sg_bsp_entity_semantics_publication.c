#include "sg_bsp_entity_semantics_publication.h"

#include "sg_bsp_entity_semantics_audit_internal.h"
#include "sg_bsp_entity_semantics_storage_internal.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SG_BSP_ENTITY_SEMANTICS_PUBLICATION_MAGIC UINT64_C(0x5345535030313031)

struct sg_bsp_entity_semantics_publication_s
{
	uint64_t magic;
	uint64_t magic_inverse;
	const sg_bsp_entity_semantics_publication_t *self;
	sg_bsp_entity_semantics_binding_t binding;
	sg_bsp_entity_semantics_completeness_t completeness;
	sg_bsp_entity_semantics_t snapshot;
	uint32_t model_count;
};

static int FloatEqual(float left, float right)
{
	uint32_t left_bits;
	uint32_t right_bits;

	memcpy(&left_bits, &left, sizeof(left_bits));
	memcpy(&right_bits, &right, sizeof(right_bits));
	return left_bits == right_bits;
}

static int FloatFinite(float value)
{
	return isfinite(value) != 0;
}

static int VectorEqual(const sg_rune_vec3_t *left,
	const sg_rune_vec3_t *right)
{
	return left && right && FloatEqual(left->value[0], right->value[0]) &&
		FloatEqual(left->value[1], right->value[1]) &&
		FloatEqual(left->value[2], right->value[2]);
}

static int VectorFinite(const sg_rune_vec3_t *value)
{
	return value && FloatFinite(value->value[0]) &&
		FloatFinite(value->value[1]) && FloatFinite(value->value[2]);
}

static int BoundsEqual(const sg_rune_bounds_t *left,
	const sg_rune_bounds_t *right)
{
	return left && right && VectorEqual(&left->mins, &right->mins) &&
		VectorEqual(&left->maxs, &right->maxs);
}

static int BoundsFinite(const sg_rune_bounds_t *value)
{
	return value && VectorFinite(&value->mins) && VectorFinite(&value->maxs) &&
		value->mins.value[0] < value->maxs.value[0] &&
		value->mins.value[1] < value->maxs.value[1] &&
		value->mins.value[2] < value->maxs.value[2];
}

static int BoundsComponentsFinite(const sg_rune_bounds_t *value)
{
	return value && VectorFinite(&value->mins) && VectorFinite(&value->maxs);
}

static int StringOffsetValid(const sg_bsp_entity_semantics_t *semantics,
	uint32_t offset)
{
	if (offset == SG_BSP_ENTITY_STRING_NONE)
		return 1;
	if (!semantics->strings || offset >= semantics->string_bytes)
		return 0;
	return memchr(semantics->strings + offset, '\0',
		(size_t)(semantics->string_bytes - offset)) != NULL;
}

static int StringEqual(const sg_bsp_entity_semantics_t *left,
	uint32_t left_offset, const sg_bsp_entity_semantics_t *right,
	uint32_t right_offset)
{
	const char *left_string;
	const char *right_string;

	if (!SG_BspEntitySemanticsStringStorageValid(left) ||
		!SG_BspEntitySemanticsStringStorageValid(right))
		return 0;
	if (left_offset == SG_BSP_ENTITY_STRING_NONE ||
		right_offset == SG_BSP_ENTITY_STRING_NONE)
		return left_offset == SG_BSP_ENTITY_STRING_NONE &&
			right_offset == SG_BSP_ENTITY_STRING_NONE;
	if (!StringOffsetValid(left, left_offset) ||
		!StringOffsetValid(right, right_offset))
		return 0;
	left_string = left->strings + left_offset;
	right_string = right->strings + right_offset;
	return !strcmp(left_string, right_string);
}

static int BindingValid(const sg_bsp_entity_semantics_binding_t *binding)
{
	return binding && SG_RuneV2ContentIdValid(&binding->source_identity) &&
		binding->source_set_identity != 0U &&
		binding->source_set_identity != UINT64_MAX &&
		SG_RuneV2ContentIdEqual(&binding->schema_identity,
			&SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID);
}

static void SetFailure(sg_bsp_entity_semantics_audit_result_t *result,
	sg_bsp_entity_semantics_audit_code_t code,
	sg_bsp_entity_semantics_fact_domain_t domain, uint32_t record)
{
	if (result->code == SG_BSP_ENTITY_SEMANTICS_AUDIT_OK)
	{
		result->code = code;
		result->domain = domain;
		result->record = record;
	}
}

static void AddCount(uint32_t *value, uint32_t amount)
{
	if (UINT32_MAX - *value < amount)
		*value = UINT32_MAX;
	else
		*value += amount;
}

static void CountDifference(sg_bsp_entity_semantics_audit_result_t *result,
	uint32_t expected, uint32_t candidate,
	sg_bsp_entity_semantics_fact_domain_t domain)
{
	if (candidate < expected)
	{
		AddCount(&result->omitted_facts, expected - candidate);
		SetFailure(result, SG_BSP_ENTITY_SEMANTICS_AUDIT_OMITTED_FACT,
			domain, candidate);
	}
	else if (candidate > expected)
	{
		AddCount(&result->invented_facts, candidate - expected);
		SetFailure(result, SG_BSP_ENTITY_SEMANTICS_AUDIT_INVENTED_FACT,
			domain, expected);
	}
}

static void RecordDisagreement(
	sg_bsp_entity_semantics_audit_result_t *result,
	sg_bsp_entity_semantics_fact_domain_t domain, uint32_t record)
{
	AddCount(&result->omitted_facts, 1U);
	AddCount(&result->invented_facts, 1U);
	SetFailure(result, SG_BSP_ENTITY_SEMANTICS_AUDIT_FACT_DISAGREEMENT,
		domain, record);
}

static int StringValuesEqual(const sg_bsp_entity_semantics_t *expected,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_fact_domain_t *domain_out, uint32_t *record_out)
{
	uint32_t index;

	if (domain_out)
		*domain_out = SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY;
	if (record_out)
		*record_out = UINT32_MAX;
	if (!expected || !candidate || expected->entity_count !=
		candidate->entity_count || expected->edge_count != candidate->edge_count)
		return 0;
	for (index = 0U; index < expected->entity_count; index++)
	{
		const sg_bsp_entity_semantic_t *left = &expected->entities[index];
		const sg_bsp_entity_semantic_t *right = &candidate->entities[index];

		if (!StringEqual(expected, left->classname, candidate, right->classname) ||
			!StringEqual(expected, left->targetname, candidate, right->targetname) ||
			!StringEqual(expected, left->required_item, candidate,
				right->required_item) ||
			!StringEqual(expected, left->spawned_classname, candidate,
				right->spawned_classname) ||
			!StringEqual(expected, left->destination_map, candidate,
					right->destination_map))
			return 0;
	}
	for (index = 0U; index < expected->edge_count; index++)
		if (!StringEqual(expected, expected->edges[index].name, candidate,
			candidate->edges[index].name))
		{
			if (domain_out)
				*domain_out = SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY;
			if (record_out)
				*record_out = index;
			return 0;
		}
	return 1;
}

static int WorldEqual(const sg_bsp_world_entity_semantics_t *left,
	const sg_bsp_world_entity_semantics_t *right)
{
	return left && right &&
		left->source_set_identity == right->source_set_identity &&
		left->source_entity_ordinal == right->source_entity_ordinal &&
		left->flags == right->flags && FloatEqual(left->gravity, right->gravity);
}

static int EntityEqual(const sg_bsp_entity_semantic_t *left,
	const sg_bsp_entity_semantic_t *right,
	sg_bsp_entity_semantics_fact_domain_t *domain_out)
{
	if (domain_out)
		*domain_out = SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY;
	if (!left || !right)
		return 0;
	if (left->source_set_identity != right->source_set_identity ||
		left->source_entity_ordinal != right->source_entity_ordinal ||
		left->canonical_ordinal != right->canonical_ordinal ||
		left->bsp_model != right->bsp_model)
		return 0;
	if (left->flags != right->flags)
	{
		if (domain_out && ((left->flags ^ right->flags) &
			(SG_BSP_ENTITY_HAS_LANDMARK | SG_BSP_ENTITY_FLAG_RED |
			 SG_BSP_ENTITY_FLAG_BLUE)))
			*domain_out = SG_BSP_ENTITY_SEMANTICS_FACT_LANDMARK;
		else if (domain_out && ((left->flags ^ right->flags) &
			SG_BSP_ENTITY_HAS_MECHANISM))
			*domain_out = SG_BSP_ENTITY_SEMANTICS_FACT_MECHANISM;
		return 0;
	}
	if (left->landmark_kind != right->landmark_kind)
	{
		if (domain_out)
			*domain_out = SG_BSP_ENTITY_SEMANTICS_FACT_LANDMARK;
		return 0;
	}
	if (left->mechanism_kind != right->mechanism_kind ||
		left->mechanism_role != right->mechanism_role)
	{
		if (domain_out)
			*domain_out = SG_BSP_ENTITY_SEMANTICS_FACT_MECHANISM;
		return 0;
	}
	if (left->physics_kind != right->physics_kind ||
		!VectorEqual(&left->origin, &right->origin) ||
		!VectorEqual(&left->angles, &right->angles) ||
		!VectorEqual(&left->move_direction, &right->move_direction) ||
		!VectorEqual(&left->move_origin, &right->move_origin) ||
		!VectorEqual(&left->move_angles, &right->move_angles) ||
		!BoundsEqual(&left->bounds, &right->bounds) ||
		!FloatEqual(left->delay_ms, right->delay_ms) ||
		!FloatEqual(left->dwell_ms, right->dwell_ms) ||
		!FloatEqual(left->pause_ms, right->pause_ms) ||
		!FloatEqual(left->speed, right->speed) ||
		!FloatEqual(left->acceleration, right->acceleration) ||
		!FloatEqual(left->deceleration, right->deceleration) ||
		!FloatEqual(left->lip, right->lip) ||
		!FloatEqual(left->height, right->height) ||
		!FloatEqual(left->distance, right->distance) ||
		!FloatEqual(left->gravity, right->gravity) ||
		!FloatEqual(left->random, right->random) ||
		left->damage != right->damage || left->count != right->count ||
		left->health != right->health || left->style != right->style ||
		left->spawnflags != right->spawnflags)
		return 0;
	return 1;
}

static int EdgeEqual(const sg_bsp_entity_semantic_edge_t *left,
	const sg_bsp_entity_semantic_edge_t *right)
{
	return left && right && left->source == right->source &&
		left->destination == right->destination && left->kind == right->kind &&
		left->fanout_ordinal == right->fanout_ordinal;
}

static int EdgeTopologyDuplicate(const sg_bsp_entity_semantic_edge_t *left,
	const sg_bsp_entity_semantic_edge_t *right)
{
	return left && right && left->source == right->source &&
		left->destination == right->destination && left->kind == right->kind;
}

static int EntityStringsValid(const sg_bsp_entity_semantics_t *semantics,
	const sg_bsp_entity_semantic_t *entity)
{
	return StringOffsetValid(semantics, entity->classname) &&
		StringOffsetValid(semantics, entity->targetname) &&
		StringOffsetValid(semantics, entity->required_item) &&
		StringOffsetValid(semantics, entity->spawned_classname) &&
		StringOffsetValid(semantics, entity->destination_map);
}

static int EntityValuesFinite(const sg_bsp_entity_semantic_t *entity)
{
	return VectorFinite(&entity->origin) && VectorFinite(&entity->angles) &&
		VectorFinite(&entity->move_direction) &&
		VectorFinite(&entity->move_origin) &&
		VectorFinite(&entity->move_angles) &&
		BoundsComponentsFinite(&entity->bounds) &&
		(!(entity->flags & SG_BSP_ENTITY_HAS_BOUNDS) ||
			BoundsFinite(&entity->bounds)) && FloatFinite(entity->delay_ms) &&
		FloatFinite(entity->dwell_ms) && FloatFinite(entity->pause_ms) &&
		FloatFinite(entity->speed) && FloatFinite(entity->acceleration) &&
		FloatFinite(entity->deceleration) && FloatFinite(entity->lip) &&
		FloatFinite(entity->height) && FloatFinite(entity->distance) &&
		FloatFinite(entity->gravity) && FloatFinite(entity->random);
}

static int CandidatePointersValid(const sg_bsp_entity_semantics_t *candidate)
{
	return candidate &&
		(candidate->entity_count == 0U || candidate->entities) &&
		(candidate->edge_count == 0U || candidate->edges) &&
		(candidate->string_bytes == 0U || candidate->strings);
}

static int CandidateStorageValid(const sg_bsp_world_t *world,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_audit_result_t *result)
{
	uint32_t index;

	if (!CandidatePointersValid(candidate))
	{
		AddCount(&result->invalid_facts, 1U);
		SetFailure(result, SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT,
			SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY, UINT32_MAX);
		return 0;
	}
	if (!SG_BspEntitySemanticsStringStorageValid(candidate))
	{
		AddCount(&result->invalid_facts, 1U);
		SetFailure(result, SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT,
			SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY, UINT32_MAX);
		return 0;
	}
	if (!FloatFinite(candidate->world.gravity))
	{
		AddCount(&result->invalid_facts, 1U);
		SetFailure(result, SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT,
			SG_BSP_ENTITY_SEMANTICS_FACT_WORLD, UINT32_MAX);
		return 0;
	}
	for (index = 0U; index < candidate->entity_count; index++)
	{
		const sg_bsp_entity_semantic_t *entity = &candidate->entities[index];
		uint32_t previous;

		if (entity->source_set_identity != binding->source_set_identity ||
			entity->source_entity_ordinal >= world->entity_byte_count ||
			(entity->bsp_model != SG_BSP_ENTITY_MODEL_NONE &&
				entity->bsp_model >= world->model_count) ||
			!EntityStringsValid(candidate, entity) ||
			!EntityValuesFinite(entity))
		{
			AddCount(&result->invalid_facts, 1U);
			SetFailure(result, SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT,
				SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY, index);
			return 0;
		}
		for (previous = 0U; previous < index; previous++)
			if (candidate->entities[previous].source_entity_ordinal ==
				entity->source_entity_ordinal)
			{
				AddCount(&result->duplicate_facts, 1U);
				SetFailure(result,
					SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT,
					SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY, index);
				return 0;
			}
	}
	for (index = 0U; index < candidate->edge_count; index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge = &candidate->edges[index];
		uint32_t previous;

		if (edge->source >= candidate->entity_count ||
			edge->destination >= candidate->entity_count)
		{
			AddCount(&result->unresolved_facts, 1U);
			SetFailure(result,
				SG_BSP_ENTITY_SEMANTICS_AUDIT_UNRESOLVED_FACT,
				SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY, index);
			return 0;
		}
		if (edge->kind < SG_MECH_EDGE_TARGET ||
			edge->kind > SG_MECH_EDGE_ROUTE_TARGET ||
			!StringOffsetValid(candidate, edge->name))
		{
			AddCount(&result->invalid_facts, 1U);
			SetFailure(result, SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT,
				SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY, index);
			return 0;
		}
		for (previous = 0U; previous < index; previous++)
			if (EdgeTopologyDuplicate(&candidate->edges[previous], edge))
			{
				AddCount(&result->duplicate_facts, 1U);
				SetFailure(result,
					SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT,
					SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY, index);
				return 0;
			}
	}
	for (index = 0U; index < candidate->entity_count; index++)
	{
		uint32_t previous;
		uint32_t model = candidate->entities[index].bsp_model;

		if (model == SG_BSP_ENTITY_MODEL_NONE)
			continue;
		for (previous = 0U; previous < index; previous++)
			if (candidate->entities[previous].bsp_model == model)
			{
				AddCount(&result->duplicate_facts, 1U);
				SetFailure(result,
					SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT,
					SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY, index);
				return 0;
			}
	}
	return 1;
}

static int SnapshotStorageValid(const sg_bsp_entity_semantics_t *snapshot,
	uint32_t model_count)
{
	uint32_t index;

	if (!CandidatePointersValid(snapshot) || model_count == 0U ||
		!FloatFinite(snapshot->world.gravity))
		return 0;
	for (index = 0U; index < snapshot->entity_count; index++)
	{
		const sg_bsp_entity_semantic_t *entity = &snapshot->entities[index];

		if (entity->source_set_identity != snapshot->source_set_identity ||
			(entity->bsp_model != SG_BSP_ENTITY_MODEL_NONE &&
				entity->bsp_model >= model_count) ||
			!EntityStringsValid(snapshot, entity) ||
			!EntityValuesFinite(entity))
			return 0;
	}
	for (index = 0U; index < snapshot->edge_count; index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge = &snapshot->edges[index];

		if (edge->source >= snapshot->entity_count ||
			edge->destination >= snapshot->entity_count ||
			edge->kind < SG_MECH_EDGE_TARGET ||
			edge->kind > SG_MECH_EDGE_ROUTE_TARGET ||
			!StringOffsetValid(snapshot, edge->name))
			return 0;
	}
	return 1;
}

static void FreeSnapshot(sg_bsp_entity_semantics_t *snapshot)
{
	if (!snapshot)
		return;
	free(snapshot->entities);
	free(snapshot->edges);
	free(snapshot->strings);
	memset(snapshot, 0, sizeof(*snapshot));
}

static int PublicationValid(
	const sg_bsp_entity_semantics_publication_t *publication)
{
	return publication && publication->magic ==
		SG_BSP_ENTITY_SEMANTICS_PUBLICATION_MAGIC &&
		publication->magic_inverse ==
			~SG_BSP_ENTITY_SEMANTICS_PUBLICATION_MAGIC &&
		publication->self == publication && BindingValid(&publication->binding) &&
		(publication->completeness == SG_BSP_ENTITY_SEMANTICS_PROVEN_EMPTY ||
			publication->completeness == SG_BSP_ENTITY_SEMANTICS_COMPLETE) &&
		publication->snapshot.source_set_identity ==
			publication->binding.source_set_identity &&
		publication->snapshot.world.source_set_identity ==
			publication->binding.source_set_identity &&
		publication->model_count != 0U &&
		SnapshotStorageValid(&publication->snapshot, publication->model_count);
}

static int CompareCandidate(const sg_bsp_world_t *world,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	const sg_bsp_entity_semantics_t *expected,
	sg_bsp_entity_semantics_audit_result_t *result_out)
{
	uint32_t index;
	if (candidate->entity_count != expected->entity_count)
	{
		CountDifference(result_out, expected->entity_count,
			candidate->entity_count, SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY);
		return 0;
	}
	if (candidate->edge_count != expected->edge_count)
	{
		CountDifference(result_out, expected->edge_count, candidate->edge_count,
			SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY);
		return 0;
	}
	if (!CandidateStorageValid(world, binding, candidate, result_out))
		return 0;
	{
		sg_bsp_entity_semantics_fact_domain_t domain;
		uint32_t record;

		if (!StringValuesEqual(expected, candidate, &domain, &record))
		{
			RecordDisagreement(result_out, domain, record);
			return 0;
		}
	}
	if (!WorldEqual(&candidate->world, &expected->world))
	{
		RecordDisagreement(result_out, SG_BSP_ENTITY_SEMANTICS_FACT_WORLD,
			UINT32_MAX);
		return 0;
	}
	for (index = 0U; index < expected->entity_count; index++)
	{
		sg_bsp_entity_semantics_fact_domain_t domain;

		if (!EntityEqual(&expected->entities[index],
			&candidate->entities[index], &domain))
		{
			RecordDisagreement(result_out, domain, index);
			return 0;
		}
	}
	for (index = 0U; index < expected->edge_count; index++)
		if (!EdgeEqual(&expected->edges[index], &candidate->edges[index]))
		{
			RecordDisagreement(result_out,
				SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY, index);
			return 0;
		}
	return 1;
}

int SG_BspEntitySemanticsAudit(
	const sg_host_collision_authority_t *authority,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_audit_result_t *result_out)
{
	sg_bsp_entity_semantics_t *expected = NULL;
	int success;

	if (result_out)
	{
		memset(result_out, 0, sizeof(*result_out));
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OK;
		result_out->domain = SG_BSP_ENTITY_SEMANTICS_FACT_NONE;
		result_out->record = UINT32_MAX;
	}
	if (!result_out || !authority || !binding || !candidate)
	{
		if (result_out)
			result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_ARGUMENT;
		return 0;
	}
	if (!SG_BspEntitySemanticsAuditOwned(authority, binding, candidate,
		&expected, result_out))
		return 0;
	success = CompareCandidate(authority->world, binding, candidate, expected,
		result_out);
	SG_BspEntitySemanticsDestroy(expected);
	return success;
}

static int CopySnapshot(const sg_bsp_entity_semantics_t *source,
	sg_bsp_entity_semantics_t *destination)
{
	size_t bytes;

	memset(destination, 0, sizeof(*destination));
	*destination = *source;
	destination->entities = NULL;
	destination->edges = NULL;
	destination->strings = NULL;
	if (!SG_BspEntitySemanticsCountsRepresentable(source->entity_count,
		source->edge_count, source->string_bytes))
		goto failed;
	if (source->entity_count)
	{
		bytes = (size_t)source->entity_count * sizeof(*destination->entities);
		destination->entities = malloc(bytes);
		if (!destination->entities)
			goto failed;
		memcpy(destination->entities, source->entities, bytes);
	}
	if (source->edge_count)
	{
		bytes = (size_t)source->edge_count * sizeof(*destination->edges);
		destination->edges = malloc(bytes);
		if (!destination->edges)
			goto failed;
		memcpy(destination->edges, source->edges, bytes);
	}
	if (source->string_bytes)
	{
		destination->strings = malloc((size_t)source->string_bytes);
		if (!destination->strings)
			goto failed;
		memcpy(destination->strings, source->strings,
			(size_t)source->string_bytes);
	}
	return 1;

failed:
	free(destination->entities);
	free(destination->edges);
	free(destination->strings);
	memset(destination, 0, sizeof(*destination));
	return 0;
}

int SG_BspEntitySemanticsPublicationIssue(
	const sg_host_collision_authority_t *authority,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_publication_t **publication_out,
	sg_bsp_entity_semantics_audit_result_t *result_out)
{
	sg_bsp_entity_semantics_publication_t *publication;
	sg_bsp_entity_semantics_t *expected = NULL;

	if (!publication_out || *publication_out || !result_out)
	{
		if (result_out)
		{
			memset(result_out, 0, sizeof(*result_out));
			result_out->code =
				SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_ARGUMENT;
			result_out->record = UINT32_MAX;
		}
		return 0;
	}
	if (!SG_BspEntitySemanticsAuditOwned(authority, binding, candidate,
		&expected, result_out) ||
		!CompareCandidate(authority->world, binding, candidate, expected,
			result_out))
	{
		SG_BspEntitySemanticsDestroy(expected);
		return 0;
	}
	/* The publication owns a fresh source reconstruction.  Candidate string
	 * extents are never copied, so trailing caller storage cannot escape audit. */
	publication = calloc(1U, sizeof(*publication));
	if (!publication)
	{
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OUT_OF_MEMORY;
		SG_BspEntitySemanticsDestroy(expected);
		return 0;
	}
	publication->magic = SG_BSP_ENTITY_SEMANTICS_PUBLICATION_MAGIC;
	publication->magic_inverse = ~SG_BSP_ENTITY_SEMANTICS_PUBLICATION_MAGIC;
	publication->self = publication;
	publication->binding = *binding;
	publication->completeness = result_out->completeness;
	publication->model_count = authority->world->model_count;
	if (!CopySnapshot(expected, &publication->snapshot))
	{
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OUT_OF_MEMORY;
		SG_BspEntitySemanticsDestroy(expected);
		free(publication);
		return 0;
	}
	SG_BspEntitySemanticsDestroy(expected);
	if (!PublicationValid(publication))
	{
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT;
		FreeSnapshot(&publication->snapshot);
		free(publication);
		return 0;
	}
	*publication_out = publication;
	return 1;
}

int SG_BspEntitySemanticsPublicationRead(
	const sg_bsp_entity_semantics_publication_t *publication,
	sg_bsp_entity_semantics_view_t *view_out)
{
	if (!PublicationValid(publication) || !view_out)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->binding = publication->binding;
	view_out->completeness = publication->completeness;
	view_out->world = publication->snapshot.world;
	view_out->entities = publication->snapshot.entities;
	view_out->entity_count = publication->snapshot.entity_count;
	view_out->edges = publication->snapshot.edges;
	view_out->edge_count = publication->snapshot.edge_count;
	view_out->strings = publication->snapshot.strings;
	view_out->string_bytes = publication->snapshot.string_bytes;
	return 1;
}

const char *SG_BspEntitySemanticsViewString(
	const sg_bsp_entity_semantics_view_t *view, uint32_t offset)
{
	if (!view || offset == SG_BSP_ENTITY_STRING_NONE ||
		!view->strings || offset >= view->string_bytes)
		return NULL;
	if (!memchr(view->strings + offset, '\0',
		(size_t)(view->string_bytes - offset)))
		return NULL;
	return view->strings + offset;
}

void SG_BspEntitySemanticsPublicationDestroy(
	sg_bsp_entity_semantics_publication_t *publication)
{
	if (!PublicationValid(publication))
		return;
	publication->magic = 0U;
	publication->magic_inverse = 0U;
	publication->self = NULL;
	FreeSnapshot(&publication->snapshot);
	free(publication);
}

const char *SG_BspEntitySemanticsAuditCodeString(
	sg_bsp_entity_semantics_audit_code_t code)
{
	switch (code) {
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_OK:
		return "ok";
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_SOURCE:
		return "invalid source";
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT:
		return "invalid fact";
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT:
		return "duplicate fact";
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_UNRESOLVED_FACT:
		return "unresolved fact";
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_OMITTED_FACT:
		return "omitted fact";
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_INVENTED_FACT:
		return "invented fact";
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_FACT_DISAGREEMENT:
		return "fact disagreement";
	case SG_BSP_ENTITY_SEMANTICS_AUDIT_OUT_OF_MEMORY:
		return "out of memory";
	default:
		return "unknown audit code";
	}
}
