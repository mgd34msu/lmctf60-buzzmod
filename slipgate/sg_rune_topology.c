/* sg_rune_topology.c -- reconcile flood contacts with a completed RUNE graph. */

#include "../q_shared.h"
#include "sg_rune_topology.h"

#include <stdlib.h>
#include <string.h>

#define TOPOLOGY_EMPTY_SLOT UINT32_MAX

typedef struct topology_workspace_s
{
	int *out_head;
	int *in_head;
	int *out_next;
	int *in_next;
	int *order;
	int *stack_node;
	int *stack_edge;
	unsigned char *seen;
} topology_workspace_t;

static uint64_t TopologyPairKey(int low, int high)
{
	return ((uint64_t)(uint32_t)low << 32) | (uint32_t)high;
}

static uint32_t TopologyHash(uint64_t key)
{
	key ^= key >> 33;
	key *= UINT64_C(0xff51afd7ed558ccd);
	key ^= key >> 33;
	key *= UINT64_C(0xc4ceb9fe1a85ec53);
	return (uint32_t)(key ^ (key >> 32));
}

static int TopologyPowerOfTwo(uint32_t value)
{
	return value != 0U && (value & (value - 1U)) == 0U;
}

sg_rune_topology_status_t SG_RuneTopologyLedgerInit(
	sg_rune_contact_ledger_t *ledger,
	sg_rune_collision_contact_t *contacts, uint32_t contact_capacity,
	uint32_t *slots, uint32_t slot_capacity)
{
	if (!ledger || !contacts || !contact_capacity || !slots ||
	    !TopologyPowerOfTwo(slot_capacity) ||
	    slot_capacity < contact_capacity * 2U)
		return SG_RUNE_TOPOLOGY_INVALID;
	memset(ledger, 0, sizeof(*ledger));
	ledger->contacts = contacts;
	ledger->contact_capacity = contact_capacity;
	ledger->slots = slots;
	ledger->slot_capacity = slot_capacity;
	for (uint32_t i = 0; i < slot_capacity; i++)
		slots[i] = TOPOLOGY_EMPTY_SLOT;
	return SG_RUNE_TOPOLOGY_OK;
}

sg_rune_topology_status_t SG_RuneTopologyRecordContact(
	sg_rune_contact_ledger_t *ledger, int first, int second,
	sg_rune_contact_provenance_t provenance,
	sg_rune_contact_kind_t kind)
{
	uint32_t mask, slot;
	uint64_t key;
	int low, high;

	if (!ledger || !ledger->contacts || !ledger->slots || first < 0 ||
	    second < 0 || first == second ||
	    (provenance != SG_RUNE_CONTACT_FLOOD_CHILD &&
	     provenance != SG_RUNE_CONTACT_FLOOD_MEETING) ||
	    kind < SG_RUNE_CONTACT_STATIC_DRY ||
	    kind > SG_RUNE_CONTACT_DECLARED_MOVER)
		return SG_RUNE_TOPOLOGY_INVALID;
	if (ledger->overflow)
		return SG_RUNE_TOPOLOGY_CAPACITY;
	low = first < second ? first : second;
	high = first < second ? second : first;
	key = TopologyPairKey(low, high);
	mask = ledger->slot_capacity - 1U;
	slot = TopologyHash(key) & mask;
	for (uint32_t probe = 0; probe < ledger->slot_capacity; probe++)
	{
		uint32_t index = ledger->slots[slot];

		if (index == TOPOLOGY_EMPTY_SLOT)
		{
			sg_rune_collision_contact_t *contact;

			if (ledger->contact_count >= ledger->contact_capacity)
			{
				ledger->overflow = 1;
				return SG_RUNE_TOPOLOGY_CAPACITY;
			}
			index = ledger->contact_count++;
			ledger->slots[slot] = index;
			contact = &ledger->contacts[index];
			contact->low_seed = low;
			contact->high_seed = high;
			contact->provenance = (uint8_t)provenance;
			contact->kind = (uint8_t)kind;
			return SG_RUNE_TOPOLOGY_OK;
		}
		if (index >= ledger->contact_count)
			return SG_RUNE_TOPOLOGY_INVALID;
		if (ledger->contacts[index].low_seed == low &&
		    ledger->contacts[index].high_seed == high)
		{
			ledger->contacts[index].provenance |= (uint8_t)provenance;
			if (ledger->contacts[index].kind < (uint8_t)kind)
				ledger->contacts[index].kind = (uint8_t)kind;
			return SG_RUNE_TOPOLOGY_OK;
		}
		slot = (slot + 1U) & mask;
	}
	ledger->overflow = 1;
	return SG_RUNE_TOPOLOGY_CAPACITY;
}

