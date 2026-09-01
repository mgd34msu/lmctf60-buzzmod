#include <stdlib.h>

int sg_rune_compact_model_fixture_main(void);
#define main sg_rune_compact_model_fixture_main
#include "sg_rune_compact_model_test.c"
#undef main

#include "../slipgate/sg_rune_compact_geometry_owner.h"
#include "../slipgate/sg_rune_compact_localize.h"

typedef struct geometry_fixture_s
{
	sg_configuration_space_t configuration;
	sg_configuration_semantics_t semantics;
	sg_configuration_cell_t cells[2];
	sg_configuration_face_t faces[12];
	sg_rune_vec3_t vertices[40];
	sg_configuration_portal_t portal;
	sg_bsp_world_t world;
	sg_bsp_model_t models[2];
	sg_bsp_leaf_t leaves[3];
	sg_bsp_area_t areas[4];
	sg_bsp_plane_t planes[5];
	sg_bsp_brush_t brushes[2];
	sg_bsp_brush_side_t brush_sides[2];
} geometry_fixture_t;

static const geometry_fixture_t *public_builder_source;
static const sg_rune_compact_identity_t *public_builder_identity;
static const sg_rune_compact_builder_t *public_builder_handle;

enum
{
	SOURCE_MODEL_WORLD = 0,
	SOURCE_MODEL_DOOR,
	SOURCE_MODEL_LIFT,
	SOURCE_MODEL_TRAIN,
	SOURCE_MODEL_COUNT,
	SOURCE_BRUSH_SIDES_PER_MODEL = 6,
	SOURCE_PLANE_COUNT = 12,
	SOURCE_BRUSH_SIDE_COUNT =
		SOURCE_MODEL_COUNT * SOURCE_BRUSH_SIDES_PER_MODEL
};

typedef struct source_catalog_fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_plane_t planes[SOURCE_PLANE_COUNT];
	sg_bsp_node_t node;
	sg_bsp_leaf_t leaves[SOURCE_MODEL_COUNT];
	uint32_t leaf_brushes[SOURCE_MODEL_COUNT];
	sg_bsp_model_t models[SOURCE_MODEL_COUNT];
	sg_bsp_brush_t brushes[SOURCE_MODEL_COUNT];
	sg_bsp_brush_side_t brush_sides[SOURCE_BRUSH_SIDE_COUNT];
	sg_bsp_area_t area;
	sg_host_collision_authority_t authority;
	sg_rune_model_identity_t source_identity;
	sg_rune_compact_identity_t compact_identity;
	sg_configuration_space_t configuration;
	sg_configuration_cell_t cell;
	sg_configuration_face_t faces[6];
	sg_rune_vec3_t vertices[24];
} source_catalog_fixture_t;

typedef struct source_catalog_allocation_s
{
	uint32_t fail_after;
	uint32_t calls;
	uint32_t live;
} source_catalog_allocation_t;

static void *SourceCatalogAllocate(void *context, size_t bytes)
{
	source_catalog_allocation_t *state =
		(source_catalog_allocation_t *)context;
	void *allocation;

	if (state->calls++ == state->fail_after)
		return NULL;
	allocation = malloc(bytes);
	if (allocation != NULL)
		state->live++;
	return allocation;
}

static void SourceCatalogRelease(void *context, void *allocation)
{
	source_catalog_allocation_t *state =
		(source_catalog_allocation_t *)context;

	if (allocation != NULL)
	{
		state->live--;
		free(allocation);
	}
}

static void GeometryPoint(sg_rune_vec3_t *point, float x, float y, float z)
{
	point->value[0] = x;
	point->value[1] = y;
	point->value[2] = z;
}

static void SourcePlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	plane->normal.value[0] = x;
	plane->normal.value[1] = y;
	plane->normal.value[2] = z;
	plane->distance = distance;
}

