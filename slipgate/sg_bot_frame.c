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

#include "sg_bot_orders.h"
#include "sg_bot_callout.h"
#include "sg_bot_combat.h"
#include "sg_bot_items.h"
#include "sg_cvars.h"
#include "sg_client_ownership.h"
#include "sg_host_engine_pmove.h"
#include "sg_host_law_owner.h"
#include "sg_hooks.h"
#include "sg_net.h"
#include "sg_rune_flight.h"
#include "sg_rune_level.h"
#include "sg_rune_mechanisms.h"
#include "sg_rune_source_authority_owner.h"
#include "sg_tactic_controller.h"
#include "sg_util.h"

void Cmd_Hook_f(edict_t *ent);
void ClientThink(edict_t *ent, usercmd_t *ucmd);

static qboolean sg_level_setup_attempted;

/* ---- level ------------------------------------------------------------- */

/* Loads this level's RUNE once; a second call while it is current for
 * the same map is a no-op, so adding a bot never reloads the artifact. */
qboolean SG_LevelSetup(void)
{
	if (SG_RuneLevelCurrent() &&
		!strcmp(sg_rune_level.mapname, level.mapname))
		return true;
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
	SG_OrdersReset();
	SG_BotCombatReset();
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
	bot->task_mechanism = SG_RUNE_CX_INDEX_NONE;
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

typedef struct team_pass_s
{
	int assigned[SG_MAXBOTS];
	int previous[SG_MAXBOTS];  /* last frame's assignment, for stickiness */
} team_pass_t;

static team_pass_t sg_team_pass;

#define ROLE_KEEP_SCALE 0.6f     /* a held role counts this much nearer */

static uint32_t StandingCellNear(const vec3_t point);

#define ESCORT_GAP_SECONDS 1.0f   /* an escort holds this far behind the carrier */
#define CARRIER_DETOUR_HEALTH 40  /* under this a carrier will go for health */

uint32_t SG_BotStandingCellNear(const vec3_t point)
{
	return StandingCellNear(point);
}

static int RoleFor(sg_bot_t *bot, qboolean carrying)
{
	int slot = (int)(bot - sg_bots);
	int role;

	if (carrying)
		return SG_ROLE_CARRY;
	role = slot >= 0 && slot < SG_MAXBOTS ? sg_team_pass.assigned[slot] : -1;
	if (role < 0 || role >= SG_ROLES)
		role = SG_ROLE_ATTACK;
	if (role == SG_ROLE_DEFEND)
		bot->def_stand = true;
	return role;
}

/* Where a team's flag is now: with its carrier, or wherever the game's one
 * flag entity for that team stands (its base, or where it was dropped). */
static qboolean FlagNow(int team, vec3_t out)
{
	edict_t *flag = ctf_flagsearch(team);
	edict_t *carrier = TeamCarrier(SG_EnemyTeam(team));

	if (carrier)
	{
		VectorCopy(carrier->s.origin, out);
		return true;
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

/* The team pass: once per frame per team, before any bot thinks.  Roles
 * are assigned across the team's bots so that our flag is never left
 * unguarded, a taken flag is chased by the bots nearest it, our carrier is
 * escorted by the bot nearest to it, and the rest attack.  Human orders
 * override the assignment for the bot they name. */
static float DistanceTo(const edict_t *e, const vec3_t point)
{
	vec3_t delta;

	VectorSubtract(point, e->s.origin, delta);
	return VectorLength(delta);
}

/* The nearest free bot to a point for a role.  The bot that held the role
 * last frame counts as nearer than it is, so a role does not flip between
 * two bots at like distances every frame. */
static int NearestUnassigned(int team, const vec3_t point, int role)
{
	int i, best = -1;
	float best_distance = 1.0e30f;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *e = sg_bots[i].ent;
		float distance;

		if (!sg_bots[i].active || !e || !e->client ||
			e->client->ctf.teamnum != team || sg_team_pass.assigned[i] >= 0 ||
			e->health <= 0)
			continue;
		distance = DistanceTo(e, point);
		if (sg_team_pass.previous[i] == role)
			distance *= ROLE_KEEP_SCALE;
		if (distance < best_distance)
		{
			best_distance = distance;
			best = i;
		}
	}
	return best;
}

static void TeamPass(int team)
{
	int i, count = 0, defenders = 0;
	edict_t *our_carrier = TeamCarrier(team);
	edict_t *their_carrier = TeamCarrier(SG_EnemyTeam(team));
	vec3_t home, flag_now;
	qboolean have_home = FlagHome(team, home);

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *e = sg_bots[i].ent;

		if (!sg_bots[i].active || !e || !e->client ||
			e->client->ctf.teamnum != team)
			continue;
		sg_team_pass.assigned[i] = -1;
		if (ClientHasFlag(e))
			sg_team_pass.assigned[i] = SG_ROLE_CARRY;
		else
		{
			int forced = SG_OrderedRole(e);

			if (forced >= 0 && forced < SG_ROLES &&
				(forced != SG_ROLE_ESCORT || SG_OrderEscortTarget(e)))
				sg_team_pass.assigned[i] = forced;
		}
		if (sg_team_pass.assigned[i] == SG_ROLE_DEFEND)
			defenders++;
		count++;
	}
	if (count == 0)
		return;
	/* Our flag is out: the two bots nearest to it recover. */
	if (their_carrier && FlagNow(team, flag_now))
	{
		int recoverers = count >= 4 ? 2 : 1;

		while (recoverers-- > 0)
		{
			int slot = NearestUnassigned(team, flag_now, SG_ROLE_RECOVER);

			if (slot < 0)
				break;
			sg_team_pass.assigned[slot] = SG_ROLE_RECOVER;
		}
	}
	/* Someone stays home when nobody was told to and there are enough of
	 * us; with three or more, one defender; with five or more, two. */
	if (have_home && defenders == 0 && count >= 3)
	{
		int want = count >= 5 ? 2 : 1;

		while (want-- > 0)
		{
			int slot = NearestUnassigned(team, home, SG_ROLE_DEFEND);

			if (slot < 0)
				break;
			sg_team_pass.assigned[slot] = SG_ROLE_DEFEND;
		}
	}
	/* Our carrier gets the nearest free bot as escort. */
	if (our_carrier)
	{
		int slot = NearestUnassigned(team, our_carrier->s.origin, SG_ROLE_ESCORT);

		if (slot >= 0)
			sg_team_pass.assigned[slot] = SG_ROLE_ESCORT;
	}
	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent && sg_bots[i].ent->client &&
			sg_bots[i].ent->client->ctf.teamnum == team &&
			sg_team_pass.assigned[i] < 0)
			sg_team_pass.assigned[i] = SG_ROLE_ATTACK;
	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent && sg_bots[i].ent->client &&
			sg_bots[i].ent->client->ctf.teamnum == team)
			sg_team_pass.previous[i] = sg_team_pass.assigned[i];
}

