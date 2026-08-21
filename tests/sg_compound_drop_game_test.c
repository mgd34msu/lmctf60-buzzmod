/* Authenticated debug staging boundary for production D_DROP. */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_drop_game.h"
#include "slipgate/sg_compound_guard.h"
#include "slipgate/sg_compound_publication.h"
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

static edict_t entities[3];
static gclient_t clients[1];
static rune_t rune;
static rune_seed_t seeds[2];
static rune_link_t links[1];
static sg_compound_publication_binding_t binding;
static sg_compound_world_preopen_t mechanism;
static cvar_t debug_cvar;
static int publication_present;
static int physics_compatible;
static int mechanism_resolved;
static int unlink_calls;
static int link_calls;
static int begin_calls;
static uint32_t begin_link;
static sg_compound_guard_result_t validate_result;
static char event_log[1024];

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, \
		    #expression); \
		return 0; \
	} \
} while (0)

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

	va_start(arguments, format);
	(void)vsnprintf(event_log + used, sizeof(event_log) - used, format,
	                arguments);
	va_end(arguments);
}

rune_t *SG_Rune(void)
{
	return &rune;
}

qboolean SG_RunePhysicsCompatible(const rune_t *candidate)
{
	return candidate == &rune && physics_compatible;
}

const sg_compound_publication_binding_t *SG_CompoundPublicationBinding(
	const rune_t *candidate, uint32_t link_index)
{
	return publication_present && candidate == &rune && link_index == 0U &&
	       links[0].action == RL_DOOR_DROP ? &binding : NULL;
}

const sg_compound_world_preopen_t *SG_CompoundPublicationMechanism(
	const rune_t *candidate,
	const sg_compound_publication_binding_t *candidate_binding)
{
	return candidate == &rune && candidate_binding == &binding ?
	       &mechanism : NULL;
}

int SG_CompoundWorldResolvedMember(
	const sg_compound_world_preopen_t *candidate, edict_t **member_out)
{
	if (!mechanism_resolved || candidate != &mechanism || !member_out)
		return 0;
	*member_out = &entities[2];
	return 1;
}

sg_compound_guard_result_t SG_CompoundGuardValidate(
	sg_compound_guard_bot_t *guard, sg_mover_lease_record_t *record)
{
	(void)guard;
	(void)record;
	return validate_result;
}

sg_compound_drop_live_result_t SG_CompoundDropLiveBegin(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, uint32_t link_index,
	const sg_replay_pose_t *pose)
{
	sg_compound_drop_live_result_t result;

	memset(&result, 0, sizeof(result));
	begin_calls++;
	begin_link = link_index;
	if (!state || !host || host->context != &sg_bots[0] || !pose)
	{
		result.outcome = SG_COMPOUND_DROP_LIVE_REJECTED;
		return result;
	}
	state->guard_owned = true;
	state->drop_link = (int)link_index;
	result.outcome = SG_COMPOUND_DROP_LIVE_RUNNING;
	return result;
}

void SG_DropLivePose(sg_replay_pose_t *pose, const pmove_state_t *pms,
	const vec3_t origin, const vec3_t velocity, qboolean grounded,
	int watertype, int waterlevel)
{
	memset(pose, 0, sizeof(*pose));
	pose->pms = *pms;
	VectorCopy(origin, pose->origin);
	VectorCopy(velocity, pose->velocity);
	pose->grounded = grounded;
	pose->watertype = watertype;
	pose->waterlevel = waterlevel;
}

