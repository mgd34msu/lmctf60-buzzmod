/* Era-4 semantic regions.
 *
 * One region per configuration cell, built in one pass from what the cell
 * already carries: its mesh, its witness, its leaf.  Support is the cell's,
 * probed at its floor; water is read at the witness; the samples are the
 * host's leaves at the stance's sample heights.  Boundaries are the cell's
 * brush faces traced back to their source brush sides.  Hook surfaces are
 * the side polygons of every solid brush in every model.
 *
 * There is no partition of a cell into support regions and no mesh rebuilt
 * from constraints; the runtime localizes support and water from the live
 * pose. */
#include "sg_configuration_semantics.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sg_rune_bsp.h"
#include "sg_rune_trace.h"
#include "sg_rune_law.h"

typedef struct semantic_build_s
{
	const sg_rune_law_t *law;
	const sg_rune_bsp_t *world;
	const sg_configuration_space_t *configuration;
	sg_configuration_semantics_limits_t limits;
	sg_configuration_semantics_t *output;
	uint32_t region_capacity, face_capacity, vertex_capacity;
	uint32_t boundary_capacity, hook_surface_capacity, hook_vertex_capacity;
	uint32_t *side_to_brush;
	uint8_t *brush_marks;
	sg_configuration_semantics_error_t error;
} semantic_build_t;

static void SetError(semantic_build_t *build,
	sg_configuration_semantics_error_code_t code, uint32_t source_index)
{
	if (build->error.code == SG_CONFIGURATION_SEMANTICS_ERROR_NONE)
	{
		build->error.code = code;
		build->error.source_index = source_index;
	}
}

static int Grow(void **array, uint32_t *capacity, uint32_t required,
	uint32_t limit, size_t element)
{
	uint32_t next;
	void *grown;

	if (required <= *capacity)
		return 1;
	if (required > limit)
		return 0;
	next = *capacity ? *capacity : 256U;
	while (next < required)
		next = next > limit / 2U ? limit : next * 2U;
	grown = realloc(*array, (size_t)next * element);
	if (!grown)
		return 0;
	*array = grown;
	*capacity = next;
	return 1;
}

