#include "sg_configuration_lattice.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <isl/constraint.h>
#include <isl/ctx.h>
#include <isl/aff.h>
#include <isl/ilp.h>
#include <isl/local_space.h>
#include <isl/options.h>
#include <isl/point.h>
#include <isl/set.h>
#include <isl/space.h>
#include <isl/val.h>

typedef struct float_integer_s
{
	int32_t mantissa;
	int exponent;
} float_integer_t;

static int FiniteNonzeroVector(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]) &&
		(value[0] != 0.0f || value[1] != 0.0f || value[2] != 0.0f);
}

static float DominantMagnitude(const float value[3])
{
	float result = fabsf(value[0]);

	if (fabsf(value[1]) > result)
		result = fabsf(value[1]);
	if (fabsf(value[2]) > result)
		result = fabsf(value[2]);
	return result;
}

static int ArgumentsValid(
	const sg_configuration_lattice_halfspace_t *halfspaces,
	uint32_t halfspace_count, const float objective[3],
	const int32_t point_out[3], const sg_configuration_lattice_stats_t *stats)
{
	uint32_t index;

	if ((!halfspaces && halfspace_count) || !point_out || !stats ||
		(objective && !FiniteNonzeroVector(objective)))
		return 0;
	for (index = 0; index < halfspace_count; index++)
		if (!FiniteNonzeroVector(halfspaces[index].normal) ||
			!isfinite(halfspaces[index].distance) ||
			(halfspaces[index].open != 0 && halfspaces[index].open != 1))
			return 0;
	return 1;
}

static float_integer_t FloatInteger(float value)
{
	uint32_t bits;
	uint32_t fraction;
	uint32_t encoded_exponent;
	float_integer_t result;

	memcpy(&bits, &value, sizeof(bits));
	fraction = bits & UINT32_C(0x007fffff);
	encoded_exponent = (bits >> 23U) & UINT32_C(0xff);
	result.mantissa = encoded_exponent ?
		(int32_t)(fraction | UINT32_C(0x00800000)) : (int32_t)fraction;
	if (bits >> 31U)
		result.mantissa = -result.mantissa;
	result.exponent = encoded_exponent ? (int)encoded_exponent - 150 : -149;
	return result;
}

static isl_val *ScaledInteger(isl_ctx *context, float_integer_t value,
	int minimum_exponent, uint32_t *maximum_shift)
{
	uint32_t shift;
	isl_val *result = isl_val_int_from_si(context, value.mantissa);

	if (!value.mantissa)
		return result;
	shift = (uint32_t)(value.exponent - minimum_exponent);
	if (shift > *maximum_shift)
		*maximum_shift = shift;
	while (result && shift--)
		result = isl_val_mul_ui(result, 2U);
	return result;
}

static isl_basic_set *AddHalfspace(isl_basic_set *set,
	const sg_configuration_lattice_halfspace_t *halfspace, uint32_t offset,
	int clearance_position, uint32_t *maximum_shift)
{
	float_integer_t values[5];
	int minimum_exponent = INT_MAX;
	isl_local_space *local;
	isl_constraint *constraint;
	isl_ctx *context = isl_basic_set_get_ctx(set);
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
	{
		values[axis] = FloatInteger(halfspace->normal[axis]);
		if (values[axis].mantissa && values[axis].exponent < minimum_exponent)
			minimum_exponent = values[axis].exponent;
	}
	values[3] = FloatInteger(halfspace->distance);
	values[3].exponent += 3;
	if (values[3].mantissa && values[3].exponent < minimum_exponent)
		minimum_exponent = values[3].exponent;
	if (clearance_position >= 0)
	{
		/* Scaling by the dominant coefficient makes t an exact L-infinity
		 * geometric clearance and keeps it invariant when a plane is scaled. */
		values[4] = FloatInteger(DominantMagnitude(halfspace->normal));
		if (values[4].exponent < minimum_exponent)
			minimum_exponent = values[4].exponent;
	}
	if (minimum_exponent == INT_MAX)
		return NULL;
	local = isl_local_space_from_space(isl_basic_set_get_space(set));
	constraint = isl_constraint_alloc_inequality(local);
	for (axis = 0; axis < 3 && constraint; axis++)
	{
		isl_val *coefficient = ScaledInteger(context, values[axis],
			minimum_exponent, maximum_shift);

		constraint = isl_constraint_set_coefficient_val(constraint, isl_dim_set,
			(int)(offset + axis), isl_val_neg(coefficient));
	}
	if (constraint)
	{
		isl_val *constant = ScaledInteger(context, values[3], minimum_exponent,
			maximum_shift);

		if (halfspace->open)
			constant = isl_val_sub_ui(constant, 1U);
		constraint = isl_constraint_set_constant_val(constraint, constant);
	}
	if (constraint && clearance_position >= 0)
		constraint = isl_constraint_set_coefficient_val(constraint, isl_dim_set,
			clearance_position, isl_val_neg(ScaledInteger(context, values[4],
				minimum_exponent, maximum_shift)));
	return constraint ? isl_basic_set_add_constraint(set, constraint) :
		isl_basic_set_free(set);
}

