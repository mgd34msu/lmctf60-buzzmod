#ifndef SG_CALLOUT_POLICY_H
#define SG_CALLOUT_POLICY_H

/*
 * A delayed line belongs to one concrete client life, not to the reusable
 * client slot that happened to queue it.  ctfid is reassigned whenever that
 * slot is admitted as a new client life.
 */
static inline int
SG_CalloutSpeakerCurrent(uint64_t expected_ctfid,
    uint64_t current_ctfid)
{
	return expected_ctfid != 0 && current_ctfid == expected_ctfid;
}

#endif
