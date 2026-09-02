/* sg_bot_callout.h -- what a bot tells its team, and when.
 *
 * Team talk is for what a teammate cannot see: where the enemy carrier
 * was just seen, that our flag is gone, dropped, or back, that a bot has
 * their flag and which way it is coming, that it needs cover, what it is
 * doing now.  Places are named by the rune: how far a point is from each
 * flag in route time makes it our base, mid, or their base.  Every line is
 * rate-limited per bot and per team so a fight does not become a stream. */
#ifndef SG_BOT_CALLOUT_H
#define SG_BOT_CALLOUT_H

struct sg_bot_s;

void SG_BotCalloutReset(void);
/* Once per frame, after the team passes: flag state transitions. */
void SG_BotCalloutFrame(void);
/* A bot's role changed (the frame tells it what it is doing now). */
void SG_BotCalloutRole(struct sg_bot_s *bot, int role);
/* A bot's fight picked a new target it can see. */
void SG_BotCalloutSeen(edict_t *self, edict_t *enemy);
/* A bot saw a powerup standing: the team hears where. */
void SG_BotCalloutPowerup(edict_t *self, edict_t *item);
/* Where a point is, in the team's words. */
const char *SG_BotCalloutWhere(int team, const vec3_t point);

#endif /* SG_BOT_CALLOUT_H */
