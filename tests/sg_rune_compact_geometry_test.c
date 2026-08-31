#include "slipgate/sg_rune_compact_geometry_owner.h"
#include "slipgate/sg_rune_compact_localize.h"

#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
	fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, \
		#expression); return 0; } } while (0)

typedef struct fixture_s
{
	sg_configuration_space_t configuration;
	sg_configuration_cell_t cells[8];
	sg_configuration_face_t faces[96];
	sg_rune_vec3_t vertices[320];
	sg_configuration_stance_overlap_t overlaps[4];
	sg_configuration_portal_t portals[4];
	sg_bsp_world_t world;
	sg_bsp_model_t model;
	sg_bsp_leaf_t leaf;
	sg_bsp_area_t area;
	sg_bsp_plane_t plane;
	sg_rune_compact_identity_t identity;
	uint32_t fail_after;
	uint32_t allocations;
	uint32_t live_allocations;
} fixture_t;

static const fixture_t *public_builder_fixture;
static const sg_rune_compact_builder_t *public_builder_handle;

static void SetPoint(sg_rune_vec3_t *point, float x, float y, float z)
{
	point->value[0] = x;
	point->value[1] = y;
	point->value[2] = z;
}

static void AddPlane(fixture_t *fixture, float nx, float ny, float nz,
	float distance, uint32_t axis, uint32_t variant,
	sg_configuration_face_kind_t kind)
{
	sg_configuration_face_t *face =
		&fixture->faces[fixture->configuration.face_count++];

	memset(face, 0, sizeof(*face));
	face->plane.normal[0] = nx;
	face->plane.normal[1] = ny;
	face->plane.normal[2] = nz;
	face->plane.distance = distance;
	face->plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
	face->plane.source_index = axis;
	face->plane.source_variant = variant;
	face->kind = kind;
	if (kind == SG_CONFIGURATION_FACE_FACET)
	{
		face->first_vertex = fixture->configuration.vertex_count;
		face->vertex_count = 3U;
		SetPoint(&fixture->vertices[fixture->configuration.vertex_count++], 0, 0, 0);
		SetPoint(&fixture->vertices[fixture->configuration.vertex_count++], 1, 0, 0);
		SetPoint(&fixture->vertices[fixture->configuration.vertex_count++], 0, 1, 0);
	}
}

static uint32_t AddBoxFaces(fixture_t *fixture, float min_x, float min_y,
	float min_z, float max_x, float max_y, float max_z,
	int constraint_max_x)
{
	const uint32_t first = fixture->configuration.face_count;

	AddPlane(fixture, 1, 0, 0, max_x, 0U, 0U,
		constraint_max_x ? SG_CONFIGURATION_FACE_CONSTRAINT_ONLY :
		SG_CONFIGURATION_FACE_FACET);
	AddPlane(fixture, -1, 0, 0, -min_x, 0U, 1U,
		SG_CONFIGURATION_FACE_FACET);
	AddPlane(fixture, 0, 1, 0, max_y, 1U, 0U,
		SG_CONFIGURATION_FACE_FACET);
	AddPlane(fixture, 0, -1, 0, -min_y, 1U, 1U,
		SG_CONFIGURATION_FACE_FACET);
	AddPlane(fixture, 0, 0, 1, max_z, 2U, 0U,
		SG_CONFIGURATION_FACE_FACET);
	AddPlane(fixture, 0, 0, -1, -min_z, 2U, 1U,
		SG_CONFIGURATION_FACE_FACET);
	return first;
}

static uint32_t AddCell(fixture_t *fixture, sg_rune_stance_t stance,
	float min_x, float min_y, float min_z, float max_x, float max_y, float max_z)
{
	const uint32_t index = fixture->configuration.cell_count++;
	sg_configuration_cell_t *cell = &fixture->cells[index];

	memset(cell, 0, sizeof(*cell));
	cell->stance = stance;
	cell->first_face = AddBoxFaces(fixture, min_x, min_y, min_z,
		max_x, max_y, max_z, 0);
	cell->face_count = 6U;
	SetPoint(&cell->bounds.mins, min_x, min_y, min_z);
	SetPoint(&cell->bounds.maxs, max_x, max_y, max_z);
	SetPoint(&cell->interior_witness, (min_x + max_x) * 0.5f,
		(min_y + max_y) * 0.5f, (min_z + max_z) * 0.5f);
	cell->bsp_leaf.index = 0U;
	cell->bsp_area.index = 0U;
	cell->bsp_cluster.index = UINT32_MAX;
	return index;
}

