#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_field_projection.h"
#include "slipgate/sg_tactic_policy.h"
#include "slipgate/sg_localization.h"
#include "slipgate/sg_intercept_policy.h"
#include "slipgate/sg_carrier_cover.h"
#include "slipgate/sg_hooks.h"

int Fields_DefensiveRoot(const rune_t *r, const unsigned char *plane);
void Fields_TestFloodFlat(rune_t *r, int *dist,
	const int *sources, const int *source_cost, int num_sources);
int SG_CacoLifecycleTest(void);
int Rally_CoverSeed(const rune_t *r, int from);

/* Field_Flood's focused production section needs only these host-owned
 * globals.  Everything else in sg_fields.c is discarded by --gc-sections. */
sg_host_t sg_host;
sg_cvars_t sg_cv;
cvar_t *ctfflags;
level_locals_t level;
game_export_t globals;
game_locals_t game;

static edict_t test_edicts[4];
edict_t *g_edicts = test_edicts;
static int allocation_count;
static rune_t *test_current_rune;

rune_t *SG_Rune(void)
{
	return test_current_rune;
}

sg_identity_status_t SG_LevelIdentitySnapshot(const char *mapname,
	sg_level_identity_t *out)
{
	const rune_identity_t *identity;

	if (!test_current_rune || !mapname || !out)
		return SG_IDENTITY_UNAVAILABLE;
	identity = &test_current_rune->artifact.identity;
	if (strcmp(mapname, identity->map_name) != 0)
		return SG_IDENTITY_MAPNAME_MISMATCH;
	memset(out, 0, sizeof(*out));
	out->bsp_checksum = identity->bsp_checksum;
	out->entity_crc32 = identity->entity_crc32;
	out->host_physics_id = identity->host_physics_id;
	memcpy(out->mapname, identity->map_name, sizeof(out->mapname));
	out->mapname[sizeof(out->mapname) - 1] = '\0';
	return SG_IDENTITY_OK;
}

int SG_TeamIdx(int team)
{
	return team - CTF_TEAM_RED;
}

static void TestDprint(const char *fmt, ...)
{
	(void)fmt;
}

static trace_t TestTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t tr;

	(void)start;
	(void)mins;
	(void)maxs;
	(void)passent;
	(void)contentmask;
	memset(&tr, 0, sizeof(tr));
	tr.fraction = 1.0f;
	VectorCopy(end, tr.endpos);
	return tr;
}

static void *TestLevelAlloc(int size)
{
	allocation_count++;
	return calloc(1, (size_t)size);
}

edict_t *SG_FlagStand(int team, qboolean own)
{
	(void)own;
	return &test_edicts[team == CTF_TEAM_BLUE ? 2 : 1];
}

int Rune_NearestSeed(rune_t *r, vec3_t p)
{
	int i, best = -1;
	float best_dist = 0.0f;

	for (i = 0; i < r->hdr.num_seeds; i++)
	{
		vec3_t d;
		float dist;

		VectorSubtract(r->seeds[i].origin, p, d);
		dist = DotProduct(d, d);
		if (best < 0 || dist < best_dist)
		{
			best = i;
			best_dist = dist;
		}
	}
	return best;
}

_Static_assert(SG_DPO_POST_RED == 0, "DPO post-red plane drift");
_Static_assert(SG_DPO_POST_BLUE == 1, "DPO post-blue plane drift");
_Static_assert(SG_DPO_INTERCEPT_RED == 2, "DPO intercept-red plane drift");
_Static_assert(SG_DPO_INTERCEPT_BLUE == 3, "DPO intercept-blue plane drift");
_Static_assert(SG_DPO_PLANE_COUNT == 4, "DPO plane-count drift");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void Link(rune_link_t *link, int from, int to, int action, int cost)
{
	memset(link, 0, sizeof(*link));
	link->from = from;
	link->to = to;
	link->action = (unsigned char)action;
	link->cost_ms = (short)cost;
	link->heading_slack = 255;
}

