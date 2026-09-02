#include "sg_rune_movement.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sg_host_engine_pmove.h"
#include "sg_host_rocket_jump_law.h"

/* A rocket jump costs about 47 health; charged as seconds of route. */
#define SG_RUNE_MOVE_ROCKET_HEALTH_COST_SECONDS 4.0f

/* Profile slots, shared by every crossing of their kind under one law. */
enum
{
	PROFILE_GROUND = 0,       /* walk, crouch, ramp: contact motion */
	PROFILE_WATER,            /* swim */
	PROFILE_AIR,              /* drop, air control */
	PROFILE_JUMP,             /* air with the engine's launch impulse */
	PROFILE_ROCKET_JUMP,      /* air from the blast with the summed velocity */
	PROFILE_COUNT
};

static const uint8_t TIME_INPUT[] = { SG_RUNE_FN_INPUT_TIME_SECONDS };
static const uint8_t DISTANCE_INPUT[] = { SG_RUNE_FN_INPUT_DISTANCE };

static void ProfileClear(sg_rune_move_profile_t *profile)
{
	uint32_t axis;

	profile->cost = SG_RUNE_FN_INDEX_NONE;
	profile->travel_time = SG_RUNE_FN_INDEX_NONE;
	for (axis = 0U; axis < 3U; axis++)
	{
		profile->position[axis] = SG_RUNE_FN_INDEX_NONE;
		profile->velocity[axis] = SG_RUNE_FN_INDEX_NONE;
	}
	profile->reachability = SG_RUNE_FN_INDEX_NONE;
	profile->lead_seconds = 0.0f;
}

/* Cost and travel time as affine functions of one input. */
static int AddCostAndTime(sg_rune_fn_store_t *store,
	sg_rune_move_profile_t *profile, const uint8_t *input, float cost_slope,
	float cost_bias, float time_slope, float time_bias)
{
	profile->cost = SG_RuneFnAppendAffine(store,
		SG_RUNE_FN_OUTPUT_COST, input, 1U, cost_bias, &cost_slope);
	profile->travel_time = SG_RuneFnAppendAffine(store,
		SG_RUNE_FN_OUTPUT_TRAVEL_TIME_SECONDS, input, 1U, time_bias,
		&time_slope);
	return profile->cost != SG_RUNE_FN_INDEX_NONE &&
		profile->travel_time != SG_RUNE_FN_INDEX_NONE;
}

static int AddReachability(sg_rune_fn_store_t *store,
	sg_rune_move_profile_t *profile, float margin)
{
	profile->reachability = SG_RuneFnAppendConstant(store,
		SG_RUNE_FN_OUTPUT_REACHABILITY, margin);
	return profile->reachability != SG_RUNE_FN_INDEX_NONE;
}

/* Contact motion: position = world + velocity * t, velocity unchanged.
 * Cost and travel time are distance over the family's speed clamp. */
static int AddContactMotion(sg_rune_fn_store_t *store,
	sg_rune_move_profile_t *profile, float speed)
{
	uint32_t axis;

	if (!(speed > 0.0f) ||
		!AddCostAndTime(store, profile, DISTANCE_INPUT, 1.0f / speed, 0.0f,
			1.0f / speed, 0.0f))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		const uint8_t inputs[] = {
			(uint8_t)(SG_RUNE_FN_INPUT_WORLD_X + axis),
			(uint8_t)(SG_RUNE_FN_INPUT_VELOCITY_X + axis),
			SG_RUNE_FN_INPUT_TIME_SECONDS
		};
		sg_rune_fn_term_t terms[2];
		const float slope = 1.0f;

		memset(terms, 0, sizeof(terms));
		terms[0].coefficient = 1.0f;
		terms[0].exponents[0] = 1U;
		terms[1].coefficient = 1.0f;
		terms[1].exponents[1] = 1U;
		terms[1].exponents[2] = 1U;
		profile->position[axis] = SG_RuneFnAppendPolynomial(store,
			(sg_rune_fn_output_t)(SG_RUNE_FN_OUTPUT_POSITION_X + axis),
			inputs, 3U, terms, 2U);
		profile->velocity[axis] = SG_RuneFnAppendAffine(store,
			(sg_rune_fn_output_t)(SG_RUNE_FN_OUTPUT_VELOCITY_X + axis),
			&inputs[1], 1U, 0.0f, &slope);
		if (profile->position[axis] == SG_RUNE_FN_INDEX_NONE ||
			profile->velocity[axis] == SG_RUNE_FN_INDEX_NONE)
			return 0;
	}
	return AddReachability(store, profile, 1.0f);
}

