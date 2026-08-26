#include <math.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_rune_hook_frontier.h"
#include "slipgate/sg_rune_proof.h"

static int failures;
static edict_t entities[1];
edict_t *g_edicts = entities;
sg_host_t sg_host;
static vec3_t aim_origins[8];
static int aim_calls;
static int hook_calls;
static int chain_discover_calls;
static int chain_exact_calls;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void *FailAllocation(int bytes)
{
	(void)bytes;
	return NULL;
}

static void IgnoreDprint(const char *format, ...)
{
	(void)format;
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	(void)start;
	(void)bite;
	VectorClear(velocity);
	return 0;
}

int SG_RuneProofHookLateralWindow(float horizontal, float rise)
{
	(void)horizontal;
	(void)rise;
	return 0;
}

void SG_RuneProofHookFrontierCursorReset(
	sg_rune_proof_hook_frontier_cursor_t *cursor)
{
	if (cursor)
		memset(cursor, 0, sizeof(*cursor));
}

size_t SG_RuneProofSelectHookFrontier(
	const sg_rune_proof_hook_frontier_t *frontier)
{
	(void)frontier;
	return 0U;
}

static trace_t HostTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int mask)
{
	trace_t trace;
	vec3_t delta;
	float length;

	(void)mins;
	(void)maxs;
	(void)passent;
	(void)mask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	trace.ent = g_edicts;
	VectorSubtract(end, start, delta);
	length = VectorLength(delta);
	if (length > 100.0f)
	{
		VectorScale(delta, 1.0f / length, delta);
		trace.fraction = 512.0f / length;
		trace.endpos[0] = start[0] + 512.0f * delta[0];
		trace.endpos[1] = start[1] + 512.0f * delta[1];
		trace.endpos[2] = start[2] + 512.0f * delta[2];
	}
	return trace;
}

qboolean SG_HookAimAngles(const vec3_t origin, float viewheight,
	const vec3_t aim, vec3_t view_angles)
{
	vec3_t delta;
	float horizontal;
	int call = aim_calls++;

	(void)viewheight;
	VectorCopy(origin, aim_origins[call]);
	VectorSubtract(aim, origin, delta);
	horizontal = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
	view_angles[PITCH] = SHORT2ANGLE((short)ANGLE2SHORT(
		-atan2f(delta[2], horizontal) * 180.0f / (float)M_PI));
	view_angles[YAW] = SHORT2ANGLE((short)ANGLE2SHORT(
		atan2f(delta[1], delta[0]) * 180.0f / (float)M_PI));
	view_angles[ROLL] = 0.0f;
	return true;
}

void vectoangles(vec3_t value1, vec3_t angles)
{
	float yaw = atan2f(value1[1], value1[0]) * 180.0f / (float)M_PI;
	float forward = sqrtf(value1[0] * value1[0] +
		value1[1] * value1[1]);

	angles[PITCH] = -atan2f(value1[2], forward) * 180.0f / (float)M_PI;
	angles[YAW] = yaw;
	angles[ROLL] = 0.0f;
}

void CTF_HookMuzzle(const vec3_t origin, float viewheight, int hand,
	const vec3_t forward, const vec3_t right, vec3_t start)
{
	vec3_t offset = { 8.0f, 8.0f, 14.0f };

	(void)viewheight;
	(void)hand;
	start[0] = origin[0] + forward[0] * offset[0] + right[0] * offset[1];
	start[1] = origin[1] + forward[1] * offset[0] + right[1] * offset[1];
	start[2] = origin[2] + forward[2] * offset[0] + right[2] * offset[1] +
		offset[2];
}

void SG_OraclePlace(sg_phantom_t *phantom, vec3_t origin)
{
	memset(phantom, 0, sizeof(*phantom));
	VectorCopy(origin, phantom->origin);
	phantom->origin[0] += 8.0f;
}

qboolean SG_OracleRunWorld(sg_phantom_t *phantom, usercmd_t *command,
	int steps)
{
	(void)phantom;
	(void)command;
	(void)steps;
	return true;
}

