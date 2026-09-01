/* sg_sidecar_wire.c -- allocation-free compact-artifact-bound sidecars. */
#include "q_shared.h"

#include <limits.h>
#include <string.h>

#include "slipgate/sg_crc32.h"
#include "slipgate/sg_sidecar_wire.h"

typedef struct sidecar_descriptor_s
{
	uint32_t magic;
	const char *name;
	const char *extension;
} sidecar_descriptor_t;

typedef struct sidecar_name_s
{
	const char *symbol;
	const char *message;
} sidecar_name_t;

static const sidecar_descriptor_t sidecar_descriptors[] = {
	{ SG_SIDECAR_HMN_MAGIC, "human", ".hmn" },
	{ SG_SIDECAR_HML_MAGIC, "flag-live", ".hml" },
	{ SG_SIDECAR_HME_MAGIC, "escape", ".hme" },
	{ SG_SIDECAR_DPO_MAGIC, "defense", ".dpo" }
};

static const sidecar_name_t sidecar_diagnostics[] = {
	{ "SCD_OK", "ok" },
	{ "SCD_ABSENT", "absent" },
	{ "SCD_INVALID_ARGUMENT", "invalid argument" },
	{ "SCD_PATH_TOO_LONG", "path too long" },
	{ "SCD_IO_ERROR", "I/O error" },
	{ "SCD_BAD_MAGIC", "bad magic" },
	{ "SCD_UNSUPPORTED_VERSION", "unsupported sidecar version" },
	{ "SCD_BAD_HEADER_SIZE", "bad header size" },
	{ "SCD_BAD_HEADER_CRC", "bad header CRC" },
	{ "SCD_NONZERO_RESERVED", "nonzero reserved field" },
	{ "SCD_RUNE_VERSION_MISMATCH", "RUNE version mismatch" },
	{ "SCD_RUNE_SCHEMA_MISMATCH", "RUNE schema mismatch" },
	{ "SCD_RUNE_IMAGE_MISMATCH", "RUNE image size mismatch" },
	{ "SCD_RUNE_CHECKSUM_MISMATCH", "RUNE checksum mismatch" },
	{ "SCD_RUNE_IDENTITY_MISMATCH", "RUNE identity mismatch" },
	{ "SCD_BAD_PAYLOAD_SIZE", "bad payload size" },
	{ "SCD_BAD_FILE_SIZE", "bad file size" },
	{ "SCD_BAD_PAYLOAD_CRC", "bad payload CRC" },
	{ "SCD_ALLOCATION_FAILED", "allocation failed" },
	{ "SCD_TEMP_EXHAUSTED", "temporary names exhausted" },
	{ "SCD_STATE_DRIFT", "bound RUNE state changed" },
	{ "SCD_INTERNAL_ERROR", "internal error" }
};

static const char *const sidecar_stages[] = {
	"argument", "path", "open", "header-read", "header", "header-crc",
	"rune-binding", "file-size", "allocation", "payload-read",
	"payload-crc", "write", "flush", "file-sync", "close", "recheck",
	"rename", "directory-sync", "cleanup", "done"
};

_Static_assert(sizeof(sidecar_descriptors) / sizeof(sidecar_descriptors[0]) ==
	SG_SIDECAR_KIND_COUNT, "sidecar descriptor inventory drift");
_Static_assert(sizeof(sidecar_diagnostics) / sizeof(sidecar_diagnostics[0]) ==
	SCD_DIAGNOSTIC_COUNT, "sidecar diagnostic inventory drift");
_Static_assert(sizeof(sidecar_stages) / sizeof(sidecar_stages[0]) ==
	SCS_STAGE_COUNT, "sidecar stage inventory drift");
_Static_assert(CHAR_BIT == 8, "sidecar format requires 8-bit bytes");
_Static_assert(SG_SIDECAR_HEADER_BYTES == 304U,
	"sidecar byte layout drift");

