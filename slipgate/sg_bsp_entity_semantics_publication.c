#include "sg_bsp_entity_semantics_publication.h"

#include "sg_bsp_entity_semantics_audit_internal.h"

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

	if (left_offset == SG_BSP_ENTITY_STRING_NONE ||
		right_offset == SG_BSP_ENTITY_STRING_NONE)
		return left_offset == SG_BSP_ENTITY_STRING_NONE &&
			right_offset == SG_BSP_ENTITY_STRING_NONE;
	left_string = SG_BspEntitySemanticsString(left, left_offset);
	right_string = SG_BspEntitySemanticsString(right, right_offset);
	return left_string && right_string && !strcmp(left_string, right_string);
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

static int WorldEqual(const sg_bsp_world_entity_semantics_t *left,
	const sg_bsp_world_entity_semantics_t *right)
{
	return left && right &&
		left->source_set_identity == right->source_set_identity &&
		left->source_entity_ordinal == right->source_entity_ordinal &&
		left->flags == right->flags && FloatEqual(left->gravity, right->gravity);
}

static int EntityEqual(const sg_bsp_entity_semantics_t *left_semantics,
	const sg_bsp_entity_semantic_t *left,
	const sg_bsp_entity_semantics_t *right_semantics,
	const sg_bsp_entity_semantic_t *right,
	sg_bsp_entity_semantics_fact_domain_t *domain_out)
{
	if (domain_out)
		*domain_out = SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY;
	if (!left_semantics || !left || !right_semantics || !right)
		return 0;
	if (left->source_set_identity != right->source_set_identity ||
		left->source_entity_ordinal != right->source_entity_ordinal ||
		left->canonical_ordinal != right->canonical_ordinal ||
		!StringEqual(left_semantics, left->classname, right_semantics,
			right->classname) ||
		!StringEqual(left_semantics, left->targetname, right_semantics,
			right->targetname) ||
		!StringEqual(left_semantics, left->required_item, right_semantics,
			right->required_item) ||
		!StringEqual(left_semantics, left->spawned_classname, right_semantics,
			right->spawned_classname) ||
		!StringEqual(left_semantics, left->destination_map, right_semantics,
			right->destination_map) || left->bsp_model != right->bsp_model)
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

static int EdgeEqual(const sg_bsp_entity_semantics_t *left_semantics,
	const sg_bsp_entity_semantic_edge_t *left,
	const sg_bsp_entity_semantics_t *right_semantics,
	const sg_bsp_entity_semantic_edge_t *right)
{
	return left && right && left->source == right->source &&
		left->destination == right->destination && left->kind == right->kind &&
		left->fanout_ordinal == right->fanout_ordinal &&
		StringEqual(left_semantics, left->name, right_semantics, right->name);
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
				result->duplicate_facts++;
				SetFailure(result,
					SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT,
					SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY, index);
				return 0;
			}
	}
	for (index = 0U; index < candidate->edge_count; index++)
	{
		const sg_bsp_entity_semantic_edge_t *edge = &candidate->edges[index];

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

int SG_BspEntitySemanticsAudit(const sg_bsp_world_t *world,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_audit_result_t *result_out)
{
	sg_bsp_entity_semantics_t *expected = NULL;
	uint32_t index;
	int success = 0;

	if (result_out)
	{
		memset(result_out, 0, sizeof(*result_out));
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OK;
		result_out->domain = SG_BSP_ENTITY_SEMANTICS_FACT_NONE;
		result_out->record = UINT32_MAX;
	}
	if (!result_out || !world || !binding || !candidate)
	{
		if (result_out)
			result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_ARGUMENT;
		return 0;
	}
	if (!BindingValid(binding))
	{
		SetFailure(result_out, SG_BSP_ENTITY_SEMANTICS_AUDIT_IDENTITY_MISMATCH,
			SG_BSP_ENTITY_SEMANTICS_FACT_IDENTITY, UINT32_MAX);
		return 0;
	}
	if (candidate->source_set_identity != binding->source_set_identity ||
		candidate->world.source_set_identity != binding->source_set_identity)
	{
		SetFailure(result_out, SG_BSP_ENTITY_SEMANTICS_AUDIT_IDENTITY_MISMATCH,
			SG_BSP_ENTITY_SEMANTICS_FACT_IDENTITY, UINT32_MAX);
		return 0;
	}
	if (!SG_BspEntitySemanticsAuditOwned(world, binding, candidate, &expected,
		result_out))
		return 0;
	if (candidate->entity_count != expected->entity_count)
	{
		CountDifference(result_out, expected->entity_count,
			candidate->entity_count, SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY);
		goto done;
	}
	if (candidate->edge_count != expected->edge_count)
	{
		CountDifference(result_out, expected->edge_count, candidate->edge_count,
			SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY);
		goto done;
	}
	if (!CandidateStorageValid(world, binding, candidate, result_out))
		goto done;
	if (!WorldEqual(&candidate->world, &expected->world))
	{
		RecordDisagreement(result_out, SG_BSP_ENTITY_SEMANTICS_FACT_WORLD,
			UINT32_MAX);
		goto done;
	}
	for (index = 0U; index < expected->entity_count; index++)
	{
		sg_bsp_entity_semantics_fact_domain_t domain;

		if (!EntityEqual(expected, &expected->entities[index], candidate,
			&candidate->entities[index], &domain))
		{
			RecordDisagreement(result_out, domain, index);
			goto done;
		}
	}
	for (index = 0U; index < expected->edge_count; index++)
		if (!EdgeEqual(expected, &expected->edges[index], candidate,
			&candidate->edges[index]))
		{
			RecordDisagreement(result_out,
				SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY, index);
			goto done;
		}
	success = 1;

done:
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

int SG_BspEntitySemanticsPublicationIssue(const sg_bsp_world_t *world,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_publication_t **publication_out,
	sg_bsp_entity_semantics_audit_result_t *result_out)
{
	sg_bsp_entity_semantics_publication_t *publication;

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
	if (!SG_BspEntitySemanticsAudit(world, binding, candidate, result_out))
		return 0;
	publication = calloc(1U, sizeof(*publication));
	if (!publication)
	{
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OUT_OF_MEMORY;
		return 0;
	}
	publication->magic = SG_BSP_ENTITY_SEMANTICS_PUBLICATION_MAGIC;
	publication->magic_inverse = ~SG_BSP_ENTITY_SEMANTICS_PUBLICATION_MAGIC;
	publication->self = publication;
	publication->binding = *binding;
	publication->completeness = result_out->completeness;
	publication->model_count = world->model_count;
	if (!CopySnapshot(candidate, &publication->snapshot))
	{
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OUT_OF_MEMORY;
		free(publication);
		return 0;
	}
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
