/* Era-4 cell builder.
 *
 * The configuration space is the set of player origins where the hull fits.
 * This builds it as a cell complex in one linear pass:
 *
 *   1. One carve, with the crouch hull.  Every BSP leaf's polytope (the node
 *      planes down to it, inside the world bounds) is cut by every player-solid
 *      brush expanded by the hull, the world's and the static models'.  What
 *      is outside every brush is free space; each convex piece of it is a
 *      crouching cell.  Lava and slime brushes then split the free pieces:
 *      what is inside one (raised by the feet's depth below the origin) is a
 *      hazard cell, kept apart so no route stands in it.  Nothing is recorded
 *      about how a piece was cut.
 *   2. Vertices are snapped to Q8 once, per final cell.
 *   3. Standing validity by translation.  The standing box shares footprint
 *      and bottom with the crouch box and is 28 taller, so the standing box
 *      fits at p exactly when the crouch box fits at p and at p + (0, 0, 28).
 *      Each crouching cell intersected with a crouching cell translated down
 *      by 28 is a standing cell; the overlap records pair them.
 *   4. Portals from shared faces.  Faces are hashed by their construction key;
 *      opposite faces of two cells on one plane are clipped against each other
 *      and a non-empty overlap is a portal, with a witness stepped into each
 *      cell and validated by the host.
 *
 * There are no certificates, topology records, lattice solves, or audits. */
#include "sg_configuration_space.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "sg_rune_bsp.h"
#include "sg_rune_trace.h"
#include "sg_rune_law.h"

#define CELL_EPSILON 0.01f
#define CELL_AREA_EPSILON 0.001f
#define CELL_STANDING_RISE 28.0f
#define CELL_GRID_SPAN 256.0f
#define CELL_PLAYER_SOLID (SG_RUNE_CONTENTS_SOLID | SG_RUNE_CONTENTS_WINDOW | \
	SG_RUNE_CONTENTS_PLAYERCLIP | SG_RUNE_CONTENTS_MONSTER)

/* ---- transient polytopes ------------------------------------------------ */

typedef struct poly_face_s
{
	sg_configuration_plane_t plane;   /* outward normal: inside is n.p <= d */
	float (*points)[3];
	uint32_t count;
	uint32_t capacity;
} poly_face_t;

typedef struct polytope_s
{
	poly_face_t *faces;
	uint32_t count;
	uint32_t capacity;
} polytope_t;

typedef struct polytope_list_s
{
	polytope_t *items;
	uint32_t count;
	uint32_t capacity;
} polytope_list_t;

static void FaceFree(poly_face_t *face)
{
	free(face->points);
	memset(face, 0, sizeof(*face));
}

static void PolytopeFree(polytope_t *poly)
{
	uint32_t index;

	for (index = 0U; index < poly->count; index++)
		FaceFree(&poly->faces[index]);
	free(poly->faces);
	memset(poly, 0, sizeof(*poly));
}

static void ListFree(polytope_list_t *list)
{
	uint32_t index;

	for (index = 0U; index < list->count; index++)
		PolytopeFree(&list->items[index]);
	free(list->items);
	memset(list, 0, sizeof(*list));
}

static int FacePush(poly_face_t *face, const float point[3])
{
	if (face->count == face->capacity)
	{
		uint32_t capacity = face->capacity ? face->capacity * 2U : 8U;
		float (*grown)[3] = realloc(face->points,
			(size_t)capacity * sizeof(*grown));

		if (!grown)
			return 0;
		face->points = grown;
		face->capacity = capacity;
	}
	memcpy(face->points[face->count++], point, sizeof(float) * 3U);
	return 1;
}

static poly_face_t *PolytopePushFace(polytope_t *poly)
{
	if (poly->count == poly->capacity)
	{
		uint32_t capacity = poly->capacity ? poly->capacity * 2U : 8U;
		poly_face_t *grown = realloc(poly->faces,
			(size_t)capacity * sizeof(*grown));

		if (!grown)
			return NULL;
		poly->faces = grown;
		poly->capacity = capacity;
	}
	memset(&poly->faces[poly->count], 0, sizeof(poly->faces[poly->count]));
	return &poly->faces[poly->count++];
}

static polytope_t *ListPush(polytope_list_t *list)
{
	if (list->count == list->capacity)
	{
		uint32_t capacity = list->capacity ? list->capacity * 2U : 8U;
		polytope_t *grown = realloc(list->items,
			(size_t)capacity * sizeof(*grown));

		if (!grown)
			return NULL;
		list->items = grown;
		list->capacity = capacity;
	}
	memset(&list->items[list->count], 0, sizeof(list->items[list->count]));
	return &list->items[list->count++];
}

/* Moves the polytope into the list; the source is emptied. */
static int ListTake(polytope_list_t *list, polytope_t *poly)
{
	polytope_t *slot = ListPush(list);

	if (!slot)
		return 0;
	*slot = *poly;
	memset(poly, 0, sizeof(*poly));
	return 1;
}

