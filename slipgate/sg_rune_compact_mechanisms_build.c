#include "sg_rune_compact_mechanisms_build.h"

#include "sg_configuration_lattice.h"
#include "sg_rune_compact_builder_owner.h"
#include "sg_rune_compact_localize.h"
#include "sg_rune_compact_mechanisms_entities.h"
#include "sg_rune_compact_mechanisms_transitions.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#if defined(SG_RUNE_COMPACT_MECHANISMS_BUILD_TESTING)
static size_t test_fail_after = SIZE_MAX;
static size_t test_allocation_count;

void SG_RuneCompactMechanismsBuildTestFailAfter(size_t allocation)
{
	test_fail_after = allocation;
	test_allocation_count = 0U;
}

size_t SG_RuneCompactMechanismsBuildTestAllocationCount(void)
{
	return test_allocation_count;
}
#endif

static void SetError(sg_rune_compact_mechanisms_error_t *error,
	sg_rune_compact_mechanisms_error_code_t code,
	sg_rune_compact_mechanisms_record_domain_t domain, uint32_t record)
{
	if (error == NULL)
		return;
	error->code = code;
	error->domain = domain;
	error->record = record;
}

static void *BuildAllocate(size_t bytes)
{
#if defined(SG_RUNE_COMPACT_MECHANISMS_BUILD_TESTING)
	if (test_allocation_count == test_fail_after)
	{
		test_allocation_count++;
		return NULL;
	}
	test_allocation_count++;
#endif
	return malloc(bytes);
}

static int SizeMultiply(size_t count, size_t width, size_t *bytes_out)
{
	if (bytes_out == NULL || (width != 0U && count > SIZE_MAX / width))
		return 0;
	*bytes_out = count * width;
	return 1;
}

static int ArrayShapeValid(const void *records, uint32_t count)
{
	return (records != NULL) == (count != 0U);
}

static int GeometryArraysValid(
	const sg_rune_compact_geometry_view_t *geometry)
{
	return geometry != NULL &&
		ArrayShapeValid(geometry->cells, geometry->cell_count) &&
		ArrayShapeValid(geometry->facets, geometry->facet_count) &&
		ArrayShapeValid(geometry->incidences, geometry->incidence_count) &&
		ArrayShapeValid(geometry->cell_incidences,
			geometry->cell_incidence_count);
}

static int AllocateArray(void **records_out, uint32_t count, size_t width,
	sg_rune_compact_mechanisms_error_t *error)
{
	size_t bytes;

	*records_out = NULL;
	if (count == 0U)
		return 1;
	if (!SizeMultiply((size_t)count, width, &bytes))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, count);
		return 0;
	}
	*records_out = BuildAllocate(bytes);
	if (*records_out == NULL)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, count);
		return 0;
	}
	memset(*records_out, 0, bytes);
	return 1;
}

static float BitsFloat(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int FloatToQ8(float value, int32_t *output)
{
	double rounded;

	if (output == NULL || !isfinite(value))
		return 0;
	rounded = nearbyint((double)value * 8.0);
	if (!isfinite(rounded) || rounded < (double)INT32_MIN ||
		rounded > (double)INT32_MAX)
		return 0;
	*output = (int32_t)rounded;
	return 1;
}

static int EntityBounds(const sg_bsp_entity_semantic_t *entity,
	sg_rune_q8_bounds_t *bounds_out, sg_rune_q8_vec3_t *point_out,
	int *has_bounds_out)
{
	uint32_t axis;

	if (entity == NULL || bounds_out == NULL || point_out == NULL ||
		has_bounds_out == NULL)
		return 0;
	*has_bounds_out =
		(entity->flags & SG_BSP_ENTITY_HAS_BOUNDS) != 0U;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!FloatToQ8(entity->origin.value[axis],
				&point_out->value[axis]))
			return 0;
		if (*has_bounds_out)
		{
			if (!FloatToQ8(entity->bounds.mins.value[axis],
					&bounds_out->mins.value[axis]) ||
				!FloatToQ8(entity->bounds.maxs.value[axis],
					&bounds_out->maxs.value[axis]) ||
				bounds_out->mins.value[axis] >=
					bounds_out->maxs.value[axis])
				return 0;
		}
		else
		{
			if (point_out->value[axis] == INT32_MIN ||
				point_out->value[axis] == INT32_MAX)
				return 0;
			bounds_out->mins.value[axis] = point_out->value[axis] - 1;
			bounds_out->maxs.value[axis] = point_out->value[axis] + 1;
		}
	}
	return 1;
}