static void CheckHookFieldAdmission(void)
{
	rune_t rune;
	rune_seed_t seeds[7];
	rune_link_t links[9];
	int first_link[7], next_link[9];
	sg_field_setup_inputs_t inputs;
	unsigned char planes[SG_DPO_PLANE_COUNT][7];
	cvar_t hook_cvar;
	cvar_t debug_cvar, mega_cvar, rope_cvar, shelf_cvar;
	int allocations_after_setup;
	int *red_field, *post_field, *lane_field, *item_field;
	int flat[7], source = 3, source_cost = 0;
	int initial_lane[7], initial_lane_seed;
	int projection_field[7], steps[SG_PROJ_BRANCH];
	int num_steps;
	qboolean setup_ok;
	unsigned initial_epoch;
	int i;

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	for (i = 0; i < 7; i++)
		first_link[i] = -1;
	memset(&inputs, 0, sizeof(inputs));
	memset(planes, 0, sizeof(planes));
	memset(&hook_cvar, 0, sizeof(hook_cvar));
	memset(&debug_cvar, 0, sizeof(debug_cvar));
	memset(&mega_cvar, 0, sizeof(mega_cvar));
	memset(&rope_cvar, 0, sizeof(rope_cvar));
	memset(&shelf_cvar, 0, sizeof(shelf_cvar));
	memset(&sg_fields, 0, sizeof(sg_fields));
	memset(&sg_caco_team_belief, 0, sizeof(sg_caco_team_belief));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&level, 0, sizeof(level));
	memset(&globals, 0, sizeof(globals));
	rune.hdr.num_seeds = 7;
	rune.hdr.num_links = 9;
	rune.seeds = seeds;
	rune.links = links;
	rune.first_link = first_link;
	rune.next_link = next_link;
	strcpy(rune.artifact.identity.map_name, "testmap");
	for (i = 0; i < 7; i++)
		seeds[i].origin[0] = (float)i * 100.0f;
	seeds[2].origin[2] = -200.0f;
	for (i = 0; i < 7; i++)
		projection_field[i] = SG_FIELD_INF;
	projection_field[1] = 1000;
	projection_field[3] = 100;
	projection_field[0] = 200;

	/* 0/1 is a RUN SCC.  Its cheap exit is a hook, while 0->2->3 is
	 * the executable but longer route. */
	Link(&links[0], 0, 1, RL_RUN, 100);
	Link(&links[1], 1, 0, RL_RUN, 100);
	Link(&links[2], 1, 3, RL_HOOK, 100);
	Link(&links[3], 0, 2, RL_RUN, 1000);
	Link(&links[4], 2, 3, RL_RUN, 1000);
	/* 4/5 is another RUN SCC whose sole exit is a hook. */
	Link(&links[5], 4, 5, RL_RUN, 100);
	Link(&links[6], 5, 4, RL_RUN, 100);
	Link(&links[7], 5, 3, RL_HOOK, 100);
	/* Seed 6 is an independent non-hook reachability control. */
	Link(&links[8], 6, 3, RL_RUN, 700);
	CHECK(Fields_LinkTraversalCostMs(&links[3]) == 1000);
	for (i = 8; i >= 0; i--)
	{
		next_link[i] = first_link[links[i].from];
		first_link[links[i].from] = i;
	}

	/* Both stands and every authenticated DPO plane use seed 3, so the
	 * cached goal and learned fields expose the same topology transition. */
	VectorCopy(seeds[3].origin, test_edicts[1].s.origin);
	VectorCopy(seeds[3].origin, test_edicts[2].s.origin);
	for (i = 0; i < SG_DPO_PLANE_COUNT; i++)
	{
		planes[i][3] = 1;
		inputs.dpo[i] = planes[i];
	}
	for (i = 0; i < 2; i++)
	{
		sg_caco_team_belief.carrier[i].client = -1;
		sg_caco_team_belief.carrier[i].seed = -1;
		sg_caco_team_belief.enemy_carrier[i].client = -1;
		sg_caco_team_belief.enemy_carrier[i].seed = -1;
	}
	globals.num_edicts = 0;
	debug_cvar.value = 0.0f;
	mega_cvar.value = 0.0f;
	rope_cvar.value = 0.0f;
	shelf_cvar.value = 0.0f;
	sg_cv.debug = &debug_cvar;
	sg_cv.megaworth = &mega_cvar;
	sg_cv.ropecost = &rope_cvar;
	sg_cv.shelfcost = &shelf_cvar;
	sg_host.dprint = TestDprint;
	sg_host.trace = TestTrace;
	sg_host.level_alloc = TestLevelAlloc;

	hook_cvar.value = CTF_OFFHAND_HOOK;
	ctfflags = &hook_cvar;
	allocation_count = 0;
	setup_ok = Fields_Setup(&rune, &inputs);
	CHECK(setup_ok);
	if (!setup_ok)
		return;
	allocations_after_setup = allocation_count;
	CHECK(allocations_after_setup > 0);
	CHECK(sg_fields.hook_admitted);
	CHECK(sg_fields.action_topology_epoch == 1U);
	initial_epoch = sg_fields.action_topology_epoch;
	CHECK(Fields_ActionTopologyCurrent(initial_epoch));
	CHECK(sg_fields.to_red_flag[0] == 200);
	CHECK(sg_fields.to_red_flag[1] == 100);
	CHECK(sg_fields.to_red_flag[4] == 200);
	CHECK(sg_fields.to_red_flag[5] == 100);
	CHECK(sg_fields.to_red_flag[6] == 700);
	CHECK(sg_fields.to_post[0][0] == 200);
	Fields_TestFloodFlat(&rune, flat, &source, &source_cost, 1);
	CHECK(flat[0] == 200);
	CHECK(flat[4] == 200);
	red_field = sg_fields.to_red_flag;
	post_field = sg_fields.to_post[0];
	lane_field = sg_fields.to_lane[0];
	item_field = sg_fields.item[SG_FC_WEAPON];
	memcpy(initial_lane, lane_field, sizeof(initial_lane));
	initial_lane_seed = sg_fields.lane_seed[0];
	CHECK(sg_fields.shelf_cliff[0][2] == 1000);
	num_steps = SG_FieldProjectionSteps(&rune, projection_field, 1, steps);
	CHECK(num_steps == 2);
	CHECK(steps[0] == 3);
	CHECK(steps[1] == 0);

	hook_cvar.value = 0.0f;
	/* Sentinel values prove DPO/lane/item buffers are overwritten rather than
	 * replaced or left on the old topology. */
	post_field[0] = 12345;
	lane_field[6] = 12345;
	item_field[0] = 12345;
	sg_fields.shelf_cliff[0][2] = 12345;
	/* A carrier in the hook-only SCC must not project through the forbidden
	 * hook merely because its destination has a lower filtered home cost. */
	sg_caco_team_belief.carrier[0].client = 0;
	sg_caco_team_belief.carrier[0].seed = 5;
	level.time = 1.0f;
	CHECK(Fields_ActionTopologyRefresh(&rune));
	CHECK(sg_fields.action_topology_pending);
	num_steps = SG_FieldProjectionSteps(&rune, projection_field, 1, steps);
	CHECK(num_steps == 1);
	CHECK(steps[0] == 0);
	Fields_Refresh(&rune);
	CHECK(!sg_fields.hook_admitted);
	CHECK(sg_fields.action_topology_epoch == 2U);
	CHECK(!sg_fields.action_topology_pending);
	CHECK(!Fields_ActionTopologyCurrent(initial_epoch));
	CHECK(Fields_ActionTopologyCurrent(sg_fields.action_topology_epoch));
	CHECK(allocation_count == allocations_after_setup);
	CHECK(sg_fields.to_red_flag == red_field);
	CHECK(sg_fields.to_post[0] == post_field);
	CHECK(sg_fields.to_lane[0] == lane_field);
	CHECK(sg_fields.item[SG_FC_WEAPON] == item_field);
	CHECK(red_field[0] == 2000);
	CHECK(red_field[1] == 2100);
	CHECK(red_field[2] == 1000);
	CHECK(red_field[4] == SG_FIELD_INF);
	CHECK(red_field[5] == SG_FIELD_INF);
	CHECK(red_field[6] == 700);
	CHECK(post_field[0] == 2000);
	CHECK(lane_field[6] != 12345);
	CHECK(item_field[0] == SG_FIELD_INF);
	CHECK(sg_fields.shelf_cliff[0][2] == 1000);
	CHECK(sg_fields.to_flag_now[0][0][0] == 2000);
	CHECK(sg_fields.our_carrier_valid[0]);
	CHECK(sg_fields.our_carrier[0][5] == 0);
	Fields_TestFloodFlat(&rune, flat, &source, &source_cost, 1);
	CHECK(flat[0] == 2000);
	CHECK(flat[4] == SG_FIELD_INF);

	/* Enabling the map capability rebuilds the same buffers back onto the
	 * hook topology. */
	hook_cvar.value = CTF_OFFHAND_HOOK;
	/* This edge occurs before the next one-second cadence, yet must rebuild
	 * synchronously on this refresh call. */
	level.time = 1.25f;
	CHECK(Fields_ActionTopologyRefresh(&rune));
	CHECK(sg_fields.action_topology_pending);
	num_steps = SG_FieldProjectionSteps(&rune, projection_field, 1, steps);
	CHECK(num_steps == 2);
	CHECK(steps[0] == 3);
	CHECK(steps[1] == 0);
	Fields_Refresh(&rune);
	CHECK(sg_fields.hook_admitted);
	CHECK(sg_fields.action_topology_epoch == 3U);
	CHECK(allocation_count == allocations_after_setup);
	CHECK(red_field[0] == 200);
	CHECK(red_field[1] == 100);
	CHECK(red_field[4] == 200);
	CHECK(red_field[5] == 100);
	CHECK(red_field[6] == 700);
	CHECK(post_field[0] == 200);
	CHECK(sg_fields.lane_seed[0] == initial_lane_seed);
	CHECK(memcmp(lane_field, initial_lane, sizeof(initial_lane)) == 0);
	CHECK(sg_fields.shelf_cliff[0][2] == 1000);
	CHECK(sg_fields.to_flag_now[1][0][0] == 200);
	CHECK(sg_fields.our_carrier[0][5] == 0);

	/* A missing host cvar is the same capability edge as an explicit zero. */
	ctfflags = NULL;
	level.time = 1.5f;
	CHECK(Fields_ActionTopologyRefresh(&rune));
	num_steps = SG_FieldProjectionSteps(&rune, projection_field, 1, steps);
	CHECK(num_steps == 1);
	CHECK(steps[0] == 0);
	Fields_Refresh(&rune);
	CHECK(!sg_fields.hook_admitted);
	CHECK(sg_fields.action_topology_epoch == 4U);
	CHECK(allocation_count == allocations_after_setup);
	CHECK(red_field[0] == 2000);
	CHECK(red_field[1] == 2100);
	CHECK(red_field[4] == SG_FIELD_INF);
	CHECK(red_field[5] == SG_FIELD_INF);
	CHECK(red_field[6] == 700);
	Fields_TestFloodFlat(&rune, flat, &source, &source_cost, 1);
	CHECK(flat[0] == 2000);
	CHECK(flat[4] == SG_FIELD_INF);
}

