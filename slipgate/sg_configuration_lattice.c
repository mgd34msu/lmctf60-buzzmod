#include "sg_configuration_lattice.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <isl/constraint.h>
#include <isl/ctx.h>
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

static isl_basic_set *AddObjective(isl_basic_set *set,
	const float objective[3], uint32_t objective_position, uint32_t point_offset,
	uint32_t *maximum_shift)
{
	float_integer_t values[3];
	int minimum_exponent = INT_MAX;
	isl_ctx *context = isl_basic_set_get_ctx(set);
	isl_local_space *local;
	isl_constraint *equality;
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
	{
		values[axis] = FloatInteger(objective[axis]);
		if (values[axis].mantissa && values[axis].exponent < minimum_exponent)
			minimum_exponent = values[axis].exponent;
	}
	if (minimum_exponent == INT_MAX)
		return isl_basic_set_free(set);
	local = isl_local_space_from_space(isl_basic_set_get_space(set));
	equality = isl_constraint_alloc_equality(local);
	equality = isl_constraint_set_coefficient_si(equality, isl_dim_set,
		(int)objective_position, -1);
	for (axis = 0; axis < 3 && equality; axis++)
		equality = isl_constraint_set_coefficient_val(equality, isl_dim_set,
			(int)(axis + point_offset), ScaledInteger(context, values[axis],
				minimum_exponent, maximum_shift));
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
	isl_set *optimized = NULL;
	isl_point *point;
	uint32_t dimensions = objective ? 4U : 3U;
	uint32_t offset = objective ? 1U : 0U;
	uint32_t index;
	int result = -1;

	if (!ArgumentsValid(halfspaces, halfspace_count, objective, point_out, stats))
		return -1;
	stats->solve_calls++;
	stats->constraints += (uint64_t)halfspace_count + 6U +
		(objective ? 1U : 0U);
	context = isl_ctx_alloc();
	if (!context)
		return -1;
	if (isl_options_set_on_error(context, ISL_ON_ERROR_CONTINUE) < 0)
	{
		isl_ctx_free(context);
		return -1;
	}
	space = isl_space_set_alloc(context, 0, dimensions);
	set = isl_basic_set_universe(space);
	set = AddBounds(set, offset);
	for (index = 0; index < halfspace_count && set; index++)
		set = AddHalfspace(set, &halfspaces[index], offset, -1,
			&stats->maximum_binary_shift);
	if (set && objective)
		set = AddObjective(set, objective, 0U, offset,
			&stats->maximum_binary_shift);
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
				isl_dim_set, (int)(offset + index));
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
	isl_set_free(optimized);
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
	isl_set *optimized;
	isl_point *point;
	uint32_t dimensions = objective ? 5U : 4U;
	uint32_t point_offset = objective ? 2U : 1U;
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
	stats->solve_calls++;
	stats->constraints += (uint64_t)halfspace_count + 6U +
		(objective ? 1U : 0U);
	context = isl_ctx_alloc();
	if (!context)
		return -1;
	if (isl_options_set_on_error(context, ISL_ON_ERROR_CONTINUE) < 0)
	{
		isl_ctx_free(context);
		return -1;
	}
	space = isl_space_set_alloc(context, 0, dimensions);
	basic = isl_basic_set_universe(space);
	basic = AddBounds(basic, point_offset);
	for (index = 0; index < halfspace_count && basic; index++)
		basic = AddHalfspace(basic, &halfspaces[index], point_offset,
			clearance_constraints[index] ? 0 : -1,
			&stats->maximum_binary_shift);
	if (basic && objective)
		basic = AddObjective(basic, objective, 1U, point_offset,
			&stats->maximum_binary_shift);
	optimized = basic ? isl_basic_set_lexmax(basic) : NULL;
	basic = NULL;
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
	isl_set_free(optimized);
	isl_basic_set_free(basic);
	if (isl_ctx_last_error(context) != isl_error_none)
		result = -1;
	isl_ctx_free(context);
	return result;
}
