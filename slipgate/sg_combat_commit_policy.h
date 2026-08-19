#ifndef SG_COMBAT_COMMIT_POLICY_H
#define SG_COMBAT_COMMIT_POLICY_H

/* Decide whether the held weapon may enter the current band's commitment
 * scan.  Mode 1 preserves the original law; mode 2 refuses only the spawn
 * blaster so a stocked real weapon can displace the fallback gun. */
static inline int SG_CombatCommitCandidateAllowed(float mode,
	int held_is_blaster, int stocked, int band_allowed)
{
	if ((held_is_blaster != 0 && held_is_blaster != 1) ||
	    (stocked != 0 && stocked != 1) ||
	    (band_allowed != 0 && band_allowed != 1) || mode == 0.0f)
		return 0;
	if (mode >= 2.0f && held_is_blaster)
		return 0;
	return stocked && band_allowed;
}

#endif