qboolean SG_OracleHookFlightClear(const vec3_t muzzle, const vec3_t bite)
{
	(void)muzzle;
	(void)bite;
	return true;
}

qboolean SG_OracleHookTraverse(sg_phantom_t *phantom, const vec3_t bite,
	const vec3_t destination, const vec3_t view_angles, int hand,
	int flight_ms, int settle_limit_ms, float old_frame_z,
	sg_hook_proof_t *proof, edict_t *passent, qboolean world_only)
{
	(void)phantom;
	(void)bite;
	(void)destination;
	(void)view_angles;
	(void)hand;
	(void)flight_ms;
	(void)settle_limit_ms;
	(void)old_frame_z;
	(void)passent;
	(void)world_only;
	hook_calls++;
	memset(proof, 0, sizeof(*proof));
	proof->pull_ms = 300;
	proof->settle_ms = 200;
	proof->exit_speed = 7;
	return true;
}

qboolean SG_OracleHookTraverseMonitored(sg_phantom_t *phantom,
	const vec3_t bite, const vec3_t destination, const vec3_t view_angles,
	int hand, int flight_ms, int settle_limit_ms, float old_frame_z,
	sg_hook_proof_t *proof, edict_t *passent, qboolean world_only,
	sg_oracle_hook_monitor_fn monitor, void *monitor_context,
	qboolean fling_release, sg_hook_replay_terminal_t terminal)
{
	(void)bite;
	(void)destination;
	(void)view_angles;
	(void)hand;
	(void)flight_ms;
	(void)settle_limit_ms;
	(void)old_frame_z;
	(void)passent;
	(void)world_only;
	(void)monitor;
	(void)monitor_context;
	(void)fling_release;
	(void)terminal;
	memset(proof, 0, sizeof(*proof));
	VectorSet(phantom->origin, 400.0f, 300.0f, 200.0f);
	return true;
}

qboolean SG_OracleChainHookDiscover(sg_phantom_t *phantom,
	const vec3_t aim[SG_CHAIN_HOOK_ROPE_COUNT], const vec3_t destination,
	int hand, float old_frame_z,
	vec3_t control_out[SG_CHAIN_HOOK_ROPE_COUNT],
	sg_chain_hook_proof_t *proof, edict_t *passent, qboolean world_only)
{
	(void)phantom;
	(void)destination;
	(void)hand;
	(void)old_frame_z;
	(void)passent;
	(void)world_only;
	chain_discover_calls++;
	VectorCopy(aim[0], control_out[0]);
	VectorCopy(aim[1], control_out[1]);
	control_out[0][ROLL] = 512.0f;
	control_out[1][ROLL] = 384.0f;
	memset(proof, 0, sizeof(*proof));
	proof->total_ms = 1400;
	proof->exit_speed = 9;
	return true;
}

qboolean SG_OracleChainHookTraverse(sg_phantom_t *phantom,
	const vec3_t control[SG_CHAIN_HOOK_ROPE_COUNT],
	const vec3_t destination, int hand, float old_frame_z,
	sg_chain_hook_proof_t *proof, edict_t *passent, qboolean world_only)
{
	(void)phantom;
	(void)control;
	(void)destination;
	(void)hand;
	(void)old_frame_z;
	(void)passent;
	(void)world_only;
	chain_exact_calls++;
	memset(proof, 0, sizeof(*proof));
	proof->total_ms = 1500;
	proof->exit_speed = 10;
	return true;
}

short SG_RuneProofGravity(void)
{
	return 100;
}

static sg_rune_hook_frontier_input_t Fixture(rune_seed_t seeds[2],
	byte stable[2], byte waterlevel[2], uint32_t *prover_calls)
{
	sg_rune_hook_frontier_input_t input;

	memset(&input, 0, sizeof(input));
	memset(seeds, 0, sizeof(*seeds) * 2U);
	seeds[1].origin[0] = 1000.0f;
	stable[0] = stable[1] = 1U;
	waterlevel[0] = waterlevel[1] = 0U;
	input.seeds = seeds;
	input.seed_count = 2;
	input.source_stable = stable;
	input.source_waterlevel = waterlevel;
	input.prover_calls = prover_calls;
	return input;
}

