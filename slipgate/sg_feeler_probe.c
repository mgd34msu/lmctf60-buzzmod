#include "slipgate/sg_feeler_probe.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_crowd_pass.h"
#include "slipgate/sg_team_collision.h"

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
