#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "tests/support/sg_bot_localization_fixture.h"
#include "slipgate/sg_compound_publication.h"
#include "slipgate/sg_compound_swim_game.h"
#include "slipgate/sg_compound_world.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"

game_import_t gi;
game_export_t globals;
game_locals_t game;
level_locals_t level;
edict_t *g_edicts;
sg_bot_t sg_bots[SG_MAXBOTS];
sg_cvars_t sg_cv;
sg_host_t sg_host;

static edict_t entities[4];
static gclient_t clients[1];
static rune_t rune;
static rune_seed_t seeds[2];
static rune_link_t links[1];
static sg_compound_publication_binding_t binding;
static sg_compound_world_preopen_t mechanism;
static cvar_t debug_cvar;
static int publication_present;
static sg_compound_guard_result_t acquire_result;
static int acquire_calls;
static int authorize_calls;
static int oracle_calls;
static vec3_t oracle_mechanism_anchor;
static int client_think_calls;
static int client_think_msec[8];
static sg_compound_guard_result_t validate_result;
static sg_mover_lease_record_t validate_record;
static char event_log[4096];
static int link_calls;
static int unlink_calls;

static void LinkEntity(edict_t *entity)
{
	(void)entity;
	link_calls++;
}

static void UnlinkEntity(edict_t *entity)
{
	(void)entity;
	unlink_calls++;
}

static void Dprint(const char *format, ...)
{
	size_t used = strlen(event_log);
	va_list arguments;

	if (used >= sizeof(event_log) - 1U)
		return;
	va_start(arguments, format);
	(void)vsnprintf(event_log + used, sizeof(event_log) - used, format,
	                arguments);
	va_end(arguments);
}

static int EventCount(const char *needle)
{
	const char *cursor = event_log;
	int count = 0;

	while ((cursor = strstr(cursor, needle)) != NULL)
	{
		count++;
		cursor += strlen(needle);
	}
	return count;
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, \
		    #expression); \
		return 0; \
	} \
} while (0)

rune_t *SG_Rune(void)
{
	return &rune;
}

const sg_compound_publication_binding_t *SG_CompoundPublicationBinding(
	const rune_t *candidate, uint32_t link_index)
{
	return publication_present && candidate == &rune && link_index == 0U &&
	       links[0].action == RL_DOOR_SWIM ?
	       &binding : NULL;
}

const sg_compound_world_preopen_t *SG_CompoundPublicationMechanism(
	const rune_t *candidate,
	const sg_compound_publication_binding_t *candidate_binding)
{
	return candidate == &rune && candidate_binding == &binding ?
	       &mechanism : NULL;
}

sg_compound_guard_result_t SG_CompoundGuardAcquireCompoundPreopen(
	sg_compound_guard_bot_t *guard, const sg_mover_key_t *keys,
	size_t key_count, int32_t link_index, uint32_t mechanism_index)
{
	(void)guard;
	(void)keys;
	(void)key_count;
	(void)link_index;
	(void)mechanism_index;
	acquire_calls++;
	return acquire_result;
}

