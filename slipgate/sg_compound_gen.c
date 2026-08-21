/* sg_compound_gen.c -- allocation-free topology selection for D_SWIM. */
#include "../q_shared.h"
#include "sg_compound_gen.h"

#include <limits.h>
#include <math.h>
#include <string.h>

typedef struct sg_compound_gen_proven_s
{
	rune_link_t link;
	int trigger_key;
	int mover_key;
	uint32_t local_rank;
} sg_compound_gen_proven_t;

static int CompoundGenAnchorValid(const float anchor[3])
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
		    scaled > (float)SHRT_MAX ||
		    scaled != (float)(short)scaled)
			return 0;
	}
	return 1;
}

static int CompoundGenCandidateGroupCompare(
	const sg_compound_gen_candidate_t *first,
	const sg_compound_gen_candidate_t *second)
{
	int axis;

	if (first->source != second->source)
		return first->source < second->source ? -1 : 1;
	if (first->trigger_key != second->trigger_key)
		return first->trigger_key < second->trigger_key ? -1 : 1;
	if (first->mover_key != second->mover_key)
		return first->mover_key < second->mover_key ? -1 : 1;
	for (axis = 0; axis < 3; axis++)
		if (first->mechanism_anchor[axis] !=
		    second->mechanism_anchor[axis])
			return first->mechanism_anchor[axis] <
			       second->mechanism_anchor[axis] ? -1 : 1;
	if (first->destination != second->destination)
		return first->destination < second->destination ? -1 : 1;
	if (first->local_rank != second->local_rank)
		return first->local_rank < second->local_rank ? -1 : 1;
	return 0;
}

static void CompoundGenSortCandidates(sg_compound_gen_candidate_t *items,
	size_t count)
{
	size_t index;

	for (index = 1; index < count; index++)
	{
		sg_compound_gen_candidate_t value = items[index];
		size_t at = index;

		while (at > 0 &&
		       CompoundGenCandidateGroupCompare(&value,
		                                        &items[at - 1]) < 0)
		{
			items[at] = items[at - 1];
			at--;
		}
		items[at] = value;
	}
}

static int CompoundGenSameAnchor(const float first[3],
	const float second[3])
{
	return first[0] == second[0] && first[1] == second[1] &&
	       first[2] == second[2];
}

static int CompoundGenSameGroup(const sg_compound_gen_candidate_t *first,
	const sg_compound_gen_candidate_t *second)
{
	return first->source == second->source &&
	       first->trigger_key == second->trigger_key &&
	       first->mover_key == second->mover_key &&
	       CompoundGenSameAnchor(first->mechanism_anchor,
	                             second->mechanism_anchor);
}

static int CompoundGenProofValid(const sg_compound_gen_proof_t *proof)
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
	    ((long long)proof->touch_ms +
	     (long long)SG_RUNE_PROOF_SERVER_FRAME_MS - 1LL) /
	    (long long)SG_RUNE_PROOF_SERVER_FRAME_MS *
	    (long long)SG_RUNE_PROOF_SERVER_FRAME_MS;
	if (touch_frame_end != (long long)proof->touch_frame_end_ms)
		return 0;
	composed = (long long)proof->touch_frame_end_ms +
	           (long long)proof->suffix_start_ms +
	           (long long)proof->arrival_ms;
	return proof->total_cost_ms >= RUNE_MIN_COST_MS &&
	       proof->total_cost_ms <= RUNE_MAX_COST_MS &&
	       proof->total_cost_ms % SG_RUNE_PROOF_SERVER_FRAME_MS == 0 &&
	       composed == (long long)proof->total_cost_ms &&
	       proof->arrival_ms > 0 &&
	       proof->arrival_ms % SG_RUNE_PROOF_SERVER_FRAME_MS == 0 &&
	       proof->arrival_ms <= proof->total_cost_ms &&
	       proof->sweep_clear_ms > 0 &&
	       proof->sweep_clear_ms % SG_RUNE_PROOF_SERVER_FRAME_MS == 0 &&
	       proof->sweep_clear_ms <= proof->arrival_ms;
}

