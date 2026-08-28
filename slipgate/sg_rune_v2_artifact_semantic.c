#include "sg_rune_v2_artifact_semantic.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define CELL_SEMANTICS_KNOWN \
	(SG_RUNE_CELL_SEMANTIC_HAZARD | SG_RUNE_CELL_SEMANTIC_SKY_BOUNDARY | \
	 SG_RUNE_CELL_SEMANTIC_VOID_BOUNDARY | SG_RUNE_CELL_SEMANTIC_MOVER_VOLUME)
#define PORTAL_FLAGS_KNOWN \
	(SG_RUNE_PORTAL_HULL_VALID | SG_RUNE_PORTAL_CONTENTS_CHANGE | \
	 SG_RUNE_PORTAL_VOID_EDGE | SG_RUNE_PORTAL_MOVER_BOUNDARY)

static sg_rune_v2_semantic_diagnostic_t Report(
	sg_rune_v2_semantic_report_t *report,
	sg_rune_v2_semantic_diagnostic_t diagnostic,
	sg_rune_v2_wire_diagnostic_t wire_diagnostic,
	sg_rune_failure_reason_t model_failure, uint16_t section, uint32_t record)
{
	if (report)
	{
		report->diagnostic = diagnostic;
		report->wire_diagnostic = wire_diagnostic;
		report->model_failure = model_failure;
		report->section = section;
		report->record = record;
	}
	return diagnostic;
}

static int BindingValid(const sg_rune_v2_wire_binding_t *binding)
{
	return binding && binding->generation != 0U &&
		SG_RuneV2ContentIdValid(&binding->bsp_identity) &&
		SG_RuneV2ContentIdValid(&binding->schema_identity);
}

static int BindingEqual(const sg_rune_v2_wire_binding_t *left,
	const sg_rune_v2_wire_binding_t *right)
{
	return left && right && left->generation == right->generation &&
		SG_RuneV2ContentIdEqual(&left->bsp_identity, &right->bsp_identity) &&
		SG_RuneV2ContentIdEqual(&left->schema_identity, &right->schema_identity);
}

static int FloatEqual(float left, float right)
{
	uint32_t left_bits;
	uint32_t right_bits;

	memcpy(&left_bits, &left, sizeof(left_bits));
	memcpy(&right_bits, &right, sizeof(right_bits));
	return left_bits == right_bits;
}

static int VecEqual(const sg_rune_vec3_t *left, const sg_rune_vec3_t *right)
{
	return left && right &&
		FloatEqual(left->value[0], right->value[0]) &&
		FloatEqual(left->value[1], right->value[1]) &&
		FloatEqual(left->value[2], right->value[2]);
}

static int BoundsEqual(const sg_rune_bounds_t *left,
	const sg_rune_bounds_t *right)
{
	return left && right && VecEqual(&left->mins, &right->mins) &&
		VecEqual(&left->maxs, &right->maxs);
}

static int HullEqual(const sg_rune_hull_profile_t *left,
	const sg_rune_hull_profile_t *right)
{
	return left && right && VecEqual(&left->mins, &right->mins) &&
		VecEqual(&left->maxs, &right->maxs);
}

static int IntervalEqual(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	return left && right && FloatEqual(left->min_value, right->min_value) &&
		FloatEqual(left->max_value, right->max_value);
}

static int Interval3Equal(const sg_rune_interval3_t *left,
	const sg_rune_interval3_t *right)
{
	return left && right && IntervalEqual(&left->x, &right->x) &&
		IntervalEqual(&left->y, &right->y) &&
		IntervalEqual(&left->z, &right->z);
}

static int StableIdEqual(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	return SG_RuneModelStableIdEqual(left, right);
}

static int StableIdCompare(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	if (left->source_set_identity != right->source_set_identity)
		return left->source_set_identity < right->source_set_identity ? -1 : 1;
	if (left->high != right->high)
		return left->high < right->high ? -1 : 1;
	if (left->low != right->low)
		return left->low < right->low ? -1 : 1;
	return 0;
}

static int OrderEqual(const sg_rune_order_key_t *left,
	const sg_rune_order_key_t *right)
{
	return left && right &&
		left->source_set_identity == right->source_set_identity &&
		left->domain == right->domain &&
		left->source_index == right->source_index &&
		left->local_ordinal == right->local_ordinal &&
		left->variant == right->variant;
}

static int GeometryEqual(const sg_rune_source_geometry_ref_t *left,
	const sg_rune_source_geometry_ref_t *right)
{
	return left && right &&
		left->source_set_identity == right->source_set_identity &&
		left->source_index == right->source_index &&
		left->source_ordinal == right->source_ordinal;
}

static int SpanEqual(uint32_t left_first, uint32_t left_count,
	uint32_t right_first, uint32_t right_count)
{
	return left_first == right_first && left_count == right_count;
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	const sg_rune_physics_parameters_t *lp;
	const sg_rune_physics_parameters_t *rp;

	if (!left || !right || left->bsp_content_id != right->bsp_content_id ||
		left->entity_semantics_id != right->entity_semantics_id ||
		left->physics_abi_id != right->physics_abi_id ||
		left->source_set_identity != right->source_set_identity ||
		left->schema_id != right->schema_id ||
		left->producer_identity != right->producer_identity ||
		!HullEqual(&left->standing_hull, &right->standing_hull) ||
		!HullEqual(&left->crouching_hull, &right->crouching_hull))
		return 0;
	lp = &left->physics;
	rp = &right->physics;
	return FloatEqual(lp->gravity, rp->gravity) &&
		FloatEqual(lp->ground_acceleration, rp->ground_acceleration) &&
		FloatEqual(lp->air_acceleration, rp->air_acceleration) &&
		FloatEqual(lp->water_acceleration, rp->water_acceleration) &&
		FloatEqual(lp->hook_acceleration, rp->hook_acceleration) &&
		FloatEqual(lp->external_acceleration, rp->external_acceleration) &&
		FloatEqual(lp->water_drag, rp->water_drag) &&
		FloatEqual(lp->max_velocity, rp->max_velocity) &&
		lp->frame_ms == rp->frame_ms && lp->substep_ms == rp->substep_ms;
}

