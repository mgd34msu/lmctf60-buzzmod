#include "g_local.h"
#include "g_ctffunc.h"
#include "bat.h"
#include "g_tourney.h"
#include <ctype.h>

int Num_Of_Players(edict_t *ent, int Ctf_Team);

static qboolean POVLock_NameEqual(const char *left, const char *right)
{
	if (!left || !right)
		return false;
	while (*left && *right)
	{
		if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
			return false;
		left++;
		right++;
	}
	return *left == '\0' && *right == '\0';
}

static qboolean POVLock_IsSpectator(const edict_t *ent)
{
	return ent && ent->client &&
		(ent->client->resp.spectator ||
		 ent->client->ctf.teamnum <= CTF_TEAM_UNDEFINED);
}

static qboolean POVLock_IsViewerEndpoint(const edict_t *ent)
{
	return ent && ent->inuse && ent->client &&
		ent->client->pers.connected && !(ent->flags & FL_BOT) &&
		ent->client->resp.spectator;
}

static qboolean POVLock_IsInitialTarget(edict_t *ent, int *sg_slot,
	unsigned long long *sg_instance)
{
	return ent && ent->client && !ent->deadflag &&
		!ent->client->resp.spectator &&
		SG_BotPOVIdentity(ent, sg_slot, sg_instance);
}

/* Ordinary in-eyes chase temporarily installs a fake client's complete
 * playerstate and body.  On the way out, do not leave that state attached to
 * a real observer.  A deliberate povrecord has separate lifecycle rules and
 * keeps its full copied state until its existing close/wait paths run. */
static void POVLock_ScrubOrdinaryViewer(edict_t *viewer, int jump_held)
{
	if (!POVLock_IsViewerEndpoint(viewer) ||
		viewer->client->pov_record_active)
		return;

	memset(&viewer->client->ps, 0, sizeof(viewer->client->ps));
	viewer->client->ps.pmove.pm_type = PM_SPECTATOR;
	viewer->client->ps.pmove.pm_flags = jump_held;
	viewer->client->ps.fov = 90;
	viewer->client->ps.stats[STAT_SPECTATOR] = 1;
	memset(&viewer->client->old_pmove, 0, sizeof(viewer->client->old_pmove));
	VectorCopy(viewer->s.origin, viewer->s.old_origin);
	VectorClear(viewer->velocity);
	VectorClear(viewer->s.angles);
	VectorClear(viewer->client->v_angle);
	viewer->viewheight = 0;
	viewer->movetype = MOVETYPE_NOCLIP;
	viewer->solid = SOLID_NOT;
	viewer->s.modelindex = 0;
	gi.linkentity(viewer);
}

static void POVLock_ClearInstant(edict_t *ent)
{
	qboolean ordinary_in_eyes;
	int jump_held;

	if (!ent || !ent->client)
		return;
	ordinary_in_eyes = ent->client->povlock_active &&
		!ent->client->pov_record_active && POVLock_IsViewerEndpoint(ent);
	jump_held = ordinary_in_eyes ?
		(ent->client->ps.pmove.pm_flags & PMF_JUMP_HELD) : 0;
	ent->client->povlock_active = false;
	ent->client->povlock_target_index = 0;
	ent->client->povlock_target_ctfid = 0;
	if (ordinary_in_eyes)
		POVLock_ScrubOrdinaryViewer(ent, jump_held);
	else
	{
		ent->client->ps.pmove.pm_flags &= ~PMF_NO_PREDICTION;
		ent->client->ps.pmove.pm_flags &= ~PMF_JUMP_HELD;
	}
}

static void POVLock_Send(edict_t *viewer, const char *command)
{
	gi.WriteByte(svc_stufftext);
	gi.WriteString((char *)command);
	gi.unicast(viewer, true);
}