static float Dot(const float a[3], const float b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static void Copy3(float out[3], const float in[3])
{
	memcpy(out, in, sizeof(float) * 3U);
}

/* Sample heights above the origin for a stance: just above the feet, half
 * way to the eyes, and at the eyes. */
static int SampleOffsets(const sg_rune_law_t *law,
	sg_cfg_stance_t stance, float offsets[3])
{
	const float *mins;
	float view_height, sample_height;
	int sample2;

	if (stance != SG_CFG_STANDING && stance != SG_CFG_CROUCHING)
		return 0;
	SG_RuneLawHull(law, stance == SG_CFG_CROUCHING, &mins, NULL, &view_height);
	sample_height = view_height - mins[2];
	if (!isfinite(sample_height) || sample_height < (float)INT_MIN ||
		sample_height >= (float)INT_MAX)
		return 0;
	sample2 = (int)sample_height;
	offsets[0] = mins[2] + 1.0f;
	offsets[1] = mins[2] + (float)(sample2 / 2);
	offsets[2] = mins[2] + (float)sample2;
	return 1;
}

static int Pose(const semantic_build_t *build, const float point[3],
	sg_cfg_stance_t stance, sg_rune_pose_t *pose_out)
{
	const float *mins, *maxs;
	float view;

	SG_RuneLawHull(build->law, stance == SG_CFG_CROUCHING, &mins, &maxs, &view);
	SG_RuneTracePose(build->world, point, mins, maxs, view, pose_out);
	return 1;
}

static int HostLeafAtPoint(const sg_rune_bsp_t *world, const float point[3],
	uint32_t *leaf_out)
{
	int32_t child = world->models[0].headnode;

	while (child >= 0)
	{
		const sg_rune_bsp_node_t *node;
		const sg_rune_bsp_plane_t *plane;
		float distance;

		if ((uint32_t)child >= world->node_count)
			return 0;
		node = &world->nodes[child];
		if (node->plane >= world->plane_count)
			return 0;
		plane = &world->planes[node->plane];
		distance = Dot(point, plane->normal) - plane->distance;
		child = node->children[distance < 0.0f];
	}
	*leaf_out = (uint32_t)(-1 - child);
	return *leaf_out < world->leaf_count;
}

/* Support is the cell's, not the witness's: a tall cell over a floor is a
 * place to stand although its centre is in the air.  Probe the host at the
 * cell's lowest origin height under the witness, stepping up a few Q8 steps
 * when the lowest corner is only valid elsewhere on a slope. */
static int CellHasFloor(const semantic_build_t *build,
	const sg_configuration_cell_t *cell, const float witness[3],
	int witness_supported)
{
	static const float rises[] = { 0.125f, 8.0f, 16.0f, 24.0f, 32.0f };
	uint32_t index;

	if (witness_supported)
		return 1;
	for (index = 0; index < sizeof(rises) / sizeof(rises[0]); index++)
	{
		sg_rune_pose_t pose;
		float point[3];

		point[0] = witness[0];
		point[1] = witness[1];
		point[2] = cell->bounds.mins.value[2] + rises[index];
		if (point[2] > witness[2])
			return 0;
		if (!Pose(build, point, cell->stance, &pose) || !pose.valid)
			continue;
		return pose.supported ? 1 : 0;
	}
	return 0;
}

/* ---- regions ------------------------------------------------------------ */

static int AppendRegion(semantic_build_t *build, uint32_t cell_index)
{
	const sg_configuration_space_t *configuration = build->configuration;
	const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
	const sg_rune_bsp_t *world = build->world;
	sg_configuration_semantics_t *output = build->output;
	sg_configuration_semantic_region_t *region;
	sg_rune_pose_t pose;
	float offsets[3];
	uint32_t face, sample;

	if (cell->bsp_leaf.index >= world->leaf_count ||
		cell->first_face > configuration->face_count ||
		cell->face_count > configuration->face_count - cell->first_face)
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
			cell_index);
		return 0;
	}
	if (!Grow((void **)&output->regions, &build->region_capacity,
		output->region_count + 1U, build->limits.max_regions,
		sizeof(*output->regions)))
	{
		SetError(build, output->region_count + 1U > build->limits.max_regions ?
			SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW :
			SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY, cell_index);
		return 0;
	}
	region = &output->regions[output->region_count];
	memset(region, 0, sizeof(*region));
	region->id = ((uint64_t)cell_index << 32) | output->region_count;
	region->cell = cell_index;
	region->bounds = cell->bounds;
	region->interior_witness = cell->interior_witness;
	region->origin_contents = (int32_t)
		world->leaves[cell->bsp_leaf.index].contents;
	region->origin_rune_contents = cell->contents;
	region->first_face = output->face_count;
	/* The cell's mesh is the region's mesh. */
	for (face = 0; face < cell->face_count; face++)
	{
		const uint32_t face_index = cell->first_face + face;
		const sg_configuration_face_t *source =
			&configuration->faces[face_index];
		sg_configuration_semantic_face_t *record;
		uint32_t vertex;

		if (source->first_vertex > configuration->vertex_count ||
			source->vertex_count >
				configuration->vertex_count - source->first_vertex)
		{
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
				face_index);
			return 0;
		}
		if (!Grow((void **)&output->faces, &build->face_capacity,
			output->face_count + 1U, build->limits.max_faces,
			sizeof(*output->faces)) ||
			!Grow((void **)&output->vertices, &build->vertex_capacity,
			output->vertex_count + source->vertex_count,
			build->limits.max_vertices, sizeof(*output->vertices)))
		{
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW,
				face_index);
			return 0;
		}
		record = &output->faces[output->face_count++];
		memset(record, 0, sizeof(*record));
		Copy3(record->normal, source->plane.normal);
		record->distance = source->plane.distance;
		record->first_vertex = output->vertex_count;
		record->vertex_count = source->vertex_count;
		record->source_kind = SG_CONFIGURATION_SEMANTIC_PLANE_CELL;
		record->source_index = face_index;
		record->reversed = (uint8_t)(source->plane.reversed != 0U);
		record->open = (uint8_t)(source->plane.source_kind ==
			SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
			source->plane.reversed != 0U);
		record->kind = source->kind == SG_CONFIGURATION_FACE_CONSTRAINT_ONLY ?
			SG_CONFIGURATION_SEMANTIC_FACE_CONSTRAINT_ONLY :
			SG_CONFIGURATION_SEMANTIC_FACE_FACET;
		for (vertex = 0; vertex < source->vertex_count; vertex++)
			output->vertices[output->vertex_count++] =
				configuration->vertices[source->first_vertex + vertex];
		if (source->plane.source_kind == SG_CONFIGURATION_PLANE_DOMAIN)
			region->flags |= SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT;
	}
	region->face_count = output->face_count - region->first_face;
	/* Samples: the host's leaves at the stance's heights over the witness. */
	if (!SampleOffsets(build->law, cell->stance, offsets))
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
			cell_index);
		return 0;
	}
	for (sample = 0; sample < 3; sample++)
	{
		float point[3];
		uint32_t leaf;

		Copy3(point, cell->interior_witness.value);
		point[2] += offsets[sample];
		if (!HostLeafAtPoint(world, point, &leaf))
		{
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_HOST_DISAGREEMENT,
				cell_index);
			return 0;
		}
		region->sample_leaves[sample] = leaf;
		region->sample_contents[sample] =
			(int32_t)world->leaves[leaf].contents;
		region->sample_areas[sample] = world->leaves[leaf].area;
		region->sample_clusters[sample] = world->leaves[leaf].cluster;
	}
	if (region->sample_contents[0] & SG_RUNE_MASK_WATER)
	{
		region->water_level = 1;
		region->water_type = region->sample_contents[0];
		if (region->sample_contents[1] & SG_RUNE_MASK_WATER)
		{
			region->water_level = 2;
			if (region->sample_contents[2] & SG_RUNE_MASK_WATER)
				region->water_level = 3;
		}
	}
	if (region->water_type & SG_RUNE_CONTENTS_WATER)
		region->flags |= SG_CONFIGURATION_SEMANTIC_REGION_WATER;
	if (region->water_type & SG_RUNE_CONTENTS_LAVA)
		region->flags |= SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
			SG_CONFIGURATION_SEMANTIC_REGION_HAZARD;
	if (region->water_type & SG_RUNE_CONTENTS_SLIME)
		region->flags |= SG_CONFIGURATION_SEMANTIC_REGION_SLIME |
			SG_CONFIGURATION_SEMANTIC_REGION_HAZARD;
	/* Support from the host at the witness, then at the floor. */
	memset(&pose, 0, sizeof(pose));
	if (!Pose(build, cell->interior_witness.value, cell->stance, &pose))
		pose.valid = 0;
	region->flags |= CellHasFloor(build, cell, cell->interior_witness.value,
		pose.valid && pose.supported) ?
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED :
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
	output->region_count++;
	return 1;
}

