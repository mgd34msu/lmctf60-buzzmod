#ifndef SG_HOOK_GAME_H
#define SG_HOOK_GAME_H

#include "../g_local.h"

typedef struct sg_bot_s sg_bot_t;

typedef enum sg_hook_game_proof_result_e
{
	SG_HOOK_GAME_PROOF_FAIL = 0,
	SG_HOOK_GAME_PROOF_OK,
	SG_HOOK_GAME_PROOF_BUSY
} sg_hook_game_proof_result_t;

/* Graph-hook game boundary. Route selection and initial aim staging remain
 * owned by sg_move; this owner authenticates and executes ordinary and chain
 * hook lifecycles after the irreversible fire boundaries. */
qboolean SG_HookGameReleaseReady(edict_t *entity, const sg_bot_t *bot);
void SG_HookGameRelease(edict_t *entity, sg_bot_t *bot,
	qboolean *cut_in_step);
void SG_HookGameFail(edict_t *entity, sg_bot_t *bot, float shelf_seconds);
void SG_HookGameFailDetail(edict_t *entity, sg_bot_t *bot,
	float shelf_seconds, const char *detail);
void SG_HookGameDisciplineRetire(edict_t *entity, sg_bot_t *bot,
	int link_index, float shelf_seconds, qboolean failure,
	const char *reason, int from_goal, int to_goal);
sg_hook_game_proof_result_t SG_HookGameOnlineProof(edict_t *entity,
	sg_bot_t *bot, float proved_distance, float *flight_distance);
qboolean SG_HookGameBeginAfterFire(edict_t *entity, sg_bot_t *bot,
	int link_index, float flight_distance);
sg_hook_game_proof_result_t SG_ChainHookGameOnlineProof(edict_t *entity,
	sg_bot_t *bot);
qboolean SG_ChainHookGameBeginAfterFire(edict_t *entity, sg_bot_t *bot,
	int link_index);
void SG_ChainHookGameReset(sg_bot_t *bot);
void SG_ChainHookGameStage(sg_bot_t *bot, int link_index);
qboolean SG_ChainHookGamePrepared(const sg_bot_t *bot, int link_index);
qboolean SG_ChainHookGameOwns(const sg_bot_t *bot);

/* Active proved hook phases own their command independently of field
 * localization. Returns true when this consumed the server frame. */
qboolean SG_HookActiveFrame(sg_bot_t *bot, edict_t *entity);

/* Observe the one production end-frame pull for an ordinary reducer-owned
 * graph hook. Other clients retain the historical engine path. */
void SG_HookLiveEndFrame(edict_t *entity);

/* Capability contract shared by link selection and execution. */
qboolean SG_HookOffhandReady(edict_t *entity);

#endif /* SG_HOOK_GAME_H */
