/* Make-side RUNE v2 reader: snapshot ownership and wire checks stay local. */
#include "slipgate/sg_rune_v2_exact_snapshot.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define RV2M_MAGIC UINT32_C(0x324e5552)
#define RV2M_VERSION UINT16_C(2)
#define RV2M_ENDIAN UINT16_C(0x0102)
#define RV2M_SCHEMA_REVISION UINT32_C(3)
#define RV2M_HEADER_BYTES UINT64_C(64)
#define RV2M_SECTION_ENTRY_BYTES UINT64_C(32)
#define RV2M_SECTION_COUNT UINT32_C(13)
#define RV2M_SECTION_ALIGNMENT UINT64_C(8)
#define RV2M_MAX_ARTIFACT_BYTES UINT64_C(4294967296)
#define RV2M_ID_BYTES 32U
#define RV2M_NONE UINT64_MAX

typedef struct rv2m_section_s
{
	uint32_t element_bytes;
	uint32_t count;
	uint64_t offset;
	uint64_t bytes;
} rv2m_section_t;

typedef struct rv2m_id_s
{
	uint64_t source;
	uint64_t high;
	uint64_t low;
} rv2m_id_t;

typedef struct rv2m_context_s
{
	const unsigned char *bytes;
	size_t size;
	uint64_t generation;
	uint64_t source;
	rv2m_section_t section[RV2M_SECTION_COUNT];
} rv2m_context_t;

typedef struct rv2m_expected_s
{
	uint64_t generation;
	unsigned char bsp[RV2M_ID_BYTES];
	unsigned char schema[RV2M_ID_BYTES];
	unsigned char artifact[RV2M_ID_BYTES];
	unsigned char exact_artifact[RV2M_ID_BYTES];
} rv2m_expected_t;

static const uint32_t rv2m_record_bytes[RV2M_SECTION_COUNT] = {
	256U, 64U, 12U, 136U, 136U, 164U, 172U, 132U, 104U, 332U,
	188U, 160U, 64U
};

static const uint32_t rv2m_max_counts[RV2M_SECTION_COUNT] = {
	1U, 4194304U, 8388608U, 262144U, 4194304U, 1048576U,
	2097152U, 2097152U, 2097152U, 4194304U, 65536U, 65536U, 1U
};

static const uint32_t rv2m_domains[RV2M_SECTION_COUNT] = {
	0U, 3U, 0U, 4U, 5U, 1U, 2U, 6U, 7U, 8U, 9U, 10U, 0U
};

