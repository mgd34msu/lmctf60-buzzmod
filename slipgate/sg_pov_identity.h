#ifndef SG_POV_IDENTITY_H
#define SG_POV_IDENTITY_H

#include "../g_local.h"

struct sg_bot_s;

/* sg_client.c is the sole production authority that calls assign/reset.
 * Queries expose no roster storage and accept only exact active ownership. */
qboolean SG_BotPOVInstanceAssign(struct sg_bot_s *bot);
void SG_BotPOVInstanceReset(struct sg_bot_s *bot);
qboolean SG_BotPOVIdentity(edict_t *ent, int *slot_out,
	unsigned long long *instance_out);
edict_t *SG_BotPOVResolve(int slot, unsigned long long instance_token);

#endif
