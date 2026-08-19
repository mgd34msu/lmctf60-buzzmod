#ifndef SG_ITEM_ROUTE_H
#define SG_ITEM_ROUTE_H

#define SG_ITEM_ROUTE_POWERUP_LEAD_SECONDS 4.0f

/* Weapon attraction is client-specific under WEAPONS_STAY.  Never fall back
 * to the class-wide field when this bot has no collectible weapon: that field
 * may be rooted entirely at pads Pickup_Weapon will reject.  Other item
 * classes retain their shared live fields. */
static inline const int *SG_ItemDetourField(int has_frame_context,
	const int *class_field, const int *client_field)
{
	return has_frame_context ? client_field : class_field;
}

/* Identity-bearing fields are safe to price only when the exact entity is
 * physically collectible by this client.  Keep the admitted class set closed:
 * adding another per-item field must also add its pickup law here. */
static inline int SG_IdentityItemRouteAdmission(int cls,
	int physical_pickup_eligible)
{
	if (physical_pickup_eligible != 0 && physical_pickup_eligible != 1)
		return 0;
	if (cls != SG_FC_POWERUP && cls != SG_FC_RUNE)
		return 0;
	return physical_pickup_eligible;
}

/* Exact per-item routes need the matching team's belief for that same entity.
 * A powerup with an earned clock may be approached during the established
 * four-second camping window; runes have no inferred respawn location. */
static inline int SG_IdentityItemBeliefAdmission(int cls, int believed_up,
	int respawn_within_lead)
{
	if ((believed_up != 0 && believed_up != 1) ||
	    (respawn_within_lead != 0 && respawn_within_lead != 1) ||
	    (cls != SG_FC_POWERUP && cls != SG_FC_RUNE))
		return 0;
	return believed_up ||
	       (cls == SG_FC_POWERUP && respawn_within_lead);
}

#endif
