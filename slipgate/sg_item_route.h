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

/* Ammo worth is priced from the weapon actually held.  The route must name
 * that weapon's ammo pool too; capacity in an unrelated pool is not utility. */
static inline int SG_AmmoRouteAdmission(int candidate_tag, int held_tag,
	int physical_pickup_eligible)
{
	if (candidate_tag < 0 || held_tag < 0 ||
	    (physical_pickup_eligible != 0 && physical_pickup_eligible != 1))
		return 0;
	return physical_pickup_eligible && candidate_tag == held_tag;
}

/* The generic weapon class is priced as an upgrade from the best weapon the
 * bot already owns.  A duplicate or lower-tier pad may be physically
 * collectible when WEAPONS_STAY is off, but it cannot deliver that utility.
 * Defense supply has a different purpose (arming a blaster-only defender) and
 * deliberately does not use this admission law. */
static inline int SG_WeaponUpgradeRouteAdmission(int current_tier,
	int candidate_tier, int physical_pickup_eligible)
{
	if (current_tier < 1 || candidate_tier < 1 ||
	    (physical_pickup_eligible != 0 && physical_pickup_eligible != 1))
		return 0;
	return physical_pickup_eligible && candidate_tier > current_tier;
}

#endif
