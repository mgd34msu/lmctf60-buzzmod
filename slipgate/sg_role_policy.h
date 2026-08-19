/* sg_role_policy.h -- pure roster laws shared by role assignment and tests. */
#ifndef SG_ROLE_POLICY_H
#define SG_ROLE_POLICY_H

/* Rank one slot among the currently live same-team bodies.  A dead/missing
 * self ranks after every live body, so it cannot reserve a live defender post
 * while waiting to respawn. */
static int SG_RoleLiveRank(const unsigned char *eligible, int count,
	int self, int *live_count)
{
	int rank = 0;
	int live = 0;
	int index;

	if (live_count)
		*live_count = 0;
	if (!eligible || count <= 0 || self < 0 || self >= count)
		return -1;
	for (index = 0; index < count; index++)
	{
		if (index == self)
			rank = live;
		if (!eligible[index])
			continue;
		live++;
	}
	if (!eligible[self])
		rank = live;
	if (live_count)
		*live_count = live;
	return rank;
}

#endif /* SG_ROLE_POLICY_H */
