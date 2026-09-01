#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_rune_compact_builder_owner.h"
#include "slipgate/sg_rune_compact_source_surface_catalog.h"
#include "slipgate/sg_rune_compact_weapon_relations.h"

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define REQUIRE(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
		failures++; \
		return; \
	} \
} while (0)

enum { CELL_COUNT = 3, PATCH_COUNT = 3, VERTEX_COUNT = 9 };

typedef struct fixture_s
{
	sg_rune_compact_identity_t identity;
	sg_rune_compact_builder_view_t builder_view;
	sg_rune_compact_builder_owner_view_t owner_view;
	sg_rune_compact_geometry_view_t geometry_view;
	sg_bsp_world_t world;
	sg_static_visibility_t visibility;
	sg_static_visibility_occluder_t visibility_occluders[1];
	sg_rune_compact_response_fragment_t fragments[CELL_COUNT];
	sg_rune_compact_response_halfspace_t halfspaces[CELL_COUNT];
	sg_rune_compact_response_patch_t patches[PATCH_COUNT];
	sg_rune_q8_vec3_t vertices[VERTEX_COUNT];
	sg_rune_compact_source_surface_t source_surfaces[PATCH_COUNT];
	sg_rune_q8_vec3_t source_surface_vertices[VERTEX_COUNT];
	sg_rune_compact_source_surface_t response_source_surfaces[PATCH_COUNT];
	sg_rune_q8_vec3_t response_source_surface_vertices[VERTEX_COUNT];
	sg_rune_compact_response_split_t splits[1];
	sg_rune_compact_response_pair_t pairs[2];
	sg_rune_compact_response_endpoint_group_t source_groups[CELL_COUNT];
	uint32_t source_members[CELL_COUNT];
	sg_rune_compact_response_endpoint_group_t target_groups[PATCH_COUNT];
	uint32_t target_members[PATCH_COUNT];
	sg_rune_compact_response_candidate_group_t candidates[4];
	sg_rune_compact_response_partition_view_t response;
	int response_seal_ok;
	uint32_t response_build_count;
	uint32_t response_read_count;
	uint32_t revoke_response_on_read;
	uint32_t response_reference_count;
	int response_retain_fail;
	int response_destroyed;
} fixture_t;

static fixture_t *active_fixture;
static int failures;

int SG_RuneCompactBuilderRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_view_t *view_out)
{
	(void)builder;
	if (active_fixture == NULL || view_out == NULL)
		return 0;
	*view_out = active_fixture->builder_view;
	return 1;
}

int SG_RuneCompactBuilderOwnerRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	(void)builder;
	if (active_fixture == NULL || view_out == NULL)
		return 0;
	*view_out = active_fixture->owner_view;
	return 1;
}

int SG_RuneCompactGeometryRead(const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_geometry_view_t *view_out)
{
	(void)geometry;
	if (active_fixture == NULL || view_out == NULL)
		return 0;
	*view_out = active_fixture->geometry_view;
	return 1;
}

int SG_RuneCompactResponsePartitionRead(
	const sg_rune_compact_response_partition_t *partition,
	sg_rune_compact_response_partition_view_t *view_out)
{
	if (active_fixture == NULL || partition !=
		(const sg_rune_compact_response_partition_t *)(uintptr_t)3U ||
		view_out == NULL || active_fixture->response_destroyed ||
		active_fixture->response_reference_count == 0U)
		return 0;
	active_fixture->response_read_count++;
	if (active_fixture->revoke_response_on_read != 0U &&
		active_fixture->response_read_count >=
			active_fixture->revoke_response_on_read)
		return 0;
	*view_out = active_fixture->response;
	return 1;
}

int SG_RuneCompactResponsePartitionRetain(
	sg_rune_compact_response_partition_t *partition)
{
	if (active_fixture == NULL || partition !=
		(sg_rune_compact_response_partition_t *)(uintptr_t)3U ||
		active_fixture->response_destroyed ||
		active_fixture->response_retain_fail ||
		active_fixture->response_reference_count == 0U ||
		active_fixture->response_reference_count == UINT32_MAX)
		return 0;
	active_fixture->response_reference_count++;
	return 1;
}

