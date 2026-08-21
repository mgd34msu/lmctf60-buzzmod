/* sg_util.c -- see sg_util.h for the argument. */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_action.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_replay.h"
#include "slipgate/sg_rune.h"

int SG_TeamIdx(int team)
{
	return team - CTF_TEAM_RED;
}

int SG_TeamFromIdx(int idx)
{
	return CTF_TEAM_RED + idx;
}

int SG_EnemyTeam(int team)
{
	return (team == CTF_TEAM_RED) ? CTF_TEAM_BLUE : CTF_TEAM_RED;
}

qboolean SG_ImmutableSupport(edict_t *ent)
{
	return ent && ent->inuse && ent->classname &&
	       (!strcmp(ent->classname, "info_flag_red") ||
	        !strcmp(ent->classname, "info_flag_blue") ||
	        !strcmp(ent->classname, "misc_teleporter_dest"));
}

qboolean SG_ActionOwnsControl(int action)
{
	return SG_ActionRuntimeHasTrait(action, SG_ACTF_OWNS_CONTROL);
}

/* Exact planar command used by both declared-mechanism generation and live
 * execution. The trigger/platform owns the vertical transition; the body
 * walks the serialized static approach/egress at normal Pmove cadence. */
qboolean SG_DeclaredCommand(const vec3_t origin, const vec3_t target,
	const pmove_state_t *pms, usercmd_t *cmd)
{
	vec3_t delta;
	float horiz, yaw;
	byte msec;

	if (!origin || !target || !pms || !cmd)
		return false;
	VectorSubtract(target, origin, delta);
	delta[2] = 0.0f;
	horiz = VectorLength(delta);
	if (!isfinite(horiz))
		return false;
	yaw = horiz > 0.01f
	    ? atan2f(delta[1], delta[0]) * 180.0f / (float)M_PI : 0.0f;
	msec = cmd->msec;
	memset(cmd, 0, sizeof(*cmd));
	cmd->msec = msec;
	cmd->angles[PITCH] = -pms->delta_angles[PITCH];
	cmd->angles[YAW] = ANGLE2SHORT(yaw) - pms->delta_angles[YAW];
	cmd->angles[ROLL] = -pms->delta_angles[ROLL];
	if (horiz > 4.0f)
		cmd->forwardmove = (horiz < 32.0f) ? 200 : 400;
	return true;
}

/* Shared terminal envelope for a human "cover me" order. Horizontal
 * closeness on stacked floors is not arrival; require a body-scale vertical
 * band so the route remains live until bot and teammate occupy one space. */
qboolean SG_EscortTerminal(edict_t *bot, edict_t *target)
{
	vec3_t delta, from, to;
	trace_t tr;

	if (!bot || !target || !bot->inuse || !target->inuse)
		return false;
	VectorSubtract(target->s.origin, bot->s.origin, delta);
	if (delta[0] * delta[0] + delta[1] * delta[1] >= 96.0f * 96.0f ||
	    fabsf(delta[2]) >= 56.0f)
		return false;
	VectorCopy(bot->s.origin, from);
	VectorCopy(target->s.origin, to);
	from[2] += bot->viewheight;
	to[2] += target->viewheight;
	tr = sg_host.trace(from, NULL, NULL, to, bot, MASK_OPAQUE);
	return !tr.startsolid && !tr.allsolid &&
	       (tr.fraction >= 1.0f || tr.ent == target);
}

edict_t *SG_FlagCarrier(edict_t *flag)
{
	edict_t *owner;

	if (!flag || !flag->inuse)
		return NULL;
	owner = flag->owner;
	if (!owner || !owner->inuse || !owner->client ||
	    ClientHasFlag(owner) != flag)
		return NULL;
	return owner;
}

qboolean SG_FlagApproachAvailableTo(edict_t *flag, edict_t *player)
{
	if (!flag || !flag->inuse || !player || !player->inuse ||
	    !player->client || player->health < 1)
		return false;
	if (SG_FlagCarrier(flag))
		return false;
	return flag->owner != player;
}

edict_t *SG_OwnFlag(int team)
{
	edict_t *f = (team == CTF_TEAM_RED) ? redflag : blueflag;

	return (f && f->inuse && !SG_FlagCarrier(f)) ? f : NULL;
}

edict_t *SG_EnemyFlag(int team)
{
	edict_t *f = (team == CTF_TEAM_RED) ? blueflag : redflag;

	return (f && f->inuse && !SG_FlagCarrier(f)) ? f : NULL;
}

