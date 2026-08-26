#include "g_local.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_train_station_candidate_game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_EDICTS 64
#define NODE_COUNT 16U
#define EDGE_COUNT 17U

game_export_t globals;
game_locals_t game;
level_locals_t level;
edict_t *g_edicts;
sg_host_t sg_host;

static edict_t edicts[TEST_EDICTS];
static rune_link_t links[4];
static int link_count;
static int failures;
static int board_only_near;
static int board_reject_positive_y;
static int board_calls;
static int trace_l_corner;
static int allocation_calls;
static int fail_allocation_call = -1;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#condition_); \
		failures++; \
	} \
} while (0)

static const uint32_t route[SG_TRAIN_STATION_ROUTE_CORNERS] = {
	30U, 31U, 32U, 33U, 34U, 35U, 36U,
	37U, 38U, 39U, 40U, 43U, 42U, 41U
};

static void *LevelAlloc(int size)
{
	allocation_calls++;
	if (allocation_calls == fail_allocation_call)
		return NULL;
	return calloc(1U, (size_t)size);
}

static void LevelFree(void *memory)
{
	free(memory);
}

static void LinkEntity(edict_t *entity)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		entity->absmin[axis] = entity->s.origin[axis] + entity->mins[axis];
		entity->absmax[axis] = entity->s.origin[axis] + entity->maxs[axis];
	}
}

static trace_t Trace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t trace;

	(void)mins;
	(void)maxs;
	(void)passent;
	(void)contentmask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	if (trace_l_corner)
	{
		if (start[1] > -296.0f && end[1] > -296.0f &&
		    start[0] > 336.0f && end[0] < 336.0f)
			trace.fraction = 0.25f;
		if (start[1] <= -296.0f && end[1] > -296.0f &&
		    start[0] > -112.0f)
			trace.fraction = 0.35f;
	}
	return trace;
}

qboolean SG_OracleTrainStationBoard(const vec3_t source,
	const vec3_t approach, edict_t *train, uint32_t dwell_ms,
	int *arrival_ms, vec3_t contact_out)
{
	float expected_z = train == &edicts[25] ? 120.125f : -1287.875f;

	board_calls++;
	if (fabsf(source[2] - expected_z) > 0.125f ||
	    fabsf(approach[2] - expected_z) > 0.125f ||
	    dwell_ms != 3000U || (board_only_near && source[0] > 250.0f) ||
	    (board_reject_positive_y && source[1] > 0.0f))
		return false;
	*arrival_ms = 100;
	VectorCopy(approach, contact_out);
	contact_out[0] += 8.0f;
	return true;
}

qboolean SG_OracleTrainStationCarry(const vec3_t source,
	edict_t *from_corner, edict_t *to_corner, edict_t *train,
	vec3_t destination_out)
{
	vec3_t delta;

	(void)train;
	VectorSubtract(to_corner->s.origin, from_corner->s.origin, delta);
	VectorAdd(source, delta, destination_out);
	return true;
}

qboolean SG_OracleTrainRideEgress(const vec3_t source,
	const vec3_t destination, edict_t *train, int *arrival_ms)
{
	vec3_t delta;

	(void)train;
	VectorSubtract(destination, source, delta);
	if (!trace_l_corner && DotProduct(delta, delta) > 256.0f * 256.0f)
		return false;
	*arrival_ms = 100;
	return true;
}

static void TrainNode(rune_mechanism_node_t *node, uint32_t key,
	uint32_t master, uint16_t team_flag)
{
	memset(node, 0, sizeof(*node));
	node->key = key;
	node->kind = SG_MECH_NODE_TRAIN;
	node->flags = SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
		SG_MECH_NODEF_MOVER | team_flag;
	node->owner_key = SG_MECH_NO_KEY;
	node->team_master_key = master;
	node->spawnflags = 1U;
	node->use_callback = SG_MECH_CALLBACK_TRAIN_USE;
	node->think_callback = SG_MECH_CALLBACK_TRAIN_NEXT;
	node->blocked_callback = SG_MECH_CALLBACK_BLOCKED_TRAIN;
	node->speed_q8 = node->accel_q8 = node->decel_q8 = 3200U;
	node->target_offset = 1U;
}