static uint16_t RV2MakeReadU16(const unsigned char *bytes)
{
	return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static uint32_t RV2MakeReadU32(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t RV2MakeReadU64(const unsigned char *bytes)
{
	uint64_t value = 0U;
	unsigned int index;

	for (index = 0U; index < 8U; index++)
		value |= (uint64_t)bytes[index] << (index * 8U);
	return value;
}

static uint32_t RV2MakeCRC32(const unsigned char *bytes, size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t index;

	for (index = 0U; index < size; index++)
	{
		unsigned int bit;

		crc ^= bytes[index];
		for (bit = 0U; bit < 8U; bit++)
			crc = (crc >> 1) ^ ((crc & 1U) != 0U
				? UINT32_C(0xedb88320) : 0U);
	}
	return ~crc;
}

static int RV2MakeAdd(uint64_t left, uint64_t right, uint64_t *result)
{
	if (!result || right > UINT64_MAX - left)
		return 0;
	*result = left + right;
	return 1;
}

static int RV2MakeMultiply(uint64_t left, uint64_t right, uint64_t *result)
{
	if (!result || (left != 0U && right > UINT64_MAX / left))
		return 0;
	*result = left * right;
	return 1;
}

static int RV2MakeAlign(uint64_t value, uint64_t *result)
{
	uint64_t padded;

	if (!RV2MakeAdd(value, RV2M_SECTION_ALIGNMENT - 1U, &padded))
		return 0;
	*result = padded & ~(RV2M_SECTION_ALIGNMENT - 1U);
	return 1;
}

static int RV2MakeRange(uint64_t offset, uint64_t bytes, uint64_t total)
{
	return offset <= total && bytes <= total - offset;
}

static int RV2MakeZero(const unsigned char *bytes, uint64_t count)
{
	uint64_t index;

	for (index = 0U; index < count; index++)
		if (bytes[(size_t)index] != 0U)
			return 0;
	return 1;
}

static int RV2MakeIdEqual(rv2m_id_t left, rv2m_id_t right)
{
	return left.source == right.source && left.high == right.high &&
		left.low == right.low;
}

static int RV2MakeIdNone(rv2m_id_t id)
{
	return id.source == RV2M_NONE && id.high == RV2M_NONE && id.low == RV2M_NONE;
}

static rv2m_id_t RV2MakeId(const unsigned char *bytes)
{
	rv2m_id_t id;

	id.source = RV2MakeReadU64(bytes);
	id.high = RV2MakeReadU64(bytes + 8U);
	id.low = RV2MakeReadU64(bytes + 16U);
	return id;
}

static int RV2MakeBytesEqual(const unsigned char *left,
	const unsigned char *right, size_t size)
{
	return memcmp(left, right, size) == 0;
}

static const unsigned char *RV2MakeRecord(const rv2m_context_t *context,
	uint32_t section, uint32_t record)
{
	const rv2m_section_t *description = &context->section[section];

	return context->bytes + (size_t)description->offset +
		(size_t)record * description->element_bytes;
}

static int RV2MakeSpan(const unsigned char *bytes, uint32_t total,
	uint32_t minimum, uint32_t maximum)
{
	uint32_t first = RV2MakeReadU32(bytes);
	uint32_t count = RV2MakeReadU32(bytes + 4U);

	return count >= minimum && count <= maximum && first <= total &&
		count <= total - first;
}

static int RV2MakeFindId(const rv2m_context_t *context, uint32_t section,
	rv2m_id_t wanted)
{
	uint32_t index;

	for (index = 0U; index < context->section[section].count; index++)
		if (RV2MakeIdEqual(RV2MakeId(RV2MakeRecord(context, section, index)),
			wanted))
			return 1;
	return 0;
}

static int RV2MakeReference(const rv2m_context_t *context, uint32_t section,
	rv2m_id_t wanted, int optional)
{
	return (optional && RV2MakeIdNone(wanted)) ||
		RV2MakeFindId(context, section, wanted);
}

static int RV2MakeParseHeader(rv2m_context_t *context)
{
	const unsigned char *bytes = context->bytes;
	uint64_t total;
	uint64_t previous_end;
	uint32_t index;
	unsigned char header[RV2M_HEADER_BYTES];

	if (context->size < RV2M_HEADER_BYTES ||
		RV2MakeReadU32(bytes) != RV2M_MAGIC ||
		RV2MakeReadU16(bytes + 4U) != RV2M_VERSION ||
		RV2MakeReadU16(bytes + 6U) != RV2M_ENDIAN ||
		RV2MakeReadU16(bytes + 8U) != RV2M_HEADER_BYTES ||
		RV2MakeReadU16(bytes + 10U) != RV2M_SECTION_ENTRY_BYTES ||
		RV2MakeReadU32(bytes + 12U) != RV2M_SECTION_COUNT ||
		RV2MakeReadU32(bytes + 16U) != 0U ||
		RV2MakeReadU32(bytes + 20U) != RV2M_SCHEMA_REVISION ||
#ifndef RV2MAKE_TEST_SKIP_HEADER_RESERVED
		!RV2MakeZero(bytes + 48U, 16U) ||
#endif
		0)
		return 0;
	context->generation = RV2MakeReadU64(bytes + 24U);
	total = RV2MakeReadU64(bytes + 32U);
	if (context->generation == 0U || total != context->size ||
		total > RV2M_MAX_ARTIFACT_BYTES)
		return 0;
	memcpy(header, bytes, sizeof(header));
	memset(header + 44U, 0, 4U);
	if (RV2MakeCRC32(header, sizeof(header)) != RV2MakeReadU32(bytes + 44U))
		return 0;
	previous_end = RV2M_HEADER_BYTES +
		(uint64_t)RV2M_SECTION_COUNT * RV2M_SECTION_ENTRY_BYTES;
	if (previous_end > total)
		return 0;
	for (index = 0U; index < RV2M_SECTION_COUNT; index++)
	{
		const unsigned char *entry = bytes + RV2M_HEADER_BYTES +
			(size_t)index * RV2M_SECTION_ENTRY_BYTES;
		rv2m_section_t *section = &context->section[index];
		uint64_t expected_offset;
		uint64_t expected_bytes;
		uint64_t end;

		if (!RV2MakeAlign(previous_end, &expected_offset))
			return 0;
		section->element_bytes = RV2MakeReadU32(entry + 4U);
		section->count = RV2MakeReadU32(entry + 8U);
		section->offset = RV2MakeReadU64(entry + 16U);
		section->bytes = RV2MakeReadU64(entry + 24U);
		if (RV2MakeReadU16(entry) != (uint16_t)(index + 1U) ||
			RV2MakeReadU16(entry + 2U) != 1U ||
			section->element_bytes != rv2m_record_bytes[index] ||
			section->count > rv2m_max_counts[index] ||
			((index == 0U || index == RV2M_SECTION_COUNT - 1U) &&
				section->count != 1U) ||
			!RV2MakeMultiply(section->element_bytes, section->count,
				&expected_bytes) || section->bytes != expected_bytes ||
			section->offset != expected_offset ||
			(section->offset & (RV2M_SECTION_ALIGNMENT - 1U)) != 0U ||
			!RV2MakeRange(section->offset, section->bytes, total) ||
			!RV2MakeZero(bytes + (size_t)previous_end,
				section->offset - previous_end) ||
			RV2MakeCRC32(bytes + (size_t)section->offset,
				(size_t)section->bytes) != RV2MakeReadU32(entry + 12U) ||
			!RV2MakeAdd(section->offset, section->bytes, &end))
			return 0;
		previous_end = end;
	}
	if (!RV2MakeAlign(previous_end, &total) || total != context->size ||
		!RV2MakeZero(bytes + (size_t)previous_end, total - previous_end) ||
		RV2MakeCRC32(bytes + RV2M_HEADER_BYTES,
			context->size - (size_t)RV2M_HEADER_BYTES) !=
			RV2MakeReadU32(bytes + 40U))
		return 0;
	return 1;
}

static int RV2MakeValidateModel(rv2m_context_t *context)
{
	const unsigned char *model = RV2MakeRecord(context, 0U, 0U);
	uint32_t cells = context->section[5].count;
	uint32_t portals = context->section[6].count;

	if (RV2MakeReadU16(model) != 2U || RV2MakeReadU16(model + 2U) != 0U ||
		RV2MakeReadU32(model + 4U) != UINT32_C(0x32554e52) ||
		RV2MakeReadU32(model + 8U) != 7U ||
		RV2MakeReadU32(model + 12U) != 0U ||
		RV2MakeReadU32(model + 184U) != 0U ||
		RV2MakeReadU32(model + 188U) != 0U ||
		RV2MakeReadU32(model + 252U) != 0U)
		return 0;
	context->source = RV2MakeReadU64(model + 40U);
	if (context->source == 0U || context->source == RV2M_NONE ||
		RV2MakeReadU64(model + 16U) == 0U ||
		RV2MakeReadU64(model + 24U) == 0U ||
		RV2MakeReadU64(model + 32U) == 0U ||
		RV2MakeReadU64(model + 48U) == 0U ||
		RV2MakeReadU64(model + 56U) == 0U ||
		RV2MakeReadU32(model + 160U) != cells ||
		RV2MakeReadU32(model + 164U) != portals ||
		RV2MakeReadU32(model + 168U) != cells ||
		RV2MakeReadU32(model + 172U) != portals || cells == 0U)
		return 0;
	return 1;
}

static int RV2MakeValidateIdentities(const rv2m_context_t *context)
{
	uint32_t section;

	for (section = 0U; section < RV2M_SECTION_COUNT; section++)
	{
		uint32_t index;
		uint32_t domain = rv2m_domains[section];

		if (domain == 0U)
			continue;
		for (index = 0U; index < context->section[section].count; index++)
		{
			const unsigned char *record = RV2MakeRecord(context, section, index);
			rv2m_id_t id = RV2MakeId(record);
			uint64_t source = RV2MakeReadU64(record + 24U);
			uint32_t order_domain = RV2MakeReadU32(record + 32U);
			uint32_t source_index = RV2MakeReadU32(record + 36U);
			uint32_t ordinal = RV2MakeReadU32(record + 40U);
			uint32_t variant = RV2MakeReadU32(record + 44U);

			if (id.source != context->source || source != context->source ||
				order_domain != domain || source_index == UINT32_MAX ||
				ordinal == UINT32_MAX || variant == UINT32_MAX ||
				id.high != ((uint64_t)domain << 32 | source_index) ||
				id.low != ((uint64_t)ordinal << 32 | variant))
				return 0;
		}
	}
	return 1;
}

static int RV2MakeValidateSpans(const rv2m_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[5].count; index++)
	{
		const unsigned char *record = RV2MakeRecord(context, 5U, index);

		if (!RV2MakeSpan(record + 88U, context->section[1].count, 4U, 64U) ||
			!RV2MakeSpan(record + 96U, context->section[3].count, 1U, 32U) ||
			!RV2MakeSpan(record + 104U, context->section[7].count, 0U, 128U) ||
			!RV2MakeSpan(record + 112U, context->section[8].count, 0U, 128U) ||
			!RV2MakeSpan(record + 120U, context->section[9].count, 0U, 128U) ||
			!RV2MakeSpan(record + 128U, context->section[10].count, 0U, 64U) ||
			!RV2MakeSpan(record + 136U, context->section[11].count, 0U, 64U))
			return 0;
	}
	for (index = 0U; index < context->section[6].count; index++)
	{
		const unsigned char *record = RV2MakeRecord(context, 6U, index);

		if (!RV2MakeSpan(record + 136U, context->section[2].count, 3U, 64U) ||
			!RV2MakeSpan(record + 144U, context->section[3].count, 1U, 16U))
			return 0;
	}
	for (index = 0U; index < context->section[8].count; index++)
	{
		const unsigned char *record = RV2MakeRecord(context, 8U, index);

		if (!RV2MakeSpan(record + 72U, context->section[7].count, 1U, 64U) ||
			!RV2MakeSpan(record + 80U, context->section[3].count, 1U, 32U))
			return 0;
	}
	for (index = 0U; index < context->section[11].count; index++)
		if (!RV2MakeSpan(RV2MakeRecord(context, 11U, index) + 148U,
			context->section[11].count, 0U, 64U))
			return 0;
	return 1;
}

static int RV2MakeValidateReferences(const rv2m_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[3].count; index++)
		if (!RV2MakeReference(context, 11U,
			RV2MakeId(RV2MakeRecord(context, 3U, index) + 72U), 1))
			return 0;
	for (index = 0U; index < context->section[4].count; index++)
	{
		const unsigned char *record = RV2MakeRecord(context, 4U, index);

		if (!RV2MakeReference(context, 5U, RV2MakeId(record + 48U), 0) ||
			!RV2MakeReference(context, 3U, RV2MakeId(record + 72U), 0) ||
			!RV2MakeReference(context, 3U, RV2MakeId(record + 96U), 0) ||
			RV2MakeReadU32(record + 132U) != 0U)
			return 0;
	}
	for (index = 0U; index < context->section[6].count; index++)
	{
		const unsigned char *record = RV2MakeRecord(context, 6U, index);

		if (!RV2MakeReference(context, 5U, RV2MakeId(record + 64U), 0) ||
			!RV2MakeReference(context, 5U, RV2MakeId(record + 88U), 0) ||
			!RV2MakeReference(context, 1U, RV2MakeId(record + 112U), 0))
			return 0;
	}
	for (index = 0U; index < context->section[7].count; index++)
	{
		const unsigned char *record = RV2MakeRecord(context, 7U, index);

		if (!RV2MakeReference(context, 5U, RV2MakeId(record + 64U), 0) ||
			!RV2MakeReference(context, 1U, RV2MakeId(record + 88U), 0))
			return 0;
	}
	for (index = 0U; index < context->section[8].count; index++)
		if (!RV2MakeReference(context, 5U,
			RV2MakeId(RV2MakeRecord(context, 8U, index) + 48U), 0))
			return 0;
	for (index = 0U; index < context->section[9].count; index++)
	{
		const unsigned char *record = RV2MakeRecord(context, 9U, index);

		if (!RV2MakeReference(context, 5U, RV2MakeId(record + 48U), 0) ||
			!RV2MakeReference(context, 5U, RV2MakeId(record + 72U), 0) ||
			!RV2MakeReference(context, 6U, RV2MakeId(record + 96U), 1) ||
			!RV2MakeReference(context, 8U, RV2MakeId(record + 120U), 1) ||
			!RV2MakeReference(context, 11U, RV2MakeId(record + 144U), 1) ||
			!RV2MakeReference(context, 3U, RV2MakeId(record + 168U), 0) ||
			!RV2MakeReference(context, 3U, RV2MakeId(record + 192U), 0) ||
			!RV2MakeReference(context, 4U, RV2MakeId(record + 216U), 1))
			return 0;
	}
	for (index = 0U; index < context->section[10].count; index++)
	{
		const unsigned char *record = RV2MakeRecord(context, 10U, index);

		if (!RV2MakeReference(context, 5U, RV2MakeId(record + 64U), 0) ||
			!RV2MakeReference(context, 11U, RV2MakeId(record + 136U), 1) ||
			!RV2MakeReference(context, 7U, RV2MakeId(record + 160U), 1))
			return 0;
	}
	for (index = 0U; index < context->section[11].count; index++)
	{
		const unsigned char *record = RV2MakeRecord(context, 11U, index);

		if (!RV2MakeReference(context, 5U, RV2MakeId(record + 52U), 0) ||
			!RV2MakeReference(context, 5U, RV2MakeId(record + 76U), 0) ||
			!RV2MakeReference(context, 10U, RV2MakeId(record + 100U), 1))
			return 0;
	}
	return 1;
}