static int SpanWithin(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int ContentsValid(sg_rune_contents_mask_t contents)
{
	return (contents & ~(sg_rune_contents_mask_t)SG_RUNE_CONTENTS_KNOWN) == 0U;
}

static int RecordIdentityValid(const sg_rune_stable_id_t *id,
	const sg_rune_order_key_t *order, uint32_t domain,
	uint64_t source_set_identity, const sg_rune_order_key_t *previous)
{
	sg_rune_stable_id_t expected;

	if (!SG_RuneModelStableIdValid(id) || !SG_RuneModelOrderKeyValid(order) ||
		order->domain != domain ||
		order->source_set_identity != source_set_identity)
		return 0;
	expected = SG_RuneModelStableIdFromOrderKey(order);
	return StableIdEqual(id, &expected) &&
		(!previous || SG_RuneModelOrderKeyCompare(previous, order) < 0);
}

static int FindExpectedCell(const sg_rune_v2_semantic_catalog_view_t *catalog,
	const sg_rune_stable_id_t *id)
{
	uint32_t first = 0U;
	uint32_t last = catalog->counts.cells;

	while (first < last)
	{
		uint32_t middle = first + (last - first) / 2U;
		int comparison = StableIdCompare(&catalog->cells[middle].id.value, id);

		if (comparison == 0)
			return 1;
		if (comparison < 0)
			first = middle + 1U;
		else
			last = middle;
	}
	return 0;
}

static int FindExpectedPortal(const sg_rune_v2_semantic_catalog_view_t *catalog,
	const sg_rune_stable_id_t *id)
{
	uint32_t first = 0U;
	uint32_t last = catalog->counts.portals;

	while (first < last)
	{
		uint32_t middle = first + (last - first) / 2U;
		int comparison = StableIdCompare(&catalog->portals[middle].id.value, id);

		if (comparison == 0)
			return 1;
		if (comparison < 0)
			first = middle + 1U;
		else
			last = middle;
	}
	return 0;
}

static int CountsValid(const sg_rune_v2_semantic_catalog_view_t *catalog)
{
	const sg_rune_v2_expected_counts_t *counts = &catalog->counts;

	return counts->cells > 0U && counts->cells <= SG_RUNE_MODEL_MAX_CELLS &&
		counts->planes <= SG_RUNE_MODEL_MAX_PLANES &&
		counts->portal_vertices <= SG_RUNE_MODEL_MAX_PORTAL_VERTICES &&
		counts->phases <= SG_RUNE_MODEL_MAX_PHASES &&
		counts->phase_transitions <= SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS &&
		counts->portals <= SG_RUNE_MODEL_MAX_PORTALS &&
		counts->surfaces <= SG_RUNE_MODEL_MAX_SURFACES &&
		counts->affordances <= SG_RUNE_MODEL_MAX_AFFORDANCES &&
		counts->kernels <= SG_RUNE_MODEL_MAX_KERNELS &&
		counts->landmarks <= SG_RUNE_MODEL_MAX_LANDMARKS &&
		counts->mechanisms <= SG_RUNE_MODEL_MAX_MECHANISMS &&
		(counts->planes == 0U || catalog->planes) &&
		(counts->portal_vertices == 0U || catalog->portal_vertices) &&
		(counts->phases == 0U || catalog->phases) && catalog->cells &&
		(counts->portals == 0U || catalog->portals) &&
		(counts->phase_transitions == 0U || catalog->phase_transitions) &&
		(counts->surfaces == 0U || catalog->surfaces) &&
		(counts->affordances == 0U || catalog->affordances) &&
		(counts->kernels == 0U || catalog->kernels) &&
		(counts->landmarks == 0U || catalog->landmarks) &&
		(counts->mechanisms == 0U || catalog->mechanisms);
}

static int CompleteModelProofValid(const sg_rune_v2_semantic_catalog_view_t *catalog)
{
	const sg_rune_v2_complete_model_proof_t *proof = &catalog->complete_model_proof;

	return proof->version == SG_RUNE_VALIDATION_EVIDENCE_VERSION &&
		proof->reserved == 0U && proof->verifier_identity != 0U &&
		proof->verifier_identity != catalog->identity.producer_identity &&
		proof->bsp_content_id == catalog->identity.bsp_content_id &&
		proof->source_set_identity == catalog->identity.source_set_identity &&
		proof->fixed_point_identity != 0U && proof->fixed_point_rounds != 0U &&
		proof->expected_cells == catalog->counts.cells &&
		proof->represented_cells == catalog->counts.cells &&
		proof->expected_portals == catalog->counts.portals &&
		proof->represented_portals == catalog->counts.portals &&
		proof->omitted_cells == 0U && proof->omitted_portals == 0U &&
		proof->invented_portals == 0U && proof->invalid_portals == 0U &&
		proof->pending_work == 0U;
}

static int CatalogSupportingIdentitiesValid(
	const sg_rune_v2_semantic_catalog_view_t *catalog)
{
	const sg_rune_order_key_t *previous = NULL;
	uint32_t index;

	for (index = 0U; index < catalog->counts.planes; index++)
	{
		const sg_rune_v2_expected_plane_t *plane = &catalog->planes[index];

		if (!RecordIdentityValid(&plane->id.value, &plane->order,
			SG_RUNE_ORDER_PLANE, catalog->identity.source_set_identity,
			previous) || !isfinite(plane->normal.value[0]) ||
			!isfinite(plane->normal.value[1]) ||
			!isfinite(plane->normal.value[2]) || !isfinite(plane->distance))
			return 0;
		previous = &plane->order;
	}
	for (index = 0U; index < catalog->counts.portal_vertices; index++)
		if (!isfinite(catalog->portal_vertices[index].x) ||
			!isfinite(catalog->portal_vertices[index].y) ||
			!isfinite(catalog->portal_vertices[index].z))
			return 0;
	previous = NULL;
	for (index = 0U; index < catalog->counts.phases; index++)
	{
		const sg_rune_v2_expected_phase_t *phase = &catalog->phases[index];

		if (!RecordIdentityValid(&phase->id.value, &phase->order,
			SG_RUNE_ORDER_PHASE, catalog->identity.source_set_identity, previous))
			return 0;
		previous = &phase->order;
	}
	previous = NULL;
	for (index = 0U; index < catalog->counts.surfaces; index++)
	{
		const sg_rune_v2_expected_surface_t *surface = &catalog->surfaces[index];

		if (!RecordIdentityValid(&surface->id.value, &surface->order,
			SG_RUNE_ORDER_SURFACE, catalog->identity.source_set_identity, previous))
			return 0;
		previous = &surface->order;
	}
	previous = NULL;
	for (index = 0U; index < catalog->counts.affordances; index++)
	{
		const sg_rune_v2_expected_affordance_t *affordance =
			&catalog->affordances[index];

		if (!RecordIdentityValid(&affordance->id.value, &affordance->order,
			SG_RUNE_ORDER_AFFORDANCE, catalog->identity.source_set_identity,
			previous))
			return 0;
		previous = &affordance->order;
	}
	previous = NULL;
	for (index = 0U; index < catalog->counts.landmarks; index++)
	{
		const sg_rune_v2_expected_landmark_t *landmark =
			&catalog->landmarks[index];

		if (!RecordIdentityValid(&landmark->id.value, &landmark->order,
			SG_RUNE_ORDER_LANDMARK, catalog->identity.source_set_identity,
			previous))
			return 0;
		previous = &landmark->order;
	}
	previous = NULL;
	for (index = 0U; index < catalog->counts.mechanisms; index++)
	{
		const sg_rune_v2_expected_mechanism_t *mechanism =
			&catalog->mechanisms[index];

		if (!RecordIdentityValid(&mechanism->id.value, &mechanism->order,
			SG_RUNE_ORDER_MECHANISM, catalog->identity.source_set_identity,
			previous))
			return 0;
		previous = &mechanism->order;
	}
	return 1;
}

static int CatalogCellsValid(const sg_rune_v2_semantic_catalog_view_t *catalog)
{
	const sg_rune_order_key_t *previous = NULL;
	uint32_t index;

	for (index = 0U; index < catalog->counts.cells; index++)
	{
		const sg_rune_v2_expected_cell_t *cell = &catalog->cells[index];
		int axis;

		if (!RecordIdentityValid(&cell->id.value, &cell->order,
			SG_RUNE_ORDER_CELL, catalog->identity.source_set_identity, previous) ||
			cell->geometry.source_set_identity !=
				catalog->identity.source_set_identity ||
			cell->geometry.source_index == UINT32_MAX ||
			cell->geometry.source_ordinal == UINT32_MAX ||
			cell->boundary_planes.count < 4U ||
			!SpanWithin(cell->boundary_planes.first,
				cell->boundary_planes.count, catalog->counts.planes) ||
			cell->phases.count == 0U ||
			!SpanWithin(cell->phases.first, cell->phases.count,
				catalog->counts.phases) ||
			!SpanWithin(cell->surfaces.first, cell->surfaces.count,
				catalog->counts.surfaces) ||
			!SpanWithin(cell->affordances.first, cell->affordances.count,
				catalog->counts.affordances) ||
			!SpanWithin(cell->kernels.first, cell->kernels.count,
				catalog->counts.kernels) ||
			!SpanWithin(cell->landmarks.first, cell->landmarks.count,
				catalog->counts.landmarks) ||
			!SpanWithin(cell->mechanisms.first, cell->mechanisms.count,
				catalog->counts.mechanisms) ||
			cell->bsp_leaf.index == UINT32_MAX ||
			cell->bsp_area.index == UINT32_MAX ||
			cell->bsp_cluster.index == UINT32_MAX ||
			!ContentsValid(cell->contents) ||
			(cell->semantics &
				~(sg_rune_cell_semantics_t)CELL_SEMANTICS_KNOWN) != 0U)
			return 0;
		for (axis = 0; axis < 3; axis++)
			if (!isfinite(cell->bounds.mins.value[axis]) ||
				!isfinite(cell->bounds.maxs.value[axis]) ||
				cell->bounds.mins.value[axis] >= cell->bounds.maxs.value[axis])
				return 0;
		previous = &cell->order;
	}
	return 1;
}

static int CatalogPortalsValid(const sg_rune_v2_semantic_catalog_view_t *catalog)
{
	const sg_rune_order_key_t *previous = NULL;
	uint32_t index;

	for (index = 0U; index < catalog->counts.portals; index++)
	{
		const sg_rune_v2_expected_portal_t *portal = &catalog->portals[index];

		if (!RecordIdentityValid(&portal->id.value, &portal->order,
			SG_RUNE_ORDER_PORTAL, catalog->identity.source_set_identity,
			previous) || portal->geometry.source_set_identity !=
				catalog->identity.source_set_identity ||
			portal->geometry.source_index == UINT32_MAX ||
			portal->geometry.source_ordinal == UINT32_MAX ||
			!FindExpectedCell(catalog, &portal->from_cell.value) ||
			!FindExpectedCell(catalog, &portal->to_cell.value) ||
			StableIdEqual(&portal->from_cell.value, &portal->to_cell.value) ||
			portal->boundary_vertices.count < 3U ||
			!SpanWithin(portal->boundary_vertices.first,
				portal->boundary_vertices.count, catalog->counts.portal_vertices) ||
			portal->phases.count == 0U ||
			!SpanWithin(portal->phases.first, portal->phases.count,
				catalog->counts.phases) ||
			portal->direction < 0 ||
			portal->direction >= SG_RUNE_PORTAL_DIRECTION_COUNT ||
			!isfinite(portal->clearance) || portal->clearance < 0.0f ||
			(portal->flags & SG_RUNE_PORTAL_HULL_VALID) == 0U ||
			(portal->flags & ~(sg_rune_portal_flags_t)PORTAL_FLAGS_KNOWN) != 0U ||
			!ContentsValid(portal->contents_from) ||
			!ContentsValid(portal->contents_to))
			return 0;
		previous = &portal->order;
	}
	return 1;
}

static int CatalogRelationsValid(const sg_rune_v2_semantic_catalog_view_t *catalog)
{
	const sg_rune_order_key_t *previous = NULL;
	uint32_t index;

	for (index = 0U; index < catalog->counts.phase_transitions; index++)
	{
		const sg_rune_v2_expected_transition_t *transition =
			&catalog->phase_transitions[index];

		if (!RecordIdentityValid(&transition->id.value, &transition->order,
			SG_RUNE_ORDER_PHASE_TRANSITION,
			catalog->identity.source_set_identity, previous) ||
			!FindExpectedCell(catalog, &transition->cell.value))
			return 0;
		previous = &transition->order;
	}
	previous = NULL;
	for (index = 0U; index < catalog->counts.kernels; index++)
	{
		const sg_rune_v2_expected_kernel_t *kernel = &catalog->kernels[index];
		int same_cell;

		if (!RecordIdentityValid(&kernel->id.value, &kernel->order,
			SG_RUNE_ORDER_KERNEL, catalog->identity.source_set_identity,
			previous) || !FindExpectedCell(catalog, &kernel->source_cell.value) ||
			!FindExpectedCell(catalog, &kernel->destination_cell.value))
			return 0;
		same_cell = StableIdEqual(&kernel->source_cell.value,
			&kernel->destination_cell.value);
		if ((!same_cell && !FindExpectedPortal(catalog,
			&kernel->boundary.value)) || kernel->family < 0 ||
			kernel->family >= SG_RUNE_CAPABILITY_FAMILY_COUNT ||
			kernel->cost_law < 0 || kernel->cost_law >= SG_RUNE_COST_LAW_COUNT)
			return 0;
		previous = &kernel->order;
	}
	return 1;
}

static sg_rune_v2_semantic_diagnostic_t CatalogCheck(
	const sg_rune_v2_semantic_catalog_view_t *catalog,
	sg_rune_v2_semantic_report_t *report)
{
	if (!catalog || catalog->version != SG_RUNE_V2_SEMANTIC_CATALOG_VERSION ||
		catalog->reserved != 0U || !BindingValid(&catalog->binding) ||
		catalog->identity.bsp_content_id == 0U ||
		catalog->identity.source_set_identity == 0U ||
		catalog->identity.source_set_identity == UINT64_MAX ||
		catalog->identity.producer_identity == 0U || !CountsValid(catalog) ||
		!CompleteModelProofValid(catalog) || !CatalogSupportingIdentitiesValid(catalog) ||
		!CatalogCellsValid(catalog) ||
		!CatalogPortalsValid(catalog) || !CatalogRelationsValid(catalog))
		return Report(report, SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED,
			SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE, 0U, 0U);
	return SG_RUNE_V2_SEMANTIC_OK;
}

static int RangeOverlap(const void *left, size_t left_count,
	size_t left_size, const void *right, size_t right_count,
	size_t right_size)
{
	uintptr_t left_begin;
	uintptr_t left_end;
	uintptr_t left_width;
	uintptr_t right_begin;
	uintptr_t right_end;
	uintptr_t right_width;
	size_t left_bytes;
	size_t right_bytes;

	if (!left || !right || left_count == 0U || right_count == 0U)
		return 0;
	if (left_size > SIZE_MAX / left_count ||
		right_size > SIZE_MAX / right_count)
		return 1;
	left_bytes = left_size * left_count;
	right_bytes = right_size * right_count;
	if ((sizeof(size_t) > sizeof(uintptr_t) &&
		left_bytes > (size_t)UINTPTR_MAX) ||
		(sizeof(size_t) > sizeof(uintptr_t) &&
		right_bytes > (size_t)UINTPTR_MAX))
		return 1;
	left_width = (uintptr_t)left_bytes;
	right_width = (uintptr_t)right_bytes;
	left_begin = (uintptr_t)left;
	right_begin = (uintptr_t)right;
	if (left_begin > UINTPTR_MAX - left_width ||
		right_begin > UINTPTR_MAX - right_width)
		return 1;
	left_end = left_begin + left_width;
	right_end = right_begin + right_width;
	return left_begin < right_end && right_begin < left_end;
}

#define REPORT_OVERLAPS_MEMBER(storage, report, member, capacity) \
	RangeOverlap((report), 1U, sizeof(*(report)), (storage)->member, \
		(storage)->capacity, sizeof((storage)->member[0]))

static int ReportOverlapsStorage(const sg_rune_v2_semantic_report_t *report,
	const sg_rune_v2_codec_storage_t *storage)
{
	return report && storage &&
		(RangeOverlap(report, 1U, sizeof(*report), storage, 1U,
			sizeof(*storage)) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, planes, plane_capacity) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, portal_vertices,
			portal_vertex_capacity) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, phases, phase_capacity) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, phase_transitions,
			phase_transition_capacity) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, cells, cell_capacity) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, portals, portal_capacity) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, surfaces, surface_capacity) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, affordances,
			affordance_capacity) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, kernels, kernel_capacity) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, landmarks,
			landmark_capacity) ||
		 REPORT_OVERLAPS_MEMBER(storage, report, mechanisms,
			mechanism_capacity));
}

