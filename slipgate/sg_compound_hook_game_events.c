#ifdef SG_COMPOUND_HOOK_GAME_EVENTS_TEST
#include "../tests/sg_compound_hook_game_events_fixture.h"
#else
#include "../g_local.h"
#endif

#include <string.h>

#ifndef SG_COMPOUND_HOOK_GAME_EVENTS_TEST
#include "sg_local.h"
#endif
#include "sg_bot.h"
#include "sg_compound_guard_game.h"
#ifndef SG_COMPOUND_HOOK_GAME_EVENTS_TEST
#include "sg_compound_hook_game.h"
#endif
#include "sg_compound_hook_game_events.h"

static sg_compound_hook_live_result_t HookGameEventResult(
	sg_compound_hook_live_outcome_t outcome,
	sg_compound_hook_live_failure_t failure)
{
	sg_compound_hook_live_result_t result;

	memset(&result, 0, sizeof(result));
	result.outcome = outcome;
	result.failure = failure;
	result.replay_reason = failure == SG_COMPOUND_HOOK_LIVE_FAILURE_NONE ?
	    SG_REPLAY_REASON_NONE : SG_REPLAY_REASON_INVALID_STATE;
	return result;
}

static sg_bot_t *HookGameBotForClient(const edict_t *client)
{
	int slot;

	for (slot = 0; slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent == client)
			return &sg_bots[slot];
	return NULL;
}

static int HookGameOwned(const sg_bot_t *bot)
{
	return bot && bot->compound_hook_live.guard_owned &&
	       bot->compound_hook_live.local_owned &&
	       bot->compound_hook_live.outer.phase != SG_COMPOUND_NONE;
}

static int HookGameBoltEqual(const sg_compound_hook_game_events_t *events,
	const sg_compound_hook_live_bolt_t *bolt)
{
	return events && events->bolt_valid && bolt && bolt->key > 0 &&
	       bolt->generation != 0U &&
	       events->bolt_subject.kind == SG_MOVER_SUBJECT_HOOK_BOLT &&
	       events->bolt_subject.edict_key == bolt->key &&
	       events->bolt_subject.generation == bolt->generation;
}

static void HookGameBoltIdentity(
	const sg_compound_hook_game_events_t *events,
	sg_compound_hook_live_bolt_t *bolt)
{
	memset(bolt, 0, sizeof(*bolt));
	if (!events || !events->bolt_valid)
		return;
	bolt->key = events->bolt_subject.edict_key;
	bolt->generation = events->bolt_subject.generation;
}

static int HookGameVectorEqual(const vec3_t left, const vec3_t right)
{
	return left && right && memcmp(left, right, sizeof(vec3_t)) == 0;
}

static int HookGameSnapshotEqual(const sg_bot_t *bot,
	const sg_compound_hook_live_snapshot_t *snapshot)
{
	return bot && snapshot &&
	       memcmp(snapshot, &bot->compound_hook_live.snapshot,
	           sizeof(*snapshot)) == 0;
}

static int HookGameBite(const edict_t *bolt, vec3_t bite)
{
	if (!bolt || !bite)
		return 0;
	if (bolt->hook_target)
		VectorAdd(bolt->hook_target->absmin, bolt->hook_offset, bite);
	else
		VectorCopy(bolt->s.origin, bite);
	return true;
}

static int HookGameBiteValid(const sg_bot_t *bot, const edict_t *bolt)
{
	vec3_t bite;

	return bot && HookGameBite(bolt, bite) &&
	       HookGameVectorEqual(bite, bot->compound_hook_live.hook_spec.bite);
}

static int HookGameLaunchValid(const sg_bot_t *bot, const edict_t *bolt)
{
	vec3_t view, forward, right, muzzle, velocity;
	const edict_t *client;

	if (!bot || !(client = bot->ent) || !client->client || !bolt ||
	    bolt->owner != client || bolt->movetype != MOVETYPE_FLYMISSILE ||
	    bolt->clipmask != MASK_SHOT || bolt->solid != SOLID_BBOX)
		return 0;
	VectorCopy(bot->compound_hook_live.hook_spec.view_angles, view);
	AngleVectors(view, forward, right, NULL);
	CTF_HookMuzzle(client->s.origin, client->viewheight,
	    client->client->pers.hand, forward, right, muzzle);
	VectorScale(forward, RUNE_HOOK_BOLT_SPEED, velocity);
	return HookGameVectorEqual(bolt->s.origin, muzzle) &&
	       HookGameVectorEqual(bolt->velocity, velocity);
}