static float Dot(const float a[3], const float b[3])
{
	return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static float PlaneDistance(const sg_configuration_plane_t *plane,
	const float point[3])
{
	return Dot(plane->normal, point) - plane->distance;
}

static void Negate(sg_configuration_plane_t *plane)
{
	plane->normal[0] = -plane->normal[0];
	plane->normal[1] = -plane->normal[1];
	plane->normal[2] = -plane->normal[2];
	plane->distance = -plane->distance;
}

/* The domain box as six faces with polygons. */
static int BoxPolytope(polytope_t *poly, const float mins[3],
	const float maxs[3])
{
	uint32_t axis, side;

	for (axis = 0U; axis < 3U; axis++)
		for (side = 0U; side < 2U; side++)
		{
			poly_face_t *face = PolytopePushFace(poly);
			const uint32_t u = (axis + 1U) % 3U;
			const uint32_t v = (axis + 2U) % 3U;
			uint32_t corner;

			if (!face)
				return 0;
			face->plane.normal[axis] = side ? 1.0f : -1.0f;
			face->plane.distance = side ? maxs[axis] : -mins[axis];
			face->plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
			face->plane.source_index = axis;
			face->plane.source_variant = side;
			for (corner = 0U; corner < 4U; corner++)
			{
				float point[3];
				/* Wind so the polygon faces outward. */
				const uint32_t order = side ? corner : 3U - corner;

				point[axis] = side ? maxs[axis] : mins[axis];
				point[u] = (order == 1U || order == 2U) ? maxs[u] : mins[u];
				point[v] = (order >= 2U) ? maxs[v] : mins[v];
				if (!FacePush(face, point))
					return 0;
			}
		}
	return 1;
}

/* Orders cap points around their centroid in the cutting plane. */
static int OrderCap(poly_face_t *cap)
{
	float centre[3] = { 0.0f, 0.0f, 0.0f };
	float u[3], v[3];
	float *angles;
	uint32_t index, axis, best;

	if (cap->count < 3U)
		return 0;
	for (index = 0U; index < cap->count; index++)
		for (axis = 0U; axis < 3U; axis++)
			centre[axis] += cap->points[index][axis];
	for (axis = 0U; axis < 3U; axis++)
		centre[axis] /= (float)cap->count;
	/* A basis in the plane: pick the axis least aligned with the normal. */
	best = fabsf(cap->plane.normal[0]) < fabsf(cap->plane.normal[1]) ? 0U : 1U;
	if (fabsf(cap->plane.normal[2]) < fabsf(cap->plane.normal[best]))
		best = 2U;
	memset(u, 0, sizeof(u));
	u[best] = 1.0f;
	{
		const float along = Dot(u, cap->plane.normal);
		float length;

		for (axis = 0U; axis < 3U; axis++)
			u[axis] -= along * cap->plane.normal[axis];
		length = sqrtf(Dot(u, u));
		if (!(length > 0.0f))
			return 0;
		for (axis = 0U; axis < 3U; axis++)
			u[axis] /= length;
	}
	v[0] = cap->plane.normal[1] * u[2] - cap->plane.normal[2] * u[1];
	v[1] = cap->plane.normal[2] * u[0] - cap->plane.normal[0] * u[2];
	v[2] = cap->plane.normal[0] * u[1] - cap->plane.normal[1] * u[0];
	angles = malloc((size_t)cap->count * sizeof(*angles));
	if (!angles)
		return -1;
	for (index = 0U; index < cap->count; index++)
	{
		float d[3];

		for (axis = 0U; axis < 3U; axis++)
			d[axis] = cap->points[index][axis] - centre[axis];
		angles[index] = atan2f(Dot(d, v), Dot(d, u));
	}
	/* Insertion sort: caps are small. */
	for (index = 1U; index < cap->count; index++)
	{
		float angle = angles[index];
		float point[3];
		uint32_t at = index;

		memcpy(point, cap->points[index], sizeof(point));
		while (at > 0U && angles[at - 1U] > angle)
		{
			angles[at] = angles[at - 1U];
			memcpy(cap->points[at], cap->points[at - 1U], sizeof(point));
			at--;
		}
		angles[at] = angle;
		memcpy(cap->points[at], point, sizeof(point));
	}
	free(angles);
	/* Drop coincident neighbours. */
	{
		uint32_t kept = 0U;

		for (index = 0U; index < cap->count; index++)
		{
			const float *prev = cap->points[kept ? kept - 1U : cap->count - 1U];
			float d[3];

			for (axis = 0U; axis < 3U; axis++)
				d[axis] = cap->points[index][axis] - prev[axis];
			if (kept != 0U && Dot(d, d) < CELL_EPSILON * CELL_EPSILON)
				continue;
			if (kept != index)
				memcpy(cap->points[kept], cap->points[index], sizeof(d));
			kept++;
		}
		cap->count = kept;
	}
	return cap->count >= 3U ? 1 : 0;
}

/* Splits one polygon by the plane.  Points with n.p - d <= 0 go inside.
 * Returns -1 on allocation failure. */
static int SplitFace(const poly_face_t *face,
	const sg_configuration_plane_t *plane, poly_face_t *inside,
	poly_face_t *outside, poly_face_t *cap_in, poly_face_t *cap_out)
{
	uint32_t index;

	for (index = 0U; index < face->count; index++)
	{
		const float *a = face->points[index];
		const float *b = face->points[(index + 1U) % face->count];
		const float da = PlaneDistance(plane, a);
		const float db = PlaneDistance(plane, b);
		const int side_a = da > CELL_EPSILON ? 1 : da < -CELL_EPSILON ? -1 : 0;
		const int side_b = db > CELL_EPSILON ? 1 : db < -CELL_EPSILON ? -1 : 0;

		if (side_a <= 0 && !FacePush(inside, a))
			return -1;
		if (side_a >= 0 && !FacePush(outside, a))
			return -1;
		if (side_a == 0)
		{
			if (!FacePush(cap_in, a) || !FacePush(cap_out, a))
				return -1;
		}
		if ((side_a > 0 && side_b < 0) || (side_a < 0 && side_b > 0))
		{
			const float t = da / (da - db);
			float point[3];
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
				point[axis] = a[axis] + t * (b[axis] - a[axis]);
			if (!FacePush(inside, point) || !FacePush(outside, point) ||
				!FacePush(cap_in, point) || !FacePush(cap_out, point))
				return -1;
		}
	}
	return 1;
}

static int FaceCoplanar(const poly_face_t *face,
	const sg_configuration_plane_t *plane)
{
	uint32_t index;

	for (index = 0U; index < face->count; index++)
		if (fabsf(PlaneDistance(plane, face->points[index])) > CELL_EPSILON)
			return 0;
	return 1;
}

static int FaceHasArea(const poly_face_t *face)
{
	float total[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t index;

	if (face->count < 3U)
		return 0;
	for (index = 1U; index + 1U < face->count; index++)
	{
		const float *a = face->points[0];
		const float *b = face->points[index];
		const float *c = face->points[index + 1U];
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
	return 0.5f * sqrtf(Dot(total, total)) > CELL_AREA_EPSILON;
}

/* Splits the polytope by the plane into the inside part (n.p <= d) and the
 * outside part.  Either may come back empty.  The cap faces carry the given
 * plane key.  Returns -1 on allocation failure. */
static int SplitPolytope(const polytope_t *poly,
	const sg_configuration_plane_t *plane, polytope_t *inside,
	polytope_t *outside)
{
	poly_face_t cap_in, cap_out;
	uint32_t index;
	int any_in = 0, any_out = 0;

	memset(inside, 0, sizeof(*inside));
	memset(outside, 0, sizeof(*outside));
	memset(&cap_in, 0, sizeof(cap_in));
	memset(&cap_out, 0, sizeof(cap_out));
	cap_in.plane = *plane;
	cap_out.plane = *plane;
	Negate(&cap_out.plane);
	for (index = 0U; index < poly->count; index++)
	{
		const poly_face_t *face = &poly->faces[index];
		poly_face_t in_face, out_face;
		int result;

		memset(&in_face, 0, sizeof(in_face));
		memset(&out_face, 0, sizeof(out_face));
		in_face.plane = face->plane;
		out_face.plane = face->plane;
		if (FaceCoplanar(face, plane))
		{
			poly_face_t *slot;
			uint32_t point;

			/* The face lies in the cutting plane: it bounds the polytope
			 * on the side its normal faces, and it is not a cap. */
			slot = Dot(face->plane.normal, plane->normal) > 0.0f ?
				PolytopePushFace(inside) : PolytopePushFace(outside);
			if (!slot)
				goto failure;
			slot->plane = face->plane;
			for (point = 0U; point < face->count; point++)
				if (!FacePush(slot, face->points[point]))
					goto failure;
			if (Dot(face->plane.normal, plane->normal) > 0.0f)
				any_in = 1;
			else
				any_out = 1;
			continue;
		}
		result = SplitFace(face, plane, &in_face, &out_face, &cap_in, &cap_out);
		if (result < 0)
			goto failure;
		if (FaceHasArea(&in_face))
		{
			poly_face_t *slot = PolytopePushFace(inside);

			if (!slot)
				goto failure;
			*slot = in_face;
			memset(&in_face, 0, sizeof(in_face));
			any_in = 1;
		}
		if (FaceHasArea(&out_face))
		{
			poly_face_t *slot = PolytopePushFace(outside);

			if (!slot)
				goto failure;
			*slot = out_face;
			memset(&out_face, 0, sizeof(out_face));
			any_out = 1;
		}
		FaceFree(&in_face);
		FaceFree(&out_face);
	}
	if (any_in && any_out)
	{
		int ordered_in = OrderCap(&cap_in);
		int ordered_out = OrderCap(&cap_out);
		poly_face_t *slot;

		if (ordered_in < 0 || ordered_out < 0)
			goto failure;
		if (ordered_in && FaceHasArea(&cap_in))
		{
			slot = PolytopePushFace(inside);
			if (!slot)
				goto failure;
			*slot = cap_in;
			memset(&cap_in, 0, sizeof(cap_in));
		}
		if (ordered_out && FaceHasArea(&cap_out))
		{
			slot = PolytopePushFace(outside);
			if (!slot)
				goto failure;
			*slot = cap_out;
			memset(&cap_out, 0, sizeof(cap_out));
		}
	}
	FaceFree(&cap_in);
	FaceFree(&cap_out);
	/* A polytope needs four faces to enclose anything. */
	if (inside->count < 4U)
		PolytopeFree(inside);
	if (outside->count < 4U)
		PolytopeFree(outside);
	return 1;

failure:
	FaceFree(&cap_in);
	FaceFree(&cap_out);
	PolytopeFree(inside);
	PolytopeFree(outside);
	return -1;
}

/* ---- the body at a point ----------------------------------------------------- */

static const sg_rune_law_t *sg_cells_law;
static const sg_rune_bsp_t *sg_cells_bsp;

static int Pose(const sg_rune_law_t *law, const float point[3],
	sg_cfg_stance_t stance, sg_rune_pose_t *pose_out)
{
	const float *mins, *maxs;
	float view;

	SG_RuneLawHull(law, stance == SG_CFG_CROUCHING, &mins, &maxs, &view);
	SG_RuneTracePose(sg_cells_bsp, point, mins, maxs, view, pose_out);
	return 1;
}

static int Clear(const sg_rune_law_t *law, const float from[3], const float to[3],
	sg_cfg_stance_t stance)
{
	const float *mins, *maxs;

	SG_RuneLawHull(law, stance == SG_CFG_CROUCHING, &mins, &maxs, NULL);
	return SG_RuneTraceClear(sg_cells_bsp, from, to, mins, maxs);
}

/* ---- build state --------------------------------------------------------- */

/* A brush reaching the leaf being carved, with the origin of the model it
 * belongs to (the world's is zero). */
typedef struct gathered_brush_s
{
	uint32_t brush;
	float origin[3];
} gathered_brush_t;

#define CELL_HAZARD (SG_RUNE_CONTENTS_LAVA | SG_RUNE_CONTENTS_SLIME)

typedef struct cell_build_s
{
	const sg_rune_law_t *law;
	const sg_rune_bsp_t *world;
	sg_configuration_limits_t limits;
	sg_configuration_space_t *space;
	sg_configuration_progress_fn progress;
	void *progress_context;
	uint32_t cell_capacity, face_capacity, vertex_capacity, portal_capacity;
	uint32_t overlap_capacity;
	uint32_t *brush_marks;        /* per brush: leaf + 1 that last gathered it */
	gathered_brush_t *brush_list; /* player-solid, reaching the leaf */
	uint32_t brush_list_count, brush_list_capacity;
	gathered_brush_t *hazard_list; /* lava and slime, reaching the leaf */
	uint32_t hazard_list_count, hazard_list_capacity;
	sg_cfg_hull_t hazard_hull;    /* the feet's depth: lava rises by it */
	uint32_t leaves_done, last_percent;
	sg_configuration_error_t error;
} cell_build_t;

static void SetError(cell_build_t *build, sg_configuration_error_code_t code,
	uint32_t source_index)
{
	if (build->error.code == SG_CONFIGURATION_ERROR_NONE)
	{
		build->error.code = code;
		build->error.source_index = source_index;
	}
}

static int GrowArray(void **array, uint32_t *capacity, uint32_t required,
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

static float Q8(float value)
{
	return (float)lrintf(value * 8.0f) * 0.125f;
}

/* ---- brush gathering ----------------------------------------------------- */

/* Gathers the brushes of one model's tree that reach the box (given in
 * the model's own frame): player-solid ones to the brush list, lava and
 * slime to the hazard list, each with the model's origin. */
static int GatherBrushesInBox(cell_build_t *build, int32_t node,
	const float mins[3], const float maxs[3], uint32_t leaf_mark,
	const float origin[3])
{
	const sg_rune_bsp_t *world = build->world;

	while (node >= 0)
	{
		const sg_rune_bsp_node_t *record;
		const sg_rune_bsp_plane_t *plane;
		float low = 0.0f, high = 0.0f;
		uint32_t axis;

		if ((uint32_t)node >= world->node_count)
			return 0;
		record = &world->nodes[node];
		if (record->plane >= world->plane_count)
			return 0;
		plane = &world->planes[record->plane];
		for (axis = 0U; axis < 3U; axis++)
		{
			const float n = plane->normal[axis];

			low += n < 0.0f ? n * maxs[axis] : n * mins[axis];
			high += n < 0.0f ? n * mins[axis] : n * maxs[axis];
		}
		if (low > plane->distance)
			node = record->children[0];
		else if (high < plane->distance)
			node = record->children[1];
		else
		{
			if (!GatherBrushesInBox(build, record->children[0], mins, maxs,
				leaf_mark, origin))
				return 0;
			node = record->children[1];
		}
	}
	{
		const uint32_t leaf = (uint32_t)(-1 - node);
		const sg_rune_bsp_leaf_t *record;
		uint32_t offset;

		if (leaf >= world->leaf_count)
			return 0;
		record = &world->leaves[leaf];
		for (offset = 0U; offset < record->leaf_brush_count; offset++)
		{
			const uint32_t slot = record->first_leaf_brush + offset;
			uint32_t brush;

			if (slot >= world->leaf_brush_count)
				return 0;
			brush = world->leaf_brushes[slot];
			if (brush >= world->brush_count ||
				build->brush_marks[brush] == leaf_mark)
				continue;
			if (world->brushes[brush].contents & CELL_PLAYER_SOLID)
			{
				gathered_brush_t *entry;

				build->brush_marks[brush] = leaf_mark;
				if (!GrowArray((void **)&build->brush_list,
					&build->brush_list_capacity, build->brush_list_count + 1U,
					UINT32_MAX, sizeof(*build->brush_list)))
					return 0;
				entry = &build->brush_list[build->brush_list_count++];
				entry->brush = brush;
				memcpy(entry->origin, origin, sizeof(entry->origin));
			}
			else if (world->brushes[brush].contents & CELL_HAZARD)
			{
				gathered_brush_t *entry;

				build->brush_marks[brush] = leaf_mark;
				if (!GrowArray((void **)&build->hazard_list,
					&build->hazard_list_capacity, build->hazard_list_count + 1U,
					UINT32_MAX, sizeof(*build->hazard_list)))
					return 0;
				entry = &build->hazard_list[build->hazard_list_count++];
				entry->brush = brush;
				memcpy(entry->origin, origin, sizeof(entry->origin));
			}
		}
	}
	return 1;
}

/* The brushes of the world and of every static model that reach a leaf's
 * hull-expanded box. */
static int GatherLeafBrushes(cell_build_t *build, const float mins[3],
	const float maxs[3], uint32_t leaf_mark)
{
	const sg_rune_bsp_t *world = build->world;
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t index;

	build->brush_list_count = 0U;
	build->hazard_list_count = 0U;
	if (!GatherBrushesInBox(build, world->models[0].headnode, mins, maxs,
		leaf_mark, zero))
		return 0;
	for (index = 0U; index < world->static_count; index++)
	{
		const sg_rune_bsp_static_t *fixed = &world->statics[index];
		float local_mins[3], local_maxs[3];
		uint32_t axis;

		if (fixed->model >= world->model_count)
			continue;
		for (axis = 0U; axis < 3U; axis++)
		{
			local_mins[axis] = mins[axis] - fixed->origin[axis];
			local_maxs[axis] = maxs[axis] - fixed->origin[axis];
		}
		if (!GatherBrushesInBox(build, world->models[fixed->model].headnode,
			local_mins, local_maxs, leaf_mark, fixed->origin))
			return 0;
	}
	return 1;
}

/* ---- carving one leaf ---------------------------------------------------- */

static float HullOffset(const sg_cfg_hull_t *hull,
	const float normal[3])
{
	float result = 0.0f;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		result += normal[axis] < 0.0f ?
			normal[axis] * hull->maxs.value[axis] :
			normal[axis] * hull->mins.value[axis];
	return result;
}

#define SIDE_ORDER_MAX 64U
#define CLEAR_EPSILON 0.75f      /* a piece a side grazes by less than this is outside it: no sub-unit slivers */
#define SAME_PLANE_DISTANCE 0.0625f /* two expansions of one side this close apart are one plane */

/* The order a brush's sides are cut in: walls first, then ceilings, floors
 * last.  A piece outside a side is taken as soon as that side is cut, so
 * the piece above a floor side is cut last of all and is confined to the
 * floor's own footprint: a cell that stands on a floor stands on it
 * everywhere, and the edge of the floor is the edge of the cell.  Cut
 * first, the piece above the floor would reach out over whatever lies
 * beside the brush, and a body walking it would walk off the edge. */
static uint32_t OrderSides(const sg_rune_bsp_t *world,
	const sg_rune_bsp_brush_t *brush, uint32_t order[SIDE_ORDER_MAX])
{
	uint32_t count = brush->side_count < SIDE_ORDER_MAX ? brush->side_count :
		SIDE_ORDER_MAX;
	uint32_t rank, filled = 0U;

	for (rank = 0U; rank < 3U; rank++)
	{
		uint32_t side;

		for (side = 0U; side < count; side++)
		{
			const uint32_t side_index = brush->first_side + side;
			float nz = 0.0f;
			uint32_t this_rank;

			if (side_index < world->side_count &&
				world->sides[side_index].plane < world->plane_count)
				nz = world->planes[world->sides[side_index].plane].normal[2];
			this_rank = nz >= 0.7f ? 2U : (nz <= -0.7f ? 1U : 0U);
			if (this_rank == rank)
				order[filled++] = side;
		}
	}
	return filled;
}

/* Subtracts one hull-expanded brush from every polytope in the list.  Pieces
 * outside any side are free and go to the output; what is inside every side
 * is solid and is dropped. */
/* Whether a piece lies wholly outside one side of the expanded brush: the
 * brush then touches none of it, and its planes must not cut it.  A slab's
 * wall plane otherwise splits the free space above the slab into a sliver
 * and a cell that never pair as a portal. */
static int PieceClearOfBrush(const cell_build_t *build,
	const gathered_brush_t *entry, const sg_cfg_hull_t *hull,
	const polytope_t *piece)
{
	const sg_rune_bsp_t *world = build->world;
	const sg_rune_bsp_brush_t *brush = &world->brushes[entry->brush];
	uint32_t k;

	for (k = 0U; k < brush->side_count; k++)
	{
		const uint32_t side_index = brush->first_side + k;
		const sg_rune_bsp_side_t *record;
		const sg_rune_bsp_plane_t *source;
		float normal[3], distance, lowest = INFINITY;
		uint32_t face, vertex, axis;

		if (side_index >= world->side_count)
			return 0;
		record = &world->sides[side_index];
		if (record->plane >= world->plane_count)
			return 0;
		source = &world->planes[record->plane];
		for (axis = 0U; axis < 3U; axis++)
			normal[axis] = source->normal[axis];
		distance = source->distance - HullOffset(hull, normal) +
			normal[0] * entry->origin[0] + normal[1] * entry->origin[1] +
			normal[2] * entry->origin[2];
		for (face = 0U; face < piece->count; face++)
			for (vertex = 0U; vertex < piece->faces[face].count; vertex++)
			{
				const float *p = piece->faces[face].points[vertex];
				float d = normal[0] * p[0] + normal[1] * p[1] + normal[2] * p[2] - distance;

				if (d < lowest)
					lowest = d;
			}
		/* Every vertex on or beyond this side: nothing of the piece is
		 * inside the brush. */
		if (lowest >= -CLEAR_EPSILON)
			return 1;
	}
	return 0;
}

static int SubtractBrush(cell_build_t *build, const gathered_brush_t *entry,
	const sg_cfg_hull_t *hull, polytope_list_t *pieces,
	polytope_list_t *out)
{
	const sg_rune_bsp_t *world = build->world;
	const uint32_t brush_index = entry->brush;
	const sg_rune_bsp_brush_t *brush = &world->brushes[brush_index];
	uint32_t order[SIDE_ORDER_MAX];
	const uint32_t ordered = OrderSides(world, brush, order);
	uint32_t piece;

	if (ordered < brush->side_count)
	{
		SetError(build, SG_CONFIGURATION_ERROR_INVALID_WORLD, brush_index);
		return 0;
	}
	for (piece = 0U; piece < pieces->count; piece++)
	{
		polytope_t current = pieces->items[piece];
		uint32_t rank;

		memset(&pieces->items[piece], 0, sizeof(pieces->items[piece]));
		if (PieceClearOfBrush(build, entry, hull, &current))
		{
			if (!ListTake(out, &current))
			{
				PolytopeFree(&current);
				SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY,
					brush_index);
				return 0;
			}
			continue;
		}
		for (rank = 0U; rank < ordered && current.count; rank++)
		{
			const uint32_t side_index = brush->first_side + order[rank];
			const sg_rune_bsp_side_t *record;
			const sg_rune_bsp_plane_t *source;
			sg_configuration_plane_t plane;
			polytope_t inside, outside;
			uint32_t axis;

			if (side_index >= world->side_count)
				goto invalid;
			record = &world->sides[side_index];
			if (record->plane >= world->plane_count)
				goto invalid;
			source = &world->planes[record->plane];
			memset(&plane, 0, sizeof(plane));
			for (axis = 0U; axis < 3U; axis++)
				plane.normal[axis] = source->normal[axis];
			/* Inside the expanded brush: n.p <= d - hull offset, the brush
			 * standing at its model's origin. */
			plane.distance = source->distance - HullOffset(hull, plane.normal) +
				plane.normal[0] * entry->origin[0] +
				plane.normal[1] * entry->origin[1] +
				plane.normal[2] * entry->origin[2];
			plane.source_kind = SG_CONFIGURATION_PLANE_EXPANDED_BRUSH;
			plane.source_index = side_index;
			plane.source_variant = SG_CFG_CROUCHING;
			if (SplitPolytope(&current, &plane, &inside, &outside) < 0)
			{
				PolytopeFree(&current);
				SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY,
					brush_index);
				return 0;
			}
			PolytopeFree(&current);
			if (outside.count)
			{
				/* The face bounding the free piece is the open side. */
				uint32_t face;

				for (face = 0U; face < outside.count; face++)
					if (outside.faces[face].plane.source_kind ==
						SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
						outside.faces[face].plane.source_index == side_index)
						outside.faces[face].plane.reversed = 1U;
				if (!ListTake(out, &outside))
				{
					PolytopeFree(&outside);
					PolytopeFree(&inside);
					SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY,
						brush_index);
					return 0;
				}
			}
			current = inside;
		}
		/* Inside every side: solid. */
		PolytopeFree(&current);
		continue;

invalid:
		PolytopeFree(&current);
		SetError(build, SG_CONFIGURATION_ERROR_INVALID_WORLD, brush_index);
		return 0;
	}
	pieces->count = 0U;
	return 1;
}

/* Splits every free piece by one lava or slime brush raised by the feet's
 * depth: what is inside every side is where a body's feet are in it and
 * goes to the hazard list; the rest stays free.  Both are kept. */
static int SplitHazard(cell_build_t *build, const gathered_brush_t *entry,
	polytope_list_t *pieces, polytope_list_t *free_out,
	polytope_list_t *hazard_out)
{
	const sg_rune_bsp_t *world = build->world;
	const uint32_t brush_index = entry->brush;
	const sg_rune_bsp_brush_t *brush = &world->brushes[brush_index];
	uint32_t piece;

	for (piece = 0U; piece < pieces->count; piece++)
	{
		polytope_t current = pieces->items[piece];
		uint32_t side;

		memset(&pieces->items[piece], 0, sizeof(pieces->items[piece]));
		for (side = 0U; side < brush->side_count && current.count; side++)
		{
			const uint32_t side_index = brush->first_side + side;
			const sg_rune_bsp_side_t *record;
			const sg_rune_bsp_plane_t *source;
			sg_configuration_plane_t plane;
			polytope_t inside, outside;
			uint32_t axis;

			if (side_index >= world->side_count)
				goto invalid;
			record = &world->sides[side_index];
			if (record->plane >= world->plane_count)
				goto invalid;
			source = &world->planes[record->plane];
			memset(&plane, 0, sizeof(plane));
			for (axis = 0U; axis < 3U; axis++)
				plane.normal[axis] = source->normal[axis];
			plane.distance = source->distance -
				HullOffset(&build->hazard_hull, plane.normal) +
				plane.normal[0] * entry->origin[0] +
				plane.normal[1] * entry->origin[1] +
				plane.normal[2] * entry->origin[2];
			plane.source_kind = SG_CONFIGURATION_PLANE_EXPANDED_BRUSH;
			plane.source_index = side_index;
			plane.source_variant = SG_CFG_CROUCHING;
			if (SplitPolytope(&current, &plane, &inside, &outside) < 0)
			{
				PolytopeFree(&current);
				SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY,
					brush_index);
				return 0;
			}
			PolytopeFree(&current);
			if (outside.count)
			{
				uint32_t face;

				for (face = 0U; face < outside.count; face++)
					if (outside.faces[face].plane.source_kind ==
						SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
						outside.faces[face].plane.source_index == side_index)
						outside.faces[face].plane.reversed = 1U;
				if (!ListTake(free_out, &outside))
				{
					PolytopeFree(&outside);
					PolytopeFree(&inside);
					SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY,
						brush_index);
					return 0;
				}
			}
			current = inside;
		}
		if (current.count && !ListTake(hazard_out, &current))
		{
			PolytopeFree(&current);
			SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, brush_index);
			return 0;
		}
		continue;

