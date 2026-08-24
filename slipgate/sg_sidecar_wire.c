/* sg_sidecar_wire.c -- allocation-free authenticated artifact sidecars. */
#include "q_shared.h"

#include <limits.h>
#include <string.h>

#include "slipgate/sg_crc32.h"
#include "slipgate/sg_sidecar_wire.h"

typedef enum sidecar_axis_e
{
	SIDECAR_AXIS_LINK = 0,
	SIDECAR_AXIS_SEED
} sidecar_axis_t;

typedef struct sidecar_descriptor_s
{
	uint32_t magic;
	uint16_t element_bytes;
	uint16_t planes;
	sidecar_axis_t axis;
	const char *name;
	const char *extension;
} sidecar_descriptor_t;

static const sidecar_descriptor_t sidecar_descriptors[] = {
	{ SG_SIDECAR_HMN_MAGIC, 1, 1, SIDECAR_AXIS_LINK,
	  "human", ".hmn" },
	{ SG_SIDECAR_HML_MAGIC, 1, 1, SIDECAR_AXIS_LINK,
	  "flag-live", ".hml" },
	{ SG_SIDECAR_HME_MAGIC, 1, 1, SIDECAR_AXIS_LINK,
	  "escape", ".hme" },
	{ SG_SIDECAR_DPO_MAGIC, 1, 4, SIDECAR_AXIS_SEED,
	  "defense", ".dpo" },
	{ SG_SIDECAR_DNG_MAGIC, 4, 2, SIDECAR_AXIS_SEED,
	  "danger", ".rune.danger" }
};

typedef struct sidecar_name_s
{
	const char *symbol;
	const char *message;
} sidecar_name_t;

static const sidecar_name_t sidecar_diagnostics[] = {
	{ "SCD_OK", "ok" },
	{ "SCD_ABSENT", "absent" },
	{ "SCD_INVALID_ARGUMENT", "invalid argument" },
	{ "SCD_PATH_TOO_LONG", "path too long" },
	{ "SCD_IO_ERROR", "I/O error" },
	{ "SCD_BAD_MAGIC", "bad magic" },
	{ "SCD_BAD_HEADER_SIZE", "bad header size" },
	{ "SCD_BAD_HEADER_CRC", "bad header CRC" },
	{ "SCD_NONZERO_RESERVED", "nonzero reserved field" },
	{ "SCD_BAD_SHAPE", "bad sidecar shape" },
	{ "SCD_BAD_COUNTS", "rune count mismatch" },
	{ "SCD_BAD_PAYLOAD_SIZE", "bad payload size" },
	{ "SCD_BAD_FILE_SIZE", "bad file size" },
	{ "SCD_RUNE_PAYLOAD_MISMATCH", "rune payload mismatch" },
	{ "SCD_ACTION_CONTRACT_MISMATCH", "action contract mismatch" },
	{ "SCD_RUNE_HEADER_MISMATCH", "rune header mismatch" },
	{ "SCD_BAD_PAYLOAD_CRC", "bad payload CRC" },
	{ "SCD_BAD_PAYLOAD_VALUE", "bad payload value" },
	{ "SCD_ALLOCATION_FAILED", "allocation failed" },
	{ "SCD_TEMP_EXHAUSTED", "temporary names exhausted" },
	{ "SCD_STATE_DRIFT", "bound rune state changed" },
	{ "SCD_INTERNAL_ERROR", "internal error" }
};

static const char *const sidecar_stages[] = {
	"argument", "path", "open", "header-read", "header", "header-crc",
	"shape", "rune-binding", "file-size", "allocation", "payload-read",
	"payload-crc", "payload-value", "write", "flush", "file-sync",
	"close", "recheck", "rename", "directory-sync", "cleanup", "done"
};

_Static_assert(sizeof(sidecar_descriptors) / sizeof(sidecar_descriptors[0]) ==
	SG_SIDECAR_KIND_COUNT, "sidecar descriptor inventory drift");
