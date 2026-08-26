#include "../g_local.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sg_local.h"
#include "sg_hooks.h"
#include "sg_compound_gen_game.h"
#include "sg_compound_world.h"

typedef struct sg_compound_gen_game_proof_context_s
{
	const rune_seed_t *seeds;
	size_t rejections[RLR_ACTION_TIMEOUT + 1];
	size_t replay_rejections[SG_REPLAY_REASON_HOOK_TERMINAL_LOST + 1];
} sg_compound_gen_game_proof_context_t;

typedef struct sg_compound_gen_game_contact_s
{
	int source;
	int trigger_key;
	int mover_key;
	vec3_t anchor;
} sg_compound_gen_game_contact_t;

typedef struct sg_compound_gen_game_proven_s
{
	rune_link_t link;
	int trigger_key;
	int mover_key;
	uint32_t local_rank;
} sg_compound_gen_game_proven_t;

static sg_compound_gen_game_result_t GameResult(
	sg_compound_gen_status_t status, rune_reject_reason_t reason)
{
	sg_compound_gen_game_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	result.reason = reason;
	return result;
}

void SG_CompoundGenGameTopologyFree(
	sg_compound_gen_game_topology_t *topology,
	sg_compound_gen_game_free_fn deallocate)
{
	if (!topology || !deallocate)
		return;
	if (topology->component)
		deallocate(topology->component);
	if (topology->objective_mask)
		deallocate(topology->objective_mask);
	topology->component = NULL;
	topology->objective_mask = NULL;
}

static uint32_t GameRank(const vec3_t source, const vec3_t mechanism,
	const vec3_t destination)
{
	uint64_t total = 0U;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		int64_t source_q8 = (int64_t)(short)(source[axis] * 8.0f);
		int64_t mechanism_q8 =
		    (int64_t)(short)(mechanism[axis] * 8.0f);
		int64_t destination_q8 =
		    (int64_t)(short)(destination[axis] * 8.0f);
		int64_t approach = source_q8 - mechanism_q8;
		int64_t suffix = destination_q8 - mechanism_q8;

		total += (uint64_t)(approach * approach) +
		         (uint64_t)(suffix * suffix);
		if (total > UINT32_MAX)
			return UINT32_MAX;
	}
	return (uint32_t)total;
}

static int GameProvenCheaper(
	const sg_compound_gen_game_proven_t *candidate,
	const sg_compound_gen_game_proven_t *current, int current_set)
{
	return !current_set || candidate->local_rank < current->local_rank ||
	       (candidate->local_rank == current->local_rank &&
	        candidate->link.to < current->link.to);
}

static int GameProvenSame(
	const sg_compound_gen_game_proven_t *first,
	const sg_compound_gen_game_proven_t *second)
{
	return first->link.from == second->link.from &&
	       first->link.to == second->link.to &&
	       first->trigger_key == second->trigger_key &&
	       first->mover_key == second->mover_key;
}

static int GameProvenBefore(
	const sg_compound_gen_game_proven_t *first,
	const sg_compound_gen_game_proven_t *second)
{
	int axis;

	if (first->link.from != second->link.from)
		return first->link.from < second->link.from;
	if (first->link.to != second->link.to)
		return first->link.to < second->link.to;
	if (first->link.cost_ms != second->link.cost_ms)
		return first->link.cost_ms < second->link.cost_ms;
	if (first->trigger_key != second->trigger_key)
		return first->trigger_key < second->trigger_key;
	if (first->mover_key != second->mover_key)
		return first->mover_key < second->mover_key;
	for (axis = 0; axis < 3; axis++)
		if (first->link.mechanism_anchor[axis] !=
		    second->link.mechanism_anchor[axis])
			return first->link.mechanism_anchor[axis] <
			       second->link.mechanism_anchor[axis];
	if (first->local_rank != second->local_rank)
		return first->local_rank < second->local_rank;
	if (first->link.sweep_clear_ms != second->link.sweep_clear_ms)
		return first->link.sweep_clear_ms < second->link.sweep_clear_ms;
	return first->link.exit_speed < second->link.exit_speed;
}