static isl_basic_set *AddBounds(isl_basic_set *set, uint32_t offset)
{
	uint32_t axis;

	for (axis = 0; axis < 3 && set; axis++)
	{
		isl_local_space *lower_space =
			isl_local_space_from_space(isl_basic_set_get_space(set));
		isl_constraint *lower = isl_constraint_alloc_inequality(lower_space);
		isl_local_space *upper_space;
		isl_constraint *upper;

		lower = isl_constraint_set_coefficient_si(lower, isl_dim_set,
			(int)(offset + axis), 1);
		lower = isl_constraint_set_constant_si(lower, 32768);
		set = isl_basic_set_add_constraint(set, lower);
		if (!set)
			break;
		upper_space = isl_local_space_from_space(isl_basic_set_get_space(set));
		upper = isl_constraint_alloc_inequality(upper_space);
		upper = isl_constraint_set_coefficient_si(upper, isl_dim_set,
			(int)(offset + axis), -1);
		upper = isl_constraint_set_constant_si(upper, 32767);
		set = isl_basic_set_add_constraint(set, upper);
	}
	return set;
}

static isl_aff *ObjectiveAff(isl_basic_set *set,
	const float objective[3], uint32_t point_offset, uint32_t *maximum_shift)
{
	float_integer_t values[3];
	int minimum_exponent = INT_MAX;
	isl_ctx *context = isl_basic_set_get_ctx(set);
	isl_local_space *local;
	isl_aff *aff;
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
	{
		values[axis] = FloatInteger(objective[axis]);
		if (values[axis].mantissa && values[axis].exponent < minimum_exponent)
			minimum_exponent = values[axis].exponent;
	}
	if (minimum_exponent == INT_MAX)
		return NULL;
	local = isl_local_space_from_space(isl_basic_set_get_space(set));
	aff = isl_aff_zero_on_domain(local);
	for (axis = 0; axis < 3U && aff; axis++)
		aff = isl_aff_set_coefficient_val(aff, isl_dim_in,
			(int)(axis + point_offset), ScaledInteger(context, values[axis],
				minimum_exponent, maximum_shift));
	return aff;
}

static isl_basic_set *ConstrainObjectiveMaximum(isl_basic_set *set,
	const float objective[3], uint32_t point_offset, isl_val *maximum,
	uint32_t *maximum_shift)
{
	isl_aff *objective_aff = ObjectiveAff(set, objective, point_offset,
		maximum_shift);
	isl_local_space *local;
	isl_aff *constant;
	isl_basic_set *optimum;

	if (!objective_aff)
		return isl_basic_set_free(set);
	local = isl_local_space_from_space(isl_basic_set_get_space(set));
	constant = isl_aff_val_on_domain(local, isl_val_copy(maximum));
	if (!constant)
	{
		isl_aff_free(objective_aff);
		return isl_basic_set_free(set);
	}
	optimum = isl_aff_eq_basic_set(objective_aff, constant);
	return optimum ? isl_basic_set_intersect(set, optimum) :
		isl_basic_set_free(set);
}

