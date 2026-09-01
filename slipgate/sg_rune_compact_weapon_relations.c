#include "sg_rune_compact_weapon_relations.h"

#include "sg_rune_compact_builder_owner.h"
#include "sg_rune_compact_source_surface_catalog.h"

#include <stdlib.h>
#include <string.h>

#define SG_RUNE_COMPACT_WEAPON_RELATIONS_STATE UINT64_C(0x535752454c415449)

struct sg_rune_compact_weapon_relations_s
{
	uint64_t state;
	uint64_t state_inverse;
	const struct sg_rune_compact_weapon_relations_s *self;
	sg_rune_compact_response_partition_t *response_owner;
	sg_rune_compact_identity_t identity;
	/* Metadata only: every response array stays in response_owner. */
	sg_rune_compact_response_partition_view_t response_snapshot;
	sg_rune_compact_response_fact_t *facts;
	uint32_t fact_count;
	sg_rune_compact_static_occluder_t *occluders;
	uint32_t occluder_count;
};

typedef struct relation_sources_s
{
	sg_rune_compact_builder_view_t builder;
	sg_rune_compact_builder_owner_view_t owner;
	sg_rune_compact_geometry_view_t geometry;
	sg_rune_compact_response_partition_view_t response;
} relation_sources_t;

#if defined(SG_RUNE_COMPACT_WEAPON_RELATIONS_TESTING)
static uint32_t test_fail_after = UINT32_MAX;
static uint32_t test_allocation_count;

void SG_RuneCompactWeaponRelationsTestFailAfter(uint32_t allocation)
{
	test_fail_after = allocation;
	test_allocation_count = 0U;
}

uint32_t SG_RuneCompactWeaponRelationsTestAllocationCount(void)
{
	return test_allocation_count;
}
#endif

static void *Allocate(size_t count, size_t size)
{
	if (count == 0U || size == 0U || count > SIZE_MAX / size)
		return NULL;
#if defined(SG_RUNE_COMPACT_WEAPON_RELATIONS_TESTING)
	if (test_allocation_count++ == test_fail_after)
		return NULL;
#endif
	return calloc(count, size);
}

static void ClearError(sg_rune_compact_weapon_relations_error_t *error)
{
	if (error != NULL)
		memset(error, 0, sizeof(*error));
}

static void SetError(sg_rune_compact_weapon_relations_error_t *error,
	sg_rune_compact_weapon_relations_error_code_t code,
	sg_rune_compact_weapon_relations_record_domain_t domain, uint32_t record)
{
	if (error == NULL)
		return;
	memset(error, 0, sizeof(*error));
	error->code = code;
	error->domain = domain;
	error->record = record;
}

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right;
}

static int CompareU64(uint64_t left, uint64_t right)
{
	return left < right ? -1 : left > right;
}

static int OccluderCompare(const sg_rune_compact_static_occluder_t *left,
	const sg_rune_compact_static_occluder_t *right)
{
	int comparison = CompareU32(left->model, right->model);

	if (comparison == 0)
		comparison = CompareU32(left->brush, right->brush);
	if (comparison == 0)
		comparison = CompareU32(left->contents, right->contents);
	if (comparison == 0)
		comparison = CompareU32(left->conditional, right->conditional);
	return comparison;
}

static int SealEqual(const sg_rune_compact_response_seal_t *left,
	const sg_rune_compact_response_seal_t *right)
{
	return left->version == right->version &&
		left->reserved == right->reserved && left->flags == right->flags &&
		left->split_frontier_count == right->split_frontier_count &&
		left->source_fragment_count == right->source_fragment_count &&
		left->target_patch_count == right->target_patch_count &&
		left->split_count == right->split_count &&
		left->response_pair_count == right->response_pair_count &&
		left->certified_direct_pair_count == right->certified_direct_pair_count &&
		left->certified_static_impact_pair_count ==
			right->certified_static_impact_pair_count &&
		left->unresolved_response_pair_count ==
			right->unresolved_response_pair_count &&
		left->unresolved_candidate_group_count ==
			right->unresolved_candidate_group_count &&
		left->source_endpoint_group_count ==
			right->source_endpoint_group_count &&
		left->target_endpoint_group_count ==
			right->target_endpoint_group_count &&
		left->source_endpoint_member_count ==
			right->source_endpoint_member_count &&
		left->target_endpoint_member_count ==
			right->target_endpoint_member_count &&
		left->static_occluder_count == right->static_occluder_count &&
		left->compact_facet_count == right->compact_facet_count &&
		left->compact_cell_count == right->compact_cell_count &&
		left->compact_source_surface_count ==
			right->compact_source_surface_count &&
		left->compact_source_surface_vertex_count ==
			right->compact_source_surface_vertex_count &&
		left->source_surface_catalog_seal ==
			right->source_surface_catalog_seal;
}

