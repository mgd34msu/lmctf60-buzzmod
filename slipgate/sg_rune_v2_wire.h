/* sg_rune_v2_wire.h -- minimal RUNE v2 wire contract. */
#ifndef SG_RUNE_V2_WIRE_H
#define SG_RUNE_V2_WIRE_H

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#define SG_RUNE_V2_MAGIC UINT32_C(0x324e5552) /* RUN2, little endian. */
#define SG_RUNE_V2_VERSION UINT16_C(2)
#define SG_RUNE_V2_ENDIAN_LITTLE UINT16_C(0x0102)
#define SG_RUNE_V2_SCHEMA_REVISION UINT32_C(2)

#define SG_RUNE_V2_CONTENT_ID_BYTES UINT32_C(32)
#define SG_RUNE_V2_HEADER_BYTES UINT16_C(64)
#define SG_RUNE_V2_SECTION_ENTRY_BYTES UINT16_C(32)
#define SG_RUNE_V2_SECTION_ALIGNMENT UINT64_C(8)
#define SG_RUNE_V2_REQUIRED_SECTION_COUNT UINT32_C(6)
#define SG_RUNE_V2_MAX_ARTIFACT_BYTES UINT64_C(1073741824)

#define SG_RUNE_V2_HEADER_MAGIC_OFFSET UINT32_C(0)
#define SG_RUNE_V2_HEADER_VERSION_OFFSET UINT32_C(4)
#define SG_RUNE_V2_HEADER_ENDIAN_OFFSET UINT32_C(6)
#define SG_RUNE_V2_HEADER_BYTES_OFFSET UINT32_C(8)
#define SG_RUNE_V2_HEADER_ENTRY_BYTES_OFFSET UINT32_C(10)
#define SG_RUNE_V2_HEADER_SECTION_COUNT_OFFSET UINT32_C(12)
#define SG_RUNE_V2_HEADER_FLAGS_OFFSET UINT32_C(16)
#define SG_RUNE_V2_HEADER_SCHEMA_REVISION_OFFSET UINT32_C(20)
#define SG_RUNE_V2_HEADER_GENERATION_OFFSET UINT32_C(24)
#define SG_RUNE_V2_HEADER_TOTAL_BYTES_OFFSET UINT32_C(32)
#define SG_RUNE_V2_HEADER_PAYLOAD_CRC_OFFSET UINT32_C(40)
#define SG_RUNE_V2_HEADER_CRC_OFFSET UINT32_C(44)
#define SG_RUNE_V2_HEADER_RESERVED_OFFSET UINT32_C(48)
#define SG_RUNE_V2_HEADER_RESERVED_BYTES UINT32_C(16)

#define SG_RUNE_V2_SECTION_TYPE_OFFSET UINT32_C(0)
#define SG_RUNE_V2_SECTION_FLAGS_OFFSET UINT32_C(2)
#define SG_RUNE_V2_SECTION_ELEMENT_BYTES_OFFSET UINT32_C(4)
#define SG_RUNE_V2_SECTION_COUNT_OFFSET UINT32_C(8)
#define SG_RUNE_V2_SECTION_CRC_OFFSET UINT32_C(12)
#define SG_RUNE_V2_SECTION_OFFSET_OFFSET UINT32_C(16)
#define SG_RUNE_V2_SECTION_BYTES_OFFSET UINT32_C(24)

#define SG_RUNE_V2_SECTION_FLAG_REQUIRED UINT16_C(1)

#define SG_RUNE_V2_CELL_RECORD_BYTES UINT32_C(32)
#define SG_RUNE_V2_PORTAL_RECORD_BYTES UINT32_C(40)
#define SG_RUNE_V2_CAPABILITY_RECORD_BYTES UINT32_C(24)
#define SG_RUNE_V2_MECHANISM_RECORD_BYTES UINT32_C(32)
#define SG_RUNE_V2_COST_RECORD_BYTES UINT32_C(16)
#define SG_RUNE_V2_BINDING_RECORD_BYTES UINT32_C(64)

#define SG_RUNE_V2_CELL_ID_OFFSET UINT32_C(0)
#define SG_RUNE_V2_CELL_FLAGS_OFFSET UINT32_C(4)
#define SG_RUNE_V2_CELL_BOUNDS_MIN_OFFSET UINT32_C(8)
#define SG_RUNE_V2_CELL_BOUNDS_MAX_OFFSET UINT32_C(20)

