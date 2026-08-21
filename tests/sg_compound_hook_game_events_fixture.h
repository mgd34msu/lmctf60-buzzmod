#ifndef SG_COMPOUND_HOOK_GAME_EVENTS_FIXTURE_H
#define SG_COMPOUND_HOOK_GAME_EVENTS_FIXTURE_H

#include "../g_local.h"
#include "../slipgate/sg_compound_hook_game_events.h"

#define SG_BOT_H
#define SG_COMPOUND_HOOK_GAME_H
#define SG_MAXBOTS 2

typedef struct sg_bot_s
{
	edict_t *ent;
	qboolean active;
	sg_compound_guard_bot_t compound_guard;
	sg_compound_hook_live_state_t compound_hook_live;
	sg_compound_hook_game_events_t compound_hook_events;
} sg_bot_t;

extern sg_bot_t sg_bots[SG_MAXBOTS];

qboolean SG_CompoundHookGameHost(sg_bot_t *bot,
	sg_compound_hook_live_host_t *host_out);
qboolean SG_CompoundHookGamePose(const edict_t *entity,
	sg_replay_pose_t *pose_out);
qboolean SG_CompoundHookGameObservation(sg_bot_t *bot,
	const edict_t *entity, sg_replay_observation_t *observation_out);
qboolean SG_CompoundHookGameAtTop(sg_bot_t *bot,
	const sg_compound_hook_live_snapshot_t *snapshot);

#endif