static int ResponseSnapshotMatches(
	const sg_rune_compact_response_partition_view_t *left,
	const sg_rune_compact_response_partition_view_t *right)
{
	return SG_RuneCompactIdentityMatches(&left->identity, &right->identity) &&
		left->source_fragments == right->source_fragments &&
		left->source_fragment_count == right->source_fragment_count &&
		left->source_halfspaces == right->source_halfspaces &&
		left->source_halfspace_count == right->source_halfspace_count &&
		left->target_patches == right->target_patches &&
		left->target_patch_count == right->target_patch_count &&
		left->target_vertices == right->target_vertices &&
		left->target_vertex_count == right->target_vertex_count &&
		left->splits == right->splits && left->split_count == right->split_count &&
		left->response_pairs == right->response_pairs &&
		left->response_pair_count == right->response_pair_count &&
		left->candidate_groups == right->candidate_groups &&
		left->candidate_group_count == right->candidate_group_count &&
		left->source_endpoint_groups == right->source_endpoint_groups &&
		left->source_endpoint_group_count == right->source_endpoint_group_count &&
		left->source_endpoint_members == right->source_endpoint_members &&
		left->source_endpoint_member_count ==
			right->source_endpoint_member_count &&
		left->target_endpoint_groups == right->target_endpoint_groups &&
		left->target_endpoint_group_count == right->target_endpoint_group_count &&
		left->target_endpoint_members == right->target_endpoint_members &&
		left->target_endpoint_member_count ==
			right->target_endpoint_member_count &&
		left->static_occluder_count == right->static_occluder_count &&
		left->compact_facet_count == right->compact_facet_count &&
		left->compact_cell_count == right->compact_cell_count &&
		left->compact_source_surfaces == right->compact_source_surfaces &&
		left->compact_source_surface_count ==
			right->compact_source_surface_count &&
		left->compact_source_surface_vertices ==
			right->compact_source_surface_vertices &&
		left->compact_source_surface_vertex_count ==
			right->compact_source_surface_vertex_count &&
		SealEqual(&left->seal, &right->seal);
}

static int SourcesRead(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_response_partition_t *response,
	relation_sources_t *sources,
	sg_rune_compact_weapon_relations_error_t *error)
{
	if (!SG_RuneCompactBuilderRead(builder, &sources->builder) ||
		!SG_RuneCompactBuilderOwnerRead(builder, &sources->owner)) {
		SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_BUILDER_READ,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_OWNER, 0U);
		return 0;
	}
	if (!SG_RuneCompactGeometryRead(geometry, &sources->geometry)) {
		SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_GEOMETRY_READ,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_OWNER, 0U);
		return 0;
	}
	if (!SG_RuneCompactResponsePartitionRead(response, &sources->response) ||
		!SG_RuneCompactResponsePartitionSealValid(&sources->response)) {
		SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_RESPONSE, 0U);
		return 0;
	}
	return 1;
}

static int SourcesValid(const relation_sources_t *sources,
	sg_rune_compact_weapon_relations_error_t *error)
{
	const sg_rune_compact_response_partition_view_t *response =
		&sources->response;
	const sg_rune_compact_geometry_view_t *geometry = &sources->geometry;
	const sg_rune_compact_builder_owner_view_t *owner = &sources->owner;
	const uint64_t catalog_seal = SG_RuneCompactSourceSurfaceCatalogSeal(
		geometry->source_surfaces, geometry->source_surface_count,
		geometry->source_surface_vertices,
		geometry->source_surface_vertex_count);

	if (!SG_RuneCompactIdentityMatches(&sources->builder.identity,
		&owner->identity) || !SG_RuneCompactIdentityMatches(
		&sources->builder.identity, &geometry->identity) ||
		!SG_RuneCompactIdentityMatches(&sources->builder.identity,
			&response->identity)) {
		SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_IDENTITY, 0U);
		return 0;
	}
	if (owner->world == NULL || owner->visibility == NULL ||
		(owner->visibility->occluder_count != 0U &&
		 owner->visibility->occluders == NULL)) {
		SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_OWNER, 0U);
		return 0;
	}
	if (response->static_occluder_count != owner->visibility->occluder_count ||
		response->compact_cell_count != geometry->cell_count ||
		response->compact_facet_count != geometry->facet_count ||
		response->compact_source_surface_count !=
			geometry->source_surface_count ||
		response->compact_source_surface_vertex_count !=
			geometry->source_surface_vertex_count || catalog_seal == 0U ||
		response->seal.source_surface_catalog_seal != catalog_seal) {
		SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_RESPONSE, 0U);
		return 0;
	}
	return 1;
}

