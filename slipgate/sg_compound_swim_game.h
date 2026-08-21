#ifndef SG_COMPOUND_SWIM_GAME_H
#define SG_COMPOUND_SWIM_GAME_H

struct edict_s;
struct sg_bot_s;

int SG_CompoundSwimGameEmit(struct sg_bot_s *bot, int link_index);
int SG_CompoundSwimGameStageAuthenticatedProbe(int link_index);
int SG_CompoundSwimGameOwns(const struct sg_bot_s *bot);
int SG_CompoundSwimGameAuthorizeTouch(struct edict_s *trigger,
	struct edict_s *activator);
int SG_CompoundSwimGameAuthorizeActivation(struct edict_s *trigger,
	struct edict_s *mover, struct edict_s *activator);
void SG_CompoundSwimGameClientRetired(struct edict_s *client);
void SG_CompoundSwimGameOrphaned(struct sg_bot_s *bot);
void SG_CompoundSwimGameReset(struct sg_bot_s *bot);

#endif
