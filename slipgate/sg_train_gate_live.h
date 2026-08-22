/* sg_train_gate_live.h -- host-free train-gate transaction reducer. */
#ifndef SG_TRAIN_GATE_LIVE_H
#define SG_TRAIN_GATE_LIVE_H

#include <stdint.h>

typedef enum sg_train_gate_phase_e
{
	SG_TRAIN_GATE_IDLE = 0,
	SG_TRAIN_GATE_APPROACH,
	SG_TRAIN_GATE_DISPATCH,
	SG_TRAIN_GATE_OPENING,
	SG_TRAIN_GATE_EGRESS,
	SG_TRAIN_GATE_COMPLETE,
	SG_TRAIN_GATE_FAILED
} sg_train_gate_phase_t;

typedef enum sg_train_gate_pose_e
{
	SG_TRAIN_GATE_POSE_CLOSED = 0,
	SG_TRAIN_GATE_POSE_OPENING,
	SG_TRAIN_GATE_POSE_OPEN,
	SG_TRAIN_GATE_POSE_CLOSING,
	SG_TRAIN_GATE_POSE_INTERRUPTED,
	SG_TRAIN_GATE_POSE_INVALID
} sg_train_gate_pose_t;

typedef enum sg_train_gate_activation_e
{
	SG_TRAIN_GATE_ACTIVATION_TOUCH = 0,
	SG_TRAIN_GATE_ACTIVATION_SHOOT
} sg_train_gate_activation_t;

typedef enum sg_train_gate_side_e
{
	SG_TRAIN_GATE_SIDE_NONE = 0,
	SG_TRAIN_GATE_SIDE_X_MIN,
	SG_TRAIN_GATE_SIDE_X_MAX,
	SG_TRAIN_GATE_SIDE_Y_MIN,
	SG_TRAIN_GATE_SIDE_Y_MAX,
	SG_TRAIN_GATE_SIDE_Z_MIN,
	SG_TRAIN_GATE_SIDE_Z_MAX
} sg_train_gate_side_t;

typedef enum sg_train_gate_command_e
{
	SG_TRAIN_GATE_COMMAND_ZERO = 0,
	SG_TRAIN_GATE_COMMAND_TO_BUTTON,
	SG_TRAIN_GATE_COMMAND_EQUIP,
	SG_TRAIN_GATE_COMMAND_AIM_BUTTON,
	SG_TRAIN_GATE_COMMAND_SHOOT_BUTTON,
	SG_TRAIN_GATE_COMMAND_TO_EGRESS
} sg_train_gate_command_t;

typedef struct sg_train_gate_witness_s
{
	uint32_t link_index;
	uint32_t button_key;
	uint32_t train_key;
	uint32_t closed_corner_key;
	uint32_t open_corner_key;
	sg_train_gate_activation_t activation;
	int16_t source_q8[3];
	int16_t button_q8[3];
	int16_t destination_q8[3];
	uint16_t opening_bound_ms;
} sg_train_gate_witness_t;

typedef struct sg_train_gate_observation_s
{
	sg_train_gate_pose_t pose;
	uint8_t alive;
	uint8_t supported;
	uint8_t dry;
	uint8_t binding_current;
	uint8_t body_clear;
	uint8_t arrived;
	uint8_t weapon_ready;
	uint8_t aim_contact_current;
	uint8_t line_of_fire_clear;
	uint8_t button_touch_count;
	uint8_t button_shot_count;
	uint8_t target_dispatch_count;
	uint8_t train_use_count;
} sg_train_gate_observation_t;

typedef struct sg_train_gate_state_s
{
	sg_train_gate_witness_t witness;
	sg_train_gate_phase_t phase;
	uint32_t opening_elapsed_ms;
	uint8_t button_touch_count;
	uint8_t button_shot_count;
	uint8_t target_dispatch_count;
	uint8_t train_use_count;
	uint8_t shot_requested;
} sg_train_gate_state_t;

int SG_TrainGateLiveBegin(sg_train_gate_state_t *state,
	const sg_train_gate_witness_t *witness,
	const sg_train_gate_observation_t *observation);
sg_train_gate_command_t SG_TrainGateLiveStep(sg_train_gate_state_t *state,
	const sg_train_gate_observation_t *observation, uint16_t step_ms);
sg_train_gate_side_t SG_TrainGateSweepSide(const float bounds_mins[3],
	const float bounds_maxs[3], const float sweep_mins[3],
	const float sweep_maxs[3]);
sg_train_gate_side_t SG_TrainGateSweepAxisSide(const float bounds_mins[3],
	const float bounds_maxs[3], const float sweep_mins[3],
	const float sweep_maxs[3], unsigned int axis);
sg_train_gate_side_t SG_TrainGateUniqueSourceSide(uint32_t side_mask);
int SG_TrainGateUniquePassageAxis(uint32_t axis_mask,
	unsigned int motion_axis);
sg_train_gate_side_t SG_TrainGateOppositeSide(sg_train_gate_side_t side);

#endif /* SG_TRAIN_GATE_LIVE_H */