static void POVLock_CloseSessionInternal(edict_t *viewer,
	qboolean allow_endpoint_send)
{
	qboolean send_stop;

	if (!viewer || !viewer->client)
		return;
	send_stop = allow_endpoint_send && viewer->client->pov_record_active &&
		!viewer->client->pov_record_stop_sent &&
		POVLock_IsViewerEndpoint(viewer) &&
		viewer->client->ctf.ctfid ==
			viewer->client->pov_record_viewer_ctfid;

	/* Close every authority/latch before invoking the engine callbacks. */
	POVLock_ClearInstant(viewer);
	viewer->client->pov_record_active = false;
	viewer->client->pov_record_pending = false;
	viewer->client->pov_record_stop_sent = send_stop;
	viewer->client->pov_record_wait_respawn = false;
	viewer->client->pov_record_sg_slot = -1;
	viewer->client->pov_record_sg_instance = 0ULL;
	if (send_stop)
		POVLock_Send(viewer, "stop\n");
}

static void POVLock_CloseSession(edict_t *viewer)
{
	POVLock_CloseSessionInternal(viewer, true);
}

static void POVLock_EnterWaitRespawn(edict_t *viewer)
{
	if (!viewer || !viewer->client || !viewer->client->pov_record_active)
		return;
	POVLock_ClearInstant(viewer);
	viewer->client->pov_record_wait_respawn = true;

	/* Do not leave the last bot frame in the outgoing playerstate while the
	 * exact SG instance has no live body. */
	memset(&viewer->client->ps, 0, sizeof(viewer->client->ps));
	viewer->client->ps.pmove.pm_type = PM_SPECTATOR;
	viewer->client->ps.fov = 90;
	viewer->client->ps.stats[STAT_SPECTATOR] = 1;
	viewer->movetype = MOVETYPE_NOCLIP;
	viewer->solid = SOLID_NOT;
	viewer->s.modelindex = 0;
	VectorClear(viewer->velocity);
	gi.linkentity(viewer);
}

static edict_t *POVLock_SessionTarget(const edict_t *viewer)
{
	edict_t *target;

	if (!viewer || !viewer->client || !viewer->client->pov_record_active)
		return NULL;
	target = SG_BotPOVResolve(viewer->client->pov_record_sg_slot,
		viewer->client->pov_record_sg_instance);
	if (!target || target->client->resp.spectator)
		return NULL;
	return target;
}

static edict_t *POVLock_LegacyTarget(const edict_t *viewer)
{
	int index;
	int sg_slot;
	unsigned long long sg_instance;
	edict_t *target;

	if (!viewer || !viewer->client || !viewer->client->povlock_active)
		return NULL;
	index = viewer->client->povlock_target_index;
	if (index < 1 || index > game.maxclients)
		return NULL;
	target = g_edicts + index;
	if (!target->inuse || target->deadflag || !target->client ||
		target->client->resp.spectator ||
		target->client->ctf.ctfid != viewer->client->povlock_target_ctfid ||
		!SG_BotPOVIdentity(target, &sg_slot, &sg_instance) ||
		sg_slot != viewer->client->pov_record_sg_slot ||
		sg_instance != viewer->client->pov_record_sg_instance)
		return NULL;
	return target;
}

void POVLock_Clear(edict_t *ent)
{
	if (!ent || !ent->client)
		return;
	if (ent->client->pov_record_active)
		POVLock_CloseSession(ent);
	else
		POVLock_ClearInstant(ent);
}

void POVLock_ViewerDisconnected(edict_t *ent)
{
	if (!ent || !ent->client)
		return;
	if (ent->client->pov_record_active)
		POVLock_CloseSessionInternal(ent, false);
	else
		POVLock_ClearInstant(ent);
}

void POVLock_SuppressInput(gclient_t *client)
{
	if (!client)
		return;
	client->latched_buttons &= ~BUTTON_ATTACK;
	client->ps.pmove.pm_flags |= PMF_JUMP_HELD;
}

void POVLock_ClearTarget(edict_t *target)
{
	int i;
	int sg_slot = -1;
	unsigned long long sg_instance = 0ULL;
	qboolean sg_owned;

	if (!target)
		return;
	sg_owned = SG_BotPOVIdentity(target, &sg_slot, &sg_instance);
	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *viewer = g_edicts + i;
		if (!viewer->client)
			continue;
		if (viewer->client->pov_record_active && sg_owned &&
		    viewer->client->pov_record_sg_slot == sg_slot &&
		    viewer->client->pov_record_sg_instance == sg_instance)
			POVLock_CloseSession(viewer);
		else if (!viewer->client->pov_record_active &&
			 viewer->client->povlock_active &&
			 viewer->client->povlock_target_index == target - g_edicts)
		{
			POVLock_ClearInstant(viewer);
			viewer->client->chase_target = NULL;
		}
	}
}

