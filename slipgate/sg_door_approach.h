/* sg_door_approach.h -- pure fixed-step law for direct-trigger door entry. */
#ifndef SG_DOOR_APPROACH_H
#define SG_DOOR_APPROACH_H

/* q_shared.h is included first by every game/test translation unit. */
#include <stdint.h>

#define SG_DOOR_APPROACH_STEP_MS 25
#define SG_DOOR_APPROACH_FRAME_MS 100
#define SG_DOOR_APPROACH_LIMIT_MS 5000
#define SG_DOOR_APPROACH_MAX_AIR_MS 300
#define SG_DOOR_APPROACH_CAPSULE_Q8 256 /* 32 world units */

typedef enum sg_door_approach_phase_e
{
	SG_DOOR_APPROACH_IDLE = 0,
	SG_DOOR_APPROACH_WALK,
	SG_DOOR_APPROACH_SNAP,
	SG_DOOR_APPROACH_FINALIZE,
	SG_DOOR_APPROACH_COMPLETE,
	SG_DOOR_APPROACH_FAILED
} sg_door_approach_phase_t;

typedef enum sg_door_approach_reason_e
{
	SG_DOOR_APPROACH_REASON_NONE = 0,
	SG_DOOR_APPROACH_REASON_ARGUMENT,
	SG_DOOR_APPROACH_REASON_IDENTITY,
	SG_DOOR_APPROACH_REASON_CADENCE,
	SG_DOOR_APPROACH_REASON_POSE,
	SG_DOOR_APPROACH_REASON_STATE,
	SG_DOOR_APPROACH_REASON_WATER,
	SG_DOOR_APPROACH_REASON_POPULATION,
	SG_DOOR_APPROACH_REASON_SWEEP,
	SG_DOOR_APPROACH_REASON_CORRIDOR,
	SG_DOOR_APPROACH_REASON_SUPPORT,
	SG_DOOR_APPROACH_REASON_AIR_TIME,
	SG_DOOR_APPROACH_REASON_FALL,
	SG_DOOR_APPROACH_REASON_SNAP,
	SG_DOOR_APPROACH_REASON_TIMEOUT
} sg_door_approach_reason_t;

/* World-dependent predicates are supplied explicitly by the oracle/live
 * adapters.  The reducer never traces, resolves an edict, or invokes Pmove. */
typedef struct sg_door_approach_observation_s
{
	pmove_state_t pms;
	int grounded;
	int static_support;
	int watertype;
	int waterlevel;
	int hazardous_liquid;
	int population_stable;
	int sweep_clear;
	int physical_touch;
	int fall_sampled;
	float fall_delta;
} sg_door_approach_observation_t;

typedef struct sg_door_approach_state_s
{
	short source_q8[3];
	short anchor_q8[3];
	pmove_state_t expected_pms;
	int expected_watertype;
	int expected_waterlevel;
	int elapsed_ms;
	int consecutive_air_ms;
	int resume_ms;
	int finalize_ms;
	int first_touch_ms;
	float old_frame_z;
	uint8_t phase;
	uint8_t touched;
	uint8_t reserved[2];
} sg_door_approach_state_t;

typedef struct sg_door_approach_result_s
{
	sg_door_approach_phase_t phase;
	sg_door_approach_reason_t reason;
	int drive;
	int snap_required;
} sg_door_approach_result_t;

void SG_DoorApproachReset(sg_door_approach_state_t *state);
sg_door_approach_result_t SG_DoorApproachBegin(
	sg_door_approach_state_t *state, const short source_q8[3],
	const short anchor_q8[3],
	const sg_door_approach_observation_t *observation);
sg_door_approach_result_t SG_DoorApproachPreStep(
	const sg_door_approach_state_t *state,
	const sg_door_approach_observation_t *observation, int command_msec);
sg_door_approach_result_t SG_DoorApproachPostStep(
	sg_door_approach_state_t *state,
	const sg_door_approach_observation_t *observation, int command_msec);
sg_door_approach_result_t SG_DoorApproachSnapped(
	sg_door_approach_state_t *state,
	const sg_door_approach_observation_t *observation);

int SG_DoorApproachPmoveEqual(const pmove_state_t *left,
	const pmove_state_t *right);
int SG_DoorApproachWaterSafe(int waterlevel, int watertype);
int SG_DoorApproachInsideCapsule(const short source_q8[3],
	const short anchor_q8[3], const short point_q8[3]);
const char *SG_DoorApproachReasonName(sg_door_approach_reason_t reason);

#endif /* SG_DOOR_APPROACH_H */