#undef REPORT_OVERLAPS_MEMBER

typedef struct provider_range_s
{
	const void *pointer;
	size_t count;
	size_t element_size;
} provider_range_t;

enum
{
	ACCEPTANCE_PROVIDER_RANGE_COUNT = 29
};

static size_t AppendStorageProviderRanges(provider_range_t *ranges,
	size_t index, const sg_rune_v2_codec_storage_t *storage)
{
	ranges[index++] = (provider_range_t){ storage, 1U, sizeof(*storage) };
	if (!storage)
		return index;
#define APPEND_STORAGE_RANGE(member, capacity) do { \
	ranges[index++] = (provider_range_t){ storage->member, \
		storage->capacity, sizeof(storage->member[0]) }; \
} while (0)
	APPEND_STORAGE_RANGE(planes, plane_capacity);
	APPEND_STORAGE_RANGE(portal_vertices, portal_vertex_capacity);
	APPEND_STORAGE_RANGE(phases, phase_capacity);
	APPEND_STORAGE_RANGE(phase_transitions, phase_transition_capacity);
	APPEND_STORAGE_RANGE(cells, cell_capacity);
	APPEND_STORAGE_RANGE(portals, portal_capacity);
	APPEND_STORAGE_RANGE(surfaces, surface_capacity);
	APPEND_STORAGE_RANGE(affordances, affordance_capacity);
	APPEND_STORAGE_RANGE(kernels, kernel_capacity);
	APPEND_STORAGE_RANGE(landmarks, landmark_capacity);
	APPEND_STORAGE_RANGE(mechanisms, mechanism_capacity);
