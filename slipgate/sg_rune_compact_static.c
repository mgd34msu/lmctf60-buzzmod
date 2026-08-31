#include "sg_rune_compact_static.h"

#include <stddef.h>

static void SetError(sg_rune_compact_static_error_t *error,
	sg_rune_compact_static_error_code_t code,
	sg_rune_compact_static_record_domain_t domain, uint32_t record)
{
	if (error == NULL)
		return;
	error->code = code;
	error->domain = domain;
	error->record = record;
}

static int ArrayPresent(const void *values, uint32_t count)
{
	return count == 0U || values != NULL;
}

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int BoundsValid(const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int PointInBounds(const sg_rune_q8_vec3_t *point,
	const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (point->value[axis] < bounds->mins.value[axis] ||
			point->value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int BoundsOverlap(const sg_rune_q8_bounds_t *left,
	const sg_rune_q8_bounds_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (left->maxs.value[axis] <= right->mins.value[axis] ||
			left->mins.value[axis] >= right->maxs.value[axis])
			return 0;
	return 1;
}

static int MechanismKindValid(sg_rune_compact_mechanism_kind_t kind)
{
	return kind >= SG_RUNE_COMPACT_MECHANISM_DOOR &&
		kind < SG_RUNE_COMPACT_MECHANISM_KIND_COUNT;
}

static int LandmarkKindValid(sg_rune_compact_landmark_kind_t kind)
{
	return kind >= SG_RUNE_COMPACT_LANDMARK_SPAWN &&
		kind < SG_RUNE_COMPACT_LANDMARK_KIND_COUNT;
}

static int PortalMechanismKindValid(sg_rune_compact_portal_mechanism_kind_t kind)
{
	return kind >= SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS &&
		kind < SG_RUNE_COMPACT_PORTAL_MECHANISM_KIND_COUNT;
}

static int PortalMechanismMatchesSource(
	sg_rune_compact_portal_mechanism_kind_t binding_kind,
	sg_rune_compact_mechanism_kind_t mechanism_kind)
{
	switch (binding_kind) {
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS:
		return mechanism_kind == SG_RUNE_COMPACT_MECHANISM_DOOR ||
			mechanism_kind == SG_RUNE_COMPACT_MECHANISM_LIFT ||
			mechanism_kind == SG_RUNE_COMPACT_MECHANISM_TRAIN ||
			mechanism_kind == SG_RUNE_COMPACT_MECHANISM_ROTATOR;
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_MOVES:
		return mechanism_kind == SG_RUNE_COMPACT_MECHANISM_LIFT ||
			mechanism_kind == SG_RUNE_COMPACT_MECHANISM_TRAIN ||
			mechanism_kind == SG_RUNE_COMPACT_MECHANISM_ROTATOR;
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_TELEPORTS:
		return mechanism_kind == SG_RUNE_COMPACT_MECHANISM_TELEPORT;
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_LAUNCHES:
		return mechanism_kind == SG_RUNE_COMPACT_MECHANISM_PUSH;
	case SG_RUNE_COMPACT_PORTAL_MECHANISM_KIND_COUNT:
		break;
	}
	return 0;
}

static int EntityRefValid(sg_rune_compact_entity_ref_t source,
	uint32_t entity_count)
{
	return source.entity_ordinal < entity_count;
}

static int MechanismCompare(const sg_rune_compact_mechanism_t *left,
	const sg_rune_compact_mechanism_t *right)
{
	int comparison = CompareU32(left->source.entity_ordinal,
		right->source.entity_ordinal);

	if (comparison == 0)
		comparison = CompareU32(left->controller.entity_ordinal,
			right->controller.entity_ordinal);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	if (comparison == 0)
		comparison = CompareU32(left->entry_cell.value,
			right->entry_cell.value);
	if (comparison == 0)
		comparison = CompareU32(left->exit_cell.value, right->exit_cell.value);
	return comparison;
}

static int MechanismEdgeCompare(const sg_rune_compact_mechanism_edge_t *left,
	const sg_rune_compact_mechanism_edge_t *right)
{
	int comparison = CompareU32(left->source.entity_ordinal,
		right->source.entity_ordinal);

	if (comparison == 0)
		comparison = CompareU32(left->destination.entity_ordinal,
			right->destination.entity_ordinal);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	if (comparison == 0)
		comparison = CompareU32(left->fanout_ordinal, right->fanout_ordinal);
	return comparison;
}

static int LandmarkCompare(const sg_rune_compact_landmark_t *left,
	const sg_rune_compact_landmark_t *right)
{
	int comparison = CompareU32(left->source.entity_ordinal,
		right->source.entity_ordinal);

	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->variant,
			(uint32_t)right->variant);
	return comparison;
}

static int PortalMechanismCompare(
	const sg_rune_compact_portal_mechanism_t *left,
	const sg_rune_compact_portal_mechanism_t *right)
{
	int comparison = CompareU32(left->portal.value, right->portal.value);

	if (comparison == 0)
		comparison = CompareU32(left->mechanism.value, right->mechanism.value);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind,
			(uint32_t)right->kind);
	return comparison;
}

static int ModelReferencesPresent(const sg_rune_compact_model_t *model)
{
	return model != NULL && model->cell_count != 0U && model->cells != NULL &&
		(model->facet_count == 0U || model->facets != NULL) &&
		(model->portal_count == 0U || model->portals != NULL) &&
		(model->incidence_count == 0U || model->incidences != NULL);
}

static int CountsValid(const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	if (static_data->mechanism_count > SG_RUNE_COMPACT_MAX_MECHANISMS ||
		static_data->mechanism_edge_count >
			SG_RUNE_COMPACT_MAX_MECHANISM_EDGES ||
		static_data->landmark_count > SG_RUNE_COMPACT_MAX_LANDMARKS ||
		static_data->landmark_cell_count >
			SG_RUNE_COMPACT_MAX_LANDMARK_CELL_REFS ||
		static_data->facet_annotation_count >
			SG_RUNE_COMPACT_MAX_FACET_ANNOTATIONS ||
		static_data->portal_mechanism_count >
			SG_RUNE_COMPACT_MAX_PORTAL_MECHANISMS) {
		SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_LIMIT_EXCEEDED,
			SG_RUNE_COMPACT_STATIC_RECORD_MODEL, 0U);
		return 0;
	}
	if (!ArrayPresent(static_data->mechanisms,
			static_data->mechanism_count) ||
		!ArrayPresent(static_data->mechanism_edges,
			static_data->mechanism_edge_count) ||
		!ArrayPresent(static_data->landmarks, static_data->landmark_count) ||
		!ArrayPresent(static_data->landmark_cells,
			static_data->landmark_cell_count) ||
		!ArrayPresent(static_data->facet_annotations,
			static_data->facet_annotation_count) ||
		!ArrayPresent(static_data->portal_mechanisms,
			static_data->portal_mechanism_count)) {
		SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

static int ValidateMechanisms(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t index;
	uint32_t edge_cursor = 0U;

	for (index = 0U; index < static_data->mechanism_count; index++) {
		const sg_rune_compact_mechanism_t *mechanism =
			&static_data->mechanisms[index];

		if (mechanism->reserved[0] != 0U || mechanism->reserved[1] != 0U ||
			mechanism->reserved[2] != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		if (!EntityRefValid(mechanism->source,
				model->identity.source_counts.entity_count) ||
			!EntityRefValid(mechanism->controller,
				model->identity.source_counts.entity_count) ||
			mechanism->entry_cell.value >= model->cell_count ||
			mechanism->exit_cell.value >= model->cell_count ||
			(mechanism->activation_landmark.value != SG_RUNE_COMPACT_INDEX_NONE &&
			 mechanism->activation_landmark.value >= static_data->landmark_count) ||
			!MechanismKindValid(mechanism->kind) ||
			(uint32_t)mechanism->activation >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_ACTIVATION_COUNT ||
			(uint32_t)mechanism->initial_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			(uint32_t)mechanism->activated_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			(uint32_t)mechanism->reset_state >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			(uint32_t)mechanism->recovery >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_RECOVERY_COUNT ||
			(mechanism->activation !=
				SG_RUNE_COMPACT_MECHANISM_ACTIVATION_AUTOMATIC &&
			 mechanism->activation_landmark.value ==
				SG_RUNE_COMPACT_INDEX_NONE) ||
			(mechanism->activation ==
				SG_RUNE_COMPACT_MECHANISM_ACTIVATION_AUTOMATIC &&
			 mechanism->activation_landmark.value !=
				SG_RUNE_COMPACT_INDEX_NONE) ||
			(mechanism->flags & (sg_rune_compact_mechanism_flags_t)
				~SG_RUNE_COMPACT_MECHANISM_FLAGS_KNOWN) != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		if (mechanism->topology.first != edge_cursor ||
			mechanism->topology.first > static_data->mechanism_edge_count ||
			mechanism->topology.count >
				static_data->mechanism_edge_count - mechanism->topology.first) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		if ((mechanism->activation ==
				SG_RUNE_COMPACT_MECHANISM_ACTIVATION_DWELL) !=
			(mechanism->dwell_ms != 0U) ||
			(mechanism->recovery ==
				SG_RUNE_COMPACT_MECHANISM_RECOVERY_WAIT_FOR_RESET) !=
			(mechanism->reset_ms != 0U) ||
			((mechanism->flags & SG_RUNE_COMPACT_MECHANISM_ONE_SHOT) != 0U &&
			 (mechanism->reset_ms != 0U ||
			  mechanism->recovery != SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE))) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		if (!BoundsValid(&mechanism->bounds)) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		if (index != 0U && MechanismCompare(&static_data->mechanisms[index - 1U],
			mechanism) >= 0) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM, index);
			return 0;
		}
		edge_cursor += mechanism->topology.count;
	}
	if (edge_cursor != static_data->mechanism_edge_count) {
		SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_EDGE, edge_cursor);
		return 0;
	}
	return 1;
}

static int ValidateMechanismEdges(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t mechanism_index;

	for (mechanism_index = 0U; mechanism_index < static_data->mechanism_count;
		mechanism_index++) {
		const sg_rune_compact_mechanism_edge_span_t span =
			static_data->mechanisms[mechanism_index].topology;
		uint32_t index;

		for (index = span.first; index < span.first + span.count; index++) {
			const sg_rune_compact_mechanism_edge_t *edge =
				&static_data->mechanism_edges[index];

			if (!EntityRefValid(edge->source,
					model->identity.source_counts.entity_count) ||
				!EntityRefValid(edge->destination,
					model->identity.source_counts.entity_count) ||
				(uint32_t)edge->kind >=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_EDGE_KIND_COUNT) {
				SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
					SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_EDGE, index);
				return 0;
			}
			if (index != span.first && MechanismEdgeCompare(
					&static_data->mechanism_edges[index - 1U], edge) >= 0) {
				SetError(error,
					SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
					SG_RUNE_COMPACT_STATIC_RECORD_MECHANISM_EDGE, index);
				return 0;
			}
		}
	}
	return 1;
}

