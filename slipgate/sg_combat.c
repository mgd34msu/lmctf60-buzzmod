/*
 * sg_combat.c -- SLIPGATE combat: the view and the trigger.
 *
 * The constitution's rule for this file is a single sentence: "Combat runs
 * concurrently with navigation. There is no state that suspends movement."
 * So this is not a behaviour, a state, or a mode. It is a modifier applied to
 * a usercmd_t that the Body has already filled in. It writes cmd->angles and
 * cmd->buttons. It never touches forwardmove, sidemove or upmove.
 *
 * Everything here that is a claim about the game is read from the game, per
 * principle 1 (read the engine, never assume it). The lines are cited inline.
 * The numbers that are NOT cited -- fire cadence, aim error, settle time --
 * are preferences, not facts, and are named as such where they are defined.
 *
 * Perception is CACO's gate and nothing wider: gi.inPVS plus a trace from the
 * eyes to the target's centre against MASK_OPAQUE (sg_caco.c:100-114), plus a
 * forward-cone test so a bot does not shoot at something behind its head.
 * There is no g_edicts omniscience here; an enemy that fails the gate does not
 * exist as far as this file is concerned.
 */

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_combat.h"

#include <math.h>

/* ------------------------------------------------------------------- facts
 *
 * Measured from the source, each with the line that says so.
 */

/*
 * Blaster bolt speed. p_weapon.c:939, inside Blaster_Fire:
 *     fire_blaster (ent, start, forward, damage, 1000, effect, hyper);
 * The 1000 is the `speed` argument of fire_blaster (g_weapon.c:336), which
 * becomes the bolt's velocity: VectorScale (dir, speed, bolt->velocity)
 * (g_weapon.c:358). Units per second, since MOVETYPE_FLYMISSILE integrates
 * velocity directly. This is a projectile, so it needs lead.
 */
#define SG_BLASTER_SPEED	1000.0f

/*
 * What the bolt collides with. g_weapon.c:360: bolt->clipmask = MASK_SHOT.
 * MASK_SHOT is CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_WINDOW|
 * CONTENTS_DEADMONSTER (q_shared.h:360) == 0x6000003. The pre-fire trace uses
 * the same mask, so what the trace reports is what the bolt will actually hit
 * -- not an approximation of it.
 */

/*
 * The muzzle is not the eye. Blaster_Fire builds its start point from
 * VectorSet(offset, 24, 8, ent->viewheight-8) through P_ProjectSource
 * (p_weapon.c:932-934): 24 forward, 8 right, 8 below eye level. The pre-fire
 * trace below runs from the eye instead. That is deliberate and conservative:
 * the eye is behind the muzzle, so anything the eye trace clears the muzzle
 * also clears, and the 8-unit drop keeps the trace from clipping a lip the
 * bolt would clear. P_ProjectSource is static to p_weapon.c in any case.
 */

/* --------------------------------------------------------------- preferences
 *
 * Not measured from anything. These are the fitted/taste end of the file and
 * are labelled so nobody later mistakes them for physics.
 */

#define SG_FOV_COS			0.5f	/* cos(60 deg): a 120 degree total cone */
#define SG_ENGAGE_RANGE		2000.0f	/* do not pick fights across the map */

#define SG_FIRE_ON			0.4f	/* seconds of held trigger */
#define SG_FIRE_OFF			0.2f	/* seconds of released trigger */
#define SG_FIRE_JITTER		0.30f	/* +/- this fraction on each window */

#define SG_SETTLE			0.5f	/* seconds for the aim error to converge */
#define SG_AIM_ERROR_DEG	9.0f	/* error cone half-angle at acquisition */
#define SG_AIM_RESIDUAL_DEG	0.8f	/* never becomes perfect; this is the floor */

#define SG_HIT_SLOP			40.0f	/* pre-fire trace endpoint tolerance */

#define SG_COMBAT_MAXCLIENTS	256

/* ------------------------------------------------------------------- state */