static int HookGamePullValid(const sg_bot_t *bot, const edict_t *bolt)
{
	vec3_t bite, view, forward, right, muzzle, expected;
	const edict_t *client;

	if (!bot || !(client = bot->ent) || !client->client || !bolt ||
	    client->client->hookstate != 2 || !HookGameBite(bolt, bite) ||
	    !HookGameVectorEqual(bite, bot->compound_hook_live.hook_spec.bite) ||
	    !HookGameVectorEqual(client->client->v_angle,
	        bot->compound_hook_live.hook_spec.view_angles))
		return 0;
	VectorCopy(bot->compound_hook_live.hook_spec.view_angles, view);
	AngleVectors(view, forward, right, NULL);
	CTF_HookMuzzle(client->s.origin, client->viewheight,
	    client->client->pers.hand, forward, right, muzzle);
	(void)CTF_HookPullVelocity(muzzle, bite, expected);
	return HookGameVectorEqual(client->velocity, expected) &&
	       HookGameVectorEqual(client->client->oldvelocity, expected);
}

static sg_compound_guard_observation_t HookGameObserveBolt(
	sg_bot_t *bot, edict_t **bolt_out)
{
	if (bolt_out)
		*bolt_out = NULL;
	if (!bot || !bolt_out || !bot->compound_hook_events.bolt_valid)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	return SG_CompoundGuardGameHookObserve(bot->ent,
	    &bot->compound_hook_events.bolt_subject, bolt_out);
}

static int HookGameCurrentBolt(sg_bot_t *bot, const edict_t *expected,
	edict_t **bolt_out)
{
	edict_t *bolt = NULL;

	if (bolt_out)
		*bolt_out = NULL;
	if (!bot || !expected || !bolt_out ||
	    !SG_CompoundHookGameAtTop(bot, &bot->compound_hook_live.snapshot) ||
	    HookGameObserveBolt(bot, &bolt) != SG_COMPOUND_GUARD_YES ||
	    bolt != expected || !bot->ent || !bot->ent->client ||
	    bot->ent->client->hook != bolt)
		return 0;
	*bolt_out = bolt;
	return 1;
}

void SG_CompoundHookGameEventsReset(sg_bot_t *bot)
{
	if (bot)
		memset(&bot->compound_hook_events, 0,
		    sizeof(bot->compound_hook_events));
}

qboolean SG_CompoundHookGameEventsIdle(const sg_bot_t *bot)
{
	static const sg_compound_hook_game_events_t idle =
	    SG_COMPOUND_HOOK_GAME_EVENTS_INITIALIZER;

	return bot && memcmp(&bot->compound_hook_events, &idle, sizeof(idle)) == 0;
}

void SG_CompoundHookGameObserveSafety(sg_bot_t *bot,
	qboolean door_passed, qboolean contaminated)
{
	if (!bot)
		return;
	if (door_passed)
		bot->compound_hook_events.door_passed = true;
	if (contaminated)
		bot->compound_hook_events.contaminated = true;
}

qboolean SG_CompoundHookGamePeekSafety(const sg_bot_t *bot,
	qboolean *door_passed, qboolean *contaminated)
{
	if (door_passed)
		*door_passed = false;
	if (contaminated)
		*contaminated = false;
	if (!bot || !door_passed || !contaminated)
		return false;
	*door_passed = bot->compound_hook_events.door_passed;
	*contaminated = bot->compound_hook_events.contaminated;
	return true;
}

qboolean SG_CompoundHookGameTakeSafety(sg_bot_t *bot,
	qboolean *door_passed, qboolean *contaminated)
{
	if (!SG_CompoundHookGamePeekSafety(bot, door_passed, contaminated))
		return false;
	bot->compound_hook_events.door_passed = false;
	bot->compound_hook_events.contaminated = false;
	return true;
}

