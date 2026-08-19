#ifndef SG_HEALTH_PICKUP_H
#define SG_HEALTH_PICKUP_H

/* Shared by route admission and Pickup_Health.  HEALTH_IGNORE_MAX items are
 * legal overheal; ordinary boxes are legal only below the client's actual
 * maximum health. */
static inline int SG_HealthPickupAllowed(int health, int max_health,
	int ignore_max)
{
	return ignore_max || health < max_health;
}

#endif
