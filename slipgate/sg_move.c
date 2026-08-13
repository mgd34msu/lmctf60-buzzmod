/*
 * sg_move.c -- the gradient made flesh: movement policy and command
 * assembly.  The strafe and air-strafe controllers, run-room and
 * pursuit geometry, the plan beam, Think_Move (aim, feelers, ribbon,
 * lookahead, doors, drops, the hook brake) and Think_Emit (sub-steps,
 * slew, weave, telemetry, ClientThink).  Moved verbatim from
 * sg_arach.c in the 2026-08-12 standards pass.
 */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_chat.h"
#include "slipgate/sg_persona.h"
#include "slipgate/sg_net.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_clock.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_weights.h"
#include "slipgate/sg_tilt.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_price.h"     /* sg_cur_role */

void		ClientThink(edict_t *ent, usercmd_t *ucmd);
void		Cmd_Hook_f(edict_t *ent);

#define SG_AS_PERIOD	1.35f
#define SG_AS_VIEWSHARE	0.55f
#define SG_AS_VIEWMAX	32.0f
#define SG_AS_CORR		25.0f
#define SG_AS_ABORT		40.0f
#define SG_AS_RUN		320.0f
#define SG_AS_HOLD		0.70f   /* the road bar a live chain is held to */
#define SG_AS_FLOOR		240.0f
#define SG_AS_FLAGKEEP	220.0f
#define SG_AS_MINCHAIN	0.6f
#define SG_PURSUIT_MAX 8    /* seeds of chain; 8 x 128u median link covers
                             * any lookahead worth trying */
#define SG_AS_BEND	30.0f       /* degrees the chord may sit off the route */
#define SG_AS_CHORD	0.80f       /* chord / arc a road has to keep */
#define SG_WEAVE_SIDE		300
#define SG_WEAVE_BASE		0.4f
#define SG_WEAVE_STEP		0.05f
#define SG_WEAVE_HOLD		150.0f	/* a step this short is a stand, not a run */

/*
 * ------------------------------------------------- the air-strafe chain
 *
 * THE VIEW AND THE PATH TURN TOGETHER (sg_airstrafe, default 0 = off).
 *
 * What a human does that the body above does not: he rotates the VIEW and
 * the strafe key together while airborne, so the wish direction stays off
 * the velocity for the whole flight instead of for the instant the route
 * happens to bend. Chained across hops that is 800-1600 u/s on a pub
 * server; this fleet's sustained runs have never exceeded ground speed.
 *
 * WHICH ENGINE IS ACTUALLY RUNNING. SG_Strafe's air branch offers a
 * derivation from PM_AirAccelerate's 30-unit clamp (sg_arach.c:1984), and
 * that dose measured NEGATIVE. It had to: PM_AirAccelerate is only reached
 * when air acceleration is switched on, and it is not. The fleet's engine
 * is yquake2 (engines/yquake2/release/q2ded); in its pmove
 *
 *     src/common/pmove.c:62      float pm_airaccelerate = 0;
 *     src/server/sv_main.c:636   Cvar_Get("sv_airaccelerate", "0", LATCH)
 *
 * and PM_AirMove's airborne branch (pmove.c:673-680) reads
 *
 *     if (pm_airaccelerate) PM_AirAccelerate(wishdir, wishspeed, 10);
 *     else                  PM_Accelerate(wishdir, wishspeed, 1);
 *
 * so with the cvar at its default the air runs the SAME function the
 * ground does, at accel 1 instead of 10 and with PM_Friction skipped
 * because groundentity is NULL. (q2repro agrees line for line:
 * src/server/main.c:2145 and game3_pmove/template.c, PM_AirMove.) So the
 * angle to fly is the one SG_Strafe's own comment derives at
 * sg_arach.c:1958, with the engine's real constants:
 *
 *     addspeed   = wishspeed - (velocity . wishdir)      PM_Accelerate
 *     accelspeed = accel * frametime * wishspeed, capped at addspeed
 *
 *     cos(theta) = (wishspeed - accelspeed) / speed
 *
 * wishspeed is pm_maxspeed = 300 (PM_AirMove clamps wishvel to it before
 * accelerating), accel is 1, and frametime is ONE SUB-STEP -- 12 or 13 ms
 * at sg_subframes 8 -- so accelspeed = 1 * 0.0125 * 300 = 3.75 u/s.
 *
 *     speed  300    400    500    600    800   1000
 *     theta  9.0d  42.2d  53.6d  60.5d  68.3d  72.8d
 *
 * and the gain at that angle is accelspeed * cos(theta) = 3.75 * 296.25 /
 * speed ~= 1111 / speed per sub-step: 2.8 u/s per step at 400, and eighty
 * steps go by in a second. Below 296.25 there is no angle to find -- the
 * cap is not binding and straight ahead is already the fastest input --
 * which is exactly why this only ever touches a body that has already been
 * pushed through the ground cap by a hop.
 *
 * WHY THE VIEW HAS TO MOVE FOR ANY OF IT TO PAY. The angle is measured
 * from the direction of TRAVEL, so holding it drags the velocity off the
 * route at roughly the same rate it feeds it -- wave 296's finding, and the
 * reason its dose 2 capped the lean at 40 degrees and harvested little. A
 * human does not hold the lean, he SWINGS it: the view (and with it the
 * strafe axis) sweeps through the heading and out the other side, so the
 * path is a shallow S whose mean is the road and whose every instant is
 * off-axis. Here that swing is a sinusoid centred on the route heading,
 * amplitude theta, biased by the standing heading error so the swing that
 * corrects gets the longer half. The lean is a signed unit number; this
 * function turns it into the command.
 *
 * WHERE THE ROTATION IS SPENT. The view carries SG_AS_VIEWSHARE of theta
 * and the input carries the rest, exactly as a player's forward+strafe
 * diagonal does. The split costs nothing in physics because the wish
 * direction is decomposed against the view pmove will ACTUALLY use this
 * sub-step (bot->vy_cur, post-slew) rather than the view that was asked
 * for -- so a slewed, lagging, rate-limited view still reconstructs the
 * commanded direction to the degree. Nothing here writes velocity: the
 * whole gain comes out of cmd.forwardmove, cmd.sidemove and cmd.angles,
 * through the same PM_Accelerate a human client drives.
 */
typedef struct
{
	float		lean;       /* the sinusoid, -1..1; sign is the lean side */
	float		vy_cur;     /* the view yaw pmove will use THIS sub-step */
	float		vp_cur;     /* and its pitch, before the engine's /3 */
	qboolean	chain;      /* dose 2: hop chaining as well as the lean */
} sg_air_t;

#define SG_AIR_ACCEL	1.0f    /* PM_AirMove's airborne PM_Accelerate accel */

/*
 * ------------------------------------------------------------ the policy
 *
 * Movement, closed-form, from the engine rather than from feel. This is the
 * legacy body's proven policy (bl_main.c:92-175, BotAirStrafe and its
 * derivation) moved into the SLIPGATE body unchanged in substance; only the
 * inputs are re-expressed in SLIPGATE terms -- the route direction is the
 * heading the surface descent chose, not a botlib bi->dir.
 *
 * pm_maxspeed caps wishspeed, not velocity. PM_Accelerate adds
 *
 *     accelspeed = accel * frametime * wishspeed
 *
 * along wishdir for as long as addspeed = wishspeed - (velocity . wishdir)
 * stays positive, so an input held off the direction of travel keeps that
 * term alive and the speed climbing. The smallest angle that still leaves
 * addspeed at the cap is
 *
 *     cos(theta) = (wishspeed - accelspeed) / speed
 *
 * On the ground accel is 10 and the limit is friction, speed * 6 * frametime,
 * which scales with speed while the gain at the best angle does not: they meet
 * near 370. Driving forwardmove straight down the heading converges on 300 and
 * stops there. In the air accel is 1 -- a tenth the rate, but no friction and
 * therefore no ceiling.
 *
 * Which way to lean is not a coin flip: leaning toward the side the route
 * turns accelerates and steers at once, so the velocity is pulled onto the
 * path instead of away from it. The view is not involved -- the bot names the
 * direction and decomposes it against the view it is already holding, so none
 * of this costs any aim.
 */
static void SG_Strafe(usercmd_t *cmd, vec3_t fwd, vec3_t right,
                      vec3_t vel, vec3_t dir,
                      float speed2d, float frametime, float accel)
{
	vec3_t	vdir, d;
	float	wishspeed = 300.0f;     /* pm_maxspeed clamps wishspeed to this */
	float	accelspeed, c, th, sn, cs, cross;

	if (speed2d < 1.0f)
		return;

	/*
	 * THE AIR CAP (sg_airgain, wave 296+). PM_AirAccelerate clamps the
	 * wishspeed IT uses to 30 (pmove.c:382) -- the whole strafe-jump
	 * exploit lives in that clamp: the projection gate is v.wish < 30,
	 * open at ANY speed for a wish pointed far enough off the velocity.
	 * This derivation with wishspeed 300 computes an air angle for an
	 * engine that does not exist; measured air gain ran NEGATIVE
	 * (-0.8/100ms vs the pub human's +3.0). In air, derive from the
	 * engine's real constant.
	 */
	if (accel < 5.0f && sg_cv.airgain->value)
		wishspeed = 30.0f;

	accelspeed = accel * frametime * wishspeed;

	/* dose 1 read NEGATIVE (296): the honest ~84-degree air lean turns
	 * the velocity off the route and the nav corrections eat more than
	 * the harvest pays. Dose 2 caps the lean at ~40 degrees: partial
	 * gain that stays roughly route-aligned. */
	#define SG_AIRLEAN_CAP 0.70f

	/*
	 * Below wishspeed - accelspeed there is no angle to find: addspeed is
	 * already saturated pointing straight down the route, so the input that
	 * accelerates hardest is also the one that steers, and leaning off it
	 * would only trade heading for nothing. Leave the caller's plain forward
	 * alone -- this is the whole of the low-speed case, and it is why the
	 * strafe is not a mode the bot enters and leaves.
	 */
	if (speed2d <= wishspeed - accelspeed)
		return;

	c = (wishspeed - accelspeed) / speed2d;
	if (c > 1.0f) c = 1.0f;
	if (c < -1.0f) c = -1.0f;
	th = acosf(c);

	vdir[0] = vel[0] / speed2d;
	vdir[1] = vel[1] / speed2d;
	vdir[2] = 0.0f;

	/* lean the way the route turns, so the gain also steers */
	cross = vdir[0] * dir[1] - vdir[1] * dir[0];
	if (cross < 0.0f)
		th = -th;

	if (accel < 5.0f &&
	    sg_cv.airgain->value >= 2 &&
	    th > SG_AIRLEAN_CAP)
		th = SG_AIRLEAN_CAP;

	sn = sinf(th);
	cs = cosf(th);
	d[0] = vdir[0] * cs - vdir[1] * sn;
	d[1] = vdir[0] * sn + vdir[1] * cs;
	d[2] = 0.0f;

	/*
	 * Decomposed against the basis pmove will actually build (pitch/3, see
	 * the caller), so the engine reconstructs the direction that was asked
	 * for. 400 on both axes before the clamp: wishvel is scaled down to
	 * pm_maxspeed anyway, and what matters is the direction.
	 */
	cmd->forwardmove = (short)(DotProduct(fwd, d) * 400.0f);
	cmd->sidemove = (short)(DotProduct(right, d) * 400.0f);
}

static void SG_AirStrafeCmd(usercmd_t *cmd, const sg_air_t *air,
                            vec3_t vel, float speed2d, float frametime)
{
	vec3_t	basis, vf, vr, vdir, d;
	float	wishspeed = 300.0f;     /* pm_maxspeed clamps wishvel to this */
	float	accelspeed, c, th, sn, cs, fl;

	if (speed2d < 1.0f)
		return;

	accelspeed = SG_AIR_ACCEL * frametime * wishspeed;

	/*
	 * Under wishspeed - accelspeed the cap is not binding: addspeed is
	 * saturated pointing straight down the travel line and leaning off it
	 * would trade heading for nothing. Same early return SG_Strafe makes,
	 * and the reason this is not a mode the body enters and leaves.
	 */
	if (speed2d <= wishspeed - accelspeed)
		return;

	c = (wishspeed - accelspeed) / speed2d;
	if (c > 1.0f) c = 1.0f;
	if (c < -1.0f) c = -1.0f;
	th = acosf(c) * air->lean;   /* the optimum, swung by the sinusoid */

	vdir[0] = vel[0] / speed2d;
	vdir[1] = vel[1] / speed2d;
	vdir[2] = 0.0f;

	sn = sinf(th);
	cs = cosf(th);
	d[0] = vdir[0] * cs - vdir[1] * sn;
	d[1] = vdir[0] * sn + vdir[1] * cs;
	d[2] = 0.0f;

	/*
	 * The basis pmove will build from the SLEWED view (PM_AirMove takes
	 * viewangles with PITCH divided by three), and the same reconstruction
	 * the per-sub-step course decomposition uses: forward's horizontal part
	 * is shortened by cos(pitch/3), so it is renormalised before the
	 * projection or the pair comes out skewed toward the strafe axis.
	 */
	basis[YAW] = air->vy_cur;
	basis[PITCH] = air->vp_cur;
	if (basis[PITCH] > 180.0f)
		basis[PITCH] -= 360.0f;
	basis[PITCH] /= 3.0f;
	basis[ROLL] = 0.0f;
	AngleVectors(basis, vf, vr, NULL);

	fl = sqrtf(vf[0] * vf[0] + vf[1] * vf[1]);
	if (fl < 0.01f)
		return;
	cmd->forwardmove = (short)(400.0f * (d[0] * vf[0] + d[1] * vf[1]) / fl);
	cmd->sidemove = (short)(400.0f * (d[0] * vr[0] + d[1] * vr[1]));
}

/*
 * The point `look` units of arc down the polyline org -> chain[0] -> ...
 * -> chain[n-1], measured horizontally. A runner does not stare at his
 * next footprint and not at the horizon either: he holds a point a fixed
 * stride-count down the road, and the road bends under it. The fixed
 * arc-distance is the whole of the anti-zigzag: the seed centers keep
 * arriving 3.2 times a second, but the point they define moves
 * continuously.
 */
static qboolean SG_PursuitPoint(vec3_t org, vec3_t chain[], int n,
                                float look, vec3_t out)
{
	vec3_t	cur;
	float	rem = look;
	int		i;

	if (n <= 0)
		return false;
	VectorCopy(org, cur);
	for (i = 0; i < n; i++)
	{
		vec3_t	seg;
		float	len;

		VectorSubtract(chain[i], cur, seg);
		seg[2] = 0.0f;
		len = VectorLength(seg);
		if (len < 1.0f)
		{
			VectorCopy(chain[i], cur);
			continue;
		}
		if (len >= rem)
		{
			float f = rem / len;

			out[0] = cur[0] + (chain[i][0] - cur[0]) * f;
			out[1] = cur[1] + (chain[i][1] - cur[1]) * f;
			out[2] = cur[2] + (chain[i][2] - cur[2]) * f;
			return true;
		}
		rem -= len;
		VectorCopy(chain[i], cur);
	}
	VectorCopy(cur, out);       /* chain ran out: aim at its far end */
	return true;
}

/*
 * Standing inside `keep` of either flag stand. The chain's hard veto: a
 * body that is about to touch a flag needs to be able to stop on it, and
 * an air-strafe is a commitment to a heading for the length of a flight.
 * Both stands, not just the goal one -- arriving at speed and leaving at
 * speed are the same mistake at the same place.
 */
static qboolean SG_NearAFlag(edict_t *e, float keep)
{
	int	t;

	if (!SG_Rune())
		return false;
	for (t = 0; t < 2; t++)
	{
		int		seed = t ? sg_fields.blue_flag_seed
		                 : sg_fields.red_flag_seed;
		vec3_t	fd;

		if (seed < 0)
			continue;
		VectorSubtract(SG_Rune()->seeds[seed].origin, e->s.origin, fd);
		if (VectorLength(fd) < keep)
			return true;
	}
	return false;
}

static qboolean SG_RunRoom(edict_t *e, int seed0, const int *route_field,
                           vec3_t dir, float want)
{
	vec3_t	chain[SG_PURSUIT_MAX], pp, pend, ch;
	trace_t	tr;
	float	acc = 0.0f, len;
	int		n = 0, cs = seed0;

	if (!SG_Rune() || cs < 0 || !route_field)
		return false;

	VectorCopy(SG_Rune()->seeds[cs].origin, chain[n]);
	n++;
	while (n < SG_PURSUIT_MAX && acc < want)
	{
		int			li5, nx5 = -1, nv5 = route_field[cs];
		rune_link_t	*l5;
		vec3_t		sgd;

		if (nv5 >= SG_FIELD_INF)
			break;
		for (li5 = SG_Rune()->first_link[cs]; li5 >= 0;
		     li5 = SG_Rune()->next_link[li5])
		{
			l5 = &SG_Rune()->links[li5];
			if (l5->action != RL_RUN)
				continue;
			if (l5->anchor[0] != 0.0f || l5->anchor[1] != 0.0f ||
			    l5->anchor[2] != 0.0f)
				continue;
			if (route_field[l5->to] < nv5)
			{
				nv5 = route_field[l5->to];
				nx5 = li5;
			}
		}
		if (nx5 < 0)
			break;
		cs = SG_Rune()->links[nx5].to;
		VectorCopy(SG_Rune()->seeds[cs].origin, chain[n]);
		VectorSubtract(chain[n], chain[n - 1], sgd);
		sgd[2] = 0.0f;
		acc += VectorLength(sgd);
		n++;
	}

	if (!SG_PursuitPoint(e->s.origin, chain, n, want, pp))
		return false;

	VectorSubtract(pp, e->s.origin, ch);
	ch[2] = 0.0f;
	len = VectorLength(ch);
	if (len < want * SG_AS_CHORD)
		return false;               /* the road bends inside the window */
	if ((ch[0] * dir[0] + ch[1] * dir[1]) / len <
	    cosf(SG_AS_BEND * (float)M_PI / 180.0f))
		return false;               /* and it has to go where we are going */

	/* the room, at the fan's own z-allowance: STEPSIZE, so stairs and
	 * ramps are road and not wall */
	pend[0] = pp[0];
	pend[1] = pp[1];
	pend[2] = e->s.origin[2] + 18.0f;
	tr = gi.trace(e->s.origin, e->mins, e->maxs, pend, e, MASK_PLAYERSOLID);
	/* a teammate is not terrain (the fan's exception, same reason) */
	if (tr.fraction < 1.0f && tr.ent && tr.ent->client && !tr.ent->deadflag)
		return true;
	return tr.fraction >= 1.0f;
}

