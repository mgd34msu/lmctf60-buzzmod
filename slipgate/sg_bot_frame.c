/* sg_bot_frame.c -- the era-4 bot driver.
 *
 * Once per server frame, for every bot: decide the role, turn the role into
 * a world destination, find the body's cell and the destination's cell,
 * take the field's step, and let the executor turn that step into the
 * command the host's own client think consumes.  Combat owns the view
 * unless the step needs it.  Nothing here knows the map except through the
 * RUNE runtime. */
#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world
#include "sg_local.h"
#include "sg_bot.h"

#include <math.h>
#include <string.h>

#include "sg_chat.h"
#include "sg_combat.h"
#include "sg_client_ownership.h"
#include "sg_host_engine_pmove.h"
#include "sg_host_law_owner.h"
#include "sg_hooks.h"
#include "sg_rune_flight.h"
#include "sg_rune_level.h"
#include "sg_rune_source_authority_owner.h"
#include "sg_tactic_controller.h"
#include "sg_util.h"

void Cmd_Hook_f(edict_t *ent);
void Caco_Frame(void);
void Clock_Frame(void);
qboolean SG_HookOffhandReady(edict_t *ent);

static qboolean sg_level_setup_attempted;

/* ---- level ------------------------------------------------------------- */

qboolean SG_LevelSetup(void)
{
	sg_level_setup_attempted = true;
	return SG_RuneLevelBegin(level.mapname) ? true : false;
}

void SG_LevelSetupAfterRuneWrite(void)
{
	if (!SG_RuneLevelCurrent())
		(void)SG_LevelSetup();
}

void SG_LevelChange(void)
{
	int i;

	SG_RuneLevelClear();
	SG_RuneSourceAuthorityReset();
	sg_level_setup_attempted = false;
	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active)
			SG_BotSlotInit(&sg_bots[i]);
}

void SG_RuneLevelStorageWillFree(void)
{
	SG_RuneLevelClear();
	sg_level_setup_attempted = false;
}

void SG_BotSlotInit(sg_bot_t *bot)
{
	edict_t *ent = bot->ent;
	qboolean active = bot->active;
	unsigned long long token = bot->instance_token;
	int lives = bot->lives;

	memset(bot, 0, sizeof(*bot));
	bot->ent = ent;
	bot->active = active;
	bot->instance_token = token;
	bot->lives = lives;
	bot->cell = SG_RUNE_CX_INDEX_NONE;
	bot->destination_cell = SG_RUNE_CX_INDEX_NONE;
	bot->flight_capability = SG_RUNE_CX_INDEX_NONE;
	bot->step.kind = SG_RUNE_STEP_HOLD;
	bot->role = SG_ROLE_ATTACK;
	bot->last_role = SG_ROLE_ATTACK;
}

/* ---- roles and destinations --------------------------------------------- */

static qboolean BotCarrying(edict_t *e)
{
	return e && e->client && ClientHasFlag(e) != NULL;
}

static edict_t *TeamCarrier(int team)
{
	int client;

	for (client = 0; client < game.maxclients; client++)
	{
		edict_t *entity = &g_edicts[client + 1];

		if (entity->inuse && entity->client &&
			entity->client->ctf.teamnum == team && ClientHasFlag(entity))
			return entity;
	}
	return NULL;
}

static int RoleFor(sg_bot_t *bot, qboolean carrying)
{
	int forced;

	if (carrying)
		return SG_ROLE_CARRY;
	forced = SG_ChatOrderedRole(bot->ent);
	if (forced >= 0 && forced < SG_ROLES &&
		(forced != SG_ROLE_ESCORT || SG_ChatEscortTarget(bot->ent)))
	{
		if (forced == SG_ROLE_DEFEND)
			bot->def_stand = true;
		return forced;
	}
	if (TeamCarrier(SG_EnemyTeam(bot->ent->client->ctf.teamnum)) &&
		!TeamCarrier(bot->ent->client->ctf.teamnum))
		return SG_ROLE_RECOVER;
	return SG_ROLE_ATTACK;
}

/* Where a team's flag is now: with its carrier, or wherever the flag entity
 * stands (its base, or the ground it was dropped on). */
static qboolean FlagNow(int team, vec3_t out)
{
	edict_t *flag = ctf_flagsearch(team);
	edict_t *carrier = TeamCarrier(SG_EnemyTeam(team));
	int i;

	if (carrier)
	{
		VectorCopy(carrier->s.origin, out);
		return true;
	}
	/* A dropped flag is a second entity of the same class, solid. */
	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *e = &g_edicts[i];

		if (e->inuse && e->classname && flag && flag->classname &&
			e != flag && strcmp(e->classname, flag->classname) == 0 &&
			e->solid != SOLID_NOT && e->item)
		{
			VectorCopy(e->s.origin, out);
			return true;
		}
	}
	if (flag)
	{
		VectorCopy(flag->s.origin, out);
		return true;
	}
	return false;
}

