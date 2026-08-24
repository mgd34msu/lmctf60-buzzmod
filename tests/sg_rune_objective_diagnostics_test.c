/* Focused RUNE graph diagnostics: compiled directly with function-section GC.
 * The production generator remains untouched by this harness; it supplies the
 * host boundary needed by the static graph helpers below. */
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"

static qboolean TestObjectiveDropProver(int from, int to, vec3_t lip_out,
	short *cost_ms, byte *exit_speed);
static qboolean TestObjectiveHookProver(int from, int to, vec3_t control_out,
	short *cost_ms, byte *exit_speed);
static qboolean TestDeclaredApproachProver(const vec3_t source,
	const vec3_t target, edict_t *entry, edict_t *support, int action,
	int *arrival_ms);
static qboolean TestButtonLiftApproachProver(const vec3_t source,
	const vec3_t target, edict_t *button, edict_t *platform,
	int *arrival_ms);
#define SG_RUNE_OBJECTIVE_DROP_PROVER TestObjectiveDropProver
#define SG_RUNE_OBJECTIVE_HOOK_PROVER TestObjectiveHookProver
#include "slipgate/sg_rune.c"

static void SetLink(rune_link_t *link, int from, int to);

sg_host_t sg_host;
game_export_t globals;
edict_t *g_edicts;
edict_t *redflag;
edict_t *blueflag;

void SG_OraclePlace(sg_phantom_t *phantom, vec3_t origin)
{
	memset(phantom, 0, sizeof(*phantom));
	VectorCopy(origin, phantom->origin);
}

qboolean SG_OracleRunWorld(sg_phantom_t *phantom, usercmd_t *command,
	int steps)
{
	(void)phantom;
	(void)command;
	(void)steps;
	return false;
}

float P_FallDelta(float old_velocity_z, float velocity_z,
	qboolean grounded, int waterlevel)
{
	(void)old_velocity_z;
	(void)velocity_z;
	(void)grounded;
	(void)waterlevel;
	return 0.0f;
}

void VectorMA(vec3_t first, float scale, vec3_t second, vec3_t result)
{
	result[0] = first[0] + scale * second[0];
	result[1] = first[1] + scale * second[1];
	result[2] = first[2] + scale * second[2];
}

int SG_RuneProofObjectiveRunReplayAccepted(int edge_seek, int airborne)
{
	return !edge_seek && !airborne;
}

static char diagnostic_log[32768];
static size_t diagnostic_used;
static FILE *telemetry_output;
static int telemetry_flush_calls;
static int allocations_before_failure = -1;
static int failures;
static qboolean objective_drop_succeeds;
static int objective_drop_calls;
static int objective_drop_from;
static int objective_drop_to;
static qboolean objective_hook_succeeds;
static int objective_hook_calls;
static edict_t *declared_expected_entry;
static qboolean declared_approach_succeeds;
static int declared_approach_calls;
static int declared_approach_action;
static int mechanism_chord_calls;
static qboolean mechanism_chord_blocked;
static qboolean timed_vault_discover;
static qboolean relay_wall_discover;

int SG_RelayWallPlanDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, sg_relay_wall_plan_witness_t *witness_out)
{
	if (!relay_wall_discover || !catalog || catalog->num_nodes != 4U ||
	    entry_key != 1U || !witness_out)
		return 0;
	memset(witness_out, 0, sizeof(*witness_out));
	witness_out->entry_key = 1U;
	witness_out->wall_key = 2U;
	witness_out->immediate_relay_key = 3U;
	witness_out->restore_relay_key = 4U;
	witness_out->touch_hold_ms = 200U;
	witness_out->cooldown_ms = 4000U;
	witness_out->active_window_ms = 4000U;
	witness_out->restore_ms = 4000U;
	return 1;
}

int SG_TimedVaultPlanDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, sg_timed_vault_plan_witness_t *witness_out)
{
	if (!timed_vault_discover || !catalog || catalog->num_nodes != 4U ||
	    entry_key != 1U || !witness_out)
		return 0;
	memset(witness_out, 0, sizeof(*witness_out));
	witness_out->entry_key = 1U;
	witness_out->mover_key = 2U;
	witness_out->short_relay_key = 3U;
	witness_out->restore_relay_key = 4U;
	witness_out->touch_hold_ms = 200U;
	witness_out->readiness_ms = 1000U;
	witness_out->usable_window_ms = 9000U;
	witness_out->restore_ms = 10000U;
	return 1;
}

int SG_ButtonMechanismPlanBindingDiscover(
	const sg_mech_catalog_view_t *catalog, uint32_t entry_key,
	sg_mechanism_plan_binding_t *binding_out)
{
	sg_relay_wall_plan_witness_t relay_wall;
	sg_timed_vault_plan_witness_t timed_vault;

	if (!binding_out)
		return 0;
	memset(binding_out, 0, sizeof(*binding_out));
	if (SG_RelayWallPlanDiscover(catalog, entry_key, &relay_wall))
	{
		binding_out->entry_key = relay_wall.entry_key;
		binding_out->mover_key = relay_wall.wall_key;
		binding_out->destination_key = relay_wall.immediate_relay_key;
		binding_out->egress_key = relay_wall.restore_relay_key;
		binding_out->controller_kind = SG_MECHANISM_CONTROLLER_RELAY_DOOR;
		binding_out->expected_members = 1U;
		binding_out->cooldown_ms = relay_wall.cooldown_ms;
		return 1;
	}
	if (!SG_TimedVaultPlanDiscover(catalog, entry_key, &timed_vault))
		return 0;
	binding_out->entry_key = timed_vault.entry_key;
	binding_out->mover_key = timed_vault.mover_key;
	binding_out->destination_key = timed_vault.short_relay_key;
	binding_out->egress_key = timed_vault.restore_relay_key;
	binding_out->controller_kind = SG_MECHANISM_CONTROLLER_TIMED_VAULT;
	binding_out->expected_members = 2U;
	binding_out->cooldown_ms = timed_vault.restore_ms;
	return 1;
}

int SG_ButtonDoorPlanBindingDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, sg_mechanism_plan_binding_t *binding_out)
{
	return SG_ButtonMechanismPlanBindingDiscover(catalog, entry_key,
		binding_out);
}

qboolean SG_OracleDeclaredApproach(const vec3_t source, const vec3_t target,
	edict_t *entry, edict_t *support, int action, int *arrival_ms)
{
	return TestDeclaredApproachProver(source, target, entry, support, action,
	    arrival_ms);
}

qboolean SG_OracleButtonLiftApproach(const vec3_t source,
	const vec3_t target, edict_t *button, edict_t *platform, int *arrival_ms)
{
	return TestButtonLiftApproachProver(source, target, button, platform,
	    arrival_ms);
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
		        #expression); \
		failures++; \
	} \
} while (0)

static void TestDprint(const char *format, ...)
{
	va_list arguments;
	int wrote;

	va_start(arguments, format);
	if (telemetry_output)
	{
		va_list output_arguments;

		va_copy(output_arguments, arguments);
		vfprintf(telemetry_output, format, output_arguments);
		va_end(output_arguments);
	}
	if (diagnostic_used < sizeof(diagnostic_log))
		wrote = vsnprintf(diagnostic_log + diagnostic_used,
		    sizeof(diagnostic_log) - diagnostic_used, format, arguments);
	else
		wrote = 0;
	va_end(arguments);
	if (wrote > 0)
	{
		size_t amount = (size_t)wrote;

		diagnostic_used += amount < sizeof(diagnostic_log) - diagnostic_used
		    ? amount : sizeof(diagnostic_log) - diagnostic_used - 1U;
	}
}

static void TestFlush(void)
{
	telemetry_flush_calls++;
	if (telemetry_output)
		fflush(telemetry_output);
}

static void *TestAlloc(int size)
{
	if (allocations_before_failure == 0)
		return NULL;
	if (allocations_before_failure > 0)
		allocations_before_failure--;
	return calloc(1U, size > 0 ? (size_t)size : 1U);
}

static void TestFree(void *memory)
{
	free(memory);
}

static qboolean TestObjectiveDropProver(int from, int to, vec3_t lip_out,
	short *cost_ms, byte *exit_speed)
{
	objective_drop_calls++;
	objective_drop_from = from;
	objective_drop_to = to;
	if (!objective_drop_succeeds)
		return false;
	VectorSet(lip_out, 10.0f, 20.0f, 30.0f);
	*cost_ms = 1100;
	*exit_speed = 74;
	return true;
}

static qboolean TestObjectiveHookProver(int from, int to, vec3_t control_out,
	short *cost_ms, byte *exit_speed)
{
	(void)from;
	(void)to;
	objective_hook_calls++;
	if (!objective_hook_succeeds)
		return false;
	VectorSet(control_out, 10.0f, 90.0f, 30.0f);
	*cost_ms = 1200;
	*exit_speed = 75;
	return true;
}

static qboolean TestDeclaredApproachProver(const vec3_t source,
	const vec3_t target, edict_t *entry, edict_t *support, int action,
	int *arrival_ms)
{
	(void)source;
	(void)target;
	(void)support;
	declared_approach_calls++;
	declared_approach_action = action;
	if (!declared_approach_succeeds || entry != declared_expected_entry)
		return false;
	*arrival_ms = 25;
	return true;
}

static qboolean TestButtonLiftApproachProver(const vec3_t source,
	const vec3_t target, edict_t *button, edict_t *platform,
	int *arrival_ms)
{
	return TestDeclaredApproachProver(source, target, button, platform,
	    RL_LIFT, arrival_ms);
}

static trace_t TestTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int mask)
{
	trace_t result;

	(void)start;
	(void)mins;
	(void)maxs;
	(void)end;
	(void)passent;
	(void)mask;
	memset(&result, 0, sizeof(result));
	result.fraction = 1.0f;
	mechanism_chord_calls++;
	if (mechanism_chord_blocked)
		result.fraction = 0.343f;
	return result;
}

qboolean SG_OracleRotatorSweepBlocks(const vec3_t start,
	const vec3_t hull_mins, const vec3_t hull_maxs, const vec3_t end,
	int contentmask)
{
	(void)start;
	(void)hull_mins;
	(void)hull_maxs;
	(void)end;
	(void)contentmask;
	return false;
}

qboolean SG_OracleDoorEgressWaterSafe(int controller_kind, int waterlevel,
	int watertype)
{
	if (controller_kind ==
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR)
		return waterlevel >= 0 && waterlevel <= 1 &&
		    !(watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) &&
		    (waterlevel == 0 || (watertype & CONTENTS_WATER));
	return waterlevel == 0 &&
	    !(watertype & (CONTENTS_LAVA | CONTENTS_SLIME));
}

static void ResetGraph(rune_seed_t *seeds, int seed_count,
	rune_link_t *links, int link_count, edict_t *red, edict_t *blue)
{
	int i;

	memset(seeds, 0, sizeof(*seeds) * (size_t)seed_count);
	memset(links, 0, sizeof(*links) * (size_t)link_count);
	for (i = 0; i < link_count; i++)
		links[i].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	gen_seeds = seeds;
	gen_num_seeds = seed_count;
	gen_links = links;
	gen_num_links = link_count;
	gen_mechanism_bindings = NULL;
	gen_num_mechanism_bindings = 0U;
	gen_mechanism_failed = false;
	memset(&gen_telemetry, 0, sizeof(gen_telemetry));
	memset(&gen_phase_telemetry, 0, sizeof(gen_phase_telemetry));
	allocations_before_failure = -1;
	objective_drop_succeeds = false;
	objective_drop_calls = 0;
	objective_drop_from = -1;
	objective_drop_to = -1;
	objective_hook_succeeds = false;
	objective_hook_calls = 0;
	gen_env_drop = 0;
	gen_env_hook = 0;
	declared_expected_entry = NULL;
	declared_approach_succeeds = false;
	declared_approach_calls = 0;
	declared_approach_action = -1;
	mechanism_chord_calls = 0;
	mechanism_chord_blocked = false;
	telemetry_flush_calls = 0;
	memset(red, 0, sizeof(*red));
	memset(blue, 0, sizeof(*blue));
	red->inuse = true;
	blue->inuse = true;
	redflag = red;
	blueflag = blue;
	diagnostic_used = 0U;
	diagnostic_log[0] = '\0';
}

