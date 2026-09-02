#include "sg_rune_trace.h"

#include <math.h>
#include <string.h>

#define DIST_EPSILON 0.03125f     /* the engine's 1/32 */
#define FLOOR_NORMAL_Z 0.7f
#define GROUND_PROBE 0.25f

typedef struct work_s
{
	const sg_rune_bsp_t *bsp;
	float start[3], end[3];
	float mins[3], maxs[3];
	float extents[3];         /* half sizes */
	int point;                /* no box */
	int32_t mask;
	sg_rune_trace_t trace;
	uint32_t checked_count;   /* brushes clipped so far this trace */
	uint32_t checked[4096];   /* their indices, so none is clipped twice */
} work_t;

static int Checked(work_t *w, uint32_t brush)
{
	uint32_t i;

	for (i = 0U; i < w->checked_count; i++)
		if (w->checked[i] == brush)
			return 1;
	if (w->checked_count < sizeof(w->checked) / sizeof(w->checked[0]))
		w->checked[w->checked_count++] = brush;
	return 0;
}

/* Clips the segment against one brush: the nearest entering plane past
 * every plane's exit is the hit. */
static void ClipBrush(work_t *w, uint32_t brush_index)
{
	const sg_rune_bsp_brush_t *brush = &w->bsp->brushes[brush_index];
	float enter = -1.0f, leave = 1.0f;
	int starts_out = 0, ends_out = 0;
	const sg_rune_bsp_side_t *lead = NULL;
	uint32_t s;

	if (brush->side_count == 0U)
		return;
	for (s = 0U; s < brush->side_count; s++)
	{
		const sg_rune_bsp_side_t *side = &w->bsp->sides[brush->first_side + s];
		const sg_rune_bsp_plane_t *plane = &w->bsp->planes[side->plane];
		float distance, d1, d2, offset = 0.0f;
		uint32_t axis;

		if (!w->point)
			for (axis = 0U; axis < 3U; axis++)
				offset += (plane->normal[axis] < 0.0f ? w->maxs[axis] : w->mins[axis]) *
					-plane->normal[axis];
		distance = plane->distance + offset;
		d1 = w->start[0] * plane->normal[0] + w->start[1] * plane->normal[1] +
			w->start[2] * plane->normal[2] - distance;
		d2 = w->end[0] * plane->normal[0] + w->end[1] * plane->normal[1] +
			w->end[2] * plane->normal[2] - distance;
		if (d2 > 0.0f)
			ends_out = 1;
		if (d1 > 0.0f)
			starts_out = 1;
		if (d1 > 0.0f && d2 >= d1)
			return;         /* wholly in front of this plane: no hit */
		if (d1 <= 0.0f && d2 <= 0.0f)
			continue;       /* behind it: no bound from this plane */
		if (d1 > d2)
		{
			float f = (d1 - DIST_EPSILON) / (d1 - d2);

			if (f > enter)
			{
				enter = f;
				lead = side;
			}
		}
		else
		{
			float f = (d1 + DIST_EPSILON) / (d1 - d2);

			if (f < leave)
				leave = f;
		}
	}
	if (!starts_out)
	{
		w->trace.startsolid = 1;
		if (!ends_out)
			w->trace.allsolid = 1;
		return;
	}
	if (enter < leave && enter > -1.0f && enter < w->trace.fraction)
	{
		const sg_rune_bsp_plane_t *plane;

		if (enter < 0.0f)
			enter = 0.0f;
		plane = &w->bsp->planes[lead->plane];
		w->trace.fraction = enter;
		memcpy(w->trace.normal, plane->normal, sizeof(w->trace.normal));
		w->trace.distance = plane->distance;
		w->trace.contents = brush->contents;
		w->trace.texinfo = lead->texinfo;
		w->trace.brush = brush_index;
	}
}

static void ClipLeaf(work_t *w, int32_t leaf_index)
{
	const sg_rune_bsp_leaf_t *leaf = &w->bsp->leaves[leaf_index];
	uint32_t k;

	if (!(leaf->contents & w->mask))
		return;
	for (k = 0U; k < leaf->leaf_brush_count; k++)
	{
		uint32_t brush = w->bsp->leaf_brushes[leaf->first_leaf_brush + k];

		if (!(w->bsp->brushes[brush].contents & w->mask) || Checked(w, brush))
			continue;
		ClipBrush(w, brush);
		if (w->trace.fraction <= 0.0f)
			return;
	}
}

/* Walks the tree along the segment from p1 (at p1f) to p2 (at p2f),
 * visiting the near side first, with the box's extent widening the split. */
