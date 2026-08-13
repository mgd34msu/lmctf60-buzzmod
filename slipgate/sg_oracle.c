/*
 * sg_oracle.c -- the physics, used as truth.
 *
 * Everything SLIPGATE knows about movement it knows because this file rolled
 * the engine's own Pmove forward and watched what happened. There is no model
 * of the physics anywhere in the system -- there is only the physics, called
 * on phantom state that belongs to no client and touches no entity.
 *
 * sg_host.pmove is the same function the server runs for every real player
 * (game.h:122, "player movement code common with client prediction"), with
 * the world queried through the trace and pointcontents callbacks we supply.
 * We pass the engine's own sg_host.trace against the live collision world, with a
 * null passent since a phantom occupies no slot and should collide with the
 * world exactly as a player-shaped body would.
 *
 * The one thing Pmove does NOT simulate is the LMCTF grapple, which lives in
 * the game code (p_weapon.c) and overwrites velocity with a flat 800 toward
 * the anchor while the rope is longer than 120 units, no gravity while taut,
 * and a distance-scaled braking ladder below (speed*5 above 100 units of
 * rope, *4 above 80, *3 above 40, *2 above 20, *1 above 10, and nothing at
 * all below 10 -- a dead stop). SG_OracleHookStep reproduces exactly that
 * rule, with the p_weapon.c lines cited, so a rollout can prove hook links
 * with the same arithmetic the game will use to execute them.
 */

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_hooks.h"

/*
 * A phantom's trace must not pass through any entity: it models a player
 * moving through the world at generation time, when the only thing that is
 * reliably present is the world itself. Entities (doors especially) are
 * handled at the link level -- a link through a door area is tagged so the
 * runtime can re-validate it against the door's current state.
 */
static trace_t SG_PhantomTrace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	return sg_host.trace(start, mins, maxs, end, NULL, MASK_PLAYERSOLID);
}

static int SG_PhantomContents(vec3_t point)
{
	return sg_host.pointcontents(point);
}

/*
 * Roll the real physics forward.
 *
 * in:  state (position, velocity, pm flags), a command, how many steps and
 *      how long each one is (msec must be what a real client could send --
 *      the caller owns making steps sum to honest time, principle 2).
 * out: the state Pmove left behind, plus what happened on the way.
 *
 * The command is applied unchanged every step. Callers who want per-step
 * decisions (the generator proving a strafe link, say) call this once per
 * step with steps=1 and change the command between calls -- the exact
 * structure a real client has, one usercmd per step of simulation.
 */
void SG_OracleRun(sg_phantom_t *ph, usercmd_t *cmd, int steps)
{
	pmove_t pm;
	int i;

	for (i = 0; i < steps; i++)
	{
		memset(&pm, 0, sizeof(pm));
		pm.s = ph->pms;
		pm.cmd = *cmd;
		pm.trace = SG_PhantomTrace;
		pm.pointcontents = SG_PhantomContents;

		sg_host.pmove(&pm);

		ph->pms = pm.s;
		ph->groundentity = pm.groundentity ? true : false;
		ph->waterlevel = pm.waterlevel;

		/* decode the fixed-point state once per step so callers read floats */
		ph->origin[0] = pm.s.origin[0] * 0.125f;
		ph->origin[1] = pm.s.origin[1] * 0.125f;
		ph->origin[2] = pm.s.origin[2] * 0.125f;
		ph->velocity[0] = pm.s.velocity[0] * 0.125f;
		ph->velocity[1] = pm.s.velocity[1] * 0.125f;
		ph->velocity[2] = pm.s.velocity[2] * 0.125f;
	}
}

/*
 * One step of being pulled by the hook, exactly as p_weapon.c:2059-2105 does
 * it to a real player each server frame while hookstate is 2:
 *
 *     VectorSubtract(hook origin, player start, dir);
 *     rope = VectorLength(dir);  VectorNormalize(dir);
 *     rope > 120: velocity = dir * 800    (and gravity added then discarded)
 *     rope > 100: velocity = dir * rope*5
 *     rope >  80: velocity = dir * rope*4
 *     rope >  40: velocity = dir * rope*3
 *     rope >  20: velocity = dir * rope*2
 *     rope >  10: velocity = dir * rope*1
 *     else:       velocity = dir          (unscaled unit vector: a dead stop)
 *
 * The overwrite happens in the game frame, then Pmove integrates the result.
 * A rollout therefore alternates: SG_OracleHookStep to set the pull, then
 * SG_OracleRun for one step to let the world act on it.
 */
