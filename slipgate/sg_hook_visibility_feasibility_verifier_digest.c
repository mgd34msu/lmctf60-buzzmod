#include "sg_hook_visibility_feasibility_internal.h"

#define VERIFIER_FNV_OFFSET UINT64_C(1469598103934665603)
#define VERIFIER_FNV_PRIME UINT64_C(1099511628211)

static uint64_t DigestMemory(uint64_t hash, const void *memory, size_t size)
{
	const uint8_t *bytes = memory;
	size_t index;

	for (index = 0U; index < size; index++)
		hash = (hash ^ bytes[index]) * VERIFIER_FNV_PRIME;
	return hash;
}

static uint64_t DigestU64(uint64_t hash, uint64_t value)
{
	uint32_t byte;

	for (byte = 0U; byte < 8U; byte++)
		hash = (hash ^ (uint8_t)(value >> (byte * 8U))) * VERIFIER_FNV_PRIME;
	return hash;
}

uint64_t SG_HookVisibilityFeasibilityVerifierSourceDigest(
	const sg_hook_visibility_feasibility_sources_t *sources)
{
	const sg_bsp_world_t *world = sources->collision->world;
	uint64_t hash = DigestMemory(VERIFIER_FNV_OFFSET,
		&sources->collision->identity, sizeof(sources->collision->identity));

	hash = DigestMemory(hash, world->planes,
		(size_t)world->plane_count * sizeof(*world->planes));
	hash = DigestMemory(hash, world->nodes,
		(size_t)world->node_count * sizeof(*world->nodes));
	hash = DigestMemory(hash, world->leaves,
		(size_t)world->leaf_count * sizeof(*world->leaves));
	hash = DigestMemory(hash, world->leaf_brushes,
		(size_t)world->leaf_brush_count * sizeof(*world->leaf_brushes));
	hash = DigestMemory(hash, world->models,
		(size_t)world->model_count * sizeof(*world->models));
	hash = DigestMemory(hash, world->brushes,
		(size_t)world->brush_count * sizeof(*world->brushes));
	hash = DigestMemory(hash, world->brush_sides,
		(size_t)world->brush_side_count * sizeof(*world->brush_sides));
	hash = DigestMemory(hash, world->texinfos,
		(size_t)world->texinfo_count * sizeof(*world->texinfos));
	hash = DigestMemory(hash, &sources->origins, sizeof(sources->origins));
	hash = DigestMemory(hash, &sources->stance, sizeof(sources->stance));
	hash = DigestMemory(hash, sources->controls,
		(size_t)sources->control_count * sizeof(*sources->controls));
	hash = DigestMemory(hash, sources->surface_rules,
		(size_t)sources->surface_rule_count * sizeof(*sources->surface_rules));
	hash = DigestMemory(hash, &sources->fire_law, sizeof(sources->fire_law));
	hash = DigestU64(hash, sources->producer_identity);
	return DigestU64(hash, sources->verifier_identity);
}