int SG_RuneCompactResponsePartitionSealValid(
	const sg_rune_compact_response_partition_view_t *view)
{
	return active_fixture != NULL && view != NULL &&
		active_fixture->response_seal_ok &&
		view->seal.source_surface_catalog_seal ==
			SG_RuneCompactSourceSurfaceCatalogSeal(
				view->compact_source_surfaces,
				view->compact_source_surface_count,
				view->compact_source_surface_vertices,
				view->compact_source_surface_vertex_count);
}

int SG_RuneCompactResponsePartitionQuery(
	const sg_rune_compact_response_partition_view_t *view,
	uint32_t source_fragment, uint32_t target_patch,
	sg_rune_compact_response_pair_t *result_out)
{
	uint32_t index;

	if (view == NULL || result_out == NULL ||
		source_fragment >= view->source_fragment_count ||
		target_patch >= view->target_patch_count)
		return 0;
	for (index = 0U; index < view->candidate_group_count; index++)
		if (view->candidate_groups[index].source_group == source_fragment &&
			view->candidate_groups[index].target_group == target_patch)
			break;
	if (index == view->candidate_group_count)
		return 0;
	for (index = 0U; index < view->response_pair_count; index++)
		if (view->response_pairs[index].source_fragment == source_fragment &&
			view->response_pairs[index].target_patch == target_patch) {
			*result_out = view->response_pairs[index];
			return 1;
		}
	memset(result_out, 0, sizeof(*result_out));
	for (index = 0U; index < view->candidate_group_count; index++)
		if (view->candidate_groups[index].source_group == source_fragment &&
			view->candidate_groups[index].target_group == target_patch) {
			const sg_rune_compact_response_candidate_group_t *candidate =
				&view->candidate_groups[index];

			result_out->source_fragment = source_fragment;
			result_out->target_patch = target_patch;
		result_out->classification =
			(sg_static_visibility_class_t)candidate->classification;
		result_out->reason = (sg_static_visibility_reason_t)candidate->reason;
		result_out->first_hit_occluder = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		result_out->requires_exact_ray = candidate->requires_exact_ray;
		result_out->requires_area_state = candidate->requires_area_state;
		result_out->certificate =
			SG_RUNE_COMPACT_RESPONSE_UNRESOLVED_EXACT_RAY;
			result_out->relation_flags = candidate->relation_flags;
			result_out->source_valid_stances =
				view->source_fragments[source_fragment].valid_stances;
			result_out->target_valid_stances =
				view->target_patches[target_patch].valid_stances;
			result_out->certificate_split =
				SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
			return 1;
		}
	return 0;
}

void SG_RuneCompactResponsePartitionDestroy(
	sg_rune_compact_response_partition_t *partition)
{
	if (active_fixture == NULL || partition !=
		(sg_rune_compact_response_partition_t *)(uintptr_t)3U ||
		active_fixture->response_destroyed ||
		active_fixture->response_reference_count == 0U)
		return;
	active_fixture->response_reference_count--;
	if (active_fixture->response_reference_count == 0U)
		active_fixture->response_destroyed = 1;
}

