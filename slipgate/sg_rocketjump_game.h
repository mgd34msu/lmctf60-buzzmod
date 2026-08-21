#ifndef SG_ROCKETJUMP_GAME_H
#define SG_ROCKETJUMP_GAME_H

struct edict_s;
struct sg_bot_s;
struct csurface_s;

int SG_RocketJumpGameOwns(const struct sg_bot_s *bot);
int SG_RocketJumpGameEmit(struct sg_bot_s *bot, int selected_link);
int SG_RocketJumpGameStageAuthenticatedProbe(int link_index);
void SG_RocketJumpGameFired(struct edict_s *owner,
	struct edict_s *projectile);
void SG_RocketJumpGameImpactBegin(struct edict_s *projectile,
	struct edict_s *other, const struct csurface_s *surface);
void SG_RocketJumpGameImpactEnd(struct edict_s *projectile);
void SG_RocketJumpGameProjectileFreed(struct edict_s *projectile);

#endif /* SG_ROCKETJUMP_GAME_H */