static int SourcesCurrent(const relation_sources_t *initial,
	const relation_sources_t *current)
{
	return SG_RuneCompactIdentityMatches(&initial->builder.identity,
		&current->builder.identity) &&
		SG_RuneCompactIdentityMatches(&initial->owner.identity,
			&current->owner.identity) &&
		SG_RuneCompactIdentityMatches(&initial->geometry.identity,
			&current->geometry.identity) &&
		initial->owner.world == current->owner.world &&
		initial->owner.collision == current->owner.collision &&
		initial->owner.host_law == current->owner.host_law &&
		initial->owner.weapon_law == current->owner.weapon_law &&
		initial->owner.configuration == current->owner.configuration &&
		initial->owner.semantics == current->owner.semantics &&
		initial->owner.entity_semantics == current->owner.entity_semantics &&
		initial->owner.visibility == current->owner.visibility &&
		initial->geometry.cells == current->geometry.cells &&
		initial->geometry.cell_count == current->geometry.cell_count &&
		initial->geometry.facets == current->geometry.facets &&
		initial->geometry.facet_count == current->geometry.facet_count &&
		initial->geometry.incidences == current->geometry.incidences &&
		initial->geometry.incidence_count == current->geometry.incidence_count &&
		initial->geometry.cell_incidences == current->geometry.cell_incidences &&
		initial->geometry.cell_incidence_count ==
			current->geometry.cell_incidence_count &&
		initial->geometry.vertices == current->geometry.vertices &&
		initial->geometry.vertex_count == current->geometry.vertex_count &&
		initial->geometry.portals == current->geometry.portals &&
		initial->geometry.portal_count == current->geometry.portal_count &&
		initial->geometry.source_surfaces == current->geometry.source_surfaces &&
		initial->geometry.source_surface_count ==
			current->geometry.source_surface_count &&
		initial->geometry.source_surface_vertices ==
			current->geometry.source_surface_vertices &&
		initial->geometry.source_surface_vertex_count ==
			current->geometry.source_surface_vertex_count &&
		initial->geometry.compact_cells_for_configuration_cell ==
			current->geometry.compact_cells_for_configuration_cell &&
		initial->geometry.compact_cells_for_configuration_cell_count ==
			current->geometry.compact_cells_for_configuration_cell_count &&
		initial->geometry.configuration_cell_compact_cells ==
			current->geometry.configuration_cell_compact_cells &&
		initial->geometry.configuration_cell_compact_cell_count ==
			current->geometry.configuration_cell_compact_cell_count &&
		ResponseSnapshotMatches(&initial->response, &current->response);
}

static int CopyOccluders(const relation_sources_t *sources,
	sg_rune_compact_weapon_relations_t *relations,
	sg_rune_compact_weapon_relations_error_t *error)
{
	const sg_static_visibility_t *visibility = sources->owner.visibility;
	uint32_t index;

	if (visibility->occluder_count == 0U)
		return 1;
	relations->occluders = Allocate(visibility->occluder_count,
		sizeof(*relations->occluders));
	if (relations->occluders == NULL) {
		SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_OCCLUDER, 0U);
		return 0;
	}
	memcpy(relations->occluders, visibility->occluders,
		(size_t)visibility->occluder_count * sizeof(*relations->occluders));
	relations->occluder_count = visibility->occluder_count;
	for (index = 0U; index < relations->occluder_count; index++) {
		const sg_static_visibility_occluder_t *source =
			&visibility->occluders[index];

		if (source->conditional > 1U ||
			source->model >= sources->owner.world->model_count ||
			source->brush >= sources->owner.world->brush_count ||
			(index != 0U && OccluderCompare(&relations->occluders[index - 1U],
				&relations->occluders[index]) >= 0)) {
			SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_SOURCE,
				SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_OCCLUDER, index);
			return 0;
		}
	}
	return 1;
}