static void InitFixture(fixture_t *fixture)
{
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	fixture->identity.bsp_sha256[0] = 1U;
	fixture->identity.bsp_bytes = 1U;
	fixture->identity.entity_semantics_id = 1U;
	fixture->identity.physics_abi_id = 2U;
	fixture->identity.collision_law_id = 3U;
	fixture->identity.pmove_law_id = 4U;
	fixture->identity.gravity_law_id = 5U;
	fixture->identity.hook_law_id = 6U;
	fixture->identity.mechanism_law_id = 7U;
	fixture->identity.weapon_law_id = 8U;
	fixture->identity.construction_id = 9U;
	fixture->identity.schema_id = 10U;
	fixture->identity.producer_identity = 11U;
	fixture->world.model_count = 1U;
	fixture->world.brush_count = 1U;
	fixture->visibility_occluders[0].model = 0U;
	fixture->visibility_occluders[0].brush = 0U;
	fixture->visibility_occluders[0].contents = 1U;
	fixture->visibility.occluders = fixture->visibility_occluders;
	fixture->visibility.occluder_count = 1U;
	fixture->builder_view.identity = fixture->identity;
	fixture->owner_view.identity = fixture->identity;
	fixture->owner_view.world = &fixture->world;
	fixture->owner_view.visibility = &fixture->visibility;
	fixture->geometry_view.identity = fixture->identity;
	fixture->geometry_view.source_surfaces = fixture->source_surfaces;
	fixture->geometry_view.source_surface_vertices =
		fixture->source_surface_vertices;
	fixture->geometry_view.cell_count = CELL_COUNT;
	fixture->geometry_view.facet_count = PATCH_COUNT;
	fixture->geometry_view.source_surface_count = PATCH_COUNT;
	fixture->geometry_view.source_surface_vertex_count = VERTEX_COUNT;
	for (index = 0U; index < CELL_COUNT; index++) {
		fixture->fragments[index].parent_cell.value = index;
		fixture->fragments[index].boundary_incidences.first = index;
		fixture->fragments[index].boundary_incidences.count = 1U;
		fixture->fragments[index].static_partition_id = UINT64_C(100) + index;
		fixture->fragments[index].configuration_region = index;
		fixture->fragments[index].configuration_cell = index;
		fixture->fragments[index].first_halfspace = index;
		fixture->fragments[index].halfspace_count = 1U;
		fixture->fragments[index].bsp_leaf = index;
		fixture->fragments[index].bsp_area = index;
		fixture->fragments[index].bsp_cluster = index;
		fixture->fragments[index].valid_stances =
			index == 0U ? SG_RUNE_STANCE_VALID_STANDING :
			SG_RUNE_STANCE_VALID_ALL;
		fixture->halfspaces[index].split =
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		fixture->patches[index].visibility_surface_id = UINT64_C(200) + index;
		fixture->patches[index].source_surface = index;
		fixture->patches[index].source_frame =
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
		fixture->patches[index].parent_facet.value = index;
		fixture->patches[index].target_cell.value = index;
		fixture->patches[index].boundary_incidences.first = index;
		fixture->patches[index].boundary_incidences.count = 1U;
		fixture->patches[index].static_partition_id = UINT64_C(300) + index;
		fixture->patches[index].configuration_region = index;
		fixture->patches[index].configuration_cell = index;
		fixture->patches[index].first_vertex = index * 3U;
		fixture->patches[index].vertex_count = 3U;
		fixture->patches[index].bsp_leaf = index;
		fixture->patches[index].bsp_area = index == 0U ? 1U : index;
		fixture->patches[index].bsp_cluster = index;
		fixture->patches[index].valid_stances =
			index == 1U ? SG_RUNE_STANCE_VALID_CROUCHING :
			SG_RUNE_STANCE_VALID_ALL;
		fixture->source_groups[index].bsp_cluster = index;
		fixture->source_groups[index].bsp_area = index;
		fixture->source_groups[index].first_member = index;
		fixture->source_groups[index].member_count = 1U;
		fixture->source_members[index] = index;
		fixture->target_groups[index].bsp_cluster = index;
		fixture->target_groups[index].bsp_area =
			fixture->patches[index].bsp_area;
		fixture->target_groups[index].first_member = index;
		fixture->target_groups[index].member_count = 1U;
		fixture->target_members[index] = index;
		fixture->source_surfaces[index].source.model = 0U;
		fixture->source_surfaces[index].source.brush = 0U;
		fixture->source_surfaces[index].source.brush_side = index;
		fixture->source_surfaces[index].source.plane = index;
		fixture->source_surfaces[index].frame =
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
		fixture->source_surfaces[index].cell.value = index;
		fixture->source_surfaces[index].parent_surface = UINT32_MAX;
		fixture->source_surfaces[index].vertices.first = index * 3U;
		fixture->source_surfaces[index].vertices.count = 3U;
	}
	fixture->pairs[0].source_fragment = 0U;
	fixture->pairs[0].target_patch = 1U;
	fixture->pairs[0].classification = SG_STATIC_VISIBILITY_CONDITIONAL;
	fixture->pairs[0].reason =
		SG_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	fixture->pairs[0].first_hit_occluder = 0U;
	fixture->pairs[0].certificate =
		SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT;
	fixture->pairs[0].relation_flags =
		SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT |
		SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING;
	fixture->pairs[0].requires_exact_ray = 1U;
	fixture->pairs[0].requires_area_state = 1U;
	fixture->pairs[0].certificate_split =
		0U;
	fixture->pairs[0].trace.fraction = 0.5f;
	fixture->pairs[0].trace.brush = 0U;
	fixture->pairs[0].trace.brush_side = 2U;
	fixture->pairs[0].source_valid_stances =
		fixture->fragments[0].valid_stances;
	fixture->pairs[0].target_valid_stances = fixture->patches[1].valid_stances;
	fixture->splits[0].kind = SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE;
	fixture->splits[0].occluder = 0U;
	fixture->pairs[1].source_fragment = 1U;
	fixture->pairs[1].target_patch = 2U;
	fixture->pairs[1].classification = SG_STATIC_VISIBILITY_CONDITIONAL;
	fixture->pairs[1].reason =
		SG_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	fixture->pairs[1].first_hit_occluder =
		SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	fixture->pairs[1].certificate = SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT;
	fixture->pairs[1].relation_flags = SG_RUNE_COMPACT_STATIC_RELATION_DIRECT |
		SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING;
	fixture->pairs[1].requires_exact_ray = 1U;
	fixture->pairs[1].requires_area_state = 1U;
	fixture->pairs[1].certificate_split =
		SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	fixture->pairs[1].trace.fraction = 1.0f;
	fixture->pairs[1].trace.brush = SG_HOST_COLLISION_BRUSH_NONE;
	fixture->pairs[1].trace.brush_side = SG_HOST_COLLISION_BRUSH_NONE;
	fixture->pairs[1].source_valid_stances =
		fixture->fragments[1].valid_stances;
	fixture->pairs[1].target_valid_stances = fixture->patches[2].valid_stances;
	fixture->candidates[0].source_group = 0U;
	fixture->candidates[0].target_group = 0U;
	fixture->candidates[0].classification =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL;
	fixture->candidates[0].reason =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	fixture->candidates[0].requires_exact_ray = 1U;
	fixture->candidates[0].requires_area_state = 1U;
	fixture->candidates[0].relation_flags =
		SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING;
	fixture->candidates[1] = fixture->candidates[0];
	fixture->candidates[1].target_group = 1U;
	fixture->candidates[2] = fixture->candidates[0];
	fixture->candidates[2].source_group = 1U;
	fixture->candidates[2].target_group = 2U;
	fixture->candidates[3].source_group = 2U;
	fixture->candidates[3].target_group = 2U;
	fixture->candidates[3].classification =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL;
	fixture->candidates[3].reason =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL;
	fixture->candidates[3].requires_exact_ray = 1U;
	fixture->response.identity = fixture->identity;
	memcpy(fixture->response_source_surfaces, fixture->source_surfaces,
		sizeof(fixture->response_source_surfaces));
	memcpy(fixture->response_source_surface_vertices,
		fixture->source_surface_vertices,
		sizeof(fixture->response_source_surface_vertices));
	fixture->response.source_fragments = fixture->fragments;
	fixture->response.source_fragment_count = CELL_COUNT;
	fixture->response.source_halfspaces = fixture->halfspaces;
	fixture->response.source_halfspace_count = CELL_COUNT;
	fixture->response.target_patches = fixture->patches;
	fixture->response.target_patch_count = PATCH_COUNT;
	fixture->response.target_vertices = fixture->vertices;
	fixture->response.target_vertex_count = VERTEX_COUNT;
	fixture->response.splits = fixture->splits;
	fixture->response.split_count = 1U;
	fixture->response.response_pairs = fixture->pairs;
	fixture->response.response_pair_count = 2U;
	fixture->response.candidate_groups = fixture->candidates;
	fixture->response.candidate_group_count = 4U;
	fixture->response.source_endpoint_groups = fixture->source_groups;
	fixture->response.source_endpoint_group_count = CELL_COUNT;
	fixture->response.source_endpoint_members = fixture->source_members;
	fixture->response.source_endpoint_member_count = CELL_COUNT;
	fixture->response.target_endpoint_groups = fixture->target_groups;
	fixture->response.target_endpoint_group_count = PATCH_COUNT;
	fixture->response.target_endpoint_members = fixture->target_members;
	fixture->response.target_endpoint_member_count = PATCH_COUNT;
	fixture->response.static_occluder_count = 1U;
	fixture->response.compact_cell_count = CELL_COUNT;
	fixture->response.compact_facet_count = PATCH_COUNT;
	fixture->response.compact_source_surface_count = PATCH_COUNT;
	fixture->response.compact_source_surfaces = fixture->response_source_surfaces;
	fixture->response.compact_source_surface_vertex_count = VERTEX_COUNT;
	fixture->response.compact_source_surface_vertices =
		fixture->response_source_surface_vertices;
	fixture->response.seal.version = SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION;
	fixture->response.seal.certified_direct_pair_count = 1U;
	fixture->response.seal.certified_static_impact_pair_count = 1U;
	fixture->response.seal.unresolved_candidate_group_count = 4U;
	fixture->response.seal.compact_source_surface_count = PATCH_COUNT;
	fixture->response.seal.compact_source_surface_vertex_count = VERTEX_COUNT;
	fixture->response.seal.source_surface_catalog_seal =
		SG_RuneCompactSourceSurfaceCatalogSeal(fixture->response_source_surfaces,
			PATCH_COUNT, fixture->response_source_surface_vertices, VERTEX_COUNT);
	fixture->response_seal_ok = 1;
	fixture->response_build_count = 1U;
	fixture->response_reference_count = 1U;
	active_fixture = fixture;
}