void POVLock_TargetWillNotRespawn(edict_t *target)
{
	/* Unlike an ordinary death, this life has no PutClientInServer edge that
	 * could ever reattach it. Use the same exact-SG terminal authority as a
	 * disconnect or retirement. */
	POVLock_ClearTarget(target);
}

void POVLock_TargetRespawning(edict_t *target)
{
	int i, sg_slot = -1;
	unsigned long long sg_instance = 0ULL;
	qboolean sg_owned = SG_BotPOVIdentity(target, &sg_slot, &sg_instance);

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *viewer = g_edicts + i;
		if (!viewer->client)
			continue;
		if (viewer->client->pov_record_active && sg_owned &&
		    viewer->client->pov_record_sg_slot == sg_slot &&
		    viewer->client->pov_record_sg_instance == sg_instance)
			POVLock_EnterWaitRespawn(viewer);
		else if (!viewer->client->pov_record_active &&
			 viewer->client->povlock_active &&
			 viewer->client->povlock_target_index == target - g_edicts)
		{
			POVLock_ClearInstant(viewer);
			viewer->client->chase_target = NULL;
		}
	}
}

void POVLock_TargetSpawned(edict_t *target)
{
	int i, sg_slot = -1;
	unsigned long long sg_instance = 0ULL;

	if (!target || !target->client || target->deadflag ||
	    target->client->resp.spectator ||
	    !SG_BotPOVIdentity(target, &sg_slot, &sg_instance))
		return;
	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *viewer = g_edicts + i;
		if (!viewer->client || !viewer->client->pov_record_active ||
		    !viewer->client->pov_record_wait_respawn ||
		    viewer->client->pov_record_sg_slot != sg_slot ||
		    viewer->client->pov_record_sg_instance != sg_instance)
			continue;
		if (!POVLock_IsViewerEndpoint(viewer) ||
		    viewer->client->ctf.ctfid !=
			viewer->client->pov_record_viewer_ctfid)
		{
			POVLock_CloseSession(viewer);
			continue;
		}
		viewer->client->povlock_active = true;
		viewer->client->povlock_target_index = (int)(target - g_edicts);
		viewer->client->povlock_target_ctfid = target->client->ctf.ctfid;
		viewer->client->pov_record_wait_respawn = false;
		(void)POVLock_Update(viewer);
	}
}

void POVLock_SGInstanceRetired(int sg_slot,
	unsigned long long instance_token)
{
	int i;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *viewer = g_edicts + i;
		if (!viewer->client)
			continue;
		if (viewer->client->pov_record_active &&
		    viewer->client->pov_record_sg_slot == sg_slot &&
		    viewer->client->pov_record_sg_instance == instance_token)
			POVLock_CloseSession(viewer);
		else if (!viewer->client->pov_record_active &&
			 viewer->client->povlock_active &&
			 viewer->client->pov_record_sg_slot == sg_slot &&
			 viewer->client->pov_record_sg_instance == instance_token)
		{
			POVLock_ClearInstant(viewer);
			viewer->client->chase_target = NULL;
		}
	}
}

void POVLock_StopAll(void)
{
	int i;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *viewer = g_edicts + i;
		if (viewer->client && viewer->client->pov_record_active)
			POVLock_CloseSession(viewer);
	}
}