static void CheckInterceptAdmission(void)
{
	rune_t rune;
	rune_seed_t seeds[5];
	rune_link_t links[3];
	int first_link[5] = { 0, -1, -1, -1, -1 };
	int next_link[3] = { 1, 2, -1 };
	int to_blue[5] = { 500, 0, 300, 400, 600 };

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	rune.hdr.num_seeds = 5;
	rune.hdr.num_links = 3;
	rune.seeds = seeds;
	rune.links = links;
	rune.first_link = first_link;
	rune.next_link = next_link;
	seeds[0].origin[0] = 0.0f;
	seeds[1].origin[0] = 400.0f;
	seeds[2].origin[1] = 150.0f;
	seeds[2].origin[2] = 100.0f;
	seeds[3].origin[1] = 50.0f;
	seeds[4].origin[1] = 300.0f;
	seeds[4].origin[2] = 300.0f;
	Link(&links[0], 0, 2, RL_HOOK, 100);
	Link(&links[1], 0, 3, RL_RUN, 100);
	Link(&links[2], 0, 4, RL_RUN, 100);

	memset(sg_caco_proj, 0, sizeof(sg_caco_proj));
	sg_caco_proj[0].client = 0;
	sg_caco_proj[0].n = 2;
	sg_caco_proj[0].seed[0] = 0;
	sg_caco_proj[0].seed[1] = 1;
	test_current_rune = &rune;

	sg_fields.hook_admitted = true;
	CHECK(SG_InterceptHoldSeed(&rune, &sg_caco_proj[0], to_blue, 1) == 2);
	sg_fields.hook_admitted = false;
	CHECK(SG_InterceptHoldSeed(&rune, &sg_caco_proj[0], to_blue, 1) == 3);
	test_current_rune = NULL;
}

