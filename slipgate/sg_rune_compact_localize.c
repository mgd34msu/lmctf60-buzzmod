#include "sg_rune_compact_localize.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define SG_RUNE_DYADIC_LIMBS 5U
#define SG_RUNE_BINARY32_MIN_EXPONENT (-149)

typedef struct sg_rune_signed_dyadic_s
{
	uint64_t limb[SG_RUNE_DYADIC_LIMBS];
	int sign;
} sg_rune_signed_dyadic_t;

static void ClearLocation(sg_rune_compact_location_t *location)
{
	memset(location, 0, sizeof(*location));
	location->cell.value = SG_RUNE_COMPACT_INDEX_NONE;
}

static int SpanWithin(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int BoundsContain(const sg_rune_q8_bounds_t *bounds,
	const sg_rune_q8_vec3_t *point)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return -1;
		if (point->value[axis] < bounds->mins.value[axis] ||
			point->value[axis] > bounds->maxs.value[axis])
			return 0;
	}
	return 1;
}

static int Binary32FiniteCanonical(uint32_t bits)
{
	return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000) &&
		bits != UINT32_C(0x80000000);
}

static uint32_t Binary32Significand(uint32_t bits)
{
	const uint32_t fraction = bits & UINT32_C(0x007fffff);
	const uint32_t exponent = (bits >> 23U) & UINT32_C(0xff);

	return exponent == 0U ? fraction : fraction | UINT32_C(0x00800000);
}

static int Binary32Exponent(uint32_t bits)
{
	const uint32_t exponent = (bits >> 23U) & UINT32_C(0xff);

	return exponent == 0U ? SG_RUNE_BINARY32_MIN_EXPONENT :
		(int)exponent - 150;
}

static int MagnitudeCompare(const uint64_t left[SG_RUNE_DYADIC_LIMBS],
	const uint64_t right[SG_RUNE_DYADIC_LIMBS])
{
	uint32_t remaining = SG_RUNE_DYADIC_LIMBS;

	while (remaining != 0U) {
		const uint32_t limb = remaining - 1U;

		if (left[limb] != right[limb])
			return left[limb] < right[limb] ? -1 : 1;
		remaining--;
	}
	return 0;
}

static void MagnitudeAdd(uint64_t destination[SG_RUNE_DYADIC_LIMBS],
	const uint64_t value[SG_RUNE_DYADIC_LIMBS])
{
	uint64_t carry = 0U;
	uint32_t limb;

	for (limb = 0U; limb < SG_RUNE_DYADIC_LIMBS; limb++) {
		const uint64_t sum = destination[limb] + value[limb];
		const uint64_t first_carry = sum < destination[limb] ? 1U : 0U;
		const uint64_t carried = sum + carry;
		const uint64_t second_carry = carried < sum ? 1U : 0U;

		destination[limb] = carried;
		carry = first_carry | second_carry;
	}
}

static void MagnitudeSubtract(uint64_t destination[SG_RUNE_DYADIC_LIMBS],
	const uint64_t value[SG_RUNE_DYADIC_LIMBS])
{
	uint64_t borrow = 0U;
	uint32_t limb;

	for (limb = 0U; limb < SG_RUNE_DYADIC_LIMBS; limb++) {
		const uint64_t subtrahend = value[limb] + borrow;
		const uint64_t wrapped = subtrahend < value[limb] ? 1U : 0U;
		const uint64_t next_borrow =
			wrapped | (destination[limb] < subtrahend ? 1U : 0U);

		destination[limb] -= subtrahend;
		borrow = next_borrow;
	}
}