static isl_basic_set *ConstrainCoordinateValue(isl_basic_set *set,
	uint32_t position, isl_val *value)
{
	isl_local_space *local =
		isl_local_space_from_space(isl_basic_set_get_space(set));
	isl_constraint *equality = isl_constraint_alloc_equality(local);

	equality = isl_constraint_set_coefficient_si(equality, isl_dim_set,
		(int)position, 1);
	equality = isl_constraint_set_constant_val(equality,
		isl_val_neg(isl_val_copy(value)));
	return equality ? isl_basic_set_add_constraint(set, equality) :
		isl_basic_set_free(set);
}

int SG_ConfigurationLatticeFind(
	const sg_configuration_lattice_halfspace_t *halfspaces,
	uint32_t halfspace_count, const float objective[3], int32_t point_out[3],
	sg_configuration_lattice_stats_t *stats)
{
	isl_ctx *context;
	isl_space *space;
	isl_basic_set *set;
	isl_aff *objective_aff = NULL;
	isl_val *maximum = NULL;
	isl_set *optimized = NULL;
	isl_point *point;
	uint32_t index;
	int result = -1;

	if (!ArgumentsValid(halfspaces, halfspace_count, objective, point_out, stats))
		return -1;
	stats->solve_calls += objective ? 2U : 1U;
	stats->constraints += objective ?
		2U * (uint64_t)halfspace_count + 13U :
		(uint64_t)halfspace_count + 6U;
	context = isl_ctx_alloc();
	if (!context)
		return -1;
	if (isl_options_set_on_error(context, ISL_ON_ERROR_CONTINUE) < 0)
	{
		isl_ctx_free(context);
		return -1;
	}
	space = isl_space_set_alloc(context, 0, 3U);
	set = isl_basic_set_universe(space);
	set = AddBounds(set, 0U);
	for (index = 0; index < halfspace_count && set; index++)
		set = AddHalfspace(set, &halfspaces[index], 0U, -1,
			&stats->maximum_binary_shift);
	if (set && objective)
		objective_aff = ObjectiveAff(set, objective, 0U,
			&stats->maximum_binary_shift);
	if (set && objective_aff)
		maximum = isl_basic_set_max_val(set, objective_aff);
	if (set && objective && maximum &&
		isl_val_is_int(maximum) == isl_bool_true)
		set = ConstrainObjectiveMaximum(set, objective, 0U, maximum,
			&stats->maximum_binary_shift);
	else if (set && objective && maximum &&
		isl_val_is_nan(maximum) != isl_bool_true)
	{
		set = isl_basic_set_free(set);
	}
	if (set && objective)
	{
		optimized = isl_basic_set_lexmax(set);
		set = NULL;
	}
	if (optimized)
	{
		point = isl_set_sample_point(optimized);
		optimized = NULL;
	}
	else if (set)
	{
		point = isl_basic_set_sample_point(set);
		set = NULL;
	}
	else
		point = NULL;
	if (point && isl_point_is_void(point) == isl_bool_false)
	{
		result = 1;
		for (index = 0; index < 3U; index++)
		{
			isl_val *coordinate = isl_point_get_coordinate_val(point,
				isl_dim_set, (int)index);
			long value;

			if (!coordinate || isl_val_is_int(coordinate) != isl_bool_true)
			{
				isl_val_free(coordinate);
				result = -1;
				break;
			}
			value = isl_val_get_num_si(coordinate);
			isl_val_free(coordinate);
			if (value < INT32_MIN || value > INT32_MAX)
			{
				result = -1;
				break;
			}
			point_out[index] = (int32_t)value;
		}
	}
	else if (point && isl_point_is_void(point) == isl_bool_true)
		result = 0;
	isl_point_free(point);
	isl_aff_free(objective_aff);
	isl_val_free(maximum);
	isl_set_free(optimized);
	isl_basic_set_free(set);
	if (isl_ctx_last_error(context) != isl_error_none)
		result = -1;
	isl_ctx_free(context);
	return result;
}