static void AddOverlap(fixture_t *fixture, uint32_t standing,
	uint32_t crouching, float min_x, float min_y, float min_z,
	float max_x, float max_y, float max_z, int constraint_max_x)
{
	sg_configuration_stance_overlap_t *overlap =
		&fixture->overlaps[fixture->configuration.stance_overlap_count++];

	memset(overlap, 0, sizeof(*overlap));
	overlap->standing_cell = standing;
	overlap->crouching_cell = crouching;
	overlap->first_face = AddBoxFaces(fixture, min_x, min_y, min_z,
		max_x, max_y, max_z, constraint_max_x);
	overlap->face_count = 6U;
	SetPoint(&overlap->bounds.mins, min_x, min_y, min_z);
	SetPoint(&overlap->bounds.maxs, max_x, max_y, max_z);
	SetPoint(&overlap->interior_witness, (min_x + max_x) * 0.5f,
		(min_y + max_y) * 0.5f, (min_z + max_z) * 0.5f);
}

static void AddPortal(fixture_t *fixture, uint32_t from_cell,
	uint32_t to_cell, float x, float min_y, float max_y, float min_z,
	float max_z)
{
	sg_configuration_portal_t *portal =
		&fixture->portals[fixture->configuration.portal_count++];

	memset(portal, 0, sizeof(*portal));
	portal->from_cell = from_cell;
	portal->to_cell = to_cell;
	portal->stance = fixture->cells[from_cell].stance;
	portal->plane.normal[0] = 1.0f;
	portal->plane.distance = x;
	portal->plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
	portal->plane.source_index = 0U;
	portal->first_vertex = fixture->configuration.vertex_count;
	portal->vertex_count = 4U;
	SetPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		x, min_y, min_z);
	SetPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		x, max_y, min_z);
	SetPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		x, max_y, max_z);
	SetPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		x, min_y, max_z);
	portal->clearance = 2.0f;
}

static void InitFixture(fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->configuration.cells = fixture->cells;
	fixture->configuration.faces = fixture->faces;
	fixture->configuration.vertices = fixture->vertices;
	fixture->configuration.stance_overlaps = fixture->overlaps;
	fixture->configuration.portals = fixture->portals;
	fixture->world.models = &fixture->model;
	fixture->world.model_count = 1U;
	fixture->world.leaves = &fixture->leaf;
	fixture->world.leaf_count = 1U;
	fixture->world.areas = &fixture->area;
	fixture->world.area_count = 1U;
	fixture->world.planes = &fixture->plane;
	fixture->world.plane_count = 1U;
	fixture->leaf.cluster = -1;
	fixture->plane.normal.value[0] = 1.0f;
	fixture->identity.source_counts.model_count = 1U;
	fixture->identity.source_counts.leaf_count = 1U;
	fixture->identity.source_counts.area_count = 1U;
	fixture->identity.source_counts.plane_count = 1U;
}

static int Materialize(fixture_t *fixture, sg_rune_compact_geometry_t **geometry,
	sg_rune_compact_geometry_error_t *error)
{
	return SG_RuneCompactGeometryOwnerMaterialize(&fixture->configuration,
		&fixture->world, &fixture->identity, NULL, geometry, error);
}

