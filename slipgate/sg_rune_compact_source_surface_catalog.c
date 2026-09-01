#include "sg_rune_compact_source_surface_catalog.h"

#define CATALOG_FNV_OFFSET UINT64_C(14695981039346656037)
#define CATALOG_FNV_PRIME UINT64_C(1099511628211)

static uint64_t MixByte(uint64_t state, uint8_t value)
{
	return (state ^ (uint64_t)value) * CATALOG_FNV_PRIME;
}

static uint64_t MixU32(uint64_t state, uint32_t value)
{
	uint32_t byte_index;

	for (byte_index = 0U; byte_index < 4U; byte_index++)
	{
		state = MixByte(state, (uint8_t)(value & UINT32_C(0xff)));
		value >>= 8U;
	}
	return state;
}

uint64_t SG_RuneCompactSourceSurfaceCatalogSeal(
	const sg_rune_compact_source_surface_t *surfaces,
	uint32_t surface_count,
	const sg_rune_q8_vec3_t *vertices,
	uint32_t vertex_count)
{
	uint64_t state = CATALOG_FNV_OFFSET;
	uint32_t index;

	if ((surface_count != 0U && surfaces == NULL) ||
		(vertex_count != 0U && vertices == NULL))
		return 0U;
	state = MixU32(state, UINT32_C(0x53534331));
	state = MixU32(state, surface_count);
	state = MixU32(state, vertex_count);
	for (index = 0U; index < surface_count; index++)
	{
		const sg_rune_compact_source_surface_t *surface = &surfaces[index];
		uint32_t axis;

		state = MixU32(state, surface->source.model);
		state = MixU32(state, surface->source.brush);
		state = MixU32(state, surface->source.brush_side);
		state = MixU32(state, surface->source.plane);
		state = MixU32(state, (uint32_t)surface->frame);
		state = MixU32(state, surface->cell.value);
		state = MixU32(state, surface->parent_surface);
		state = MixU32(state, surface->split_ordinal);
		for (axis = 0U; axis < 3U; axis++)
			state = MixU32(state, surface->plane.normal_bits[axis]);
		state = MixU32(state, surface->plane.distance_bits);
		state = MixU32(state, surface->vertices.first);
		state = MixU32(state, surface->vertices.count);
	}
	for (index = 0U; index < vertex_count; index++)
	{
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
			state = MixU32(state, (uint32_t)vertices[index].value[axis]);
	}
	return state != 0U ? state : UINT64_C(0x5353433100000001);
}
