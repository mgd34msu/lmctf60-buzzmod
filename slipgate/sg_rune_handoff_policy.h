#ifndef SG_RUNE_HANDOFF_POLICY_H
#define SG_RUNE_HANDOFF_POLICY_H

/* Rune delivery is an optional attacker/organic-escort job.  Recovery and
 * defense are essential objective owners, a carrier cannot hand off to
 * itself, and an explicit human order remains the complete role authority. */
static inline qboolean SG_RuneHandoffEligible(sg_role_t role,
	qboolean carrying, int ordered_role)
{
	if (carrying || ordered_role >= 0)
		return false;
	return role == SG_ROLE_ATTACK || role == SG_ROLE_ESCORT;
}

#endif
