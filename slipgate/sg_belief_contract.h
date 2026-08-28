/* Sparse, earned-information runtime belief contracts. */
#ifndef SG_BELIEF_CONTRACT_H
#define SG_BELIEF_CONTRACT_H

#include <stdint.h>

#define SG_BELIEF_MAX_PARTICLES 64U
#define SG_BELIEF_MAX_CLIENTS 256U
#define SG_BELIEF_WEIGHT_EPSILON 0.000001f
#define SG_BELIEF_NO_CELL UINT32_MAX

typedef enum sg_belief_observation_source_e
{
	SG_BELIEF_SOURCE_VISUAL = 0,
	SG_BELIEF_SOURCE_SOUND,
	SG_BELIEF_SOURCE_DAMAGE,
	SG_BELIEF_SOURCE_ITEM,
	SG_BELIEF_SOURCE_FLAG,
	SG_BELIEF_SOURCE_TEAM_REPORT,
	SG_BELIEF_SOURCE_HOOK,
	SG_BELIEF_SOURCE_MECHANISM,
	SG_BELIEF_SOURCE_WATER,
	SG_BELIEF_SOURCE_COUNT
} sg_belief_observation_source_t;

typedef enum sg_belief_issuer_kind_e
{
	SG_BELIEF_ISSUER_HOST_SENSOR = 0,
	SG_BELIEF_ISSUER_BOT,
	SG_BELIEF_ISSUER_TEAMMATE,
	SG_BELIEF_ISSUER_COUNT
} sg_belief_issuer_kind_t;

typedef enum sg_belief_motion_state_e
{
	SG_BELIEF_MOTION_UNKNOWN = 0,
	SG_BELIEF_MOTION_GROUND,
	SG_BELIEF_MOTION_AIR,
	SG_BELIEF_MOTION_WATER,
	SG_BELIEF_MOTION_HOOK,
	SG_BELIEF_MOTION_MOVER,
	SG_BELIEF_MOTION_COUNT
} sg_belief_motion_state_t;

typedef enum sg_belief_shape_e
{
	SG_BELIEF_SHAPE_CONCENTRATED = 0,
	SG_BELIEF_SHAPE_DIFFUSE,
	SG_BELIEF_SHAPE_NEGATIVE
} sg_belief_shape_t;

typedef struct sg_belief_authentication_s
{
	uint8_t authenticated;
	uint8_t issuer_kind;
	uint8_t issuer_team;
	uint8_t audience_team;
	uint32_t issuer_client;
	uint64_t observation_id;
	uint64_t authenticated_at_ms;
} sg_belief_authentication_t;

typedef struct sg_belief_observation_s
{
	sg_belief_authentication_t auth;
	sg_belief_observation_source_t source;
	sg_belief_shape_t shape;
	uint8_t target_team;
	uint16_t target_client;
	uint8_t movement_state;
	uint8_t weapon_state;
	uint64_t observed_at_ms;
	uint64_t valid_until_ms;
	uint32_t cell_id;
	float position[3];
	float velocity[3];
	float acceleration[3];
	float orientation[3];
	float spread_radius;
	float confidence;
} sg_belief_observation_t;

typedef struct sg_belief_particle_s
{
	uint32_t cell_id;
	uint8_t movement_state;
	uint8_t weapon_state;
	uint16_t reserved;
	uint64_t future_time_ms;
	float position[3];
	float velocity[3];
	float acceleration[3];
	float orientation[3];
	float spread_radius;
	float weight;
} sg_belief_particle_t;

typedef struct sg_belief_state_s
{
	uint8_t audience_team;
	uint8_t target_team;
	uint16_t target_client;
	uint16_t particle_count;
	uint16_t particle_capacity;
	uint64_t generation;
	uint64_t updated_at_ms;
	float total_weight;
	sg_belief_particle_t *particles;
} sg_belief_state_t;

typedef struct sg_belief_team_store_s
{
	uint8_t audience_team;
	uint8_t reserved[3];
	uint32_t track_count;
	uint32_t track_capacity;
	uint64_t generation;
	sg_belief_state_t *tracks;
} sg_belief_team_store_t;