void SG_OracleHookStep(sg_phantom_t *ph, vec3_t anchor)
{
	vec3_t dir;
	float rope;

	VectorSubtract(anchor, ph->origin, dir);
	rope = VectorLength(dir);
	if (rope < 1.0f)
		return;
	VectorScale(dir, 1.0f / rope, dir);

	if (rope > 120.0f)
		VectorScale(dir, 800.0f, ph->velocity);
	else if (rope > 100.0f)
		VectorScale(dir, rope * 5.0f, ph->velocity);
	else if (rope > 80.0f)
		VectorScale(dir, rope * 4.0f, ph->velocity);
	else if (rope > 40.0f)
		VectorScale(dir, rope * 3.0f, ph->velocity);
	else if (rope > 20.0f)
		VectorScale(dir, rope * 2.0f, ph->velocity);
	else if (rope > 10.0f)
		VectorScale(dir, rope * 1.0f, ph->velocity);
	/* under 10: p_weapon.c leaves dir unscaled -- effectively zero speed */

	/* write back into the fixed-point state Pmove will read */
	ph->pms.velocity[0] = (short)(ph->velocity[0] * 8.0f);
	ph->pms.velocity[1] = (short)(ph->velocity[1] * 8.0f);
	ph->pms.velocity[2] = (short)(ph->velocity[2] * 8.0f);
}

/*
 * ------------------------------------------------------------- rocket jump
 *
 * A rocket jump is not a movement rule. It is a DAMAGE event the mover
 * arranges to happen underneath himself: the splash of his own rocket pushes
 * him, and the push stacks on the jump pmove already gave him. Nothing here
 * is tuned; every number is read out of this tree.
 *
 *   the shot    Weapon_RocketLauncher_Fire calls fire_rocket with speed 650,
 *               damage_radius 120 and radius_damage 120 -- the live #else
 *               branch (p_weapon.c:864-866, :889). The CTF_WEAP_BALANCE
 *               numbers above them sit inside #ifdef WEAP_BALANCE_OK, which
 *               nothing in the build defines, so they are dead text.
 *   the muzzle  VectorSet(offset, 8, 8, ent->viewheight-8) then
 *               P_ProjectSource (p_weapon.c:880-881), which is
 *               G_ProjectSource (g_utils.c:7-12) for a right-handed client:
 *               origin + forward*8 + right*8 + (0,0,14), viewheight being 22
 *               (p_client.c:1923).
 *   the flight  MOVETYPE_FLYMISSILE with mins and maxs cleared
 *               (g_weapon.c:692-696): a POINT travelling a straight line at
 *               650 u/s, clipped by MASK_SHOT, no gravity. A point trace is
 *               therefore the projectile's own path, not an approximation of
 *               it. Sky ends the rocket without an explosion
 *               (rocket_touch, g_weapon.c:635-637).
 *   the owner   rocket_touch returns the instant it strikes its owner
 *               (g_weapon.c:631-632), so the jumper never eats the 100-119
 *               direct hit (p_weapon.c:851) and never takes its knockback,
 *               which is zero anyway (g_weapon.c:653). ALL of the push and
 *               ALL of the self-damage come from T_RadiusDamage.
 *   the splash  T_RadiusDamage (g_combat.c:698-756): the distance is measured
 *               from the explosion to the target's bbox CENTRE, not its
 *               origin (g_combat.c:716-718); points = damage - 0.5*dist
 *               (g_combat.c:742); halved because the jumper is his own
 *               attacker (g_combat.c:745-746); and findradius hard-culls
 *               anything past the radius (g_utils.c:60-83), so outside 120
 *               units nothing happens at all -- there is no taper to zero.
 *               (int)points is passed as BOTH the damage and the knockback
 *               (g_combat.c:752).
 *   the push    T_Damage normalises dir (g_combat.c:471) and, because the
 *               attacker IS the target, scales by 1600 instead of 500 -- the
 *               line id itself labelled "the rocket jump hack"
 *               (g_combat.c:491-494). Mass is 200 for a player
 *               (p_client.c:1926), floored at 50, so the kick is exactly
 *               8 units/s per point of knockback. A body still standing on
 *               the floor cannot be pushed down into it (g_combat.c:505-508).
 *   the health  take starts at (int)points and only ever comes DOWN from
 *               there -- power armour, the Resist rune, armour
 *               (g_combat.c:534-548). A link records the WORST case, which is
 *               also the honest one for a generator that cannot know what the
 *               jumper will be wearing: an unarmoured body pays all of it.
 *
 * Nothing above says anything about how high the jumper goes. That is
 * pmove's business and it is never assumed here: the caller applies this
 * force and then lets SG_OracleRun integrate, exactly as with the hook.
 */