static int TopologyContactCompare(const void *left, const void *right)
{
	const sg_rune_collision_contact_t *a = left;
	const sg_rune_collision_contact_t *b = right;

	if (a->low_seed != b->low_seed)
		return a->low_seed < b->low_seed ? -1 : 1;
	if (a->high_seed != b->high_seed)
		return a->high_seed < b->high_seed ? -1 : 1;
	return 0;
}

static int TopologyGraphValid(const sg_rune_topology_graph_t *graph)
{
	int count;

	if (!graph || !graph->seeds || !graph->seed_count ||
	    graph->seed_count > RUNE_MAX_SEEDS || !graph->links ||
	    !graph->link_count || graph->link_capacity > RUNE_MAX_LINKS)
		return 0;
	count = *graph->link_count;
	if (count < 0 || (uint32_t)count > graph->link_capacity)
		return 0;
	for (int i = 0; i < count; i++)
		if (graph->links[i].from < 0 || graph->links[i].to < 0 ||
		    (uint32_t)graph->links[i].from >= graph->seed_count ||
		    (uint32_t)graph->links[i].to >= graph->seed_count)
			return 0;
	return 1;
}

static uint64_t TopologyGraphIdentityUnchecked(
	const sg_rune_topology_graph_t *graph)
{
	uint64_t identity = UINT64_C(1469598103934665603);
	int link_count = *graph->link_count;

	identity ^= graph->seed_count;
	identity *= UINT64_C(1099511628211);
	identity ^= (uint32_t)link_count;
	identity *= UINT64_C(1099511628211);
	for (int index = 0; index < link_count; index++)
	{
		identity ^= (uint32_t)graph->links[index].from;
		identity *= UINT64_C(1099511628211);
		identity ^= (uint32_t)graph->links[index].to;
		identity *= UINT64_C(1099511628211);
	}
	return identity;
}

sg_rune_topology_status_t SG_RuneTopologyGraphIdentity(
	const sg_rune_topology_graph_t *graph, uint64_t *identity_out)
{
	if (!identity_out || !TopologyGraphValid(graph))
		return SG_RUNE_TOPOLOGY_INVALID;
	*identity_out = TopologyGraphIdentityUnchecked(graph);
	return SG_RUNE_TOPOLOGY_OK;
}

static void TopologyWorkspaceFree(topology_workspace_t *workspace,
	sg_rune_topology_release_fn release)
{
	if (workspace->out_head) release(workspace->out_head);
	if (workspace->in_head) release(workspace->in_head);
	if (workspace->out_next) release(workspace->out_next);
	if (workspace->in_next) release(workspace->in_next);
	if (workspace->order) release(workspace->order);
	if (workspace->stack_node) release(workspace->stack_node);
	if (workspace->stack_edge) release(workspace->stack_edge);
	if (workspace->seen) release(workspace->seen);
	memset(workspace, 0, sizeof(*workspace));
}

static int TopologyWorkspaceAllocate(topology_workspace_t *workspace,
	const sg_rune_topology_graph_t *graph,
	sg_rune_topology_allocate_fn allocate)
{
	size_t seeds = graph->seed_count;
	size_t links = graph->link_capacity ? graph->link_capacity : 1U;

	memset(workspace, 0, sizeof(*workspace));
	workspace->out_head = allocate(seeds * sizeof(*workspace->out_head));
	workspace->in_head = allocate(seeds * sizeof(*workspace->in_head));
	workspace->out_next = allocate(links * sizeof(*workspace->out_next));
	workspace->in_next = allocate(links * sizeof(*workspace->in_next));
	workspace->order = allocate(seeds * sizeof(*workspace->order));
	workspace->stack_node = allocate(seeds * sizeof(*workspace->stack_node));
	workspace->stack_edge = allocate(seeds * sizeof(*workspace->stack_edge));
	workspace->seen = allocate(seeds * sizeof(*workspace->seen));
	return workspace->out_head && workspace->in_head && workspace->out_next &&
		workspace->in_next && workspace->order && workspace->stack_node &&
		workspace->stack_edge && workspace->seen;
}