static int PairValid(const sg_rune_compact_response_partition_view_t *response,
	const sg_rune_compact_response_pair_t *pair)
{
	const sg_rune_compact_response_fragment_t *source;
	const sg_rune_compact_response_patch_t *target;

	if (pair->source_fragment >= response->source_fragment_count ||
		pair->target_patch >= response->target_patch_count ||
		pair->reserved[0] != 0U || pair->reserved[1] != 0U)
		return 0;
	source = &response->source_fragments[pair->source_fragment];
	target = &response->target_patches[pair->target_patch];
	if (pair->source_valid_stances != source->valid_stances ||
		pair->target_valid_stances != target->valid_stances ||
		pair->requires_exact_ray > 1U || pair->requires_area_state > 1U ||
		pair->classification > SG_STATIC_VISIBILITY_CONDITIONAL ||
		pair->reason > SG_STATIC_VISIBILITY_REASON_SKY ||
		(pair->relation_flags &
			~(sg_rune_compact_static_relation_flags_t)
				SG_RUNE_COMPACT_STATIC_RELATION_FLAGS_KNOWN) != 0U)
		return 0;
	if (pair->certificate == SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT)
		return pair->first_hit_occluder ==
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
			pair->certificate_split == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
			(pair->relation_flags &
				SG_RUNE_COMPACT_STATIC_RELATION_DIRECT) != 0U &&
			(pair->relation_flags &
				SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT) == 0U;
	if (pair->certificate == SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT)
		return pair->first_hit_occluder < response->static_occluder_count &&
			pair->certificate_split < response->split_count &&
			response->splits[pair->certificate_split].kind ==
				SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE &&
			response->splits[pair->certificate_split].occluder ==
				pair->first_hit_occluder &&
			(pair->relation_flags &
				SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT) != 0U &&
			(pair->relation_flags &
				SG_RUNE_COMPACT_STATIC_RELATION_DIRECT) == 0U;
	return 0;
}

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int Q8Equal(const sg_rune_q8_vec3_t *left, const sg_rune_q8_vec3_t *right)
{
	return left->value[0] == right->value[0] &&
		left->value[1] == right->value[1] &&
		left->value[2] == right->value[2];
}

static int TraceEqual(const sg_host_collision_trace_t *left,
	const sg_host_collision_trace_t *right)
{
	uint32_t axis;

	if (left->allsolid != right->allsolid ||
		left->startsolid != right->startsolid ||
		FloatBits(left->fraction) != FloatBits(right->fraction) ||
		FloatBits(left->plane.distance) != FloatBits(right->plane.distance) ||
		left->plane.type != right->plane.type ||
		left->contents != right->contents || left->texinfo != right->texinfo ||
		left->surface_flags != right->surface_flags ||
		left->model_index != right->model_index ||
		left->instance_id != right->instance_id || left->brush != right->brush ||
		left->brush_side != right->brush_side)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (FloatBits(left->end[axis]) != FloatBits(right->end[axis]) ||
			FloatBits(left->plane.normal[axis]) !=
				FloatBits(right->plane.normal[axis]))
			return 0;
	return 1;
}

static int PairEqual(const sg_rune_compact_response_pair_t *left,
	const sg_rune_compact_response_pair_t *right)
{
	return left->source_fragment == right->source_fragment &&
		left->target_patch == right->target_patch &&
		left->classification == right->classification &&
		left->reason == right->reason &&
		left->first_hit_occluder == right->first_hit_occluder &&
		left->requires_exact_ray == right->requires_exact_ray &&
		left->requires_area_state == right->requires_area_state &&
		left->certificate == right->certificate &&
		left->relation_flags == right->relation_flags &&
		left->source_valid_stances == right->source_valid_stances &&
		left->target_valid_stances == right->target_valid_stances &&
		left->reserved[0] == right->reserved[0] &&
		left->reserved[1] == right->reserved[1] &&
		left->certificate_split == right->certificate_split &&
		Q8Equal(&left->target_witness, &right->target_witness) &&
		TraceEqual(&left->trace, &right->trace);
}

static int FactMatchesPair(const sg_rune_compact_response_fact_t *fact,
	const sg_rune_compact_response_pair_t *pair)
{
	const uint32_t expected_occluder_count = pair->certificate ==
		SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT ? 1U : 0U;
	const uint32_t expected_first = expected_occluder_count != 0U ?
		pair->first_hit_occluder : 0U;

	return fact->source_fragment == pair->source_fragment &&
		fact->target_patch == pair->target_patch &&
		fact->flags == pair->relation_flags &&
		fact->visibility == (sg_rune_compact_static_visibility_class_t)
			pair->classification &&
		fact->visibility_reason ==
			(sg_rune_compact_static_visibility_reason_t)pair->reason &&
		fact->requires_exact_ray == pair->requires_exact_ray &&
		fact->requires_area_state == pair->requires_area_state &&
		fact->certificate_split == pair->certificate_split &&
		Q8Equal(&fact->target_witness, &pair->target_witness) &&
		fact->occluders.first == expected_first &&
		fact->occluders.count == expected_occluder_count &&
		TraceEqual(&fact->trace, &pair->trace);
}