invalid:
		PolytopeFree(&current);
		SetError(build, SG_CONFIGURATION_ERROR_INVALID_WORLD, brush_index);
		return 0;
	}
	pieces->count = 0U;
	return 1;
}

static int EmitCell(cell_build_t *build, const polytope_t *poly,
	uint32_t leaf_index, sg_cfg_stance_t stance, int hazard,
	uint32_t *cell_out);

static int CarveLeaf(cell_build_t *build, uint32_t leaf_index,
	const polytope_t *leaf_poly, const sg_cfg_hull_t *hull)
{
	const sg_rune_bsp_t *world = build->world;
	const sg_rune_bsp_leaf_t *leaf = &world->leaves[leaf_index];
	polytope_list_t pieces, next, hazard;
	float mins[3], maxs[3];
	uint32_t brush, axis, index;

	if (leaf->contents & SG_RUNE_CONTENTS_SOLID)
		return 1;
	memset(&pieces, 0, sizeof(pieces));
	memset(&next, 0, sizeof(next));
	memset(&hazard, 0, sizeof(hazard));
	/* Brushes whose hull expansion can reach this leaf. */
	for (axis = 0U; axis < 3U; axis++)
	{
		mins[axis] = (float)leaf->mins[axis] +
			hull->mins.value[axis] - 1.0f;
		maxs[axis] = (float)leaf->maxs[axis] +
			hull->maxs.value[axis] + 1.0f;
	}
	if (!GatherLeafBrushes(build, mins, maxs, leaf_index + 1U))
	{
		SetError(build, SG_CONFIGURATION_ERROR_INVALID_WORLD, leaf_index);
		return 0;
	}
	{
		polytope_t *first = ListPush(&pieces);
		uint32_t face;

		if (!first)
			goto out_of_memory;
		for (face = 0U; face < leaf_poly->count; face++)
		{
			poly_face_t *slot = PolytopePushFace(first);
			uint32_t point;

			if (!slot)
				goto out_of_memory;
			slot->plane = leaf_poly->faces[face].plane;
			for (point = 0U; point < leaf_poly->faces[face].count; point++)
				if (!FacePush(slot, leaf_poly->faces[face].points[point]))
					goto out_of_memory;
		}
	}
	for (brush = 0U; brush < build->brush_list_count && pieces.count; brush++)
	{
		if (!SubtractBrush(build, &build->brush_list[brush], hull, &pieces, &next))
		{
			ListFree(&pieces);
			ListFree(&next);
			return 0;
		}
		{
			polytope_list_t swap = pieces;

			pieces = next;
			next = swap;
		}
	}
	/* Lava and slime: the free pieces are split, not cut away. */
	for (brush = 0U; brush < build->hazard_list_count && pieces.count; brush++)
	{
		if (!SplitHazard(build, &build->hazard_list[brush], &pieces, &next,
			&hazard))
		{
			ListFree(&pieces);
			ListFree(&next);
			ListFree(&hazard);
			return 0;
		}
		{
			polytope_list_t swap = pieces;

			pieces = next;
			next = swap;
		}
	}
	for (index = 0U; index < pieces.count; index++)
	{
		uint32_t cell;

		if (!EmitCell(build, &pieces.items[index], leaf_index,
			SG_CFG_CROUCHING, 0, &cell))
		{
			ListFree(&pieces);
			ListFree(&next);
			ListFree(&hazard);
			return 0;
		}
	}
	for (index = 0U; index < hazard.count; index++)
	{
		uint32_t cell;

		if (!EmitCell(build, &hazard.items[index], leaf_index,
			SG_CFG_CROUCHING, 1, &cell))
		{
			ListFree(&pieces);
			ListFree(&next);
			ListFree(&hazard);
			return 0;
		}
	}
	ListFree(&pieces);
	ListFree(&next);
	ListFree(&hazard);
	return 1;

out_of_memory:
	ListFree(&pieces);
	ListFree(&next);
	ListFree(&hazard);
	SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, leaf_index);
	return 0;
}

