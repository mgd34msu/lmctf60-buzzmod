#include "../g_local.h"

#include <stdio.h>
#include <string.h>

#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_hook_game.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", \
	__FILE__, __LINE__, #c); return 0; } } while (0)
#define STUB_CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", \
	__FILE__, __LINE__, #c); stub_failed = 1; } } while (0)

static rune_t rune_fixture;
static sg_compound_publication_binding_t binding;
static sg_compound_world_preopen_t mechanism;
static edict_t entities[8];
static gclient_t client;
static sg_bot_t bot;
edict_t *g_edicts = entities;

static sg_compound_guard_result_t acquire_result;
static sg_compound_guard_result_t outside_result;
static sg_compound_guard_result_t release_result;
static sg_compound_guard_result_t orphan_result;
static sg_compound_guard_observation_t hook_observation;
static sg_compound_guard_observation_t hook_absent;
static qboolean events_idle;
static qboolean latched_door;
static qboolean latched_contaminated;
static qboolean at_top;
static int events_reset_calls;
static int abort_begin_calls;
static int abort_end_calls;
static int physical_abort_calls;
static int expected_frame;
static int touch_calls;
static int activate_calls;
static int recover_calls;
static int prestep_calls;
static int stub_failed;

static sg_compound_hook_live_result_t Result(
	sg_compound_hook_live_outcome_t outcome)
{
	sg_compound_hook_live_result_t result;

	memset(&result, 0, sizeof(result));
	result.outcome = outcome;
	return result;
}

rune_t *SG_Rune(void) { return &rune_fixture; }
qboolean SG_RunePhysicsCompatible(const rune_t *rune)
{
	return rune == &rune_fixture;
}

const sg_compound_publication_binding_t *SG_CompoundPublicationBinding(
	const rune_t *rune, uint32_t link_index)
{
	return rune == &rune_fixture && link_index == binding.link_index ?
	    &binding : NULL;
}

const sg_compound_world_preopen_t *SG_CompoundPublicationMechanism(
	const rune_t *rune, const sg_compound_publication_binding_t *candidate)
{
	return rune == &rune_fixture && candidate == &binding ?
	    &mechanism : NULL;
}

int SG_CompoundWorldResolvedMember(
	const sg_compound_world_preopen_t *candidate, edict_t **member_out)
{
	if (member_out) *member_out = NULL;
	if (candidate != &mechanism || !member_out) return 0;
	*member_out = &entities[2];
	return 1;
}

int SG_CompoundPublicationCaptureAngleBias(
	const sg_compound_publication_checkpoint_t *expected,
	const sg_compound_publication_checkpoint_t *live,
	sg_compound_publication_angle_bias_t *bias)
{
	if (!expected || !live || !bias) return 0;
	bias->axis[0] = 3; bias->axis[1] = 5; bias->axis[2] = 7;
	return 1;
}

int SG_CompoundPublicationCheckpointMatches(
	const sg_compound_publication_checkpoint_t *expected,
	const sg_compound_publication_checkpoint_t *live,
	const sg_compound_publication_angle_bias_t *bias)
{
	return expected && live && bias && bias->axis[0] == 3 &&
	    bias->axis[1] == 5 && bias->axis[2] == 7;
}

sg_compound_guard_result_t SG_CompoundGuardAcquireCompoundPreopen(
	sg_compound_guard_bot_t *guard, const sg_mover_key_t *keys,
	size_t count, int link, uint32_t trigger)
{
	STUB_CHECK(guard == &bot.compound_guard && keys && count == 1U);
	STUB_CHECK(keys[0] == mechanism.mover_key && link == (int)binding.link_index);
	STUB_CHECK(trigger == (uint32_t)mechanism.trigger_key);
	return acquire_result;
}

