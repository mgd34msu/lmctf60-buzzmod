#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world
#include "sg_local.h"
#include "sg_bot.h"
#include "sg_bot_callout.h"
#include "sg_bot_combat.h"

#include <math.h>
#include <string.h>

#include "sg_bot_cvars.h"
#include "sg_rune_level.h"
#include "sg_bot_persona.h"
#include "sg_bot_util.h"
#include "sg_bot_weapons.h"
#include "sg_rune_fire.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- the weapons the bot can hold ------------------------------------------ */

#define WEAPON_COUNT 10

static gitem_t *sg_weapon_items[WEAPON_COUNT];
static gitem_t *sg_hook_item;
static qboolean sg_weapons_cached;

static void CacheWeapons(void)
{
	int index;

	if (sg_weapons_cached)
		return;
	sg_weapons_cached = true;
	for (index = 0; index < WEAPON_COUNT && index < SG_BotWeaponCount(); index++)
		sg_weapon_items[index] = FindItem((char *)SG_BotWeapon(index)->item);
	sg_hook_item = FindItem("Grappling Hook");
}

static const sg_bot_weapon_t *Weapon(int slot)
{
	return slot >= 0 && slot < WEAPON_COUNT ? SG_BotWeapon(slot) : NULL;
}

static int SlotOfItem(const gitem_t *item)
{
	int index;

	if (!item)
		return -1;
	for (index = 0; index < WEAPON_COUNT; index++)
		if (sg_weapon_items[index] == item)
			return index;
	return -1;
}

static int AmmoFor(const edict_t *self, const gitem_t *item)
{
	gitem_t *ammo;

	if (!item->ammo)
		return 999;
	ammo = FindItem(item->ammo);
	return ammo ? self->client->pers.inventory[ITEM_INDEX(ammo)] : 0;
}

static qboolean Affordable(const edict_t *self, int slot)
{
	const gitem_t *item = slot >= 0 ? sg_weapon_items[slot] : NULL;

	if (!item || self->client->pers.inventory[ITEM_INDEX(item)] <= 0)
		return false;
	return AmmoFor(self, item) >= (item->quantity > 0 ? item->quantity : 1);
}

/* ---- per-client memory ------------------------------------------------------ */

typedef struct seen_s
{
	vec3_t origin;
	vec3_t velocity;
	float at;                 /* level.time last seen */
} seen_t;

typedef struct combat_state_s
{
	seen_t seen[MAX_CLIENTS];
	int target;               /* client index + 1 of the engaged enemy, or 0 */
	float target_since;
	float aim_yaw, aim_pitch; /* the view as last commanded: the slew state */
	qboolean aim_valid;
	float error_yaw, error_pitch;
	float error_next;         /* when the tremor re-samples */
	int choice;               /* weapon slot chosen last frame, -1 none */
	float choice_at;
	uint32_t random;
	vec3_t shoot_point;
	float shoot_until;        /* level.time the request lasts to */
} combat_state_t;

static combat_state_t sg_combat[MAX_CLIENTS];

static combat_state_t *StateOf(const edict_t *self)
{
	int ci = (int)(self->client - game.clients);

	return ci >= 0 && ci < MAX_CLIENTS ? &sg_combat[ci] : NULL;
}

void SG_BotCombatReset(void)
{
	memset(sg_combat, 0, sizeof(sg_combat));
	sg_weapons_cached = false;
}

void SG_BotCombatResetClient(edict_t *self)
{
	combat_state_t *state = self && self->client ? StateOf(self) : NULL;

	if (state)
	{
		memset(state, 0, sizeof(*state));
		state->choice = -1;
	}
}

static float RandomUnit(combat_state_t *state)
{
	state->random = state->random * 1664525U + 1013904223U;
	return (float)(state->random >> 8) / 16777216.0f;
}

/* ---- perception ------------------------------------------------------------- */

static void Eye(const edict_t *self, vec3_t eye)
{
	VectorCopy(self->s.origin, eye);
	eye[2] += self->viewheight;
}