static int GameProvenCompare(const void *first, const void *second)
{
	const sg_compound_gen_game_proven_t *left = first;
	const sg_compound_gen_game_proven_t *right = second;

	if (GameProvenBefore(left, right))
		return -1;
	return GameProvenBefore(right, left) ? 1 : 0;
}

static void GameProvenSort(sg_compound_gen_game_proven_t *items, size_t count)
{
	qsort(items, count, sizeof(*items), GameProvenCompare);
}

static int GameContactCompare(const sg_compound_gen_game_contact_t *first,
	const sg_compound_gen_game_contact_t *second)
{
	int axis;

	if (first->source != second->source)
		return first->source < second->source ? -1 : 1;
	if (first->trigger_key != second->trigger_key)
		return first->trigger_key < second->trigger_key ? -1 : 1;
	if (first->mover_key != second->mover_key)
		return first->mover_key < second->mover_key ? -1 : 1;
	for (axis = 0; axis < 3; axis++)
		if (first->anchor[axis] != second->anchor[axis])
			return first->anchor[axis] < second->anchor[axis] ? -1 : 1;
	return 0;
}

static int GameReserve(void **items, size_t *capacity, size_t count,
	size_t required, size_t item_size, sg_compound_gen_game_alloc_fn allocate,
	sg_compound_gen_game_free_fn deallocate)
{
	void *expanded;
	size_t next = *capacity ? *capacity : 64U;
	size_t limit;

	if (required <= *capacity)
		return 1;
	limit = (size_t)INT_MAX / item_size;
	if (required > limit)
		return 0;
	while (next < required)
	{
		if (next > limit / 2U)
		{
			next = limit;
			break;
		}
		next *= 2U;
	}
	expanded = allocate((int)(next * item_size));
	if (!expanded)
		return 0;
	if (*items)
	{
		memcpy(expanded, *items, count * item_size);
		deallocate(*items);
	}
	*items = expanded;
	*capacity = next;
	return 1;
}

static int GameContactCompareQsort(const void *first, const void *second)
{
	return GameContactCompare(first, second);
}

static void GameContactSortDeduplicate(sg_compound_gen_game_contact_t *contacts,
	size_t *count)
{
	size_t read, write = 0U;

	qsort(contacts, *count, sizeof(*contacts), GameContactCompareQsort);
	for (read = 0U; read < *count; read++)
		if (write == 0U ||
		    GameContactCompare(&contacts[read], &contacts[write - 1U]) != 0)
			contacts[write++] = contacts[read];
	*count = write;
}

static rune_reject_reason_t GameProve(void *opaque,
	const sg_compound_gen_candidate_t *candidate,
	sg_compound_gen_proof_t *result)
{
	sg_compound_gen_game_proof_context_t *context = opaque;
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_source_t source;
	sg_compound_swim_proof_t proof;
	sg_phantom_t phantom;
	rune_reject_reason_t reason;
	sg_replay_reason_t replay_reason = SG_REPLAY_REASON_NONE;

	if (!context || !context->seeds || !candidate || !result)
		return RLR_BAD_CONTROL_POLICY;
	memset(result, 0, sizeof(*result));
	memset(&resolved, 0, sizeof(resolved));
	reason = SG_CompoundWorldResolvePreopen(candidate->mechanism_anchor,
	                                      &resolved);
	if (reason != RLR_OK ||
	    resolved.trigger_key != candidate->trigger_key ||
	    resolved.mover_key != candidate->mover_key)
	{
		reason = reason == RLR_OK ? RLR_MECHANISM_UNRESOLVED : reason;
		if ((unsigned int)reason <= RLR_ACTION_TIMEOUT)
			context->rejections[reason]++;
		return reason;
	}
	memset(&source, 0, sizeof(source));
	reason = SG_OracleCompoundSwimPrepareSource(
	    context->seeds[candidate->source].origin, &resolved, 0.0f,
	    &source, NULL, true, true);
	if (reason != RLR_OK)
	{
		if ((unsigned int)reason <= RLR_ACTION_TIMEOUT)
			context->rejections[reason]++;
		return reason;
	}
	phantom = source.phantom;
	memset(&proof, 0, sizeof(proof));
	reason = SG_OracleCompoundSwimPreopen(&phantom, &resolved,
	    candidate->mechanism_anchor,
	    context->seeds[candidate->destination].origin,
	    (context->seeds[candidate->destination].flags & RSF_WATER) != 0,
	    source.old_frame_z, &proof, &replay_reason, NULL, true, true);
	if (reason != RLR_OK)
	{
		if ((unsigned int)reason <= RLR_ACTION_TIMEOUT)
			context->rejections[reason]++;
		if ((unsigned int)replay_reason <=
		    SG_REPLAY_REASON_HOOK_TERMINAL_LOST)
			context->replay_rejections[replay_reason]++;
		return reason;
	}
	result->touch_ms = proof.touch_ms;
	result->touch_frame_end_ms = proof.touch_frame_end_ms;
	result->mover_top_ms = proof.mover_top_ms;
	result->suffix_start_ms = proof.suffix_start_ms;
	result->total_cost_ms = proof.total_cost_ms;
	result->arrival_ms = proof.arrival_ms;
	result->sweep_clear_ms = proof.sweep_clear_ms;
	result->exit_speed = proof.exit_speed;
	return RLR_OK;
}