int SG_ConfigurationLatticeCoordinateBounds(
	const sg_configuration_lattice_halfspace_t *halfspaces,
	uint32_t halfspace_count, uint32_t axis, int32_t *minimum_out,
	int32_t *maximum_out, sg_configuration_lattice_stats_t *stats)
{
	isl_ctx *context;
	isl_space *space;
	isl_basic_set *set;
	isl_val *minimum = NULL;
	isl_val *maximum = NULL;
	uint32_t index;
	int result = -1;

	if ((!halfspaces && halfspace_count) || axis >= 3U || !minimum_out ||
		!maximum_out || !stats)
		return -1;
	for (index = 0; index < halfspace_count; index++)
		if (!FiniteNonzeroVector(halfspaces[index].normal) ||
			!isfinite(halfspaces[index].distance) ||
			(halfspaces[index].open != 0 && halfspaces[index].open != 1))
			return -1;
	stats->solve_calls += 2U;
	stats->constraints += 2U * ((uint64_t)halfspace_count + 6U);
	context = isl_ctx_alloc();
	if (!context)
		return -1;
	if (isl_options_set_on_error(context, ISL_ON_ERROR_CONTINUE) < 0)
	{
		isl_ctx_free(context);
		return -1;
	}
	space = isl_space_set_alloc(context, 0, 3U);
	set = isl_basic_set_universe(space);
	set = AddBounds(set, 0U);
	for (index = 0; index < halfspace_count && set; index++)
		set = AddHalfspace(set, &halfspaces[index], 0U, -1,
			&stats->maximum_binary_shift);
	if (set)
		maximum = isl_basic_set_dim_max_val(isl_basic_set_copy(set), (int)axis);
	if (set)
		minimum = isl_set_dim_min_val(isl_set_from_basic_set(set), (int)axis);
	set = NULL;
	if (minimum && maximum &&
		isl_val_is_nan(minimum) == isl_bool_true &&
		isl_val_is_nan(maximum) == isl_bool_true)
		result = 0;
	else if (minimum && maximum &&
		isl_val_is_int(minimum) == isl_bool_true &&
		isl_val_is_int(maximum) == isl_bool_true)
	{
		long minimum_value = isl_val_get_num_si(minimum);
		long maximum_value = isl_val_get_num_si(maximum);

		if (minimum_value >= INT32_MIN && minimum_value <= INT32_MAX &&
			maximum_value >= INT32_MIN && maximum_value <= INT32_MAX)
		{
			*minimum_out = (int32_t)minimum_value;
			*maximum_out = (int32_t)maximum_value;
			result = 1;
		}
	}
	isl_val_free(minimum);
	isl_val_free(maximum);
	isl_basic_set_free(set);
	if (isl_ctx_last_error(context) != isl_error_none)
		result = -1;
	isl_ctx_free(context);
	return result;
}

