#include "../g_local.h"
#include "../g_ctffunc.h"
#undef world
#include "sg_local.h"
#include "sg_bot.h"
#include "sg_bot_combat.h"

#include <math.h>
#include <string.h>

#include "sg_cvars.h"
#include "sg_hooks.h"
#include "sg_persona.h"
#include "sg_util.h"
#include "sg_weapon_effect_profile.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- the weapons the bot can hold ------------------------------------------ */

typedef struct weapon_slot_s
{
	const char *pickup;       /* FindItem name */
	sg_weapon_profile_id_t profile;
	int rank;                 /* tie-break only: later is better */
} weapon_slot_t;

/* Every weapon the profiles describe, by the item name the game uses. */
static const weapon_slot_t sg_weapons[] = {
	{ "Blaster", SG_WEAPON_PROFILE_BLASTER, 0 },
	{ "Shotgun", SG_WEAPON_PROFILE_SHOTGUN, 1 },
	{ "Super Shotgun", SG_WEAPON_PROFILE_SUPER_SHOTGUN, 2 },
	{ "Machinegun", SG_WEAPON_PROFILE_MACHINEGUN, 3 },
	{ "Chaingun", SG_WEAPON_PROFILE_CHAINGUN, 4 },
	{ "Grenade Launcher", SG_WEAPON_PROFILE_GRENADE_LAUNCHER, 5 },
	{ "Rocket Launcher", SG_WEAPON_PROFILE_ROCKET_LAUNCHER, 6 },
	{ "HyperBlaster", SG_WEAPON_PROFILE_HYPERBLASTER, 7 },
	{ "Railgun", SG_WEAPON_PROFILE_RAILGUN, 8 },
	{ "BFG10K", SG_WEAPON_PROFILE_BFG, 9 },
};
#define WEAPON_COUNT (sizeof(sg_weapons) / sizeof(sg_weapons[0]))

static gitem_t *sg_weapon_items[WEAPON_COUNT];
static gitem_t *sg_hook_item;
static const sg_weapon_profile_t *sg_weapon_profiles[WEAPON_COUNT];
static qboolean sg_weapons_cached;

static void CacheWeapons(void)
{
	size_t index;

	if (sg_weapons_cached)
		return;
	sg_weapons_cached = true;
	for (index = 0U; index < WEAPON_COUNT; index++)
	{
		sg_weapon_items[index] = FindItem((char *)sg_weapons[index].pickup);
		if (!SG_WeaponProfileLookup(sg_weapons[index].profile,
			&sg_weapon_profiles[index]))
			sg_weapon_profiles[index] = NULL;
	}
	sg_hook_item = FindItem("Grappling Hook");
}

static int SlotOfItem(const gitem_t *item)
{
	size_t index;

	for (index = 0U; index < WEAPON_COUNT; index++)
		if (sg_weapon_items[index] == item)
			return (int)index;
	return -1;
}

/* Ammo in the pocket for a weapon, and whether one shot is affordable. */
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
	const gitem_t *item = sg_weapon_items[slot];

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
	tr = sg_host.trace(eye, NULL, NULL, point, (edict_t *)self, MASK_OPAQUE);
	if (tr.fraction == 1.0f)
		return true;
	point[2] += other->viewheight;
	tr = sg_host.trace(eye, NULL, NULL, point, (edict_t *)self, MASK_OPAQUE);
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
			state->target_since = level.time;
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
	int grade = SG_PersonaAimGrade((edict_t *)self);

	if (skill < 0.0f)
		skill = 0.0f;
	if (skill > 4.0f)
		skill = 4.0f;
	if (grade >= 0)
		skill = (skill + (4.0f - (float)grade)) * 0.5f;
	return 4.0f - skill * 0.85f;    /* 4 degrees at skill 0, 0.6 at 4 */
}

static float LandChance(const sg_weapon_profile_t *profile, float range,
	float error_degrees, float target_speed)
{
	float target_radius = 24.0f;    /* half the body's height, roughly */
	float angular = range > 1.0f ? atanf(target_radius / range) * 180.0f /
		(float)M_PI : 90.0f;
	float spread = error_degrees;
	float chance;

	if (profile->effects & SG_WEAPON_EFFECT_SPREAD)
		spread += profile->yaw_spread_degrees > 0.0f ?
			profile->yaw_spread_degrees : 3.0f;
	if (profile->effects & SG_WEAPON_EFFECT_PROJECTILE &&
		profile->projectile_speed > 0.0f)
	{
		/* Where the target will be is uncertain by how far it can move in
		 * the flight, against the lead's own guess. */
		float flight = range / profile->projectile_speed;
		float drift = target_speed * flight * 0.5f;

		spread += range > 1.0f ? atanf(drift / range) * 180.0f / (float)M_PI :
			0.0f;
	}
	chance = spread > 0.0f ? angular / (angular + spread) : 1.0f;
	if (profile->splash_radius > 0.0f && range > 1.0f)
	{
		float splash_angular = atanf(profile->splash_radius / range) * 180.0f /
			(float)M_PI;
		float splash_chance = splash_angular / (splash_angular + spread);

		if (splash_chance > chance)
			chance = chance + (splash_chance - chance) *
				(profile->splash_damage / (profile->direct_damage + 1.0f));
	}
	return chance > 1.0f ? 1.0f : chance;
}