static void CompoundGenBuildProven(sg_compound_gen_proven_t *record,
	const sg_compound_gen_candidate_t *candidate,
	const sg_compound_gen_proof_t *proof)
{
	memset(record, 0, sizeof(*record));
	record->link.from = candidate->source;
	record->link.to = candidate->destination;
	record->link.action = (byte)RL_DOOR_SWIM;
	record->link.provenance = (byte)RL_CONTRACTED;
	record->link.mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	record->link.exit_speed = proof->exit_speed;
	record->link.cost_ms = (short)proof->total_cost_ms;
	record->link.mechanism_anchor[0] = candidate->mechanism_anchor[0];
	record->link.mechanism_anchor[1] = candidate->mechanism_anchor[1];
	record->link.mechanism_anchor[2] = candidate->mechanism_anchor[2];
	record->link.sweep_clear_ms = (unsigned short)proof->sweep_clear_ms;
	record->link.mode = (byte)RLCM_PREOPEN;
	record->trigger_key = candidate->trigger_key;
	record->mover_key = candidate->mover_key;
	record->local_rank = candidate->local_rank;
}

static int CompoundGenProvenLocallyCheaper(
	const sg_compound_gen_proven_t *candidate,
	const sg_compound_gen_proven_t *current)
{
	return !current || candidate->local_rank < current->local_rank ||
	       (candidate->local_rank == current->local_rank &&
	        candidate->link.to < current->link.to);
}

static int CompoundGenSameProvenCandidate(
	const sg_compound_gen_proven_t *first,
	const sg_compound_gen_proven_t *second)
{
	return first->link.from == second->link.from &&
	       first->link.to == second->link.to &&
	       first->trigger_key == second->trigger_key &&
	       first->mover_key == second->mover_key;
}

static int CompoundGenProvenCompare(const sg_compound_gen_proven_t *first,
	const sg_compound_gen_proven_t *second)
{
	int axis;

	if (first->link.from != second->link.from)
		return first->link.from < second->link.from ? -1 : 1;
	if (first->link.to != second->link.to)
		return first->link.to < second->link.to ? -1 : 1;
	if (first->link.cost_ms != second->link.cost_ms)
		return first->link.cost_ms < second->link.cost_ms ? -1 : 1;
	if (first->trigger_key != second->trigger_key)
		return first->trigger_key < second->trigger_key ? -1 : 1;
	if (first->mover_key != second->mover_key)
		return first->mover_key < second->mover_key ? -1 : 1;
	for (axis = 0; axis < 3; axis++)
		if (first->link.mechanism_anchor[axis] !=
		    second->link.mechanism_anchor[axis])
			return first->link.mechanism_anchor[axis] <
			       second->link.mechanism_anchor[axis] ? -1 : 1;
	if (first->local_rank != second->local_rank)
		return first->local_rank < second->local_rank ? -1 : 1;
	if (first->link.sweep_clear_ms != second->link.sweep_clear_ms)
		return first->link.sweep_clear_ms < second->link.sweep_clear_ms
		    ? -1 : 1;
	if (first->link.exit_speed != second->link.exit_speed)
		return first->link.exit_speed < second->link.exit_speed ? -1 : 1;
	return 0;
}

static void CompoundGenSortProven(sg_compound_gen_proven_t *items,
	size_t count)
{
	size_t index;

	for (index = 1; index < count; index++)
	{
		sg_compound_gen_proven_t value = items[index];
		size_t at = index;

		while (at > 0 && CompoundGenProvenCompare(&value,
		                                                &items[at - 1]) < 0)
		{
			items[at] = items[at - 1];
			at--;
		}
		items[at] = value;
	}
}

static sg_compound_gen_result_t CompoundGenResult(
	sg_compound_gen_status_t status)
{
	sg_compound_gen_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	return result;
}

