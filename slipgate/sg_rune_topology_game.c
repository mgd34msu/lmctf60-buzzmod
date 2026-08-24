/* sg_rune_topology_game.c -- generator adapter for flood-contact auditing. */

#include "../q_shared.h"
#include "sg_rune_topology_game.h"
#include "sg_rune_mechanism_catalog.h"

#include <string.h>

enum
{
	TOPOLOGY_OWNER_DRY = 1,
	TOPOLOGY_OWNER_WATER,
	TOPOLOGY_OWNER_MOVER,
	TOPOLOGY_OWNER_UNKNOWN
};

typedef struct topology_game_context_s
{
	const sg_rune_topology_game_request_t *request;
} topology_game_context_t;

static int TopologySegmentIntersectsBounds(const vec3_t from,
	const vec3_t to, const vec3_t mins, const vec3_t maxs)
{
	float low = 0.0f, high = 1.0f;

	for (int axis = 0; axis < 3; axis++)
	{
		float delta = to[axis] - from[axis];

		if (fabsf(delta) < 0.0001f)
		{
			if (from[axis] < mins[axis] || from[axis] > maxs[axis])
				return 0;
		}
		else
		{
			float first = (mins[axis] - from[axis]) / delta;
			float second = (maxs[axis] - from[axis]) / delta;

			if (first > second)
			{
				float swap = first;

				first = second;
				second = swap;
			}
			if (first > low) low = first;
			if (second < high) high = second;
			if (low > high)
				return 0;
		}
	}
	return 1;
}

sg_rune_contact_kind_t SG_RuneTopologyGameContactKind(
	const rune_seed_t *seeds, int first, int second,
	const rune_mechanism_node_t *nodes, uint32_t node_count)
{
	static const vec3_t hull_mins = { -16.0f, -16.0f, -24.0f };
	static const vec3_t hull_maxs = { 16.0f, 16.0f, 32.0f };

	for (uint32_t index = 0U; index < node_count; index++)
	{
		const rune_mechanism_node_t *node = &nodes[index];
		vec3_t mins, maxs;

		if (!(node->flags & SG_MECH_NODEF_MOVER))
			continue;
		for (int axis = 0; axis < 3; axis++)
		{
			mins[axis] = node->absmin_q8[axis] * 0.125f - hull_maxs[axis];
			maxs[axis] = node->absmax_q8[axis] * 0.125f - hull_mins[axis];
		}
		if (TopologySegmentIntersectsBounds(seeds[first].origin,
		    seeds[second].origin, mins, maxs))
			return SG_RUNE_CONTACT_DECLARED_MOVER;
	}
	if ((seeds[first].flags | seeds[second].flags) & RSF_WATER)
		return SG_RUNE_CONTACT_WATER_BOUNDARY;
	return SG_RUNE_CONTACT_STATIC_DRY;
}

static sg_rune_topology_proof_result_t TopologyTryDirection(void *data,
	const sg_rune_collision_contact_t *contact, int from, int to)
{
	topology_game_context_t *context = data;
	const sg_rune_topology_game_request_t *request = context->request;
	sg_rune_topology_proof_result_t result = {
		SG_RUNE_TOPOLOGY_REJECTED, TOPOLOGY_OWNER_DRY, 1U
	};
	static const int ordinary[] = { RL_RUN, RL_JUMP };

	if (contact->kind == SG_RUNE_CONTACT_WATER_BOUNDARY)
	{
		int proved = request->try_action(request->context, from, to,
			RL_SWIM);

		result.owner = TOPOLOGY_OWNER_WATER;
		if (proved < 0)
		{
			result.status = SG_RUNE_TOPOLOGY_FATAL;
			result.reason = 4U;
		}
		else if (proved > 0)
		{
			result.status = SG_RUNE_TOPOLOGY_ADDED;
			result.reason = 0U;
		}
		return result;
	}
	if (contact->kind != SG_RUNE_CONTACT_STATIC_DRY)
	{
		result.status = SG_RUNE_TOPOLOGY_DEFERRED;
		result.owner = contact->kind == SG_RUNE_CONTACT_DECLARED_MOVER
			? TOPOLOGY_OWNER_MOVER : TOPOLOGY_OWNER_UNKNOWN;
		result.reason = 2U;
		return result;
	}
	for (uint32_t index = 0U; index < 2U; index++)
	{
		int proved = request->try_action(request->context, from, to,
			ordinary[index]);

		if (proved < 0)
		{
			result.status = SG_RUNE_TOPOLOGY_FATAL;
			result.reason = 4U;
			return result;
		}
		if (proved > 0)
		{
			result.status = SG_RUNE_TOPOLOGY_ADDED;
			result.reason = 0U;
			return result;
		}
	}
	if (request->graph.seeds[to].origin[2] <
	    request->graph.seeds[from].origin[2])
	{
		int proved = request->try_action(request->context, from, to,
			RL_DROP);

		if (proved < 0)
		{
			result.status = SG_RUNE_TOPOLOGY_FATAL;
			result.reason = 4U;
		}
		else if (proved > 0)
		{
			result.status = SG_RUNE_TOPOLOGY_ADDED;
			result.reason = 0U;
		}
	}
	return result;
}