sg_compound_hook_live_result_t SG_CompoundHookGameLinked(
	edict_t *client, edict_t *bolt, const sg_mover_subject_t *subject)
{
	sg_compound_hook_live_host_t host;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_compound_hook_live_bolt_t identity;
	sg_bot_t *bot = HookGameBotForClient(client);

	if (!HookGameOwned(bot))
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_IDLE,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_NONE);
	if (!bolt || !subject || !SG_MoverSubjectValid(subject) ||
	    subject->kind != SG_MOVER_SUBJECT_HOOK_BOLT ||
	    subject->edict_key != bolt->s.number || bolt->owner != client ||
	    !HookGameLaunchValid(bot, bolt) ||
	    bot->compound_hook_events.bolt_valid)
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	bot->compound_hook_events.bolt_subject = *subject;
	bot->compound_hook_events.bolt_valid = true;
	if (bot->compound_hook_live.outer.phase != SG_COMPOUND_TOP ||
	    !SG_CompoundHookGameAtTop(bot, &bot->compound_hook_live.snapshot) ||
	    !SG_CompoundHookGameHost(bot, &host) ||
	    !SG_CompoundHookGamePose(client, &pose) ||
	    !SG_CompoundHookGameObservation(bot, client, &observation))
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	HookGameBoltIdentity(&bot->compound_hook_events, &identity);
	return SG_CompoundHookLiveLinked(&bot->compound_hook_live, &host,
	    &identity, level.framenum, &pose, &observation);
}

sg_compound_hook_game_event_gate_t SG_CompoundHookGameAttachWillApply(
	edict_t *bolt,
	edict_t *target, const csurface_t *surface)
{
	sg_bot_t *bot;
	edict_t *current;

	if (!bolt || !(bot = HookGameBotForClient(bolt->owner)) ||
	    !HookGameOwned(bot))
		return SG_COMPOUND_HOOK_GAME_EVENT_BYPASS;
	if (!target || target != g_edicts || !target->classname ||
	    strcmp(target->classname, "worldspawn") != 0 ||
	    !bot->compound_hook_events.bolt_valid ||
	    bot->compound_hook_events.attached ||
	    bot->compound_hook_events.attach_pending || target == bolt->owner ||
	    target->deadflag || (surface && (surface->flags & SURF_SKY)) ||
	    !HookGameCurrentBolt(bot, bolt, &current) || current != bolt ||
	    !HookGameBiteValid(bot, bolt))
		return SG_COMPOUND_HOOK_GAME_EVENT_DENIED;
	bot->compound_hook_events.attach_target = target;
	bot->compound_hook_events.attach_pending = true;
	return SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED;
}

sg_compound_hook_live_result_t SG_CompoundHookGameAttached(edict_t *bolt)
{
	sg_compound_hook_live_host_t host;
	sg_replay_pose_t pose;
	sg_compound_hook_live_bolt_t identity;
	sg_bot_t *bot;
	edict_t *current;
	edict_t *target;

	bot = bolt ? HookGameBotForClient(bolt->owner) : NULL;
	if (!HookGameOwned(bot))
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_IDLE,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_NONE);
	if (!bolt ||
	    !bot->compound_hook_events.attach_pending ||
	    !(target = bot->compound_hook_events.attach_target) ||
	    !HookGameCurrentBolt(bot, bolt, &current) || current != bolt ||
	    bolt->hook_target != target || bolt->solid != SOLID_TRIGGER ||
	    !HookGameVectorEqual(bolt->velocity, vec3_origin) ||
	    bolt->owner->client->hookstate != 2 ||
	    !HookGameBiteValid(bot, bolt) ||
	    !SG_CompoundHookGameHost(bot, &host) ||
	    !SG_CompoundHookGamePose(bot->ent, &pose))
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	bot->compound_hook_events.attach_pending = false;
	bot->compound_hook_events.attach_target = NULL;
	bot->compound_hook_events.attached = true;
	HookGameBoltIdentity(&bot->compound_hook_events, &identity);
	return SG_CompoundHookLiveAttached(&bot->compound_hook_live, &host,
	    &identity, level.framenum, &pose);
}

