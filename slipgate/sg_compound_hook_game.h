#ifndef SG_COMPOUND_HOOK_GAME_H
#define SG_COMPOUND_HOOK_GAME_H

#include "sg_compound_guard.h"
#include "sg_compound_hook_live.h"

struct edict_s;
struct sg_bot_s;

typedef struct sg_compound_hook_game_state_s
{
	sg_compound_publication_angle_bias_t angle_bias;
	qboolean angle_bias_valid;
} sg_compound_hook_game_state_t;

typedef enum sg_compound_hook_game_authorization_e
{
	SG_COMPOUND_HOOK_GAME_BYPASS = -1,
	SG_COMPOUND_HOOK_GAME_DENIED = 0,
	SG_COMPOUND_HOOK_GAME_ACCEPTED = 1
} sg_compound_hook_game_authorization_t;

void SG_CompoundHookGameReset(struct sg_bot_s *bot);
qboolean SG_CompoundHookGameHost(struct sg_bot_s *bot,
	sg_compound_hook_live_host_t *host_out);
qboolean SG_CompoundHookGamePose(const struct edict_s *entity,
	sg_replay_pose_t *pose_out);
qboolean SG_CompoundHookGameObservation(struct sg_bot_s *bot,
	const struct edict_s *entity,
	sg_replay_observation_t *observation_out);
qboolean SG_CompoundHookGameTakeObservation(struct sg_bot_s *bot,
	const struct edict_s *entity,
	sg_replay_observation_t *observation_out);
qboolean SG_CompoundHookGameIdleAdmission(const struct sg_bot_s *bot);
sg_compound_hook_live_result_t SG_CompoundHookGameBegin(
	struct sg_bot_s *bot, uint32_t link_index);
qboolean SG_CompoundHookGameCurrent(struct sg_bot_s *bot,
	const sg_compound_hook_live_snapshot_t *snapshot,
	const sg_compound_publication_binding_t **binding_out,
	const sg_compound_world_preopen_t **mechanism_out,
	struct edict_s **member_out);
qboolean SG_CompoundHookGameAtTop(struct sg_bot_s *bot,
	const sg_compound_hook_live_snapshot_t *snapshot);

sg_compound_hook_game_authorization_t SG_CompoundHookGameAuthorizeTouch(
	struct sg_bot_s *bot,
	struct edict_s *source, struct edict_s *activator, int frame_serial);
sg_compound_hook_game_authorization_t SG_CompoundHookGameAuthorizeActivation(
	struct sg_bot_s *bot,
	struct edict_s *source, struct edict_s *door_master,
	struct edict_s *activator, int frame_serial);
sg_compound_guard_result_t SG_CompoundHookGameOrphan(
	struct sg_bot_s *bot);
sg_compound_hook_live_result_t SG_CompoundHookGameRecoverOwnedFailure(
	struct sg_bot_s *bot, usercmd_t *same_slot_command);

#endif
