/* Probabilistic weapon-effect and exact pre-fire boundary contracts. */
#ifndef SG_WEAPON_CONTRACT_H
#define SG_WEAPON_CONTRACT_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "sg_belief_contract.h"
#include "sg_destination.h"
#include "sg_weapon_effect_profile.h"

typedef enum sg_weapon_trace_status_e
{
	SG_WEAPON_TRACE_NOT_RUN = 0,
	SG_WEAPON_TRACE_ACCEPTED,
	SG_WEAPON_TRACE_REJECTED
} sg_weapon_trace_status_t;

typedef struct sg_weapon_affordance_s
{
	uint64_t rune_identity;
	uint64_t visibility_revision;
	uint32_t source_cell_id;
	uint32_t target_cell_id;
	uint32_t allowed_effects;
	float visibility_probability;
	float occlusion_probability;
	uint8_t projectile_corridor;
	uint8_t splash_surface;
	uint8_t exact_live_trace_required;
	uint8_t reserved;
} sg_weapon_affordance_t;

typedef struct sg_weapon_effect_query_s
{
	const sg_weapon_profile_t *profile;
	const sg_weapon_affordance_t *affordance;
	const sg_belief_state_t *target_belief;
	const sg_belief_state_t *teammate_beliefs;
	size_t teammate_belief_count;
	uint16_t ammo_available;
	uint16_t shooter_client;
	uint16_t target_client;
	uint8_t audience_team;
	uint8_t shooter_team;
	uint8_t target_team;
	uint8_t reserved;
	sg_belief_life_identity_t target_life;
	uint32_t shooter_cell_id;
	uint32_t target_cell_id;
	uint32_t opportunity_cost_ms;
	uint64_t rune_identity;
	uint64_t now_ms;
	uint64_t prediction_time_ms;
	uint64_t teammate_snapshot_revision;
	uint64_t build_identity;
	uint64_t physics_abi_id;
	uint8_t teammate_evidence_complete;
	uint8_t reserved2[7];
	float shooter_origin[3];
	float shooter_health;
	float shooter_armor;
} sg_weapon_effect_query_t;

typedef struct sg_weapon_effect_result_s
{
	float hit_probability;
	float expected_direct_damage;
	float expected_splash_damage;
	float expected_self_risk;
	float expected_teammate_risk;
	float expected_ammo_cost;
	float net_effect_score;
	uint32_t expected_arrival_ms;
	uint32_t opportunity_cost_ms;
	uint16_t best_particle;
	uint16_t shooter_client;
	uint16_t target_client;
	uint8_t audience_team;
	uint8_t shooter_team;
	sg_belief_life_identity_t target_life;
	uint32_t shooter_cell_id;
	uint32_t target_cell_id;
	uint64_t prediction_time_ms;
	uint64_t target_belief_generation;
	uint64_t teammate_snapshot_revision;
	uint8_t affordable;
	uint8_t exact_live_trace_required;
	uint8_t valid;
} sg_weapon_effect_result_t;

typedef struct sg_weapon_prefire_request_s
{
	uint64_t shot_id;
	uint64_t shot_revision;
	uint64_t rune_identity;
	uint64_t pose_revision;
	uint64_t fired_at_ms;
	uint64_t prediction_time_ms;
	uint32_t source_cell_id;
	uint32_t target_cell_id;
	uint16_t shooter_client;
	uint16_t target_client;
	uint16_t profile_id;
	uint8_t shooter_team;
	uint8_t target_team;
	uint8_t audience_team;
	uint8_t exact_required;
	uint8_t reserved[2];
	float muzzle_origin[3];
	float aim_direction[3];
	float intended_impact[3];
} sg_weapon_prefire_request_t;

typedef struct sg_weapon_prefire_validation_s
{
	uint64_t shot_id;
	uint64_t shot_revision;
	uint64_t rune_identity;
	uint64_t pose_revision;
	uint64_t fired_at_ms;
	uint64_t prediction_time_ms;
	uint32_t source_cell_id;
	uint32_t target_cell_id;
	uint16_t shooter_client;
	uint16_t target_client;
	uint16_t profile_id;
	uint8_t shooter_team;
	uint8_t target_team;
	uint8_t audience_team;
	sg_weapon_trace_status_t trace_status;
	uint8_t muzzle_clear;
	uint8_t host_agrees;
	uint8_t authenticated;
	uint8_t reserved;
	uint64_t authorization_id;
	float muzzle_origin[3];
	float aim_direction[3];
	float intended_impact[3];
} sg_weapon_prefire_validation_t;