/* Can the bot see this body: a clear line to its centre or its head. */
static qboolean Visible(const edict_t *self, const edict_t *other)
{
	vec3_t eye, point;
	trace_t tr;

	Eye(self, eye);
	VectorCopy(other->s.origin, point);
	tr = gi.trace(eye, NULL, NULL, point, (edict_t *)self, MASK_OPAQUE);
	if (tr.fraction == 1.0f)
		return true;
	point[2] += other->viewheight;
	tr = gi.trace(eye, NULL, NULL, point, (edict_t *)self, MASK_OPAQUE);
	return tr.fraction == 1.0f;
}

static qboolean Enemy(const edict_t *self, const edict_t *other)
{
	return other != self && other->inuse && other->client &&
		other->health > 0 && !other->deadflag &&
		other->movetype != MOVETYPE_NOCLIP &&
		other->client->ctf.teamnum != self->client->ctf.teamnum &&
		(other->client->ctf.teamnum == CTF_TEAM_RED ||
		 other->client->ctf.teamnum == CTF_TEAM_BLUE);
}

/* Look: note every visible enemy; pick the target.  The enemy carrying our
 * flag outranks anything; otherwise the nearest visible one; a target seen
 * within the last two seconds is kept while nothing better shows. */
#define CARRIER_GUARD_RANGE 600.0f

/* Our team's flag carrier, if any. */
static edict_t *TeamCarrier(int team)
{
	int i;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *e = &g_edicts[i];

		if (e->inuse && e->client && e->health > 0 &&
			e->client->ctf.teamnum == team && ClientHasFlag(e))
			return e;
	}
	return NULL;
}

static edict_t *Look(edict_t *self, combat_state_t *state)
{
	int i, best = 0;
	float best_score = -1.0f;
	edict_t *kept = NULL;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *other = &g_edicts[i];
		float distance, score;

		if (!Enemy(self, other) || !Visible(self, other))
			continue;
		VectorCopy(other->s.origin, state->seen[i - 1].origin);
		VectorCopy(other->velocity, state->seen[i - 1].velocity);
		state->seen[i - 1].at = level.time;
		distance = VectorLength(other->s.origin) - 0.0f;
		{
			vec3_t delta;

			VectorSubtract(other->s.origin, self->s.origin, delta);
			distance = VectorLength(delta);
		}
		score = 4096.0f - distance;
		if (ClientHasFlag(other))
			score += 8192.0f;
		/* An enemy close to our carrier is the one to stop. */
		{
			edict_t *carrier = TeamCarrier(self->client->ctf.teamnum);

			if (carrier && carrier != self)
			{
				vec3_t to_carrier;

				VectorSubtract(other->s.origin, carrier->s.origin, to_carrier);
				if (VectorLength(to_carrier) < CARRIER_GUARD_RANGE)
					score += 4096.0f;
			}
		}
		if (i == state->target)
			score += 256.0f;    /* stay on the one already engaged */
		if (score > best_score)
		{
			best_score = score;
			best = i;
		}
	}
	if (best)
	{
		if (best != state->target)
		{
			state->target_since = level.time;
			SG_BotCalloutSeen(self, &g_edicts[best]);
		}
		state->target = best;
		return &g_edicts[best];
	}
	if (state->target && level.time - state->seen[state->target - 1].at < 2.0f)
	{
		kept = &g_edicts[state->target];
		if (Enemy(self, kept))
			return kept;
	}
	state->target = 0;
	return NULL;
}

/* A sound carries as far as its attenuation lets it; every bot on the
 * other team within that reach learns where the emitter was, as if seen. */
void SG_NoteSound(edict_t *emitter, vec3_t origin, int channel,
	int soundindex, float volume, float attenuation)
{
	int i;
	float reach;
	vec3_t where;

	(void)channel;
	(void)soundindex;
	if (!emitter || !emitter->client || volume <= 0.0f)
		return;
	reach = attenuation <= 0.0f ? 8192.0f : 1000.0f / attenuation;
	if (origin)
		VectorCopy(origin, where);
	else
		VectorCopy(emitter->s.origin, where);
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *self = sg_bots[i].ent;
		combat_state_t *state;
		vec3_t delta;
		int ci;

		if (!sg_bots[i].active || !self || !self->client || !Enemy(self, emitter))
			continue;
		VectorSubtract(where, self->s.origin, delta);
		if (VectorLength(delta) > reach)
			continue;
		state = StateOf(self);
		ci = (int)(emitter->client - game.clients);
		if (!state || ci < 0 || ci >= MAX_CLIENTS)
			continue;
		VectorCopy(where, state->seen[ci].origin);
		VectorCopy(emitter->velocity, state->seen[ci].velocity);
		state->seen[ci].at = level.time;
		if (!state->target)
		{
			state->target = ci + 1;
			state->target_since = level.time;
		}
	}
}