#define SG_RUNE_V2_PORTAL_ID_OFFSET UINT32_C(0)
#define SG_RUNE_V2_PORTAL_FROM_CELL_OFFSET UINT32_C(4)
#define SG_RUNE_V2_PORTAL_TO_CELL_OFFSET UINT32_C(8)
#define SG_RUNE_V2_PORTAL_FLAGS_OFFSET UINT32_C(12)
#define SG_RUNE_V2_PORTAL_BOUNDS_MIN_OFFSET UINT32_C(16)
#define SG_RUNE_V2_PORTAL_BOUNDS_MAX_OFFSET UINT32_C(28)

#define SG_RUNE_V2_CAPABILITY_ID_OFFSET UINT32_C(0)
#define SG_RUNE_V2_CAPABILITY_FROM_CELL_OFFSET UINT32_C(4)
#define SG_RUNE_V2_CAPABILITY_TO_CELL_OFFSET UINT32_C(8)
#define SG_RUNE_V2_CAPABILITY_KIND_OFFSET UINT32_C(12)
#define SG_RUNE_V2_CAPABILITY_COST_OFFSET UINT32_C(16)
#define SG_RUNE_V2_CAPABILITY_MECHANISM_OFFSET UINT32_C(20)

#define SG_RUNE_V2_MECHANISM_ID_OFFSET UINT32_C(0)
#define SG_RUNE_V2_MECHANISM_KIND_OFFSET UINT32_C(4)
#define SG_RUNE_V2_MECHANISM_ENTRY_CELL_OFFSET UINT32_C(8)
#define SG_RUNE_V2_MECHANISM_EXIT_CELL_OFFSET UINT32_C(12)
#define SG_RUNE_V2_MECHANISM_CONTROLLER_OFFSET UINT32_C(16)
#define SG_RUNE_V2_MECHANISM_DWELL_MS_OFFSET UINT32_C(20)
#define SG_RUNE_V2_MECHANISM_TRAVEL_MS_OFFSET UINT32_C(24)
#define SG_RUNE_V2_MECHANISM_RESERVED_OFFSET UINT32_C(28)

#define SG_RUNE_V2_COST_ID_OFFSET UINT32_C(0)
#define SG_RUNE_V2_COST_MIN_MS_OFFSET UINT32_C(4)
#define SG_RUNE_V2_COST_MAX_MS_OFFSET UINT32_C(8)
#define SG_RUNE_V2_COST_FLAGS_OFFSET UINT32_C(12)

#define SG_RUNE_V2_BINDING_BSP_OFFSET UINT32_C(0)
#define SG_RUNE_V2_BINDING_SCHEMA_OFFSET UINT32_C(32)

#define SG_RUNE_V2_MAX_CELLS UINT32_C(4194304)
#define SG_RUNE_V2_MAX_PORTALS UINT32_C(8388608)
#define SG_RUNE_V2_MAX_CAPABILITIES UINT32_C(8388608)
#define SG_RUNE_V2_MAX_MECHANISMS UINT32_C(1048576)
#define SG_RUNE_V2_MAX_COSTS UINT32_C(8388608)
#define SG_RUNE_V2_REFERENCE_NONE UINT32_MAX

typedef struct sg_rune_v2_content_id_s
{
	uint8_t bytes[SG_RUNE_V2_CONTENT_ID_BYTES];
} sg_rune_v2_content_id_t;

typedef enum sg_rune_v2_section_type_e
{
	SG_RUNE_V2_SECTION_CELLS = 1,
	SG_RUNE_V2_SECTION_PORTALS = 2,
	SG_RUNE_V2_SECTION_CAPABILITIES = 3,
	SG_RUNE_V2_SECTION_MECHANISMS = 4,
	SG_RUNE_V2_SECTION_COSTS = 5,
	SG_RUNE_V2_SECTION_BINDING = 6
} sg_rune_v2_section_type_t;

typedef enum sg_rune_v2_capability_kind_e
{
	SG_RUNE_V2_CAPABILITY_WALK = 1,
	SG_RUNE_V2_CAPABILITY_CROUCH,
	SG_RUNE_V2_CAPABILITY_JUMP,
	SG_RUNE_V2_CAPABILITY_DROP,
	SG_RUNE_V2_CAPABILITY_SWIM,
	SG_RUNE_V2_CAPABILITY_AIR,
	SG_RUNE_V2_CAPABILITY_HOOK,
	SG_RUNE_V2_CAPABILITY_MOVER,
	SG_RUNE_V2_CAPABILITY_PUSH,
	SG_RUNE_V2_CAPABILITY_TELEPORT,
	SG_RUNE_V2_CAPABILITY_MECHANISM
} sg_rune_v2_capability_kind_t;

