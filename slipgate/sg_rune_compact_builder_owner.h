#ifndef SG_RUNE_COMPACT_BUILDER_OWNER_H
#define SG_RUNE_COMPACT_BUILDER_OWNER_H

#include "sg_bsp_entity_semantics.h"
#include "sg_rune_compact_builder.h"

typedef struct sg_rune_compact_builder_owner_view_s
{
	sg_rune_compact_identity_t identity;
	const sg_bsp_world_t *world;
	const sg_host_collision_authority_t *collision;
	const sg_host_law_view_t *host_law;
	const sg_rune_source_weapon_law_t *weapon_law;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_bsp_entity_semantics_t *entity_semantics;
	const sg_static_visibility_t *visibility;
} sg_rune_compact_builder_owner_view_t;

int SG_RuneCompactBuilderOwnerRead(
	const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out);

#endif