static sg_compound_gen_status_t GamePlanBatch(
	const sg_compound_gen_candidate_t *candidates, size_t candidate_count,
	const sg_compound_gen_seed_t *seeds, size_t seed_count,
	sg_compound_gen_game_proof_context_t *proof_context,
	sg_compound_gen_game_proven_t slots[4], int slot_set[4],
	sg_compound_gen_game_result_t *result)
{
	rune_link_t output[4];
	sg_compound_gen_request_t request;
	sg_compound_gen_result_t plan;
	size_t output_index;

	if (candidate_count == 0U)
		return SG_COMPOUND_GEN_OK;
	memset(&request, 0, sizeof(request));
	request.seeds = seeds;
	request.seed_count = seed_count;
	request.candidates = candidates;
	request.candidate_count = candidate_count;
	request.output = output;
	request.output_capacity = 4U;
	request.prove = GameProve;
	request.context = proof_context;
	request.production_enabled = 1;
	plan = SG_CompoundGenPlan(&request);
	result->proof_calls += plan.proof_calls;
	if (plan.status == SG_COMPOUND_GEN_NO_IMPROVEMENT ||
	    plan.status == SG_COMPOUND_GEN_NO_PROOF)
		return SG_COMPOUND_GEN_OK;
	if (plan.status != SG_COMPOUND_GEN_OK)
		return plan.status;
	for (output_index = 0U; output_index < plan.emitted; output_index++)
	{
		sg_compound_gen_game_proven_t proven;
		const sg_compound_gen_candidate_t *candidate = NULL;
		uint8_t missing;
		uint8_t new_objective;
		int crosses;
		int slot;

		for (size_t index = 0U; index < candidate_count; index++)
			if (candidates[index].source == output[output_index].from &&
			    candidates[index].destination == output[output_index].to)
			{
				candidate = &candidates[index];
				break;
			}
		if (!candidate)
			return SG_COMPOUND_GEN_BAD_PROOF;
		memset(&proven, 0, sizeof(proven));
		proven.link = output[output_index];
		proven.trigger_key = candidate->trigger_key;
		proven.mover_key = candidate->mover_key;
		proven.local_rank = candidate->local_rank;
		missing = (uint8_t)(SG_COMPOUND_GEN_OBJECTIVE_MASK &
		    ~seeds[candidate->source].objective_mask);
		new_objective = (uint8_t)(
		    seeds[candidate->destination].objective_mask & missing);
		crosses = seeds[candidate->source].component >= 0 &&
		    seeds[candidate->destination].component >= 0 &&
		    seeds[candidate->source].component !=
		        seeds[candidate->destination].component;
		for (slot = 0; slot < 4; slot++)
		{
			int qualifies = slot == 0 ||
			    (slot == 1 && (new_objective & 1U)) ||
			    (slot == 2 && (new_objective & 2U)) ||
			    (slot == 3 && crosses);

			if (qualifies && GameProvenCheaper(&proven, &slots[slot],
			        slot_set[slot]))
			{
				slots[slot] = proven;
				slot_set[slot] = 1;
			}
		}
	}
	return SG_COMPOUND_GEN_OK;
}