static int TestThreeAtomOverlap(void)
{
	fixture_t fixture;
	sg_rune_compact_geometry_t *geometry = NULL;
	sg_rune_compact_geometry_view_t view;
	sg_rune_compact_geometry_error_t error;
	uint32_t index;
	uint32_t stance_counts[4] = { 0U, 0U, 0U, 0U };
	int found_constraint = 0;
	int found_flipped_constraint = 0;
	int found_shared_x4 = 0;
	int found_shared_x6 = 0;

	InitFixture(&fixture);
	AddCell(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 0, 10, 4, 4);
	AddCell(&fixture, SG_RUNE_STANCE_CROUCHING, 4, 0, 0, 6, 4, 4);
	AddOverlap(&fixture, 0U, 1U, 4, 0, 0, 6, 4, 4, 1);
	CHECK(Materialize(&fixture, &geometry, &error));
	CHECK(SG_RuneCompactGeometryRead(geometry, &view));
	CHECK(view.cell_count == 3U);
	CHECK(view.compact_cells_for_configuration_cell_count == 2U);
	CHECK(view.compact_cells_for_configuration_cell[0].count == 3U);
	CHECK(view.compact_cells_for_configuration_cell[1].count == 1U);
	for (index = 0U; index < view.cell_count; index++)
		stance_counts[view.cells[index].valid_stances]++;
	CHECK(stance_counts[SG_RUNE_STANCE_VALID_STANDING] == 2U);
	CHECK(stance_counts[SG_RUNE_STANCE_VALID_ALL] == 1U);
	for (index = 0U; index < view.facet_count; index++)
		if (view.facets[index].kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY)
		{
			float normal_x;

			found_constraint = 1;
			CHECK(view.facets[index].vertices.count == 0U);
			CHECK(view.facets[index].incidences.count == 1U);
			CHECK(view.facets[index].portal.value == SG_RUNE_COMPACT_INDEX_NONE);
			CHECK(view.facets[index].source.kind ==
				SG_RUNE_COMPACT_SOURCE_DOMAIN);
			CHECK(view.facets[index].source.value.domain.axis == 0U);
			memcpy(&normal_x, &view.facets[index].plane.normal_bits[0],
				sizeof(normal_x));
			if (normal_x < 0.0f)
			{
				found_flipped_constraint = 1;
				CHECK(view.facets[index].source.value.domain.maximum_side == 0U);
			}
			else
				CHECK(view.facets[index].source.value.domain.maximum_side == 1U);
		}
	CHECK(found_constraint);
	CHECK(found_flipped_constraint);
	CHECK(view.portal_count == 2U);
	for (index = 0U; index < view.portal_count; index++)
	{
		const sg_rune_compact_portal_t *portal = &view.portals[index];
		const sg_rune_compact_facet_t *facet = &view.facets[portal->facet.value];
		float normal_x;
		float distance;

		memcpy(&normal_x, &facet->plane.normal_bits[0], sizeof(normal_x));
		memcpy(&distance, &facet->plane.distance_bits, sizeof(distance));
		CHECK(normal_x == 1.0f);
		CHECK(portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH);
		CHECK(portal->clearance_q8 == 32U);
		if (distance == 4.0f)
			found_shared_x4 = 1;
		else if (distance == 6.0f)
			found_shared_x6 = 1;
		else
			CHECK(0);
	}
	CHECK(found_shared_x4);
	CHECK(found_shared_x6);
	{
		sg_rune_compact_model_t model;
		sg_rune_compact_location_t location;
		sg_rune_q8_vec3_t point = { { 32, 8, 8 } };
		uint32_t boundary_index;
		uint32_t containing = 0U;

		memset(&model, 0, sizeof(model));
		model.cells = view.cells;
		model.cell_count = view.cell_count;
		model.facets = view.facets;
		model.facet_count = view.facet_count;
		model.incidences = view.incidences;
		model.incidence_count = view.incidence_count;
		model.cell_incidences = view.cell_incidences;
		model.cell_incidence_count = view.cell_incidence_count;
		CHECK(SG_RuneCompactLocalize(&model, &point, &location) ==
			SG_RUNE_COMPACT_LOCALIZE_OK);
		CHECK(location.cell.value != SG_RUNE_COMPACT_INDEX_NONE);
		for (boundary_index = 0U; boundary_index < view.cell_count;
			boundary_index++)
		{
			sg_rune_compact_cell_index_t candidate = { boundary_index };
			if (SG_RuneCompactLocalizeIndexed(&model, &point, &candidate, 1U,
				&location) == SG_RUNE_COMPACT_LOCALIZE_OK)
				containing++;
		}
		CHECK(containing == 1U);
		point.value[0] = 48;
		containing = 0U;
		CHECK(SG_RuneCompactLocalize(&model, &point, &location) ==
			SG_RUNE_COMPACT_LOCALIZE_OK);
		CHECK(location.cell.value != SG_RUNE_COMPACT_INDEX_NONE);
		for (boundary_index = 0U; boundary_index < view.cell_count;
			boundary_index++)
		{
			sg_rune_compact_cell_index_t candidate = { boundary_index };
			if (SG_RuneCompactLocalizeIndexed(&model, &point, &candidate, 1U,
				&location) == SG_RUNE_COMPACT_LOCALIZE_OK)
				containing++;
		}
		CHECK(containing == 1U);
	}
	SG_RuneCompactGeometryDestroy(geometry);
	return 1;
}