static void ReportLeaf(cell_build_t *build)
{
	uint32_t percent;

	build->leaves_done++;
	if (!build->progress || !build->world->leaf_count)
		return;
	percent = (uint32_t)(((uint64_t)build->leaves_done * 100U) /
		build->world->leaf_count);
	if (percent == build->last_percent)
		return;
	build->last_percent = percent;
	build->progress(build->progress_context, build->leaves_done,
		build->world->leaf_count);
}

/* Walks the tree with the polytope of the region seen so far; at a leaf that
 * polytope is the leaf's, cut by nothing but node planes and the domain. */
static int CarveNode(cell_build_t *build, int32_t node, polytope_t *region,
	const sg_cfg_hull_t *hull)
{
	const sg_rune_bsp_t *world = build->world;
	const sg_rune_bsp_node_t *record;
	const sg_rune_bsp_plane_t *source;
	sg_configuration_plane_t plane;
	polytope_t inside, outside;
	uint32_t axis;
	int ok;

	if (node < 0)
	{
		const uint32_t leaf = (uint32_t)(-1 - node);

		if (leaf >= world->leaf_count)
		{
			SetError(build, SG_CONFIGURATION_ERROR_INVALID_WORLD, leaf);
			return 0;
		}
		ok = region->count ? CarveLeaf(build, leaf, region, hull) : 1;
		ReportLeaf(build);
		return ok;
	}
	if ((uint32_t)node >= world->node_count)
	{
		SetError(build, SG_CONFIGURATION_ERROR_INVALID_WORLD, (uint32_t)node);
		return 0;
	}
	record = &world->nodes[node];
	if (record->plane >= world->plane_count)
	{
		SetError(build, SG_CONFIGURATION_ERROR_INVALID_WORLD, record->plane);
		return 0;
	}
	source = &world->planes[record->plane];
	memset(&plane, 0, sizeof(plane));
	for (axis = 0U; axis < 3U; axis++)
		plane.normal[axis] = source->normal[axis];
	plane.distance = source->distance;
	plane.source_kind = SG_CONFIGURATION_PLANE_BSP;
	plane.source_index = record->plane;
	/* Back child is n.p <= d (inside); front child is the outside part. */
	if (SplitPolytope(region, &plane, &inside, &outside) < 0)
	{
		SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, (uint32_t)node);
		return 0;
	}
	{
		uint32_t face;

		for (face = 0U; face < outside.count; face++)
			if (outside.faces[face].plane.source_kind ==
				SG_CONFIGURATION_PLANE_BSP &&
				outside.faces[face].plane.source_index == record->plane)
				outside.faces[face].plane.reversed = 1U;
	}
	ok = CarveNode(build, record->children[0], &outside, hull) &&
		CarveNode(build, record->children[1], &inside, hull);
	PolytopeFree(&inside);
	PolytopeFree(&outside);
	return ok;
}

