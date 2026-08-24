#include <stdio.h>
#include <string.h>

#include "q_shared.h"
#include "slipgate/sg_timed_vault_egress.h"

static int failures;

static void Check(int condition, const char *name)
{
	if (!condition)
	{
		fprintf(stderr, "FAIL: %s\n", name);
		failures++;
	}
}

static void Seed(rune_seed_t *seed, float x, float y, float z, int flags)
{
	memset(seed, 0, sizeof(*seed));
	seed->origin[0] = x;
	seed->origin[1] = y;
	seed->origin[2] = z;
	seed->flags = (short)flags;
}

static void Link(rune_link_t *link, int from, int to, int action)
{
	memset(link, 0, sizeof(*link));
	link->from = from;
	link->to = to;
	link->action = (byte)action;
}

static qboolean RejectNearSide(const vec3_t origin, const vec3_t source,
	void *context)
{
	int *calls = context;

	(void)origin;
	(*calls)++;
	return source[0] != 128.0f;
}

static qboolean RejectAll(const vec3_t origin, const vec3_t source,
	void *context)
{
	(void)origin;
	(void)source;
	(void)context;
	return false;
}

static qboolean AcceptAll(const vec3_t origin, const vec3_t source,
	void *context)
{
	(void)origin;
	(void)source;
	(void)context;
	return true;
}

static void TestTargetFacingWaterBranch(void)
{
	rune_seed_t seeds[7];
	rune_link_t links[5];
	int next[7], dist[7], incoming[7], next_incoming[5], queue[7];
	int heap[7], heap_pos[7];
	float score[7];
	vec3_t final = { 400.0f, 0.0f, 0.0f };

	Seed(&seeds[0], 0.0f, 0.0f, -32.0f, RSF_WATER);
	Seed(&seeds[1], 0.0f, 64.0f, -32.0f, RSF_WATER);
	Seed(&seeds[2], 0.0f, 128.0f, -32.0f, RSF_WATER);
	Seed(&seeds[3], 0.0f, 192.0f, 0.0f, 0);
	Seed(&seeds[4], 100.0f, 0.0f, -32.0f, RSF_WATER);
	Seed(&seeds[5], 200.0f, 0.0f, -32.0f, RSF_WATER);
	Seed(&seeds[6], 300.0f, 0.0f, 0.0f, 0);
	Link(&links[0], 0, 1, RL_SWIM);
	Link(&links[1], 1, 3, RL_SWIM);
	Link(&links[2], 0, 4, RL_SWIM);
	Link(&links[3], 4, 5, RL_SWIM);
	Link(&links[4], 5, 6, RL_SWIM);

	Check(SG_WaterEscapeIndexBuild(seeds, 7, links, 5, next, dist,
	          incoming, next_incoming, queue),
	    "target branch fixture has a generic escape index");
	Check(next[0] == 1,
	    "generic any-shore index discards the longer target-facing branch");
	Check(SG_WaterEscapeTargetIndexBuild(seeds, 7, links, 5, final, next,
	          score, incoming, next_incoming, heap, heap_pos),
	    "target-facing water index builds");
	Check(next[0] == 4 && next[4] == 5 && next[5] == 6 && next[6] == -1,
	    "target-facing index retains only the authenticated water branch");
}

