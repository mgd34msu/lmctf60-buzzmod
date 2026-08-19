#ifndef SG_ITEM_POLICY_H
#define SG_ITEM_POLICY_H

typedef enum sg_item_pickup_disposition_e
{
	SG_ITEM_PICKUP_IGNORE = 0,
	SG_ITEM_PICKUP_COMMIT_ONLY,
	SG_ITEM_PICKUP_COMMIT_AND_COMMUNICATE
} sg_item_pickup_disposition_t;

/* A successful major static pickup always closes controller ownership.
 * Communication is a strictly downstream option, never pickup authority. */
static inline sg_item_pickup_disposition_t SG_ItemPickupDisposition(
	int successful, int dropped, int major, int communication_enabled)
{
	if (!successful || dropped || !major)
		return SG_ITEM_PICKUP_IGNORE;
	return communication_enabled ? SG_ITEM_PICKUP_COMMIT_AND_COMMUNICATE
	                             : SG_ITEM_PICKUP_COMMIT_ONLY;
}

#endif
