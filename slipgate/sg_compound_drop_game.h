/* Game boundary for the authenticated PREOPEN RL_DOOR_DROP controller. */
#ifndef SG_COMPOUND_DROP_GAME_H
#define SG_COMPOUND_DROP_GAME_H

#include "sg_compound_drop_live.h"

struct edict_s;
struct sg_bot_s;

qboolean SG_CompoundDropGameHost(struct sg_bot_s *bot,
	sg_compound_drop_live_host_t *host_out);
qboolean SG_CompoundDropGamePose(const struct edict_s *entity,
	sg_replay_pose_t *pose_out);
qboolean SG_CompoundDropGameObservation(struct sg_bot_s *bot,
	const struct edict_s *entity, sg_replay_observation_t *observation_out);
int SG_CompoundDropGameStageAuthenticatedProbe(int link_index);
void SG_CompoundDropGameDebugResult(struct sg_bot_s *bot,
	const char *stage, const sg_compound_drop_live_result_t *result,
	const sg_replay_pose_t *pose);

/* -1 means this callback does not belong to an active D_DROP transaction. */
int SG_CompoundDropGameAuthorizeTouch(struct sg_bot_s *bot,
	struct edict_s *source, struct edict_s *activator, int frame_serial);
int SG_CompoundDropGameAuthorizeActivation(struct sg_bot_s *bot,
	struct edict_s *source, struct edict_s *door_master,
	struct edict_s *activator, int frame_serial);
int SG_CompoundDropGameOwnsTargetDispatch(const struct sg_bot_s *bot,
	const struct edict_s *source);
void SG_CompoundDropGameOrphan(struct sg_bot_s *bot);

#endif /* SG_COMPOUND_DROP_GAME_H */