static void TestWorldBitesDriveFreshProofPoses(void)
{
	rune_seed_t seeds[2];
	byte stable[2], waterlevel[2];
	uint32_t prover_calls = 0U;
	sg_rune_hook_frontier_input_t input = Fixture(seeds, stable, waterlevel,
		&prover_calls);
	int32_t bites[2][3] = { { 6400, 800, 800 }, { 7200, -800, 1200 } };
	sg_rune_hook_nomination_proof_t proof;

	aim_calls = 0;
	CHECK(SG_RuneProveHookNomination(&input, 0, 1, 1U,
		(const int32_t (*)[3])bites, &proof));
	CHECK(proof.action == RL_HOOK &&
		fabsf(proof.control[0][ROLL] - 512.0f) < 0.01f);
	CHECK(proof.cost_ms > 0 && hook_calls == 1);
	CHECK(aim_calls == 2 && aim_origins[1][0] == 8.0f);
	CHECK(prover_calls == 1U);

	aim_calls = 0;
	CHECK(SG_RuneProveHookNomination(&input, 0, 1, 2U,
		(const int32_t (*)[3])bites, &proof));
	CHECK(proof.action == RL_CHAIN_HOOK && proof.cost_ms == 1400);
	CHECK(chain_discover_calls == 1);
	CHECK(aim_calls == 2);
	CHECK(aim_origins[1][0] == 400.0f &&
		aim_origins[1][1] == 300.0f && aim_origins[1][2] == 200.0f);
	CHECK(prover_calls == 2U);
}

static void TestSourceControlsUseExactOracle(void)
{
	rune_seed_t seeds[2];
	byte stable[2], waterlevel[2];
	uint32_t prover_calls = 0U;
	sg_rune_hook_frontier_input_t input = Fixture(seeds, stable, waterlevel,
		&prover_calls);
	vec3_t controls[2] = {
		{ 0.0f, 0.0f, 512.0f },
		{ 0.0f, 90.0f, 384.0f }
	};
	sg_rune_hook_nomination_proof_t proof;

	CHECK(SG_RuneReproveHookControl(&input, 0, 1, RL_HOOK,
		(const vec3_t *)controls, &proof));
	CHECK(proof.action == RL_HOOK && proof.control[0][ROLL] == 512.0f);
	CHECK(SG_RuneReproveHookControl(&input, 0, 1, RL_CHAIN_HOOK,
		(const vec3_t *)controls, &proof));
	CHECK(proof.action == RL_CHAIN_HOOK && proof.cost_ms == 1500);
	CHECK(chain_exact_calls == 1 && prover_calls == 2U);
}

static void TestFrontierAllocationFailureIsFatal(void)
{
	rune_seed_t seeds[2];
	rune_link_t links[1];
	byte stable[2], waterlevel[2], objective_mask[2] = { 3U, 3U };
	int component[2] = { 0, 0 };
	int link_count = 0, hook_envelopes = 0;
	uint32_t prover_calls = 0U;
	qboolean link_overflow = false;
	sg_rune_hook_frontier_input_t input = Fixture(seeds, stable, waterlevel,
		&prover_calls);

	input.component = component;
	input.objective_mask = objective_mask;
	input.component_count = 1;
	input.links = links;
	input.link_count = &link_count;
	input.link_overflow = &link_overflow;
	input.hook_envelope_count = &hook_envelopes;
	sg_host.level_alloc = FailAllocation;
	sg_host.dprint = IgnoreDprint;
	CHECK(!SG_RuneGenerateHookFrontier(&input));
	CHECK(link_count == 0 && !link_overflow && hook_envelopes == 0);
	sg_host.level_alloc = NULL;
}

int main(void)
{
	memset(&sg_host, 0, sizeof(sg_host));
	sg_host.trace = HostTrace;
	TestWorldBitesDriveFreshProofPoses();
	TestSourceControlsUseExactOracle();
	TestFrontierAllocationFailureIsFatal();
	if (failures)
	{
		fprintf(stderr, "sg_rune_hook_nomination_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_hook_nomination_test: ok");
	return 0;
}