/* Which of its team's defenders this bot is, in roster order: the first
 * takes the best post, the second the next. */
static int DefendRank(const sg_bot_t *bot)
{
	int slot = (int)(bot - sg_bots), i, rank = 0;

	for (i = 0; i < slot && i < SG_MAXBOTS; i++)
	{
		const sg_bot_t *other = &sg_bots[i];

		if (other->ent && other->ent->inuse && other->ent->client &&
			sg_team_pass.assigned[i] == SG_ROLE_DEFEND &&
			other->ent->client->ctf.teamnum == bot->ent->client->ctf.teamnum)
			rank++;
	}
	return rank;
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
		target = SG_OrderEscortTarget(e);
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
		bot->post_facing_valid = false;
		if (target)
		{
			VectorCopy(target->s.origin, out);
			return true;
		}
		/* The flag at home: stand where the rune says the approaches are
		 * covered, the second defender where the first leaves gaps. */
		if (FlagHome(team, out))
		{
			uint32_t flag_cell = StandingCellNear(out);
			vec3_t post, facing;

			if (flag_cell != SG_RUNE_CX_INDEX_NONE &&
				SG_RuneLevelDefendPost(flag_cell, DefendRank(bot), post, facing))
			{
				VectorCopy(post, out);
				VectorCopy(facing, bot->post_facing);
				bot->post_facing_valid = true;
			}
			return true;
		}
		return false;
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
	SG_BotCombatResetClient(e);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 100;
	/* Respawn consumes a fresh latched press, so pulse attack at 5 Hz. */
	cmd.buttons = (((int)(level.time * 10.0f)) & 2) ? BUTTON_ATTACK : 0;
	ClientThink(e, &cmd);
}