static void DyadicAddTerm(sg_rune_signed_dyadic_t *sum, uint64_t magnitude,
	uint32_t shift, int sign)
{
	uint64_t term[SG_RUNE_DYADIC_LIMBS] = { 0U };
	const uint32_t word = shift / 64U;
	const uint32_t bits = shift % 64U;
	int comparison;

	if (magnitude == 0U)
		return;
	term[word] = magnitude << bits;
	if (bits != 0U && word + 1U < SG_RUNE_DYADIC_LIMBS)
		term[word + 1U] = magnitude >> (64U - bits);
	if (sum->sign == 0) {
		memcpy(sum->limb, term, sizeof(term));
		sum->sign = sign;
		return;
	}
	if (sum->sign == sign) {
		MagnitudeAdd(sum->limb, term);
		return;
	}
	comparison = MagnitudeCompare(sum->limb, term);
	if (comparison == 0) {
		memset(sum, 0, sizeof(*sum));
		return;
	}
	if (comparison > 0) {
		MagnitudeSubtract(sum->limb, term);
		return;
	}
	MagnitudeSubtract(term, sum->limb);
	memcpy(sum->limb, term, sizeof(term));
	sum->sign = sign;
}

static void AddNormalTerm(sg_rune_signed_dyadic_t *sum, uint32_t bits,
	int32_t coordinate)
{
	const uint32_t significand = Binary32Significand(bits);
	const uint32_t coordinate_magnitude = coordinate < 0 ?
		(uint32_t)(-(int64_t)coordinate) : (uint32_t)coordinate;
	const uint64_t magnitude =
		(uint64_t)significand * (uint64_t)coordinate_magnitude;
	const int negative = ((bits >> 31U) != 0U) != (coordinate < 0);
	const int exponent = Binary32Exponent(bits);

	DyadicAddTerm(sum, magnitude,
		(uint32_t)(exponent - SG_RUNE_BINARY32_MIN_EXPONENT),
		negative ? -1 : 1);
}

static int PlaneRelation(const sg_rune_binary32_plane_t *plane,
	const sg_rune_q8_vec3_t *point, int *relation_out)
{
	sg_rune_signed_dyadic_t sum = { { 0U }, 0 };
	uint32_t axis;
	int has_normal = 0;

	if (!Binary32FiniteCanonical(plane->distance_bits))
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		if (!Binary32FiniteCanonical(plane->normal_bits[axis]))
			return 0;
		if (Binary32Significand(plane->normal_bits[axis]) != 0U)
			has_normal = 1;
		AddNormalTerm(&sum, plane->normal_bits[axis], point->value[axis]);
	}
	if (!has_normal)
		return 0;
	DyadicAddTerm(&sum, Binary32Significand(plane->distance_bits),
		(uint32_t)(Binary32Exponent(plane->distance_bits) + 3 -
			SG_RUNE_BINARY32_MIN_EXPONENT),
		(plane->distance_bits >> 31U) != 0U ? 1 : -1);
	*relation_out = sum.sign;
	return 1;
}