static void SG_MovePolicy(edict_t *e, usercmd_t *cmd, vec3_t fwd,
                          vec3_t right, vec3_t dir,
                          qboolean open_ahead, qboolean run_link,
                          float frametime, const sg_air_t *air)
{
	float	sp2, sp, toward;
	int		pmf = e->client->ps.pmove.pm_flags;

	if (e->waterlevel > 1 || (pmf & PMF_DUCKED))
		return;

	sp2 = e->velocity[0] * e->velocity[0] + e->velocity[1] * e->velocity[1];
	if (sp2 < 200.0f * 200.0f)
		return;                 /* below this, straight ahead is the fastest
		                         * thing there is: addspeed is wide open */
	sp = sqrtf(sp2);

	/*
	 * The strafe leans off the direction of TRAVEL, so travel has to be
	 * roughly where the route wants to go before leaning off it means
	 * anything. A bot that needs to turn ninety degrees should turn, not
	 * harvest acceleration into the wall it is heading for.
	 */
	toward = (e->velocity[0] * dir[0] + e->velocity[1] * dir[1]) / sp;
	if (toward < 0.5f)
		return;

	if (e->groundentity)
	{
		/*
		 * Tap, never hold: PM_CheckJump sets PMF_JUMP_HELD when it fires and
		 * refuses every jump after it until a command arrives with upmove
		 * under 10. The caller releases after every step.
		 */
		/*
		 * 270, not 320: the audit under honest respawns (waves 84-86,
		 * 45k live samples) read fleet median 199 against a 300 run
		 * speed -- and this gate was the circle. Ground friction caps a
		 * run at 300, the hop is the only way THROUGH 300, and a hop
		 * gated at 320 waits for a speed that running cannot reach. Hop
		 * at the approach to the cap, let the air-strafe harvest do the
		 * exceeding: the carrier control group (median 310, straight
		 * sprints) already proved the ceiling is real.
		 */
		/*
		 * JUMP CHAINING (sg_airstrafe dose 2). Inside a live chain the
		 * gate is not "fast enough to be worth a hop" but "on the ground
		 * at all": PM_CheckJump runs BEFORE PM_Friction and a jump clears
		 * groundentity, which is the condition PM_Friction tests, so the
		 * step that leaves pays nothing and the step that stays pays
		 * speed * 6 * frametime. 270 was chosen to keep bots from hopping
		 * on the way up to running speed; a chain is already through it,
		 * and every ground step it spends waiting is the friction the
		 * whole feature exists to skip. TIME_LAND still binds -- the
		 * engine refuses the jump outright while it is set.
		 */
		if (run_link && open_ahead && !(pmf & PMF_TIME_LAND) &&
		    (sp > 270.0f || (air && air->chain)))
			cmd->upmove = 400;

		SG_Strafe(cmd, fwd, right, e->velocity, dir, sp, frametime, 10.0f);
	}
	else
	{
		/*
		 * THE LANDING TICK (sg_landtick, wave 286+). The think runs at
		 * 10Hz but pmove executes 8 sub-steps per command -- a jump
		 * decided only on frames that BEGIN grounded is a 1-in-8
		 * lottery against a mid-frame touchdown, and every lost draw
		 * pays speed * 6 * ft in friction. The demo census priced it:
		 * bots lose 66 u/s per touchdown (humans 34-46) and chain half
		 * the relaunches of a pub player. The human technique is the
		 * fix: HOLD jump while falling, and the landing sub-step fires
		 * it frictionless (PMF_JUMP_HELD was already released the
		 * frame after the previous hop, so the hold is armed).
		 */
		/* 240, not the ground gate's 270: touch-loss drops the landing
		 * under 270 and disarms the very hold meant to prevent it --
		 * air speed at arm time understates speed at the touchdown the
		 * hold is FOR (wave 289-290 read: relaunches +45% where armed) */
		/* a chain holds the same jump for the same reason, without
		 * needing sg_landtick set: the hold IS the chain */
		if ((sg_cv.landtick->value ||
		     (air && air->chain)) &&
		    run_link && open_ahead &&
		    e->velocity[2] < 0.0f && sp > 240.0f)
			cmd->upmove = 400;

		if (air)
			SG_AirStrafeCmd(cmd, air, e->velocity, sp, frametime);
		else
			SG_Strafe(cmd, fwd, right, e->velocity, dir, sp, frametime, 1.0f);
	}
}

/*
 * IN-WORLD PLAN DRAWING (sg_drawplan, capability census gap 9).
 *
 * A route defect is obvious to the eye and nearly invisible in a log: an
 * orbit, a chain that doubles back on itself, a belief parked inside a
 * wall. Set sg_drawplan to a bot's client number PLUS ONE (or -1 for every
 * bot) and once a second that bot draws what it has committed to -- a beam
 * from the body to the current link's destination, a second beam on to
 * where the route field goes after that, and a short post at every enemy
 * position its own team still believes in. A spectator then watches the
 * decision instead of reconstructing it.
 *
 * Cost when the cvar is 0, which is always in a real match: one float
 * compare per bot per frame. The cvar itself is read inside, so it is read
 * at most once per bot per second and never on a hot path.
 *
 * The wire format is the mod's own bfg-laser emission, copied from
 * g_weapon.c (bfg_think, the TE_BFG_LASER block): svc_temp_entity, the
 * effect byte, two positions, one multicast. The scope is the single
 * deviation -- MULTICAST_ALL instead of that site's MULTICAST_PHS, because
 * the spectator doing the diagnosing is rarely within earshot of the bot he
 * is watching, and a debug overlay that is off by default can afford it.
 */
static void SG_PlanBeam(vec3_t from, vec3_t to)
{
	gi.WriteByte(svc_temp_entity);
	gi.WriteByte(TE_BFG_LASER);
	gi.WritePosition(from);
	gi.WritePosition(to);
	gi.multicast(from, MULTICAST_ALL);
}

static void SG_DrawPlan(sg_bot_t *bot, int team, int link,
                        const int *route_field)
{
	edict_t	*e = bot->ent;
	vec3_t	a, b, c;
	int		dp, k, nx = -1;

	dp = (int)sg_cv.drawplan->value;
	if (!dp || !SG_Rune() || !e || !e->client || !e->inuse)
		return;
	if (dp > 0 && dp - 1 != (int)(e->client - game.clients))
		return;

	/*
	 * The committed link is the one this frame chose; when the final
	 * approach drops the link entirely (the last ten metres are a straight
	 * line) the incumbent is what the bot was last riding, and that is the
	 * honest thing to draw.
	 */
	if (link < 0)
		link = bot->sticky_link;

	VectorCopy(e->s.origin, a);
	a[2] += 16.0f;

	if (link >= 0 && link < SG_Rune()->hdr.num_links)
	{
		int to = SG_Rune()->links[link].to;

		VectorCopy(SG_Rune()->seeds[to].origin, b);
		b[2] += 16.0f;
		SG_PlanBeam(a, b);

		/*
		 * One more step down the same field, by the same first-order
		 * descent the lookahead aim uses -- so the second segment is
		 * the route the bot is about to take rather than a guess at it.
		 */
		if (route_field)
		{
			int li2, nv = route_field[to];

			for (li2 = SG_Rune()->first_link[to]; li2 >= 0;
			     li2 = SG_Rune()->next_link[li2])
			{
				if (route_field[SG_Rune()->links[li2].to] < nv)
				{
					nv = route_field[SG_Rune()->links[li2].to];
					nx = li2;
				}
			}
		}
		if (nx >= 0)
		{
			VectorCopy(SG_Rune()->seeds[SG_Rune()->links[nx].to].origin, c);
			c[2] += 16.0f;
			SG_PlanBeam(b, c);
		}
	}

	/* the belief the route was priced against: one post per sighting still
	 * inside the staleness window (sg_caco.c owns the table) */
	if (team == CTF_TEAM_RED || team == CTF_TEAM_BLUE)
	{
		for (k = 0; k < SG_MAX_ENEMY_TRACK; k++)
		{
			sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][k];

			if (en->client < 0 || en->seed < 0 ||
			    en->seed >= SG_Rune()->hdr.num_seeds)
				continue;
			if (SG_AgeOver(en->seen_time, SG_BELIEF_STALE))
				continue;
			VectorCopy(SG_Rune()->seeds[en->seed].origin, b);
			VectorCopy(b, c);
			c[2] += 72.0f;
			SG_PlanBeam(b, c);
		}
	}
}

/*
 * THE GRADIENT MADE FLESH (split from SG_BotThink, 2026-08-11 standards
 * pass; body verbatim): the aim -- link, anchor, or goal-entity
 * fallback with the through-extension -- the feeler fan, the ribbon,
 * lookahead and pursuit, doors, drops, and the hook brake. Emits the
 * movement policy the command stage executes.
 */