static qboolean FlagHome(int team, vec3_t out)
{
	edict_t *flag = ctf_flagsearch(team);

	if (!flag)
		return false;
	VectorCopy(flag->s.origin, out);
	return true;
}

static qboolean DestinationFor(sg_bot_t *bot, int role, vec3_t out)
{
	edict_t *e = bot->ent;
	int team = e->client->ctf.teamnum;
	int enemy = SG_EnemyTeam(team);
	edict_t *target;

	switch (role)
	{
	case SG_ROLE_CARRY:
		return FlagHome(team, out);
	case SG_ROLE_RECOVER:
		return FlagNow(team, out);
	case SG_ROLE_ESCORT:
		target = SG_ChatEscortTarget(e);
		if (!target)
			target = TeamCarrier(team);
		if (target)
		{
			VectorCopy(target->s.origin, out);
			return true;
		}
		return FlagNow(enemy, out);
	case SG_ROLE_DEFEND:
		target = TeamCarrier(enemy);
		if (target)
		{
			VectorCopy(target->s.origin, out);
			return true;
		}
		return FlagHome(team, out);
	default:
		return FlagNow(enemy, out);
	}
}

/* ---- the frame ------------------------------------------------------------ */

static short Move(float value)
{
	if (!isfinite(value))
		return 0;
	if (value > 400.0f)
		value = 400.0f;
	else if (value < -400.0f)
		value = -400.0f;
	return (short)lrintf(value);
}

static sg_host_hook_phase_t HookPhase(const sg_bot_t *bot, const edict_t *e)
{
	if (e->client->hookstate == 1 && e->client->hook)
		return SG_HOST_HOOK_IN_FLIGHT;
	if (e->client->hookstate == 2 && e->client->hook)
		return SG_HOST_HOOK_ATTACHED;
	if (bot->hook_phase == 3 && e->client->hookstate == 0)
		return SG_HOST_HOOK_COAST;
	return SG_HOST_HOOK_IDLE;
}

static void ThinkDead(sg_bot_t *bot, edict_t *e)
{
	usercmd_t cmd;

	if (!bot->death_taught)
	{
		bot->death_taught = true;
		bot->lives++;
	}
	bot->cell = SG_RUNE_CX_INDEX_NONE;
	bot->destination_cell = SG_RUNE_CX_INDEX_NONE;
	bot->flight_capability = SG_RUNE_CX_INDEX_NONE;
	bot->step.kind = SG_RUNE_STEP_HOLD;
	bot->hook_phase = 0;
	bot->hook_entity = NULL;
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 100;
	/* Respawn consumes a fresh latched press, so pulse attack at 5 Hz. */
	cmd.buttons = (((int)(level.time * 10.0f)) & 2) ? BUTTON_ATTACK : 0;
	ClientThink(e, &cmd);
}

/* The step for this frame: on a floor, the field's step; in the air, the
 * flight the body is on (or a fresh trace of where it is falling), steered
 * toward its landing. */