static void TestTeleporterApproachOwnsNonlinearStaging(void)
{
	rune_seed_t seeds[2];
	rune_link_t links[1];
	edict_t red, blue, pad, owned_trigger, alternate_trigger;
	vec3_t body = { 240.0f, 0.0f, 0.0f };
	int arrival_ms = 0;

	ResetGraph(seeds, 2, links, 1, &red, &blue);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, -64.0f, 0.0f, 0.0f);
	SetLink(&links[0], 1, 0);
	gen_source_stable[0] = true;
	gen_source_waterlevel[0] = 0;
	memset(&pad, 0, sizeof(pad));
	memset(&owned_trigger, 0, sizeof(owned_trigger));
	memset(&alternate_trigger, 0, sizeof(alternate_trigger));
	pad.inuse = true;
	owned_trigger.inuse = true;
	alternate_trigger.inuse = true;
	g_edicts = &pad;
	globals.num_edicts = 1;
	declared_expected_entry = &owned_trigger;
	declared_approach_succeeds = true;
	mechanism_chord_blocked = true;

	CHECK(Gen_MechanismSeedNear(body, 256.0f, 16.0f, &owned_trigger,
	    &pad, true, true, true, false, RL_TELEPORT, &arrival_ms) == 0);
	CHECK(arrival_ms == 25);
	CHECK(declared_approach_calls == 1);
	CHECK(declared_approach_action == RL_TELEPORT);
	CHECK(mechanism_chord_calls == 0);

	arrival_ms = 0;
	CHECK(Gen_MechanismSeedNear(body, 256.0f, 16.0f,
	    &alternate_trigger, &pad, true, true, true, false, RL_TELEPORT,
	    &arrival_ms) == -1);
	CHECK(declared_approach_calls == 2);
	CHECK(arrival_ms == 0);
	CHECK(mechanism_chord_calls == 0);

	CHECK(Gen_MechanismSeedNear(body, 256.0f, 16.0f, &owned_trigger,
	    &pad, true, true, true, false, RL_LIFT, &arrival_ms) == -1);
	CHECK(declared_approach_calls == 2);
	CHECK(mechanism_chord_calls == 1);
}

static void SetLink(rune_link_t *link, int from, int to)
{
	link->from = from;
	link->to = to;
	link->action = RL_RUN;
	link->provenance = RL_PROVEN;
	link->cost_ms = 100;
	link->mechanism_plan = RUNE_NO_MECHANISM_PLAN;
}

static void TestBidirectionalCore(void)
{
	rune_seed_t seeds[2];
	rune_link_t links[2];
	rune_seed_t seed_snapshot[2];
	rune_link_t link_snapshot[2];
	edict_t red, blue;

	ResetGraph(seeds, 2, links, 2, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 100.0f, 0.0f, 0.0f);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 100.0f, 0.0f, 0.0f);
	SetLink(&links[0], 0, 1);
	SetLink(&links[1], 1, 0);
	memcpy(seed_snapshot, seeds, sizeof(seeds));
	memcpy(link_snapshot, links, sizeof(links));
	CHECK(Graph_PruneObjectiveCore());
	CHECK(strstr(diagnostic_log, "red_to_blue=1 blue_to_red=1") != NULL);
	CHECK(strstr(diagnostic_log,
	    "objective-partitions red_only=0 blue_only=0 shared=2 neither=0") != NULL);
	CHECK(strstr(diagnostic_log,
	    "objective-boundary phase=initial reported=0 limit=16") != NULL);
	CHECK(gen_num_seeds == 2 && gen_num_links == 2);
	CHECK(memcmp(seeds, seed_snapshot, sizeof(seeds)) == 0);
	CHECK(memcmp(links, link_snapshot, sizeof(links)) == 0);
}

static void TestDisconnectedPartitionDiagnostics(void)
{
	rune_seed_t seeds[4];
	rune_link_t links[4];
	rune_seed_t seed_snapshot[4];
	rune_link_t link_snapshot[4];
	edict_t red, blue;

	ResetGraph(seeds, 4, links, 4, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 1000.0f, 0.0f, 0.0f);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 1000.0f, 0.0f, 0.0f);
	VectorSet(seeds[2].origin, 400.0f, 0.0f, 0.0f);
	VectorSet(seeds[3].origin, 500.0f, 0.0f, 0.0f);
	SetLink(&links[0], 0, 2);
	SetLink(&links[1], 2, 0);
	SetLink(&links[2], 1, 3);
	SetLink(&links[3], 3, 1);
	memcpy(seed_snapshot, seeds, sizeof(seeds));
	memcpy(link_snapshot, links, sizeof(links));
	CHECK(!Graph_PruneObjectiveCore());
	CHECK(strstr(diagnostic_log,
	    "objective-partitions red_only=2 blue_only=2 shared=0 neither=0") != NULL);
	CHECK(strstr(diagnostic_log,
	    "objective-gap pair=red-blue from=2") != NULL);
	CHECK(strstr(diagnostic_log, "to=3") != NULL);
	CHECK(strstr(diagnostic_log, "distance=100.000 exact=1 pairs=4") != NULL);
	CHECK(strstr(diagnostic_log,
	    "objective-boundary phase=initial reported=0 limit=16") != NULL);
	CHECK(gen_num_seeds == 4 && gen_num_links == 4);
	CHECK(memcmp(seeds, seed_snapshot, sizeof(seeds)) == 0);
	CHECK(memcmp(links, link_snapshot, sizeof(links)) == 0);
}