static int Build(fixture_t *fixture,
	sg_rune_compact_weapon_relations_t **relations,
	sg_rune_compact_weapon_relations_error_t *error)
{
	active_fixture = fixture;
	return SG_RuneCompactWeaponRelationsBuild(
		(const sg_rune_compact_builder_t *)(uintptr_t)1U,
		(const sg_rune_compact_geometry_t *)(uintptr_t)2U,
		(sg_rune_compact_response_partition_t *)(uintptr_t)3U,
		relations, error);
}

static void TestExceptionsAndOwnership(void)
{
	fixture_t fixture;
	sg_rune_compact_weapon_relations_t *relations = NULL;
	sg_rune_compact_weapon_relations_error_t error;
	sg_rune_compact_weapon_relations_view_t view;
	sg_rune_compact_response_fact_t fact;

	InitFixture(&fixture);
	CHECK(fixture.response.compact_source_surfaces !=
		fixture.geometry_view.source_surfaces);
	CHECK(fixture.response.compact_source_surface_vertices !=
		fixture.geometry_view.source_surface_vertices);
	REQUIRE(Build(&fixture, &relations, &error));
	REQUIRE(SG_RuneCompactWeaponRelationsRead(relations, &view));
	CHECK(view.version == SG_RUNE_COMPACT_WEAPON_RELATIONS_VERSION);
	CHECK(SG_RuneCompactIdentityMatches(&view.identity, &fixture.identity));
	CHECK(view.owner == relations);
	CHECK(view.response.fact_count == 2U);
	CHECK(view.response.candidate_group_count == 4U);
	CHECK(view.response.occluder_count == 1U);
	CHECK(view.response.facts[0].source_fragment == 0U);
	CHECK(view.response.facts[0].target_patch == 1U);
	CHECK(view.response.facts[0].occluders.first == 0U);
	CHECK(view.response.facts[0].occluders.count == 1U);
	CHECK(view.response.facts[0].certificate_split == 0U);
	CHECK(view.response.facts[0].trace.fraction == 0.5f);
	CHECK(view.response.facts[0].trace.brush == 0U);
	CHECK(view.response.facts[0].trace.brush_side == 2U);
	CHECK(view.response.facts[1].source_fragment == 1U);
	CHECK(view.response.facts[1].target_patch == 2U);
	CHECK(view.response.facts[1].occluders.count == 0U);
	CHECK(view.response.facts[1].trace.fraction == 1.0f);
	fixture.fragments[0].parent_cell.value = UINT32_MAX;
	fixture.visibility_occluders[0].brush = UINT32_MAX;
	CHECK(view.response.source_fragments == fixture.fragments);
	CHECK(view.response.source_halfspaces == fixture.halfspaces);
	CHECK(view.response.target_patches == fixture.patches);
	CHECK(view.response.target_vertices == fixture.vertices);
	CHECK(view.response.splits == fixture.splits);
	CHECK(view.response.candidate_groups == fixture.candidates);
	CHECK(view.response.source_endpoint_groups == fixture.source_groups);
	CHECK(view.response.source_endpoint_members == fixture.source_members);
	CHECK(view.response.target_endpoint_groups == fixture.target_groups);
	CHECK(view.response.target_endpoint_members == fixture.target_members);
	CHECK(view.response.seal.source_surface_catalog_seal ==
		fixture.response.seal.source_surface_catalog_seal);
	CHECK(view.response.seal.compact_source_surface_count == PATCH_COUNT);
	CHECK(view.response.seal.compact_source_surface_vertex_count == VERTEX_COUNT);
	CHECK(fixture.response_build_count == 1U);
	CHECK(fixture.response_reference_count == 2U);
	CHECK(view.response.source_fragments[0].parent_cell.value == UINT32_MAX);
	CHECK(view.response.occluders[0].brush == 0U);
	fixture.fragments[0].parent_cell.value = 0U;
	fixture.visibility_occluders[0].brush = 0U;
	REQUIRE(SG_RuneCompactWeaponRelationsQuery(&view, 0U, 1U, &fact));
	CHECK(fact.flags == (SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT |
		SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING));
	CHECK(fact.visibility == SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL);
	CHECK(fact.visibility_reason ==
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED);
	CHECK(fact.requires_exact_ray == 1U);
	CHECK(fact.requires_area_state == 1U);
	REQUIRE(SG_RuneCompactWeaponRelationsQuery(&view, 1U, 2U, &fact));
	CHECK(fact.flags == (SG_RUNE_COMPACT_STATIC_RELATION_DIRECT |
		SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING));
	CHECK(fact.requires_exact_ray == 1U);
	CHECK(fact.requires_area_state == 1U);
	SG_RuneCompactWeaponRelationsDestroy(relations);
	CHECK(fixture.response_reference_count == 1U);
}