typedef enum sg_rune_v2_wire_diagnostic_e
{
	SG_RUNE_V2_WIRE_OK = 0,
	SG_RUNE_V2_WIRE_INVALID_ARGUMENT,
	SG_RUNE_V2_WIRE_TRUNCATED,
	SG_RUNE_V2_WIRE_BAD_HEADER,
	SG_RUNE_V2_WIRE_BAD_VERSION,
	SG_RUNE_V2_WIRE_BAD_ENDIAN,
	SG_RUNE_V2_WIRE_BAD_SIZE,
	SG_RUNE_V2_WIRE_BAD_HEADER_CRC,
	SG_RUNE_V2_WIRE_BAD_PAYLOAD_CRC,
	SG_RUNE_V2_WIRE_BAD_SECTION,
	SG_RUNE_V2_WIRE_BAD_SECTION_CRC,
	SG_RUNE_V2_WIRE_HOSTILE_COUNT,
	SG_RUNE_V2_WIRE_BAD_RECORD,
	SG_RUNE_V2_WIRE_BAD_REFERENCE,
	SG_RUNE_V2_WIRE_BAD_BINDING
} sg_rune_v2_wire_diagnostic_t;

typedef struct sg_rune_v2_wire_header_s
{
	uint64_t generation;
	uint64_t total_bytes;
	uint32_t payload_crc32;
} sg_rune_v2_wire_header_t;

typedef struct sg_rune_v2_wire_section_s
{
	uint16_t type;
	uint32_t element_bytes;
	uint32_t count;
	uint64_t offset;
	uint64_t bytes;
} sg_rune_v2_wire_section_t;

typedef struct sg_rune_v2_wire_view_s
{
	sg_rune_v2_wire_header_t header;
	sg_rune_v2_wire_section_t section[SG_RUNE_V2_REQUIRED_SECTION_COUNT];
	sg_rune_v2_content_id_t bsp_identity;
	sg_rune_v2_content_id_t schema_identity;
} sg_rune_v2_wire_view_t;

/* This identity is supplied by the exact-file identity boundary.  The wire
 * contract neither computes nor transforms it. */
typedef struct sg_rune_v2_artifact_binding_s
{
	uint64_t generation;
	sg_rune_v2_content_id_t bsp_identity;
	sg_rune_v2_content_id_t schema_identity;
	sg_rune_v2_content_id_t artifact_identity;
} sg_rune_v2_artifact_binding_t;