static void TestOneWayAndFixedPointFailures(void)
{
	rune_seed_t seeds[7];
	rune_link_t links[6];
	edict_t red, blue;

	ResetGraph(seeds, 7, links, 6, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 1000.0f, 0.0f, 0.0f);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 1000.0f, 0.0f, 0.0f);
	VectorSet(seeds[2].origin, 2000.0f, 0.0f, 0.0f);
	VectorSet(seeds[3].origin, 2100.0f, 0.0f, 0.0f);
	VectorSet(seeds[4].origin, 2200.0f, 0.0f, 0.0f);
	VectorSet(seeds[5].origin, 2300.0f, 0.0f, 0.0f);
	VectorSet(seeds[6].origin, 2400.0f, 0.0f, 0.0f);
	SetLink(&links[0], 0, 5);
	SetLink(&links[1], 1, 6);
	SetLink(&links[2], 2, 0);
	SetLink(&links[3], 3, 1);
	SetLink(&links[4], 4, 2);
	SetLink(&links[5], 4, 3);
	CHECK(!Graph_PruneObjectiveCore());
	CHECK(strstr(diagnostic_log, "red_to_blue=0 blue_to_red=0") != NULL);
	CHECK(strstr(diagnostic_log, "iteration=0 removed=6") != NULL);
	CHECK(strstr(diagnostic_log, "iteration=1 removed=1") != NULL);
	CHECK(strstr(diagnostic_log,
	    "objective-boundary phase=initial reported=") != NULL);
}

static void TestOneWayCrossRootReject(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[2];
	edict_t red, blue;

	ResetGraph(seeds, 3, links, 2, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 100.0f, 0.0f, 0.0f);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 100.0f, 0.0f, 0.0f);
	VectorSet(seeds[2].origin, 200.0f, 0.0f, 0.0f);
	SetLink(&links[0], 0, 1);
	SetLink(&links[1], 1, 2);
	CHECK(!Graph_PruneObjectiveCore());
	CHECK(strstr(diagnostic_log,
	    "objective-core initial red_reach=1 blue_reach=2") != NULL);
	CHECK(strstr(diagnostic_log,
	    "FAILED: flag objectives share no closed route core") != NULL);
}

static void TestHookReverseDropRepair(void)
{
	rune_seed_t seeds[4];
	rune_link_t links[6];
	edict_t red, blue;

	ResetGraph(seeds, 4, links, 5, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 1000.0f, 0.0f, 240.0f);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 1000.0f, 0.0f, 240.0f);
	VectorSet(seeds[2].origin, 64.0f, 0.0f, 0.0f);
	VectorSet(seeds[3].origin, 636.0f, 0.0f, 240.0f);
	SetLink(&links[0], 0, 2);
	SetLink(&links[1], 2, 0);
	SetLink(&links[2], 1, 3);
	SetLink(&links[3], 3, 1);
	SetLink(&links[4], 2, 3);
	links[4].action = RL_HOOK;
	objective_drop_succeeds = true;
	CHECK(Graph_PruneObjectiveCore());
	CHECK(objective_drop_calls == 1);
	CHECK(objective_drop_from == 3 && objective_drop_to == 2);
	CHECK(gen_num_links == 6);
	CHECK(links[5].from == 3 && links[5].to == 2);
	CHECK(links[5].action == RL_DROP);
	CHECK(links[5].cost_ms == 1100 && links[5].exit_speed == 74);
	CHECK(strstr(diagnostic_log,
	    "objective-repair kind=hook-reverse-drop") != NULL);
	CHECK(strstr(diagnostic_log,
	    "red_to_blue=1 blue_to_red=1") != NULL);
}

static void TestObjectiveReverseHookRepair(void)
{
	rune_seed_t seeds[4];
	rune_link_t links[6];
	edict_t red, blue;

	ResetGraph(seeds, 4, links, 5, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 1000.0f, 0.0f, 0.0f);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 1000.0f, 0.0f, 0.0f);
	VectorSet(seeds[2].origin, 64.0f, 0.0f, 0.0f);
	VectorSet(seeds[3].origin, 636.0f, 0.0f, 0.0f);
	SetLink(&links[0], 0, 2);
	SetLink(&links[1], 2, 0);
	SetLink(&links[2], 1, 3);
	SetLink(&links[3], 3, 1);
	SetLink(&links[4], 2, 3);
	links[4].action = RL_DROP;
	objective_hook_succeeds = true;
	CHECK(Graph_PruneObjectiveCore());
	CHECK(objective_hook_calls == 1);
	CHECK(gen_num_links == 6);
	CHECK(links[5].from == 3 && links[5].to == 2);
	CHECK(links[5].action == RL_HOOK);
	CHECK(links[5].cost_ms == 1200 && links[5].exit_speed == 75);
	CHECK(links[5].anchor[0] == 10.0f && links[5].anchor[1] == 90.0f &&
	    links[5].anchor[2] == 30.0f);
	CHECK(links[5].heading == 64);
	CHECK(links[5].heading_slack == RUNE_HOOK_CONTROL_SLACK);
	CHECK(links[5].min_speed == 0 && gen_env_hook == 1);
	CHECK(strstr(diagnostic_log, "objective-repair kind=reverse-hook") != NULL);
}