static void SourceFace(source_catalog_fixture_t *fixture, uint32_t face,
	float x, float y, float z, float distance, uint32_t axis,
	uint32_t variant, const float points[4][3])
{
	uint32_t vertex;

	fixture->faces[face].plane.normal[0] = x;
	fixture->faces[face].plane.normal[1] = y;
	fixture->faces[face].plane.normal[2] = z;
	fixture->faces[face].plane.distance = distance;
	(void)axis;
	(void)variant;
	fixture->faces[face].plane.source_kind = SG_CONFIGURATION_PLANE_BSP;
	fixture->faces[face].plane.source_index =
		SOURCE_BRUSH_SIDES_PER_MODEL + face;
	fixture->faces[face].kind = SG_CONFIGURATION_FACE_FACET;
	fixture->faces[face].first_vertex = face * 4U;
	fixture->faces[face].vertex_count = 4U;
	for (vertex = 0U; vertex < 4U; vertex++)
		GeometryPoint(&fixture->vertices[face * 4U + vertex],
			points[vertex][0], points[vertex][1], points[vertex][2]);
}

static void InitSourceCatalogFixture(source_catalog_fixture_t *fixture)
{
	static const float cell_faces[6][4][3] = {
		{{-90,0,0},{-90,8,0},{-90,8,8},{-90,0,8}},
		{{-100,0,0},{-100,0,8},{-100,8,8},{-100,8,0}},
		{{-100,8,0},{-100,8,8},{-90,8,8},{-90,8,0}},
		{{-100,0,0},{-90,0,0},{-90,0,8},{-100,0,8}},
		{{-100,0,8},{-90,0,8},{-90,8,8},{-100,8,8}},
		{{-100,0,0},{-100,8,0},{-90,8,0},{-90,0,0}}
	};
	uint32_t model;
	uint32_t side;
	uint32_t axis;

	memset(fixture, 0, sizeof(*fixture));
	SourcePlane(&fixture->planes[0], 1, 0, 0, 4);
	SourcePlane(&fixture->planes[1], -1, 0, 0, 0);
	SourcePlane(&fixture->planes[2], 0, 1, 0, 4);
	SourcePlane(&fixture->planes[3], 0, -1, 0, 0);
	SourcePlane(&fixture->planes[4], 0, 0, 1, 4);
	SourcePlane(&fixture->planes[5], 0, 0, -1, 0);
	SourcePlane(&fixture->planes[6], 1, 0, 0, -90);
	SourcePlane(&fixture->planes[7], -1, 0, 0, 100);
	SourcePlane(&fixture->planes[8], 0, 1, 0, 8);
	SourcePlane(&fixture->planes[9], 0, -1, 0, 0);
	SourcePlane(&fixture->planes[10], 0, 0, 1, 8);
	SourcePlane(&fixture->planes[11], 0, 0, -1, 0);
	for (model = 0U; model < SOURCE_MODEL_COUNT; model++)
	{
		fixture->models[model].headnode = -(int32_t)(model + 1U);
		fixture->leaves[model].cluster = 0;
		fixture->leaves[model].area = 0U;
		fixture->leaves[model].first_leaf_brush = model;
		fixture->leaves[model].leaf_brush_count = 1U;
		fixture->leaf_brushes[model] = model;
		fixture->brushes[model].first_side =
			model * SOURCE_BRUSH_SIDES_PER_MODEL;
		fixture->brushes[model].side_count = SOURCE_BRUSH_SIDES_PER_MODEL;
		fixture->brushes[model].contents = SG_HOST_CONTENTS_SOLID;
		for (side = 0U; side < SOURCE_BRUSH_SIDES_PER_MODEL; side++)
		{
			fixture->brush_sides[model * SOURCE_BRUSH_SIDES_PER_MODEL + side].plane =
				side;
			fixture->brush_sides[
				model * SOURCE_BRUSH_SIDES_PER_MODEL + side].texinfo = -1;
		}
	}
	fixture->world.models = fixture->models;
	fixture->world.model_count = SOURCE_MODEL_COUNT;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_count = SOURCE_MODEL_COUNT;
	fixture->world.leaf_brushes = fixture->leaf_brushes;
	fixture->world.leaf_brush_count = SOURCE_MODEL_COUNT;
	fixture->world.areas = &fixture->area;
	fixture->world.area_count = 1U;
	fixture->world.planes = fixture->planes;
	fixture->world.plane_count = SOURCE_PLANE_COUNT;
	fixture->node.plane = 0U;
	fixture->node.children[0] = -1;
	fixture->node.children[1] = -1;
	fixture->world.nodes = &fixture->node;
	fixture->world.node_count = 1U;
	fixture->world.brushes = fixture->brushes;
	fixture->world.brush_count = SOURCE_MODEL_COUNT;
	fixture->world.brush_sides = fixture->brush_sides;
	fixture->world.brush_side_count = SOURCE_BRUSH_SIDE_COUNT;
	fixture->source_identity.bsp_content_id = UINT64_C(0x1234);
	fixture->source_identity.entity_semantics_id = UINT64_C(0x5678);
	fixture->source_identity.physics_abi_id = UINT64_C(0x9abc);
	fixture->source_identity.source_set_identity = UINT64_C(0xdef0);
	fixture->source_identity.schema_id = UINT64_C(0x1357);
	fixture->source_identity.producer_identity = UINT64_C(0x2468);
	for (axis = 0U; axis < 3U; axis++)
	{
		fixture->source_identity.standing_hull.mins.value[axis] = -1.0f;
		fixture->source_identity.standing_hull.maxs.value[axis] = 1.0f;
		fixture->source_identity.crouching_hull.mins.value[axis] = -1.0f;
		fixture->source_identity.crouching_hull.maxs.value[axis] = 1.0f;
		fixture->configuration.domain.mins.value[axis] =
			SG_CONFIGURATION_PMOVE_ORIGIN_MIN;
		fixture->configuration.domain.maxs.value[axis] =
			SG_CONFIGURATION_PMOVE_ORIGIN_MAX;
	}
	fixture->source_identity.physics.max_velocity = 2000.0f;
	fixture->source_identity.physics.frame_ms = 100U;
	fixture->source_identity.physics.substep_ms = 10U;
	fixture->configuration.identity = fixture->source_identity;
	fixture->configuration.cells = &fixture->cell;
	fixture->configuration.cell_count = 1U;
	fixture->configuration.faces = fixture->faces;
	fixture->configuration.face_count = 6U;
	fixture->configuration.vertices = fixture->vertices;
	fixture->configuration.vertex_count = 24U;
	fixture->cell.stance = SG_RUNE_STANCE_STANDING;
	fixture->cell.first_face = 0U;
	fixture->cell.face_count = 6U;
	GeometryPoint(&fixture->cell.bounds.mins, -100, 0, 0);
	GeometryPoint(&fixture->cell.bounds.maxs, -90, 8, 8);
	GeometryPoint(&fixture->cell.interior_witness, -95, 4, 4);
	fixture->cell.bsp_leaf.index = 0U;
	fixture->cell.bsp_area.index = 0U;
	fixture->cell.bsp_cluster.index = 0U;
	SourceFace(fixture, 0U, 1, 0, 0, -90, 0U, 0U, cell_faces[0]);
	SourceFace(fixture, 1U, -1, 0, 0, 100, 0U, 1U, cell_faces[1]);
	SourceFace(fixture, 2U, 0, 1, 0, 8, 1U, 0U, cell_faces[2]);
	SourceFace(fixture, 3U, 0, -1, 0, 0, 1U, 1U, cell_faces[3]);
	SourceFace(fixture, 4U, 0, 0, 1, 8, 2U, 0U, cell_faces[4]);
	SourceFace(fixture, 5U, 0, 0, -1, 0, 2U, 1U, cell_faces[5]);
	fixture->compact_identity.source_counts.model_count = SOURCE_MODEL_COUNT;
	fixture->compact_identity.source_counts.leaf_count = SOURCE_MODEL_COUNT;
	fixture->compact_identity.source_counts.area_count = 1U;
	fixture->compact_identity.source_counts.plane_count = SOURCE_PLANE_COUNT;
	fixture->compact_identity.source_counts.brush_count = SOURCE_MODEL_COUNT;
	fixture->compact_identity.source_counts.brush_side_count =
		SOURCE_BRUSH_SIDE_COUNT;
}