static void GeometryModel(const sg_rune_compact_geometry_view_t *geometry,
	sg_rune_compact_model_t *model_out)
{
	memset(model_out, 0, sizeof(*model_out));
	model_out->cells = geometry->cells;
	model_out->cell_count = geometry->cell_count;
	model_out->facets = geometry->facets;
	model_out->facet_count = geometry->facet_count;
	model_out->incidences = geometry->incidences;
	model_out->incidence_count = geometry->incidence_count;
	model_out->cell_incidences = geometry->cell_incidences;
	model_out->cell_incidence_count = geometry->cell_incidence_count;
}

static int PointInBounds(const sg_rune_q8_vec3_t *point,
	const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (point->value[axis] < bounds->mins.value[axis] ||
			point->value[axis] > bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int LocatePoint(const sg_rune_compact_model_t *model,
	const sg_rune_q8_vec3_t *point, sg_rune_compact_cell_index_t *cell_out)
{
	sg_rune_compact_location_t location;

	if (SG_RuneCompactLocalize(model, point, &location) !=
		SG_RUNE_COMPACT_LOCALIZE_OK)
		return 0;
	*cell_out = location.cell;
	return 1;
}

static int AppendCellHalfspaces(const sg_rune_compact_geometry_view_t *geometry,
	uint32_t cell_index, sg_configuration_lattice_halfspace_t *halfspaces,
	uint32_t *count_out)
{
	const sg_rune_compact_cell_t *cell;
	uint32_t local;
	uint32_t count = 0U;

	if (geometry == NULL || halfspaces == NULL || count_out == NULL ||
		cell_index >= geometry->cell_count)
		return 0;
	cell = &geometry->cells[cell_index];
	if (cell->incidences.first > geometry->cell_incidence_count ||
		cell->incidences.count > geometry->cell_incidence_count -
			cell->incidences.first ||
		(cell->incidences.count != 0U &&
			geometry->cell_incidences == NULL))
		return 0;
	for (local = 0U; local < cell->incidences.count; local++)
	{
		const uint32_t reference = cell->incidences.first + local;
		const uint32_t incidence_index =
			geometry->cell_incidences[reference].value;
		const sg_rune_compact_incidence_t *incidence;
		const sg_rune_binary32_plane_t *plane;
		float sign;
		uint32_t axis;

		if (incidence_index >= geometry->incidence_count ||
			geometry->incidences == NULL || geometry->facets == NULL)
			return 0;
		incidence = &geometry->incidences[incidence_index];
		if (incidence->cell.value != cell_index ||
			incidence->cell_ordinal != local ||
			incidence->facet.value >= geometry->facet_count ||
			incidence->side < SG_RUNE_FACET_NEGATIVE_SIDE ||
			incidence->side >= SG_RUNE_FACET_SIDE_COUNT ||
			incidence->boundary < SG_RUNE_BOUNDARY_OPEN ||
			incidence->boundary >= SG_RUNE_BOUNDARY_OWNERSHIP_COUNT)
			return 0;
		plane = &geometry->facets[incidence->facet.value].plane;
		sign = incidence->side == SG_RUNE_FACET_NEGATIVE_SIDE ? 1.0f : -1.0f;
		for (axis = 0U; axis < 3U; axis++)
			halfspaces[count].normal[axis] =
				sign * BitsFloat(plane->normal_bits[axis]);
		halfspaces[count].distance =
			sign * BitsFloat(plane->distance_bits);
		halfspaces[count].open =
			incidence->boundary == SG_RUNE_BOUNDARY_OPEN;
		count++;
	}
	*count_out = count;
	return 1;
}

static void AppendBoundsHalfspaces(const sg_rune_q8_bounds_t *bounds,
	sg_configuration_lattice_halfspace_t *halfspaces, uint32_t *count)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		memset(&halfspaces[*count], 0, sizeof(halfspaces[*count]));
		halfspaces[*count].normal[axis] = -1.0f;
		halfspaces[*count].distance =
			-(float)bounds->mins.value[axis] * 0.125f;
		(*count)++;
		memset(&halfspaces[*count], 0, sizeof(halfspaces[*count]));
		halfspaces[*count].normal[axis] = 1.0f;
		halfspaces[*count].distance =
			(float)bounds->maxs.value[axis] * 0.125f;
		(*count)++;
	}
}