static int LandmarkMechanismValid(const sg_rune_compact_landmark_t *landmark,
	const sg_rune_compact_static_t *static_data)
{
	const sg_rune_compact_mechanism_t *mechanism;

	if (landmark->mechanism.value == SG_RUNE_COMPACT_INDEX_NONE)
		return landmark->kind != SG_RUNE_COMPACT_LANDMARK_BUTTON &&
			landmark->kind != SG_RUNE_COMPACT_LANDMARK_TRIGGER &&
			landmark->kind != SG_RUNE_COMPACT_LANDMARK_MECHANISM_ENTRY;
	if (landmark->mechanism.value >= static_data->mechanism_count)
		return 0;
	mechanism = &static_data->mechanisms[landmark->mechanism.value];
	if (landmark->kind == SG_RUNE_COMPACT_LANDMARK_BUTTON)
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_BUTTON &&
			landmark->source.entity_ordinal == mechanism->source.entity_ordinal;
	if (landmark->kind == SG_RUNE_COMPACT_LANDMARK_TRIGGER)
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_TRIGGER &&
			landmark->source.entity_ordinal == mechanism->source.entity_ordinal;
	if (landmark->kind == SG_RUNE_COMPACT_LANDMARK_TELEPORTER_DESTINATION)
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_TELEPORT;
	if (landmark->kind == SG_RUNE_COMPACT_LANDMARK_JUMPPAD_LANDING)
		return mechanism->kind == SG_RUNE_COMPACT_MECHANISM_PUSH;
	return 1;
}