static int TestProductionSourceSurfaceCatalog(void)
{
	source_catalog_fixture_t fixture;
	sg_configuration_semantics_limits_t limits;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_t *semantics = NULL;
	sg_host_collision_error_t collision_error;
	sg_rune_compact_geometry_error_t geometry_error;
	sg_rune_compact_geometry_t *geometry = NULL;
	sg_rune_compact_geometry_view_t view;
	sg_rune_compact_source_surface_t expected_surfaces[
		SOURCE_BRUSH_SIDE_COUNT];
	sg_rune_q8_vec3_t expected_source_vertices[
		SOURCE_BRUSH_SIDE_COUNT * 4U];
	uint32_t model_counts[SOURCE_MODEL_COUNT] = { 0U, 0U, 0U, 0U };
	uint32_t index;

	InitSourceCatalogFixture(&fixture);
	CHECK(SG_HostCollisionInit(&fixture.authority, &fixture.world,
		&fixture.source_identity, &collision_error));
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	if (!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &semantics, &semantics_error))
	{
		fprintf(stderr, "source semantics error: code=%d source=%u\n",
			(int)semantics_error.code, semantics_error.source_index);
		CHECK(0);
	}
	if (semantics == NULL)
		return 0;
	CHECK(SG_RuneCompactGeometryOwnerMaterialize(&fixture.configuration,
		semantics, &fixture.world, &fixture.compact_identity, NULL, &geometry,
		&geometry_error));
	if (geometry == NULL)
	{
		SG_ConfigurationSemanticsDestroy(semantics);
		return 0;
	}
	CHECK(SG_RuneCompactGeometryRead(geometry, &view));
	CHECK(view.source_surface_count == SOURCE_BRUSH_SIDE_COUNT);
	CHECK(view.source_surface_vertex_count == SOURCE_BRUSH_SIDE_COUNT * 4U);
	for (index = 0U; index < view.source_surface_count; index++)
	{
		const sg_rune_compact_source_surface_t *surface =
			&view.source_surfaces[index];

		CHECK(surface->source.model < SOURCE_MODEL_COUNT);
		model_counts[surface->source.model]++;
		CHECK(surface->source.brush == surface->source.model);
		CHECK(surface->source.brush_side == index);
		CHECK(surface->source.plane ==
			surface->source.brush_side % SOURCE_BRUSH_SIDES_PER_MODEL);
		CHECK(surface->frame == (surface->source.model == SOURCE_MODEL_WORLD ?
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD :
			SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL));
		CHECK(surface->cell.value == SG_RUNE_COMPACT_INDEX_NONE);
		CHECK(surface->parent_surface == SG_RUNE_COMPACT_INDEX_NONE);
		CHECK(surface->split_ordinal == 0U);
		CHECK(surface->vertices.count == 4U);
	}
	for (index = 0U; index < SOURCE_MODEL_COUNT; index++)
		CHECK(model_counts[index] == SOURCE_BRUSH_SIDES_PER_MODEL);
	CHECK(view.source_surfaces[SOURCE_MODEL_DOOR *
		SOURCE_BRUSH_SIDES_PER_MODEL].source.model == SOURCE_MODEL_DOOR);
	CHECK(view.source_surfaces[SOURCE_MODEL_LIFT *
		SOURCE_BRUSH_SIDES_PER_MODEL].source.model == SOURCE_MODEL_LIFT);
	CHECK(view.source_surfaces[SOURCE_MODEL_TRAIN *
		SOURCE_BRUSH_SIDES_PER_MODEL].source.model == SOURCE_MODEL_TRAIN);
	/* Door, lift, and train roots intentionally share the world's exact plane
	 * geometry.  Their model/brush/side tuples must remain distinct. */
	for (index = 1U; index < SOURCE_MODEL_COUNT; index++)
	{
		const sg_rune_compact_source_surface_t *world =
			&view.source_surfaces[0U];
		const sg_rune_compact_source_surface_t *mover =
			&view.source_surfaces[index * SOURCE_BRUSH_SIDES_PER_MODEL];

		CHECK(memcmp(&world->plane, &mover->plane,
			sizeof(world->plane)) == 0);
		CHECK(world->source.model == SOURCE_MODEL_WORLD);
		CHECK(mover->source.model == index);
		CHECK(world->source.brush != mover->source.brush);
		CHECK(world->source.brush_side != mover->source.brush_side);
	}
	memcpy(expected_surfaces, view.source_surfaces, sizeof(expected_surfaces));
	memcpy(expected_source_vertices, view.source_surface_vertices,
		sizeof(expected_source_vertices));
	SG_RuneCompactGeometryDestroy(geometry);
	geometry = NULL;
	{
		sg_configuration_hook_surface_t first = semantics->hook_surfaces[0];
		sg_configuration_hook_surface_t last = semantics->hook_surfaces[
			SOURCE_BRUSH_SIDE_COUNT - 1U];

		/* Source records and their packed vertex spans must be invariant under
		 * the configuration producer's input order. */
		semantics->hook_surfaces[0] = last;
		semantics->hook_surfaces[SOURCE_BRUSH_SIDE_COUNT - 1U] = first;
		semantics->hook_surfaces[0].id = 0U;
		semantics->hook_surfaces[SOURCE_BRUSH_SIDE_COUNT - 1U].id =
			SOURCE_BRUSH_SIDE_COUNT - 1U;
		CHECK(SG_RuneCompactGeometryOwnerMaterialize(&fixture.configuration,
			semantics, &fixture.world, &fixture.compact_identity, NULL, &geometry,
			&geometry_error));
		CHECK(geometry != NULL);
		CHECK(SG_RuneCompactGeometryRead(geometry, &view));
		CHECK(memcmp(view.source_surfaces, expected_surfaces,
			sizeof(expected_surfaces)) == 0);
		CHECK(memcmp(view.source_surface_vertices, expected_source_vertices,
			sizeof(expected_source_vertices)) == 0);
		SG_RuneCompactGeometryDestroy(geometry);
		geometry = NULL;
		semantics->hook_surfaces[0] = first;
		semantics->hook_surfaces[SOURCE_BRUSH_SIDE_COUNT - 1U] = last;
	}
	{
		source_catalog_allocation_t state;
		sg_rune_compact_geometry_allocator_t allocator;
		uint32_t fail_after;

		memset(&state, 0, sizeof(state));
		allocator.context = &state;
		allocator.allocate = SourceCatalogAllocate;
		allocator.release = SourceCatalogRelease;
		for (fail_after = 0U; fail_after < 10000U; fail_after++)
		{
			sg_rune_compact_geometry_t *candidate =
				(sg_rune_compact_geometry_t *)(uintptr_t)0x1234U;

			state.fail_after = fail_after;
			state.calls = 0U;
			state.live = 0U;
			if (SG_RuneCompactGeometryOwnerMaterialize(&fixture.configuration,
				semantics, &fixture.world, &fixture.compact_identity, &allocator,
				&candidate, &geometry_error))
			{
				CHECK(candidate != NULL && candidate !=
					(sg_rune_compact_geometry_t *)(uintptr_t)0x1234U);
				SG_RuneCompactGeometryDestroy(candidate);
				CHECK(state.live == 0U);
				break;
			}
			CHECK(candidate ==
				(sg_rune_compact_geometry_t *)(uintptr_t)0x1234U);
			CHECK(geometry_error.code ==
				SG_RUNE_COMPACT_GEOMETRY_ERROR_OUT_OF_MEMORY);
			CHECK(state.live == 0U);
		}
		CHECK(fail_after < 10000U);
	}
	{
		sg_configuration_hook_surface_t saved = semantics->hook_surfaces[
			SOURCE_BRUSH_SIDES_PER_MODEL];
		sg_rune_compact_geometry_t *sentinel =
			(sg_rune_compact_geometry_t *)(uintptr_t)0x5678U;

		semantics->hook_surfaces[SOURCE_BRUSH_SIDES_PER_MODEL].model = 0U;
		semantics->hook_surfaces[SOURCE_BRUSH_SIDES_PER_MODEL].brush = 0U;
		semantics->hook_surfaces[SOURCE_BRUSH_SIDES_PER_MODEL].brush_side = 0U;
		CHECK(!SG_RuneCompactGeometryOwnerMaterialize(&fixture.configuration,
			semantics, &fixture.world, &fixture.compact_identity, NULL, &sentinel,
			&geometry_error));
		CHECK(sentinel ==
			(sg_rune_compact_geometry_t *)(uintptr_t)0x5678U);
		CHECK(geometry_error.code ==
			SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION);
		semantics->hook_surfaces[SOURCE_BRUSH_SIDES_PER_MODEL] = saved;
		CHECK(!SG_RuneCompactGeometryOwnerMaterialize(&fixture.configuration,
			NULL, &fixture.world, &fixture.compact_identity, NULL, &sentinel,
			&geometry_error));
		CHECK(sentinel ==
			(sg_rune_compact_geometry_t *)(uintptr_t)0x5678U);
		CHECK(geometry_error.code ==
			SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_ARGUMENT);
	}
	SG_ConfigurationSemanticsDestroy(semantics);
	return 1;
}