static void TestObjectiveReverseHookRepairFromRunBoundary(void)
{
	rune_seed_t seeds[4];
	rune_link_t links[6];
	edict_t red, blue;

	ResetGraph(seeds, 4, links, 5, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 1000.0f, 0.0f, 0.0f);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 1000.0f, 0.0f, 0.0f);
	VectorSet(seeds[2].origin, 64.0f, 0.0f, 0.0f);
	VectorSet(seeds[3].origin, 636.0f, 0.0f, 0.0f);
	SetLink(&links[0], 0, 2);
	SetLink(&links[1], 2, 0);
	SetLink(&links[2], 1, 3);
	SetLink(&links[3], 3, 1);
	SetLink(&links[4], 2, 3);
	objective_hook_succeeds = true;
	CHECK(Graph_PruneObjectiveCore());
	CHECK(objective_hook_calls == 1);
	CHECK(gen_num_links == 6);
	CHECK(links[5].from == 3 && links[5].to == 2);
	CHECK(links[5].action == RL_HOOK);
}

static void TestObjectiveReverseRejectsFailedHook(void)
{
	rune_seed_t seeds[4];
	rune_link_t links[6];
	edict_t red, blue;

	ResetGraph(seeds, 4, links, 5, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 1000.0f, 0.0f, 0.0f);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 1000.0f, 0.0f, 0.0f);
	VectorSet(seeds[2].origin, 64.0f, 0.0f, 0.0f);
	VectorSet(seeds[3].origin, 636.0f, 0.0f, 0.0f);
	SetLink(&links[0], 0, 2);
	SetLink(&links[1], 2, 0);
	SetLink(&links[2], 1, 3);
	SetLink(&links[3], 3, 1);
	SetLink(&links[4], 2, 3);
	links[4].action = RL_DROP;
	CHECK(!Graph_PruneObjectiveCore());
	CHECK(objective_hook_calls == 1);
	CHECK(gen_num_links == 5);
}

static void TestObjectiveReverseHookProofIsBounded(void)
{
	rune_seed_t seeds[66];
	rune_link_t links[33];
	byte red_reach[66], blue_reach[66];
	edict_t red, blue;

	ResetGraph(seeds, 66, links, 33, &red, &blue);
	for (int pair = 0; pair < 33; pair++)
	{
		int from = pair * 2;
		int to = from + 1;

		VectorSet(seeds[from].origin, (float)(pair * 32), 0.0f, 0.0f);
		VectorSet(seeds[to].origin, (float)(pair * 32 + 16), 0.0f, 0.0f);
		SetLink(&links[pair], from, to);
		links[pair].action = RL_DROP;
		red_reach[from] = blue_reach[from] = 1U;
		red_reach[to] = 1U;
		blue_reach[to] = 0U;
	}
	CHECK(!Graph_ProveObjectiveReverse(red_reach, blue_reach, NULL, NULL));
	CHECK(objective_hook_calls == (int)SG_RUNE_REVERSE_BOUNDARY_CAP);
	CHECK(gen_num_links == 33);
}


static void TestObjectiveMetricUnits(void)
{
	rune_seed_t seeds[1];
	rune_link_t links[1];
	byte has_out[1] = { 1 };
	vec3_t objective = { 0.0f, 0.0f, 0.0f };
	graph_objective_diag_t diag;
	edict_t red, blue;

	ResetGraph(seeds, 1, links, 0, &red, &blue);
	VectorSet(seeds[0].origin, 96.0f, 0.0f, 0.0f);
	CHECK(Graph_ObjectiveRoot(objective, has_out, &diag) == 0);
	CHECK(fabsf(diag.score - 9216.0f) < 0.001f);
	CHECK(fabsf(diag.distance - 96.0f) < 0.001f);
	Graph_LogObjectiveRoot("red", &diag, 0);
	CHECK(strstr(diagnostic_log, "score=9216.000 distance=96.000") != NULL);

	ResetGraph(seeds, 1, links, 0, &red, &blue);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 80.0f);
	CHECK(Graph_ObjectiveRoot(objective, has_out, &diag) == 0);
	CHECK(fabsf(diag.score - 1600.0f) < 0.001f);
	CHECK(fabsf(diag.distance - 80.0f) < 0.001f);
	Graph_LogObjectiveRoot("blue", &diag, 0);
	CHECK(strstr(diagnostic_log, "score=1600.000 distance=80.000") != NULL);
}

static void TestNearestUnlinked(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[1];
	byte has_out[3] = { 0, 1, 0 };
	vec3_t objective = { 0.0f, 0.0f, 0.0f };
	graph_objective_diag_t diag;
	edict_t red, blue;

	ResetGraph(seeds, 3, links, 1, &red, &blue);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 100.0f, 0.0f, 0.0f);
	VectorSet(seeds[2].origin, 200.0f, 0.0f, 0.0f);
	SetLink(&links[0], 1, 2);
	CHECK(Graph_ObjectiveRoot(objective, has_out, &diag) == -1);
	CHECK(diag.nearest == 0);
	CHECK(!diag.has_out);
	CHECK(diag.no_out_rejects == 1U);
	Graph_LogObjectiveRoot("red", &diag, -1);
	CHECK(strstr(diagnostic_log, "nearest=0") != NULL);
	CHECK(strstr(diagnostic_log, "has_out=0") != NULL);
}

