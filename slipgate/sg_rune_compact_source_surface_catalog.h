#ifndef SG_RUNE_COMPACT_SOURCE_SURFACE_CATALOG_H
#define SG_RUNE_COMPACT_SOURCE_SURFACE_CATALOG_H

#include <stdint.h>

#include "sg_rune_compact_model.h"

uint64_t SG_RuneCompactSourceSurfaceCatalogSeal(
	const sg_rune_compact_source_surface_t *surfaces,
	uint32_t surface_count,
	const sg_rune_q8_vec3_t *vertices,
	uint32_t vertex_count);

#endif