static void GeometryPlane(geometry_fixture_t *fixture, float nx, float ny,
	float nz, float distance, uint32_t axis, uint32_t variant)
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
	face->kind = SG_CONFIGURATION_FACE_FACET;
	face->first_vertex = fixture->configuration.vertex_count;
	face->vertex_count = 3U;
	GeometryPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		0, 0, 0);
	GeometryPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		1, 0, 0);
	GeometryPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		0, 1, 0);
}

static void GeometryCell(geometry_fixture_t *fixture, uint32_t index,
	float min_x, float max_x, uint32_t leaf)
{
	sg_configuration_cell_t *cell = &fixture->cells[index];

	memset(cell, 0, sizeof(*cell));
	cell->stance = SG_RUNE_STANCE_STANDING;
	cell->first_face = fixture->configuration.face_count;
	cell->face_count = 6U;
	GeometryPlane(fixture, 1, 0, 0, max_x, 0U, 0U);
	GeometryPlane(fixture, -1, 0, 0, -min_x, 0U, 1U);
	GeometryPlane(fixture, 0, 1, 0, 8, 1U, 0U);
	GeometryPlane(fixture, 0, -1, 0, 0, 1U, 1U);
	GeometryPlane(fixture, 0, 0, 1, 8, 2U, 0U);
	GeometryPlane(fixture, 0, 0, -1, 0, 2U, 1U);
	GeometryPoint(&cell->bounds.mins, min_x, 0, 0);
	GeometryPoint(&cell->bounds.maxs, max_x, 8, 8);
	GeometryPoint(&cell->interior_witness, (min_x + max_x) * 0.5f, 4, 4);
	cell->bsp_leaf.index = leaf;
	cell->bsp_area.index = 2U;
	cell->bsp_cluster.index = 3U;
}

