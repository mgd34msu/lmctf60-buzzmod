/* sg_util.c -- see sg_util.h for the argument. */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_util.h"

edict_t *SG_OwnFlag(int team)
{
	edict_t *f = (team == CTF_TEAM_RED) ? redflag : blueflag;

	return (f && f->inuse && !f->owner) ? f : NULL;
}

edict_t *SG_EnemyFlag(int team)
{
	edict_t *f = (team == CTF_TEAM_RED) ? blueflag : redflag;

	return (f && f->inuse && !f->owner) ? f : NULL;
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
	tr = gi.trace(eye, NULL, NULL, tgt, e, MASK_OPAQUE);
	return tr.fraction >= 1.0f;
}