/* ---- boundaries --------------------------------------------------------- */

static int BuildSideToBrush(semantic_build_t *build)
{
	const sg_rune_bsp_t *world = build->world;
	uint32_t brush;

	build->side_to_brush = malloc((size_t)(world->side_count ?
		world->side_count : 1U) * sizeof(*build->side_to_brush));
	if (!build->side_to_brush)
		return 0;
	memset(build->side_to_brush, 0xFF,
		(size_t)world->side_count * sizeof(*build->side_to_brush));
	for (brush = 0; brush < world->brush_count; brush++)
	{
		const sg_rune_bsp_brush_t *record = &world->brushes[brush];
		uint32_t side;

		if (record->first_side > world->side_count ||
			record->side_count > world->side_count - record->first_side)
			return 0;
		for (side = 0; side < record->side_count; side++)
			build->side_to_brush[record->first_side + side] = brush;
	}
	return 1;
}

/* A cell's brush and domain faces, each traced to what it touches. */
static int AppendBoundaries(semantic_build_t *build, uint32_t cell_index)
{
	const sg_configuration_space_t *configuration = build->configuration;
	const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
	const sg_rune_bsp_t *world = build->world;
	sg_configuration_semantics_t *output = build->output;
	uint32_t local;

	for (local = 0; local < cell->face_count; local++)
	{
		const uint32_t face_index = cell->first_face + local;
		const sg_configuration_face_t *face = &configuration->faces[face_index];
		sg_configuration_boundary_t boundary;

		if (face->plane.source_kind != SG_CONFIGURATION_PLANE_DOMAIN &&
			face->plane.source_kind != SG_CONFIGURATION_PLANE_EXPANDED_BRUSH)
			continue;
		memset(&boundary, 0, sizeof(boundary));
		boundary.id = ((uint64_t)cell_index << 32) | local;
		boundary.cell = cell_index;
		boundary.configuration_face = face_index;
		boundary.brush = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
		boundary.brush_side = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
		boundary.texinfo = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
		Copy3(boundary.origin_normal, face->plane.normal);
		boundary.origin_distance = face->plane.distance;
		if (face->plane.source_kind == SG_CONFIGURATION_PLANE_DOMAIN)
			boundary.flags = SG_CONFIGURATION_BOUNDARY_VOID;
		else
		{
			const sg_rune_bsp_side_t *side;
			const uint32_t side_index = face->plane.source_index;

			if (side_index >= world->side_count ||
				build->side_to_brush[side_index] == UINT32_MAX)
			{
				SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
					face_index);
				return 0;
			}
			side = &world->sides[side_index];
			if (side->plane >= world->plane_count)
			{
				SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
					side_index);
				return 0;
			}
			Copy3(boundary.surface_normal,
				world->planes[side->plane].normal);
			boundary.surface_distance = world->planes[side->plane].distance;
			boundary.brush = build->side_to_brush[side_index];
			boundary.brush_side = side_index;
			boundary.flags = SG_CONFIGURATION_BOUNDARY_PHYSICAL;
			if (boundary.surface_normal[2] >= 0.7f)
				boundary.flags |= SG_CONFIGURATION_BOUNDARY_SUPPORT_CANDIDATE;
			if (side->texinfo >= 0)
			{
				if ((uint32_t)side->texinfo >= world->texinfo_count)
				{
					SetError(build,
						SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
						side_index);
					return 0;
				}
				boundary.texinfo = (uint32_t)side->texinfo;
				boundary.surface_flags =
					world->texinfos[side->texinfo].flags;
			}
		}
		if (!Grow((void **)&output->boundaries, &build->boundary_capacity,
			output->boundary_count + 1U, build->limits.max_boundaries,
			sizeof(*output->boundaries)))
		{
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW,
				face_index);
			return 0;
		}
		output->boundaries[output->boundary_count++] = boundary;
	}
	return 1;
}

