#ifndef SG_PUSH_LIVE_H
#define SG_PUSH_LIVE_H

#include <stdint.h>

#define SG_PUSH_STEP_MS 25
#define SG_PUSH_FRAME_STEPS 4
#define SG_PUSH_ARRIVAL_GRACE_MS 500
#define SG_PUSH_ARRIVAL_RADIUS_Q8 384
#define SG_PUSH_ARRIVAL_Z_Q8 576
#define SG_PUSH_HEALTH_RESERVE 15

typedef enum sg_push_phase_e
{
	SG_PUSH_IDLE = 0,
	SG_PUSH_APPROACH,
	SG_PUSH_FLIGHT,
	SG_PUSH_COMPLETE,
	SG_PUSH_FAILED
} sg_push_phase_t;

typedef enum sg_push_command_e
{
	SG_PUSH_COMMAND_ZERO = 0
} sg_push_command_t;

typedef enum sg_push_failure_e
{
	SG_PUSH_FAILURE_NONE = 0,
	SG_PUSH_FAILURE_ARGUMENT,
	SG_PUSH_FAILURE_WITNESS,
	SG_PUSH_FAILURE_SOURCE,
	SG_PUSH_FAILURE_TOUCH,
	SG_PUSH_FAILURE_IMPULSE,
	SG_PUSH_FAILURE_TIMEOUT,
	SG_PUSH_FAILURE_LANDING
} sg_push_failure_t;

typedef struct sg_push_witness_s
{
	int link_index;
	uint32_t entry_key;
	short source_q8[3];
	short destination_q8[3];
	float push_velocity[3];
	unsigned short cost_ms;
} sg_push_witness_t;

typedef struct sg_push_observation_s
{
	short origin_q8[3];
	qboolean alive;
	qboolean grounded;
	qboolean dry;
} sg_push_observation_t;

typedef struct sg_push_live_state_s
{
	sg_push_witness_t witness;
	sg_push_phase_t phase;
	sg_push_failure_t failure;
	int elapsed_ms;
	qboolean airborne_seen;
} sg_push_live_state_t;

void SG_PushLiveReset(sg_push_live_state_t *state);
qboolean SG_PushArrivalEnvelope(const short origin_q8[3],
	const short destination_q8[3]);
qboolean SG_PushMinimumHealth(float source_z, float destination_z,
	float push_z, float gravity, qboolean falling_damage,
	int *minimum_health_out);
qboolean SG_PushLiveBegin(sg_push_live_state_t *state,
	const sg_push_witness_t *witness,
	const sg_push_observation_t *observation);
sg_push_command_t SG_PushLiveCommand(sg_push_live_state_t *state,
	const sg_push_observation_t *observation);
qboolean SG_PushLiveTouched(sg_push_live_state_t *state,
	uint32_t entry_key, const float push_velocity[3]);
qboolean SG_PushLiveStep(sg_push_live_state_t *state, int step_ms);
qboolean SG_PushLiveBoundary(sg_push_live_state_t *state,
	qboolean arrived, qboolean grounded);
qboolean SG_PushLiveOwns(const sg_push_live_state_t *state);
const char *SG_PushLiveFailureName(sg_push_failure_t failure);

#endif