#undef APPEND_STORAGE_RANGE
	return index;
}

static sg_rune_v2_semantic_diagnostic_t ProviderRangesCheck(
	const sg_rune_v2_semantic_catalog_t *catalog,
	const provider_range_t *ranges, size_t range_count)
{
	size_t byte_sizes[ACCEPTANCE_PROVIDER_RANGE_COUNT];
	size_t index;

	if (!catalog || !ranges ||
		range_count > (size_t)ACCEPTANCE_PROVIDER_RANGE_COUNT)
		return SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT;
	for (index = 0U; index < range_count; index++)
	{
		if (ranges[index].count != 0U &&
			ranges[index].element_size > SIZE_MAX / ranges[index].count)
			return SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT;
		byte_sizes[index] = ranges[index].count * ranges[index].element_size;
		if (ranges[index].pointer && byte_sizes[index] != 0U &&
			((sizeof(size_t) > sizeof(uintptr_t) &&
			 byte_sizes[index] > (size_t)UINTPTR_MAX) ||
			 (uintptr_t)ranges[index].pointer >
			 UINTPTR_MAX - (uintptr_t)byte_sizes[index]))
			return SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT;
	}
	for (index = 0U; index < range_count; index++)
		if (ranges[index].pointer && byte_sizes[index] != 0U &&
			SG_RuneV2CompleteModelProofSemanticCatalogStorageOverlaps(
				catalog, ranges[index].pointer, byte_sizes[index]))
			return SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY;
	return SG_RUNE_V2_SEMANTIC_OK;
}

#define OVERLAPS_CANDIDATE(pointer, count, type) \
	(RangeOverlap((pointer), (count), sizeof(type), candidate, 1U, \
		sizeof(*candidate)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate_evidence, 1U, \
		sizeof(*candidate_evidence)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate->planes, \
		candidate->plane_count, sizeof(*candidate->planes)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate->portal_vertices, \
		candidate->portal_vertex_count, sizeof(*candidate->portal_vertices)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate->phases, \
		candidate->phase_count, sizeof(*candidate->phases)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate->cells, \
		candidate->cell_count, sizeof(*candidate->cells)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate->portals, \
		candidate->portal_count, sizeof(*candidate->portals)) || \
	 RangeOverlap((pointer), (count), sizeof(type), \
		candidate->phase_transitions, candidate->phase_transition_count, \
		sizeof(*candidate->phase_transitions)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate->kernels, \
		candidate->kernel_count, sizeof(*candidate->kernels)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate->surfaces, \
		candidate->surface_count, sizeof(*candidate->surfaces)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate->affordances, \
		candidate->affordance_count, sizeof(*candidate->affordances)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate->landmarks, \
		candidate->landmark_count, sizeof(*candidate->landmarks)) || \
	 RangeOverlap((pointer), (count), sizeof(type), candidate->mechanisms, \
		candidate->mechanism_count, sizeof(*candidate->mechanisms)))

static int CatalogOverlapsCandidate(const sg_rune_model_t *candidate,
	const sg_rune_validation_evidence_t *candidate_evidence,
	const sg_rune_v2_semantic_catalog_view_t *catalog)
{
	return RangeOverlap(catalog, 1U, sizeof(*catalog), candidate, 1U,
			sizeof(*candidate)) ||
		RangeOverlap(catalog, 1U, sizeof(*catalog), candidate_evidence, 1U,
			sizeof(*candidate_evidence)) ||
		OVERLAPS_CANDIDATE(catalog->planes, catalog->counts.planes,
			sg_rune_v2_expected_plane_t) ||
		OVERLAPS_CANDIDATE(catalog->portal_vertices,
			catalog->counts.portal_vertices, sg_rune_v2_expected_vertex_t) ||
		OVERLAPS_CANDIDATE(catalog->phases, catalog->counts.phases,
			sg_rune_v2_expected_phase_t) ||
		OVERLAPS_CANDIDATE(catalog->cells, catalog->counts.cells,
			sg_rune_v2_expected_cell_t) ||
		OVERLAPS_CANDIDATE(catalog->portals, catalog->counts.portals,
			sg_rune_v2_expected_portal_t) ||
		OVERLAPS_CANDIDATE(catalog->phase_transitions,
			catalog->counts.phase_transitions,
			sg_rune_v2_expected_transition_t) ||
		OVERLAPS_CANDIDATE(catalog->surfaces, catalog->counts.surfaces,
			sg_rune_v2_expected_surface_t) ||
		OVERLAPS_CANDIDATE(catalog->affordances, catalog->counts.affordances,
			sg_rune_v2_expected_affordance_t) ||
		OVERLAPS_CANDIDATE(catalog->kernels, catalog->counts.kernels,
			sg_rune_v2_expected_kernel_t) ||
		OVERLAPS_CANDIDATE(catalog->landmarks, catalog->counts.landmarks,
			sg_rune_v2_expected_landmark_t) ||
		OVERLAPS_CANDIDATE(catalog->mechanisms, catalog->counts.mechanisms,
			sg_rune_v2_expected_mechanism_t);
}

#undef OVERLAPS_CANDIDATE

