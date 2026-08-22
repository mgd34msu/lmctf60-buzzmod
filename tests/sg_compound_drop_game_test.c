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

static edict_t entities[6];
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
static sg_compound_drop_live_outcome_t begin_outcome;
static int continue_calls;
static int recover_calls;
static sg_phantom_t continued_phantom;
static sg_phantom_t recovered_phantom;
static float continued_old_frame_z;
static float recovered_old_frame_z;
static edict_t *continued_passent;
static edict_t *recovered_passent;
static int live_orphan_calls;
static int live_orphan_bolt_key;
static int live_recover_calls;
static int live_prestep_calls;
static float live_recover_old_frame_z;
static sg_compound_drop_live_result_t live_recover_result;
static sg_compound_drop_live_result_t live_prestep_result;
static sg_compound_guard_result_t validate_result;
static char event_log[1024];

vec_t VectorLength(vec3_t value)
{
	return sqrtf(value[0] * value[0] + value[1] * value[1] +
	             value[2] * value[2]);
}

const char *SG_CompoundDropLiveFailureName(
	sg_compound_drop_live_failure_t failure)
{
	(void)failure;
	return "fixture";
}

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
	*member_out = &entities[3];
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
	if (begin_outcome != SG_COMPOUND_DROP_LIVE_RUNNING)
	{
		result.outcome = begin_outcome;
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

void SG_Mark(float *stamp)
{
	*stamp = level.time;
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

rune_reject_reason_t SG_OracleCompoundDropContinue(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *candidate,
	const vec3_t destination, const vec3_t anchor, byte heading,
	qboolean destination_water, float old_frame_z,
	sg_compound_drop_proof_t *proof, edict_t *passent)
{
	(void)candidate;
	(void)destination; (void)anchor; (void)heading;
	(void)destination_water;
	continue_calls++;
	continued_phantom = *ph;
	continued_old_frame_z = old_frame_z;
	continued_passent = passent;
	memset(proof, 0, sizeof(*proof));
	proof->arrival_ms = 325;
	proof->sweep_clear_ms = 225;
	proof->exit_speed = 123.0f;
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundDropRecover(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t destination, const vec3_t lip, byte heading,
	qboolean destination_water, float old_frame_z,
	sg_compound_drop_proof_t *proof, edict_t *passent)
{
	(void)resolved; (void)destination; (void)lip; (void)heading;
	(void)destination_water;
	recover_calls++;
	recovered_phantom = *ph;
	recovered_old_frame_z = old_frame_z;
	recovered_passent = passent;
	memset(proof, 0, sizeof(*proof));
	proof->arrival_ms = 425;
	proof->sweep_clear_ms = 125;
	proof->exit_speed = 99.0f;
	return RLR_OK;
}

sg_compound_drop_live_result_t SG_CompoundDropLiveRecover(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose, float old_frame_z)
{
	(void)host; (void)pose;
	live_recover_calls++;
	live_recover_old_frame_z = old_frame_z;
	if (live_recover_result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING &&
	    live_recover_result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_NONE)
		state->replay_kind = SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY;
	else
		state->replay_kind = SG_COMPOUND_DROP_LIVE_REPLAY_NONE;
	return live_recover_result;
}

sg_compound_drop_live_result_t SG_CompoundDropLivePreStep(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose, usercmd_t *command)
{
	(void)state; (void)host; (void)pose;
	live_prestep_calls++;
	if (command)
	{
		memset(command, 0, sizeof(*command));
		command->msec = SG_REPLAY_STEP_MS;
		command->forwardmove = 400;
	}
	return live_prestep_result;
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
	const sg_compound_drop_live_host_t *host, int bolt_key)
{
	sg_compound_drop_live_result_t result;
	(void)host;
	memset(&result, 0, sizeof(result));
	live_orphan_calls++;
	live_orphan_bolt_key = bolt_key;
	state->guard_owned = false;
	result.outcome = SG_COMPOUND_DROP_LIVE_SAFE_STOPPED;
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
	globals.num_edicts = 6;
	for (int entity_index = 0; entity_index < 6; entity_index++)
		entities[entity_index].s.number = entity_index;
	level.time = 42.0f;
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
	begin_outcome = SG_COMPOUND_DROP_LIVE_RUNNING;
	continue_calls = 0;
	live_recover_calls = 0;
	live_prestep_calls = 0;
	live_recover_old_frame_z = 0.0f;
	memset(&live_recover_result, 0, sizeof(live_recover_result));
	memset(&live_prestep_result, 0, sizeof(live_prestep_result));
	memset(&continued_phantom, 0, sizeof(continued_phantom));
	continued_old_frame_z = 0.0f;
	continued_passent = NULL;
	sg_bots[0].active = true;
	sg_bots[0].ent = &entities[1];
	entities[1].inuse = true;
	entities[1].client = &clients[0];
	entities[1].health = 100;
	entities[1].deadflag = DEAD_NO;
	entities[3].inuse = true;
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
	CHECK(memcmp(sg_bots[0].stuck_origin, binding.source_seed.origin,
	             sizeof(vec3_t)) == 0);
	CHECK(memcmp(sg_bots[0].watch_org, binding.source_seed.origin,
	             sizeof(vec3_t)) == 0);
	CHECK(memcmp(sg_bots[0].stag_org, binding.source_seed.origin,
	             sizeof(vec3_t)) == 0);
	CHECK(memcmp(sg_bots[0].wedge_org, binding.source_seed.origin,
	             sizeof(vec3_t)) == 0);
	CHECK(sg_bots[0].watch_since == level.time);
	CHECK(sg_bots[0].stag_since == level.time);
	CHECK(sg_bots[0].wedge_since == level.time);
	CHECK(!sg_bots[0].seedless_active &&
	      sg_bots[0].seedless_since == 0.0f &&
	      sg_bots[0].seedless_turn_until == 0.0f);
	CHECK(sg_bots[0].compound_drop_live.guard_owned);
	CHECK(strstr(event_log, "ddrop probe-staged bot=0 link=0 ") != NULL);
	return 1;
}

static int TestTopProofUsesExactLiveContinue(void)
{
	sg_compound_drop_live_host_t host;
	sg_compound_drop_live_snapshot_t snapshot;
	sg_compound_drop_live_proof_t proof;
	sg_replay_pose_t pose;
	int axis;

	FixtureInit(RL_DOOR_DROP);
	memset(&snapshot, 0, sizeof(snapshot));
	memset(&proof, 0, sizeof(proof));
	memset(&pose, 0, sizeof(pose));
	snapshot.binding = binding;
	snapshot.trigger_key = mechanism.trigger_key;
	snapshot.mover_key = mechanism.mover_key;
	clients[0].ps.pmove.pm_type = PM_NORMAL;
	clients[0].ps.pmove.pm_time = 3;
	clients[0].old_pmove.pm_time = 2;
	clients[0].oldvelocity[2] = -37.0f;
	VectorSet(entities[1].s.origin, 8.0f, 16.0f, 24.0f);
	VectorSet(entities[1].velocity, 32.0f, -16.0f, -64.0f);
	VectorSet(entities[1].mins, -16.0f, -16.0f, -24.0f);
	VectorSet(entities[1].maxs, 16.0f, 16.0f, 32.0f);
	entities[1].groundentity = &entities[2];
	entities[1].watertype = CONTENTS_WATER;
	entities[1].waterlevel = 1;
	CHECK(SG_CompoundDropGameHost(&sg_bots[0], &host));
	CHECK(host.prove_suffix(host.context, &snapshot, &pose, false, &proof) ==
	      SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED);
	CHECK(continue_calls == 1);
	CHECK(continued_passent == &entities[1]);
	CHECK(continued_old_frame_z == -37.0f);
	CHECK(memcmp(&continued_phantom.pms, &clients[0].ps.pmove,
	             sizeof(continued_phantom.pms)) == 0);
	CHECK(memcmp(&continued_phantom.old_pms, &clients[0].old_pmove,
	             sizeof(continued_phantom.old_pms)) == 0);
	for (axis = 0; axis < 3; axis++)
	{
		CHECK(continued_phantom.origin[axis] == entities[1].s.origin[axis]);
		CHECK(continued_phantom.velocity[axis] == entities[1].velocity[axis]);
		CHECK(continued_phantom.mins[axis] == entities[1].mins[axis]);
		CHECK(continued_phantom.maxs[axis] == entities[1].maxs[axis]);
	}
	CHECK(continued_phantom.groundentity);
	CHECK(continued_phantom.groundentity_entity == &entities[2]);
	CHECK(continued_phantom.watertype == CONTENTS_WATER);
	CHECK(continued_phantom.waterlevel == 1);
	CHECK(proof.arrival_ms == 325 && proof.sweep_clear_ms == 225 &&
	      proof.exit_speed == 123.0f);
	CHECK(host.prove_suffix(host.context, &snapshot, &pose, true, &proof) ==
	      SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED);
	CHECK(continue_calls == 1 && recover_calls == 1);
	CHECK(recovered_passent == &entities[1]);
	CHECK(recovered_old_frame_z == -37.0f);
	CHECK(memcmp(&recovered_phantom, &continued_phantom,
	             sizeof(recovered_phantom)) == 0);
	CHECK(proof.arrival_ms == 425 && proof.sweep_clear_ms == 125 &&
	      proof.exit_speed == 99.0f);
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

static void SetActiveController(int controller)
{
	switch (controller)
	{
	case 0: sg_bots[0].hook_phase = 1; break;
	case 1: sg_bots[0].rocketjump.phase = SG_ROCKETJUMP_EQUIP; break;
	case 2: sg_bots[0].nade_phase = 1; break;
	case 3: sg_bots[0].hook_replay_active = true; break;
	case 4: clients[0].hookstate = 1; break;
	case 5: clients[0].hook = &entities[2]; break;
	case 6: sg_bots[0].jump_started = true; break;
	case 7: sg_bots[0].drop_started = true; break;
	case 8: sg_bots[0].drop_replay_active = true; break;
	case 9: sg_bots[0].swim_replay_active = true; break;
	case 10: sg_bots[0].swim_validated = true; break;
	case 11: sg_bots[0].declared_started = true; break;
	case 12: sg_bots[0].declared_touched = true; break;
	case 13: sg_bots[0].declared_triggered = true; break;
	case 14: sg_bots[0].declared_activated = true; break;
	case 15: sg_bots[0].declared_guard_paused = true; break;
	case 16: sg_bots[0].compound_drop_live.guard_owned = true; break;
	case 17:
		sg_bots[0].compound_drop_live.outer.phase = SG_COMPOUND_RECOVER;
		break;
	case 18:
		sg_bots[0].compound_drop_live.replay_kind =
		    SG_COMPOUND_DROP_LIVE_REPLAY_RECOVERY;
		break;
	case 19: sg_bots[0].compound_drop_live.command_pending = true; break;
	case 20: sg_bots[0].declared_door_retreat = true; break;
	default: break;
	}
}

static int TestControllerOwnershipBlocksAdmissionAndStaging(void)
{
	int controller;

	for (controller = 0; controller <= 20; controller++)
	{
		sg_bot_t bot_before;
		edict_t entity_before;
		gclient_t client_before;

		FixtureInit(RL_DOOR_DROP);
		debug_cvar.value = 1.0f;
		SetActiveController(controller);
		bot_before = sg_bots[0];
		entity_before = entities[1];
		client_before = clients[0];
		CHECK(!SG_CompoundDropGameIdleAdmission(&sg_bots[0]));
		CHECK(!SG_CompoundDropGameStageAuthenticatedProbe(0));
		CHECK(unlink_calls == 0 && link_calls == 0 && begin_calls == 0);
		CHECK(memcmp(&sg_bots[0], &bot_before, sizeof(bot_before)) == 0);
		CHECK(memcmp(&entities[1], &entity_before,
		             sizeof(entity_before)) == 0);
		CHECK(memcmp(&clients[0], &client_before,
		             sizeof(client_before)) == 0);
	}
	FixtureInit(RL_DOOR_DROP);
	debug_cvar.value = 1.0f;
	CHECK(SG_CompoundDropGameIdleAdmission(&sg_bots[0]));
	CHECK(SG_CompoundDropGameStageAuthenticatedProbe(0));
	CHECK(begin_calls == 1);
	return 1;
}

static int TestAuthenticatedProbeRestoresDeniedBegin(void)
{
	sg_bot_t bot_before;
	edict_t entity_before;
	gclient_t client_before;
	rune_t rune_before;
	sg_compound_publication_binding_t binding_before;
	sg_compound_world_preopen_t mechanism_before;

	FixtureInit(RL_DOOR_DROP);
	debug_cvar.value = 1.0f;
	VectorSet(entities[1].s.origin, -91.0f, 12.0f, 33.0f);
	VectorSet(entities[1].velocity, 8.0f, 9.0f, 10.0f);
	sg_bots[0].commit_link = 44;
	sg_bots[0].sticky_link = 45;
	bot_before = sg_bots[0];
	entity_before = entities[1];
	client_before = clients[0];
	rune_before = rune;
	binding_before = binding;
	mechanism_before = mechanism;
	begin_outcome = SG_COMPOUND_DROP_LIVE_WAIT;
	CHECK(!SG_CompoundDropGameStageAuthenticatedProbe(0));
	CHECK(begin_calls == 1 && unlink_calls == 2 && link_calls == 2);
	CHECK(memcmp(&sg_bots[0], &bot_before, sizeof(bot_before)) == 0);
	CHECK(memcmp(&entities[1], &entity_before, sizeof(entity_before)) == 0);
	CHECK(memcmp(&clients[0], &client_before, sizeof(client_before)) == 0);
	CHECK(memcmp(&rune, &rune_before, sizeof(rune_before)) == 0);
	CHECK(memcmp(&binding, &binding_before, sizeof(binding_before)) == 0);
	CHECK(memcmp(&mechanism, &mechanism_before,
	             sizeof(mechanism_before)) == 0);
	return 1;
}

static int TestOrphanPassesExactHookBolt(void)
{
	FixtureInit(RL_DOOR_DROP);
	sg_bots[0].compound_drop_live.guard_owned = true;
	CHECK(SG_CompoundDropGameOrphan(&sg_bots[0], 17) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(live_orphan_calls == 1 && live_orphan_bolt_key == 17);
	CHECK(!sg_bots[0].compound_drop_live.guard_owned);
	return 1;
}


static int TestOwnedFailureRecoveryCoordinator(void)
{
	sg_compound_drop_live_host_t host;
	sg_compound_drop_live_result_t result;
	sg_replay_pose_t pose;
	usercmd_t command;

	FixtureInit(RL_DOOR_DROP);
	clients[0].oldvelocity[2] = -37.0f;
	memset(&pose, 0, sizeof(pose));
	CHECK(SG_CompoundDropGameHost(&sg_bots[0], &host));
	live_recover_result.outcome = SG_COMPOUND_DROP_LIVE_RECOVERING;
	live_recover_result.failure = SG_COMPOUND_DROP_LIVE_FAILURE_NONE;
	live_prestep_result.outcome = SG_COMPOUND_DROP_LIVE_RECOVERING;
	live_prestep_result.failure = SG_COMPOUND_DROP_LIVE_FAILURE_NONE;
	live_prestep_result.command_ready = true;
	memset(&command, 0, sizeof(command));
	result = SG_CompoundDropGameRecoverOwnedFailure(&sg_bots[0], &host,
	                                               &pose, &command);
	CHECK(result.command_ready && command.msec == SG_REPLAY_STEP_MS &&
	      command.forwardmove == 400);
	CHECK(live_recover_calls == 1 && live_prestep_calls == 1 &&
	      live_recover_old_frame_z == -37.0f);

	live_recover_calls = 0;
	live_prestep_calls = 0;
	result = SG_CompoundDropGameRecoverOwnedFailure(&sg_bots[0], &host,
	                                               &pose, NULL);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING);
	CHECK(live_recover_calls == 1 && live_prestep_calls == 0);

	live_recover_calls = 0;
	live_prestep_calls = 0;
	live_recover_result.outcome = SG_COMPOUND_DROP_LIVE_SAFE_STOPPED;
	result = SG_CompoundDropGameRecoverOwnedFailure(&sg_bots[0], &host,
	                                               &pose, &command);
	CHECK(result.outcome == SG_COMPOUND_DROP_LIVE_SAFE_STOPPED);
	CHECK(live_recover_calls == 1 && live_prestep_calls == 0);

	live_recover_calls = 0;
	live_recover_result.outcome = SG_COMPOUND_DROP_LIVE_RECOVERING;
	live_recover_result.failure = SG_COMPOUND_DROP_LIVE_FAILURE_REPROOF;
	result = SG_CompoundDropGameRecoverOwnedFailure(&sg_bots[0], &host,
	                                               &pose, &command);
	CHECK(result.failure == SG_COMPOUND_DROP_LIVE_FAILURE_REPROOF);
	CHECK(!result.command_ready && live_recover_calls == 1 &&
	      live_prestep_calls == 0);
	return 1;
}

int main(void)
{
	if (!TestAuthenticatedProbeStagesPublishedSource() ||
	    !TestAuthenticatedProbeRejectsInvalidRequests() ||
	    !TestControllerOwnershipBlocksAdmissionAndStaging() ||
	    !TestAuthenticatedProbeRestoresDeniedBegin() ||
	    !TestTopProofUsesExactLiveContinue() ||
	    !TestOrphanPassesExactHookBolt() ||
	    !TestOwnedFailureRecoveryCoordinator())
		return 1;
	puts("sg_compound_drop_game_test: ok");
	return 0;
}
