#ifndef SG_CROWD_PASS_H
#define SG_CROWD_PASS_H

#include <stdint.h>

/*
 * Two head-on clients must choose the same relative turn sign.  Their view
 * headings differ by roughly 180 degrees, so the same signed yaw offset moves
 * them toward opposite world-space shoulders.  A per-client sign does the
 * reverse for many pairs: opposite relative signs send both bodies toward the
 * same shoulder and preserve the collision.
 *
 * CTF identities are unique for the current client lives.  Sorting the pair
 * before mixing makes the decision identical from either participant's point
 * of view, while rebinding it at respawn prevents a recycled client slot from
 * permanently owning one side.
 */
static int
SG_CrowdPassSide(uint64_t self_ctfid, uint64_t mate_ctfid)
{
	uint64_t lo;
	uint64_t hi;
	uint64_t mixed;

	if (self_ctfid == 0 || mate_ctfid == 0 || self_ctfid == mate_ctfid)
		return 0;

	lo = self_ctfid < mate_ctfid ? self_ctfid : mate_ctfid;
	hi = self_ctfid < mate_ctfid ? mate_ctfid : self_ctfid;
	mixed = lo * UINT64_C(0x9e3779b97f4a7c15);
	mixed ^= hi + UINT64_C(0x85ebca77c2b2ae63) + (mixed << 6) +
	         (mixed >> 2);
	mixed ^= mixed >> 33;
	mixed *= UINT64_C(0xff51afd7ed558ccd);
	mixed ^= mixed >> 33;

	return (mixed & UINT64_C(1)) ? 1 : -1;
}

#endif