/* ---- hook surfaces ------------------------------------------------------ */

typedef struct hook_surface_context_s
{
	semantic_build_t *build;
	uint32_t model;
} hook_surface_context_t;

static int AppendHookSurface(void *context, uint32_t brush,
	uint32_t brush_side, const float (*points)[3], uint32_t count)
{
	hook_surface_context_t *hook = context;
	semantic_build_t *build = hook->build;
	const sg_rune_bsp_t *world = build->world;
	sg_configuration_semantics_t *output = build->output;
	const sg_rune_bsp_side_t *side;
	sg_configuration_hook_surface_t *surface;
	uint32_t vertex, axis;

	if (brush_side >= world->side_count || count < 3U)
		return 1;
	side = &world->sides[brush_side];
	if (side->plane >= world->plane_count)
		return 1;
	if (!Grow((void **)&output->hook_surfaces, &build->hook_surface_capacity,
		output->hook_surface_count + 1U, build->limits.max_hook_surfaces,
		sizeof(*output->hook_surfaces)) ||
		!Grow((void **)&output->hook_vertices, &build->hook_vertex_capacity,
		output->hook_vertex_count + count, build->limits.max_hook_vertices,
		sizeof(*output->hook_vertices)))
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW, brush);
		return 0;
	}
	surface = &output->hook_surfaces[output->hook_surface_count];
	memset(surface, 0, sizeof(*surface));
	surface->id = output->hook_surface_count;
	surface->model = hook->model;
	surface->brush = brush;
	surface->brush_side = brush_side;
	surface->texinfo = SG_CONFIGURATION_SEMANTICS_INDEX_NONE;
	Copy3(surface->normal, world->planes[side->plane].normal);
	surface->distance = world->planes[side->plane].distance;
	surface->first_vertex = output->hook_vertex_count;
	surface->vertex_count = count;
	for (axis = 0; axis < 3; axis++)
	{
		surface->bounds.mins.value[axis] = INFINITY;
		surface->bounds.maxs.value[axis] = -INFINITY;
	}
	for (vertex = 0; vertex < count; vertex++)
	{
		sg_cfg_vec3_t *out = &output->hook_vertices[output->hook_vertex_count +
			vertex];

		for (axis = 0; axis < 3; axis++)
		{
			const float value = points[vertex][axis];

			out->value[axis] = value;
			if (value < surface->bounds.mins.value[axis])
				surface->bounds.mins.value[axis] = value;
			if (value > surface->bounds.maxs.value[axis])
				surface->bounds.maxs.value[axis] = value;
		}
	}
	if (side->texinfo >= 0 && (uint32_t)side->texinfo < world->texinfo_count)
	{
		surface->texinfo = (uint32_t)side->texinfo;
		surface->surface_flags = world->texinfos[side->texinfo].flags;
	}
	if (surface->surface_flags & SG_RUNE_SURF_SKY)
		surface->flags |= SG_CONFIGURATION_HOOK_SURFACE_SKY;
	else
		surface->flags |= SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE;
	if (hook->model != 0U)
		surface->flags |= SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL;
	output->hook_vertex_count += count;
	output->hook_surface_count++;
	return 1;
}