static void TestResponseLifetimeRetained(void)
{
	fixture_t fixture;
	sg_rune_compact_weapon_relations_t *relations = NULL;
	sg_rune_compact_weapon_relations_error_t error;
	sg_rune_compact_weapon_relations_view_t view;
	sg_rune_compact_response_fact_t fact;

	InitFixture(&fixture);
	REQUIRE(Build(&fixture, &relations, &error));
	CHECK(fixture.response_reference_count == 2U);
	SG_RuneCompactResponsePartitionDestroy(
		(sg_rune_compact_response_partition_t *)(uintptr_t)3U);
	CHECK(fixture.response_reference_count == 1U);
	CHECK(!fixture.response_destroyed);
	REQUIRE(SG_RuneCompactWeaponRelationsRead(relations, &view));
	REQUIRE(SG_RuneCompactWeaponRelationsQuery(&view, 1U, 2U, &fact));
	SG_RuneCompactWeaponRelationsDestroy(relations);
	CHECK(fixture.response_reference_count == 0U);
	CHECK(fixture.response_destroyed);
}

static void TestResponsePolicyDriftFailsClosed(void)
{
	fixture_t fixture;
	sg_rune_compact_weapon_relations_t *relations = NULL;
	sg_rune_compact_weapon_relations_error_t error;
	sg_rune_compact_weapon_relations_view_t view;
	sg_rune_compact_weapon_relations_view_t rejected_view;
	sg_rune_compact_weapon_relations_view_t preserved_view;
	sg_rune_compact_response_fact_t fact;
	sg_rune_compact_response_fact_t preserved_fact;

	InitFixture(&fixture);
	REQUIRE(Build(&fixture, &relations, &error));
	REQUIRE(SG_RuneCompactWeaponRelationsRead(relations, &view));
	fixture.pairs[1].trace.fraction = 0.5f;
	memset(&rejected_view, 0xa5, sizeof(rejected_view));
	preserved_view = rejected_view;
	CHECK(!SG_RuneCompactWeaponRelationsRead(relations, &rejected_view));
	CHECK(memcmp(&rejected_view, &preserved_view, sizeof(rejected_view)) == 0);
	memset(&fact, 0xa5, sizeof(fact));
	preserved_fact = fact;
	CHECK(!SG_RuneCompactWeaponRelationsQuery(&view, 1U, 2U, &fact));
	CHECK(memcmp(&fact, &preserved_fact, sizeof(fact)) == 0);
	SG_RuneCompactWeaponRelationsDestroy(relations);
	relations = NULL;
	InitFixture(&fixture);
	REQUIRE(Build(&fixture, &relations, &error));
	REQUIRE(SG_RuneCompactWeaponRelationsRead(relations, &view));
	fixture.pairs[0].trace.brush_side++;
	CHECK(!SG_RuneCompactWeaponRelationsRead(relations, &rejected_view));
	SG_RuneCompactWeaponRelationsDestroy(relations);
}

