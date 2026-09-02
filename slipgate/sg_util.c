/* sg_util.c -- team, flag, sight, and timer helpers shared by the bot
 * units.  Engine facts only. */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_util.h"

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