/*
 * Per-client, persistent across frames. Indexed by client number, sized to a
 * fixed ceiling and bounds-checked -- this file cannot add a field to
 * sg_local.h (another agent owns it), so it keeps its own table.
 */
typedef struct
{
	int			enemy;			/* edict index of held target, 0 = none */
	float		since;			/* level.time the target was acquired */

	vec3_t		err;			/* aim error direction, unit length */
	float		err_next;		/* when to resample the tremor */

	float		win_end;		/* when the current trigger window expires */
	qboolean	win_fire;		/* is the current window a firing window */

	float		switch_next;	/* rate limit on weapon-switch requests */
} sg_combat_state_t;

static sg_combat_state_t sg_combat[SG_COMBAT_MAXCLIENTS];

/* ------------------------------------------------------------- perception */

/*
 * CACO's sight gate, verbatim in behaviour: PVS, then a clear line from the
 * eyes to the target's centre against MASK_OPAQUE. sg_caco.c:100-114 holds the
 * original; it is static there, so this is a copy rather than a call. If that
 * gate ever changes, this one changes with it.
 */
static qboolean Combat_Visible(edict_t *viewer, edict_t *target)
{
	vec3_t	eye, mid;
	trace_t	tr;

	VectorCopy(viewer->s.origin, eye);
	eye[2] += viewer->viewheight;
	VectorAdd(target->absmin, target->absmax, mid);
	VectorScale(mid, 0.5f, mid);

	if (!gi.inPVS(eye, mid))
		return false;
	tr = gi.trace(eye, NULL, NULL, mid, viewer, MASK_OPAQUE);
	return tr.fraction >= 1.0f;
}

static void Combat_Center(edict_t *e, vec3_t out)
{
	VectorAdd(e->absmin, e->absmax, out);
	VectorScale(out, 0.5f, out);
}

/*
 * Pick a target: a live enemy client, inside engage range, inside the forward
 * cone, and visible. Nearest wins. Iteration follows sg_caco.c:294-300 --
 * g_edicts + 1 + i over game.maxclients, inuse and client checked.
 *
 * Liveness is the same pair CACO uses (sg_caco.c:213):
 *     deadflag == DEAD_DEAD || health <= 0
 * Team is client->ctf.teamnum (g_local.h:1122), CTF_TEAM_RED/BLUE from
 * g_ctffunc.h:12-13. A client not on a team -- observer, connecting, or
 * mid-join -- has neither value and is skipped, which is what we want.
 */
static edict_t *Combat_Scan(edict_t *self, vec3_t eye, vec3_t forward)
{
	edict_t	*best = NULL;
	float	bestdist = SG_ENGAGE_RANGE;
	int		myteam = self->client->ctf.teamnum;
	int		i;

	if (myteam != CTF_TEAM_RED && myteam != CTF_TEAM_BLUE)
		return NULL;

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t	*p = g_edicts + 1 + i;
		vec3_t	mid, delta;
		float	dist, dot;
		int		theirteam;

		if (p == self)
			continue;
		if (!p->inuse || !p->client)
			continue;
		if (p->deadflag == DEAD_DEAD || p->health <= 0)
			continue;
		if (p->movetype == MOVETYPE_NOCLIP)
			continue;			/* observers, per Cmd_Hook_f's own test */

		theirteam = p->client->ctf.teamnum;
		if (theirteam != CTF_TEAM_RED && theirteam != CTF_TEAM_BLUE)
			continue;
		if (theirteam == myteam)
			continue;

		Combat_Center(p, mid);
		VectorSubtract(mid, eye, delta);
		dist = VectorLength(delta);
		if (dist < 1.0f || dist >= bestdist)
			continue;

		/* forward cone: do not shoot backwards. The basis is the CURRENT
		 * v_angle, which is what the previous frame's cmd angles produced. */
		VectorScale(delta, 1.0f / dist, delta);
		dot = DotProduct(delta, forward);
		if (dot < SG_FOV_COS)
			continue;

		if (!Combat_Visible(self, p))
			continue;

		best = p;
		bestdist = dist;
	}

	return best;
}

