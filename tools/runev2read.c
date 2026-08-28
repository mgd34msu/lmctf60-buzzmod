/* Standalone little-endian RUNE v2 reader. No production codec dependency. */
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RV2_MAGIC UINT32_C(0x324e5552)
#define RV2_VERSION UINT16_C(2)
#define RV2_ENDIAN UINT16_C(0x0102)
#define RV2_SCHEMA_REVISION UINT32_C(3)
#define RV2_HEADER_BYTES UINT32_C(64)
#define RV2_ENTRY_BYTES UINT32_C(32)
#define RV2_SECTION_COUNT UINT32_C(13)
#define RV2_ALIGNMENT UINT64_C(8)
#define RV2_MAX_BYTES UINT64_C(4294967296)
#define RV2_ID_BYTES 32U
#define RV2_NONE UINT64_MAX

#define RV2_DOMAIN_CELL UINT32_C(1)
#define RV2_DOMAIN_PORTAL UINT32_C(2)
#define RV2_DOMAIN_PLANE UINT32_C(3)
#define RV2_DOMAIN_PHASE UINT32_C(4)
#define RV2_DOMAIN_TRANSITION UINT32_C(5)
#define RV2_DOMAIN_SURFACE UINT32_C(6)
#define RV2_DOMAIN_AFFORDANCE UINT32_C(7)
#define RV2_DOMAIN_KERNEL UINT32_C(8)
#define RV2_DOMAIN_LANDMARK UINT32_C(9)
#define RV2_DOMAIN_MECHANISM UINT32_C(10)
#define RV2_DOMAIN_COUNT UINT32_C(11)

#define RV2_CONTENTS_KNOWN UINT32_C(0x1fff)
#define RV2_CELL_SEMANTICS_KNOWN UINT32_C(0x0f)
#define RV2_SURFACE_SEMANTICS_KNOWN UINT32_C(0x1f)
#define RV2_PORTAL_FLAGS_KNOWN UINT32_C(0x0f)
#define RV2_KERNEL_FLAGS_KNOWN UINT32_C(0x1f)

typedef struct rv2_section_s
{
	uint32_t element_bytes;
	uint32_t count;
	uint64_t offset;
	uint64_t bytes;
} rv2_section_t;

typedef struct rv2_id_s
{
	uint64_t source;
	uint64_t high;
	uint64_t low;
} rv2_id_t;

typedef struct rv2_context_s
{
	const unsigned char *data;
	size_t size;
	uint64_t generation;
	uint64_t source;
	uint64_t physics_id;
	float physics[8];
	rv2_section_t section[RV2_SECTION_COUNT];
} rv2_context_t;

typedef struct rv2_expected_s
{
	uint64_t generation;
	unsigned char bsp[RV2_ID_BYTES];
	unsigned char schema[RV2_ID_BYTES];
	unsigned char artifact[RV2_ID_BYTES];
	unsigned char exact_artifact[RV2_ID_BYTES];
} rv2_expected_t;

static const uint32_t rv2_record_bytes[RV2_SECTION_COUNT] = {
	256U, 64U, 12U, 136U, 136U, 164U, 172U, 132U, 104U, 332U,
	188U, 160U, 64U
};

static const uint32_t rv2_max_counts[RV2_SECTION_COUNT] = {
	1U, 4194304U, 8388608U, 262144U, 4194304U, 1048576U,
	2097152U, 2097152U, 2097152U, 4194304U, 65536U, 65536U, 1U
};

static uint16_t ReadU16(const unsigned char *bytes)
{
	return (uint16_t)((uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8));
}