static void Walk(work_t *w, int32_t node_index, float p1f, float p2f,
	const float p1[3], const float p2[3])
{
	const sg_rune_bsp_node_t *node;
	const sg_rune_bsp_plane_t *plane;
	float t1, t2, offset, frac, frac2, midf, mid[3];
	int side;

	if (w->trace.fraction <= p1f)
		return;         /* already hit nearer than this span */
	if (node_index < 0)
	{
		ClipLeaf(w, -1 - node_index);
		return;
	}
	node = &w->bsp->nodes[node_index];
	plane = &w->bsp->planes[node->plane];
	t1 = plane->normal[0] * p1[0] + plane->normal[1] * p1[1] +
		plane->normal[2] * p1[2] - plane->distance;
	t2 = plane->normal[0] * p2[0] + plane->normal[1] * p2[1] +
		plane->normal[2] * p2[2] - plane->distance;
	offset = w->point ? 0.0f :
		fabsf(w->extents[0] * plane->normal[0]) +
		fabsf(w->extents[1] * plane->normal[1]) +
		fabsf(w->extents[2] * plane->normal[2]);
	if (t1 >= offset && t2 >= offset)
	{
		Walk(w, node->children[0], p1f, p2f, p1, p2);
		return;
	}
	if (t1 < -offset && t2 < -offset)
	{
		Walk(w, node->children[1], p1f, p2f, p1, p2);
		return;
	}
	if (t1 < t2)
	{
		float idist = 1.0f / (t1 - t2);

		side = 1;
		frac2 = (t1 + offset + DIST_EPSILON) * idist;
		frac = (t1 - offset + DIST_EPSILON) * idist;
	}
	else if (t1 > t2)
	{
		float idist = 1.0f / (t1 - t2);

		side = 0;
		frac2 = (t1 - offset - DIST_EPSILON) * idist;
		frac = (t1 + offset + DIST_EPSILON) * idist;
	}
	else
	{
		side = 0;
		frac = 1.0f;
		frac2 = 0.0f;
	}
	if (frac < 0.0f)
		frac = 0.0f;
	if (frac > 1.0f)
		frac = 1.0f;
	midf = p1f + (p2f - p1f) * frac;
	mid[0] = p1[0] + frac * (p2[0] - p1[0]);
	mid[1] = p1[1] + frac * (p2[1] - p1[1]);
	mid[2] = p1[2] + frac * (p2[2] - p1[2]);
	Walk(w, node->children[side], p1f, midf, p1, mid);
	if (frac2 < 0.0f)
		frac2 = 0.0f;
	if (frac2 > 1.0f)
		frac2 = 1.0f;
	midf = p1f + (p2f - p1f) * frac2;
	mid[0] = p1[0] + frac2 * (p2[0] - p1[0]);
	mid[1] = p1[1] + frac2 * (p2[1] - p1[1]);
	mid[2] = p1[2] + frac2 * (p2[2] - p1[2]);
	Walk(w, node->children[side ^ 1], midf, p2f, mid, p2);
}

int SG_RuneTraceBox(const sg_rune_bsp_t *bsp, uint32_t model,
	const float model_origin[3], const float start[3], const float mins[3],
	const float maxs[3], const float end[3], int32_t mask,
	sg_rune_trace_t *trace_out)
{
	work_t w;
	uint32_t axis;

	if (!trace_out)
		return 0;
	memset(trace_out, 0, sizeof(*trace_out));
	trace_out->fraction = 1.0f;
	trace_out->texinfo = -1;
	trace_out->brush = UINT32_MAX;
	if (!bsp || model >= bsp->model_count || !start || !end)
		return 0;
	memset(&w, 0, sizeof(w));
	w.bsp = bsp;
	w.mask = mask;
	w.trace = *trace_out;
	w.point = mins == NULL || maxs == NULL;
	for (axis = 0U; axis < 3U; axis++)
	{
		float shift = model_origin ? model_origin[axis] : 0.0f;

		w.start[axis] = start[axis] - shift;
		w.end[axis] = end[axis] - shift;
		if (!w.point)
		{
			w.mins[axis] = mins[axis];
			w.maxs[axis] = maxs[axis];
			w.extents[axis] = (maxs[axis] - mins[axis]) * 0.5f;
			/* Trace the box's centre so the extents are symmetric. */
			w.start[axis] += (mins[axis] + maxs[axis]) * 0.5f;
			w.end[axis] += (mins[axis] + maxs[axis]) * 0.5f;
			w.mins[axis] = -w.extents[axis];
			w.maxs[axis] = w.extents[axis];
		}
	}
	if (w.start[0] == w.end[0] && w.start[1] == w.end[1] && w.start[2] == w.end[2])
	{
		/* A position test: every leaf the box touches. */
		float lo[3], hi[3];
		int32_t stack[256];
		int depth = 0;

		for (axis = 0U; axis < 3U; axis++)
		{
			lo[axis] = w.start[axis] - w.extents[axis] - 1.0f;
			hi[axis] = w.start[axis] + w.extents[axis] + 1.0f;
		}
		stack[depth++] = bsp->models[model].headnode;
		while (depth > 0)
		{
			int32_t n = stack[--depth];

			if (n < 0)
			{
				ClipLeaf(&w, -1 - n);
				continue;
			}
			{
				const sg_rune_bsp_node_t *node = &bsp->nodes[n];
				const sg_rune_bsp_plane_t *plane = &bsp->planes[node->plane];
				float dmin = 0.0f, dmax = 0.0f;
				uint32_t a;

				for (a = 0U; a < 3U; a++)
				{
					if (plane->normal[a] >= 0.0f)
					{
						dmin += plane->normal[a] * lo[a];
						dmax += plane->normal[a] * hi[a];
					}
					else
					{
						dmin += plane->normal[a] * hi[a];
						dmax += plane->normal[a] * lo[a];
					}
				}
				if (depth + 2 > (int)(sizeof(stack) / sizeof(stack[0])))
					break;
				if (dmax - plane->distance >= 0.0f)
					stack[depth++] = node->children[0];
				if (dmin - plane->distance < 0.0f)
					stack[depth++] = node->children[1];
			}
		}
	}
	else
		Walk(&w, bsp->models[model].headnode, 0.0f, 1.0f, w.start, w.end);
	*trace_out = w.trace;
	if (trace_out->fraction >= 1.0f)
		memcpy(trace_out->end, end, sizeof(trace_out->end));
	else
		for (axis = 0U; axis < 3U; axis++)
			trace_out->end[axis] = start[axis] +
				trace_out->fraction * (end[axis] - start[axis]);
	return 1;
}