/* ---- emitting cells ------------------------------------------------------ */

/* Inside within one Q8 step: a snapped point that crosses an oblique face
 * by less than the snap is still the cell's. */
static int PointInsideCell(const sg_configuration_space_t *space,
	const sg_configuration_cell_t *cell, const float point[3])
{
	uint32_t face;

	for (face = 0U; face < cell->face_count; face++)
		if (PlaneDistance(&space->faces[cell->first_face + face].plane, point) >
			0.13f)
			return 0;
	return 1;
}

/* A Q8 point inside the cell: the centroid snapped, or a nearby Q8 point. */
static int CellWitness(const sg_configuration_space_t *space,
	const sg_configuration_cell_t *cell, float witness[3])
{
	static const float offsets[7][3] = {
		{ 0.0f, 0.0f, 0.0f }, { 0.125f, 0.0f, 0.0f }, { -0.125f, 0.0f, 0.0f },
		{ 0.0f, 0.125f, 0.0f }, { 0.0f, -0.125f, 0.0f },
		{ 0.0f, 0.0f, 0.125f }, { 0.0f, 0.0f, -0.125f }
	};
	float centre[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t total = 0U, face, axis, attempt;

	for (face = 0U; face < cell->face_count; face++)
	{
		const sg_configuration_face_t *record =
			&space->faces[cell->first_face + face];
		uint32_t vertex;

		for (vertex = 0U; vertex < record->vertex_count; vertex++)
		{
			for (axis = 0U; axis < 3U; axis++)
				centre[axis] += space->vertices[record->first_vertex + vertex]
					.value[axis];
			total++;
		}
	}
	if (!total)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		centre[axis] /= (float)total;
	for (attempt = 0U; attempt < 7U; attempt++)
	{
		for (axis = 0U; axis < 3U; axis++)
			witness[axis] = Q8(centre[axis] + offsets[attempt][axis]);
		if (PointInsideCell(space, cell, witness))
			return 1;
	}
	memcpy(witness, centre, sizeof(float) * 3U);
	return 1;
}

static int EmitCell(cell_build_t *build, const polytope_t *poly,
	uint32_t leaf_index, sg_cfg_stance_t stance, int hazard,
	uint32_t *cell_out)
{
	sg_configuration_space_t *space = build->space;
	const sg_rune_bsp_leaf_t *leaf = &build->world->leaves[leaf_index];
	sg_configuration_cell_t *cell;
	sg_rune_pose_t pose;
	const uint32_t cell_index = space->cell_count;
	uint32_t face, axis;
	float witness[3];

	if (!GrowArray((void **)&space->cells, &build->cell_capacity,
		cell_index + 1U, build->limits.max_cells, sizeof(*space->cells)))
	{
		SetError(build, cell_index + 1U > build->limits.max_cells ?
			SG_CONFIGURATION_ERROR_OVERFLOW :
			SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, leaf_index);
		return 0;
	}
	cell = &space->cells[cell_index];
	memset(cell, 0, sizeof(*cell));
	cell->stance = stance;
	cell->hazard = hazard ? 1U : 0U;
	cell->first_face = space->face_count;
	for (axis = 0U; axis < 3U; axis++)
	{
		cell->bounds.mins.value[axis] = INFINITY;
		cell->bounds.maxs.value[axis] = -INFINITY;
	}
	for (face = 0U; face < poly->count; face++)
	{
		const poly_face_t *source = &poly->faces[face];
		sg_configuration_face_t *record;
		uint32_t point, kept = 0U;

		if (!GrowArray((void **)&space->faces, &build->face_capacity,
			space->face_count + 1U, build->limits.max_faces,
			sizeof(*space->faces)) ||
			!GrowArray((void **)&space->vertices, &build->vertex_capacity,
			space->vertex_count + source->count, build->limits.max_vertices,
			sizeof(*space->vertices)))
		{
			SetError(build, SG_CONFIGURATION_ERROR_OVERFLOW, leaf_index);
			return 0;
		}
		record = &space->faces[space->face_count];
		memset(record, 0, sizeof(*record));
		record->plane = source->plane;
		record->kind = SG_CONFIGURATION_FACE_FACET;
		record->first_vertex = space->vertex_count;
		for (point = 0U; point < source->count; point++)
		{
			sg_cfg_vec3_t *vertex = &space->vertices[space->vertex_count + kept];
			int distinct = 1;

			for (axis = 0U; axis < 3U; axis++)
				vertex->value[axis] = Q8(source->points[point][axis]);
			if (kept)
			{
				const sg_cfg_vec3_t *prev =
					&space->vertices[space->vertex_count + kept - 1U];

				distinct = memcmp(prev, vertex, sizeof(*vertex)) != 0;
			}
			if (!distinct)
				continue;
			for (axis = 0U; axis < 3U; axis++)
			{
				if (vertex->value[axis] < cell->bounds.mins.value[axis])
					cell->bounds.mins.value[axis] = vertex->value[axis];
				if (vertex->value[axis] > cell->bounds.maxs.value[axis])
					cell->bounds.maxs.value[axis] = vertex->value[axis];
			}
			kept++;
		}
		if (kept >= 2U && memcmp(&space->vertices[space->vertex_count],
			&space->vertices[space->vertex_count + kept - 1U],
			sizeof(sg_cfg_vec3_t)) == 0)
			kept--;
		if (kept < 3U)
			continue;
		record->vertex_count = kept;
		space->vertex_count += kept;
		space->face_count++;
	}
	cell->face_count = space->face_count - cell->first_face;
	if (cell->face_count < 4U)
	{
		/* Snapping collapsed it: nothing a player can occupy. */
		space->face_count = cell->first_face;
		return 1;
	}
	cell->bsp_leaf.index = leaf_index;
	cell->bsp_area.index = leaf->area;
	cell->bsp_cluster.index = (uint32_t)leaf->cluster;
	cell->contents = leaf->contents;
	if (!CellWitness(space, cell, witness))
	{
		space->face_count = cell->first_face;
		return 1;
	}
	for (axis = 0U; axis < 3U; axis++)
		cell->interior_witness.value[axis] = witness[axis];
	if (Pose(build->law, witness, stance, &pose) && pose.valid)
	{
		cell->witness_pose_flags |= pose.supported ?
			SG_CONFIGURATION_POSE_SUPPORTED : SG_CONFIGURATION_POSE_AIRBORNE;
		if (pose.water_level)
			cell->witness_pose_flags |= SG_CONFIGURATION_POSE_WATER;
		cell->witness_water_level = pose.water_level;
	}
	space->cell_count++;
	if (cell_out)
		*cell_out = cell_index;
	return 1;
}

/* ---- standing cells by translation -------------------------------------- */

typedef struct cell_grid_s
{
	float origin[3];
	uint32_t size[3];
	uint32_t *first;     /* per bucket: first entry index, NONE for empty */
	uint32_t *next;      /* per entry */
	uint32_t *cell;      /* per entry */
	uint32_t entry_count, entry_capacity;
} cell_grid_t;

static void GridFree(cell_grid_t *grid)
{
	free(grid->first);
	free(grid->next);
	free(grid->cell);
	memset(grid, 0, sizeof(*grid));
}

static uint32_t GridCoordinate(const cell_grid_t *grid, float value,
	uint32_t axis)
{
	float slot = floorf((value - grid->origin[axis]) / CELL_GRID_SPAN);

	if (slot < 0.0f)
		return 0U;
	if (slot >= (float)grid->size[axis])
		return grid->size[axis] - 1U;
	return (uint32_t)slot;
}

static int GridBuild(cell_grid_t *grid, const sg_configuration_space_t *space,
	uint32_t cell_count)
{
	uint32_t axis, cell, buckets;

	memset(grid, 0, sizeof(*grid));
	for (axis = 0U; axis < 3U; axis++)
	{
		const float span = space->domain.maxs.value[axis] -
			space->domain.mins.value[axis];

		grid->origin[axis] = space->domain.mins.value[axis];
		grid->size[axis] = (uint32_t)(span / CELL_GRID_SPAN) + 1U;
		if (grid->size[axis] > 4096U)
			return 0;
	}
	buckets = grid->size[0] * grid->size[1] * grid->size[2];
	grid->first = malloc((size_t)buckets * sizeof(*grid->first));
	if (!grid->first)
		return 0;
	memset(grid->first, 0xFF, (size_t)buckets * sizeof(*grid->first));
	for (cell = 0U; cell < cell_count; cell++)
	{
		const sg_cfg_bounds_t *bounds = &space->cells[cell].bounds;
		uint32_t low[3], high[3], x, y, z;

		for (axis = 0U; axis < 3U; axis++)
		{
			low[axis] = GridCoordinate(grid, bounds->mins.value[axis], axis);
			high[axis] = GridCoordinate(grid, bounds->maxs.value[axis], axis);
		}
		for (x = low[0]; x <= high[0]; x++)
			for (y = low[1]; y <= high[1]; y++)
				for (z = low[2]; z <= high[2]; z++)
				{
					const uint32_t bucket =
						(x * grid->size[1] + y) * grid->size[2] + z;

					if (grid->entry_count == grid->entry_capacity)
					{
						uint32_t capacity = grid->entry_capacity ?
							grid->entry_capacity * 2U : 4096U;
						uint32_t *next = realloc(grid->next,
							(size_t)capacity * sizeof(*next));
						uint32_t *cells = next ? realloc(grid->cell,
							(size_t)capacity * sizeof(*cells)) : NULL;

						if (next)
							grid->next = next;
						if (!cells)
							return 0;
						grid->cell = cells;
						grid->entry_capacity = capacity;
					}
					grid->next[grid->entry_count] = grid->first[bucket];
					grid->cell[grid->entry_count] = cell;
					grid->first[bucket] = grid->entry_count++;
				}
	}
	return 1;
}

/* Rebuilds a cell's polytope from its stored faces. */
static int CellPolytopeFrom(const sg_configuration_space_t *space,
	const sg_configuration_cell_t *cell, polytope_t *poly)
{
	uint32_t face;

	memset(poly, 0, sizeof(*poly));
	for (face = 0U; face < cell->face_count; face++)
	{
		const sg_configuration_face_t *record =
			&space->faces[cell->first_face + face];
		poly_face_t *slot = PolytopePushFace(poly);
		uint32_t vertex;

		if (!slot)
			return 0;
		slot->plane = record->plane;
		for (vertex = 0U; vertex < record->vertex_count; vertex++)
			if (!FacePush(slot, space->vertices[record->first_vertex + vertex]
				.value))
				return 0;
	}
	return 1;
}

/* Every crouching cell becomes disjoint convex pieces, each one either
 * standing-valid (both stances) or crouch-only.  The standing box fits at p
 * exactly when the crouch box fits at p and at p + (0, 0, 28), so a piece
 * inside some crouching cell translated down by 28 is standing-valid; what
 * is outside all of them is crouch-only.  The pieces replace the cell, so
 * the complex stays a partition and each cell has one stance validity. */
static int EmitStancePieces(cell_build_t *build)
{
	sg_configuration_space_t *space = build->space;
	const uint32_t crouch_count = space->cell_count;
	cell_grid_t grid;
	uint32_t *seen = NULL;
	sg_configuration_cell_t *originals = NULL;
	uint32_t lower;
	int ok = 0;

	if (!GridBuild(&grid, space, crouch_count))
		goto out_of_memory;
	seen = calloc(crouch_count ? crouch_count : 1U, sizeof(*seen));
	originals = malloc((size_t)(crouch_count ? crouch_count : 1U) *
		sizeof(*originals));
	if (!seen || !originals)
		goto out_of_memory;
	/* The originals are read from a copy; the space's cell array is rebuilt
	 * from the pieces.  Their faces stay in place; pieces append new faces. */
	memcpy(originals, space->cells, (size_t)crouch_count * sizeof(*originals));
	space->cell_count = 0U;
	for (lower = 0U; lower < crouch_count; lower++)
	{
		const sg_configuration_cell_t *original = &originals[lower];
		sg_cfg_bounds_t lifted = original->bounds;
		polytope_list_t pending, next, standing;
		uint32_t low[3], high[3], x, y, z, axis, index;
		polytope_t *first;

		memset(&pending, 0, sizeof(pending));
		memset(&next, 0, sizeof(next));
		memset(&standing, 0, sizeof(standing));
		first = ListPush(&pending);
		if (!first || !CellPolytopeFrom(space, original, first))
		{
			ListFree(&pending);
			goto out_of_memory;
		}
		lifted.mins.value[2] += CELL_STANDING_RISE;
		lifted.maxs.value[2] += CELL_STANDING_RISE;
		for (axis = 0U; axis < 3U; axis++)
		{
			low[axis] = GridCoordinate(&grid, lifted.mins.value[axis], axis);
			high[axis] = GridCoordinate(&grid, lifted.maxs.value[axis], axis);
		}
		for (x = low[0]; x <= high[0]; x++)
			for (y = low[1]; y <= high[1]; y++)
				for (z = low[2]; z <= high[2]; z++)
				{
					const uint32_t bucket =
						(x * grid.size[1] + y) * grid.size[2] + z;
					uint32_t entry;

					for (entry = grid.first[bucket]; entry != UINT32_MAX;
						entry = grid.next[entry])
					{
						const uint32_t upper = grid.cell[entry];
						const sg_configuration_cell_t *above = &originals[upper];
						uint32_t piece, face;
						int overlaps = 1;

						if (seen[upper] == lower + 1U)
							continue;
						seen[upper] = lower + 1U;
						for (axis = 0U; axis < 3U && overlaps; axis++)
							if (above->bounds.mins.value[axis] >=
								lifted.maxs.value[axis] ||
								above->bounds.maxs.value[axis] <=
								lifted.mins.value[axis])
								overlaps = 0;
						if (!overlaps)
							continue;
						/* Cut every pending piece by the upper cell's faces
						 * translated down; what is inside all of them is a
						 * standing piece, the outside parts stay pending. */
						for (piece = 0U; piece < pending.count; piece++)
						{
							polytope_t current = pending.items[piece];

							memset(&pending.items[piece], 0,
								sizeof(pending.items[piece]));
							for (face = 0U; face < above->face_count &&
								current.count; face++)
							{
								sg_configuration_plane_t plane =
									space->faces[above->first_face + face]
									.plane;
								polytope_t inside, outside;

								plane.distance -= plane.normal[2] *
									CELL_STANDING_RISE;
								plane.source_variant =
									SG_CFG_STANDING;
								if (SplitPolytope(&current, &plane, &inside,
									&outside) < 0)
								{
									PolytopeFree(&current);
									ListFree(&pending);
									ListFree(&next);
									ListFree(&standing);
									goto out_of_memory;
								}
								PolytopeFree(&current);
								if (outside.count && !ListTake(&next, &outside))
								{
									PolytopeFree(&outside);
									PolytopeFree(&inside);
									ListFree(&pending);
									ListFree(&next);
									ListFree(&standing);
									goto out_of_memory;
								}
								current = inside;
							}
							if (current.count && !ListTake(&standing, &current))
							{
								PolytopeFree(&current);
								ListFree(&pending);
								ListFree(&next);
								ListFree(&standing);
								goto out_of_memory;
							}
						}
						pending.count = 0U;
						{
							polytope_list_t swap = pending;

							pending = next;
							next = swap;
						}
					}
				}
		for (index = 0U; index < standing.count; index++)
			if (!EmitCell(build, &standing.items[index],
				original->bsp_leaf.index, SG_CFG_STANDING, original->hazard, NULL))
			{
				ListFree(&pending);
				ListFree(&next);
				ListFree(&standing);
				goto failed;
			}
		for (index = 0U; index < pending.count; index++)
			if (!EmitCell(build, &pending.items[index],
				original->bsp_leaf.index, SG_CFG_CROUCHING, original->hazard, NULL))
			{
				ListFree(&pending);
				ListFree(&next);
				ListFree(&standing);
				goto failed;
			}
		ListFree(&pending);
		ListFree(&next);
		ListFree(&standing);
	}
	ok = 1;
	goto done;

out_of_memory:
	SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, 0U);
failed:
	ok = 0;
done:
	free(seen);
	free(originals);
	GridFree(&grid);
	return ok;
}