static uint32_t ReadU32(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t ReadU64(const unsigned char *bytes)
{
	uint64_t value = 0U;
	unsigned int index;

	for (index = 0U; index < 8U; index++)
		value |= (uint64_t)bytes[index] << (index * 8U);
	return value;
}

static float ReadF32(const unsigned char *bytes)
{
	uint32_t bits = ReadU32(bytes);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static uint32_t CRC32(const unsigned char *bytes, size_t size)
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

static uint64_t Align(uint64_t value)
{
	return (value + (RV2_ALIGNMENT - 1U)) & ~(RV2_ALIGNMENT - 1U);
}

static int RangeWithin(uint64_t offset, uint64_t bytes, uint64_t total)
{
	return offset <= total && bytes <= total - offset;
}

static int ZeroBytes(const unsigned char *bytes, uint64_t count)
{
	uint64_t index;

	for (index = 0U; index < count; index++)
		if (bytes[(size_t)index] != 0U)
			return 0;
	return 1;
}

static int IdEqual(rv2_id_t left, rv2_id_t right)
{
	return left.source == right.source && left.high == right.high &&
		left.low == right.low;
}

static int IdNone(rv2_id_t id)
{
	return id.source == RV2_NONE && id.high == RV2_NONE && id.low == RV2_NONE;
}

static rv2_id_t ReadId(const unsigned char *bytes)
{
	rv2_id_t id;

	id.source = ReadU64(bytes);
	id.high = ReadU64(bytes + 8U);
	id.low = ReadU64(bytes + 16U);
	return id;
}

static int IdValid(rv2_id_t id)
{
	uint32_t domain = (uint32_t)(id.high >> 32);

	return id.source != 0U && id.source != RV2_NONE &&
		domain >= RV2_DOMAIN_CELL && domain < RV2_DOMAIN_COUNT &&
		(uint32_t)id.high != UINT32_MAX &&
		(uint32_t)(id.low >> 32) != UINT32_MAX &&
		(uint32_t)id.low != UINT32_MAX;
}

static int OrderCompare(const unsigned char *left, const unsigned char *right)
{
	uint64_t left_source = ReadU64(left);
	uint64_t right_source = ReadU64(right);
	unsigned int field;

	if (left_source != right_source)
		return left_source < right_source ? -1 : 1;
	for (field = 0U; field < 4U; field++)
	{
		uint32_t left_value = ReadU32(left + 8U + field * 4U);
		uint32_t right_value = ReadU32(right + 8U + field * 4U);

		if (left_value != right_value)
			return left_value < right_value ? -1 : 1;
	}
	return 0;
}

static int RecordIdentity(const unsigned char *record, uint32_t domain,
	uint64_t source, const unsigned char *previous_order)
{
	rv2_id_t id = ReadId(record);
	const unsigned char *order = record + 24U;
	uint64_t order_source = ReadU64(order);
	uint32_t order_domain = ReadU32(order + 8U);
	uint32_t source_index = ReadU32(order + 12U);
	uint32_t ordinal = ReadU32(order + 16U);
	uint32_t variant = ReadU32(order + 20U);

	if (!IdValid(id) || order_source != source || order_domain != domain ||
		source_index == UINT32_MAX || ordinal == UINT32_MAX ||
		variant == UINT32_MAX || id.source != order_source ||
		id.high != ((uint64_t)domain << 32 | source_index) ||
		id.low != ((uint64_t)ordinal << 32 | variant))
		return 0;
	return !previous_order || OrderCompare(previous_order, order) < 0;
}

static const unsigned char *Record(const rv2_context_t *context,
	uint32_t section_index, uint32_t record_index)
{
	const rv2_section_t *section = &context->section[section_index];
	return context->data + (size_t)section->offset +
		(size_t)record_index * section->element_bytes;
}

static int FiniteValues(const unsigned char *bytes, unsigned int count)
{
	unsigned int index;

	for (index = 0U; index < count; index++)
		if (!isfinite(ReadF32(bytes + index * 4U)))
			return 0;
	return 1;
}

static int IntervalValid(const unsigned char *bytes, int nonnegative)
{
	float minimum;
	float maximum;

	if (!FiniteValues(bytes, 2U))
		return 0;
	minimum = ReadF32(bytes);
	maximum = ReadF32(bytes + 4U);
	return minimum <= maximum && (!nonnegative || minimum >= 0.0f);
}

static int Interval3Valid(const unsigned char *bytes, int nonnegative)
{
	return IntervalValid(bytes, nonnegative) &&
		IntervalValid(bytes + 8U, nonnegative) &&
		IntervalValid(bytes + 16U, nonnegative);
}

static int BoundsValid(const unsigned char *bytes)
{
	unsigned int axis;

	if (!FiniteValues(bytes, 6U))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (ReadF32(bytes + axis * 4U) >=
			ReadF32(bytes + 12U + axis * 4U))
			return 0;
	return 1;
}

static int SpanValid(const unsigned char *bytes, uint32_t total,
	uint32_t minimum, uint32_t maximum)
{
	uint32_t first = ReadU32(bytes);
	uint32_t count = ReadU32(bytes + 4U);

	return count >= minimum && count <= maximum && first <= total &&
		count <= total - first;
}

static int FindId(const rv2_context_t *context, uint32_t section_index,
	rv2_id_t target, uint32_t *index_out)
{
	uint32_t index;

	if (!IdValid(target))
		return 0;
	for (index = 0U; index < context->section[section_index].count; index++)
		if (IdEqual(ReadId(Record(context, section_index, index)), target))
		{
			if (index_out)
				*index_out = index;
			return 1;
		}
	return 0;
}

static int RefValid(const rv2_context_t *context, uint32_t section_index,
	rv2_id_t target, int optional)
{
	return (optional && IdNone(target)) || FindId(context, section_index,
		target, NULL);
}

static int ParseHeader(rv2_context_t *context)
{
	const unsigned char *data = context->data;
	uint64_t total;
	uint64_t previous_end;
	uint32_t index;
	unsigned char header[RV2_HEADER_BYTES];

	if (context->size < RV2_HEADER_BYTES)
		return 0;
	if (ReadU32(data) != RV2_MAGIC || ReadU16(data + 4U) != RV2_VERSION ||
		ReadU16(data + 6U) != RV2_ENDIAN ||
		ReadU16(data + 8U) != RV2_HEADER_BYTES ||
		ReadU16(data + 10U) != RV2_ENTRY_BYTES ||
		ReadU32(data + 12U) != RV2_SECTION_COUNT ||
		ReadU32(data + 16U) != 0U ||
		ReadU32(data + 20U) != RV2_SCHEMA_REVISION ||
		!ZeroBytes(data + 48U, 16U))
		return 0;
	context->generation = ReadU64(data + 24U);
	total = ReadU64(data + 32U);
	if (context->generation == 0U || total != context->size ||
		total > RV2_MAX_BYTES)
		return 0;
	memcpy(header, data, sizeof(header));
	memset(header + 44U, 0, 4U);
	if (CRC32(header, sizeof(header)) != ReadU32(data + 44U))
		return 0;
	previous_end = RV2_HEADER_BYTES + RV2_SECTION_COUNT * RV2_ENTRY_BYTES;
	if (previous_end > total)
		return 0;
	for (index = 0U; index < RV2_SECTION_COUNT; index++)
	{
		const unsigned char *entry = data + RV2_HEADER_BYTES +
			(size_t)index * RV2_ENTRY_BYTES;
		rv2_section_t *section = &context->section[index];
		uint64_t expected_offset = Align(previous_end);

		section->element_bytes = ReadU32(entry + 4U);
		section->count = ReadU32(entry + 8U);
		section->offset = ReadU64(entry + 16U);
		section->bytes = ReadU64(entry + 24U);
		if (ReadU16(entry) != (uint16_t)(index + 1U) ||
			ReadU16(entry + 2U) != 1U ||
			section->element_bytes != rv2_record_bytes[index] ||
			section->count > rv2_max_counts[index] ||
			((index == 0U || index == 12U) && section->count != 1U) ||
			(uint64_t)section->element_bytes * section->count != section->bytes ||
			section->offset != expected_offset ||
			!RangeWithin(section->offset, section->bytes, total) ||
			!ZeroBytes(data + (size_t)previous_end,
				section->offset - previous_end) ||
			CRC32(data + (size_t)section->offset, (size_t)section->bytes) !=
				ReadU32(entry + 12U))
			return 0;
		previous_end = section->offset + section->bytes;
	}
	if (total != Align(previous_end) ||
		!ZeroBytes(data + (size_t)previous_end, total - previous_end) ||
		CRC32(data + RV2_HEADER_BYTES, context->size - RV2_HEADER_BYTES) !=
			ReadU32(data + 40U))
		return 0;
	return 1;
}

static int ValidateModel(rv2_context_t *context)
{
	const unsigned char *model = Record(context, 0U, 0U);
	uint64_t producer;
	uint32_t cells = context->section[5].count;
	uint32_t portals = context->section[6].count;
	unsigned int index;

	if (ReadU16(model) != 2U || ReadU16(model + 2U) != 0U ||
		ReadU32(model + 4U) != UINT32_C(0x32554e52) ||
		ReadU32(model + 8U) != 7U || ReadU32(model + 12U) != 0U ||
		ReadU32(model + 188U) != 0U || ReadU32(model + 252U) != 0U)
		return 0;
	context->physics_id = ReadU64(model + 32U);
	context->source = ReadU64(model + 40U);
	producer = ReadU64(model + 56U);
	if (ReadU64(model + 16U) == 0U || ReadU64(model + 24U) == 0U ||
		context->physics_id == 0U || context->source == 0U ||
		context->source == UINT64_MAX || ReadU64(model + 48U) == 0U ||
		producer == 0U || !BoundsValid(model + 64U) ||
		!BoundsValid(model + 88U) || !FiniteValues(model + 112U, 8U))
		return 0;
	for (index = 0U; index < 8U; index++)
		context->physics[index] = ReadF32(model + 112U + index * 4U);
	for (index = 0U; index < 7U; index++)
		if (context->physics[index] < 0.0f)
			return 0;
	if (context->physics[7] <= 0.0f || ReadU32(model + 144U) == 0U ||
		ReadU32(model + 148U) == 0U ||
		ReadU32(model + 148U) > ReadU32(model + 144U))
		return 0;
	if (ReadU32(model + 152U) != 2U || ReadU32(model + 156U) != 0U ||
		ReadU32(model + 160U) != cells || ReadU32(model + 164U) != portals ||
		ReadU32(model + 168U) != cells || ReadU32(model + 172U) != portals ||
		cells == 0U || ReadU32(model + 176U) != UINT32_MAX)
		return 0;
	if (ReadU32(model + 180U) != 1U || ReadU32(model + 184U) != 0U ||
		ReadU64(model + 192U) == 0U || ReadU64(model + 192U) == producer ||
		ReadU64(model + 200U) != ReadU64(model + 16U) ||
		ReadU64(model + 208U) != context->source ||
		ReadU64(model + 216U) == 0U || ReadU32(model + 224U) == 0U ||
		ReadU32(model + 228U) != cells || ReadU32(model + 232U) != portals ||
		ReadU32(model + 236U) != 0U || ReadU32(model + 240U) != 0U ||
		ReadU32(model + 244U) != 0U || ReadU32(model + 248U) != 0U)
		return 0;
	return 1;
}

static int ValidateIdentityArrays(const rv2_context_t *context)
{
	static const uint32_t domains[RV2_SECTION_COUNT] = {
		0U, RV2_DOMAIN_PLANE, 0U, RV2_DOMAIN_PHASE,
		RV2_DOMAIN_TRANSITION, RV2_DOMAIN_CELL, RV2_DOMAIN_PORTAL,
		RV2_DOMAIN_SURFACE, RV2_DOMAIN_AFFORDANCE, RV2_DOMAIN_KERNEL,
		RV2_DOMAIN_LANDMARK, RV2_DOMAIN_MECHANISM, 0U
	};
	uint32_t section_index;

	for (section_index = 0U; section_index < RV2_SECTION_COUNT; section_index++)
	{
		const unsigned char *previous = NULL;
		uint32_t index;

		if (domains[section_index] == 0U)
			continue;
		for (index = 0U; index < context->section[section_index].count; index++)
		{
			const unsigned char *record = Record(context, section_index, index);

			if (!RecordIdentity(record, domains[section_index], context->source,
				previous))
				return 0;
			previous = record + 24U;
		}
	}
	return 1;
}

static int ValidatePlanes(const rv2_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[1].count; index++)
	{
		const unsigned char *record = Record(context, 1U, index);
		double x;
		double y;
		double z;

		if (!FiniteValues(record + 48U, 4U))
			return 0;
		x = ReadF32(record + 48U);
		y = ReadF32(record + 52U);
		z = ReadF32(record + 56U);
		if (!isfinite(x * x + y * y + z * z) || x * x + y * y + z * z <= 0.0)
			return 0;
	}
	return 1;
}

static int ValidatePhases(const rv2_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[3].count; index++)
	{
		const unsigned char *record = Record(context, 3U, index);
		uint32_t stance = ReadU32(record + 48U);
		uint32_t motion = ReadU32(record + 52U);
		uint32_t support = ReadU32(record + 56U);
		uint32_t medium = ReadU32(record + 60U);
		uint32_t frame = ReadU32(record + 68U);
		rv2_id_t mover = ReadId(record + 72U);

		if (stance >= 2U || motion >= 3U || support >= 3U || medium >= 4U ||
			ReadU32(record + 64U) >= 2U || frame >= 2U ||
			!Interval3Valid(record + 96U, 0) ||
			!IntervalValid(record + 120U, 1) ||
			ReadU32(record + 128U) == 0U ||
			ReadU32(record + 132U) < ReadU32(record + 128U))
			return 0;
		if ((motion == 0U && support == 0U) ||
			(motion == 1U && support != 0U) ||
			(motion == 2U && ((medium < 1U || medium > 3U) || support != 0U)) ||
			(support == 2U && frame != 1U))
			return 0;
		if ((frame == 0U && !IdNone(mover)) ||
			(frame == 1U && (IdNone(mover) || (uint32_t)(mover.high >> 32) !=
				RV2_DOMAIN_MECHANISM || support != 2U)))
			return 0;
	}
	return 1;
}