static int PairCompare(const sg_rune_compact_response_partition_view_t *response,
	const sg_rune_compact_response_pair_t *left,
	const sg_rune_compact_response_pair_t *right)
{
	const sg_rune_compact_response_fragment_t *left_source =
		&response->source_fragments[left->source_fragment];
	const sg_rune_compact_response_fragment_t *right_source =
		&response->source_fragments[right->source_fragment];
	const sg_rune_compact_response_patch_t *left_target =
		&response->target_patches[left->target_patch];
	const sg_rune_compact_response_patch_t *right_target =
		&response->target_patches[right->target_patch];
	int comparison;

#define PAIR_COMPARE(a, b) \
	do { comparison = CompareU32((a), (b)); if (comparison != 0) return comparison; } while (0)
	PAIR_COMPARE(left_source->parent_cell.value, right_source->parent_cell.value);
	PAIR_COMPARE(left_target->target_cell.value, right_target->target_cell.value);
	comparison = CompareU64(left_source->static_partition_id,
		right_source->static_partition_id);
	if (comparison != 0)
		return comparison;
	comparison = CompareU64(left_target->static_partition_id,
		right_target->static_partition_id);
	if (comparison != 0)
		return comparison;
	PAIR_COMPARE(left_source->configuration_region,
		right_source->configuration_region);
	PAIR_COMPARE(left_source->configuration_cell,
		right_source->configuration_cell);
	PAIR_COMPARE(left_target->configuration_region,
		right_target->configuration_region);
	PAIR_COMPARE(left_target->configuration_cell,
		right_target->configuration_cell);
	PAIR_COMPARE(left_source->bsp_leaf, right_source->bsp_leaf);
	PAIR_COMPARE(left_source->bsp_area, right_source->bsp_area);
	PAIR_COMPARE(left_source->bsp_cluster, right_source->bsp_cluster);
	PAIR_COMPARE(left_target->bsp_leaf, right_target->bsp_leaf);
	PAIR_COMPARE(left_target->bsp_area, right_target->bsp_area);
	PAIR_COMPARE(left_target->bsp_cluster, right_target->bsp_cluster);
	PAIR_COMPARE(left_source->boundary_incidences.first,
		right_source->boundary_incidences.first);
	PAIR_COMPARE(left_source->boundary_incidences.count,
		right_source->boundary_incidences.count);
	PAIR_COMPARE(left_target->boundary_incidences.first,
		right_target->boundary_incidences.first);
	PAIR_COMPARE(left_target->boundary_incidences.count,
		right_target->boundary_incidences.count);
	PAIR_COMPARE(left->source_fragment, right->source_fragment);
	PAIR_COMPARE(left->target_patch, right->target_patch);
#undef PAIR_COMPARE
	return 0;
}

static int ResponsePolicyMatches(
	const sg_rune_compact_response_partition_view_t *response,
	const sg_rune_compact_response_fact_t *facts, uint32_t fact_count)
{
	uint32_t expected_count;
	uint32_t index;

	if (response->seal.certified_direct_pair_count > UINT32_MAX -
		response->seal.certified_static_impact_pair_count)
		return 0;
	expected_count = response->seal.certified_direct_pair_count +
		response->seal.certified_static_impact_pair_count;
	if (response->seal.unresolved_response_pair_count != 0U ||
		response->response_pair_count != expected_count ||
		fact_count != expected_count ||
		(expected_count != 0U && facts == NULL))
		return 0;
	for (index = 0U; index < response->response_pair_count; index++) {
		const sg_rune_compact_response_pair_t *pair =
			&response->response_pairs[index];
		sg_rune_compact_response_pair_t resolved;

		if (!PairValid(response, pair) ||
			(index != 0U && PairCompare(response,
				&response->response_pairs[index - 1U], pair) >= 0) ||
			!SG_RuneCompactResponsePartitionQuery(response,
				pair->source_fragment, pair->target_patch, &resolved) ||
			!PairEqual(pair, &resolved) || !FactMatchesPair(&facts[index], pair))
			return 0;
	}
	return 1;
}

