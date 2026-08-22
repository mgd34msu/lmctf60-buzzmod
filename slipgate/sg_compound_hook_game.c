#include "../g_local.h"
#include "../g_ctffunc.h"

#include <limits.h>
#include <string.h>

#include "sg_local.h"
#include "sg_bot.h"
#include "sg_compound_guard_game.h"
#include "sg_compound_hook_game.h"
#include "sg_compound_hook_game_events.h"
#include "sg_compound_world.h"
#include "sg_cvars.h"
#include "sg_hooks.h"
#include "sg_move.h"
#include "sg_util.h"

typedef struct compound_hook_game_current_s
{
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *mechanism;
	edict_t *member;
} compound_hook_game_current_t;

sg_compound_guard_observation_t SG_CompoundGuardGameHookObserve(
	edict_t *client, const sg_mover_subject_t *subject,
	edict_t **current_out);
sg_compound_guard_observation_t SG_CompoundGuardGameHookAbsent(
	edict_t *client);
static int CompoundHookGameResolve(sg_bot_t *bot,
	const sg_compound_hook_live_snapshot_t *snapshot,
	compound_hook_game_current_t *current)
{
	rune_t *rune;
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *mechanism;
	edict_t *member;

	if (current)
		memset(current, 0, sizeof(*current));
	if (!bot || !snapshot || !current || !bot->active || !bot->ent ||
	    !(rune = SG_Rune()) || !SG_RunePhysicsCompatible(rune) ||
	    !(binding = SG_CompoundPublicationBinding(rune,
	        snapshot->binding.link_index)) ||
	    binding->link.action != RL_DOOR_HOOK ||
	    memcmp(binding, &snapshot->binding, sizeof(*binding)) != 0 ||
	    !(mechanism = SG_CompoundPublicationMechanism(rune, binding)) ||
	    mechanism->trigger_key != snapshot->trigger_key ||
	    mechanism->mover_key != snapshot->mover_key ||
	    !SG_CompoundWorldResolvedMember(mechanism, &member) || !member)
		return 0;
	current->binding = binding;
	current->mechanism = mechanism;
	current->member = member;
	return 1;
}

qboolean SG_CompoundHookGameCurrent(sg_bot_t *bot,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_publication_binding_t **binding_out,
	const sg_compound_world_preopen_t **mechanism_out,
	edict_t **member_out)
{
	compound_hook_game_current_t current;

	if (binding_out)
		*binding_out = NULL;
	if (mechanism_out)
		*mechanism_out = NULL;
	if (member_out)
		*member_out = NULL;
	if (!binding_out || !mechanism_out || !member_out ||
	    !CompoundHookGameResolve(bot, snapshot, &current))
		return false;
	*binding_out = current.binding;
	*mechanism_out = current.mechanism;
	*member_out = current.member;
	return true;
}

qboolean SG_CompoundHookGameAtTop(sg_bot_t *bot,
	const sg_compound_hook_live_snapshot_t *snapshot)
{
	compound_hook_game_current_t current;

	return CompoundHookGameResolve(bot, snapshot, &current) &&
	       SG_CompoundWorldAtTopFor(current.mechanism,
	           SG_COMPOUND_HOLD_LEASE_MS);
}

static int CompoundHookGameCheckpoint(const edict_t *entity,
	sg_compound_publication_checkpoint_t *checkpoint)
{
	if (checkpoint)
		memset(checkpoint, 0, sizeof(*checkpoint));
	if (!entity || !entity->inuse || !entity->client || !checkpoint)
		return 0;
	checkpoint->pms = entity->client->ps.pmove;
	checkpoint->old_pms = entity->client->old_pmove;
	checkpoint->grounded = entity->groundentity != NULL;
	checkpoint->watertype = entity->watertype;
	checkpoint->waterlevel = entity->waterlevel;
	checkpoint->old_frame_z = entity->client->oldvelocity[2];
	return 1;
}

static int CompoundHookGamePoseMatches(const edict_t *entity,
	const sg_replay_pose_t *pose,
	const sg_compound_publication_checkpoint_t *live)
{
	return entity && pose && live &&
	       memcmp(&pose->pms, &live->pms, sizeof(pose->pms)) == 0 &&
	       memcmp(pose->origin, entity->s.origin,
	           sizeof(pose->origin)) == 0 &&
	       memcmp(pose->velocity, entity->velocity,
	           sizeof(pose->velocity)) == 0 &&
	       pose->grounded == live->grounded &&
	       pose->watertype == live->watertype &&
	       pose->waterlevel == live->waterlevel;
}

