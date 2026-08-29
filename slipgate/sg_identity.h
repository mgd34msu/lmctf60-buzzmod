/* sg_identity.h -- immutable, fail-closed identity for one spawned level. */
#ifndef SG_IDENTITY_H
#define SG_IDENTITY_H

#include <stdint.h>

#define SG_LEVEL_IDENTITY_MAPNAME_BYTES 64U
#define SG_LEVEL_BSP_SHA256_BYTES       32U
#define SG_LEVEL_ENTITY_TEXT_LIMIT      0x40000U
#define SG_HOST_PHYSICS_EPOCH           UINT32_C(1)

typedef struct sg_level_identity_s
{
	uint32_t bsp_checksum;
	uint32_t entity_crc32;
	uint32_t host_physics_id;
	uint64_t bsp_bytes;
	uint8_t bsp_sha256[SG_LEVEL_BSP_SHA256_BYTES];
	char mapname[SG_LEVEL_IDENTITY_MAPNAME_BYTES];
} sg_level_identity_t;

/* Engine bridge contract (rune-host-identity): CM_LoadMap publishes the exact
 * selected BSP buffer before SpawnEntities as protected cvars.  The digest is
 * 64 lowercase hexadecimal characters in sv_rune_bsp_sha256; its nonzero byte
 * length is canonical unsigned decimal in sv_rune_bsp_bytes.  The game DLL
 * never reconstructs engine search-path or package selection. */

/* Stable, append-only failure identities. */
typedef enum sg_identity_status_e
{
	SG_IDENTITY_OK = 0,
	SG_IDENTITY_UNAVAILABLE,
	SG_IDENTITY_INVALID_ARGUMENT,
	SG_IDENTITY_INVALID_MAPNAME,
	SG_IDENTITY_INVALID_TRANSITION,
	SG_IDENTITY_ALREADY_COMMITTED,
	SG_IDENTITY_NOT_COMMITTED,
	SG_IDENTITY_HOST_CVAR_UNAVAILABLE,
	SG_IDENTITY_MAPCHECKSUM_MISSING,
	SG_IDENTITY_MAPCHECKSUM_UNPROTECTED,
	SG_IDENTITY_MAPCHECKSUM_NONCANONICAL,
	SG_IDENTITY_PHYSICS_ID_MISSING,
	SG_IDENTITY_PHYSICS_ID_UNPROTECTED,
	SG_IDENTITY_PHYSICS_ID_NONCANONICAL,
	SG_IDENTITY_PHYSICS_ID_UNSUPPORTED,
	SG_IDENTITY_ENTITY_TEXT_MISSING,
	SG_IDENTITY_ENTITY_TEXT_UNTERMINATED,
	SG_IDENTITY_MAPNAME_MISMATCH,
	SG_IDENTITY_BSP_CHECKSUM_MISMATCH,
	SG_IDENTITY_ENTITY_CRC_MISMATCH,
	SG_IDENTITY_PHYSICS_ID_MISMATCH,
	SG_IDENTITY_CRC_FAILURE,
	SG_IDENTITY_BSP_SHA256_MISSING,
	SG_IDENTITY_BSP_SHA256_UNPROTECTED,
	SG_IDENTITY_BSP_SHA256_NONCANONICAL,
	SG_IDENTITY_BSP_BYTES_MISSING,
	SG_IDENTITY_BSP_BYTES_UNPROTECTED,
	SG_IDENTITY_BSP_BYTES_NONCANONICAL,
	SG_IDENTITY_STATUS_COUNT
} sg_identity_status_t;

/* Spawn lifecycle authority.  Begin invalidates first as a fail-closed backstop;
 * the host also explicitly resets as the first operation of every transition. */
void SG_LevelIdentityReset(void);
sg_identity_status_t SG_LevelIdentityBegin(const char *mapname);
sg_identity_status_t SG_LevelIdentityCaptureEntities(const char *mapname,
	const char *text);
sg_identity_status_t SG_LevelIdentityCommit(const char *mapname);

/* Consumer boundary.  Snapshot returns a copy, never mutable authority.
 * Every map argument uses the canonical, case-preserving artifact grammar. */
sg_identity_status_t SG_LevelIdentitySnapshot(const char *expected_mapname,
	sg_level_identity_t *out);
sg_identity_status_t SG_LevelIdentityMatch(const char *expected_mapname,
	uint32_t bsp_checksum, uint32_t entity_crc32, uint32_t physics_id);
const char *SG_LevelIdentityReason(sg_identity_status_t status);

#endif