sg_compound_gen_result_t SG_CompoundGenPlan(
	const sg_compound_gen_request_t *request)
{
	sg_compound_gen_candidate_t candidates[SG_COMPOUND_GEN_MAX_CANDIDATES];
	sg_compound_gen_proven_t proven[SG_COMPOUND_GEN_MAX_SELECTED];
	sg_compound_gen_result_t result = CompoundGenResult(
		SG_COMPOUND_GEN_INVALID);
	size_t proven_count = 0;
	size_t group_start;
	int saw_destination = 0;

	/* Deliberately first: the production-disabled path is inert even when no
	 * discovery/proof capability has been supplied. */
	if (!request || !request->production_enabled)
		return CompoundGenResult(SG_COMPOUND_GEN_DISABLED);
	if (!request->seeds || request->seed_count == 0 ||
	    !request->candidates || request->candidate_count == 0 ||
	    !request->prove ||
	    (request->output_capacity > 0 && !request->output))
		return result;
	if (request->candidate_count > SG_COMPOUND_GEN_MAX_CANDIDATES)
		return CompoundGenResult(SG_COMPOUND_GEN_BUDGET);
	if (request->seed_count > RUNE_MAX_SEEDS ||
	    request->output_capacity > RUNE_MAX_LINKS)
		return result;
	{
		size_t seed_index;

		for (seed_index = 0; seed_index < request->seed_count; seed_index++)
		{
			const sg_compound_gen_seed_t *seed = &request->seeds[seed_index];

			if (seed->component < -1 ||
			    (seed->objective_mask &
			     ~SG_COMPOUND_GEN_OBJECTIVE_MASK) != 0 ||
			    seed->water > 1 ||
			    seed->has_incoming > 1 || seed->has_outgoing > 1)
				return result;
		}
	}

	memcpy(candidates, request->candidates,
	       request->candidate_count * sizeof(candidates[0]));
	CompoundGenSortCandidates(candidates, request->candidate_count);

	/* Validate the complete bounded input before crossing the injected proof
	 * boundary.  A malformed later group therefore cannot leave externally
	 * visible proof work behind.  The candidate cap also bounds group count;
	 * no smaller map-shape-dependent group cap is imposed. */
	for (group_start = 0; group_start < request->candidate_count; )
	{
		const sg_compound_gen_candidate_t *first = &candidates[group_start];
		size_t group_end, index;

		for (group_end = group_start + 1;
		     group_end < request->candidate_count &&
		     CompoundGenSameGroup(first, &candidates[group_end]);
		     group_end++)
			;
		if (first->source < 0 ||
		    (size_t)first->source >= request->seed_count ||
		    first->trigger_key <= 0 || first->mover_key <= 0 ||
		    first->trigger_key == first->mover_key ||
		    !CompoundGenAnchorValid(first->mechanism_anchor))
			return result;
		for (index = group_start; index < group_end; index++)
		{
			const sg_compound_gen_candidate_t *candidate = &candidates[index];

			if (candidate->destination < 0 ||
			    (size_t)candidate->destination >= request->seed_count ||
			    candidate->source == candidate->destination ||
			    !CompoundGenAnchorValid(candidate->mechanism_anchor) ||
			    !CompoundGenSameAnchor(first->mechanism_anchor,
			                           candidate->mechanism_anchor))
				return result;
			if (index > group_start &&
			    candidates[index - 1].destination ==
			        candidate->destination)
				return CompoundGenResult(SG_COMPOUND_GEN_DUPLICATE);
			if ((request->seeds[candidate->destination].objective_mask &
			     ~SG_COMPOUND_GEN_OBJECTIVE_MASK) != 0)
				return result;
		}
		if ((request->seeds[first->source].objective_mask &
		     ~SG_COMPOUND_GEN_OBJECTIVE_MASK) != 0)
			return result;
		group_start = group_end;
	}

	/* Exact proof precedes slot selection.  Every eligible candidate is proved
	 * at most once, so a rejected cheapest candidate naturally falls through
	 * to the next successful local/bit/cross candidate. */
	for (group_start = 0; group_start < request->candidate_count; )
	{
		const sg_compound_gen_candidate_t *first = &candidates[group_start];
		const sg_compound_gen_seed_t *source = &request->seeds[first->source];
		sg_compound_gen_proven_t slots[4];
		int slot_set[4] = { 0, 0, 0, 0 };
		size_t group_end, index;
		uint8_t missing = (uint8_t)(SG_COMPOUND_GEN_OBJECTIVE_MASK &
		                            ~source->objective_mask);

		for (group_end = group_start + 1;
		     group_end < request->candidate_count &&
		     CompoundGenSameGroup(first, &candidates[group_end]);
		     group_end++)
			;
		/* Candidate existence already means the caller prepared this exact
		 * source/mechanism pair and discovered its first contact.  Deep-water
		 * seeds are intentionally not gen_source_stable, so require only graph
		 * ownership here and leave exact source validity to the proof callback. */
		if (!source->water || !source->has_incoming)
		{
			group_start = group_end;
			continue;
		}
		for (index = group_start; index < group_end; index++)
		{
			const sg_compound_gen_candidate_t *candidate = &candidates[index];
			const sg_compound_gen_seed_t *destination =
				&request->seeds[candidate->destination];
			sg_compound_gen_proof_t proof;
			sg_compound_gen_proven_t candidate_proven;
			rune_reject_reason_t reason;
			uint8_t new_bits;
			int crosses;

			if (!destination->has_outgoing)
				continue;
			new_bits = (uint8_t)(destination->objective_mask & missing);
			crosses = source->component >= 0 &&
			          destination->component >= 0 &&
			          source->component != destination->component;
			saw_destination = 1;
			memset(&proof, 0, sizeof(proof));
			result.proof_calls++;
			reason = request->prove(request->context, candidate, &proof);
			if (reason != RLR_OK)
				continue;
			if (!CompoundGenProofValid(&proof))
			{
				result.status = SG_COMPOUND_GEN_BAD_PROOF;
				return result;
			}
			CompoundGenBuildProven(&candidate_proven, candidate, &proof);
			if (CompoundGenProvenLocallyCheaper(&candidate_proven,
			        slot_set[0] ? &slots[0] : NULL))
			{
				slots[0] = candidate_proven;
				slot_set[0] = 1;
			}
			if ((new_bits & 1U) &&
			    CompoundGenProvenLocallyCheaper(&candidate_proven,
			        slot_set[1] ? &slots[1] : NULL))
			{
				slots[1] = candidate_proven;
				slot_set[1] = 1;
			}
			if ((new_bits & 2U) &&
			    CompoundGenProvenLocallyCheaper(&candidate_proven,
			        slot_set[2] ? &slots[2] : NULL))
			{
				slots[2] = candidate_proven;
				slot_set[2] = 1;
			}
			if (crosses &&
			    CompoundGenProvenLocallyCheaper(&candidate_proven,
			        slot_set[3] ? &slots[3] : NULL))
			{
				slots[3] = candidate_proven;
				slot_set[3] = 1;
			}
		}
		{
			size_t slot;

			for (slot = 0; slot < 4; slot++)
			{
				size_t prior;

				if (!slot_set[slot])
					continue;
				for (prior = 0; prior < slot; prior++)
					if (slot_set[prior] &&
					    CompoundGenSameProvenCandidate(&slots[prior],
					                                   &slots[slot]))
						break;
				if (prior < slot)
					continue;
				if (proven_count >= SG_COMPOUND_GEN_MAX_SELECTED)
					return CompoundGenResult(SG_COMPOUND_GEN_BUDGET);
				proven[proven_count++] = slots[slot];
			}
		}
		group_start = group_end;
	}

	result.selected = proven_count;
	if (!saw_destination)
	{
		result.status = SG_COMPOUND_GEN_NO_IMPROVEMENT;
		return result;
	}
	if (proven_count == 0)
	{
		result.status = SG_COMPOUND_GEN_NO_PROOF;
		return result;
	}
	CompoundGenSortProven(proven, proven_count);
	{
		size_t unique_count = 0, index;

		for (index = 0; index < proven_count; index++)
			if (index == 0 || proven[index].link.from !=
			                      proven[index - 1].link.from ||
			                  proven[index].link.to !=
			                      proven[index - 1].link.to)
				unique_count++;
		if (unique_count > request->output_capacity)
		{
			result.status = SG_COMPOUND_GEN_CAPACITY;
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
	result.status = SG_COMPOUND_GEN_OK;
	return result;
}

const char *SG_CompoundGenStatusName(sg_compound_gen_status_t status)
{
	switch (status)
	{
	case SG_COMPOUND_GEN_OK: return "ok";
	case SG_COMPOUND_GEN_DISABLED: return "disabled";
	case SG_COMPOUND_GEN_INVALID: return "invalid";
	case SG_COMPOUND_GEN_DUPLICATE: return "duplicate";
	case SG_COMPOUND_GEN_BUDGET: return "budget";
	case SG_COMPOUND_GEN_NO_IMPROVEMENT: return "no-improvement";
	case SG_COMPOUND_GEN_NO_PROOF: return "no-proof";
	case SG_COMPOUND_GEN_BAD_PROOF: return "bad-proof";
	case SG_COMPOUND_GEN_CAPACITY: return "capacity";
	default: return "unknown";
	}
}