static const sidecar_descriptor_t *Sidecar_Descriptor(sg_sidecar_kind_t kind)
{
	if (kind < SG_SIDECAR_HUMAN || kind >= SG_SIDECAR_KIND_COUNT)
		return NULL;
	return &sidecar_descriptors[(uint32_t)kind];
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

static void Sidecar_PutU64(unsigned char *out, uint64_t value)
{
	uint32_t index;

	for (index = 0U; index < 8U; index++)
		out[index] = (unsigned char)(value >> (index * 8U));
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

static uint64_t Sidecar_GetU64(const unsigned char *in)
{
	uint64_t value = 0U;
	uint32_t index;

	for (index = 0U; index < 8U; index++)
		value |= (uint64_t)in[index] << (index * 8U);
	return value;
}

static void Sidecar_EncodeIdentity(const sg_rune_compact_identity_t *identity,
	unsigned char *out)
{
	uint32_t index;
	size_t offset = 0U;
	const uint64_t ids[] = {
		identity->entity_semantics_id, identity->physics_abi_id,
		identity->collision_law_id, identity->pmove_law_id,
		identity->gravity_law_id, identity->hook_law_id,
		identity->mechanism_law_id, identity->weapon_law_id,
		identity->construction_id, identity->schema_id,
		identity->producer_identity
	};
	const uint32_t counts[] = {
		identity->source_counts.model_count, identity->source_counts.leaf_count,
		identity->source_counts.area_count, identity->source_counts.plane_count,
		identity->source_counts.brush_count,
		identity->source_counts.brush_side_count,
		identity->source_counts.entity_count
	};
	const uint32_t standing[] = {
		(uint32_t)identity->standing_hull.mins.value[0],
		(uint32_t)identity->standing_hull.mins.value[1],
		(uint32_t)identity->standing_hull.mins.value[2],
		(uint32_t)identity->standing_hull.maxs.value[0],
		(uint32_t)identity->standing_hull.maxs.value[1],
		(uint32_t)identity->standing_hull.maxs.value[2]
	};
	const uint32_t crouching[] = {
		(uint32_t)identity->crouching_hull.mins.value[0],
		(uint32_t)identity->crouching_hull.mins.value[1],
		(uint32_t)identity->crouching_hull.mins.value[2],
		(uint32_t)identity->crouching_hull.maxs.value[0],
		(uint32_t)identity->crouching_hull.maxs.value[1],
		(uint32_t)identity->crouching_hull.maxs.value[2]
	};
	const uint32_t physics[] = {
		identity->physics.gravity_bits,
		identity->physics.ground_acceleration_bits,
		identity->physics.air_acceleration_bits,
		identity->physics.water_acceleration_bits,
		identity->physics.hook_acceleration_bits,
		identity->physics.external_acceleration_bits,
		identity->physics.water_drag_bits, identity->physics.max_velocity_bits,
		identity->physics.frame_ms, identity->physics.substep_ms
	};

	memcpy(out + offset, identity->bsp_sha256, 32U);
	offset += 32U;
	Sidecar_PutU64(out + offset, identity->bsp_bytes);
	offset += 8U;
	Sidecar_PutU32(out + offset, identity->bsp_checksum);
	offset += 4U;
	Sidecar_PutU32(out + offset, identity->entity_crc32);
	offset += 4U;
	for (index = 0U; index < 11U; index++)
	{
		Sidecar_PutU64(out + offset, ids[index]);
		offset += 8U;
	}
	for (index = 0U; index < 7U; index++)
	{
		Sidecar_PutU32(out + offset, counts[index]);
		offset += 4U;
	}
	for (index = 0U; index < 6U; index++)
	{
		Sidecar_PutU32(out + offset, standing[index]);
		offset += 4U;
	}
	for (index = 0U; index < 6U; index++)
	{
		Sidecar_PutU32(out + offset, crouching[index]);
		offset += 4U;
	}
	for (index = 0U; index < 10U; index++)
	{
		Sidecar_PutU32(out + offset, physics[index]);
		offset += 4U;
	}
	Sidecar_PutU64(out + offset, identity->weapon_profile_catalog_id);
}

static void Sidecar_DecodeIdentity(const unsigned char *in,
	sg_rune_compact_identity_t *identity)
{
	uint32_t index;
	size_t offset = 0U;
	uint64_t *const ids[] = {
		&identity->entity_semantics_id, &identity->physics_abi_id,
		&identity->collision_law_id, &identity->pmove_law_id,
		&identity->gravity_law_id, &identity->hook_law_id,
		&identity->mechanism_law_id, &identity->weapon_law_id,
		&identity->construction_id, &identity->schema_id,
		&identity->producer_identity
	};
	uint32_t *const counts[] = {
		&identity->source_counts.model_count, &identity->source_counts.leaf_count,
		&identity->source_counts.area_count, &identity->source_counts.plane_count,
		&identity->source_counts.brush_count,
		&identity->source_counts.brush_side_count,
		&identity->source_counts.entity_count
	};
	int32_t *const standing[] = {
		&identity->standing_hull.mins.value[0],
		&identity->standing_hull.mins.value[1],
		&identity->standing_hull.mins.value[2],
		&identity->standing_hull.maxs.value[0],
		&identity->standing_hull.maxs.value[1],
		&identity->standing_hull.maxs.value[2]
	};
	int32_t *const crouching[] = {
		&identity->crouching_hull.mins.value[0],
		&identity->crouching_hull.mins.value[1],
		&identity->crouching_hull.mins.value[2],
		&identity->crouching_hull.maxs.value[0],
		&identity->crouching_hull.maxs.value[1],
		&identity->crouching_hull.maxs.value[2]
	};
	uint32_t *const physics[] = {
		&identity->physics.gravity_bits,
		&identity->physics.ground_acceleration_bits,
		&identity->physics.air_acceleration_bits,
		&identity->physics.water_acceleration_bits,
		&identity->physics.hook_acceleration_bits,
		&identity->physics.external_acceleration_bits,
		&identity->physics.water_drag_bits, &identity->physics.max_velocity_bits,
		&identity->physics.frame_ms, &identity->physics.substep_ms
	};

	memset(identity, 0, sizeof(*identity));
	memcpy(identity->bsp_sha256, in + offset, 32U);
	offset += 32U;
	identity->bsp_bytes = Sidecar_GetU64(in + offset);
	offset += 8U;
	identity->bsp_checksum = Sidecar_GetU32(in + offset);
	offset += 4U;
	identity->entity_crc32 = Sidecar_GetU32(in + offset);
	offset += 4U;
	for (index = 0U; index < 11U; index++)
	{
		*ids[index] = Sidecar_GetU64(in + offset);
		offset += 8U;
	}
	for (index = 0U; index < 7U; index++)
	{
		*counts[index] = Sidecar_GetU32(in + offset);
		offset += 4U;
	}
	for (index = 0U; index < 6U; index++)
	{
		*standing[index] = (int32_t)Sidecar_GetU32(in + offset);
		offset += 4U;
	}
	for (index = 0U; index < 6U; index++)
	{
		*crouching[index] = (int32_t)Sidecar_GetU32(in + offset);
		offset += 4U;
	}
	for (index = 0U; index < 10U; index++)
	{
		*physics[index] = Sidecar_GetU32(in + offset);
		offset += 4U;
	}
	identity->weapon_profile_catalog_id = Sidecar_GetU64(in + offset);
}

static int Sidecar_IdentityMatches(const sg_rune_compact_identity_t *left,
	const sg_rune_compact_identity_t *right)
{
	unsigned char left_bytes[260];
	unsigned char right_bytes[260];

	if (left == NULL || right == NULL)
		return 0;
	Sidecar_EncodeIdentity(left, left_bytes);
	Sidecar_EncodeIdentity(right, right_bytes);
	return memcmp(left_bytes, right_bytes, sizeof(left_bytes)) == 0;
}

static int Sidecar_ArtifactInfoValid(const sg_rune_compact_wire_info_t *info)
{
	return info != NULL &&
		info->wire_version == SG_RUNE_COMPACT_WIRE_VERSION &&
		info->model_version == SG_RUNE_COMPACT_MODEL_VERSION &&
		info->analytic_version == SG_RUNE_COMPACT_ANALYTIC_VERSION &&
		info->schema_tag == SG_RUNE_COMPACT_MODEL_SCHEMA_TAG &&
		info->image_bytes > 0U &&
		info->image_bytes <= SG_RUNE_COMPACT_WIRE_MAX_IMAGE_BYTES;
}

static sg_sidecar_diagnostic_t Sidecar_HeaderCRC(
	const unsigned char *encoded, size_t encoded_size, uint32_t *crc_out)
{
	unsigned char canonical[SG_SIDECAR_HEADER_BYTES];

	if (!encoded || !crc_out || encoded_size != SG_SIDECAR_HEADER_BYTES)
		return SCD_INVALID_ARGUMENT;
	memcpy(canonical, encoded, sizeof(canonical));
	memset(canonical + SG_SIDECAR_HEADER_CRC_OFFSET, 0, 4U);
	if (!SG_CRC32Buffer(canonical, sizeof(canonical), crc_out))
		return SCD_INTERNAL_ERROR;
	return SCD_OK;
}

static void Sidecar_EncodeHeaderBytes(const sg_sidecar_header_t *header,
	unsigned char out[SG_SIDECAR_HEADER_BYTES])
{
	memset(out, 0, SG_SIDECAR_HEADER_BYTES);
	Sidecar_PutU32(out + 0U, header->magic);
	Sidecar_PutU16(out + 4U, header->format_version);
	Sidecar_PutU16(out + 6U, header->header_bytes);
	Sidecar_PutU16(out + 8U, header->rune_wire_version);
	Sidecar_PutU16(out + 10U, header->rune_model_version);
	Sidecar_PutU16(out + 12U, header->rune_analytic_version);
	Sidecar_PutU16(out + 14U, header->reserved);
	Sidecar_PutU32(out + 16U, header->rune_schema_tag);
	Sidecar_PutU64(out + 20U, header->rune_image_bytes);
	Sidecar_PutU32(out + 28U, header->rune_checksum);
	Sidecar_EncodeIdentity(&header->rune_identity, out + 32U);
	Sidecar_PutU32(out + 292U, header->payload_bytes);
	Sidecar_PutU32(out + 296U, header->payload_crc32);
	Sidecar_PutU32(out + 300U, header->header_crc32);
}

static void Sidecar_DecodeHeaderBytes(const unsigned char *in,
	sg_sidecar_header_t *header)
{
	memset(header, 0, sizeof(*header));
	header->magic = Sidecar_GetU32(in + 0U);
	header->format_version = Sidecar_GetU16(in + 4U);
	header->header_bytes = Sidecar_GetU16(in + 6U);
	header->rune_wire_version = Sidecar_GetU16(in + 8U);
	header->rune_model_version = Sidecar_GetU16(in + 10U);
	header->rune_analytic_version = Sidecar_GetU16(in + 12U);
	header->reserved = Sidecar_GetU16(in + 14U);
	header->rune_schema_tag = Sidecar_GetU32(in + 16U);
	header->rune_image_bytes = Sidecar_GetU64(in + 20U);
	header->rune_checksum = Sidecar_GetU32(in + 28U);
	Sidecar_DecodeIdentity(in + 32U, &header->rune_identity);
	header->payload_bytes = Sidecar_GetU32(in + 292U);
	header->payload_crc32 = Sidecar_GetU32(in + 296U);
	header->header_crc32 = Sidecar_GetU32(in + 300U);
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
	return sidecar_diagnostics[(uint32_t)diagnostic].symbol;
}

const char *SG_SidecarDiagnosticMessage(sg_sidecar_diagnostic_t diagnostic)
{
	if (diagnostic < SCD_OK || diagnostic >= SCD_DIAGNOSTIC_COUNT)
		return "unknown sidecar diagnostic";
	return sidecar_diagnostics[(uint32_t)diagnostic].message;
}

const char *SG_SidecarStageName(sg_sidecar_stage_t stage)
{
	if (stage < SCS_ARGUMENT || stage >= SCS_STAGE_COUNT)
		return "unknown";
	return sidecar_stages[(uint32_t)stage];
}

sg_sidecar_diagnostic_t SG_SidecarFileSize(sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info, size_t payload_size,
	size_t *size_out)
{
	if (!Sidecar_Descriptor(kind) || !Sidecar_ArtifactInfoValid(info) ||
		!size_out)
		return SCD_INVALID_ARGUMENT;
	if (payload_size > SG_SIDECAR_MAX_PAYLOAD_BYTES)
		return SCD_BAD_PAYLOAD_SIZE;
	*size_out = SG_SIDECAR_HEADER_BYTES + payload_size;
	return SCD_OK;
}

static sg_sidecar_diagnostic_t Sidecar_BindingMatches(
	const sg_sidecar_header_t *header,
	const sg_rune_compact_wire_info_t *info)
{
	if (header->rune_wire_version != info->wire_version ||
		header->rune_model_version != info->model_version ||
		header->rune_analytic_version != info->analytic_version)
		return SCD_RUNE_VERSION_MISMATCH;
	if (header->rune_schema_tag != info->schema_tag)
		return SCD_RUNE_SCHEMA_MISMATCH;
	if (header->rune_image_bytes != info->image_bytes)
		return SCD_RUNE_IMAGE_MISMATCH;
	if (header->rune_checksum != info->checksum)
		return SCD_RUNE_CHECKSUM_MISMATCH;
	return Sidecar_IdentityMatches(&header->rune_identity, &info->identity)
		? SCD_OK : SCD_RUNE_IDENTITY_MISMATCH;
}

sg_sidecar_diagnostic_t SG_SidecarInspect(
	const unsigned char *encoded_header, size_t encoded_header_size,
	size_t full_file_size, sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info, sg_sidecar_header_t *header_out)
{
	const sidecar_descriptor_t *descriptor = Sidecar_Descriptor(kind);
	sg_sidecar_header_t header;
	uint32_t computed_crc;
	size_t expected_file_size;
	sg_sidecar_diagnostic_t diagnostic;

	if (!encoded_header || !descriptor || !Sidecar_ArtifactInfoValid(info) ||
		!header_out)
		return SCD_INVALID_ARGUMENT;
	if (encoded_header_size != SG_SIDECAR_HEADER_BYTES)
		return SCD_BAD_HEADER_SIZE;
	Sidecar_DecodeHeaderBytes(encoded_header, &header);
	if (header.magic != descriptor->magic)
		return SCD_BAD_MAGIC;
	if (header.format_version != SG_SIDECAR_FORMAT_VERSION)
		return SCD_UNSUPPORTED_VERSION;
	if (header.header_bytes != SG_SIDECAR_HEADER_BYTES)
		return SCD_BAD_HEADER_SIZE;
	diagnostic = Sidecar_HeaderCRC(encoded_header, encoded_header_size,
		&computed_crc);
	if (diagnostic != SCD_OK)
		return diagnostic;
	if (computed_crc != header.header_crc32)
		return SCD_BAD_HEADER_CRC;
	if (header.reserved != 0U)
		return SCD_NONZERO_RESERVED;
	diagnostic = Sidecar_BindingMatches(&header, info);
	if (diagnostic != SCD_OK)
		return diagnostic;
	if (header.payload_bytes > SG_SIDECAR_MAX_PAYLOAD_BYTES)
		return SCD_BAD_PAYLOAD_SIZE;
	expected_file_size = SG_SIDECAR_HEADER_BYTES +
		(size_t)header.payload_bytes;
	if (full_file_size != expected_file_size)
		return SCD_BAD_FILE_SIZE;
	*header_out = header;
	return SCD_OK;
}

sg_sidecar_diagnostic_t SG_SidecarEncode(sg_sidecar_kind_t kind,
	const sg_rune_compact_wire_info_t *info, const unsigned char *payload,
	size_t payload_size, unsigned char *encoded, size_t encoded_capacity,
	size_t *encoded_size_out)
{
	const sidecar_descriptor_t *descriptor = Sidecar_Descriptor(kind);
	sg_sidecar_header_t header;
	unsigned char raw_header[SG_SIDECAR_HEADER_BYTES];
	uint32_t header_crc;
	uint32_t payload_crc;
	size_t file_size;
	sg_sidecar_diagnostic_t diagnostic;

	if (!descriptor || !Sidecar_ArtifactInfoValid(info) ||
		(payload_size != 0U && payload == NULL) || encoded == NULL ||
		encoded_size_out == NULL)
		return SCD_INVALID_ARGUMENT;
	diagnostic = SG_SidecarFileSize(kind, info, payload_size, &file_size);
	if (diagnostic != SCD_OK)
		return diagnostic;
	if (encoded_capacity < file_size)
		return SCD_BAD_FILE_SIZE;
	if (!SG_CRC32Buffer(payload, payload_size, &payload_crc))
		return SCD_INTERNAL_ERROR;
	memset(&header, 0, sizeof(header));
	header.magic = descriptor->magic;
	header.format_version = SG_SIDECAR_FORMAT_VERSION;
	header.header_bytes = SG_SIDECAR_HEADER_BYTES;
	header.rune_wire_version = info->wire_version;
	header.rune_model_version = info->model_version;
	header.rune_analytic_version = info->analytic_version;
	header.rune_schema_tag = info->schema_tag;
	header.rune_image_bytes = info->image_bytes;
	header.rune_checksum = info->checksum;
	header.rune_identity = info->identity;
	header.payload_bytes = (uint32_t)payload_size;
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
	const sg_rune_compact_wire_info_t *info, unsigned char *payload_out,
	size_t payload_capacity, size_t *payload_size_out)
{
	sg_sidecar_header_t header;
	const unsigned char *payload;
	uint32_t payload_crc;
	sg_sidecar_diagnostic_t diagnostic;

	if (!encoded || !Sidecar_ArtifactInfoValid(info) || !payload_out ||
		!payload_size_out)
		return SCD_INVALID_ARGUMENT;
	if (encoded_size < SG_SIDECAR_HEADER_BYTES)
		return SCD_BAD_FILE_SIZE;
	diagnostic = SG_SidecarInspect(encoded, SG_SIDECAR_HEADER_BYTES,
		encoded_size, kind, info, &header);
	if (diagnostic != SCD_OK)
		return diagnostic;
	payload = encoded + SG_SIDECAR_HEADER_BYTES;
	if (!SG_CRC32Buffer(payload, header.payload_bytes, &payload_crc))
		return SCD_INTERNAL_ERROR;
	if (payload_crc != header.payload_crc32)
		return SCD_BAD_PAYLOAD_CRC;
	if (payload_capacity < header.payload_bytes)
		return SCD_BAD_PAYLOAD_SIZE;
	if (header.payload_bytes != 0U)
		memmove(payload_out, payload, header.payload_bytes);
	*payload_size_out = header.payload_bytes;
	return SCD_OK;
}