sg_compound_hook_live_result_t SG_CompoundHookGamePullApplied(
	edict_t *client, edict_t *bolt)
{
	sg_compound_hook_live_host_t host;
	sg_replay_pose_t pose;
	sg_compound_hook_live_bolt_t identity;
	sg_bot_t *bot = HookGameBotForClient(client);
	edict_t *current;

	if (!HookGameOwned(bot))
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_IDLE,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_NONE);
	if (!bolt || !bot->compound_hook_events.attached ||
	    bot->compound_hook_events.abort_pending ||
	    !HookGameCurrentBolt(bot, bolt, &current) || current != bolt ||
	    !HookGamePullValid(bot, bolt))
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	if (!SG_CompoundHookGameHost(bot, &host) ||
	    !SG_CompoundHookGamePose(client, &pose))
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	HookGameBoltIdentity(&bot->compound_hook_events, &identity);
	return SG_CompoundHookLivePullApplied(&bot->compound_hook_live, &host,
	    &identity, level.framenum, &pose);
}

sg_compound_hook_game_event_gate_t SG_CompoundHookGameReleaseRequested(
	edict_t *client,
	edict_t *bolt)
{
	sg_bot_t *bot = HookGameBotForClient(client);
	edict_t *current;

	if (!HookGameOwned(bot))
		return SG_COMPOUND_HOOK_GAME_EVENT_BYPASS;
	if (!bolt || !bot->compound_hook_events.attached ||
	    bot->compound_hook_events.release_requested ||
	    !bot->compound_hook_live.hook.release_requested ||
	    !HookGameCurrentBolt(bot, bolt, &current) || current != bolt ||
	    !HookGameBiteValid(bot, bolt))
		return SG_COMPOUND_HOOK_GAME_EVENT_DENIED;
	bot->compound_hook_events.release_requested = true;
	return SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED;
}

sg_compound_hook_game_event_gate_t SG_CompoundHookGameAbortBegin(
	edict_t *client, edict_t *bolt)
{
	sg_bot_t *bot = HookGameBotForClient(client);
	edict_t *current;

	if (!HookGameOwned(bot))
		return SG_COMPOUND_HOOK_GAME_EVENT_BYPASS;
	if (!bolt || bot->compound_hook_events.abort_consumed ||
	    bot->compound_hook_events.abort_receipt ||
	    !HookGameCurrentBolt(bot, bolt, &current) || current != bolt)
		return SG_COMPOUND_HOOK_GAME_EVENT_DENIED;
	if (bot->compound_hook_events.abort_pending)
		return bot->compound_hook_events.abort_bolt == bolt &&
		       !bot->compound_hook_events.abort_recovery ?
		       SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED :
		       SG_COMPOUND_HOOK_GAME_EVENT_DENIED;
	bot->compound_hook_events.abort_bolt = bolt;
	bot->compound_hook_events.abort_pending = true;
	bot->compound_hook_events.abort_recovery = false;
	return SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED;
}

static int HookGameFinishAbort(sg_bot_t *bot)
{
	edict_t *current = NULL;
	edict_t *bolt;

	if (bot && bot->compound_hook_events.abort_receipt &&
	    bot->compound_hook_events.bolt_evicted &&
	    !bot->compound_hook_events.abort_pending &&
	    !bot->compound_hook_events.abort_bolt)
		return 1;
	if (!bot || !bot->compound_hook_events.abort_pending ||
	    !(bolt = bot->compound_hook_events.abort_bolt))
		return 0;
	bot->compound_hook_events.abort_bolt = NULL;
	bot->compound_hook_events.abort_pending = false;
	bot->compound_hook_events.abort_consumed = true;
	if (HookGameObserveBolt(bot, &current) != SG_COMPOUND_GUARD_NO || current ||
	    !bot->ent || !bot->ent->client || bot->ent->client->hook ||
	    bot->ent->client->hookstate != 0 ||
	    !SG_CompoundHookGameAtTop(bot, &bot->compound_hook_live.snapshot) ||
	    SG_CompoundGuardGameBoltEvicted(bot->ent, bolt) !=
	        SG_COMPOUND_GUARD_OK)
		return 0;
	bot->compound_hook_events.abort_receipt = true;
	bot->compound_hook_events.bolt_evicted = true;
	return 1;
}

