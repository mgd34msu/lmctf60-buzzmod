#ifndef SG_CARRIER_COVER_H
#define SG_CARRIER_COVER_H

#include "g_local.h"
#include "slipgate/sg_rune.h"

/* A carrier standoff may keep its current seed or take one proved ordinary
 * step. A clear point trace alone does not authorize direct movement. */
static inline int SG_CarrierCoverRouteAllowed(const rune_t *rune,
	int from_seed, int cover_seed)
{
	int link_index;
	int walked;

	if (!rune || rune->hdr.num_seeds <= 0 || rune->hdr.num_links < 0 ||
	    from_seed < 0 || from_seed >= rune->hdr.num_seeds ||
	    cover_seed < 0 || cover_seed >= rune->hdr.num_seeds)
		return 0;
	if (cover_seed == from_seed)
		return 1;
	if (!rune->links || !rune->first_link || !rune->next_link)
		return 0;

	link_index = rune->first_link[from_seed];
	for (walked = 0; link_index >= 0 && walked < rune->hdr.num_links; walked++)
	{
		const rune_link_t *link;

		if (link_index >= rune->hdr.num_links)
			return 0;
		link = &rune->links[link_index];
		if (link->from != from_seed)
			return 0;
		if (link->action == RL_RUN && link->to == cover_seed)
			return 1;
		link_index = rune->next_link[link_index];
	}
	return 0;
}

#endif /* SG_CARRIER_COVER_H */
