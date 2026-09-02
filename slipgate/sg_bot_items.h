/* sg_bot_items.h -- items worth a detour.
 *
 * Every item on the map that can be picked up now is worth something to
 * this bot from its own state: health it lacks, armor it lacks, a weapon
 * it does not own, ammo it is short of, a powerup.  The detour it costs
 * is read off the goal's own field: the walk to the item plus the field's
 * cost from the item on, less the field's cost from where the bot stands.
 * The item whose worth per second of detour is highest, above a floor,
 * becomes the destination until it is taken or gone. */
#ifndef SG_BOT_ITEMS_H
#define SG_BOT_ITEMS_H

#include "sg_bot.h"
#include "sg_rune_field.h"

/* Chooses the item to detour for, if any: writes its position and returns
 * 1.  goal_field is the field toward the bot's goal; bot->cell is set. */
/* The most worthwhile pickup standing within radius (flat) of a point, for
 * this body: a posted defender stocks up around its flag.  0 when none. */
int SG_BotItemNear(sg_bot_t *bot, const vec3_t centre, float radius, vec3_t point_out);

int SG_BotItemDetour(sg_bot_t *bot, const sg_rune_field_t *goal_field,
	vec3_t point_out);

#endif /* SG_BOT_ITEMS_H */