edict_t *SG_FlagStand(int team, qboolean own)
{
	qboolean red = (team == CTF_TEAM_RED) ? own : !own;

	return G_Find(NULL, FOFS(classname),
	              red ? "info_flag_red" : "info_flag_blue");
}

float SG_DistXY(const vec3_t a, const vec3_t b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1];

	return sqrtf(dx * dx + dy * dy);
}

qboolean SG_CanSee(edict_t *e, const vec3_t pt, float lift_z)
{
	vec3_t eye, tgt;
	trace_t tr;

	VectorCopy(e->s.origin, eye);
	eye[2] += e->viewheight;
	VectorCopy(pt, tgt);
	tgt[2] += lift_z;
	tr = sg_host.trace(eye, NULL, NULL, tgt, e, MASK_OPAQUE);
	return tr.fraction >= 1.0f;
}

qboolean SG_HookAimAngles(const vec3_t origin, float viewheight,
	const vec3_t aim, vec3_t view_angles)
{
	vec3_t eye, dir;
	float horizontal, yaw, pitch;

	VectorCopy(origin, eye);
	eye[2] += viewheight;
	VectorSubtract(aim, eye, dir);
	horizontal = sqrtf(dir[0] * dir[0] + dir[1] * dir[1]);
	if (!isfinite(horizontal) || !isfinite(dir[2]) ||
	    (horizontal < 0.01f && fabsf(dir[2]) < 0.01f))
		return false;

	yaw = atan2f(dir[1], dir[0]) * 180.0f / (float)M_PI;
	pitch = -atan2f(dir[2], horizontal) * 180.0f / (float)M_PI;
	/* Pmove receives shorts, not ideal floats. Reconstruct the exact angles
	 * that AngleVectors will see after the command crosses that boundary. */
	view_angles[PITCH] = SHORT2ANGLE((short)ANGLE2SHORT(pitch));
	view_angles[YAW] = SHORT2ANGLE((short)ANGLE2SHORT(yaw));
	view_angles[ROLL] = 0.0f;
	return true;
}

/* Graph-hook control.  The link stores the exact quantized view
 * angles that were proved and the distance travelled by the corresponding
 * handed muzzle ray to its static-world bite.  Reconstructing both from those
 * three scalars is idempotent; unlike solving angles back from a bite, it
 * cannot cross an adjacent usercmd-short boundary. */
qboolean SG_HookControlDecode(const vec3_t origin, float viewheight, int hand,
	const vec3_t control, vec3_t view_angles, vec3_t muzzle, vec3_t bite)
{
	vec3_t forward, right;
	float distance;

	if (!isfinite(control[PITCH]) || !isfinite(control[YAW]) ||
	    !isfinite(control[ROLL]) ||
	    control[PITCH] < -89.0f || control[PITCH] > 89.0f ||
	    control[PITCH] != SHORT2ANGLE((short)ANGLE2SHORT(control[PITCH])) ||
	    control[YAW] != SHORT2ANGLE((short)ANGLE2SHORT(control[YAW])))
		return false;
	distance = control[ROLL];
	if (distance < 1.0f || distance > RUNE_HOOK_MAX_RAY)
		return false;
	view_angles[PITCH] = control[PITCH];
	view_angles[YAW] = control[YAW];
	view_angles[ROLL] = 0.0f;
	AngleVectors(view_angles, forward, right, NULL);
	CTF_HookMuzzle(origin, viewheight, hand, forward, right, muzzle);
	VectorNormalize(forward); /* fire_hook normalizes its flight direction */
	VectorMA(muzzle, distance, forward, bite);
	return true;
}

qboolean SG_SwimCommand(const vec3_t origin, const vec3_t destination,
	const pmove_state_t *pms, usercmd_t *cmd)
{
	sg_replay_pose_t pose;
	byte msec;

	if (!origin || !destination || !pms || !cmd)
		return false;
	msec = cmd->msec;
	memset(cmd, 0, sizeof(*cmd));
	cmd->msec = msec;
	if (msec != SG_SWIM_STEP_MSEC)
		return false;
	memset(&pose, 0, sizeof(pose));
	pose.pms = *pms;
	VectorCopy(origin, pose.origin);
	return SG_SwimReplayCommand(&pose, destination,
	                            SG_SWIM_REPLAY_EGRESS, cmd);
}

