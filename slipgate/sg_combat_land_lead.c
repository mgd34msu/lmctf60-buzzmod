#include "slipgate/sg_combat_land_lead.h"

#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"

#define SG_ROCKET_MAX_DISTANCE 8000.0f

float SG_CombatRocketSpeed(void)
{
#ifdef WEAP_BALANCE_OK
	if (ctfflags && ((int)ctfflags->value & CTF_WEAP_BALANCE))
		return 750.0f;
#endif
	return 650.0f;
}

float SG_CombatRocketRadius(void)
{
#ifdef WEAP_BALANCE_OK
	if (ctfflags && ((int)ctfflags->value & CTF_WEAP_BALANCE))
		return 240.0f;
#endif
	return 120.0f;
}

static qboolean Combat_RocketSplashReaches(float distance)
{
#ifdef WEAP_BALANCE_OK
	if (ctfflags && ((int)ctfflags->value & CTF_WEAP_BALANCE))
		return distance < SG_CombatRocketRadius();
#endif
	return distance <= SG_CombatRocketRadius();
}

qboolean SG_CombatLandingAim(edict_t *enemy, vec3_t point,
                             sg_combat_landing_t *landing)
{
	vec3_t p0, p1, center_offset;
	float grav = sv_gravity ? sv_gravity->value : 800.0f;
	int seg;

	if (enemy->groundentity || enemy->waterlevel >= 2 ||
	    enemy->client->hookstate != 0 ||
	    enemy->client->hook || !sg_cv.landlead->value)
		return false;
	VectorAdd(enemy->mins, enemy->maxs, center_offset);
	VectorScale(center_offset, 0.5f, center_offset);
	VectorCopy(enemy->s.origin, p0);
	for (seg = 1; seg <= 30; seg++)
	{
		float t = 0.05f * (float)seg;
		trace_t tr;

		p1[0] = enemy->s.origin[0] + enemy->velocity[0] * t;
		p1[1] = enemy->s.origin[1] + enemy->velocity[1] * t;
		p1[2] = enemy->s.origin[2] + enemy->velocity[2] * t
		      - 0.5f * grav * t * t;
		tr = sg_host.trace(p0, enemy->mins, enemy->maxs, p1, enemy,
		                   MASK_PLAYERSOLID);
		if (tr.fraction < 1.0f)
		{
			float plane_gap;

			if (tr.startsolid || tr.allsolid || tr.plane.normal[2] < 0.7f)
				return false;
			landing->touch_time = 0.05f * ((float)(seg - 1) + tr.fraction);
			landing->gravity = grav;
			VectorAdd(tr.endpos, center_offset, point);
			plane_gap = DotProduct(point, tr.plane.normal) - tr.plane.dist;
			VectorMA(point, -plane_gap, tr.plane.normal, point);
			return true;
		}
		VectorCopy(p1, p0);
	}
	return false;
}

static qboolean Combat_PredictedCanDamage(const vec3_t impact,
                                          const vec3_t target,
                                          edict_t *enemy)
{
	static const float offset[5][2] = {
		{ 0.0f, 0.0f }, { 15.0f, 15.0f }, { 15.0f, -15.0f },
		{ -15.0f, 15.0f }, { -15.0f, -15.0f }
	};
	vec3_t dest;
	int i;

	for (i = 0; i < 5; i++)
	{
		VectorCopy(target, dest);
		dest[0] += offset[i][0];
		dest[1] += offset[i][1];
		if (sg_host.trace(impact, NULL, NULL, dest, enemy,
		                  MASK_SOLID).fraction >= 1.0f)
			return true;
	}
	return false;
}

qboolean SG_CombatLandingSplashClear(edict_t *self, edict_t *enemy,
                                     vec3_t muzzle, vec3_t shotdir,
                                     sg_combat_landing_t landing,
                                     float *projectile_time, vec3_t impact)
{
	vec3_t end, target_origin, target_center, center_offset, delta;
	trace_t tr;
	float speed = SG_CombatRocketSpeed();

	VectorMA(muzzle, SG_ROCKET_MAX_DISTANCE, shotdir, end);
	tr = sg_host.trace(muzzle, NULL, NULL, end, self, MASK_SHOT);
	if (tr.startsolid || tr.allsolid || tr.fraction >= 1.0f ||
	    (tr.surface && (tr.surface->flags & SURF_SKY)) ||
	    tr.ent != &g_edicts[0])
		return false;
	VectorSubtract(tr.endpos, muzzle, delta);
	*projectile_time = VectorLength(delta) / speed;
	if (*projectile_time > landing.touch_time)
		return false;
	VectorMA(enemy->s.origin, *projectile_time, enemy->velocity,
	         target_origin);
	target_origin[2] -= 0.5f * landing.gravity *
	                    *projectile_time * *projectile_time;
	VectorAdd(enemy->mins, enemy->maxs, center_offset);
	VectorMA(target_origin, 0.5f, center_offset, target_center);
	VectorSubtract(target_center, tr.endpos, delta);
	if (!Combat_RocketSplashReaches(VectorLength(delta)) ||
	    !Combat_PredictedCanDamage(tr.endpos, target_origin, enemy))
		return false;
	VectorCopy(tr.endpos, impact);
	return true;
}

#ifdef SG_COMBAT_AIM_TEST
int SG_CombatLandLeadTestSplashReaches(float distance)
{
	return Combat_RocketSplashReaches(distance) ? 1 : 0;
}
#endif
