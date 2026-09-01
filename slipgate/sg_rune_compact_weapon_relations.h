/* Identity-bound compact weapon response policy for RUNEs. */
#ifndef SG_RUNE_COMPACT_WEAPON_RELATIONS_H
#define SG_RUNE_COMPACT_WEAPON_RELATIONS_H

#include <stdint.h>

#include "sg_rune_compact_response_partition.h"

#define SG_RUNE_COMPACT_WEAPON_RELATIONS_VERSION UINT16_C(3)

typedef struct sg_rune_compact_weapon_relations_s
	sg_rune_compact_weapon_relations_t;

typedef struct sg_rune_compact_weapon_relations_view_s
{
	uint16_t version;
	uint16_t reserved;
	sg_rune_compact_identity_t identity;
	const sg_rune_compact_weapon_relations_t *owner;
	/* Relations retain the response owner.  They own only facts and occluder
	 * policy, presented together as the one model projection. */
	sg_rune_compact_response_projection_t response;
} sg_rune_compact_weapon_relations_view_t;

typedef enum sg_rune_compact_weapon_relations_error_code_e
{
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_NONE = 0,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_BUILDER_READ,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_GEOMETRY_READ,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_SOURCE,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_GEOMETRY,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_INVALID_RESPONSE,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_OVERFLOW,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_ERROR_CODE_COUNT
} sg_rune_compact_weapon_relations_error_code_t;

typedef enum sg_rune_compact_weapon_relations_record_domain_e
{
	SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_OWNER = 0,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_IDENTITY,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_GEOMETRY_CELL,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_RESPONSE,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_RELATION,
	SG_RUNE_COMPACT_WEAPON_RELATIONS_RECORD_OCCLUDER
} sg_rune_compact_weapon_relations_record_domain_t;

typedef struct sg_rune_compact_weapon_relations_error_s
{
	sg_rune_compact_weapon_relations_error_code_t code;
	sg_rune_compact_weapon_relations_record_domain_t domain;
	uint32_t record;
	uint64_t expected;
	uint64_t observed;
} sg_rune_compact_weapon_relations_error_t;

int SG_RuneCompactWeaponRelationsBuild(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_response_partition_t *response,
	sg_rune_compact_weapon_relations_t **relations_out,
	sg_rune_compact_weapon_relations_error_t *error_out);

int SG_RuneCompactWeaponRelationsRead(
	const sg_rune_compact_weapon_relations_t *relations,
	sg_rune_compact_weapon_relations_view_t *view_out);

int SG_RuneCompactWeaponRelationsQuery(
	const sg_rune_compact_weapon_relations_view_t *view,
	uint32_t source_fragment, uint32_t target_patch,
	sg_rune_compact_response_fact_t *fact_out);

void SG_RuneCompactWeaponRelationsDestroy(
	sg_rune_compact_weapon_relations_t *relations);

const char *SG_RuneCompactWeaponRelationsErrorString(
	sg_rune_compact_weapon_relations_error_code_t code);

#if defined(SG_RUNE_COMPACT_WEAPON_RELATIONS_TESTING)
void SG_RuneCompactWeaponRelationsTestFailAfter(uint32_t allocation);
uint32_t SG_RuneCompactWeaponRelationsTestAllocationCount(void);
#endif

#endif