static void TestResponseRevocationIsAtomic(void)
{
	fixture_t fixture;
	sg_rune_compact_weapon_relations_t *relations =
		(sg_rune_compact_weapon_relations_t *)(uintptr_t)4U;
	sg_rune_compact_weapon_relations_error_t error;
	sg_rune_compact_weapon_relations_view_t view;
	sg_rune_compact_weapon_relations_view_t preserved;

	InitFixture(&fixture);
	fixture.revoke_response_on_read = 2U;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations ==
		(sg_rune_compact_weapon_relations_t *)(uintptr_t)4U);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	CHECK(fixture.response_build_count == 1U);
	CHECK(fixture.response_reference_count == 1U);

	InitFixture(&fixture);
	fixture.response_retain_fail = 1;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations ==
		(sg_rune_compact_weapon_relations_t *)(uintptr_t)4U);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	CHECK(fixture.response_reference_count == 1U);

	InitFixture(&fixture);
	relations = NULL;
	REQUIRE(Build(&fixture, &relations, &error));
	CHECK(fixture.response_build_count == 1U);
	memset(&view, 0xa5, sizeof(view));
	preserved = view;
	fixture.revoke_response_on_read = fixture.response_read_count + 1U;
	CHECK(!SG_RuneCompactWeaponRelationsRead(relations, &view));
	CHECK(memcmp(&view, &preserved, sizeof(view)) == 0);
	SG_RuneCompactWeaponRelationsDestroy(relations);
}

