#ifndef SG_ARMOR_PICKUP_H
#define SG_ARMOR_PICKUP_H

/* Pure admission half of Pickup_Armor. Shards, an empty armor slot, and an
 * upgrade always add protection. A lateral/downgrade pickup is useful only
 * when its salvaged count raises the held total below the current armor cap. */
static inline int SG_ArmorPickupAllowed(int shard, int has_old,
	float old_protection, int old_count, int old_max,
	float new_protection, int new_base)
{
	int salvage_count, new_count;

	if (shard || !has_old || new_protection > old_protection)
		return 1;
	if (!(old_protection > 0.0f) || !(new_protection >= 0.0f) ||
	    new_base < 0 || old_max < 0)
		return 0;
	salvage_count = (int)((new_protection / old_protection) *
	    (float)new_base);
	new_count = old_count + salvage_count;
	if (new_count > old_max)
		new_count = old_max;
	return old_count < new_count;
}

#endif