static int TopologySccBuild(const sg_rune_topology_graph_t *graph,
	topology_workspace_t *workspace, int *labels)
{
	uint32_t order_count = 0U;
	int component_count = 0;
	int link_count = *graph->link_count;

	for (uint32_t i = 0; i < graph->seed_count; i++)
	{
		workspace->out_head[i] = -1;
		workspace->in_head[i] = -1;
		workspace->seen[i] = 0U;
		labels[i] = -1;
	}
	for (int i = 0; i < link_count; i++)
	{
		const rune_link_t *link = &graph->links[i];

		workspace->out_next[i] = workspace->out_head[link->from];
		workspace->out_head[link->from] = i;
		workspace->in_next[i] = workspace->in_head[link->to];
		workspace->in_head[link->to] = i;
	}
	for (uint32_t root = 0; root < graph->seed_count; root++)
	{
		uint32_t depth;

		if (workspace->seen[root])
			continue;
		depth = 1U;
		workspace->stack_node[0] = (int)root;
		workspace->stack_edge[0] = workspace->out_head[root];
		workspace->seen[root] = 1U;
		while (depth)
		{
			uint32_t top = depth - 1U;
			int edge = workspace->stack_edge[top];

			if (edge >= 0)
			{
				int to = graph->links[edge].to;

				workspace->stack_edge[top] = workspace->out_next[edge];
				if (!workspace->seen[to])
				{
					workspace->seen[to] = 1U;
					workspace->stack_node[depth] = to;
					workspace->stack_edge[depth] = workspace->out_head[to];
					depth++;
				}
			}
			else
			{
				workspace->order[order_count++] = workspace->stack_node[top];
				depth--;
			}
		}
	}
	while (order_count)
	{
		uint32_t depth = 0U;
		int root = workspace->order[--order_count];

		if (labels[root] >= 0)
			continue;
		labels[root] = component_count;
		workspace->stack_node[depth++] = root;
		while (depth)
		{
			int node = workspace->stack_node[--depth];

			for (int edge = workspace->in_head[node]; edge >= 0;
			     edge = workspace->in_next[edge])
			{
				int from = graph->links[edge].from;

				if (labels[from] >= 0)
					continue;
				labels[from] = component_count;
				workspace->stack_node[depth++] = from;
			}
		}
		component_count++;
	}
	return component_count;
}

static void TopologySnapshotSeal(const sg_rune_topology_graph_t *graph,
	sg_rune_topology_snapshot_t *snapshot, uint32_t component_count)
{
	snapshot->seed_count = graph->seed_count;
	snapshot->link_count = (uint32_t)*graph->link_count;
	snapshot->graph_identity = TopologyGraphIdentityUnchecked(graph);
	snapshot->component_count = component_count;
}

sg_rune_topology_status_t SG_RuneTopologySnapshotBuild(
	const sg_rune_topology_graph_t *graph,
	sg_rune_topology_snapshot_t *snapshot,
	sg_rune_topology_allocate_fn allocate,
	sg_rune_topology_release_fn release)
{
	topology_workspace_t workspace;
	int component_count;

	if (!TopologyGraphValid(graph) || !snapshot || !snapshot->components ||
	    snapshot->component_capacity < graph->seed_count || !allocate ||
	    !release)
		return SG_RUNE_TOPOLOGY_INVALID;
	if (!TopologyWorkspaceAllocate(&workspace, graph, allocate))
	{
		TopologyWorkspaceFree(&workspace, release);
		return SG_RUNE_TOPOLOGY_NO_MEMORY;
	}
	component_count = TopologySccBuild(graph, &workspace,
		snapshot->components);
	TopologySnapshotSeal(graph, snapshot, (uint32_t)component_count);
	TopologyWorkspaceFree(&workspace, release);
	return SG_RUNE_TOPOLOGY_OK;
}