static int RV2MakeValidateBinding(const rv2m_context_t *context,
	const rv2m_expected_t *expected, const unsigned char *exact_identity)
{
	const unsigned char *binding = RV2MakeRecord(context, 12U, 0U);

	return context->generation == expected->generation &&
		!RV2MakeZero(binding, RV2M_ID_BYTES) &&
		!RV2MakeZero(binding + RV2M_ID_BYTES, RV2M_ID_BYTES) &&
		RV2MakeBytesEqual(binding, expected->bsp, RV2M_ID_BYTES) &&
		RV2MakeBytesEqual(binding + RV2M_ID_BYTES, expected->schema, RV2M_ID_BYTES) &&
		!RV2MakeZero(expected->artifact, RV2M_ID_BYTES) &&
		RV2MakeBytesEqual(expected->artifact, expected->exact_artifact,
			RV2M_ID_BYTES) &&
		RV2MakeBytesEqual(exact_identity, expected->artifact, RV2M_ID_BYTES);
}

static int RV2MakeValidate(const sg_rune_v2_snapshot_view_t *snapshot,
	const rv2m_expected_t *expected, rv2m_context_t *context)
{
	memset(context, 0, sizeof(*context));
	context->bytes = snapshot->bytes;
	context->size = snapshot->size;
	return RV2MakeParseHeader(context) && RV2MakeValidateModel(context) &&
		RV2MakeValidateIdentities(context) && RV2MakeValidateSpans(context) &&
		RV2MakeValidateReferences(context) && RV2MakeValidateBinding(context,
			expected, snapshot->content_identity.bytes);
}

