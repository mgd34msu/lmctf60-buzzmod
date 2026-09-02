/* Era-4 compact geometry.
 *
 * The configuration space is already a partition of free space into convex
 * cells with one stance validity each, with faces, vertices, and host-
 * validated portals.  Compact geometry is that complex in the model's
 * terms: a compact cell per configuration cell; a shared facet for every
 * portal, carrying the overlap polygon and two incidences; a boundary facet
 * for every remaining face piece; Q8 vertices; and the all-model source
 * surface inventory from the brushes' own side polygons.  Nothing is
 * re-derived and nothing is partitioned again. */
#include "sg_rune_cx_build.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sg_bsp_world.h"
#include "sg_configuration_semantics.h"
#include "sg_configuration_space.h"
#include "sg_host_collision.h"

#define GEOMETRY_STATE UINT32_C(0x47454f4d)

struct sg_rune_cx_s
{
	uint32_t state;
	sg_rune_cx_allocator_t allocator;
	sg_rune_cx_cell_t *cells;
	uint32_t cell_count;
	sg_rune_cx_facet_t *facets;
	uint32_t facet_count;
	sg_rune_cx_incidence_t *incidences;
	uint32_t incidence_count;
	uint32_t *cell_incidences;
	uint32_t cell_incidence_count;
	sg_rune_cx_vec3_t *vertices;
	uint32_t vertex_count;
	sg_rune_cx_portal_t *portals;
	uint32_t portal_count;
	sg_rune_cx_surface_t *surfaces;
	uint32_t surface_count;
	sg_rune_cx_vec3_t *surface_vertices;
	uint32_t surface_vertex_count;
};

typedef struct geometry_build_s
{
	sg_rune_cx_t *geometry;
	const sg_bsp_world_t *world;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	uint32_t facet_capacity, incidence_capacity, vertex_capacity;
	uint32_t portal_capacity, surface_capacity, surface_vertex_capacity;
	uint32_t *side_to_brush;
	uint32_t *portal_of_face;   /* per configuration face: first portal from it */
	uint32_t *face_incidence;   /* per configuration face: scratch */
	sg_rune_cx_error_t error;
} geometry_build_t;

/* ---- allocation through the caller's allocator ------------------------- */

static void *Allocate(const sg_rune_cx_allocator_t *allocator,
	size_t bytes)
{
	if (allocator && allocator->allocate)
		return allocator->allocate(allocator->context, bytes);
	return malloc(bytes);
}

static void Release(const sg_rune_cx_allocator_t *allocator,
	void *memory)
{
	if (!memory)
		return;
	if (allocator && allocator->release)
		allocator->release(allocator->context, memory);
	else
		free(memory);
}

static int Grow(const sg_rune_cx_allocator_t *allocator,
	void **array, uint32_t *capacity, uint32_t required, size_t element)
{
	uint32_t next;
	void *grown;

	if (required <= *capacity)
		return 1;
	next = *capacity ? *capacity : 1024U;
	while (next < required)
	{
		if (next > UINT32_MAX / 2U)
			return 0;
		next *= 2U;
	}
	grown = Allocate(allocator, (size_t)next * element);
	if (!grown)
		return 0;
	if (*array)
	{
		memcpy(grown, *array, (size_t)*capacity * element);
		Release(allocator, *array);
	}
	*array = grown;
	*capacity = next;
	return 1;
}

static void SetError(geometry_build_t *build,
	sg_rune_cx_error_code_t code,
	sg_rune_cx_record_domain_t domain, uint32_t record)
{
	if (build->error.code == SG_RUNE_CX_ERROR_NONE)
	{
		build->error.code = code;
		build->error.domain = domain;
		build->error.record = record;
	}
}

/* ---- conversions -------------------------------------------------------- */

static int32_t Q8(float value)
{
	return (int32_t)lrintf(value * 8.0f);
}

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static sg_rune_cx_plane_t PlaneBits(const float normal[3],
	float distance)
{
	sg_rune_cx_plane_t plane;

	plane.normal_bits[0] = Bits(normal[0]);
	plane.normal_bits[1] = Bits(normal[1]);
	plane.normal_bits[2] = Bits(normal[2]);
	plane.distance_bits = Bits(distance);
	return plane;
}

/* A facet's provenance from the face's construction key. */
static sg_rune_cx_source_t FacetSource(const geometry_build_t *build,
	const sg_configuration_plane_t *plane, uint32_t leaf)
{
	const sg_bsp_world_t *world = build->world;
	sg_rune_cx_source_t source;

	memset(&source, 0, sizeof(source));
	switch (plane->source_kind)
	{
	case SG_CONFIGURATION_PLANE_DOMAIN:
		source.kind = SG_RUNE_CX_SOURCE_DOMAIN;
		source.domain.axis = plane->source_index;
		source.domain.maximum_side = plane->source_variant;
		break;
	case SG_CONFIGURATION_PLANE_BSP:
		source.kind = SG_RUNE_CX_SOURCE_BSP_PLANE;
		source.bsp_plane.model = 0U;
		source.bsp_plane.leaf = leaf;
		source.bsp_plane.plane = plane->source_index;
		break;
	case SG_CONFIGURATION_PLANE_EXPANDED_BRUSH:
	default:
		source.kind = SG_RUNE_CX_SOURCE_EXPANDED_BRUSH_SIDE;
		source.brush_side.model = 0U;
		source.brush_side.brush_side = plane->source_index;
		source.brush_side.brush =
			plane->source_index < world->brush_side_count ?
			build->side_to_brush[plane->source_index] : UINT32_MAX;
		source.brush_side.plane =
			plane->source_index < world->brush_side_count ?
			world->brush_sides[plane->source_index].plane : UINT32_MAX;
		break;
	}
	return source;
}