/* ---- weapon choice ------------------------------------------------------------ */

/* Expected damage per second of a weapon at this range, from its profile:
 * damage per shot times the chance the shot lands times shots per second.
 * The chance a shot lands is the target's angular size against the
 * weapon's spread plus the bot's aim error; a splash weapon also lands its
 * splash when the direct shot misses by less than the radius; a projectile
 * loses to a moving target by its flight time. */
static float AimErrorDegrees(const edict_t *self)
{
	float skill = sg_cv.skill ? sg_cv.skill->value : 3.0f;
	const sg_bot_persona_t *persona = SG_BotPersona(self);
	int grade = persona ? persona->aim : 0;

	if (skill < 0.0f)
		skill = 0.0f;
	if (skill > 4.0f)
		skill = 4.0f;
	skill += (float)grade * 0.5f;
	if (skill < 0.0f)
		skill = 0.0f;
	if (skill > 4.0f)
		skill = 4.0f;
	return 4.0f - skill * 0.85f;    /* 4 degrees at skill 0, 0.6 at 4 */
}

/* The chance one trigger lands on a body at this range, under this aim
 * error: the body's angular size against the error plus the weapon's own
 * spread plus, for a projectile, how far the target can drift during the
 * flight.  A burst widens what counts as landing, by the share of the
 * damage the burst carries. */
static float LandChance(const sg_bot_weapon_t *weapon, float range,
	float error_degrees, float target_speed)
{
	float target_radius = 24.0f;
	float angular = range > 1.0f ? atanf(target_radius / range) * 180.0f /
		(float)M_PI : 90.0f;
	float spread = error_degrees + weapon->spread;
	float chance;

	if (weapon->speed > 0.0f)
	{
		float flight = range / weapon->speed;
		float drift = target_speed * flight * 0.5f;

		spread += range > 1.0f ? atanf(drift / range) * 180.0f / (float)M_PI : 0.0f;
	}
	chance = spread > 0.0f ? angular / (angular + spread) : 1.0f;
	if (weapon->radius > 0.0f && range > 1.0f)
	{
		float burst_angular = atanf(weapon->radius / range) * 180.0f / (float)M_PI;
		float burst_chance = burst_angular / (burst_angular + spread);

		if (burst_chance > chance)
			chance += (burst_chance - chance) * (weapon->burst / (weapon->hit + 1.0f));
	}
	return chance > 1.0f ? 1.0f : chance;
}

static float ExpectedDamagePerSecond(const sg_bot_weapon_t *weapon, float range,
	float error_degrees, float target_speed)
{
	float triggers_per_second = weapon->seconds > 0.0f ? 1.0f / weapon->seconds : 10.0f;
	float per_trigger = weapon->hit * (float)(weapon->pellets > 1 ? weapon->pellets : 1) *
		(float)(weapon->shots > 1 ? weapon->shots : 1);

	if (per_trigger <= 0.0f && weapon->burst > 0.0f)
		per_trigger = weapon->burst;
	if (range > weapon->limit)
		return 0.0f;
	return per_trigger * triggers_per_second *
		LandChance(weapon, range, error_degrees, target_speed);
}

/* A burst weapon is unsafe when its burst would reach the bot itself with
 * too little health to spare, or a teammate at the impact point. */
