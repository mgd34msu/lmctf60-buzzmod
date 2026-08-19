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

#endif