static int MaterializeFacts(const sg_rune_compact_response_partition_view_t *response,
	sg_rune_compact_weapon_relations_t *relations,
	sg_rune_compact_weapon_relations_error_t *error)
{
	uint32_t index;
	uint32_t fact_index = 0U;

	relations->fact_count = response->seal.certified_direct_pair_count +
		response->seal.certified_static_impact_pair_count;
	if (relations->fact_count < response->seal.certified_direct_pair_count ||
		response->seal.unresolved_response_pair_count != 0U ||
		response->response_pair_count != relations->fact_count) {
		SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_RELATION, 0U);
		return 0;
	}
	if (relations->fact_count != 0U) {
		relations->facts = Allocate(relations->fact_count,
			sizeof(*relations->facts));
		if (relations->facts == NULL) {
			SetError(error,
				SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_OUT_OF_MEMORY,
				SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_RELATION, 0U);
			return 0;
		}
	}
	for (index = 0U; index < response->response_pair_count; index++) {
		const sg_rune_compact_response_pair_t *pair =
			&response->response_pairs[index];
		sg_rune_compact_response_pair_t resolved;
		sg_rune_compact_response_fact_t *fact;

		if (!PairValid(response, pair) ||
			(index != 0U && PairCompare(response,
				&response->response_pairs[index - 1U], pair) >= 0) ||
			!SG_RuneCompactResponsePartitionQuery(response,
				pair->source_fragment, pair->target_patch, &resolved) ||
			resolved.source_fragment != pair->source_fragment ||
			resolved.target_patch != pair->target_patch ||
			resolved.certificate != pair->certificate ||
			!PairEqual(pair, &resolved) || !PairValid(response, &resolved) ||
			fact_index >= relations->fact_count) {
			SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE,
				SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_RELATION, index);
			return 0;
		}
		fact = &relations->facts[fact_index++];
		memset(fact, 0, sizeof(*fact));
		fact->source_fragment = resolved.source_fragment;
		fact->target_patch = resolved.target_patch;
		fact->flags = resolved.relation_flags;
		fact->visibility =
			(sg_rune_compact_static_visibility_class_t)resolved.classification;
		fact->visibility_reason =
			(sg_rune_compact_static_visibility_reason_t)resolved.reason;
		fact->requires_exact_ray = (uint8_t)resolved.requires_exact_ray;
		fact->requires_area_state = (uint8_t)resolved.requires_area_state;
		fact->certificate_split = resolved.certificate_split;
		fact->target_witness = resolved.target_witness;
		fact->trace = resolved.trace;
		if (resolved.certificate ==
			SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT) {
			fact->occluders.first = resolved.first_hit_occluder;
			fact->occluders.count = 1U;
		}
	}
	if (fact_index != relations->fact_count || !ResponsePolicyMatches(response,
		relations->facts, relations->fact_count)) {
		SetError(error, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_RELATION, fact_index);
		return 0;
	}
	return 1;
}

static uint32_t GroupForMember(
	const sg_rune_compact_response_endpoint_group_t *groups,
	uint32_t group_count, const uint32_t *members, uint32_t member)
{
	uint32_t group;

	for (group = 0U; group < group_count; group++) {
		uint32_t low = groups[group].first_member;
		uint32_t high;

		if (groups[group].member_count > UINT32_MAX - low)
			return SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		high = low + groups[group].member_count;
		while (low < high) {
			const uint32_t middle = low + (high - low) / 2U;

			if (members[middle] < member)
				low = middle + 1U;
			else
				high = middle;
		}
		if (low < groups[group].first_member + groups[group].member_count &&
			members[low] == member)
			return group;
	}
	return SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
}

int SG_RuneCompactWeaponRelationsBuild(const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_response_partition_t *response,
	sg_rune_compact_weapon_relations_t **relations_out,
	sg_rune_compact_weapon_relations_error_t *error_out)
{
	relation_sources_t initial;
	relation_sources_t current;
	sg_rune_compact_weapon_relations_t *relations = NULL;

	ClearError(error_out);
	if (relations_out == NULL || builder == NULL || geometry == NULL ||
		response == NULL) {
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_OWNER, 0U);
		return 0;
	}
	memset(&initial, 0, sizeof(initial));
	if (!SourcesRead(builder, geometry, response, &initial, error_out) ||
		!SourcesValid(&initial, error_out))
		return 0;
	if (!SG_RuneCompactResponsePartitionRetain(response)) {
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_RESPONSE, 0U);
		return 0;
	}
	relations = Allocate(1U, sizeof(*relations));
	if (relations == NULL) {
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_OWNER, 0U);
		SG_RuneCompactResponsePartitionDestroy(response);
		return 0;
	}
	memset(relations, 0, sizeof(*relations));
	relations->response_owner = response;
	if (!CopyOccluders(&initial, relations, error_out) ||
		!MaterializeFacts(&initial.response, relations, error_out))
		goto failed;
	memset(&current, 0, sizeof(current));
	if (!SourcesRead(builder, geometry, response, &current, error_out) ||
		!SourcesValid(&current, error_out) || !SourcesCurrent(&initial, &current) ||
		!ResponsePolicyMatches(&current.response, relations->facts,
			relations->fact_count)) {
		if (error_out != NULL && error_out->code ==
			SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_NONE)
			SetError(error_out,
				SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE,
				SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_RESPONSE, 0U);
		goto failed;
	}
	relations->identity = initial.builder.identity;
	relations->response_snapshot = initial.response;
	relations->state = SG_RUNE_COMPACT_WEAPON_RELATIONS_STATE;
	relations->state_inverse = ~SG_RUNE_COMPACT_WEAPON_RELATIONS_STATE;
	relations->self = relations;
	*relations_out = relations;
	return 1;