static int RV2MakeHexDigit(char value, unsigned char *output)
{
	if (value >= '0' && value <= '9')
		*output = (unsigned char)(value - '0');
	else if (value >= 'a' && value <= 'f')
		*output = (unsigned char)(value - 'a' + 10);
	else if (value >= 'A' && value <= 'F')
		*output = (unsigned char)(value - 'A' + 10);
	else
		return 0;
	return 1;
}

static int RV2MakeParseIdentity(const char *text, unsigned char *output)
{
	size_t index;

	if (strlen(text) != RV2M_ID_BYTES * 2U)
		return 0;
	for (index = 0U; index < RV2M_ID_BYTES; index++)
	{
		unsigned char high;
		unsigned char low;

		if (!RV2MakeHexDigit(text[index * 2U], &high) ||
			!RV2MakeHexDigit(text[index * 2U + 1U], &low))
			return 0;
		output[index] = (unsigned char)(high * 16U + low);
	}
	return 1;
}

static int RV2MakeParseU64(const char *text, uint64_t *output)
{
	uint64_t value = 0U;

	if (!text[0])
		return 0;
	for (; *text; text++)
	{
		unsigned int digit;

		if (*text < '0' || *text > '9')
			return 0;
		digit = (unsigned int)(*text - '0');
		if (value > (UINT64_MAX - digit) / 10U)
			return 0;
		value = value * 10U + digit;
	}
	*output = value;
	return 1;
}