qboolean SG_SwimArrived(const vec3_t origin, const vec3_t destination,
	qboolean destination_water, qboolean grounded, int watertype, int waterlevel,
	edict_t *passent)
{
	vec3_t delta, from, to;
	trace_t tr;

	VectorSubtract(destination, origin, delta);
	if (delta[0] * delta[0] + delta[1] * delta[1] >= 40.0f * 40.0f ||
	    delta[2] <= -72.0f || delta[2] >= 72.0f)
		return false;
	if (waterlevel > 0 && (watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		return false;
	if (destination_water ?
	        (waterlevel < 2 || !(watertype & CONTENTS_WATER)) :
	        (!grounded || waterlevel >= 2))
		return false;

	VectorCopy(origin, from);
	VectorCopy(destination, to);
	from[2] += 16.0f;
	to[2] += 16.0f;
	tr = sg_host.trace(from, NULL, NULL, to, passent, MASK_PLAYERSOLID);
	return !tr.startsolid && !tr.allsolid && tr.fraction >= 1.0f;
}

/* Arrival shared by graph-hook settlement. Proximity alone can put the body
 * on the opposite side of a thin wall or stacked floor; require support and a
 * chest-height player-solid contact line exactly as the other proved actions
 * do, and never call lava/slime a successful landing. */
qboolean SG_SupportedArrived(const vec3_t origin, const vec3_t destination,
	qboolean grounded, int watertype, int waterlevel, edict_t *passent)
{
	vec3_t delta, from, to;
	trace_t tr;

	VectorSubtract(destination, origin, delta);
	if (delta[0] * delta[0] + delta[1] * delta[1] >= 40.0f * 40.0f ||
	    delta[2] <= -72.0f || delta[2] >= 72.0f ||
	    (!grounded && waterlevel < 2) ||
	    (waterlevel > 0 && (watertype & (CONTENTS_LAVA | CONTENTS_SLIME))))
		return false;
	VectorCopy(origin, from);
	VectorCopy(destination, to);
	from[2] += 16.0f;
	to[2] += 16.0f;
	tr = sg_host.trace(from, NULL, NULL, to, passent, MASK_PLAYERSOLID);
	return !tr.startsolid && !tr.allsolid && tr.fraction >= 1.0f;
}

/* A misc_teleporter's solid model sits below its origin while the trigger is
 * above it. Dry movement is planar, but submerged movement aims in full 3-D;
 * every nominal proof, online proof and live command must therefore use the
 * same reachable player-centre point on top of the pad rather than steering
 * into its raw entity origin. Keep the point on the pmove 1/8-unit lattice. */
qboolean SG_TeleportApproachPoint(edict_t *pad, vec3_t approach)
{
	int axis;

	if (!pad || !pad->inuse || !pad->classname ||
	    strcmp(pad->classname, "misc_teleporter") != 0)
		return false;
	VectorSet(approach, pad->s.origin[0], pad->s.origin[1],
	          pad->s.origin[2] + pad->maxs[2] + 24.0f);
	for (axis = 0; axis < 3; axis++)
	{
		if (!isfinite(approach[axis]) || approach[axis] < -4096.0f ||
		    approach[axis] > 4095.875f)
			return false;
		approach[axis] = (short)(approach[axis] * 8.0f) * 0.125f;
	}
	return true;
}

/* A plat's center trigger spans the shaft.  Touching it while the plat is at
 * the top postpones the return timer, so a waiting bot must get its whole
 * player hull outside before it can wait safely.  Return an XY escape point
 * only while the current body overlaps that expanded trigger footprint. */
qboolean SG_LiftWaitPoint(edict_t *plat, const vec3_t origin, vec3_t wait)
{
	edict_t *trigger = NULL;
	float lo_x, hi_x, lo_y, hi_y;
	float distances[4];
	int i, side = 0;

	if (!plat)
		return false;
	for (i = 0; i < globals.num_edicts; i++)
	{
		edict_t *candidate = &g_edicts[i];

		if (candidate->inuse && candidate->solid == SOLID_TRIGGER &&
		    candidate->enemy == plat)
		{
			trigger = candidate;
			break;
		}
	}
	if (!trigger)
		return false;

	/* Player hull is [-16,+16] in XY.  Two extra units clear linkentity's
	 * clip expansion and keep the next 25 ms touch query outside. */
	lo_x = trigger->absmin[0] - 18.0f;
	hi_x = trigger->absmax[0] + 18.0f;
	lo_y = trigger->absmin[1] - 18.0f;
	hi_y = trigger->absmax[1] + 18.0f;
	if (origin[0] <= lo_x || origin[0] >= hi_x ||
	    origin[1] <= lo_y || origin[1] >= hi_y)
		return false;

	distances[0] = origin[0] - lo_x;
	distances[1] = hi_x - origin[0];
	distances[2] = origin[1] - lo_y;
	distances[3] = hi_y - origin[1];
	for (i = 1; i < 4; i++)
		if (distances[i] < distances[side])
			side = i;
	VectorCopy(origin, wait);
	if (side == 0) wait[0] = lo_x - 2.0f;
	if (side == 1) wait[0] = hi_x + 2.0f;
	if (side == 2) wait[1] = lo_y - 2.0f;
	if (side == 3) wait[1] = hi_y + 2.0f;
	return true;
}

/* Client physics can clear groundentity before SV_Push translates a rider;
 * SG_RunFrame then observes the translated body with no ground pointer.  The
 * geometry after the pusher loop is authoritative: touching the top plane and
 * overlapping the platform footprint means this body is still being carried. */
qboolean SG_LiftRider(edict_t *plat, edict_t *body)
{
	float feet;

	if (!plat || !body || !plat->inuse || !body->inuse)
		return false;
	feet = body->s.origin[2] + body->mins[2];
	if (fabsf(feet - plat->absmax[2]) > 4.0f)
		return false;
	return body->absmax[0] > plat->absmin[0] + 1.0f &&
	       body->absmin[0] < plat->absmax[0] - 1.0f &&
	       body->absmax[1] > plat->absmin[1] + 1.0f &&
	       body->absmin[1] < plat->absmax[1] - 1.0f;
}

/* Resolve the exact fixed-point player origin resting at a raised lift's
 * centre. Inline brush-model maxs include collision padding, so arithmetic
 * `origin + maxs + 24` is one unit above the physical top on stock maps and
 * Pmove will not categorize it as grounded. Trace the production player hull
 * onto the actual linked platform instead, then quantize exactly as
 * ClientThink/SG_OraclePlace do. Generation and live activation share this
 * result so the egress witness begins from the state runtime establishes. */
qboolean SG_LiftTopRest(edict_t *plat, edict_t *passent, vec3_t rest)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t start, end;
	trace_t tr;
	int axis;

	if (!plat || !rest || !plat->inuse || !plat->classname ||
	    strcmp(plat->classname, "func_plat") != 0)
		return false;
	start[0] = plat->s.origin[0] + (plat->mins[0] + plat->maxs[0]) * 0.5f;
	start[1] = plat->s.origin[1] + (plat->mins[1] + plat->maxs[1]) * 0.5f;
	start[2] = plat->s.origin[2] + plat->maxs[2] + 24.0f;
	VectorCopy(start, end);
	end[2] -= 8.0f;
	tr = sg_host.trace(start, mins, maxs, end, passent, MASK_PLAYERSOLID);
	if (tr.startsolid || tr.allsolid || tr.fraction >= 1.0f ||
	    tr.ent != plat || tr.plane.normal[2] < 0.7f)
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		short fixed;

		if (!isfinite(tr.endpos[axis]) || tr.endpos[axis] < -4096.0f ||
		    tr.endpos[axis] > 4095.875f)
			return false;
		fixed = (short)(tr.endpos[axis] * 8.0f);
		rest[axis] = fixed * 0.125f;
	}
	return true;
}

void SG_TimerArm(float *stamp, float delay)
{
	*stamp = level.time + delay;
}

qboolean SG_TimerReady(float stamp)
{
	return level.time >= stamp;
}

qboolean SG_TimerReadyStrict(float stamp)
{
	return level.time > stamp;
}

qboolean SG_TimerPending(float stamp)
{
	return level.time < stamp;
}

float SG_TimerRemaining(float stamp)
{
	return stamp - level.time;
}

void SG_Mark(float *stamp)
{
	*stamp = level.time;
}

float SG_Age(float since)
{
	return level.time - since;
}

qboolean SG_AgeOver(float since, float span)
{
	return (level.time - since) > span;
}

qboolean SG_AgeAtLeast(float since, float span)
{
	return (level.time - since) >= span;
}

qboolean SG_AgeUnder(float since, float span)
{
	return (level.time - since) < span;
}
