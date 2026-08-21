/* Bounded topology planner for compound door links. */
#include "sg_compound_action_gen.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#include "sg_action.h"
#include "sg_compound.h"

typedef struct sg_compound_action_gen_proven_s
{
	rune_link_t link;
	int trigger_key;
	int mover_key;
	uint32_t local_rank;
} sg_compound_action_gen_proven_t;

static sg_compound_action_gen_result_t ActionGenResult(
	sg_compound_action_gen_status_t status)
{
	sg_compound_action_gen_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	return result;
}

static int ActionGenAnchorValid(const float anchor[3])
{
	int axis;

	if (!anchor)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled;

		if (!isfinite(anchor[axis]) ||
		    (anchor[axis] == 0.0f && signbit(anchor[axis])))
			return 0;
		scaled = anchor[axis] * 8.0f;
		if (!isfinite(scaled) || scaled < (float)SHRT_MIN ||
		    scaled > (float)SHRT_MAX || scaled != (float)(short)scaled)
			return 0;
	}
	return 1;
}

static int ActionGenSameAnchor(const float first[3], const float second[3])
{
	return first[0] == second[0] && first[1] == second[1] &&
	       first[2] == second[2];
}

static int ActionGenCandidateCompare(
	const sg_compound_action_gen_candidate_t *first,
	const sg_compound_action_gen_candidate_t *second)
{
	int axis;

	if (first->source != second->source)
		return first->source < second->source ? -1 : 1;
	if (first->trigger_key != second->trigger_key)
		return first->trigger_key < second->trigger_key ? -1 : 1;
	if (first->mover_key != second->mover_key)
		return first->mover_key < second->mover_key ? -1 : 1;
	if (first->mode != second->mode)
		return first->mode < second->mode ? -1 : 1;
	for (axis = 0; axis < 3; axis++)
		if (first->mechanism_anchor[axis] != second->mechanism_anchor[axis])
			return first->mechanism_anchor[axis] <
			       second->mechanism_anchor[axis] ? -1 : 1;
	if (first->destination != second->destination)
		return first->destination < second->destination ? -1 : 1;
	if (first->local_rank != second->local_rank)
		return first->local_rank < second->local_rank ? -1 : 1;
	return 0;
}

static void ActionGenSortCandidates(
	sg_compound_action_gen_candidate_t *candidates, size_t count)
{
	size_t index;

	for (index = 1; index < count; index++)
	{
		sg_compound_action_gen_candidate_t value = candidates[index];
		size_t at = index;

		while (at > 0 &&
		       ActionGenCandidateCompare(&value, &candidates[at - 1]) < 0)
		{
			candidates[at] = candidates[at - 1];
			at--;
		}
		candidates[at] = value;
	}
}

static int ActionGenSameGroup(
	const sg_compound_action_gen_candidate_t *first,
	const sg_compound_action_gen_candidate_t *second)
{
	return first->source == second->source &&
	       first->trigger_key == second->trigger_key &&
	       first->mover_key == second->mover_key &&
	       first->mode == second->mode &&
	       ActionGenSameAnchor(first->mechanism_anchor,
	                           second->mechanism_anchor);
}

static int ActionGenTimingValid(
	const sg_compound_action_gen_proof_t *proof)
{
	long long composed;
	long long touch_frame_end;

	if (!proof || proof->touch_ms <= 0 ||
	    proof->touch_ms > RUNE_MAX_COST_MS ||
	    proof->touch_ms % SG_RUNE_PROOF_PMOVE_SUBSTEP_MS != 0 ||
	    proof->touch_frame_end_ms <= 0 ||
	    proof->touch_frame_end_ms > RUNE_MAX_COST_MS ||
	    proof->touch_frame_end_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    proof->mover_top_ms < 2 * SG_RUNE_PROOF_SERVER_FRAME_MS ||
	    proof->mover_top_ms > RUNE_MAX_COST_MS ||
	    proof->mover_top_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    proof->suffix_start_ms < SG_RUNE_PROOF_SERVER_FRAME_MS ||
	    proof->suffix_start_ms > RUNE_MAX_COST_MS ||
	    proof->suffix_start_ms !=
	        proof->mover_top_ms - SG_RUNE_PROOF_SERVER_FRAME_MS)
		return 0;
	touch_frame_end =
		((long long)proof->touch_ms + SG_RUNE_PROOF_SERVER_FRAME_MS - 1LL) /
		SG_RUNE_PROOF_SERVER_FRAME_MS * SG_RUNE_PROOF_SERVER_FRAME_MS;
	composed = (long long)proof->touch_frame_end_ms +
	           proof->suffix_start_ms + proof->arrival_ms;
	return touch_frame_end == proof->touch_frame_end_ms &&
	       proof->total_cost_ms >= RUNE_MIN_COST_MS &&
	       proof->total_cost_ms <= RUNE_MAX_COST_MS &&
	       proof->total_cost_ms % SG_RUNE_PROOF_SERVER_FRAME_MS == 0 &&
	       composed == proof->total_cost_ms && proof->arrival_ms > 0 &&
	       proof->arrival_ms % SG_RUNE_PROOF_SERVER_FRAME_MS == 0 &&
	       proof->sweep_clear_ms > 0 &&
	       proof->sweep_clear_ms % SG_RUNE_PROOF_SERVER_FRAME_MS == 0 &&
	       proof->sweep_clear_ms <= proof->arrival_ms;
}