/* ---- portals from shared faces ----------------------------------------- */

static sg_configuration_portal_stats_t portal_stats;

const sg_configuration_portal_stats_t *SG_ConfigurationLastPortalStats(void)
{
	return &portal_stats;
}

typedef struct face_ref_s
{
	uint32_t cell;
	uint32_t face;
} face_ref_t;

static uint64_t FaceKey(const sg_configuration_plane_t *plane,
	sg_cfg_stance_t stance)
{
	/* The variant (the hull the plane was expanded for) is left out: a
	 * wall expanded for the crouch hull and for the standing hull is the
	 * same plane, and the faces on it must meet.  SameKey tells them
	 * apart where the expansions differ. */
	uint64_t key = ((uint64_t)plane->source_kind << 56) ^
		(uint64_t)plane->source_index;

	(void)stance;

	key ^= key >> 29;
	key *= UINT64_C(0x9E3779B97F4A7C15);
	key ^= key >> 32;
	return key;
}

static int SameKey(const sg_configuration_plane_t *a,
	const sg_configuration_plane_t *b)
{
	return a->source_kind == b->source_kind &&
		a->source_index == b->source_index &&
		(a->source_variant == b->source_variant ||
		 fabsf(fabsf(a->distance) - fabsf(b->distance)) < SAME_PLANE_DISTANCE);
}

/* Clips polygon a by the edges of coplanar polygon b (b's winding is
 * opposite to a's, so its edge planes are built from a's normal). */
static int OverlapPolygon(const sg_configuration_space_t *space,
	const sg_configuration_face_t *a, const sg_configuration_face_t *b,
	poly_face_t *out)
{
	poly_face_t current, clipped;
	uint32_t vertex, edge, axis;

	memset(&current, 0, sizeof(current));
	memset(&clipped, 0, sizeof(clipped));
	memset(out, 0, sizeof(*out));
	current.plane = a->plane;
	for (vertex = 0U; vertex < a->vertex_count; vertex++)
		if (!FacePush(&current, space->vertices[a->first_vertex + vertex]
			.value))
			goto failure;
	for (edge = 0U; edge < b->vertex_count && current.count >= 3U; edge++)
	{
		const float *p = space->vertices[b->first_vertex + edge].value;
		const float *q = space->vertices[b->first_vertex +
			(edge + 1U) % b->vertex_count].value;
		sg_configuration_plane_t side;
		float along[3];
		float length;

		/* Edge plane of b, oriented so b's interior is inside (n.p <= d).
		 * b winds counter-clockwise about its own normal, which is the
		 * reverse of a's, so a's normal crossed with the edge points out
		 * of b. */
		for (axis = 0U; axis < 3U; axis++)
			along[axis] = q[axis] - p[axis];
		memset(&side, 0, sizeof(side));
		side.normal[0] = a->plane.normal[1] * along[2] -
			a->plane.normal[2] * along[1];
		side.normal[1] = a->plane.normal[2] * along[0] -
			a->plane.normal[0] * along[2];
		side.normal[2] = a->plane.normal[0] * along[1] -
			a->plane.normal[1] * along[0];
		length = sqrtf(Dot(side.normal, side.normal));
		if (!(length > 0.0f))
			continue;
		for (axis = 0U; axis < 3U; axis++)
			side.normal[axis] /= length;
		side.distance = Dot(side.normal, p);
		clipped.count = 0U;
		for (vertex = 0U; vertex < current.count; vertex++)
		{
			const float *s = current.points[vertex];
			const float *t = current.points[(vertex + 1U) % current.count];
			const float ds = PlaneDistance(&side, s);
			const float dt = PlaneDistance(&side, t);

			if (ds <= CELL_EPSILON && !FacePush(&clipped, s))
				goto failure;
			if ((ds > CELL_EPSILON && dt < -CELL_EPSILON) ||
				(ds < -CELL_EPSILON && dt > CELL_EPSILON))
			{
				const float f = ds / (ds - dt);
				float point[3];

				for (axis = 0U; axis < 3U; axis++)
					point[axis] = s[axis] + f * (t[axis] - s[axis]);
				if (!FacePush(&clipped, point))
					goto failure;
			}
		}
		{
			poly_face_t swap = current;

			current = clipped;
			clipped = swap;
		}
	}
	FaceFree(&clipped);
	if (current.count < 3U || !FaceHasArea(&current))
	{
		FaceFree(&current);
		return 0;
	}
	*out = current;
	return 1;

failure:
	FaceFree(&current);
	FaceFree(&clipped);
	return -1;
}

