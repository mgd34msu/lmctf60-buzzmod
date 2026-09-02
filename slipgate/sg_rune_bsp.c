#include "sg_rune_bsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sg_rune_crc.h"

#define MAGIC "IBSP"
#define VERSION 38U
#define HEADER_BYTES (4U + 4U + SG_RUNE_BSP_LUMP_COUNT * 8U)

/* On-disk record sizes (all little-endian). */
#define DISK_PLANE 20U
#define DISK_NODE 28U
#define DISK_LEAF 28U
#define DISK_LEAF_BRUSH 2U
#define DISK_MODEL 48U
#define DISK_BRUSH 12U
#define DISK_SIDE 4U
#define DISK_TEXINFO 76U

typedef struct lump_s
{
	uint32_t offset, length;
} lump_t;

static uint32_t U32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
		((uint32_t)p[3] << 24);
}

static int32_t I32(const uint8_t *p)
{
	return (int32_t)U32(p);
}

static int16_t I16(const uint8_t *p)
{
	return (int16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint16_t U16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static float F32(const uint8_t *p)
{
	uint32_t bits = U32(p);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int Fault(sg_rune_bsp_fault_t *fault, const char *what, int lump,
	uint32_t record)
{
	if (fault)
	{
		fault->what = what;
		fault->lump = lump;
		fault->record = record;
	}
	return 0;
}

/* A lump's byte range, checked against the image and the record size. */
static int Lump(const uint8_t *image, size_t size, const lump_t *lumps, int lump,
	uint32_t record_bytes, const uint8_t **bytes_out, uint32_t *count_out,
	sg_rune_bsp_fault_t *fault)
{
	const lump_t *l = &lumps[lump];

	if (l->offset > size || l->length > size - l->offset ||
		(record_bytes && l->length % record_bytes))
		return Fault(fault, "lump", lump, 0U);
	*bytes_out = image + l->offset;
	*count_out = record_bytes ? l->length / record_bytes : l->length;
	return 1;
}

static size_t Align(size_t bytes)
{
	return (bytes + 15U) & ~(size_t)15U;
}

int SG_RuneBspLoadImage(const uint8_t *image, size_t size, sg_rune_bsp_t *bsp,
	sg_rune_bsp_fault_t *fault)
{
	lump_t lumps[SG_RUNE_BSP_LUMP_COUNT];
	const uint8_t *entities, *planes, *nodes, *leaves, *leaf_brushes, *models;
	const uint8_t *brushes, *sides, *texinfos, *vis;
	uint32_t entity_bytes, plane_count, node_count, leaf_count, leaf_brush_count;
	uint32_t model_count, brush_count, side_count, texinfo_count, vis_bytes;
	uint32_t cluster_count = 0U, index, lump;
	size_t need, at;
	uint8_t *arena;
	sg_rune_bsp_plane_t *out_planes;
	sg_rune_bsp_node_t *out_nodes;
	sg_rune_bsp_leaf_t *out_leaves;
	uint32_t *out_leaf_brushes;
	sg_rune_bsp_model_t *out_models;
	sg_rune_bsp_brush_t *out_brushes;
	sg_rune_bsp_side_t *out_sides;
	sg_rune_bsp_texinfo_t *out_texinfos;
	uint32_t *out_offsets;
	uint8_t *out_vis;
	char *out_entities;

	if (fault)
		memset(fault, 0, sizeof(*fault));
	if (!bsp)
		return 0;
	memset(bsp, 0, sizeof(*bsp));
	if (!image || size < HEADER_BYTES)
		return Fault(fault, "io", -1, 0U);
	if (memcmp(image, MAGIC, 4U) != 0)
		return Fault(fault, "magic", -1, 0U);
	if (U32(image + 4U) != VERSION)
		return Fault(fault, "version", -1, U32(image + 4U));
	for (lump = 0U; lump < SG_RUNE_BSP_LUMP_COUNT; lump++)
	{
		lumps[lump].offset = U32(image + 8U + lump * 8U);
		lumps[lump].length = U32(image + 12U + lump * 8U);
	}
	if (!Lump(image, size, lumps, SG_RUNE_BSP_LUMP_ENTITIES, 0U, &entities,
			&entity_bytes, fault) ||
		!Lump(image, size, lumps, SG_RUNE_BSP_LUMP_PLANES, DISK_PLANE, &planes,
			&plane_count, fault) ||
		!Lump(image, size, lumps, SG_RUNE_BSP_LUMP_NODES, DISK_NODE, &nodes,
			&node_count, fault) ||
		!Lump(image, size, lumps, SG_RUNE_BSP_LUMP_LEAVES, DISK_LEAF, &leaves,
			&leaf_count, fault) ||
		!Lump(image, size, lumps, SG_RUNE_BSP_LUMP_LEAF_BRUSHES, DISK_LEAF_BRUSH,
			&leaf_brushes, &leaf_brush_count, fault) ||
		!Lump(image, size, lumps, SG_RUNE_BSP_LUMP_MODELS, DISK_MODEL, &models,
			&model_count, fault) ||
		!Lump(image, size, lumps, SG_RUNE_BSP_LUMP_BRUSHES, DISK_BRUSH, &brushes,
			&brush_count, fault) ||
		!Lump(image, size, lumps, SG_RUNE_BSP_LUMP_BRUSH_SIDES, DISK_SIDE, &sides,
			&side_count, fault) ||
		!Lump(image, size, lumps, SG_RUNE_BSP_LUMP_TEXINFO, DISK_TEXINFO, &texinfos,
			&texinfo_count, fault) ||
		!Lump(image, size, lumps, SG_RUNE_BSP_LUMP_VISIBILITY, 0U, &vis, &vis_bytes,
			fault))
		return 0;
	if (plane_count == 0U || node_count == 0U || leaf_count == 0U || model_count == 0U)
		return Fault(fault, "lump", plane_count ? (node_count ? (leaf_count ?
			SG_RUNE_BSP_LUMP_MODELS : SG_RUNE_BSP_LUMP_LEAVES) : SG_RUNE_BSP_LUMP_NODES) :
			SG_RUNE_BSP_LUMP_PLANES, 0U);
	if (vis_bytes >= 4U)
	{
		cluster_count = U32(vis);
		if (cluster_count > 65536U || (uint64_t)cluster_count * 8U + 4U > vis_bytes)
			return Fault(fault, "lump", SG_RUNE_BSP_LUMP_VISIBILITY, cluster_count);
	}
	/* Entities end at the first NUL or the lump end. */
	{
		uint32_t n;

		for (n = 0U; n < entity_bytes && entities[n] != 0; n++)
			;
		entity_bytes = n;
	}
#define PIECE(count, type) Align((size_t)(count) * sizeof(type) + 4U)
	need = PIECE(plane_count, sg_rune_bsp_plane_t) + PIECE(node_count, sg_rune_bsp_node_t) +
		PIECE(leaf_count, sg_rune_bsp_leaf_t) + PIECE(leaf_brush_count, uint32_t) +
		PIECE(model_count, sg_rune_bsp_model_t) + PIECE(brush_count, sg_rune_bsp_brush_t) +
		PIECE(side_count, sg_rune_bsp_side_t) + PIECE(texinfo_count, sg_rune_bsp_texinfo_t) +
		PIECE(cluster_count, uint32_t) + PIECE(vis_bytes, uint8_t) +
		PIECE(entity_bytes + 1U, char);
#undef PIECE
	arena = calloc(1U, need);
	if (!arena)
		return Fault(fault, "memory", -1, 0U);
	at = 0U;
#define TAKE(pointer, count, type) \
	do { pointer = (type *)(arena + at); at += Align((size_t)(count) * sizeof(type) + 4U); } while (0)
	TAKE(out_planes, plane_count, sg_rune_bsp_plane_t);
	TAKE(out_nodes, node_count, sg_rune_bsp_node_t);
	TAKE(out_leaves, leaf_count, sg_rune_bsp_leaf_t);
	TAKE(out_leaf_brushes, leaf_brush_count, uint32_t);
	TAKE(out_models, model_count, sg_rune_bsp_model_t);
	TAKE(out_brushes, brush_count, sg_rune_bsp_brush_t);
	TAKE(out_sides, side_count, sg_rune_bsp_side_t);
	TAKE(out_texinfos, texinfo_count, sg_rune_bsp_texinfo_t);
	TAKE(out_offsets, cluster_count, uint32_t);
	TAKE(out_vis, vis_bytes, uint8_t);
	TAKE(out_entities, entity_bytes + 1U, char);
#undef TAKE
	(void)at;

	for (index = 0U; index < plane_count; index++)
	{
		const uint8_t *p = planes + index * DISK_PLANE;
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
			out_planes[index].normal[axis] = F32(p + axis * 4U);
		out_planes[index].distance = F32(p + 12U);
		if (!isfinite(out_planes[index].normal[0]) ||
			!isfinite(out_planes[index].normal[1]) ||
			!isfinite(out_planes[index].normal[2]) ||
			!isfinite(out_planes[index].distance))
		{
			free(arena);
			return Fault(fault, "reference", SG_RUNE_BSP_LUMP_PLANES, index);
		}
	}
	for (index = 0U; index < node_count; index++)
	{
		const uint8_t *p = nodes + index * DISK_NODE;
		int side;

		out_nodes[index].plane = U32(p);
		if (out_nodes[index].plane >= plane_count)
		{
			free(arena);
			return Fault(fault, "reference", SG_RUNE_BSP_LUMP_NODES, index);
		}
		for (side = 0; side < 2; side++)
		{
			int32_t child = I32(p + 4U + (uint32_t)side * 4U);

			out_nodes[index].children[side] = child;
			if (child >= 0 ? (uint32_t)child >= node_count :
				(uint32_t)(-1 - child) >= leaf_count)
			{
				free(arena);
				return Fault(fault, "reference", SG_RUNE_BSP_LUMP_NODES, index);
			}
		}
	}
	for (index = 0U; index < leaf_count; index++)
	{
		const uint8_t *p = leaves + index * DISK_LEAF;
		sg_rune_bsp_leaf_t *leaf = &out_leaves[index];

		leaf->contents = I32(p);
		leaf->cluster = I16(p + 4U);
		leaf->area = I16(p + 6U);
		{
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
			{
				leaf->mins[axis] = (float)I16(p + 8U + axis * 2U);
				leaf->maxs[axis] = (float)I16(p + 14U + axis * 2U);
			}
		}
		leaf->first_leaf_brush = U16(p + 24U);
		leaf->leaf_brush_count = U16(p + 26U);
		if (leaf->first_leaf_brush > leaf_brush_count ||
			leaf->leaf_brush_count > leaf_brush_count - leaf->first_leaf_brush ||
			(leaf->cluster >= 0 && cluster_count && (uint32_t)leaf->cluster >= cluster_count))
		{
			free(arena);
			return Fault(fault, "reference", SG_RUNE_BSP_LUMP_LEAVES, index);
		}
	}
	for (index = 0U; index < leaf_brush_count; index++)
	{
		out_leaf_brushes[index] = U16(leaf_brushes + index * DISK_LEAF_BRUSH);
		if (out_leaf_brushes[index] >= brush_count)
		{
			free(arena);
			return Fault(fault, "reference", SG_RUNE_BSP_LUMP_LEAF_BRUSHES, index);
		}
	}
	for (index = 0U; index < model_count; index++)
	{
		const uint8_t *p = models + index * DISK_MODEL;
		sg_rune_bsp_model_t *model = &out_models[index];
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
		{
			model->mins[axis] = F32(p + axis * 4U);
			model->maxs[axis] = F32(p + 12U + axis * 4U);
			model->origin[axis] = F32(p + 24U + axis * 4U);
		}
		model->headnode = I32(p + 36U);
		if (model->headnode >= 0 ? (uint32_t)model->headnode >= node_count :
			(uint32_t)(-1 - model->headnode) >= leaf_count)
		{
			free(arena);
			return Fault(fault, "reference", SG_RUNE_BSP_LUMP_MODELS, index);
		}
	}
	for (index = 0U; index < brush_count; index++)
	{
		const uint8_t *p = brushes + index * DISK_BRUSH;
		sg_rune_bsp_brush_t *brush = &out_brushes[index];

		brush->first_side = U32(p);
		brush->side_count = U32(p + 4U);
		brush->contents = I32(p + 8U);
		if (brush->first_side > side_count ||
			brush->side_count > side_count - brush->first_side)
		{
			free(arena);
			return Fault(fault, "reference", SG_RUNE_BSP_LUMP_BRUSHES, index);
		}
	}
	for (index = 0U; index < side_count; index++)
	{
		const uint8_t *p = sides + index * DISK_SIDE;

		out_sides[index].plane = U16(p);
		out_sides[index].texinfo = I16(p + 2U);
		if (out_sides[index].plane >= plane_count ||
			(out_sides[index].texinfo >= 0 &&
			 (uint32_t)out_sides[index].texinfo >= texinfo_count))
		{
			free(arena);
			return Fault(fault, "reference", SG_RUNE_BSP_LUMP_BRUSH_SIDES, index);
		}
	}
	for (index = 0U; index < texinfo_count; index++)
	{
		const uint8_t *p = texinfos + index * DISK_TEXINFO;

		out_texinfos[index].flags = I32(p + 32U);
		out_texinfos[index].value = I32(p + 36U);
		memcpy(out_texinfos[index].texture, p + 40U, SG_RUNE_BSP_TEXTURE_BYTES);
		out_texinfos[index].texture[SG_RUNE_BSP_TEXTURE_BYTES - 1U] = 0;
	}
	for (index = 0U; index < cluster_count; index++)
	{
		out_offsets[index] = U32(vis + 4U + index * 8U);   /* the PVS row */
		if (out_offsets[index] >= vis_bytes)
		{
			free(arena);
			return Fault(fault, "reference", SG_RUNE_BSP_LUMP_VISIBILITY, index);
		}
	}
	if (vis_bytes)
		memcpy(out_vis, vis, vis_bytes);
	memcpy(out_entities, entities, entity_bytes);
	out_entities[entity_bytes] = 0;

	bsp->planes = out_planes;
	bsp->plane_count = plane_count;
	bsp->nodes = out_nodes;
	bsp->node_count = node_count;
	bsp->leaves = out_leaves;
	bsp->leaf_count = leaf_count;
	bsp->leaf_brushes = out_leaf_brushes;
	bsp->leaf_brush_count = leaf_brush_count;
	bsp->models = out_models;
	bsp->model_count = model_count;
	bsp->brushes = out_brushes;
	bsp->brush_count = brush_count;
	bsp->sides = out_sides;
	bsp->side_count = side_count;
	bsp->texinfos = out_texinfos;
	bsp->texinfo_count = texinfo_count;
	bsp->visibility.cluster_count = cluster_count;
	bsp->visibility.pvs_offsets = out_offsets;
	bsp->visibility.bytes = out_vis;
	bsp->visibility.byte_count = vis_bytes;
	bsp->entities = out_entities;
	bsp->entity_bytes = entity_bytes;
	bsp->file_crc32 = SG_RuneCrc32(image, size);
	bsp->entity_crc32 = SG_RuneCrc32((const uint8_t *)out_entities, entity_bytes);
	bsp->file_bytes = size;
	bsp->arena = arena;
	return 1;
}

int SG_RuneBspLoadFile(const char *path, sg_rune_bsp_t *bsp,
	sg_rune_bsp_fault_t *fault)
{
	FILE *file;
	long length;
	uint8_t *image;
	int ok;

	if (fault)
		memset(fault, 0, sizeof(*fault));
	if (!path || !bsp)
		return 0;
	memset(bsp, 0, sizeof(*bsp));
	file = fopen(path, "rb");
	if (!file)
		return Fault(fault, "io", -1, 0U);
	if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
		fseek(file, 0L, SEEK_SET) != 0)
	{
		fclose(file);
		return Fault(fault, "io", -1, 0U);
	}
	image = malloc((size_t)length ? (size_t)length : 1U);
	if (!image)
	{
		fclose(file);
		return Fault(fault, "memory", -1, 0U);
	}
	if (fread(image, 1U, (size_t)length, file) != (size_t)length)
	{
		free(image);
		fclose(file);
		return Fault(fault, "io", -1, 0U);
	}
	fclose(file);
	ok = SG_RuneBspLoadImage(image, (size_t)length, bsp, fault);
	free(image);
	return ok;
}

void SG_RuneBspFree(sg_rune_bsp_t *bsp)
{
	if (!bsp)
		return;
	free(bsp->arena);
	free(bsp->entities_owned);
	memset(bsp, 0, sizeof(*bsp));
}

int SG_RuneBspReplaceEntities(sg_rune_bsp_t *bsp, const char *text)
{
	size_t length;
	char *copy;

	if (!bsp || !text)
		return 0;
	length = strlen(text);
	copy = malloc(length + 1U);
	if (!copy)
		return 0;
	memcpy(copy, text, length + 1U);
	free(bsp->entities_owned);
	bsp->entities_owned = copy;
	bsp->entities = copy;
	bsp->entity_bytes = (uint32_t)length;
	bsp->entity_crc32 = SG_RuneCrc32((const uint8_t *)copy, length);
	return 1;
}

int32_t SG_RuneBspLeafAt(const sg_rune_bsp_t *bsp, uint32_t model,
	const float point[3])
{
	int32_t node;

	if (!bsp || model >= bsp->model_count || !point)
		return -1;
	node = bsp->models[model].headnode;
	while (node >= 0)
	{
		const sg_rune_bsp_node_t *record = &bsp->nodes[node];
		const sg_rune_bsp_plane_t *plane = &bsp->planes[record->plane];
		float side = plane->normal[0] * point[0] + plane->normal[1] * point[1] +
			plane->normal[2] * point[2] - plane->distance;

		node = record->children[side >= 0.0f ? 0 : 1];
	}
	return -1 - node;
}