static uint32_t CatalogSectionCount(const sg_rune_v2_semantic_catalog_view_t *catalog,
	uint16_t section)
{
	switch (section)
	{
	case SG_RUNE_V2_SECTION_MODEL: return 1U;
	case SG_RUNE_V2_SECTION_PLANES: return catalog->counts.planes;
	case SG_RUNE_V2_SECTION_PORTAL_VERTICES:
		return catalog->counts.portal_vertices;
	case SG_RUNE_V2_SECTION_PHASES: return catalog->counts.phases;
	case SG_RUNE_V2_SECTION_PHASE_TRANSITIONS:
		return catalog->counts.phase_transitions;
	case SG_RUNE_V2_SECTION_CELLS: return catalog->counts.cells;
	case SG_RUNE_V2_SECTION_PORTALS: return catalog->counts.portals;
	case SG_RUNE_V2_SECTION_SURFACES: return catalog->counts.surfaces;
	case SG_RUNE_V2_SECTION_AFFORDANCES: return catalog->counts.affordances;
	case SG_RUNE_V2_SECTION_KERNELS: return catalog->counts.kernels;
	case SG_RUNE_V2_SECTION_LANDMARKS: return catalog->counts.landmarks;
	case SG_RUNE_V2_SECTION_MECHANISMS: return catalog->counts.mechanisms;
	case SG_RUNE_V2_SECTION_BINDING: return 1U;
	default: return UINT32_MAX;
	}
}

static int ModelCountsEqual(const sg_rune_model_t *model,
	const sg_rune_v2_expected_counts_t *counts, uint16_t *section_out)
{
#define CHECK_COUNT(member, expected, section_name) do { \
	if (model->member != counts->expected) { \
		*section_out = (section_name); \
		return 0; \
	} \
} while (0)
	CHECK_COUNT(plane_count, planes, SG_RUNE_V2_SECTION_PLANES);
	CHECK_COUNT(portal_vertex_count, portal_vertices,
		SG_RUNE_V2_SECTION_PORTAL_VERTICES);
	CHECK_COUNT(phase_count, phases, SG_RUNE_V2_SECTION_PHASES);
	CHECK_COUNT(phase_transition_count, phase_transitions,
		SG_RUNE_V2_SECTION_PHASE_TRANSITIONS);
	CHECK_COUNT(cell_count, cells, SG_RUNE_V2_SECTION_CELLS);
	CHECK_COUNT(portal_count, portals, SG_RUNE_V2_SECTION_PORTALS);
	CHECK_COUNT(surface_count, surfaces, SG_RUNE_V2_SECTION_SURFACES);
	CHECK_COUNT(affordance_count, affordances, SG_RUNE_V2_SECTION_AFFORDANCES);
	CHECK_COUNT(kernel_count, kernels, SG_RUNE_V2_SECTION_KERNELS);
	CHECK_COUNT(landmark_count, landmarks, SG_RUNE_V2_SECTION_LANDMARKS);
	CHECK_COUNT(mechanism_count, mechanisms, SG_RUNE_V2_SECTION_MECHANISMS);
#undef CHECK_COUNT
	return 1;
}

static int EvidenceEqual(const sg_rune_validation_evidence_t *candidate,
	const sg_rune_v2_complete_model_proof_t *proof)
{
	return candidate && proof && candidate->version == proof->version &&
		candidate->reserved == proof->reserved &&
		candidate->verifier_identity == proof->verifier_identity &&
		candidate->bsp_content_id == proof->bsp_content_id &&
		candidate->source_set_identity == proof->source_set_identity &&
		candidate->fixed_point_identity == proof->fixed_point_identity &&
		candidate->fixed_point_rounds == proof->fixed_point_rounds &&
		candidate->proved_cells == proof->represented_cells &&
		candidate->proved_portals == proof->represented_portals &&
		candidate->omitted_cells == proof->omitted_cells &&
		candidate->omitted_portals == proof->omitted_portals &&
		candidate->invented_portals == proof->invented_portals &&
		candidate->pending_work == proof->pending_work;
}

static int EntityEqual(const sg_rune_entity_ref_t *candidate,
	const sg_rune_entity_ref_t *expected)
{
	return candidate->index == expected->index &&
		candidate->spawn_ordinal == expected->spawn_ordinal;
}

static int PlaneEqual(const sg_rune_plane_t *candidate,
	const sg_rune_v2_expected_plane_t *expected)
{
	return StableIdEqual(&candidate->id.value, &expected->id.value) &&
		OrderEqual(&candidate->order, &expected->order) &&
		VecEqual(&candidate->normal, &expected->normal) &&
		FloatEqual(candidate->distance, expected->distance);
}

static int VertexEqual(const sg_rune_vec3_t *candidate,
	const sg_rune_v2_expected_vertex_t *expected)
{
	return FloatEqual(candidate->value[0], expected->x) &&
		FloatEqual(candidate->value[1], expected->y) &&
		FloatEqual(candidate->value[2], expected->z);
}

static int PhaseEqual(const sg_rune_phase_basis_t *candidate,
	const sg_rune_v2_expected_phase_t *expected)
{
	return StableIdEqual(&candidate->id.value, &expected->id.value) &&
		OrderEqual(&candidate->order, &expected->order) &&
		candidate->stance == expected->stance &&
		candidate->motion == expected->motion &&
		candidate->support == expected->support &&
		candidate->medium == expected->medium &&
		candidate->void_relation == expected->void_relation &&
		candidate->reference_frame == expected->reference_frame &&
		StableIdEqual(&candidate->mover.value, &expected->mover.value) &&
		Interval3Equal(&candidate->velocity, &expected->velocity) &&
		IntervalEqual(&candidate->elapsed_ms, &expected->elapsed_ms) &&
		candidate->time_quantum_ms == expected->time_quantum_ms &&
		candidate->time_horizon_ms == expected->time_horizon_ms;
}

static int CellEqual(const sg_rune_cell_t *candidate,
	const sg_rune_v2_expected_cell_t *expected)
{
	return StableIdEqual(&candidate->id.value, &expected->id.value) &&
		OrderEqual(&candidate->order, &expected->order) &&
		GeometryEqual(&candidate->geometry, &expected->geometry) &&
		BoundsEqual(&candidate->bounds, &expected->bounds) &&
		SpanEqual(candidate->boundary_planes.first,
			candidate->boundary_planes.count, expected->boundary_planes.first,
			expected->boundary_planes.count) &&
		SpanEqual(candidate->phases.first, candidate->phases.count,
			expected->phases.first, expected->phases.count) &&
		SpanEqual(candidate->surfaces.first, candidate->surfaces.count,
			expected->surfaces.first, expected->surfaces.count) &&
		SpanEqual(candidate->affordances.first, candidate->affordances.count,
			expected->affordances.first, expected->affordances.count) &&
		SpanEqual(candidate->kernels.first, candidate->kernels.count,
			expected->kernels.first, expected->kernels.count) &&
		SpanEqual(candidate->landmarks.first, candidate->landmarks.count,
			expected->landmarks.first, expected->landmarks.count) &&
		SpanEqual(candidate->mechanisms.first, candidate->mechanisms.count,
			expected->mechanisms.first, expected->mechanisms.count) &&
		candidate->bsp_leaf.index == expected->bsp_leaf.index &&
		candidate->bsp_area.index == expected->bsp_area.index &&
		candidate->bsp_cluster.index == expected->bsp_cluster.index &&
		candidate->contents == expected->contents &&
		candidate->semantics == expected->semantics;
}