static qboolean POVLock_CopyTargetState(edict_t *viewer, edict_t *target,
	qboolean follower_end_frame)
{
	int i;
	int jump_held = viewer->client->ps.pmove.pm_flags & PMF_JUMP_HELD;

	viewer->client->ps = target->client->ps;
	/* The ordinary spectator's jump bit is an input edge latch, not bot POV
	 * state.  Import the exact playerstate for a recording, but retain that
	 * one viewer-local bit for chase cycling. */
	if (!viewer->client->pov_record_active)
	{
		viewer->client->ps.pmove.pm_flags &= ~PMF_JUMP_HELD;
		viewer->client->ps.pmove.pm_flags |= jump_held;
	}
	VectorCopy(target->s.origin, viewer->s.origin);
	VectorCopy(target->s.old_origin, viewer->s.old_origin);
	VectorCopy(target->velocity, viewer->velocity);
	VectorCopy(target->s.angles, viewer->s.angles);
	VectorCopy(target->client->v_angle, viewer->client->v_angle);
	viewer->viewheight = target->viewheight;
	for (i = 0; i < 3; i++)
		viewer->client->ps.pmove.delta_angles[i] =
			ANGLE2SHORT(target->client->v_angle[i] -
				viewer->client->resp.cmd_angles[i]);
	viewer->client->ps.pmove.pm_type = PM_FREEZE;
	viewer->client->ps.pmove.pm_flags |= PMF_NO_PREDICTION;
	viewer->movetype = MOVETYPE_NOCLIP;
	viewer->solid = SOLID_NOT;
	viewer->s.modelindex = 0;
	gi.linkentity(viewer);

	if (follower_end_frame && viewer->client->pov_record_active &&
	    viewer->client->pov_record_pending &&
	    !viewer->client->pov_record_sent &&
	    POVLock_IsViewerEndpoint(viewer) &&
	    viewer->client->ctf.ctfid ==
		viewer->client->pov_record_viewer_ctfid)
	{
		/* The final target state is already installed. Latch before callbacks. */
		viewer->client->pov_record_pending = false;
		viewer->client->pov_record_sent = true;
		POVLock_Send(viewer, "record pov\n");
	}
	return true;
}

void POVLock_UpdateFollowers(edict_t *target)
{
	int i;
	int target_index;

	if (!target)
		return;
	target_index = (int)(target - g_edicts);
	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *viewer = g_edicts + i;
		if (viewer->inuse && viewer->client && viewer->client->povlock_active &&
			viewer->client->povlock_target_index == target_index)
		{
			edict_t *resolved;

			if (viewer->client->pov_record_active)
			{
				resolved = POVLock_SessionTarget(viewer);
				if (!resolved)
				{
					POVLock_CloseSession(viewer);
					continue;
				}
				if (resolved->deadflag)
				{
					POVLock_EnterWaitRespawn(viewer);
					continue;
				}
				if (resolved != target)
					continue;
				if (resolved->client->ctf.ctfid !=
				    viewer->client->povlock_target_ctfid)
				{
					POVLock_EnterWaitRespawn(viewer);
					continue;
				}
				(void)POVLock_CopyTargetState(viewer, resolved, true);
			}
			else
				(void)POVLock_Update(viewer);
		}
	}
}

qboolean POVLock_CommandNameIs(const char *command)
{
	return POVLock_NameEqual(command, "povlock");
}

/*
 * The console owns only this one fixed message shape.  It deliberately does
 * not expose POVLock_Send(): an rcon/admin argument may select two already
 * connected objects, but it can never become arbitrary client stufftext.
 */
static qboolean POVRecord_AdminName(const char *name, qboolean sg_name)
{
	const unsigned char *p;
	size_t length;

	if (!name)
		return false;
	length = strlen(name);
	if (sg_name)
	{
		if (length < 5 || length > 36 || strncmp(name, "[SG]", 4) != 0)
			return false;
		p = (const unsigned char *)name + 4;
	}
	else
	{
		if (length < 1 || length > 32)
			return false;
		p = (const unsigned char *)name;
	}
	if (!isalnum(*p))
		return false;
	for (p++; *p; p++)
		if (!isalnum(*p) && *p != '_' && *p != '-')
			return false;
	return true;
}

static qboolean POVRecord_AdminEndpoint(edict_t *ent, int client_index)
{
	return ent && ent->inuse && ent->client && game.clients &&
		ent->client == &game.clients[client_index] &&
		ent->client->pers.connected && ent->client->ctf.ctfid != 0 &&
		!(ent->flags & FL_BOT) && ent->client->resp.spectator &&
		ent->client->ctf.teamnum != CTF_TEAM_RED &&
		ent->client->ctf.teamnum != CTF_TEAM_BLUE;
}