static void TestDirectShallowApproachEnvelope(void)
{
	/* Exact lmctf58 source/wait pairs for Red/Blue CellarDoor and
	 * CellarDoor2.  Each production approach replay succeeds, but the generic
	 * 48-unit discovery filter used to discard it before replay because the
	 * only wait sharing a proved shallow egress is 72 units lower. */
	static const struct
	{
		int entry;
		vec3_t source;
		vec3_t wait;
	} cellars[] = {
		{ 32,  { -2955.0f, 2164.0f, -339.875f },
		       { -3275.0f, 2164.0f, -411.875f } },
		{ 35,  { -2955.0f, 3351.0f, -339.875f },
		       { -3275.0f, 3351.0f, -411.875f } },
		{ 186, { 1351.0f, 2928.0f, -339.875f },
		       { 1671.0f, 2928.0f, -411.875f } },
		{ 189, { 1351.0f, 1741.0f, -339.875f },
		       { 1671.0f, 1741.0f, -411.875f } }
	};
	rune_seed_t seeds[2];
	rune_link_t links[1];
	edict_t red, blue;
	vec3_t delta;
	size_t i;

	ResetGraph(seeds, 2, links, 0, &red, &blue);
	gen_source_waterlevel[0] = 1;
	gen_source_watertype[0] = CONTENTS_WATER;
	gen_source_waterlevel[1] = 0;
	gen_source_watertype[1] = 0;
	for (i = 0; i < sizeof(cellars) / sizeof(cellars[0]); i++)
	{
		CHECK(cellars[i].entry > 0);
		VectorSubtract(cellars[i].wait, cellars[i].source, delta);
		CHECK(fabsf(delta[0]) == 320.0f);
		CHECK(delta[1] == 0.0f);
		CHECK(delta[2] == -72.0f);
		CHECK(Door_ApproachEnvelopeEligible(
		    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
		CHECK(!Door_ApproachEnvelopeEligible(
		    SG_MECHANISM_CONTROLLER_AUTO_DOOR, 0, delta));
		CHECK(!Door_ApproachEnvelopeEligible(
		    SG_MECHANISM_CONTROLLER_BUTTON_DOOR, 0, delta));
	}
	/* A shallow best slot can enable source discovery, but the final
	 * per-picked-slot gate must reject a dry alternate for the same 72-unit
	 * approach. Each selected destination authorizes only itself. */
	CHECK(Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 1, delta));

	/* The production helper must not bootstrap the larger discovery envelope
	 * from a dry, malformed, hazardous, or deep destination. */
	gen_source_waterlevel[0] = 0;
	gen_source_watertype[0] = 0;
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	gen_source_waterlevel[0] = 1;
	gen_source_watertype[0] = 0;
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	gen_source_watertype[0] = CONTENTS_WATER | CONTENTS_LAVA;
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	gen_source_watertype[0] = CONTENTS_WATER | CONTENTS_SLIME;
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	gen_source_waterlevel[0] = 2;
	gen_source_watertype[0] = CONTENTS_WATER;
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));

	/* Historical 48-unit DIRECT discovery remains available without water;
	 * neither path expands beyond the existing egress vertical budget. */
	gen_source_waterlevel[0] = 0;
	gen_source_watertype[0] = 0;
	delta[0] = 320.0f;
	delta[1] = 0.0f;
	delta[2] = -48.0f;
	CHECK(Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	gen_source_waterlevel[0] = 1;
	gen_source_watertype[0] = CONTENTS_WATER;
	delta[2] = -72.0f;
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, -1, delta));
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, gen_num_seeds, delta));
	delta[0] = 320.125f;
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	delta[0] = 320.0f;
	delta[2] = -96.0f;
	CHECK(Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	delta[2] = 96.0f;
	CHECK(Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	delta[2] = -96.125f;
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	delta[2] = 96.125f;
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, delta));
	CHECK(!Door_ApproachEnvelopeEligible(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 0, NULL));
}


static void TestBoundaryCap(void)
{
	rune_seed_t seeds[19];
	rune_link_t links[18];
	byte red_reach[19];
	byte blue_reach[19];
	edict_t red, blue;
	const char *previous = diagnostic_log;
	int i;

	ResetGraph(seeds, 19, links, 18, &red, &blue);
	memset(red_reach, 0, sizeof(red_reach));
	memset(blue_reach, 0, sizeof(blue_reach));
	for (i = 0; i < 19; i++)
		red_reach[i] = (byte)(i & 1);
	for (i = 0; i < 18; i++)
		SetLink(&links[i], i, i + 1);
	Graph_LogBoundaryLinks("test", red_reach, blue_reach);
	for (i = 0; i < 16; i++)
	{
		char expected[48];
		const char *found;

		snprintf(expected, sizeof(expected), "ordinal=%d link=%d", i, i);
		found = strstr(previous, expected);
		CHECK(found != NULL);
		if (found)
			previous = found + 1;
	}
	CHECK(strstr(diagnostic_log, "ordinal=16") == NULL);
	CHECK(strstr(diagnostic_log, "link=16") == NULL);
	CHECK(strstr(diagnostic_log,
	    "objective-boundary phase=test reported=16 limit=16") != NULL);
}

static void TestZeroAndAllocationFailures(void)
{
	rune_seed_t seeds[2];
	rune_link_t links[1];
	edict_t red, blue;

	ResetGraph(seeds, 0, links, 0, &red, &blue);
	CHECK(!Graph_PruneObjectiveCore());
	CHECK(strstr(diagnostic_log, "reason=no-seeds") != NULL);

	ResetGraph(seeds, 2, links, 0, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 100.0f, 0.0f, 0.0f);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 100.0f, 0.0f, 0.0f);
	CHECK(!Graph_PruneObjectiveCore());
	CHECK(strstr(diagnostic_log, "root=-1") != NULL);

	ResetGraph(seeds, 2, links, 1, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 100.0f, 0.0f, 0.0f);
	VectorSet(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	VectorSet(seeds[1].origin, 100.0f, 0.0f, 0.0f);
	SetLink(&links[0], 0, 1);
	allocations_before_failure = 3;
	CHECK(!Graph_PruneObjectiveCore());
	CHECK(strstr(diagnostic_log, "FAILED: objective-core allocation") != NULL);
	CHECK(gen_num_seeds == 2 && gen_num_links == 1);
	allocations_before_failure = -1;
}