static int ValidateLandmarks(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t index;
	uint32_t cell_cursor = 0U;

	for (index = 0U; index < static_data->landmark_count; index++) {
		const sg_rune_compact_landmark_t *landmark =
			&static_data->landmarks[index];

		if (landmark->reserved != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
			return 0;
		}
		if (!EntityRefValid(landmark->source,
				model->identity.source_counts.entity_count) ||
			landmark->cells.first != cell_cursor || landmark->cells.count == 0U ||
			landmark->cells.first > static_data->landmark_cell_count ||
			landmark->cells.count >
				static_data->landmark_cell_count - landmark->cells.first ||
			!LandmarkKindValid(landmark->kind) ||
			!LandmarkMechanismValid(landmark, static_data)) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
			return 0;
		}
		if (!BoundsValid(&landmark->bounds) ||
			!PointInBounds(&landmark->origin, &landmark->bounds)) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
			return 0;
		}
		{
			uint32_t offset;
			int origin_owned = 0;

			for (offset = 0U; offset < landmark->cells.count; offset++) {
				const uint32_t reference = landmark->cells.first + offset;
				const uint32_t cell = static_data->landmark_cells[reference].value;

				if (cell >= model->cell_count ||
					(offset != 0U && static_data->landmark_cells[
						reference - 1U].value >= cell) ||
					!BoundsOverlap(&landmark->bounds, &model->cells[cell].bounds)) {
					SetError(error,
						SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
					return 0;
				}
				if (PointInBounds(&landmark->origin, &model->cells[cell].bounds))
					origin_owned = 1;
			}
			if (!origin_owned) {
				SetError(error,
					SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
				return 0;
			}
		}
		if (index != 0U && LandmarkCompare(&static_data->landmarks[index - 1U],
			landmark) >= 0) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, index);
			return 0;
		}
		cell_cursor += landmark->cells.count;
	}
	if (cell_cursor != static_data->landmark_cell_count) {
		SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_STATIC_RECORD_LANDMARK, cell_cursor);
		return 0;
	}
	return 1;
}