static void CheckRallyCoverAdmission(void)
{
	rune_t rune;
	rune_seed_t seeds[5];
	rune_link_t links[4];
	int first_link[5] = { 0, -1, -1, -1, -1 };
	int next_link[4] = { 1, 2, 3, -1 };

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	rune.hdr.num_seeds = 5;
	rune.hdr.num_links = 4;
	rune.seeds = seeds;
	rune.links = links;
	rune.first_link = first_link;
	rune.next_link = next_link;
	seeds[0].area_hint = 100;
	seeds[1].area_hint = 20;
	seeds[1].origin[0] = 900.0f; /* proved but outside the local cover band */
	seeds[2].area_hint = 20;
	seeds[2].origin[0] = 200.0f; /* a hook is not direct-walk authority */
	seeds[3].area_hint = 20;
	seeds[3].origin[0] = 400.0f;
	seeds[4].area_hint = 20;
	seeds[4].origin[0] = 40.0f;  /* nearby but has no proved edge */
	Link(&links[0], 0, 1, RL_RUN, 100);
	Link(&links[1], 0, 2, RL_HOOK, 100);
	Link(&links[2], 0, 3, RL_RUN, 100);
	Link(&links[3], 4, 0, RL_RUN, 100);

	CHECK(SG_CarrierCoverRouteAllowed(&rune, 0, 0));
	CHECK(SG_CarrierCoverRouteAllowed(&rune, 0, 1));
	CHECK(SG_CarrierCoverRouteAllowed(&rune, 0, 3));
	CHECK(!SG_CarrierCoverRouteAllowed(&rune, 0, 2));
	CHECK(!SG_CarrierCoverRouteAllowed(&rune, 0, 4));
	CHECK(!SG_CarrierCoverRouteAllowed(&rune, 0, 5));
	rune.links = NULL;
	CHECK(SG_CarrierCoverRouteAllowed(&rune, 0, 0));
	CHECK(!SG_CarrierCoverRouteAllowed(&rune, 0, 1));
	rune.links = links;
	CHECK(Rally_CoverSeed(&rune, 0) == 3);
	seeds[3].area_hint = 100;
	CHECK(Rally_CoverSeed(&rune, 0) == -1);
	seeds[0].area_hint = 60;
	CHECK(Rally_CoverSeed(&rune, 0) == 0);
	CHECK(Rally_CoverSeed(&rune, -1) == -1);
	CHECK(Rally_CoverSeed(NULL, 0) == -1);
}