static qboolean SplashSafe(const edict_t *self, const sg_bot_weapon_t *weapon,
	const vec3_t impact)
{
	float radius = weapon->radius + 16.0f;
	vec3_t delta;
	int i;

	if (weapon->radius <= 0.0f)
		return true;
	VectorSubtract(impact, self->s.origin, delta);
	if (VectorLength(delta) < radius &&
		self->health < weapon->burst * weapon->self_burst + 20.0f)
		return false;
	for (i = 1; i <= game.maxclients; i++)
	{
		const edict_t *mate = &g_edicts[i];

		if (mate == self || !mate->inuse || !mate->client || mate->health <= 0 ||
			mate->client->ctf.teamnum != self->client->ctf.teamnum)
			continue;
		VectorSubtract(impact, mate->s.origin, delta);
		if (VectorLength(delta) < radius)
			return false;
	}
	return true;
}

/* What the rune says about the pair of cells the fight is in, against
 * what this weapon reaches through: a full match when the shot itself
 * gets there, a part when only the burst or the lob does, nothing when
 * neither.  With no relation recorded, the live sight decides alone. */
static float FireScale(const sg_bot_weapon_t *weapon, uint32_t relation)
{
	uint32_t open;

	if (relation == 0U)
		return 1.0f;
	open = weapon->reach & relation;
	if (open & (SG_RUNE_FIRE_LINE | SG_RUNE_FIRE_CORRIDOR))
		return 1.0f;
	if (open & SG_RUNE_FIRE_BLAST)
		return 0.6f;
	if (open & SG_RUNE_FIRE_LOB)
		return 0.5f;
	return 0.0f;
}

static uint32_t FireRelation(const edict_t *self, const vec3_t target_origin)
{
	uint32_t here = SG_RuneLevelLocate(self->s.origin,
		(self->client->ps.pmove.pm_flags & PMF_DUCKED) != 0, NULL);
	uint32_t there = SG_RuneLevelLocate(target_origin, 0, NULL);

	if (here == SG_RUNE_CX_INDEX_NONE || there == SG_RUNE_CX_INDEX_NONE)
		return 0U;
	return SG_RuneLevelFire(here, there);
}

/* The weapon to hold against this target at this range: the highest
 * expected damage the rune allows, the one in hand keeping its place
 * unless another is clearly better. */
static int Choose(edict_t *self, combat_state_t *state, float range,
	float target_speed, const vec3_t impact)
{
	float error = AimErrorDegrees(self);
	int held = SlotOfItem(self->client->pers.weapon);
	int best = -1;
	float best_value = 0.0f, held_value = 0.0f;
	uint32_t relation = range < 1.0e5f ? FireRelation(self, impact) : 0U;
	int index;

	for (index = 0; index < WEAPON_COUNT; index++)
	{
		const sg_bot_weapon_t *weapon = Weapon(index);
		float value;

		if (!weapon || !Affordable(self, index))
			continue;
		if (!SplashSafe(self, weapon, impact))
			continue;
		value = ExpectedDamagePerSecond(weapon, range, error, target_speed) *
			FireScale(weapon, relation);
		if (index == held)
			held_value = value;
		if (value > best_value || (value == best_value && index > best))
		{
			best_value = value;
			best = index;
		}
	}
	if (best < 0)
		return held;
	/* A switch in progress finishes before another is asked for; a held
	 * weapon nearly as good stays; a change holds for two seconds. */
	if (held >= 0 && self->client->weaponstate != WEAPON_READY)
		return held;
	if (held >= 0 && held_value > 0.0f && best_value < held_value * 1.25f)
		return held;
	if (state->choice == best || level.time - state->choice_at > 2.0f)
	{
		state->choice = best;
		state->choice_at = level.time;
		return best;
	}
	return held >= 0 ? held : best;
}

static void Hold(edict_t *self, int slot)
{
	gitem_t *item = slot >= 0 ? sg_weapon_items[slot] : NULL;

	if (item && self->client->pers.weapon != item && self->client->newweapon != item)
		self->client->newweapon = item;
}

/* ---- aim ----------------------------------------------------------------------- */

/* Where to point for this weapon: the target's centre, led by its velocity
 * over the projectile's flight, and for a lobbed projectile raised by the
 * arc the map's gravity needs. */