sg_compound_gen_game_result_t SG_CompoundGenGameBuild(
	const sg_compound_gen_game_request_t *request)
{
	sg_compound_world_candidate_t *mechanisms = NULL;
	sg_compound_gen_seed_t *seeds = NULL;
	sg_compound_gen_candidate_t *candidates = NULL;
	sg_compound_gen_game_contact_t *contacts = NULL;
	sg_compound_gen_game_proven_t *selected = NULL;
	sg_compound_gen_game_proof_context_t proof_context;
	sg_compound_gen_game_result_t result =
	    GameResult(SG_COMPOUND_GEN_INVALID, RLR_BAD_CONTROL_POLICY);
	size_t selected_count = 0U;
	size_t selected_capacity = 0U;
	size_t contact_count = 0U;
	size_t contact_capacity = 0U;
	size_t candidate_capacity = 0U;
	size_t source_index, destination_index, link_index;
	int mechanism_count = 0;
	int mechanism_index;
	rune_reject_reason_t reason;

	if (!request || !request->seeds || request->seed_count == 0U ||
	    request->seed_count > RUNE_MAX_SEEDS ||
	    !request->links || !request->link_count ||
	    *request->link_count > request->link_capacity ||
	    !request->components || !request->objective_masks ||
	    !request->allocate || !request->deallocate)
		return result;
	for (link_index = 0; link_index < *request->link_count; link_index++)
		if (request->links[link_index].from < 0 ||
		    request->links[link_index].to < 0 ||
		    (size_t)request->links[link_index].from >= request->seed_count ||
		    (size_t)request->links[link_index].to >= request->seed_count)
			return GameResult(SG_COMPOUND_GEN_INVALID, RLR_BAD_INDEX);
	reason = SG_CompoundWorldEnumeratePreopen(NULL, 0, &mechanism_count);
	if (reason != RLR_OK)
		return GameResult(SG_COMPOUND_GEN_INVALID, reason);
	if (mechanism_count == 0)
		return GameResult(SG_COMPOUND_GEN_OK, RLR_OK);
	if (mechanism_count < 0 ||
	    (size_t)mechanism_count > (size_t)INT_MAX / sizeof(*mechanisms) ||
	    request->seed_count > (size_t)INT_MAX / sizeof(*seeds) ||
	    request->seed_count > (size_t)INT_MAX / sizeof(*candidates))
		return GameResult(SG_COMPOUND_GEN_CAPACITY, RLR_OK);
	mechanisms = request->allocate((int)(sizeof(*mechanisms) *
	    (size_t)mechanism_count));
	seeds = request->allocate((int)(sizeof(*seeds) * request->seed_count));
	if (!mechanisms || !seeds ||
	    !GameReserve((void **)&candidates, &candidate_capacity, 0U,
	        request->seed_count, sizeof(*candidates), request->allocate,
	        request->deallocate))
	{
		result.status = SG_COMPOUND_GEN_CAPACITY;
		result.reason = RLR_OK;
		goto done;
	}
	memset(mechanisms, 0, sizeof(*mechanisms) * (size_t)mechanism_count);
	memset(seeds, 0, sizeof(*seeds) * request->seed_count);
	memset(&proof_context, 0, sizeof(proof_context));
	proof_context.seeds = request->seeds;
	reason = SG_CompoundWorldEnumeratePreopen(mechanisms, mechanism_count,
	                                         &mechanism_count);
	if (reason != RLR_OK)
	{
		result.reason = reason;
		goto done;
	}
	for (source_index = 0; source_index < request->seed_count; source_index++)
	{
		seeds[source_index].component = request->components[source_index];
		seeds[source_index].objective_mask =
		    request->objective_masks[source_index];
		seeds[source_index].water =
		    (request->seeds[source_index].flags & RSF_WATER) != 0;
	}
	for (link_index = 0; link_index < *request->link_count; link_index++)
	{
		seeds[request->links[link_index].from].has_outgoing = 1U;
		seeds[request->links[link_index].to].has_incoming = 1U;
	}
	for (mechanism_index = 0; mechanism_index < mechanism_count;
	     mechanism_index++)
	{
		const sg_compound_world_candidate_t *mechanism =
		    &mechanisms[mechanism_index];

		for (source_index = 0; source_index < request->seed_count;
		     source_index++)
		{
			sg_compound_swim_source_t prepared;
			int hint_index;

			if (!seeds[source_index].water ||
			    !seeds[source_index].has_incoming)
				continue;
			memset(&prepared, 0, sizeof(prepared));
			reason = SG_OracleCompoundSwimPrepareSource(
			    request->seeds[source_index].origin,
			    &mechanism->resolved, 0.0f, &prepared, NULL, true, true);
			if (reason != RLR_OK)
				continue;
			for (hint_index = 0; hint_index < mechanism->hint_count;
			     hint_index++)
			{
				sg_compound_gen_game_contact_t contact;

				memset(&contact, 0, sizeof(contact));
				reason = SG_OracleCompoundSwimDiscoverContact(
				    &prepared, &mechanism->resolved,
				    mechanism->hints[hint_index], contact.anchor, NULL, true,
				    true);
				if (reason != RLR_OK)
					continue;
				contact.source = (int)source_index;
				contact.trigger_key = mechanism->resolved.trigger_key;
				contact.mover_key = mechanism->resolved.mover_key;
				if (!GameReserve((void **)&contacts, &contact_capacity,
				    contact_count, contact_count + 1U, sizeof(*contacts),
				    request->allocate, request->deallocate))
				{
					result.status = SG_COMPOUND_GEN_CAPACITY;
					result.reason = RLR_OK;
					goto done;
				}
				contacts[contact_count++] = contact;
			}
		}
	}
	if (contact_count)
		GameContactSortDeduplicate(contacts, &contact_count);
	for (source_index = 0U; source_index < contact_count; source_index++)
	{
		const sg_compound_gen_game_contact_t *contact =
		    &contacts[source_index];
		sg_compound_world_preopen_t resolved;
		sg_compound_gen_game_proven_t slots[4];
		int slot_set[4] = { 0, 0, 0, 0 };
		size_t candidate_count = 0U;
		int slot;

		memset(&resolved, 0, sizeof(resolved));
		if (SG_CompoundWorldResolvePreopen(contact->anchor, &resolved) !=
		        RLR_OK ||
		    resolved.trigger_key != contact->trigger_key ||
		    resolved.mover_key != contact->mover_key)
			continue;
		memset(slots, 0, sizeof(slots));
		for (destination_index = 0;
		     destination_index < request->seed_count; destination_index++)
		{
			sg_compound_gen_candidate_t *candidate;

			if (destination_index == (size_t)contact->source ||
			    !seeds[destination_index].has_outgoing ||
			    !SG_CompoundWorldCrossesSweep(&resolved, contact->anchor,
			        request->seeds[destination_index].origin))
				continue;
			candidate = &candidates[candidate_count++];
			memset(candidate, 0, sizeof(*candidate));
			candidate->source = contact->source;
			candidate->destination = (int)destination_index;
			candidate->trigger_key = contact->trigger_key;
			candidate->mover_key = contact->mover_key;
			VectorCopy(contact->anchor, candidate->mechanism_anchor);
			candidate->local_rank = GameRank(
			    request->seeds[contact->source].origin, contact->anchor,
			    request->seeds[destination_index].origin);
			result.candidates++;
		}
		{
			sg_compound_gen_status_t status = GamePlanBatch(candidates,
			    candidate_count, seeds, request->seed_count, &proof_context,
			    slots, slot_set, &result);

			if (status != SG_COMPOUND_GEN_OK)
			{
				result.status = status;
				goto done;
			}
		}
		for (slot = 0; slot < 4; slot++)
		{
			int prior;

			if (!slot_set[slot])
				continue;
			for (prior = 0; prior < slot; prior++)
				if (slot_set[prior] &&
				    GameProvenSame(&slots[prior], &slots[slot]))
					break;
			if (prior < slot)
				continue;
			if (!GameReserve((void **)&selected, &selected_capacity,
			    selected_count, selected_count + 1U, sizeof(*selected),
			    request->allocate, request->deallocate))
			{
				result.status = SG_COMPOUND_GEN_CAPACITY;
				result.reason = RLR_OK;
				goto done;
			}
			selected[selected_count++] = slots[slot];
		}
	}
	result.selected = selected_count;
	if (result.candidates == 0U)
	{
		result.status = SG_COMPOUND_GEN_OK;
		result.reason = RLR_OK;
		goto done;
	}
	result.reason = RLR_OK;
	result.status = selected_count == 0U ? SG_COMPOUND_GEN_NO_PROOF :
	    SG_COMPOUND_GEN_OK;
	{
		int reject;

		for (reject = 1; reject <= RLR_ACTION_TIMEOUT; reject++)
			if (proof_context.rejections[reject] > result.proof_rejections)
			{
				result.proof_rejection = (rune_reject_reason_t)reject;
				result.proof_rejections = proof_context.rejections[reject];
			}
		for (reject = 1; reject <= SG_REPLAY_REASON_HOOK_TERMINAL_LOST;
		     reject++)
			if (proof_context.replay_rejections[reject] >
		        result.replay_rejections)
			{
				result.replay_rejection = (sg_replay_reason_t)reject;
				result.replay_rejections =
				    proof_context.replay_rejections[reject];
			}
	}
	if (result.status != SG_COMPOUND_GEN_OK)
		goto done;
	GameProvenSort(selected, selected_count);
	result.emitted = 0U;
	for (source_index = 0U; source_index < selected_count; source_index++)
		if (source_index == 0U ||
		    selected[source_index].link.from !=
		        selected[source_index - 1U].link.from ||
		    selected[source_index].link.to !=
		        selected[source_index - 1U].link.to)
			result.emitted++;
	if (result.emitted > request->link_capacity - *request->link_count)
	{
		result.status = SG_COMPOUND_GEN_CAPACITY;
		result.emitted = 0U;
		goto done;
	}
	for (source_index = 0U; source_index < selected_count; source_index++)
	{
		if (source_index > 0U &&
		    selected[source_index].link.from ==
		        selected[source_index - 1U].link.from &&
		    selected[source_index].link.to ==
		        selected[source_index - 1U].link.to)
			continue;
		request->links[(*request->link_count)++] = selected[source_index].link;
	}

done:
	if (selected)
		request->deallocate(selected);
	if (contacts)
		request->deallocate(contacts);
	if (candidates)
		request->deallocate(candidates);
	if (mechanisms)
		request->deallocate(mechanisms);
	if (seeds)
		request->deallocate(seeds);
	return result;
}

