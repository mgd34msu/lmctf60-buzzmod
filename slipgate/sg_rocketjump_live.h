#ifndef SG_ROCKETJUMP_LIVE_H
#define SG_ROCKETJUMP_LIVE_H

#include <stdint.h>

#include "slipgate/sg_action_contract.generated.h"
#include "slipgate/sg_rocketjump_phase.h"

#define SG_ROCKETJUMP_STEP_MS 25
#define SG_ROCKETJUMP_FRAME_STEPS 4
#define SG_ROCKETJUMP_HEALTH_MARGIN \
	SG_RUNE_PROOF_ROCKETJUMP_HEALTH_MARGIN
#define SG_ROCKETJUMP_MAX_ACTION_MS SG_RUNE_PROOF_ROCKETJUMP_TOTAL_MS
#define SG_ROCKETJUMP_ARRIVAL_RADIUS_Q8 320
#define SG_ROCKETJUMP_ARRIVAL_Z_Q8 576

typedef struct sg_rocketjump_projectile_key_s
{
	uint32_t key;
	uint32_t generation;
} sg_rocketjump_projectile_key_t;

typedef enum sg_rocketjump_command_e
{
	SG_ROCKETJUMP_COMMAND_ZERO = 0,
	SG_ROCKETJUMP_COMMAND_EQUIP,
	SG_ROCKETJUMP_COMMAND_FIRE,
	SG_ROCKETJUMP_COMMAND_FLIGHT
} sg_rocketjump_command_t;

typedef enum sg_rocketjump_failure_e
{
	SG_ROCKETJUMP_FAILURE_NONE = 0,
	SG_ROCKETJUMP_FAILURE_ARGUMENT,
	SG_ROCKETJUMP_FAILURE_WITNESS,
	SG_ROCKETJUMP_FAILURE_SOURCE,
	SG_ROCKETJUMP_FAILURE_LAUNCH_STATE,
	SG_ROCKETJUMP_FAILURE_TIMEOUT,
	SG_ROCKETJUMP_FAILURE_PROJECTILE,
	SG_ROCKETJUMP_FAILURE_IMPACT,
	SG_ROCKETJUMP_FAILURE_DAMAGE,
	SG_ROCKETJUMP_FAILURE_LANDING
} sg_rocketjump_failure_t;

typedef struct sg_rocketjump_witness_s
{
	int link_index;
	short source_q8[3];
	short destination_q8[3];
	short pitch;
	short yaw;
	unsigned short cost_ms;
	byte health_price;
} sg_rocketjump_witness_t;

typedef struct sg_rocketjump_observation_s
{
	short origin_q8[3];
	short velocity_q8[3];
	qboolean alive;
	qboolean grounded;
	qboolean dry;
	qboolean immutable_support;
	qboolean normal_move;
	qboolean standing;
	qboolean jump_released;
	qboolean right_handed;
	qboolean quad_active;
	qboolean standard_weapon_law;
	qboolean launcher_owned;
	qboolean launcher_selected;
	qboolean weapon_ready;
	int health;
	int rockets;
} sg_rocketjump_observation_t;

typedef struct sg_rocketjump_live_state_s
{
	sg_rocketjump_witness_t witness;
	sg_rocketjump_phase_t phase;
	sg_rocketjump_failure_t failure;
	int elapsed_ms;
	sg_rocketjump_projectile_key_t projectile;
	short expected_impact_q8[3];
	int impact_health_before;
	short impact_velocity_before_q8[3];
	qboolean impact_pending;
	qboolean impact_confirmed;
} sg_rocketjump_live_state_t;

void SG_RocketJumpLiveReset(sg_rocketjump_live_state_t *state);
qboolean SG_RocketJumpArrivalEnvelope(const short origin_q8[3],
	const short destination_q8[3]);
qboolean SG_RocketJumpControlMuzzle(const vec3_t origin, short pitch,
	short yaw, vec3_t muzzle, vec3_t forward);
qboolean SG_RocketJumpLiveBegin(sg_rocketjump_live_state_t *state,
	const sg_rocketjump_witness_t *witness,
	const sg_rocketjump_observation_t *observation);
sg_rocketjump_command_t SG_RocketJumpLiveCommand(
	sg_rocketjump_live_state_t *state,
	const sg_rocketjump_observation_t *observation);
qboolean SG_RocketJumpLiveFired(sg_rocketjump_live_state_t *state,
	sg_rocketjump_projectile_key_t projectile,
	const short expected_impact_q8[3]);
qboolean SG_RocketJumpLiveImpactBegin(sg_rocketjump_live_state_t *state,
	sg_rocketjump_projectile_key_t projectile, qboolean world_hit,
	qboolean sky, qboolean expected_surface, int health_before,
	const short velocity_before_q8[3]);
qboolean SG_RocketJumpLiveImpactEnd(sg_rocketjump_live_state_t *state,
	sg_rocketjump_projectile_key_t projectile, int health_after,
	const short velocity_after_q8[3]);
qboolean SG_RocketJumpLiveStep(sg_rocketjump_live_state_t *state,
	int step_ms);
qboolean SG_RocketJumpLiveBoundary(sg_rocketjump_live_state_t *state,
	qboolean arrived, qboolean grounded);
qboolean SG_RocketJumpLiveOwns(const sg_rocketjump_live_state_t *state);
const char *SG_RocketJumpLiveFailureName(sg_rocketjump_failure_t failure);

#endif /* SG_ROCKETJUMP_LIVE_H */