static int ActionGenBuildLink(rune_link_t *link, int action,
	const sg_compound_action_gen_candidate_t *candidate,
	const sg_compound_action_gen_proof_t *proof,
	const sg_compound_action_gen_seed_t *seeds, size_t seed_count)
{
	rune_seed_t endpoints[2];

	if (!link || !candidate || !proof || !seeds ||
	    !ActionGenTimingValid(proof))
		return 0;
	memset(link, 0, sizeof(*link));
	link->from = candidate->source;
	link->to = candidate->destination;
	link->action = (byte)action;
	link->provenance = (byte)RL_CONTRACTED;
	link->heading = proof->heading;
	link->heading_slack = proof->heading_slack;
	link->exit_speed = proof->exit_speed;
	link->cost_ms = (short)proof->total_cost_ms;
	memcpy(link->anchor, proof->suffix_anchor, sizeof(link->anchor));
	memcpy(link->mechanism_anchor, candidate->mechanism_anchor,
	       sizeof(link->mechanism_anchor));
	link->sweep_clear_ms = (unsigned short)proof->sweep_clear_ms;
	link->mode = candidate->mode;
	link->mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	memset(endpoints, 0, sizeof(endpoints));
	endpoints[0].flags = seeds[candidate->source].water ? RSF_WATER : 0;
	endpoints[1].flags = seeds[candidate->destination].water ? RSF_WATER : 0;
	return seed_count <= RUNE_MAX_SEEDS &&
	       SG_CompoundValidateLink(endpoints, 2, &(rune_link_t){
	           .from = 0,
	           .to = 1,
	           .action = link->action,
	           .provenance = link->provenance,
	           .min_speed = link->min_speed,
	           .heading = link->heading,
	           .heading_slack = link->heading_slack,
	           .exit_speed = link->exit_speed,
	           .cost_ms = link->cost_ms,
	           .anchor = { link->anchor[0], link->anchor[1], link->anchor[2] },
	           .mechanism_anchor = {
	               link->mechanism_anchor[0], link->mechanism_anchor[1],
	               link->mechanism_anchor[2]
	           },
	           .sweep_clear_ms = link->sweep_clear_ms,
	           .mode = link->mode,
	           .mechanism_plan = RUNE_NO_MECHANISM_PLAN
	       }) == RLR_OK;
}

static int ActionGenProvenCheaper(
	const sg_compound_action_gen_proven_t *candidate,
	const sg_compound_action_gen_proven_t *current, int current_set)
{
	return !current_set || candidate->local_rank < current->local_rank ||
	       (candidate->local_rank == current->local_rank &&
	        candidate->link.to < current->link.to);
}

static int ActionGenSameProven(
	const sg_compound_action_gen_proven_t *first,
	const sg_compound_action_gen_proven_t *second)
{
	return first->link.from == second->link.from &&
	       first->link.to == second->link.to &&
	       first->trigger_key == second->trigger_key &&
	       first->mover_key == second->mover_key &&
	       first->link.mode == second->link.mode;
}

static int ActionGenProvenCompare(
	const sg_compound_action_gen_proven_t *first,
	const sg_compound_action_gen_proven_t *second)
{
	if (first->link.from != second->link.from)
		return first->link.from < second->link.from ? -1 : 1;
	if (first->link.to != second->link.to)
		return first->link.to < second->link.to ? -1 : 1;
	if (first->link.cost_ms != second->link.cost_ms)
		return first->link.cost_ms < second->link.cost_ms ? -1 : 1;
	if (first->local_rank != second->local_rank)
		return first->local_rank < second->local_rank ? -1 : 1;
	return 0;
}

static void ActionGenSortProven(sg_compound_action_gen_proven_t *items,
	size_t count)
{
	size_t index;

	for (index = 1; index < count; index++)
	{
		sg_compound_action_gen_proven_t value = items[index];
		size_t at = index;

		while (at > 0 && ActionGenProvenCompare(&value, &items[at - 1]) < 0)
		{
			items[at] = items[at - 1];
			at--;
		}
		items[at] = value;
	}
}