/* Every solid brush of the model, found through its leaves. */
static int MarkModelBrushes(semantic_build_t *build, int32_t node)
{
	const sg_rune_bsp_t *world = build->world;

	while (node >= 0)
	{
		if ((uint32_t)node >= world->node_count)
			return 0;
		if (!MarkModelBrushes(build, world->nodes[node].children[0]))
			return 0;
		node = world->nodes[node].children[1];
	}
	{
		const uint32_t leaf = (uint32_t)(-1 - node);
		const sg_rune_bsp_leaf_t *record;
		uint32_t offset;

		if (leaf >= world->leaf_count)
			return 0;
		record = &world->leaves[leaf];
		for (offset = 0; offset < record->leaf_brush_count; offset++)
		{
			const uint32_t slot = record->first_leaf_brush + offset;

			if (slot >= world->leaf_brush_count ||
				world->leaf_brushes[slot] >= world->brush_count)
				return 0;
			build->brush_marks[world->leaf_brushes[slot]] = 1;
		}
	}
	return 1;
}

static int BuildHookSurfaces(semantic_build_t *build)
{
	const sg_rune_bsp_t *world = build->world;
	uint32_t model, brush;

	build->brush_marks = calloc(world->brush_count ? world->brush_count : 1U,
		sizeof(*build->brush_marks));
	if (!build->brush_marks)
	{
		SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	for (model = 0; model < world->model_count; model++)
	{
		hook_surface_context_t context = { build, model };

		memset(build->brush_marks, 0,
			(size_t)world->brush_count * sizeof(*build->brush_marks));
		if (!MarkModelBrushes(build, world->models[model].headnode))
		{
			SetError(build, SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE,
				model);
			return 0;
		}
		for (brush = 0; brush < world->brush_count; brush++)
		{
			if (!build->brush_marks[brush] ||
				!(world->brushes[brush].contents & SG_RUNE_CONTENTS_SOLID))
				continue;
			if (!SG_ConfigurationBrushPolygons(world, brush, AppendHookSurface,
				&context))
			{
				if (build->error.code == SG_CONFIGURATION_SEMANTICS_ERROR_NONE)
					SetError(build,
						SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY, brush);
				return 0;
			}
		}
	}
	return 1;
}

/* ---- public API --------------------------------------------------------- */

void SG_ConfigurationSemanticsDefaultLimits(
	sg_configuration_semantics_limits_t *limits_out)
{
	if (!limits_out)
		return;
	limits_out->max_regions = UINT32_MAX;
	limits_out->max_faces = UINT32_MAX;
	limits_out->max_vertices = UINT32_MAX;
	limits_out->max_boundaries = UINT32_MAX;
	limits_out->max_hook_surfaces = UINT32_MAX;
	limits_out->max_hook_vertices = UINT32_MAX;
}

int SG_ConfigurationSemanticsBuild(
	const sg_rune_bsp_t *bsp, const sg_rune_law_t *law,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_limits_t *limits,
	sg_configuration_semantics_t **semantics_out,
	sg_configuration_semantics_error_t *error_out)
{
	semantic_build_t build;
	uint32_t cell;
	int ok = 0;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!bsp || !law || !configuration || !limits ||
		!semantics_out || *semantics_out || !limits->max_regions ||
		!limits->max_faces || !limits->max_vertices ||
		!limits->max_boundaries || !limits->max_hook_surfaces ||
		!limits->max_hook_vertices)
	{
		if (error_out)
			error_out->code = SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	memset(&build, 0, sizeof(build));
	build.law = law;
	build.world = bsp;
	build.configuration = configuration;
	build.limits = *limits;
	build.output = calloc(1, sizeof(*build.output));
	if (!build.output || !BuildSideToBrush(&build))
	{
		SetError(&build, SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY, 0U);
		goto done;
	}
	for (cell = 0; cell < configuration->cell_count; cell++)
		if (!AppendRegion(&build, cell) || !AppendBoundaries(&build, cell))
			goto done;
	if (!BuildHookSurfaces(&build))
		goto done;
	ok = 1;

done:
	free(build.side_to_brush);
	free(build.brush_marks);
	if (ok)
		*semantics_out = build.output;
	else
	{
		SG_ConfigurationSemanticsDestroy(build.output);
		if (error_out)
			*error_out = build.error;
	}
	return ok;
}

void SG_ConfigurationSemanticsDestroy(sg_configuration_semantics_t *semantics)
{
	if (!semantics)
		return;
	free(semantics->regions);
	free(semantics->faces);
	free(semantics->vertices);
	free(semantics->boundaries);
	free(semantics->hook_surfaces);
	free(semantics->hook_vertices);
	free(semantics);
}

const char *SG_ConfigurationSemanticsErrorString(
	sg_configuration_semantics_error_code_t code)
{
	switch (code)
	{
	case SG_CONFIGURATION_SEMANTICS_ERROR_NONE: return "none";
	case SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE:
		return "invalid source";
	case SG_CONFIGURATION_SEMANTICS_ERROR_NONFINITE_GEOMETRY:
		return "non-finite geometry";
	case SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW: return "overflow";
	case SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_CONFIGURATION_SEMANTICS_ERROR_SOLVER: return "solver";
	case SG_CONFIGURATION_SEMANTICS_ERROR_HOST_DISAGREEMENT:
		return "host disagreement";
	default: return "unknown configuration semantics error";
	}
}
