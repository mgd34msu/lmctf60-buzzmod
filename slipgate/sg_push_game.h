#ifndef SG_PUSH_GAME_H
#define SG_PUSH_GAME_H

struct edict_s;
struct sg_bot_s;

int SG_PushGameOwns(const struct sg_bot_s *bot);
int SG_PushGameEmit(struct sg_bot_s *bot, int selected_link);
void SG_PushGameTouched(struct edict_s *trigger, struct edict_s *entity);

#endif