static int LocateEntity(
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_model_t *model,
	const sg_bsp_entity_semantic_t *entity,
	sg_configuration_lattice_halfspace_t *halfspaces,
	sg_rune_compact_cell_index_t *cell_out,
	sg_rune_q8_vec3_t *witness_out, sg_rune_q8_bounds_t *bounds_out)
{
	sg_rune_q8_vec3_t origin;
	int has_bounds;
	uint32_t cell;

	if (!EntityBounds(entity, bounds_out, &origin, &has_bounds))
		return 0;
	if ((!has_bounds || PointInBounds(&origin, bounds_out)) &&
		LocatePoint(model, &origin, cell_out))
	{
		*witness_out = origin;
		return 1;
	}
	for (cell = 0U; cell < geometry->cell_count; cell++)
	{
		sg_configuration_lattice_stats_t stats;
		int32_t point[3];
		uint32_t halfspace_count;
		int found;

		memset(&stats, 0, sizeof(stats));
		if (!AppendCellHalfspaces(geometry, cell, halfspaces,
				&halfspace_count))
			return 0;
		AppendBoundsHalfspaces(&geometry->cells[cell].bounds, halfspaces,
			&halfspace_count);
		AppendBoundsHalfspaces(bounds_out, halfspaces, &halfspace_count);
		found = SG_ConfigurationLatticeFind(halfspaces, halfspace_count, NULL,
			point, &stats);
		if (found < 0)
			return -1;
		if (found == 0)
			continue;
		witness_out->value[0] = point[0];
		witness_out->value[1] = point[1];
		witness_out->value[2] = point[2];
		if (!PointInBounds(witness_out, bounds_out) ||
			!LocatePoint(model, witness_out, cell_out))
			return -1;
		return 1;
	}
	return 0;
}

static void CopyAuthorityScalars(
	const sg_rune_compact_mechanism_entity_authority_t *source,
	sg_rune_compact_mechanism_authority_t *destination)
{
	destination->source = source->source;
	destination->kind = source->kind;
	destination->activation = source->activation;
	destination->controllers = source->controllers;
	destination->topology = source->topology;
	destination->delay_ms = source->delay_ms;
	destination->dwell_ms = source->dwell_ms;
	destination->pause_ms = source->pause_ms;
	destination->travel_ms = source->travel_ms;
	destination->damage = source->damage;
	destination->health = source->health;
	destination->required_item = source->required_item;
	destination->initial_state = source->initial_state;
	destination->activated_state = source->activated_state;
	destination->reset_state = source->reset_state;
	destination->recovery_ms = source->recovery_ms;
	destination->flags = source->flags;
}

static int MoverTransition(
	const sg_rune_compact_mechanism_transition_t *transition)
{
	return transition->kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE ||
		transition->kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT;
}

