#include "slipgate/sg_rune_compact_geometry_owner.h"
#include "slipgate/sg_rune_compact_response_partition_owner.h"
#include "slipgate/sg_rune_compact_source_surface_catalog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { if (!(expression)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
	return 0; \
} } while (0)

enum
{
	CELL_COUNT = 3,
	FACES_PER_CELL = 6,
	SURFACE_COUNT = 3,
	HOOK_VERTEX_COUNT = 12,
	BRUSH_COUNT = 5,
	BRUSH_SIDE_COUNT = 15
};

typedef struct allocation_state_s
{
	uint32_t allocations;
	uint32_t fail_after;
	uint32_t live;
} allocation_state_t;

typedef struct fixture_s
{
	sg_configuration_space_t configuration;
	sg_configuration_cell_t cells[CELL_COUNT];
	sg_configuration_face_t faces[CELL_COUNT * FACES_PER_CELL];
	sg_rune_vec3_t vertices[CELL_COUNT * FACES_PER_CELL * 3];
	sg_configuration_semantics_t semantics;
	sg_configuration_semantic_region_t regions[CELL_COUNT];
	sg_configuration_semantic_face_t semantic_faces[
		CELL_COUNT * FACES_PER_CELL];
	sg_configuration_hook_surface_t surfaces[SURFACE_COUNT];
	sg_rune_vec3_t hook_vertices[HOOK_VERTEX_COUNT];
	sg_static_visibility_t visibility;
	sg_static_visibility_partition_t partitions[CELL_COUNT];
	uint32_t area_components[2];
	sg_static_visibility_occluder_t occluders[2];
	sg_static_visibility_surface_t visibility_surfaces[SURFACE_COUNT];
	sg_bsp_world_t world;
	sg_bsp_model_t models[2];
	sg_bsp_leaf_t leaf;
	sg_bsp_area_t areas[2];
	sg_bsp_plane_t planes[BRUSH_SIDE_COUNT];
	sg_bsp_brush_t brushes[BRUSH_COUNT];
	sg_bsp_brush_side_t brush_sides[BRUSH_SIDE_COUNT];
	sg_host_collision_authority_t collision;
	sg_rune_compact_identity_t identity;
	sg_rune_compact_builder_owner_view_t owner;
	sg_rune_compact_geometry_t *geometry;
	sg_rune_compact_geometry_view_t geometry_view;
} fixture_t;

static fixture_t *active_fixture;
static uint32_t visibility_query_count;
static uint32_t visibility_hit_brush;
static uint32_t visibility_hit_brush_side;

static void Point(sg_rune_vec3_t *point, float x, float y, float z)
{
	point->value[0] = x;
	point->value[1] = y;
	point->value[2] = z;
}

static void BspPoint(sg_bsp_vec3_t *point, float x, float y, float z)
{
	point->value[0] = x;
	point->value[1] = y;
	point->value[2] = z;
}

static void FaceVertices(fixture_t *fixture,
	sg_configuration_face_t *face)
{
	face->first_vertex = fixture->configuration.vertex_count;
	face->vertex_count = 3U;
	Point(&fixture->vertices[fixture->configuration.vertex_count++], 0, 0, 0);
	Point(&fixture->vertices[fixture->configuration.vertex_count++], 1, 0, 0);
	Point(&fixture->vertices[fixture->configuration.vertex_count++], 0, 1, 0);
}

static void AddFace(fixture_t *fixture, float x, float y, float z,
	float distance, uint32_t axis, uint32_t variant, int constraint)
{
	sg_configuration_face_t *face =
		&fixture->faces[fixture->configuration.face_count++];

	memset(face, 0, sizeof(*face));
	face->plane.normal[0] = x;
	face->plane.normal[1] = y;
	face->plane.normal[2] = z;
	face->plane.distance = distance;
	face->plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
	face->plane.source_index = axis;
	face->plane.source_variant = variant;
	face->kind = constraint ? SG_CONFIGURATION_FACE_CONSTRAINT_ONLY :
		SG_CONFIGURATION_FACE_FACET;
	if (!constraint)
		FaceVertices(fixture, face);
}

static void AddCell(fixture_t *fixture, uint32_t index, float min_x,
	float max_x, int constraint)
{
	sg_configuration_cell_t *cell = &fixture->cells[index];
	uint32_t face;

	memset(cell, 0, sizeof(*cell));
	cell->stance = SG_RUNE_STANCE_STANDING;
	cell->first_face = fixture->configuration.face_count;
	AddFace(fixture, 1, 0, 0, max_x, 0U, 0U, 0);
	AddFace(fixture, -1, 0, 0, -min_x, 0U, 1U, 0);
	AddFace(fixture, 0, 1, 0, 2, 1U, 0U, constraint);
	AddFace(fixture, 0, -1, 0, 2, 1U, 1U, 0);
	AddFace(fixture, 0, 0, 1, 2, 2U, 0U, 0);
	AddFace(fixture, 0, 0, -1, 0, 2U, 1U, 0);
	cell->face_count = FACES_PER_CELL;
	Point(&cell->bounds.mins, min_x, -2, 0);
	Point(&cell->bounds.maxs, max_x, 2, 2);
	Point(&cell->interior_witness, 0.5f * (min_x + max_x), 0, 1);
	cell->bsp_leaf.index = 0U;
	cell->bsp_area.index = 0U;
	cell->bsp_cluster.index = UINT32_MAX;
	fixture->regions[index].id = UINT64_C(1000) + index;
	fixture->regions[index].cell = index;
	fixture->regions[index].first_face = index * FACES_PER_CELL;
	fixture->regions[index].face_count = FACES_PER_CELL;
	fixture->regions[index].bounds = cell->bounds;
	fixture->regions[index].interior_witness = cell->interior_witness;
	for (face = 0U; face < FACES_PER_CELL; face++)
	{
		const sg_configuration_face_t *source =
			&fixture->faces[cell->first_face + face];
		sg_configuration_semantic_face_t *destination =
			&fixture->semantic_faces[index * FACES_PER_CELL + face];

		memset(destination, 0, sizeof(*destination));
		memcpy(destination->normal, source->plane.normal,
			sizeof(destination->normal));
		destination->distance = source->plane.distance;
		destination->source_kind = source->plane.source_kind;
		destination->source_index = source->plane.source_index;
		destination->source_variant = source->plane.source_variant;
		destination->kind = source->kind == SG_CONFIGURATION_FACE_FACET ?
			SG_CONFIGURATION_SEMANTIC_FACE_FACET :
			SG_CONFIGURATION_SEMANTIC_FACE_CONSTRAINT_ONLY;
	}
	fixture->partitions[index].id = fixture->regions[index].id;
	fixture->partitions[index].configuration_region = index;
	fixture->partitions[index].configuration_cell = index;
	fixture->partitions[index].bsp_leaf = 0U;
	fixture->partitions[index].bsp_area = 0U;
	fixture->partitions[index].bsp_cluster = UINT32_MAX;
}

static void AddSurface(fixture_t *fixture, uint32_t index, uint64_t id,
	uint32_t model, uint32_t brush, uint32_t side, float nx, float ny, float nz,
	float distance, sg_configuration_hook_surface_flags_t flags,
	const float points[4][3])
{
	sg_configuration_hook_surface_t *surface = &fixture->surfaces[index];
	uint32_t vertex;

	memset(surface, 0, sizeof(*surface));
	surface->id = id;
	surface->model = model;
	surface->brush = brush;
	surface->brush_side = side;
	surface->normal[0] = nx;
	surface->normal[1] = ny;
	surface->normal[2] = nz;
	surface->distance = distance;
	surface->first_vertex = index * 4U;
	surface->vertex_count = 4U;
	surface->flags = flags;
	for (vertex = 0U; vertex < 4U; vertex++)
		Point(&fixture->hook_vertices[index * 4U + vertex], points[vertex][0],
			points[vertex][1], points[vertex][2]);
	fixture->visibility_surfaces[index].id = id;
	fixture->visibility_surfaces[index].semantic_surface = index;
	fixture->visibility_surfaces[index].model = model;
	fixture->visibility_surfaces[index].brush = brush;
	fixture->visibility_surfaces[index].brush_side = side;
	fixture->visibility_surfaces[index].flags = flags;
}

