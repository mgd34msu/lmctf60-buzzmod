/* sg_bot_orders.h -- humans ordering bots by team chat.
 *
 * "attack", "defend", "recover", "escort me", "escort <name>", "free",
 * addressed to one bot by name ("Arach defend") or to every bot on the
 * team ("all defend").  An order holds for a while and expires; a bot
 * reads its current order each frame when choosing a role. */
#ifndef SG_BOT_ORDERS_H
#define SG_BOT_ORDERS_H

/* A team-chat line from a human.  Returns 1 when it was an order. */
int SG_OrdersHear(edict_t *speaker, const char *text);

/* The role ordered for this bot, or -1; the escort target it was given. */
int SG_OrderedRole(edict_t *bot);
edict_t *SG_OrderEscortTarget(edict_t *bot);

void SG_OrdersReset(void);

#endif /* SG_BOT_ORDERS_H */