static void TestConditionalGroupsAndStances(void)
{
	fixture_t fixture;
	sg_rune_compact_weapon_relations_t *relations = NULL;
	sg_rune_compact_weapon_relations_error_t error;
	sg_rune_compact_weapon_relations_view_t view;
	sg_rune_compact_response_fact_t fact;

	InitFixture(&fixture);
	REQUIRE(Build(&fixture, &relations, &error));
	REQUIRE(SG_RuneCompactWeaponRelationsRead(relations, &view));
	CHECK(view.response.source_fragments[0].valid_stances ==
		SG_RUNE_STANCE_VALID_STANDING);
	CHECK(view.response.target_patches[1].valid_stances ==
		SG_RUNE_STANCE_VALID_CROUCHING);
	REQUIRE(SG_RuneCompactWeaponRelationsQuery(&view, 0U, 0U, &fact));
	CHECK(fact.visibility == SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL);
	CHECK(fact.visibility_reason ==
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED);
	CHECK(fact.requires_exact_ray == 1U);
	CHECK(fact.requires_area_state == 1U);
	CHECK(fact.flags == SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING);
	CHECK(fact.occluders.count == 0U);
	CHECK(fact.certificate_split == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE);
	REQUIRE(SG_RuneCompactWeaponRelationsQuery(&view, 2U, 2U, &fact));
	CHECK(fact.visibility_reason ==
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL);
	CHECK(fact.requires_exact_ray == 1U);
	CHECK(fact.requires_area_state == 0U);
	CHECK(view.response.fact_count == 2U);
	SG_RuneCompactWeaponRelationsDestroy(relations);
}