static void AimPoint(const edict_t *self, const sg_bot_weapon_t *weapon,
	const edict_t *target, vec3_t point)
{
	vec3_t eye, delta;
	float range;

	Eye(self, eye);
	VectorCopy(target->s.origin, point);
	VectorSubtract(point, eye, delta);
	range = VectorLength(delta);
	if (weapon && weapon->speed > 0.0f)
	{
		float flight = range / weapon->speed;

		vec3_t velocity;

		/* Lead a target that has been moving the same way. */
		VectorCopy(target->velocity, velocity);
		VectorMA(point, flight, velocity, point);
		if (target->groundentity == NULL)
			point[2] -= 0.5f * sv_gravity->value * flight * flight;
		if (weapon->falls)
		{
			/* A lobbed projectile drops g t^2 / 2 over its flight: aim that
			 * much higher, less what its own upward launch gives back. */
			point[2] += 0.5f * sv_gravity->value * flight * flight -
				weapon->rise * flight;
		}
	}
}

static void AnglesFor(const vec3_t from, const vec3_t to, float *yaw,
	float *pitch)
{
	vec3_t delta;
	float flat;

	VectorSubtract(to, from, delta);
	flat = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
	*yaw = atan2f(delta[1], delta[0]) * 180.0f / (float)M_PI;
	*pitch = -atan2f(delta[2], flat) * 180.0f / (float)M_PI;
}

static float WrapDegrees(float degrees)
{
	while (degrees > 180.0f)
		degrees -= 360.0f;
	while (degrees < -180.0f)
		degrees += 360.0f;
	return degrees;
}

/* The view moves toward the wanted angles no faster than the turn rate,
 * and carries a tremor that re-samples a few times a second: the hand,
 * not a servo. */
static void Slew(combat_state_t *state, float want_yaw, float want_pitch,
	float error_degrees, float *yaw_out, float *pitch_out)
{
	float rate = (sg_cv.turnrate ? sg_cv.turnrate->value : 720.0f) * 0.1f;
	float dy, dp;

	if (level.time >= state->error_next)
	{
		state->error_yaw = (RandomUnit(state) * 2.0f - 1.0f) * error_degrees;
		state->error_pitch = (RandomUnit(state) * 2.0f - 1.0f) * error_degrees;
		state->error_next = level.time + 0.2f + RandomUnit(state) * 0.3f;
	}
	want_yaw += state->error_yaw;
	want_pitch += state->error_pitch;
	if (!state->aim_valid)
	{
		state->aim_yaw = want_yaw;
		state->aim_pitch = want_pitch;
		state->aim_valid = true;
	}
	dy = WrapDegrees(want_yaw - state->aim_yaw);
	dp = want_pitch - state->aim_pitch;
	if (dy > rate)
		dy = rate;
	if (dy < -rate)
		dy = -rate;
	if (dp > rate)
		dp = rate;
	if (dp < -rate)
		dp = -rate;
	state->aim_yaw = WrapDegrees(state->aim_yaw + dy);
	state->aim_pitch = state->aim_pitch + dp;
	if (state->aim_pitch > 89.0f)
		state->aim_pitch = 89.0f;
	if (state->aim_pitch < -89.0f)
		state->aim_pitch = -89.0f;
	*yaw_out = state->aim_yaw;
	*pitch_out = state->aim_pitch;
}

/* The shot as it would leave: from the eye along the view, to where it
 * stops.  Fire when it reaches the target's body, or lands its splash
 * within the radius of the target while safe for us and ours. */