sg_compound_hook_live_result_t SG_CompoundHookGameAbortEnd(edict_t *client)
{
	sg_compound_hook_live_host_t host;
	sg_replay_pose_t pose;
	sg_compound_hook_live_bolt_t identity;
	sg_bot_t *bot = HookGameBotForClient(client);
	sg_compound_hook_live_result_t result;

	if (!HookGameOwned(bot))
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_IDLE,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_NONE);
	if (bot->compound_hook_events.abort_recovery ||
	    bot->compound_hook_events.release_applied || !HookGameFinishAbort(bot))
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	if (!SG_CompoundHookGameHost(bot, &host) ||
	    !SG_CompoundHookGamePose(client, &pose))
		return HookGameEventResult(SG_COMPOUND_HOOK_LIVE_RECOVERING,
		    SG_COMPOUND_HOOK_LIVE_FAILURE_IDENTITY);
	HookGameBoltIdentity(&bot->compound_hook_events, &identity);
	result = SG_CompoundHookLiveReleaseApplied(&bot->compound_hook_live,
	    &host, &identity, level.framenum, &pose);
	bot->compound_hook_events.release_applied = true;
	return result;
}

qboolean SG_CompoundHookGameRecoveryAbortBegin(sg_bot_t *bot,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	edict_t *current;

	if (!HookGameSnapshotEqual(bot, snapshot) ||
	    !HookGameBoltEqual(&bot->compound_hook_events, bolt))
		return false;
	if (bot->compound_hook_events.abort_receipt)
	{
		if (!bot->compound_hook_events.bolt_evicted ||
		    bot->compound_hook_events.abort_pending ||
		    bot->compound_hook_events.abort_bolt)
			return false;
		bot->compound_hook_events.abort_recovery = true;
		return true;
	}
	if (bot->compound_hook_events.abort_pending)
	{
		bot->compound_hook_events.abort_recovery = true;
		return bot->compound_hook_events.abort_bolt != NULL;
	}
	if (HookGameObserveBolt(bot, &current) != SG_COMPOUND_GUARD_YES ||
	    !current || bot->ent->client->hook != current ||
	    !SG_CompoundHookGameAtTop(bot, snapshot))
		return false;
	bot->compound_hook_events.abort_bolt = current;
	bot->compound_hook_events.abort_pending = true;
	bot->compound_hook_events.abort_recovery = true;
	return true;
}

qboolean SG_CompoundHookGameRecoveryAbortEnd(sg_bot_t *bot,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	return HookGameSnapshotEqual(bot, snapshot) &&
	       HookGameBoltEqual(&bot->compound_hook_events, bolt) &&
	       bot->compound_hook_events.abort_recovery &&
	       HookGameFinishAbort(bot);
}

sg_compound_hook_live_host_result_t SG_CompoundHookGameAuthorizeEvent(
	sg_bot_t *bot, const sg_compound_hook_live_snapshot_t *snapshot,
	sg_compound_hook_live_event_t event,
	const sg_compound_hook_live_bolt_t *bolt)
{
	edict_t *current = NULL;
	sg_compound_guard_observation_t observed;

	if (!HookGameSnapshotEqual(bot, snapshot) ||
	    !HookGameBoltEqual(&bot->compound_hook_events, bolt) ||
	    !SG_CompoundHookGameAtTop(bot, snapshot))
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	if (event == SG_COMPOUND_HOOK_LIVE_EVENT_LINKED)
		return bot->compound_hook_events.bolt_valid &&
		       !bot->compound_hook_events.attached ?
		    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
		    SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	observed = HookGameObserveBolt(bot, &current);
	if (event == SG_COMPOUND_HOOK_LIVE_EVENT_RELEASE)
		return observed == SG_COMPOUND_GUARD_NO && !current &&
			       bot->compound_hook_events.release_requested &&
			       bot->compound_hook_events.abort_receipt &&
			       bot->compound_hook_events.bolt_evicted &&
			       !bot->compound_hook_events.abort_recovery ?
			       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
			       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	if (observed != SG_COMPOUND_GUARD_YES || !current ||
	    bot->ent->client->hook != current || !HookGameBiteValid(bot, current))
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	if (event == SG_COMPOUND_HOOK_LIVE_EVENT_ATTACHED)
		return bot->compound_hook_events.attached &&
			       current->hook_target && current->solid == SOLID_TRIGGER &&
			       bot->ent->client->hookstate == 2 ?
			       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
			       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	if (event == SG_COMPOUND_HOOK_LIVE_EVENT_PULL)
		return bot->compound_hook_events.attached &&
		       bot->ent->client->hookstate == 2 &&
		       !bot->compound_hook_events.abort_pending &&
		       HookGamePullValid(bot, current) ?
			       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
			       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
}
