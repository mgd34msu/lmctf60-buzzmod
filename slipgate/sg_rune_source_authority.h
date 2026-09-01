#ifndef SG_RUNE_SOURCE_AUTHORITY_H
#define SG_RUNE_SOURCE_AUTHORITY_H

#include <stddef.h>
#include <stdint.h>

#include "sg_host_law_owner.h"
#include "sg_identity.h"

#define SG_RUNE_SOURCE_AUTHORITY_VERSION UINT32_C(1)

typedef struct sg_rune_source_authority_s sg_rune_source_authority_t;

typedef struct sg_rune_source_entity_record_s
{
	uint32_t source_ordinal;
	int32_t effective_spawnflags;
} sg_rune_source_entity_record_t;

typedef struct sg_rune_source_weapon_law_s
{
	uint8_t weapon_balance_compiled;
	uint8_t weapon_balance_enabled;
	uint8_t rail_match_active;
	uint8_t deathmatch_active;
	uint8_t fast_switch_enabled;
	uint8_t reserved[3];
} sg_rune_source_weapon_law_t;

typedef struct sg_rune_source_snapshot_s
{
	uint32_t version;
	uint32_t reserved;
	uint64_t publication_generation;
	uint64_t publication_generation_complement;
	sg_level_identity_t level_identity;
	sg_host_law_runtime_authority_t host_authority;
	sg_rune_source_weapon_law_t weapon_law;
	uint32_t entity_text_bytes;
	uint32_t entity_record_count;
} sg_rune_source_snapshot_t;

typedef enum sg_rune_source_status_e
{
	SG_RUNE_SOURCE_OK = 0,
	SG_RUNE_SOURCE_INVALID_ARGUMENT,
	SG_RUNE_SOURCE_INVALID_STATE,
	SG_RUNE_SOURCE_ENTITY_TEXT_MISSING,
	SG_RUNE_SOURCE_ENTITY_TEXT_UNTERMINATED,
	SG_RUNE_SOURCE_ALLOCATION_FAILED,
	SG_RUNE_SOURCE_SIZE_OVERFLOW,
	SG_RUNE_SOURCE_RECORD_ORDER,
	SG_RUNE_SOURCE_IDENTITY_UNAVAILABLE,
	SG_RUNE_SOURCE_IDENTITY_MISMATCH,
	SG_RUNE_SOURCE_HOST_UNAVAILABLE,
	SG_RUNE_SOURCE_HOST_DRIFT,
	SG_RUNE_SOURCE_WEAPON_UNAVAILABLE,
	SG_RUNE_SOURCE_WEAPON_DRIFT,
	SG_RUNE_SOURCE_GENERATION_DRIFT,
	SG_RUNE_SOURCE_BUFFER_TOO_SMALL,
	SG_RUNE_SOURCE_STATUS_COUNT
} sg_rune_source_status_t;

sg_rune_source_status_t SG_RuneSourceAuthorityAcquire(
	sg_rune_source_authority_t **authority_out);
sg_rune_source_status_t SG_RuneSourceAuthoritySizes(
	const sg_rune_source_authority_t *authority,
	size_t *entity_text_bytes_out, size_t *entity_record_count_out);
sg_rune_source_status_t SG_RuneSourceAuthorityCopy(
	const sg_rune_source_authority_t *authority,
	sg_rune_source_snapshot_t *snapshot_out,
	char *entity_text_out, size_t entity_text_capacity,
	sg_rune_source_entity_record_t *entity_records_out,
	size_t entity_record_capacity);
/* Read the sealed metadata without copying entity payload.  This is the
 * production admission path for source-owned law inputs. */
sg_rune_source_status_t SG_RuneSourceAuthoritySnapshot(
	const sg_rune_source_authority_t *authority,
	sg_rune_source_snapshot_t *snapshot_out);
sg_rune_source_status_t SG_RuneSourceAuthorityCurrent(
	const sg_rune_source_authority_t *authority);
void SG_RuneSourceAuthorityDestroy(sg_rune_source_authority_t *authority);
const char *SG_RuneSourceAuthorityReason(sg_rune_source_status_t status);

#endif