static int GeometryValid(const unsigned char *bytes, uint64_t source)
{
	return ReadU64(bytes) == source && ReadU32(bytes + 8U) != UINT32_MAX &&
		ReadU32(bytes + 12U) != UINT32_MAX;
}

static int ValidateCells(const rv2_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[5].count; index++)
	{
		const unsigned char *record = Record(context, 5U, index);

		if (!GeometryValid(record + 48U, context->source) ||
			!BoundsValid(record + 64U) ||
			!SpanValid(record + 88U, context->section[1].count, 4U, 64U) ||
			!SpanValid(record + 96U, context->section[3].count, 1U, 32U) ||
			!SpanValid(record + 104U, context->section[7].count, 0U, 128U) ||
			!SpanValid(record + 112U, context->section[8].count, 0U, 128U) ||
			!SpanValid(record + 120U, context->section[9].count, 0U, 128U) ||
			!SpanValid(record + 128U, context->section[10].count, 0U, 64U) ||
			!SpanValid(record + 136U, context->section[11].count, 0U, 64U) ||
			ReadU32(record + 144U) == UINT32_MAX ||
			ReadU32(record + 148U) == UINT32_MAX ||
			ReadU32(record + 152U) == UINT32_MAX ||
			(ReadU32(record + 156U) & ~RV2_CONTENTS_KNOWN) != 0U ||
			(ReadU32(record + 160U) & ~RV2_CELL_SEMANTICS_KNOWN) != 0U)
			return 0;
	}
	return 1;
}