/* A Q8 point stepped from the polygon centre along the face normal into
 * the cell; the nearest inside step is primary, the deepest is fallback.
 * The host validates the pose. */
static int SideWitness(const cell_build_t *build, uint32_t cell_index,
	const poly_face_t *polygon, float witness[3])
{
	static const float steps[] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f,
		16.0f, 32.0f };
	const sg_configuration_space_t *space = build->space;
	const sg_configuration_cell_t *cell = &space->cells[cell_index];
	float centre[3] = { 0.0f, 0.0f, 0.0f };
	float primary[3], fallback[3];
	sg_rune_pose_t pose;
	uint32_t vertex, axis, step;
	int have = 0;

	for (vertex = 0U; vertex < polygon->count; vertex++)
		for (axis = 0U; axis < 3U; axis++)
			centre[axis] += polygon->points[vertex][axis];
	for (axis = 0U; axis < 3U; axis++)
		centre[axis] /= (float)polygon->count;
	/* The face normal points out of the cell: step against it. */
	for (step = 0U; step < sizeof(steps) / sizeof(steps[0]); step++)
	{
		float point[3];

		for (axis = 0U; axis < 3U; axis++)
			point[axis] = Q8(centre[axis] -
				steps[step] * polygon->plane.normal[axis]);
		if (!PointInsideCell(space, cell, point))
		{
			if (have)
				break;
			continue;
		}
		if (!have)
			memcpy(primary, point, sizeof(primary));
		memcpy(fallback, point, sizeof(fallback));
		have = 1;
	}
	if (!have)
		return 0;
	if (Pose(build->law, primary, cell->stance, &pose) && pose.valid)
	{
		memcpy(witness, primary, sizeof(primary));
		return 1;
	}
	if (Pose(build->law, fallback, cell->stance, &pose) && pose.valid)
	{
		memcpy(witness, fallback, sizeof(fallback));
		return 1;
	}
	return 0;
}

static int EmitPortal(cell_build_t *build, uint32_t from, uint32_t to,
	const sg_configuration_face_t *from_face, const poly_face_t *polygon,
	const float from_witness[3], const float to_witness[3])
{
	sg_configuration_space_t *space = build->space;
	sg_configuration_portal_t *portal;
	const uint32_t index = space->portal_count;
	uint32_t vertex, axis;
	float clearance = INFINITY;
	/* The crossing is made in the stance both cells allow: crouching when
	 * either side is crouch-only. */
	const sg_cfg_stance_t stance = space->cells[from].stance == SG_CFG_CROUCHING ||
		space->cells[to].stance == SG_CFG_CROUCHING ? SG_CFG_CROUCHING : SG_CFG_STANDING;

	if (!Clear(build->law, from_witness, to_witness, stance))
	{
		portal_stats.transition_failed++;
		return 1;
	}
	if (!GrowArray((void **)&space->portals, &build->portal_capacity,
		index + 1U, build->limits.max_portals, sizeof(*space->portals)) ||
		!GrowArray((void **)&space->vertices, &build->vertex_capacity,
		space->vertex_count + polygon->count, build->limits.max_vertices,
		sizeof(*space->vertices)))
	{
		SetError(build, SG_CONFIGURATION_ERROR_OVERFLOW, index);
		return 0;
	}
	portal = &space->portals[index];
	memset(portal, 0, sizeof(*portal));
	portal->from_cell = from;
	portal->to_cell = to;
	portal->stance = stance;
	portal->plane = from_face->plane;
	portal->first_vertex = space->vertex_count;
	portal->vertex_count = polygon->count;
	for (vertex = 0U; vertex < polygon->count; vertex++)
	{
		const float *p = polygon->points[vertex];
		const float *q = polygon->points[(vertex + 1U) % polygon->count];
		float centre[3] = { 0.0f, 0.0f, 0.0f };
		float d[3], along[3], across, length;
		uint32_t other;

		for (axis = 0U; axis < 3U; axis++)
			space->vertices[space->vertex_count + vertex].value[axis] = p[axis];
		/* Clearance: the centre's distance to the nearest edge. */
		for (other = 0U; other < polygon->count; other++)
			for (axis = 0U; axis < 3U; axis++)
				centre[axis] += polygon->points[other][axis] /
					(float)polygon->count;
		for (axis = 0U; axis < 3U; axis++)
		{
			along[axis] = q[axis] - p[axis];
			d[axis] = centre[axis] - p[axis];
		}
		length = sqrtf(Dot(along, along));
		if (!(length > 0.0f))
			continue;
		across = Dot(d, along) / length;
		across = sqrtf(fmaxf(Dot(d, d) - across * across, 0.0f));
		if (across < clearance)
			clearance = across;
	}
	space->vertex_count += polygon->count;
	portal->clearance = isfinite(clearance) ? clearance : 0.0f;
	space->portal_count++;
	return 1;
}

