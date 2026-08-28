#include "sg_hook_visibility_feasibility_internal.h"

#include <stdlib.h>
#include <string.h>

static int AllocationFits(size_t count, size_t element_size)
{
	return !element_size || count <= SIZE_MAX / element_size;
}

static int DomainCompare(const void *left, const void *right)
{
	const sg_hook_visibility_domain_term_t *a = left;
	const sg_hook_visibility_domain_term_t *b = right;
	uint32_t axis;

#define COMPARE(field) do { \
	if (a->field != b->field) \
		return a->field > b->field ? 1 : -1; \
} while (0)
	COMPARE(pitch_min);
	COMPARE(pitch_max);
	COMPARE(yaw_min);
	COMPARE(yaw_max);
	COMPARE(hand_mask);
	for (axis = 0U; axis < 3U; axis++)
	{
		if (a->origins.mins[axis] != b->origins.mins[axis])
			return a->origins.mins[axis] > b->origins.mins[axis] ? 1 : -1;
		if (a->origins.maxs[axis] != b->origins.maxs[axis])
			return a->origins.maxs[axis] > b->origins.maxs[axis] ? 1 : -1;
	}
#undef COMPARE
	return 0;
}

static int SameControl(const sg_hook_visibility_domain_term_t *left,
	const sg_hook_visibility_domain_term_t *right)
{
	return left->pitch_min == right->pitch_min &&
		left->pitch_max == right->pitch_max &&
		left->yaw_min == right->yaw_min &&
		left->yaw_max == right->yaw_max &&
		left->hand_mask == right->hand_mask;
}

static int LegalControl(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain)
{
	uint32_t control, matches = 0U;

	if (domain->pitch_min != domain->pitch_max ||
		domain->yaw_min != domain->yaw_max || !domain->hand_mask ||
		(domain->hand_mask & (domain->hand_mask - 1U)) != 0U ||
		(domain->hand_mask & ~SG_HOOK_VISIBILITY_ALL_HANDS) != 0U)
		return 0;
	for (control = 0U; control < sources->control_count; control++)
		if (domain->pitch_min >= sources->controls[control].pitch_min &&
			domain->pitch_min <= sources->controls[control].pitch_max &&
			domain->yaw_min >= sources->controls[control].yaw_min &&
			domain->yaw_min <= sources->controls[control].yaw_max)
			matches++;
	return matches == 1U;
}

static int AddCardinality(const sg_hook_visibility_domain_term_t *domain,
	uint64_t *total)
{
	uint64_t amount = 1U;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		uint64_t width = (uint64_t)((int32_t)domain->origins.maxs[axis] -
			domain->origins.mins[axis] + 1);

		if (width && amount > UINT64_MAX / width)
			return 0;
		amount *= width;
	}
	if (*total > UINT64_MAX - amount)
		return 0;
	*total += amount;
	return 1;
}

static int VerifyYAndZ(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domains, uint32_t first,
	uint32_t limit, uint64_t *cardinality)
{
	int32_t y_cursor = sources->origins.mins[1];
	uint32_t index = first;

	while (index < limit)
	{
		int16_t y_min = domains[index].origins.mins[1];
		int16_t y_max = domains[index].origins.maxs[1];
		int32_t z_cursor = sources->origins.mins[2];

		if (y_min != y_cursor || y_max < y_min)
			return 0;
		while (index < limit && domains[index].origins.mins[1] == y_min &&
			domains[index].origins.maxs[1] == y_max)
		{
			if (domains[index].origins.mins[2] != z_cursor ||
				domains[index].origins.maxs[2] <
					domains[index].origins.mins[2] ||
				!AddCardinality(&domains[index], cardinality))
				return 0;
			z_cursor = (int32_t)domains[index].origins.maxs[2] + 1;
			index++;
		}
		if (z_cursor != (int32_t)sources->origins.maxs[2] + 1)
			return 0;
		y_cursor = (int32_t)y_max + 1;
	}
	return y_cursor == (int32_t)sources->origins.maxs[1] + 1;
}

static int VerifyControlTiling(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domains, uint32_t first,
	uint32_t limit, uint64_t *cardinality)
{
	int32_t x_cursor = sources->origins.mins[0];
	uint32_t index = first;

	if (!LegalControl(sources, &domains[first]))
		return 0;
	while (index < limit)
	{
		int16_t x_min = domains[index].origins.mins[0];
		int16_t x_max = domains[index].origins.maxs[0];
		uint32_t slab = index;

		if (x_min != x_cursor || x_max < x_min)
			return 0;
		while (index < limit && domains[index].origins.mins[0] == x_min &&
			domains[index].origins.maxs[0] == x_max)
			index++;
		if (!VerifyYAndZ(sources, domains, slab, index, cardinality))
			return 0;
		x_cursor = (int32_t)x_max + 1;
	}
	return x_cursor == (int32_t)sources->origins.maxs[0] + 1;
}

int SG_HookVisibilityFeasibilityAuditTiling(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	uint64_t *cardinality_out)
{
	sg_hook_visibility_domain_term_t *domains;
	uint64_t cardinality = 0U, expected_groups = 0U;
	uint32_t terminal, first = 0U, groups = 0U, control;

	if (!catalog->terminal_count)
		return 0;
	if (!AllocationFits(catalog->terminal_count, sizeof(*domains)))
		return -1;
	domains = malloc((size_t)catalog->terminal_count * sizeof(*domains));
	if (!domains && catalog->terminal_count)
		return -1;
	for (terminal = 0U; terminal < catalog->terminal_count; terminal++)
		domains[terminal] = catalog->terminals[terminal].domain;
	qsort(domains, catalog->terminal_count, sizeof(*domains), DomainCompare);
	while (first < catalog->terminal_count)
	{
		uint32_t limit = first + 1U;

		while (limit < catalog->terminal_count &&
			SameControl(&domains[first], &domains[limit]))
			limit++;
		if (!VerifyControlTiling(sources, domains, first, limit, &cardinality))
		{
			free(domains);
			return 0;
		}
		groups++;
		first = limit;
	}
	for (control = 0U; control < sources->control_count; control++)
	{
		const sg_hook_visibility_control_root_t *root =
			&sources->controls[control];
		uint64_t fiber = root->pitch_min == root->pitch_max ?
			(uint64_t)((int32_t)root->yaw_max - root->yaw_min + 1) :
			(uint64_t)((int32_t)root->pitch_max - root->pitch_min + 1);

		expected_groups += fiber * SG_HOOK_VISIBILITY_HAND_COUNT;
	}
	free(domains);
	if (groups != expected_groups)
		return 0;
	*cardinality_out = cardinality;
	return 1;
}