static int PhaseInCell(const rv2_context_t *context, uint32_t cell_index,
	uint32_t phase_index)
{
	const unsigned char *cell = Record(context, 5U, cell_index);
	uint32_t first = ReadU32(cell + 96U);
	uint32_t count = ReadU32(cell + 100U);

	return phase_index >= first && phase_index - first < count;
}

static int IntervalEqual(const unsigned char *left, const unsigned char *right)
{
	return ReadF32(left) == ReadF32(right) &&
		ReadF32(left + 4U) == ReadF32(right + 4U);
}

static int Interval3Equal(const unsigned char *left, const unsigned char *right)
{
	return IntervalEqual(left, right) &&
		IntervalEqual(left + 8U, right + 8U) &&
		IntervalEqual(left + 16U, right + 16U);
}

static int PhaseDiscreteEqual(const unsigned char *left,
	const unsigned char *right)
{
	return ReadU32(left + 48U) == ReadU32(right + 48U) &&
		ReadU32(left + 52U) == ReadU32(right + 52U) &&
		ReadU32(left + 56U) == ReadU32(right + 56U) &&
		ReadU32(left + 60U) == ReadU32(right + 60U) &&
		ReadU32(left + 64U) == ReadU32(right + 64U) &&
		ReadU32(left + 68U) == ReadU32(right + 68U) &&
		IdEqual(ReadId(left + 72U), ReadId(right + 72U));
}

static int PhaseClockEqual(const unsigned char *left,
	const unsigned char *right)
{
	return ReadU32(left + 128U) == ReadU32(right + 128U) &&
		ReadU32(left + 132U) == ReadU32(right + 132U);
}

static int PhaseEqualExceptStance(const unsigned char *left,
	const unsigned char *right)
{
	return ReadU32(left + 52U) == ReadU32(right + 52U) &&
		ReadU32(left + 56U) == ReadU32(right + 56U) &&
		ReadU32(left + 60U) == ReadU32(right + 60U) &&
		ReadU32(left + 64U) == ReadU32(right + 64U) &&
		ReadU32(left + 68U) == ReadU32(right + 68U) &&
		IdEqual(ReadId(left + 72U), ReadId(right + 72U)) &&
		Interval3Equal(left + 96U, right + 96U) &&
		IntervalEqual(left + 120U, right + 120U) &&
		PhaseClockEqual(left, right);
}