static void TopologyLog(const sg_rune_topology_game_request_t *request,
	const sg_rune_topology_report_t *report, sg_rune_topology_status_t status)
{
	uint32_t present = 0U;
	uint32_t unresolved_contacts = 0U;
	uint32_t unexamined;

	for (uint32_t index = 0U; index < report->outcome_count; index++)
	{
		const sg_rune_topology_outcome_t *outcome = &report->outcomes[index];
		if (outcome->result == SG_RUNE_TOPOLOGY_PRESENT)
			present++;

		request->print("rune: topology-outcome from=%d to=%d scc=%d,%d "
			"kind=%u provenance=%u owner=%u result=%u reason=%u "
			"first_link=%u links_added=%u final_same_scc=%u\n",
			outcome->from, outcome->to, outcome->initial_from_scc,
			outcome->initial_to_scc, (unsigned int)outcome->contact_kind,
			(unsigned int)outcome->provenance,
			(unsigned int)outcome->owner, (unsigned int)outcome->result,
			(unsigned int)outcome->reason, outcome->first_link,
			outcome->links_added, (unsigned int)outcome->final_same_scc);
	}
	for (uint32_t index = 0U; index < report->outcome_count; index += 2U)
		if (!report->outcomes[index].final_same_scc ||
		    index + 1U >= report->outcome_count ||
		    !report->outcomes[index + 1U].final_same_scc)
			unresolved_contacts++;
	unexamined = report->crossing_directions > report->outcome_count
		? report->crossing_directions - report->outcome_count : 0U;
	if (status == SG_RUNE_TOPOLOGY_OK && !report->crossing_directions)
		request->print("rune: topology no-witnessed-crossing-contact\n");
	request->print("rune: topology status=%d contacts=%u contact_overflow=%d "
		"initial_crossing_contacts=%u initial_crossing_directions=%u "
		"owner_calls=%u proved_added=%u proved_present=%u "
		"owner_rejected=%u owner_deferred=%u unexamined=%u "
		"unresolved_contacts=%u unresolved_directions=%u "
		"initial_sccs=%u final_sccs=%u scc_builds=%u added_links=%u\n",
		(int)status, report->contacts, request->ledger->overflow,
		report->crossing_contacts, report->crossing_directions,
		report->proof_calls, report->edges_added, present, report->rejected,
		report->deferred, unexamined, unresolved_contacts,
		report->unresolved, report->initial_sccs, report->final_sccs,
		report->scc_builds, report->edges_added);
}

sg_rune_topology_status_t SG_RuneTopologyGameRepair(
	const sg_rune_topology_game_request_t *request,
	sg_rune_topology_report_t *report)
{
	topology_game_context_t context = { request };
	sg_rune_topology_proof_ops_t proof = { TopologyTryDirection, &context };
	sg_rune_topology_status_t status;

	if (!request || !report || !request->ledger || !request->try_action ||
	    !request->allocate || !request->release || !request->print)
		return SG_RUNE_TOPOLOGY_INVALID;
	memset(report, 0, sizeof(*report));
	report->outcomes = request->outcomes;
	report->outcome_capacity = request->outcome_capacity;
	report->final_snapshot = request->final_snapshot;
	status = SG_RuneTopologyReconcile(request->ledger, &request->graph,
		&proof, report, request->allocate, request->release);
	TopologyLog(request, report, status);
	return status;
}
