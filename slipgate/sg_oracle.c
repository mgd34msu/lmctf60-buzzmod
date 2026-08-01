/*
 * sg_oracle.c -- the physics, used as truth.
 *
 * Everything SLIPGATE knows about movement it knows because this file rolled
 * the engine's own Pmove forward and watched what happened. There is no model
 * of the physics anywhere in the system -- there is only the physics, called
 * on phantom state that belongs to no client and touches no entity.
 *
 * gi.Pmove is the same function the server runs for every real player
 * (game.h:122, "player movement code common with client prediction"), with
 * the world queried through the trace and pointcontents callbacks we supply.
 * We pass the engine's own gi.trace against the live collision world, with a
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

/*
 * A phantom's trace must not pass through any entity: it models a player
 * moving through the world at generation time, when the only thing that is
 * reliably present is the world itself. Entities (doors especially) are
 * handled at the link level -- a link through a door area is tagged so the
 * runtime can re-validate it against the door's current state.
 */
static trace_t SG_PhantomTrace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	return gi.trace(start, mins, maxs, end, NULL, MASK_PLAYERSOLID);
}

static int SG_PhantomContents(vec3_t point)
{
	return gi.pointcontents(point);
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

		gi.Pmove(&pm);

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
