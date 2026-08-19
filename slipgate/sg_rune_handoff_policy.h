#ifndef SG_RUNE_HANDOFF_POLICY_H
#define SG_RUNE_HANDOFF_POLICY_H

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

#endif