static int TestTwoDisjointOverlaps(void)
{
	fixture_t fixture;
	sg_rune_compact_geometry_t *geometry = NULL;
	sg_rune_compact_geometry_view_t view;
	sg_rune_compact_geometry_error_t error;
	uint32_t index;
	uint32_t standing_ranges = 0U;
	uint32_t overlap_ranges = 0U;

	InitFixture(&fixture);
	AddCell(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 0, 10, 4, 4);
	AddCell(&fixture, SG_RUNE_STANCE_CROUCHING, 1, 0, 0, 2, 4, 4);
	AddCell(&fixture, SG_RUNE_STANCE_CROUCHING, 8, 0, 0, 9, 4, 4);
	AddOverlap(&fixture, 0U, 1U, 1, 0, 0, 2, 4, 4, 0);
	AddOverlap(&fixture, 0U, 2U, 8, 0, 0, 9, 4, 4, 0);
	CHECK(Materialize(&fixture, &geometry, &error));
	CHECK(SG_RuneCompactGeometryRead(geometry, &view));
	CHECK(view.cell_count == 5U);
	CHECK(view.compact_cells_for_configuration_cell_count == 3U);
	CHECK(view.compact_cells_for_configuration_cell[0].count == 5U);
	CHECK(view.compact_cells_for_configuration_cell[1].count == 1U);
	CHECK(view.compact_cells_for_configuration_cell[2].count == 1U);
	for (index = 0U; index < view.cell_count; index++)
	{
		const sg_rune_compact_cell_t *cell = &view.cells[index];

		if (cell->valid_stances == SG_RUNE_STANCE_VALID_STANDING)
		{
			CHECK((cell->bounds.mins.value[0] == 0 &&
				cell->bounds.maxs.value[0] == 8) ||
				(cell->bounds.mins.value[0] == 16 &&
				cell->bounds.maxs.value[0] == 64) ||
				(cell->bounds.mins.value[0] == 72 &&
				cell->bounds.maxs.value[0] == 80));
			standing_ranges++;
		}
		else if (cell->valid_stances == SG_RUNE_STANCE_VALID_ALL)
		{
			CHECK((cell->bounds.mins.value[0] == 8 &&
				cell->bounds.maxs.value[0] == 16) ||
				(cell->bounds.mins.value[0] == 64 &&
				cell->bounds.maxs.value[0] == 72));
			overlap_ranges++;
		}
		else
			CHECK(0);
	}
	CHECK(standing_ranges == 3U);
	CHECK(overlap_ranges == 2U);
	SG_RuneCompactGeometryDestroy(geometry);
	return 1;
}