_Static_assert(sizeof(sidecar_diagnostics) / sizeof(sidecar_diagnostics[0]) ==
	SCD_DIAGNOSTIC_COUNT, "sidecar diagnostic inventory drift");
_Static_assert(sizeof(sidecar_stages) / sizeof(sidecar_stages[0]) ==
	SCS_STAGE_COUNT, "sidecar stage inventory drift");
_Static_assert(CHAR_BIT == 8, "sidecar format requires 8-bit bytes");
_Static_assert(SIZE_MAX >=
	(size_t)RUNE_MAX_LINKS + SG_SIDECAR_HEADER_BYTES,
	"sidecar format requires a size_t that holds its bounded payload");

static const sidecar_descriptor_t *Sidecar_Descriptor(sg_sidecar_kind_t kind)
{
	if (kind < SG_SIDECAR_HUMAN || kind >= SG_SIDECAR_KIND_COUNT)
		return NULL;
	return &sidecar_descriptors[(int)kind];
}

static void Sidecar_PutU16(unsigned char *out, uint16_t value)
{
	out[0] = (unsigned char)(value & UINT16_C(0xff));
	out[1] = (unsigned char)(value >> 8);
}

static void Sidecar_PutU32(unsigned char *out, uint32_t value)
{
	out[0] = (unsigned char)(value & UINT32_C(0xff));
	out[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	out[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	out[3] = (unsigned char)(value >> 24);
}

static uint16_t Sidecar_GetU16(const unsigned char *in)
{
	return (uint16_t)((uint16_t)in[0] | ((uint16_t)in[1] << 8));
}

static uint32_t Sidecar_GetU32(const unsigned char *in)
{
	return (uint32_t)in[0] | ((uint32_t)in[1] << 8) |
	       ((uint32_t)in[2] << 16) | ((uint32_t)in[3] << 24);
}

static sg_sidecar_diagnostic_t Sidecar_HeaderCRC(
	const unsigned char *encoded, size_t encoded_size, uint32_t *crc_out)
{
	unsigned char canonical[SG_SIDECAR_HEADER_BYTES];

	if (!encoded || !crc_out || encoded_size != SG_SIDECAR_HEADER_BYTES)
		return SCD_INVALID_ARGUMENT;
	memcpy(canonical, encoded, sizeof(canonical));
	memset(canonical + SG_SIDECAR_HEADER_CRC_OFFSET, 0, 4);
	if (!SG_CRC32Buffer(canonical, sizeof(canonical), crc_out))
		return SCD_INTERNAL_ERROR;
	return SCD_OK;
}

static void Sidecar_EncodeHeaderBytes(const sg_sidecar_header_t *header,
	unsigned char out[SG_SIDECAR_HEADER_BYTES])
{
	memset(out, 0, SG_SIDECAR_HEADER_BYTES);
	Sidecar_PutU32(out + 0, header->magic);
	Sidecar_PutU16(out + 4, 0U);
	Sidecar_PutU16(out + 6, header->header_bytes);
	Sidecar_PutU16(out + 8, 0U);
	Sidecar_PutU16(out + 10, header->element_bytes);
	Sidecar_PutU16(out + 12, header->planes);
	Sidecar_PutU16(out + 14, 0U);
	Sidecar_PutU32(out + 16, header->num_seeds);
	Sidecar_PutU32(out + 20, header->num_links);
	Sidecar_PutU32(out + 24, header->rune_payload_crc32);
	Sidecar_PutU32(out + 28, header->action_contract_crc32);
	Sidecar_PutU32(out + 32, header->rune_header_crc32);
	Sidecar_PutU32(out + 36, header->payload_bytes);
	Sidecar_PutU32(out + 40, header->payload_crc32);
	Sidecar_PutU32(out + 44, header->header_crc32);
}

static void Sidecar_DecodeHeaderBytes(const unsigned char *in,
	sg_sidecar_header_t *header)
{
	memset(header, 0, sizeof(*header));
	header->magic = Sidecar_GetU32(in + 0);
	header->header_bytes = Sidecar_GetU16(in + 6);
	header->element_bytes = Sidecar_GetU16(in + 10);
	header->planes = Sidecar_GetU16(in + 12);
	header->num_seeds = Sidecar_GetU32(in + 16);
	header->num_links = Sidecar_GetU32(in + 20);
	header->rune_payload_crc32 = Sidecar_GetU32(in + 24);
	header->action_contract_crc32 = Sidecar_GetU32(in + 28);
	header->rune_header_crc32 = Sidecar_GetU32(in + 32);
	header->payload_bytes = Sidecar_GetU32(in + 36);
	header->payload_crc32 = Sidecar_GetU32(in + 40);
	header->header_crc32 = Sidecar_GetU32(in + 44);
}

const char *SG_SidecarKindName(sg_sidecar_kind_t kind)
{
	const sidecar_descriptor_t *descriptor = Sidecar_Descriptor(kind);

	return descriptor ? descriptor->name : "unknown";
}

const char *SG_SidecarKindExtension(sg_sidecar_kind_t kind)
{
	const sidecar_descriptor_t *descriptor = Sidecar_Descriptor(kind);

	return descriptor ? descriptor->extension : NULL;
}

const char *SG_SidecarDiagnosticName(sg_sidecar_diagnostic_t diagnostic)
{
	if (diagnostic < SCD_OK || diagnostic >= SCD_DIAGNOSTIC_COUNT)
		return "SCD_UNKNOWN";
	return sidecar_diagnostics[(int)diagnostic].symbol;
}

const char *SG_SidecarDiagnosticMessage(sg_sidecar_diagnostic_t diagnostic)
{
	if (diagnostic < SCD_OK || diagnostic >= SCD_DIAGNOSTIC_COUNT)
		return "unknown sidecar diagnostic";
	return sidecar_diagnostics[(int)diagnostic].message;
}

const char *SG_SidecarStageName(sg_sidecar_stage_t stage)
{
	if (stage < SCS_ARGUMENT || stage >= SCS_STAGE_COUNT)
		return "unknown";
	return sidecar_stages[(int)stage];
}

static uint32_t Sidecar_Magic(sg_sidecar_kind_t kind)
{
	switch (kind)
	{
	case SG_SIDECAR_HUMAN: return SG_SIDECAR_HMN_MAGIC;
	case SG_SIDECAR_FLAG_LIVE: return SG_SIDECAR_HML_MAGIC;
	case SG_SIDECAR_ESCAPE: return SG_SIDECAR_HME_MAGIC;
	case SG_SIDECAR_DEFENSE: return SG_SIDECAR_DPO_MAGIC;
	case SG_SIDECAR_DANGER: return SG_SIDECAR_DNG_MAGIC;
	default: return 0U;
	}
}

static int Sidecar_ArtifactValid(const rune_artifact_t *artifact)
{
	return artifact && artifact->magic == RUNE_ARTIFACT_MAGIC &&
		SG_RuneRouteContractValid(artifact->route_contract) &&
		artifact->num_seeds > 0U &&
		artifact->num_seeds <= RUNE_MAX_SEEDS &&
		artifact->num_links <= RUNE_MAX_LINKS &&
		artifact->num_mechanism_nodes <= RUNE_MAX_MECHANISM_NODES &&
		artifact->num_mechanism_edges <= RUNE_MAX_MECHANISM_EDGES &&
		artifact->num_inventory_edges <= artifact->num_mechanism_edges &&
		artifact->num_mechanism_plans <= RUNE_MAX_MECHANISM_PLANS &&
		artifact->string_bytes > 0U &&
		artifact->string_bytes <= RUNE_MAX_MECHANISM_STRING_BYTES &&
		artifact->identity.map_name[0] != '\0' &&
		memchr(artifact->identity.map_name, '\0',
			sizeof(artifact->identity.map_name)) != NULL;
}

static sg_sidecar_diagnostic_t Sidecar_PayloadBytes(
	const sidecar_descriptor_t *descriptor,
	const rune_artifact_t *artifact, uint32_t *payload_bytes_out)
{
	uint32_t count;
	uint64_t bytes;

	if (!descriptor || !Sidecar_ArtifactValid(artifact) ||
	    !payload_bytes_out)
		return SCD_INVALID_ARGUMENT;
	count = descriptor->axis == SIDECAR_AXIS_LINK
		? artifact->num_links : artifact->num_seeds;
	bytes = (uint64_t)count * descriptor->element_bytes *
		descriptor->planes;
	if (bytes > UINT32_MAX || bytes > SIZE_MAX)
		return SCD_BAD_PAYLOAD_SIZE;
	*payload_bytes_out = (uint32_t)bytes;
	return SCD_OK;
}

sg_sidecar_diagnostic_t SG_SidecarFileSize(sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact, size_t *size_out)
{
	const sidecar_descriptor_t *descriptor = Sidecar_Descriptor(kind);
	uint32_t payload_bytes;
	sg_sidecar_diagnostic_t diagnostic;

	if (!size_out)
		return SCD_INVALID_ARGUMENT;
	diagnostic = Sidecar_PayloadBytes(descriptor, artifact,
		&payload_bytes);
	if (diagnostic != SCD_OK)
		return diagnostic;
	*size_out = SG_SIDECAR_HEADER_BYTES + (size_t)payload_bytes;
	return SCD_OK;
}

sg_sidecar_diagnostic_t SG_SidecarInspect(
	const unsigned char *encoded_header, size_t encoded_header_size,
	size_t full_file_size, sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact, sg_sidecar_header_t *header_out)
{
	const sidecar_descriptor_t *descriptor = Sidecar_Descriptor(kind);
	sg_sidecar_header_t header;
	uint32_t expected_payload;
	uint32_t computed_crc;
	size_t expected_file_size;
	sg_sidecar_diagnostic_t diagnostic;

	if (!encoded_header || !descriptor || !Sidecar_ArtifactValid(artifact) ||
	    !header_out)
		return SCD_INVALID_ARGUMENT;
	if (encoded_header_size != SG_SIDECAR_HEADER_BYTES)
		return SCD_BAD_HEADER_SIZE;
	Sidecar_DecodeHeaderBytes(encoded_header, &header);
	if (header.magic != Sidecar_Magic(kind))
		return SCD_BAD_MAGIC;
	if (header.header_bytes != SG_SIDECAR_HEADER_BYTES)
		return SCD_BAD_HEADER_SIZE;
	diagnostic = Sidecar_HeaderCRC(encoded_header, encoded_header_size,
		&computed_crc);
	if (diagnostic != SCD_OK)
		return diagnostic;
	if (computed_crc != header.header_crc32)
		return SCD_BAD_HEADER_CRC;
	if (Sidecar_GetU16(encoded_header + 4) != 0U ||
	    Sidecar_GetU16(encoded_header + 8) != 0U ||
	    Sidecar_GetU16(encoded_header + 14) != 0U)
		return SCD_NONZERO_RESERVED;
	if (header.element_bytes != descriptor->element_bytes ||
	    header.planes != descriptor->planes)
		return SCD_BAD_SHAPE;
	diagnostic = Sidecar_PayloadBytes(descriptor, artifact,
		&expected_payload);
	if (diagnostic != SCD_OK)
		return diagnostic;
	if (header.num_seeds != artifact->num_seeds ||
	    header.num_links != artifact->num_links)
		return SCD_BAD_COUNTS;
	if (header.payload_bytes != expected_payload)
		return SCD_BAD_PAYLOAD_SIZE;
	expected_file_size = SG_SIDECAR_HEADER_BYTES +
		(size_t)expected_payload;
	if (full_file_size != expected_file_size)
		return SCD_BAD_FILE_SIZE;
	if (header.action_contract_crc32 != artifact->action_contract_crc32)
		return SCD_ACTION_CONTRACT_MISMATCH;
	if (header.rune_payload_crc32 != artifact->payload_crc32)
		return SCD_RUNE_PAYLOAD_MISMATCH;
	if (header.rune_header_crc32 != artifact->header_crc32)
		return SCD_RUNE_HEADER_MISMATCH;
	*header_out = header;
	return SCD_OK;
}

sg_sidecar_diagnostic_t SG_SidecarValidatePayload(
	sg_sidecar_kind_t kind, const rune_artifact_t *artifact,
	const uint8_t *live_seed_marks, size_t live_seed_capacity,
	const unsigned char *payload, size_t payload_size,
	uint32_t *plane_out, uint32_t *index_out)
{
	const sidecar_descriptor_t *descriptor = Sidecar_Descriptor(kind);
	uint32_t expected_payload;
	uint32_t plane;
	uint32_t seed;
	sg_sidecar_diagnostic_t diagnostic;

	if (plane_out)
		*plane_out = SG_SIDECAR_INDEX_NONE;
	if (index_out)
		*index_out = SG_SIDECAR_INDEX_NONE;
	if (!descriptor || !Sidecar_ArtifactValid(artifact) ||
	    (payload_size > 0U && !payload))
		return SCD_INVALID_ARGUMENT;
	diagnostic = Sidecar_PayloadBytes(descriptor, artifact,
		&expected_payload);
	if (diagnostic != SCD_OK)
		return diagnostic;
	if (payload_size != expected_payload)
		return SCD_BAD_PAYLOAD_SIZE;
	if (descriptor->axis != SIDECAR_AXIS_SEED)
		return SCD_OK;
	if (!live_seed_marks || live_seed_capacity < artifact->num_seeds)
		return SCD_INVALID_ARGUMENT;
	for (seed = 0U; seed < artifact->num_seeds; seed++)
		if (live_seed_marks[seed] > 1U)
			return SCD_INVALID_ARGUMENT;
	for (plane = 0U; plane < descriptor->planes; plane++)
	{
		for (seed = 0U; seed < artifact->num_seeds; seed++)
		{
			size_t offset = ((size_t)plane * artifact->num_seeds + seed) *
				descriptor->element_bytes;
			uint32_t value = descriptor->element_bytes == 1U
				? payload[offset] : Sidecar_GetU32(payload + offset);

			if ((kind == SG_SIDECAR_DANGER &&
			     value > (uint32_t)SG_SIDECAR_DANGER_MAX) ||
			    (!live_seed_marks[seed] && value != 0U))
			{
				if (plane_out)
					*plane_out = plane;
				if (index_out)
					*index_out = seed;
				return SCD_BAD_PAYLOAD_VALUE;
			}
		}
	}
	return SCD_OK;
}

sg_sidecar_diagnostic_t SG_SidecarEncode(sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact, const uint8_t *live_seed_marks,
	size_t live_seed_capacity, const unsigned char *payload,
	size_t payload_size, unsigned char *encoded, size_t encoded_capacity,
	size_t *encoded_size_out)
{
	const sidecar_descriptor_t *descriptor = Sidecar_Descriptor(kind);
	sg_sidecar_header_t header;
	unsigned char raw_header[SG_SIDECAR_HEADER_BYTES];
	uint32_t expected_payload;
	uint32_t payload_crc;
	uint32_t header_crc;
	size_t file_size;
	sg_sidecar_diagnostic_t diagnostic;

	if (!descriptor || !Sidecar_ArtifactValid(artifact) || !encoded ||
	    !encoded_size_out)
		return SCD_INVALID_ARGUMENT;
	diagnostic = Sidecar_PayloadBytes(descriptor, artifact,
		&expected_payload);
	if (diagnostic != SCD_OK)
		return diagnostic;
	if (payload_size != (size_t)expected_payload)
		return SCD_BAD_PAYLOAD_SIZE;
	diagnostic = SG_SidecarValidatePayload(kind, artifact,
		live_seed_marks, live_seed_capacity, payload, payload_size,
		NULL, NULL);
	if (diagnostic != SCD_OK)
		return diagnostic;
	file_size = SG_SIDECAR_HEADER_BYTES + payload_size;
	if (encoded_capacity < file_size)
		return SCD_BAD_FILE_SIZE;
	if (!SG_CRC32Buffer(payload, payload_size, &payload_crc))
		return SCD_INTERNAL_ERROR;
	memset(&header, 0, sizeof(header));
	header.magic = Sidecar_Magic(kind);
	header.header_bytes = SG_SIDECAR_HEADER_BYTES;
	header.element_bytes = descriptor->element_bytes;
	header.planes = descriptor->planes;
	header.num_seeds = artifact->num_seeds;
	header.num_links = artifact->num_links;
	header.rune_payload_crc32 = artifact->payload_crc32;
	header.action_contract_crc32 = artifact->action_contract_crc32;
	header.rune_header_crc32 = artifact->header_crc32;
	header.payload_bytes = expected_payload;
	header.payload_crc32 = payload_crc;
	Sidecar_EncodeHeaderBytes(&header, raw_header);
	diagnostic = Sidecar_HeaderCRC(raw_header, sizeof(raw_header),
		&header_crc);
	if (diagnostic != SCD_OK)
		return diagnostic;
	header.header_crc32 = header_crc;
	Sidecar_EncodeHeaderBytes(&header, raw_header);
	memcpy(encoded, raw_header, sizeof(raw_header));
	if (payload_size != 0U)
		memmove(encoded + SG_SIDECAR_HEADER_BYTES, payload, payload_size);
	*encoded_size_out = file_size;
	return SCD_OK;
}

sg_sidecar_diagnostic_t SG_SidecarDecode(const unsigned char *encoded,
	size_t encoded_size, sg_sidecar_kind_t kind,
	const rune_artifact_t *artifact, const uint8_t *live_seed_marks,
	size_t live_seed_capacity, unsigned char *payload_out,
	size_t payload_capacity, size_t *payload_size_out)
{
	sg_sidecar_header_t header;
	const unsigned char *payload;
	uint32_t payload_crc;
	sg_sidecar_diagnostic_t diagnostic;

	if (!encoded || !Sidecar_ArtifactValid(artifact) || !payload_out ||
	    !payload_size_out)
		return SCD_INVALID_ARGUMENT;
	if (encoded_size < SG_SIDECAR_HEADER_BYTES)
		return SCD_BAD_FILE_SIZE;
	diagnostic = SG_SidecarInspect(encoded, SG_SIDECAR_HEADER_BYTES,
		encoded_size, kind, artifact, &header);
	if (diagnostic != SCD_OK)
		return diagnostic;
	payload = encoded + SG_SIDECAR_HEADER_BYTES;
	if (!SG_CRC32Buffer(payload, header.payload_bytes, &payload_crc))
		return SCD_INTERNAL_ERROR;
	if (payload_crc != header.payload_crc32)
		return SCD_BAD_PAYLOAD_CRC;
	diagnostic = SG_SidecarValidatePayload(kind, artifact,
		live_seed_marks, live_seed_capacity, payload, header.payload_bytes,
		NULL, NULL);
	if (diagnostic != SCD_OK)
		return diagnostic;
	if (payload_capacity < header.payload_bytes)
		return SCD_BAD_PAYLOAD_SIZE;
	memmove(payload_out, payload, header.payload_bytes);
	*payload_size_out = header.payload_bytes;
	return SCD_OK;
}