void Think_Move(sg_bot_t *bot, sg_think_t *tc, usercmd_t *cmd)
{
	/* the former parameter list, unpacked from the think context so the
	 * body below reads exactly as it did when these arrived as arguments;
	 * cmd stays a real parameter until the whole frame speaks context.
	 * Eight former parameters -- carrying, live, w, route_pure,
	 * post_sight, duel_org, duel_want, duel_expo -- were never read by
	 * this body and have no unpack. */
	edict_t *e = tc->e;
	sg_role_t role = tc->role;
	int team = tc->team;
	const int *goal_field = tc->goal_field;
	const int *route_field = tc->route_field;
	int bestlink = tc->bestlink;
	qboolean precision = tc->precision;
	qboolean hold_post = tc->hold_post;
	qboolean rally_hold = tc->rally_hold;
	qboolean rail_hold = tc->rail_hold;
	float post_yaw = tc->post_yaw;
	qboolean duel = tc->duel;

	vec3_t move_dir;
	float view_yaw = tc->view_yaw, view_pitch = tc->view_pitch;
	qboolean have_move = false, open_ahead = false, run_link = false;
	int door_hold = 0;
	edict_t *door_ent = NULL;
	qboolean drop_yaw_locked = false;
	float drop_yaw = 0.0f;
	qboolean hook_brake = false;
	vec3_t want, d;

	VectorClear(move_dir);
	VectorClear(want);
	VectorClear(d);

		vec3_t aim;
		qboolean have_aim = false;
		qboolean aim_is_anchor = false;
		qboolean jump_now = false;

		/*
		 * Just let go of a rope: the prover steered forwardmove 400 at the
		 * destination until the phantom grounded (SG_Rune().c:529-534), and
		 * the link was only recorded because that landing worked. The body
		 * flies the same approach instead of falling back down the wall it
		 * just climbed.
		 */
		/* rocket-jump phase steps run before the aim is built */
		if (bot->rj_phase)
		{
			static gitem_t *rj_rl2;

			if (!rj_rl2)
				rj_rl2 = FindItem("Rocket Launcher");
			if (SG_TimerReadyStrict(bot->rj_deadline))
				bot->rj_phase = 0;
			else if (bot->rj_phase == 1 && e->client->pers.weapon == rj_rl2)
			{
				bot->rj_phase = 2;
				/* two weapon frames to guarantee the fire state runs */
				SG_TimerArm(&bot->rj_fire_until, 0.25f);
			}
			else if (bot->rj_phase == 2 && SG_TimerReadyStrict(bot->rj_fire_until))
			{
				bot->rj_phase = 3;
				SG_TimerArm(&bot->rj_deadline, 2.5f);
			}
			else if (bot->rj_phase == 3 && e->groundentity)
			{
				bot->rj_phase = 0;
				bot->commit_link = -1;  /* the arc ended; argue fresh */
			}
		}

		/* flying the arc: the landing is the aim, as with a cut rope */
		if (bot->rj_phase == 3)
		{
			VectorCopy(bot->rj_dest, aim);
			have_aim = true;
		}

		/*
		 * THE PRE-TURN (sg_preturn, wave 224). The grammar census: a
		 * human flies with eyes a median 93 degrees off velocity --
		 * the body arrives already facing where it goes NEXT. During a
		 * cut-rope flight the aim slides from the landing spot to the
		 * best onward step from the landing seed, so touchdown exits
		 * at speed instead of standing to re-argue. Ballistics are
		 * unchanged -- only the eyes and the landing basis pre-align.
		 */
		if (sg_cv.preturn->value &&
		    ((bot->hook_phase == 3 && bot->flow_release) ||
		     bot->rj_phase == 3) &&
		    !e->groundentity && SG_Rune())
		{
			int ls = Rune_NearestSeed(SG_Rune(),
			    (bot->rj_phase == 3) ? bot->rj_dest : bot->hook_dest);

			if (ls >= 0)
			{
				int li3, nx3 = -1, nv3;
				const int *rf3 = goal_field;

				nv3 = (rf3[ls] < SG_FIELD_INF) ? rf3[ls] : 0x7fffffff;
				for (li3 = SG_Rune()->first_link[ls]; li3 >= 0;
				     li3 = SG_Rune()->next_link[li3])
				{
					rune_link_t *l3 = &SG_Rune()->links[li3];

					if (l3->action != RL_RUN)
						continue;
					if (rf3[l3->to] < nv3)
					{
						nv3 = rf3[l3->to];
						nx3 = li3;
					}
				}
				if (nx3 >= 0)
				{
					/* one step: the dose curve peaked here (+46%%
					 * chains) and flattened at two (226: the far aim
					 * clips corridor corners). The next tile it is. */
					VectorCopy(
					    SG_Rune()->seeds[SG_Rune()->links[nx3].to].origin,
					    aim);
					have_aim = true;
				}
			}
		}

		if (bot->hook_phase == 3)
		{
			/*
			 * APEX CHAINING: hook short of the lip, cut, and at the top
			 * of the throw the next rope is already legal -- the classic
			 * chain (named exactly by the owner). A flow-cut flight ends
			 * its phase at the apex (vertical speed dying, still
			 * airborne), the surface argues the next step from the air,
			 * and if that step is a rope it fires right there.
			 */
			if (bot->flow_release && !e->groundentity &&
			    e->velocity[2] < 60.0f)
			{
				bot->hook_phase = 0;
				bot->flow_release = false;
				bot->commit_link = -1;
				/* ropetravel: a clean apex is a link in the chain --
				 * the next rope is legal on the next beat */
				if (sg_cv.ropetravel->value > 0.0f)
					SG_TimerArm(&bot->speedhook_next, 0.25f);
				if (sg_cv.debug->value)
					gi.dprintf("HOOKEND %s apex\n",
					           e->client->pers.netname);
			}
			else if (e->groundentity || SG_TimerReadyStrict(bot->hook_deadline))
			{
				if (sg_cv.debug->value)
				{
					if (e->groundentity)
					{
						vec3_t ld;

						VectorSubtract(bot->hook_dest, e->s.origin, ld);
						gi.dprintf("HOOKLAND %s dist=%.0f dz=%.0f\n",
						           e->client->pers.netname,
						           sqrtf(ld[0] * ld[0] + ld[1] * ld[1]),
						           ld[2]);
					}
					else
						/* deadline hit still airborne: the throw
						 * never came down anywhere useful */
						gi.dprintf("HOOKEND %s drop\n",
						           e->client->pers.netname);
				}
				bot->hook_phase = 0;
				if (!bot->flow_release)
					SG_TimerArm(&bot->hook_landbrake, 0.3f);
				bot->flow_release = false;
				/* a rope ride ENDS its commitment: wherever this landing
				 * is, the next step is argued fresh from here */
				bot->commit_link = -1;
				/*
				 * HOOK PING-PONG SHELF (sg_hookpong, rung-2 revisit
				 * diagnosis 2026-08-11): 29% of all A-B-A events sit
				 * on three HOOK-heavy spans at 8-45x the human rate --
				 * grapple-decision oscillation, not field flatness,
				 * which is why pricing immediate returns had nothing
				 * to bite on. If this landing puts the body back where
				 * the PREVIOUS ride departed within 8s, the ridden
				 * link joins the shelf ring exactly like a failed
				 * anchor: the pair stops flapping for 45s and the
				 * surface argues a different road.
				 */
				if (sg_cv.hookpong->value > 0.0f &&
				    bot->hp_prev_land > 0.0f &&
				    SG_AgeUnder(bot->hp_prev_land, 8.0f) &&
				    bot->hook_link >= 0)
				{
					vec3_t hpd;

					VectorSubtract(e->s.origin, bot->hp_prev_dep, hpd);
					if (VectorLength(hpd) < 250.0f)
					{
						int b9, old9 = 0;

						for (b9 = 0; b9 < SG_BL_MAX; b9++)
							if (bot->bl_until[b9] < bot->bl_until[old9])
								old9 = b9;
						bot->bl_link[old9] = bot->hook_link;
						SG_TimerArm(&bot->bl_until[old9], 45.0f);
						SG_TeachLinkFutility(bot->hook_link);
						if (sg_cv.debug->value)
							gi.dprintf("HOOKPONG %s link=%d\n",
							           e->client->pers.netname,
							           bot->hook_link);
					}
				}
				VectorCopy(bot->hp_cur_dep, bot->hp_prev_dep);
				SG_Mark(&bot->hp_prev_land);

				/* ropetravel: a grounded landing chains too -- the beat
				 * is slightly longer than the apex's because the legs
				 * carry a step before the next throw */
				if (sg_cv.ropetravel->value > 0.0f &&
				    e->groundentity && bot->hookfail_streak == 0)
					SG_TimerArm(&bot->speedhook_next, 0.35f);

				/*
				 * A ride that did not SERVE the field failed, and a
				 * failed anchor gets shelved on the spot. Without this,
				 * sibling anchors flap (each landing re-argues, picks
				 * the other, neither converts) and the 4s same-link
				 * watch never fires -- smap05's attackers rode ropes in
				 * place for 180 seconds a game at the water's edge.
				 */
				if (bot->hook_link >= 0 &&
				    bot->hook_link < SG_Rune()->hdr.num_links &&
				    bot->seed >= 0 &&
				    goal_field[bot->seed] < SG_FIELD_INF)
				{
					rune_link_t *hl = &SG_Rune()->links[bot->hook_link];

					if (goal_field[hl->to] < SG_FIELD_INF &&
					    goal_field[bot->seed] >
					        goal_field[hl->to] + 300)
					{
						int b, oldest = 0;

						for (b = 0; b < SG_BL_MAX; b++)
							if (bot->bl_until[b] < bot->bl_until[oldest])
								oldest = b;
						bot->bl_link[oldest] = bot->hook_link;
						SG_TimerArm(&bot->bl_until[oldest], 60.0f);
						/*
						 * Two failed rides in a row and the rope is
						 * CONFISCATED: shelving one anchor at a time
						 * drained a doorway's fan of near-identical
						 * ropes failure by failure while the door
						 * itself stood open a walk away (wave 98,
						 * narrated live in capitals). Twenty seconds
						 * on the legs beats another lap of the wall.
						 */
						bot->hookfail_streak++;
						if (bot->hookfail_streak >= 2)
						{
							SG_TimerArm(&bot->hookban_until, 20.0f);
							bot->hookfail_streak = 0;
							/*
							 * Arming the hook ban releases any committed
							 * leg that needs the hook, so the pricing skip
							 * can take effect at the next replan.
							 * Think_CommitLink (sg_descend.c) holds a
							 * chosen link across frames until it arrives,
							 * overachieves, times out, or gets shelved --
							 * none of which fires just because a rope got
							 * banned this frame. Left holding a hook leg,
							 * the router keeps re-affirming the same
							 * banned link every think and the body has
							 * nothing left it is allowed to execute: it
							 * stands at the anchor point for the length of
							 * the ban. Clearing the commitment through the
							 * same field a finished ride clears (commit_link
							 * = -1) forces a fresh pick next think, and the
							 * RL_HOOK skip already in the pricing loop
							 * steers that pick onto a walking route.
							 */
							if (bot->commit_link >= 0 &&
							    bot->commit_link < SG_Rune()->hdr.num_links &&
							    SG_Rune()->links[bot->commit_link].action ==
							        RL_HOOK)
								bot->commit_link = -1;
							if (sg_cv.debug->value)
								gi.dprintf("HOOKBAN %s 20s\n",
								           e->client->pers.netname);
						}
						if (sg_cv.debug->value)
							gi.dprintf("HOOKFAIL %s link=%d\n",
							           e->client->pers.netname,
							           bot->hook_link);
					}
					else
					{
						/* the ride served: forgiveness */
						bot->hookfail_streak = 0;
					}
				}
				bot->hook_link = -1;
			}
			else
			{
				VectorCopy(bot->hook_dest, aim);
				have_aim = true;
			}
		}

		if (!have_aim && bestlink >= 0)
		{
			rune_link_t *l = &SG_Rune()->links[bestlink];

			VectorCopy(SG_Rune()->seeds[l->to].origin, aim);
			if (sg_cv.ribbon->value > 0.0f &&
			    l->action == RL_RUN && bot->ribbon_off != 0.0f)
			{
				vec3_t rdir, roff, rprobe;
				trace_t rtr;

				VectorSubtract(aim, e->s.origin, rdir);
				rdir[2] = 0.0f;
				if (VectorLength(rdir) > 32.0f)
				{
					float rl = VectorLength(rdir);

					roff[0] = -rdir[1] / rl * bot->ribbon_off;
					roff[1] = rdir[0] / rl * bot->ribbon_off;
					roff[2] = 0.0f;
					VectorAdd(aim, roff, rprobe);
					rtr = gi.trace(e->s.origin, e->mins, e->maxs, rprobe,
					               e, MASK_PLAYERSOLID);
					/* the corridor decides: a blocked offset collapses to the
					 * seed line -- the band is only as wide as the room */
					if (rtr.fraction >= 1.0f)
						VectorCopy(rprobe, aim);
				}
			}
			have_aim = true;

			/*
			 * THE LOOKAHEAD (sg_lookahead, A/B wave 178+). The Brownian
			 * hunt's terminal finding: unopposed attackers churn their
			 * COMMANDED heading 81 degrees a second while goals, seeds,
			 * links, and dodges all measure stable -- the body servos on
			 * the center of a seed it is about to overrun, overshoots,
			 * turns back, orbits. A runner does not stare at his next
			 * footprint; he looks down the road. When the immediate seed
			 * is close (inside 160) and the route continues, the aim
			 * slides one RUN link further down the gradient: the seed
			 * underfoot becomes something passed THROUGH, the heading
			 * derives from where the route goes next, and the fan still
			 * owns the last arm's-length via precision mode. Only plain
			 * runs chain -- hooks, jumps, drops keep exact aim, their
			 * geometry is the point.
			 */
			if (sg_cv.lookahead->value &&
			    !sg_cv.pursuit->value &&
			    l->action == RL_RUN && !precision)
			{
				vec3_t nd0;

				VectorSubtract(SG_Rune()->seeds[l->to].origin,
				               e->s.origin, nd0);
				nd0[2] = 0.0f;
				if (VectorLength(nd0) < 160.0f)
				{
					int li2, nx = -1, nv = route_field[l->to];

					for (li2 = SG_Rune()->first_link[l->to];
					     li2 >= 0; li2 = SG_Rune()->next_link[li2])
					{
						rune_link_t *l2 = &SG_Rune()->links[li2];

						if (l2->action != RL_RUN)
							continue;
						if (route_field[l2->to] < nv)
						{
							nv = route_field[l2->to];
							nx = li2;
						}
					}
					if (nx >= 0)
						VectorCopy(
						    SG_Rune()->seeds[SG_Rune()->links[nx].to].origin,
						    aim);
				}
			}
			/*
			 * A RUN link with a stored waypoint is one whose proof had to
			 * ROUND something -- the oracle's detour apex lives in the
			 * anchor (empty since the format was born, now earning rent).
			 * Steer via it until it is done, then at the destination; the
			 * fan still handles the last arm's-length. This is the body
			 * finally walking the line the proof actually walked, instead
			 * of the chord the proof never claimed.
			 */
			if (l->action == RL_RUN &&
			    (l->anchor[0] != 0.0f || l->anchor[1] != 0.0f ||
			     l->anchor[2] != 0.0f))
			{
				vec3_t wd;

				VectorSubtract(l->anchor, e->s.origin, wd);
				wd[2] = 0.0f;
				if (VectorLength(wd) > 48.0f)
				{
					VectorCopy(l->anchor, aim);
					aim_is_anchor = true;
				}
			}

			/*
			 * PURE PURSUIT (sg_pursuit, wave 311+). The demo census
			 * reconstructed the COMMANDED heading from the rune alone:
			 * aiming at the next seed center churns 68-78 deg/s at a
			 * 42-45%% reversal rate -- the whole of the Brownian walk,
			 * before the fan or combat touch it -- because the target
			 * sits a median 54-60 units out and jumps 3.2 times a
			 * second. Where the chain is GEOMETRICALLY STRAIGHT the
			 * body still turns 40 deg/s. The same reconstruction on a
			 * point held 300 units down the chain reads 36-43 deg at
			 * 29-31%%. The seed centers are beads on a road; steer at
			 * the road. The cvar value IS the arc distance.
			 */
			if (sg_cv.pursuit->value > 0.0f &&
			    !aim_is_anchor && l->action == RL_RUN && !precision &&
			    e->waterlevel < 2 && bot->hook_phase == 0 &&
			    bot->rj_phase == 0)
			{
				vec3_t	chain[SG_PURSUIT_MAX];
				float	look = sg_cv.pursuit->value;
				int		nchain = 0, cs = l->to, k;
				float	acc = 0.0f;

				VectorCopy(SG_Rune()->seeds[cs].origin, chain[nchain]);
				nchain++;
				while (nchain < SG_PURSUIT_MAX && acc < look)
				{
					int li5, nx5 = -1, nv5 = route_field[cs];
					vec3_t sgd;

					if (nv5 >= SG_FIELD_INF)
						break;
					for (li5 = SG_Rune()->first_link[cs]; li5 >= 0;
					     li5 = SG_Rune()->next_link[li5])
					{
						rune_link_t *l5 = &SG_Rune()->links[li5];

						/* plain runs only: jumps/ropes/drops are
						 * geometry executed exactly, and a pursuit
						 * point across a gap steers off the lip */
						if (l5->action != RL_RUN)
							continue;
						/* a link whose proof had to ROUND something is
						 * not a chord -- its anchor IS the route */
						if (l5->anchor[0] != 0.0f ||
						    l5->anchor[1] != 0.0f ||
						    l5->anchor[2] != 0.0f)
							continue;
						if (route_field[l5->to] < nv5)
						{
							nv5 = route_field[l5->to];
							nx5 = li5;
						}
					}
					if (nx5 < 0)
						break;
					cs = SG_Rune()->links[nx5].to;
					VectorCopy(SG_Rune()->seeds[cs].origin, chain[nchain]);
					VectorSubtract(chain[nchain], chain[nchain - 1], sgd);
					sgd[2] = 0.0f;
					acc += VectorLength(sgd);
					nchain++;
				}

				/*
				 * THE CORNER GUARD. A chord held 300 units out crosses
				 * whatever stands inside the bend. The fan's own
				 * player-box trace, run to the pursuit point; if the box
				 * does not fit, walk the point back down the chain a
				 * vertex at a time. At k == 1 the point IS the seed
				 * center -- the guard can only cost the improvement,
				 * never the safety.
				 */
				for (k = nchain; k >= 1; k--)
				{
					vec3_t pp, pend;
					trace_t ptr;

					if (!SG_PursuitPoint(e->s.origin, chain, k, look, pp))
						continue;
					pend[0] = pp[0];
					pend[1] = pp[1];
					/* sg_pursuitz: chord z-allowance. 8 vetoed every
					 * stair and ramp (311: guard collapsed the chord
					 * to the seed center most ticks); 18 is STEPSIZE */
					pend[2] = e->s.origin[2] +
					          (sg_cv.pursuitz->value);
					ptr = gi.trace(e->s.origin, e->mins, e->maxs, pend,
					               e, MASK_PLAYERSOLID);
					/* a teammate is not terrain, and a door is not a
					 * wall (the fan's two exceptions, same reasons) */
					if (ptr.fraction < 1.0f && ptr.ent &&
					    ((ptr.ent->client && !ptr.ent->deadflag) ||
					     (ptr.ent->classname &&
					      strncmp(ptr.ent->classname, "func_door",
					              9) == 0)))
						ptr.fraction = 1.0f;
					if (ptr.fraction >= 1.0f)
					{
						VectorCopy(pp, aim);
						/* guard census: k==nchain is the full chord,
						 * k==1 is a collapse to today's behavior */
						if (sg_cv.debug->value &&
						    SG_TimerReady(bot->next_report - 0.9f))
							gi.dprintf("PURSUITK %s k=%d n=%d\n",
							           e->client->pers.netname,
							           k, nchain);
						break;
					}
				}
			}
			/*
			 * EDGERIDE (sg_edgeride, owner ruling on Open Question #2).
			 * The forensics: 62% of the last rung-2 tell is human feet
			 * 10-40u past the seed edge on walkway margins. Ribbon
			 * offsets died here twice because lookahead and pursuit
			 * OVERWRITE the aim after the offset lands ("the steering
			 * re-centers whatever the aim does"). So the offset is
			 * re-applied LAST, to the final road point, per-leg side
			 * and amplitude from the ribbon state scaled to the cvar.
			 * The wall trace still collapses a blocked offset; the
			 * feeler fan and every fall guard run downstream untouched
			 * -- feet ride AT the safety boundary, never past it.
			 * Carrier exempt. Falls are the trial's kill switch.
			 */
			if (sg_cv.edgeride->value > 0.0f &&
			    l->action == RL_RUN && !precision &&
			    e->groundentity && bot->hook_phase == 0 &&
			    sg_cur_role != SG_ROLE_CARRY &&
			    bot->ribbon_off != 0.0f)
			{
				vec3_t edir, eoff, eprobe;
				trace_t etr;
				float el, esc;

				VectorSubtract(aim, e->s.origin, edir);
				edir[2] = 0.0f;
				el = VectorLength(edir);
				if (el > 48.0f)
				{
					esc = sg_cv.edgeride->value /
					      ((sg_cv.ribbon->value
					        > 0.0f)
					       ? sg_cv.ribbon->value
					       : 48.0f);
					eoff[0] = -edir[1] / el * bot->ribbon_off * esc;
					eoff[1] = edir[0] / el * bot->ribbon_off * esc;
					eoff[2] = 0.0f;
					VectorAdd(aim, eoff, eprobe);
					etr = gi.trace(e->s.origin, e->mins, e->maxs,
					               eprobe, e, MASK_PLAYERSOLID);
					if (etr.fraction >= 1.0f)
						VectorCopy(eprobe, aim);
				}
			}

			if (l->action == RL_JUMP && e->groundentity)
			{
				/*
				 * A momentum link's proof entered at 320 u/s and jumped
				 * off that speed; hopping without it lands in the gap.
				 * Hold the run until the body carries most of what the
				 * envelope claims (from-rest links claim zero and hop
				 * as they always did).
				 */
				float jh = sqrtf(e->velocity[0] * e->velocity[0] +
				                 e->velocity[1] * e->velocity[1]);

				if (jh >= (float)(l->min_speed * 4) * 0.8f)
					jump_now = true;
			}
			/* the landing hop belongs on running ground, not on a link
			 * whose traversal is itself a jump, a drop, a rope or a swim */
			if (l->action == RL_RUN)
			{
				run_link = true;

				/*
				 * THE SPEED HOOK -- the tech that made LMCTF movement an
				 * art: a short rope thrown high-ahead on an open runway,
				 * ridden three tenths of a second to the pull's 800 u/s,
				 * released, momentum kept. Fired ON THE RUN (no brake, no
				 * standing frame -- the anchor sits near the current view
				 * line so the slew arrives in a step or two), gated to
				 * long clear stretches when nobody is engaged and the
				 * legs are the bottleneck. Cooldown keeps it a burst,
				 * not a lifestyle.
				 */
				/*
				 * FREERIDE (sg_freeride, rung-2 off-graph tell, owner
				 * signed 2026-08-07). The speed hook was already the
				 * right tech; the judges' 3-18% human off-graph mass
				 * against our 0.026 was a DOSE gap: the burst was gated
				 * to the far half of the map (goal_field > 4000), one
				 * rope per 4s, and a straight-ahead probe only. Under
				 * freeride the runway reaches the approach (> 2000 --
				 * the no-ropes-in-the-house rule still owns the flag
				 * room), the cooldown halves, the speed window's top
				 * lifts to 560 (humans chain ropes off fast runs), and
				 * a missed straight probe retries once at +/-22 deg --
				 * the corner sling. Everything else -- persona taste,
				 * the worth bar, the plane test, hookban -- unchanged.
				 */
				if (bot->hook_phase == 0 && !bot->engaged_last &&
				    SG_TimerReady(bot->hookban_until) &&
				    SG_TimerReady(bot->speedhook_next) &&
				    e->groundentity && e->waterlevel == 0 &&
				    goal_field[bot->seed] >
				        ((sg_cv.freeride->value > 0.0f ||
				          sg_cv.ropetravel->value > 0.0f)
				             ? 2000 : 4000))
				{
					/*
					 * ROPE-PRIMARY TRAVEL (sg_ropetravel, rung-2's
					 * named blocker): humans TRAVEL by rope -- 3-18%
					 * of their samples are flight -- and the
					 * arithmetic killed every occasional-rope cut
					 * (0.3% of player time at doubled volume). Under
					 * ropetravel the successful ride's cooldown drops
					 * to a 0.25s chain beat (set at the landing/apex
					 * sites), the speed ceiling lifts to 700 so fast
					 * landings chain, and the apex-chaining machinery
					 * already in the tree does the rest. The rope
					 * becomes the gait; legs become the connector.
					 */
					float hsp2 = e->velocity[0] * e->velocity[0]
					           + e->velocity[1] * e->velocity[1];
					float hcap = (sg_cv.ropetravel->value > 0.0f) ? 700.0f :
					             (sg_cv.freeride->value
					              > 0.0f) ? 560.0f : 480.0f;

					if (hsp2 > 220.0f * 220.0f && hsp2 < hcap * hcap)
					{
						vec3_t hd, heye, hend;
						trace_t htr;
						float hyaw;
						int hfan;

						VectorSubtract(aim, e->s.origin, hd);
						hyaw = atan2f(hd[1], hd[0]);
						heye[0] = e->s.origin[0];
						heye[1] = e->s.origin[1];
						heye[2] = e->s.origin[2] + e->viewheight;
						/*
						 * WANDER THROW (sg_ropetravel 2). The wave
						 * reports killed the volume theory: fleet
						 * bots already land ~600 ropes a game and
						 * off-graph never moves, because proven-link
						 * flight rides exactly the air the node cloud
						 * was mined from. The human 0.03-0.18 is
						 * IDIOSYNCRATIC flight -- one-off arcs through
						 * space nobody routes through. Dose 2: one
						 * throw in ~7 widens the fan to +/-60 degrees
						 * and takes the arc for its own sake.
						 */
						int hwander =
						    (sg_cv.ropetravel->value
						     >= 2.0f && (rand() % 7) == 0);

						for (hfan = 0; hfan < 3; hfan++)
						{
						float hy2 = hyaw + (hwander
						    ? ((hfan - 1) * 1.05f)
						    : ((hfan == 1) ? 0.384f :
						       (hfan == 2) ? -0.384f : 0.0f));
						/*
						 * FREERIDE v2 (sg_freeride 2, rung-2 set #2:
						 * failed 16/18 on the same off-graph tell).
						 * v1 doubled ride volume and moved NOTHING,
						 * because a 30-degree rope skims the corridor
						 * and its flight never leaves the node cloud;
						 * human off-graph mass is HIGH arcs through
						 * open room air. v2 probes ~54 degrees up:
						 * shorter reach, higher anchor, and the swing
						 * itself is the off-graph flight.
						 */
						float hfar = (sg_cv.freeride->value >= 2.0f) ? 300.0f : 480.0f;
						float hup  = (sg_cv.freeride->value >= 2.0f) ? 420.0f : 280.0f;

						hend[0] = heye[0] + cosf(hy2) * hfar;
						hend[1] = heye[1] + sinf(hy2) * hfar;
						hend[2] = heye[2] + hup;    /* v1 ~30deg, v2 ~54deg */
						htr = gi.trace(heye, NULL, NULL, hend, e,
						               MASK_SOLID);
						/*
						 * The bar the optional rope has to clear, and the
						 * one number on this gate that is a PREFERENCE
						 * rather than a fact -- the speed window and the
						 * ground test are physics, but "is 170 units of
						 * rope worth the throw" is taste. Slip's taste is
						 * a shorter rope more often; Vore's is to keep
						 * running. Divided, not multiplied: enthusiasm
						 * LOWERS the bar. The cooldown below moves with
						 * it so an eager bot also comes back to it
						 * sooner, and both are 1.0 with no persona.
						 */
						if (htr.fraction >= 1.0f || htr.startsolid ||
						    htr.fraction * 560.0f <=
						        170.0f / SG_PersonaHookScale(e) ||
						    htr.plane.normal[2] >= 0.7f)
						{
							/* the side probes exist only under
							 * freeride; stock behavior is one look */
							if (sg_cv.freeride->value
							    > 0.0f ||
							    sg_cv.ropetravel->value > 0.0f)
								continue;
							break;
						}
						{
							/* wave 218 (sg_legcarrier): the burst is
							 * the OPTIONAL rope -- a standing aim for
							 * speed legs already have. The carrier
							 * keeps climb ropes and loses the
							 * ceremony; everyone else bursts on. */
							if (!(sg_cv.legcarrier->value &&
							      sg_cur_role == SG_ROLE_CARRY))
							{
								VectorCopy(htr.endpos, bot->hook_anchor);
								VectorCopy(aim, bot->hook_dest);
								VectorCopy(e->s.origin, bot->hp_cur_dep);
								VectorCopy(e->s.origin, bot->hp_cur_dep);
						bot->hook_phase = 1;
								SG_TimerArm(&bot->hook_deadline, 1.0f);
								bot->speedhook = true;
								SG_TimerArm(&bot->speedhook_next,
								    ((sg_cv.ropetravel->value > 0.0f) ? 1.0f :
								     (sg_cv.freeride->value > 0.0f) ? 2.0f : 4.0f)
								    / SG_PersonaHookScale(e));
							}
						}
						break;
						}
					}
				}
			}

			/*
			 * A hook link executes the way the rune proved it: aim at the
			 * STORED anchor, fire, ride the flat-800 pull, release near
			 * the destination or inside the brake band (the p_weapon.c
			 * ladder starts at 120), then steer the fall onto the landing.
			 * The view is the aim: LMCTF's Weapon_Hook_Fire fires along
			 * v_angle.
			 */
			if (l->action == RL_HOOK && bot->hook_phase == 0)
			{
				vec3_t ad;
				float alen;
				float hspd = sqrtf(e->velocity[0] * e->velocity[0] +
				                   e->velocity[1] * e->velocity[1]);

				vec3_t fsd;
				float fsdist;

				VectorSubtract(SG_Rune()->seeds[l->from].origin,
				               e->s.origin, fsd);
				fsd[2] = 0.0f;
				fsdist = VectorLength(fsd);

				/*
				 * The proof fired from THE SEED, at rest (ProveHook
				 * places the phantom on the seed origin and stands it
				 * still). Both halves matter: the rope-line clearance
				 * was traced from the seed's eye -- fired from 50 units
				 * away the same ray clips different geometry and the
				 * rope bites en route (smap05: 2552 attaches 150-650
				 * from their proven anchors) -- and the swing arc is
				 * entry-sensitive. Walk to the seed, brake, THEN fire:
				 * the shot the proof rolled is the shot the body takes.
				 */
				/* the walk-to-seed gate tried here was an over-fit to
				 * smap05: on lmctf03 it turned 95%-converting hooks into
				 * abort loops (it7: 547 misses on the healthy map). The
				 * brake and the eye aim stay; the pilgrimage goes. */
				(void)fsdist;
				if (hspd > 160.0f)
				{
					hook_brake = true;      /* fire next frame, slower */
					VectorCopy(l->anchor, aim);
				}
				else
				{
					VectorSubtract(l->anchor, e->s.origin, ad);
					alen = VectorLength(ad);
					if (alen > 1.0f)
					{
						VectorCopy(l->anchor, bot->hook_anchor);
						VectorCopy(SG_Rune()->seeds[l->to].origin,
						           bot->hook_dest);

						/*
						 * THE BALLISTIC ANCHOR. The rope sets speed to a
						 * flat 800 along itself, so a cut rope IS a
						 * projectile launch at 800 -- and where to hook
						 * for a fling that LANDS somewhere is therefore
						 * closed-form: solve the projectile equation for
						 * launch pitch to the destination at s=800, and
						 * the anchor is wherever that ray bites the
						 * world. Low solution first (flatter, faster),
						 * high if the low ray finds no bite. When
						 * neither ray bites within rope reach, the
						 * proof's stored anchor and a full ride remain
						 * -- physics first, terminus as the fallback.
						 */
						{
							vec3_t bd, eye2;
							float bx, by2, s2 = 800.0f * 800.0f;
							float grav2 = e->client->ps.pmove.gravity
							              ? (float)e->client->ps.pmove.gravity
							              : 800.0f;
							float disc;

							VectorSubtract(SG_Rune()->seeds[l->to].origin,
							               e->s.origin, bd);
							by2 = bd[2];
							bx = sqrtf(bd[0] * bd[0] + bd[1] * bd[1]);
							disc = s2 * s2 - grav2 *
							       (grav2 * bx * bx + 2.0f * by2 * s2);
							if (bx > 64.0f && disc > 0.0f)
							{
								float az = atan2f(bd[1], bd[0]);
								float root = sqrtf(disc);
								int lohi;

								VectorCopy(e->s.origin, eye2);
								eye2[2] += e->viewheight;
								for (lohi = 0; lohi < 2; lohi++)
								{
									float tanth = (s2 - (lohi ? -root : root))
									              / (grav2 * bx);
									float th = atanf(tanth);
									vec3_t ray, hit;
									trace_t btr;

									ray[0] = cosf(az) * cosf(th);
									ray[1] = sinf(az) * cosf(th);
									ray[2] = sinf(th);
									VectorMA(eye2, 700.0f, ray, hit);
									btr = gi.trace(eye2, NULL, NULL, hit,
									               e, MASK_SOLID);
									if (btr.fraction < 1.0f &&
									    !btr.startsolid &&
									    btr.fraction * 700.0f > 150.0f)
									{
										VectorCopy(btr.endpos,
										           bot->hook_anchor);
										break;
									}
								}
							}
						}
						bot->hook_link = bestlink;
						bot->hook_bite_logged = false;
						bot->hook_phase = 1;
						/* flight + climb budget: gravity fights the pull
						 * on a tall rope, so the real ascent runs well
						 * past the naive rope/800 figure -- a 1.5s margin
						 * cut every z=348 tower climb loose 176 short of
						 * its landing (A/B match, Slip, four identical
						 * shortfalls) */
						SG_TimerArm(&bot->hook_deadline,
						    alen / 800.0f + 3.0f);
					}
				}
			}

			/* a drop link goes via its stored lip, not the far endpoint */
			if (l->action == RL_DROP)
			{
				vec3_t lipd;
				float liph;

				VectorSubtract(l->anchor, e->s.origin, lipd);
				lipd[2] = 0.0f;
				liph = VectorLength(lipd);
				if (liph > 24.0f)
					VectorCopy(l->anchor, aim);
				/*
				 * The whole drop executes the way the prover walked it:
				 * straight at the lip, no fan (ProveDrop's approach walk
				 * steers dead at the lip, SG_Rune().c), then off along the
				 * RECORDED heading (dd_last_heading, SG_Rune().c:734). The
				 * fan, given a railing beside the gap, deflects off the
				 * exact line the proof demonstrated -- Phase orbited a
				 * balcony's proven drops 60-140 units from their lips,
				 * with a 96-unit lock radius it never entered.
				 */
				drop_yaw_locked = true;
				/* 8, not 24: the proof fell FROM the lip point, and at
				 * 20 units of lateral offset the recorded heading runs
				 * into the railing beside the gap (it13: attackers
				 * jittering on the balcony lip at goal 4700, dl=1) */
				drop_yaw = (liph > 8.0f)
				               ? atan2f(lipd[1], lipd[0]) * 180.0f / M_PI
				               : l->heading * (360.0f / 256.0f);
			}

			/*
			 * A rocket-jump link arms its sequence: raise the launcher,
			 * then one aim-and-fire frame on the PROVEN aim vector
			 * (anchor[0/1]; z = -sqrt(1-x^2-y^2), sg_rune.h), then fly
			 * the arc. The selection gate above already priced the
			 * health and checked the inventory.
			 */
			if (l->action == RL_ROCKETJUMP && bot->rj_phase == 0 &&
			    e->groundentity)
			{
				bot->rj_aim[0] = l->anchor[0];
				bot->rj_aim[1] = l->anchor[1];
				bot->rj_aim[2] = -sqrtf(1.0f -
				    l->anchor[0] * l->anchor[0] -
				    l->anchor[1] * l->anchor[1]);
				VectorCopy(SG_Rune()->seeds[l->to].origin, bot->rj_dest);
				bot->rj_phase = 1;
				SG_TimerArm(&bot->rj_deadline, 4.0f);
				bot->rj_use_next = 0.0f;
			}
		}

		/*
		 * Rocket-jump fire frame(s): the view IS the proven aim vector --
		 * down and behind, which is what throws the body forward -- while
		 * the jump and the trigger go down together. Weapon_RocketLauncher
		 * fires along v_angle on its fire frame, same contract as the hook.
		 */
		if (bot->rj_phase == 2)
		{
			float ry, rp;

			ry = atan2f(bot->rj_aim[1], bot->rj_aim[0]) * 180.0f / M_PI;
			rp = -asinf(bot->rj_aim[2]) * 180.0f / M_PI;
			cmd->angles[YAW] = ANGLE2SHORT(ry)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd->angles[PITCH] = ANGLE2SHORT(rp)
			                - e->client->ps.pmove.delta_angles[PITCH];
			view_yaw = ry;
			view_pitch = rp;
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 400;
			cmd->buttons |= BUTTON_ATTACK;
			have_move = false;
		}

		/* while aiming to fire, the cmd angles ARE the anchor bearing --
		 * this overrides the navigation view for exactly one frame */
		if (bot->hook_phase == 1)
		{
			vec3_t ad, hook_eye;
			float alen, ay, ap;

			/*
			 * From the EYES, not the origin: the proof drew its rope line
			 * from the phantom's eyes ("the rope line from the source's
			 * eyes wins", SG_Rune().c), and 22 units of vertical aim bias
			 * against a wall-face anchor bites a DIFFERENT surface --
			 * every smap05 ride was landing exactly 143 under its ledge
			 * because the rope was attaching under the proven point.
			 */
			VectorCopy(e->s.origin, hook_eye);
			hook_eye[2] += e->viewheight;
			VectorSubtract(bot->hook_anchor, hook_eye, ad);
			alen = VectorLength(ad);
			ay = atan2f(ad[1], ad[0]) * 180.0f / M_PI;
			ap = -asinf(ad[2] / (alen > 1.0f ? alen : 1.0f)) * 180.0f / M_PI;
			cmd->angles[YAW] = ANGLE2SHORT(ay)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd->angles[PITCH] = ANGLE2SHORT(ap)
			                - e->client->ps.pmove.delta_angles[PITCH];
			view_yaw = ay;
			view_pitch = ap;
		}
		else if (bot->seed >= 0 &&
		         goal_field[bot->seed] >= SG_FIELD_INF)
		{
			/*
			 * Off the known surface: the field is infinite here. The right
			 * move is not the goal line -- greedy steering at a distant
			 * goal orbits concave geometry forever, and did, for four
			 * minutes straight. It is the shortest walk ONTO the surface:
			 * the nearest seed where the field turns finite. From there
			 * the links take over.
			 */
			int i, best = -1;
			float bestd = 1e30f;

			for (i = 0; i < SG_Rune()->hdr.num_seeds; i++)
			{
				vec3_t dd;
				float dsq;

				if (goal_field[i] >= SG_FIELD_INF)
					continue;
				VectorSubtract(SG_Rune()->seeds[i].origin, e->s.origin, dd);
				dsq = dd[0] * dd[0] + dd[1] * dd[1] + dd[2] * dd[2] * 4.0f;
				if (dsq < bestd)
				{
					bestd = dsq;
					best = i;
				}
			}
			if (best >= 0)
			{
				VectorCopy(SG_Rune()->seeds[best].origin, aim);
				have_aim = true;
			}
		}

		if (!have_aim)
		{
			/* last resort: the goal itself, by belief */
			edict_t *gf = NULL;

			/*
			 * TERMINAL HOMING (carry forensics, wave 312): the carrier
			 * at its own stand aims at the LIVE FLAG ITEM, not the
			 * info_flag_* spawn marker. The two usually coincide --
			 * but the item droptofloors and on several stands settles
			 * offset enough that a bot walking exactly onto the marker
			 * orbits 16u from the touch that scores (the 200-second
			 * wedge orbits, 12 of 19 parity arrivals). The engine's
			 * own redflag/blueflag pointers name the real entity.
			 */
			if (role == SG_ROLE_CARRY)
				gf = SG_OwnFlag(team);

			if (!gf && role == SG_ROLE_ATTACK)
			{
				/* enemy stand position is common knowledge */
				edict_t *marker = SG_FlagStand(team, false);
				edict_t *enemy_item = SG_EnemyFlag(team);

				gf = marker;
				/*
				 * THE ROUTE GOES THROUGH THE FLAG (owner's order,
				 * 2026-08-11 -- his third strike on this disease).
				 * Wave 312 taught the CARRIER to aim at the live item
				 * because droptofloor settles it off the marker far
				 * enough to orbit; the grab side never got the fix, so
				 * attackers still walked to the marker and circled a
				 * flag a body-length away (conduct audit: stand-grind
				 * cluster, 1.5% vs 3.3% conversion). Aim at the live
				 * item -- but only while it sits at home: a dropped
				 * flag elsewhere is Rule 19 intel the bot may not have.
				 */
				if (enemy_item && marker &&
				    SG_DistXY(enemy_item->s.origin,
				              marker->s.origin) < 64.0f)
					gf = enemy_item;
			}
			else if (!gf)
			{
				gf = SG_FlagStand(team, true);
			}
			if (gf)
			{
				VectorCopy(gf->s.origin, aim);
				have_aim = true;

				/*
				 * GRAB THROUGH, NOT AT. Escape speed in the first five
				 * seconds of a carry measured 256 -- slower than plain
				 * running -- because the terminal walk aimed AT the
				 * flag: arrive, stop, turn, accelerate, die (365 samples
				 * across six ten-wide waves). A human runs THROUGH the
				 * stand at full stride, already on the exit line. Inside
				 * 160 units the aim shifts past the flag to the first
				 * seed of the route home; the touch happens mid-sprint
				 * and the grab inherits a running start.
				 */
				/*
				 * CAPTURE THROUGH (owner's point, wave 321): the same
				 * disease on the scoring touch -- converge, stop, cap.
				 * Inside 160 the carrier aims PAST its flag along the
				 * line it arrived on; the touch happens mid-stride.
				 */
				/*
				 * ...and the GRAB gets the same stride (owner's order,
				 * 2026-08-11): this block guarded on CARRY alone even
				 * though its own comment promised the attacker's grab a
				 * running start. The route now goes THROUGH the flag
				 * for both touches -- grab and cap.
				 */
				if ((role == SG_ROLE_CARRY || role == SG_ROLE_ATTACK) &&
				    bot->seed >= 0)
				{
					vec3_t fd7;
					float fl7;

					VectorSubtract(gf->s.origin, e->s.origin, fd7);
					fd7[2] = 0.0f;
					fl7 = VectorLength(fd7);
					if (fl7 > 1.0f && fl7 < 160.0f)
					{
						trace_t wtr;
						vec3_t wend;

						VectorScale(fd7, (fl7 + 150.0f) / fl7, fd7);
						VectorAdd(e->s.origin, fd7, wend);
						wend[2] = gf->s.origin[2] + 16.0f;
						/* flag against a wall (owner's edge case):
						 * the exit line stops where the room does --
						 * clamp the through-point at solid geometry,
						 * never closer than the flag itself */
						wtr = gi.trace(gf->s.origin, e->mins, e->maxs,
						               wend, e, MASK_PLAYERSOLID);
						if (wtr.fraction < 1.0f)
							VectorCopy(wtr.endpos, wend);
						VectorCopy(wend, aim);
						aim[2] = gf->s.origin[2];
					}
				}

				/*
				 * THE TERMINAL BRAKE (set-#4 forensics, wave 397: Field at
				 * its own stand, flag home, 118 seconds of full-sprint orbit
				 * at radius ~90). Pure physics: at 300 u/s under sg_turnrate
				 * the minimum turn radius IS that orbit -- a missile circling
				 * a target it aims at perfectly, and CAPTURE THROUGH feeds it
				 * by projecting the aim along an arrival line that rotates
				 * with the body. The human answer at every stand: brake into
				 * the turn. Throttle follows alignment inside 220u -- badly
				 * misaligned means slow, slow means tight, tight means the
				 * spiral ends in a touch. sg_termbrake 0 restores the orbit
				 * for A/B; the 5v0 canary's partial conversions are the
				 * standing instrument that has flagged this for 25 waves.
				 */
				if ((role == SG_ROLE_CARRY || role == SG_ROLE_ATTACK) &&
				    sg_cv.termbrake->value)
				{
					vec3_t tb, tv;
					float tbl, spd2;

					VectorSubtract(gf->s.origin, e->s.origin, tb);
					tb[2] = 0.0f;
					tbl = VectorLength(tb);
					VectorCopy(e->velocity, tv);
					tv[2] = 0.0f;
					spd2 = VectorLength(tv);
					if (tbl > 1.0f && tbl < 220.0f && spd2 > 120.0f)
					{
						float al = DotProduct(tv, tb) / (spd2 * tbl);

						if (al < 0.50f)
							bot->term_brake = 0.30f;
						else if (al < 0.85f)
							bot->term_brake = 0.55f;
					}
				}
				if (role == SG_ROLE_ATTACK && bot->seed >= 0)
				{
					vec3_t fd4;

					VectorSubtract(gf->s.origin, e->s.origin, fd4);
					fd4[2] = 0.0f;
					if (VectorLength(fd4) < 160.0f)
					{
						int fs = Rune_NearestSeed(SG_Rune(), gf->s.origin);
						int *home4 = (team == CTF_TEAM_RED)
						             ? sg_fields.to_red_flag
						             : sg_fields.to_blue_flag;

						if (fs >= 0)
						{
							int li4, best4 = -1, bv4 = home4[fs];

							for (li4 = SG_Rune()->first_link[fs]; li4 >= 0;
							     li4 = SG_Rune()->next_link[li4])
							{
								rune_link_t *l4 = &SG_Rune()->links[li4];

								if (home4[l4->to] < bv4)
								{
									bv4 = home4[l4->to];
									best4 = l4->to;
								}
							}
							if (best4 >= 0)
								VectorCopy(SG_Rune()->seeds[best4].origin,
								           aim);
						}
					}
				}
			}
		}

		if (have_aim)
		{
			vec3_t fwd, probe;
			trace_t tr;
			float best_open = -1.0f;
			float try_yaw, base_yaw, chosen_yaw;
			int k;

			VectorSubtract(aim, e->s.origin, want);
			base_yaw = atan2f(want[1], want[0]) * 180.0f / M_PI;
			chosen_yaw = base_yaw;

			/*
			 * Feelers: try the goal heading first, then fan out. Take the
			 * most open heading nearest the goal line. This is what makes
			 * the local gradient walk around a doorframe instead of into
			 * it.
			 */
			/* sg_fandense (wave 294+): the census read a full-speed wall
			 * bump every ~7 seconds, and the geometry that does it lives
			 * in the 30-degree blind wedge between the fan's first rays.
			 * Dense mode adds the 15s; the preference-decay ordering is
			 * preserved (nearest the goal line first). */
			{
			static const float fan_dense[11] = { 0, -15, 15, -30, 30, -60,
			                                     60, -100, 100, -145, 145 };
			static const float fan_base[9]   = { 0, -30, 30, -60, 60, -100,
			                                     100, -145, 145 };
			const float *fan = sg_cv.fandense->value
			                   ? fan_dense : fan_base;
			int fan_n = sg_cv.fandense->value ? 11 : 9;

			for (k = 0; k < fan_n; k++)
			{
				float score;

				float reach = 96.0f;

				/* dose 2: reach scales with speed -- a fixed 96u probe
				 * at 400 u/s is 0.24s of warning, and the census's
				 * full-speed wall bumps live exactly there */
				if (sg_cv.fandense->value >= 2)
				{
					float fsp = sqrtf(e->velocity[0] * e->velocity[0] +
					                  e->velocity[1] * e->velocity[1]);
					reach = 96.0f + (fsp > 274.0f ? (fsp - 274.0f) * 0.5f : 0.0f);
					if (reach > 220.0f) reach = 220.0f;
				}
				try_yaw = (base_yaw + fan[k]) * M_PI / 180.0f;
				fwd[0] = cosf(try_yaw); fwd[1] = sinf(try_yaw); fwd[2] = 0;
				VectorMA(e->s.origin, reach, fwd, probe);
				probe[2] += 8.0f;
				tr = gi.trace(e->s.origin, e->mins, e->maxs, probe,
				              e, MASK_PLAYERSOLID);
				/*
				 * A teammate is not terrain. Blocked by one on the goal
				 * line: remember it (the progress watch must not bill a
				 * friendly body to the link -- at 5v5 that billed 204-278
				 * shelves a match), and bias the walk to a side chosen by
				 * slot parity, so two bots meeting head-on pass on
				 * opposite shoulders instead of mirroring forever.
				 */
				if (k == 0 && tr.fraction < 1.0f && tr.ent &&
				    tr.ent->client && !tr.ent->deadflag &&
				    tr.ent->client->ctf.teamnum == team)
				{
					bot->mate_block_last = true;
					base_yaw += ((int)(e->client - game.clients) & 1)
					            ? 28.0f : -28.0f;
				}
				/*
				 * A closed door is not a wall: walking into it (its
				 * auto-spawned trigger, g_func.c Think_SpawnDoorTrigger,
				 * reaches ~60 units out) is precisely how it opens. The
				 * rune proved these routes with doors held open; a feeler
				 * that deflects off a door steers away from the only
				 * action that makes the route real. Every shelve cluster
				 * in match 7 sat beside a door complex. Doors that only a
				 * button opens will fail to yield and the progress watch
				 * shelves that link -- the net below the honesty.
				 */
				if (tr.fraction < 1.0f && tr.ent && tr.ent->classname &&
				    strncmp(tr.ent->classname, "func_door", 9) == 0)
				{
					int dd;
					qboolean dead = false;

					/* a door that already refused to yield from here is a
					 * wall: no fraction override, the fan walks around */
					for (dd = 0; dd < SG_DEAD_DOORS; dd++)
						if (bot->dead_door[dd] == tr.ent &&
						    SG_TimerPending(bot->dead_door_until[dd]))
							dead = true;
					if (dead && k == 0)
					{
						bot->deaddoor_ahead = true;
						VectorCopy(tr.endpos, bot->deaddoor_spot);
					}
					if (!dead)
					{
						/*
						 * A ROTATING door swings through the space in
						 * front of it; a body pressing at it blocks the
						 * swing and the door reverses shut, forever
						 * (match 8: one bot, 75 shelves, jamming the door
						 * with its own face). Stand outside the arc, or
						 * back out of it, and let the floor trigger swing
						 * it. Sliding doors travel out of the path and
						 * are safe to press.
						 */
						if (k == 0 && strcmp(tr.ent->classname,
						                     "func_door_rotating") == 0)
						{
							door_hold = (tr.fraction * 96.0f < 64.0f) ? 2 : 1;
							door_ent = tr.ent;
						}
						tr.fraction = 1.0f;
					}
				}
				score = tr.fraction * (1.0f - 0.05f * (k > 0) * (k + 1));
				/*
				 * Side latch. A pillar dead ahead leaves -30 and +30 both
				 * open and equal; the winner then alternates as each
				 * sidestep swings the goal bearing, and the body flaps in
				 * place against the obstacle -- seed 327 on lmctf01, the
				 * main valley route, whole matches lost to one pillar
				 * (iter 44-45). Once a detour side is chosen it stays
				 * preferred for 0.7s: enough to clear a pillar, too short
				 * to matter anywhere else. An open goal line clears it.
				 */
				if (bot->fan_side && SG_TimerPending(bot->fan_side_until) &&
				    fan[k] * (float)bot->fan_side < 0.0f)
					score *= 0.6f;
				if (score > best_open)
				{
					best_open = score;
					chosen_yaw = base_yaw + fan[k];
				}
				if (tr.fraction >= 1.0f && k == 0)
				{
					bot->fan_side = 0;  /* goal line open: latch released */
					break;
				}
			}
			}
			if (chosen_yaw != base_yaw)
			{
				int side = (chosen_yaw > base_yaw) ? 1 : -1;

				if (bot->fan_side != side || SG_TimerReady(bot->fan_side_until))
					SG_TimerArm(&bot->fan_side_until, 0.7f);
				bot->fan_side = side;
			}

			/*
			 * THE STEADY HAND (sg_smooth, A/B wave 195+). The human
			 * calibration: 52 degrees median heading change per
			 * travelling second against every bot's 70-73 -- and the
			 * commanded direction churns 81, so the scribble is born
			 * here, where the fan re-picks the walk ten times a
			 * second. A human wrist turns through headings; it does
			 * not teleport between them. The walk heading now slews
			 * at 300 degrees a second -- fast enough for any corner
			 * at 1Hz, too slow to flap -- except in a fight, at a
			 * drop lip, in precision range, or in water, where the
			 * snap IS the skill.
			 */
			if (sg_cv.smooth->value &&
			    !duel && !precision && bot->hook_phase == 0 &&
			    e->waterlevel < 2)
			{
				float sdt = SG_Age(bot->nav_yaw_t);
				float sdy = chosen_yaw - bot->nav_yaw_cur;
				/* the cvar IS the slew rate in deg/s (owner's blend,
				 * wave 321): 1 keeps the legacy 300 */
				float srate = sg_cv.smooth->value;

				if (srate <= 1.0f)
					srate = 300.0f;
				while (sdy > 180.0f) sdy -= 360.0f;
				while (sdy < -180.0f) sdy += 360.0f;
				if (sdt > 0.0f && sdt < 0.5f)
				{
					float cap = srate * sdt;

					if (sdy > cap) sdy = cap;
					else if (sdy < -cap) sdy = -cap;
					chosen_yaw = bot->nav_yaw_cur + sdy;
				}
				bot->nav_yaw_cur = chosen_yaw;
				SG_Mark(&bot->nav_yaw_t);
			}
			else
			{
				bot->nav_yaw_cur = chosen_yaw;
				SG_Mark(&bot->nav_yaw_t);
			}

			/* at a drop lip the proven walk-off heading overrides the fan:
			 * the proof is a line, and the line is the record's */
			if (drop_yaw_locked)
				chosen_yaw = drop_yaw;

			/*
			 * A burst rope aims WHILE RUNNING: the exemption that kept
			 * the speedhook's legs moving also skipped the phase-1 aim
			 * writer, so the view never turned to the anchor, the fire
			 * gate waited for an arrival that could not come, and the
			 * bot stuck in phase 1 forever -- which gates out COMBAT
			 * too. Three of wave 92's five games went totally silent,
			 * every bot wedged on its first burst attempt. The burst
			 * view now steers to the anchor here (yaw now, pitch via
			 * swim-pitch's slot below), and phase 1 carries a hard
			 * deadline as the belt to this suspender.
			 */
			if (bot->hook_phase == 1 && bot->speedhook)
			{
				vec3_t sha;

				VectorSubtract(bot->hook_anchor, e->s.origin, sha);
				chosen_yaw = atan2f(sha[1], sha[0]) * 180.0f / (float)M_PI;
			}

			/*
			 * Rail mode: the retry that trusts the proof over the fan.
			 * Stage 1 walks to the link's from-seed (the proof's start);
			 * stage 2 drives the straight from->to line with the fan
			 * silenced -- pmove slides along the slit's edges exactly as
			 * the phantom's pmove did. Arrival, a better field value, or
			 * the clock ends it; a timeout hands the link to the shelf.
			 */
			if (bot->rail_stage > 0 && bestlink == bot->rail_link &&
			    bestlink >= 0)
			{
				rune_link_t *rl = &SG_Rune()->links[bestlink];
				vec3_t rd;

				if (SG_TimerReadyStrict(bot->rail_until) ||
				    bot->seed == rl->to)
				{
					if (SG_TimerReadyStrict(bot->rail_until) &&
					    bot->seed != rl->to)
					{
						int b2, old2 = 0;

						for (b2 = 0; b2 < SG_BL_MAX; b2++)
							if (bot->bl_until[b2] < bot->bl_until[old2])
								old2 = b2;
						bot->bl_link[old2] = bestlink;
						SG_TimerArm(&bot->bl_until[old2], 45.0f);
						bot->commit_link = -1;
						SG_TeachLinkFutility(bestlink);
						if (sg_cv.debug->value)
							gi.dprintf("RAILFAIL %s link=%d seed=%d\n",
							           e->client->pers.netname,
							           bestlink, bot->seed);
					}
					else if (sg_cv.debug->value)
						gi.dprintf("RAILWIN %s link=%d\n",
						           e->client->pers.netname, bestlink);
					bot->rail_stage = 0;
				}
				else if (bot->rail_stage == 1)
				{
					VectorSubtract(SG_Rune()->seeds[rl->from].origin,
					               e->s.origin, rd);
					rd[2] = 0.0f;
					if (VectorLength(rd) < 24.0f)
					{
						bot->rail_stage = 2;
						SG_TimerArm(&bot->rail_until, 3.0f);
					}
					else
						chosen_yaw = atan2f(rd[1], rd[0])
						             * 180.0f / (float)M_PI;
				}
				if (bot->rail_stage == 2)
				{
					VectorSubtract(SG_Rune()->seeds[rl->to].origin,
					               e->s.origin, rd);
					chosen_yaw = atan2f(rd[1], rd[0])
					             * 180.0f / (float)M_PI;
				}
			}
			else if (bot->rail_stage > 0)
				bot->rail_stage = 0;    /* the surface moved on: stand down */

			/* backing out of a pocket overrides everything but the lip:
			 * the retreat only ends early if the goal line opens up */
			if (SG_TimerPending(bot->escape_until) && !drop_yaw_locked)
			{
				if (best_open >= 1.0f && chosen_yaw == base_yaw)
					bot->escape_until = 0.0f;
				else
					chosen_yaw = bot->escape_yaw;
			}

			/*
			 * THE AIM FRAME OWNS THE VIEW. Phase 1 wrote the anchor
			 * bearing above; this write, running after it, was flattening
			 * every rope's pitch to zero and pointing it down the goal
			 * line -- 1519 of 1533 bad bites flew off the aim line
			 * (iteration 23), every under-climb since the first match
			 * traces here. The aim frame is a standing frame, exactly
			 * the posture the proofs fired from.
			 */
			if (bot->hook_phase == 1 && !bot->speedhook)
			{
				cmd->forwardmove = 0;
				cmd->sidemove = 0;
				cmd->upmove = 0;
				have_move = false;
			}
			else
			{
			float swim_pitch = 0.0f;

			/*
			 * PM_WaterMove runs along the FULL view vector: with the
			 * pitch flattened to zero a swimming body can only paddle
			 * horizontally, and every swim link whose destination is
			 * above or below is physically unexecutable -- lmctf01
			 * carries 65k swim links and iter 41 shows zero ever taken,
			 * the attack fields plateauing at the water. Underwater the
			 * pitch belongs to the line to the target.
			 */
			if (e->waterlevel > 1 && have_aim)
			{
				vec3_t wd;
				float wh;

				VectorSubtract(aim, e->s.origin, wd);
				wh = sqrtf(wd[0] * wd[0] + wd[1] * wd[1]);
				swim_pitch = -atan2f(wd[2], wh) * 180.0f / (float)M_PI;
				if (swim_pitch > 85.0f) swim_pitch = 85.0f;
				if (swim_pitch < -85.0f) swim_pitch = -85.0f;
			}
			else if (bot->hook_phase == 1 && bot->speedhook)
			{
				vec3_t wd;
				float wh;

				VectorSubtract(bot->hook_anchor, e->s.origin, wd);
				wd[2] -= e->viewheight;
				wh = sqrtf(wd[0] * wd[0] + wd[1] * wd[1]);
				swim_pitch = -atan2f(wd[2], wh) * 180.0f / (float)M_PI;
				if (swim_pitch > 85.0f) swim_pitch = 85.0f;
				if (swim_pitch < -85.0f) swim_pitch = -85.0f;
			}

			cmd->angles[YAW] = ANGLE2SHORT(chosen_yaw)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd->angles[PITCH] = ANGLE2SHORT(swim_pitch)
			                  - e->client->ps.pmove.delta_angles[PITCH];
			cmd->forwardmove = 400;
			if (jump_now)
				cmd->upmove = 400;

			view_yaw = chosen_yaw;
			view_pitch = swim_pitch;
			bot->nav_drove = true;
			}
			move_dir[0] = cosf(chosen_yaw * (float)M_PI / 180.0f);
			move_dir[1] = sinf(chosen_yaw * (float)M_PI / 180.0f);
			move_dir[2] = 0.0f;
			have_move = true;

			/*
			 * Room to hop into. A landing jump commits the bot to whatever
			 * speed and heading it left with for the whole arc, so it is only
			 * worth taking where the way ahead is actually clear -- the same
			 * player-box trace the feelers use, run further out along the
			 * heading that was chosen.
			 */
			/*
			 * Carriers get a shorter clearance bar when sg_carryhop is
			 * set (the trial value IS the distance): escape corridors
			 * rarely offer 160 clear units, so the fleeing carrier --
			 * the one role whose touch-loss decides games -- was the
			 * role hopping least (wave 270 census: 38%% airtime vs the
			 * attackers' 51%%).
			 */
			{
				float hop_reach = 160.0f;
				if (sg_cur_role == SG_ROLE_CARRY &&
				    sg_cv.carryhop->value > 0)
					hop_reach = sg_cv.carryhop->value;
				VectorMA(e->s.origin, hop_reach, move_dir, probe);
			}
			probe[2] += 8.0f;
			tr = gi.trace(e->s.origin, e->mins, e->maxs, probe,
			              e, MASK_PLAYERSOLID);
			/* same rule as the feelers: a door ahead is not a wall, but
			 * do NOT hop at one -- arrive on foot, inside its trigger */
			open_ahead = (tr.fraction >= 1.0f);

			VectorSubtract(e->s.origin, bot->last_origin, d);
			if (VectorLength(d) < 4.0f)
			{
				bot->stuck_time += 0.1f;
				if (bot->stuck_time > 1.0f && e->groundentity)
					cmd->upmove = 400;   /* hop what the feelers missed */
			}
			else
				bot->stuck_time = 0.0f;
		}

		/* braking for a rope: kill the run so the fire happens from the
		 * standing start the proof used */
		if (hook_brake)
		{
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			bot->nav_drove = false;
		}

		/* and braking OUT of a rope: hold the landing until the body is
		 * standing where the phantom stood, then argue the next step --
		 * except a carrier under legcarrier dose 2: the pace ledger
		 * (218-219: legs +59%% speed) says the flag pays for stillness
		 * with blood, and a landing run out beats a landing stood */
		if (SG_TimerPending(bot->hook_landbrake) && e->groundentity &&
		    !(sg_cur_role == SG_ROLE_CARRY &&
		      sg_cv.legcarrier->value >= 2.0f))
		{
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			bot->nav_drove = false;
		}

		/* rotating door working its arc: hold ground (or yield the arc),
		 * keep facing it, and let the trigger under our feet do the work.
		 * A door still shut after 2.5 seconds is not going to open from
		 * this side (no trigger reaches here): remember it as a wall for
		 * thirty seconds and let the surface reroute. */
		if (door_hold && have_move)
		{
			cmd->forwardmove = (door_hold == 2) ? -200 : 0;
			cmd->upmove = 0;
			bot->door_held_last = true;
			bot->nav_drove = false;

			if (door_ent != bot->door_hold_ent)
			{
				bot->door_hold_ent = door_ent;
				SG_Mark(&bot->door_hold_since);
			}
			else if (SG_AgeOver(bot->door_hold_since, 2.5f))
			{
				int dd, oldest = 0;

				for (dd = 0; dd < SG_DEAD_DOORS; dd++)
					if (bot->dead_door_until[dd] < bot->dead_door_until[oldest])
						oldest = dd;
				bot->dead_door[oldest] = door_ent;
				SG_TimerArm(&bot->dead_door_until[oldest], 30.0f);
				bot->door_hold_ent = NULL;
				/* a door with no trigger on this side is one-way by the
				 * mapper's hand (lmctf03: both bd doors trigger only from
				 * the base side). The 30s memory reroutes THIS bot; the
				 * field funnels the rest of the team in behind it unless
				 * the corridor repricies globally. Same cure as the wall. */
				SG_TeachFutility(bot->seed);
				if (sg_cv.debug->value)
					gi.dprintf("DEADDOOR %s at (%.0f %.0f %.0f)\n",
					           e->client->pers.netname, e->s.origin[0],
					           e->s.origin[1], e->s.origin[2]);
			}
		}
		else
		{
			bot->door_hold_ent = NULL;
			bot->door_held_last = false;
		}

		/*
		 * BREATH OUTRANKS EVERYTHING. Twelve seconds of air is what the
		 * game gives; the lmctf01 moat tunnel costs ten at pace and any
		 * stall drowns the swimmer -- wave 55's first-ever carrier on
		 * that map 'sank like a rock' mid-return, and the census says
		 * drowning, not defense, is what kills conversions there. Four
		 * seconds from the gurgle, the route stops mattering: pitch up,
		 * kick for the surface, breathe, and let the field resume from
		 * wherever the gasp happened. The rope is the one thing faster
		 * than swimming, so a live pull is left alone.
		 */
		if (e->waterlevel >= 3 && bot->hook_phase != 2 &&
		    SG_TimerRemaining(e->air_finished) <
		        ((role == SG_ROLE_CARRY) ? 8.0f : 4.0f))
		{
			int an = (sg_airnext && bot->seed >= 0)
			         ? sg_airnext[bot->seed] : -1;

			if (an >= 0)
			{
				/* swim the graph's way out, not the ceiling's */
				vec3_t ad;
				float ay, ap, al;

				VectorSubtract(SG_Rune()->seeds[an].origin, e->s.origin,
				               ad);
				al = VectorLength(ad);
				ay = atan2f(ad[1], ad[0]) * 180.0f / (float)M_PI;
				ap = (al > 1.0f)
				     ? -asinf(ad[2] / al) * 180.0f / (float)M_PI : -85.0f;
				cmd->angles[YAW] = ANGLE2SHORT(ay)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd->angles[PITCH] = ANGLE2SHORT(ap)
				                  - e->client->ps.pmove.delta_angles[PITCH];
				view_pitch = ap;
			}
			else
			{
				cmd->angles[PITCH] = ANGLE2SHORT(-85.0f)
				                  - e->client->ps.pmove.delta_angles[PITCH];
				view_pitch = -85.0f;
			}
			cmd->forwardmove = 400;
			cmd->upmove = 400;
			bot->nav_drove = false;     /* not the route's fault */
		}

		/*
		 * WAITING OUT A RELOAD (sg_railrhythm). The wait happens where
		 * the body already is, because the decision above only arms it
		 * from a spot the railer cannot see -- the last cover point is
		 * the one being stood on. No walk to it, no facing change: the
		 * eyes stay on the route and combat, which never stops, keeps
		 * whatever target it was holding.
		 *
		 * THE TERMINAL BRAKE OUTRANKS IT, literally: bot->term_brake is
		 * set earlier in this same stage and a value under 1.0 means the
		 * body is throttling into a link that has to be entered at a
		 * measured speed. A hold dropped on top of that would strand the
		 * bot mid-corner. The brake wins and the crossing goes ahead.
		 */
		if (rail_hold && have_move && bot->term_brake >= 1.0f)
		{
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			bot->nav_drove = false;     /* the wait is not the route */
			bot->stuck_time = 0.0f;     /* nor is it being stuck */
		}

		/* rallying: get to cover first, stand there, face the push */
		if (rally_hold && have_move)
		{
			vec3_t cvd;
			qboolean at_cover = true;

			if (bot->rally_cover >= 0)
			{
				VectorSubtract(SG_Rune()->seeds[bot->rally_cover].origin,
				               e->s.origin, cvd);
				cvd[2] = 0.0f;
				at_cover = (VectorLength(cvd) < 48.0f);
			}
			if (at_cover)
			{
				cmd->forwardmove = 0;
				cmd->sidemove = 0;
				cmd->upmove = 0;
				bot->nav_drove = false;
				bot->stuck_time = 0.0f;
			}
			else
			{
				float cy = atan2f(cvd[1], cvd[0]) * 180.0f / (float)M_PI;

				cmd->angles[YAW] = ANGLE2SHORT(cy)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd->forwardmove = 400;
				view_yaw = cy;
				bot->nav_drove = false;     /* the wait is not the route */
			}
		}

		/* on post: whatever the descent wanted, guard duty overrides it */
		if (hold_post)
		{
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			bot->nav_drove = false;
			cmd->angles[YAW] = ANGLE2SHORT(post_yaw)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd->angles[PITCH] = -e->client->ps.pmove.delta_angles[PITCH];
			view_yaw = post_yaw;
			view_pitch = 0.0f;
			have_move = false;
			bot->stuck_time = 0.0f;
		}

	VectorCopy(move_dir, tc->move_dir);
	tc->view_yaw = view_yaw;
	tc->view_pitch = view_pitch;
	tc->have_move = have_move;
	tc->open_ahead = open_ahead;
	tc->run_link = run_link;
	tc->door_hold = door_hold;
	tc->door_ent = door_ent;
	tc->drop_yaw_locked = drop_yaw_locked;
	tc->drop_yaw = drop_yaw;
	tc->hook_brake = hook_brake;
}