failed:
	SG_RuneCompactWeaponRelationsDestroy(relations);
	return 0;
}

static int RelationsCurrent(const sg_rune_compact_weapon_relations_t *relations,
	sg_rune_compact_response_partition_view_t *response_out)
{
	sg_rune_compact_response_partition_view_t response;

	if (relations == NULL ||
		relations->state != SG_RUNE_COMPACT_WEAPON_RELATIONS_STATE ||
		relations->state_inverse != ~SG_RUNE_COMPACT_WEAPON_RELATIONS_STATE ||
		relations->self != relations ||
		!SG_RuneCompactResponsePartitionRead(relations->response_owner,
			&response) || !SG_RuneCompactResponsePartitionSealValid(&response) ||
		!ResponseSnapshotMatches(&relations->response_snapshot, &response) ||
		!ResponsePolicyMatches(&response, relations->facts, relations->fact_count))
		return 0;
	if (response_out != NULL)
		*response_out = response;
	return 1;
}

int SG_RuneCompactWeaponRelationsRead(
	const sg_rune_compact_weapon_relations_t *relations,
	sg_rune_compact_weapon_relations_view_t *view_out)
{
	sg_rune_compact_response_partition_view_t response;

	if (view_out == NULL || !RelationsCurrent(relations, &response))
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->version = SG_RUNE_COMPACT_WEAPON_RELATIONS_VERSION;
	view_out->identity = relations->identity;
	view_out->owner = relations;
	view_out->response.source_fragments = response.source_fragments;
	view_out->response.source_fragment_count = response.source_fragment_count;
	view_out->response.source_halfspaces = response.source_halfspaces;
	view_out->response.source_halfspace_count = response.source_halfspace_count;
	view_out->response.target_patches = response.target_patches;
	view_out->response.target_patch_count = response.target_patch_count;
	view_out->response.target_vertices = response.target_vertices;
	view_out->response.target_vertex_count = response.target_vertex_count;
	view_out->response.splits = response.splits;
	view_out->response.split_count = response.split_count;
	view_out->response.facts = relations->facts;
	view_out->response.fact_count = relations->fact_count;
	view_out->response.candidate_groups = response.candidate_groups;
	view_out->response.candidate_group_count = response.candidate_group_count;
	view_out->response.source_endpoint_groups = response.source_endpoint_groups;
	view_out->response.source_endpoint_group_count =
		response.source_endpoint_group_count;
	view_out->response.source_endpoint_members = response.source_endpoint_members;
	view_out->response.source_endpoint_member_count =
		response.source_endpoint_member_count;
	view_out->response.target_endpoint_groups = response.target_endpoint_groups;
	view_out->response.target_endpoint_group_count =
		response.target_endpoint_group_count;
	view_out->response.target_endpoint_members = response.target_endpoint_members;
	view_out->response.target_endpoint_member_count =
		response.target_endpoint_member_count;
	view_out->response.occluders = relations->occluders;
	view_out->response.occluder_count = relations->occluder_count;
	view_out->response.seal = response.seal;
	view_out->response.exact_live_prefire_trace_required = 1U;
	return 1;
}