static int ToggleMechanism(
	const sg_rune_compact_mechanism_authority_t *mechanism)
{
	return (mechanism->flags &
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT) == 0U &&
		mechanism->initial_state != mechanism->activated_state &&
		mechanism->reset_state == mechanism->activated_state;
}

/* A mechanism-level duration is publishable only when every authenticated
 * mover transition agrees.  Individual transitions always keep their exact
 * host elapsed time, so zero here is the explicit no-aggregate sentinel. */
static int PopulateTimingAggregates(
	sg_rune_compact_mechanisms_candidate_t *candidate,
	sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t mechanism_index;

	for (mechanism_index = 0U; mechanism_index < candidate->mechanism_count;
		mechanism_index++)
	{
		sg_rune_compact_mechanism_authority_t *mechanism =
			&candidate->mechanisms[mechanism_index];
		uint32_t travel = SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED;
		uint32_t recovery = SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED;
		int uniform_travel = 1;
		int uniform_recovery = 1;
		int saw_travel = 0;
		int saw_recovery = 0;
		uint32_t transition_index;

		for (transition_index = mechanism->transitions.first;
			transition_index < mechanism->transitions.first +
				mechanism->transitions.count;
			transition_index++)
		{
			const sg_rune_compact_mechanism_transition_t *transition =
				&candidate->transitions[transition_index];
			const uint32_t elapsed = (uint32_t)transition->elapsed_ms;

			if (!MoverTransition(transition))
				continue;
			if (transition->elapsed_ms == 0U ||
				transition->elapsed_ms > UINT32_MAX)
			{
				SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
					SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION,
					transition_index);
				return 0;
			}
			if (!saw_travel)
				travel = elapsed;
			else if (travel != elapsed)
				uniform_travel = 0;
			saw_travel = 1;
			if (!ToggleMechanism(mechanism) &&
				transition->source_state == mechanism->activated_state &&
				transition->destination_state == mechanism->reset_state)
			{
				if (!saw_recovery)
					recovery = elapsed;
				else if (recovery != elapsed)
					uniform_recovery = 0;
				saw_recovery = 1;
			}
		}
		mechanism->travel_ms = saw_travel && uniform_travel ? travel :
			SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED;
		mechanism->recovery_ms = saw_recovery && uniform_recovery ? recovery :
			SG_RUNE_COMPACT_MECHANISM_TIME_UNSPECIFIED;
		for (transition_index = mechanism->transitions.first;
			transition_index < mechanism->transitions.first +
				mechanism->transitions.count;
			transition_index++)
		{
			sg_rune_compact_mechanism_transition_t *transition =
				&candidate->transitions[transition_index];

			if (transition->kind ==
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE)
				transition->value.portal_state.recovery_ms = mechanism->recovery_ms;
		}
	}
	return 1;
}

static int TransitionSpansValid(
	const sg_rune_compact_mechanism_transitions_result_t *transitions,
	uint32_t mechanism_count, sg_rune_compact_mechanisms_error_t *error)
{
	uint32_t index;
	uint32_t next = 0U;

	if (transitions->mechanism_count != mechanism_count ||
		!ArrayShapeValid(transitions->spans, transitions->mechanism_count) ||
		!ArrayShapeValid(transitions->transitions,
			transitions->transition_count))
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		return 0;
	}
	for (index = 0U; index < mechanism_count; index++)
	{
		const sg_rune_compact_mechanism_span_t span =
			transitions->spans[index];

		if (span.first != next || span.first > transitions->transition_count ||
			span.count > transitions->transition_count - span.first)
		{
			SetError(error,
				SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_TRANSITION, index);
			return 0;
		}
		next = span.first + span.count;
	}
	if (next != transitions->transition_count)
	{
		SetError(error, SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, next);
		return 0;
	}
	return 1;
}