static void RV2MakePrintIdentity(const unsigned char *identity)
{
	unsigned int index;

	for (index = 0U; index < RV2M_ID_BYTES; index++)
		(void)printf("%02x", identity[index]);
}

static void RV2MakePrintSummary(const rv2m_context_t *context)
{
	const unsigned char *binding = RV2MakeRecord(context, 12U, 0U);

	(void)printf("{\"affordances\":%" PRIu32 ",\"bsp\":\"",
		context->section[8].count);
	RV2MakePrintIdentity(binding);
	(void)printf("\",\"cells\":%" PRIu32 ",\"generation\":%" PRIu64
		",\"kernels\":%" PRIu32 ",\"landmarks\":%" PRIu32
		",\"mechanisms\":%" PRIu32 ",\"phases\":%" PRIu32
		",\"planes\":%" PRIu32 ",\"portal_vertices\":%" PRIu32
		",\"portals\":%" PRIu32 ",\"schema\":\"",
		context->section[5].count, context->generation,
		context->section[9].count, context->section[10].count,
		context->section[11].count, context->section[3].count,
		context->section[1].count, context->section[2].count,
		context->section[6].count);
	RV2MakePrintIdentity(binding + RV2M_ID_BYTES);
	(void)printf("\",\"surfaces\":%" PRIu32 ",\"transitions\":%" PRIu32
		"}\n", context->section[7].count, context->section[4].count);
}