static int PortalEqual(const sg_rune_portal_t *candidate,
	const sg_rune_v2_expected_portal_t *expected)
{
	return StableIdEqual(&candidate->id.value, &expected->id.value) &&
		OrderEqual(&candidate->order, &expected->order) &&
		GeometryEqual(&candidate->geometry, &expected->geometry) &&
		StableIdEqual(&candidate->from_cell.value,
			&expected->from_cell.value) &&
		StableIdEqual(&candidate->to_cell.value, &expected->to_cell.value) &&
		StableIdEqual(&candidate->boundary_plane.value,
			&expected->boundary_plane.value) &&
		SpanEqual(candidate->boundary_vertices.first,
			candidate->boundary_vertices.count,
			expected->boundary_vertices.first,
			expected->boundary_vertices.count) &&
		SpanEqual(candidate->phases.first, candidate->phases.count,
			expected->phases.first, expected->phases.count) &&
		candidate->direction == expected->direction &&
		FloatEqual(candidate->clearance, expected->clearance) &&
		candidate->contents_from == expected->contents_from &&
		candidate->contents_to == expected->contents_to &&
		candidate->flags == expected->flags;
}

static int SurfaceEqual(const sg_rune_surface_t *candidate,
	const sg_rune_v2_expected_surface_t *expected)
{
	return StableIdEqual(&candidate->id.value, &expected->id.value) &&
		OrderEqual(&candidate->order, &expected->order) &&
		GeometryEqual(&candidate->geometry, &expected->geometry) &&
		StableIdEqual(&candidate->owner_cell.value,
			&expected->owner_cell.value) &&
		StableIdEqual(&candidate->plane.value, &expected->plane.value) &&
		VecEqual(&candidate->normal, &expected->normal) &&
		candidate->contents == expected->contents &&
		candidate->semantics == expected->semantics;
}

static int AffordanceEqual(const sg_rune_affordance_t *candidate,
	const sg_rune_v2_expected_affordance_t *expected)
{
	return StableIdEqual(&candidate->id.value, &expected->id.value) &&
		OrderEqual(&candidate->order, &expected->order) &&
		StableIdEqual(&candidate->owner_cell.value,
			&expected->owner_cell.value) &&
		SpanEqual(candidate->surfaces.first, candidate->surfaces.count,
			expected->surfaces.first, expected->surfaces.count) &&
		SpanEqual(candidate->phases.first, candidate->phases.count,
			expected->phases.first, expected->phases.count) &&
		candidate->kind == expected->kind &&
		IntervalEqual(&candidate->range, &expected->range) &&
		candidate->flags == expected->flags;
}

static int TransitionEqual(const sg_rune_phase_transition_t *candidate,
	const sg_rune_v2_expected_transition_t *expected)
{
	return StableIdEqual(&candidate->id.value, &expected->id.value) &&
		OrderEqual(&candidate->order, &expected->order) &&
		StableIdEqual(&candidate->cell.value, &expected->cell.value) &&
		StableIdEqual(&candidate->source_phase.value,
			&expected->source_phase.value) &&
		StableIdEqual(&candidate->destination_phase.value,
			&expected->destination_phase.value) &&
		candidate->kind == expected->kind &&
		IntervalEqual(&candidate->duration_ms, &expected->duration_ms) &&
		candidate->flags == expected->flags;
}

static int KernelEqual(const sg_rune_capability_kernel_t *candidate,
	const sg_rune_v2_expected_kernel_t *expected)
{
	const sg_rune_kernel_parameters_t *cp = &candidate->parameters;
	const sg_rune_kernel_parameters_t *ep = &expected->parameters;

	return StableIdEqual(&candidate->id.value, &expected->id.value) &&
		OrderEqual(&candidate->order, &expected->order) &&
		StableIdEqual(&candidate->source_cell.value,
			&expected->source_cell.value) &&
		StableIdEqual(&candidate->destination_cell.value,
			&expected->destination_cell.value) &&
		StableIdEqual(&candidate->boundary.value, &expected->boundary.value) &&
		StableIdEqual(&candidate->affordance.value,
			&expected->affordance.value) &&
		StableIdEqual(&candidate->mechanism.value,
			&expected->mechanism.value) &&
		StableIdEqual(&candidate->source_phase.value,
			&expected->source_phase.value) &&
		StableIdEqual(&candidate->destination_phase.value,
			&expected->destination_phase.value) &&
		StableIdEqual(&candidate->transition.value,
			&expected->transition.value) &&
		candidate->family == expected->family &&
		candidate->cost_law == expected->cost_law &&
		Interval3Equal(&cp->displacement, &ep->displacement) &&
		IntervalEqual(&cp->duration_ms, &ep->duration_ms) &&
		IntervalEqual(&cp->speed, &ep->speed) &&
		IntervalEqual(&cp->acceleration, &ep->acceleration) &&
		IntervalEqual(&cp->vertical_acceleration, &ep->vertical_acceleration) &&
		FloatEqual(cp->gravity, ep->gravity) && FloatEqual(cp->drag, ep->drag) &&
		cp->physics_abi_id == ep->physics_abi_id &&
		cp->fixed_latency_ms == ep->fixed_latency_ms &&
		cp->dwell_ms == ep->dwell_ms && candidate->flags == expected->flags;
}

static int LandmarkEqual(const sg_rune_landmark_t *candidate,
	const sg_rune_v2_expected_landmark_t *expected)
{
	return StableIdEqual(&candidate->id.value, &expected->id.value) &&
		OrderEqual(&candidate->order, &expected->order) &&
		GeometryEqual(&candidate->geometry, &expected->geometry) &&
		StableIdEqual(&candidate->cell.value, &expected->cell.value) &&
		EntityEqual(&candidate->entity, &expected->entity) &&
		candidate->kind == expected->kind &&
		VecEqual(&candidate->origin, &expected->origin) &&
		BoundsEqual(&candidate->bounds, &expected->bounds) &&
		StableIdEqual(&candidate->mechanism.value,
			&expected->mechanism.value) &&
		StableIdEqual(&candidate->surface.value, &expected->surface.value) &&
		candidate->semantics == expected->semantics;
}

static int MechanismEqual(const sg_rune_mechanism_t *candidate,
	const sg_rune_v2_expected_mechanism_t *expected)
{
	return StableIdEqual(&candidate->id.value, &expected->id.value) &&
		OrderEqual(&candidate->order, &expected->order) &&
		candidate->kind == expected->kind &&
		StableIdEqual(&candidate->entry_cell.value,
			&expected->entry_cell.value) &&
		StableIdEqual(&candidate->exit_cell.value,
			&expected->exit_cell.value) &&
		StableIdEqual(&candidate->activation_landmark.value,
			&expected->activation_landmark.value) &&
		EntityEqual(&candidate->entity, &expected->entity) &&
		IntervalEqual(&candidate->dwell_ms, &expected->dwell_ms) &&
		IntervalEqual(&candidate->travel_ms, &expected->travel_ms) &&
		SpanEqual(candidate->topology.first, candidate->topology.count,
			expected->topology.first, expected->topology.count) &&
		candidate->flags == expected->flags;
}

