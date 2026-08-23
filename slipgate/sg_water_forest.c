#include "sg_water_forest.h"

static int WaterForestFind(sg_water_forest_t *forest, int seed)
{
	int root = seed;

	while (forest->parents[root] != root)
		root = forest->parents[root];
	while (forest->parents[seed] != seed)
	{
		int parent = forest->parents[seed];

		forest->parents[seed] = root;
		seed = parent;
	}
	return root;
}

static size_t WaterForestSlot(const sg_water_forest_t *forest, int from,
	int to, int *found)
{
	uint32_t hash = (uint32_t)from * 73856093u ^ (uint32_t)to * 19349663u;
	size_t slot = (size_t)hash & (forest->edge_slot_capacity - 1U);
	size_t scanned;

	for (scanned = 0; scanned < forest->edge_slot_capacity; scanned++)
	{
		int edge = forest->edge_slots[slot];

		if (edge < 0)
		{
			*found = 0;
			return slot;
		}
		if (forest->edges[edge].from == from && forest->edges[edge].to == to)
		{
			*found = 1;
			return slot;
		}
		slot = (slot + 1U) & (forest->edge_slot_capacity - 1U);
	}
	*found = 0;
	return forest->edge_slot_capacity;
}

static void WaterForestUnion(sg_water_forest_t *forest, int from, int to)
{
	int from_root = WaterForestFind(forest, from);
	int to_root = WaterForestFind(forest, to);

	if (from_root == to_root)
		return;
	if (forest->ranks[from_root] < forest->ranks[to_root])
		forest->parents[from_root] = to_root;
	else
	{
		forest->parents[to_root] = from_root;
		if (forest->ranks[from_root] == forest->ranks[to_root])
			forest->ranks[from_root]++;
	}
}

int SG_WaterForestInit(sg_water_forest_t *forest, int *parents,
	uint8_t *ranks, size_t seed_capacity, sg_water_edge_t *edges,
	size_t edge_capacity, int *edge_slots, size_t edge_slot_capacity)
{
	size_t i;

	if (!forest || !parents || !ranks || !seed_capacity || !edges ||
		!edge_capacity || !edge_slots || edge_slot_capacity <= edge_capacity ||
		(edge_slot_capacity & (edge_slot_capacity - 1U)) != 0U)
		return 0;
	forest->parents = parents;
	forest->ranks = ranks;
	forest->edges = edges;
	forest->edge_slots = edge_slots;
	forest->seed_capacity = seed_capacity;
	forest->edge_capacity = edge_capacity;
	forest->edge_slot_capacity = edge_slot_capacity;
	forest->edge_count = 0;
	forest->overflow = 0;
	for (i = 0; i < seed_capacity; i++)
	{
		parents[i] = (int)i;
		ranks[i] = 0;
	}
	for (i = 0; i < edge_slot_capacity; i++)
		edge_slots[i] = -1;
	return 1;
}

sg_water_connect_result_t SG_WaterForestConnect(sg_water_forest_t *forest,
	int from, int to, sg_water_prove_fn prove, void *context)
{
	sg_water_proof_t forward_proof = {0}, reverse_proof = {0};
	int had_forward, had_reverse, forward, reverse;
	int found;
	size_t forward_slot, reverse_slot;
	size_t needed;

	if (!forest || !prove || from < 0 || to < 0 || from == to ||
		(size_t)from >= forest->seed_capacity ||
		(size_t)to >= forest->seed_capacity)
		return SG_WATER_CONNECT_INVALID;
	if (forest->overflow)
		return SG_WATER_CONNECT_OVERFLOW;
	if (WaterForestFind(forest, from) == WaterForestFind(forest, to))
		return SG_WATER_CONNECT_ALREADY;
	forward_slot = WaterForestSlot(forest, from, to, &found);
	had_forward = found;
	reverse_slot = WaterForestSlot(forest, to, from, &found);
	had_reverse = found;
	if (forward_slot == forest->edge_slot_capacity ||
	    reverse_slot == forest->edge_slot_capacity)
	{
		forest->overflow = 1;
		return SG_WATER_CONNECT_OVERFLOW;
	}
	forward = had_forward;
	reverse = had_reverse;
	if (!forward && prove(context, from, to, &forward_proof) > 0 &&
		forward_proof.cost_ms > 0)
		forward = 1;
	if (!reverse && prove(context, to, from, &reverse_proof) > 0 &&
		reverse_proof.cost_ms > 0)
		reverse = 1;
	needed = (size_t)(!had_forward && forward) +
		(size_t)(!had_reverse && reverse);
	if (!needed)
		return SG_WATER_CONNECT_NO_ROUTE;
	if (needed > forest->edge_capacity - forest->edge_count)
	{
		forest->overflow = 1;
		return SG_WATER_CONNECT_OVERFLOW;
	}
	if (!had_forward && forward)
	{
		forest->edges[forest->edge_count].from = from;
		forest->edges[forest->edge_count].to = to;
		forest->edges[forest->edge_count].proof = forward_proof;
		forest->edge_slots[forward_slot] = (int)forest->edge_count++;
	}
	if (!had_reverse && reverse)
	{
		reverse_slot = WaterForestSlot(forest, to, from, &found);
		forest->edges[forest->edge_count].from = to;
		forest->edges[forest->edge_count].to = from;
		forest->edges[forest->edge_count].proof = reverse_proof;
		forest->edge_slots[reverse_slot] = (int)forest->edge_count++;
	}
	if (forward && reverse)
		WaterForestUnion(forest, from, to);
	return SG_WATER_CONNECT_RECORDED;
}