static int TransitionSemanticsValid(uint32_t kind,
	const unsigned char *source, const unsigned char *destination)
{
	if (ReadU32(source + 60U) != ReadU32(destination + 60U))
		return 0;
	switch (kind)
	{
	case 1U:
		return PhaseEqualExceptStance(source, destination) &&
			ReadU32(source + 48U) != ReadU32(destination + 48U);
	case 2U:
		return PhaseDiscreteEqual(source, destination) &&
			PhaseClockEqual(source, destination) &&
			!Interval3Equal(source + 96U, destination + 96U) &&
			IntervalEqual(source + 120U, destination + 120U);
	case 3U:
		return PhaseDiscreteEqual(source, destination) &&
			PhaseClockEqual(source, destination) &&
			Interval3Equal(source + 96U, destination + 96U) &&
			!IntervalEqual(source + 120U, destination + 120U);
	case 4U:
		return PhaseDiscreteEqual(source, destination) &&
			PhaseClockEqual(source, destination) && ReadU32(source + 56U) == 2U &&
			Interval3Equal(source + 96U, destination + 96U) &&
			!IntervalEqual(source + 120U, destination + 120U);
	case 5U:
		return ReadU32(source + 52U) == 0U && ReadU32(source + 56U) != 0U &&
			ReadU32(destination + 52U) == 1U &&
			ReadU32(destination + 56U) == 0U &&
			ReadU32(source + 48U) == ReadU32(destination + 48U) &&
			ReadU32(source + 64U) == ReadU32(destination + 64U) &&
			PhaseClockEqual(source, destination) &&
			ReadU32(destination + 68U) == 0U &&
			IdNone(ReadId(destination + 72U));
	case 6U:
		return ReadU32(source + 52U) == 1U &&
			ReadU32(destination + 52U) == 1U &&
			PhaseDiscreteEqual(source, destination) &&
			PhaseClockEqual(source, destination) &&
			(!Interval3Equal(source + 96U, destination + 96U) ||
			 !IntervalEqual(source + 120U, destination + 120U));
	case 7U:
		return ReadU32(source + 52U) == 1U && ReadU32(source + 56U) == 0U &&
			ReadU32(destination + 52U) == 0U &&
			ReadU32(destination + 56U) != 0U &&
			ReadU32(source + 48U) == ReadU32(destination + 48U) &&
			ReadU32(source + 64U) == ReadU32(destination + 64U) &&
			PhaseClockEqual(source, destination);
	default:
		return 0;
	}
}

static int ValidateTransitions(const rv2_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[4].count; index++)
	{
		const unsigned char *record = Record(context, 4U, index);
		uint32_t cell;
		uint32_t source_phase;
		uint32_t destination_phase;

		if (!FindId(context, 5U, ReadId(record + 48U), &cell) ||
			!FindId(context, 3U, ReadId(record + 72U), &source_phase) ||
			!FindId(context, 3U, ReadId(record + 96U), &destination_phase) ||
			source_phase == destination_phase || ReadU32(record + 120U) < 1U ||
			ReadU32(record + 120U) >= 8U ||
			!IntervalValid(record + 124U, 1) ||
			ReadF32(record + 128U) <= 0.0f || ReadU32(record + 132U) != 0U ||
			!PhaseInCell(context, cell, source_phase) ||
			!PhaseInCell(context, cell, destination_phase) ||
			!TransitionSemanticsValid(ReadU32(record + 120U),
				Record(context, 3U, source_phase),
				Record(context, 3U, destination_phase)))
			return 0;
	}
	return 1;
}

static int ValidatePortals(const rv2_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[6].count; index++)
	{
		const unsigned char *record = Record(context, 6U, index);
		rv2_id_t from = ReadId(record + 64U);
		rv2_id_t to = ReadId(record + 88U);
		uint32_t first_vertex = ReadU32(record + 136U);
		uint32_t vertex_count = ReadU32(record + 140U);
		uint32_t vertex;
		uint32_t flags = ReadU32(record + 168U);

		if (!GeometryValid(record + 48U, context->source) ||
			!RefValid(context, 5U, from, 0) || !RefValid(context, 5U, to, 0) ||
			IdEqual(from, to) || !RefValid(context, 1U, ReadId(record + 112U), 0) ||
			!SpanValid(record + 136U, context->section[2].count, 3U, 64U) ||
			!SpanValid(record + 144U, context->section[3].count, 1U, 16U) ||
			ReadU32(record + 152U) >= 3U || !FiniteValues(record + 156U, 1U) ||
			ReadF32(record + 156U) < 0.0f || (flags & 1U) == 0U ||
			(flags & ~RV2_PORTAL_FLAGS_KNOWN) != 0U ||
			(ReadU32(record + 160U) & ~RV2_CONTENTS_KNOWN) != 0U ||
			(ReadU32(record + 164U) & ~RV2_CONTENTS_KNOWN) != 0U ||
			((flags & 2U) == 0U && ReadU32(record + 160U) !=
				ReadU32(record + 164U)))
			return 0;
		for (vertex = 0U; vertex < vertex_count; vertex++)
			if (!FiniteValues(Record(context, 2U, first_vertex + vertex), 3U))
				return 0;
	}
	return 1;
}

static int EntityValid(const unsigned char *bytes)
{
	return (ReadU32(bytes) == UINT32_MAX) ==
		(ReadU32(bytes + 4U) == UINT32_MAX);
}

static int ValidateSurfacesAndAffordances(const rv2_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[7].count; index++)
	{
		const unsigned char *record = Record(context, 7U, index);

		if (!GeometryValid(record + 48U, context->source) ||
			!RefValid(context, 5U, ReadId(record + 64U), 0) ||
			!RefValid(context, 1U, ReadId(record + 88U), 0) ||
			!FiniteValues(record + 112U, 3U) ||
			(ReadU32(record + 124U) & ~RV2_CONTENTS_KNOWN) != 0U ||
			(ReadU32(record + 128U) & ~RV2_SURFACE_SEMANTICS_KNOWN) != 0U)
			return 0;
	}
	for (index = 0U; index < context->section[8].count; index++)
	{
		const unsigned char *record = Record(context, 8U, index);

		if (!RefValid(context, 5U, ReadId(record + 48U), 0) ||
			!SpanValid(record + 72U, context->section[7].count, 1U, 64U) ||
			!SpanValid(record + 80U, context->section[3].count, 1U, 32U) ||
			ReadU32(record + 88U) >= 7U || !IntervalValid(record + 92U, 1))
			return 0;
	}
	return 1;
}