static void InitGeometryFixture(geometry_fixture_t *fixture)
{
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	fixture->configuration.cells = fixture->cells;
	fixture->configuration.cell_count = 2U;
	fixture->configuration.faces = fixture->faces;
	fixture->configuration.vertices = fixture->vertices;
	fixture->configuration.portals = &fixture->portal;
	fixture->configuration.portal_count = 1U;
	GeometryCell(fixture, 0U, 0, 8, 1U);
	GeometryCell(fixture, 1U, 8, 16, 2U);
	fixture->faces[4].kind = SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
	fixture->faces[4].vertex_count = 0U;
	fixture->faces[10].kind = SG_CONFIGURATION_FACE_CONSTRAINT_ONLY;
	fixture->faces[10].vertex_count = 0U;
	fixture->portal.from_cell = 0U;
	fixture->portal.to_cell = 1U;
	fixture->portal.stance = SG_RUNE_STANCE_STANDING;
	fixture->portal.plane.normal[0] = 1.0f;
	fixture->portal.plane.distance = 8.0f;
	fixture->portal.plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
	fixture->portal.plane.source_index = 0U;
	fixture->portal.first_vertex = fixture->configuration.vertex_count;
	fixture->portal.vertex_count = 4U;
	GeometryPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		8, 0, 0);
	GeometryPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		8, 8, 0);
	GeometryPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		8, 8, 8);
	GeometryPoint(&fixture->vertices[fixture->configuration.vertex_count++],
		8, 0, 8);
	fixture->portal.clearance = 4.0f;
	fixture->world.models = fixture->models;
	fixture->world.model_count = 2U;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_count = 3U;
	fixture->world.areas = fixture->areas;
	fixture->world.area_count = 4U;
	fixture->world.planes = fixture->planes;
	fixture->world.plane_count = 5U;
	fixture->world.brushes = fixture->brushes;
	fixture->world.brush_count = 2U;
	fixture->world.brush_sides = fixture->brush_sides;
	fixture->world.brush_side_count = 2U;
	fixture->brushes[0].side_count = 1U;
	fixture->brushes[1].first_side = 1U;
	fixture->brushes[1].side_count = 1U;
	fixture->semantics.identity = fixture->configuration.identity;
	for (index = 0U; index < 5U; index++)
		fixture->planes[index].normal.value[0] = 1.0f;
	fixture->leaves[0].cluster = -1;
	fixture->leaves[1].cluster = 3;
	fixture->leaves[2].cluster = 3;
	fixture->leaves[1].area = 2U;
	fixture->leaves[2].area = 2U;
}