/* The cell a body would stand in at or near a point: flags, items, and
 * players sit at various heights over their floor, so try the standing
 * origin above the point first, then the point, then higher and lower. */
static uint32_t StandingCellNear(const vec3_t point)
{
	static const float rises[] = { 24.0f, 0.0f, 48.0f, -24.0f, 72.0f, 12.0f };
	uint32_t index;

	for (index = 0U; index < sizeof(rises) / sizeof(rises[0]); index++)
	{
		vec3_t probe;
		uint32_t cell;

		VectorCopy(point, probe);
		probe[2] += rises[index];
		cell = SG_RuneLevelLocate(probe, 0, NULL);
		if (cell != SG_RUNE_CX_INDEX_NONE)
			return cell;
	}
	return SG_RUNE_CX_INDEX_NONE;
}

/* A crossing that failed on this body is avoided for a while. */
#define AVOID_SECONDS 30.0f
#define RIDE_STALL_SECONDS 1.5f
#define RIDE_STALL_DISTANCE 24.0f

static void Avoid(sg_bot_t *bot, uint32_t capability)
{
	int i, oldest = 0;

	if (capability == SG_RUNE_CX_INDEX_NONE)
		return;
	for (i = 0; i < SG_BOT_AVOID; i++)
	{
		if (bot->avoid[i] == capability)
		{
			bot->avoid_until[i] = level.time + AVOID_SECONDS;
			return;
		}
		if (bot->avoid_until[i] < bot->avoid_until[oldest])
			oldest = i;
	}
	bot->avoid[oldest] = capability;
	bot->avoid_until[oldest] = level.time + AVOID_SECONDS;
}

static uint32_t Avoided(const sg_bot_t *bot, uint32_t list[SG_BOT_AVOID])
{
	uint32_t count = 0U;
	int i;

	for (i = 0; i < SG_BOT_AVOID; i++)
		if (bot->avoid_until[i] > level.time)
			list[count++] = bot->avoid[i];
	return count;
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
		bot->destination_cell = StandingCellNear(destination);
	}
	if (bot->destination_cell == SG_RUNE_CX_INDEX_NONE)
	{
		if (sg_cv.debug && sg_cv.debug->value && level.framenum % 50 == 0)
			gi.dprintf("SGBOT %s destination (%.0f %.0f %.0f) has no cell\n",
				e->client->pers.netname, destination[0], destination[1],
				destination[2]);
		return;
	}
	/* A rope out or attached: the hook crossing goes on, whatever is under
	 * the body, until the rope is let go; then the body is on a flight to
	 * the record's landing. */
	if (e->client->hookstate != 0 && bot->flight_capability != SG_RUNE_CX_INDEX_NONE)
	{
		const sg_rune_move_capability_t *record =
			&sg_rune_level.artifact.movement.capabilities[bot->flight_capability];

		if (record->kind == SG_RUNE_MOVE_HOOK)
		{
			/* A rope that pulls the body nowhere is let go, and that ride
			 * is not tried again for a while. */
			if (e->client->hookstate == 2)
			{
				vec3_t moved;

				if (bot->ride_since <= 0.0f)
				{
					bot->ride_since = level.time;
					VectorCopy(e->s.origin, bot->ride_origin);
				}
				VectorSubtract(e->s.origin, bot->ride_origin, moved);
				if (VectorLength(moved) >= RIDE_STALL_DISTANCE)
				{
					bot->ride_since = level.time;
					VectorCopy(e->s.origin, bot->ride_origin);
				}
				else if (level.time - bot->ride_since > RIDE_STALL_SECONDS)
				{
					ctf_hook_abort(e);
					bot->hook_phase = 3;
					bot->hook_entity = NULL;
					Avoid(bot, bot->flight_capability);
					bot->flight_capability = SG_RUNE_CX_INDEX_NONE;
					bot->ride_since = 0.0f;
					if (sg_cv.debug && sg_cv.debug->value)
						gi.dprintf("SGBOT %s rope stalled: ride avoided\n",
							e->client->pers.netname);
					goto grounded;
				}
			}
			else
				bot->ride_since = 0.0f;
			bot->airborne = (uint8_t)!supported;
			bot->step.kind = SG_RUNE_STEP_CROSS;
			bot->step.cell = bot->cell;
			bot->step.portal = SG_RUNE_CX_INDEX_NONE;
			bot->step.capability = bot->flight_capability;
			bot->step.move_kind = SG_RUNE_MOVE_HOOK;
			bot->step.crouching_now = (uint8_t)crouching;
			VectorCopy(bot->flight_landing, bot->step.target);
			VectorCopy(record->anchor, bot->step.hook_point);
			bot->step.hook_point_present = 1U;
			bot->step.hook_release_distance = record->parameter;
			return;
		}
	}