static void CheckCarrierTrailFollowsTheScoringRoute(void)
{
	rune_t rune;
	rune_seed_t seeds[4];
	rune_link_t links[2];
	int first_link[4] = { -1, 0, 1, -1 };
	int next_link[2] = { -1, -1 };
	int home[4] = { 1000, 1500, 2000, 1500 };

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	rune.hdr.num_seeds = 4;
	rune.hdr.num_links = 2;
	rune.seeds = seeds;
	rune.links = links;
	rune.first_link = first_link;
	rune.next_link = next_link;
	seeds[1].origin[0] = 100.0f;
	seeds[2].origin[0] = 200.0f;
	seeds[3].origin[0] = 1.0f;
	Link(&links[0], 1, 0, RL_RUN, 500);
	Link(&links[1], 2, 1, RL_RUN, 500);

	CHECK(SG_FieldCarrierTrailStation(&rune, home, 0, 1400, 1600) == 1);
	CHECK(SG_FieldCarrierTrailStation(&rune, home, 0, 1900, 2100) == 2);
	links[0].cost_ms = 600;
	CHECK(SG_FieldCarrierTrailStation(&rune, home, 0, 1400, 2100) == -1);
}

static void CheckLocalFieldSeedAdmission(void)
{
	rune_t rune;
	rune_seed_t seeds[2];
	byte linked[2] = { 1, 1 };
	int field[2] = { SG_FIELD_INF, 500 };
	vec3_t point = { 0.0f, 0.0f, 0.0f };

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	rune.hdr.num_seeds = 2;
	rune.seeds = seeds;
	rune.linked_seed = linked;

	CHECK(SG_LocalSeedScore(&rune, field, 1, 64.0f, 0.0f, 32.0f) >= 0.0f);
	CHECK(SG_LocalSeedScore(&rune, field, 0, 1.0f, 0.0f, 0.0f) < 0.0f);
	CHECK(SG_LocalSeedScore(&rune, field, 1, 129.0f, 0.0f, 0.0f) < 0.0f);
	CHECK(SG_LocalSeedScore(&rune, field, 1, 0.0f, 0.0f, 97.0f) < 0.0f);
	seeds[1].flags = RSF_TOMBSTONE;
	CHECK(SG_LocalSeedScore(&rune, field, 1, 1.0f, 0.0f, 0.0f) < 0.0f);
	seeds[1].flags = 0;
	linked[1] = 0;
	CHECK(SG_LocalSeedScore(&rune, field, 1, 1.0f, 0.0f, 0.0f) < 0.0f);
	linked[1] = 1;
	CHECK(SG_LocalSeedScore(&rune, NULL, 1, 1.0f, 0.0f, 0.0f) >= 0.0f);

	rune.artifact.route_contract = RUNE_ROUTE_CONTRACT_LOCAL_ONLY;
	linked[0] = 0;
	seeds[0].flags = RSF_OBJECTIVE;
	seeds[1].flags = RSF_OBJECTIVE;
	seeds[1].origin[0] = 300.0f;
	CHECK(SG_LocalObjectiveSeed(&rune, point) == 0);
	point[0] = 150.0f;
	CHECK(SG_LocalObjectiveSeed(&rune, point) == -1);
	point[0] = 32.0f;
	seeds[1].origin[0] = 64.0f;
	CHECK(SG_LocalObjectiveSeed(&rune, point) == -1);
	rune.artifact.route_contract = RUNE_ROUTE_CONTRACT_COMPLETE;
	point[0] = 0.0f;
	CHECK(SG_LocalObjectiveSeed(&rune, point) == -1);
}