qboolean POVRecord_AdminDirective(const char *spectator_name,
	const char *target_name, qboolean stop)
{
	edict_t *viewer = NULL;
	edict_t *target = NULL;
	char command[64];
	int written;
	int i, viewer_matches = 0, target_matches = 0;

	if (!POVRecord_AdminName(spectator_name, false) ||
	    (!stop && !POVRecord_AdminName(target_name, true)) ||
	    (stop && target_name != NULL))
		return false;
	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *candidate = &g_edicts[i + 1];

		if (POVRecord_AdminEndpoint(candidate, i) &&
		    strcmp(candidate->client->pers.netname, spectator_name) == 0)
		{
			viewer = candidate;
			viewer_matches++;
		}
	}
	if (viewer_matches != 1)
		return false;
	if (!stop)
	{
		for (i = 1; i <= game.maxclients; i++)
		{
			edict_t *candidate = &g_edicts[i];
			int sg_slot;
			unsigned long long sg_instance;

			if (!candidate->client || candidate->deadflag ||
			    candidate->client->resp.spectator ||
			    (candidate->client->ctf.teamnum != CTF_TEAM_RED &&
			     candidate->client->ctf.teamnum != CTF_TEAM_BLUE) ||
			    strcmp(candidate->client->pers.netname, target_name) != 0 ||
			    !SG_BotPOVIdentity(candidate, &sg_slot, &sg_instance))
				continue;
			target = candidate;
			target_matches++;
		}
		if (target_matches != 1 || !target)
			return false;
		written = snprintf(command, sizeof(command), "cmd povlock %s\n",
		                   target_name);
		if (written < 0 || (size_t)written >= sizeof(command))
			return false;
	}
	else
		strcpy(command, "cmd povlock off\n");

	POVLock_Send(viewer, command);
	return true;
}

qboolean POVLock_Command(edict_t *ent, const char *argument)
{
	edict_t *selected = NULL;
	int selected_sg_slot = -1;
	unsigned long long selected_sg_instance = 0ULL;
	int i;
	int matches = 0;

	if (!POVLock_IsSpectator(ent) || !POVLock_IsViewerEndpoint(ent))
		return false;
	if (argument && POVLock_NameEqual(argument, "off"))
	{
		POVLock_Clear(ent);
		return true;
	}
	/* A live recording never goes back through mutable names. Repeated
	 * `povlock` commands merely refresh the already captured SG identity. */
	if (ent->client->pov_record_active)
	{
		selected = POVLock_SessionTarget(ent);
		if (argument && *argument &&
		    (!selected || !POVLock_NameEqual(
			selected->client->pers.netname, argument)))
			return false;
		return POVLock_Update(ent);
	}

	if (argument && *argument)
	{
		for (i = 1; i <= game.maxclients; i++)
		{
			edict_t *candidate = g_edicts + i;
			int sg_slot;
			unsigned long long sg_instance;
			if (!POVLock_IsInitialTarget(candidate, &sg_slot, &sg_instance) ||
				!POVLock_NameEqual(candidate->client->pers.netname, argument))
				continue;
			selected = candidate;
			selected_sg_slot = sg_slot;
			selected_sg_instance = sg_instance;
			matches++;
		}
		if (matches != 1)
		{
			POVLock_Clear(ent);
			return false;
		}
	}
	else
	{
		for (i = 1; i <= game.maxclients; i++)
		{
			edict_t *candidate = g_edicts + i;
			int sg_slot;
			unsigned long long sg_instance;
			if (!POVLock_IsInitialTarget(candidate, &sg_slot, &sg_instance))
				continue;
			if (!selected || candidate->client->resp.score > selected->client->resp.score ||
				(candidate->client->resp.score == selected->client->resp.score &&
				 i > (int)(selected - g_edicts)))
				selected = candidate;
			if (selected == candidate)
			{
				selected_sg_slot = sg_slot;
				selected_sg_instance = sg_instance;
			}
		}
		if (!selected)
		{
			POVLock_Clear(ent);
			return false;
		}
	}

	ent->client->chase_target = NULL;
	ent->client->povlock_active = true;
	ent->client->povlock_target_index = (int)(selected - g_edicts);
	ent->client->povlock_target_ctfid = selected->client->ctf.ctfid;
	ent->client->pov_record_active = true;
	ent->client->pov_record_pending = true;
	ent->client->pov_record_sent = false;
	ent->client->pov_record_stop_sent = false;
	ent->client->pov_record_wait_respawn = false;
	ent->client->pov_record_sg_slot = selected_sg_slot;
	ent->client->pov_record_sg_instance = selected_sg_instance;
	ent->client->pov_record_viewer_ctfid = ent->client->ctf.ctfid;
	return POVLock_Update(ent);
}