static float ExpectedDamagePerSecond(const sg_weapon_profile_t *profile,
	float range, float error_degrees, float target_speed)
{
	float shots_per_second = profile->cadence_ms > 0U ?
		1000.0f / (float)profile->cadence_ms : 10.0f;
	float per_shot = profile->direct_damage;

	if (profile->projectile_count_min > 1U)
		per_shot *= (float)profile->projectile_count_min;
	if (per_shot <= 0.0f && profile->splash_damage > 0.0f)
		per_shot = profile->splash_damage;
	if (profile->effects & SG_WEAPON_EFFECT_HITSCAN &&
		profile->ray_distance > 0.0f && range > profile->ray_distance)
		return 0.0f;
	return per_shot * shots_per_second *
		LandChance(profile, range, error_degrees, target_speed);
}

/* A splash weapon is unsafe when its blast would reach the bot itself or a
 * teammate at the impact point. */
static qboolean SplashSafe(const edict_t *self, const sg_weapon_profile_t *profile,
	const vec3_t impact)
{
	float radius = profile->splash_radius + 16.0f;
	vec3_t delta;
	int i;

	if (profile->splash_radius <= 0.0f)
		return true;
	VectorSubtract(impact, self->s.origin, delta);
	if (VectorLength(delta) < radius && self->health < profile->splash_damage *
		profile->self_damage_scale + 20.0f)
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

/* The weapon to hold against this target at this range; the one in hand
 * keeps its place unless another is clearly better, so a target drifting
 * across a range boundary does not cost a switch every frame. */
static int Choose(edict_t *self, combat_state_t *state, float range,
	float target_speed, const vec3_t impact)
{
	float error = AimErrorDegrees(self);
	int held = SlotOfItem(self->client->pers.weapon);
	int best = -1;
	float best_value = 0.0f, held_value = 0.0f;
	size_t index;

	for (index = 0U; index < WEAPON_COUNT; index++)
	{
		const sg_weapon_profile_t *profile = sg_weapon_profiles[index];
		float value;

		if (!profile || !Affordable(self, (int)index))
			continue;
		if (!SplashSafe(self, profile, impact))
			continue;
		value = ExpectedDamagePerSecond(profile, range, error, target_speed);
		if ((int)index == held)
			held_value = value;
		if (value > best_value || (value == best_value && best >= 0 &&
			sg_weapons[index].rank > sg_weapons[best].rank))
		{
			best_value = value;
			best = (int)index;
		}
	}
	if (best < 0)
		return held;
	if (held >= 0 && held_value > 0.0f && best_value < held_value * 1.25f)
		return held;
	if (state->choice == best || level.time - state->choice_at > 0.5f)
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
static void AimPoint(const edict_t *self, const sg_weapon_profile_t *profile,
	const edict_t *target, vec3_t point)
{
	vec3_t eye, delta;
	float range;

	Eye(self, eye);
	VectorCopy(target->s.origin, point);
	VectorSubtract(point, eye, delta);
	range = VectorLength(delta);
	if (profile && (profile->effects & SG_WEAPON_EFFECT_PROJECTILE) &&
		profile->projectile_speed > 0.0f)
	{
		float flight = range / profile->projectile_speed;

		vec3_t velocity;

		/* Lead a target that has been moving the same way. */
		VectorCopy(target->velocity, velocity);
		VectorMA(point, flight, velocity, point);
		if (target->groundentity == NULL)
			point[2] -= 0.5f * sv_gravity->value * flight * flight;
		if (profile->gravity_scale > 0.0f)
		{
			/* A lobbed projectile drops g t^2 / 2 over its flight: aim that
			 * much higher, plus what its own upward launch gives back. */
			float g = sv_gravity->value * profile->gravity_scale;

			point[2] += 0.5f * g * flight * flight -
				profile->launch_vertical_speed * flight;
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
static qboolean ShotLands(edict_t *self, const sg_weapon_profile_t *profile,
	const edict_t *target, float yaw, float pitch, vec3_t impact_out)
{
	vec3_t eye, forward, end, angles, delta;
	trace_t tr;
	float reach = profile && profile->ray_distance > 0.0f ?
		profile->ray_distance : 8192.0f;

	Eye(self, eye);
	angles[PITCH] = pitch;
	angles[YAW] = yaw;
	angles[ROLL] = 0.0f;
	AngleVectors(angles, forward, NULL, NULL);
	VectorMA(eye, reach, forward, end);
	tr = sg_host.trace(eye, NULL, NULL, end, self, MASK_SHOT);
	VectorCopy(tr.endpos, impact_out);
	if (tr.ent == target)
		return true;
	if (profile && profile->splash_radius > 0.0f)
	{
		VectorSubtract(tr.endpos, target->s.origin, delta);
		if (VectorLength(delta) < profile->splash_radius * 0.8f)
			return true;
	}
	return false;
}

/* ---- the frame ------------------------------------------------------------------- */

void SG_BotCombatFrame(edict_t *self, usercmd_t *cmd, qboolean *engaged_out)
{
	combat_state_t *state;
	edict_t *target;
	const sg_weapon_profile_t *profile = NULL;
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
	if (!target)
	{
		/* Nothing to fight: the view follows the walk; keep the best
		 * general-purpose weapon in hand for what comes next. */
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
	if (slot >= 0)
		profile = sg_weapon_profiles[slot];
	AimPoint(self, profile, target, point);
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
		self->client->weaponstate != WEAPON_READY || !profile)
		return;
	if (!ShotLands(self, profile, target, yaw, pitch, impact))
		return;
	if (!SplashSafe(self, profile, impact))
		return;
	cmd->buttons |= BUTTON_ATTACK;
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