static void CornerNode(rune_mechanism_node_t *node, uint32_t key)
{
	memset(node, 0, sizeof(*node));
	node->key = key;
	node->kind = SG_MECH_NODE_PATH_CORNER;
	node->flags = SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE;
	node->owner_key = SG_MECH_NO_KEY;
	node->team_master_key = SG_MECH_NO_KEY;
	node->touch_callback = SG_MECH_CALLBACK_PATH_CORNER_TOUCH;
	node->wait_ms = key == 30U || key == 37U ? 3000 : 0;
	node->target_offset = 1U;
	node->targetname_offset = 1U;
}

static void Edge(rune_mechanism_edge_t *edge, uint32_t from, uint32_t to,
	uint16_t kind)
{
	memset(edge, 0, sizeof(*edge));
	edge->from_key = from;
	edge->to_key = to;
	edge->kind = kind;
}

static void Catalog(rune_mechanism_node_t nodes[NODE_COUNT],
	rune_mechanism_edge_t edges[EDGE_COUNT], sg_mech_catalog_view_t *view)
{
	uint32_t i;

	TrainNode(&nodes[0], 25U, 25U, SG_MECH_NODEF_TEAM_MASTER);
	for (i = 0U; i < SG_TRAIN_STATION_ROUTE_CORNERS; i++)
		CornerNode(&nodes[i + 1U], route[i]);
	for (i = 0U; i < SG_TRAIN_STATION_ROUTE_CORNERS; i++)
	{
		uint32_t j;

		for (j = i + 1U; j < SG_TRAIN_STATION_ROUTE_CORNERS; j++)
			if (nodes[j + 1U].key < nodes[i + 1U].key)
			{
				rune_mechanism_node_t swap = nodes[i + 1U];
				nodes[i + 1U] = nodes[j + 1U];
				nodes[j + 1U] = swap;
			}
	}
	TrainNode(&nodes[15], 44U, 25U, SG_MECH_NODEF_TEAM_MEMBER);
	Edge(&edges[0], 25U, 44U, SG_MECH_EDGE_TEAM);
	Edge(&edges[1], 25U, 31U, SG_MECH_EDGE_ROUTE_TARGET);
	Edge(&edges[2], 44U, 38U, SG_MECH_EDGE_ROUTE_TARGET);
	for (i = 0U; i < SG_TRAIN_STATION_ROUTE_CORNERS; i++)
		Edge(&edges[i + 3U], route[i],
			route[(i + 1U) % SG_TRAIN_STATION_ROUTE_CORNERS],
			SG_MECH_EDGE_ROUTE_TARGET);
	for (i = 0U; i < EDGE_COUNT; i++)
	{
		uint32_t j;

		for (j = i + 1U; j < EDGE_COUNT; j++)
			if (edges[j].from_key < edges[i].from_key ||
			    (edges[j].from_key == edges[i].from_key &&
			     edges[j].kind < edges[i].kind))
			{
				rune_mechanism_edge_t swap = edges[i];
				edges[i] = edges[j];
				edges[j] = swap;
			}
	}
	memset(view, 0, sizeof(*view));
	view->nodes = nodes;
	view->num_nodes = NODE_COUNT;
	view->edges = edges;
	view->num_edges = EDGE_COUNT;
}

static int Connected(void *context, int seed)
{
	(void)context;
	(void)seed;
	return 1;
}

static rune_link_t *Append(void *context, int from, int to, int cost_ms)
{
	rune_link_t *link;

	(void)context;
	if (link_count >= (int)(sizeof(links) / sizeof(links[0])))
		return NULL;
	link = &links[link_count++];
	memset(link, 0, sizeof(*link));
	link->from = from;
	link->to = to;
	link->cost_ms = (short)cost_ms;
	return link;
}