static void SelectStep(sg_bot_t *bot, edict_t *e, const vec3_t destination)
{
	const sg_rune_field_t *field;
	float violation;
	int crouching = (e->client->ps.pmove.pm_flags & PMF_DUCKED) != 0;
	qboolean supported = e->groundentity != NULL;
	qboolean swimming = e->waterlevel >= 2;

	memset(&bot->step, 0, sizeof(bot->step));
	bot->step.kind = SG_RUNE_STEP_HOLD;
	bot->crouching = (uint8_t)crouching;
	bot->cell = SG_RuneLevelLocate(e->s.origin, crouching, &violation);
	if (bot->cell == SG_RUNE_CX_INDEX_NONE)
		return;
	/* The destination's cell, resolved again when the point moves. */
	if (bot->destination_cell == SG_RUNE_CX_INDEX_NONE ||
		VectorLength(bot->destination) == 0.0f ||
		fabsf(destination[0] - bot->destination[0]) > 24.0f ||
		fabsf(destination[1] - bot->destination[1]) > 24.0f ||
		fabsf(destination[2] - bot->destination[2]) > 48.0f)
	{
		VectorCopy(destination, bot->destination);
		bot->destination_cell = SG_RuneLevelLocate(destination, 0, NULL);
		if (bot->destination_cell == SG_RUNE_CX_INDEX_NONE)
		{
			/* Items and carriers float above their floor. */
			vec3_t lowered;

			VectorCopy(destination, lowered);
			lowered[2] -= 24.0f;
			bot->destination_cell = SG_RuneLevelLocate(lowered, 0, NULL);
		}
	}
	if (bot->destination_cell == SG_RUNE_CX_INDEX_NONE)
		return;
	bot->airborne = (uint8_t)(!supported && !swimming);
	if (bot->airborne)
	{
		/* Riding a chosen flight: steer to its landing.  Otherwise trace
		 * where this fall goes and steer there. */
		if (bot->flight_capability == SG_RUNE_CX_INDEX_NONE)
		{
			sg_rune_flight_t flight;
			vec3_t velocity;

			VectorCopy(e->velocity, velocity);
			if (SG_RuneFlightTrace(&sg_rune_level.artifact.complex,
				&sg_rune_level.artifact.law, bot->cell, e->s.origin, velocity,
				&flight) && (flight.outcome == SG_RUNE_FLIGHT_LANDED ||
				flight.outcome == SG_RUNE_FLIGHT_WATER))
				VectorCopy(flight.landing, bot->flight_landing);
			else
				VectorCopy(e->s.origin, bot->flight_landing);
		}
		bot->step.kind = SG_RUNE_STEP_CROSS;
		bot->step.cell = bot->cell;
		bot->step.portal = SG_RUNE_CX_INDEX_NONE;
		bot->step.capability = bot->flight_capability;
		bot->step.move_kind = SG_RUNE_MOVE_AIR_CONTROL;
		bot->step.crouching_now = (uint8_t)crouching;
		VectorCopy(bot->flight_landing, bot->step.target);
		return;
	}
	bot->flight_capability = SG_RUNE_CX_INDEX_NONE;
	field = SG_RuneLevelField(bot->destination_cell);
	if (!field)
		return;
	(void)SG_RuneStepSelect(&sg_rune_level.router, field, bot->cell, crouching,
		destination, &bot->step);
	if (bot->step.kind == SG_RUNE_STEP_CROSS &&
		(bot->step.move_kind == SG_RUNE_MOVE_JUMP ||
		 bot->step.move_kind == SG_RUNE_MOVE_DROP ||
		 bot->step.move_kind == SG_RUNE_MOVE_ROCKET_JUMP))
	{
		const sg_rune_move_capability_t *record =
			&sg_rune_level.artifact.movement.capabilities[bot->step.capability];

		/* A flight: its landing is where the executor steers once the
		 * body leaves the floor. */
		if (record->seconds > 0.0f)
		{
			bot->flight_capability = bot->step.capability;
			VectorCopy(&sg_rune_level.router.cell_center[record->destination * 3U],
				bot->flight_landing);
		}
	}
}

static void Emit(sg_bot_t *bot, edict_t *e)
{
	usercmd_t cmd;
	sg_tactic_body_t body;
	sg_tactic_command_t command;
	qboolean engaged = false;
	qboolean moving;

	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 100;
	memset(&body, 0, sizeof(body));
	VectorCopy(e->s.origin, body.origin);
	VectorCopy(e->velocity, body.velocity);
	body.view_height = (float)e->viewheight;
	body.supported = e->groundentity != NULL ? 1U : 0U;
	body.waterlevel = (uint8_t)e->waterlevel;
	body.crouched = (e->client->ps.pmove.pm_flags & PMF_DUCKED) != 0 ? 1U : 0U;
	body.hook_phase = HookPhase(bot, e);
	body.launcher_ready = SG_CombatRocketLauncherReady(e) ? 1U : 0U;
	body.hook_ready = SG_HookOffhandReady(e) ? 1U : 0U;
	body.gravity = sv_gravity->value;
	body.frame_ms = SG_HOST_ENGINE_FRAME_MS;
	body.substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	if (!SG_TacticControl(&bot->step, &body, &command))
	{
		memset(&command, 0, sizeof(command));
		command.status = SG_TACTIC_COMMAND_HOLD;
	}
	moving = command.status != SG_TACTIC_COMMAND_HOLD && command.speed > 0.0f &&
		isfinite(command.direction[0]) && isfinite(command.direction[1]);
	if (moving)
	{
		float yaw = atan2f(command.direction[1], command.direction[0]) *
			180.0f / (float)M_PI;

		cmd.angles[YAW] = (short)(ANGLE2SHORT(yaw) -
			e->client->ps.pmove.delta_angles[YAW]);
	}
	if (command.aim_owned)
	{
		cmd.angles[YAW] = (short)(ANGLE2SHORT(command.yaw) -
			e->client->ps.pmove.delta_angles[YAW]);
		cmd.angles[PITCH] = (short)(ANGLE2SHORT(command.pitch) -
			e->client->ps.pmove.delta_angles[PITCH]);
	}
	else
		SG_CombatFrame(e, &cmd, &engaged);
	bot->engaged_last = engaged;
	if (moving)
	{
		float command_yaw = (float)SHORT2ANGLE(((int)cmd.angles[YAW] +
			e->client->ps.pmove.delta_angles[YAW]) & 65535);
		float radians = command_yaw * (float)M_PI / 180.0f;

		cmd.forwardmove = Move(400.0f * command.speed *
			(command.direction[0] * cosf(radians) +
			 command.direction[1] * sinf(radians)));
		cmd.sidemove = Move(400.0f * command.speed *
			(command.direction[0] * sinf(radians) -
			 command.direction[1] * cosf(radians)));
	}
	cmd.upmove = Move(400.0f * command.up);
	if (command.attack)
		cmd.buttons |= BUTTON_ATTACK;
	ClientThink(e, &cmd);
	if (command.want_launcher)
		SG_CombatRequestRocketLauncher(e);
	if (command.hook_fire && SG_HookOffhandReady(e))
	{
		Cmd_Hook_f(e);
		bot->hook_phase = 2;
		bot->hook_entity = e->client->hook;
	}
	else if (command.hook_release && e->client->hookstate != 0)
	{
		ctf_hook_abort(e);
		bot->hook_phase = 3;
		bot->hook_entity = NULL;
	}
	if (bot->hook_phase == 3 && e->groundentity && e->client->hookstate == 0)
		bot->hook_phase = 0;
}