static sg_compound_hook_live_host_result_t CompoundHookGameBind(
	void *context, uint32_t link_index,
	sg_compound_hook_live_snapshot_t *snapshot)
{
	sg_bot_t *bot = context;
	rune_t *rune;
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *mechanism;
	edict_t *member;

	if (!snapshot)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	memset(snapshot, 0, sizeof(*snapshot));
	if (!bot || !bot->active || !bot->ent || !(rune = SG_Rune()) ||
	    !SG_RunePhysicsCompatible(rune) ||
	    !(binding = SG_CompoundPublicationBinding(rune, link_index)) ||
	    binding->link.action != RL_DOOR_HOOK ||
	    !(mechanism = SG_CompoundPublicationMechanism(rune, binding)) ||
	    !SG_CompoundWorldResolvedMember(mechanism, &member) || !member)
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	snapshot->binding = *binding;
	snapshot->trigger_key = mechanism->trigger_key;
	snapshot->mover_key = mechanism->mover_key;
	return snapshot->trigger_key > 0 && snapshot->mover_key > 0 ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
}

static sg_compound_hook_live_host_result_t CompoundHookGameSource(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_bot_t *bot = context;
	compound_hook_game_current_t current;
	sg_compound_publication_checkpoint_t live;
	sg_compound_publication_angle_bias_t bias;

	if (!observation || !CompoundHookGameResolve(bot, snapshot, &current) ||
	    !CompoundHookGameCheckpoint(bot->ent, &live) ||
	    !CompoundHookGamePoseMatches(bot->ent, pose, &live) ||
	    !SG_CompoundPublicationCaptureAngleBias(&snapshot->binding.source,
	        &live, &bias))
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	bot->compound_hook_game.angle_bias = bias;
	bot->compound_hook_game.angle_bias_valid = true;
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static sg_compound_hook_live_host_result_t CompoundHookGameSuffix(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation)
{
	sg_bot_t *bot = context;
	compound_hook_game_current_t current;
	sg_compound_publication_checkpoint_t live;

	if (!observation || !CompoundHookGameResolve(bot, snapshot, &current) ||
	    !bot->compound_hook_game.angle_bias_valid ||
	    !CompoundHookGameCheckpoint(bot->ent, &live) ||
	    !CompoundHookGamePoseMatches(bot->ent, pose, &live) ||
	    !SG_CompoundPublicationCheckpointMatches(&snapshot->binding.suffix,
	        &live, &bot->compound_hook_game.angle_bias))
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static sg_compound_hook_live_host_result_t CompoundHookGameAcquire(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot)
{
	sg_bot_t *bot = context;
	compound_hook_game_current_t current;
	sg_mover_key_t key;

	if (!CompoundHookGameResolve(bot, snapshot, &current) ||
	    snapshot->mover_key <= 0 || snapshot->mover_key > UINT16_MAX ||
	    snapshot->trigger_key <= 0)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	key = (sg_mover_key_t)snapshot->mover_key;
	return SG_CompoundGuardAcquireCompoundPreopen(&bot->compound_guard,
	    &key, 1U, (int)snapshot->binding.link_index,
	    (uint32_t)snapshot->trigger_key) == SG_COMPOUND_GUARD_OK ?
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_hook_live_host_result_t CompoundHookGameAuthorize(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot)
{
	sg_bot_t *bot = context;
	compound_hook_game_current_t current;
	sg_mover_key_t key;

	if (!CompoundHookGameResolve(bot, snapshot, &current) ||
	    snapshot->mover_key <= 0 || snapshot->mover_key > UINT16_MAX)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	key = (sg_mover_key_t)snapshot->mover_key;
	return SG_CompoundGuardAuthorize(&bot->compound_guard,
	    SG_MOVER_LAW_COMPOUND_PREOPEN, &key, 1U,
	    (int)snapshot->binding.link_index,
	    (uint32_t)snapshot->trigger_key) == SG_COMPOUND_GUARD_OK ?
	    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	    SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_hook_live_host_result_t CompoundHookGameHold(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot,
	int lease_ms)
{
	sg_bot_t *bot = context;
	compound_hook_game_current_t current;

	if (lease_ms != SG_COMPOUND_HOLD_LEASE_MS ||
	    !CompoundHookGameResolve(bot, snapshot, &current))
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	return SG_CompoundGuardMaintain(&bot->compound_guard) ==
	       SG_COMPOUND_GUARD_OK ? SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	                              SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_guard_observation_t CompoundHookGameObserveBolt(
	sg_bot_t *bot, const sg_compound_hook_live_bolt_t *bolt,
	edict_t **bolt_out)
{
	sg_mover_subject_t subject;

	if (bolt_out)
		*bolt_out = NULL;
	if (!bot || !bot->ent || !bolt || bolt->key <= 0 ||
	    bolt->generation == 0U || !bolt_out)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	memset(&subject, 0, sizeof(subject));
	subject.kind = SG_MOVER_SUBJECT_HOOK_BOLT;
	subject.edict_key = bolt->key;
	subject.generation = bolt->generation;
	return SG_CompoundGuardGameHookObserve(bot->ent, &subject, bolt_out);
}

static sg_compound_hook_live_host_result_t CompoundHookGameBodyClear(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	sg_bot_t *bot = context;
	compound_hook_game_current_t current;

	(void)bolt;
	return CompoundHookGameResolve(bot, snapshot, &current) &&
	       SG_CompoundGuardAllSubjectsOutside(&bot->compound_guard) ==
	           SG_COMPOUND_GUARD_OK ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_hook_live_host_result_t CompoundHookGameBoltClear(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	sg_bot_t *bot = context;
	compound_hook_game_current_t current;
	sg_compound_guard_observation_t observed;
	edict_t *entity;

	if (!CompoundHookGameResolve(bot, snapshot, &current))
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	if (!bolt)
		return SG_CompoundGuardGameHookAbsent(bot->ent) ==
		       SG_COMPOUND_GUARD_YES ?
			       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
			       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	observed = CompoundHookGameObserveBolt(bot, bolt, &entity);
	if (observed == SG_COMPOUND_GUARD_NO)
		return bot->compound_hook_live.bolt_abort_applied ?
			       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
			       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	if (observed != SG_COMPOUND_GUARD_YES || !entity)
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	return SG_CompoundWorldOutsideSweep(current.mechanism,
	           entity->s.origin) ?
	       SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
}

static sg_compound_hook_live_host_result_t CompoundHookGameRelease(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot)
{
	sg_bot_t *bot = context;
	compound_hook_game_current_t current;

	if (!CompoundHookGameResolve(bot, snapshot, &current))
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	if (SG_CompoundGuardReleaseProvedClear(&bot->compound_guard) !=
	    SG_COMPOUND_GUARD_OK)
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	memset(&bot->compound_hook_game, 0, sizeof(bot->compound_hook_game));
	SG_CompoundHookGameEventsReset(bot);
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static sg_compound_hook_live_host_result_t CompoundHookGameOrphan(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	sg_bot_t *bot = context;
	compound_hook_game_current_t current;
	sg_compound_guard_observation_t observed;
	edict_t *entity;

	if (!CompoundHookGameResolve(bot, snapshot, &current) ||
	    (bolt && (bolt->key <= 0 || bolt->generation == 0U)))
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	if (bolt)
	{
		observed = CompoundHookGameObserveBolt(bot, bolt, &entity);
		if (observed != SG_COMPOUND_GUARD_YES || !entity ||
		    bot->ent->client->hook != entity)
			return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	}
	else if (SG_CompoundGuardGameHookAbsent(bot->ent) !=
	         SG_COMPOUND_GUARD_YES)
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	if (SG_CompoundGuardOrphan(&bot->compound_guard,
	        bolt ? bolt->key : 0) != SG_COMPOUND_GUARD_OK)
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	memset(&bot->compound_hook_game, 0, sizeof(bot->compound_hook_game));
	SG_CompoundHookGameEventsReset(bot);
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static sg_compound_hook_live_host_result_t CompoundHookGameAbort(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_hook_live_bolt_t *bolt)
{
	sg_bot_t *bot = context;
	compound_hook_game_current_t current;
	sg_compound_guard_observation_t observed;
	edict_t *entity;

	if (!CompoundHookGameResolve(bot, snapshot, &current) || !bolt ||
	    bolt->key <= 0 || bolt->generation == 0U ||
	    !SG_CompoundHookGameRecoveryAbortBegin(bot, snapshot, bolt))
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	observed = CompoundHookGameObserveBolt(bot, bolt, &entity);
	if (observed == SG_COMPOUND_GUARD_NO && !entity)
		return SG_CompoundHookGameRecoveryAbortEnd(bot, snapshot, bolt) ?
		    SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED :
		    SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	if (observed != SG_COMPOUND_GUARD_YES || !entity ||
	    bot->ent->client->hook != entity)
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	ctf_hook_abort(bot->ent);
	observed = CompoundHookGameObserveBolt(bot, bolt, &entity);
	if (observed != SG_COMPOUND_GUARD_NO || entity ||
	    bot->ent->client->hook != NULL || bot->ent->client->hookstate != 0 ||
	    SG_CompoundGuardGameHookAbsent(bot->ent) != SG_COMPOUND_GUARD_YES ||
	    !SG_CompoundHookGameRecoveryAbortEnd(bot, snapshot, bolt))
		return SG_COMPOUND_HOOK_LIVE_HOST_DENIED;
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static sg_compound_hook_live_host_result_t CompoundHookGameEvent(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot,
	sg_compound_hook_live_event_t event,
	const sg_compound_hook_live_bolt_t *bolt)
{
	return SG_CompoundHookGameAuthorizeEvent(context, snapshot, event, bolt);
}

static sg_compound_hook_live_host_result_t CompoundHookGameSweep(
	void *context, const sg_compound_hook_live_snapshot_t *snapshot,
	const vec3_t start, const vec3_t end,
	sg_compound_hook_live_sweep_t *sweep)
{
	compound_hook_game_current_t current;

	if (sweep)
		memset(sweep, 0, sizeof(*sweep));
	if (!start || !end || !sweep ||
	    !CompoundHookGameResolve(context, snapshot, &current))
		return SG_COMPOUND_HOOK_LIVE_HOST_ERROR;
	sweep->start_outside =
	    SG_CompoundWorldOutsideSweep(current.mechanism, start);
	sweep->end_outside =
	    SG_CompoundWorldOutsideSweep(current.mechanism, end);
	sweep->crossed =
	    SG_CompoundWorldCrossesSweep(current.mechanism, start, end);
	return SG_COMPOUND_HOOK_LIVE_HOST_ACCEPTED;
}

static qboolean CompoundHookGameShadow(
	const sg_hook_replay_state_t *state, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command)
{
	sg_hook_replay_state_t copy;

	if (!state || !pose || !observation || !command)
		return false;
	if (state->phase == SG_HOOK_REPLAY_WAIT_ATTACH)
		return SG_HookReplayFixedViewCommand(pose, state->spec.view_angles,
		    command);
	copy = *state;
	return SG_HookReplayPreStep(&copy, pose, observation, command) ==
	       SG_REPLAY_RUNNING;
}

void SG_CompoundHookGameReset(sg_bot_t *bot)
{
	if (!bot)
		return;
	memset(&bot->compound_hook_live, 0, sizeof(bot->compound_hook_live));
	bot->compound_hook_live.swim_link = -1;
	bot->compound_hook_live.hook_link = -1;
	memset(&bot->compound_hook_game, 0, sizeof(bot->compound_hook_game));
	SG_CompoundHookGameEventsReset(bot);
}

qboolean SG_CompoundHookGameHost(sg_bot_t *bot,
	sg_compound_hook_live_host_t *host)
{
	if (!bot || !host)
		return false;
	memset(host, 0, sizeof(*host));
	host->context = bot;
	host->bind = CompoundHookGameBind;
	host->acquire = CompoundHookGameAcquire;
	host->authorize = CompoundHookGameAuthorize;
	host->hold_open = CompoundHookGameHold;
	host->body_clear = CompoundHookGameBodyClear;
	host->bolt_clear = CompoundHookGameBoltClear;
	host->release = CompoundHookGameRelease;
	host->orphan = CompoundHookGameOrphan;
	host->abort_bolt = CompoundHookGameAbort;
	host->source_checkpoint = CompoundHookGameSource;
	host->suffix_checkpoint = CompoundHookGameSuffix;
	host->event_authorize = CompoundHookGameEvent;
	host->sweep_segment = CompoundHookGameSweep;
	host->hook_shadow = CompoundHookGameShadow;
	return true;
}

qboolean SG_CompoundHookGamePose(const edict_t *entity,
	sg_replay_pose_t *pose)
{
	if (!entity || !entity->inuse || !entity->client || !pose)
		return false;
	SG_DropLivePose(pose, &entity->client->ps.pmove, entity->s.origin,
	    entity->velocity, entity->groundentity != NULL, entity->watertype,
	    entity->waterlevel);
	return true;
}

static qboolean CompoundHookGameObservationMode(sg_bot_t *bot,
	const edict_t *entity, qboolean consume,
	sg_replay_observation_t *observation)
{
	compound_hook_game_current_t current;
	qboolean have_current;
	sg_compound_guard_observation_t observed;
	edict_t *bolt;
	vec3_t view, forward, right, muzzle, bite, velocity;

	if (observation)
		memset(observation, 0, sizeof(*observation));
	if (!bot || !entity || entity != bot->ent || !entity->client ||
	    !observation)
		return false;
	have_current = CompoundHookGameResolve(bot,
	    &bot->compound_hook_live.snapshot, &current);
	observation->ground_support_valid = entity->groundentity &&
	    (entity->groundentity == g_edicts ||
	     SG_ImmutableSupport(entity->groundentity));
	if (have_current)
		observation->contact_clear = SG_SupportedArrived(entity->s.origin,
		    current.binding->destination_seed.origin,
		    entity->groundentity != NULL, entity->watertype,
		    entity->waterlevel, (edict_t *)entity);
	SG_CompoundHookGameObserveSafety(bot, false,
	    entity->waterlevel > 0 &&
	        (entity->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)));
	if (!(consume ? SG_CompoundHookGameTakeSafety(bot,
	          &observation->door_passed, &observation->contaminated) :
	      SG_CompoundHookGamePeekSafety(bot, &observation->door_passed,
	          &observation->contaminated)))
		return false;
	if (!bot->compound_hook_live.bolt_linked)
		return true;
	observed = CompoundHookGameObserveBolt(bot,
	    &bot->compound_hook_live.bolt, &bolt);
	if (observed == SG_COMPOUND_GUARD_NO)
		return bot->compound_hook_live.bolt_abort_applied;
	if (observed != SG_COMPOUND_GUARD_YES || !bolt ||
	    entity->client->hook != bolt || !have_current)
		return false;
	if (entity->client->hookstate != 2)
		return true;
	VectorCopy(current.binding->hook_proof.spec.view_angles, view);
	AngleVectors(view, forward, right, NULL);
	CTF_HookMuzzle(entity->s.origin, entity->viewheight,
	    entity->client->pers.hand, forward, right, muzzle);
	if (bolt->hook_target)
		VectorAdd(bolt->hook_target->absmin, bolt->hook_offset, bite);
	else
		VectorCopy(bolt->s.origin, bite);
	observation->hook_rope_length =
	    CTF_HookPullVelocity(muzzle, bite, velocity);
	observation->hook_rope_valid = observation->hook_rope_length >= 0;
	return true;
}

qboolean SG_CompoundHookGameObservation(sg_bot_t *bot,
	const edict_t *entity, sg_replay_observation_t *observation)
{
	return CompoundHookGameObservationMode(bot, entity, false, observation);
}

qboolean SG_CompoundHookGameTakeObservation(sg_bot_t *bot,
	const edict_t *entity, sg_replay_observation_t *observation)
{
	return CompoundHookGameObservationMode(bot, entity, true, observation);
}

qboolean SG_CompoundHookGameIdleAdmission(const sg_bot_t *bot)
{
	const edict_t *entity;
	const sg_compound_hook_live_state_t *hook;

	if (!bot || !bot->active || !(entity = bot->ent) || !entity->inuse ||
	    !entity->client || entity->deadflag != DEAD_NO || entity->health <= 0)
		return false;
	hook = &bot->compound_hook_live;
	if (hook->guard_owned || hook->local_owned ||
	    hook->outer.phase != SG_COMPOUND_NONE || hook->swim_active ||
	    hook->hook_active || hook->bolt_linked || hook->command_pending ||
	    hook->command_approved || hook->aborted_command_pending ||
	    hook->recovering || bot->compound_hook_game.angle_bias_valid ||
	    !SG_CompoundHookGameEventsIdle(bot) ||
	    bot->compound_drop_live.guard_owned ||
	    bot->compound_drop_live.outer.phase != SG_COMPOUND_NONE ||
	    bot->compound_drop_live.drop_active ||
	    bot->compound_drop_live.command_pending ||
	    bot->compound_drop_live.recovering || bot->hook_phase != 0 ||
	    bot->hook_replay_active ||
	    (bot->rocketjump.phase == SG_ROCKETJUMP_EQUIP ||
	     SG_RocketJumpPhasePhysical(bot->rocketjump.phase)) ||
	    bot->nade_phase != 0 ||
	    bot->jump_started || bot->drop_started || bot->drop_replay_active ||
	    bot->swim_replay_active || bot->swim_validated ||
	    bot->declared_started || bot->declared_touched ||
	    bot->declared_triggered || bot->declared_activated ||
	    bot->declared_door_retreat || bot->declared_guard_paused ||
	    entity->client->hookstate != 0 || entity->client->hook != NULL)
		return false;
	return true;
}

int SG_CompoundHookGameStageAuthenticatedProbe(int link_index)
{
	rune_t *rune = SG_Rune();
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *mechanism;
	sg_compound_hook_live_result_t result;
	sg_bot_t *bot = NULL;
	edict_t *entity;
	vec3_t staged_origin;
	int axis, slot;

	if (!sg_cv.debug || sg_cv.debug->value <= 0.0f || !rune ||
	    !SG_RunePhysicsCompatible(rune) || link_index < 0 ||
	    link_index >= rune->hdr.num_links ||
	    !(binding = SG_CompoundPublicationBinding(rune,
	        (uint32_t)link_index)) || binding->link.action != RL_DOOR_HOOK ||
	    !(mechanism = SG_CompoundPublicationMechanism(rune, binding)) ||
	    mechanism->trigger_key <= 0 || mechanism->mover_key <= 0)
		return false;
	for (slot = 0; slot < SG_MAXBOTS; slot++)
		if (SG_CompoundHookGameIdleAdmission(&sg_bots[slot]) &&
		    SG_HookOffhandReady(sg_bots[slot].ent))
		{
			bot = &sg_bots[slot];
			break;
		}
	if (!bot || !gi.unlinkentity || !sg_host.linkentity)
		return false;
	entity = bot->ent;
	{
		sg_bot_t bot_before = *bot;
		edict_t entity_before = *entity;
		gclient_t client_before = *entity->client;

		gi.unlinkentity(entity);
		entity->client->ps.pmove = binding->source.pms;
		entity->client->old_pmove = binding->source.old_pms;
		for (axis = 0; axis < 3; axis++)
		{
			entity->s.origin[axis] = binding->source_seed.origin[axis];
			entity->s.old_origin[axis] = binding->source_seed.origin[axis];
			entity->velocity[axis] =
			    binding->source.pms.velocity[axis] * 0.125f;
		}
		VectorClear(entity->client->oldvelocity);
		entity->client->oldvelocity[2] = binding->source.old_frame_z;
		entity->groundentity = binding->source.grounded ? g_edicts : NULL;
		entity->groundentity_linkcount = entity->groundentity ?
		    entity->groundentity->linkcount : 0;
		entity->waterlevel = binding->source.waterlevel;
		entity->watertype = binding->source.watertype;
		SG_CompoundHookGameReset(bot);
		bot->seed = binding->link.from;
		VectorCopy(entity->s.origin, bot->last_origin);
		bot->commit_link = link_index;
		bot->sticky_link = link_index;
		bot->commit_until = level.time + 5.0f;
		bot->latch_until = level.time + 5.0f;
		VectorCopy(entity->s.origin, staged_origin);
		bot->stuck_time = 0.0f;
		VectorCopy(staged_origin, bot->stuck_origin);
		VectorCopy(staged_origin, bot->watch_org);
		VectorCopy(staged_origin, bot->stag_org);
		VectorCopy(staged_origin, bot->wedge_org);
		SG_Mark(&bot->watch_since);
		SG_Mark(&bot->stag_since);
		SG_Mark(&bot->wedge_since);
		bot->seedless_active = false;
		bot->seedless_since = 0.0f;
		bot->seedless_turn_until = 0.0f;
		sg_host.linkentity(entity);
		if (!SG_HookOffhandReady(entity))
			goto rollback;
		result = SG_CompoundHookGameBegin(bot, (uint32_t)link_index, true);
		if (result.outcome != SG_COMPOUND_HOOK_LIVE_RUNNING ||
		    !bot->compound_hook_live.guard_owned ||
		    !bot->compound_hook_live.local_owned)
		{
			if (bot->compound_hook_live.guard_owned ||
			    bot->compound_hook_live.local_owned)
				return false;
			goto rollback;
		}
		SG_CompoundHookGameDebugResult(bot, "begin", &result);
		if (sg_host.dprint)
			sg_host.dprint("slipgate: dhook probe-staged bot=%d link=%d "
			    "trigger=%d mover=%d source=(%.3f %.3f %.3f) "
			    "destination=(%.3f %.3f %.3f)\n", slot, link_index,
			    mechanism->trigger_key, mechanism->mover_key,
			    staged_origin[0], staged_origin[1], staged_origin[2],
			    binding->destination_seed.origin[0],
			    binding->destination_seed.origin[1],
			    binding->destination_seed.origin[2]);
		return true;

	rollback:
		gi.unlinkentity(entity);
		*entity->client = client_before;
		*entity = entity_before;
		*bot = bot_before;
		sg_host.linkentity(entity);
	}
	return false;
}

void SG_CompoundHookGameDebugResult(sg_bot_t *bot, const char *stage,
	const sg_compound_hook_live_result_t *result)
{
	const sg_compound_hook_live_state_t *live;
	const edict_t *entity;

	if (!bot || !stage || !result || !(entity = bot->ent) ||
	    !sg_cv.debug || sg_cv.debug->value <= 0.0f || !sg_host.dprint)
		return;
	live = &bot->compound_hook_live;
	sg_host.dprint("slipgate: dhook stage=%s client=%d link=%u phase=%d "
	               "control=%d event=%d outcome=%d failure=%s "
	               "replay_reason=%d elapsed=%d guard=%d local=%d linked=%d "
	               "attached=%d release_requested=%d release_applied=%d "
	               "hookstate=%d hook=%d origin=(%.3f %.3f %.3f) "
	               "velocity=(%.3f %.3f %.3f)\n",
	               stage, entity->s.number,
	               live->snapshot.binding.link_index, live->outer.phase,
	               live->control, live->last_event, result->outcome,
	               SG_CompoundHookLiveFailureName(result->failure),
	               result->replay_reason, live->transaction_elapsed_ms,
	               live->guard_owned, live->local_owned, live->bolt_linked,
	               bot->compound_hook_events.attached,
	               bot->compound_hook_events.release_requested,
	               bot->compound_hook_events.release_applied,
	               entity->client ? entity->client->hookstate : -1,
	               entity->client && entity->client->hook != NULL,
	               entity->s.origin[0], entity->s.origin[1],
	               entity->s.origin[2], entity->velocity[0],
	               entity->velocity[1], entity->velocity[2]);
}

static void CompoundHookGameDebugStage(sg_bot_t *bot, const char *stage)
{
	const sg_compound_hook_live_state_t *live;

	if (!bot || !stage || !sg_cv.debug || sg_cv.debug->value <= 0.0f ||
	    !sg_host.dprint)
		return;
	live = &bot->compound_hook_live;
	sg_host.dprint("slipgate: dhook stage=%s client=%d link=%u phase=%d "
	               "control=%d event=%d guard=%d local=%d\n",
	               stage, bot->ent ? bot->ent->s.number : -1,
	               live->snapshot.binding.link_index, live->outer.phase,
	               live->control, live->last_event, live->guard_owned,
	               live->local_owned);
}

sg_compound_hook_live_result_t SG_CompoundHookGameBegin(sg_bot_t *bot,
	uint32_t link_index, qboolean offhand_ready)
{
	sg_compound_hook_live_host_t host;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_compound_hook_live_result_t result;

	memset(&result, 0, sizeof(result));
	result.outcome = SG_COMPOUND_HOOK_LIVE_REJECTED;
	result.failure = SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT;
	result.replay_reason = SG_REPLAY_REASON_INVALID_ARGUMENT;
	if (!offhand_ready || !SG_CompoundHookGameIdleAdmission(bot) ||
	    !SG_CompoundHookGameHost(bot, &host) ||
	    !SG_CompoundHookGamePose(bot->ent, &pose) ||
	    !SG_CompoundHookGameObservation(bot, bot->ent, &observation))
		return result;
	memset(&bot->compound_hook_game, 0, sizeof(bot->compound_hook_game));
	result = SG_CompoundHookLiveBegin(&bot->compound_hook_live, &host,
	    link_index, &pose, &observation);
	if (result.outcome != SG_COMPOUND_HOOK_LIVE_RUNNING)
		memset(&bot->compound_hook_game, 0,
		    sizeof(bot->compound_hook_game));
	return result;
}

sg_compound_hook_game_authorization_t SG_CompoundHookGameAuthorizeTouch(
	sg_bot_t *bot, edict_t *source,
	edict_t *activator, int frame_serial)
{
	sg_compound_hook_live_host_t host;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_compound_hook_live_result_t result;

	if (!bot || !bot->compound_hook_live.guard_owned)
		return SG_COMPOUND_HOOK_GAME_BYPASS;
	if (!source || !activator || activator != bot->ent ||
	    source->s.number != bot->compound_hook_live.snapshot.trigger_key ||
	    !SG_CompoundHookGameHost(bot, &host) ||
	    !SG_CompoundHookGamePose(activator, &pose) ||
	    !SG_CompoundHookGameObservation(bot, activator, &observation))
		return SG_COMPOUND_HOOK_GAME_DENIED;
	result = SG_CompoundHookLiveTouch(&bot->compound_hook_live, &host,
	    source->s.number, &pose, &observation, frame_serial);
	if (result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING)
		SG_CompoundHookGameDebugResult(bot, "touch", &result);
	return result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING ?
	    SG_COMPOUND_HOOK_GAME_ACCEPTED : SG_COMPOUND_HOOK_GAME_DENIED;
}

sg_compound_hook_game_authorization_t
SG_CompoundHookGameAuthorizeActivation(sg_bot_t *bot, edict_t *source,
	edict_t *door_master, edict_t *activator, int frame_serial)
{
	sg_compound_hook_live_host_t host;
	sg_compound_hook_live_result_t result;

	if (!bot || !bot->compound_hook_live.guard_owned)
		return SG_COMPOUND_HOOK_GAME_BYPASS;
	if (!source || !door_master || !activator || activator != bot->ent ||
	    source->s.number != bot->compound_hook_live.snapshot.trigger_key ||
	    door_master->s.number != bot->compound_hook_live.snapshot.mover_key ||
	    !SG_CompoundHookGameHost(bot, &host))
		return SG_COMPOUND_HOOK_GAME_DENIED;
	result = SG_CompoundHookLiveActivate(&bot->compound_hook_live, &host,
	    source->s.number, door_master->s.number, frame_serial);
	if (result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING)
		SG_CompoundHookGameDebugResult(bot, "activation", &result);
	return result.outcome == SG_COMPOUND_HOOK_LIVE_RUNNING ?
	    SG_COMPOUND_HOOK_GAME_ACCEPTED : SG_COMPOUND_HOOK_GAME_DENIED;
}

sg_compound_hook_live_result_t SG_CompoundHookGameRecoverOwnedFailure(
	sg_bot_t *bot, usercmd_t *same_slot_command)
{
	sg_compound_hook_live_host_t host;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_compound_hook_live_result_t result;

	memset(&result, 0, sizeof(result));
	result.outcome = SG_COMPOUND_HOOK_LIVE_REJECTED;
	result.failure = SG_COMPOUND_HOOK_LIVE_FAILURE_ARGUMENT;
	result.replay_reason = SG_REPLAY_REASON_INVALID_ARGUMENT;
	if (!bot || !bot->ent || !bot->ent->client ||
	    !SG_CompoundHookGameHost(bot, &host) ||
	    !SG_CompoundHookGamePose(bot->ent, &pose) ||
	    !SG_CompoundHookGameObservation(bot, bot->ent, &observation))
		return result;
	result = SG_CompoundHookLiveRecover(&bot->compound_hook_live, &host,
	    &pose, &observation, bot->ent->client->oldvelocity[2]);
	SG_CompoundHookGameDebugResult(bot, "recovery", &result);
	if (result.outcome != SG_COMPOUND_HOOK_LIVE_RECOVERING ||
	    !same_slot_command)
		return result;
	result = SG_CompoundHookLivePreStep(&bot->compound_hook_live, &host,
	    &pose, &observation, same_slot_command);
	SG_CompoundHookGameDebugResult(bot, "recovery-prestep", &result);
	return result;
}

qboolean SG_CompoundHookGameApplyRequestedRelease(sg_bot_t *bot)
{
	sg_compound_hook_game_event_gate_t gate;

	if (!bot || !bot->ent || !bot->ent->client ||
	    !bot->compound_hook_live.guard_owned)
		return false;
	if (!bot->compound_hook_live.hook.release_requested ||
	    bot->compound_hook_live.hook_released)
		return true;
	gate = SG_CompoundHookGameReleaseRequested(bot->ent,
	    bot->ent->client->hook);
	if (gate != SG_COMPOUND_HOOK_GAME_EVENT_ACCEPTED)
		return false;
	CompoundHookGameDebugStage(bot, "release-requested");
	ctf_hook_abort(bot->ent);
	return bot->compound_hook_live.hook_released ||
	       bot->compound_hook_live.recovering;
}