static void Edicts(void)
{
	static const vec3_t origins[SG_TRAIN_STATION_ROUTE_CORNERS] = {
		{ -64.0f, -64.0f, -204.0f },
		{ 840.0f, -64.0f, -204.0f },
		{ 876.0f, -64.0f, -212.0f },
		{ 888.0f, -64.0f, -244.0f },
		{ 888.0f, -64.0f, -1572.0f },
		{ 876.0f, -64.0f, -1604.0f },
		{ 840.0f, -64.0f, -1612.0f },
		{ -64.0f, -64.0f, -1612.0f },
		{ -1020.0f, -64.0f, -1612.0f },
		{ -1056.0f, -64.0f, -1604.0f },
		{ -1068.0f, -64.0f, -1572.0f },
		{ -1068.0f, -64.0f, -244.0f },
		{ -1056.0f, -64.0f, -212.0f },
		{ -1020.0f, -64.0f, -204.0f }
	};
	uint32_t i;

	memset(edicts, 0, sizeof(edicts));
	for (i = 0U; i < SG_TRAIN_STATION_ROUTE_CORNERS; i++)
	{
		edict_t *corner = &edicts[route[i]];

		corner->inuse = true;
		corner->classname = "path_corner";
		VectorCopy(origins[i], corner->s.origin);
	}
	edicts[25].inuse = true;
	edicts[25].classname = "func_train";
	edicts[25].spawnflags = 1;
	VectorSet(edicts[25].mins, -64.0f, -64.0f, -204.0f);
	VectorSet(edicts[25].maxs, 200.0f, 192.0f, 96.0f);
	edicts[44].inuse = true;
	edicts[44].classname = "func_train";
	edicts[44].spawnflags = 1;
	VectorSet(edicts[44].mins, -64.0f, -64.0f, -1612.0f);
	VectorSet(edicts[44].maxs, 200.0f, 192.0f, -1312.0f);
}

