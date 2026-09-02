/* bsppoint: what the RUNE's own BSP reader and trace see at a point: the
 * leaf and its contents, the brushes the leaf lists with their contents,
 * and whether the standing hull fits there.  usage: bsppoint MAP.bsp x y z */
#include <stdio.h>
#include <stdlib.h>
#include "slipgate/sg_rune_bsp.h"
#include "slipgate/sg_rune_trace.h"
#include "slipgate/sg_rune_law.h"

int main(int argc, char **argv)
{
	sg_rune_bsp_t bsp;
	sg_rune_bsp_fault_t fault;
	sg_rune_law_t law;
	float p[3];
	int32_t leaf;
	sg_rune_trace_t trace;
	uint32_t k;

	if (argc != 5)
	{
		fprintf(stderr, "usage: bsppoint MAP.bsp x y z\n");
		return 2;
	}
	if (!SG_RuneBspLoadFile(argv[1], &bsp, &fault))
	{
		fprintf(stderr, "load failed\n");
		return 1;
	}
	SG_RuneLawEngine(&law, 800.0f);
	p[0] = (float)atof(argv[2]); p[1] = (float)atof(argv[3]); p[2] = (float)atof(argv[4]);
	leaf = SG_RuneBspLeafAt(&bsp, 0U, p);
	printf("leaf %d contents 0x%x cluster %d area %d\n", leaf,
		leaf >= 0 ? bsp.leaves[leaf].contents : 0,
		leaf >= 0 ? bsp.leaves[leaf].cluster : -1, leaf >= 0 ? bsp.leaves[leaf].area : -1);
	if (leaf >= 0)
		for (k = 0U; k < bsp.leaves[leaf].leaf_brush_count; k++)
		{
			uint32_t b = bsp.leaf_brushes[bsp.leaves[leaf].first_leaf_brush + k];

			printf("  brush %u contents 0x%x sides %u\n", b, bsp.brushes[b].contents,
				bsp.brushes[b].side_count);
		}
	SG_RuneTraceBox(&bsp, 0U, NULL, p, law.standing_mins, law.standing_maxs, p,
		SG_RUNE_MASK_PLAYER_SOLID, &trace);
	printf("standing hull at the point: startsolid %d allsolid %d (brush %u contents 0x%x)\n",
		trace.startsolid, trace.allsolid, trace.brush, trace.contents);
	if (trace.startsolid && trace.brush < bsp.brush_count)
	{
		const sg_rune_bsp_brush_t *br = &bsp.brushes[trace.brush];
		uint32_t k;

		printf("  solid brush %u contents 0x%x:\n", trace.brush, br->contents);
		for (k = 0; k < br->side_count; k++)
		{
			const sg_rune_bsp_side_t *side = &bsp.sides[br->first_side + k];
			const sg_rune_bsp_plane_t *pl = &bsp.planes[side->plane];

			printf("    side %u: (%.3f %.3f %.3f) d=%.1f tex %s flags 0x%x\n", k,
				pl->normal[0], pl->normal[1], pl->normal[2], pl->distance,
				side->texinfo >= 0 && (uint32_t)side->texinfo < bsp.texinfo_count ? bsp.texinfos[side->texinfo].texture : "-",
				side->texinfo >= 0 && (uint32_t)side->texinfo < bsp.texinfo_count ? bsp.texinfos[side->texinfo].flags : 0);
		}
	}
	printf("point contents 0x%x\n", SG_RuneTraceContents(&bsp, 0U, NULL, p));
	SG_RuneBspFree(&bsp);
	return 0;
}