#define SG_RJ_RADIUS_DAMAGE	120.0f		/* p_weapon.c:865 */
#define SG_RJ_DAMAGE_RADIUS	120.0f		/* p_weapon.c:866 */
#define SG_RJ_ROCKET_SPEED	650.0f		/* p_weapon.c:889 */
#define SG_RJ_PLAYER_MASS	200.0f		/* p_client.c:1926 */
#define SG_RJ_VIEWHEIGHT	22.0f		/* p_client.c:1923 */
#define SG_RJ_BBOX_CENTRE	4.0f		/* 0.5*(mins+maxs) z for a standing
                                         * player box, g_combat.c:716-717 */

/*
 * Where does the rocket a body fires from here, along this aim, actually go
 * off -- and how long does it take to get there? Both are geometry plus the
 * game's own muzzle offset and projectile speed, so both belong here rather
 * than in a prover.
 *
 * Returns false when the shot never detonates near the shooter: no surface
 * within the trace, or sky (which frees the rocket silently).
 */
qboolean SG_OracleRocketJumpAim(vec3_t origin, vec3_t aim,
                                vec3_t boom_out, float *flight_ms)
{
	vec3_t angles, forward, right, start, end, d;
	trace_t tr;
	float travel;

	vectoangles(aim, angles);
	AngleVectors(angles, forward, right, NULL);

	/* p_weapon.c:880-881 -> g_utils.c:7-12, right-handed */
	start[0] = origin[0] + forward[0] * 8.0f + right[0] * 8.0f;
	start[1] = origin[1] + forward[1] * 8.0f + right[1] * 8.0f;
	start[2] = origin[2] + forward[2] * 8.0f + right[2] * 8.0f
	           + (SG_RJ_VIEWHEIGHT - 8.0f);

	VectorMA(start, 8192.0f, forward, end);
	tr = sg_host.trace(start, NULL, NULL, end, NULL, MASK_SHOT);
	if (tr.startsolid || tr.allsolid || tr.fraction >= 1.0f)
		return false;
	if (tr.surface && (tr.surface->flags & SURF_SKY))
		return false;               /* g_weapon.c:635-637: no explosion */

	VectorCopy(tr.endpos, boom_out);
	VectorSubtract(boom_out, start, d);
	travel = VectorLength(d);
	*flight_ms = travel / SG_RJ_ROCKET_SPEED * 1000.0f;
	return true;
}

/*
 * The detonation itself, applied to a phantom exactly as T_RadiusDamage ->
 * T_Damage applies it to a real player, and in the same place in the frame:
 * the game code changes velocity, then Pmove integrates the result. A rollout
 * therefore rolls the flight time with SG_OracleRun, calls this once at the
 * moment the rocket arrives, and keeps rolling.
 *
 * Returns the health the jumper pays -- 0 when the burst is out of range or
 * on the wrong side of a wall, in which case nothing was applied.
 */
int SG_OracleRocketJumpStep(sg_phantom_t *ph, vec3_t boom)
{
	vec3_t centre, v, dir, kvel;
	float dist, points, len;
	int knockback;
	trace_t tr;

	/* distance to the bbox CENTRE, g_combat.c:716-718 */
	VectorCopy(ph->origin, centre);
	centre[2] += SG_RJ_BBOX_CENTRE;
	VectorSubtract(boom, centre, v);
	dist = VectorLength(v);

	/* findradius hard-culls past the radius, g_utils.c:60-83 */
	if (dist > SG_RJ_DAMAGE_RADIUS)
		return 0;

	/*
	 * CanDamage (g_combat.c:14-70, called at :749): a point trace from the
	 * explosion to the target ORIGIN against MASK_SOLID, and when that is
	 * blocked, four more to the corners at +/-15 in x and y. Same five traces
	 * here, with the phantom's null passent.
	 */
	{
		static const float corners[5][2] = {
			{ 0.0f, 0.0f }, { 15.0f, 15.0f }, { 15.0f, -15.0f },
			{ -15.0f, 15.0f }, { -15.0f, -15.0f },
		};
		int c;
		qboolean seen = false;

		for (c = 0; c < 5 && !seen; c++)
		{
			vec3_t dest;

			VectorCopy(ph->origin, dest);
			dest[0] += corners[c][0];
			dest[1] += corners[c][1];
			tr = sg_host.trace(boom, vec3_origin, vec3_origin, dest, NULL,
			              MASK_SOLID);
			if (tr.fraction == 1.0f)
				seen = true;
		}
		if (!seen)
			return 0;
	}

	/* points = damage - 0.5*dist (g_combat.c:742), halved for self
	 * (g_combat.c:745-746), and only a positive result does anything
	 * (g_combat.c:747) */
	points = (SG_RJ_RADIUS_DAMAGE - 0.5f * dist) * 0.5f;
	if (points <= 0.0f)
		return 0;
	knockback = (int)points;        /* g_combat.c:752 passes (int)points as
	                                 * the damage AND as the knockback */

	/* dir runs explosion -> target ORIGIN (g_combat.c:751) -- the origin, not
	 * the centre the distance was measured to -- and T_Damage normalises it
	 * (g_combat.c:471) */
	VectorSubtract(ph->origin, boom, dir);
	len = VectorLength(dir);
	if (len < 0.001f)
		return 0;
	VectorScale(dir, 1.0f / len, dir);

	/* "the rocket jump hack": 1600 for self-damage, 500 for everything else
	 * (g_combat.c:491-494), over a player's mass of 200 (p_client.c:1926) */
	VectorScale(dir, 1600.0f * (float)knockback / SG_RJ_PLAYER_MASS, kvel);

	/* standing on the floor cannot be pushed into it, g_combat.c:505-508 */
	if (ph->groundentity && kvel[2] < 0.0f)
		kvel[2] = 0.0f;

	/* VectorAdd(targ->velocity, kvel, targ->velocity), g_combat.c:510 --
	 * an ADD onto whatever pmove left there, which is what makes the jump
	 * and the blast stack */
	VectorAdd(ph->velocity, kvel, ph->velocity);

	/* write back into the fixed-point state Pmove will read, as the hook
	 * step does */
	ph->pms.velocity[0] = (short)(ph->velocity[0] * 8.0f);
	ph->pms.velocity[1] = (short)(ph->velocity[1] * 8.0f);
	ph->pms.velocity[2] = (short)(ph->velocity[2] * 8.0f);

	return knockback;               /* == the damage taken, worst case */
}