grounded:
	bot->ride_since = 0.0f;
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
	/* Into or out of the enemy base, the route keeps out of the lines the
	 * enemy's defenders hold where it can. */
	field = NULL;
	if (bot->role == SG_ROLE_ATTACK || bot->role == SG_ROLE_CARRY ||
		bot->role == SG_ROLE_ESCORT)
	{
		vec3_t enemy_home;

		if (FlagHome(SG_EnemyTeam(e->client->ctf.teamnum), enemy_home))
		{
			uint32_t enemy_flag_cell = StandingCellNear(enemy_home);

			if (enemy_flag_cell != SG_RUNE_CX_INDEX_NONE)
				field = SG_RuneLevelFieldExposed(bot->destination_cell, enemy_flag_cell);
		}
	}
	if (!field)
		field = SG_RuneLevelField(bot->destination_cell);
	if (!field)
		return;
	/* An item worth the detour becomes the destination for now; the goal's
	 * own field prices the detour.  A carrier never detours unless it is
	 * about to die, and then only for health. */
	if (bot->role != SG_ROLE_CARRY || e->health < CARRIER_DETOUR_HEALTH)
	{
		vec3_t item_point;

		if (SG_BotItemDetour(bot, field, item_point))
		{
			uint32_t item_cell = StandingCellNear(item_point);
			const sg_rune_field_t *item_field = item_cell != SG_RUNE_CX_INDEX_NONE ?
				SG_RuneLevelField(item_cell) : NULL;

			if (item_field)
			{
				uint32_t avoid[SG_BOT_AVOID];
				uint32_t avoid_count = Avoided(bot, avoid);

				(void)SG_RuneStepSelectAvoiding(&sg_rune_level.router, item_field,
					bot->cell, crouching, item_point, avoid, avoid_count, &bot->step);
				goto flight;
			}
		}
	}
	{
		uint32_t avoid[SG_BOT_AVOID];
		uint32_t avoid_count = Avoided(bot, avoid);

		(void)SG_RuneStepSelectAvoiding(&sg_rune_level.router, field, bot->cell,
			crouching, destination, avoid, avoid_count, &bot->step);
	}
	/* An escort keeps a second behind the carrier rather than on top of
	 * it: close enough to fight what reaches the carrier, out of its way. */
	if (bot->role == SG_ROLE_ESCORT && bot->step.kind == SG_RUNE_STEP_CROSS &&
		bot->step.cost_to_go < ESCORT_GAP_SECONDS)
	{
		bot->step.kind = SG_RUNE_STEP_ARRIVED;
		bot->step.portal = SG_RUNE_CX_INDEX_NONE;
		bot->step.capability = SG_RUNE_CX_INDEX_NONE;
		VectorCopy(e->s.origin, bot->step.target);
	}