int32_t SG_RuneTraceContents(const sg_rune_bsp_t *bsp, uint32_t model,
	const float model_origin[3], const float point[3])
{
	float local[3];
	int32_t leaf;

	if (!bsp || model >= bsp->model_count || !point)
		return 0;
	local[0] = point[0] - (model_origin ? model_origin[0] : 0.0f);
	local[1] = point[1] - (model_origin ? model_origin[1] : 0.0f);
	local[2] = point[2] - (model_origin ? model_origin[2] : 0.0f);
	leaf = SG_RuneBspLeafAt(bsp, model, local);
	return leaf < 0 ? 0 : bsp->leaves[leaf].contents;
}

void SG_RuneTracePose(const sg_rune_bsp_t *bsp, const float origin[3],
	const float mins[3], const float maxs[3], float view_height,
	sg_rune_pose_t *pose_out)
{
	sg_rune_trace_t trace;
	float below[3], probe[3];
	int32_t contents;

	if (!pose_out)
		return;
	memset(pose_out, 0, sizeof(*pose_out));
	if (!bsp || !origin || !mins || !maxs)
		return;
	/* Fits: the hull at the origin is not inside anything solid. */
	SG_RuneTraceBox(bsp, 0U, NULL, origin, mins, maxs, origin,
		SG_RUNE_MASK_PLAYER_SOLID, &trace);
	if (trace.startsolid || trace.allsolid)
		return;
	pose_out->valid = 1;
	/* Stands: a quarter unit down meets a floor-facing plane. */
	memcpy(below, origin, sizeof(below));
	below[2] -= GROUND_PROBE;
	SG_RuneTraceBox(bsp, 0U, NULL, origin, mins, maxs, below,
		SG_RUNE_MASK_PLAYER_SOLID, &trace);
	if (trace.fraction < 1.0f && !trace.startsolid && trace.normal[2] >= FLOOR_NORMAL_Z)
	{
		pose_out->supported = 1;
		memcpy(pose_out->floor_normal, trace.normal, sizeof(pose_out->floor_normal));
	}
	/* Water: feet, waist, eyes. */
	memcpy(probe, origin, sizeof(probe));
	probe[2] = origin[2] + mins[2] + 1.0f;
	contents = SG_RuneTraceContents(bsp, 0U, NULL, probe);
	if (contents & SG_RUNE_MASK_WATER)
	{
		pose_out->water_type = contents & SG_RUNE_MASK_WATER;
		pose_out->water_level = 1U;
		probe[2] = origin[2] + (mins[2] + maxs[2]) * 0.5f;
		if (SG_RuneTraceContents(bsp, 0U, NULL, probe) & SG_RUNE_MASK_WATER)
		{
			pose_out->water_level = 2U;
			probe[2] = origin[2] + view_height;
			if (SG_RuneTraceContents(bsp, 0U, NULL, probe) & SG_RUNE_MASK_WATER)
				pose_out->water_level = 3U;
		}
	}
}

int SG_RuneTraceClear(const sg_rune_bsp_t *bsp, const float from[3],
	const float to[3], const float mins[3], const float maxs[3])
{
	sg_rune_trace_t trace;

	if (!SG_RuneTraceBox(bsp, 0U, NULL, from, mins, maxs, to,
		SG_RUNE_MASK_PLAYER_SOLID, &trace))
		return 0;
	return !trace.startsolid && !trace.allsolid && trace.fraction >= 1.0f;
}