static int ValidateMechanisms(const rv2_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[11].count; index++)
	{
		const unsigned char *record = Record(context, 11U, index);
		rv2_id_t entry = ReadId(record + 52U);
		rv2_id_t exit_cell = ReadId(record + 76U);
		rv2_id_t activation = ReadId(record + 100U);

		if (ReadU32(record + 48U) >= 8U ||
			!RefValid(context, 5U, entry, 0) ||
			!RefValid(context, 5U, exit_cell, 0) || IdEqual(entry, exit_cell) ||
			!EntityValid(record + 124U) || !IntervalValid(record + 132U, 1) ||
			!IntervalValid(record + 140U, 1) ||
			!SpanValid(record + 148U, context->section[11].count, 0U, 64U) ||
			(!IdNone(activation) && !RefValid(context, 10U, activation, 0)) ||
			(IdNone(activation) && ReadU32(record + 124U) == UINT32_MAX))
			return 0;
	}
	return 1;
}

static int ValidatePhaseMovers(const rv2_context_t *context)
{
	uint32_t phase_index;

	for (phase_index = 0U; phase_index < context->section[3].count;
		phase_index++)
	{
		const unsigned char *phase = Record(context, 3U, phase_index);
		uint32_t mechanism_index;
		uint32_t entry;
		uint32_t exit_cell;
		const unsigned char *mechanism;

		if (ReadU32(phase + 68U) != 1U)
			continue;
		if (!FindId(context, 11U, ReadId(phase + 72U), &mechanism_index))
			return 0;
		mechanism = Record(context, 11U, mechanism_index);
		if (!FindId(context, 5U, ReadId(mechanism + 52U), &entry) ||
			!FindId(context, 5U, ReadId(mechanism + 76U), &exit_cell) ||
			(!PhaseInCell(context, entry, phase_index) &&
			 !PhaseInCell(context, exit_cell, phase_index)))
			return 0;
	}
	return 1;
}

static int PointInside(const unsigned char *point, const unsigned char *bounds)
{
	unsigned int axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float value = ReadF32(point + axis * 4U);

		if (value < ReadF32(bounds + axis * 4U) ||
			value > ReadF32(bounds + 12U + axis * 4U))
			return 0;
	}
	return 1;
}

static int ValidateLandmarks(const rv2_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[10].count; index++)
	{
		const unsigned char *record = Record(context, 10U, index);
		uint32_t cell;
		uint32_t surface;

		if (!GeometryValid(record + 48U, context->source) ||
			!FindId(context, 5U, ReadId(record + 64U), &cell) ||
			!EntityValid(record + 88U) || ReadU32(record + 96U) >= 9U ||
			!FiniteValues(record + 100U, 3U) || !BoundsValid(record + 112U) ||
			!PointInside(record + 100U, record + 112U) ||
			!PointInside(record + 100U, Record(context, 5U, cell) + 64U) ||
			!RefValid(context, 11U, ReadId(record + 136U), 1) ||
			!RefValid(context, 7U, ReadId(record + 160U), 1))
			return 0;
		if (!IdNone(ReadId(record + 160U)))
		{
			if (!FindId(context, 7U, ReadId(record + 160U), &surface) ||
				!IdEqual(ReadId(Record(context, 7U, surface) + 64U),
					ReadId(record + 64U)))
				return 0;
		}
	}
	return 1;
}

static int PortalAllows(const rv2_context_t *context, uint32_t portal_index,
	rv2_id_t source, rv2_id_t destination)
{
	const unsigned char *portal = Record(context, 6U, portal_index);
	int forward = IdEqual(ReadId(portal + 64U), source) &&
		IdEqual(ReadId(portal + 88U), destination);
	int reverse = IdEqual(ReadId(portal + 88U), source) &&
		IdEqual(ReadId(portal + 64U), destination);
	uint32_t direction = ReadU32(portal + 152U);

	return (forward || reverse) && (direction != 1U || forward) &&
		(direction != 2U || reverse);
}

