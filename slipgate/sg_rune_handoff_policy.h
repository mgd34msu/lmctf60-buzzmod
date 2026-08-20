#ifndef SG_RUNE_HANDOFF_POLICY_H
#define SG_RUNE_HANDOFF_POLICY_H

#include <math.h>

/* Rune delivery is optional preparation.  Outside coordinated offense an
 * attacker or organic escort may courier it.  Once the coordinator owns the
 * body, only the effective escort may converge or execute the irreversible
 * toss; pressure/recovery duties keep their objective and aim authority. */
static inline qboolean SG_RuneHandoffEligible(sg_role_t role,
	qboolean carrying, int ordered_role, qboolean strike_active,
	qboolean escort_mission)
{
	if ((carrying != false && carrying != true) ||
	    (strike_active != false && strike_active != true) ||
	    (escort_mission != false && escort_mission != true))
		return false;
	if (carrying || ordered_role >= 0)
		return false;
	if (strike_active)
		return escort_mission;
	return role == SG_ROLE_ATTACK || role == SG_ROLE_ESCORT;
}

static inline qboolean SG_RuneHandoffAllowsOptional(qboolean route_active)
{
	if (route_active != false && route_active != true)
		return false;
	return !route_active;
}

static inline qboolean SG_RuneHandoffEnemyPressure(qboolean route_active,
	qboolean enemy_pressure)
{
	if ((route_active != false && route_active != true) ||
	    (enemy_pressure != false && enemy_pressure != true))
		return false;
	return !route_active && enemy_pressure;
}

/* A belief selects the candidate slot, but the current CTF state authorizes
 * convergence and the irreversible toss.  Bounds, life, team and actual flag
 * possession and an empty rune slot are all required; a former carrier,
 * reused client slot, or physically occupied receiver is not a destination. */
static inline qboolean SG_RuneHandoffCarrierAllowed(int team, int maxclients,
	int believed_client, qboolean inuse, qboolean has_client, int health,
	qboolean dead, int carrier_team, qboolean carrying_flag,
	qboolean receiver_has_rune)
{
	if ((inuse != false && inuse != true) ||
	    (has_client != false && has_client != true) ||
	    (dead != false && dead != true) ||
	    (carrying_flag != false && carrying_flag != true) ||
	    (receiver_has_rune != false && receiver_has_rune != true))
		return false;
	return (team == CTF_TEAM_RED || team == CTF_TEAM_BLUE) &&
	       maxclients > 0 && believed_client >= 0 &&
	       believed_client < maxclients && inuse && has_client &&
	       health > 0 && !dead && carrier_team == team && carrying_flag &&
	       !receiver_has_rune;
}

/* ctf_TossEnt reads client->v_angle synchronously.  Produce the one flat yaw
 * that both that immediate boundary and the submitted command must share;
 * a vertical-only or malformed displacement cannot name a throw direction. */
static inline qboolean SG_RuneHandoffAim(float delta_x, float delta_y,
	float *yaw_out)
{
	if (!yaw_out || !isfinite(delta_x) || !isfinite(delta_y) ||
	    (delta_x == 0.0f && delta_y == 0.0f))
		return false;
	*yaw_out = atan2f(delta_y, delta_x) * 180.0f / (float)M_PI;
	return isfinite(*yaw_out);
}

/* Dropping the rune is irreversible.  Distance makes the teammate a courier
 * candidate; current visibility proves there is not a solid wall between the
 * physical toss and its intended receiver.  Convergence may continue while
 * hidden, but the item stays in hand until the path opens. */
static inline qboolean SG_RuneHandoffTossPathAllowed(float distance,
	qboolean visible)
{
	if ((visible != false && visible != true) || !isfinite(distance))
		return false;
	return visible && distance > 0.0f && distance < 400.0f;
}

#endif