typedef struct sg_belief_motion_edge_s
{
	uint32_t from_cell_id;
	uint32_t to_cell_id;
	uint32_t travel_ms;
	uint8_t valid;
	uint8_t reserved[3];
	float displacement[3];
} sg_belief_motion_edge_t;

typedef struct sg_belief_prediction_s
{
	uint64_t at_time_ms;
	uint32_t dominant_cell_id;
	float position[3];
	float velocity[3];
	float confidence;
	uint8_t valid;
	uint8_t reserved[3];
} sg_belief_prediction_t;

static inline int SG_BeliefTeamValid(uint8_t team)
{
	return team == 1U || team == 2U;
}

static inline int SG_BeliefSourceValid(sg_belief_observation_source_t source)
{
	return source >= SG_BELIEF_SOURCE_VISUAL && source < SG_BELIEF_SOURCE_COUNT;
}

static inline int SG_BeliefShapeValid(sg_belief_shape_t shape)
{
	return shape >= SG_BELIEF_SHAPE_CONCENTRATED &&
	       shape <= SG_BELIEF_SHAPE_NEGATIVE;
}

static inline int SG_BeliefFloatValid(float value)
{
	return value < 3.402823466e+38F && value > -3.402823466e+38F;
}

static inline int SG_BeliefIssuerValid(
	const sg_belief_authentication_t *auth, uint8_t audience_team)
{
	if (!auth || !SG_BeliefTeamValid(audience_team) ||
	    auth->authenticated != 1U || auth->audience_team != audience_team ||
	    auth->issuer_kind >= SG_BELIEF_ISSUER_COUNT)
		return 0;
	if (auth->issuer_kind == SG_BELIEF_ISSUER_HOST_SENSOR)
		return auth->issuer_team == 0U || auth->issuer_team == audience_team;
	return auth->issuer_team == audience_team &&
	       SG_BeliefTeamValid(auth->issuer_team) && auth->issuer_client != 0U &&
	       auth->issuer_client < SG_BELIEF_MAX_CLIENTS;
}

static inline int SG_BeliefParticleValid(const sg_belief_particle_t *particle)
{
	uint32_t axis;

	if (!particle || particle->cell_id == SG_BELIEF_NO_CELL ||
	    particle->movement_state >= SG_BELIEF_MOTION_COUNT ||
	    particle->weight < 0.0f || !SG_BeliefFloatValid(particle->weight) ||
	    particle->spread_radius < 0.0f ||
	    !SG_BeliefFloatValid(particle->spread_radius))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_BeliefFloatValid(particle->position[axis]) ||
		    !SG_BeliefFloatValid(particle->velocity[axis]) ||
		    !SG_BeliefFloatValid(particle->acceleration[axis]) ||
		    !SG_BeliefFloatValid(particle->orientation[axis]))
			return 0;
	return 1;
}

static inline int SG_BeliefMotionEdgeValid(
	const sg_belief_motion_edge_t *edge)
{
	return edge && edge->valid == 1U && edge->from_cell_id != SG_BELIEF_NO_CELL &&
	       edge->to_cell_id != SG_BELIEF_NO_CELL && edge->travel_ms != 0U &&
	       SG_BeliefFloatValid(edge->displacement[0]) &&
	       SG_BeliefFloatValid(edge->displacement[1]) &&
	       SG_BeliefFloatValid(edge->displacement[2]);
}

static inline int SG_BeliefStateValid(const sg_belief_state_t *state)
{
	uint16_t index;

	if (!state || !SG_BeliefTeamValid(state->audience_team) ||
	    !SG_BeliefTeamValid(state->target_team) ||
	    state->target_client >= SG_BELIEF_MAX_CLIENTS ||
	    state->particle_capacity == 0U ||
	    state->particle_count > state->particle_capacity ||
	    state->particle_capacity > SG_BELIEF_MAX_PARTICLES || !state->particles ||
	    state->generation == 0U || state->total_weight < 0.0f ||
	    !SG_BeliefFloatValid(state->total_weight))
		return 0;
	for (index = 0U; index < state->particle_count; index++)
		if (!SG_BeliefParticleValid(&state->particles[index]) ||
		    state->particles[index].future_time_ms > state->updated_at_ms)
			return 0;
	return 1;
}