static int ValidateKernels(const rv2_context_t *context)
{
	uint32_t index;

	for (index = 0U; index < context->section[9].count; index++)
	{
		const unsigned char *record = Record(context, 9U, index);
		rv2_id_t source_cell = ReadId(record + 48U);
		rv2_id_t destination_cell = ReadId(record + 72U);
		rv2_id_t boundary = ReadId(record + 96U);
		rv2_id_t mechanism = ReadId(record + 144U);
		rv2_id_t transition = ReadId(record + 216U);
		uint32_t source_cell_index;
		uint32_t destination_cell_index;
		uint32_t source_phase;
		uint32_t destination_phase;
		uint32_t portal;
		uint32_t family = ReadU32(record + 240U);
		uint32_t flags = ReadU32(record + 328U);
		uint32_t source_medium;
		uint32_t destination_medium;
		int water;
		float acceleration_limit;

		if (!FindId(context, 5U, source_cell, &source_cell_index) ||
			!FindId(context, 5U, destination_cell, &destination_cell_index) ||
			!FindId(context, 3U, ReadId(record + 168U), &source_phase) ||
			!FindId(context, 3U, ReadId(record + 192U), &destination_phase) ||
			!PhaseInCell(context, source_cell_index, source_phase) ||
			!PhaseInCell(context, destination_cell_index, destination_phase) ||
			!RefValid(context, 8U, ReadId(record + 120U), 1) ||
			!RefValid(context, 11U, mechanism, 1))
			return 0;
		if (IdEqual(source_cell, destination_cell))
		{
			uint32_t transition_index;
			const unsigned char *transition_record;

			if (!IdNone(boundary) || IdNone(transition) ||
				!FindId(context, 4U, transition, &transition_index))
				return 0;
			transition_record = Record(context, 4U, transition_index);
			if (!IdEqual(ReadId(transition_record + 48U), source_cell) ||
				!IdEqual(ReadId(transition_record + 72U), ReadId(record + 168U)) ||
				!IdEqual(ReadId(transition_record + 96U), ReadId(record + 192U)))
				return 0;
		}
		else if (!IdNone(transition) || !FindId(context, 6U, boundary, &portal) ||
			!PortalAllows(context, portal, source_cell, destination_cell))
			return 0;
		if (family >= 6U || ReadU32(record + 244U) >= 6U ||
			(flags & ~RV2_KERNEL_FLAGS_KNOWN) != 0U || (flags & 0x13U) != 0x13U ||
			!Interval3Valid(record + 248U, 0) ||
			!IntervalValid(record + 272U, 1) || ReadF32(record + 276U) <= 0.0f ||
			!IntervalValid(record + 280U, 1) ||
			!IntervalValid(record + 288U, 1) ||
			!IntervalValid(record + 296U, 1) ||
			!FiniteValues(record + 304U, 2U) ||
			ReadU64(record + 312U) != context->physics_id ||
			ReadF32(record + 304U) != context->physics[0] ||
			ReadF32(record + 284U) > context->physics[7])
			return 0;
		acceleration_limit = family == 0U ? context->physics[1] :
			family == 1U ? context->physics[2] :
			family == 2U ? context->physics[3] :
			family == 3U ? context->physics[4] : context->physics[5];
		if (ReadF32(record + 292U) > acceleration_limit ||
			ReadF32(record + 300U) > acceleration_limit ||
			(family == 4U && IdNone(mechanism)))
			return 0;
		source_medium = ReadU32(Record(context, 3U, source_phase) + 60U);
		destination_medium = ReadU32(Record(context, 3U, destination_phase) + 60U);
		water = (source_medium >= 1U && source_medium <= 3U) ||
			(destination_medium >= 1U && destination_medium <= 3U);
		if (ReadF32(record + 308U) != (water ? context->physics[6] : 0.0f) ||
			(((flags & 4U) != 0U) != (source_medium != destination_medium)) ||
			((flags & 8U) != 0U &&
			 ReadU32(Record(context, 3U, source_phase) + 56U) == 0U) ||
			(family == 2U && !water))
			return 0;
		if (!IdEqual(source_cell, destination_cell) &&
			source_medium != destination_medium &&
			(ReadU32(Record(context, 6U, portal) + 168U) & 2U) == 0U)
			return 0;
	}
	return 1;
}

static int SpanContains(const unsigned char *span, uint32_t index)
{
	uint32_t first = ReadU32(span);
	uint32_t count = ReadU32(span + 4U);

	return index >= first && index - first < count;
}

static int ValidateOwnership(const rv2_context_t *context)
{
	uint32_t cell_index;
	uint32_t record_index;

	for (cell_index = 0U; cell_index < context->section[5].count; cell_index++)
	{
		const unsigned char *cell = Record(context, 5U, cell_index);
		rv2_id_t cell_id = ReadId(cell);
		uint32_t first;
		uint32_t count;
		uint32_t index;

		first = ReadU32(cell + 104U);
		count = ReadU32(cell + 108U);
		for (index = first; index < first + count; index++)
			if (!IdEqual(ReadId(Record(context, 7U, index) + 64U), cell_id))
				return 0;
		first = ReadU32(cell + 112U);
		count = ReadU32(cell + 116U);
		for (index = first; index < first + count; index++)
			if (!IdEqual(ReadId(Record(context, 8U, index) + 48U), cell_id))
				return 0;
		first = ReadU32(cell + 120U);
		count = ReadU32(cell + 124U);
		for (index = first; index < first + count; index++)
			if (!IdEqual(ReadId(Record(context, 9U, index) + 48U), cell_id))
				return 0;
		first = ReadU32(cell + 128U);
		count = ReadU32(cell + 132U);
		for (index = first; index < first + count; index++)
			if (!IdEqual(ReadId(Record(context, 10U, index) + 64U), cell_id))
				return 0;
		first = ReadU32(cell + 136U);
		count = ReadU32(cell + 140U);
		for (index = first; index < first + count; index++)
		{
			const unsigned char *mechanism = Record(context, 11U, index);

			if (!IdEqual(ReadId(mechanism + 52U), cell_id) &&
				!IdEqual(ReadId(mechanism + 76U), cell_id))
				return 0;
		}
	}
	for (record_index = 0U; record_index < context->section[7].count;
		record_index++)
	{
		uint32_t owner;
		if (!FindId(context, 5U,
			ReadId(Record(context, 7U, record_index) + 64U), &owner) ||
			!SpanContains(Record(context, 5U, owner) + 104U, record_index))
			return 0;
	}
	for (record_index = 0U; record_index < context->section[8].count;
		record_index++)
	{
		uint32_t owner;
		if (!FindId(context, 5U,
			ReadId(Record(context, 8U, record_index) + 48U), &owner) ||
			!SpanContains(Record(context, 5U, owner) + 112U, record_index))
			return 0;
	}
	for (record_index = 0U; record_index < context->section[9].count;
		record_index++)
	{
		uint32_t owner;
		if (!FindId(context, 5U,
			ReadId(Record(context, 9U, record_index) + 48U), &owner) ||
			!SpanContains(Record(context, 5U, owner) + 120U, record_index))
			return 0;
	}
	for (record_index = 0U; record_index < context->section[10].count;
		record_index++)
	{
		uint32_t owner;
		if (!FindId(context, 5U,
			ReadId(Record(context, 10U, record_index) + 64U), &owner) ||
			!SpanContains(Record(context, 5U, owner) + 128U, record_index))
			return 0;
	}
	return 1;
}

