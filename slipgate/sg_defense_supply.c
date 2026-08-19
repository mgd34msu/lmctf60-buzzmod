/* Deterministic policy core for the bounded defender weapon sortie. */
#include "sg_defense_supply.h"

#include <stddef.h>

#define SG_DEFENSE_SUPPLY_FIELD_INF 0x3fffffff

sg_defense_supply_phase_t SG_DefenseSupplyPhaseStep(
	sg_defense_supply_phase_t phase,
	const sg_defense_supply_step_t *step)
{
	if (!step || phase == SG_DEFENSE_SUPPLY_PHASE_NONE)
		return SG_DEFENSE_SUPPLY_PHASE_NONE;
	if (!step->identity_valid || !step->owner_valid)
		return SG_DEFENSE_SUPPLY_PHASE_NONE;
	if (phase == SG_DEFENSE_SUPPLY_PHASE_RETURN)
		return SG_DEFENSE_SUPPLY_PHASE_RETURN;
	if (phase != SG_DEFENSE_SUPPLY_PHASE_OUTBOUND)
		return SG_DEFENSE_SUPPLY_PHASE_NONE;
	if (step->weapon_available || !step->own_flag_home || step->threat ||
	    step->engaged || step->human_order || step->other_owner ||
	    !step->target_valid ||
	    !step->weapon_field_valid || !step->deadline_pending)
		return SG_DEFENSE_SUPPLY_PHASE_RETURN;
	return SG_DEFENSE_SUPPLY_PHASE_OUTBOUND;
}

int SG_DefenseSupplyRoute(
	sg_defense_supply_phase_t phase,
	const int *weapon_class_field,
	const int *selected_target_field,
	const int *home_field,
	int current_seed,
	int max_route_ms,
	const int **route_out)
{
	if (route_out)
		*route_out = NULL;
	if (!route_out || current_seed < 0 || max_route_ms < 0)
		return 0;
	if (phase == SG_DEFENSE_SUPPLY_PHASE_RETURN)
	{
		if (!home_field)
			return 0;
		*route_out = home_field;
		return 1;
	}
	if (phase != SG_DEFENSE_SUPPLY_PHASE_OUTBOUND ||
	    !selected_target_field ||
	    (weapon_class_field &&
	     (weapon_class_field[current_seed] >= SG_DEFENSE_SUPPLY_FIELD_INF ||
	      weapon_class_field[current_seed] > max_route_ms)) ||
	    selected_target_field[current_seed] >= SG_DEFENSE_SUPPLY_FIELD_INF ||
	    selected_target_field[current_seed] > max_route_ms)
		return 0;
	*route_out = selected_target_field;
	return 1;
}

int SG_DefenseSupplyChooseNeighbor(
	const sg_defense_supply_neighbor_t *candidates,
	unsigned candidate_count,
	int current_route_cost_ms)
{
	int best_link = -1;
	int best_cost = current_route_cost_ms;
	unsigned i;

	if (!candidates || current_route_cost_ms < 0)
		return -1;
	for (i = 0; i < candidate_count; i++)
	{
		const sg_defense_supply_neighbor_t *candidate = &candidates[i];

		if (candidate->link_index < 0 || candidate->to_seed < 0 ||
		    candidate->route_cost_ms < 0 ||
		    candidate->route_cost_ms >= best_cost)
			continue;
		best_cost = candidate->route_cost_ms;
		best_link = candidate->link_index;
	}
	return best_link;
}

int SG_DefenseSupplyActionAllowed(
	sg_defense_supply_phase_t phase,
	int ordinary_run)
{
	if (phase == SG_DEFENSE_SUPPLY_PHASE_OUTBOUND)
		return ordinary_run != 0;
	return phase == SG_DEFENSE_SUPPLY_PHASE_RETURN;
}

int SG_DefenseSupplyGenericRetryAllowed(
	sg_defense_supply_phase_t phase,
	int armed)
{
	return !armed && phase == SG_DEFENSE_SUPPLY_PHASE_NONE;
}