static inline uint16_t SG_RuneV2WireGetU16(const unsigned char *bytes)
{
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static inline uint32_t SG_RuneV2WireGetU32(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static inline uint64_t SG_RuneV2WireGetU64(const unsigned char *bytes)
{
	uint64_t value = 0U;
	unsigned int index;

	for (index = 0; index < 8U; index++)
		value |= (uint64_t)bytes[index] << (index * 8U);
	return value;
}

static inline void SG_RuneV2WirePutU16(unsigned char *bytes, uint16_t value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
}

static inline void SG_RuneV2WirePutU32(unsigned char *bytes, uint32_t value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static inline void SG_RuneV2WirePutU64(unsigned char *bytes, uint64_t value)
{
	unsigned int index;

	for (index = 0; index < 8U; index++)
		bytes[index] = (unsigned char)(value >> (index * 8U));
}

static inline int SG_RuneV2ContentIdEqual(const sg_rune_v2_content_id_t *left,
	const sg_rune_v2_content_id_t *right)
{
	size_t index;

	if (!left || !right)
		return 0;
	for (index = 0; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		if (left->bytes[index] != right->bytes[index])
			return 0;
	return 1;
}

static inline int SG_RuneV2ContentIdValid(const sg_rune_v2_content_id_t *id)
{
	size_t index;

	if (!id)
		return 0;
	for (index = 0; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		if (id->bytes[index] != 0U)
			return 1;
	return 0;
}

static inline uint32_t SG_RuneV2WireCRC32(const unsigned char *bytes,
	size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t index;

	if (!bytes && size != 0U)
		return 0U;
	for (index = 0; index < size; index++)
	{
		unsigned int bit;

		crc ^= bytes[index];
		for (bit = 0; bit < 8U; bit++)
			crc = (crc >> 1) ^ ((crc & 1U) ? UINT32_C(0xedb88320) : 0U);
	}
	return ~crc;
}

static inline uint32_t SG_RuneV2WireHeaderCRC32(const unsigned char *header)
{
	unsigned char copy[SG_RUNE_V2_HEADER_BYTES];
	size_t index;

	if (!header)
		return 0U;
	for (index = 0; index < SG_RUNE_V2_HEADER_BYTES; index++)
		copy[index] = header[index];
	SG_RuneV2WirePutU32(copy + SG_RUNE_V2_HEADER_CRC_OFFSET, 0U);
	return SG_RuneV2WireCRC32(copy, sizeof(copy));
}

static inline int SG_RuneV2WireCheckedAdd(uint64_t left, uint64_t right,
	uint64_t *result)
{
	if (!result || right > UINT64_MAX - left)
		return 0;
	*result = left + right;
	return 1;
}

static inline int SG_RuneV2WireCheckedMul(uint64_t left, uint64_t right,
	uint64_t *result)
{
	if (!result || (left != 0U && right > UINT64_MAX / left))
		return 0;
	*result = left * right;
	return 1;
}

static inline uint32_t SG_RuneV2WireRecordBytes(uint16_t type)
{
	switch (type)
	{
	case SG_RUNE_V2_SECTION_CELLS: return SG_RUNE_V2_CELL_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_PORTALS: return SG_RUNE_V2_PORTAL_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_CAPABILITIES: return SG_RUNE_V2_CAPABILITY_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_MECHANISMS: return SG_RUNE_V2_MECHANISM_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_COSTS: return SG_RUNE_V2_COST_RECORD_BYTES;
	case SG_RUNE_V2_SECTION_BINDING: return SG_RUNE_V2_BINDING_RECORD_BYTES;
	default: return 0U;
	}
}

static inline uint32_t SG_RuneV2WireMaxCount(uint16_t type)
{
	switch (type)
	{
	case SG_RUNE_V2_SECTION_CELLS: return SG_RUNE_V2_MAX_CELLS;
	case SG_RUNE_V2_SECTION_PORTALS: return SG_RUNE_V2_MAX_PORTALS;
	case SG_RUNE_V2_SECTION_CAPABILITIES: return SG_RUNE_V2_MAX_CAPABILITIES;
	case SG_RUNE_V2_SECTION_MECHANISMS: return SG_RUNE_V2_MAX_MECHANISMS;
	case SG_RUNE_V2_SECTION_COSTS: return SG_RUNE_V2_MAX_COSTS;
	case SG_RUNE_V2_SECTION_BINDING: return 1U;
	default: return 0U;
	}
}

static inline const unsigned char *SG_RuneV2WireSectionData(
	const unsigned char *encoded, const sg_rune_v2_wire_section_t *section)
{
	return encoded + (size_t)section->offset;
}

static inline int SG_RuneV2WireBoundsOrdered(const unsigned char *record,
	uint32_t minimum_offset, uint32_t maximum_offset)
{
	unsigned int axis;

	for (axis = 0; axis < 3U; axis++)
	{
		int32_t minimum = (int32_t)SG_RuneV2WireGetU32(record + minimum_offset +
			axis * 4U);
		int32_t maximum = (int32_t)SG_RuneV2WireGetU32(record + maximum_offset +
			axis * 4U);

		if (minimum >= maximum)
			return 0;
	}
	return 1;
}

static inline sg_rune_v2_wire_diagnostic_t SG_RuneV2WireValidateRecords(
	const unsigned char *encoded, const sg_rune_v2_wire_view_t *view)
{
	uint32_t index;
	uint32_t cells = view->section[SG_RUNE_V2_SECTION_CELLS - 1U].count;
	uint32_t costs = view->section[SG_RUNE_V2_SECTION_COSTS - 1U].count;
	uint32_t mechanisms = view->section[SG_RUNE_V2_SECTION_MECHANISMS - 1U].count;

	for (index = 0; index < SG_RUNE_V2_REQUIRED_SECTION_COUNT - 1U; index++)
	{
		const sg_rune_v2_wire_section_t *section = &view->section[index];
		const unsigned char *record = SG_RuneV2WireSectionData(encoded, section);
		uint32_t item;

		for (item = 0; item < section->count; item++, record += section->element_bytes)
		{
			if (SG_RuneV2WireGetU32(record) != item)
				return SG_RUNE_V2_WIRE_BAD_RECORD;
			switch (section->type)
			{
			case SG_RUNE_V2_SECTION_CELLS:
				if (!SG_RuneV2WireBoundsOrdered(record,
					SG_RUNE_V2_CELL_BOUNDS_MIN_OFFSET,
					SG_RUNE_V2_CELL_BOUNDS_MAX_OFFSET))
					return SG_RUNE_V2_WIRE_BAD_RECORD;
				break;
			case SG_RUNE_V2_SECTION_PORTALS:
				if (SG_RuneV2WireGetU32(record + SG_RUNE_V2_PORTAL_FROM_CELL_OFFSET) >= cells ||
					SG_RuneV2WireGetU32(record + SG_RUNE_V2_PORTAL_TO_CELL_OFFSET) >= cells ||
					SG_RuneV2WireGetU32(record + SG_RUNE_V2_PORTAL_FROM_CELL_OFFSET) ==
					SG_RuneV2WireGetU32(record + SG_RUNE_V2_PORTAL_TO_CELL_OFFSET))
					return SG_RUNE_V2_WIRE_BAD_REFERENCE;
				if (!SG_RuneV2WireBoundsOrdered(record,
					SG_RUNE_V2_PORTAL_BOUNDS_MIN_OFFSET,
					SG_RUNE_V2_PORTAL_BOUNDS_MAX_OFFSET))
					return SG_RUNE_V2_WIRE_BAD_RECORD;
				break;
			case SG_RUNE_V2_SECTION_CAPABILITIES:
			{
				uint32_t mechanism = SG_RuneV2WireGetU32(record +
					SG_RUNE_V2_CAPABILITY_MECHANISM_OFFSET);
				uint32_t kind = SG_RuneV2WireGetU32(record +
					SG_RUNE_V2_CAPABILITY_KIND_OFFSET);

				if (SG_RuneV2WireGetU32(record + SG_RUNE_V2_CAPABILITY_FROM_CELL_OFFSET) >= cells ||
					SG_RuneV2WireGetU32(record + SG_RUNE_V2_CAPABILITY_TO_CELL_OFFSET) >= cells ||
					SG_RuneV2WireGetU32(record + SG_RUNE_V2_CAPABILITY_COST_OFFSET) >= costs ||
					(kind < SG_RUNE_V2_CAPABILITY_WALK ||
					 kind > SG_RUNE_V2_CAPABILITY_MECHANISM) ||
					(mechanism != SG_RUNE_V2_REFERENCE_NONE && mechanism >= mechanisms))
					return SG_RUNE_V2_WIRE_BAD_REFERENCE;
				break;
			}
			case SG_RUNE_V2_SECTION_MECHANISMS:
				if (SG_RuneV2WireGetU32(record + SG_RUNE_V2_MECHANISM_KIND_OFFSET) == 0U ||
					SG_RuneV2WireGetU32(record + SG_RUNE_V2_MECHANISM_ENTRY_CELL_OFFSET) >= cells ||
					SG_RuneV2WireGetU32(record + SG_RUNE_V2_MECHANISM_EXIT_CELL_OFFSET) >= cells ||
					SG_RuneV2WireGetU32(record + SG_RUNE_V2_MECHANISM_RESERVED_OFFSET) != 0U)
					return SG_RUNE_V2_WIRE_BAD_REFERENCE;
				break;
			case SG_RUNE_V2_SECTION_COSTS:
				if (SG_RuneV2WireGetU32(record + SG_RUNE_V2_COST_MAX_MS_OFFSET) == 0U ||
					SG_RuneV2WireGetU32(record + SG_RUNE_V2_COST_MIN_MS_OFFSET) >
					SG_RuneV2WireGetU32(record + SG_RUNE_V2_COST_MAX_MS_OFFSET))
					return SG_RUNE_V2_WIRE_BAD_RECORD;
				break;
			default:
				return SG_RUNE_V2_WIRE_BAD_SECTION;
			}
		}
	}
	return SG_RUNE_V2_WIRE_OK;
}

static inline sg_rune_v2_wire_diagnostic_t SG_RuneV2WireInspect(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_v2_wire_view_t *view_out)
{
	sg_rune_v2_wire_view_t view = { 0 };
	uint64_t directory_end;
	uint64_t previous_end;
	uint32_t index;

	if (!encoded || !view_out)
		return SG_RUNE_V2_WIRE_INVALID_ARGUMENT;
	if (encoded_size < SG_RUNE_V2_HEADER_BYTES)
		return SG_RUNE_V2_WIRE_TRUNCATED;
	if (SG_RuneV2WireGetU32(encoded + SG_RUNE_V2_HEADER_MAGIC_OFFSET) != SG_RUNE_V2_MAGIC ||
		SG_RuneV2WireGetU16(encoded + SG_RUNE_V2_HEADER_BYTES_OFFSET) != SG_RUNE_V2_HEADER_BYTES ||
		SG_RuneV2WireGetU16(encoded + SG_RUNE_V2_HEADER_ENTRY_BYTES_OFFSET) !=
		SG_RUNE_V2_SECTION_ENTRY_BYTES ||
		SG_RuneV2WireGetU32(encoded + SG_RUNE_V2_HEADER_SECTION_COUNT_OFFSET) !=
		SG_RUNE_V2_REQUIRED_SECTION_COUNT ||
		SG_RuneV2WireGetU32(encoded + SG_RUNE_V2_HEADER_FLAGS_OFFSET) != 0U ||
		SG_RuneV2WireGetU32(encoded + SG_RUNE_V2_HEADER_SCHEMA_REVISION_OFFSET) !=
		SG_RUNE_V2_SCHEMA_REVISION)
		return SG_RUNE_V2_WIRE_BAD_HEADER;
	if (SG_RuneV2WireGetU16(encoded + SG_RUNE_V2_HEADER_VERSION_OFFSET) !=
		SG_RUNE_V2_VERSION)
		return SG_RUNE_V2_WIRE_BAD_VERSION;
	if (SG_RuneV2WireGetU16(encoded + SG_RUNE_V2_HEADER_ENDIAN_OFFSET) !=
		SG_RUNE_V2_ENDIAN_LITTLE)
		return SG_RUNE_V2_WIRE_BAD_ENDIAN;
	for (index = 0; index < SG_RUNE_V2_HEADER_RESERVED_BYTES; index++)
		if (encoded[SG_RUNE_V2_HEADER_RESERVED_OFFSET + index] != 0U)
			return SG_RUNE_V2_WIRE_BAD_HEADER;
	view.header.generation = SG_RuneV2WireGetU64(encoded +
		SG_RUNE_V2_HEADER_GENERATION_OFFSET);
	view.header.total_bytes = SG_RuneV2WireGetU64(encoded +
		SG_RUNE_V2_HEADER_TOTAL_BYTES_OFFSET);
	view.header.payload_crc32 = SG_RuneV2WireGetU32(encoded +
		SG_RUNE_V2_HEADER_PAYLOAD_CRC_OFFSET);
	if (view.header.generation == 0U || view.header.total_bytes != encoded_size ||
		view.header.total_bytes > SG_RUNE_V2_MAX_ARTIFACT_BYTES)
		return SG_RUNE_V2_WIRE_BAD_SIZE;
	if (SG_RuneV2WireHeaderCRC32(encoded) != SG_RuneV2WireGetU32(encoded +
		SG_RUNE_V2_HEADER_CRC_OFFSET))
		return SG_RUNE_V2_WIRE_BAD_HEADER_CRC;
	directory_end = SG_RUNE_V2_HEADER_BYTES +
		(uint64_t)SG_RUNE_V2_REQUIRED_SECTION_COUNT * SG_RUNE_V2_SECTION_ENTRY_BYTES;
	if (directory_end > view.header.total_bytes)
		return SG_RUNE_V2_WIRE_BAD_SIZE;
	previous_end = directory_end;
	for (index = 0; index < SG_RUNE_V2_REQUIRED_SECTION_COUNT; index++)
	{
		const unsigned char *entry = encoded + SG_RUNE_V2_HEADER_BYTES +
			(size_t)index * SG_RUNE_V2_SECTION_ENTRY_BYTES;
		sg_rune_v2_wire_section_t *section = &view.section[index];
		uint64_t expected_bytes;
		uint64_t end;

		section->type = SG_RuneV2WireGetU16(entry + SG_RUNE_V2_SECTION_TYPE_OFFSET);
		section->element_bytes = SG_RuneV2WireGetU32(entry +
			SG_RUNE_V2_SECTION_ELEMENT_BYTES_OFFSET);
		section->count = SG_RuneV2WireGetU32(entry + SG_RUNE_V2_SECTION_COUNT_OFFSET);
		section->offset = SG_RuneV2WireGetU64(entry + SG_RUNE_V2_SECTION_OFFSET_OFFSET);
		section->bytes = SG_RuneV2WireGetU64(entry + SG_RUNE_V2_SECTION_BYTES_OFFSET);
		if (section->type != index + 1U ||
			SG_RuneV2WireGetU16(entry + SG_RUNE_V2_SECTION_FLAGS_OFFSET) !=
			SG_RUNE_V2_SECTION_FLAG_REQUIRED ||
			section->element_bytes != SG_RuneV2WireRecordBytes(section->type) ||
			section->count > SG_RuneV2WireMaxCount(section->type) ||
			(section->type == SG_RUNE_V2_SECTION_CELLS && section->count == 0U) ||
			(section->type == SG_RUNE_V2_SECTION_BINDING && section->count != 1U))
			return section->count > SG_RuneV2WireMaxCount(section->type)
				? SG_RUNE_V2_WIRE_HOSTILE_COUNT : SG_RUNE_V2_WIRE_BAD_SECTION;
		if (!SG_RuneV2WireCheckedMul(section->element_bytes, section->count,
			&expected_bytes) || expected_bytes != section->bytes ||
			section->offset < previous_end ||
			(section->offset & (SG_RUNE_V2_SECTION_ALIGNMENT - 1U)) != 0U ||
			!SG_RuneV2WireCheckedAdd(section->offset, section->bytes, &end) ||
			end > view.header.total_bytes)
			return SG_RUNE_V2_WIRE_BAD_SECTION;
		while (previous_end < section->offset)
			if (encoded[(size_t)previous_end++] != 0U)
				return SG_RUNE_V2_WIRE_BAD_SECTION;
		if (SG_RuneV2WireCRC32(encoded + (size_t)section->offset,
			(size_t)section->bytes) != SG_RuneV2WireGetU32(entry +
			SG_RUNE_V2_SECTION_CRC_OFFSET))
			return SG_RUNE_V2_WIRE_BAD_SECTION_CRC;
		previous_end = end;
	}
	while (previous_end < view.header.total_bytes)
		if (encoded[(size_t)previous_end++] != 0U)
			return SG_RUNE_V2_WIRE_BAD_SIZE;
	if (SG_RuneV2WireCRC32(encoded + SG_RUNE_V2_HEADER_BYTES,
		encoded_size - SG_RUNE_V2_HEADER_BYTES) != view.header.payload_crc32)
		return SG_RUNE_V2_WIRE_BAD_PAYLOAD_CRC;
	{
		const unsigned char *binding = SG_RuneV2WireSectionData(encoded,
			&view.section[SG_RUNE_V2_SECTION_BINDING - 1U]);

		for (index = 0; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		{
			view.bsp_identity.bytes[index] = binding[SG_RUNE_V2_BINDING_BSP_OFFSET + index];
			view.schema_identity.bytes[index] = binding[
				SG_RUNE_V2_BINDING_SCHEMA_OFFSET + index];
		}
	}
	if (!SG_RuneV2ContentIdValid(&view.bsp_identity) ||
		!SG_RuneV2ContentIdValid(&view.schema_identity))
		return SG_RUNE_V2_WIRE_BAD_BINDING;
	{
		sg_rune_v2_wire_diagnostic_t diagnostic =
			SG_RuneV2WireValidateRecords(encoded, &view);

		if (diagnostic != SG_RUNE_V2_WIRE_OK)
			return diagnostic;
	}
	*view_out = view;
	return SG_RUNE_V2_WIRE_OK;
}

static inline int SG_RuneV2ArtifactBindingAccepts(
	const sg_rune_v2_wire_view_t *wire,
	const sg_rune_v2_artifact_binding_t *binding,
	const sg_rune_v2_content_id_t *exact_file_identity)
{
	return wire && binding && wire->header.generation == binding->generation &&
		binding->generation != 0U && SG_RuneV2ContentIdValid(exact_file_identity) &&
		SG_RuneV2ContentIdEqual(&wire->bsp_identity, &binding->bsp_identity) &&
		SG_RuneV2ContentIdEqual(&wire->schema_identity, &binding->schema_identity) &&
		SG_RuneV2ContentIdEqual(&binding->artifact_identity,
			exact_file_identity);
}

#endif /* SG_RUNE_V2_WIRE_H */
