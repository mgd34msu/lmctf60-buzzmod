/* sg_shoot_door_live.h -- host-free shoot-door transaction reducer. */
#ifndef SG_SHOOT_DOOR_LIVE_H
#define SG_SHOOT_DOOR_LIVE_H

#include <stdint.h>

#define SG_SHOOT_DOOR_PLAYER_WIDTH 32U
#define SG_SHOOT_DOOR_JUMP_APPROACH_Q8 \
	(2U * SG_SHOOT_DOOR_PLAYER_WIDTH * 8U)

typedef enum sg_shoot_door_phase_e
{
	SG_SHOOT_DOOR_IDLE = 0,
	SG_SHOOT_DOOR_ACTIVATE,
	SG_SHOOT_DOOR_OPENING,
	SG_SHOOT_DOOR_CROSS,
	SG_SHOOT_DOOR_COMPLETE,
	SG_SHOOT_DOOR_FAILED
} sg_shoot_door_phase_t;

typedef enum sg_shoot_door_side_e
{
	SG_SHOOT_DOOR_SIDE_NONE = 0,
	SG_SHOOT_DOOR_SIDE_MIN,
	SG_SHOOT_DOOR_SIDE_MAX
} sg_shoot_door_side_t;

typedef enum sg_shoot_door_command_e
{
	SG_SHOOT_DOOR_COMMAND_ZERO = 0,
	SG_SHOOT_DOOR_COMMAND_EQUIP,
	SG_SHOOT_DOOR_COMMAND_AIM,
	SG_SHOOT_DOOR_COMMAND_SHOOT,
	SG_SHOOT_DOOR_COMMAND_TO_DESTINATION,
	SG_SHOOT_DOOR_COMMAND_TO_DESTINATION_JUMP
} sg_shoot_door_command_t;

typedef struct sg_shoot_door_witness_s
{
	uint32_t link_index;
	uint32_t master_key;
	uint16_t expected_members;
	uint16_t opening_bound_ms;
	uint8_t passage_axis;
	sg_shoot_door_side_t source_side;
} sg_shoot_door_witness_t;

typedef struct sg_shoot_door_observation_s
{
	uint8_t alive;
	uint8_t supported;
	uint8_t dry;
	uint8_t binding_current;
	uint8_t team_closed;
	uint8_t team_opening;
	uint8_t team_open;
	uint8_t body_clear;
	uint8_t arrived;
	uint8_t weapon_ready;
	uint8_t aim_contact_current;
	uint8_t line_of_fire_clear;
	uint8_t shot_count;
	sg_shoot_door_side_t body_side;
	uint16_t hull_to_sweep_gap_q8;
} sg_shoot_door_observation_t;

typedef struct sg_shoot_door_state_s
{
	sg_shoot_door_witness_t witness;
	sg_shoot_door_phase_t phase;
	uint32_t elapsed_ms;
	uint8_t shot_count;
	uint8_t shot_requested;
} sg_shoot_door_state_t;

int SG_ShootDoorLiveBegin(sg_shoot_door_state_t *state,
	const sg_shoot_door_witness_t *witness,
	const sg_shoot_door_observation_t *observation);
sg_shoot_door_command_t SG_ShootDoorLiveStep(sg_shoot_door_state_t *state,
	const sg_shoot_door_observation_t *observation, uint16_t step_ms);

#endif /* SG_SHOOT_DOOR_LIVE_H */
