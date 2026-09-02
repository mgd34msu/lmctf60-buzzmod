/* Era-4 visibility: the map's own potentially-visible sets.
 *
 * The BSP says which clusters may see which; the hook and weapon passes
 * ask it before tracing, so the traces go only where the map says a line
 * could exist.  One decoded row is kept: callers select a cluster and then
 * ask about others. */
#ifndef SG_RUNE_VIS_H
#define SG_RUNE_VIS_H

#include <stdint.h>

struct sg_rune_bsp_s;

typedef struct sg_rune_vis_s
{
	const struct sg_rune_bsp_s *bsp;
	uint32_t cluster_count;   /* at least one */
	uint8_t *row;             /* the selected cluster's decoded row */
	int32_t row_cluster;      /* which cluster row holds, or -2 */
} sg_rune_vis_t;

int SG_RuneVisInit(sg_rune_vis_t *vis, const struct sg_rune_bsp_s *bsp);
void SG_RuneVisFree(sg_rune_vis_t *vis);

/* The cluster of the leaf holding point, or -1 outside the world. */
int32_t SG_RuneVisClusterAt(const struct sg_rune_bsp_s *bsp,
	const float point[3]);

/* Decodes cluster's row (everything visible when the map has no data). */
void SG_RuneVisSelect(sg_rune_vis_t *vis, int32_t cluster);
int SG_RuneVisSees(const sg_rune_vis_t *vis, int32_t cluster);

#endif /* SG_RUNE_VIS_H */