static void CheckCarrierProjectionPricesWholeEdge(void)
{
	rune_t rune;
	rune_seed_t seeds[4];
	rune_link_t links[4];
	int first_link[4] = { 0, -1, 2, -1 };
	int next_link[4] = { 1, 3, -1, -1 };
	int home[4] = { 600, 200, 500, 200 };
	int steps[SG_PROJ_BRANCH];

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	rune.hdr.num_seeds = 4;
	rune.hdr.num_links = 4;
	rune.seeds = seeds;
	rune.links = links;
	rune.first_link = first_link;
	rune.next_link = next_link;
	Link(&links[0], 0, 1, RL_RUN, 1000);
	Link(&links[1], 0, 2, RL_RUN, 100);
	Link(&links[2], 2, 3, RL_RUN, 100);
	Link(&links[3], 0, 1, RL_RUN, 1500);
	seeds[1].origin[0] = 10.0f;
	seeds[3].origin[0] = 50.0f;

	/* The smallest destination suffix owns the slow edge. Projection must
	 * follow the cheaper complete route instead. */
	CHECK(SG_FieldProjectionStep(&rune, home, 0) == 2);
	CHECK(SG_FieldProjectionSteps(&rune, home, 0, steps) == 1);
	CHECK(steps[0] == 2);
	links[3].cost_ms = 50;
	CHECK(SG_FieldProjectionStep(&rune, home, 0) == 1);
	CHECK(SG_FieldProjectionSteps(&rune, home, 0, steps) == 2);
	CHECK(steps[0] == 1);
	CHECK(steps[1] == 2);
	links[3].cost_ms = 1500;
	/* Seed 1 occupies the requested cost band but is on the slow parallel
	 * branch. The lead station stays on the carrier's selected route. */
	CHECK(SG_FieldCarrierLeadStation(&rune, home, 0, 150, 250) == 3);
	/* A lead station must advance off the carrier's own seed even when the
	 * requested near-home band also contains the carrier. */
	home[0] = 300;
	home[2] = 200;
	home[3] = 100;
	CHECK(SG_FieldCarrierLeadStation(&rune, home, 0, 0, 450) == 2);
	home[0] = SG_FIELD_INF;
	CHECK(SG_FieldCarrierLeadStation(&rune, home, 0, 150, 250) == -1);
	home[0] = 600;
	home[1] = 700;
	home[2] = 800;
	CHECK(SG_FieldProjectionStep(&rune, home, 0) == -1);
}

