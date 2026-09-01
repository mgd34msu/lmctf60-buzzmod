/* Runtime-only region hierarchy for compact destination-field updates. */
#ifndef SG_RUNE_COMPACT_FIELD_REGION_HIERARCHY_H
#define SG_RUNE_COMPACT_FIELD_REGION_HIERARCHY_H

#include <stdint.h>

#include "sg_rune_compact_model.h"

typedef struct sg_rune_compact_field_region_hierarchy_s
	sg_rune_compact_field_region_hierarchy_t;

typedef enum sg_rune_compact_field_region_status_e
{
	SG_RUNE_COMPACT_FIELD_REGION_OK = 0,
	SG_RUNE_COMPACT_FIELD_REGION_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_FIELD_REGION_INVALID_MODEL,
	SG_RUNE_COMPACT_FIELD_REGION_ALLOCATION_FAILED,
	SG_RUNE_COMPACT_FIELD_REGION_STATUS_COUNT
} sg_rune_compact_field_region_status_t;

/* The immutable hierarchy groups cells by source provenance.  A leaf is one
 * (model, area, cluster) tuple.  Its parent is the (model, area) tuple.  The
 * field itself is the implicit root.  These groups order and measure exact
 * dependency-scheduled updates; they never define reachability. */
sg_rune_compact_field_region_status_t
SG_RuneCompactFieldRegionHierarchyCreate(
	const sg_rune_compact_model_t *model,
	sg_rune_compact_field_region_hierarchy_t **hierarchy_out);

void SG_RuneCompactFieldRegionHierarchyDestroy(
	sg_rune_compact_field_region_hierarchy_t *hierarchy);

uint32_t SG_RuneCompactFieldRegionLeafCount(
	const sg_rune_compact_field_region_hierarchy_t *hierarchy);
uint32_t SG_RuneCompactFieldRegionCoarseCount(
	const sg_rune_compact_field_region_hierarchy_t *hierarchy);
uint32_t SG_RuneCompactFieldRegionCellLeaf(
	const sg_rune_compact_field_region_hierarchy_t *hierarchy,
	uint32_t cell);
uint32_t SG_RuneCompactFieldRegionCellCoarse(
	const sg_rune_compact_field_region_hierarchy_t *hierarchy,
	uint32_t cell);
uint32_t SG_RuneCompactFieldRegionLeafCoarse(
	const sg_rune_compact_field_region_hierarchy_t *hierarchy,
	uint32_t leaf);

const char *SG_RuneCompactFieldRegionStatusString(
	sg_rune_compact_field_region_status_t status);

#endif /* SG_RUNE_COMPACT_FIELD_REGION_HIERARCHY_H */