sg_compound_guard_result_t SG_CompoundGuardAuthorize(
	sg_compound_guard_bot_t *guard, sg_mover_lease_law_t law,
	const sg_mover_key_t *keys, size_t key_count, int32_t link_index,
	uint32_t mechanism_index)
{
	(void)guard;
	(void)law;
	(void)keys;
	(void)key_count;
	(void)link_index;
	(void)mechanism_index;
	authorize_calls++;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardMaintain(
	sg_compound_guard_bot_t *guard)
{
	(void)guard;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardReleaseProvedClear(
	sg_compound_guard_bot_t *guard)
{
	(void)guard;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardValidate(
	sg_compound_guard_bot_t *guard, sg_mover_lease_record_t *record)
{
	(void)guard;
	if (record)
		*record = validate_record;
	return validate_result;
}

rune_reject_reason_t SG_OracleCompoundSwimPreopen(sg_phantom_t *phantom,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t mechanism_anchor, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_proof_t *proof, sg_replay_reason_t *replay_reason,
	edict_t *passent, qboolean world_only, qboolean loader_replay)
{
	(void)phantom;
	(void)resolved;
	(void)destination;
	(void)destination_water;
	(void)old_frame_z;
	(void)replay_reason;
	(void)passent;
	(void)world_only;
	(void)loader_replay;
	oracle_calls++;
	VectorCopy(mechanism_anchor, oracle_mechanism_anchor);
	memset(proof, 0, sizeof(*proof));
	proof->touch_ms = 225;
	proof->touch_frame_end_ms = 300;
	proof->mover_top_ms = 500;
	proof->suffix_start_ms = 400;
	proof->arrival_ms = 300;
	proof->sweep_clear_ms = 200;
	proof->total_cost_ms = 1000;
	proof->exit_speed = 12;
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundSwimContinue(sg_phantom_t *phantom,
	const sg_compound_world_preopen_t *resolved, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_recovery_proof_t *proof, edict_t *passent)
{
	(void)phantom;
	(void)resolved;
	(void)destination;
	(void)destination_water;
	(void)old_frame_z;
	(void)proof;
	(void)passent;
	return RLR_APPROACH_REPLAY_FAILED;
}

rune_reject_reason_t SG_OracleCompoundSwimRecover(sg_phantom_t *phantom,
	const sg_compound_world_preopen_t *resolved, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_recovery_proof_t *proof, edict_t *passent)
{
	return SG_OracleCompoundSwimContinue(phantom, resolved, destination,
	    destination_water, old_frame_z, proof, passent);
}

int SG_CompoundWorldAtTopFor(
	const sg_compound_world_preopen_t *resolved, int lease_ms)
{
	(void)resolved;
	(void)lease_ms;
	return false;
}

int SG_CompoundWorldOutsideSweep(
	const sg_compound_world_preopen_t *resolved, const vec3_t origin)
{
	(void)resolved;
	(void)origin;
	return true;
}

int SG_CompoundWorldCrossesSweep(
	const sg_compound_world_preopen_t *resolved, const vec3_t from,
	const vec3_t to)
{
	(void)resolved;
	(void)from;
	(void)to;
	return false;
}

int SG_CompoundPublicationCaptureAngleBias(
	const sg_compound_publication_checkpoint_t *expected,
	const sg_compound_publication_checkpoint_t *live,
	sg_compound_publication_angle_bias_t *bias)
{
	(void)expected;
	(void)live;
	memset(bias, 0, sizeof(*bias));
	return 1;
}

qboolean SG_SwimArrived(const vec3_t origin, const vec3_t destination,
	qboolean destination_water, qboolean grounded, int watertype,
	int waterlevel, edict_t *passent)
{
	(void)origin;
	(void)destination;
	(void)destination_water;
	(void)grounded;
	(void)watertype;
	(void)waterlevel;
	(void)passent;
	return false;
}

void ClientThink(edict_t *entity, usercmd_t *command)
{
	(void)entity;
	if (client_think_calls < 8)
		client_think_msec[client_think_calls] = command->msec;
	client_think_calls++;
}

static void FixtureInit(int action)
{
	memset(&globals, 0, sizeof(globals));
	memset(&gi, 0, sizeof(gi));
	memset(&level, 0, sizeof(level));
	memset(entities, 0, sizeof(entities));
	memset(clients, 0, sizeof(clients));
	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	memset(&binding, 0, sizeof(binding));
	memset(&mechanism, 0, sizeof(mechanism));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(&sg_cv, 0, sizeof(sg_cv));
	memset(&sg_host, 0, sizeof(sg_host));
	memset(&debug_cvar, 0, sizeof(debug_cvar));
	memset(event_log, 0, sizeof(event_log));
	memset(client_think_msec, 0, sizeof(client_think_msec));
	publication_present = 1;
	acquire_result = SG_COMPOUND_GUARD_OK;
	acquire_calls = 0;
	authorize_calls = 0;
	oracle_calls = 0;
	VectorClear(oracle_mechanism_anchor);
	client_think_calls = 0;
	link_calls = 0;
	unlink_calls = 0;
	validate_result = SG_COMPOUND_GUARD_NO_LEASE;
	memset(&validate_record, 0, sizeof(validate_record));
	g_edicts = entities;
	globals.num_edicts = 4;
	level.framenum = 42;
	rune.hdr.num_links = 1;
	rune.hdr.num_seeds = 2;
	rune.seeds = seeds;
	rune.links = links;
	links[0].action = action;
	links[0].from = 0;
	links[0].to = 1;
	binding.link_index = 0;
	binding.link.from = 0;
	binding.link.to = 1;
	binding.link.action = RL_DOOR_SWIM;
	binding.link.provenance = RL_CONTRACTED;
	binding.link.mode = RLCM_PREOPEN;
	binding.link.exit_speed = 12;
	VectorSet(binding.link.mechanism_anchor, 80.0f, 0.0f, 0.0f);
	binding.source_seed.flags = RSF_WATER;
	VectorSet(binding.source_seed.origin, 16.0f, 32.0f, 48.0f);
	binding.source.pms.origin[0] = 128;
	binding.source.pms.origin[1] = 256;
	binding.source.pms.origin[2] = 384;
	binding.source.pms.velocity[0] = 8;
	binding.source.pms.velocity[1] = -16;
	binding.source.pms.velocity[2] = 24;
	binding.source.old_pms = binding.source.pms;
	binding.source.old_pms.pm_time = 7;
	binding.source.watertype = CONTENTS_WATER;
	binding.source.waterlevel = 3;
	binding.source.old_frame_z = -5.0f;
	seeds[0] = binding.source_seed;
	VectorSet(binding.destination_seed.origin, 200.0f, 0.0f, 0.0f);
	seeds[1] = binding.destination_seed;
	VectorSet(binding.canonical_hint, 72.0f, 0.0f, 0.0f);
	binding.mechanism_index = 0;
	mechanism.trigger_key = 2;
	mechanism.mover_key = 3;
	sg_bots[0].active = true;
	sg_bots[0].instance_token = 123456789ULL;
	sg_bots[0].ent = &entities[1];
	entities[1].inuse = true;
	entities[1].client = &clients[0];
	entities[1].health = 100;
	entities[1].deadflag = DEAD_NO;
	entities[1].waterlevel = 3;
	entities[1].watertype = CONTENTS_WATER;
	sg_host.dprint = Dprint;
	sg_host.linkentity = LinkEntity;
	gi.unlinkentity = UnlinkEntity;
	sg_cv.debug = &debug_cvar;
}

static int TestAuthenticatedProbeStagesPublishedSource(void)
{
	FixtureInit(RL_DOOR_SWIM);
	debug_cvar.value = 1.0f;
	CHECK(SG_CompoundSwimGameStageAuthenticatedProbe(0) == true);
	CHECK(unlink_calls == 1);
	CHECK(link_calls == 1);
	CHECK(memcmp(entities[1].s.origin, binding.source_seed.origin,
	             sizeof(vec3_t)) == 0);
	CHECK(memcmp(entities[1].s.old_origin, binding.source_seed.origin,
	             sizeof(vec3_t)) == 0);
	CHECK(entities[1].velocity[0] == 1.0f);
	CHECK(entities[1].velocity[1] == -2.0f);
	CHECK(entities[1].velocity[2] == 3.0f);
	CHECK(memcmp(&clients[0].ps.pmove, &binding.source.pms,
	             sizeof(binding.source.pms)) == 0);
	CHECK(memcmp(&clients[0].old_pmove, &binding.source.old_pms,
	             sizeof(binding.source.old_pms)) == 0);
	CHECK(clients[0].oldvelocity[2] == -5.0f);
	CHECK(entities[1].waterlevel == 3);
	CHECK(entities[1].watertype == CONTENTS_WATER);
	CHECK(SG_BotLocalizationCell(&sg_bots[0]) < 0);
	CHECK(sg_bots[0].commit_link == 0);
	CHECK(sg_bots[0].sticky_link == 0);
	CHECK(sg_bots[0].compound_swim.guard_owned);
	CHECK(EventCount("event=begin") == 1);
	CHECK(strstr(event_log,
	    "dswim probe-staged bot=0 link=0 source=(16.000 32.000 48.000) "
	    "destination=(200.000 0.000 0.000)") != NULL);
	return 1;
}

static int TestAuthenticatedProbeRejectsUnauthorizedAndMalformedLinks(void)
{
	FixtureInit(RL_DOOR_SWIM);
	VectorSet(entities[1].s.origin, 9.0f, 9.0f, 9.0f);
	CHECK(SG_CompoundSwimGameStageAuthenticatedProbe(0) == false);
	CHECK(unlink_calls == 0);
	CHECK(entities[1].s.origin[0] == 9.0f);
	debug_cvar.value = 1.0f;
	CHECK(SG_CompoundSwimGameStageAuthenticatedProbe(-1) == false);
	CHECK(SG_CompoundSwimGameStageAuthenticatedProbe(1) == false);
	links[0].action = RL_RUN;
	CHECK(SG_CompoundSwimGameStageAuthenticatedProbe(0) == false);
	links[0].action = RL_DOOR_SWIM;
	publication_present = 0;
	CHECK(SG_CompoundSwimGameStageAuthenticatedProbe(0) == false);
	CHECK(unlink_calls == 0);
	return 1;
}

static int TestNonCompoundFallsThrough(void)
{
	FixtureInit(RL_RUN);
	CHECK(SG_CompoundSwimGameEmit(&sg_bots[0], 0) == false);
	CHECK(acquire_calls == 0);
	CHECK(client_think_calls == 0);
	return 1;
}

static int TestCompoundWaitOwnsFrame(void)
{
	FixtureInit(RL_DOOR_SWIM);
	acquire_result = SG_COMPOUND_GUARD_CONFLICT;
	CHECK(SG_CompoundSwimGameEmit(&sg_bots[0], 0) == true);
	CHECK(acquire_calls == 1);
	CHECK(oracle_calls == 0);
	CHECK(client_think_calls == 0);
	CHECK(!sg_bots[0].compound_swim.guard_owned);
	CHECK(strcmp(event_log,
	    "slipgate: dswim event=failure bot=0 instance=123456789 link=0 "
	    "frame=42 reason=acquire replay=0\n") == 0);
	return 1;
}

static int TestCompoundRejectOwnsFrame(void)
{
	FixtureInit(RL_DOOR_SWIM);
	binding.link.provenance = RL_OBSERVED;
	CHECK(SG_CompoundSwimGameEmit(&sg_bots[0], 0) == true);
	CHECK(acquire_calls == 0);
	CHECK(oracle_calls == 0);
	CHECK(client_think_calls == 0);
	CHECK(strstr(event_log,
	    "event=failure bot=0 instance=123456789 link=0 frame=42 "
	    "reason=plan replay=1") != NULL);
	return 1;
}

static int TestCompoundRunsFourSubstepsPerEmit(void)
{
	int index;

	FixtureInit(RL_DOOR_SWIM);
	CHECK(SG_CompoundSwimGameEmit(&sg_bots[0], 0) == true);
	CHECK(acquire_calls == 1);
	CHECK(oracle_calls == 1);
	CHECK(memcmp(oracle_mechanism_anchor,
	             binding.link.mechanism_anchor, sizeof(vec3_t)) == 0);
	CHECK(memcmp(oracle_mechanism_anchor, binding.canonical_hint,
	             sizeof(vec3_t)) != 0);
	CHECK(client_think_calls == 4);
	CHECK(sg_bots[0].compound_swim.command_pending);
	CHECK(sg_bots[0].compound_swim.replay.progress.elapsed_ms == 75);
	CHECK(SG_CompoundSwimGameEmit(&sg_bots[0], -1) == true);
	CHECK(client_think_calls == 8);
	for (index = 0; index < 8; index++)
		CHECK(client_think_msec[index] == SG_REPLAY_STEP_MS);
	CHECK(sg_bots[0].compound_swim.guard_owned);
	CHECK(sg_bots[0].compound_swim.command_pending);
	CHECK(sg_bots[0].compound_swim.replay.progress.elapsed_ms == 175);
	CHECK(authorize_calls >= 10);
	CHECK(EventCount("event=begin") == 1);
	CHECK(EventCount("event=failure") == 0);
	CHECK(strstr(event_log,
	    "slipgate: dswim event=begin bot=0 instance=123456789 link=0 "
	    "frame=42 reason=none replay=0\n") != NULL);
	return 1;
}

static void FixtureOwnsCompound(void)
{
	sg_bots[0].compound_swim.guard_owned = true;
	sg_bots[0].compound_swim.snapshot.binding.link_index = 0U;
	sg_bots[0].compound_swim.snapshot.mover_key = 3;
}

static int TestGameSuppressesRepeatedAuthenticatedTouch(void)
{
	FixtureInit(RL_DOOR_SWIM);
	FixtureOwnsCompound();
	sg_bots[0].compound_swim.snapshot.binding = binding;
	sg_bots[0].compound_swim.snapshot.trigger_key = 2;
	sg_bots[0].compound_swim.outer.phase = SG_COMPOUND_OPENING;
	sg_bots[0].compound_swim.outer.link_index = 0;
	sg_bots[0].compound_swim.outer.mover_key = 3;
	sg_bots[0].compound_swim.outer.action = RL_DOOR_SWIM;
	sg_bots[0].compound_swim.touch_frame_serial = level.framenum;
	CHECK(SG_CompoundSwimGameAuthorizeTouch(&entities[2],
	                                        &entities[1]) == false);
	CHECK(sg_bots[0].compound_swim.outer.phase == SG_COMPOUND_OPENING);
	CHECK(EventCount("event=failure") == 0);
	CHECK(SG_CompoundSwimGameAuthorizeTouch(&entities[3],
	                                        &entities[1]) == false);
	CHECK(sg_bots[0].compound_swim.outer.phase == SG_COMPOUND_RECOVER);
	CHECK(strstr(event_log, "event=failure bot=0 instance=123456789 "
	             "link=0 frame=42 reason=touch") != NULL);
	return 1;
}

static int TestClientRetirementRequiresOrphanOrNoLease(void)
{
	FixtureInit(RL_DOOR_SWIM);
	FixtureOwnsCompound();
	validate_result = SG_COMPOUND_GUARD_OK;
	validate_record.law = SG_MOVER_LAW_COMPOUND_PREOPEN;
	validate_record.state = SG_MOVER_LEASE_ACTIVE;
	validate_record.link_index = 0;
	validate_record.key_count = 1U;
	validate_record.keys[0] = 3U;
	SG_CompoundSwimGameClientRetired(&entities[1]);
	CHECK(sg_bots[0].compound_swim.guard_owned);
	validate_record.state = SG_MOVER_LEASE_ORPHAN;
	SG_CompoundSwimGameClientRetired(&entities[1]);
	CHECK(!sg_bots[0].compound_swim.guard_owned);

	FixtureOwnsCompound();
	validate_result = SG_COMPOUND_GUARD_NO_LEASE;
	SG_CompoundSwimGameClientRetired(&entities[1]);
	CHECK(!sg_bots[0].compound_swim.guard_owned);
	return 1;
}

static int TestSlotResetRequiresDetachedGuard(void)
{
	FixtureInit(RL_DOOR_SWIM);
	FixtureOwnsCompound();
	validate_result = SG_COMPOUND_GUARD_OK;
	SG_CompoundSwimGameReset(&sg_bots[0]);
	CHECK(sg_bots[0].compound_swim.guard_owned);
	validate_result = SG_COMPOUND_GUARD_NOT_ATTACHED;
	SG_CompoundSwimGameReset(&sg_bots[0]);
	CHECK(!sg_bots[0].compound_swim.guard_owned);
	return 1;
}

int main(void)
{
	if (!TestAuthenticatedProbeStagesPublishedSource() ||
	    !TestAuthenticatedProbeRejectsUnauthorizedAndMalformedLinks() ||
	    !TestNonCompoundFallsThrough() ||
	    !TestCompoundWaitOwnsFrame() ||
	    !TestCompoundRejectOwnsFrame() ||
	    !TestCompoundRunsFourSubstepsPerEmit() ||
	    !TestGameSuppressesRepeatedAuthenticatedTouch() ||
	    !TestClientRetirementRequiresOrphanOrNoLease() ||
	    !TestSlotResetRequiresDetachedGuard())
		return 1;
	puts("sg_compound_swim_game_test: ok");
	return 0;
}