/* Free flight under the host's substep Euler, gravity applied before each
 * substep's move, so the boundary position carries a half-substep term:
 *   position(t) = world + offset + (velocity + impulse - gravity*dt/2) t
 *                 - (gravity/2) t^2                          (z only)
 *   velocity(t) = velocity + impulse - gravity t              (z only)
 * Cost and travel time are the time spent in the air, after the lead. */
static int AddAirMotion(sg_rune_fn_store_t *store,
	sg_rune_move_profile_t *profile, const sg_rune_move_law_t *law,
	float vertical_impulse, float vertical_offset, float lead_seconds,
	float cost_bias)
{
	const float substep_seconds = (float)law->substep_ms / 1000.0f;
	uint32_t axis;

	if (!(law->gravity >= 0.0f) || !isfinite(law->gravity) ||
		!AddCostAndTime(store, profile, TIME_INPUT, 1.0f,
			lead_seconds + cost_bias, 1.0f, lead_seconds))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		const uint8_t inputs[] = {
			(uint8_t)(SG_RUNE_FN_INPUT_WORLD_X + axis),
			(uint8_t)(SG_RUNE_FN_INPUT_VELOCITY_X + axis),
			SG_RUNE_FN_INPUT_TIME_SECONDS
		};
		sg_rune_fn_term_t terms[5];
		uint32_t count = 0U;
		float velocity_bias = 0.0f, velocity_slope = 1.0f;
		const float time_slope = axis == 2U ? -law->gravity : 0.0f;
		uint8_t velocity_inputs[2];
		float velocity_slopes[2];

		memset(terms, 0, sizeof(terms));
		terms[count].coefficient = 1.0f;
		terms[count++].exponents[0] = 1U;
		terms[count].coefficient = 1.0f;
		terms[count].exponents[1] = 1U;
		terms[count++].exponents[2] = 1U;
		if (axis == 2U)
		{
			const float linear = vertical_impulse -
				0.5f * law->gravity * substep_seconds;

			if (linear != 0.0f)
			{
				terms[count].coefficient = linear;
				terms[count++].exponents[2] = 1U;
			}
			if (vertical_offset != 0.0f)
				terms[count++].coefficient = vertical_offset;
			if (law->gravity != 0.0f)
			{
				terms[count].coefficient = -0.5f * law->gravity;
				terms[count++].exponents[2] = 2U;
			}
			velocity_bias = vertical_impulse;
		}
		profile->position[axis] = SG_RuneFnAppendPolynomial(store,
			(sg_rune_fn_output_t)(SG_RUNE_FN_OUTPUT_POSITION_X + axis),
			inputs, 3U, terms, count);
		velocity_inputs[0] = inputs[1];
		velocity_inputs[1] = SG_RUNE_FN_INPUT_TIME_SECONDS;
		velocity_slopes[0] = velocity_slope;
		velocity_slopes[1] = time_slope;
		profile->velocity[axis] = SG_RuneFnAppendAffine(store,
			(sg_rune_fn_output_t)(SG_RUNE_FN_OUTPUT_VELOCITY_X + axis),
			velocity_inputs, 2U, velocity_bias, velocity_slopes);
		if (profile->position[axis] == SG_RUNE_FN_INDEX_NONE ||
			profile->velocity[axis] == SG_RUNE_FN_INDEX_NONE)
			return 0;
	}
	profile->lead_seconds = lead_seconds;
	return AddReachability(store, profile, 1.0f);
}

