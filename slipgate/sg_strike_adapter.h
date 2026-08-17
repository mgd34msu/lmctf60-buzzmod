/* sg_strike_adapter.h -- game-frame boundary for the strike coordinator. */
#ifndef SG_STRIKE_ADAPTER_H
#define SG_STRIKE_ADAPTER_H

#include "slipgate/sg_strike.h"

/* The adapter owns the two team reducers and the previous immutable frame
 * needed to turn authoritative flag/carrier edges into reducer events.  A
 * caller must fill both frames before calling BeginFrame; the adapter copies
 * them before stepping either team, so the later serial bot-think loop cannot
 * feed one team's mutations into the other team's input. */
typedef struct sg_strike_adapter_s
{
	sg_strike_team_t team[2];
	sg_strike_frame_t frame[2];
	sg_strike_frame_t previous[2];
	unsigned char previous_valid[2];
	unsigned frame_serial;
} sg_strike_adapter_t;

void SG_StrikeAdapterReset(sg_strike_adapter_t *adapter);

/* Advance both teams exactly once from the supplied pre-serial snapshots.
 * Returns zero if either reducer rejects its frame; neither team is published
 * in that case. */
int SG_StrikeAdapterBeginFrame(sg_strike_adapter_t *adapter,
	const sg_strike_frame_t frames[2]);

const sg_strike_team_t *SG_StrikeAdapterTeam(
	const sg_strike_adapter_t *adapter, int team_index);
const sg_strike_frame_t *SG_StrikeAdapterFrame(
	const sg_strike_adapter_t *adapter, int team_index);

/* A recycled SG ownership slot is a new client identity.  Forget only that
 * slot's retained same-life deadline; ordinary death/presence gaps must not
 * call this function because the core deliberately preserves their deadline.
 */
void SG_StrikeAdapterForgetSlot(sg_strike_adapter_t *adapter, int slot);

#endif /* SG_STRIKE_ADAPTER_H */