static sg_rune_v2_semantic_diagnostic_t ArtifactLintView(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_semantic_catalog_view_t *catalog,
	sg_rune_v2_semantic_report_t *report_out)
{
	sg_rune_v2_wire_view_t wire;
	sg_rune_v2_wire_diagnostic_t wire_diagnostic;
	sg_rune_v2_semantic_diagnostic_t diagnostic;
	uint16_t section;

	if (!encoded)
		return Report(report_out, SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT,
			SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE, 0U, 0U);
	diagnostic = CatalogCheck(catalog, report_out);
	if (diagnostic != SG_RUNE_V2_SEMANTIC_OK)
		return diagnostic;
	wire_diagnostic = SG_RuneV2WireInspect(encoded, encoded_size, &wire);
	if (wire_diagnostic != SG_RUNE_V2_WIRE_OK)
		return Report(report_out, SG_RUNE_V2_SEMANTIC_WIRE_REJECTED,
			wire_diagnostic, SG_RUNE_FAILURE_NONE, 0U, 0U);
	if (!BindingEqual(&wire.binding, &catalog->binding))
		return Report(report_out, SG_RUNE_V2_SEMANTIC_BINDING_MISMATCH,
			SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
			SG_RUNE_V2_SECTION_BINDING, 0U);
	for (section = SG_RUNE_V2_SECTION_MODEL;
		section <= SG_RUNE_V2_SECTION_BINDING; section++)
		if (wire.section[section - 1U].count !=
			CatalogSectionCount(catalog, section))
			return Report(report_out,
				SG_RUNE_V2_SEMANTIC_SECTION_COUNT_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE, section, 0U);
	return Report(report_out, SG_RUNE_V2_SEMANTIC_OK, SG_RUNE_V2_WIRE_OK,
		SG_RUNE_FAILURE_NONE, 0U, 0U);
}