qboolean POVLock_Update(edict_t *ent)
{
	edict_t *target;

	if (!POVLock_IsSpectator(ent))
	{
		POVLock_Clear(ent);
		return false;
	}
	if (ent->client->pov_record_active)
	{
		if (!POVLock_IsViewerEndpoint(ent) ||
		    ent->client->ctf.ctfid != ent->client->pov_record_viewer_ctfid)
		{
			POVLock_CloseSession(ent);
			return false;
		}
		target = POVLock_SessionTarget(ent);
		if (!target)
		{
			POVLock_CloseSession(ent);
			return false;
		}
		if (target->deadflag)
		{
			POVLock_EnterWaitRespawn(ent);
			return false;
		}
		if (ent->client->pov_record_wait_respawn)
			return false;
		if (!ent->client->povlock_active ||
		    ent->client->povlock_target_index != target - g_edicts ||
		    ent->client->povlock_target_ctfid != target->client->ctf.ctfid)
		{
			POVLock_EnterWaitRespawn(ent);
			return false;
		}
		return POVLock_CopyTargetState(ent, target, false);
	}
	target = POVLock_LegacyTarget(ent);
	if (!target)
	{
		POVLock_ClearInstant(ent);
		/* A normal chase may never silently fall back to a stale bot's
		 * third-person camera.  The observer can select a new target instead. */
		ent->client->chase_target = NULL;
		return false;
	}

	/* Ordinary SG chase is in-eyes: copy the bot's client-visible state. */
	return POVLock_CopyTargetState(ent, target, false);
}

static qboolean Chase_TargetAllowed(edict_t *viewer, edict_t *candidate)
{
	int sg_slot;
	unsigned long long sg_instance;

	if (!viewer || !viewer->client || !candidate || !candidate->inuse ||
		!candidate->client || !candidate->client->pers.connected ||
		candidate->client->resp.spectator)
		return false;
	if ((viewer->client->ctf.teamnum == CTF_TEAM_OBSERVER_RED &&
		 candidate->client->ctf.teamnum != CTF_TEAM_RED) ||
		(viewer->client->ctf.teamnum == CTF_TEAM_OBSERVER_BLUE &&
		 candidate->client->ctf.teamnum != CTF_TEAM_BLUE))
		return false;
	/* Fake clients must be a live, uniquely owned SG life before they can
	 * become an in-eyes endpoint.  A dead/reused/unowned bot is skipped rather
	 * than becoming a stale third-person target. */
	if (candidate->flags & FL_BOT)
		return !candidate->deadflag &&
			SG_BotPOVIdentity(candidate, &sg_slot, &sg_instance);
	return true;
}

static void Chase_SetTarget(edict_t *viewer, edict_t *target)
{
	int sg_slot;
	unsigned long long sg_instance;

	if (!viewer || !viewer->client || !Chase_TargetAllowed(viewer, target))
		return;
	/* This is an ordinary spectator chase, never a povrecord session.  Only
	 * an actual SG in-eyes departure needs a state scrub; clearing a human
	 * chase here would erase the viewer's held-jump edge before a bot copy can
	 * preserve it. */
	if (viewer->client->povlock_active &&
		!viewer->client->pov_record_active)
		POVLock_ClearInstant(viewer);
	viewer->client->chase_target = target;
	viewer->client->update_chase = true;
	if (!SG_BotPOVIdentity(target, &sg_slot, &sg_instance))
		return;                         /* human: retain third-person chase */
	viewer->client->povlock_active = true;
	viewer->client->povlock_target_index = (int)(target - g_edicts);
	viewer->client->povlock_target_ctfid = target->client->ctf.ctfid;
	viewer->client->pov_record_sg_slot = sg_slot;
	viewer->client->pov_record_sg_instance = sg_instance;
}