static int TestPortalSplitAndDeterminism(void)
{
	fixture_t fixture;
	sg_rune_compact_geometry_t *first = NULL;
	sg_rune_compact_geometry_t *second = NULL;
	sg_rune_compact_geometry_view_t left;
	sg_rune_compact_geometry_view_t right;
	sg_rune_compact_geometry_error_t error;
	uint32_t index;
	uint32_t source_fragments = 0U;
	uint32_t internal_fragments = 0U;

	InitFixture(&fixture);
	AddCell(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 0, 5, 10, 4);
	AddCell(&fixture, SG_RUNE_STANCE_STANDING, 5, 0, 0, 10, 10, 4);
	AddCell(&fixture, SG_RUNE_STANCE_CROUCHING, 4, 0, 0, 5, 5, 4);
	AddOverlap(&fixture, 0U, 2U, 4, 0, 0, 5, 5, 4, 0);
	AddPortal(&fixture, 0U, 1U, 5, 0, 10, 0, 4);
	if (!Materialize(&fixture, &first, &error))
	{
		fprintf(stderr, "portal materialize: %s domain=%u record=%u\n",
			SG_RuneCompactGeometryErrorString(error.code),
			(unsigned)error.domain, error.record);
		return 0;
	}
	CHECK(Materialize(&fixture, &second, &error));
	CHECK(SG_RuneCompactGeometryRead(first, &left));
	CHECK(SG_RuneCompactGeometryRead(second, &right));
	CHECK(left.cell_count == right.cell_count);
	CHECK(left.facet_count == right.facet_count);
	CHECK(left.incidence_count == right.incidence_count);
	CHECK(left.vertex_count == right.vertex_count);
	CHECK(left.portal_count == right.portal_count);
	CHECK(memcmp(left.cells, right.cells,
		(size_t)left.cell_count * sizeof(left.cells[0])) == 0);
	CHECK(memcmp(left.facets, right.facets,
		(size_t)left.facet_count * sizeof(left.facets[0])) == 0);
	CHECK(memcmp(left.incidences, right.incidences,
		(size_t)left.incidence_count * sizeof(left.incidences[0])) == 0);
	CHECK(memcmp(left.vertices, right.vertices,
		(size_t)left.vertex_count * sizeof(left.vertices[0])) == 0);
	CHECK(memcmp(left.portals, right.portals,
		(size_t)left.portal_count * sizeof(left.portals[0])) == 0);
	for (index = 0U; index < left.portal_count; index++)
	{
		const sg_rune_compact_portal_t *portal = &left.portals[index];
		const sg_rune_compact_facet_t *facet = &left.facets[portal->facet.value];
		const sg_rune_compact_incidence_t *negative =
			&left.incidences[portal->negative_incidence.value];
		const sg_rune_compact_incidence_t *positive =
			&left.incidences[portal->positive_incidence.value];
		float normal_x;
		float distance;

		CHECK(facet->incidences.count == 2U);
		CHECK(negative->boundary == SG_RUNE_BOUNDARY_CLOSED);
		CHECK(positive->boundary == SG_RUNE_BOUNDARY_OPEN);
		CHECK(portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH);
		memcpy(&normal_x, &facet->plane.normal_bits[0], sizeof(normal_x));
		memcpy(&distance, &facet->plane.distance_bits, sizeof(distance));
		if (normal_x == 1.0f && distance == 5.0f)
		{
			CHECK(portal->clearance_q8 == 36U);
			source_fragments++;
		}
		else
		{
			CHECK(portal->clearance_q8 != 0U);
			internal_fragments++;
		}
	}
	CHECK(source_fragments == 2U);
	CHECK(internal_fragments != 0U);
	SG_RuneCompactGeometryDestroy(first);
	SG_RuneCompactGeometryDestroy(second);
	return 1;
}

static int TestTwoDisjointPortals(void)
{
	fixture_t fixture;
	sg_rune_compact_geometry_t *geometry = NULL;
	sg_rune_compact_geometry_view_t view;
	sg_rune_compact_geometry_error_t error;
	uint32_t index;
	uint32_t source_portals = 0U;

	InitFixture(&fixture);
	AddCell(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 0, 5, 10, 4);
	AddCell(&fixture, SG_RUNE_STANCE_STANDING, 5, 0, 0, 10, 10, 4);
	AddPortal(&fixture, 0U, 1U, 5, 0, 3, 0, 4);
	AddPortal(&fixture, 0U, 1U, 5, 7, 10, 0, 4);
	CHECK(Materialize(&fixture, &geometry, &error));
	CHECK(SG_RuneCompactGeometryRead(geometry, &view));
	for (index = 0U; index < view.portal_count; index++)
	{
		CHECK(view.portals[index].source.kind ==
			SG_RUNE_COMPACT_SOURCE_DOMAIN);
		CHECK(view.portals[index].source.value.domain.axis == 0U);
		CHECK(view.portals[index].source.value.domain.maximum_side == 1U);
		CHECK(view.portals[index].direction ==
			SG_RUNE_PORTAL_CONTINUITY_BOTH);
		CHECK(view.portals[index].clearance_q8 == 28U);
		source_portals++;
	}
	CHECK(source_portals == 2U);
	CHECK(view.portal_count == 2U);
	SG_RuneCompactGeometryDestroy(geometry);
	return 1;
}