static inline int SG_BeliefObservationValidForTeam(
	const sg_belief_observation_t *observation, uint8_t audience_team)
{
	uint32_t axis;

	if (!observation || !SG_BeliefTeamValid(audience_team) ||
	    !SG_BeliefSourceValid(observation->source) ||
	    !SG_BeliefShapeValid(observation->shape) ||
	    !SG_BeliefTeamValid(observation->target_team) ||
	    observation->target_client >= SG_BELIEF_MAX_CLIENTS ||
	    observation->movement_state >= SG_BELIEF_MOTION_COUNT ||
	    !SG_BeliefIssuerValid(&observation->auth, audience_team) ||
	    observation->auth.observation_id == 0U ||
	    observation->observed_at_ms == 0U ||
	    observation->auth.authenticated_at_ms > observation->observed_at_ms ||
	    (observation->valid_until_ms != 0U &&
	     observation->valid_until_ms < observation->observed_at_ms) ||
	    observation->cell_id == SG_BELIEF_NO_CELL ||
	    observation->confidence < 0.0f || observation->confidence > 1.0f ||
	    observation->spread_radius < 0.0f ||
	    !SG_BeliefFloatValid(observation->confidence) ||
	    !SG_BeliefFloatValid(observation->spread_radius))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_BeliefFloatValid(observation->position[axis]) ||
		    !SG_BeliefFloatValid(observation->velocity[axis]) ||
		    !SG_BeliefFloatValid(observation->acceleration[axis]) ||
		    !SG_BeliefFloatValid(observation->orientation[axis]))
			return 0;
	if (observation->source == SG_BELIEF_SOURCE_TEAM_REPORT &&
	    observation->auth.issuer_kind != SG_BELIEF_ISSUER_TEAMMATE)
		return 0;
	if (observation->source == SG_BELIEF_SOURCE_SOUND &&
	    (observation->shape != SG_BELIEF_SHAPE_DIFFUSE ||
	     observation->spread_radius <= 0.0f))
		return 0;
	/* Exclusion needs both strength and a non-empty spatial support. */
	if (observation->shape == SG_BELIEF_SHAPE_NEGATIVE &&
	    (observation->confidence <= 0.0f || observation->spread_radius <= 0.0f))
		return 0;
	return 1;
}

static inline int SG_BeliefStoreValid(const sg_belief_team_store_t *store)
{
	uint32_t index;

	if (!store || !SG_BeliefTeamValid(store->audience_team) ||
	    store->track_count > store->track_capacity ||
	    store->track_capacity > SG_BELIEF_MAX_CLIENTS ||
	    (store->track_count != 0U && !store->tracks) || store->generation == 0U)
		return 0;
	for (index = 0U; index < store->track_count; index++)
		if (!SG_BeliefStateValid(&store->tracks[index]) ||
		    store->tracks[index].audience_team != store->audience_team)
			return 0;
	return 1;
}

/* Downstream belief nodes own mutation, propagation, and prediction. */
int SG_BeliefStateInit(sg_belief_state_t *state, uint8_t audience_team,
	uint8_t target_team, uint16_t target_client,
	sg_belief_particle_t *storage, uint16_t capacity);
int SG_BeliefApplyObservation(sg_belief_state_t *state,
	const sg_belief_observation_t *observation);
int SG_BeliefAdvance(sg_belief_state_t *state, uint64_t now_ms,
	const sg_belief_motion_edge_t *edges, uint32_t edge_count,
	uint32_t decay_ms);
int SG_BeliefPredict(const sg_belief_state_t *state, uint64_t at_time_ms,
	sg_belief_prediction_t *out);

#endif /* SG_BELIEF_CONTRACT_H */