static int BuildProfiles(sg_rune_move_store_t *store)
{
	const sg_rune_move_law_t *law = &store->law;
	sg_host_rocket_jump_launch_t launch;
	uint32_t index;

	store->profiles = calloc(PROFILE_COUNT, sizeof(*store->profiles));
	if (!store->profiles)
		return 0;
	store->profile_count = PROFILE_COUNT;
	for (index = 0U; index < PROFILE_COUNT; index++)
		ProfileClear(&store->profiles[index]);
	if (!AddContactMotion(&store->analytic, &store->profiles[PROFILE_GROUND],
			SG_HOST_ENGINE_MAX_SPEED) ||
		!AddContactMotion(&store->analytic, &store->profiles[PROFILE_WATER],
			SG_HOST_ENGINE_WATER_SPEED) ||
		!AddAirMotion(&store->analytic, &store->profiles[PROFILE_AIR], law,
			0.0f, 0.0f, 0.0f, 0.0f) ||
		!AddAirMotion(&store->analytic, &store->profiles[PROFILE_JUMP], law,
			SG_HOST_ENGINE_JUMP_VELOCITY, 0.0f, 0.0f, 0.0f))
		return 0;
	store->jump_rise = law->gravity > 0.0f ?
		(SG_HOST_ENGINE_JUMP_VELOCITY * SG_HOST_ENGINE_JUMP_VELOCITY) /
			(2.0f * law->gravity) : 0.0f;
	/* Rocket jump: the air profile from the blast height with the summed
	 * vertical velocity, its clock offset by the lead frames.  When the law
	 * yields no launch under this gravity the profile is unreachable and no
	 * crossing attaches to it. */
	if (SG_HostRocketJumpLaunch(law->gravity, law->frame_ms, law->substep_ms,
		0, &launch))
	{
		/* The cost carries the health the blast takes: the seconds a body
		 * would spend walking to earn that much back are not free. */
		if (!AddAirMotion(&store->analytic, &store->profiles[PROFILE_ROCKET_JUMP],
			law, launch.vertical_velocity, launch.pre_blast_rise,
			(float)launch.lead_frames * (float)law->frame_ms / 1000.0f,
			SG_RUNE_MOVE_ROCKET_HEALTH_COST_SECONDS))
			return 0;
		store->rocket_rise = launch.rise;
		store->rocket_velocity = launch.vertical_velocity;
		store->rocket_lead_seconds = (float)launch.lead_frames *
			(float)law->frame_ms / 1000.0f;
		store->rocket_pre_blast_rise = launch.pre_blast_rise;
	}
	else
	{
		if (!AddCostAndTime(&store->analytic,
				&store->profiles[PROFILE_ROCKET_JUMP], TIME_INPUT, 1.0f, 0.0f,
				1.0f, 0.0f) ||
			!AddReachability(&store->analytic,
				&store->profiles[PROFILE_ROCKET_JUMP], -1.0f))
			return 0;
		store->rocket_rise = 0.0f;
	}
	return 1;
}

int SG_RuneMoveStoreInit(sg_rune_move_store_t *store,
	const sg_rune_move_law_t *law)
{
	if (!store || !law || !isfinite(law->gravity) || law->frame_ms == 0U ||
		law->substep_ms == 0U)
		return 0;
	memset(store, 0, sizeof(*store));
	store->law = *law;
	SG_RuneFnStoreInit(&store->analytic);
	if (!BuildProfiles(store))
	{
		SG_RuneMoveStoreFree(store);
		return 0;
	}
	return 1;
}

void SG_RuneMoveStoreFree(sg_rune_move_store_t *store)
{
	if (!store)
		return;
	free(store->capabilities);
	free(store->profiles);
	SG_RuneFnStoreFree(&store->analytic);
	memset(store, 0, sizeof(*store));
}

void SG_RuneMoveStoreView(const sg_rune_move_store_t *store,
	sg_rune_move_table_t *table_out)
{
	if (!table_out)
		return;
	memset(table_out, 0, sizeof(*table_out));
	if (!store)
		return;
	table_out->capabilities = store->capabilities;
	table_out->capability_count = store->capability_count;
	table_out->profiles = store->profiles;
	table_out->profile_count = store->profile_count;
	SG_RuneFnStoreView(&store->analytic, &table_out->analytic);
}