static int ValidateFacetAnnotations(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t index;

	for (index = 0U; index < static_data->facet_annotation_count; index++) {
		const sg_rune_compact_facet_annotation_t *annotation =
			&static_data->facet_annotations[index];

		if (annotation->reserved != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
		if (annotation->facet.value >= model->facet_count ||
			annotation->attributes == 0U ||
			(annotation->attributes & (sg_rune_compact_facet_attributes_t)
				~SG_RUNE_COMPACT_FACET_ATTRIBUTES_KNOWN) != 0U ||
			(annotation->hookable_stances &
				(sg_rune_stance_validity_t)~SG_RUNE_STANCE_VALID_ALL) != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
		if ((annotation->attributes & SG_RUNE_COMPACT_FACET_HOOKABLE) == 0U &&
			annotation->hookable_stances != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
		if ((annotation->attributes & SG_RUNE_COMPACT_FACET_HOOKABLE) != 0U &&
			annotation->hookable_stances == 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
		if ((annotation->attributes & (SG_RUNE_COMPACT_FACET_HOOKABLE |
			SG_RUNE_COMPACT_FACET_SKY)) ==
			(SG_RUNE_COMPACT_FACET_HOOKABLE | SG_RUNE_COMPACT_FACET_SKY)) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
		if (index != 0U && static_data->facet_annotations[index - 1U].facet.value >=
			annotation->facet.value) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_STATIC_RECORD_FACET_ANNOTATION, index);
			return 0;
		}
	}
	return 1;
}

static int ValidatePortalMechanisms(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error)
{
	uint32_t index;

	for (index = 0U; index < static_data->portal_mechanism_count; index++) {
		const sg_rune_compact_portal_mechanism_t *portal_mechanism =
			&static_data->portal_mechanisms[index];

		if (portal_mechanism->reserved[0] != 0U ||
			portal_mechanism->reserved[1] != 0U ||
			portal_mechanism->reserved[2] != 0U) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM, index);
			return 0;
		}
		if (portal_mechanism->portal.value >= model->portal_count ||
			portal_mechanism->mechanism.value >= static_data->mechanism_count ||
			!PortalMechanismKindValid(portal_mechanism->kind) ||
			!PortalMechanismMatchesSource(portal_mechanism->kind,
				static_data->mechanisms[portal_mechanism->mechanism.value].kind)) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM, index);
			return 0;
		}
		if (index != 0U && PortalMechanismCompare(
				&static_data->portal_mechanisms[index - 1U],
				portal_mechanism) >= 0) {
			SetError(error, SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_STATIC_RECORD_PORTAL_MECHANISM, index);
			return 0;
		}
	}
	return 1;
}

