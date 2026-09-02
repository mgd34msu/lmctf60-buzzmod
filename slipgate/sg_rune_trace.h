/* Era-4 trace: a box through the map's brushes, the way the engine does it.
 *
 * A box trace expands every brush's planes by the box's extent along the
 * plane's normal and clips the segment against the brushes the tree walk
 * reaches, keeping the nearest hit; a point trace is the same with no
 * extent.  Start-solid and all-solid are reported as the engine reports
 * them.  Inline models are traced in their own tree at an offset.  On top
 * of the trace: what contents a point is in, whether a body in a stance
 * fits at a point and stands on a floor there, how deep it is in water,
 * and whether a body can sweep from one point to another. */
#ifndef SG_RUNE_TRACE_H
#define SG_RUNE_TRACE_H

#include <stdint.h>

#include "sg_rune_bsp.h"

typedef struct sg_rune_trace_s
{
	int startsolid;           /* the start was inside something */
	int allsolid;             /* the whole segment was */
	float fraction;           /* 0..1 of the segment travelled */
	float end[3];
	float normal[3];          /* of the plane hit (zero when none) */
	float distance;
	int32_t contents;         /* of the brush hit, or 0 */
	int32_t texinfo;          /* of the side hit, or -1 */
	uint32_t brush;           /* of the brush hit, or UINT32_MAX */
} sg_rune_trace_t;

/* The segment start..end with the box mins..maxs (NULL for a point)
 * against model's brushes whose contents meet mask.  model_origin, when
 * given, is where the model stands (its tree is at the origin). */
int SG_RuneTraceBox(const sg_rune_bsp_t *bsp, uint32_t model,
	const float model_origin[3], const float start[3], const float mins[3],
	const float maxs[3], const float end[3], int32_t mask,
	sg_rune_trace_t *trace_out);

/* The contents of the leaf holding point in model's tree. */
int32_t SG_RuneTraceContents(const sg_rune_bsp_t *bsp, uint32_t model,
	const float model_origin[3], const float point[3]);

typedef struct sg_rune_pose_s
{
	int valid;                /* the hull fits at the origin */
	int supported;            /* a floor within a quarter unit under it */
	uint8_t water_level;      /* 0 dry, 1 feet, 2 waist, 3 eyes */
	int32_t water_type;       /* contents bits of the water */
	float floor_normal[3];    /* when supported */
} sg_rune_pose_t;

/* A body with hull mins..maxs and eye height view at origin. */
void SG_RuneTracePose(const sg_rune_bsp_t *bsp, const float origin[3],
	const float mins[3], const float maxs[3], float view_height,
	sg_rune_pose_t *pose_out);

/* Whether the hull sweeps from one point to another without meeting the
 * world (both ends included). */
int SG_RuneTraceClear(const sg_rune_bsp_t *bsp, const float from[3],
	const float to[3], const float mins[3], const float maxs[3]);

#endif /* SG_RUNE_TRACE_H */