int SG_RuneTopologySnapshotCurrent(const sg_rune_topology_graph_t *graph,
	const sg_rune_topology_snapshot_t *snapshot)
{
	return TopologyGraphValid(graph) && snapshot && snapshot->components &&
		snapshot->component_capacity >= graph->seed_count &&
		snapshot->seed_count == graph->seed_count &&
		snapshot->link_count == (uint32_t)*graph->link_count &&
		snapshot->graph_identity == TopologyGraphIdentityUnchecked(graph);
}

static int TopologyDirectLink(const sg_rune_topology_graph_t *graph,
	const topology_workspace_t *workspace, int from, int to)
{
	for (int edge = workspace->out_head[from]; edge >= 0;
	     edge = workspace->out_next[edge])
		if (graph->links[edge].to == to)
			return edge;
	return -1;
}

sg_rune_topology_status_t SG_RuneTopologyReconcile(
	const sg_rune_contact_ledger_t *ledger,
	const sg_rune_topology_graph_t *graph,
	const sg_rune_topology_proof_ops_t *proof_ops,
	sg_rune_topology_report_t *report,
	sg_rune_topology_allocate_fn allocate,
	sg_rune_topology_release_fn release)
{
	topology_workspace_t workspace;
	sg_rune_collision_contact_t *contacts = NULL;
	int *initial_scc = NULL, *final_scc = NULL;
	sg_rune_topology_outcome_t *outcomes;
	sg_rune_topology_snapshot_t *final_snapshot;
	uint32_t outcome_capacity;
	sg_rune_topology_status_t status = SG_RUNE_TOPOLOGY_INVALID;

	if (!ledger || !graph || !proof_ops || !proof_ops->prove || !report ||
	    !allocate || !release || ledger->overflow || !ledger->contacts ||
	    !TopologyGraphValid(graph))
		return ledger && ledger->overflow ? SG_RUNE_TOPOLOGY_CAPACITY : status;
	outcomes = report->outcomes;
	outcome_capacity = report->outcome_capacity;
	final_snapshot = report->final_snapshot;
	memset(report, 0, sizeof(*report));
	report->outcomes = outcomes;
	report->outcome_capacity = outcome_capacity;
	report->final_snapshot = final_snapshot;
	if (final_snapshot && (!final_snapshot->components ||
	    final_snapshot->component_capacity < graph->seed_count))
		return SG_RUNE_TOPOLOGY_INVALID;
	report->contacts = ledger->contact_count;
	if (!TopologyWorkspaceAllocate(&workspace, graph, allocate))
	{
		TopologyWorkspaceFree(&workspace, release);
		return SG_RUNE_TOPOLOGY_NO_MEMORY;
	}
	contacts = allocate((ledger->contact_count ? ledger->contact_count : 1U) *
		sizeof(*contacts));
	initial_scc = allocate(graph->seed_count * sizeof(*initial_scc));
	final_scc = allocate(graph->seed_count * sizeof(*final_scc));
	if (!contacts || !initial_scc || !final_scc)
	{
		status = SG_RUNE_TOPOLOGY_NO_MEMORY;
		goto done;
	}
	memcpy(contacts, ledger->contacts,
		ledger->contact_count * sizeof(*contacts));
	qsort(contacts, ledger->contact_count, sizeof(*contacts),
		TopologyContactCompare);
	for (uint32_t i = 0; i < ledger->contact_count; i++)
		if (contacts[i].low_seed < 0 ||
		    contacts[i].low_seed >= contacts[i].high_seed ||
		    (uint32_t)contacts[i].high_seed >= graph->seed_count ||
		    contacts[i].kind > SG_RUNE_CONTACT_DECLARED_MOVER ||
		    !(contacts[i].provenance &
		      (SG_RUNE_CONTACT_FLOOD_CHILD |
		       SG_RUNE_CONTACT_FLOOD_MEETING)))
			goto done;
	report->initial_sccs = (uint32_t)TopologySccBuild(graph, &workspace,
		initial_scc);
	report->scc_builds = 1U;
	for (uint32_t i = 0; i < ledger->contact_count; i++)
		if (initial_scc[contacts[i].low_seed] !=
		    initial_scc[contacts[i].high_seed])
		{
			report->crossing_contacts++;
			report->crossing_directions += 2U;
		}
	if (report->crossing_directions > outcome_capacity ||
	    (report->crossing_directions && !outcomes))
	{
		status = SG_RUNE_TOPOLOGY_CAPACITY;
		goto done;
	}
	for (uint32_t i = 0; i < ledger->contact_count; i++)
	{
		const sg_rune_collision_contact_t *contact = &contacts[i];
		int endpoints[2] = { contact->low_seed, contact->high_seed };

		if (initial_scc[endpoints[0]] == initial_scc[endpoints[1]])
			continue;
		for (uint32_t direction = 0; direction < 2U; direction++)
		{
			int from = endpoints[direction];
			int to = endpoints[1U - direction];
			int before = *graph->link_count;
			int direct_link;
			sg_rune_topology_proof_result_t proof = {
				SG_RUNE_TOPOLOGY_PRESENT, 0U, 0U
			};
			sg_rune_topology_outcome_t *outcome =
				&outcomes[report->outcome_count++];

			memset(outcome, 0, sizeof(*outcome));
			outcome->from = from;
			outcome->to = to;
			outcome->initial_from_scc = initial_scc[from];
			outcome->initial_to_scc = initial_scc[to];
			outcome->contact_kind = contact->kind;
			outcome->provenance = contact->provenance;
			outcome->first_link = UINT32_MAX;
			direct_link = TopologyDirectLink(graph, &workspace, from, to);
			if (direct_link < 0)
			{
				report->proof_calls++;
				proof = proof_ops->prove(proof_ops->context, contact,
					from, to);
			}
			else
				outcome->first_link = (uint32_t)direct_link;
			outcome->result = (uint8_t)proof.status;
			outcome->owner = proof.owner;
			outcome->reason = proof.reason;
			if (proof.status == SG_RUNE_TOPOLOGY_ADDED)
			{
				if (*graph->link_count <= before ||
				    !TopologyGraphValid(graph))
				{
					status = SG_RUNE_TOPOLOGY_OWNER_FATAL;
					goto done;
				}
				outcome->links_added =
					(uint32_t)(*graph->link_count - before);
				outcome->first_link = (uint32_t)before;
				report->edges_added += outcome->links_added;
			}
			else if (*graph->link_count != before ||
			         proof.status < SG_RUNE_TOPOLOGY_PRESENT ||
			         proof.status > SG_RUNE_TOPOLOGY_FATAL)
			{
				status = SG_RUNE_TOPOLOGY_OWNER_FATAL;
				goto done;
			}
			else if (proof.status == SG_RUNE_TOPOLOGY_REJECTED)
				report->rejected++;
			else if (proof.status == SG_RUNE_TOPOLOGY_DEFERRED)
				report->deferred++;
			else if (proof.status == SG_RUNE_TOPOLOGY_FATAL)
			{
				status = SG_RUNE_TOPOLOGY_OWNER_FATAL;
				goto done;
			}
		}
	}
	report->final_sccs = (uint32_t)TopologySccBuild(graph, &workspace,
		final_scc);
	report->scc_builds = 2U;
	if (final_snapshot)
	{
		memcpy(final_snapshot->components, final_scc,
			graph->seed_count * sizeof(*final_scc));
		TopologySnapshotSeal(graph, final_snapshot, report->final_sccs);
	}
	for (uint32_t i = 0; i < report->outcome_count; i++)
	{
		sg_rune_topology_outcome_t *outcome = &outcomes[i];

		outcome->final_same_scc =
			(uint8_t)(final_scc[outcome->from] == final_scc[outcome->to]);
		if (!outcome->final_same_scc)
			report->unresolved++;
	}
	status = SG_RUNE_TOPOLOGY_OK;

done:
	if (contacts) release(contacts);
	if (initial_scc) release(initial_scc);
	if (final_scc) release(final_scc);
	TopologyWorkspaceFree(&workspace, release);
	return status;
}