int SG_RuneCompactStaticValidate(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_static_error_t *error_out)
{
	if (error_out != NULL) {
		error_out->code = SG_RUNE_COMPACT_STATIC_ERROR_NONE;
		error_out->domain = SG_RUNE_COMPACT_STATIC_RECORD_MODEL;
		error_out->record = 0U;
	}
	if (!ModelReferencesPresent(model) || static_data == NULL) {
		SetError(error_out, SG_RUNE_COMPACT_STATIC_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_STATIC_RECORD_MODEL, 0U);
		return 0;
	}
	if (!CountsValid(static_data, error_out) ||
		!ValidateMechanisms(model, static_data, error_out) ||
		!ValidateMechanismEdges(model, static_data, error_out) ||
		!ValidateLandmarks(model, static_data, error_out) ||
		!ValidateFacetAnnotations(model, static_data, error_out) ||
		!ValidatePortalMechanisms(model, static_data, error_out))
		return 0;
	return 1;
}

const char *SG_RuneCompactStaticErrorString(
	sg_rune_compact_static_error_code_t code)
{
	switch (code) {
	case SG_RUNE_COMPACT_STATIC_ERROR_NONE:
		return "none";
	case SG_RUNE_COMPACT_STATIC_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_STATIC_ERROR_LIMIT_EXCEEDED:
		return "limit exceeded";
	case SG_RUNE_COMPACT_STATIC_ERROR_NONZERO_RESERVED:
		return "nonzero reserved field";
	case SG_RUNE_COMPACT_STATIC_ERROR_NONCANONICAL_ORDER:
		return "noncanonical order";
	case SG_RUNE_COMPACT_STATIC_ERROR_INVALID_REFERENCE:
		return "invalid reference";
	case SG_RUNE_COMPACT_STATIC_ERROR_INVALID_SEMANTICS:
		return "invalid static semantics";
	case SG_RUNE_COMPACT_STATIC_ERROR_CODE_COUNT:
		break;
	}
	return "unknown compact static error";
}
