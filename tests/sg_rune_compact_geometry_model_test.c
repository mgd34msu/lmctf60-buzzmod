int sg_rune_compact_model_fixture_main(void);
#define main sg_rune_compact_model_fixture_main
#include "sg_rune_compact_model_test.c"
#undef main

#include "../slipgate/sg_rune_compact_geometry_owner.h"
#include "../slipgate/sg_rune_compact_localize.h"

typedef struct geometry_fixture_s
{
	sg_configuration_space_t configuration;
	sg_configuration_cell_t cells[2];
	sg_configuration_face_t faces[12];
	sg_rune_vec3_t vertices[40];
	sg_configuration_portal_t portal;
	sg_bsp_world_t world;
	sg_bsp_model_t models[1];
	sg_bsp_leaf_t leaves[3];
	sg_bsp_area_t areas[4];
	sg_bsp_plane_t planes[5];
	sg_bsp_brush_t brush;
	sg_bsp_brush_side_t brush_side;
} geometry_fixture_t;

static void GeometryPoint(sg_rune_vec3_t *point, float x, float y, float z)
{
	point->value[0] = x;
	point->value[1] = y;
	point->value[2] = z;
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
	fixture->world.model_count = 1U;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_count = 3U;
	fixture->world.areas = fixture->areas;
	fixture->world.area_count = 4U;
	fixture->world.planes = fixture->planes;
	fixture->world.plane_count = 5U;
	fixture->world.brushes = &fixture->brush;
	fixture->world.brush_count = 1U;
	fixture->world.brush_sides = &fixture->brush_side;
	fixture->world.brush_side_count = 1U;
	fixture->brush.side_count = 1U;
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
	(void)builder; (void)view_out; return 0;
}
int SG_RuneCompactBuilderOwnerRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	(void)builder; (void)view_out; return 0;
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

	InitFixture(&complete);
	InitGeometryFixture(&source);
	CHECK(SG_RuneCompactGeometryOwnerMaterialize(&source.configuration,
		&source.world, &complete.model.identity, NULL, &geometry,
		&geometry_error));
	CHECK(SG_RuneCompactGeometryRead(geometry, &view));
	CHECK(view.cell_count == 2U);
	for (cell = 0U; cell < 2U; cell++)
	{
		complete.cells[cell] = view.cells[cell];
		complete.cells[cell].movement_fields =
			(sg_rune_movement_field_span_t){ cell, 1U };
		complete.cells[cell].weapon_regions =
			(sg_rune_weapon_response_region_span_t){ cell, 1U };
		complete.movement_fields[cell].valid_stances =
			complete.cells[cell].valid_stances;
		complete.weapon_regions[cell].source_boundary_incidences =
			(sg_rune_compact_cell_incidence_span_t){
				complete.cells[cell].incidences.first, 1U };
		complete.weapon_regions[1U - cell].target_boundary_incidences =
			(sg_rune_compact_cell_incidence_span_t){
				complete.cells[cell].incidences.first, 1U };
	}
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
	puts("sg_rune_compact_geometry_model_test: PASS");
	return failures == 0 ? 0 : 1;
}
