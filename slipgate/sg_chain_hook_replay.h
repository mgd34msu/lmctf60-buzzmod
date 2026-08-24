/* Pure two-rope coordinator for the bot-only RL_CHAIN_HOOK action. */
#ifndef SG_CHAIN_HOOK_REPLAY_H
#define SG_CHAIN_HOOK_REPLAY_H

#include "sg_replay.h"

#define SG_CHAIN_HOOK_ROPE_COUNT 2

typedef enum sg_chain_hook_replay_phase_e
{
	SG_CHAIN_HOOK_REPLAY_FIRST_ROPE = 0,
	SG_CHAIN_HOOK_REPLAY_SECOND_AIM,
	SG_CHAIN_HOOK_REPLAY_WAIT_SECOND_FIRE,
	SG_CHAIN_HOOK_REPLAY_SECOND_ROPE,
	SG_CHAIN_HOOK_REPLAY_COMPLETE,
	SG_CHAIN_HOOK_REPLAY_FAILED
} sg_chain_hook_replay_phase_t;

typedef enum sg_chain_hook_replay_effect_e
{
	SG_CHAIN_HOOK_REPLAY_EFFECT_NONE = 0,
	SG_CHAIN_HOOK_REPLAY_EFFECT_RELEASE,
	SG_CHAIN_HOOK_REPLAY_EFFECT_FIRE_NEXT
} sg_chain_hook_replay_effect_t;

typedef enum sg_chain_hook_replay_event_e
{
	SG_CHAIN_HOOK_REPLAY_EVENT_ATTACHED = 0,
	SG_CHAIN_HOOK_REPLAY_EVENT_PULL_APPLIED,
	SG_CHAIN_HOOK_REPLAY_EVENT_RELEASE_APPLIED,
	SG_CHAIN_HOOK_REPLAY_EVENT_NEXT_FIRED
} sg_chain_hook_replay_event_t;

typedef struct sg_chain_hook_checkpoint_s
{
	sg_replay_pose_t pose;
	float old_frame_z;
} sg_chain_hook_checkpoint_t;

typedef struct sg_chain_hook_replay_spec_s
{
	sg_hook_replay_spec_t rope[SG_CHAIN_HOOK_ROPE_COUNT];
	sg_chain_hook_checkpoint_t refire_start;
	sg_chain_hook_checkpoint_t second_fire;
} sg_chain_hook_replay_spec_t;

typedef struct sg_chain_hook_replay_state_s
{
	sg_chain_hook_replay_spec_t spec;
	sg_hook_replay_state_t rope;
	sg_chain_hook_replay_phase_t phase;
	sg_replay_status_t status;
	sg_replay_reason_t reason;
	int aim_step;
	qboolean aim_step_pending;
} sg_chain_hook_replay_state_t;

typedef struct sg_chain_hook_replay_result_s
{
	sg_replay_status_t status;
	sg_replay_reason_t reason;
	sg_chain_hook_replay_effect_t effect;
} sg_chain_hook_replay_result_t;

sg_chain_hook_replay_result_t SG_ChainHookReplayBegin(
	sg_chain_hook_replay_state_t *state,
	const sg_chain_hook_replay_spec_t *spec,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z);
sg_chain_hook_replay_result_t SG_ChainHookReplayPreStep(
	sg_chain_hook_replay_state_t *state, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command);
sg_chain_hook_replay_result_t SG_ChainHookReplayPostStep(
	sg_chain_hook_replay_state_t *state, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z);
sg_chain_hook_replay_result_t SG_ChainHookReplayEvent(
	sg_chain_hook_replay_state_t *state, sg_chain_hook_replay_event_t event,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z);

#endif