static int EmitPortals(cell_build_t *build)
{
	sg_configuration_space_t *space = build->space;
	const uint32_t face_total = space->face_count;
	uint32_t *hash;
	face_ref_t *refs;
	uint32_t capacity = 1024U, cell, face, filled = 0U, slot;

	while (capacity < face_total * 2U)
		capacity *= 2U;
	hash = malloc((size_t)capacity * sizeof(*hash));
	refs = malloc((size_t)face_total * sizeof(*refs));
	if (!hash || !refs)
	{
		free(hash);
		free(refs);
		SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	memset(hash, 0xFF, (size_t)capacity * sizeof(*hash));
	/* Faces hashed by construction key; only domain, BSP, brush and
	 * stance-split planes can be shared.  Cells of different stances pair
	 * too: a standing cell beside a crouch-only one is crossed crouching,
	 * which the portal's stance records and the witness sweep checks. */
	for (cell = 0U; cell < space->cell_count; cell++)
		for (face = 0U; face < space->cells[cell].face_count; face++)
		{
			const uint32_t face_index = space->cells[cell].first_face + face;
			const sg_configuration_face_t *record = &space->faces[face_index];
			const sg_configuration_cell_t *owner = &space->cells[cell];

			slot = (uint32_t)FaceKey(&record->plane, owner->stance) &
				(capacity - 1U);
			{
				const char *watch = getenv("SG_CFG_WATCH_D");

				if (watch && fabsf(fabsf(record->plane.distance) - (float)atof(watch)) < 0.01f)
				{
					float lo[3] = { INFINITY, INFINITY, INFINITY }, hi[3] = { -INFINITY, -INFINITY, -INFINITY };
					uint32_t v, ax;

					for (v = 0U; v < record->vertex_count; v++)
						for (ax = 0U; ax < 3U; ax++)
						{
							float c = space->vertices[record->first_vertex + v].value[ax];

							if (c < lo[ax]) lo[ax] = c;
							if (c > hi[ax]) hi[ax] = c;
						}
					fprintf(stderr, "insert: cell %u stance %u normal (%.2f %.2f %.2f) d %.3f kind %u index %u variant %u slot %u: y %.1f..%.1f z %.1f..%.1f\n",
						cell, (unsigned)owner->stance, record->plane.normal[0], record->plane.normal[1], record->plane.normal[2],
						record->plane.distance, (unsigned)record->plane.source_kind, (unsigned)record->plane.source_index,
						(unsigned)record->plane.source_variant, slot, lo[1], hi[1], lo[2], hi[2]);
				}
			}
			while (hash[slot] != UINT32_MAX)
			{
				const face_ref_t *other = &refs[hash[slot]];
				const sg_configuration_cell_t *other_cell =
					&space->cells[other->cell];
				const sg_configuration_face_t *other_face =
					&space->faces[other_cell->first_face + other->face];

				if (other->cell != cell &&
					SameKey(&other_face->plane, &record->plane) &&
					Dot(other_face->plane.normal, record->plane.normal) < 0.0f)
				{
					poly_face_t overlap;
					int result = OverlapPolygon(space, record, other_face,
						&overlap);
					const char *watch = getenv("SG_CFG_WATCH_D");
					int watched = watch && fabsf(fabsf(record->plane.distance) - (float)atof(watch)) < 0.01f;

					if (result < 0)
						goto out_of_memory;
					if (watched)
					{
						float lo[2][3] = { { INFINITY, INFINITY, INFINITY }, { INFINITY, INFINITY, INFINITY } };
						float hi[2][3] = { { -INFINITY, -INFINITY, -INFINITY }, { -INFINITY, -INFINITY, -INFINITY } };
						const sg_configuration_face_t *faces[2] = { record, other_face };
						uint32_t which, v, ax;

						for (which = 0U; which < 2U; which++)
							for (v = 0U; v < faces[which]->vertex_count; v++)
								for (ax = 0U; ax < 3U; ax++)
								{
									float c = space->vertices[faces[which]->first_vertex + v].value[ax];

									if (c < lo[which][ax]) lo[which][ax] = c;
									if (c > hi[which][ax]) hi[which][ax] = c;
								}
						fprintf(stderr, "portal pass: cells %u/%u d %.3f overlap %d: a y %.1f..%.1f z %.1f..%.1f | b y %.1f..%.1f z %.1f..%.1f\n",
							cell, other->cell, record->plane.distance, result,
							lo[0][1], hi[0][1], lo[0][2], hi[0][2], lo[1][1], hi[1][1], lo[1][2], hi[1][2]);
					}
					if (result)
					{
						float here[3], there[3];
						poly_face_t reverse;
						uint32_t vertex;
						int ok = 1;

						portal_stats.overlaps++;
						/* The other side sees the overlap through its own
						 * face: reversed winding, its plane. */
						memset(&reverse, 0, sizeof(reverse));
						reverse.plane = other_face->plane;
						for (vertex = overlap.count; vertex-- > 0U; )
							if (!FacePush(&reverse, overlap.points[vertex]))
								ok = 0;
						if (watched)
							fprintf(stderr, "  witness here %d there %d\n",
								SideWitness(build, cell, &overlap, here),
								SideWitness(build, other->cell, &reverse, there));
						if (ok && SideWitness(build, cell, &overlap, here) &&
							SideWitness(build, other->cell, &reverse, there))
						{
							portal_stats.witnessed++;
							ok = EmitPortal(build, cell, other->cell, record,
								&overlap, here, there) &&
								EmitPortal(build, other->cell, cell, other_face,
									&reverse, there, here);
						}
						FaceFree(&reverse);
						FaceFree(&overlap);
						if (!ok)
						{
							free(hash);
							free(refs);
							return 0;
						}
					}
				}
				slot = (slot + 1U) & (capacity - 1U);
			}
			refs[filled].cell = cell;
			refs[filled].face = face;
			hash[slot] = filled++;
		}
	free(hash);
	free(refs);
	return 1;

out_of_memory:
	free(hash);
	free(refs);
	SetError(build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, 0U);
	return 0;
}

/* ---- public API --------------------------------------------------------- */

void SG_ConfigurationDefaultLimits(sg_configuration_limits_t *limits_out)
{
	if (!limits_out)
		return;
	limits_out->max_cells = SG_CONFIGURATION_DEFAULT_MAX_CELLS;
	limits_out->max_faces = SG_CONFIGURATION_DEFAULT_MAX_FACES;
	limits_out->max_vertices = SG_CONFIGURATION_DEFAULT_MAX_VERTICES;
	limits_out->max_portals = SG_CONFIGURATION_DEFAULT_MAX_PORTALS;
	limits_out->max_stance_overlaps =
		SG_CONFIGURATION_DEFAULT_MAX_STANCE_OVERLAPS;
}

int SG_ConfigurationBuild(const sg_rune_bsp_t *bsp, const sg_rune_law_t *law,
	const sg_configuration_limits_t *limits,
	sg_configuration_space_t **space_out, sg_configuration_error_t *error_out)
{
	return SG_ConfigurationBuildWithProgress(bsp, law, limits, NULL, NULL,
		space_out, error_out);
}

int SG_ConfigurationBuildWithProgress(const sg_rune_bsp_t *bsp,
	const sg_rune_law_t *law, const sg_configuration_limits_t *limits,
	sg_configuration_progress_fn progress, void *progress_context,
	sg_configuration_space_t **space_out, sg_configuration_error_t *error_out)
{
	cell_build_t build;
	sg_configuration_limits_t defaults;
	polytope_t domain;
	float mins[3], maxs[3];
	uint32_t axis;
	int ok = 0;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!space_out)
		return 0;
	*space_out = NULL;
	if (!bsp || !law || !bsp->models || !bsp->model_count || !SG_RuneLawValid(law))
	{
		if (error_out)
			error_out->code = SG_CONFIGURATION_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	memset(&build, 0, sizeof(build));
	memset(&portal_stats, 0, sizeof(portal_stats));
	memset(&domain, 0, sizeof(domain));
	build.law = law;
	build.world = bsp;
	sg_cells_law = law;
	sg_cells_bsp = bsp;
	build.progress = progress;
	build.progress_context = progress_context;
	if (limits)
		build.limits = *limits;
	else
	{
		SG_ConfigurationDefaultLimits(&defaults);
		build.limits = defaults;
	}
	build.space = calloc(1, sizeof(*build.space));
	build.brush_marks = calloc(build.world->brush_count ?
		build.world->brush_count : 1U, sizeof(*build.brush_marks));
	if (!build.space || !build.brush_marks)
	{
		SetError(&build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, 0U);
		goto done;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		mins[axis] = build.world->models[0].mins[axis];
		maxs[axis] = build.world->models[0].maxs[axis];
		build.space->domain.mins.value[axis] = mins[axis];
		build.space->domain.maxs.value[axis] = maxs[axis];
		if (!(maxs[axis] > mins[axis]))
		{
			SetError(&build, SG_CONFIGURATION_ERROR_INVALID_WORLD, axis);
			goto done;
		}
	}
	if (!BoxPolytope(&domain, mins, maxs))
	{
		SetError(&build, SG_CONFIGURATION_ERROR_OUT_OF_MEMORY, 0U);
		goto done;
	}
	{
		sg_cfg_hull_t crouching;

		memcpy(crouching.mins.value, law->crouching_mins, sizeof(crouching.mins.value));
		memcpy(crouching.maxs.value, law->crouching_maxs, sizeof(crouching.maxs.value));
		/* The feet are a unit above the hull's bottom: an origin is in a
		 * liquid when that point is, so the liquid rises by the depth. */
		memset(&build.hazard_hull, 0, sizeof(build.hazard_hull));
		build.hazard_hull.mins.value[2] = law->crouching_mins[2] + 1.0f;
		if (!CarveNode(&build, build.world->models[0].headnode, &domain, &crouching))
			goto done;
	}
	if (!EmitStancePieces(&build))
		goto done;
	if (!EmitPortals(&build))
		goto done;
	ok = 1;

done:
	PolytopeFree(&domain);
	free(build.brush_marks);
	free(build.brush_list);
	free(build.hazard_list);
	if (ok)
		*space_out = build.space;
	else
	{
		SG_ConfigurationDestroy(build.space);
		if (error_out)
			*error_out = build.error;
	}
	return ok;
}

void SG_ConfigurationDestroy(sg_configuration_space_t *space)
{
	if (!space)
		return;
	free(space->cells);
	free(space->faces);
	free(space->vertices);
	free(space->portals);
	free(space->stance_overlaps);
	free(space);
}

const char *SG_ConfigurationErrorString(sg_configuration_error_code_t code)
{
	switch (code)
	{
	case SG_CONFIGURATION_ERROR_NONE: return "none";
	case SG_CONFIGURATION_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_CONFIGURATION_ERROR_INVALID_WORLD: return "invalid world";
	case SG_CONFIGURATION_ERROR_INVALID_HULL: return "invalid hull";
	case SG_CONFIGURATION_ERROR_NONFINITE_GEOMETRY:
		return "non-finite geometry";
	case SG_CONFIGURATION_ERROR_DEGENERATE_GEOMETRY:
		return "degenerate geometry";
	case SG_CONFIGURATION_ERROR_OVERFLOW: return "overflow";
	case SG_CONFIGURATION_ERROR_OUT_OF_MEMORY: return "out of memory";
	case SG_CONFIGURATION_ERROR_INVALID_TOPOLOGY: return "invalid topology";
	case SG_CONFIGURATION_ERROR_HOST_DISAGREEMENT:
		return "host disagreement";
	default: return "unknown configuration error";
	}
}

int SG_ConfigurationBrushPolygons(const sg_rune_bsp_t *world, uint32_t brush,
	sg_configuration_brush_polygon_fn fn, void *context)
{
	const sg_rune_bsp_brush_t *record;
	polytope_t poly;
	float mins[3], maxs[3];
	uint32_t side, axis, face;
	int ok = 1;

	if (!world || !fn || brush >= world->brush_count || !world->model_count)
		return 0;
	record = &world->brushes[brush];
	for (axis = 0U; axis < 3U; axis++)
	{
		mins[axis] = world->models[0].mins[axis] - 64.0f;
		maxs[axis] = world->models[0].maxs[axis] + 64.0f;
	}
	memset(&poly, 0, sizeof(poly));
	if (!BoxPolytope(&poly, mins, maxs))
	{
		PolytopeFree(&poly);
		return 0;
	}
	for (side = 0U; side < record->side_count && poly.count; side++)
	{
		const uint32_t side_index = record->first_side + side;
		const sg_rune_bsp_plane_t *source;
		sg_configuration_plane_t plane;
		polytope_t inside, outside;

		if (side_index >= world->side_count ||
			world->sides[side_index].plane >= world->plane_count)
		{
			PolytopeFree(&poly);
			return 0;
		}
		source = &world->planes[world->sides[side_index].plane];
		memset(&plane, 0, sizeof(plane));
		for (axis = 0U; axis < 3U; axis++)
			plane.normal[axis] = source->normal[axis];
		plane.distance = source->distance;
		plane.source_kind = SG_CONFIGURATION_PLANE_EXPANDED_BRUSH;
		plane.source_index = side_index;
		if (SplitPolytope(&poly, &plane, &inside, &outside) < 0)
		{
			PolytopeFree(&poly);
			return 0;
		}
		PolytopeFree(&poly);
		PolytopeFree(&outside);
		poly = inside;
	}
	for (face = 0U; face < poly.count && ok; face++)
	{
		const poly_face_t *record_face = &poly.faces[face];

		if (record_face->plane.source_kind !=
			SG_CONFIGURATION_PLANE_EXPANDED_BRUSH)
			continue;
		ok = fn(context, brush, record_face->plane.source_index,
			(const float (*)[3])record_face->points, record_face->count);
	}
	PolytopeFree(&poly);
	return ok;
}
