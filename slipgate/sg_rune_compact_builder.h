#ifndef SG_RUNE_COMPACT_BUILDER_H
#define SG_RUNE_COMPACT_BUILDER_H

#include "sg_bsp_entity_semantics_publication.h"
#include "sg_host_law_publication.h"
#include "sg_rune_compact_model.h"
#include "sg_static_visibility.h"
#include "sg_weapon_effect_profile.h"

typedef struct sg_rune_compact_builder_s sg_rune_compact_builder_t;

typedef enum sg_rune_compact_builder_error_code_e
{
	SG_RUNE_COMPACT_BUILDER_ERROR_NONE = 0,
	SG_RUNE_COMPACT_BUILDER_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_BUILDER_ERROR_HOST_AUTHORITY,
	SG_RUNE_COMPACT_BUILDER_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_BUILDER_ERROR_BSP_LOAD,
	SG_RUNE_COMPACT_BUILDER_ERROR_CONFIGURATION,
	SG_RUNE_COMPACT_BUILDER_ERROR_CONFIGURATION_AUDIT,
	SG_RUNE_COMPACT_BUILDER_ERROR_SEMANTICS,
	SG_RUNE_COMPACT_BUILDER_ERROR_SEMANTICS_AUDIT,
	SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_SEMANTICS,
	SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUDIT,
	SG_RUNE_COMPACT_BUILDER_ERROR_VISIBILITY,
	SG_RUNE_COMPACT_BUILDER_ERROR_VISIBILITY_AUDIT,
	SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUTHORITY,
	SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_AUTHORITY,
	SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_PROFILE,
	SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_BUILDER_ERROR_CODE_COUNT
} sg_rune_compact_builder_error_code_t;

typedef struct sg_rune_compact_builder_error_s
{
	sg_rune_compact_builder_error_code_t code;
	uint32_t record;
	uint64_t expected;
	uint64_t observed;
} sg_rune_compact_builder_error_t;

typedef struct sg_rune_compact_builder_input_s
{
	const sg_host_law_construction_t *construction;
	const sg_configuration_limits_t *configuration_limits;
	const sg_configuration_semantics_limits_t *semantics_limits;
	const sg_static_visibility_limits_t *visibility_limits;
} sg_rune_compact_builder_input_t;

/* Arrays are borrowed from the builder and remain valid until destroy. */
typedef struct sg_rune_compact_builder_view_s
{
	sg_rune_compact_identity_t identity;
	const sg_rune_weapon_profile_t *weapon_profiles;
	const sg_weapon_profile_t *resolved_weapon_profiles;
	uint32_t weapon_profile_count;
} sg_rune_compact_builder_view_t;

int SG_RuneCompactBuilderBuild(
	const sg_rune_compact_builder_input_t *input,
	sg_rune_compact_builder_t **builder_out,
	sg_rune_compact_builder_error_t *error_out);
int SG_RuneCompactBuilderBuildDevelopmentAudit(
	const sg_rune_compact_builder_input_t *input,
	sg_rune_compact_builder_t **builder_out,
	sg_rune_compact_builder_error_t *error_out);
int SG_RuneCompactBuilderRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_view_t *view_out);
void SG_RuneCompactBuilderDestroy(sg_rune_compact_builder_t *builder);
const char *SG_RuneCompactBuilderErrorString(
	sg_rune_compact_builder_error_code_t code);

#endif