static inline int SG_WeaponAffordanceValid(
	const sg_weapon_affordance_t *affordance)
{
	return affordance && affordance->rune_identity != 0U &&
	       affordance->visibility_revision != 0U &&
	       affordance->source_cell_id != SG_DESTINATION_NO_CELL &&
	       affordance->target_cell_id != SG_DESTINATION_NO_CELL &&
	       affordance->allowed_effects != 0U &&
	       (affordance->allowed_effects & ~(uint32_t)SG_WEAPON_EFFECT_MASK) == 0U &&
	       SG_WeaponFloatValid(affordance->visibility_probability) &&
	       affordance->visibility_probability >= 0.0f &&
	       affordance->visibility_probability <= 1.0f &&
	       SG_WeaponFloatValid(affordance->occlusion_probability) &&
	       affordance->occlusion_probability >= 0.0f &&
	       affordance->occlusion_probability <= 1.0f &&
	       affordance->projectile_corridor <= 1U &&
	       affordance->splash_surface <= 1U &&
	       affordance->exact_live_trace_required <= 1U;
}

static inline int SG_WeaponBeliefBoundToQuery(
	const sg_belief_state_t *belief, uint8_t audience_team,
	uint8_t target_team, const sg_belief_life_identity_t *target_life,
	uint64_t now_ms,
	uint64_t prediction_time_ms)
{
	return SG_BeliefStateValid(belief) && belief->particle_count != 0U &&
	       belief->audience_team == audience_team &&
	       belief->target_team == target_team &&
	       SG_BeliefLifeIdentityEqual(&belief->target_life, target_life) &&
	       belief->updated_at_ms <= now_ms && prediction_time_ms >= now_ms;
}

static inline int SG_WeaponEffectQueryValid(
	const sg_weapon_effect_query_t *query)
{
	size_t index;
	uint32_t axis;
	uint8_t teammate_seen[SG_BELIEF_MAX_CLIENTS] = {0};

	if (!query || !SG_WeaponProfileValid(query->profile) ||
	    query->profile->resolved != 1U || query->build_identity == 0U ||
	    query->physics_abi_id == 0U ||
	    query->build_identity != query->profile->build_identity ||
	    query->physics_abi_id != query->profile->physics_abi_id ||
	    !SG_WeaponAffordanceValid(query->affordance) ||
	    query->rune_identity != query->affordance->rune_identity ||
	    !SG_BeliefTeamValid(query->audience_team) ||
	    !SG_BeliefTeamValid(query->shooter_team) ||
	    !SG_BeliefTeamValid(query->target_team) ||
	    query->audience_team != query->shooter_team ||
	    query->target_team == query->shooter_team ||
	    query->shooter_client >= SG_BELIEF_MAX_CLIENTS ||
	    query->target_client >= SG_BELIEF_MAX_CLIENTS ||
	    query->shooter_client == query->target_client ||
	    !SG_BeliefLifeIdentityValid(&query->target_life) ||
	    query->target_life.client_id != query->target_client ||
	    query->shooter_cell_id != query->affordance->source_cell_id ||
	    query->target_cell_id != query->affordance->target_cell_id ||
	    query->now_ms == 0U || query->prediction_time_ms < query->now_ms ||
	    query->teammate_snapshot_revision == 0U ||
	    query->teammate_evidence_complete != 1U ||
	    !SG_WeaponBeliefBoundToQuery(query->target_belief,
		query->audience_team, query->target_team, &query->target_life,
		query->now_ms, query->prediction_time_ms) ||
	    query->teammate_belief_count > SG_BELIEF_MAX_CLIENTS ||
	    (query->teammate_belief_count != 0U && !query->teammate_beliefs) ||
	    query->ammo_available < query->profile->ammo.live_fire_minimum ||
	    (query->affordance->allowed_effects & query->profile->effects) == 0U ||
	    !SG_WeaponFloatValid(query->shooter_health) ||
	    query->shooter_health < 0.0f ||
	    !SG_WeaponFloatValid(query->shooter_armor) || query->shooter_armor < 0.0f)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!SG_WeaponFloatValid(query->shooter_origin[axis]))
			return 0;
	for (index = 0U; index < query->teammate_belief_count; index++)
	{
		const sg_belief_life_identity_t *teammate_life =
			&query->teammate_beliefs[index].target_life;
		uint32_t teammate_client = teammate_life->client_id;

		if (!SG_WeaponBeliefBoundToQuery(&query->teammate_beliefs[index],
			query->audience_team, query->shooter_team,
			teammate_life, query->now_ms,
			query->prediction_time_ms) ||
		    teammate_client == query->shooter_client ||
		    teammate_client == query->target_client ||
		    teammate_seen[teammate_client] != 0U)
			return 0;
		teammate_seen[teammate_client] = 1U;
	}
	return 1;
}

