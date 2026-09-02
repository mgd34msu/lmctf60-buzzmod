#include "sg_rune_vis.h"

#include <stdlib.h>
#include <string.h>

#include "sg_bsp_world.h"

int SG_RuneVisInit(sg_rune_vis_t *vis, const sg_bsp_world_t *bsp)
{
	if (!vis || !bsp)
		return 0;
	memset(vis, 0, sizeof(*vis));
	vis->bsp = bsp;
	vis->cluster_count = bsp->visibility.cluster_count;
	if (vis->cluster_count == 0U)
		vis->cluster_count = 1U;
	vis->row = malloc((size_t)(vis->cluster_count + 7U) / 8U + 1U);
	vis->row_cluster = -2;
	return vis->row != NULL;
}

void SG_RuneVisFree(sg_rune_vis_t *vis)
{
	if (!vis)
		return;
	free(vis->row);
	memset(vis, 0, sizeof(*vis));
}

int32_t SG_RuneVisClusterAt(const sg_bsp_world_t *bsp, const float point[3])
{
	int32_t node;

	if (!bsp || !bsp->models || !bsp->nodes || !bsp->planes || !bsp->leaves)
		return -1;
	node = bsp->models[0].headnode;
	while (node >= 0)
	{
		const sg_bsp_node_t *record = &bsp->nodes[node];
		const sg_bsp_plane_t *plane = &bsp->planes[record->plane];
		float side = plane->normal.value[0] * point[0] +
			plane->normal.value[1] * point[1] +
			plane->normal.value[2] * point[2] - plane->distance;

		node = record->children[side >= 0.0f ? 0 : 1];
	}
	return bsp->leaves[-1 - node].cluster;
}

void SG_RuneVisSelect(sg_rune_vis_t *vis, int32_t cluster)
{
	const sg_bsp_visibility_t *data;
	uint32_t row_bytes, offset, out = 0U;

	if (!vis || cluster == vis->row_cluster)
		return;
	data = &vis->bsp->visibility;
	row_bytes = (vis->cluster_count + 7U) / 8U;
	vis->row_cluster = cluster;
	memset(vis->row, 0xff, row_bytes);   /* no data: everything visible */
	if (cluster < 0 || (uint32_t)cluster >= data->cluster_count || !data->bytes ||
		!data->bit_offsets)
		return;
	offset = data->bit_offsets[cluster][0];
	memset(vis->row, 0, row_bytes);
	while (out < row_bytes && offset < data->byte_count)
	{
		uint8_t byte = data->bytes[offset++];

		if (byte)
		{
			vis->row[out++] = byte;
			continue;
		}
		if (offset >= data->byte_count)
			break;
		out += data->bytes[offset++];
	}
}

int SG_RuneVisSees(const sg_rune_vis_t *vis, int32_t cluster)
{
	if (!vis || cluster < 0 || (uint32_t)cluster >= vis->cluster_count)
		return 0;
	return (vis->row[(uint32_t)cluster >> 3] & (1U << ((uint32_t)cluster & 7U))) != 0;
}