int SG_RuneCompactWeaponRelationsQuery(
	const sg_rune_compact_weapon_relations_view_t *view,
	uint32_t source_fragment, uint32_t target_patch,
	sg_rune_compact_response_fact_t *fact_out)
{
	uint32_t source_group;
	uint32_t target_group;
	uint32_t index;
	sg_rune_compact_response_partition_view_t current;

	if (view == NULL || fact_out == NULL ||
		!RelationsCurrent(view->owner, &current) ||
		!SG_RuneCompactIdentityMatches(&view->identity,
			&view->owner->identity) ||
		view->response.source_fragments != current.source_fragments ||
		view->response.source_fragment_count != current.source_fragment_count ||
		view->response.source_halfspaces != current.source_halfspaces ||
		view->response.source_halfspace_count != current.source_halfspace_count ||
		view->response.target_patches != current.target_patches ||
		view->response.target_patch_count != current.target_patch_count ||
		view->response.target_vertices != current.target_vertices ||
		view->response.target_vertex_count != current.target_vertex_count ||
		view->response.splits != current.splits ||
		view->response.split_count != current.split_count ||
		view->response.facts != view->owner->facts ||
		view->response.fact_count != view->owner->fact_count ||
		view->response.candidate_groups != current.candidate_groups ||
		view->response.candidate_group_count != current.candidate_group_count ||
		view->response.source_endpoint_groups != current.source_endpoint_groups ||
		view->response.source_endpoint_group_count !=
			current.source_endpoint_group_count ||
		view->response.source_endpoint_members != current.source_endpoint_members ||
		view->response.source_endpoint_member_count !=
			current.source_endpoint_member_count ||
		view->response.target_endpoint_groups != current.target_endpoint_groups ||
		view->response.target_endpoint_group_count !=
			current.target_endpoint_group_count ||
		view->response.target_endpoint_members != current.target_endpoint_members ||
		view->response.target_endpoint_member_count !=
			current.target_endpoint_member_count ||
		view->response.occluders != view->owner->occluders ||
		view->response.occluder_count != view->owner->occluder_count ||
		!SealEqual(&view->response.seal, &current.seal) ||
		view->response.exact_live_prefire_trace_required != 1U ||
		source_fragment >= view->response.source_fragment_count ||
		target_patch >= view->response.target_patch_count ||
		(view->response.fact_count != 0U && view->response.facts == NULL) ||
		(view->response.candidate_group_count != 0U &&
		 view->response.candidate_groups == NULL) ||
		view->response.source_endpoint_groups == NULL ||
		view->response.source_endpoint_members == NULL ||
		view->response.target_endpoint_groups == NULL ||
		view->response.target_endpoint_members == NULL)
		return 0;
	for (index = 0U; index < view->response.fact_count; index++)
		if (view->response.facts[index].source_fragment == source_fragment &&
			view->response.facts[index].target_patch == target_patch) {
			*fact_out = view->response.facts[index];
			return 1;
		}
	source_group = GroupForMember(view->response.source_endpoint_groups,
		view->response.source_endpoint_group_count,
		view->response.source_endpoint_members, source_fragment);
	target_group = GroupForMember(view->response.target_endpoint_groups,
		view->response.target_endpoint_group_count,
		view->response.target_endpoint_members, target_patch);
	if (source_group == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
		target_group == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)
		return 0;
	for (index = 0U; index < view->response.candidate_group_count; index++) {
		const sg_rune_compact_response_candidate_group_t *candidate =
			&view->response.candidate_groups[index];

		if (candidate->source_group != source_group ||
			candidate->target_group != target_group)
			continue;
		memset(fact_out, 0, sizeof(*fact_out));
		fact_out->source_fragment = source_fragment;
		fact_out->target_patch = target_patch;
		fact_out->certificate_split = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		fact_out->flags = candidate->relation_flags;
		fact_out->visibility = candidate->classification;
		fact_out->visibility_reason = candidate->reason;
		fact_out->requires_exact_ray = candidate->requires_exact_ray;
		fact_out->requires_area_state = candidate->requires_area_state;
		return 1;
	}
	return 0;
}

void SG_RuneCompactWeaponRelationsDestroy(
	sg_rune_compact_weapon_relations_t *relations)
{
	sg_rune_compact_response_partition_t *response_owner;

	if (relations == NULL || (relations->state != 0U &&
		(relations->state != SG_RUNE_COMPACT_WEAPON_RELATIONS_STATE ||
		 relations->state_inverse != ~SG_RUNE_COMPACT_WEAPON_RELATIONS_STATE ||
		 relations->self != relations)))
		return;
	response_owner = relations->response_owner;
	free(relations->facts);
	free(relations->occluders);
	if (response_owner != NULL)
		SG_RuneCompactResponsePartitionDestroy(response_owner);
	free(relations);
}

const char *SG_RuneCompactWeaponRelationsErrorString(
	sg_rune_compact_weapon_relations_error_code_t code)
{
	static const char *const messages[] = {
		"none", "invalid argument", "builder read failed", "geometry read failed",
		"compact identities differ", "builder source is invalid",
		"compact geometry is invalid", "response partition is invalid",
		"compact relation size overflow", "out of memory"
	};

	return code < SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_CODE_COUNT ?
		messages[code] : "unknown compact weapon relation error";
}