int SG_ConfigurationLatticeFindMaxClearance(
	const sg_configuration_lattice_halfspace_t *halfspaces,
	const uint8_t *clearance_constraints, uint32_t halfspace_count,
	const float objective[3], int32_t point_out[3], int *positive_margin_out,
	sg_configuration_lattice_stats_t *stats)
{
	isl_ctx *context;
	isl_space *space;
	isl_basic_set *basic;
	isl_aff *objective_aff = NULL;
	isl_val *margin_maximum = NULL;
	isl_val *objective_maximum = NULL;
	isl_set *optimized = NULL;
	isl_point *point;
	uint32_t point_offset = 1U;
	uint32_t index;
	int has_clearance = 0;
	int result = -1;

	if (!ArgumentsValid(halfspaces, halfspace_count, objective, point_out,
			stats) || !clearance_constraints || !positive_margin_out)
		return -1;
	for (index = 0; index < halfspace_count; index++)
	{
		if (clearance_constraints[index] > 1U)
			return -1;
		has_clearance |= clearance_constraints[index] != 0U;
	}
	if (!has_clearance)
		return -1;
	stats->solve_calls += objective ? 3U : 1U;
	stats->constraints += objective ?
		3U * (uint64_t)halfspace_count + 21U :
		(uint64_t)halfspace_count + 6U;
	context = isl_ctx_alloc();
	if (!context)
		return -1;
	if (isl_options_set_on_error(context, ISL_ON_ERROR_CONTINUE) < 0)
	{
		isl_ctx_free(context);
		return -1;
	}
	space = isl_space_set_alloc(context, 0, 4U);
	basic = isl_basic_set_universe(space);
	basic = AddBounds(basic, point_offset);
	for (index = 0; index < halfspace_count && basic; index++)
		basic = AddHalfspace(basic, &halfspaces[index], point_offset,
			clearance_constraints[index] ? 0 : -1,
			&stats->maximum_binary_shift);
	if (basic && !objective)
	{
		optimized = isl_basic_set_lexmax(basic);
		basic = NULL;
	}
	if (basic)
		margin_maximum = isl_basic_set_dim_max_val(isl_basic_set_copy(basic), 0);
	if (basic && margin_maximum &&
		isl_val_is_int(margin_maximum) == isl_bool_true)
		basic = ConstrainCoordinateValue(basic, 0U, margin_maximum);
	else if (basic && margin_maximum &&
		isl_val_is_nan(margin_maximum) != isl_bool_true)
		basic = isl_basic_set_free(basic);
	if (basic && objective)
		objective_aff = ObjectiveAff(basic, objective, point_offset,
			&stats->maximum_binary_shift);
	if (basic && objective_aff)
		objective_maximum = isl_basic_set_max_val(basic, objective_aff);
	if (basic && objective && objective_maximum &&
		isl_val_is_int(objective_maximum) == isl_bool_true)
		basic = ConstrainObjectiveMaximum(basic, objective, point_offset,
			objective_maximum, &stats->maximum_binary_shift);
	else if (basic && objective && objective_maximum &&
		isl_val_is_nan(objective_maximum) != isl_bool_true)
		basic = isl_basic_set_free(basic);
	if (basic)
	{
		optimized = isl_basic_set_lexmax(basic);
		basic = NULL;
	}
	point = optimized ? isl_set_sample_point(optimized) : NULL;
	optimized = NULL;
	if (point && isl_point_is_void(point) == isl_bool_false)
	{
		isl_val *margin = isl_point_get_coordinate_val(point, isl_dim_set, 0);

		if (margin && isl_val_is_int(margin) == isl_bool_true)
		{
			*positive_margin_out = isl_val_sgn(margin) > 0;
			result = 1;
		}
		isl_val_free(margin);
		for (index = 0; index < 3U && result == 1; index++)
		{
			isl_val *coordinate = isl_point_get_coordinate_val(point,
				isl_dim_set, (int)(point_offset + index));
			long value;

			if (!coordinate || isl_val_is_int(coordinate) != isl_bool_true)
			{
				isl_val_free(coordinate);
				result = -1;
				break;
			}
			value = isl_val_get_num_si(coordinate);
			isl_val_free(coordinate);
			if (value < INT32_MIN || value > INT32_MAX)
			{
				result = -1;
				break;
			}
			point_out[index] = (int32_t)value;
		}
	}
	else if (point && isl_point_is_void(point) == isl_bool_true)
		result = 0;
	isl_point_free(point);
	isl_aff_free(objective_aff);
	isl_val_free(margin_maximum);
	isl_val_free(objective_maximum);
	isl_set_free(optimized);
	isl_basic_set_free(basic);
	if (isl_ctx_last_error(context) != isl_error_none)
		result = -1;
	isl_ctx_free(context);
	return result;
}
