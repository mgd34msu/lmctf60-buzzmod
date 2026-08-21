#include "../g_local.h"

#include <limits.h>
#include <stdint.h>
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

typedef struct sg_compound_gen_game_destination_s
{
	int index;
	uint32_t rank;
} sg_compound_gen_game_destination_t;

typedef enum sg_compound_gen_game_destination_role_e
{
	SG_COMPOUND_GEN_GAME_DESTINATION_ANY,
	SG_COMPOUND_GEN_GAME_DESTINATION_RED,
	SG_COMPOUND_GEN_GAME_DESTINATION_BLUE,
	SG_COMPOUND_GEN_GAME_DESTINATION_CROSS,
	SG_COMPOUND_GEN_GAME_DESTINATION_ROLE_COUNT
} sg_compound_gen_game_destination_role_t;

#define SG_COMPOUND_GEN_GAME_MAX_CONTACTS 64
#define SG_COMPOUND_GEN_GAME_MAX_ROLE_QUOTA \
	(SG_COMPOUND_GEN_MAX_CANDIDATES / \
	 SG_COMPOUND_GEN_GAME_DESTINATION_ROLE_COUNT)

typedef struct sg_compound_gen_game_contact_s
{
	int source;
	int trigger_key;
	int mover_key;
	vec3_t anchor;
} sg_compound_gen_game_contact_t;

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

static int GameCandidateExists(
	const sg_compound_gen_candidate_t *candidates, size_t count,
	const sg_compound_gen_candidate_t *candidate)
{
	size_t index;

	for (index = 0; index < count; index++)
		if (candidates[index].source == candidate->source &&
		    candidates[index].destination == candidate->destination &&
		    candidates[index].trigger_key == candidate->trigger_key &&
		    candidates[index].mover_key == candidate->mover_key &&
		    memcmp(candidates[index].mechanism_anchor,
		           candidate->mechanism_anchor,
		           sizeof(candidate->mechanism_anchor)) == 0)
			return 1;
	return 0;
}