static sg_rune_compact_localize_status_t CellContains(
	const sg_rune_compact_model_t *model, uint32_t cell_index,
	const sg_rune_q8_vec3_t *point, int *contains_out)
{
	const sg_rune_compact_cell_t *cell = &model->cells[cell_index];
	const int bounds_result = BoundsContain(&cell->bounds, point);
	uint32_t local;

	*contains_out = 0;
	if (bounds_result < 0 || cell->valid_stances == 0U ||
		(cell->valid_stances & (sg_rune_stance_validity_t)
			~SG_RUNE_STANCE_VALID_ALL) != 0U ||
		cell->incidences.count == 0U ||
		!SpanWithin(cell->incidences.first, cell->incidences.count,
			model->cell_incidence_count))
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_MODEL;
	if (bounds_result == 0)
		return SG_RUNE_COMPACT_LOCALIZE_OK;
	if (!model->cell_incidences || !model->incidences || !model->facets)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_MODEL;
	for (local = 0U; local < cell->incidences.count; local++) {
		const uint32_t reference = cell->incidences.first + local;
		const uint32_t incidence_index =
			model->cell_incidences[reference].value;
		const sg_rune_compact_incidence_t *incidence;
		int relation;

		if (incidence_index >= model->incidence_count)
			return SG_RUNE_COMPACT_LOCALIZE_INVALID_MODEL;
		incidence = &model->incidences[incidence_index];
		if (incidence->cell.value != cell_index ||
			incidence->cell_ordinal != local ||
			incidence->facet.value >= model->facet_count ||
			incidence->side < 0 ||
			incidence->side >= SG_RUNE_FACET_SIDE_COUNT ||
			incidence->boundary < 0 ||
			incidence->boundary >= SG_RUNE_BOUNDARY_OWNERSHIP_COUNT ||
			!PlaneRelation(&model->facets[incidence->facet.value].plane,
				point, &relation))
			return SG_RUNE_COMPACT_LOCALIZE_INVALID_MODEL;
		if (relation == 0 && incidence->boundary == SG_RUNE_BOUNDARY_OPEN)
			return SG_RUNE_COMPACT_LOCALIZE_OK;
		if ((incidence->side == SG_RUNE_FACET_NEGATIVE_SIDE && relation > 0) ||
			(incidence->side == SG_RUNE_FACET_POSITIVE_SIDE && relation < 0))
			return SG_RUNE_COMPACT_LOCALIZE_OK;
	}
	*contains_out = 1;
	return SG_RUNE_COMPACT_LOCALIZE_OK;
}

static sg_rune_compact_localize_status_t Localize(
	const sg_rune_compact_model_t *model, const sg_rune_q8_vec3_t *point,
	const sg_rune_compact_cell_index_t *candidate_cells,
	uint32_t candidate_count, int indexed,
	sg_rune_compact_location_t *location_out)
{
	uint32_t selected = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t candidate;

	ClearLocation(location_out);
	if (!model || !point)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT;
	if (!model->cells || model->cell_count == 0U)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_MODEL;
	if (indexed && candidate_count != 0U && !candidate_cells)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT;
	for (candidate = 0U; candidate < candidate_count; candidate++) {
		const uint32_t cell_index = indexed ? candidate_cells[candidate].value :
			candidate;
		int contains;
		sg_rune_compact_localize_status_t status;

		if (cell_index >= model->cell_count)
			return SG_RUNE_COMPACT_LOCALIZE_INVALID_CANDIDATE;
		status = CellContains(model, cell_index, point, &contains);
		if (status != SG_RUNE_COMPACT_LOCALIZE_OK)
			return status;
		if (contains && (selected == SG_RUNE_COMPACT_INDEX_NONE ||
			cell_index < selected))
			selected = cell_index;
	}
	if (selected == SG_RUNE_COMPACT_INDEX_NONE)
		return SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND;
	location_out->cell.value = selected;
	location_out->valid_stances = model->cells[selected].valid_stances;
	return SG_RUNE_COMPACT_LOCALIZE_OK;
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalize(
	const sg_rune_compact_model_t *model, const sg_rune_q8_vec3_t *point,
	sg_rune_compact_location_t *location_out)
{
	if (!location_out)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT;
	return Localize(model, point, NULL, model ? model->cell_count : 0U, 0,
		location_out);
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalizeIndexed(
	const sg_rune_compact_model_t *model, const sg_rune_q8_vec3_t *point,
	const sg_rune_compact_cell_index_t *candidate_cells,
	uint32_t candidate_count, sg_rune_compact_location_t *location_out)
{
	if (!location_out)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT;
	return Localize(model, point, candidate_cells, candidate_count, 1,
		location_out);
}

const char *SG_RuneCompactLocalizeStatusString(
	sg_rune_compact_localize_status_t status)
{
	static const char *const names[SG_RUNE_COMPACT_LOCALIZE_STATUS_COUNT] = {
		"ok",
		"not found",
		"invalid argument",
		"invalid model",
		"invalid candidate"
	};

	return (uint32_t)status <
		(uint32_t)SG_RUNE_COMPACT_LOCALIZE_STATUS_COUNT ? names[status] :
		"unknown compact localization status";
}
