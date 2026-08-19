#ifndef SG_SOUND_POLICY_H
#define SG_SOUND_POLICY_H

#include <math.h>

/* A client sound can name a place only when the client mixer applies a
 * finite, positive spatial attenuation.  ATTN_NONE announcements are audible
 * everywhere but carry no direction or range; treating their emitter edict as
 * a position leaks server-side location through capture, vote, and countdown
 * sounds. */
static inline int SG_SoundCarriesPosition(float volume, float attenuation)
{
	return isfinite(volume) && isfinite(attenuation) && volume > 0.0f &&
	       attenuation > 0.0f;
}

#endif
