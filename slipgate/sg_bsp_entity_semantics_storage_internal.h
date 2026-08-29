#ifndef SG_BSP_ENTITY_SEMANTICS_STORAGE_INTERNAL_H
#define SG_BSP_ENTITY_SEMANTICS_STORAGE_INTERNAL_H

#include "sg_bsp_entity_semantics.h"

/* Only builder-registered arrays and string allocations may be read as
 * candidate facts.  Capacities are the exact issued allocation extents. */
int SG_BspEntitySemanticsStorageRegister(
	sg_bsp_entity_semantics_t *semantics, uint32_t entity_capacity,
	uint32_t edge_capacity);
int SG_BspEntitySemanticsEntityStorageValid(
	const sg_bsp_entity_semantics_t *semantics);
int SG_BspEntitySemanticsEdgeStorageValid(
	const sg_bsp_entity_semantics_t *semantics);
int SG_BspEntitySemanticsStringStorageValid(
	const sg_bsp_entity_semantics_t *semantics);
void SG_BspEntitySemanticsStorageForget(sg_bsp_entity_semantics_t *semantics);

#endif