/* A body that has not moved a body length in two seconds while it has a
 * crossing to make is stuck: forget the destination's cell so the route is
 * taken again from where the body actually is, and hop once. */
static qboolean Stuck(sg_bot_t *bot, edict_t *e)
{
	float dx = e->s.origin[0] - bot->stuck_origin[0];
	float dy = e->s.origin[1] - bot->stuck_origin[1];
	float dz = e->s.origin[2] - bot->stuck_origin[2];

	if (dx * dx + dy * dy + dz * dz > 32.0f * 32.0f ||
		bot->step.kind != SG_RUNE_STEP_CROSS)
	{
		VectorCopy(e->s.origin, bot->stuck_origin);
		bot->stuck_since = level.time;
		return false;
	}
	if (level.time - bot->stuck_since < 2.0f)
		return false;
	bot->stuck_since = level.time;
	bot->destination_cell = SG_RUNE_CX_INDEX_NONE;
	return true;
}

void SG_BotThink(sg_bot_t *bot)
{
	edict_t *e = bot->ent;
	qboolean carrying;
	vec3_t destination;

	if (!e || !e->client)
		return;
	if (e->deadflag)
	{
		ThinkDead(bot, e);
		return;
	}
	bot->death_taught = false;
	carrying = BotCarrying(e);
	bot->role = RoleFor(bot, carrying);
	bot->last_role = bot->role;
	if (carrying && !bot->was_carrying)
		bot->carry_start = level.time;
	bot->was_carrying = carrying;
	if (!SG_RuneLevelCurrent() || !DestinationFor(bot, bot->role, destination))
	{
		memset(&bot->step, 0, sizeof(bot->step));
		bot->step.kind = SG_RUNE_STEP_HOLD;
		Emit(bot, e);
		return;
	}
	SelectStep(bot, e, destination);
	if (Stuck(bot, e) && e->groundentity)
		bot->step.move_kind = SG_RUNE_MOVE_JUMP;
	Emit(bot, e);
}

void SG_RunFrame(void)
{
	int i;

	(void)SG_HostLawProductionEnsureLevel(level.mapname);
	if (!sg_level_setup_attempted)
		(void)SG_LevelSetup();
	Caco_Frame();
	Botfill_Frame();
	Clock_Frame();
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *ent;

		if (!sg_bots[i].active)
			continue;
		ent = sg_bots[i].ent;
		if (!ent || !ent->inuse || !ent->client || !(ent->flags & FL_BOT))
		{
			if (ent && ent->client && !ent->inuse && (ent->flags & FL_BOT))
				SG_FreeClientEdict(ent);
			SG_DisownBot(ent);
			continue;
		}
		if (ent->client->ctf.teamnum != CTF_TEAM_RED &&
			ent->client->ctf.teamnum != CTF_TEAM_BLUE)
		{
			SG_RetireBotForClient(ent);
			continue;
		}
		SG_BotThink(&sg_bots[i]);
	}
}