static void TestLocalOnlyPublicationRetainsProvenUnion(void)
{
	rune_seed_t seeds[6];
	rune_link_t links[7];
	rune_link_t retained[6];
	edict_t red, blue;

	ResetGraph(seeds, 6, links, 7, &red, &blue);
	VectorSet(red.homeposition, 0.0f, 0.0f, 0.0f);
	VectorSet(blue.homeposition, 1000.0f, 0.0f, 0.0f);
	for (int i = 0; i < 6; i++)
		VectorSet(seeds[i].origin, (float)i * 100.0f, 0.0f, 0.0f);
	SetLink(&links[0], 0, 2);
	SetLink(&links[1], 2, 0);
	SetLink(&links[2], 1, 3);
	SetLink(&links[3], 3, 1);
	SetLink(&links[4], 4, 2);
	SetLink(&links[5], 2, 4);
	SetLink(&links[6], 5, 5);
	links[4].action = RL_HOOK;
	links[4].cost_ms = 777;
	memcpy(retained, links, sizeof(retained));

	CHECK(Graph_PruneLocalObjectiveUnion(0, 1));
	CHECK(gen_num_links == 6);
	CHECK(memcmp(links, retained, sizeof(retained)) == 0);
	CHECK((seeds[0].flags & RSF_OBJECTIVE) != 0);
	CHECK((seeds[1].flags & RSF_OBJECTIVE) != 0);
	CHECK((seeds[5].flags & RSF_TOMBSTONE) != 0);
	CHECK(strstr(diagnostic_log,
	    "route contract local-only red_reach=3 blue_reach=2 shared=0 retained=5") != NULL);
}

static void TestLocalOnlyPublicationRejectsClosedOrUnallocatableGraph(void)
{
	rune_seed_t seeds[2];
	rune_link_t links[2];
	rune_seed_t seed_snapshot[2];
	rune_link_t link_snapshot[2];
	edict_t red, blue;

	ResetGraph(seeds, 2, links, 2, &red, &blue);
	SetLink(&links[0], 0, 1);
	SetLink(&links[1], 1, 0);
	memcpy(seed_snapshot, seeds, sizeof(seeds));
	memcpy(link_snapshot, links, sizeof(links));
	CHECK(!Graph_PruneLocalObjectiveUnion(0, 1));
	CHECK(gen_num_links == 2);
	CHECK(memcmp(seeds, seed_snapshot, sizeof(seeds)) == 0);
	CHECK(memcmp(links, link_snapshot, sizeof(links)) == 0);

	ResetGraph(seeds, 2, links, 0, &red, &blue);
	memcpy(seed_snapshot, seeds, sizeof(seeds));
	allocations_before_failure = 0;
	CHECK(!Graph_PruneLocalObjectiveUnion(0, 1));
	CHECK(gen_num_links == 0);
	CHECK(memcmp(seeds, seed_snapshot, sizeof(seeds)) == 0);
	allocations_before_failure = -1;
}

static void TestTelemetryFlush(void)
{
	char buffering[1024];
	char observed[2048];
	FILE *output;
	size_t read_count;

	output = tmpfile();
	CHECK(output != NULL);
	if (!output)
		return;
	CHECK(setvbuf(output, buffering, _IOFBF, sizeof(buffering)) == 0);
	telemetry_output = output;
	diagnostic_used = 0U;
	diagnostic_log[0] = '\0';
	memset(&gen_telemetry, 0, sizeof(gen_telemetry));
	Rune_TelemetryAdd(&gen_telemetry.pair_scans, UINT32_MAX);
	Rune_TelemetryAdd(&gen_telemetry.pair_scans, 1U);
	Rune_TelemetryPhaseStart("test");
	CHECK(telemetry_flush_calls == 1);
	CHECK(fseek(output, 0L, SEEK_SET) == 0);
	read_count = fread(observed, 1U, sizeof(observed) - 1U, output);
	observed[read_count] = '\0';
	CHECK(strstr(observed, "event=phase-start phase=test") != NULL);
	CHECK(fseek(output, 0L, SEEK_END) == 0);
	Rune_TelemetryPhaseEnd();
	CHECK(telemetry_flush_calls == 2);
	CHECK(fseek(output, 0L, SEEK_SET) == 0);
	read_count = fread(observed, 1U, sizeof(observed) - 1U, output);
	observed[read_count] = '\0';
	CHECK(gen_telemetry.pair_scans == UINT32_MAX);
	CHECK(strstr(observed, "event=phase-end phase=test") != NULL);
	CHECK(strstr(observed, "pair_scans=4294967295") != NULL);
	telemetry_output = NULL;
	fclose(output);
}

static void TestRejectedMechanismCandidateDoesNotPoisonGraph(void)
{
	rune_seed_t seeds[2];
	rune_link_t links[1];
	edict_t red, blue;
	byte keep[2] = { 1, 1 };

	ResetGraph(seeds, 1, links, 0, &red, &blue);
	CHECK(!Mechanism_Bind(NULL, SG_MECH_NO_KEY, SG_MECH_NO_KEY,
	    SG_MECH_NO_KEY, SG_MECH_NO_KEY,
	    SG_MECHANISM_CONTROLLER_PLATFORM, 1U, 0U));
	CHECK(!gen_mechanism_failed);
	CHECK(!Mechanism_BindTrain(NULL, NULL, NULL, NULL, NULL, 0U,
	    SG_MECHANISM_CONTROLLER_TRAIN));
	CHECK(!gen_mechanism_failed);
	CHECK(!Mechanism_BindDoor(NULL, NULL));
	CHECK(!gen_mechanism_failed);

	ResetGraph(seeds, 2, links, 1, &red, &blue);
	SetLink(&links[0], 0, 1);
	links[0].mechanism_plan = 0U;
	CHECK(!Graph_ApplyObjectiveKeep(keep, "invalid binding"));
	CHECK(gen_mechanism_failed);
}