/*
 * THE COMMAND (split from SG_BotThink, 2026-08-11 standards pass; body
 * verbatim): the movement policy becomes a usercmd -- sub-stepping, the
 * view slew, the weave, the duel hold, the plan beam and telemetry --
 * and the frame ends in ClientThink.
 */
void Think_Emit(sg_bot_t *bot, sg_think_t *tc, usercmd_t *cmd)
{
	/* the former parameter list, unpacked from the think context so the
	 * body below reads exactly as it did when these arrived as arguments;
	 * cmd stays a real parameter until the whole frame speaks context.
	 * Seventeen former parameters -- half the interface -- were never
	 * read by this body and have no unpack. */
	edict_t *e = tc->e;
	sg_role_t role = tc->role;
	int team = tc->team;
	const int *goal_field = tc->goal_field;
	const int *route_field = tc->route_field;
	int bestlink = tc->bestlink;
	qboolean precision = tc->precision;
	qboolean duel = tc->duel;
	vec_t *move_dir = tc->move_dir;
	float view_yaw = tc->view_yaw;
	float view_pitch = tc->view_pitch;
	qboolean have_move = tc->have_move;
	qboolean open_ahead = tc->open_ahead;
	qboolean run_link = tc->run_link;
	int door_hold = tc->door_hold;
	qboolean drop_yaw_locked = tc->drop_yaw_locked;

	vec3_t basis_fwd, basis_right;
	int sub_steps = 1, sub_msec = 0;
	float slew_want_y = 0.0f, slew_want_p = 0.0f, slew_rate = 0.0f;
	qboolean duel_hold = false;
	short weave_side = 0;
	vec3_t d;


	/*
	 * The basis the engine will actually use, not a convenient one.
	 *
	 * Pmove builds it before every land move (pmove.c, PM_AirMove; quoted at
	 * bl_main.c:347-370): the view angles with PITCH divided by three, and
	 * then wishvel[i] = forward[i]*fmove + right[i]*smove for i in {0,1}. So
	 * forward's horizontal length is scaled by cos(pitch/3) while right stays
	 * fully horizontal. Solving against a pitch-zero basis makes the engine
	 * reconstruct a different direction than the one asked for -- shorter, and
	 * skewed toward the strafe axis, which lowers wishspeed and with it the
	 * acceleration. Here the navigation view is pitch zero, so the division
	 * changes nothing today; it is written the engine's way so that it stays
	 * correct the moment the body pitches.
	 */
	{
		vec3_t basis;

		basis[PITCH] = view_pitch;
		if (basis[PITCH] > 180.0f)
			basis[PITCH] -= 360.0f;
		basis[PITCH] /= 3.0f;
		basis[YAW] = view_yaw;
		basis[ROLL] = 0.0f;
		AngleVectors(basis, basis_fwd, basis_right, NULL);
	}

	/*
	 * The weave, decided here and applied per step below.
	 *
	 * A bot whose route has run out -- no improving link, or a destination it
	 * is already standing on top of -- has nothing left to spend its movement
	 * on, and standing still in a firefight is the one thing that is certainly
	 * wrong. So it oscillates sideways instead. The direction needs no work:
	 * combat has already put the view on the target, and pmove's own basis
	 * makes `right` exactly perpendicular to that view in the horizontal plane
	 * -- with roll zero, right = (sin yaw, -cos yaw, 0) regardless of pitch
	 * (AngleVectors, q_shared.c). sidemove alone is therefore across the enemy
	 * line by construction, and forwardmove is dropped so the weave adds no
	 * drift along it.
	 *
	 * The period is per bot, not per squad: four bots weaving on one clock is
	 * one wide target. Two-thirds of a rocket's flight time at close range,
	 * spread across ten phases by client number.
	 *
	 * Never for the carrier (2.4-D2 is a route, not a fight), never with a
	 * rope out (the hook SETS velocity -- an off-axis input accumulates into
	 * nothing), and never on the final approach, where the whole point is
	 * being able to stop on the flag.
	 */
	/* sg_noweave (A/B wave 176+): the Brownian census -- median 73
	 * degrees of heading change per travelling second, 32%% outright
	 * reversals -- with goals, seeds, and links all measured stable.
	 * The lateral oscillators are the remaining suspects, and they
	 * trigger on BELIEF-engagement, which at parity is nearly always.
	 * Dodging nobody's bullets is the owner's "unnecessary movement"
	 * by definition; this gate measures what the dodge layers cost. */
	if (duel && role != SG_ROLE_CARRY && !precision &&
	    bot->hook_phase == 0 &&
	    !sg_cv.noweave->value)
	{
		if (bestlink < 0)
			duel_hold = true;
		else
		{
			VectorSubtract(SG_Rune()->seeds[SG_Rune()->links[bestlink].to].origin,
			               e->s.origin, d);
			duel_hold = (VectorLength(d) < SG_WEAVE_HOLD);
		}

		if (duel_hold)
		{
			float period = SG_WEAVE_BASE + SG_WEAVE_STEP *
			               (float)((int)(e->client - game.clients) % 10);

			weave_side = (fmodf(level.time, period) < period * 0.5f)
			             ? SG_WEAVE_SIDE : -SG_WEAVE_SIDE;
		}
	}

	/*
	 * Execute the frame in physics steps, and decide the movement again on
	 * every one of them.
	 *
	 * The angle that keeps PM_Accelerate paying depends on the current
	 * velocity, and the velocity is exactly what the previous step just
	 * changed: a command computed at 300 is the wrong command by the time the
	 * bot is doing 500. The landing jump wants the very step the bot touches
	 * down, and a bot that only gets one chance per tenth of a second spends
	 * far longer on the floor than a player whose client sends a dozen
	 * commands in the same window. A real client does this continuously; this
	 * is the bot catching up to that, not overtaking it.
	 *
	 * The clock is not touched. msec is the frame's real time, and the steps
	 * are that integer split with the remainder spread over the first few, so
	 * they sum to exactly what passed -- eight steps of 13ms for a 100ms frame
	 * would be 104ms of simulation for 100ms of play, which is free speed
	 * rather than finer movement. Finer decisions, never a longer clock.
	 */

	/*
	 * THE BREATHER (sg_breather, wave 388 trial). The movement-texture
	 * judge, 4/4 blind on mactf06: "constant run plateau with
	 * instantaneous needle spikes, zero acceleration ramping, no
	 * burst/rest cadence, texture statistically identical from t=0 to
	 * t=850." A human's throttle is not a switch: they ease off checking
	 * corners, after fights, waiting on timers. This eases off ONLY on
	 * safe legs -- never carrying, never on the rope, never engaged, feet
	 * on the floor -- for 0.5-1.8s at a time, on average once per
	 * cvar-seconds of safe travel. Danger cancels it instantly; the cap
	 * on cost is a fraction of a second of arrival time per leg.
	 */
	{
		float dose = sg_cv.breather->value;

		if (dose > 0.0f && role != SG_ROLE_CARRY &&
		    bot->hook_phase == 0 && !bot->engaged_last &&
		    e->groundentity != NULL)
		{
			if (SG_TimerReady(bot->breather_next))
			{
				SG_TimerArm(&bot->breather_next,
				    dose * (0.5f + (float)(rand() % 100) / 100.0f));
				SG_TimerArm(&bot->breather_until,
				    0.5f + (float)(rand() % 130) / 100.0f);
			}
			if (SG_TimerPending(bot->breather_until))
			{
				cmd->forwardmove = (short)(cmd->forwardmove * 0.35f);
				cmd->sidemove = (short)(cmd->sidemove * 0.35f);
			}
		}
		else
			bot->breather_until = 0.0f;     /* danger ends the stroll */
	}

	{
		int		total = cmd->msec;
		int		sub = (int)sg_cv.subframes->value;
		int		base, rem, step;
		short	plain_forward = cmd->forwardmove;
		short	nav_jump = cmd->upmove;
		/* combat's own answer to "is there a fight on RIGHT NOW", as opposed
		 * to the up-to-two-seconds-old belief the surface terms were priced
		 * from. The weave below needs the live one. */
		qboolean engaged = false;
		/* sg_airstrafe, decided once for the frame and spent per sub-step */
		qboolean as_ok = false;         /* the chain is live this frame */
		qboolean as_chain = false;      /* dose 2: hop chaining as well */
		float    as_lean = 0.0f;        /* the sinusoid, -1..1 */

		if (sub < 1)
			sub = 1;
		if (sub > total)
			sub = total;            /* a step cannot be shorter than 1ms */
		base = total / sub;
		rem = total % sub;
		sub_steps = sub;

		/*
		 * Combat rides the frame's command: view and trigger only, movement
		 * untouched. It writes the base cmd once and every subframe inherits
		 * it. Ordering rule from sg_combat.h: at hook_phase 1 the cmd angles
		 * ARE the anchor bearing (the rope fires along v_angle), so combat
		 * must not steal the view that frame.
		 *
		 * Phase 2 -- rope out, being pulled -- used to be gated out too, on the
		 * assumption that shooting and grappling could not share the attack
		 * button. That is true only when the grapple is pers.weapon
		 * (g_cmds.c:1405-1412), which SG_CombatFrame now guarantees never
		 * happens (WEAPONS.md rule S3). An OFFHAND rope is sustained by
		 * ClientEndServerFrame with no button and no view input
		 * (p_view.c:988-990), and while it pulls it SETS velocity outright
		 * (p_weapon.c:2071-2102) -- so the view costs the movement nothing on
		 * those frames and the trigger is free. That is WEAPONS.md 2.4-D2's
		 * flee doctrine: a carrier that grapples and shoots at the same time.
		 *
		 * Phase 3 stays gated out for the opposite reason: the rope is gone,
		 * the bot is flying its own landing on forwardmove down the chosen yaw
		 * (above), and a view stolen there is a landing missed.
		 */
		if (bot->hook_phase != 1 && bot->hook_phase != 3 &&
		    bot->rj_phase == 0 && bot->nade_phase == 0)
			SG_CombatFrame(e, cmd, &engaged);

		/*
		 * The bomb sequence owns weapon, view, and trigger while it
		 * runs: switch (0.5s), cook with the button held (1.3s, view
		 * arced 25 degrees over the target's bearing), release -- the
		 * grenade code throws on release. Combat resumes next frame
		 * and the ladder takes the weapon back.
		 */
		if (bot->nade_phase == 1)
		{
			/* cook only once the grenade is VERIFIABLY in hand -- the
			 * switch runs through down/up animations, and holding the
			 * trigger early fires whatever is still equipped (wave 127:
			 * zero grenades thrown, the cook squeezed the rail) */
			if (e->client->pers.weapon &&
			    e->client->pers.weapon->pickup_name &&
			    !Q_stricmp(e->client->pers.weapon->pickup_name,
			               "Grenades"))
			{
				bot->nade_phase = 2;
				/* the engine's in-hand deadline: cook_start + TIMER +
				 * 0.2. The release moment is computed against FLIGHT
				 * TIME each aim frame below -- the fixed 2.2s cook
				 * left ~1s of fuse to spend BOUNCING off the impact
				 * point at 667 u/s, which is exactly the one-room-off
				 * miss NADEPOP measured (medians 434 and 717, radius
				 * 165, waves 140/142) */
				SG_TimerArm(&bot->nade_until, 3.2f);
				if (sg_cv.debug->value)
					gi.dprintf("NADE %s cooking\n",
					           e->client->pers.netname);
			}
			else if (SG_TimerReady(bot->nade_until + 1.2f))
			{
				bot->nade_phase = 0;    /* switch never landed */
				SG_TimerArm(&bot->nade_next, 4.0f);
			}
		}
		if (bot->nade_phase == 2)
		{
			/*
			 * THE BOMB LEADS THE LANDING (sg_nadelead, wave 309+). The
			 * owner's doctrine already won this argument for rockets
			 * (landlead, adopted 249): an airborne enemy is on a
			 * committed arc. The cooked grenade is the same shot with
			 * a better fuse -- while cooking, if the live enemy is in
			 * the air, walk its parabola to the touchdown and put the
			 * bomb THERE instead of at the danger book's historic
			 * post. Ground targets keep the book (a runner outlives
			 * the fuse; the eight nulled mechanisms all chased them).
			 */
			if (sg_cv.nadelead->value)
			{
				edict_t *len9 = SG_CombatLiveEnemy(e);

				if (len9 && !len9->groundentity)
				{
					vec3_t lp0, lp1;
					trace_t lltr;
					float ltt, lgrav = 800.0f;
					int lseg;

					VectorCopy(len9->s.origin, lp0);
					for (lseg = 1; lseg <= 30; lseg++)
					{
						ltt = 0.05f * (float)lseg;
						lp1[0] = len9->s.origin[0] + len9->velocity[0] * ltt;
						lp1[1] = len9->s.origin[1] + len9->velocity[1] * ltt;
						lp1[2] = len9->s.origin[2] + len9->velocity[2] * ltt
						       - 0.5f * lgrav * ltt * ltt;
						lltr = gi.trace(lp0, len9->mins, len9->maxs, lp1,
						                len9, MASK_PLAYERSOLID);
						if (lltr.fraction < 1.0f)
						{
							VectorCopy(lltr.endpos, bot->nade_at);
							bot->nade_at[2] += 24.0f;
							break;
						}
						VectorCopy(lp1, lp0);
					}
				}
			}

			/*
			 * The bomb aims like the rope: solve the projectile. Throw
			 * speed scales with cook (400 to 800 across the 3s timer;
			 * our 1.3s cook gives ~575), gravity is the server's, and
			 * the launch pitch to land ON the post is closed-form -- the
			 * same equation the ballistic anchor uses. The flat -25
			 * guess sailed 24 of 24 throws over the room (wave 128,
			 * zero kills, duel ratio unmoved).
			 */
			vec3_t na;
			float nyaw, npitch, nh;
			/* the engine's clock: timer remaining if released this frame
			 * is nade_until - now (p_weapon.c: grenade_time = cook_start
			 * + TIMER + 0.2, and nade_until holds exactly that sum) */
			float ntmr = SG_TimerRemaining(bot->nade_until);
			float nheld = 3.0f - (ntmr < 0.0f ? 0.0f
			                     : (ntmr > 3.0f ? 3.0f : ntmr));
			float nsp = 400.0f + nheld * ((800.0f - 400.0f) / 3.0f);
			float ng = e->client->ps.pmove.gravity
			           ? (float)e->client->ps.pmove.gravity : 800.0f;
			float ns2 = nsp * nsp, ndisc, nfly = -1.0f;

			VectorSubtract(bot->nade_at, e->s.origin, na);
			nh = sqrtf(na[0] * na[0] + na[1] * na[1]);
			nyaw = atan2f(na[1], na[0]) * 180.0f / (float)M_PI;
			ndisc = ns2 * ns2 - ng * (ng * nh * nh + 2.0f * na[2] * ns2);
			if (ndisc > 0.0f && nh > 32.0f)
			{
				float ntan = (ns2 - sqrtf(ndisc)) / (ng * nh);

				npitch = -atanf(ntan) * 180.0f / (float)M_PI;
				/* flight time from the same closed form: horizontal
				 * range over horizontal speed */
				nfly = nh * sqrtf(1.0f + ntan * ntan) / nsp;

				/*
				 * THE ARC CHECK (wave 237). Three timing doses moved
				 * nothing because most throws never get their
				 * parabola -- from the band, the arc to the stand
				 * runs through corridor walls, and the grenade
				 * bounces to a random floor pop (air%% pinned at 30,
				 * median 556). Sample the intended arc; a blocked
				 * flight is an attempt abandoned at zero cost, per
				 * the owner's economy. Volume falls to what is real.
				 */
				{
					vec3_t ap, lp;
					trace_t atr;
					float tstep = nfly / 6.0f, tt2;
					int seg;
					float cy = cosf(nyaw * (float)M_PI / 180.0f);
					float sy = sinf(nyaw * (float)M_PI / 180.0f);
					float hv2 = nsp / sqrtf(1.0f + ntan * ntan);
					float vv2 = hv2 * ntan;

					VectorCopy(e->s.origin, lp);
					lp[2] += e->viewheight;
					for (seg = 1; seg <= 6; seg++)
					{
						tt2 = tstep * (float)seg;
						ap[0] = e->s.origin[0] + cy * hv2 * tt2;
						ap[1] = e->s.origin[1] + sy * hv2 * tt2;
						ap[2] = e->s.origin[2] + e->viewheight
						      + vv2 * tt2 - 0.5f * ng * tt2 * tt2;
						atr = gi.trace(lp, NULL, NULL, ap, e,
						               MASK_SOLID);
						if (atr.fraction < 1.0f)
						{
							nfly = -2.0f;   /* blocked: no throw */
							break;
						}
						VectorCopy(ap, lp);
					}
				}
				if (nfly < -1.5f)
				{
					bot->nade_phase = 0;    /* abandon, cost-free */
					SG_TimerArm(&bot->nade_next, 4.0f);
					cmd->buttons &= ~BUTTON_ATTACK;
				}
			}
			else
				npitch = -atan2f(na[2], nh) * 180.0f / (float)M_PI
				         - 30.0f;
			/*
			 * THE SILENT COOK (wave 233). The zero-cost audit caught
			 * the veer: this block owned the view for the whole cook
			 * and the legs followed it off the route -- a 33 percent
			 * steal tax on the trial arm. The owner's cook touches
			 * nothing: the view belongs to navigation until the
			 * release frame, when the aim exists for exactly one
			 * command -- the flick. Attack stays held throughout;
			 * cooking needs the trigger, not the eyes.
			 */
			if (!(sg_cv.flycook->value) ||
			    (nfly >= 0.0f && ntmr - 0.2f <= nfly + 0.15f) ||
			    ntmr <= 0.75f)
			{
				cmd->angles[YAW] = ANGLE2SHORT(nyaw)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd->angles[PITCH] = ANGLE2SHORT(npitch)
				                  - e->client->ps.pmove.delta_angles[PITCH];
			}
			/*
			 * THE TRUE AIRBURST. Release when the fuse remaining equals
			 * the flight ahead, so the pop happens ON ARRIVAL -- the
			 * fixed cook landed the bomb with a second to spend
			 * bouncing away from the aim point, and NADEPOP measured
			 * the result: medians 434 and 717 against a 165 radius.
			 * A degenerate solve means the throw is TOO SLOW SO FAR --
			 * speed builds with the cook (wave 143: releasing on
			 * degenerate threw every bomb at 400 u/s on frame one,
			 * fuse=3.0, two pops all wave) -- so keep cooking until
			 * the closed form comes back. The 0.6s floor keeps the POP
			 * outside our own splash radius (0.4s of fuse is ~250
			 * units of clearance) and throws whatever the solve says.
			 */
			/* wave 236: pop EARLY -- hold the cook until remaining
			 * fuse undercuts the flight by 0.15s, so the grenade
			 * expires airborne short of the floor it would have
			 * bounced off (235: aim height alone left 69 percent of
			 * pops grounded -- bounces and clipped arcs land before
			 * the fuse, and a landed grenade is an announcement) */
			if (ntmr > 0.6f &&
			    (nfly < 0.0f || ntmr - 0.2f > nfly - 0.15f))
				cmd->buttons |= BUTTON_ATTACK;
			else
			{
				cmd->buttons &= ~BUTTON_ATTACK;   /* the release throws */
				bot->nade_phase = 0;
				SG_TimerArm(&bot->nade_next, 8.0f);
				if (sg_cv.debug->value)
					gi.dprintf("NADE %s thrown fly=%.2f fuse=%.2f\n",
					           e->client->pers.netname,
					           nfly, ntmr - 0.2f);
			}
		}
		/*
		 * SOUND-DIRECTED FIRE (sg_soundfire, wave 244). The owner:
		 * "I can shoot rockets in the direction of sounds while
		 * travelling a route." The ear places heard_only beliefs that
		 * combat refuses to aim at -- this aims at them on purpose,
		 * once per cadence, launcher already in hand, one flick
		 * frame, zero route seconds. Free speculation; the splash
		 * does the rest or nothing does.
		 */
		if (sg_cv.soundfire->value &&
		    !duel && !engaged && role != SG_ROLE_CARRY &&
		    bot->nade_phase == 0 && bot->hook_phase == 0 &&
		    SG_TimerReady(bot->soundfire_next) &&
		    e->client->pers.weapon &&
		    e->client->pers.weapon->pickup_name &&
		    !Q_stricmp(e->client->pers.weapon->pickup_name,
		               "Rocket Launcher"))
		{
			int s15;

			for (s15 = 0; s15 < SG_MAX_ENEMY_TRACK; s15++)
			{
				sg_belief_enemy_t *en15 =
				    &sg_caco_enemies[SG_TeamIdx(team)][s15];
				vec3_t sd15;
				float sl15;

				if (en15->client < 0 || en15->seed < 0 ||
				    !en15->heard_only ||
				    SG_AgeAtLeast(en15->seen_time, 2.0f))
					continue;
				VectorSubtract(SG_Rune()->seeds[en15->seed].origin,
				               e->s.origin, sd15);
				sl15 = VectorLength(sd15);
				if (sl15 < 600.0f || sl15 > 1500.0f)
					continue;   /* too close = own splash; too
					             * far = pure noise */
				/* wave 245: never rocket a ghost a teammate is
				 * standing on -- the one premium this free trick
				 * could quietly charge is friendly splash */
				{
					int bi15, mate15 = 0;

					for (bi15 = 0; bi15 < SG_MAXBOTS; bi15++)
					{
						sg_bot_t *mb15 = &sg_bots[bi15];
						vec3_t md15;

						if (!mb15->active || mb15 == bot ||
						    !mb15->ent || !mb15->ent->inuse)
							continue;
						if (mb15->ent->client->ctf.teamnum != team)
							continue;
						VectorSubtract(mb15->ent->s.origin,
						    SG_Rune()->seeds[en15->seed].origin, md15);
						if (VectorLength(md15) < 250.0f)
						{
							mate15 = 1;
							break;
						}
					}
					if (mate15)
						continue;
				}
				{
					float sy15 = atan2f(sd15[1], sd15[0])
					             * 180.0f / (float)M_PI;
					float sp15 = -atan2f(sd15[2],
					    sqrtf(sd15[0]*sd15[0] + sd15[1]*sd15[1]))
					             * 180.0f / (float)M_PI;

					cmd->angles[YAW] = ANGLE2SHORT(sy15)
					    - e->client->ps.pmove.delta_angles[YAW];
					cmd->angles[PITCH] = ANGLE2SHORT(sp15)
					    - e->client->ps.pmove.delta_angles[PITCH];
					cmd->buttons |= BUTTON_ATTACK;
					SG_TimerArm(&bot->soundfire_next, 8.0f);
					if (sg_cv.debug->value)
						gi.dprintf("SNDFIRE %s rng=%.0f\n",
						           e->client->pers.netname, sl15);
				}
				break;
			}
		}

		bot->engaged_last = engaged;

		/*
		 * Combat re-aimed: rebuild the movement basis from the view pmove
		 * will ACTUALLY use. Solving the strafe against the navigation
		 * basis while flying the combat view made the engine reconstruct
		 * a different direction than the one asked for -- the bot ran
		 * down its AIM instead of its route on every engaged frame (the
		 * duel implementation's flagged coupling, now closed).
		 */
		if (engaged)
		{
			vec3_t basis;

			basis[YAW] = SHORT2ANGLE((short)(cmd->angles[YAW] +
			             e->client->ps.pmove.delta_angles[YAW]));
			basis[PITCH] = SHORT2ANGLE((short)(cmd->angles[PITCH] +
			               e->client->ps.pmove.delta_angles[PITCH]));
			if (basis[PITCH] > 180.0f)
				basis[PITCH] -= 360.0f;
			basis[PITCH] /= 3.0f;
			basis[ROLL] = 0.0f;
			AngleVectors(basis, basis_fwd, basis_right, NULL);
		}

		/*
		 * THE SPAWN BEAT'S EYES. The last writer before the slew, so it
		 * is looking around rather than arguing with navigation about
		 * where to look; the slew below then carries the sweep at
		 * sg_turnrate like any other ask, which is why this asks for an
		 * ANGLE and not a rate.
		 *
		 * The sweep is centred on the heading the frame already wanted --
		 * the route's, this early -- so the beat ends looking down the
		 * road it is about to run instead of snapping back onto it. One
		 * full sine over the window is centre, one shoulder, through
		 * centre, the other shoulder, centre: what a player's mouse does
		 * in the half second after the screen comes back.
		 *
		 * Danger ends it on the frame danger arrives. `engaged` is
		 * combat's live answer, not last frame's, and the damage ring
		 * catches the shot that came from somewhere the eye had not got
		 * to yet -- which, spawning, is most of the map.
		 */
		if (SG_TimerPending(bot->beat_until))
		{
			/* bot->engaged_last is this same value by here -- it was
			 * assigned from `engaged` a few lines up, so the live read
			 * is the one worth making */
			if (engaged || Beat_HurtSince(e, bot->beat_from))
				bot->beat_until = 0.0f;
			else
			{
				float span = bot->beat_until - bot->beat_from;
				float t = (span > 0.001f)
				          ? SG_Age(bot->beat_from) / span : 1.0f;
				float yaw = SHORT2ANGLE((short)(cmd->angles[YAW] +
				            e->client->ps.pmove.delta_angles[YAW]));

				yaw += (float)bot->beat_sign * bot->beat_arc *
				       sinf(t * 2.0f * (float)M_PI);
				cmd->angles[YAW] = ANGLE2SHORT(yaw)
				                - e->client->ps.pmove.delta_angles[YAW];
			}
		}

		/*
		 * THE AIR-STRAFE CHAIN, armed once a frame and spent per sub-step
		 * (sg_airstrafe: 0 off, 1 the lean, 2 the lean and the hops).
		 *
		 * The angle is the engine's and is derived at SG_AirStrafeCmd; what
		 * is decided here is whether the body is allowed to fly it, and
		 * which way the swing is leaning at this instant.
		 *
		 * THE SWING. sin() over SG_AS_PERIOD, plus the standing heading
		 * error over SG_AS_CORR. The bias is the whole of the anti-drift:
		 * the lean is measured off the direction of TRAVEL, so a swing held
		 * one way walks the body off its road, and adding the error to the
		 * sinusoid gives the correcting shoulder the longer half of every
		 * cycle. Past SG_AS_ABORT the error is not a lean to be biased, it
		 * is a turn to be made, and the chain ends.
		 *
		 * THE VETOES, in the order they are worth stating: never on a rope
		 * (the hook SETS velocity and an off-axis input accumulates into
		 * nothing), never while combat is live (`engaged`, this frame's
		 * answer, not the belief), never with the terminal brake down
		 * (bot->term_brake is the carrier's cornering throttle -- a chain
		 * there is a flag missed), never inside SG_AS_FLAGKEEP of a stand,
		 * never on the final approach, in water, ducked, mid-rocket-jump,
		 * mid-bomb, or through the spawn beat.
		 *
		 * THE ROAD. A hop chain commits the body for the length of a
		 * flight, so it wants a road to spend that on: run_link and
		 * open_ahead say the next step is ground running with room in it,
		 * and SG_RunRoom walks the same seeds the lookahead trusts, holds
		 * the point a stride down them, and asks whether the chord to it is
		 * straight, on-heading, and clear of the player box.
		 *
		 * THE APPETITE. The bar on that road is divided by the persona's
		 * hook enthusiasm -- the trait that already means "appetite for the
		 * optional piece of movement tech" -- so the keen bots commit on
		 * roads the cautious ones walk. Same +/-15% band every other
		 * consumer gets, and sixteen bots stop doing it identically.
		 */
		{
			float dose = sg_cv.airstrafe->value;
			float sp = sqrtf(e->velocity[0] * e->velocity[0] +
			                 e->velocity[1] * e->velocity[1]);

			if (dose > 0.0f && sp >= SG_AS_FLOOR && have_move &&
			    run_link && open_ahead && bestlink >= 0 && SG_Rune() &&
			    !precision && !engaged &&
			    bot->hook_phase == 0 && bot->rj_phase == 0 &&
			    bot->nade_phase == 0 && bot->term_brake >= 1.0f &&
			    e->waterlevel <= 1 && SG_TimerReady(bot->beat_until) &&
			    !(e->client->ps.pmove.pm_flags & PMF_DUCKED) &&
			    !SG_NearAFlag(e, SG_AS_FLAGKEEP))
			{
				vec3_t	vdir;
				float	cross, dot, err;
				/* the bar on the road, and the lower bar a chain ALREADY
				 * running is held to. A player who has committed to a
				 * chain does not re-audit the corridor every tenth of a
				 * second and stop dead when it narrows; he finishes the
				 * hop he is in. Without the hysteresis the road test
				 * chatters on and off across the bar and no chain lives
				 * long enough to reach the speeds the technique is for. */
				float	aswant = SG_AS_RUN / SG_PersonaHookScale(e);

				if (bot->as_since != 0.0f)
					aswant *= SG_AS_HOLD;

				vdir[0] = e->velocity[0] / sp;
				vdir[1] = e->velocity[1] / sp;
				vdir[2] = 0.0f;
				/* signed error from travel to route: positive is the route
				 * lying counter-clockwise, which is the sign a positive
				 * lean rotates the wish toward */
				cross = vdir[0] * move_dir[1] - vdir[1] * move_dir[0];
				dot = vdir[0] * move_dir[0] + vdir[1] * move_dir[1];
				err = atan2f(cross, dot) * 180.0f / (float)M_PI;

				if (err > -SG_AS_ABORT && err < SG_AS_ABORT &&
				    SG_RunRoom(e, SG_Rune()->links[bestlink].to,
				               route_field, move_dir, aswant))
				{
					float dt = (float)total / 1000.0f;

					as_ok = true;
					as_chain = (dose >= 2.0f);

					bot->as_phase += 2.0f * (float)M_PI * dt / SG_AS_PERIOD;
					while (bot->as_phase > 2.0f * (float)M_PI)
						bot->as_phase -= 2.0f * (float)M_PI;

					as_lean = sinf(bot->as_phase) + err / SG_AS_CORR;
					if (as_lean > 1.0f)
						as_lean = 1.0f;
					if (as_lean < -1.0f)
						as_lean = -1.0f;
				}
			}

			if (as_ok)
			{
				/* the same closed form the command uses, at the frame's
				 * speed and one sub-step of frametime, turned into the
				 * VIEW's share of the swing and asked for through the slew
				 * below like any other heading */
				float	ft = (float)base / 1000.0f;
				float	accelspeed = SG_AIR_ACCEL * ft * 300.0f;
				float	c = (300.0f - accelspeed) / sp;
				float	lv, yaw;

				if (bot->as_since == 0.0f)
				{
					SG_Mark(&bot->as_since);
					bot->as_entry = sp;
					bot->as_peak = sp;
				}
				else if (sp > bot->as_peak)
					bot->as_peak = sp;

				if (c > 1.0f)
					c = 1.0f;
				if (c < -1.0f)
					c = -1.0f;
				lv = acosf(c) * 180.0f / (float)M_PI *
				     as_lean * SG_AS_VIEWSHARE;
				if (lv > SG_AS_VIEWMAX)
					lv = SG_AS_VIEWMAX;
				if (lv < -SG_AS_VIEWMAX)
					lv = -SG_AS_VIEWMAX;

				yaw = SHORT2ANGLE((short)(cmd->angles[YAW] +
				      e->client->ps.pmove.delta_angles[YAW]));
				yaw += lv;
				cmd->angles[YAW] = ANGLE2SHORT(yaw)
				                - e->client->ps.pmove.delta_angles[YAW];
			}
			else
			{
				if (bot->as_since != 0.0f)
				{
					float dur = SG_Age(bot->as_since);

					/* one sustained chain in eight: a fleet chaining hops
					 * would otherwise write a line a second per bot, and
					 * the log is for reading */
					if (dur >= SG_AS_MINCHAIN &&
					    sg_cv.debug->value &&
					    !(bot->as_said++ & 7))
						gi.dprintf("AIRCHAIN %s %.2fs entry=%.0f "
						           "peak=%.0f\n",
						           e->client->pers.netname, dur,
						           bot->as_entry, bot->as_peak);
					bot->as_since = 0.0f;
				}
				bot->as_phase = 0.0f;
			}
		}

		/*
		 * THE VIEW SLEWS; IT NO LONGER TELEPORTS. Every writer above --
		 * navigation, combat, the rope aim, the swim pitch -- asks for a
		 * heading, and until now the ask was granted instantly: a 180
		 * happened in one server frame, superhuman to play against and
		 * unwatchable in chase-cam (reported live). The desired heading
		 * is decoded once here, and each subframe advances the actual
		 * view toward it along the shortest arc at sg_turnrate degrees a
		 * second (default 600: a competitive flick, half a second for a
		 * full 360). Zero restores the teleport.
		 */
		{
			float want_y = SHORT2ANGLE((short)(cmd->angles[YAW] +
			               e->client->ps.pmove.delta_angles[YAW]));
			float want_p = SHORT2ANGLE((short)(cmd->angles[PITCH] +
			               e->client->ps.pmove.delta_angles[PITCH]));
			float rate = sg_cv.turnrate->value;

			if (!bot->view_on || rate <= 0.0f)
			{
				bot->vy_cur = want_y;
				bot->vp_cur = want_p;
				bot->view_on = true;
			}
			slew_want_y = want_y;
			slew_want_p = want_p;
			slew_rate = rate;
		}

		for (step = 0; step < sub; step++)
		{
			cmd->msec = (byte)(base + (step < rem ? 1 : 0));
			if (!cmd->msec)
				continue;
			sub_msec = cmd->msec;

			if (slew_rate > 0.0f)
			{
				float dt = (float)cmd->msec / 1000.0f;
				float dy = slew_want_y - bot->vy_cur;
				float dp = slew_want_p - bot->vp_cur;
				float stepmax = slew_rate * dt;

				while (dy > 180.0f) dy -= 360.0f;
				while (dy < -180.0f) dy += 360.0f;
				while (dp > 180.0f) dp -= 360.0f;
				while (dp < -180.0f) dp += 360.0f;
				if (dy > stepmax) dy = stepmax;
				if (dy < -stepmax) dy = -stepmax;
				if (dp > stepmax) dp = stepmax;
				if (dp < -stepmax) dp = -stepmax;
				bot->vy_cur += dy;
				bot->vp_cur += dp;
				cmd->angles[YAW] = ANGLE2SHORT(bot->vy_cur)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd->angles[PITCH] = ANGLE2SHORT(bot->vp_cur)
				                  - e->client->ps.pmove.delta_angles[PITCH];
			}

			/*
			 * THE LEGS ARE FREE OF THE EYES. Movement was welded to the
			 * view -- forwardmove always ran down the gaze -- which is
			 * why the look-ahead experiment steered bodies into walls,
			 * why hook aim needed a standing frame, and why the fleet
			 * read as a mob of people walking wherever they happened to
			 * be staring (called out in exactly those words). A Quake
			 * body translates in four directions relative to ANY view:
			 * the course is a world-space fact (move_dir, the fan's
			 * answer), the view is whatever combat or the rope or the
			 * route wants, and forward/side are just the course
			 * decomposed into the ACTUAL slewed view frame, every
			 * subframe, so even mid-turn the body holds its line.
			 * Underwater stays welded -- pmove swims along the full
			 * view vector by design, and the swim pitch already aims
			 * the view down the course.
			 */
			if (plain_forward > 0 && have_move && e->waterlevel <= 1)
			{
				vec3_t vb, vf, vr;
				float fl;

				vb[YAW] = bot->vy_cur;
				vb[PITCH] = bot->vp_cur / 3.0f;
				vb[ROLL] = 0.0f;
				AngleVectors(vb, vf, vr, NULL);
				fl = sqrtf(vf[0] * vf[0] + vf[1] * vf[1]);
				if (fl > 0.01f)
				{
					cmd->forwardmove = (short)(400.0f *
					    (move_dir[0] * vf[0] + move_dir[1] * vf[1]) / fl);
					cmd->sidemove = (short)(400.0f *
					    (move_dir[0] * vr[0] + move_dir[1] * vr[1]));
				}
				else
				{
					cmd->forwardmove = plain_forward;
					cmd->sidemove = 0;
				}
			}
			else
			{
				cmd->forwardmove = plain_forward;
				cmd->sidemove = 0;
			}
			if (step == 0)
				cmd->upmove = nav_jump;

			/*
			 * No tricks while a rope is out -- the hook SETS velocity, so
			 * there is nothing for an off-axis input to accumulate -- and
			 * none on the final approach, where being able to stop on the
			 * flag is worth more than the speed.
			 */
			/*
			 * The weave replaces the step rather than adding to it: the
			 * strafe work above leans off the direction of TRAVEL to harvest
			 * acceleration down a route, and there is no route left to run
			 * here. `engaged` and not `duel` is the test -- a target that
			 * walked behind a wall two seconds ago is worth holding a range
			 * against, and is not worth dodging.
			 */
			if (duel_hold && engaged)
			{
				cmd->forwardmove = 0;
				cmd->sidemove = weave_side;
			}
			else if (have_move && !precision && bot->hook_phase == 0)
			{
				sg_air_t		airs;
				const sg_air_t	*airp = NULL;
				vec3_t			mf, mr;

				VectorCopy(basis_fwd, mf);
				VectorCopy(basis_right, mr);
				if (as_ok)
				{
					vec3_t vb;

					airs.lean = as_lean;
					airs.chain = as_chain;
					airs.vy_cur = bot->vy_cur;
					airs.vp_cur = bot->vp_cur;
					airp = &airs;

					/*
					 * A chain swings the VIEW, so the basis the policy
					 * decomposes against has to be the swung one -- the
					 * frame basis was built from the heading that was
					 * ASKED for, and solving a lean against a view the
					 * engine is not holding points the wish somewhere
					 * else. Rebuilt per sub-step, the same way the course
					 * decomposition above is.
					 */
					vb[YAW] = bot->vy_cur;
					vb[PITCH] = bot->vp_cur;
					if (vb[PITCH] > 180.0f)
						vb[PITCH] -= 360.0f;
					vb[PITCH] /= 3.0f;
					vb[ROLL] = 0.0f;
					AngleVectors(vb, mf, mr, NULL);
				}

				/*
				 * THE LAST TEN METERS, THE MOVEMENT HALF (406 canary
				 * forensics): the brake slowed the orbit but the strafe
				 * LEAN kept re-making it -- realign, accelerate past
				 * 200, lean off-axis, miss, brake, repeat, 80 seconds
				 * at goal-cost 200. Wave 96 ruled the walk straight;
				 * the aim obeyed, the legs never did. In terminal mode
				 * there is no lean and no hop: plain forward down the
				 * exact aim, throttle on the brake, touch.
				 */
				if (!bot->terminal)
				SG_MovePolicy(e, cmd, mf, mr, move_dir,
				              open_ahead, run_link,
				              (float)cmd->msec / 1000.0f, airp);

				/*
				 * THE CARRIER'S JINK. The parity killer census (waves
				 * 141-146, 5v5/7v7): 30 of 64 carrier deaths to the
				 * rail, escorts standing right there for 54 of the 65
				 * carries -- a screen does not bend a hitscan line, only
				 * the target's own motion does. This is NOT the weave
				 * (2.4-D2 stands: a carrier never trades its route for a
				 * fight): forward motion is kept whole and a half-rate
				 * side component serpentines the line of travel, ~26
				 * degrees of wander for ~11%% of the pace, only while a
				 * fresh contact (3s) is believed and the legs are on the
				 * ground doing route work.
				 */
				/* open ground only: wave 147's first jink census traded
				 * rails (47%%->25%%) for WEDGES -- carrier suicides went
				 * from one every other wave to four in one, the
				 * serpentine drifting bodies into corners the feelers
				 * had already vetoed. No open room ahead, no jink: a
				 * wall stops more rails than it starts. */
				if (role == SG_ROLE_CARRY && cmd->forwardmove != 0 &&
				    open_ahead &&
				    !sg_cv.noweave->value)
				{
					int s9;

					for (s9 = 0; s9 < SG_MAX_ENEMY_TRACK; s9++)
					{
						sg_belief_enemy_t *en9 =
						    &sg_caco_enemies[SG_TeamIdx(team)][s9];

						if (en9->client >= 0 &&
						    SG_AgeUnder(en9->seen_time, 3.0f))
						{
							float jp = SG_WEAVE_BASE + SG_WEAVE_STEP *
							    (float)((int)(e->client - game.clients) % 10);
							short js = (fmodf(level.time, jp) < jp * 0.5f)
							           ? 1 : -1;

							cmd->sidemove = (short)(cmd->sidemove / 2
							               + js * (cmd->forwardmove > 0
							                       ? cmd->forwardmove
							                       : -cmd->forwardmove) / 2);
							break;
						}
					}
				}
			}

			/*
			 * THE SPAWN BEAT'S LEGS, applied last so nothing downstream
			 * can hand the throttle back. The breather scales the
			 * command where it is first built and SG_MovePolicy rebuilds
			 * it afterwards from the course; a beat that wants the body
			 * genuinely idling has to be the final word, and this is the
			 * only place that exists.
			 */
			if (SG_TimerPending(bot->beat_until))
			{
				cmd->forwardmove = (short)(cmd->forwardmove * 0.30f);
				cmd->sidemove = (short)(cmd->sidemove * 0.30f);
			}

			/*
			 * DE-PACE (sg_depace, rung-4 cut #4). The post-zone split
			 * killed the defender hypothesis: 86-89% of the escort
			 * gap is mid-field co-travel, and the anti-linger
			 * surcharge nulled because corridors have no lateral room
			 * -- every candidate link sits inside the carrier's 400u
			 * bubble, so a spatial price has no gradient. Humans
			 * separate TEMPORALLY: varied speeds, item stops,
			 * staggered starts. A lingering non-escort eases off the
			 * throttle (cvar = the scale) until it falls out of the
			 * bubble; the carrier never slows, the convoy de-phases,
			 * and in film it reads as pacing variation, not flight.
			 */
			if (bot->linger_hot &&
			    sg_cv.depace->value > 0.0f)
			{
				float dp = sg_cv.depace->value;

				cmd->forwardmove = (short)(cmd->forwardmove * dp);
				cmd->sidemove = (short)(cmd->sidemove * dp);
			}

			/* the terminal brake: cornering throttle at the stands,
			 * same final-word slot for the same reason */
			if (bot->term_brake < 1.0f)
			{
				cmd->forwardmove = (short)(cmd->forwardmove * bot->term_brake);
				cmd->sidemove = (short)(cmd->sidemove * bot->term_brake);
			}

			ClientThink(e, cmd);

			/*
			 * Let go of the jump. PM_CheckJump clears PMF_JUMP_HELD only when
			 * a command arrives with upmove under 10 and refuses to jump at
			 * all while it is set, so holding the key buys nothing and costs
			 * the next hop. The release lands inside the same tenth of a
			 * second as the press.
			 */
			if (cmd->upmove >= 10)
				cmd->upmove = 0;
		}
		cmd->msec = (byte)sub_msec;
	}

	/*
	 * Hook lifecycle, after the think so v_angle reflects this frame's
	 * aim. Fire with the game's own Cmd_Hook_f -- the same entry the
	 * console command uses -- and release before the rope enters the
	 * p_weapon.c brake band (ladder starts at 120; 200 leaves a frame of
	 * margin at pull speed). A rope that never attached by its deadline
	 * is cut loose.
	 */
	{
		/*
		 * A rope this bot does not think it owns is a rope it cannot ever
		 * release: g_cmds.c's Cmd_Unhook_f, when the grapple happens to be
		 * pers.weapon, only forces -attack and NEVER aborts -- the live
		 * hook's short-rope dead-stop then overwrites velocity with ~0
		 * every frame (p_weapon.c:2099-2104) and p_client.c:2834 zeroes
		 * gravity, freezing the bot in place for good (Trace, 96 seconds,
		 * 4v4 match). The bot releases through ctf_hook_abort directly --
		 * the same unconditional abort p_weapon.c itself calls -- and this
		 * guard clears any rope left over from a path we did not arm.
		 */
		if (bot->hook_phase == 0 && e->client->hookstate != 0)
			ctf_hook_abort(e);

		/* rocket-jump phase 1: ask for the launcher through the same use
		 * path a player's "use" command runs, at a polite rate */
		if (bot->rj_phase == 1 && SG_TimerReady(bot->rj_use_next))
		{
			static gitem_t *rj_rl3;

			if (!rj_rl3)
				rj_rl3 = FindItem("Rocket Launcher");
			if (rj_rl3 && rj_rl3->use)
				rj_rl3->use(e, rj_rl3);
			SG_TimerArm(&bot->rj_use_next, 0.5f);
		}

		if (bot->hook_phase == 1 && SG_TimerReadyStrict(bot->hook_deadline))
		{
			/* the aim never arrived (blocked slew, moving anchor line,
			 * whatever): stand down clean. Without this, a wedged phase
			 * 1 gates out combat FOREVER -- wave 92's three silent
			 * games. */
			bot->hook_phase = 0;
			bot->speedhook = false;
		}
		else if (bot->hook_phase == 1)
		{
			/* the rope fires along the ACTUAL view, which now slews:
			 * hold phase 1 (a standing frame anyway) until the view has
			 * arrived within 3 degrees of the anchor bearing */
			vec3_t ad2;
			float ay, ap, ddy, ddp;

			VectorSubtract(bot->hook_anchor, e->s.origin, ad2);
			ad2[2] -= e->viewheight;
			ay = atan2f(ad2[1], ad2[0]) * 180.0f / (float)M_PI;
			ap = -atan2f(ad2[2], sqrtf(ad2[0] * ad2[0] + ad2[1] * ad2[1]))
			     * 180.0f / (float)M_PI;
			ddy = ay - bot->vy_cur;
			ddp = ap - bot->vp_cur;
			while (ddy > 180.0f) ddy -= 360.0f;
			while (ddy < -180.0f) ddy += 360.0f;
			/* (quickrope's 10-degree carrier fire read NEGATIVE
			 * pooled 216-217 -- sloppy ropes ride worse than the
			 * ritual they save. The sniper's 3 stands for all.) */
			/* THE HOP-FIRE, corrected (wave 223): the owner hops
			 * BEFORE the hook, not with it -- wave 222 put jump and
			 * fire on one command frame and the drag still started
			 * grounded. Now the hop goes in as the aim closes (8
			 * degrees); by the 3-degree fire gate the body is
			 * already airborne and the pull never meets friction. */
			if (sg_cv.hopfire->value &&
			    e->groundentity &&
			    fabsf(ddy) < 8.0f && fabsf(ddp) < 8.0f)
				cmd->upmove = 400;
			if (slew_rate > 0.0f &&
			    (fabsf(ddy) > 3.0f || fabsf(ddp) > 3.0f))
				goto hook_wait;

			/* (The anchor-distance veto lived here for one wave --
			 * 24,728 vetoes, land rate unmoved. Wrong test: bites
			 * were never the failure.) The HOOKABORT census named
			 * the real one: 2,403 bolts a wave die on the SKYBOX --
			 * a lip anchor plus three degrees of slew slack at four
			 * hundred units sails the bolt over the edge. This trace
			 * tests the ACTUAL fire line for the ACTUAL failure: if
			 * the view's own ray ends in sky, wait a frame for the
			 * slew to settle instead of donating the rope to the
			 * void. Cannot strobe a live rope; fires still happen the
			 * moment the line lands on architecture. */
			{
				vec3_t sdir, seye, send;
				trace_t str;

				sdir[0] = cosf(bot->vp_cur * (float)M_PI / 180.0f)
				        * cosf(bot->vy_cur * (float)M_PI / 180.0f);
				sdir[1] = cosf(bot->vp_cur * (float)M_PI / 180.0f)
				        * sinf(bot->vy_cur * (float)M_PI / 180.0f);
				sdir[2] = -sinf(bot->vp_cur * (float)M_PI / 180.0f);
				VectorCopy(e->s.origin, seye);
				seye[2] += e->viewheight;
				VectorMA(seye, 4096.0f, sdir, send);
				str = gi.trace(seye, NULL, NULL, send, e, MASK_SOLID);
				if (str.surface &&
				    (str.surface->flags & SURF_SKY))
				{
					if (sg_cv.debug->value)
						gi.dprintf("HOOKSKYHOLD %s\n",
						           e->client->pers.netname);
					goto hook_wait;
				}
				/* same hold for a teammate in the fire line: the bolt
				 * dies on their body (hook_touch rejects teammates) --
				 * ~57 wasted fires a wave. MASK_SHOT sees bodies where
				 * the sky trace's MASK_SOLID cannot. */
				str = gi.trace(seye, NULL, NULL, send, e, MASK_SHOT);
				if (str.ent && str.ent->client &&
				    str.ent->client->ctf.teamnum ==
				    e->client->ctf.teamnum)
				{
					if (sg_cv.debug->value)
						gi.dprintf("HOOKMATEHOLD %s\n",
						           e->client->pers.netname);
					goto hook_wait;
				}
			}
			Cmd_Hook_f(e);
			bot->hook_phase = 2;
			if (sg_cv.debug->value)
				gi.dprintf("HOOKFIRE %s at (%.0f %.0f %.0f)\n",
				           e->client->pers.netname, bot->hook_anchor[0],
				           bot->hook_anchor[1], bot->hook_anchor[2]);
		}
		else if (bot->hook_phase == 2)
		{
			vec3_t rd, td;
			float rope;
			qboolean arrived, was_pulling;

			/*
			 * NO attach-point verification, on evidence. Release
			 * conditions track the DESTINATION, so a rope biting 50-150
			 * off its proven anchor still flies a working ride -- lmctf03
			 * converted 95% that way all along. Policing the anchor
			 * (tried at 48, then 96) turned every imperfect rope into a
			 * 10Hz fire-abort strobe: 2704 aborts, 9 landings, zero
			 * kills. Rides that genuinely fail are caught where failure
			 * is real -- the field-served check at landing, which
			 * shelves the link (HOOKFAIL).
			 *
			 * But MEASURE the bite: 78-87 percent of rides fail the
			 * field test across four maps, and the two suspects for
			 * wrong bites at scale are teammates' bodies (the bolt
			 * clips MASK_SHOT, which includes players) and doors closed
			 * at runtime that generation held open for the rope-line
			 * proof. The attach entity's classname names the culprit.
			 */
			if (sg_cv.debug->value &&
			    e->client->hook && e->client->hook->hook_target &&
			    !bot->hook_bite_logged)
			{
				vec3_t ba;
				edict_t *ht = e->client->hook->hook_target;

				VectorSubtract(e->client->hook->s.origin,
				               bot->hook_anchor, ba);
				if (VectorLength(ba) > 96.0f)
					gi.dprintf("HOOKBITE %s off=%.0f into=%s org=(%.0f %.0f %.0f) want=(%.0f %.0f %.0f) got=(%.0f %.0f %.0f)\n",
					           e->client->pers.netname, VectorLength(ba),
					           ht->classname ? ht->classname :
					           (ht == g_edicts ? "world" : "?"),
					           e->s.origin[0], e->s.origin[1], e->s.origin[2],
					           bot->hook_anchor[0], bot->hook_anchor[1],
					           bot->hook_anchor[2],
					           e->client->hook->s.origin[0],
					           e->client->hook->s.origin[1],
					           e->client->hook->s.origin[2]);
				bot->hook_bite_logged = true;
			}
			{

			VectorSubtract(bot->hook_anchor, e->s.origin, rd);
			rope = VectorLength(rd);
			/*
			 * Release the way the prover released (SG_Rune().c:494-502):
			 * horizontally near the DESTINATION with the height nearly
			 * made, or rope inside the brake band. The old rope<200 cut
			 * every climb loose below its lip -- the bot slid back down
			 * and re-fired the same anchor forever.
			 */
			VectorSubtract(bot->hook_dest, e->s.origin, td);
			arrived = (td[0] * td[0] + td[1] * td[1] < 80.0f * 80.0f &&
			           td[2] > -96.0f && td[2] < 96.0f);

			/*
			 * THE EARLY RELEASE -- ride the rope only as long as the
			 * rope is doing something momentum cannot. The old release
			 * conditions rode every rope to its terminus (observed
			 * live: a bot riding all the way onto the platform where a
			 * human cuts loose under the ceiling and lets the throw
			 * fling them through the door into a bhop chain). Each ride
			 * frame projects the ballistic throw: velocity toward the
			 * destination within ~35 degrees, arrival inside 1.2s, and
			 * the parabola landing within a jumpable window of the
			 * destination height -- cut NOW, fly the rest, keep every
			 * unit of speed. flow_release skips the landing brake: the
			 * whole point of the cut is what happens after it.
			 */
			if (!bot->speedhook && e->client->hookstate != 0)
			{
				float hd2 = sqrtf(td[0] * td[0] + td[1] * td[1]);
				float hv2 = sqrtf(e->velocity[0] * e->velocity[0] +
				                  e->velocity[1] * e->velocity[1]);

				if (hv2 > 300.0f && hd2 > 40.0f)
				{
					float tt = hd2 / hv2;
					float toward = (e->velocity[0] * td[0] +
					                e->velocity[1] * td[1]) / (hv2 * hd2);

					/*
					 * td[2] < 160: the cut is for HORIZONTAL finishes.
					 * Cutting a vertical climb throws the body up BESIDE
					 * the ledge lip to fall straight back down -- one
					 * shaft room turned the whole fleet into confused
					 * circlers within a wave of the feature shipping
					 * (live report, lowest port). Climbs ride to the
					 * top; that is what riding is FOR. And the parabola
					 * must clear ABOVE the destination, never scrape
					 * under it.
					 */
					if (tt < 1.2f && toward > 0.82f)
					{
						float grav = e->client->ps.pmove.gravity
						             ? (float)e->client->ps.pmove.gravity
						             : 800.0f;
						float zp = e->velocity[2] * tt
						         - 0.5f * grav * tt * tt;

						if (zp - td[2] > 24.0f && zp - td[2] < 260.0f)
						{
							/*
							 * THE BODY HAS A BOX (owner's order, wave 372):
							 * the fling arc is flown by a player-sized box,
							 * not a point. Walk the parabola in six segments
							 * with the real mins/maxs; an arc that clips
							 * architecture is not released -- keep riding the
							 * rope, which is the safe fallback and also what
							 * a human does when the fling line is not there.
							 */
							{
								vec3_t ap0, ap1;
								trace_t atr;
								int aseg;
								qboolean arc_clear = true;

								VectorCopy(e->s.origin, ap0);
								for (aseg = 1; aseg <= 6; aseg++)
								{
									float at = tt * (float)aseg / 6.0f;

									ap1[0] = e->s.origin[0] + e->velocity[0] * at;
									ap1[1] = e->s.origin[1] + e->velocity[1] * at;
									ap1[2] = e->s.origin[2] + e->velocity[2] * at
									       - 0.5f * grav * at * at;
									atr = gi.trace(ap0, e->mins, e->maxs, ap1,
									               e, MASK_PLAYERSOLID);
									/*
									 * aseg 1 exempt: the box leaves from beside the
									 * wall the rope hangs on. And a CONTACT is not a
									 * CRASH (wave 378: ~300 vetoes/game survived the
									 * seg-1 exemption -- corridor flings graze walls
									 * constantly, and pmove clip-slide carries them
									 * through). Only a head-on hit -- the segment
									 * direction driving into the plane -- kills the
									 * fling; grazes fly on. Seg 6 is landing contact.
									 */
									if (atr.fraction < 1.0f && aseg > 1 && aseg < 6)
									{
										vec3_t sd;
										float sl;
										VectorSubtract(ap1, ap0, sd);
										sl = VectorLength(sd);
										if (sl > 1.0f)
										{
											VectorScale(sd, 1.0f / sl, sd);
											/* floors don't veto: descending onto a lip
											 * near the target is a landing, not a crash
											 * (wave 380: graze fix halved vetoes but
											 * 100-300/game remained -- late-arc floor
											 * touchdowns reading as head-on). Walls and
											 * ceilings only. */
											if (atr.plane.normal[2] < 0.7f &&
											    DotProduct(sd, atr.plane.normal) < -0.7f)
											{
												arc_clear = false;
												break;
											}
										}
									}
									VectorCopy(ap1, ap0);
								}
								if (!arc_clear)
								{
									if (sg_cv.debug->value)
										gi.dprintf("HOOKARCVETO %s\n",
										           e->client->pers.netname);
									goto hook_wait;
								}
							}
							ctf_hook_abort(e);
							bot->hook_phase = 3;
							bot->flow_release = true;
							SG_TimerArm(&bot->hook_deadline, 1.4f);
							bot->hookfail_streak = 0;
							/*
							 * THE FALL-THROUGH (found wave 285): without
							 * this exit, control ran on into the release
							 * chain below, which saw the rope this cut
							 * just killed (hookstate 0), printed a false
							 * "noattach", and reset hook_phase to 0 --
							 * destroying the flow ride armed two lines
							 * up. 91% of the noattach mass, ~3,200 rides
							 * a wave, were the bots' BEST hooks being
							 * reported as their failures and stripped of
							 * the landing steer/pre-turn/brake.
							 */
							goto hook_wait;
						}
					}
				}
			}

			if (bot->speedhook)
			{
				/* the burst release: cut at speed and KEEP it -- no
				 * landing brake, no shelf accounting, phase straight to
				 * zero and the legs inherit 600+ */
				float bs2 = e->velocity[0] * e->velocity[0]
				          + e->velocity[1] * e->velocity[1]
				          + e->velocity[2] * e->velocity[2];

				if (bs2 > 600.0f * 600.0f ||
				    SG_TimerReadyStrict(bot->hook_deadline) ||
				    e->client->hookstate == 0)
				{
					if (e->client->hookstate != 0)
						ctf_hook_abort(e);
					/* the burst's three ends, named: reached speed
					 * (the SILENT SUCCESS that made the land-rate
					 * denominator a lie), stalled to deadline, or
					 * the rope never held */
					if (sg_cv.debug->value)
						gi.dprintf("HOOKEND %s %s\n",
						           e->client->pers.netname,
						           bs2 > 600.0f * 600.0f ? "burst"
						           : (e->client->hookstate == 0
						              ? "noattach" : "burststall"));
					bot->hook_phase = 0;
					bot->speedhook = false;
					bot->commit_link = -1;
				}
			}
			else if (arrived || rope < 130.0f ||
			    SG_TimerReadyStrict(bot->hook_deadline) || e->client->hookstate == 0)
			{
				was_pulling = (e->client->hookstate != 0);
				if (was_pulling)
					ctf_hook_abort(e);
				/* a cut live rope hands off to the landing steer; a rope
				 * that never attached does not */
				if (!was_pulling && sg_cv.debug->value)
					gi.dprintf("HOOKEND %s noattach\n",
					           e->client->pers.netname);
				bot->hook_phase = was_pulling ? 3 : 0;
				/* this phase-3 entry is a plain rope cut, never a flow
				 * release -- the flag used to stay latched from an
				 * earlier wiped release and made ~1,000 rides a wave
				 * take the apex cut and skip the landing brake */
				bot->flow_release = false;
				SG_TimerArm(&bot->hook_deadline, 1.0f);
			}
			}
		}
	}

hook_wait:;
	/* the literal emission record: what this frame's usercmd contained */
	if (sg_cv.debug->value >= 2 ||
	    (sg_cv.debug->value && SG_TimerReady(bot->next_cmdlog)))
	{
		SG_TimerArm(&bot->next_cmdlog, 1.0f);
		/* the last step of the frame: fwd/side/up are that step's command,
		 * and msec x steps is how the frame's real time was spent */
		gi.dprintf("CMD %s: fwd=%d side=%d up=%d btn=%d yaw=%d pitch=%d msec=%d x%d\n",
		           e->client->pers.netname, cmd->forwardmove, cmd->sidemove,
		           cmd->upmove, cmd->buttons, cmd->angles[YAW],
		           cmd->angles[PITCH], cmd->msec, sub_steps);
	}

	/* once a second, the full body state: enough to reconstruct any stall
	 * offline without another instrumented rerun */
	if (sg_cv.debug->value && SG_TimerReady(bot->next_report))
	{
		float sp = sqrtf(e->velocity[0] * e->velocity[0] +
		                 e->velocity[1] * e->velocity[1]);
		/* sgoal: the bot's cost on the STATIC field for its role's true
		 * destination (attacker -> enemy stand, everyone else -> own).
		 * The composed goal= number rebuilds under a standing bot --
		 * item beliefs, danger, re-floods -- and wave 169's traces
		 * caught a stationary attacker "receding" 800ms/s in it. Route
		 * progress gets measured against a number that only moves when
		 * the body does. */
		int sgoal = -1;
		const int *sfld = (role == SG_ROLE_ATTACK)
		    ? ((team == CTF_TEAM_RED) ? sg_fields.to_blue_flag
		                              : sg_fields.to_red_flag)
		    : ((team == CTF_TEAM_RED) ? sg_fields.to_red_flag
		                              : sg_fields.to_blue_flag);

		if (bot->seed >= 0 && sfld && sfld[bot->seed] < SG_FIELD_INF)
			sgoal = sfld[bot->seed];
		SG_TimerArm(&bot->next_report, 1.0f);
		gi.dprintf("SG %s: role=%d seed=%d goal=%d sgoal=%d spd=%.0f org=(%.0f %.0f %.0f) link=%d "
		           "act=%d hp=%d dh=%d dl=%d st=%.1f gnd=%d eng=%d\n",
		           e->client->pers.netname, role, bot->seed,
		           (bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF)
		               ? goal_field[bot->seed] : -1,
		           sgoal,
		           sp, e->s.origin[0], e->s.origin[1], e->s.origin[2],
		           bestlink,
		           (bestlink >= 0) ? SG_Rune()->links[bestlink].action : -1,
		           bot->hook_phase, door_hold, (int)drop_yaw_locked,
		           bot->stuck_time, e->groundentity != NULL,
		           (int)bot->engaged_last);
	}

	/*
	 * The same once-a-second cadence, for the eye instead of the log
	 * (sg_drawplan). Its own clock, because the debug report above is
	 * gated on sg_debug and the two are useful separately.
	 */
	if (SG_TimerReady(bot->plan_next))
	{
		SG_TimerArm(&bot->plan_next, 1.0f);
		SG_DrawPlan(bot, team, bestlink, route_field);
	}
}