flight:
	/* Standing where the complex knows no floor (an entity's top, a brush
	 * model): walk to the nearest floor the field reaches from. */
	if (bot->step.kind == SG_RUNE_STEP_UNREACHABLE && supported)
	{
		vec3_t point;
		const sg_rune_field_t *route = SG_RuneLevelField(bot->destination_cell);

		if (route && SG_RuneFieldNearestReachable(&sg_rune_level.router,
			&sg_rune_level.locator, route, e->s.origin, 640.0f, point) !=
			SG_RUNE_CX_INDEX_NONE)
		{
			memset(&bot->step, 0, sizeof(bot->step));
			bot->step.kind = SG_RUNE_STEP_CROSS;
			bot->step.cell = bot->cell;
			bot->step.portal = SG_RUNE_CX_INDEX_NONE;
			bot->step.capability = SG_RUNE_CX_INDEX_NONE;
			bot->step.move_kind = SG_RUNE_MOVE_WALK;
			bot->step.crouching_now = (uint8_t)crouching;
			VectorCopy(point, bot->step.target);
		}
	}
	if (bot->step.kind == SG_RUNE_STEP_CROSS &&
		(bot->step.move_kind == SG_RUNE_MOVE_JUMP ||
		 bot->step.move_kind == SG_RUNE_MOVE_DROP ||
		 bot->step.move_kind == SG_RUNE_MOVE_ROCKET_JUMP ||
		 bot->step.move_kind == SG_RUNE_MOVE_HOOK ||
		 bot->step.move_kind == SG_RUNE_MOVE_EXTERNAL_FORCE))
	{
		const sg_rune_move_capability_t *record =
			&sg_rune_level.artifact.movement.capabilities[bot->step.capability];

		/* A flight: its landing is where the executor steers once the
		 * body leaves the floor. */
		if (record->seconds > 0.0f)
		{
			const float *landing =
				&sg_rune_level.router.cell_center[record->destination * 3U];

			bot->flight_capability = bot->step.capability;
			VectorCopy(landing, bot->flight_landing);
		}
	}
}

#define MOVER_STATE_TOP 0
#define MOVER_STATE_UP 2

/* The step goes through a mechanism: what the body does about it now,
 * from the mechanism's live state. */
static void ApplyMechanism(sg_bot_t *bot, edict_t *e)
{
	const sg_rune_move_capability_t *capability;
	const sg_rune_mech_t *record;
	edict_t *mover;
	vec3_t point;

	if (bot->step.kind != SG_RUNE_STEP_CROSS ||
		bot->step.capability == SG_RUNE_CX_INDEX_NONE)
	{
		bot->task_mechanism = SG_RUNE_CX_INDEX_NONE;
		return;
	}
	capability = &sg_rune_level.artifact.movement.capabilities[bot->step.capability];
	if (capability->mechanism == SG_RUNE_CX_INDEX_NONE)
	{
		bot->task_mechanism = SG_RUNE_CX_INDEX_NONE;
		return;
	}
	record = &sg_rune_level.artifact.mechanisms.records[capability->mechanism];
	mover = SG_RuneLevelMechanismEdict(capability->mechanism);
	switch (record->kind)
	{
	case SG_RUNE_MECH_PUSH:
		/* Walk into the pad; the launch is the map's. */
		VectorCopy(record->origin, point);
		point[2] = record->mins[2] + 24.0f;
		VectorCopy(point, bot->step.target);
		bot->step.move_kind = SG_RUNE_MOVE_WALK;
		break;
	case SG_RUNE_MECH_TELEPORTER:
		VectorCopy(record->origin, bot->step.target);
		bot->step.target[2] += 24.0f;
		bot->step.move_kind = SG_RUNE_MOVE_WALK;
		break;
	case SG_RUNE_MECH_PLATFORM:
		if (!mover)
			break;
		if (e->groundentity == mover)
		{
			/* Riding: wait for the top, then walk off toward the field's
			 * destination floor. */
			if (mover->moveinfo.state != MOVER_STATE_TOP)
			{
				bot->step.kind = SG_RUNE_STEP_HOLD;
				break;
			}
			bot->step.move_kind = SG_RUNE_MOVE_WALK;
			break;
		}
		/* Get on: the middle of its top where it is now. */
		bot->step.target[0] = record->origin[0];
		bot->step.target[1] = record->origin[1];
		bot->step.target[2] = mover->absmax[2] + 24.0f;
		bot->step.move_kind = SG_RUNE_MOVE_WALK;
		break;
	case SG_RUNE_MECH_TRAIN:
		if (!mover)
			break;
		if (e->groundentity == mover)
		{
			vec3_t delta;

			VectorSubtract(bot->step.target, mover->s.origin, delta);
			delta[2] = 0.0f;
			if (VectorLength(delta) > 96.0f)
			{
				bot->step.kind = SG_RUNE_STEP_HOLD;   /* ride */
				break;
			}
			bot->step.move_kind = SG_RUNE_MOVE_WALK;   /* step off */
			break;
		}
		{
			vec3_t delta;

			VectorSubtract(mover->s.origin, e->s.origin, delta);
			delta[2] = 0.0f;
			if (VectorLength(delta) > 160.0f)
			{
				/* Not here yet: wait where the field says to board. */
				const float *center =
					&sg_rune_level.router.cell_center[bot->step.cell * 3U];

				VectorCopy(center, bot->step.target);
				bot->step.target[2] += 24.0f;
				bot->step.kind = SG_RUNE_STEP_ARRIVED;
				break;
			}
			bot->step.target[0] = (mover->absmin[0] + mover->absmax[0]) * 0.5f;
			bot->step.target[1] = (mover->absmin[1] + mover->absmax[1]) * 0.5f;
			bot->step.target[2] = mover->absmax[2] + 24.0f;
			bot->step.move_kind = SG_RUNE_MOVE_WALK;
		}
		break;
	case SG_RUNE_MECH_DOOR:
	{
		int open = !mover || mover->moveinfo.state == MOVER_STATE_TOP ||
			mover->moveinfo.state == MOVER_STATE_UP;

		if (open || record->activation == SG_RUNE_MECH_ACTIVATE_TOUCH)
		{
			bot->task_mechanism = SG_RUNE_CX_INDEX_NONE;
			break;
		}
		if (record->activation == SG_RUNE_MECH_ACTIVATE_SHOT)
		{
			SG_BotCombatShootAt(e, record->origin);
			bot->step.kind = SG_RUNE_STEP_HOLD;
			break;
		}
		if (record->activation == SG_RUNE_MECH_ACTIVATE_TARGETED &&
			record->activator != SG_RUNE_CX_INDEX_NONE)
		{
			const sg_rune_mech_t *worker =
				&sg_rune_level.artifact.mechanisms.records[record->activator];

			if (bot->task_mechanism != capability->mechanism)
			{
				bot->task_mechanism = capability->mechanism;
				bot->task_since = level.time;
			}
			if (level.time - bot->task_since > 12.0f)
			{
				/* It did not open for us: try the door itself. */
				bot->task_mechanism = SG_RUNE_CX_INDEX_NONE;
				break;
			}
			if (worker->activation == SG_RUNE_MECH_ACTIVATE_SHOT)
			{
				SG_BotCombatShootAt(e, worker->origin);
				bot->step.kind = SG_RUNE_STEP_HOLD;
				break;
			}
			VectorCopy(worker->origin, bot->step.target);
			bot->step.target[2] = worker->mins[2] + 24.0f;
			if (bot->step.target[2] < worker->origin[2] - 32.0f)
				bot->step.target[2] = worker->origin[2];
			bot->step.move_kind = SG_RUNE_MOVE_WALK;
			break;
		}
		bot->step.kind = SG_RUNE_STEP_HOLD;
		break;
	}
	default:
		break;
	}
}