void UpdateChaseCam(edict_t *ent)
{
	vec3_t goal, forward, right, angles;
	edict_t *targ;
	trace_t trace;
	int i;

	if (!ent || !ent->client || !ent->client->chase_target)
		return;
	if (ent->client->povlock_active)
	{
		(void)POVLock_Update(ent);
		return;
	}

	// is our chase target gone?
	if (!ent->client->chase_target->inuse
		|| ent->client->chase_target->client->resp.spectator) {
		edict_t *old = ent->client->chase_target;
		ChaseNext(ent);
		if (ent->client->chase_target == old) {
			ent->client->chase_target = NULL;
			ent->client->ps.pmove.pm_flags &= ~PMF_NO_PREDICTION;
			return;
		}
	}

	targ = ent->client->chase_target;
	VectorCopy(targ->client->v_angle, angles);
	VectorCopy (targ->s.origin, goal);
	goal[2] += targ->viewheight;

	vec3_t	targorigin;

	VectorCopy (goal, targorigin);

	AngleVectors (angles, forward, right, NULL);
	VectorMA (goal, 30, forward, goal);

	// trace from targorigin to final chase origin goal
	trace = gi.trace (targorigin, vec3_origin, vec3_origin, goal, targ, MASK_SOLID);

	// test for hit so we don't go out of the map!
	if (trace.fraction < 1) {
		vec3_t	temp;

		// we hit something, need to do a bit of avoidance

		// take real end point
		VectorCopy (trace.endpos, goal);

		// real dir vector
		VectorSubtract (goal, targorigin, temp);

		// scale it back bit more
		VectorMA (targorigin, 0.9f, temp, goal);
	}

	VectorCopy(goal, ent->s.origin);
	for (i=0 ; i<3 ; i++) {
		ent->client->ps.pmove.delta_angles[i] = ANGLE2SHORT(targ->client->v_angle[i] - ent->client->resp.cmd_angles[i]);
	}

	if (targ->deadflag) {
		ent->client->ps.viewangles[ROLL] = 40;
		ent->client->ps.viewangles[PITCH] = -15;
		ent->client->ps.viewangles[YAW] = targ->client->killer_yaw;
		ent->client->ps.pmove.pm_type = PM_DEAD;
	} else {
		VectorCopy(targ->client->v_angle, ent->client->ps.viewangles);
		VectorCopy(targ->client->v_angle, ent->client->v_angle);
		ent->client->ps.pmove.pm_type = PM_FREEZE;
	}

	ent->viewheight = 0;
	ent->client->ps.pmove.pm_flags |= PMF_NO_PREDICTION;
	gi.linkentity(ent);
}

void ChaseNext(edict_t *ent)
{
int i;
edict_t *e;

	if(!ent->client->chase_target)
		return;

	i = ent->client->chase_target - g_edicts;
	do 
	{
		i++;
		if (i > maxclients->value)
			i = 1;
		e = g_edicts + i;
		if (Chase_TargetAllowed(ent, e))
			break;
	} while (e != ent->client->chase_target);

	if (e != ent->client->chase_target)
		Chase_SetTarget(ent, e);
}

void ChasePrev(edict_t *ent)
{
int i;
edict_t *e;

	if(!ent->client->chase_target)
		return;

	i = ent->client->chase_target - g_edicts;
	do 
	{
		i--;
		if(i < 1)
			i = maxclients->value;
		e = g_edicts + i;
		if (Chase_TargetAllowed(ent, e))
			break;
	} while (e != ent->client->chase_target);

	if (e != ent->client->chase_target)
		Chase_SetTarget(ent, e);
}


//bat
int Team_Observer_OK(int Team_To_View, edict_t *ent)
{
	if(Num_Of_Players(ent, Team_To_View) > 0)
		return(true);
	
	if(Team_To_View == CTF_TEAM_RED)
		gi.centerprintf(ent, "No red players to chase.");
	else
		gi.centerprintf(ent, "No blue players to chase.");

	return(false);
}


void GetChaseTarget(edict_t *ent)
{
int i;
edict_t *other;

	for(i = 1; i <= maxclients->value; i++) 
	{
		//other is the guy we are chasing
		other = g_edicts + i;

		if (Chase_TargetAllowed(ent, other))
		{
			Chase_SetTarget(ent, other);
			UpdateChaseCam(ent);
			return;
		}
	}

	gi.centerprintf(ent, "No other players to chase.");
}