static sg_rune_v2_semantic_diagnostic_t SemanticValidateView(
	const sg_rune_model_t *candidate,
	const sg_rune_validation_evidence_t *candidate_evidence,
	const sg_rune_v2_semantic_catalog_view_t *catalog,
	sg_rune_v2_semantic_report_t *report_out)
{
	sg_rune_failure_reason_t reason;
	sg_rune_v2_semantic_diagnostic_t diagnostic;
	uint16_t section = 0U;
	uint32_t index;

	if (!candidate || !candidate_evidence || !catalog)
		return Report(report_out, SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT,
			SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE, 0U, 0U);
	if (CatalogOverlapsCandidate(candidate, candidate_evidence, catalog))
		return Report(report_out, SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
			SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE, 0U, 0U);
	diagnostic = CatalogCheck(catalog, report_out);
	if (diagnostic != SG_RUNE_V2_SEMANTIC_OK)
		return diagnostic;
	reason = SG_RuneModelValidate(candidate, candidate_evidence);
	if (reason != SG_RUNE_FAILURE_NONE)
		return Report(report_out, SG_RUNE_V2_SEMANTIC_MODEL_REJECTED,
			SG_RUNE_V2_WIRE_OK, reason, SG_RUNE_V2_SECTION_MODEL, 0U);
	if (!ModelCountsEqual(candidate, &catalog->counts, &section))
		return Report(report_out, SG_RUNE_V2_SEMANTIC_SECTION_COUNT_MISMATCH,
			SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE, section, 0U);
	if (!IdentityEqual(&candidate->identity, &catalog->identity) ||
		candidate->completeness.state != SG_RUNE_COMPLETENESS_COMPLETE ||
		candidate->completeness.reason != SG_RUNE_FAILURE_NONE ||
		candidate->completeness.expected_cells != catalog->counts.cells ||
		candidate->completeness.covered_cells != catalog->counts.cells ||
		candidate->completeness.expected_portals != catalog->counts.portals ||
		candidate->completeness.covered_portals != catalog->counts.portals ||
		candidate->completeness.failure_record != UINT32_MAX)
		return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
			SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
			SG_RUNE_V2_SECTION_MODEL, 0U);
	if (!EvidenceEqual(candidate_evidence, &catalog->complete_model_proof))
		return Report(report_out, SG_RUNE_V2_SEMANTIC_EVIDENCE_MISMATCH,
			SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
			SG_RUNE_V2_SECTION_MODEL, 0U);
	for (index = 0U; index < catalog->counts.planes; index++)
		if (!PlaneEqual(&candidate->planes[index], &catalog->planes[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_PLANES, index);
	for (index = 0U; index < catalog->counts.portal_vertices; index++)
		if (!VertexEqual(&candidate->portal_vertices[index],
			&catalog->portal_vertices[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_PORTAL_VERTICES, index);
	for (index = 0U; index < catalog->counts.phases; index++)
		if (!PhaseEqual(&candidate->phases[index], &catalog->phases[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_PHASES, index);
	for (index = 0U; index < catalog->counts.phase_transitions; index++)
		if (!TransitionEqual(&candidate->phase_transitions[index],
			&catalog->phase_transitions[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_PHASE_TRANSITIONS, index);
	for (index = 0U; index < catalog->counts.cells; index++)
		if (!CellEqual(&candidate->cells[index], &catalog->cells[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_CELLS, index);
	for (index = 0U; index < catalog->counts.portals; index++)
		if (!PortalEqual(&candidate->portals[index], &catalog->portals[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_PORTALS, index);
	for (index = 0U; index < catalog->counts.surfaces; index++)
		if (!SurfaceEqual(&candidate->surfaces[index],
			&catalog->surfaces[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_SURFACES, index);
	for (index = 0U; index < catalog->counts.affordances; index++)
		if (!AffordanceEqual(&candidate->affordances[index],
			&catalog->affordances[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_AFFORDANCES, index);
	for (index = 0U; index < catalog->counts.kernels; index++)
		if (!KernelEqual(&candidate->kernels[index], &catalog->kernels[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_KERNELS, index);
	for (index = 0U; index < catalog->counts.landmarks; index++)
		if (!LandmarkEqual(&candidate->landmarks[index],
			&catalog->landmarks[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_LANDMARKS, index);
	for (index = 0U; index < catalog->counts.mechanisms; index++)
		if (!MechanismEqual(&candidate->mechanisms[index],
			&catalog->mechanisms[index]))
			return Report(report_out, SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
				SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE,
				SG_RUNE_V2_SECTION_MECHANISMS, index);
	return Report(report_out, SG_RUNE_V2_SEMANTIC_OK, SG_RUNE_V2_WIRE_OK,
		SG_RUNE_FAILURE_NONE, 0U, 0U);
}

typedef struct semantic_accept_context_s
{
	const sg_rune_v2_semantic_catalog_view_t *catalog;
	sg_rune_v2_semantic_report_t *report;
	sg_rune_v2_semantic_diagnostic_t diagnostic;
} semantic_accept_context_t;

static int SemanticAcceptCandidate(
	const sg_rune_v2_wire_binding_t *binding,
	const sg_rune_model_t *candidate,
	const sg_rune_validation_evidence_t *evidence,
	void *context_pointer)
{
	semantic_accept_context_t *context =
		(semantic_accept_context_t *)context_pointer;

	if (!BindingEqual(binding, &context->catalog->binding))
		context->diagnostic = Report(context->report,
			SG_RUNE_V2_SEMANTIC_BINDING_MISMATCH, SG_RUNE_V2_WIRE_OK,
			SG_RUNE_FAILURE_NONE, SG_RUNE_V2_SECTION_BINDING, 0U);
	else
		context->diagnostic = SemanticValidateView(candidate, evidence,
			context->catalog, context->report);
	return context->diagnostic == SG_RUNE_V2_SEMANTIC_OK;
}

static sg_rune_v2_semantic_diagnostic_t CatalogAuthenticate(
	const sg_rune_v2_semantic_catalog_t *catalog,
	const sg_rune_v2_semantic_catalog_view_t **view_out)
{
	if (!catalog || !view_out)
		return SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED;
	if (!SG_RuneV2CompleteModelProofSemanticCatalogRead(catalog, view_out))
		return SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED;
	return SG_RUNE_V2_SEMANTIC_OK;
}

static sg_rune_v2_semantic_diagnostic_t AcceptanceProviderRangesCheck(
	const sg_rune_v2_semantic_catalog_t *catalog,
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_codec_storage_t *scratch,
	const sg_rune_v2_codec_storage_t *published,
	sg_rune_v2_wire_binding_t *binding_out, sg_rune_model_t *model_out,
	sg_rune_validation_evidence_t *evidence_out,
	sg_rune_v2_semantic_report_t *report_out)
{
	provider_range_t ranges[ACCEPTANCE_PROVIDER_RANGE_COUNT];
	size_t count = 0U;

	ranges[count++] = (provider_range_t){ encoded, encoded_size, 1U };
	count = AppendStorageProviderRanges(ranges, count, scratch);
	count = AppendStorageProviderRanges(ranges, count, published);
	ranges[count++] = (provider_range_t){ binding_out, 1U,
		sizeof(*binding_out) };
	ranges[count++] = (provider_range_t){ model_out, 1U,
		sizeof(*model_out) };
	ranges[count++] = (provider_range_t){ evidence_out, 1U,
		sizeof(*evidence_out) };
	ranges[count++] = (provider_range_t){ report_out, 1U,
		sizeof(*report_out) };
	return ProviderRangesCheck(catalog, ranges, count);
}

sg_rune_v2_semantic_diagnostic_t SG_RuneV2ArtifactLint(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_semantic_catalog_t *catalog,
	sg_rune_v2_semantic_report_t *report_out)
{
	const sg_rune_v2_semantic_catalog_view_t *view;
	sg_rune_v2_semantic_diagnostic_t diagnostic;
	provider_range_t ranges[2];

	diagnostic = CatalogAuthenticate(catalog, &view);
	if (diagnostic != SG_RUNE_V2_SEMANTIC_OK)
		return diagnostic;
	ranges[0] = (provider_range_t){ encoded, encoded_size, 1U };
	ranges[1] = (provider_range_t){ report_out, 1U, sizeof(*report_out) };
	diagnostic = ProviderRangesCheck(catalog, ranges, 2U);
	if (diagnostic != SG_RUNE_V2_SEMANTIC_OK)
		return diagnostic;
	if (RangeOverlap(report_out, 1U, sizeof(*report_out), encoded,
		encoded_size, 1U))
		return SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT;
	return ArtifactLintView(encoded, encoded_size, view, report_out);
}

sg_rune_v2_semantic_diagnostic_t SG_RuneV2ArtifactAccept(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_semantic_catalog_t *catalog,
	const sg_rune_v2_codec_storage_t *scratch,
	const sg_rune_v2_codec_storage_t *published,
	sg_rune_v2_wire_binding_t *binding_out,
	sg_rune_model_t *model_out,
	sg_rune_validation_evidence_t *evidence_out,
	sg_rune_v2_semantic_report_t *report_out)
{
	const sg_rune_v2_semantic_catalog_view_t *view;
	semantic_accept_context_t context;
	sg_rune_v2_semantic_diagnostic_t diagnostic;
	sg_rune_v2_wire_diagnostic_t wire_diagnostic;
	int accepted = 0;

	diagnostic = CatalogAuthenticate(catalog, &view);
	if (diagnostic != SG_RUNE_V2_SEMANTIC_OK)
		return diagnostic;
	diagnostic = AcceptanceProviderRangesCheck(catalog, encoded, encoded_size,
		scratch, published, binding_out, model_out, evidence_out, report_out);
	if (diagnostic != SG_RUNE_V2_SEMANTIC_OK)
		return diagnostic;
	if (RangeOverlap(report_out, 1U, sizeof(*report_out), encoded,
			encoded_size, 1U) ||
		ReportOverlapsStorage(report_out, scratch) ||
		ReportOverlapsStorage(report_out, published) ||
		RangeOverlap(report_out, 1U, sizeof(*report_out), binding_out, 1U,
			sizeof(*binding_out)) ||
		RangeOverlap(report_out, 1U, sizeof(*report_out), model_out, 1U,
			sizeof(*model_out)) ||
		RangeOverlap(report_out, 1U, sizeof(*report_out), evidence_out, 1U,
			sizeof(*evidence_out)))
		return SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT;
	if (!encoded || !catalog || !scratch || !published || !binding_out ||
		!model_out || !evidence_out)
		return Report(report_out, SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT,
			SG_RUNE_V2_WIRE_OK, SG_RUNE_FAILURE_NONE, 0U, 0U);
	diagnostic = ArtifactLintView(encoded, encoded_size, view, report_out);
	if (diagnostic != SG_RUNE_V2_SEMANTIC_OK)
		return diagnostic;
	context.catalog = view;
	context.report = report_out;
	context.diagnostic = SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED;
	wire_diagnostic = SG_RuneV2CodecDecodeValidated(encoded, encoded_size,
		scratch, published, SemanticAcceptCandidate, &context, &accepted,
		binding_out, model_out, evidence_out);
	if (wire_diagnostic != SG_RUNE_V2_WIRE_OK)
		return Report(report_out, SG_RUNE_V2_SEMANTIC_WIRE_REJECTED,
			wire_diagnostic, SG_RUNE_FAILURE_NONE, 0U, 0U);
	if (!accepted)
		return context.diagnostic;
	return Report(report_out, SG_RUNE_V2_SEMANTIC_OK, SG_RUNE_V2_WIRE_OK,
		SG_RUNE_FAILURE_NONE, 0U, 0U);
}

#ifdef SG_RUNE_V2_SEMANTIC_TESTING
sg_rune_v2_semantic_diagnostic_t SG_RuneV2ArtifactSemanticCompareForTesting(
	const sg_rune_model_t *candidate,
	const sg_rune_validation_evidence_t *candidate_evidence,
	const sg_rune_v2_semantic_catalog_t *catalog,
	sg_rune_v2_semantic_report_t *report_out)
{
	const sg_rune_v2_semantic_catalog_view_t *view;
	sg_rune_v2_semantic_diagnostic_t diagnostic;
	provider_range_t range;

	diagnostic = CatalogAuthenticate(catalog, &view);
	if (diagnostic != SG_RUNE_V2_SEMANTIC_OK)
		return diagnostic;
	range = (provider_range_t){ report_out, 1U, sizeof(*report_out) };
	diagnostic = ProviderRangesCheck(catalog, &range, 1U);
	if (diagnostic != SG_RUNE_V2_SEMANTIC_OK)
		return diagnostic;
	return SemanticValidateView(candidate, candidate_evidence, view,
		report_out);
}
#endif

const char *SG_RuneV2SemanticDiagnosticString(
	sg_rune_v2_semantic_diagnostic_t diagnostic)
{
	switch (diagnostic)
	{
	case SG_RUNE_V2_SEMANTIC_OK: return "ok";
	case SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT: return "invalid argument";
	case SG_RUNE_V2_SEMANTIC_WIRE_REJECTED: return "wire rejected";
	case SG_RUNE_V2_SEMANTIC_BINDING_MISMATCH: return "binding mismatch";
	case SG_RUNE_V2_SEMANTIC_SECTION_COUNT_MISMATCH:
		return "section count mismatch";
	case SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED: return "catalog rejected";
	case SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY:
		return "circular authority";
	case SG_RUNE_V2_SEMANTIC_MODEL_REJECTED: return "model rejected";
	case SG_RUNE_V2_SEMANTIC_EVIDENCE_MISMATCH: return "evidence mismatch";
	case SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH: return "record mismatch";
	}
	return "invalid semantic diagnostic";
}