static void CheckCarrierScreenUsesTravelTime(void)
{
	rune_t rune;
	rune_seed_t seeds[5];
	rune_link_t links[4];
	int first_link[5] = { 0, 1, 2, 3, -1 };
	int next_link[4] = { -1, -1, -1, -1 };
	int home[5] = { 6000, 5300, 4300, 2000, 0 };

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	rune.hdr.num_seeds = 5;
	rune.hdr.num_links = 4;
	rune.seeds = seeds;
	rune.links = links;
	rune.first_link = first_link;
	rune.next_link = next_link;
	Link(&links[0], 0, 1, RL_RUN, 700);
	Link(&links[1], 1, 2, RL_RUN, 1000);
	Link(&links[2], 2, 3, RL_RUN, 2300);
	Link(&links[3], 3, 4, RL_RUN, 2000);

	CHECK(SG_FieldCarrierScreenStation(&rune, home, 0) == 2);
	CHECK(SG_FieldCarrierSupportRoot(&rune, home, 1, 0) == 2);
	first_link[1] = -1;
	CHECK(SG_FieldCarrierScreenStation(&rune, home, 0) == -1);
	CHECK(SG_FieldCarrierSupportRoot(&rune, home, 1, 0) == 0);
	home[1] = 3000;
	CHECK(SG_FieldCarrierScreenStation(&rune, home, 0) == 1);
	first_link[0] = -1;
	CHECK(SG_FieldCarrierScreenStation(&rune, home, 0) == -1);
	CHECK(SG_FieldCarrierSupportRoot(&rune, home, 1, 0) == 0);
	CHECK(SG_FieldCarrierSupportRoot(&rune, home, 0, 0) == -1);
	CHECK(SG_FieldCarrierSupportRoot(&rune, home, 2, 0) == -1);
	CHECK(SG_FieldCarrierScreenStation(NULL, home, 0) == -1);
}

static void CheckObjectiveFieldRootTracksMovingGoals(void)
{
	rune_t rune;
	int field[5] = { 900, 500, 0, 700, SG_FIELD_INF };

	memset(&rune, 0, sizeof(rune));
	rune.hdr.num_seeds = 5;
	CHECK(SG_FieldRootSeed(&rune, field) == 2);
	field[4] = 0;
	CHECK(SG_FieldRootSeed(&rune, field) == 2);
	field[4] = SG_FIELD_INF;
	field[2] = 800;
	field[3] = 0;
	CHECK(SG_FieldRootSeed(&rune, field) == 3);
	field[3] = SG_FIELD_INF;
	CHECK(SG_FieldRootSeed(&rune, field) == 1);
	field[1] = SG_FIELD_INF;
	field[2] = SG_FIELD_INF;
	field[0] = SG_FIELD_INF;
	CHECK(SG_FieldRootSeed(&rune, field) == -1);
	CHECK(SG_FieldRootSeed(NULL, field) == -1);
}