int main(void)
{
	rune_mechanism_node_t nodes[NODE_COUNT];
	rune_mechanism_edge_t edges[EDGE_COUNT];
	sg_mech_catalog_view_t catalog;
	rune_seed_t seeds[4];
	rune_link_t dry_links[2];
	byte stable[6] = { 1, 1, 1, 1, 1, 1 };
	byte waterlevel[6] = { 0, 0, 0, 0, 0, 0 };
	sg_mechanism_plan_binding_t bindings[4];
	uint32_t num_bindings = 0U;
	sg_train_station_candidate_game_request_t request;
	const sg_train_station_candidate_game_diagnostics_t *diagnostics;

	memset(seeds, 0, sizeof(seeds));
	memset(bindings, 0, sizeof(bindings));
	memset(dry_links, 0, sizeof(dry_links));
	memset(links, 0, sizeof(links));
	memset(&request, 0, sizeof(request));
	g_edicts = edicts;
	globals.num_edicts = TEST_EDICTS;
	sg_host.level_alloc = LevelAlloc;
	sg_host.level_free = LevelFree;
	sg_host.linkentity = LinkEntity;
	sg_host.trace = Trace;
	Edicts();
	Catalog(nodes, edges, &catalog);
	VectorSet(seeds[0].origin, -72.0f, -160.0f, 120.125f);
	VectorSet(seeds[1].origin, -72.0f, -160.0f, -1287.875f);
	VectorSet(seeds[2].origin, -128.0f, -224.0f, 120.125f);
	VectorSet(seeds[3].origin, -128.0f, -224.0f, -1287.875f);
	dry_links[0].from = 0;
	dry_links[0].to = 2;
	dry_links[0].action = RL_RUN;
	dry_links[0].provenance = RL_PROVEN;
	dry_links[0].cost_ms = 100;
	dry_links[0].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	dry_links[1].from = 1;
	dry_links[1].to = 3;
	dry_links[1].action = RL_RUN;
	dry_links[1].provenance = RL_PROVEN;
	dry_links[1].cost_ms = 100;
	dry_links[1].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	request.catalog = &catalog;
	request.seeds = seeds;
	request.num_seeds = 4;
	request.links = dry_links;
	request.num_links = 2;
	request.source_stable = stable;
	request.source_waterlevel = waterlevel;
	request.has_incoming = Connected;
	request.has_outgoing = Connected;
	request.append = Append;
	request.bindings = bindings;
	request.num_bindings = &num_bindings;
	request.binding_capacity = 4U;
	{
		int added = SG_TrainStationCandidateGameGenerate(&request);

		CHECK(added == 2);
	}
	diagnostics = SG_TrainStationCandidateGameLastDiagnostics();
	if (diagnostics && diagnostics->appended != 2U)
		fprintf(stderr, "diag c=%u d=%u s=%u b=%u/%u carry=%u/%u "
			"egress=%u/%u append=%u\n", diagnostics->candidates,
			diagnostics->directions, diagnostics->source_candidates,
			diagnostics->board_successes, diagnostics->board_attempts,
			diagnostics->carry_successes, diagnostics->carry_attempts,
			diagnostics->egress_successes, diagnostics->egress_attempts,
			diagnostics->appended);
	CHECK(diagnostics != NULL);
	CHECK(diagnostics->candidates == 1U);
	CHECK(diagnostics->directions == 2U);
	CHECK(diagnostics->board_successes >= 2U);
	CHECK(diagnostics->carry_successes >= 14U);
	CHECK(diagnostics->egress_successes >= 2U);
	CHECK(diagnostics->appended == 2U);
	CHECK(link_count == 2);
	CHECK(num_bindings == 2U);
	CHECK(links[0].from == dry_links[0].to);
	CHECK(links[1].from == dry_links[1].to);
	CHECK(links[0].anchor[0] == -80.125f &&
	    links[0].anchor[1] == -47.875f &&
	    links[0].anchor[2] == 120.125f);
	CHECK(links[1].anchor[0] == -80.125f &&
	    links[1].anchor[1] == -47.875f &&
	    links[1].anchor[2] == -1287.875f);
	CHECK(links[0].mechanism_anchor[0] == -72.125f &&
	    links[0].mechanism_anchor[1] == -47.875f);
	CHECK(links[1].mechanism_anchor[0] == -72.125f &&
	    links[1].mechanism_anchor[1] == -47.875f);
	CHECK(memcmp(links[0].anchor, links[0].mechanism_anchor,
	    sizeof(vec3_t)) != 0);
	CHECK(memcmp(links[1].anchor, links[1].mechanism_anchor,
	    sizeof(vec3_t)) != 0);

	link_count = 0;
	num_bindings = 0U;
	request.links = NULL;
	request.num_links = 0;
	CHECK(SG_TrainStationCandidateGameGenerate(&request) == -1);
	CHECK(link_count == 0);

	request.links = dry_links;
	request.num_links = 2;
	dry_links[0].anchor[0] = 8.0f;
	dry_links[1].provenance = RL_DECLARED;
	CHECK(SG_TrainStationCandidateGameGenerate(&request) == 0);
	CHECK(link_count == 0);
	CHECK(num_bindings == 0U);

	{
		rune_seed_t ranked_seeds[6];
		rune_link_t ranked_links[66];
		int i;

		memset(ranked_seeds, 0, sizeof(ranked_seeds));
		memset(ranked_links, 0, sizeof(ranked_links));
		VectorSet(ranked_seeds[0].origin, 416.0f, -384.0f, 120.125f);
		VectorSet(ranked_seeds[1].origin, 416.0f, -384.0f, -1287.875f);
		VectorSet(ranked_seeds[2].origin, 400.0f, -384.0f, 120.125f);
		VectorSet(ranked_seeds[3].origin, 400.0f, -384.0f, -1287.875f);
		VectorSet(ranked_seeds[4].origin, 216.0f, 120.0f, 120.125f);
		VectorSet(ranked_seeds[5].origin, 216.0f, 120.0f, -1287.875f);
		for (i = 0; i < 66; i++)
		{
			rune_link_t *dry = &ranked_links[i];

			dry->from = i < 33 ? 0 : 1;
			dry->to = i < 32 ? 2 : i == 32 ? 4 : i < 65 ? 3 : 5;
			dry->action = RL_RUN;
			dry->provenance = RL_PROVEN;
			dry->cost_ms = 100;
			dry->mechanism_plan = RUNE_NO_MECHANISM_PLAN;
		}
		link_count = 0;
		num_bindings = 0U;
		board_only_near = 1;
		board_calls = 0;
		request.seeds = ranked_seeds;
		request.num_seeds = 6;
		request.links = ranked_links;
		request.num_links = 66;
		request.source_stable = stable;
		request.source_waterlevel = waterlevel;
		CHECK(SG_TrainStationCandidateGameGenerate(&request) == 2);
		diagnostics = SG_TrainStationCandidateGameLastDiagnostics();
		CHECK(diagnostics->board_attempts == 2U);
		CHECK(board_calls == 2);
		CHECK(link_count == 2);
		CHECK(links[0].anchor[0] == 216.125f);
		CHECK(links[0].anchor[1] == 120.0f);
		CHECK(links[1].anchor[0] == 216.125f);
		CHECK(links[1].anchor[1] == 120.0f);
	}

	{
		rune_seed_t many_seeds[40];
		rune_link_t many_links[39];
		byte many_stable[40];
		byte many_waterlevel[40];
		int i;

		memset(many_seeds, 0, sizeof(many_seeds));
		memset(many_links, 0, sizeof(many_links));
		memset(many_stable, 1, sizeof(many_stable));
		memset(many_waterlevel, 0, sizeof(many_waterlevel));
		VectorSet(many_seeds[0].origin, -72.0f, -160.0f, -1287.875f);
		for (i = 1; i < 39; i++)
			VectorSet(many_seeds[i].origin, 216.0f, 120.0f, 120.125f);
		VectorSet(many_seeds[39].origin, 416.0f, -384.0f, 120.125f);
		for (i = 0; i < 39; i++)
		{
			many_links[i].from = 0;
			many_links[i].to = i + 1;
			many_links[i].action = RL_RUN;
			many_links[i].provenance = RL_PROVEN;
			many_links[i].cost_ms = 100;
			many_links[i].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
		}
		link_count = 0;
		num_bindings = 0U;
		memset(bindings, 0, sizeof(bindings));
		board_only_near = 0;
		board_reject_positive_y = 1;
		board_calls = 0;
		trace_l_corner = 1;
		request.seeds = many_seeds;
		request.num_seeds = 40;
		request.links = many_links;
		request.num_links = 39;
		request.source_stable = many_stable;
		request.source_waterlevel = many_waterlevel;
		CHECK(SG_TrainStationCandidateGameGenerate(&request) == 1);
		diagnostics = SG_TrainStationCandidateGameLastDiagnostics();
		CHECK(diagnostics->board_attempts > 32U);
		CHECK(diagnostics->board_attempts == 39U);
		CHECK(board_calls == 39);
		CHECK(link_count == 1);
		CHECK(links[0].from == 39 && links[0].to == 0);
		link_count = 0;
		num_bindings = 0U;
		allocation_calls = 0;
		fail_allocation_call = 3;
		CHECK(SG_TrainStationCandidateGameGenerate(&request) == -1);
		CHECK(link_count == 0 && num_bindings == 0U);
		fail_allocation_call = -1;
		board_reject_positive_y = 0;
	}

	memset(seeds, 0, sizeof(seeds));
	memset(dry_links, 0, sizeof(dry_links));
	VectorSet(seeds[0].origin, 416.0f, -192.0f, 120.125f);
	VectorSet(seeds[1].origin, 416.0f, -192.0f, -1287.875f);
	VectorSet(seeds[2].origin, 352.0f, -192.0f, 120.125f);
	VectorSet(seeds[3].origin, 352.0f, -192.0f, -1287.875f);
	for (int i = 0; i < 2; i++)
	{
		dry_links[i].from = i;
		dry_links[i].to = i + 2;
		dry_links[i].action = RL_RUN;
		dry_links[i].provenance = RL_PROVEN;
		dry_links[i].cost_ms = 100;
		dry_links[i].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	}
	link_count = 0;
	num_bindings = 0U;
	board_only_near = 0;
	board_calls = 0;
	trace_l_corner = 1;
	request.seeds = seeds;
	request.num_seeds = 4;
	request.links = dry_links;
	request.num_links = 2;
	request.source_stable = stable;
	request.source_waterlevel = waterlevel;
	CHECK(SG_TrainStationCandidateGameGenerate(&request) == 2);
	CHECK(board_calls == 2);
	CHECK(link_count == 2);
	CHECK(links[0].from == 2);
	CHECK(links[1].from == 3);
	CHECK(links[0].anchor[0] == 183.875f);
	CHECK(links[0].anchor[1] == -80.125f);
	CHECK(links[1].anchor[0] == 183.875f);
	CHECK(links[1].anchor[1] == -80.125f);
	if (failures)
		return 1;
	puts("sg_train_station_candidate_game_test: PASS");
	return 0;
}