/* Teammates do not walk through each other: a teammate close ahead pushes
 * the direction sideways, away from it, the more the closer. */
#define GIVE_WAY_REACH 56.0f

static void GiveWay(const edict_t *e, float direction[3])
{
	int i;
	float push[2] = { 0.0f, 0.0f };

	for (i = 1; i <= game.maxclients; i++)
	{
		const edict_t *other = &g_edicts[i];
		float dx, dy, flat, ahead, side, weight;

		if (other == e || !other->inuse || !other->client || other->health <= 0 ||
			other->client->ctf.teamnum != e->client->ctf.teamnum)
			continue;
		dx = other->s.origin[0] - e->s.origin[0];
		dy = other->s.origin[1] - e->s.origin[1];
		if (fabsf(other->s.origin[2] - e->s.origin[2]) > 48.0f)
			continue;
		flat = sqrtf(dx * dx + dy * dy);
		if (flat >= GIVE_WAY_REACH || flat < 1.0f)
			continue;
		ahead = (dx * direction[0] + dy * direction[1]) / flat;
		if (ahead < 0.3f)
			continue;   /* beside or behind: no need */
		/* Which side it is on, from our direction; push to the other. */
		side = direction[0] * dy - direction[1] * dx;
		weight = (GIVE_WAY_REACH - flat) / GIVE_WAY_REACH;
		if (side >= 0.0f)
		{
			push[0] += direction[1] * weight;
			push[1] += -direction[0] * weight;
		}
		else
		{
			push[0] += -direction[1] * weight;
			push[1] += direction[0] * weight;
		}
	}
	if (push[0] != 0.0f || push[1] != 0.0f)
	{
		float x = direction[0] + push[0], y = direction[1] + push[1];
		float length = sqrtf(x * x + y * y);

		if (length > 1e-3f)
		{
			direction[0] = x / length;
			direction[1] = y / length;
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
	body.launcher_ready = SG_BotLauncherReady(e) ? 1U : 0U;
	body.hook_ready = SG_BotHookReady(e) ? 1U : 0U;
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
		GiveWay(e, command.direction);
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
		SG_BotCombatFrame(e, &cmd, &engaged);
	bot->engaged_last = engaged;
	/* Posted and nothing in sight: face where the approaches are. */
	if (!engaged && !moving && bot->post_facing_valid &&
		bot->step.kind == SG_RUNE_STEP_ARRIVED)
	{
		float yaw = atan2f(bot->post_facing[1] - e->s.origin[1],
			bot->post_facing[0] - e->s.origin[0]) * 180.0f / (float)M_PI;

		cmd.angles[YAW] = (short)(ANGLE2SHORT(yaw) -
			e->client->ps.pmove.delta_angles[YAW]);
		cmd.angles[PITCH] = (short)(0 - e->client->ps.pmove.delta_angles[PITCH]);
	}
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
		SG_BotRequestLauncher(e);
	if (command.hook_fire && SG_BotHookReady(e))
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
		(bot->step.kind != SG_RUNE_STEP_CROSS &&
		 bot->step.kind != SG_RUNE_STEP_ARRIVED))
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
	SG_BotCalloutRole(bot, bot->role);
	bot->last_role = bot->role;
	if (carrying && !bot->was_carrying)
		bot->carry_start = level.time;
	bot->was_carrying = carrying;
	if (!SG_RuneLevelCurrent() || !DestinationFor(bot, bot->role, destination))
	{
		if (sg_cv.debug && sg_cv.debug->value && level.framenum % 50 == 0)
			gi.dprintf("SGBOT %s no destination: rune %d role %d own flag %p "
				"enemy flag %p\n", e->client->pers.netname, SG_RuneLevelCurrent(),
				bot->role, (void *)ctf_flagsearch(e->client->ctf.teamnum),
				(void *)ctf_flagsearch(SG_EnemyTeam(e->client->ctf.teamnum)));
		memset(&bot->step, 0, sizeof(bot->step));
		bot->step.kind = SG_RUNE_STEP_HOLD;
		Emit(bot, e);
		return;
	}
	SelectStep(bot, e, destination);
	ApplyMechanism(bot, e);
	if (Stuck(bot, e) && e->groundentity)
		bot->step.move_kind = SG_RUNE_MOVE_JUMP;
	if (sg_cv.debug && sg_cv.debug->value && level.framenum % 50 == 0)
		gi.dprintf("SGBOT %s role=%d cell=%u dest=%u step=%s/%s at=(%.0f %.0f %.0f) "
			"target=(%.0f %.0f %.0f) cost=%.1f st=%c%c v=%.0f hook=%d hp=%d\n",
			e->client->pers.netname,
			bot->role, (unsigned int)bot->cell, (unsigned int)bot->destination_cell,
			SG_RuneStepKindString(bot->step.kind),
			bot->step.kind == SG_RUNE_STEP_CROSS ? SG_RuneMoveKindString(
				(sg_rune_move_kind_t)bot->step.move_kind) : "-",
			e->s.origin[0], e->s.origin[1], e->s.origin[2], bot->step.target[0],
			bot->step.target[1], bot->step.target[2], bot->step.cost_to_go,
			(e->client->ps.pmove.pm_flags & PMF_DUCKED) ? 'C' : 'S',
			bot->step.crouching_next ? 'c' : 's', VectorLength(e->velocity),
			e->client->hookstate, e->health);
	Emit(bot, e);
}

void SG_RunFrame(void)
{
	int i;

	SG_CvarsInit();
	(void)SG_HostLawProductionEnsureLevel(level.mapname);
	if (!sg_level_setup_attempted)
		(void)SG_LevelSetup();
	Botfill_Frame();
	TeamPass(CTF_TEAM_RED);
	TeamPass(CTF_TEAM_BLUE);
	SG_BotCalloutFrame();
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
