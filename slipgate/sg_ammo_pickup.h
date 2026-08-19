#ifndef SG_AMMO_PICKUP_H
#define SG_AMMO_PICKUP_H

static inline int SG_AmmoPickupAllowed(int count, int maximum)
{
	return maximum >= 0 && count < maximum;
}

#endif
