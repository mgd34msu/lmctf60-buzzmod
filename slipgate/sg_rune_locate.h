/* Era-4 locator: world point to cell.
 *
 * A uniform grid over the complex's bounds lists the cells whose bounds
 * touch each bucket; a query tests the bucket's candidates against their
 * own facet planes and returns the cell that contains the point, preferring
 * one valid for the asked stance.  Built once per level from the artifact. */
#ifndef SG_RUNE_LOCATE_H
#define SG_RUNE_LOCATE_H

#include <stdint.h>

#include "sg_rune_artifact.h"

typedef struct sg_rune_locator_s
{
	const sg_rune_artifact_t *artifact;
	int32_t origin_q8[3];
	uint32_t dims[3];
	int32_t bucket_q8;
	uint32_t *first;          /* per bucket, into entries; dims product + 1 */
	uint32_t *entries;        /* cell indices */
	uint32_t entry_count;
} sg_rune_locator_t;

int SG_RuneLocatorBuild(sg_rune_locator_t *locator,
	const sg_rune_artifact_t *artifact);
void SG_RuneLocatorFree(sg_rune_locator_t *locator);

/* The cell containing origin, or SG_RUNE_CX_INDEX_NONE.  stance is a
 * SG_RUNE_MOVE_* bit; a cell valid for it wins over one that is not.  A
 * point up to `slack` units outside every candidate still resolves to the
 * least-violated candidate.  violation_out receives how far outside (0 when
 * inside). */
uint32_t SG_RuneLocate(const sg_rune_locator_t *locator,
	const float origin[3], uint8_t stance, float slack, float *violation_out);

/* The supported (floor) cell whose floor point is nearest to point within
 * radius horizontally and rise vertically, or INDEX_NONE. */
uint32_t SG_RuneLocateNearestFloor(const sg_rune_locator_t *locator,
	const float point[3], float radius, float rise);

/* How far outside cell the point lies (0 inside); INFINITY when the cell is
 * out of range. */
float SG_RuneCellViolation(const sg_rune_artifact_t *artifact, uint32_t cell,
	const float origin[3]);

#endif /* SG_RUNE_LOCATE_H */
