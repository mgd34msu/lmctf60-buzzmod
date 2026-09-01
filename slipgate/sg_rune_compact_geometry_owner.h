#ifndef SG_RUNE_COMPACT_GEOMETRY_OWNER_H
#define SG_RUNE_COMPACT_GEOMETRY_OWNER_H

#include "sg_rune_compact_builder_owner.h"
#include "sg_rune_compact_geometry.h"

/* Owner-only seam for focused construction tests and later private stages. */
int SG_RuneCompactGeometryOwnerMaterialize(
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_bsp_world_t *world,
	const sg_rune_compact_identity_t *identity,
	const sg_rune_compact_geometry_allocator_t *allocator,
	sg_rune_compact_geometry_t **geometry_out,
	sg_rune_compact_geometry_error_t *error_out);

#endif