/*
 * The highest a rocket jump can possibly lift a body, from the arithmetic
 * above and nothing else. This is a CANDIDATE FILTER, not a claim: it says
 * which pairs are worth handing to the prover, and the prover still has to
 * roll the real physics for a link to exist.
 *
 * The strongest burst the game can produce under a standing player is one on
 * the floor directly beneath him: his box bottom is 24 below his origin
 * (p_client.c's player mins) and the centre the damage is measured to sits 4
 * above it, so the shortest distance is 28 and no arrangement of aim can beat
 * it. That gives points = (120 - 14)/2 = 53, a kick of 8*53 = 424 up.
 *
 * On top of that goes the jump pmove hands out. PM_CheckJump lives in the
 * engine, not in this tree, so its 270 is the one constant here that cannot
 * carry a line number from these sources -- which is exactly why it is
 * confined to this filter and kept out of every proof: SG_OracleRun gets the
 * real number from sg_host.pmove every time it steps.
 *
 * Ballistic rise for the stacked launch speed, under the server's own
 * gravity: v^2 / 2g. At sv_gravity 800 that is (424+270)^2/1600 = 301 units.
 * Any real jump comes in under it -- the rocket takes time to reach the
 * floor, and by the time it detonates the body has already left, which puts
 * the burst further away and costs it kick.
 */
float SG_OracleRocketJumpCeiling(void)
{
	float points = (SG_RJ_RADIUS_DAMAGE - 0.5f * (24.0f + SG_RJ_BBOX_CENTRE))
	               * 0.5f;
	float kick = 1600.0f * (float)(int)points / SG_RJ_PLAYER_MASS;
	float launch = kick + 270.0f;   /* PM_CheckJump, engine pmove */
	float g = (sv_gravity && sv_gravity->value > 1.0f)
	              ? sv_gravity->value : 800.0f;

	return launch * launch / (2.0f * g);
}

/*
 * Stand a phantom up at a position, at rest, feet on whatever is below.
 * The usual way a generator seed begins.
 */
void SG_OraclePlace(sg_phantom_t *ph, vec3_t origin)
{
	memset(ph, 0, sizeof(*ph));
	VectorCopy(origin, ph->origin);
	ph->pms.origin[0] = (short)(origin[0] * 8.0f);
	ph->pms.origin[1] = (short)(origin[1] * 8.0f);
	ph->pms.origin[2] = (short)(origin[2] * 8.0f);
	ph->pms.pm_type = PM_NORMAL;
	/*
	 * Weight. pmove applies pm->s.gravity, which the game sets per client
	 * from sv_gravity every frame (p_client.c:2798) -- and a memset phantom
	 * had zero. Every drop proof ever attempted stepped off its lip and
	 * LEVITATED at source height until the budget died; jumps rose 270 and
	 * never came back; only the hook proofs survived, because the rope
	 * overwrites velocity wholesale. One uninitialized field, four failed
	 * prover designs built on top of it.
	 */
	ph->pms.gravity = (short)sv_gravity->value;
}