static void CheckTacticCacheTracksObjectiveIdentity(void)
{
	int fixed_field[2] = { 500, 0 };
	int moving_field[2] = { 0, 500 };
	sg_tactic_cache_t cache = {
		.topology_current = true, .tactic_seed = 4,
		.cached_role = SG_ROLE_ATTACK, .current_role = SG_ROLE_ATTACK,
		.cached_goal = { moving_field, 1 },
		.current_goal = { moving_field, 1 },
		.committed_at = 10.0f, .now = 11.0f, .route_cost = 900
	};
	sg_tactic_cache_t base = cache;
#define CHECK_REFRESH(statement) do { \
	cache = base; statement; \
	CHECK(SG_TacticCacheNeedsRefresh(&cache)); \
} while (0)
	CHECK(!SG_TacticCacheNeedsRefresh(&cache));
	CHECK_REFRESH(cache.current_goal.root_seed = 0);
	CHECK_REFRESH(cache.current_goal.field = fixed_field);
	CHECK_REFRESH(cache.current_role = SG_ROLE_RECOVER);
	CHECK_REFRESH(cache.committed_at = 12.0f);
	cache = base;
	cache.now = 20.0f;
	CHECK(!SG_TacticCacheNeedsRefresh(&cache));
	CHECK_REFRESH(cache.now = 20.1f);
	CHECK_REFRESH(cache.topology_current = false);
	CHECK_REFRESH(cache.tactic_seed = -1);
	CHECK_REFRESH(cache.route_cost = 299);
	cache = base;
	cache.route_cost = 300;
	CHECK(!SG_TacticCacheNeedsRefresh(&cache));
	CHECK_REFRESH(cache.route_cost = SG_FIELD_INF);
#undef CHECK_REFRESH
}

int main(void)
{
	rune_t rune;
	rune_seed_t seeds[5];
	byte linked[5];
	unsigned char plane[5];

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(linked, 1, sizeof(linked));
	rune.hdr.num_seeds = 5;
	rune.seeds = seeds;

	CHECK(Fields_DefensiveRoot(&rune, NULL) == -1);
	memset(plane, 0, sizeof(plane));
	CHECK(Fields_DefensiveRoot(&rune, plane) == -1);

	/* The first maximum wins, preserving the historic deterministic tie. */
	plane[0] = 7;
	plane[1] = 12;
	plane[2] = 12;
	CHECK(Fields_DefensiveRoot(&rune, plane) == 1);

	/* Even a malformed nonzero tombstone cell can never become the root. */
	seeds[1].flags = RSF_TOMBSTONE;
	plane[1] = 255;
	CHECK(Fields_DefensiveRoot(&rune, plane) == 2);

	memset(plane, 0, sizeof(plane));
	plane[1] = 255;
	CHECK(Fields_DefensiveRoot(&rune, plane) == -1);

	/* A corrupted non-tombstone without outgoing ownership is also inert. */
	memset(seeds, 0, sizeof(seeds));
	memset(plane, 0, sizeof(plane));
	rune.linked_seed = linked;
	linked[3] = 0;
	plane[3] = 255;
	plane[4] = 9;
	CHECK(Fields_DefensiveRoot(&rune, plane) == 4);

	CheckHookFieldAdmission();
	CheckInterceptAdmission();
	CheckRallyCoverAdmission();
	CheckCarrierTrailFollowsTheScoringRoute();
	CheckLocalFieldSeedAdmission();
	failures += SG_CacoLifecycleTest();
	CheckCarrierProjectionPricesWholeEdge();
	CheckCarrierScreenUsesTravelTime();
	CheckObjectiveFieldRootTracksMovingGoals();
	CheckTacticCacheTracksObjectiveIdentity();

	if (failures)
	{
		fprintf(stderr, "sg_fields_candidate_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_fields_candidate_test: ok");
	return 0;
}