sg_compound_guard_result_t SG_CompoundGuardAcquireCompoundPreopen(
	sg_compound_guard_bot_t *guard, const sg_mover_key_t *keys,
	size_t count, int32_t link_index, uint32_t mechanism_index)
{
	(void)guard; (void)keys; (void)count; (void)link_index;
	(void)mechanism_index;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardAuthorize(
	sg_compound_guard_bot_t *guard, sg_mover_lease_law_t law,
	const sg_mover_key_t *keys, size_t count, int32_t link_index,
	uint32_t mechanism_index)
{
	(void)guard; (void)law; (void)keys; (void)count; (void)link_index;
	(void)mechanism_index;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardMaintain(
	sg_compound_guard_bot_t *guard)
{
	(void)guard;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardAllSubjectsOutside(
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

sg_compound_guard_result_t SG_CompoundGuardOrphan(
	sg_compound_guard_bot_t *guard, int32_t owner_slot)
{
	(void)guard; (void)owner_slot;
	return SG_COMPOUND_GUARD_OK;
}

int SG_CompoundPublicationCaptureAngleBias(
	const sg_compound_publication_checkpoint_t *expected,
	const sg_compound_publication_checkpoint_t *live,
	sg_compound_publication_angle_bias_t *bias)
{
	(void)expected; (void)live;
	memset(bias, 0, sizeof(*bias));
	return 1;
}

int SG_CompoundPublicationCheckpointMatches(
	const sg_compound_publication_checkpoint_t *expected,
	const sg_compound_publication_checkpoint_t *live,
	const sg_compound_publication_angle_bias_t *bias)
{
	(void)expected; (void)live; (void)bias;
	return 1;
}

int SG_CompoundWorldAtTopFor(
	const sg_compound_world_preopen_t *candidate, int lease_ms)
{
	(void)candidate; (void)lease_ms;
	return 1;
}

int SG_CompoundWorldOutsideSweep(
	const sg_compound_world_preopen_t *candidate, const vec3_t origin)
{
	(void)candidate; (void)origin;
	return 1;
}

int SG_CompoundWorldCrossesSweep(
	const sg_compound_world_preopen_t *candidate, const vec3_t from,
	const vec3_t to)
{
	(void)candidate; (void)from; (void)to;
	return 0;
}

qboolean SG_ImmutableSupport(edict_t *entity)
{
	(void)entity;
	return true;
}

qboolean SG_DropReplayPlanarYawCommand(float x, float y,
	short delta_yaw, short *yaw_out)
{
	(void)x; (void)y; (void)delta_yaw;
	*yaw_out = 0;
	return true;
}

rune_reject_reason_t SG_OracleCompoundDropPreopen(
	const vec3_t source, const sg_compound_world_preopen_t *candidate,
	const vec3_t mechanism_anchor, const vec3_t destination,
	const vec3_t anchor, byte heading, qboolean destination_water,
	sg_compound_drop_proof_t *proof, qboolean loader_replay)
{
	(void)source; (void)candidate; (void)mechanism_anchor;
	(void)destination; (void)anchor; (void)heading;
	(void)destination_water; (void)loader_replay;
	memset(proof, 0, sizeof(*proof));
	return RLR_OK;
}

sg_compound_drop_live_result_t SG_CompoundDropLiveAuthorizeTouch(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, int trigger_key,
	const sg_replay_pose_t *pose, int frame_serial)
{
	sg_compound_drop_live_result_t result;
	(void)state; (void)host; (void)trigger_key; (void)pose;
	(void)frame_serial;
	memset(&result, 0, sizeof(result));
	return result;
}

sg_compound_drop_live_result_t SG_CompoundDropLiveAuthorizeActivation(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host, int trigger_key,
	int mover_key, int frame_serial)
{
	sg_compound_drop_live_result_t result;
	(void)state; (void)host; (void)trigger_key; (void)mover_key;
	(void)frame_serial;
	memset(&result, 0, sizeof(result));
	return result;
}

sg_compound_drop_live_result_t SG_CompoundDropLiveOrphan(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host)
{
	sg_compound_drop_live_result_t result;
	(void)state; (void)host;
	memset(&result, 0, sizeof(result));
	return result;
}

static void FixtureInit(int action)
{
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
	g_edicts = entities;
	rune.hdr.num_links = 1;
	rune.hdr.num_seeds = 2;
	rune.seeds = seeds;
	rune.links = links;
	links[0].action = action;
	binding.link_index = 0;
	binding.link.action = RL_DOOR_DROP;
	binding.link.from = 0;
	binding.link.to = 1;
	VectorSet(binding.source_seed.origin, 16.0f, 32.0f, 48.0f);
	binding.source.pms.velocity[0] = 8;
	binding.source.pms.velocity[1] = -16;
	binding.source.pms.velocity[2] = 24;
	binding.source.old_pms.pm_time = 7;
	binding.source.old_frame_z = -5.0f;
	binding.source.grounded = true;
	VectorSet(binding.destination_seed.origin, 200.0f, 0.0f, 0.0f);
	mechanism.trigger_key = 2;
	mechanism.mover_key = 3;
	publication_present = 1;
	physics_compatible = 1;
	mechanism_resolved = 1;
	validate_result = SG_COMPOUND_GUARD_NO_LEASE;
	unlink_calls = 0;
	link_calls = 0;
	begin_calls = 0;
	begin_link = UINT32_MAX;
	sg_bots[0].active = true;
	sg_bots[0].ent = &entities[1];
	entities[1].inuse = true;
	entities[1].client = &clients[0];
	entities[1].health = 100;
	entities[1].deadflag = DEAD_NO;
	sg_cv.debug = &debug_cvar;
	sg_host.dprint = Dprint;
	sg_host.linkentity = LinkEntity;
	gi.unlinkentity = UnlinkEntity;
}

static int TestAuthenticatedProbeStagesPublishedSource(void)
{
	FixtureInit(RL_DOOR_DROP);
	debug_cvar.value = 1.0f;
	CHECK(SG_CompoundDropGameStageAuthenticatedProbe(0));
	CHECK(unlink_calls == 1 && link_calls == 1 && begin_calls == 1);
	CHECK(begin_link == 0U);
	CHECK(memcmp(entities[1].s.origin, binding.source_seed.origin,
	             sizeof(vec3_t)) == 0);
	CHECK(entities[1].velocity[0] == 1.0f);
	CHECK(entities[1].velocity[1] == -2.0f);
	CHECK(entities[1].velocity[2] == 3.0f);
	CHECK(memcmp(&clients[0].ps.pmove, &binding.source.pms,
	             sizeof(binding.source.pms)) == 0);
	CHECK(memcmp(&clients[0].old_pmove, &binding.source.old_pms,
	             sizeof(binding.source.old_pms)) == 0);
	CHECK(clients[0].oldvelocity[2] == -5.0f);
	CHECK(entities[1].groundentity == g_edicts);
	CHECK(sg_bots[0].seed == 0 && sg_bots[0].commit_link == 0 &&
	      sg_bots[0].sticky_link == 0);
	CHECK(sg_bots[0].compound_drop_live.guard_owned);
	CHECK(strstr(event_log, "ddrop probe-staged bot=0 link=0 ") != NULL);
	return 1;
}

static int TestAuthenticatedProbeRejectsInvalidRequests(void)
{
	FixtureInit(RL_DOOR_DROP);
	VectorSet(entities[1].s.origin, 9.0f, 9.0f, 9.0f);
	CHECK(!SG_CompoundDropGameStageAuthenticatedProbe(0));
	CHECK(unlink_calls == 0 && begin_calls == 0);
	CHECK(entities[1].s.origin[0] == 9.0f);
	debug_cvar.value = 1.0f;
	CHECK(!SG_CompoundDropGameStageAuthenticatedProbe(-1));
	CHECK(!SG_CompoundDropGameStageAuthenticatedProbe(1));
	links[0].action = RL_RUN;
	CHECK(!SG_CompoundDropGameStageAuthenticatedProbe(0));
	links[0].action = RL_DOOR_DROP;
	publication_present = 0;
	CHECK(!SG_CompoundDropGameStageAuthenticatedProbe(0));
	publication_present = 1;
	physics_compatible = 0;
	CHECK(!SG_CompoundDropGameStageAuthenticatedProbe(0));
	physics_compatible = 1;
	mechanism.trigger_key = 0;
	CHECK(!SG_CompoundDropGameStageAuthenticatedProbe(0));
	CHECK(unlink_calls == 0 && begin_calls == 0);
	return 1;
}

int main(void)
{
	if (!TestAuthenticatedProbeStagesPublishedSource() ||
	    !TestAuthenticatedProbeRejectsInvalidRequests())
		return 1;
	puts("sg_compound_drop_game_test: ok");
	return 0;
}