static int ValidateBinding(const rv2_context_t *context,
	const rv2_expected_t *expected)
{
	const unsigned char *binding = Record(context, 12U, 0U);

	return context->generation == expected->generation &&
		!ZeroBytes(binding, RV2_ID_BYTES) &&
		!ZeroBytes(binding + RV2_ID_BYTES, RV2_ID_BYTES) &&
		memcmp(binding, expected->bsp, RV2_ID_BYTES) == 0 &&
		memcmp(binding + RV2_ID_BYTES, expected->schema, RV2_ID_BYTES) == 0 &&
		!ZeroBytes(expected->exact_artifact, RV2_ID_BYTES) &&
		memcmp(expected->artifact, expected->exact_artifact, RV2_ID_BYTES) == 0;
}

static int Validate(const unsigned char *data, size_t size,
	const rv2_expected_t *expected, rv2_context_t *context)
{
	memset(context, 0, sizeof(*context));
	context->data = data;
	context->size = size;
	return ParseHeader(context) && ValidateModel(context) &&
		ValidateIdentityArrays(context) && ValidatePlanes(context) &&
		ValidatePhases(context) && ValidateCells(context) &&
		ValidateTransitions(context) && ValidatePortals(context) &&
		ValidateSurfacesAndAffordances(context) && ValidateMechanisms(context) &&
		ValidatePhaseMovers(context) &&
		ValidateLandmarks(context) && ValidateKernels(context) &&
		ValidateOwnership(context) &&
		ValidateBinding(context, expected);
}

static int HexDigit(char value, unsigned char *output)
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

static int ParseIdentity(const char *text, unsigned char output[RV2_ID_BYTES])
{
	size_t index;

	if (strlen(text) != RV2_ID_BYTES * 2U)
		return 0;
	for (index = 0U; index < RV2_ID_BYTES; index++)
	{
		unsigned char high;
		unsigned char low;

		if (!HexDigit(text[index * 2U], &high) ||
			!HexDigit(text[index * 2U + 1U], &low))
			return 0;
		output[index] = (unsigned char)(high * 16U + low);
	}
	return 1;
}

static int ParseU64(const char *text, uint64_t *output)
{
	char *end;
	uintmax_t value;

	errno = 0;
	value = strtoumax(text, &end, 10);
	if (errno != 0 || *text == '\0' || *text == '-' || *end != '\0' ||
		value > UINT64_MAX)
		return 0;
	*output = (uint64_t)value;
	return 1;
}

static int ReadFile(const char *path, unsigned char **bytes_out,
	size_t *size_out)
{
	FILE *file;
	long length;
	unsigned char *bytes;
	size_t size;

	file = fopen(path, "rb");
	if (!file)
		return 0;
	if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 0L ||
		(uint64_t)length > RV2_MAX_BYTES || fseek(file, 0L, SEEK_SET) != 0)
	{
		(void)fclose(file);
		return 0;
	}
	size = (size_t)length;
	bytes = size != 0U ? malloc(size) : NULL;
	if (size != 0U && !bytes)
	{
		(void)fclose(file);
		return 0;
	}
	if (size != 0U && fread(bytes, 1U, size, file) != size)
	{
		(void)fclose(file);
		free(bytes);
		return 0;
	}
	if (fclose(file) != 0)
	{
		free(bytes);
		return 0;
	}
	*bytes_out = bytes;
	*size_out = size;
	return 1;
}

static void PrintHex(const unsigned char *bytes)
{
	unsigned int index;

	for (index = 0U; index < RV2_ID_BYTES; index++)
		(void)printf("%02x", bytes[index]);
}

static void PrintSummary(const rv2_context_t *context)
{
	const unsigned char *binding = Record(context, 12U, 0U);

	(void)printf("{\"affordances\":%" PRIu32 ",\"bsp\":\"",
		context->section[8].count);
	PrintHex(binding);
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
	PrintHex(binding + RV2_ID_BYTES);
	(void)printf("\",\"surfaces\":%" PRIu32 ",\"transitions\":%" PRIu32
		"}\n", context->section[7].count, context->section[4].count);
}

int main(int argc, char **argv)
{
	rv2_expected_t expected;
	rv2_context_t context;
	unsigned char *data = NULL;
	size_t size = 0U;
	const char *path;

	if (argc != 12 || strcmp(argv[1], "--generation") != 0 ||
		strcmp(argv[3], "--bsp-id") != 0 || strcmp(argv[5], "--schema-id") != 0 ||
		strcmp(argv[7], "--artifact-id") != 0 ||
		strcmp(argv[9], "--exact-artifact-id") != 0)
	{
		(void)fprintf(stderr, "usage: runev2read --generation N --bsp-id HEX "
			"--schema-id HEX --artifact-id HEX --exact-artifact-id HEX FILE\n");
		return 2;
	}
	path = argv[11];
	if (!ParseU64(argv[2], &expected.generation) ||
		!ParseIdentity(argv[4], expected.bsp) ||
		!ParseIdentity(argv[6], expected.schema) ||
		!ParseIdentity(argv[8], expected.artifact) ||
		!ParseIdentity(argv[10], expected.exact_artifact))
	{
		(void)fprintf(stderr, "runev2read: invalid expected identity\n");
		return 2;
	}
	if (!ReadFile(path, &data, &size))
	{
		(void)fprintf(stderr, "runev2read: cannot read %s\n", path);
		return 2;
	}
	if (!Validate(data, size, &expected, &context))
	{
		free(data);
		(void)fprintf(stderr, "runev2read: reject\n");
		return 1;
	}
	PrintSummary(&context);
	free(data);
	return 0;
}
