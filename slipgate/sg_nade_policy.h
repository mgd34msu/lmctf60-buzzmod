#ifndef SG_NADE_POLICY_H
#define SG_NADE_POLICY_H

#include <math.h>

/* Only a live cook transaction may keep the trigger held.  -1 means the
 * ballistic solve is not ready yet and may continue cooking; values below
 * -1.5 are the production blocked-arc sentinel and must never re-arm attack. */
static inline int SG_NadeCookShouldHold(int nade_phase,
	float timer_remaining, float flight_time)
{
	if (nade_phase != 2 || !isfinite(timer_remaining) ||
	    !isfinite(flight_time) || timer_remaining <= 0.6f ||
	    flight_time < -1.5f)
		return 0;
	return flight_time < 0.0f ||
	    timer_remaining - 0.2f > flight_time - 0.15f;
}

#endif /* SG_NADE_POLICY_H */