int SG_RuneCompactMechanismsBuildCandidate(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_mechanisms_candidate_t *candidate_out,
	sg_rune_compact_mechanisms_error_t *error_out)
{
	sg_rune_compact_builder_view_t builder_view;
	sg_rune_compact_builder_owner_view_t owner_view;
	sg_rune_compact_geometry_view_t geometry_view;
	sg_rune_compact_model_t geometry_model;
	sg_rune_compact_mechanisms_entities_t entities;
	sg_rune_compact_mechanism_transitions_result_t transitions;
	sg_rune_compact_mechanisms_candidate_t candidate;
	sg_configuration_lattice_halfspace_t *halfspaces = NULL;
	uint32_t maximum_incidences = 0U;
	uint32_t index;
	int success = 0;

	memset(&builder_view, 0, sizeof(builder_view));
	memset(&owner_view, 0, sizeof(owner_view));
	memset(&geometry_view, 0, sizeof(geometry_view));
	memset(&geometry_model, 0, sizeof(geometry_model));
	memset(&entities, 0, sizeof(entities));
	memset(&transitions, 0, sizeof(transitions));
	memset(&candidate, 0, sizeof(candidate));
	if (error_out != NULL)
		memset(error_out, 0, sizeof(*error_out));
	if (builder == NULL || geometry == NULL || candidate_out == NULL ||
		!SG_RuneCompactBuilderRead(builder, &builder_view) ||
		!SG_RuneCompactBuilderOwnerRead(builder, &owner_view) ||
		!SG_RuneCompactGeometryRead(geometry, &geometry_view))
	{
		SetError(error_out,
			SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		goto cleanup;
	}
	if (!SG_RuneCompactIdentityMatches(&builder_view.identity,
			&owner_view.identity) ||
		!SG_RuneCompactIdentityMatches(&builder_view.identity,
			&geometry_view.identity))
	{
		SetError(error_out,
			SG_RUNE_COMPACT_MECHANISMS_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_BUILDER, 0U);
		goto cleanup;
	}
	if (!GeometryArraysValid(&geometry_view))
	{
		SetError(error_out,
			SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		goto cleanup;
	}
	GeometryModel(&geometry_view, &geometry_model);
	if (!SG_RuneCompactMechanismEntitiesEnumerate(builder, &entities,
			error_out))
		goto cleanup;
	if (!ArrayShapeValid(entities.mechanisms, entities.mechanism_count) ||
		!ArrayShapeValid(entities.controllers, entities.controller_count) ||
		!ArrayShapeValid(entities.topology_edges,
			entities.topology_edge_count))
	{
		SetError(error_out,
			SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_RESULT, 0U);
		goto cleanup;
	}
	if (!AllocateArray((void **)&candidate.mechanisms,
			entities.mechanism_count, sizeof(*candidate.mechanisms), error_out) ||
		!AllocateArray((void **)&candidate.controllers,
			entities.controller_count, sizeof(*candidate.controllers), error_out))
		goto cleanup;
	for (index = 0U; index < geometry_view.cell_count; index++)
		if (geometry_view.cells[index].incidences.count > maximum_incidences)
			maximum_incidences = geometry_view.cells[index].incidences.count;
	if (maximum_incidences > UINT32_MAX - 12U)
	{
		SetError(error_out, SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_MECHANISMS_RECORD_CELL, maximum_incidences);
		goto cleanup;
	}
	if ((entities.mechanism_count != 0U || entities.controller_count != 0U) &&
		!AllocateArray((void **)&halfspaces, maximum_incidences + 12U,
			sizeof(*halfspaces), error_out))
		goto cleanup;
	for (index = 0U; index < entities.mechanism_count; index++)
	{
		const sg_rune_compact_mechanism_entity_authority_t *source =
			&entities.mechanisms[index];
		int located;

		if (source->source.entity_ordinal >=
				owner_view.entity_semantics->entity_count ||
			owner_view.entity_semantics->entities == NULL)
		{
			SetError(error_out,
				SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, index);
			goto cleanup;
		}
		CopyAuthorityScalars(source, &candidate.mechanisms[index]);
		located = LocateEntity(&geometry_view, &geometry_model,
			&owner_view.entity_semantics->entities[
				source->source.entity_ordinal], halfspaces,
			&candidate.mechanisms[index].activation_cell,
			&candidate.mechanisms[index].activation_witness,
			&candidate.mechanisms[index].activation_bounds);
		if (located <= 0)
		{
			SetError(error_out, located < 0 ?
				SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION :
				SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_CELL, index);
			goto cleanup;
		}
	}
	for (index = 0U; index < entities.controller_count; index++)
	{
		const sg_rune_compact_mechanism_entity_controller_t *source =
			&entities.controllers[index];
		int located;

		if (source->controller.entity_ordinal >=
				owner_view.entity_semantics->entity_count ||
			owner_view.entity_semantics->entities == NULL)
		{
			SetError(error_out,
				SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY, index);
			goto cleanup;
		}
		candidate.controllers[index].mechanism = source->mechanism;
		candidate.controllers[index].controller = source->controller;
		candidate.controllers[index].topology_edge = source->topology_edge;
		candidate.controllers[index].activation = source->activation;
		candidate.controllers[index].damage = source->damage;
		candidate.controllers[index].health = source->health;
		candidate.controllers[index].required_item = source->required_item;
		candidate.controllers[index].flags = source->flags;
		candidate.controllers[index].spatiality = source->spatiality;
		candidate.controllers[index].activation_cell.value =
			SG_RUNE_COMPACT_INDEX_NONE;
		if (source->spatiality ==
			SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL)
			continue;
		located = LocateEntity(&geometry_view, &geometry_model,
			&owner_view.entity_semantics->entities[
				source->controller.entity_ordinal], halfspaces,
			&candidate.controllers[index].activation_cell,
			&candidate.controllers[index].activation_witness,
			&candidate.controllers[index].activation_bounds);
		if (located <= 0)
		{
			SetError(error_out, located < 0 ?
				SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION :
				SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_MECHANISMS_RECORD_CELL, index);
			goto cleanup;
		}
	}
	candidate.mechanism_count = entities.mechanism_count;
	candidate.controller_count = entities.controller_count;
	candidate.topology_edges = entities.topology_edges;
	candidate.topology_edge_count = entities.topology_edge_count;
	entities.topology_edges = NULL;
	entities.topology_edge_count = 0U;
	if (!SG_RuneCompactMechanismTransitionsDerive(builder, geometry,
			candidate.mechanisms, candidate.mechanism_count, &transitions,
			error_out) || !TransitionSpansValid(&transitions,
			candidate.mechanism_count, error_out))
		goto cleanup;
	for (index = 0U; index < candidate.mechanism_count; index++)
		candidate.mechanisms[index].transitions = transitions.spans[index];
	candidate.transitions = transitions.transitions;
	candidate.transition_count = transitions.transition_count;
	transitions.transitions = NULL;
	transitions.transition_count = 0U;
	if (!PopulateTimingAggregates(&candidate, error_out))
		goto cleanup;
	*candidate_out = candidate;
	memset(&candidate, 0, sizeof(candidate));
	success = 1;

cleanup:
	free(halfspaces);
	SG_RuneCompactMechanismTransitionsRelease(&transitions);
	SG_RuneCompactMechanismEntitiesRelease(&entities);
	SG_RuneCompactMechanismsReleaseCandidate(&candidate);
	return success;
}

void SG_RuneCompactMechanismsReleaseCandidate(
	sg_rune_compact_mechanisms_candidate_t *candidate)
{
	if (candidate == NULL)
		return;
	free(candidate->mechanisms);
	free(candidate->controllers);
	free(candidate->topology_edges);
	free(candidate->transitions);
	memset(candidate, 0, sizeof(*candidate));
}
