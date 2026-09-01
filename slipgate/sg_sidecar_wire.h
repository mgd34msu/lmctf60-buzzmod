/* sg_sidecar_wire.h -- compact-artifact-bound optional sidecars. */
#ifndef SG_SIDECAR_WIRE_H
#define SG_SIDECAR_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_compact_wire.h"

/* The header contains the complete serialized v12 identity.  It never relies
 * on the ABI layout or padding of sg_rune_compact_identity_t. */
#define SG_SIDECAR_FORMAT_VERSION UINT16_C(1)
#define SG_SIDECAR_HEADER_BYTES UINT16_C(304)
#define SG_SIDECAR_HEADER_CRC_OFFSET 300U
#define SG_SIDECAR_MAX_PAYLOAD_BYTES UINT32_C(67108864)
#define SG_SIDECAR_HMN_MAGIC UINT32_C(0x524e4d48) /* "HMNR" */
#define SG_SIDECAR_HML_MAGIC UINT32_C(0x524c4d48) /* "HMLR" */
#define SG_SIDECAR_HME_MAGIC UINT32_C(0x52454d48) /* "HMER" */
#define SG_SIDECAR_DPO_MAGIC UINT32_C(0x524f5044) /* "DPOR" */

typedef enum sg_sidecar_kind_e
{
	SG_SIDECAR_HUMAN = 0,
	SG_SIDECAR_FLAG_LIVE,
	SG_SIDECAR_ESCAPE,
	SG_SIDECAR_DEFENSE,
	SG_SIDECAR_KIND_COUNT
} sg_sidecar_kind_t;

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
	SCD_BAD_HEADER_CRC,
	SCD_NONZERO_RESERVED,
	SCD_RUNE_VERSION_MISMATCH,
	SCD_RUNE_SCHEMA_MISMATCH,
	SCD_RUNE_IMAGE_MISMATCH,
	SCD_RUNE_CHECKSUM_MISMATCH,
	SCD_RUNE_IDENTITY_MISMATCH,
	SCD_BAD_PAYLOAD_SIZE,
	SCD_BAD_FILE_SIZE,
	SCD_BAD_PAYLOAD_CRC,
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
	SCS_RUNE_BINDING,
	SCS_FILE_SIZE,
	SCS_ALLOCATION,
	SCS_PAYLOAD_READ,
	SCS_PAYLOAD_CRC,
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

/* This value is decoded from explicit little-endian bytes.  Its C layout is
 * not a file format. */
typedef struct sg_sidecar_header_s
{
	uint32_t magic;
	uint16_t format_version;
	uint16_t header_bytes;
	uint16_t rune_wire_version;
	uint16_t rune_model_version;
	uint16_t rune_analytic_version;
	uint16_t reserved;
	uint32_t rune_schema_tag;
	uint64_t rune_image_bytes;
	uint32_t rune_checksum;
	sg_rune_compact_identity_t rune_identity;
	uint32_t payload_bytes;
	uint32_t payload_crc32;
	uint32_t header_crc32;
} sg_sidecar_header_t;

const char *SG_SidecarKindName(sg_sidecar_kind_t kind);
const char *SG_SidecarKindExtension(sg_sidecar_kind_t kind);
const char *SG_SidecarDiagnosticName(sg_sidecar_diagnostic_t diagnostic);
const char *SG_SidecarDiagnosticMessage(sg_sidecar_diagnostic_t diagnostic);
const char *SG_SidecarStageName(sg_sidecar_stage_t stage);

/* info must be the result of the canonical compact wire inspector for the
 * exact candidate artifact.  Sidecar callers retain this value through their
 * own RUNE revalidation step immediately before publication. */
sg_sidecar_diagnostic_t SG_SidecarFileSize(sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info, size_t payload_size,
	size_t *size_out);
sg_sidecar_diagnostic_t SG_SidecarInspect(
	const unsigned char *encoded_header, size_t encoded_header_size,
	size_t full_file_size, sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info, sg_sidecar_header_t *header_out);
sg_sidecar_diagnostic_t SG_SidecarEncode(sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info, const unsigned char *payload,
	size_t payload_size, unsigned char *encoded, size_t encoded_capacity,
	size_t *encoded_size_out);
sg_sidecar_diagnostic_t SG_SidecarDecode(const unsigned char *encoded,
	size_t encoded_size, sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info, unsigned char *payload_out,
	size_t payload_capacity, size_t *payload_size_out);

#endif /* SG_SIDECAR_WIRE_H */