int main(void)
{
	rune_seed_t seeds[6];
	rune_link_t links[6];
	int next[6], dist[6], incoming[6], next_incoming[6], queue[6];
	vec3_t origin = { 62.0f, 0.0f, -32.0f };
	vec3_t final = { 0.0f, 0.0f, 96.0f };
	vec3_t far_final = { 192.0f, 0.0f, 16.0f };
	vec3_t target;
	vec3_t velocity = { 0.0f, 0.0f, 0.0f };
	int route_seed = -1;
	int reachable_calls = 0;
	qboolean exact_capture = false;

	TestTargetFacingWaterBranch();

	Seed(&seeds[0], 0.0f, 0.0f, 96.0f, 0);
	Seed(&seeds[1], 0.0f, 0.0f, -32.0f, RSF_WATER);
	Seed(&seeds[2], 64.0f, 0.0f, -32.0f, RSF_WATER);
	Seed(&seeds[3], 128.0f, 0.0f, -16.0f, RSF_WATER);
	Seed(&seeds[4], 192.0f, 0.0f, 16.0f, 0);
	Seed(&seeds[5], 64.0f, 32.0f, -32.0f, RSF_WATER);
	Link(&links[0], 1, 2, RL_SWIM);
	Link(&links[1], 2, 3, RL_SWIM);
	Link(&links[2], 3, 4, RL_SWIM);
	Link(&links[3], 5, 4, RL_RUN);
	Link(&links[4], 5, 0, RL_SWIM);
	Link(&links[5], 1, 0, RL_RUN);

	Check(SG_WaterEscapeIndexBuild(seeds, 6, links, 6, next, dist,
	          incoming, next_incoming, queue), "build proved swim index");
	Check(next[1] == 2 && next[2] == 3 && next[3] == 4,
	    "multi-hop path follows proved swim links");
	Check(next[5] == 0, "run edge ignored but proved swim retained");
	origin[0] = 62.0f;
	Check(SG_TimedVaultEgressSourceSelect(seeds, 6, next, origin, final,
	          AcceptAll, NULL) == 5,
	    "target-facing authenticated escape beats nearer wrong shoreline");
	origin[0] = 170.0f;
	Check(SG_TimedVaultEgressSourceSelect(seeds, 6, next, origin, far_final,
	          RejectNearSide, &reachable_calls) == 2 && reachable_calls > 1,
	    "blocked nearest source yields to a reachable proved source");
	Check(SG_TimedVaultEgressSourceSelect(seeds, 6, next, origin, final,
	          RejectAll, NULL) == -1,
	    "no physically reachable source fails closed");
	origin[0] = 62.0f;
	Check(SG_TimedVaultEgressTarget(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final, target),
	    "submerged timed vault has bounded graph waypoint");
	Check(target[0] == 128.0f && target[1] == 0.0f && target[2] == -16.0f,
	    "nearest water seed advances to its air hop");
	origin[0] = 170.0f;
	route_seed = -1;
	Check(SG_TimedVaultEgressAdvance(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final,
	          &route_seed, target) && route_seed < -1 && target[0] == 128.0f,
	    "off-seed localization reaches the proved link source first");
	origin[0] = 100.0f;
	Check(SG_TimedVaultEgressAdvance(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final,
	          &route_seed, target) && route_seed < -1 && target[0] == 128.0f,
	    "ordinary hop radius cannot authorize an inexact link source");
	origin[0] = 127.0f;
	origin[2] = -16.0f;
	Check(SG_TimedVaultEgressAdvance(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final,
	          &route_seed, target) && route_seed == 3 && target[0] == 192.0f,
	    "exact source arrival begins the authenticated outgoing link");
	origin[0] = 62.0f;
	origin[2] = -32.0f;
	route_seed = -1;
	Check(SG_TimedVaultEgressAdvance(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final,
	          &route_seed, target) && route_seed == 2 && target[0] == 128.0f,
	    "cursor selects one authenticated outgoing hop");
	origin[0] = 30.0f;
	Check(SG_TimedVaultEgressAdvance(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final,
	          &route_seed, target) && route_seed == 2 && target[0] == 128.0f,
	    "nearer prior seed cannot reverse retained hop");
	origin[0] = 127.0f;
	Check(SG_TimedVaultEgressAdvance(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final,
	          &route_seed, target) && route_seed == 3 && target[0] == 192.0f,
	    "proved swim hop advances at the ordinary replay arrival radius");
	origin[2] = -16.0f;
	Check(SG_TimedVaultEgressAdvance(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final,
	          &route_seed, target) && route_seed == 3 && target[0] == 192.0f,
	    "dry shoreline remains the next authenticated destination");
	route_seed = 2;
	velocity[1] = 150.0f;
	Check(SG_TimedVaultEgressAdvancePose(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, velocity, final,
	          &route_seed, &exact_capture, target) && route_seed == 3 &&
	      !exact_capture && target[0] == 192.0f,
	    "authenticated hop arrival does not invent a velocity-settle tax");
	VectorClear(velocity);
	Check(SG_TimedVaultEgressAdvancePose(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, velocity, final,
	          &route_seed, &exact_capture, target) && route_seed == 3 &&
	      !exact_capture && target[0] == 192.0f,
	    "settled exact hop advances to dry shoreline handoff");
	Check(SG_TimedVaultEgressAdvance(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 1, origin, final,
	          &route_seed, target) && route_seed == -1 &&
	      target[2] == final[2], "safe shoreline hands back to exact endpoint");
	route_seed = 2;
	next[2] = 2;
	Check(!SG_TimedVaultEgressAdvance(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final,
	          &route_seed, target), "self-loop route fails closed");
	next[2] = 3;
	Check(SG_TimedVaultEgressTarget(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 1, origin, final, target) &&
	      target[0] == final[0] && target[1] == final[1] &&
	      target[2] == final[2], "surfaced timed vault resumes exact endpoint");
	Check(SG_TimedVaultEgressTarget(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_BUTTON_DOOR, 2, origin, final, target) &&
	      target[0] == final[0] && target[1] == final[1] &&
	      target[2] == final[2], "ordinary button path is unchanged");
	Check(SG_TimedVaultEgressBudgetMs(
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT) == 9000,
	    "timed vault uses its full post-readiness lease");
	Check(SG_TimedVaultEgressBudgetMs(
	          SG_MECHANISM_CONTROLLER_BUTTON_DOOR) == 5000,
	    "ordinary button keeps the stock declared egress budget");
	origin[0] = 1000.0f;
	Check(!SG_TimedVaultEgressTarget(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final, target),
	    "submerged body outside proved graph fails closed");
	seeds[2].flags |= RSF_TOMBSTONE;
	next[1] = next[3] = next[5] = -1;
	origin[0] = 62.0f;
	Check(!SG_TimedVaultEgressTarget(seeds, 6, next,
	          SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, origin, final, target),
	    "tombstoned nearest route is not executable");

	if (failures)
		return 1;
	puts("timed-vault egress tests: ok");
	return 0;
}