static qboolean ShotLands(edict_t *self, const sg_bot_weapon_t *weapon,
	const edict_t *target, float yaw, float pitch, vec3_t impact_out)
{
	vec3_t eye, forward, end, angles, delta;
	trace_t tr;
	float reach = weapon && weapon->limit > 0.0f ? weapon->limit : 8192.0f;

	Eye(self, eye);
	angles[PITCH] = pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0.0f;
	AngleVectors(angles, forward, NULL, NULL);
	VectorMA(eye, reach, forward, end);
	tr = gi.trace(eye, NULL, NULL, end, self, MASK_SHOT);
	VectorCopy(tr.endpos, impact_out);
	if (!target)
		return tr.fraction < 1.0f;
	if (tr.ent == target)
		return true;
	if (weapon && weapon->radius > 0.0f)
	{
		VectorSubtract(tr.endpos, target->s.origin, delta);
		if (VectorLength(delta) < weapon->radius * 0.8f)
			return true;
	}
	/* A near miss still fires when the ray gets as far as the target and
	 * passes within the body plus what the weapon's own spread covers at
	 * that range: waiting for the tremor to line up exactly is not how a
	 * fight is fought. */
	{
		vec3_t to_target, along;
		float distance, ahead, miss, allowed;

		VectorSubtract(target->s.origin, eye, to_target);
		distance = VectorLength(to_target);
		ahead = DotProduct(to_target, forward);
		if (ahead <= 0.0f || tr.fraction * reach < distance - 40.0f)
			return false;
		VectorScale(forward, ahead, along);
		VectorSubtract(to_target, along, delta);
		miss = VectorLength(delta);
		/* Within the body plus the wider of the weapon's spread and the
		 * aim's own error cone at this range: the shot is taken, and the
		 * tremor decides whether it lands. */
		{
			float cone = tanf(AimErrorDegrees(self) * (float)M_PI / 180.0f);
			float spread = weapon && weapon->spread > 0.0f ?
				tanf(weapon->spread * (float)M_PI / 180.0f) : 0.0f;

			allowed = 20.0f + distance * (cone > spread ? cone : spread);
		}
		return miss <= allowed;
	}
}

/* ---- the frame ------------------------------------------------------------------- */

static void CombatDebug(const edict_t *self, const edict_t *target, int slot,
	float range, const char *what)
{
	if (!sg_cv.debug || !sg_cv.debug->value || level.framenum % 50 != 0)
		return;
	gi.dprintf("SGFIGHT %s target=%s range=%.0f slot=%d held=%s state=%d %s\n",
		self->client->pers.netname, target->client->pers.netname, range, slot,
		self->client->pers.weapon ? self->client->pers.weapon->pickup_name : "none",
		self->client->weaponstate, what);
}