static void GameDestinationInsert(
	sg_compound_gen_game_destination_t *destinations,
	size_t capacity, int destination, uint32_t rank)
{
	size_t position;

	for (position = 0U; position < capacity; position++)
		if (destinations[position].index < 0 ||
		    rank < destinations[position].rank ||
		    (rank == destinations[position].rank &&
		     destination < destinations[position].index))
			break;
	if (position == capacity)
		return;
	memmove(&destinations[position + 1U], &destinations[position],
	    (capacity - position - 1U) *
	        sizeof(destinations[0]));
	destinations[position].index = destination;
	destinations[position].rank = rank;
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

static int GameContactInsert(sg_compound_gen_game_contact_t *contacts,
	size_t *count, const sg_compound_gen_game_contact_t *contact)
{
	size_t position;

	for (position = 0U; position < *count; position++)
	{
		int order = GameContactCompare(contact, &contacts[position]);

		if (order == 0)
			return 1;
		if (order < 0)
			break;
	}
	if (*count == SG_COMPOUND_GEN_GAME_MAX_CONTACTS)
		return 0;
	memmove(&contacts[position + 1U], &contacts[position],
	    (*count - position) * sizeof(contacts[0]));
	contacts[position] = *contact;
	(*count)++;
	return 1;
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

sg_compound_gen_game_result_t SG_CompoundGenGameBuild(
	const sg_compound_gen_game_request_t *request)
{
	sg_compound_world_candidate_t *mechanisms = NULL;
	sg_compound_gen_seed_t *seeds = NULL;
	sg_compound_gen_candidate_t candidates[SG_COMPOUND_GEN_MAX_CANDIDATES];
	sg_compound_gen_game_contact_t contacts[SG_COMPOUND_GEN_GAME_MAX_CONTACTS];
	rune_link_t planned[SG_COMPOUND_GEN_MAX_SELECTED];
	sg_compound_gen_game_proof_context_t proof_context;
	sg_compound_gen_request_t plan_request;
	sg_compound_gen_game_result_t result =
	    GameResult(SG_COMPOUND_GEN_INVALID, RLR_BAD_CONTROL_POLICY);
	size_t candidate_count = 0U;
	size_t contact_count = 0U;
	size_t source_index, destination_index, link_index;
	int mechanism_count = 0;
	int mechanism_index;
	rune_reject_reason_t reason;

	if (!request || !request->seeds || request->seed_count == 0U ||
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
	mechanisms = request->allocate((int)(sizeof(*mechanisms) *
	    (size_t)mechanism_count));
	seeds = request->allocate((int)(sizeof(*seeds) * request->seed_count));
	if (!mechanisms || !seeds)
		goto done;
	memset(mechanisms, 0, sizeof(*mechanisms) * (size_t)mechanism_count);
	memset(seeds, 0, sizeof(*seeds) * request->seed_count);
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
				if (!GameContactInsert(contacts, &contact_count, &contact))
				{
					result.status = SG_COMPOUND_GEN_BUDGET;
					result.reason = RLR_OK;
					result.candidates =
					    SG_COMPOUND_GEN_MAX_CANDIDATES + 1U;
					goto done;
				}
			}
		}
	}
	for (source_index = 0U; source_index < contact_count; source_index++)
	{
		const sg_compound_gen_game_contact_t *contact =
		    &contacts[source_index];
		sg_compound_world_preopen_t resolved;
		sg_compound_gen_game_destination_t destinations
		    [SG_COMPOUND_GEN_GAME_DESTINATION_ROLE_COUNT]
		    [SG_COMPOUND_GEN_GAME_MAX_ROLE_QUOTA];
		size_t quota = SG_COMPOUND_GEN_MAX_CANDIDATES /
		    (SG_COMPOUND_GEN_GAME_DESTINATION_ROLE_COUNT * contact_count);
		int category;
		size_t slot;

		memset(&resolved, 0, sizeof(resolved));
		if (SG_CompoundWorldResolvePreopen(contact->anchor, &resolved) !=
		        RLR_OK ||
		    resolved.trigger_key != contact->trigger_key ||
		    resolved.mover_key != contact->mover_key)
			continue;
		memset(destinations, 0xff, sizeof(destinations));
		for (destination_index = 0;
		     destination_index < request->seed_count; destination_index++)
		{
			uint8_t new_objective;
			uint32_t rank;
			int crosses;

			if (destination_index == (size_t)contact->source ||
			    !seeds[destination_index].has_outgoing ||
			    !SG_CompoundWorldCrossesSweep(&resolved, contact->anchor,
			        request->seeds[destination_index].origin))
				continue;
			new_objective = (uint8_t)(
			    seeds[destination_index].objective_mask &
			    (SG_COMPOUND_GEN_OBJECTIVE_MASK &
			     ~seeds[contact->source].objective_mask));
			crosses = seeds[contact->source].component >= 0 &&
			    seeds[destination_index].component >= 0 &&
			    seeds[contact->source].component !=
			        seeds[destination_index].component;
			rank = GameRank(request->seeds[contact->source].origin,
			    contact->anchor,
			    request->seeds[destination_index].origin);
			GameDestinationInsert(
			    destinations[SG_COMPOUND_GEN_GAME_DESTINATION_ANY], quota,
			    (int)destination_index, rank);
			if (new_objective & 1U)
				GameDestinationInsert(
				    destinations[SG_COMPOUND_GEN_GAME_DESTINATION_RED], quota,
				    (int)destination_index, rank);
			if (new_objective & 2U)
				GameDestinationInsert(
				    destinations[SG_COMPOUND_GEN_GAME_DESTINATION_BLUE], quota,
				    (int)destination_index, rank);
			if (crosses)
				GameDestinationInsert(
				    destinations[SG_COMPOUND_GEN_GAME_DESTINATION_CROSS], quota,
				    (int)destination_index, rank);
		}
		for (category = 0;
		     category < SG_COMPOUND_GEN_GAME_DESTINATION_ROLE_COUNT;
		     category++)
			for (slot = 0U;
			     slot < quota; slot++)
			{
				sg_compound_gen_candidate_t candidate;
				int picked = destinations[category][slot].index;

				if (picked < 0)
					break;
				memset(&candidate, 0, sizeof(candidate));
				candidate.source = contact->source;
				candidate.destination = picked;
				candidate.trigger_key = contact->trigger_key;
				candidate.mover_key = contact->mover_key;
				VectorCopy(contact->anchor, candidate.mechanism_anchor);
				candidate.local_rank =
				    destinations[category][slot].rank;
				if (GameCandidateExists(candidates, candidate_count,
				                        &candidate))
					continue;
				candidates[candidate_count++] = candidate;
			}
	}
	result.candidates = candidate_count;
	if (candidate_count == 0U)
	{
		result.status = SG_COMPOUND_GEN_OK;
		result.reason = RLR_OK;
		goto done;
	}
	memset(&plan_request, 0, sizeof(plan_request));
	memset(&proof_context, 0, sizeof(proof_context));
	proof_context.seeds = request->seeds;
	plan_request.seeds = seeds;
	plan_request.seed_count = request->seed_count;
	plan_request.candidates = candidates;
	plan_request.candidate_count = candidate_count;
	plan_request.output = planned;
	plan_request.output_capacity = SG_COMPOUND_GEN_MAX_SELECTED;
	plan_request.prove = GameProve;
	plan_request.context = &proof_context;
	plan_request.production_enabled = 1;
	{
		sg_compound_gen_result_t plan = SG_CompoundGenPlan(&plan_request);

		result.status = plan.status;
		result.reason = RLR_OK;
		result.selected = plan.selected;
		result.proof_calls = plan.proof_calls;
		result.emitted = plan.emitted;
	}
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
	if (result.status == SG_COMPOUND_GEN_NO_IMPROVEMENT ||
	    result.status == SG_COMPOUND_GEN_NO_PROOF)
		result.status = SG_COMPOUND_GEN_OK;
	if (result.status != SG_COMPOUND_GEN_OK)
		goto done;
	if (result.emitted > request->link_capacity - *request->link_count)
	{
		result.status = SG_COMPOUND_GEN_CAPACITY;
		result.emitted = 0U;
		goto done;
	}
	memcpy(&request->links[*request->link_count], planned,
	       result.emitted * sizeof(planned[0]));
	*request->link_count += result.emitted;

done:
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
	sg_host.dprint("rune: compound status=%s candidates=%u selected=%u "
	               "proofs=%u emitted=%u reason=%d reject=%d rejects=%u "
	               "replay=%d replay_rejects=%u\n",
	               SG_CompoundGenStatusName(result.status),
	               (unsigned int)result.candidates,
	               (unsigned int)result.selected,
	               (unsigned int)result.proof_calls,
	               (unsigned int)result.emitted, (int)result.reason,
	               (int)result.proof_rejection,
	               (unsigned int)result.proof_rejections,
	               (int)result.replay_rejection,
	               (unsigned int)result.replay_rejections);
	if (result.status != SG_COMPOUND_GEN_OK || count > link_capacity)
		return 0;
	*link_count = (int)count;
	return 1;
}