int main(int argc, char **argv)
{
	rv2m_expected_t expected;
	rv2m_context_t context;
	sg_rune_v2_exact_snapshot_t *snapshot = NULL;
	const sg_rune_v2_snapshot_view_t *view = NULL;
	const char *path;
	int result = 1;

	if (argc != 12 || strcmp(argv[1], "--generation") != 0 ||
		strcmp(argv[3], "--bsp-id") != 0 ||
		strcmp(argv[5], "--schema-id") != 0 ||
		strcmp(argv[7], "--artifact-id") != 0 ||
		strcmp(argv[9], "--exact-artifact-id") != 0)
		return 2;
	memset(&expected, 0, sizeof(expected));
	if (!RV2MakeParseU64(argv[2], &expected.generation) ||
		!RV2MakeParseIdentity(argv[4], expected.bsp) ||
		!RV2MakeParseIdentity(argv[6], expected.schema) ||
		!RV2MakeParseIdentity(argv[8], expected.artifact) ||
		!RV2MakeParseIdentity(argv[10], expected.exact_artifact))
		return 2;
	path = argv[11];
	if (SG_RuneV2ExactSnapshotAcquireFile(path, SG_RUNE_V2_SNAPSHOT_ARTIFACT,
		&snapshot) != SG_RUNE_V2_SNAPSHOT_OK ||
		SG_RuneV2ExactSnapshotInspect(snapshot, &view) != SG_RUNE_V2_SNAPSHOT_OK)
		goto done;
	if (!RV2MakeValidate(view, &expected, &context))
		goto done;
	RV2MakePrintSummary(&context);
	result = 0;

done:
	SG_RuneV2ExactSnapshotDestroy(snapshot);
	return result;
}