void SG_BotCombatFrame(edict_t *self, usercmd_t *cmd, qboolean *engaged_out)
{
	combat_state_t *state;
	edict_t *target;
	const sg_bot_weapon_t *weapon = NULL;
	vec3_t eye, point, impact, delta;
	float range, want_yaw, want_pitch, yaw, pitch, target_speed;
	int slot;

	if (engaged_out)
		*engaged_out = false;
	if (!self || !self->client || !cmd)
		return;
	CacheWeapons();
	state = StateOf(self);
	if (!state)
		return;
	target = Look(self, state);
	if (!target && level.time < state->shoot_until)
	{
		/* Something to shoot and nothing to fight: aim at the point with
		 * whatever is in hand and fire when the shot reaches it. */
		vec3_t impact, delta;
		int held = SlotOfItem(self->client->pers.weapon);

		Eye(self, eye);
		AnglesFor(eye, state->shoot_point, &want_yaw, &want_pitch);
		Slew(state, want_yaw, want_pitch, 0.5f, &yaw, &pitch);
		cmd->angles[YAW] = (short)(ANGLE2SHORT(yaw) -
			self->client->ps.pmove.delta_angles[YAW]);
		cmd->angles[PITCH] = (short)(ANGLE2SHORT(pitch) -
			self->client->ps.pmove.delta_angles[PITCH]);
		if (engaged_out)
			*engaged_out = true;
		if (held >= 0 && self->client->weaponstate == WEAPON_READY &&
			ShotLands(self, Weapon(held), NULL, yaw, pitch, impact))
		{
			VectorSubtract(impact, state->shoot_point, delta);
			if (VectorLength(delta) < 48.0f)
				cmd->buttons |= BUTTON_ATTACK;
		}
		return;
	}
	if (!target)
	{
		/* Nothing to fight: the view follows the walk; keep the best
		 * general-purpose weapon in hand for what comes next. */
		if (sg_cv.debug && sg_cv.debug->value && level.framenum % 50 == 0)
		{
			int i, nearest = 0;
			float nearest_range = 1.0e9f;

			for (i = 1; i <= game.maxclients; i++)
			{
				edict_t *other = &g_edicts[i];
				vec3_t d;
				float r;

				if (!Enemy(self, other))
					continue;
				VectorSubtract(other->s.origin, self->s.origin, d);
				r = VectorLength(d);
				if (r < nearest_range)
				{
					nearest_range = r;
					nearest = i;
				}
			}
			if (nearest)
				gi.dprintf("SGFIGHT %s no target; nearest enemy %s range=%.0f "
					"visible=%d\n", self->client->pers.netname,
					g_edicts[nearest].client->pers.netname, nearest_range,
					Visible(self, &g_edicts[nearest]) ? 1 : 0);
			else
				gi.dprintf("SGFIGHT %s no target; no enemy in the game\n",
					self->client->pers.netname);
		}
		state->aim_valid = false;
		if (level.time - state->choice_at > 2.0f)
		{
			vec3_t nowhere = { 1.0e6f, 1.0e6f, 1.0e6f };

			Hold(self, Choose(self, state, 600.0f, 0.0f, nowhere));
		}
		return;
	}
	Eye(self, eye);
	VectorSubtract(target->s.origin, eye, delta);
	range = VectorLength(delta);
	target_speed = VectorLength(target->velocity);
	slot = Choose(self, state, range, target_speed, target->s.origin);
	Hold(self, slot);
	weapon = Weapon(slot);
	AimPoint(self, weapon, target, point);
	AnglesFor(eye, point, &want_yaw, &want_pitch);
	Slew(state, want_yaw, want_pitch, AimErrorDegrees(self), &yaw, &pitch);
	cmd->angles[YAW] = (short)(ANGLE2SHORT(yaw) -
		self->client->ps.pmove.delta_angles[YAW]);
	cmd->angles[PITCH] = (short)(ANGLE2SHORT(pitch) -
		self->client->ps.pmove.delta_angles[PITCH]);
	if (engaged_out)
		*engaged_out = true;
	/* Fire only with the chosen weapon in hand and ready, when the shot as
	 * aimed lands, and never into our own blast. */
	if (self->client->pers.weapon != (slot >= 0 ? sg_weapon_items[slot] : NULL) ||
		self->client->weaponstate != WEAPON_READY || !weapon)
	{
		CombatDebug(self, target, slot, range, "weapon not ready");
		return;
	}
	if (!ShotLands(self, weapon, target, yaw, pitch, impact))
	{
		CombatDebug(self, target, slot, range, "shot misses");
		return;
	}
	if (!SplashSafe(self, weapon, impact))
	{
		CombatDebug(self, target, slot, range, "splash unsafe");
		return;
	}
	CombatDebug(self, target, slot, range, "fire");
	cmd->buttons |= BUTTON_ATTACK;
}

void SG_BotCombatShootAt(edict_t *self, const vec3_t point)
{
	combat_state_t *state = self && self->client ? StateOf(self) : NULL;

	if (!state || !point)
		return;
	VectorCopy(point, state->shoot_point);
	state->shoot_until = level.time + 0.2f;
}

/* ---- the executor's questions ------------------------------------------------------ */

qboolean SG_BotLauncherReady(edict_t *self)
{
	gitem_t *launcher;

	if (!self || !self->client)
		return false;
	CacheWeapons();
	launcher = sg_weapon_items[6];
	return launcher && self->client->pers.weapon == launcher &&
		self->client->weaponstate == WEAPON_READY &&
		AmmoFor(self, launcher) >= 1 && self->health > 60;
}

void SG_BotRequestLauncher(edict_t *self)
{
	gitem_t *launcher;

	if (!self || !self->client)
		return;
	CacheWeapons();
	launcher = sg_weapon_items[6];
	if (launcher && self->client->pers.inventory[ITEM_INDEX(launcher)] > 0 &&
		AmmoFor(self, launcher) >= 1)
		Hold(self, 6);
}

qboolean SG_BotHookReady(edict_t *self)
{
	if (!self || !self->client || self->movetype == MOVETYPE_NOCLIP)
		return false;
	CacheWeapons();
	if (!((int)ctfflags->value & CTF_OFFHAND_HOOK))
		return false;
	if (self->client->hook || self->client->hookstate != 0)
		return false;
	return sg_hook_item &&
		self->client->pers.inventory[ITEM_INDEX(sg_hook_item)] > 0 &&
		self->client->pers.weapon != sg_hook_item;
}
