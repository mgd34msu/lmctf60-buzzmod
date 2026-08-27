#ifndef SG_HOOK_ORACLE_H
#define SG_HOOK_ORACLE_H

#include "sg_chain_hook_replay.h"

struct edict_s;
struct sg_phantom_s;

typedef struct sg_hook_proof_s
{
	int pull_ms;
	int release_ms;
	int settle_arrival_ms;
	int settle_ms;
	byte exit_speed;
	pmove_state_t attach_pms;
	qboolean attach_groundentity;
	int attach_watertype;
	int attach_waterlevel;
	qboolean fling_release;
	sg_replay_reason_t reason;
	int failure_phase;
	int failure_elapsed_ms;
} sg_hook_proof_t;

typedef struct sg_chain_hook_proof_s
{
	sg_chain_hook_replay_spec_t replay;
	sg_hook_proof_t rope[SG_CHAIN_HOOK_ROPE_COUNT];
	int total_ms;
	byte exit_speed;
} sg_chain_hook_proof_t;

typedef qboolean (*sg_oracle_hook_monitor_fn)(void *context,
	const struct sg_phantom_s *phantom, const vec3_t before,
	const vec3_t after, int elapsed_ms);

void SG_AirHookLaunchCommand(const pmove_state_t *pmove, byte heading,
	byte frame, byte substep, usercmd_t *command);
qboolean SG_OracleAirHookLaunchFrame(const vec3_t seed_origin, byte heading,
	byte frame, struct sg_phantom_s *phantom);
qboolean SG_OracleAirHookCoastFrame(struct sg_phantom_s *phantom);

int SG_OracleHookStep(struct sg_phantom_s *phantom, const vec3_t bite,
	const vec3_t view_angles, int hand);
qboolean SG_OracleHookTraverse(struct sg_phantom_s *phantom,
	const vec3_t bite, const vec3_t destination, const vec3_t view_angles,
	int hand, int flight_ms, int settle_limit_ms, float old_frame_z,
	sg_hook_proof_t *proof, struct edict_s *passent, qboolean world_only);
qboolean SG_OracleHookTraverseMonitored(struct sg_phantom_s *phantom,
	const vec3_t bite, const vec3_t destination, const vec3_t view_angles,
	int hand, int flight_ms, int settle_limit_ms, float old_frame_z,
	sg_hook_proof_t *proof, struct edict_s *passent, qboolean world_only,
	sg_oracle_hook_monitor_fn monitor, void *monitor_context,
	qboolean fling_release, sg_hook_replay_terminal_t terminal);
qboolean SG_OracleChainHookTraverse(struct sg_phantom_s *phantom,
	const vec3_t control[SG_CHAIN_HOOK_ROPE_COUNT],
	const vec3_t destination, int hand, float old_frame_z,
	sg_chain_hook_proof_t *proof, struct edict_s *passent,
	qboolean world_only);
qboolean SG_OracleChainHookDiscover(struct sg_phantom_s *phantom,
	const vec3_t aim[SG_CHAIN_HOOK_ROPE_COUNT],
	const vec3_t destination, int hand, float old_frame_z,
	vec3_t control_out[SG_CHAIN_HOOK_ROPE_COUNT],
	sg_chain_hook_proof_t *proof, struct edict_s *passent,
	qboolean world_only);

#endif
