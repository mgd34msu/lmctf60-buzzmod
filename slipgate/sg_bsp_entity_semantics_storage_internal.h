#ifndef SG_BSP_ENTITY_SEMANTICS_STORAGE_INTERNAL_H
#define SG_BSP_ENTITY_SEMANTICS_STORAGE_INTERNAL_H

#include "sg_bsp_entity_semantics.h"

/* Only builder-registered string allocations may be read as candidate facts. */
int SG_BspEntitySemanticsStringStorageRegister(
	sg_bsp_entity_semantics_t *semantics);
int SG_BspEntitySemanticsStringStorageValid(
	const sg_bsp_entity_semantics_t *semantics);
void SG_BspEntitySemanticsStringStorageForget(
	sg_bsp_entity_semantics_t *semantics);

#endif