static void TestBoundaryRejections(void)
{
	fixture_t fixture;
	sg_rune_compact_weapon_relations_t *relations = NULL;
	sg_rune_compact_weapon_relations_error_t error;
	sg_rune_compact_response_pair_t pair;

	InitFixture(&fixture);
	fixture.geometry_view.identity.bsp_sha256[0] = 2U;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_IDENTITY_MISMATCH);
	InitFixture(&fixture);
	fixture.response_seal_ok = 0;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	InitFixture(&fixture);
	fixture.response.compact_source_surface_count--;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	InitFixture(&fixture);
	fixture.response_source_surfaces[0].source.plane = 1U;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	InitFixture(&fixture);
	fixture.response.compact_source_surfaces = NULL;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	InitFixture(&fixture);
	fixture.revoke_response_on_read = 1U;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	InitFixture(&fixture);
	fixture.pairs[1].source_fragment = UINT32_MAX;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	InitFixture(&fixture);
	fixture.pairs[0].certificate_split =
		SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	InitFixture(&fixture);
	fixture.fragments[1].parent_cell.value =
		fixture.fragments[0].parent_cell.value;
	fixture.patches[2].target_cell.value = fixture.patches[1].target_cell.value;
	fixture.fragments[0].static_partition_id = UINT64_C(102);
	fixture.fragments[1].static_partition_id = UINT64_C(101);
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	InitFixture(&fixture);
	pair = fixture.pairs[0];
	fixture.pairs[0] = fixture.pairs[1];
	fixture.pairs[1] = pair;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
	InitFixture(&fixture);
	fixture.pairs[0].target_valid_stances = SG_RUNE_STANCE_VALID_ALL;
	CHECK(!Build(&fixture, &relations, &error));
	CHECK(relations == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE);
}

static void TestAtomicAllocation(void)
{
	fixture_t fixture;
	sg_rune_compact_weapon_relations_t *relations = NULL;
	sg_rune_compact_weapon_relations_error_t error;
	uint32_t allocation_count;
	uint32_t allocation;

	InitFixture(&fixture);
	SG_RuneCompactWeaponRelationsTestFailAfter(UINT32_MAX);
	REQUIRE(Build(&fixture, &relations, &error));
	allocation_count = SG_RuneCompactWeaponRelationsTestAllocationCount();
	SG_RuneCompactWeaponRelationsDestroy(relations);
	relations = NULL;
	for (allocation = 0U; allocation < allocation_count; allocation++) {
		InitFixture(&fixture);
		SG_RuneCompactWeaponRelationsTestFailAfter(allocation);
		CHECK(!Build(&fixture, &relations, &error));
		CHECK(relations == NULL);
		CHECK(error.code == SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_OUT_OF_MEMORY);
	}
	SG_RuneCompactWeaponRelationsTestFailAfter(UINT32_MAX);
}

int main(void)
{
	TestExceptionsAndOwnership();
	TestResponseLifetimeRetained();
	TestResponseRevocationIsAtomic();
	TestResponsePolicyDriftFailsClosed();
	TestConditionalGroupsAndStances();
	TestBoundaryRejections();
	TestAtomicAllocation();
	if (failures != 0)
		return 1;
	puts("compact weapon relations checks passed");
	return 0;
}
