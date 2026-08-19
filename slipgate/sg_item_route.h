#ifndef SG_ITEM_ROUTE_H
#define SG_ITEM_ROUTE_H

/* Weapon attraction is client-specific under WEAPONS_STAY.  Never fall back
 * to the class-wide field when this bot has no collectible weapon: that field
 * may be rooted entirely at pads Pickup_Weapon will reject.  Other item
 * classes retain their shared live fields. */
static inline const int *SG_ItemDetourField(int weapon_class,
	const int *class_field, const int *collectible_weapon_field)
{
	return weapon_class ? collectible_weapon_field : class_field;
}

#endif