sg_compound_action_gen_result_t SG_CompoundActionGenPlan(
	const sg_compound_action_gen_request_t *request)
{
	sg_compound_action_gen_candidate_t candidates[
		SG_COMPOUND_ACTION_GEN_MAX_CANDIDATES];
	sg_compound_action_gen_proven_t proven[
		SG_COMPOUND_ACTION_GEN_MAX_SELECTED];
	sg_compound_action_gen_result_t result =
		ActionGenResult(SG_COMPOUND_ACTION_GEN_INVALID);
	size_t proven_count = 0;
	size_t group_start;
	int saw_candidate = 0;

	if (!request)
		return result;
	if (!request->production_enabled)
		return ActionGenResult(SG_COMPOUND_ACTION_GEN_DISABLED);
	if ((request->action != RL_DOOR_DROP &&
	     request->action != RL_DOOR_HOOK) ||
	    !request->seeds || request->seed_count == 0 ||
	    request->seed_count > RUNE_MAX_SEEDS || !request->candidates ||
	    request->candidate_count == 0 || !request->prove ||
	    request->output_capacity > RUNE_MAX_LINKS ||
	    (request->output_capacity > 0 && !request->output))
		return result;
	if (request->candidate_count > SG_COMPOUND_ACTION_GEN_MAX_CANDIDATES)
		return ActionGenResult(SG_COMPOUND_ACTION_GEN_BUDGET);
	for (group_start = 0; group_start < request->seed_count; group_start++)
	{
		const sg_compound_action_gen_seed_t *seed = &request->seeds[group_start];

		if (seed->component < -1 ||
		    (seed->objective_mask & ~SG_COMPOUND_ACTION_GEN_OBJECTIVE_MASK) ||
		    seed->water > 1 || seed->has_incoming > 1 ||
		    seed->has_outgoing > 1)
			return result;
	}
	for (group_start = 0; group_start < request->candidate_count;
	     group_start++)
	{
		const sg_compound_action_gen_candidate_t *candidate =
			&request->candidates[group_start];

		if (candidate->source < 0 ||
		    (size_t)candidate->source >= request->seed_count ||
		    candidate->destination < 0 ||
		    (size_t)candidate->destination >= request->seed_count ||
		    candidate->source == candidate->destination ||
		    candidate->trigger_key <= 0 || candidate->mover_key <= 0 ||
		    candidate->trigger_key == candidate->mover_key ||
		    !ActionGenAnchorValid(candidate->mechanism_anchor) ||
		    candidate->mode == RLCM_NONE ||
		    !SG_ActionAllowsMode(request->action, candidate->mode) ||
		    !SG_ActionEndpointAllowed(request->action,
		        request->seeds[candidate->source].water,
		        request->seeds[candidate->destination].water))
			return result;
	}
	memcpy(candidates, request->candidates,
	       request->candidate_count * sizeof(candidates[0]));
	ActionGenSortCandidates(candidates, request->candidate_count);
	for (group_start = 0; group_start < request->candidate_count; )
	{
		const sg_compound_action_gen_candidate_t *first =
			&candidates[group_start];
		size_t group_end;
		size_t index;

		for (group_end = group_start + 1;
		     group_end < request->candidate_count &&
		     ActionGenSameGroup(first, &candidates[group_end]);
		     group_end++)
			;
		for (index = group_start; index < group_end; index++)
		{
			const sg_compound_action_gen_candidate_t *candidate =
				&candidates[index];

			if (!ActionGenSameGroup(first, candidate) ||
			    (index > group_start &&
			     candidates[index - 1].destination == candidate->destination))
				return ActionGenResult(index > group_start &&
				    candidates[index - 1].destination == candidate->destination
				        ? SG_COMPOUND_ACTION_GEN_DUPLICATE
				        : SG_COMPOUND_ACTION_GEN_INVALID);
		}
		group_start = group_end;
	}
	for (group_start = 0; group_start < request->candidate_count; )
	{
		const sg_compound_action_gen_candidate_t *first =
			&candidates[group_start];
		const sg_compound_action_gen_seed_t *source =
			&request->seeds[first->source];
		sg_compound_action_gen_proven_t slots[4];
		int slot_set[4] = { 0, 0, 0, 0 };
		uint8_t missing = (uint8_t)(SG_COMPOUND_ACTION_GEN_OBJECTIVE_MASK &
		                            ~source->objective_mask);
		size_t group_end;
		size_t index;

		for (group_end = group_start + 1;
		     group_end < request->candidate_count &&
		     ActionGenSameGroup(first, &candidates[group_end]);
		     group_end++)
			;
		memset(slots, 0, sizeof(slots));
		if (!source->has_incoming)
		{
			group_start = group_end;
			continue;
		}
		for (index = group_start; index < group_end; index++)
		{
			const sg_compound_action_gen_candidate_t *candidate =
				&candidates[index];
			const sg_compound_action_gen_seed_t *destination =
				&request->seeds[candidate->destination];
			sg_compound_action_gen_proof_t proof;
			sg_compound_action_gen_proven_t item;
			uint8_t new_bits;
			int crosses;

			if (!destination->has_outgoing)
				continue;
			new_bits = (uint8_t)(destination->objective_mask & missing);
			crosses = source->component >= 0 && destination->component >= 0 &&
			          source->component != destination->component;
			saw_candidate = 1;
			memset(&proof, 0, sizeof(proof));
			result.proof_calls++;
			if (request->prove(request->context, request->action, candidate,
			                   &proof) != RLR_OK)
				continue;
			memset(&item, 0, sizeof(item));
			if (!ActionGenBuildLink(&item.link, request->action, candidate,
			                        &proof, request->seeds,
			                        request->seed_count))
			{
				result.status = SG_COMPOUND_ACTION_GEN_BAD_PROOF;
				return result;
			}
			item.trigger_key = candidate->trigger_key;
			item.mover_key = candidate->mover_key;
			item.local_rank = candidate->local_rank;
			if (ActionGenProvenCheaper(&item, &slots[0], slot_set[0]))
			{
				slots[0] = item;
				slot_set[0] = 1;
			}
			if ((new_bits & 1U) &&
			    ActionGenProvenCheaper(&item, &slots[1], slot_set[1]))
			{
				slots[1] = item;
				slot_set[1] = 1;
			}
			if ((new_bits & 2U) &&
			    ActionGenProvenCheaper(&item, &slots[2], slot_set[2]))
			{
				slots[2] = item;
				slot_set[2] = 1;
			}
			if (crosses &&
			    ActionGenProvenCheaper(&item, &slots[3], slot_set[3]))
			{
				slots[3] = item;
				slot_set[3] = 1;
			}
		}
		for (index = 0; index < 4; index++)
		{
			size_t prior;

			if (!slot_set[index])
				continue;
			for (prior = 0; prior < index; prior++)
				if (slot_set[prior] &&
				    ActionGenSameProven(&slots[prior], &slots[index]))
					break;
			if (prior < index)
				continue;
			if (proven_count >= SG_COMPOUND_ACTION_GEN_MAX_SELECTED)
			{
				result.status = SG_COMPOUND_ACTION_GEN_BUDGET;
				return result;
			}
			proven[proven_count++] = slots[index];
		}
		group_start = group_end;
	}
	result.selected = proven_count;
	if (!saw_candidate)
	{
		result.status = SG_COMPOUND_ACTION_GEN_NO_IMPROVEMENT;
		return result;
	}
	if (proven_count == 0)
	{
		result.status = SG_COMPOUND_ACTION_GEN_NO_PROOF;
		return result;
	}
	ActionGenSortProven(proven, proven_count);
	{
		size_t unique_count = 0;
		size_t index;

		for (index = 0; index < proven_count; index++)
			if (index == 0 || proven[index].link.from !=
			                      proven[index - 1].link.from ||
			    proven[index].link.to != proven[index - 1].link.to)
				unique_count++;
		if (unique_count > request->output_capacity)
		{
			result.status = SG_COMPOUND_ACTION_GEN_CAPACITY;
			return result;
		}
		for (index = 0; index < proven_count; index++)
		{
			if (index > 0 && proven[index].link.from ==
			                     proven[index - 1].link.from &&
			                 proven[index].link.to ==
			                     proven[index - 1].link.to)
				continue;
			request->output[result.emitted++] = proven[index].link;
		}
	}
	result.status = SG_COMPOUND_ACTION_GEN_OK;
	return result;
}

const char *SG_CompoundActionGenStatusName(
	sg_compound_action_gen_status_t status)
{
	switch (status)
	{
	case SG_COMPOUND_ACTION_GEN_OK: return "ok";
	case SG_COMPOUND_ACTION_GEN_DISABLED: return "disabled";
	case SG_COMPOUND_ACTION_GEN_INVALID: return "invalid";
	case SG_COMPOUND_ACTION_GEN_DUPLICATE: return "duplicate";
	case SG_COMPOUND_ACTION_GEN_BUDGET: return "budget";
	case SG_COMPOUND_ACTION_GEN_NO_IMPROVEMENT: return "no-improvement";
	case SG_COMPOUND_ACTION_GEN_NO_PROOF: return "no-proof";
	case SG_COMPOUND_ACTION_GEN_BAD_PROOF: return "bad-proof";
	case SG_COMPOUND_ACTION_GEN_CAPACITY: return "capacity";
	default: return "unknown";
	}
}
