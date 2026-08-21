#include "slipgate/sg_feeler_probe.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_crowd_pass.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_team_collision.h"

float SG_MoveFeelerReach(const edict_t *e)
{
	float reach = 96.0f;

	if (sg_cv.fandense->value >= 2.0f)
	{
		float speed = sqrtf(e->velocity[0] * e->velocity[0] +
		    e->velocity[1] * e->velocity[1]);

		reach += speed > 274.0f ? (speed - 274.0f) * 0.5f : 0.0f;
		if (reach > 220.0f)
			reach = 220.0f;
	}
	return reach;
}

qboolean SG_CarrierJinkApplyIfClear(edict_t *e, int weave_sign,
	usercmd_t *cmd)
{
	vec3_t angles, forward, right, direction, endpoint;
	trace_t trace;
	short proposed_sidemove;

	proposed_sidemove = (short)(cmd->sidemove / 2 + weave_sign *
	    (cmd->forwardmove > 0 ? cmd->forwardmove : -cmd->forwardmove) / 2);
	angles[YAW] = SHORT2ANGLE((short)(cmd->angles[YAW] +
	    e->client->ps.pmove.delta_angles[YAW]));
	angles[PITCH] = SHORT2ANGLE((short)(cmd->angles[PITCH] +
	    e->client->ps.pmove.delta_angles[PITCH]));
	angles[ROLL] = SHORT2ANGLE((short)(cmd->angles[ROLL] +
	    e->client->ps.pmove.delta_angles[ROLL]));
	if (angles[PITCH] > 89.0f && angles[PITCH] < 180.0f)
		angles[PITCH] = 89.0f;
	else if (angles[PITCH] >= 180.0f && angles[PITCH] < 271.0f)
		angles[PITCH] = 271.0f;
	if (angles[PITCH] > 180.0f)
		angles[PITCH] -= 360.0f;
	angles[PITCH] /= 3.0f;
	AngleVectors(angles, forward, right, NULL);
	VectorScale(forward, cmd->forwardmove, direction);
	VectorMA(direction, proposed_sidemove, right, direction);
	direction[2] = 0.0f;
	if (VectorNormalize(direction) == 0.0f)
		return false;
	VectorMA(e->s.origin, SG_MoveFeelerReach(e), direction, endpoint);
	endpoint[2] += 8.0f;
	trace = sg_host.trace(e->s.origin, e->mins, e->maxs, endpoint, e,
	    MASK_PLAYERSOLID);
	if (trace.startsolid || trace.allsolid || !(trace.fraction >= 1.0f))
		return false;
	cmd->sidemove = proposed_sidemove;
	return true;
}

sg_feeler_probe_t SG_FeelerProbe(edict_t *e, int team, float yaw,
	float reach, qboolean allow_teammate_redirect)
{
	sg_feeler_probe_t result;
	vec3_t forward, endpoint;
	int attempt;

	result.yaw = yaw;
	result.teammate_blocked = false;
	for (attempt = 0; attempt < 2; attempt++)
	{
		float radians = result.yaw * (float)M_PI / 180.0f;
		int pass_side;

		forward[0] = cosf(radians);
		forward[1] = sinf(radians);
		forward[2] = 0.0f;
		endpoint[0] = e->s.origin[0] + reach * forward[0];
		endpoint[1] = e->s.origin[1] + reach * forward[1];
		endpoint[2] = e->s.origin[2] + 8.0f;
		result.trace = sg_host.trace(e->s.origin, e->mins, e->maxs,
		    endpoint, e, MASK_PLAYERSOLID);
		if (attempt || !allow_teammate_redirect ||
		    result.trace.fraction >= 1.0f || !result.trace.ent ||
		    !SG_TeammateBodyPassable(team,
		        result.trace.ent->client != NULL,
		        result.trace.ent->deadflag != 0,
		        result.trace.ent->client
		            ? result.trace.ent->client->ctf.teamnum : 0))
			break;
		result.teammate_blocked = true;
		pass_side = SG_CrowdPassSide(e->client->ctf.ctfid,
		    result.trace.ent->client->ctf.ctfid);
		if (!pass_side)
			break;
		result.yaw += 28.0f * (float)pass_side;
	}
	return result;
}