static void InitFixture(fixture_t *fixture)
{
	static const float floor_points[4][3] = {
		{ -4, 0, 0 }, { 4, 0, 0 }, { 4, 1.5f, 0 }, { -4, 1.5f, 0 }
	};
	static const float sky_points[4][3] = {
		{ -4, -1, 2 }, { -4, 1, 2 }, { 4, 1, 2 }, { 4, -1, 2 }
	};
	static const float mover_points[4][3] = {
		{ -1, 0, 0.25f }, { 1, 0, 0.25f },
		{ 1, 0, 1.75f }, { -1, 0, 1.75f }
	};
	static const float normals[BRUSH_SIDE_COUNT][3] = {
		{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
		{ 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
		{ 0, 0, 1 }, { 0, 0, -1 }, { 0, 1, 0 },
		{ 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
		{ 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
	};
	uint32_t side;

	memset(fixture, 0, sizeof(*fixture));
	visibility_hit_brush = 0U;
	visibility_hit_brush_side = 2U;
	fixture->configuration.cells = fixture->cells;
	fixture->configuration.cell_count = CELL_COUNT;
	fixture->configuration.faces = fixture->faces;
	fixture->configuration.vertices = fixture->vertices;
	AddCell(fixture, 0U, -4, -1, 0);
	AddCell(fixture, 1U, -1, 1, 1);
	AddCell(fixture, 2U, 1, 4, 0);
	fixture->partitions[2].bsp_area = 1U;
	fixture->semantics.regions = fixture->regions;
	fixture->semantics.region_count = CELL_COUNT;
	fixture->semantics.faces = fixture->semantic_faces;
	fixture->semantics.face_count = CELL_COUNT * FACES_PER_CELL;
	fixture->semantics.hook_surfaces = fixture->surfaces;
	fixture->semantics.hook_surface_count = SURFACE_COUNT;
	fixture->semantics.hook_vertices = fixture->hook_vertices;
	fixture->semantics.hook_vertex_count = HOOK_VERTEX_COUNT;
	AddSurface(fixture, 0U, UINT64_C(0), 0U, 1U, 6U, 0, 0, 1, 0,
		SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE, floor_points);
	AddSurface(fixture, 1U, UINT64_C(1), 0U, 2U, 7U, 0, 0, -1, -2,
		SG_CONFIGURATION_HOOK_SURFACE_SKY, sky_points);
	AddSurface(fixture, 2U, UINT64_C(2), 1U, 3U, 8U, 0, 1, 0, 0,
		SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE |
		SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL, mover_points);
	fixture->visibility.partitions = fixture->partitions;
	fixture->visibility.partition_count = CELL_COUNT;
	fixture->visibility.area_components = fixture->area_components;
	fixture->visibility.area_count = 2U;
	fixture->visibility.occluders = fixture->occluders;
	fixture->visibility.occluder_count = 1U;
	fixture->visibility.surfaces = fixture->visibility_surfaces;
	fixture->visibility.surface_count = SURFACE_COUNT;
	fixture->occluders[0].model = 0U;
	fixture->occluders[0].brush = 0U;
	fixture->occluders[0].contents = SG_HOST_CONTENTS_SOLID;
	fixture->occluders[1].model = 0U;
	fixture->occluders[1].brush = 4U;
	fixture->occluders[1].contents = SG_HOST_CONTENTS_SOLID;
	fixture->world.models = fixture->models;
	fixture->world.model_count = 2U;
	fixture->models[0].headnode = -1;
	BspPoint(&fixture->models[0].mins, -8, -8, -8);
	BspPoint(&fixture->models[0].maxs, 8, 8, 8);
	fixture->models[1].headnode = -1;
	BspPoint(&fixture->models[1].mins, -1, -1, 0);
	BspPoint(&fixture->models[1].maxs, 1, 1, 2);
	fixture->world.leaves = &fixture->leaf;
	fixture->world.leaf_count = 1U;
	fixture->leaf.cluster = -1;
	fixture->world.areas = fixture->areas;
	fixture->world.area_count = 2U;
	fixture->world.planes = fixture->planes;
	fixture->world.plane_count = BRUSH_SIDE_COUNT;
	fixture->world.brushes = fixture->brushes;
	fixture->world.brush_count = BRUSH_COUNT;
	fixture->world.brush_sides = fixture->brush_sides;
	fixture->world.brush_side_count = BRUSH_SIDE_COUNT;
	fixture->brushes[0].side_count = 6U;
	fixture->brushes[0].contents = SG_HOST_CONTENTS_SOLID;
	fixture->brushes[1].first_side = 6U;
	fixture->brushes[1].side_count = 1U;
	fixture->brushes[2].first_side = 7U;
	fixture->brushes[2].side_count = 1U;
	fixture->brushes[3].first_side = 8U;
	fixture->brushes[3].side_count = 1U;
	fixture->brushes[4].first_side = 9U;
	fixture->brushes[4].side_count = 6U;
	fixture->brushes[4].contents = SG_HOST_CONTENTS_SOLID;
	for (side = 0U; side < BRUSH_SIDE_COUNT; side++)
	{
		memcpy(fixture->planes[side].normal.value, normals[side],
			sizeof(fixture->planes[side].normal.value));
		fixture->planes[side].distance = side < 4U ||
			(side >= 9U && side < 13U) ? 0.5f :
			(side == 4U || side == 13U ? 1.5f :
			 (side == 5U || side == 14U ? -0.5f :
			  (side == 6U || side == 8U ? 0.0f : -2.0f)));
		fixture->brush_sides[side].plane = side;
	}
	fixture->identity.source_counts.model_count = 2U;
	fixture->identity.source_counts.leaf_count = 1U;
	fixture->identity.source_counts.area_count = 2U;
	fixture->identity.source_counts.plane_count = BRUSH_SIDE_COUNT;
	fixture->identity.source_counts.brush_count = BRUSH_COUNT;
	fixture->identity.source_counts.brush_side_count = BRUSH_SIDE_COUNT;
	fixture->collision.world = &fixture->world;
	fixture->owner.identity = fixture->identity;
	fixture->owner.world = &fixture->world;
	fixture->owner.collision = &fixture->collision;
	fixture->owner.configuration = &fixture->configuration;
	fixture->owner.semantics = &fixture->semantics;
	fixture->owner.visibility = &fixture->visibility;
}

int SG_RuneCompactIdentityMatches(const sg_rune_compact_identity_t *left,
	const sg_rune_compact_identity_t *right)
{
	return left != NULL && right != NULL &&
		memcmp(left, right, sizeof(*left)) == 0;
}

int SG_RuneCompactBuilderOwnerRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	(void)builder;
	(void)view_out;
	return 0;
}

int SG_RuneCompactBuilderRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_view_t *view_out)
{
	(void)builder;
	(void)view_out;
	return 0;
}

int SG_StaticVisibilityQueryBoundSurface(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t source_partition,
	const float source[3], uint32_t surface_index, const float target[3],
	sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out)
{
	const sg_configuration_hook_surface_t *surface;
	uint32_t axis;

	visibility_query_count++;
	if (active_fixture == NULL || authority != &active_fixture->collision ||
		configuration != &active_fixture->configuration ||
		semantics != &active_fixture->semantics ||
		visibility != &active_fixture->visibility ||
		source_partition >= CELL_COUNT || surface_index >= SURFACE_COUNT ||
		result_out == NULL || scene == NULL || scene->instances != NULL ||
		scene->instance_count != 0U || source == NULL || target == NULL)
		return 0;
	if (error_out != NULL)
		memset(error_out, 0, sizeof(*error_out));
	memset(result_out, 0, sizeof(*result_out));
	surface = &semantics->hook_surfaces[surface_index];
	result_out->trace.fraction = 1.0f;
	result_out->trace.texinfo = SG_HOST_COLLISION_TEXINFO_NONE;
	result_out->trace.brush = SG_HOST_COLLISION_BRUSH_NONE;
	result_out->trace.brush_side = SG_HOST_COLLISION_BRUSH_NONE;
	if ((surface->flags & SG_CONFIGURATION_HOOK_SURFACE_SKY) != 0U)
	{
		result_out->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		result_out->reason = SG_STATIC_VISIBILITY_REASON_SKY;
	}
	else if (surface->id == UINT64_C(0) && source_partition == 0U)
	{
		const sg_bsp_plane_t *plane;
		float source_distance;
		float target_distance;

		if (visibility_hit_brush >= active_fixture->world.brush_count ||
			visibility_hit_brush_side >= active_fixture->world.brush_side_count)
			return 0;
		plane = &active_fixture->world.planes[visibility_hit_brush_side];
		source_distance = -plane->distance;
		target_distance = -plane->distance;
		for (axis = 0U; axis < 3U; axis++)
		{
			source_distance += plane->normal.value[axis] * source[axis];
			target_distance += plane->normal.value[axis] * target[axis];
		}

		if (fabsf(target_distance - source_distance) < 0.0001f)
			return 0;
		result_out->classification = SG_STATIC_VISIBILITY_OCCLUDED;
		result_out->reason = SG_STATIC_VISIBILITY_REASON_STATIC_WORLD;
		result_out->trace.fraction = -source_distance /
			(target_distance - source_distance);
		if (!(result_out->trace.fraction > 0.0f &&
			result_out->trace.fraction <= 1.0f))
			return 0;
		result_out->trace.contents = SG_HOST_CONTENTS_SOLID;
		result_out->trace.model_index = SG_HOST_COLLISION_MODEL_WORLD;
		result_out->trace.brush = visibility_hit_brush;
		result_out->trace.brush_side = visibility_hit_brush_side;
		memcpy(result_out->trace.plane.normal, plane->normal.value,
			sizeof(result_out->trace.plane.normal));
		result_out->trace.plane.distance = plane->distance;
		for (axis = 0U; axis < 3U; axis++)
			result_out->trace.end[axis] = source[axis] +
				result_out->trace.fraction * (target[axis] - source[axis]);
	}
	else
	{
		result_out->classification = SG_STATIC_VISIBILITY_VISIBLE;
		result_out->trace.model_index = SG_HOST_COLLISION_MODEL_WORLD;
		for (axis = 0U; axis < 3U; axis++)
			result_out->trace.end[axis] = target[axis];
	}
	return 1;
}

static void *TestAllocate(void *context, size_t bytes)
{
	allocation_state_t *state = context;
	const uint32_t allocation = state->allocations++;

	if (state->fail_after != UINT32_MAX && allocation == state->fail_after)
		return NULL;
	state->live++;
	return malloc(bytes);
}

static void TestRelease(void *context, void *allocation)
{
	allocation_state_t *state = context;

	if (allocation != NULL)
	{
		state->live--;
		free(allocation);
	}
}

static int FixtureBuildGeometry(fixture_t *fixture)
{
	sg_rune_compact_geometry_error_t error;

	CHECK(SG_RuneCompactGeometryOwnerMaterialize(&fixture->configuration,
		&fixture->semantics, &fixture->world, &fixture->identity, NULL,
		&fixture->geometry, &error));
	CHECK(SG_RuneCompactGeometryRead(fixture->geometry,
		&fixture->geometry_view));
	return 1;
}

static float PublishedFloat(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int FragmentUsesSplit(
	const sg_rune_compact_response_partition_view_t *view, uint32_t fragment,
	uint32_t split)
{
	const sg_rune_compact_response_fragment_t *source;
	uint32_t halfspace;

	if (fragment >= view->source_fragment_count)
		return 0;
	source = &view->source_fragments[fragment];
	for (halfspace = 0U; halfspace < source->halfspace_count; halfspace++)
		if (view->source_halfspaces[source->first_halfspace + halfspace].split ==
			split)
			return 1;
	return 0;
}

static int CertifiedDirectPair(
	const sg_rune_compact_response_partition_view_t *view,
	uint32_t source, uint32_t target,
	sg_rune_compact_response_pair_t *pair)
{
	const sg_rune_compact_response_patch_t *patch;
	uint32_t axis;

	if (!SG_RuneCompactResponsePartitionQuery(view, source, target, pair))
		return 0;
	patch = &view->target_patches[target];
	pair->certificate = SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT;
	pair->relation_flags |= SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
	pair->certificate_split = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	pair->target_witness = view->target_vertices[patch->first_vertex];
	pair->trace.fraction = 1.0f;
	pair->trace.model_index = SG_HOST_COLLISION_MODEL_WORLD;
	pair->trace.brush = SG_HOST_COLLISION_BRUSH_NONE;
	pair->trace.brush_side = SG_HOST_COLLISION_BRUSH_NONE;
	for (axis = 0U; axis < 3U; axis++)
	{
		pair->trace.end[axis] =
			(float)pair->target_witness.value[axis] / 8.0f;
	}
	pair->trace.texinfo = SG_HOST_COLLISION_TEXINFO_NONE;
	return 1;
}

static int CertifiedStaticImpactPair(
	const sg_rune_compact_response_partition_view_t *view,
	sg_rune_compact_response_pair_t *pair)
{
	uint32_t source;

	for (source = 0U; source < view->source_fragment_count; source++)
	{
		uint32_t target;

		for (target = 0U; target < view->target_patch_count; target++)
		{
			const sg_rune_compact_response_patch_t *patch =
				&view->target_patches[target];
			uint32_t split;

			if ((patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) != 0U ||
				!SG_RuneCompactResponsePartitionQuery(view, source, target, pair))
				continue;
			for (split = 0U; split < view->split_count; split++)
			{
				const sg_rune_compact_response_split_t *boundary =
					&view->splits[split];
				double source_distance;
				double target_distance;
				double fraction;
				uint32_t axis;

				if (boundary->kind !=
					SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE)
					continue;
				pair->target_witness =
					view->target_vertices[patch->first_vertex];
				source_distance =
					-(double)PublishedFloat(boundary->plane.distance_bits);
				target_distance = source_distance;
				for (axis = 0U; axis < 3U; axis++)
				{
					const double normal = (double)PublishedFloat(
						boundary->plane.normal_bits[axis]);

					source_distance += normal *
						((double)view->source_fragments[source].witness.value[axis] /
						 8.0);
					target_distance += normal *
						((double)pair->target_witness.value[axis] / 8.0);
				}
				if (fabs(source_distance - target_distance) < 0.000001)
					continue;
				fraction = source_distance /
					(source_distance - target_distance);
				if (!(fraction > 0.0 && fraction <= 1.0))
					continue;
				pair->certificate =
					SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT;
				pair->first_hit_occluder = boundary->occluder;
				pair->relation_flags |=
					SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT;
				pair->certificate_split = split;
				pair->trace.fraction = (float)fraction;
				pair->trace.contents = SG_HOST_CONTENTS_SOLID;
				pair->trace.model_index = SG_HOST_COLLISION_MODEL_WORLD;
				if (boundary->edge >=
					view->static_occluder_side_count)
					return 0;
				pair->trace.brush = view->static_occluder_sides[
					boundary->edge].brush;
				pair->trace.brush_side = view->static_occluder_sides[
					boundary->edge].brush_side;
				for (axis = 0U; axis < 3U; axis++)
				{
					const float origin =
						(float)view->source_fragments[source].witness.value[axis] /
						8.0f;
					const float target_value =
						(float)pair->target_witness.value[axis] / 8.0f;

					pair->trace.end[axis] = origin +
						pair->trace.fraction * (target_value - origin);
					pair->trace.plane.normal[axis] = PublishedFloat(
						boundary->plane.normal_bits[axis]);
				}
				pair->trace.plane.distance =
					PublishedFloat(boundary->plane.distance_bits);
				return 1;
			}
		}
	}
	return 0;
}

static uint32_t PairWithCertificate(
	const sg_rune_compact_response_partition_view_t *view,
	sg_rune_compact_response_certificate_t certificate)
{
	uint32_t index;

	for (index = 0U; index < view->response_pair_count; index++)
		if (view->response_pairs[index].certificate == certificate)
			return index;
	return SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
}

static int TestSharedRefinement(void)
{
	fixture_t fixture;
	sg_rune_compact_response_partition_t *partition = NULL;
	sg_rune_compact_response_partition_view_t view;
	sg_rune_compact_response_error_t error;
	uint32_t index;
	int found_mover = 0;
	int found_sky = 0;
	int found_unresolved = 0;
	int found_constraint = 0;
	int found_certified_direct = 0;
	int found_certified_impact = 0;
	int found_silhouette_refinement = 0;
	int found_tie_refinement = 0;
	uint32_t split_kinds = 0U;

	InitFixture(&fixture);
	visibility_query_count = 0U;
	CHECK(FixtureBuildGeometry(&fixture));
	active_fixture = &fixture;
	CHECK(SG_RuneCompactResponsePartitionOwnerBuild(&fixture.owner,
		&fixture.geometry_view, NULL, &partition, &error));
	CHECK(SG_RuneCompactResponsePartitionRead(partition, &view));
	CHECK(SG_RuneCompactResponsePartitionSealValid(&view));
	CHECK(view.seal.split_frontier_count == 0U);
	CHECK(view.source_fragment_count > CELL_COUNT);
	CHECK(view.target_patch_count == SURFACE_COUNT);
	CHECK(view.source_fragment_count < 64U);
	CHECK(view.target_patch_count < 32U);
	for (index = 0U; index < fixture.geometry_view.facet_count; index++)
		found_constraint |= fixture.geometry_view.facets[index].kind ==
			SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;
	CHECK(found_constraint);
	for (index = 0U; index < view.split_count; index++)
	{
		split_kinds |= UINT32_C(1) << view.splits[index].kind;
	}
	CHECK(split_kinds == ((UINT32_C(1) <<
		SG_RUNE_COMPACT_RESPONSE_SPLIT_KIND_COUNT) - 1U));
	for (index = 0U; index < view.target_patch_count; index++)
	{
		const sg_rune_compact_response_patch_t *patch =
			&view.target_patches[index];

		if (patch->model == 1U)
		{
			found_mover = 1;
			CHECK(patch->parent_facet.value == UINT32_MAX);
			CHECK((patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING) != 0U);
			CHECK(patch->bsp_cluster == UINT32_MAX);
		}
		found_sky |= (patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) != 0U;
	}
	CHECK(view.response_pair_count != 0U);
	CHECK(view.candidate_group_count != 0U);
	CHECK(view.candidate_group_count <
		view.source_fragment_count * view.target_patch_count);
	CHECK(view.response_pair_count <= view.candidate_group_count);
	for (index = 0U; index < view.source_fragment_count; index++)
	{
		uint32_t target;

		for (target = 0U; target < view.target_patch_count; target++)
		{
			sg_rune_compact_response_pair_t pair;
			const int sky = (view.target_patches[target].flags &
				SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) != 0U;
			const int queried = SG_RuneCompactResponsePartitionQuery(&view,
				index, target, &pair);

			if (sky)
			{
				CHECK(!queried);
				continue;
			}
			CHECK(queried);
			found_unresolved |= pair.certificate ==
				SG_RUNE_COMPACT_RESPONSE_UNRESOLVED_EXACT_RAY;
			CHECK(pair.reason ==
				((view.target_patches[target].flags &
					SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING) != 0U ?
					SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL :
					SG_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED));
		}
	}
	for (index = 0U; index < view.response_pair_count; index++)
	{
		const sg_rune_compact_response_pair_t *pair =
			&view.response_pairs[index];
		uint32_t split;

		CHECK(SG_RuneCompactResponsePartitionQuery(&view,
			pair->source_fragment, pair->target_patch,
			&(sg_rune_compact_response_pair_t){ 0 }));
		if (pair->certificate == SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT)
			found_certified_direct = 1;
		else if (pair->certificate ==
			SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT)
		{
			found_certified_impact = 1;
			CHECK(pair->certificate_split < view.split_count);
			CHECK(view.splits[pair->certificate_split].kind ==
				SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE);
		}
		else
			CHECK(0);
		for (split = 0U; split < view.split_count; split++)
		{
			if (view.splits[split].kind ==
				SG_RUNE_COMPACT_RESPONSE_SPLIT_TARGET_EDGE &&
				FragmentUsesSplit(&view, pair->source_fragment, split))
				found_silhouette_refinement = 1;
			if (view.splits[split].kind ==
				SG_RUNE_COMPACT_RESPONSE_SPLIT_FIRST_HIT_TIE &&
				view.splits[split].target_surface_id ==
					view.target_patches[pair->target_patch].visibility_surface_id &&
				view.splits[split].occluder == pair->first_hit_occluder &&
				FragmentUsesSplit(&view, pair->source_fragment, split))
				found_tie_refinement = 1;
		}
	}
	CHECK(found_mover);
	CHECK(found_sky);
	CHECK(found_unresolved);
	CHECK(found_certified_direct);
	CHECK(found_certified_impact);
	CHECK(found_silhouette_refinement);
	CHECK(found_tie_refinement);
	CHECK(view.seal.certified_direct_pair_count != 0U);
	CHECK(view.seal.certified_static_impact_pair_count != 0U);
	CHECK(view.seal.unresolved_response_pair_count == 0U);
	CHECK(view.seal.unresolved_candidate_group_count ==
		view.candidate_group_count);
	CHECK((view.seal.flags &
		SG_RUNE_COMPACT_RESPONSE_SEAL_CONSTANT_RESPONSE_PAIRS) == 0U);
	CHECK(visibility_query_count >= view.response_pair_count);
	{
		sg_rune_compact_response_partition_view_t exception_view = view;
		sg_rune_compact_response_pair_t exception;
		sg_rune_compact_response_pair_t queried;
		uint32_t source = 0U;
		uint32_t target = 0U;

		while (source < view.source_fragment_count &&
			view.source_fragments[source].parent_cell.value != 2U)
			source++;
		while (target < view.target_patch_count &&
			((view.target_patches[target].flags &
				SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) != 0U ||
			 view.target_patches[target].model !=
				SG_HOST_COLLISION_MODEL_WORLD))
			target++;
		CHECK(source < view.source_fragment_count);
		CHECK(target < view.target_patch_count);
		CHECK(CertifiedDirectPair(&view, source, target, &exception));
		exception_view.response_pairs = &exception;
		exception_view.response_pair_count = 1U;
		exception_view.seal.response_pair_count = 1U;
		exception_view.seal.certified_direct_pair_count = 1U;
		exception_view.seal.certified_static_impact_pair_count = 0U;
		CHECK(exception.classification == SG_STATIC_VISIBILITY_CONDITIONAL);
		CHECK(exception.reason ==
			SG_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED);
		CHECK(exception.requires_exact_ray == 1U);
		CHECK(exception.requires_area_state == 1U);
		CHECK((exception.relation_flags &
			SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING) != 0U);
		CHECK(SG_RuneCompactResponsePartitionSealValid(&exception_view));
		CHECK(SG_RuneCompactResponsePartitionQuery(&exception_view, source, target,
			&queried));
		CHECK(queried.certificate ==
			SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT);
		CHECK(queried.requires_area_state == 1U);
		exception.requires_area_state = 0U;
		CHECK(!SG_RuneCompactResponsePartitionSealValid(&exception_view));
		exception.requires_area_state = 1U;
		exception.relation_flags &=
			~(sg_rune_compact_static_relation_flags_t)
				SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING;
		CHECK(!SG_RuneCompactResponsePartitionSealValid(&exception_view));
		exception.relation_flags |=
			SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING;
		exception.trace.end[0] += 1.0f;
		CHECK(!SG_RuneCompactResponsePartitionSealValid(&exception_view));
		CHECK(CertifiedDirectPair(&view, source, target, &exception));
		memset(&exception.trace, 0, sizeof(exception.trace));
		CHECK(!SG_RuneCompactResponsePartitionSealValid(&exception_view));
		CHECK(CertifiedDirectPair(&view, source, target, &exception));
		exception.certificate =
			SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT;
		exception.first_hit_occluder = 0U;
		exception.relation_flags &=
			~(sg_rune_compact_static_relation_flags_t)
				SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
		exception.relation_flags |=
			SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT;
		exception_view.seal.certified_direct_pair_count = 0U;
		exception_view.seal.certified_static_impact_pair_count = 1U;
		CHECK(!SG_RuneCompactResponsePartitionSealValid(&exception_view));
	}
	{
		sg_rune_compact_response_partition_view_t exception_view = view;
		sg_rune_compact_response_pair_t exception;
		sg_rune_compact_response_pair_t queried;

		CHECK(CertifiedStaticImpactPair(&view, &exception));
		exception_view.response_pairs = &exception;
		exception_view.response_pair_count = 1U;
		exception_view.seal.response_pair_count = 1U;
		exception_view.seal.certified_direct_pair_count = 0U;
		exception_view.seal.certified_static_impact_pair_count = 1U;
		CHECK(SG_RuneCompactResponsePartitionSealValid(&exception_view));
		CHECK(SG_RuneCompactResponsePartitionQuery(&exception_view,
			exception.source_fragment, exception.target_patch, &queried));
		CHECK(queried.certificate ==
			SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT);
		CHECK(queried.first_hit_occluder == exception.first_hit_occluder);
		exception.certificate_split = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		CHECK(!SG_RuneCompactResponsePartitionSealValid(&exception_view));
		CHECK(CertifiedStaticImpactPair(&view, &exception));
		exception.trace.end[0] += 0.5f;
		CHECK(!SG_RuneCompactResponsePartitionSealValid(&exception_view));
	}
	{
		sg_rune_compact_response_partition_view_t exception_view = view;
		sg_rune_compact_response_pair_t exception;
		sg_rune_compact_response_pair_t queried;
		uint32_t target = 0U;

		while (target < view.target_patch_count &&
			(view.target_patches[target].flags &
				SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING) == 0U)
			target++;
		CHECK(target < view.target_patch_count);
		CHECK(CertifiedDirectPair(&view, 0U, target, &exception));
		CHECK(exception.reason ==
			SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL);
		exception_view.response_pairs = &exception;
		exception_view.response_pair_count = 1U;
		exception_view.seal.response_pair_count = 1U;
		exception_view.seal.certified_direct_pair_count = 1U;
		exception_view.seal.certified_static_impact_pair_count = 0U;
		CHECK(SG_RuneCompactResponsePartitionSealValid(&exception_view));
		CHECK(SG_RuneCompactResponsePartitionQuery(&exception_view, 0U, target,
			&queried));
		CHECK(queried.certificate ==
			SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT);
		CHECK(queried.reason ==
			SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL);
		CHECK(queried.requires_exact_ray == 1U);
		exception.reason = SG_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
		CHECK(!SG_RuneCompactResponsePartitionSealValid(&exception_view));
		CHECK(!SG_RuneCompactResponsePartitionQuery(&exception_view, 0U, target,
			&queried));
	}
	SG_RuneCompactResponsePartitionDestroy(partition);
	SG_RuneCompactGeometryDestroy(fixture.geometry);
	active_fixture = NULL;
	return 1;
}

static int StaticImpactUsesOnlyOwner(
	const sg_rune_compact_response_partition_view_t *view, uint32_t owner)
{
	uint32_t index;
	int found_impact = 0;

	for (index = 0U; index < view->split_count; index++)
	{
		const sg_rune_compact_response_split_t *split = &view->splits[index];

		if (split->kind ==
			SG_RUNE_COMPACT_RESPONSE_SPLIT_TARGET_EDGE)
			continue;
		if (split->occluder != owner)
			return 0;
	}
	for (index = 0U; index < view->response_pair_count; index++)
	{
		const sg_rune_compact_response_pair_t *pair =
			&view->response_pairs[index];
		const sg_rune_compact_response_split_t *split;
		const sg_rune_compact_response_occluder_side_t *side;

		if (pair->certificate !=
			SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT)
			continue;
		if (pair->first_hit_occluder != owner ||
			pair->certificate_split >= view->split_count)
			return 0;
		split = &view->splits[pair->certificate_split];
		if (split->kind !=
			SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE ||
			split->edge >= view->static_occluder_side_count)
			return 0;
		side = &view->static_occluder_sides[split->edge];
		if (side->occluder != owner || split->brush_side != side->brush_side ||
			pair->trace.brush != side->brush ||
			pair->trace.brush_side != side->brush_side)
			return 0;
		found_impact = 1;
	}
	return found_impact;
}

static int TestCoplanarImpactProvenance(void)
{
	fixture_t first;
	fixture_t second;
	fixture_t repeated;
	sg_rune_compact_response_partition_t *first_partition = NULL;
	sg_rune_compact_response_partition_t *second_partition = NULL;
	sg_rune_compact_response_partition_t *repeated_partition = NULL;
	sg_rune_compact_response_partition_view_t first_view;
	sg_rune_compact_response_partition_view_t second_view;
	sg_rune_compact_response_partition_view_t repeated_view;

	InitFixture(&first);
	first.visibility.occluder_count = 2U;
	visibility_hit_brush = 0U;
	visibility_hit_brush_side = 2U;
	CHECK(FixtureBuildGeometry(&first));
	active_fixture = &first;
	CHECK(SG_RuneCompactResponsePartitionOwnerBuild(&first.owner,
		&first.geometry_view, NULL, &first_partition, NULL));
	CHECK(SG_RuneCompactResponsePartitionRead(first_partition, &first_view));
	CHECK(SG_RuneCompactResponsePartitionSealValid(&first_view));
	CHECK(first_view.static_occluder_count == 2U);
	CHECK(StaticImpactUsesOnlyOwner(&first_view, 0U));

	InitFixture(&second);
	second.visibility.occluder_count = 2U;
	visibility_hit_brush = 4U;
	visibility_hit_brush_side = 11U;
	CHECK(FixtureBuildGeometry(&second));
	active_fixture = &second;
	CHECK(SG_RuneCompactResponsePartitionOwnerBuild(&second.owner,
		&second.geometry_view, NULL, &second_partition, NULL));
	CHECK(SG_RuneCompactResponsePartitionRead(second_partition, &second_view));
	CHECK(SG_RuneCompactResponsePartitionSealValid(&second_view));
	CHECK(second_view.static_occluder_count == 2U);
	CHECK(StaticImpactUsesOnlyOwner(&second_view, 1U));

	InitFixture(&repeated);
	repeated.visibility.occluder_count = 2U;
	visibility_hit_brush = 4U;
	visibility_hit_brush_side = 11U;
	CHECK(FixtureBuildGeometry(&repeated));
	active_fixture = &repeated;
	CHECK(SG_RuneCompactResponsePartitionOwnerBuild(&repeated.owner,
		&repeated.geometry_view, NULL, &repeated_partition, NULL));
	CHECK(SG_RuneCompactResponsePartitionRead(repeated_partition,
		&repeated_view));
	CHECK(second_view.split_count == repeated_view.split_count);
	CHECK(second_view.response_pair_count == repeated_view.response_pair_count);
	CHECK(memcmp(second_view.splits, repeated_view.splits,
		(size_t)second_view.split_count * sizeof(*second_view.splits)) == 0);
	CHECK(memcmp(second_view.response_pairs, repeated_view.response_pairs,
		(size_t)second_view.response_pair_count *
			sizeof(*second_view.response_pairs)) == 0);
	SG_RuneCompactResponsePartitionDestroy(first_partition);
	SG_RuneCompactResponsePartitionDestroy(second_partition);
	SG_RuneCompactResponsePartitionDestroy(repeated_partition);
	SG_RuneCompactGeometryDestroy(first.geometry);
	SG_RuneCompactGeometryDestroy(second.geometry);
	SG_RuneCompactGeometryDestroy(repeated.geometry);
	active_fixture = NULL;
	return 1;
}

static int TestDeterministicReorder(void)
{
	fixture_t first;
	fixture_t second;
	sg_rune_compact_response_partition_t *left = NULL;
	sg_rune_compact_response_partition_t *right = NULL;
	sg_rune_compact_response_partition_view_t left_view;
	sg_rune_compact_response_partition_view_t right_view;
	uint32_t surface;

	InitFixture(&first);
	InitFixture(&second);
	for (surface = 0U; surface < SURFACE_COUNT; surface++)
	{
		const uint32_t vertex_first = second.surfaces[surface].first_vertex;
		const sg_rune_vec3_t vertex = second.hook_vertices[vertex_first];

		second.hook_vertices[vertex_first] =
			second.hook_vertices[vertex_first + 1U];
		second.hook_vertices[vertex_first + 1U] =
			second.hook_vertices[vertex_first + 2U];
		second.hook_vertices[vertex_first + 2U] =
			second.hook_vertices[vertex_first + 3U];
		second.hook_vertices[vertex_first + 3U] = vertex;
	}
	CHECK(FixtureBuildGeometry(&first));
	CHECK(FixtureBuildGeometry(&second));
	active_fixture = &first;
	CHECK(SG_RuneCompactResponsePartitionOwnerBuild(&first.owner,
		&first.geometry_view, NULL, &left, NULL));
	active_fixture = &second;
	CHECK(SG_RuneCompactResponsePartitionOwnerBuild(&second.owner,
		&second.geometry_view, NULL, &right, NULL));
	CHECK(SG_RuneCompactResponsePartitionRead(left, &left_view));
	CHECK(SG_RuneCompactResponsePartitionRead(right, &right_view));
	CHECK(left_view.source_fragment_count == right_view.source_fragment_count);
	CHECK(left_view.target_patch_count == right_view.target_patch_count);
	CHECK(left_view.split_count == right_view.split_count);
	CHECK(left_view.response_pair_count == right_view.response_pair_count);
	CHECK(left_view.candidate_group_count == right_view.candidate_group_count);
	CHECK(left_view.source_endpoint_group_count ==
		right_view.source_endpoint_group_count);
	CHECK(left_view.target_endpoint_group_count ==
		right_view.target_endpoint_group_count);
	CHECK(left_view.source_endpoint_member_count ==
		right_view.source_endpoint_member_count);
	CHECK(left_view.target_endpoint_member_count ==
		right_view.target_endpoint_member_count);
	CHECK(left_view.static_occluder_count == right_view.static_occluder_count);
	CHECK(left_view.static_occluder_side_count ==
		right_view.static_occluder_side_count);
	CHECK(left_view.static_occluder_edge_count ==
		right_view.static_occluder_edge_count);
	CHECK(left_view.source_halfspace_count == right_view.source_halfspace_count);
	CHECK(memcmp(left_view.source_fragments, right_view.source_fragments,
		(size_t)left_view.source_fragment_count *
			sizeof(*left_view.source_fragments)) == 0);
	CHECK(memcmp(left_view.source_halfspaces, right_view.source_halfspaces,
		(size_t)left_view.source_halfspace_count *
			sizeof(*left_view.source_halfspaces)) == 0);
	CHECK(memcmp(left_view.target_patches, right_view.target_patches,
		(size_t)left_view.target_patch_count *
			sizeof(*left_view.target_patches)) == 0);
	CHECK(memcmp(left_view.target_vertices, right_view.target_vertices,
		(size_t)left_view.target_vertex_count *
			sizeof(*left_view.target_vertices)) == 0);
	CHECK(memcmp(left_view.splits, right_view.splits,
		(size_t)left_view.split_count * sizeof(*left_view.splits)) == 0);
	CHECK(left_view.response_pair_count != 0U);
	CHECK(memcmp(left_view.response_pairs, right_view.response_pairs,
		(size_t)left_view.response_pair_count *
			sizeof(*left_view.response_pairs)) == 0);
	CHECK(memcmp(left_view.candidate_groups, right_view.candidate_groups,
		(size_t)left_view.candidate_group_count *
			sizeof(*left_view.candidate_groups)) == 0);
	CHECK(memcmp(left_view.source_endpoint_groups,
		right_view.source_endpoint_groups,
		(size_t)left_view.source_endpoint_group_count *
			sizeof(*left_view.source_endpoint_groups)) == 0);
	CHECK(memcmp(left_view.target_endpoint_groups,
		right_view.target_endpoint_groups,
		(size_t)left_view.target_endpoint_group_count *
			sizeof(*left_view.target_endpoint_groups)) == 0);
	CHECK(memcmp(left_view.source_endpoint_members,
		right_view.source_endpoint_members,
		(size_t)left_view.source_endpoint_member_count *
			sizeof(*left_view.source_endpoint_members)) == 0);
	CHECK(memcmp(left_view.target_endpoint_members,
		right_view.target_endpoint_members,
		(size_t)left_view.target_endpoint_member_count *
			sizeof(*left_view.target_endpoint_members)) == 0);
	CHECK(memcmp(left_view.static_occluders, right_view.static_occluders,
		(size_t)left_view.static_occluder_count *
			sizeof(*left_view.static_occluders)) == 0);
	CHECK(memcmp(left_view.static_occluder_sides,
		right_view.static_occluder_sides,
		(size_t)left_view.static_occluder_side_count *
			sizeof(*left_view.static_occluder_sides)) == 0);
	CHECK(memcmp(left_view.static_occluder_edges,
		right_view.static_occluder_edges,
		(size_t)left_view.static_occluder_edge_count *
			sizeof(*left_view.static_occluder_edges)) == 0);
	CHECK(memcmp(&left_view.seal, &right_view.seal,
		sizeof(left_view.seal)) == 0);
	SG_RuneCompactResponsePartitionDestroy(left);
	SG_RuneCompactResponsePartitionDestroy(right);
	SG_RuneCompactGeometryDestroy(first.geometry);
	SG_RuneCompactGeometryDestroy(second.geometry);
	active_fixture = NULL;
	return 1;
}

static int TestSealRejectsHostileStorage(void)
{
	fixture_t fixture;
	sg_rune_compact_response_partition_t *partition = NULL;
	sg_rune_compact_response_partition_view_t view;
	sg_rune_compact_response_partition_view_t hostile;
	sg_rune_compact_response_fragment_t *fragments;
	sg_rune_compact_response_halfspace_t *halfspaces;
	sg_rune_compact_response_patch_t *patches;
	sg_rune_q8_vec3_t *vertices;
	sg_rune_compact_response_split_t *splits;
	sg_rune_compact_response_pair_t *response_pairs;
	sg_rune_compact_response_candidate_group_t *candidate_groups;
	sg_rune_compact_source_surface_t *source_surfaces;
	sg_rune_q8_vec3_t *source_surface_vertices;
	sg_rune_compact_response_occluder_t *occluders;
	sg_rune_compact_response_occluder_side_t *occluder_sides;
	sg_rune_compact_response_occluder_edge_t *occluder_edges;
	uint32_t direct_pair;
	uint32_t impact_pair;
	uint32_t mover = 0U;
	int success = 0;

	InitFixture(&fixture);
	CHECK(FixtureBuildGeometry(&fixture));
	active_fixture = &fixture;
	CHECK(SG_RuneCompactResponsePartitionOwnerBuild(&fixture.owner,
		&fixture.geometry_view, NULL, &partition, NULL));
	CHECK(SG_RuneCompactResponsePartitionRead(partition, &view));
	CHECK(SG_RuneCompactResponsePartitionSealValid(&view));
	fragments = malloc((size_t)view.source_fragment_count * sizeof(*fragments));
	halfspaces = malloc((size_t)view.source_halfspace_count * sizeof(*halfspaces));
	patches = malloc((size_t)view.target_patch_count * sizeof(*patches));
	vertices = malloc((size_t)view.target_vertex_count * sizeof(*vertices));
	splits = malloc((size_t)view.split_count * sizeof(*splits));
	response_pairs = malloc((size_t)view.response_pair_count *
		sizeof(*response_pairs));
	source_surfaces = malloc((size_t)view.compact_source_surface_count *
		sizeof(*source_surfaces));
	source_surface_vertices = malloc(
		(size_t)view.compact_source_surface_vertex_count *
		sizeof(*source_surface_vertices));
	candidate_groups = malloc((size_t)(view.candidate_group_count + 1U) *
		sizeof(*candidate_groups));
	occluders = malloc((size_t)view.static_occluder_count *
		sizeof(*occluders));
	occluder_sides = malloc((size_t)view.static_occluder_side_count *
		sizeof(*occluder_sides));
	occluder_edges = malloc((size_t)view.static_occluder_edge_count *
		sizeof(*occluder_edges));
	if (fragments == NULL || halfspaces == NULL || patches == NULL ||
		vertices == NULL || splits == NULL || response_pairs == NULL ||
		source_surfaces == NULL ||
		source_surface_vertices == NULL || candidate_groups == NULL ||
		occluders == NULL || occluder_sides == NULL || occluder_edges == NULL)
	{
		free(occluder_edges);
		free(occluder_sides);
		free(occluders);
		free(candidate_groups);
		free(source_surface_vertices);
		free(source_surfaces);
		free(response_pairs);
		free(splits);
		free(vertices);
		free(patches);
		free(halfspaces);
		free(fragments);
		SG_RuneCompactResponsePartitionDestroy(partition);
		SG_RuneCompactGeometryDestroy(fixture.geometry);
		active_fixture = NULL;
		return 0;
	}
	memcpy(fragments, view.source_fragments,
		(size_t)view.source_fragment_count * sizeof(*fragments));
	hostile = view;
	hostile.source_fragments = fragments;
	fragments[0].first_halfspace++;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(fragments, view.source_fragments,
		(size_t)view.source_fragment_count * sizeof(*fragments));
	fragments[0].witness.value[0] = fragments[0].bounds.maxs.value[0] + 1;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(halfspaces, view.source_halfspaces,
		(size_t)view.source_halfspace_count * sizeof(*halfspaces));
	hostile = view;
	hostile.source_halfspaces = halfspaces;
	halfspaces[0].plane.distance_bits = UINT32_C(0x7fc00000);
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(patches, view.target_patches,
		(size_t)view.target_patch_count * sizeof(*patches));
	hostile = view;
	hostile.target_patches = patches;
	patches[0].first_vertex++;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(patches, view.target_patches,
		(size_t)view.target_patch_count * sizeof(*patches));
	patches[0].bounds.mins.value[0]--;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(patches, view.target_patches,
		(size_t)view.target_patch_count * sizeof(*patches));
	patches[0].source_surface = view.compact_source_surface_count;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(patches, view.target_patches,
		(size_t)view.target_patch_count * sizeof(*patches));
	patches[0].parent_facet.value = 0U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	if (view.compact_source_surface_count < 2U)
		goto done;
	memcpy(patches, view.target_patches,
		(size_t)view.target_patch_count * sizeof(*patches));
	patches[0].source_surface = patches[0].source_surface == 0U ? 1U : 0U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	while (mover < view.target_patch_count &&
		(view.target_patches[mover].flags &
			SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING) == 0U)
		mover++;
	if (mover >= view.target_patch_count)
		goto done;
	memcpy(patches, view.target_patches,
		(size_t)view.target_patch_count * sizeof(*patches));
	patches[mover].source_frame = SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(patches, view.target_patches,
		(size_t)view.target_patch_count * sizeof(*patches));
	patches[mover].boundary_incidences.first = 123U;
	patches[mover].boundary_incidences.count = 456U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(vertices, view.target_vertices,
		(size_t)view.target_vertex_count * sizeof(*vertices));
	hostile = view;
	hostile.target_vertices = vertices;
	vertices[0].value[2]++;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(splits, view.splits,
		(size_t)view.split_count * sizeof(*splits));
	hostile = view;
	hostile.splits = splits;
	splits[0].kind = SG_RUNE_COMPACT_RESPONSE_SPLIT_KIND_COUNT;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(splits, view.splits,
		(size_t)view.split_count * sizeof(*splits));
	splits[0].plane.distance_bits = UINT32_C(0x7fc00000);
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(response_pairs, view.response_pairs,
		(size_t)view.response_pair_count * sizeof(*response_pairs));
	hostile = view;
	hostile.response_pairs = response_pairs;
	response_pairs[0].trace.fraction = 0.0f;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	direct_pair = PairWithCertificate(&view,
		SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT);
	impact_pair = PairWithCertificate(&view,
		SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT);
	if (direct_pair == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
		impact_pair == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)
		goto done;
	/* A direct certificate is collision's canonical no-hit trace, so neither
	 * an impact owner nor a target-plane surrogate can be smuggled into it. */
	memcpy(response_pairs, view.response_pairs,
		(size_t)view.response_pair_count * sizeof(*response_pairs));
	response_pairs[direct_pair].trace.brush = 0U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(response_pairs, view.response_pairs,
		(size_t)view.response_pair_count * sizeof(*response_pairs));
	response_pairs[direct_pair].trace.plane.normal[0] = 1.0f;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	/* Static impacts require both exact parts of the owning brush-side key. */
	memcpy(response_pairs, view.response_pairs,
		(size_t)view.response_pair_count * sizeof(*response_pairs));
	response_pairs[impact_pair].trace.brush = SG_HOST_COLLISION_BRUSH_NONE;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(response_pairs, view.response_pairs,
		(size_t)view.response_pair_count * sizeof(*response_pairs));
	response_pairs[impact_pair].trace.brush_side =
		response_pairs[impact_pair].trace.brush_side == 0U ? 1U : 0U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(splits, view.splits,
		(size_t)view.split_count * sizeof(*splits));
	hostile = view;
	hostile.splits = splits;
	splits[view.response_pairs[impact_pair].certificate_split].brush_side =
		splits[view.response_pairs[impact_pair].certificate_split].brush_side ==
			0U ? 1U : 0U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	if (view.split_count > 1U)
	{
		memcpy(splits, view.splits,
			(size_t)view.split_count * sizeof(*splits));
		splits[1] = splits[0];
		if (SG_RuneCompactResponsePartitionSealValid(&hostile))
			goto done;
	}
	{
		uint32_t plane_split = 0U;
		uint32_t edge_split = 0U;
		uint32_t tie_split = 0U;

		while (plane_split < view.split_count && view.splits[plane_split].kind !=
			SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE)
			plane_split++;
		while (edge_split < view.split_count && view.splits[edge_split].kind !=
			SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_EDGE)
			edge_split++;
		while (tie_split < view.split_count && view.splits[tie_split].kind !=
			SG_RUNE_COMPACT_RESPONSE_SPLIT_FIRST_HIT_TIE)
			tie_split++;
		if (plane_split >= view.split_count || edge_split >= view.split_count ||
			tie_split >= view.split_count || view.static_occluder_side_count < 2U ||
			view.static_occluder_edge_count < 2U)
			goto done;
		memcpy(splits, view.splits,
			(size_t)view.split_count * sizeof(*splits));
		hostile = view;
		hostile.splits = splits;
		splits[plane_split].edge = splits[plane_split].edge == 0U ? 1U : 0U;
		if (SG_RuneCompactResponsePartitionSealValid(&hostile))
			goto done;
		memcpy(splits, view.splits,
			(size_t)view.split_count * sizeof(*splits));
		splits[edge_split].edge = splits[edge_split].edge == 0U ? 1U : 0U;
		if (SG_RuneCompactResponsePartitionSealValid(&hostile))
			goto done;
		memcpy(splits, view.splits,
			(size_t)view.split_count * sizeof(*splits));
		splits[tie_split].edge = splits[tie_split].edge == 0U ? 1U : 0U;
		if (SG_RuneCompactResponsePartitionSealValid(&hostile))
			goto done;
	}
	memcpy(occluders, view.static_occluders,
		(size_t)view.static_occluder_count * sizeof(*occluders));
	hostile = view;
	hostile.static_occluders = occluders;
	occluders[0].brush = occluders[0].brush == 0U ? 1U : 0U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(occluder_sides, view.static_occluder_sides,
		(size_t)view.static_occluder_side_count * sizeof(*occluder_sides));
	hostile = view;
	hostile.static_occluder_sides = occluder_sides;
	occluder_sides[0].halfspace_plane.distance_bits ^= UINT32_C(1);
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(occluder_edges, view.static_occluder_edges,
		(size_t)view.static_occluder_edge_count * sizeof(*occluder_edges));
	hostile = view;
	hostile.static_occluder_edges = occluder_edges;
	occluder_edges[0].from.value_bits[0] ^= UINT32_C(1);
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	if (view.candidate_group_count == 0U)
		goto done;
	hostile = view;
	hostile.candidate_group_count--;
	hostile.seal.unresolved_candidate_group_count--;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(candidate_groups, view.candidate_groups,
		(size_t)view.candidate_group_count * sizeof(*candidate_groups));
	candidate_groups[view.candidate_group_count] =
		candidate_groups[view.candidate_group_count - 1U];
	hostile = view;
	hostile.candidate_groups = candidate_groups;
	hostile.candidate_group_count++;
	hostile.seal.unresolved_candidate_group_count++;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(candidate_groups, view.candidate_groups,
		(size_t)view.candidate_group_count * sizeof(*candidate_groups));
	hostile = view;
	hostile.candidate_groups = candidate_groups;
	candidate_groups[0].requires_exact_ray = 2U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(candidate_groups, view.candidate_groups,
		(size_t)view.candidate_group_count * sizeof(*candidate_groups));
	candidate_groups[0].reserved[0] = 1U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(candidate_groups, view.candidate_groups,
		(size_t)view.candidate_group_count * sizeof(*candidate_groups));
	candidate_groups[0].reserved[1] = 1U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(source_surfaces, view.compact_source_surfaces,
		(size_t)view.compact_source_surface_count * sizeof(*source_surfaces));
	hostile = view;
	hostile.compact_source_surfaces = source_surfaces;
	source_surfaces[0].source.plane =
		source_surfaces[0].source.plane == 0U ? 1U : 0U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(source_surfaces, view.compact_source_surfaces,
		(size_t)view.compact_source_surface_count * sizeof(*source_surfaces));
	source_surfaces[0].source.brush =
		source_surfaces[0].source.brush == 0U ? 1U : 0U;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(source_surfaces, view.compact_source_surfaces,
		(size_t)view.compact_source_surface_count * sizeof(*source_surfaces));
	source_surfaces[0].frame =
		source_surfaces[0].frame == SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ?
			SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL :
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(source_surface_vertices, view.compact_source_surface_vertices,
		(size_t)view.compact_source_surface_vertex_count *
			sizeof(*source_surface_vertices));
	hostile = view;
	hostile.compact_source_surface_vertices = source_surface_vertices;
	source_surface_vertices[0].value[2] += 2;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(source_surface_vertices, view.compact_source_surface_vertices,
		(size_t)view.compact_source_surface_vertex_count *
			sizeof(*source_surface_vertices));
	source_surface_vertices[0].value[0]++;
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	memcpy(source_surface_vertices, view.compact_source_surface_vertices,
		(size_t)view.compact_source_surface_vertex_count *
			sizeof(*source_surface_vertices));
	{
		const sg_rune_q8_vec3_t first = source_surface_vertices[0];

		source_surface_vertices[0] = source_surface_vertices[1];
		source_surface_vertices[1] = first;
	}
	if (SG_RuneCompactResponsePartitionSealValid(&hostile))
		goto done;
	success = 1;

done:
	free(occluder_edges);
	free(occluder_sides);
	free(occluders);
	free(candidate_groups);
	free(source_surface_vertices);
	free(source_surfaces);
	free(response_pairs);
	free(splits);
	free(vertices);
	free(patches);
	free(halfspaces);
	free(fragments);
	SG_RuneCompactResponsePartitionDestroy(partition);
	SG_RuneCompactGeometryDestroy(fixture.geometry);
	active_fixture = NULL;
	return success;
}

static int TestAllocationFailures(void)
{
	fixture_t fixture;
	allocation_state_t baseline = { 0U, UINT32_MAX, 0U };
	sg_rune_compact_response_allocator_t allocator = {
		&baseline, TestAllocate, TestRelease
	};
	sg_rune_compact_response_partition_t *partition = NULL;
	uint32_t allocation;

	InitFixture(&fixture);
	CHECK(FixtureBuildGeometry(&fixture));
	active_fixture = &fixture;
	CHECK(SG_RuneCompactResponsePartitionOwnerBuild(&fixture.owner,
		&fixture.geometry_view, &allocator, &partition, NULL));
	SG_RuneCompactResponsePartitionDestroy(partition);
	CHECK(baseline.live == 0U);
	for (allocation = 0U; allocation < baseline.allocations; allocation++)
	{
		allocation_state_t state = { 0U, allocation, 0U };
		sg_rune_compact_response_allocator_t failing = {
			&state, TestAllocate, TestRelease
		};
		sg_rune_compact_response_partition_t *failed = NULL;
		sg_rune_compact_response_error_t error;

		CHECK(!SG_RuneCompactResponsePartitionOwnerBuild(&fixture.owner,
			&fixture.geometry_view, &failing, &failed, &error));
		CHECK(failed == NULL);
		CHECK(error.code == SG_RUNE_COMPACT_RESPONSE_ERROR_OUT_OF_MEMORY);
		CHECK(state.live == 0U);
	}
	SG_RuneCompactGeometryDestroy(fixture.geometry);
	active_fixture = NULL;
	return 1;
}

static int TestSourceCatalogLifetime(void)
{
	fixture_t fixture;
	sg_rune_compact_response_partition_t *partition = NULL;
	sg_rune_compact_response_partition_view_t view;

	InitFixture(&fixture);
	CHECK(FixtureBuildGeometry(&fixture));
	active_fixture = &fixture;
	CHECK(SG_RuneCompactResponsePartitionOwnerBuild(&fixture.owner,
		&fixture.geometry_view, NULL, &partition, NULL));
	SG_RuneCompactGeometryDestroy(fixture.geometry);
	fixture.geometry = NULL;
	memset(&fixture.geometry_view, 0, sizeof(fixture.geometry_view));
	CHECK(SG_RuneCompactResponsePartitionRead(partition, &view));
	CHECK(view.compact_source_surfaces != NULL);
	CHECK(view.compact_source_surface_vertices != NULL);
	CHECK(SG_RuneCompactResponsePartitionSealValid(&view));
	SG_RuneCompactResponsePartitionDestroy(partition);
	active_fixture = NULL;
	return 1;
}

static int TestSourceCatalogSeal(void)
{
	fixture_t fixture;
	sg_rune_compact_source_surface_t surfaces[SURFACE_COUNT];
	sg_rune_q8_vec3_t vertices[HOOK_VERTEX_COUNT];
	uint64_t expected;

	InitFixture(&fixture);
	CHECK(FixtureBuildGeometry(&fixture));
	CHECK(SG_RuneCompactSourceSurfaceCatalogSeal(NULL, 1U, NULL, 0U) == 0U);
	CHECK(SG_RuneCompactSourceSurfaceCatalogSeal(NULL, 0U, NULL, 1U) == 0U);
	CHECK(SG_RuneCompactSourceSurfaceCatalogSeal(NULL, 0U, NULL, 0U) != 0U);
	expected = SG_RuneCompactSourceSurfaceCatalogSeal(
		fixture.geometry_view.source_surfaces,
		fixture.geometry_view.source_surface_count,
		fixture.geometry_view.source_surface_vertices,
		fixture.geometry_view.source_surface_vertex_count);
	CHECK(expected != 0U);
	CHECK(expected == SG_RuneCompactSourceSurfaceCatalogSeal(
		fixture.geometry_view.source_surfaces,
		fixture.geometry_view.source_surface_count,
		fixture.geometry_view.source_surface_vertices,
		fixture.geometry_view.source_surface_vertex_count));
	CHECK(fixture.geometry_view.source_surface_count <= SURFACE_COUNT);
	CHECK(fixture.geometry_view.source_surface_vertex_count <=
		HOOK_VERTEX_COUNT);
	memcpy(surfaces, fixture.geometry_view.source_surfaces,
		(size_t)fixture.geometry_view.source_surface_count * sizeof(*surfaces));
	memcpy(vertices, fixture.geometry_view.source_surface_vertices,
		(size_t)fixture.geometry_view.source_surface_vertex_count *
			sizeof(*vertices));
	surfaces[0].source.plane = surfaces[0].source.plane == 0U ? 1U : 0U;
	CHECK(expected != SG_RuneCompactSourceSurfaceCatalogSeal(surfaces,
		fixture.geometry_view.source_surface_count, vertices,
		fixture.geometry_view.source_surface_vertex_count));
	memcpy(surfaces, fixture.geometry_view.source_surfaces,
		(size_t)fixture.geometry_view.source_surface_count * sizeof(*surfaces));
	vertices[0].value[0]++;
	CHECK(expected != SG_RuneCompactSourceSurfaceCatalogSeal(surfaces,
		fixture.geometry_view.source_surface_count, vertices,
		fixture.geometry_view.source_surface_vertex_count));
	SG_RuneCompactGeometryDestroy(fixture.geometry);
	return 1;
}

static int TestRetainedLifetime(void)
{
	fixture_t fixture;
	allocation_state_t state = { 0U, UINT32_MAX, 0U };
	sg_rune_compact_response_allocator_t allocator = {
		&state, TestAllocate, TestRelease
	};
	sg_rune_compact_response_partition_t *partition = NULL;
	sg_rune_compact_response_partition_view_t view;

	InitFixture(&fixture);
	CHECK(FixtureBuildGeometry(&fixture));
	active_fixture = &fixture;
	CHECK(SG_RuneCompactResponsePartitionOwnerBuild(&fixture.owner,
		&fixture.geometry_view, &allocator, &partition, NULL));
	CHECK(state.live != 0U);
	CHECK(!SG_RuneCompactResponsePartitionRetain(NULL));
	CHECK(SG_RuneCompactResponsePartitionRetain(partition));
	SG_RuneCompactResponsePartitionDestroy(partition);
	CHECK(state.live != 0U);
	CHECK(SG_RuneCompactResponsePartitionRead(partition, &view));
	CHECK(SG_RuneCompactResponsePartitionSealValid(&view));
	SG_RuneCompactResponsePartitionDestroy(partition);
	CHECK(state.live == 0U);
	SG_RuneCompactGeometryDestroy(fixture.geometry);
	active_fixture = NULL;
	return 1;
}

int main(void)
{
	const int skip_exhaustive_oom =
		getenv("SG_RESPONSE_TEST_SKIP_EXHAUSTIVE_OOM") != NULL;

	if (!TestSharedRefinement() || !TestCoplanarImpactProvenance() ||
		!TestDeterministicReorder() ||
		!TestSealRejectsHostileStorage() ||
		(!skip_exhaustive_oom && !TestAllocationFailures()) ||
		!TestSourceCatalogLifetime() || !TestSourceCatalogSeal() ||
		!TestRetainedLifetime())
		return 1;
	puts("compact response partition tests passed");
	return 0;
}