int SG_RuneCompactBuilderRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_view_t *view_out)
{
	if (builder != public_builder_handle || public_builder_identity == NULL ||
		view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = *public_builder_identity;
	return 1;
}
int SG_RuneCompactBuilderOwnerRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	if (builder != public_builder_handle || public_builder_source == NULL ||
		view_out == NULL)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->world = &public_builder_source->world;
	view_out->configuration = &public_builder_source->configuration;
	view_out->semantics = &public_builder_source->semantics;
	return 1;
}

int main(void)
{
	compact_fixture_t complete;
	geometry_fixture_t source;
	sg_rune_compact_geometry_t *geometry = NULL;
	sg_rune_compact_geometry_view_t view;
	sg_rune_compact_geometry_error_t geometry_error;
	sg_rune_compact_error_t model_error;
	sg_rune_compact_location_t location;
	sg_rune_q8_vec3_t boundary = { { 64, 32, 32 } };
	uint32_t cell;

	CHECK(TestProductionSourceSurfaceCatalog());

	InitFixture(&complete);
	InitGeometryFixture(&source);
	public_builder_source = &source;
	public_builder_identity = &complete.model.identity;
	public_builder_handle =
		(const sg_rune_compact_builder_t *)(const void *)&source;
	if (!SG_RuneCompactGeometryMaterialize(public_builder_handle, NULL,
		&geometry, &geometry_error))
	{
		fprintf(stderr, "geometry materialize error: code=%d domain=%d record=%u\n",
			(int)geometry_error.code, (int)geometry_error.domain,
			geometry_error.record);
		failures++;
		return 1;
	}
	public_builder_source = NULL;
	public_builder_identity = NULL;
	public_builder_handle = NULL;
	CHECK(SG_RuneCompactGeometryRead(geometry, &view));
	CHECK(view.cell_count == 2U);
	for (cell = 0U; cell < 2U; cell++)
	{
		uint32_t local;
		uint32_t polygon_reference = UINT32_MAX;
		uint32_t polygon_facet = UINT32_MAX;

		complete.cells[cell] = view.cells[cell];
		complete.cells[cell].movement_fields =
			(sg_rune_movement_field_span_t){ cell, 1U };
		complete.movement_fields[cell].valid_stances =
			complete.cells[cell].valid_stances;
		for (local = 0U; local < complete.cells[cell].incidences.count; local++)
		{
			const uint32_t reference =
				complete.cells[cell].incidences.first + local;
			const uint32_t incidence = view.cell_incidences[reference].value;

			if (incidence < view.incidence_count &&
				view.incidences[incidence].facet.value < view.facet_count &&
				view.facets[view.incidences[incidence].facet.value].kind ==
					SG_RUNE_COMPACT_FACET_POLYGON)
			{
				polygon_reference = reference;
				polygon_facet = view.incidences[incidence].facet.value;
				break;
			}
		}
		CHECK(polygon_reference != UINT32_MAX);
		CHECK(polygon_facet != UINT32_MAX);
		if (polygon_reference == UINT32_MAX || polygon_facet == UINT32_MAX)
			continue;
		{
			const sg_rune_compact_cell_t *compact_cell =
				&complete.cells[cell];
			const sg_rune_compact_facet_t *facet = &view.facets[polygon_facet];
			const uint32_t vertex_base = cell * 3U;
			sg_rune_compact_response_fragment_t *fragment =
				&complete.response_source_fragments[cell];
			sg_rune_compact_response_patch_t *patch =
				&complete.response_target_patches[cell];

			fragment->boundary_incidences =
				(sg_rune_compact_cell_incidence_span_t){
					polygon_reference, 1U };
			fragment->bounds = compact_cell->bounds;
			fragment->witness = compact_cell->bounds.mins;
			fragment->witness.value[0] =
				(compact_cell->bounds.mins.value[0] +
				 compact_cell->bounds.maxs.value[0]) / 2;
			fragment->witness.value[1] =
				(compact_cell->bounds.mins.value[1] +
				 compact_cell->bounds.maxs.value[1]) / 2;
			fragment->witness.value[2] =
				(compact_cell->bounds.mins.value[2] +
				 compact_cell->bounds.maxs.value[2]) / 2;
			fragment->bsp_leaf = compact_cell->source.leaf;
			fragment->bsp_area = compact_cell->source.area;
			fragment->bsp_cluster = (uint32_t)compact_cell->source.cluster;
			fragment->valid_stances = compact_cell->valid_stances;

			patch->parent_facet.value = polygon_facet;
			patch->target_cell.value = cell;
			patch->boundary_incidences =
				(sg_rune_compact_incidence_span_t){ polygon_reference, 1U };
			patch->plane = facet->plane;
			patch->first_vertex = vertex_base;
			patch->vertex_count = 3U;
			patch->bounds = compact_cell->bounds;
			patch->bsp_leaf = compact_cell->source.leaf;
			patch->bsp_area = compact_cell->source.area;
			patch->bsp_cluster = (uint32_t)compact_cell->source.cluster;
			patch->valid_stances = compact_cell->valid_stances;
			CHECK(facet->vertices.count >= 3U);
			CHECK(facet->vertices.first <= view.vertex_count - 3U);
			if (facet->vertices.count < 3U ||
				facet->vertices.first > view.vertex_count - 3U)
				continue;
			for (local = 0U; local < 3U; local++)
				complete.response_target_vertices[vertex_base + local] =
					view.vertices[facet->vertices.first + local];
			patch->bounds.mins =
				complete.response_target_vertices[vertex_base];
			patch->bounds.maxs =
				complete.response_target_vertices[vertex_base];
			for (local = 1U; local < 3U; local++)
			{
				const sg_rune_q8_vec3_t *vertex =
					&complete.response_target_vertices[vertex_base + local];
				uint32_t axis;

				for (axis = 0U; axis < 3U; axis++)
				{
					if (vertex->value[axis] < patch->bounds.mins.value[axis])
						patch->bounds.mins.value[axis] = vertex->value[axis];
					if (vertex->value[axis] > patch->bounds.maxs.value[axis])
						patch->bounds.maxs.value[axis] = vertex->value[axis];
				}
			}
			complete.response_facts[cell].target_witness =
				complete.response_target_vertices[vertex_base];
		}
	}
	complete.response_source_groups[0].bsp_cluster =
		(uint32_t)complete.cells[0].source.cluster;
	complete.response_source_groups[0].bsp_area =
		complete.cells[0].source.area;
	complete.response_target_groups[0].bsp_cluster =
		(uint32_t)complete.cells[0].source.cluster;
	complete.response_target_groups[0].bsp_area =
		complete.cells[0].source.area;
	complete.model.cells = complete.cells;
	complete.model.facets = view.facets;
	complete.model.facet_count = view.facet_count;
	complete.model.incidences = view.incidences;
	complete.model.incidence_count = view.incidence_count;
	complete.model.cell_incidences = view.cell_incidences;
	complete.model.cell_incidence_count = view.cell_incidence_count;
	complete.model.vertices = view.vertices;
	complete.model.vertex_count = view.vertex_count;
	complete.model.portals = view.portals;
	complete.model.portal_count = view.portal_count;
	complete.model.response.seal.compact_facet_count = view.facet_count;
	complete.facet_annotations[0].facet.value = 0U;
	if (!SG_RuneCompactModelValidateBound(&complete.model,
		&complete.model.identity, &model_error))
	{
		fprintf(stderr, "geometry model error: code=%d domain=%d record=%u\n",
			(int)model_error.code, (int)model_error.domain, model_error.record);
		failures++;
	}
	CHECK(SG_RuneCompactLocalize(&complete.model, &boundary, &location) ==
		SG_RUNE_COMPACT_LOCALIZE_OK);
	CHECK(location.cell.value == 0U);
	SG_RuneCompactGeometryDestroy(geometry);
	if (failures != 0)
		return 1;
	puts("sg_rune_compact_geometry_model_test: PASS");
	return 0;
}