static void *FailAllocate(void *context, size_t bytes)
{
	fixture_t *fixture = (fixture_t *)context;

	if (fixture->allocations++ >= fixture->fail_after)
		return NULL;
	{
		void *allocation = malloc(bytes);
		if (allocation != NULL)
			fixture->live_allocations++;
		return allocation;
	}
}

static void FailRelease(void *context, void *allocation)
{
	fixture_t *fixture = (fixture_t *)context;

	if (allocation != NULL)
		fixture->live_allocations--;
	free(allocation);
}

static int TestOomSentinel(void)
{
	fixture_t fixture;
	sg_rune_compact_geometry_t *sentinel =
		(sg_rune_compact_geometry_t *)(uintptr_t)0x1234U;
	sg_rune_compact_geometry_error_t error;
	sg_rune_compact_geometry_allocator_t allocator;

	InitFixture(&fixture);
	AddCell(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 0, 10, 4, 4);
	allocator.context = &fixture;
	allocator.allocate = FailAllocate;
	allocator.release = FailRelease;
	for (fixture.fail_after = 0U; ; fixture.fail_after++)
	{
		fixture.allocations = 0U;
		fixture.live_allocations = 0U;
		sentinel = (sg_rune_compact_geometry_t *)(uintptr_t)0x1234U;
		if (SG_RuneCompactGeometryOwnerMaterialize(&fixture.configuration,
			&fixture.world, &fixture.identity, &allocator, &sentinel, &error))
		{
			CHECK(sentinel != NULL);
			SG_RuneCompactGeometryDestroy(sentinel);
			CHECK(fixture.live_allocations == 0U);
			break;
		}
		CHECK(sentinel == (sg_rune_compact_geometry_t *)(uintptr_t)0x1234U);
		if (error.code != SG_RUNE_COMPACT_GEOMETRY_ERROR_OUT_OF_MEMORY)
		{
			fprintf(stderr, "fail_at=%u reported %s\n", fixture.fail_after,
				SG_RuneCompactGeometryErrorString(error.code));
			return 0;
		}
		CHECK(fixture.live_allocations == 0U);
		CHECK(fixture.fail_after < 10000U);
	}
	for (fixture.fail_after = 0U; ; fixture.fail_after++)
	{
		fixture.allocations = 0U;
		fixture.live_allocations = 0U;
		sentinel = (sg_rune_compact_geometry_t *)(uintptr_t)0x1234U;
		if (SG_RuneCompactGeometryOwnerMaterialize(&fixture.configuration,
			&fixture.world, &fixture.identity, &allocator, &sentinel, NULL))
		{
			CHECK(sentinel != NULL);
			SG_RuneCompactGeometryDestroy(sentinel);
			CHECK(fixture.live_allocations == 0U);
			break;
		}
		CHECK(sentinel == (sg_rune_compact_geometry_t *)(uintptr_t)0x1234U);
		CHECK(fixture.live_allocations == 0U);
		CHECK(fixture.fail_after < 10000U);
	}
	return 1;
}

static int TestUncoveredPortalFails(void)
{
	fixture_t fixture;
	sg_rune_compact_geometry_t *sentinel =
		(sg_rune_compact_geometry_t *)(uintptr_t)0x9abcU;
	sg_rune_compact_geometry_error_t error;

	InitFixture(&fixture);
	AddCell(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 0, 5, 4, 4);
	AddCell(&fixture, SG_RUNE_STANCE_STANDING, 5, 0, 0, 10, 4, 4);
	AddPortal(&fixture, 0U, 1U, 5, 8, 10, 0, 4);
	CHECK(!Materialize(&fixture, &sentinel, &error));
	CHECK(sentinel == (sg_rune_compact_geometry_t *)(uintptr_t)0x9abcU);
	CHECK(error.code == SG_RUNE_COMPACT_GEOMETRY_ERROR_UNSUPPORTED_TOPOLOGY);
	CHECK(error.domain == SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL);
	CHECK(error.record == 0U);
	return 1;
}