sg_compound_guard_result_t SG_CompoundGuardAuthorize(
	sg_compound_guard_bot_t *guard, sg_mover_lease_law_t law,
	const sg_mover_key_t *keys, size_t count, int link, uint32_t trigger)
{
	STUB_CHECK(guard == &bot.compound_guard && law == SG_MOVER_LAW_COMPOUND_PREOPEN);
	STUB_CHECK(keys && count == 1U && keys[0] == mechanism.mover_key);
	STUB_CHECK(link == (int)binding.link_index &&
	    trigger == (uint32_t)mechanism.trigger_key);
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardMaintain(
	sg_compound_guard_bot_t *guard)
{
	return guard == &bot.compound_guard ? SG_COMPOUND_GUARD_OK :
	    SG_COMPOUND_GUARD_INVALID_ARGUMENT;
}

sg_compound_guard_result_t SG_CompoundGuardAllSubjectsOutside(
	sg_compound_guard_bot_t *guard)
{
	STUB_CHECK(guard == &bot.compound_guard);
	return outside_result;
}

sg_compound_guard_result_t SG_CompoundGuardReleaseProvedClear(
	sg_compound_guard_bot_t *guard)
{
	STUB_CHECK(guard == &bot.compound_guard);
	return release_result;
}

sg_compound_guard_result_t SG_CompoundGuardOrphan(
	sg_compound_guard_bot_t *guard, int32_t bolt_key)
{
	STUB_CHECK(guard == &bot.compound_guard && (bolt_key == 0 || bolt_key == 6));
	return orphan_result;
}

sg_compound_guard_observation_t SG_CompoundGuardGameHookObserve(
	edict_t *owner, const sg_mover_subject_t *subject, edict_t **current_out)
{
	if (current_out) *current_out = NULL;
	if (owner != bot.ent || !subject || !current_out ||
	    subject->kind != SG_MOVER_SUBJECT_HOOK_BOLT ||
	    subject->edict_key != 6 || subject->generation != 55U)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	if (hook_observation == SG_COMPOUND_GUARD_YES)
		*current_out = &entities[6];
	return hook_observation;
}

sg_compound_guard_observation_t SG_CompoundGuardGameHookAbsent(edict_t *owner)
{
	return owner == bot.ent ? hook_absent :
	    SG_COMPOUND_GUARD_OBSERVATION_ERROR;
}

qboolean SG_CompoundHookGameEventsIdle(const sg_bot_t *candidate)
{
	return candidate == &bot && events_idle;
}

void SG_CompoundHookGameEventsReset(sg_bot_t *candidate)
{
	STUB_CHECK(candidate == &bot);
	events_reset_calls++;
	events_idle = true;
	latched_door = false;
	latched_contaminated = false;
}

void SG_CompoundHookGameObserveSafety(sg_bot_t *candidate,
	qboolean door_passed, qboolean contaminated)
{
	STUB_CHECK(candidate == &bot);
	latched_door = latched_door || door_passed;
	latched_contaminated = latched_contaminated || contaminated;
}

qboolean SG_CompoundHookGamePeekSafety(const sg_bot_t *candidate,
	qboolean *door_passed, qboolean *contaminated)
{
	if (candidate != &bot || !door_passed || !contaminated) return false;
	*door_passed = latched_door;
	*contaminated = latched_contaminated;
	return true;
}

qboolean SG_CompoundHookGameTakeSafety(sg_bot_t *candidate,
	qboolean *door_passed, qboolean *contaminated)
{
	if (!SG_CompoundHookGamePeekSafety(candidate, door_passed, contaminated))
		return false;
	latched_door = false;
	latched_contaminated = false;
	return true;
}

qboolean SG_CompoundHookGameRecoveryAbortBegin(sg_bot_t *candidate,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	STUB_CHECK(candidate == &bot && snapshot && bolt);
	abort_begin_calls++;
	return true;
}

qboolean SG_CompoundHookGameRecoveryAbortEnd(sg_bot_t *candidate,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	STUB_CHECK(candidate == &bot && snapshot && bolt);
	abort_end_calls++;
	return true;
}

sg_compound_hook_live_host_result_t SG_CompoundHookGameAuthorizeEvent(
	sg_bot_t *candidate, const sg_compound_hook_live_snapshot_t *snapshot,
	sg_compound_hook_live_event_t event,
	const sg_compound_hook_live_bolt_t *bolt)
{
	return candidate == &bot && snapshot && bolt &&
	    event > SG_COMPOUND_HOOK_LIVE_EVENT_NONE ?
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

int SG_CompoundWorldOutsideSweep(
	const sg_compound_world_preopen_t *candidate, const float origin[3])
{
	return candidate == &mechanism && origin && origin[0] >= 32.0f;
}

int SG_CompoundWorldCrossesSweep(
	const sg_compound_world_preopen_t *candidate, const float from[3],
	const float to[3])
{
	return candidate == &mechanism && from && to &&
	    from[0] < 32.0f && to[0] >= 32.0f;
}

int SG_CompoundWorldAtTopFor(
	const sg_compound_world_preopen_t *candidate, int lease_ms)
{
	return candidate == &mechanism &&
	    lease_ms == SG_COMPOUND_HOLD_LEASE_MS && at_top;
}

qboolean SG_ImmutableSupport(const edict_t *entity)
{
	return entity == &entities[0];
}

qboolean SG_SupportedArrived(const vec3_t origin, const vec3_t destination,
	qboolean grounded, int watertype, int waterlevel, edict_t *passent)
{
	return origin && destination && grounded && watertype == 0 &&
	    waterlevel == 0 && passent == bot.ent && origin[0] == destination[0];
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

qboolean SG_HookReplayFixedViewCommand(const sg_replay_pose_t *pose,
	const vec3_t view, usercmd_t *command)
{
	if (!pose || !view || !command) return false;
	memset(command, 0, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
	return true;
}

sg_replay_status_t SG_HookReplayPreStep(sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command)
{
	if (!state || !pose || !observation || !command) return SG_REPLAY_FAILED;
	memset(command, 0, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
	return SG_REPLAY_RUNNING;
}

void AngleVectors(vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	(void)angles;
	if (forward) VectorSet(forward, 1.0f, 0.0f, 0.0f);
	if (right) VectorSet(right, 0.0f, 1.0f, 0.0f);
	if (up) VectorSet(up, 0.0f, 0.0f, 1.0f);
}

void CTF_HookMuzzle(const vec3_t origin, float viewheight, int hand,
	const vec3_t forward, const vec3_t right, vec3_t start)
{
	(void)viewheight; (void)hand; (void)forward; (void)right;
	VectorCopy(origin, start);
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	(void)start; (void)bite; VectorClear(velocity); return 77;
}

void ctf_hook_abort(edict_t *owner)
{
	STUB_CHECK(owner == bot.ent);
	physical_abort_calls++;
	owner->client->hook = NULL;
	owner->client->hookstate = 0;
	hook_observation = SG_COMPOUND_GUARD_NO;
	hook_absent = SG_COMPOUND_GUARD_YES;
}

sg_compound_hook_live_result_t SG_CompoundHookLiveBegin(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, uint32_t link_index,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_compound_hook_live_snapshot_t snapshot;
	sg_compound_hook_live_state_t candidate = *state;

	memset(&snapshot, 0, sizeof(snapshot));
	if (host->bind(host->context, link_index, &snapshot) !=
	        SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED ||
	    host->source_checkpoint(host->context, &snapshot, pose, observation) !=
	        SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return Result(SG_COMPOUND_HOOK_LIVE_REJECTED);
	if (host->acquire(host->context, &snapshot) !=
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED)
		return Result(SG_COMPOUND_HOOK_LIVE_WAIT);
	candidate.snapshot = snapshot;
	candidate.guard_owned = true;
	candidate.local_owned = true;
	candidate.outer.phase = SG_COMPOUND_APPROACH;
	*state = candidate;
	return Result(SG_COMPOUND_HOOK_LIVE_RUNNING);
}

sg_compound_hook_live_result_t SG_CompoundHookLiveTouch(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, int trigger_key,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, int frame_serial)
{
	STUB_CHECK(state == &bot.compound_hook_live && host && pose && observation);
	STUB_CHECK(trigger_key == mechanism.trigger_key && frame_serial == expected_frame);
	touch_calls++;
	state->outer.phase = SG_COMPOUND_TOUCHED;
	state->touch_frame_serial = frame_serial;
	return Result(SG_COMPOUND_HOOK_LIVE_RUNNING);
}

sg_compound_hook_live_result_t SG_CompoundHookLiveActivate(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, int trigger_key,
	int mover_key, int frame_serial)
{
	STUB_CHECK(state == &bot.compound_hook_live && host);
	STUB_CHECK(trigger_key == mechanism.trigger_key &&
	    mover_key == mechanism.mover_key && frame_serial == expected_frame &&
	    frame_serial == state->touch_frame_serial);
	activate_calls++;
	state->outer.phase = SG_COMPOUND_OPENING;
	return Result(SG_COMPOUND_HOOK_LIVE_RUNNING);
}

sg_compound_hook_live_result_t SG_CompoundHookLiveRecover(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z)
{
	STUB_CHECK(state == &bot.compound_hook_live && host && pose && observation);
	STUB_CHECK(old_frame_z == client.oldvelocity[2]);
	recover_calls++;
	return Result(SG_COMPOUND_HOOK_LIVE_RECOVERING);
}

sg_compound_hook_live_result_t SG_CompoundHookLivePreStep(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command)
{
	STUB_CHECK(state == &bot.compound_hook_live && host && pose && observation &&
	    command);
	prestep_calls++;
	memset(command, 0, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
	return Result(SG_COMPOUND_HOOK_LIVE_RECOVERING);
}

static void FixtureInit(void)
{
	memset(&rune_fixture, 0, sizeof(rune_fixture));
	memset(&binding, 0, sizeof(binding));
	memset(&mechanism, 0, sizeof(mechanism));
	memset(entities, 0, sizeof(entities));
	memset(&client, 0, sizeof(client));
	memset(&bot, 0, sizeof(bot));
	binding.link_index = 3U;
	binding.link.action = RL_DOOR_HOOK;
	binding.destination_seed.origin[0] = 64.0f;
	mechanism.trigger_key = 4;
	mechanism.mover_key = 2;
	entities[0].inuse = true;
	entities[1].inuse = true;
	entities[1].s.number = 1;
	entities[1].client = &client;
	entities[1].health = 100;
	entities[1].deadflag = DEAD_NO;
	entities[1].groundentity = &entities[0];
	entities[1].s.origin[0] = 8.0f;
	entities[1].velocity[0] = 1.0f;
	entities[2].inuse = true; entities[2].s.number = 2;
	entities[4].inuse = true; entities[4].s.number = 4;
	entities[6].inuse = true; entities[6].s.number = 6;
	entities[6].s.origin[0] = 64.0f;
	bot.active = true;
	bot.ent = &entities[1];
	bot.compound_hook_live.swim_link = -1;
	bot.compound_hook_live.hook_link = -1;
	acquire_result = SG_COMPOUND_GUARD_OK;
	outside_result = SG_COMPOUND_GUARD_OK;
	release_result = SG_COMPOUND_GUARD_OK;
	orphan_result = SG_COMPOUND_GUARD_OK;
	hook_observation = SG_COMPOUND_GUARD_NO;
	hook_absent = SG_COMPOUND_GUARD_YES;
	events_idle = true;
	latched_door = false;
	latched_contaminated = false;
	at_top = true;
	events_reset_calls = abort_begin_calls = abort_end_calls = 0;
	physical_abort_calls = 0;
	touch_calls = activate_calls = recover_calls = prestep_calls = 0;
	stub_failed = 0;
	expected_frame = 17;
}

static int HostFixture(void)
{
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_snapshot_t snapshot;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;

	FixtureInit();
	CHECK(SG_CompoundHookGameHost(&bot, &host));
	CHECK(host.bind && host.acquire && host.authorize && host.hold_open &&
	    host.body_clear && host.bolt_clear && host.release && host.orphan &&
	    host.abort_bolt && host.source_checkpoint && host.suffix_checkpoint &&
	    host.event_authorize && host.sweep_segment && host.hook_shadow);
	CHECK(host.bind(&bot, binding.link_index, &snapshot) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	CHECK(SG_CompoundHookGamePose(bot.ent, &pose));
	CHECK(SG_CompoundHookGameObservation(&bot, bot.ent, &observation));
	CHECK(host.source_checkpoint(&bot, &snapshot, &pose, &observation) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	CHECK(host.suffix_checkpoint(&bot, &snapshot, &pose, &observation) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	pose.origin[0] += 0.125f;
	CHECK(host.source_checkpoint(&bot, &snapshot, &pose, &observation) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	CHECK(SG_CompoundHookGamePose(bot.ent, &pose));
	pose.velocity[0] += 0.125f;
	CHECK(host.suffix_checkpoint(&bot, &snapshot, &pose, &observation) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	CHECK(!stub_failed);
	return 1;
}

static int TransactionFixture(void)
{
	sg_compound_hook_live_result_t result;
	usercmd_t command;

	FixtureInit();
	bot.compound_drop_live.guard_owned = true;
	CHECK(!SG_CompoundHookGameIdleAdmission(&bot));
	CHECK(SG_CompoundHookGameBegin(&bot, binding.link_index).outcome ==
	    SG_COMPOUND_HOOK_LIVE_REJECTED);
	bot.compound_drop_live.guard_owned = false;
	acquire_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	result = SG_CompoundHookGameBegin(&bot, binding.link_index);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_WAIT);
	CHECK(!bot.compound_hook_game.angle_bias_valid);
	CHECK(!bot.compound_hook_live.guard_owned &&
	    bot.compound_hook_live.outer.phase == SG_COMPOUND_NONE);
	CHECK(SG_CompoundHookGameIdleAdmission(&bot));
	acquire_result = SG_COMPOUND_GUARD_OK;
	CHECK(SG_CompoundHookGameBegin(&bot, binding.link_index).outcome ==
	    SG_COMPOUND_HOOK_LIVE_RUNNING);
	CHECK(SG_CompoundHookGameAuthorizeTouch(&bot, &entities[4], bot.ent,
	    expected_frame) == SG_COMPOUND_HOOK_GAME_ACCEPTED);
	CHECK(SG_CompoundHookGameAuthorizeActivation(&bot, &entities[4],
	    &entities[2], bot.ent, expected_frame) ==
	    SG_COMPOUND_HOOK_GAME_ACCEPTED);
	CHECK(touch_calls == 1 && activate_calls == 1);
	memset(&command, 0, sizeof(command));
	result = SG_CompoundHookGameRecoverOwnedFailure(&bot, &command);
	CHECK(result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING);
	CHECK(recover_calls == 1 && prestep_calls == 1 &&
	    command.msec == SG_REPLAY_STEP_MS);
	CHECK(!stub_failed);
	return 1;
}

#define IDLE_BLOCK(statement) do { \
	FixtureInit(); \
	statement; \
	CHECK(!SG_CompoundHookGameIdleAdmission(&bot)); \
} while (0)

static int IdleFixture(void)
{
	FixtureInit();
	CHECK(SG_CompoundHookGameIdleAdmission(&bot));
	IDLE_BLOCK(bot.compound_hook_live.guard_owned = true);
	IDLE_BLOCK(bot.compound_hook_live.local_owned = true);
	IDLE_BLOCK(bot.compound_hook_live.outer.phase = SG_COMPOUND_APPROACH);
	IDLE_BLOCK(bot.compound_hook_live.swim_active = true);
	IDLE_BLOCK(bot.compound_hook_live.hook_active = true);
	IDLE_BLOCK(bot.compound_hook_live.bolt_linked = true);
	IDLE_BLOCK(bot.compound_hook_live.command_pending = true);
	IDLE_BLOCK(bot.compound_hook_live.command_approved = true);
	IDLE_BLOCK(bot.compound_hook_live.aborted_command_pending = true);
	IDLE_BLOCK(bot.compound_hook_live.recovering = true);
	IDLE_BLOCK(bot.compound_hook_game.angle_bias_valid = true);
	IDLE_BLOCK(events_idle = false);
	IDLE_BLOCK(bot.compound_drop_live.guard_owned = true);
	IDLE_BLOCK(bot.compound_drop_live.outer.phase = SG_COMPOUND_APPROACH);
	IDLE_BLOCK(bot.compound_drop_live.drop_active = true);
	IDLE_BLOCK(bot.compound_drop_live.command_pending = true);
	IDLE_BLOCK(bot.compound_drop_live.recovering = true);
	IDLE_BLOCK(bot.hook_phase = 1);
	IDLE_BLOCK(bot.hook_replay_active = true);
	IDLE_BLOCK(bot.rj_phase = 1);
	IDLE_BLOCK(bot.nade_phase = 1);
	IDLE_BLOCK(bot.jump_started = true);
	IDLE_BLOCK(bot.drop_started = true);
	IDLE_BLOCK(bot.drop_replay_active = true);
	IDLE_BLOCK(bot.swim_replay_active = true);
	IDLE_BLOCK(bot.swim_validated = true);
	IDLE_BLOCK(bot.declared_started = true);
	IDLE_BLOCK(bot.declared_touched = true);
	IDLE_BLOCK(bot.declared_triggered = true);
	IDLE_BLOCK(bot.declared_activated = true);
	IDLE_BLOCK(bot.declared_door_retreat = true);
	IDLE_BLOCK(bot.declared_guard_paused = true);
	IDLE_BLOCK(client.hookstate = 1);
	IDLE_BLOCK(client.hook = &entities[6]);
	IDLE_BLOCK(bot.active = false);
	IDLE_BLOCK(entities[1].inuse = false);
	IDLE_BLOCK(entities[1].health = 0);
	IDLE_BLOCK(entities[1].deadflag = DEAD_DEAD);
	CHECK(!stub_failed);
	return 1;
}

static int SafetyFixture(void)
{
	sg_replay_observation_t observation;

	FixtureInit();
	latched_door = true;
	latched_contaminated = true;
	events_idle = false;
	CHECK(SG_CompoundHookGameObservation(&bot, bot.ent, &observation));
	CHECK(observation.door_passed && observation.contaminated);
	CHECK(latched_door && latched_contaminated);
	CHECK(SG_CompoundHookGameObservation(&bot, bot.ent, &observation));
	CHECK(observation.door_passed && observation.contaminated);
	CHECK(SG_CompoundHookGameTakeObservation(&bot, bot.ent, &observation));
	CHECK(observation.door_passed && observation.contaminated);
	CHECK(!latched_door && !latched_contaminated);
	CHECK(SG_CompoundHookGameTakeObservation(&bot, bot.ent, &observation));
	CHECK(!observation.door_passed && !observation.contaminated);
	CHECK(!stub_failed);
	return 1;
}

static int ClearFixture(void)
{
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_snapshot_t snapshot;
	sg_compound_hook_live_bolt_t bolt = { 6, 55U };
	sg_compound_hook_live_sweep_t sweep;
	vec3_t inside = { 8.0f, 0.0f, 0.0f };
	vec3_t outside = { 64.0f, 0.0f, 0.0f };

	FixtureInit();
	CHECK(SG_CompoundHookGameHost(&bot, &host));
	CHECK(host.bind(&bot, binding.link_index, &snapshot) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	CHECK(host.body_clear(&bot, &snapshot, NULL) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	outside_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	CHECK(host.body_clear(&bot, &snapshot, NULL) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	CHECK(host.bolt_clear(&bot, &snapshot, NULL) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	hook_absent = SG_COMPOUND_GUARD_NO;
	CHECK(host.bolt_clear(&bot, &snapshot, NULL) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	hook_observation = SG_COMPOUND_GUARD_YES;
	client.hook = &entities[6]; client.hookstate = 1;
	CHECK(host.bolt_clear(&bot, &snapshot, &bolt) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	CHECK(host.sweep_segment(&bot, &snapshot, inside, outside, &sweep) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	CHECK(!sweep.start_outside && sweep.end_outside && sweep.crossed);
	CHECK(host.abort_bolt(&bot, &snapshot, &bolt) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	CHECK(abort_begin_calls == 1 && abort_end_calls == 1 &&
	    physical_abort_calls == 1);
	CHECK(host.abort_bolt(&bot, &snapshot, &bolt) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	CHECK(abort_begin_calls == 2 && abort_end_calls == 2 &&
	    physical_abort_calls == 1);
	CHECK(!stub_failed);
	return 1;
}

static int TerminalFixture(void)
{
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_snapshot_t snapshot;
	sg_compound_hook_live_bolt_t exact_bolt = { 6, 55U };
	sg_compound_hook_live_bolt_t wrong_generation = { 6, 56U };

	FixtureInit();
	CHECK(SG_CompoundHookGameHost(&bot, &host));
	CHECK(host.bind(&bot, binding.link_index, &snapshot) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	bot.compound_hook_game.angle_bias_valid = true; events_idle = false;
	CHECK(host.release(&bot, &snapshot) == SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	CHECK(!bot.compound_hook_game.angle_bias_valid && events_idle &&
	    events_reset_calls == 1);
	bot.compound_hook_game.angle_bias_valid = true; events_idle = false;
	release_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	CHECK(host.release(&bot, &snapshot) == SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	CHECK(bot.compound_hook_game.angle_bias_valid && !events_idle);
	release_result = SG_COMPOUND_GUARD_OK;
	hook_absent = SG_COMPOUND_GUARD_NO;
	CHECK(host.orphan(&bot, &snapshot, NULL) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	CHECK(bot.compound_hook_game.angle_bias_valid && !events_idle);
	hook_absent = SG_COMPOUND_GUARD_YES;
	CHECK(host.orphan(&bot, &snapshot, &wrong_generation) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	hook_observation = SG_COMPOUND_GUARD_NO;
	CHECK(host.orphan(&bot, &snapshot, &exact_bolt) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	CHECK(bot.compound_hook_game.angle_bias_valid && !events_idle);
	CHECK(host.orphan(&bot, &snapshot, NULL) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED);
	CHECK(!bot.compound_hook_game.angle_bias_valid && events_idle);
	bot.compound_hook_game.angle_bias_valid = true; events_idle = false;
	orphan_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	CHECK(host.orphan(&bot, &snapshot, NULL) ==
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED);
	CHECK(bot.compound_hook_game.angle_bias_valid && !events_idle);
	CHECK(!stub_failed);
	return 1;
}

static int CurrentFixture(void)
{
	const sg_compound_publication_binding_t *found_binding;
	const sg_compound_world_preopen_t *found_mechanism;
	sg_compound_hook_live_snapshot_t snapshot;
	edict_t *member;

	FixtureInit();
	snapshot.binding = binding;
	snapshot.trigger_key = mechanism.trigger_key;
	snapshot.mover_key = mechanism.mover_key;
	CHECK(SG_CompoundHookGameCurrent(&bot, &snapshot, &found_binding,
	    &found_mechanism, &member));
	CHECK(found_binding == &binding && found_mechanism == &mechanism &&
	    member == &entities[2]);
	CHECK(SG_CompoundHookGameAtTop(&bot, &snapshot));
	at_top = false;
	CHECK(!SG_CompoundHookGameAtTop(&bot, &snapshot));
	CHECK(!stub_failed);
	return 1;
}

int main(void)
{
	if (!HostFixture() || !TransactionFixture() || !IdleFixture() ||
	    !SafetyFixture() ||
	    !ClearFixture() || !TerminalFixture() || !CurrentFixture())
		return 1;
	puts("sg_compound_hook_game_test: ok");
	return 0;
}