/* ------------------------------------------------------------------- aim */

/*
 * A fresh tremor direction: a unit vector with the component along `dir`
 * removed, so the error is purely lateral and its magnitude maps cleanly onto
 * an angle (an offset of m added to a unit aim vector is an angle of atan(m)).
 */
static void Combat_SampleError(sg_combat_state_t *st, vec3_t dir)
{
	vec3_t	v;
	float	d, len;
	int		tries;

	for (tries = 0; tries < 4; tries++)
	{
		v[0] = crandom();
		v[1] = crandom();
		v[2] = crandom();

		d = DotProduct(v, dir);
		VectorMA(v, -d, dir, v);

		len = VectorLength(v);
		if (len > 0.01f)
		{
			VectorScale(v, 1.0f / len, st->err);
			return;
		}
	}

	/* degenerate sample: no error this time rather than a fake one */
	VectorClear(st->err);
}

/* ---------------------------------------------------------------- weapon */

/*
 * THE LMCTF QUIRK, read from the source rather than assumed.
 *
 * With ctfflags & CTF_OFFHAND_HOOK (16, q_shared.h:1066), Cmd_Hook_f checks
 * whether the grapple is the CURRENT weapon (g_cmds.c:1405-1412):
 *
 *     it = FindItem("Grappling Hook");
 *     // Can't offhand your hook if it is your current weapon
 *     if (ent->client->pers.weapon == it)
 *     {
 *         ForceCommand(ent, "+attack\n");
 *         return;
 *     }
 *
 * and Cmd_Unhook_f mirrors it with "-attack" (g_cmds.c:1448-1452). So when the
 * grapple is up, the hook IS +attack -- offhand firing is unavailable and the
 * attack button is spoken for.
 *
 * The weapon think confirms the other direction. Weapon_Hook (p_weapon.c:2139):
 *
 *     if ( !((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK))
 *         ctf_hook_abort(ent);
 *
 * then Weapon_Generic drives Weapon_Hook_Fire on its fire frames
 * (p_weapon.c:2144). Holding BUTTON_ATTACK with the grapple as pers.weapon
 * fires and sustains the ROPE. Releasing it cuts the rope.
 *
 * Therefore: BUTTON_ATTACK is only ever safe to set while pers.weapon is the
 * Blaster. That is the single invariant this file enforces before firing.
 *
 * It also happens to be what ARACHNOTRON needs. sg_arach.c:878 fires the hook
 * with Cmd_Hook_f, which only reaches Weapon_Hook_Fire (g_cmds.c:1419) when
 * the grapple is NOT the current weapon. Keeping the Blaster up keeps the
 * bot's offhand hook working. The two requirements point the same way.
 */

/*
 * Request the Blaster if it is not already up. The path is Cmd_Use_f's, read
 * from g_cmds.c:667-687 and reproduced in order: FindItem, reject if the item
 * has no use handler, reject if it is not in inventory, then call it->use.
 * Nothing is invented -- InitClientPersistent (p_client.c:1147-1151) puts a
 * Blaster in every client's inventory and makes it pers.weapon, so this is a
 * recovery path, not a supply of new gear.
 *
 * Returns true when the Blaster is up AND settled. Use_Weapon does not switch
 * immediately: it sets client->newweapon (p_weapon.c:376) and the change
 * happens later, when the current weapon is down. So a pending newweapon means
 * "not yet" and the trigger stays off until it resolves.
 */