static void TestTimedVaultCandidatesBindFourDirectedPlans(void)
{
	static rune_mechanism_node_t nodes[4];
	static sg_mechanism_plan_binding_t bindings[4];
	rune_link_t links[4];
	static edict_t entities[5];
	uint32_t index;

	memset(nodes, 0, sizeof(nodes));
	memset(bindings, 0, sizeof(bindings));
	memset(links, 0, sizeof(links));
	memset(entities, 0, sizeof(entities));
	for (index = 0U; index < 4U; index++)
	{
		nodes[index].key = index + 1U;
		nodes[index].owner_key = SG_MECH_NO_KEY;
		nodes[index].team_master_key = SG_MECH_NO_KEY;
	}
	nodes[0].kind = SG_MECH_NODE_BUTTON;
	nodes[0].touch_callback = SG_MECH_CALLBACK_BUTTON_TOUCH;
	nodes[0].use_callback = SG_MECH_CALLBACK_BUTTON_USE;
	nodes[0].wait_ms = 10000;
	gen_mechanism_catalog.nodes = nodes;
	gen_mechanism_catalog.num_nodes = 4U;
	gen_mechanism_catalog.edges = NULL;
	gen_mechanism_catalog.num_edges = 0U;
	gen_mechanism_bindings = bindings;
	gen_num_mechanism_bindings = 0U;
	g_edicts = entities;
	globals.num_edicts = 5;
	timed_vault_discover = true;
	for (index = 0U; index < 4U; index++)
	{
		links[index].from = (uint16_t)index;
		links[index].to = (uint16_t)(index ^ 1U);
		links[index].action = RL_BUTTON_DOOR;
		links[index].mode = RLCM_PREOPEN;
		links[index].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
		CHECK(Mechanism_BindButtonDoor(&links[index], &entities[1]));
		CHECK(links[index].mechanism_plan == index);
		CHECK(bindings[index].entry_key == 1U);
		CHECK(bindings[index].mover_key == 2U);
		CHECK(bindings[index].destination_key == 3U);
		CHECK(bindings[index].egress_key == 4U);
		CHECK(bindings[index].controller_kind ==
		    SG_MECHANISM_CONTROLLER_TIMED_VAULT);
		CHECK(bindings[index].expected_members == 2U);
		CHECK(bindings[index].cooldown_ms == 10000U);
	}
	CHECK(gen_num_mechanism_bindings == 4U);
	timed_vault_discover = false;
}

static void TestRelayWallCandidatesBindAuthenticatedPlan(void)
{
	static rune_mechanism_node_t nodes[4];
	static sg_mechanism_plan_binding_t binding;
	rune_link_t link;
	static edict_t entities[5];

	memset(nodes, 0, sizeof(nodes));
	memset(&binding, 0, sizeof(binding));
	memset(&link, 0, sizeof(link));
	memset(entities, 0, sizeof(entities));
	for (uint32_t index = 0U; index < 4U; index++)
	{
		nodes[index].key = index + 1U;
		nodes[index].owner_key = SG_MECH_NO_KEY;
		nodes[index].team_master_key = SG_MECH_NO_KEY;
	}
	nodes[0].kind = SG_MECH_NODE_BUTTON;
	nodes[0].touch_callback = SG_MECH_CALLBACK_BUTTON_TOUCH;
	nodes[0].use_callback = SG_MECH_CALLBACK_BUTTON_USE;
	nodes[0].wait_ms = 4000;
	gen_mechanism_catalog.nodes = nodes;
	gen_mechanism_catalog.num_nodes = 4U;
	gen_mechanism_catalog.edges = NULL;
	gen_mechanism_catalog.num_edges = 0U;
	gen_mechanism_bindings = &binding;
	gen_num_mechanism_bindings = 0U;
	g_edicts = entities;
	globals.num_edicts = 5;
	link.from = 0U;
	link.to = 1U;
	link.action = RL_BUTTON_DOOR;
	link.mode = RLCM_PREOPEN;
	link.mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	relay_wall_discover = true;
	CHECK(Mechanism_BindButtonDoor(&link, &entities[1]));
	CHECK(link.mechanism_plan == 0U);
	CHECK(binding.entry_key == 1U);
	CHECK(binding.mover_key == 2U);
	CHECK(binding.destination_key == 3U);
	CHECK(binding.egress_key == 4U);
	CHECK(binding.controller_kind ==
	    SG_MECHANISM_CONTROLLER_RELAY_DOOR);
	CHECK(binding.expected_members == 1U);
	CHECK(binding.cooldown_ms == 4000U);
	relay_wall_discover = false;
}

int main(void)
{
	(void)ProveHook;
	sg_host.dprint = TestDprint;
	sg_host.flush = TestFlush;
	sg_host.trace = TestTrace;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	TestBidirectionalCore();
	TestDisconnectedPartitionDiagnostics();
	TestOneWayCrossRootReject();
	TestHookReverseDropRepair();
	TestObjectiveReverseHookRepair();
	TestObjectiveReverseHookRepairFromRunBoundary();
	TestObjectiveReverseRejectsFailedHook();
	TestObjectiveReverseHookProofIsBounded();
	TestOneWayAndFixedPointFailures();
	TestObjectiveMetricUnits();
	TestNearestUnlinked();
	TestDirectShallowApproachEnvelope();
	TestTeleporterApproachOwnsNonlinearStaging();
	TestBoundaryCap();
	TestZeroAndAllocationFailures();
	TestLocalOnlyPublicationRetainsProvenUnion();
	TestLocalOnlyPublicationRejectsClosedOrUnallocatableGraph();
	TestTelemetryFlush();
	TestRejectedMechanismCandidateDoesNotPoisonGraph();
	TestRelayWallCandidatesBindAuthenticatedPlan();
	TestTimedVaultCandidatesBindFourDirectedPlans();
	if (failures)
		return 1;
	puts("sg_rune_objective_diagnostics_test: ok");
	return 0;
}