/* ---- polygons ------------------------------------------------------------ */

typedef struct polygon_s
{
	float (*points)[3];
	uint32_t count;
	uint32_t capacity;
} polygon_t;

static void PolygonFree(polygon_t *polygon)
{
	free(polygon->points);
	memset(polygon, 0, sizeof(*polygon));
}

static int PolygonPush(polygon_t *polygon, const float point[3])
{
	if (polygon->count == polygon->capacity)
	{
		uint32_t capacity = polygon->capacity ? polygon->capacity * 2U : 8U;
		float (*grown)[3] = realloc(polygon->points,
			(size_t)capacity * sizeof(*grown));

		if (!grown)
			return 0;
		polygon->points = grown;
		polygon->capacity = capacity;
	}
	memcpy(polygon->points[polygon->count++], point, sizeof(float) * 3U);
	return 1;
}

static int PolygonFromVertices(polygon_t *polygon,
	const sg_rune_vec3_t *vertices, uint32_t count)
{
	uint32_t index;

	memset(polygon, 0, sizeof(*polygon));
	for (index = 0U; index < count; index++)
		if (!PolygonPush(polygon, vertices[index].value))
			return 0;
	return 1;
}

static float Dot(const float a[3], const float b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static int PolygonHasArea(const polygon_t *polygon)
{
	float total[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t index;

	if (polygon->count < 3U)
		return 0;
	for (index = 1U; index + 1U < polygon->count; index++)
	{
		const float *a = polygon->points[0];
		const float *b = polygon->points[index];
		const float *c = polygon->points[index + 1U];
		float ab[3], ac[3];
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
		{
			ab[axis] = b[axis] - a[axis];
			ac[axis] = c[axis] - a[axis];
		}
		total[0] += ab[1] * ac[2] - ab[2] * ac[1];
		total[1] += ab[2] * ac[0] - ab[0] * ac[2];
		total[2] += ab[0] * ac[1] - ab[1] * ac[0];
	}
	return 0.5f * sqrtf(Dot(total, total)) > 0.01f;
}

/* Splits a polygon by an edge plane in its own plane: the part with
 * n.p <= d goes to inside, the rest to outside.  Returns -1 on allocation
 * failure. */
static int PolygonSplit(const polygon_t *polygon, const float normal[3],
	float distance, polygon_t *inside, polygon_t *outside)
{
	uint32_t index;

	memset(inside, 0, sizeof(*inside));
	memset(outside, 0, sizeof(*outside));
	for (index = 0U; index < polygon->count; index++)
	{
		const float *a = polygon->points[index];
		const float *b = polygon->points[(index + 1U) % polygon->count];
		const float da = Dot(normal, a) - distance;
		const float db = Dot(normal, b) - distance;
		const int side_a = da > 0.01f ? 1 : da < -0.01f ? -1 : 0;
		const int side_b = db > 0.01f ? 1 : db < -0.01f ? -1 : 0;

		if (side_a <= 0 && !PolygonPush(inside, a))
			return -1;
		if (side_a >= 0 && !PolygonPush(outside, a))
			return -1;
		if ((side_a > 0 && side_b < 0) || (side_a < 0 && side_b > 0))
		{
			const float t = da / (da - db);
			float point[3];
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
				point[axis] = a[axis] + t * (b[axis] - a[axis]);
			if (!PolygonPush(inside, point) || !PolygonPush(outside, point))
				return -1;
		}
	}
	return 1;
}

typedef struct piece_list_s
{
	polygon_t *items;
	uint32_t count;
	uint32_t capacity;
} piece_list_t;

static void PieceListFree(piece_list_t *list)
{
	uint32_t index;

	for (index = 0U; index < list->count; index++)
		PolygonFree(&list->items[index]);
	free(list->items);
	memset(list, 0, sizeof(*list));
}

static int PieceListTake(piece_list_t *list, polygon_t *polygon)
{
	if (list->count == list->capacity)
	{
		uint32_t capacity = list->capacity ? list->capacity * 2U : 4U;
		polygon_t *grown = realloc(list->items,
			(size_t)capacity * sizeof(*grown));

		if (!grown)
			return 0;
		list->items = grown;
		list->capacity = capacity;
	}
	list->items[list->count++] = *polygon;
	memset(polygon, 0, sizeof(*polygon));
	return 1;
}

/* ---- output records ------------------------------------------------------ */

static int AppendVertices(geometry_build_t *build, const polygon_t *polygon,
	sg_rune_cx_span_t *span_out)
{
	sg_rune_cx_t *geometry = build->geometry;
	uint32_t index, axis;

	if (!Grow(&geometry->allocator, (void **)&geometry->vertices,
		&build->vertex_capacity, geometry->vertex_count + polygon->count,
		sizeof(*geometry->vertices)))
		return 0;
	span_out->first = geometry->vertex_count;
	span_out->count = polygon->count;
	for (index = 0U; index < polygon->count; index++)
		for (axis = 0U; axis < 3U; axis++)
			geometry->vertices[geometry->vertex_count + index].value[axis] =
				Q8(polygon->points[index][axis]);
	geometry->vertex_count += polygon->count;
	return 1;
}

/* One facet with one or two incidences.  The plane is the negative cell's
 * outward plane; the negative cell is inside n.p <= d. */
static int AppendFacet(geometry_build_t *build, uint32_t negative_cell,
	uint32_t positive_cell, const sg_configuration_plane_t *plane,
	uint32_t leaf, const polygon_t *polygon, uint32_t *facet_out,
	uint32_t *source_incidence_out, uint32_t *destination_incidence_out)
{
	sg_rune_cx_t *geometry = build->geometry;
	sg_rune_cx_facet_t *facet;
	const uint32_t facet_index = geometry->facet_count;
	const uint32_t incidence_index = geometry->incidence_count;
	const uint32_t incidence_count = positive_cell == UINT32_MAX ? 1U : 2U;
	uint32_t side;

	if (!Grow(&geometry->allocator, (void **)&geometry->facets,
		&build->facet_capacity, facet_index + 1U, sizeof(*geometry->facets)) ||
		!Grow(&geometry->allocator, (void **)&geometry->incidences,
		&build->incidence_capacity, incidence_index + incidence_count,
		sizeof(*geometry->incidences)))
		return 0;
	facet = &geometry->facets[facet_index];
	memset(facet, 0, sizeof(*facet));
	facet->source = FacetSource(build, plane, leaf);
	facet->plane = PlaneBits(plane->normal, plane->distance);
	if (!AppendVertices(build, polygon, &facet->vertices))
		return 0;
	facet->incidences.first = incidence_index;
	facet->incidences.count = incidence_count;
	facet->portal = SG_RUNE_CX_INDEX_NONE;
	facet->kind = SG_RUNE_CX_FACET_POLYGON;
	for (side = 0U; side < incidence_count; side++)
	{
		sg_rune_cx_incidence_t *incidence =
			&geometry->incidences[incidence_index + side];

		memset(incidence, 0, sizeof(*incidence));
		incidence->cell = side ? positive_cell : negative_cell;
		incidence->facet = facet_index;
		incidence->cell_ordinal = UINT32_MAX;   /* assigned when cells gather */
		incidence->side = side ? SG_RUNE_CX_POSITIVE_SIDE :
			SG_RUNE_CX_NEGATIVE_SIDE;
		incidence->boundary = plane->source_kind ==
			SG_CONFIGURATION_PLANE_EXPANDED_BRUSH && plane->reversed != 0U ?
			SG_RUNE_CX_BOUNDARY_OPEN : SG_RUNE_CX_BOUNDARY_CLOSED;
	}
	geometry->facet_count++;
	geometry->incidence_count += incidence_count;
	if (facet_out)
		*facet_out = facet_index;
	if (source_incidence_out)
		*source_incidence_out = incidence_index;
	if (destination_incidence_out)
		*destination_incidence_out = incidence_count == 2U ?
			incidence_index + 1U : SG_RUNE_CX_INDEX_NONE;
	return 1;
}

/* ---- cells ---------------------------------------------------------------- */

static int BuildSideToBrush(geometry_build_t *build)
{
	const sg_bsp_world_t *world = build->world;
	uint32_t brush;

	build->side_to_brush = malloc((size_t)(world->brush_side_count ?
		world->brush_side_count : 1U) * sizeof(*build->side_to_brush));
	if (!build->side_to_brush)
		return 0;
	memset(build->side_to_brush, 0xFF,
		(size_t)world->brush_side_count * sizeof(*build->side_to_brush));
	for (brush = 0U; brush < world->brush_count; brush++)
	{
		const sg_bsp_brush_t *record = &world->brushes[brush];
		uint32_t side;

		for (side = 0U; side < record->side_count; side++)
			if (record->first_side + side < world->brush_side_count)
				build->side_to_brush[record->first_side + side] = brush;
	}
	return 1;
}

static int EmitCells(geometry_build_t *build)
{
	sg_rune_cx_t *geometry = build->geometry;
	const sg_configuration_space_t *configuration = build->configuration;
	const sg_configuration_semantics_t *semantics = build->semantics;
	uint32_t cell, boundary, axis;

	geometry->cells = Allocate(&geometry->allocator,
		(size_t)(configuration->cell_count ? configuration->cell_count : 1U) *
		sizeof(*geometry->cells));
	if (!geometry->cells)
	{
		SetError(build, SG_RUNE_CX_ERROR_OUT_OF_MEMORY,
			SG_RUNE_CX_RECORD_CELL, 0U);
		return 0;
	}
	for (cell = 0U; cell < configuration->cell_count; cell++)
	{
		const sg_configuration_cell_t *source = &configuration->cells[cell];
		sg_rune_cx_cell_t *record = &geometry->cells[cell];

		memset(record, 0, sizeof(*record));
		record->source.model = 0U;
		record->source.leaf = source->bsp_leaf.index;
		record->source.area = source->bsp_area.index;
		record->source.cluster = (int32_t)source->bsp_cluster.index;
		record->source.split_ordinal = cell;
		for (axis = 0U; axis < 3U; axis++)
		{
			record->bounds.mins.value[axis] = Q8(source->bounds.mins.value[axis]);
			record->bounds.maxs.value[axis] = Q8(source->bounds.maxs.value[axis]);
		}
		record->contents = (sg_rune_cx_contents_t)source->contents;
		record->valid_stances = source->stance == SG_RUNE_STANCE_STANDING ?
			(sg_rune_cx_stances_t)SG_RUNE_CX_STANCE_ALL :
			(sg_rune_cx_stances_t)SG_RUNE_CX_STANCE_CROUCHING;
		if (cell < semantics->region_count)
		{
			uint32_t flags = semantics->regions[cell].flags;

			if (flags & SG_CONFIGURATION_SEMANTIC_REGION_HAZARD)
				record->semantics |= SG_RUNE_CX_CELL_HAZARD;
			if (flags & SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED)
				record->semantics |= SG_RUNE_CX_CELL_SUPPORTED;
			if (flags & SG_CONFIGURATION_SEMANTIC_REGION_WATER)
				record->semantics |= SG_RUNE_CX_CELL_WATER;
		}
	}
	geometry->cell_count = configuration->cell_count;
	/* Sky and void from the cell's boundaries. */
	for (boundary = 0U; boundary < semantics->boundary_count; boundary++)
	{
		const sg_configuration_boundary_t *record =
			&semantics->boundaries[boundary];

		if (record->cell >= geometry->cell_count)
			continue;
		if (record->flags & SG_CONFIGURATION_BOUNDARY_VOID)
			geometry->cells[record->cell].semantics |=
				SG_RUNE_CX_CELL_VOID_BOUNDARY;
		if (record->surface_flags & SG_HOST_SURFACE_SKY)
			geometry->cells[record->cell].semantics |=
				SG_RUNE_CX_CELL_SKY_BOUNDARY;
	}
	return 1;
}

/* ---- facets and portals --------------------------------------------------- */

/* For every configuration face: the portal facets carved from it, then the
 * remainder as boundary facets.  A portal facet is shared: it is created when
 * the lower-numbered directed portal is seen, and the reverse portal reuses
 * it. */
static int EmitFacets(geometry_build_t *build)
{
	sg_rune_cx_t *geometry = build->geometry;
	const sg_configuration_space_t *configuration = build->configuration;
	uint32_t *face_first_portal;
	uint32_t *portal_next;
	uint32_t *portal_facet;
	uint32_t portal, cell, face_index = 0U;
	int ok = 0;

	/* Portals grouped by the face they leave through.  A portal's face is
	 * the from-cell's face on the portal plane. */
	face_first_portal = malloc((size_t)(configuration->face_count ?
		configuration->face_count : 1U) * sizeof(*face_first_portal));
	portal_next = malloc((size_t)(configuration->portal_count ?
		configuration->portal_count : 1U) * sizeof(*portal_next));
	portal_facet = malloc((size_t)(configuration->portal_count ?
		configuration->portal_count : 1U) * sizeof(*portal_facet));
	if (!face_first_portal || !portal_next || !portal_facet)
		goto out_of_memory;
	memset(face_first_portal, 0xFF,
		(size_t)configuration->face_count * sizeof(*face_first_portal));
	memset(portal_facet, 0xFF,
		(size_t)configuration->portal_count * sizeof(*portal_facet));
	for (portal = 0U; portal < configuration->portal_count; portal++)
	{
		const sg_configuration_portal_t *record = &configuration->portals[portal];
		const sg_configuration_cell_t *from = &configuration->cells[record->from_cell];
		uint32_t local, found = UINT32_MAX;

		for (local = 0U; local < from->face_count; local++)
		{
			const sg_configuration_plane_t *plane =
				&configuration->faces[from->first_face + local].plane;

			if (plane->source_kind == record->plane.source_kind &&
				plane->source_index == record->plane.source_index &&
				plane->source_variant == record->plane.source_variant &&
				Dot(plane->normal, record->plane.normal) > 0.0f)
			{
				found = from->first_face + local;
				break;
			}
		}
		if (found == UINT32_MAX)
		{
			SetError(build, SG_RUNE_CX_ERROR_INVALID_REFERENCE,
				SG_RUNE_CX_RECORD_PORTAL, portal);
			goto done;
		}
		portal_next[portal] = face_first_portal[found];
		face_first_portal[found] = portal;
	}
	geometry->portals = Allocate(&geometry->allocator,
		(size_t)(configuration->portal_count ? configuration->portal_count : 1U) *
		sizeof(*geometry->portals));
	if (!geometry->portals)
		goto out_of_memory;
	memset(geometry->portals, 0,
		(size_t)configuration->portal_count * sizeof(*geometry->portals));
	geometry->portal_count = configuration->portal_count;
	for (cell = 0U; cell < configuration->cell_count; cell++)
	{
		const sg_configuration_cell_t *record = &configuration->cells[cell];
		uint32_t local;

		for (local = 0U; local < record->face_count; local++)
		{
			const sg_configuration_face_t *face =
				&configuration->faces[record->first_face + local];
			piece_list_t pieces, next_pieces;
			polygon_t whole;
			uint32_t at, piece;

			face_index = record->first_face + local;
			memset(&pieces, 0, sizeof(pieces));
			memset(&next_pieces, 0, sizeof(next_pieces));
			if (!PolygonFromVertices(&whole,
				&configuration->vertices[face->first_vertex], face->vertex_count) ||
				!PieceListTake(&pieces, &whole))
			{
				PolygonFree(&whole);
				PieceListFree(&pieces);
				goto out_of_memory;
			}
			for (at = face_first_portal[face_index]; at != UINT32_MAX;
				at = portal_next[at])
			{
				const sg_configuration_portal_t *portal_record =
					&configuration->portals[at];
				polygon_t overlap;
				uint32_t facet, negative, positive, edge;
				uint32_t reverse = UINT32_MAX;

				if (!PolygonFromVertices(&overlap,
					&configuration->vertices[portal_record->first_vertex],
					portal_record->vertex_count))
				{
					PolygonFree(&overlap);
					PieceListFree(&pieces);
					PieceListFree(&next_pieces);
					goto out_of_memory;
				}
				if (portal_facet[at] == UINT32_MAX)
				{
					/* Find the reverse by scanning the other cell's faces'
					 * portal lists for (to -> from) on the same key. */
					const sg_configuration_cell_t *other =
						&configuration->cells[portal_record->to_cell];
					uint32_t other_local;

					for (other_local = 0U; other_local < other->face_count &&
						reverse == UINT32_MAX; other_local++)
					{
						uint32_t scan;

						for (scan = face_first_portal[other->first_face +
							other_local]; scan != UINT32_MAX; scan = portal_next[scan])
							if (configuration->portals[scan].from_cell ==
								portal_record->to_cell &&
								configuration->portals[scan].to_cell ==
								portal_record->from_cell &&
								configuration->portals[scan].vertex_count ==
								portal_record->vertex_count &&
								portal_facet[scan] != UINT32_MAX)
							{
								reverse = scan;
								break;
							}
					}
				}
				if (reverse != UINT32_MAX)
				{
					const sg_rune_cx_facet_t *shared =
						&geometry->facets[portal_facet[reverse]];

					facet = portal_facet[reverse];
					negative = shared->incidences.first;
					positive = shared->incidences.first + 1U;
					portal_facet[at] = facet;
					geometry->portals[at].source = shared->source;
					geometry->portals[at].facet = facet;
					geometry->portals[at].source_incidence = positive;
					geometry->portals[at].destination_incidence = negative;
				}
				else
				{
					if (!AppendFacet(build, cell, portal_record->to_cell,
						&face->plane, record->bsp_leaf.index, &overlap, &facet,
						&negative, &positive))
					{
						PolygonFree(&overlap);
						PieceListFree(&pieces);
						PieceListFree(&next_pieces);
						goto out_of_memory;
					}
					portal_facet[at] = facet;
					geometry->facets[facet].portal = at;
					geometry->portals[at].source = geometry->facets[facet].source;
					geometry->portals[at].facet = facet;
					geometry->portals[at].source_incidence = negative;
					geometry->portals[at].destination_incidence = positive;
				}
				geometry->portals[at].clearance_q8 =
					(uint32_t)lrintf(fmaxf(portal_record->clearance, 0.0f) * 8.0f);
				geometry->portals[at].valid_stances =
					geometry->cells[cell].valid_stances &
					geometry->cells[portal_record->to_cell].valid_stances;
				/* Subtract the overlap from every remainder piece: what is
				 * outside an edge is kept, what is inside all edges is the
				 * portal itself. */
				for (piece = 0U; piece < pieces.count; piece++)
				{
					polygon_t current = pieces.items[piece];

					memset(&pieces.items[piece], 0, sizeof(pieces.items[piece]));
					for (edge = 0U; edge < overlap.count && current.count >= 3U;
						edge++)
					{
						const float *p = overlap.points[edge];
						const float *q = overlap.points[(edge + 1U) % overlap.count];
						float along[3], normal[3], length;
						polygon_t inside, outside;
						uint32_t axis;

						for (axis = 0U; axis < 3U; axis++)
							along[axis] = q[axis] - p[axis];
						/* Outward edge normal of a polygon wound counter-
						 * clockwise about the face normal: edge x normal. */
						normal[0] = along[1] * face->plane.normal[2] -
							along[2] * face->plane.normal[1];
						normal[1] = along[2] * face->plane.normal[0] -
							along[0] * face->plane.normal[2];
						normal[2] = along[0] * face->plane.normal[1] -
							along[1] * face->plane.normal[0];
						length = sqrtf(Dot(normal, normal));
						if (!(length > 0.0f))
							continue;
						for (axis = 0U; axis < 3U; axis++)
							normal[axis] /= length;
						if (PolygonSplit(&current, normal, Dot(normal, p), &inside,
							&outside) < 0)
						{
							PolygonFree(&current);
							PolygonFree(&overlap);
							PieceListFree(&pieces);
							PieceListFree(&next_pieces);
							goto out_of_memory;
						}
						PolygonFree(&current);
						if (PolygonHasArea(&outside) &&
							!PieceListTake(&next_pieces, &outside))
						{
							PolygonFree(&outside);
							PolygonFree(&inside);
							PolygonFree(&overlap);
							PieceListFree(&pieces);
							PieceListFree(&next_pieces);
							goto out_of_memory;
						}
						PolygonFree(&outside);
						current = inside;
					}
					PolygonFree(&current);
				}
				pieces.count = 0U;
				{
					piece_list_t swap = pieces;

					pieces = next_pieces;
					next_pieces = swap;
				}
				PolygonFree(&overlap);
			}
			for (piece = 0U; piece < pieces.count; piece++)
				if (pieces.items[piece].count >= 3U &&
					PolygonHasArea(&pieces.items[piece]) &&
					!AppendFacet(build, cell, UINT32_MAX, &face->plane,
						record->bsp_leaf.index, &pieces.items[piece], NULL, NULL,
						NULL))
				{
					PieceListFree(&pieces);
					PieceListFree(&next_pieces);
					goto out_of_memory;
				}
			PieceListFree(&pieces);
			PieceListFree(&next_pieces);
		}
	}
	ok = 1;
	goto done;

out_of_memory:
	SetError(build, SG_RUNE_CX_ERROR_OUT_OF_MEMORY,
		SG_RUNE_CX_RECORD_FACE, face_index);
done:
	free(face_first_portal);
	free(portal_next);
	free(portal_facet);
	return ok;
}

/* Each cell's incidences gathered into one span, ordinals assigned. */
static int GatherCellIncidences(geometry_build_t *build)
{
	sg_rune_cx_t *geometry = build->geometry;
	uint32_t *counts;
	uint32_t incidence, cell, cursor;

	counts = calloc(geometry->cell_count ? geometry->cell_count : 1U,
		sizeof(*counts));
	geometry->cell_incidences = Allocate(&geometry->allocator,
		(size_t)(geometry->incidence_count ? geometry->incidence_count : 1U) *
		sizeof(*geometry->cell_incidences));
	if (!counts || !geometry->cell_incidences)
	{
		free(counts);
		return 0;
	}
	for (incidence = 0U; incidence < geometry->incidence_count; incidence++)
		counts[geometry->incidences[incidence].cell]++;
	cursor = 0U;
	for (cell = 0U; cell < geometry->cell_count; cell++)
	{
		geometry->cells[cell].incidences.first = cursor;
		geometry->cells[cell].incidences.count = counts[cell];
		cursor += counts[cell];
		counts[cell] = 0U;
	}
	for (incidence = 0U; incidence < geometry->incidence_count; incidence++)
	{
		const uint32_t owner = geometry->incidences[incidence].cell;
		const uint32_t slot = geometry->cells[owner].incidences.first +
			counts[owner];

		geometry->incidences[incidence].cell_ordinal = counts[owner]++;
		geometry->cell_incidences[slot] = incidence;
	}
	geometry->cell_incidence_count = geometry->incidence_count;
	free(counts);
	return 1;
}

/* ---- source surfaces ------------------------------------------------------ */

typedef struct surface_context_s
{
	geometry_build_t *build;
	uint32_t model;
} surface_context_t;

/* A cell rests on a floor when one of its closed facets faces down out of
 * it: the expanded floor plane under the body.  That is the complex's own
 * word on support; the probe-based region flag is kept where it agrees. */
static void MarkSupport(geometry_build_t *build)
{
	sg_rune_cx_t *geometry = build->geometry;
	uint32_t cell;

	for (cell = 0U; cell < geometry->cell_count; cell++)
	{
		sg_rune_cx_cell_t *record = &geometry->cells[cell];
		uint32_t slot;

		for (slot = 0U; slot < record->incidences.count; slot++)
		{
			const sg_rune_cx_incidence_t *incidence = &geometry->incidences[
				geometry->cell_incidences[record->incidences.first + slot]];
			const sg_rune_cx_facet_t *facet = &geometry->facets[incidence->facet];
			float nz;

			if (facet->incidences.count != 1U)
				continue;   /* shared with another cell: not a wall or floor */
			memcpy(&nz, &facet->plane.normal_bits[2], sizeof(nz));
			if (incidence->side == SG_RUNE_CX_POSITIVE_SIDE)
				nz = -nz;
			if (nz <= -0.7f)
			{
				record->semantics |= SG_RUNE_CX_CELL_SUPPORTED;
				break;
			}
		}
	}
}

static int AppendSourceSurface(void *context, uint32_t brush,
	uint32_t brush_side, const float (*points)[3], uint32_t count)
{
	surface_context_t *surface_context = context;
	geometry_build_t *build = surface_context->build;
	sg_rune_cx_t *geometry = build->geometry;
	const sg_bsp_world_t *world = build->world;
	sg_rune_cx_surface_t *surface;
	const sg_bsp_plane_t *plane;
	float normal[3];
	uint32_t index, axis;

	if (brush_side >= world->brush_side_count ||
		world->brush_sides[brush_side].plane >= world->plane_count)
		return 0;
	plane = &world->planes[world->brush_sides[brush_side].plane];
	if (!Grow(&geometry->allocator, (void **)&geometry->surfaces,
		&build->surface_capacity, geometry->surface_count + 1U,
		sizeof(*geometry->surfaces)) ||
		!Grow(&geometry->allocator, (void **)&geometry->surface_vertices,
		&build->surface_vertex_capacity,
		geometry->surface_vertex_count + count,
		sizeof(*geometry->surface_vertices)))
		return 0;
	surface = &geometry->surfaces[geometry->surface_count];
	memset(surface, 0, sizeof(*surface));
	surface->source.model = surface_context->model;
	surface->source.brush = brush;
	surface->source.brush_side = brush_side;
	surface->source.plane = world->brush_sides[brush_side].plane;
	{
		int32_t texinfo = world->brush_sides[brush_side].texinfo;
		int32_t flags = texinfo >= 0 && (uint32_t)texinfo < world->texinfo_count ?
			world->texinfos[texinfo].flags : 0;

		surface->flags = (flags & SG_HOST_SURFACE_SKY) ? SG_RUNE_CX_SURFACE_SKY :
			SG_RUNE_CX_SURFACE_HOOKABLE;
	}
	surface->frame = surface_context->model == 0U ?
		SG_RUNE_CX_SURFACE_WORLD :
		SG_RUNE_CX_SURFACE_MODEL_LOCAL;
	surface->cell = SG_RUNE_CX_INDEX_NONE;
	surface->parent_surface = UINT32_MAX;
	surface->split_ordinal = 0U;
	for (axis = 0U; axis < 3U; axis++)
		normal[axis] = plane->normal.value[axis];
	surface->plane = PlaneBits(normal, plane->distance);
	surface->vertices.first = geometry->surface_vertex_count;
	surface->vertices.count = count;
	for (index = 0U; index < count; index++)
		for (axis = 0U; axis < 3U; axis++)
			geometry->surface_vertices[
				geometry->surface_vertex_count + index].value[axis] =
				Q8(points[index][axis]);
	geometry->surface_vertex_count += count;
	geometry->surface_count++;
	return 1;
}

static int MarkModelBrushes(const sg_bsp_world_t *world, int32_t node,
	uint8_t *marks)
{
	while (node >= 0)
	{
		if ((uint32_t)node >= world->node_count)
			return 0;
		if (!MarkModelBrushes(world, world->nodes[node].children[0], marks))
			return 0;
		node = world->nodes[node].children[1];
	}
	{
		const uint32_t leaf = (uint32_t)(-1 - node);
		const sg_bsp_leaf_t *record;
		uint32_t offset;

		if (leaf >= world->leaf_count)
			return 0;
		record = &world->leaves[leaf];
		for (offset = 0U; offset < record->leaf_brush_count; offset++)
		{
			const uint32_t slot = record->first_leaf_brush + offset;

			if (slot >= world->leaf_brush_count ||
				world->leaf_brushes[slot] >= world->brush_count)
				return 0;
			marks[world->leaf_brushes[slot]] = 1U;
		}
	}
	return 1;
}

static int EmitSourceSurfaces(geometry_build_t *build)
{
	const sg_bsp_world_t *world = build->world;
	uint8_t *marks;
	uint32_t model, brush;
	int ok = 0;

	marks = calloc(world->brush_count ? world->brush_count : 1U, sizeof(*marks));
	if (!marks)
		goto done;
	for (model = 0U; model < world->model_count; model++)
	{
		surface_context_t context = { build, model };

		memset(marks, 0, (size_t)world->brush_count * sizeof(*marks));
		if (!MarkModelBrushes(world, world->models[model].headnode, marks))
		{
			SetError(build, SG_RUNE_CX_ERROR_INVALID_WORLD,
				SG_RUNE_CX_RECORD_WORLD, model);
			goto done;
		}
		for (brush = 0U; brush < world->brush_count; brush++)
		{
			if (!marks[brush] ||
				!(world->brushes[brush].contents & SG_HOST_CONTENTS_SOLID))
				continue;
			if (!SG_ConfigurationBrushPolygons(world, brush, AppendSourceSurface,
				&context))
			{
				SetError(build, SG_RUNE_CX_ERROR_OUT_OF_MEMORY,
					SG_RUNE_CX_RECORD_SOURCE_SURFACE, brush);
				goto done;
			}
		}
	}
	ok = 1;

done:
	free(marks);
	return ok;
}

/* ---- public API --------------------------------------------------------- */


int SG_RuneCxFromSpace(const sg_bsp_world_t *world,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_rune_cx_allocator_t *allocator,
	sg_rune_cx_t **geometry_out,
	sg_rune_cx_error_t *error_out)
{
	geometry_build_t build;
	sg_rune_cx_t *geometry;
	int ok = 0;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!geometry_out)
		return 0;
	*geometry_out = NULL;
	memset(&build, 0, sizeof(build));
	if (!world || !configuration || !semantics)
	{
		if (error_out)
			error_out->code = SG_RUNE_CX_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	geometry = Allocate(allocator, sizeof(*geometry));
	if (!geometry)
	{
		if (error_out)
			error_out->code = SG_RUNE_CX_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	memset(geometry, 0, sizeof(*geometry));
	geometry->state = GEOMETRY_STATE;
	if (allocator)
		geometry->allocator = *allocator;
	build.geometry = geometry;
	build.world = world;
	build.configuration = configuration;
	build.semantics = semantics;
	if (!BuildSideToBrush(&build))
	{
		SetError(&build, SG_RUNE_CX_ERROR_OUT_OF_MEMORY,
			SG_RUNE_CX_RECORD_WORLD, 0U);
		goto done;
	}
	if (!EmitCells(&build) || !EmitFacets(&build) ||
		!GatherCellIncidences(&build))
	{
		if (build.error.code == SG_RUNE_CX_ERROR_NONE)
			SetError(&build, SG_RUNE_CX_ERROR_OUT_OF_MEMORY,
				SG_RUNE_CX_RECORD_RESULT, 0U);
		goto done;
	}
	MarkSupport(&build);
	if (!EmitSourceSurfaces(&build))
	{
		if (build.error.code == SG_RUNE_CX_ERROR_NONE)
			SetError(&build, SG_RUNE_CX_ERROR_OUT_OF_MEMORY,
				SG_RUNE_CX_RECORD_RESULT, 0U);
		goto done;
	}
	ok = 1;

done:
	free(build.side_to_brush);
	if (ok)
		*geometry_out = geometry;
	else
	{
		SG_RuneCxDestroy(geometry);
		if (error_out)
			*error_out = build.error;
	}
	return ok;
}

sg_rune_cx_cell_t *SG_RuneCxCellsMutable(sg_rune_cx_t *geometry)
{
	return geometry ? geometry->cells : NULL;
}

int SG_RuneCxRead(const sg_rune_cx_t *geometry,
	sg_rune_cx_view_t *view_out)
{
	if (!view_out)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	if (!geometry || geometry->state != GEOMETRY_STATE)
		return 0;
	view_out->cells = geometry->cells;
	view_out->cell_count = geometry->cell_count;
	view_out->facets = geometry->facets;
	view_out->facet_count = geometry->facet_count;
	view_out->incidences = geometry->incidences;
	view_out->incidence_count = geometry->incidence_count;
	view_out->cell_incidences = geometry->cell_incidences;
	view_out->cell_incidence_count = geometry->cell_incidence_count;
	view_out->vertices = geometry->vertices;
	view_out->vertex_count = geometry->vertex_count;
	view_out->portals = geometry->portals;
	view_out->portal_count = geometry->portal_count;
	view_out->surfaces = geometry->surfaces;
	view_out->surface_count = geometry->surface_count;
	view_out->surface_vertices = geometry->surface_vertices;
	view_out->surface_vertex_count =
		geometry->surface_vertex_count;
	return 1;
}

void SG_RuneCxDestroy(sg_rune_cx_t *geometry)
{
	sg_rune_cx_allocator_t allocator;

	if (!geometry || geometry->state != GEOMETRY_STATE)
		return;
	allocator = geometry->allocator;
	Release(&allocator, geometry->cells);
	Release(&allocator, geometry->facets);
	Release(&allocator, geometry->incidences);
	Release(&allocator, geometry->cell_incidences);
	Release(&allocator, geometry->vertices);
	Release(&allocator, geometry->portals);
	Release(&allocator, geometry->surfaces);
	Release(&allocator, geometry->surface_vertices);
	geometry->state = 0U;
	Release(&allocator, geometry);
}

const char *SG_RuneCxErrorString(
	sg_rune_cx_error_code_t code)
{
	switch (code)
	{
	case SG_RUNE_CX_ERROR_NONE: return "none";
	case SG_RUNE_CX_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_CX_ERROR_INVALID_CONFIGURATION:
		return "invalid configuration";
	case SG_RUNE_CX_ERROR_INVALID_WORLD: return "invalid world";
	case SG_RUNE_CX_ERROR_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_RUNE_CX_ERROR_NONFINITE_GEOMETRY:
		return "non-finite geometry";
	case SG_RUNE_CX_ERROR_INVALID_GEOMETRY:
		return "invalid geometry";
	case SG_RUNE_CX_ERROR_INVALID_REFERENCE:
		return "invalid reference";
	case SG_RUNE_CX_ERROR_UNSUPPORTED_TOPOLOGY:
		return "unsupported topology";
	case SG_RUNE_CX_ERROR_Q8_CONVERSION:
		return "Q8 conversion";
	case SG_RUNE_CX_ERROR_OVERFLOW: return "overflow";
	case SG_RUNE_CX_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	default: return "unknown compact geometry error";
	}
}
