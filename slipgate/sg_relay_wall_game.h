/* sg_relay_wall_game.h -- engine boundary for authenticated relay walls. */
#ifndef SG_RELAY_WALL_GAME_H
#define SG_RELAY_WALL_GAME_H

struct edict_s;

typedef enum sg_relay_wall_game_target_result_e
{
	SG_RELAY_WALL_GAME_NOT_OWNED = 0,
	SG_RELAY_WALL_GAME_ALLOW_STOCK,
	SG_RELAY_WALL_GAME_HANDLED
} sg_relay_wall_game_target_result_t;

sg_relay_wall_game_target_result_t SG_RelayWallGameHandleTargets(
	struct edict_s *source, struct edict_s *activator);
void SG_RelayWallGameTagDelayedTarget(struct edict_s *source,
	struct edict_s *activator, struct edict_s *delayed);
int SG_RelayWallGameDelayedUseDurable(const struct edict_s *delayed);
int SG_RelayWallGameDelayedUseDeferred(const struct edict_s *delayed);
void SG_RelayWallGameRetireActivator(struct edict_s *delayed,
	struct edict_s *activator);
void SG_RelayWallGameEntityFreed(struct edict_s *entity);

#endif /* SG_RELAY_WALL_GAME_H */