static inline int SG_WeaponPrefireRequestValid(
	const sg_weapon_prefire_request_t *request)
{
	float aim_length_squared = 0.0f;
	uint32_t axis;

	if (!request || request->shot_id == 0U || request->shot_revision == 0U ||
	    request->rune_identity == 0U || request->pose_revision == 0U ||
	    request->fired_at_ms == 0U ||
	    request->prediction_time_ms < request->fired_at_ms ||
	    request->source_cell_id == SG_DESTINATION_NO_CELL ||
	    request->target_cell_id == SG_DESTINATION_NO_CELL ||
	    request->shooter_client >= SG_BELIEF_MAX_CLIENTS ||
	    request->target_client >= SG_BELIEF_MAX_CLIENTS ||
	    request->shooter_client == request->target_client ||
	    !SG_BeliefTeamValid(request->shooter_team) ||
	    !SG_BeliefTeamValid(request->target_team) ||
	    request->target_team == request->shooter_team ||
	    request->audience_team != request->shooter_team ||
	    !SG_WeaponProfileIdValid(request->profile_id) ||
	    request->exact_required != 1U)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!SG_WeaponFloatValid(request->muzzle_origin[axis]) ||
		    !SG_WeaponFloatValid(request->aim_direction[axis]) ||
		    !SG_WeaponFloatValid(request->intended_impact[axis]))
			return 0;
		aim_length_squared += request->aim_direction[axis] *
			request->aim_direction[axis];
	}
	return aim_length_squared > 0.0f && SG_WeaponFloatValid(aim_length_squared);
}

static inline int SG_WeaponPrefireShotMatches(
	const sg_weapon_prefire_request_t *request,
	const sg_weapon_prefire_validation_t *validation)
{
	uint32_t axis;

	if (!SG_WeaponPrefireRequestValid(request) || !validation ||
	    !SG_WeaponProfileIdValid(validation->profile_id) ||
	    validation->shot_id != request->shot_id ||
	    validation->shot_revision != request->shot_revision ||
	    validation->rune_identity != request->rune_identity ||
	    validation->pose_revision != request->pose_revision ||
	    validation->fired_at_ms != request->fired_at_ms ||
	    validation->prediction_time_ms != request->prediction_time_ms ||
	    validation->source_cell_id != request->source_cell_id ||
	    validation->target_cell_id != request->target_cell_id ||
	    validation->shooter_client != request->shooter_client ||
	    validation->target_client != request->target_client ||
	    validation->profile_id != request->profile_id ||
	    validation->shooter_team != request->shooter_team ||
	    validation->target_team != request->target_team ||
	    validation->audience_team != request->audience_team)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (validation->muzzle_origin[axis] != request->muzzle_origin[axis] ||
		    validation->aim_direction[axis] != request->aim_direction[axis] ||
		    validation->intended_impact[axis] != request->intended_impact[axis])
			return 0;
	return 1;
}

/* Downstream combat nodes own scoring and irreversible fire authorization. */
int SG_WeaponEffectQuery(const sg_weapon_effect_query_t *query,
	sg_weapon_effect_result_t *out);
int SG_WeaponPrefireAllowed(const sg_weapon_prefire_request_t *request,
	const sg_weapon_prefire_validation_t *validation);

#endif /* SG_WEAPON_CONTRACT_H */
