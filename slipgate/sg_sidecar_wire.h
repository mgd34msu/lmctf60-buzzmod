/* sg_sidecar_wire.h -- explicit little-endian, artifact-bound sidecars. */
#ifndef SG_SIDECAR_WIRE_H
#define SG_SIDECAR_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune.h"

/* Sidecars bind the exact RUNE artifact header and payload. */
#define SG_SIDECAR_HEADER_BYTES UINT16_C(48)
#define SG_SIDECAR_HEADER_CRC_OFFSET 44U
#define SG_SIDECAR_HMN_MAGIC UINT32_C(0x524e4d48) /* "HMNR" */
#define SG_SIDECAR_HML_MAGIC UINT32_C(0x524c4d48) /* "HMLR" */
#define SG_SIDECAR_HME_MAGIC UINT32_C(0x52454d48) /* "HMER" */
#define SG_SIDECAR_DPO_MAGIC UINT32_C(0x524f5044) /* "DPOR" */
#define SG_SIDECAR_DNG_MAGIC UINT32_C(0x52474e44) /* "DNGR" */
#define SG_SIDECAR_DANGER_MIN 0
#define SG_SIDECAR_DANGER_MAX 8000

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
	SCD_BAD_HEADER_SIZE,
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

typedef struct sg_sidecar_header_s
{
	uint32_t magic;
	uint16_t header_bytes;
	uint16_t element_bytes;
	uint16_t planes;
	uint32_t num_seeds;
	uint32_t num_links;
	uint32_t rune_payload_crc32;
	uint32_t action_contract_crc32;
	uint32_t rune_header_crc32;
	uint32_t payload_bytes;
	uint32_t payload_crc32;
	uint32_t header_crc32;
} sg_sidecar_header_t;

const char *SG_SidecarKindName(sg_sidecar_kind_t kind);
const char *SG_SidecarKindExtension(sg_sidecar_kind_t kind);
const char *SG_SidecarDiagnosticName(sg_sidecar_diagnostic_t diagnostic);
const char *SG_SidecarDiagnosticMessage(sg_sidecar_diagnostic_t diagnostic);
const char *SG_SidecarStageName(sg_sidecar_stage_t stage);
sg_sidecar_diagnostic_t SG_SidecarFileSize(sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact, size_t *size_out);
sg_sidecar_diagnostic_t SG_SidecarInspect(
	const unsigned char *encoded_header, size_t encoded_header_size,
	size_t full_file_size, sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact, sg_sidecar_header_t *header_out);
sg_sidecar_diagnostic_t SG_SidecarValidatePayload(
	sg_sidecar_kind_t kind, const rune_artifact_t *artifact,
	const uint8_t *live_seed_marks, size_t live_seed_capacity,
	const unsigned char *payload, size_t payload_size,
	uint32_t *plane_out, uint32_t *index_out);
sg_sidecar_diagnostic_t SG_SidecarEncode(sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact, const uint8_t *live_seed_marks,
	size_t live_seed_capacity, const unsigned char *payload,
	size_t payload_size, unsigned char *encoded, size_t encoded_capacity,
	size_t *encoded_size_out);
sg_sidecar_diagnostic_t SG_SidecarDecode(const unsigned char *encoded,
	size_t encoded_size, sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact, const uint8_t *live_seed_marks,
	size_t live_seed_capacity, unsigned char *payload_out,
	size_t payload_capacity, size_t *payload_size_out);

#endif /* SG_SIDECAR_WIRE_H */