static int Append(sg_rune_move_store_t *store, uint32_t cell,
	uint32_t portal, uint32_t destination, sg_rune_move_kind_t kind,
	uint8_t source_stance, uint8_t destination_stance, uint32_t profile,
	const float launch_velocity[3], float seconds)
{
	sg_rune_move_capability_t *record;

	if (store->capability_count == store->capability_capacity)
	{
		uint32_t capacity = store->capability_capacity ?
			store->capability_capacity * 2U : 4096U;
		sg_rune_move_capability_t *grown = realloc(store->capabilities,
			(size_t)capacity * sizeof(*grown));

		if (!grown)
			return 0;
		store->capabilities = grown;
		store->capability_capacity = capacity;
	}
	record = &store->capabilities[store->capability_count++];
	memset(record, 0, sizeof(*record));
	record->cell = cell;
	record->portal = portal;
	record->destination = destination;
	record->mechanism = SG_RUNE_FN_INDEX_NONE;
	record->kind = (uint8_t)kind;
	if (launch_velocity)
		memcpy(record->launch_velocity, launch_velocity,
			sizeof(record->launch_velocity));
	record->seconds = seconds;
	record->source_stances = source_stance;
	record->destination_stances = destination_stance;
	record->profile = profile;
	return 1;
}

/* The far side's stance for a crossing made in the near side's stance: the
 * same one when the far side admits it, otherwise whatever it admits. */
static uint8_t ExactOrOtherStance(uint8_t preferred, uint8_t allowed)
{
	if (allowed & preferred)
		return preferred;
	if (allowed & SG_RUNE_MOVE_STANDING)
		return SG_RUNE_MOVE_STANDING;
	if (allowed & SG_RUNE_MOVE_CROUCHING)
		return SG_RUNE_MOVE_CROUCHING;
	return 0U;
}

/* Every crossing the ordinary player can make, one record per exact source
 * stance.  Admissibility is decided from what the complex carries: water
 * both sides is a swim; level supported sides within a step are a walk or a
 * crouch; a lower far floor is a drop; a higher one within the jump's rise
 * is a jump, and within the rocket jump's rise a rocket jump, which is also
 * offered beside every upward jump; leaving support is a drop through a
 * partition or a jump up through the floor's own boundary; airborne into
 * anything is air control. */
int SG_RuneMoveEmitCrossing(sg_rune_move_store_t *store,
	const sg_rune_move_crossing_t *crossing)
{
	static const uint8_t stances[2] = {
		SG_RUNE_MOVE_STANDING, SG_RUNE_MOVE_CROUCHING
	};
	uint8_t source_stances, target_stances;
	uint32_t index;

	if (!store || !crossing || !isfinite(crossing->floor_delta))
		return 0;
	source_stances = (uint8_t)(crossing->cell_stances & crossing->portal_stances);
	target_stances = (uint8_t)(crossing->other_stances & crossing->portal_stances);
	if (!source_stances || !target_stances)
		return 1;
	for (index = 0U; index < 2U; index++)
	{
		const uint8_t source_stance = stances[index];
		uint8_t target_stance;
		sg_rune_move_kind_t kind = SG_RUNE_MOVE_KIND_COUNT;
		uint32_t profile = 0U;
		int rocket = 0;

		if (!(source_stances & source_stance))
			continue;
		target_stance = ExactOrOtherStance(source_stance, target_stances);
		if (!target_stance)
			continue;
		if (crossing->source_water &&
			(crossing->target_water || crossing->target_supported))
		{
			/* Through water, or out of it onto a floor. */
			kind = SG_RUNE_MOVE_SWIM;
			profile = PROFILE_WATER;
		}
		else if (crossing->source_supported && crossing->target_supported &&
			crossing->vertical_facet)
		{
			if (fabsf(crossing->floor_delta) <= SG_HOST_ENGINE_STEP_SIZE)
			{
				kind = source_stance == SG_RUNE_MOVE_CROUCHING ?
					SG_RUNE_MOVE_CROUCH : SG_RUNE_MOVE_WALK;
				profile = PROFILE_GROUND;
			}
			else if (crossing->floor_delta < 0.0f)
			{
				kind = SG_RUNE_MOVE_DROP;
				profile = PROFILE_AIR;
			}
			else
			{
				if (crossing->floor_delta <= store->jump_rise)
				{
					kind = SG_RUNE_MOVE_JUMP;
					profile = PROFILE_JUMP;
				}
				/* A step up beyond a jump is a rocket jump when the blast
				 * carries that high: the far floor is the landing. */
				rocket = crossing->floor_delta <= store->rocket_rise;
			}
		}
		/* Off a floor into the air, and anything airborne, is not a
		 * contact crossing: the builder traces those flights. */
		if (kind != SG_RUNE_MOVE_KIND_COUNT &&
			!Append(store, crossing->cell, crossing->portal,
				crossing->other_cell, kind, source_stance, target_stance,
				profile, NULL, 0.0f))
			return 0;
		if (rocket && !Append(store, crossing->cell, crossing->portal,
			crossing->other_cell, SG_RUNE_MOVE_ROCKET_JUMP, source_stance,
			target_stance, PROFILE_ROCKET_JUMP, NULL, 0.0f))
			return 0;
	}
	return 1;
}