int SG_CompoundGenGameGenerate(const rune_seed_t *seeds, size_t seed_count,
	rune_link_t *links, int *link_count, size_t link_capacity,
	const sg_compound_gen_game_topology_t *topology,
	sg_compound_gen_game_alloc_fn allocate,
	sg_compound_gen_game_free_fn deallocate)
{
	sg_compound_gen_game_request_t request;
	sg_compound_gen_game_result_t result;
	size_t count;

	if (!link_count || *link_count < 0 || !topology)
		return 0;
	if (!SG_ActionMechanismAdmitted(RL_DOOR_SWIM))
		return 1;
	memset(&request, 0, sizeof(request));
	count = (size_t)*link_count;
	request.seeds = seeds;
	request.seed_count = seed_count;
	request.links = links;
	request.link_count = &count;
	request.link_capacity = link_capacity;
	request.components = topology->component;
	request.objective_masks = topology->objective_mask;
	request.allocate = allocate;
	request.deallocate = deallocate;
	result = SG_CompoundGenGameBuild(&request);
	sg_host.dprint("rune: compound status=%s candidates=%zu selected=%zu "
	               "proofs=%zu emitted=%zu reason=%d reject=%d rejects=%zu "
	               "replay=%d replay_rejects=%zu\n",
	               SG_CompoundGenStatusName(result.status),
	               result.candidates, result.selected, result.proof_calls,
	               result.emitted, (int)result.reason,
	               (int)result.proof_rejection,
	               result.proof_rejections,
	               (int)result.replay_rejection,
	               result.replay_rejections);
	if (result.status != SG_COMPOUND_GEN_OK || count > link_capacity)
		return 0;
	*link_count = (int)count;
	return 1;
}
