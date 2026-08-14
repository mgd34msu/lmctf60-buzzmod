/* sg_sidecar_wire.h -- explicit little-endian, RUNE v3-bound sidecars. */
#ifndef SG_SIDECAR_WIRE_H
#define SG_SIDECAR_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_wire.h"

#define SG_SIDECAR_V3_FORMAT_VERSION UINT16_C(1)
#define SG_SIDECAR_V3_HEADER_BYTES UINT16_C(48)
#define SG_SIDECAR_V3_HEADER_CRC_OFFSET 44U

/* Explicit little-endian fourcc values: the encoded bytes spell the suffix. */
#define SG_SIDECAR_V3_HMN_MAGIC UINT32_C(0x334e4d48) /* "HMN3" */
#define SG_SIDECAR_V3_HML_MAGIC UINT32_C(0x334c4d48) /* "HML3" */
#define SG_SIDECAR_V3_HME_MAGIC UINT32_C(0x33454d48) /* "HME3" */
#define SG_SIDECAR_V3_DPO_MAGIC UINT32_C(0x334f5044) /* "DPO3" */
#define SG_SIDECAR_V3_DNG_MAGIC UINT32_C(0x33474e44) /* "DNG3" */

#define SG_SIDECAR_V3_DANGER_MIN 0
#define SG_SIDECAR_V3_DANGER_MAX 8000
#define SG_SIDECAR_INDEX_NONE UINT32_MAX

typedef enum sg_sidecar_kind_e
{
	SG_SIDECAR_HUMAN = 0,
	SG_SIDECAR_FLAG_LIVE,
	SG_SIDECAR_ESCAPE,
	SG_SIDECAR_DEFENSE,
	SG_SIDECAR_DANGER,
	SG_SIDECAR_KIND_COUNT
} sg_sidecar_kind_t;

/* Sidecar diagnostics are append-only and intentionally separate from RLW_*.
 * RLW_BAD_SIDECAR remains the coarse cross-module classification; these values
 * retain the actionable format/I/O reason for C, Python, logs, and tests. */
typedef enum sg_sidecar_diagnostic_e
{
	SCD_OK = 0,
	SCD_ABSENT,
	SCD_INVALID_ARGUMENT,
	SCD_PATH_TOO_LONG,
	SCD_IO_ERROR,
	SCD_BAD_MAGIC,
	SCD_UNSUPPORTED_VERSION,
	SCD_BAD_HEADER_SIZE,
	SCD_BAD_RUNE_VERSION,
	SCD_BAD_HEADER_CRC,
	SCD_NONZERO_RESERVED,
	SCD_BAD_SHAPE,
	SCD_BAD_COUNTS,
	SCD_BAD_PAYLOAD_SIZE,
	SCD_BAD_FILE_SIZE,
	SCD_RUNE_PAYLOAD_MISMATCH,
	SCD_ACTION_CONTRACT_MISMATCH,
	SCD_RUNE_HEADER_MISMATCH,
	SCD_BAD_PAYLOAD_CRC,
	SCD_BAD_PAYLOAD_VALUE,
	SCD_ALLOCATION_FAILED,
	SCD_TEMP_EXHAUSTED,
	SCD_STATE_DRIFT,
	SCD_INTERNAL_ERROR,
	SCD_DIAGNOSTIC_COUNT
} sg_sidecar_diagnostic_t;

typedef enum sg_sidecar_stage_e
{
	SCS_ARGUMENT = 0,
	SCS_PATH,
	SCS_OPEN,
	SCS_HEADER_READ,
	SCS_HEADER,
	SCS_HEADER_CRC,
	SCS_SHAPE,
	SCS_RUNE_BINDING,
	SCS_FILE_SIZE,
	SCS_ALLOCATION,
	SCS_PAYLOAD_READ,
	SCS_PAYLOAD_CRC,
	SCS_PAYLOAD_VALUE,
	SCS_WRITE,
	SCS_FLUSH,
	SCS_FILE_SYNC,
	SCS_CLOSE,
	SCS_RECHECK,
	SCS_RENAME,
	SCS_DIRECTORY_SYNC,
	SCS_CLEANUP,
	SCS_DONE,
	SCS_STAGE_COUNT
} sg_sidecar_stage_t;

typedef struct sg_sidecar_v3_header_s
{
	uint32_t magic;
	uint16_t format_version;
	uint16_t header_bytes;
	uint16_t rune_version;
	uint16_t element_bytes;
	uint16_t planes;
	uint16_t reserved;
	uint32_t num_seeds;
	uint32_t num_links;
	uint32_t rune_payload_crc32;
	uint32_t action_contract_crc32;
	uint32_t rune_header_crc32;
	uint32_t payload_bytes;
	uint32_t payload_crc32;
	uint32_t header_crc32;
} sg_sidecar_v3_header_t;

const char *SG_SidecarKindName(sg_sidecar_kind_t kind);
const char *SG_SidecarKindExtension(sg_sidecar_kind_t kind);
const char *SG_SidecarDiagnosticName(sg_sidecar_diagnostic_t diagnostic);
const char *SG_SidecarDiagnosticMessage(sg_sidecar_diagnostic_t diagnostic);
const char *SG_SidecarStageName(sg_sidecar_stage_t stage);
rune_wire_diagnostic_t SG_SidecarDiagnosticWire(
	sg_sidecar_diagnostic_t diagnostic);

/* Exact encoded size for one kind bound to one already-authenticated rune. */
sg_sidecar_diagnostic_t SG_SidecarV3FileSize(sg_sidecar_kind_t kind,
	const sg_rune_v3_header_t *rune, size_t *size_out);

/* Header-first preflight for bounded allocation.  The 48-byte header and the
 * full file size are authenticated before header_out is published. */
sg_sidecar_diagnostic_t SG_SidecarV3Inspect(
	const unsigned char *encoded_header, size_t encoded_header_size,
	size_t full_file_size, sg_sidecar_kind_t kind,
	const sg_rune_v3_header_t *rune,
	sg_sidecar_v3_header_t *header_out);

/* Whole-file helpers.  They allocate nothing and leave outputs unchanged on
 * failure. Seed-indexed kinds require one live-owner mark per rune seed so a
 * tombstone can never acquire a learned value; link-indexed kinds ignore that
 * array. Structs are semantic views only; no native object is a wire image. */
sg_sidecar_diagnostic_t SG_SidecarV3ValidatePayload(
	sg_sidecar_kind_t kind, const sg_rune_v3_header_t *rune,
	const uint8_t *live_seed_marks, size_t live_seed_capacity,
	const unsigned char *payload, size_t payload_size,
	uint32_t *plane_out, uint32_t *index_out);
sg_sidecar_diagnostic_t SG_SidecarV3Encode(sg_sidecar_kind_t kind,
	const sg_rune_v3_header_t *rune, const uint8_t *live_seed_marks,
	size_t live_seed_capacity, const unsigned char *payload, size_t payload_size,
	unsigned char *encoded, size_t encoded_capacity, size_t *encoded_size_out);
sg_sidecar_diagnostic_t SG_SidecarV3Decode(const unsigned char *encoded,
	size_t encoded_size, sg_sidecar_kind_t kind,
	const sg_rune_v3_header_t *rune, const uint8_t *live_seed_marks,
	size_t live_seed_capacity, unsigned char *payload_out,
	size_t payload_capacity, size_t *payload_size_out);

#endif /* SG_SIDECAR_WIRE_H */
