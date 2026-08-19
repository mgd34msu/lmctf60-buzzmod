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

/* Speculative fire spends a long cadence, so client-slot order must never
 * decide which of several earned sounds receives it.  Only current observations
 * enter the ranking; the newest safe region wins, then the nearer region, with
 * client number retained solely as a stable final tie-breaker. */
static inline int SG_SoundFireObservationFresh(float now, float observed_at,
	float freshness)
{
	return isfinite(now) && isfinite(observed_at) && isfinite(freshness) &&
	       freshness > 0.0f && observed_at >= 0.0f && observed_at <= now &&
	       now - observed_at < freshness;
}

static inline int SG_SoundFireCandidateBetter(float candidate_time,
	float candidate_range, int candidate_client, int have_best,
	float best_time, float best_range, int best_client)
{
	if (!isfinite(candidate_time) || candidate_time < 0.0f ||
	    !isfinite(candidate_range) || candidate_range < 0.0f ||
	    candidate_client < 0)
		return 0;
	if (!have_best)
		return 1;
	if (!isfinite(best_time) || best_time < 0.0f ||
	    !isfinite(best_range) || best_range < 0.0f || best_client < 0)
		return 1;
	if (candidate_time != best_time)
		return candidate_time > best_time;
	if (candidate_range != best_range)
		return candidate_range < best_range;
	return candidate_client < best_client;
}

#endif