static int TestQ8RoundingModes(void)
{
	const int modes[] = {
		FE_TONEAREST, FE_DOWNWARD, FE_UPWARD, FE_TOWARDZERO
	};
	const int original_mode = fegetround();
	uint32_t mode_index;

	CHECK(original_mode != -1);
	for (mode_index = 0U;
		mode_index < (uint32_t)(sizeof(modes) / sizeof(modes[0]));
		mode_index++)
	{
		fixture_t fixture;
		sg_rune_compact_geometry_t *geometry = NULL;
		sg_rune_compact_geometry_view_t view;
		sg_rune_compact_geometry_error_t error;
		int materialized;

		InitFixture(&fixture);
		AddCell(&fixture, SG_RUNE_STANCE_STANDING,
			-0.1875f, -0.0625f, 0.1875f,
			1.1875f, 1.0625f, 1.3125f);
		CHECK(fesetround(modes[mode_index]) == 0);
		materialized = Materialize(&fixture, &geometry, &error);
		CHECK(fesetround(original_mode) == 0);
		CHECK(materialized);
		CHECK(SG_RuneCompactGeometryRead(geometry, &view));
		CHECK(view.cell_count == 1U);
		CHECK(view.cells[0].bounds.mins.value[0] == -2);
		CHECK(view.cells[0].bounds.maxs.value[0] == 10);
		CHECK(view.cells[0].bounds.mins.value[1] == 0);
		CHECK(view.cells[0].bounds.maxs.value[1] == 8);
		CHECK(view.cells[0].bounds.mins.value[2] == 2);
		CHECK(view.cells[0].bounds.maxs.value[2] == 10);
		SG_RuneCompactGeometryDestroy(geometry);
	}
	return 1;
}

static int TestOverflowSentinel(void)
{
	fixture_t fixture;
	sg_rune_compact_geometry_t *sentinel =
		(sg_rune_compact_geometry_t *)(uintptr_t)0x5678U;
	sg_rune_compact_geometry_error_t error;

	InitFixture(&fixture);
	fixture.configuration.cell_count = SG_RUNE_COMPACT_MAX_CELLS + 1U;
	CHECK(!Materialize(&fixture, &sentinel, &error));
	CHECK(sentinel == (sg_rune_compact_geometry_t *)(uintptr_t)0x5678U);
	CHECK(error.code == SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW);
	return 1;
}

static int TestPublicWrapper(void)
{
	fixture_t fixture;
	sg_rune_compact_geometry_t *geometry = NULL;
	sg_rune_compact_geometry_view_t view;
	sg_rune_compact_geometry_error_t error;
	int materialized;

	InitFixture(&fixture);
	AddCell(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 0, 4, 4, 4);
	public_builder_fixture = &fixture;
	public_builder_handle = (const sg_rune_compact_builder_t *)(const void *)&fixture;
	materialized = SG_RuneCompactGeometryMaterialize(public_builder_handle, NULL,
		&geometry, &error);
	public_builder_fixture = NULL;
	public_builder_handle = NULL;
	CHECK(materialized);
	CHECK(SG_RuneCompactGeometryRead(geometry, &view));
	CHECK(view.cell_count == 1U);
	SG_RuneCompactGeometryDestroy(geometry);
	return 1;
}

int SG_RuneCompactBuilderRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_view_t *view_out)
{
	if (builder != public_builder_handle || public_builder_fixture == NULL ||
		view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = public_builder_fixture->identity;
	return 1;
}
int SG_RuneCompactBuilderOwnerRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	if (builder != public_builder_handle || public_builder_fixture == NULL ||
		view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->world = &public_builder_fixture->world;
	view_out->configuration = &public_builder_fixture->configuration;
	return 1;
}

int main(void)
{
	if (!TestThreeAtomOverlap() || !TestTwoDisjointOverlaps() ||
		!TestPortalSplitAndDeterminism() || !TestOomSentinel() ||
		!TestTwoDisjointPortals() || !TestUncoveredPortalFails() ||
		!TestQ8RoundingModes() || !TestOverflowSentinel() ||
		!TestPublicWrapper())
		return 1;
	puts("sg_rune_compact_geometry_test: PASS");
	return 0;
}