int SG_RuneMoveAppendFlight(sg_rune_move_store_t *store, uint32_t cell,
	uint32_t portal, sg_rune_move_kind_t kind, uint8_t source_stances,
	uint8_t destination_stances, uint32_t destination,
	const float launch_velocity[3], float seconds)
{
	uint32_t profile;

	if (!store || !launch_velocity || !isfinite(seconds) || seconds < 0.0f ||
		!source_stances || !destination_stances)
		return 0;
	switch (kind)
	{
	case SG_RUNE_MOVE_JUMP: profile = PROFILE_JUMP; break;
	case SG_RUNE_MOVE_DROP: profile = PROFILE_AIR; break;
	case SG_RUNE_MOVE_ROCKET_JUMP: profile = PROFILE_ROCKET_JUMP; break;
	default: return 0;
	}
	return Append(store, cell, portal, destination, kind, source_stances,
		destination_stances, profile, launch_velocity, seconds);
}

int SG_RuneMoveAppendMechanism(sg_rune_move_store_t *store, uint32_t cell,
	uint32_t destination, sg_rune_move_kind_t kind, uint8_t stances,
	uint32_t mechanism, const float velocity[3], float seconds)
{
	uint32_t profile;
	static const float still[3] = { 0.0f, 0.0f, 0.0f };

	if (!store || !stances || !isfinite(seconds) || seconds < 0.0f)
		return 0;
	switch (kind)
	{
	case SG_RUNE_MOVE_TELEPORT:
	case SG_RUNE_MOVE_PLATFORM:
	case SG_RUNE_MOVE_TRAIN:
	case SG_RUNE_MOVE_MOVER:
		profile = PROFILE_GROUND;
		break;
	case SG_RUNE_MOVE_EXTERNAL_FORCE:
		profile = PROFILE_AIR;
		break;
	default:
		return 0;
	}
	if (!Append(store, cell, SG_RUNE_FN_INDEX_NONE, destination, kind, stances,
		stances, profile, velocity ? velocity : still, seconds))
		return 0;
	store->capabilities[store->capability_count - 1U].mechanism = mechanism;
	return 1;
}

void SG_RuneMoveGate(sg_rune_move_store_t *store, const uint32_t *cells,
	uint32_t cell_count, uint32_t mechanism)
{
	uint32_t index, slot;

	if (!store || !cells)
		return;
	for (index = 0U; index < store->capability_count; index++)
	{
		sg_rune_move_capability_t *record = &store->capabilities[index];

		if (record->mechanism != SG_RUNE_FN_INDEX_NONE)
			continue;
		for (slot = 0U; slot < cell_count; slot++)
			if (record->destination == cells[slot])
			{
				record->mechanism = mechanism;
				break;
			}
	}
}

float SG_RuneMoveJumpVelocity(const sg_rune_move_store_t *store)
{
	(void)store;
	return SG_HOST_ENGINE_JUMP_VELOCITY;
}

float SG_RuneMoveRocketVelocity(const sg_rune_move_store_t *store)
{
	return store ? store->rocket_velocity : 0.0f;
}

float SG_RuneMoveRocketLead(const sg_rune_move_store_t *store)
{
	return store ? store->rocket_lead_seconds : 0.0f;
}

const char *SG_RuneMoveKindString(sg_rune_move_kind_t kind)
{
	static const char *const names[SG_RUNE_MOVE_KIND_COUNT] = {
		"walk", "crouch", "ramp", "jump", "drop", "swim", "air control",
		"rocket jump", "hook", "mover", "external force", "controller action",
		"teleport", "platform", "train"
	};

	return (uint32_t)kind < (uint32_t)SG_RUNE_MOVE_KIND_COUNT ?
		names[kind] : "unknown movement kind";
}