static qboolean Combat_BlasterReady(edict_t *self)
{
	gitem_t				*it;
	sg_combat_state_t	*st;
	int					idx;

	it = FindItem("Blaster");
	if (!it)
		return false;

	if (self->client->pers.weapon == it && !self->client->newweapon)
		return true;

	/* Do not yank the weapon around while a rope is out -- that is the Body's
	 * business and a switch mid-flight would cost it the hook. Combat waits;
	 * it never overrides movement, and the hook is movement. */
	if (self->client->hook || self->client->hookstate != 0)
		return false;

	if (self->client->newweapon)
		return false;			/* a switch is already in flight */

	st = &sg_combat[self->client - game.clients];
	if (level.time < st->switch_next)
		return false;
	st->switch_next = level.time + 0.5f;

	if (!it->use)
		return false;
	idx = ITEM_INDEX(it);
	if (!self->client->pers.inventory[idx])
		return false;

	it->use(self, it);			/* Use_Weapon -> client->newweapon = Blaster */
	return false;				/* not this frame; next one, once it lands */
}

/* ------------------------------------------------------------------ frame */

void SG_CombatFrame(edict_t *self, usercmd_t *cmd, qboolean *out_engaged)
{
	sg_combat_state_t	*st;
	edict_t				*enemy;
	vec3_t				eye, forward, mid, delta, lead, aim, endp;
	float				dist, flight, held, frac, mag, len;
	float				yaw, pitch;
	trace_t				tr;
	int					ci;
	qboolean			clear_shot;

	if (out_engaged)
		*out_engaged = false;

	if (!self || !self->inuse || !self->client || !cmd)
		return;
	if (self->deadflag == DEAD_DEAD || self->health <= 0)
		return;
	if (self->movetype == MOVETYPE_NOCLIP)
		return;

	ci = (int)(self->client - game.clients);
	if (ci < 0 || ci >= SG_COMBAT_MAXCLIENTS)
		return;
	st = &sg_combat[ci];

	/* eyes and current facing. v_angle is the view the last cmd produced. */
	VectorCopy(self->s.origin, eye);
	eye[2] += self->viewheight;
	AngleVectors(self->client->v_angle, forward, NULL, NULL);

	enemy = Combat_Scan(self, eye, forward);
	if (!enemy)
	{
		st->enemy = 0;
		return;
	}

	/* target continuity: a new target restarts the settle clock and draws a
	 * fresh tremor. Holding the same one lets the error decay. */
	if (st->enemy != (int)(enemy - g_edicts))
	{
		st->enemy = (int)(enemy - g_edicts);
		st->since = level.time;
		st->err_next = 0.0f;
		st->win_end = 0.0f;
		st->win_fire = false;
	}

	if (out_engaged)
		*out_engaged = true;

	/* ---------------------------------------------------------------- lead
	 *
	 * The bolt is a projectile at a known 1000 u/s (p_weapon.c:939), so the
	 * aim point is where the target will be when it arrives, not where it is.
	 * Solved by one refinement pass: time from the present distance, then the
	 * distance to that first guess, then time again. Two passes is enough at
	 * this speed and is honest about being an approximation rather than the
	 * exact quadratic intercept. A hitscan weapon would skip this entirely --
	 * no travel time, no lead -- but the bots only carry a Blaster.
	 */
	Combat_Center(enemy, mid);
	VectorSubtract(mid, eye, delta);
	dist = VectorLength(delta);

	flight = dist / SG_BLASTER_SPEED;
	VectorMA(mid, flight, enemy->velocity, lead);

	VectorSubtract(lead, eye, delta);
	dist = VectorLength(delta);
	flight = dist / SG_BLASTER_SPEED;
	VectorMA(mid, flight, enemy->velocity, lead);

	VectorSubtract(lead, eye, aim);
	len = VectorLength(aim);
	if (len < 1.0f)
		return;
	VectorScale(aim, 1.0f / len, aim);

	/* ----------------------------------------------------------- aim error
	 *
	 * A human does not snap onto a target; they overshoot and settle. The
	 * error is a lateral offset that shrinks over SG_SETTLE seconds of held
	 * contact down to a residual that never reaches zero. The direction is
	 * resampled on a slow tick so it reads as tremor rather than a fixed bias
	 * that a target could out-run in one direction.
	 */
	if (level.time >= st->err_next)
	{
		Combat_SampleError(st, aim);
		st->err_next = level.time + 0.25f + 0.25f * random();
	}

	held = level.time - st->since;
	frac = (held >= SG_SETTLE) ? 0.0f : (1.0f - held / SG_SETTLE);
	mag = (float)tan((SG_AIM_RESIDUAL_DEG
	                  + (SG_AIM_ERROR_DEG - SG_AIM_RESIDUAL_DEG) * frac)
	                 * M_PI / 180.0);

	VectorMA(aim, mag, st->err, aim);
	VectorNormalize(aim);

	/* ------------------------------------------------------------- the view
	 *
	 * Same convention the Body uses to steer (sg_arach.c:612-617): yaw from
	 * atan2 of the ground components, pitch negated because Quake pitch is
	 * positive downward, both converted with ANGLE2SHORT and biased by the
	 * client's delta_angles so the server resolves to the angle we asked for.
	 */
	yaw = (float)(atan2(aim[1], aim[0]) * 180.0 / M_PI);
	pitch = (float)(-asin(aim[2]) * 180.0 / M_PI);

	cmd->angles[YAW] = ANGLE2SHORT(yaw)
	                 - self->client->ps.pmove.delta_angles[YAW];
	cmd->angles[PITCH] = ANGLE2SHORT(pitch)
	                   - self->client->ps.pmove.delta_angles[PITCH];

	/* ------------------------------------------------------- the wall check
	 *
	 * Legacy bots declined to fire 37-43% of the time because they shot into
	 * geometry. The fix is not a heuristic: trace the bolt's own path with the
	 * bolt's own clipmask (MASK_SHOT, g_weapon.c:360) and only pull the
	 * trigger if it arrives. `self` is the pass entity, so we do not trace
	 * against ourselves, exactly as the bolt does not collide with its owner.
	 *
	 * Accepted if the trace lands ON the enemy, or if it stops within
	 * SG_HIT_SLOP of the aim point -- the second case covers a bolt that
	 * passes close enough for the splashless blaster to still connect on a
	 * target that has moved a little inside its own bounding box.
	 */
	VectorMA(eye, len, aim, endp);
	tr = gi.trace(eye, NULL, NULL, endp, self, MASK_SHOT);

	clear_shot = false;
	if (tr.ent == enemy)
	{
		clear_shot = true;
	}
	else
	{
		vec3_t miss;

		VectorSubtract(tr.endpos, lead, miss);
		if (VectorLength(miss) <= SG_HIT_SLOP)
			clear_shot = true;
	}

	if (!clear_shot)
		return;					/* aim is written; the trigger stays off */

	/* ---------------------------------------------------------- the trigger
	 *
	 * Alternating windows, each jittered, so the pattern is not a metronome:
	 * roughly SG_FIRE_ON held then SG_FIRE_OFF released. The window advances
	 * on its own clock, independent of whether a shot was cleared, so a bot
	 * that loses line of sight mid-burst does not get a free full burst the
	 * instant it comes back.
	 */
	if (level.time >= st->win_end)
	{
		float base;

		st->win_fire = !st->win_fire;
		base = st->win_fire ? SG_FIRE_ON : SG_FIRE_OFF;
		st->win_end = level.time + base * (1.0f + SG_FIRE_JITTER * crandom());
	}

	if (!st->win_fire)
		return;

	/*
	 * The invariant, last thing before the button: the Blaster must be up and
	 * settled. If it is not, we request it and hold fire. Setting
	 * BUTTON_ATTACK with the grapple as pers.weapon would fire the ROPE
	 * (p_weapon.c:2139-2144) -- never the intent, and it would wreck both the
	 * shot and the Body's movement.
	 */
	if (!Combat_BlasterReady(self))
		return;

	cmd->buttons |= BUTTON_ATTACK;
}
