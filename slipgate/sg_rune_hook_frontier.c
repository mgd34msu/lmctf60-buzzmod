#include "g_local.h"

#include "slipgate/sg_hooks.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_rune_hook_frontier.h"
#include "slipgate/sg_rune_proof.h"
#include "slipgate/sg_util.h"

#include <stdint.h>

#define Q2_MASK_SHOT_GEN 0x6000003
#define RUNE_HOOK_REACH 448.0f
_Static_assert(SG_CHAIN_HOOK_ROPE_COUNT == 2,
	"serialized chain transitions contain exactly two hooks");

typedef struct rune_hook_frontier_state_s
{
	const sg_rune_hook_frontier_input_t *input;
	int retention_limit;
	uint64_t capacity_skips;
	uint64_t air_trace_rejects;
	uint64_t air_replay_rejects[128];
} rune_hook_frontier_state_t;

static qboolean RuneHook_TraceControl(const sg_phantom_t *phantom,
	const vec3_t requested, qboolean discover_distance, vec3_t control,
	vec3_t bite, int *flight_ms);

static void RuneHook_CountProver(rune_hook_frontier_state_t *state)
{
	uint32_t *value = state->input->prover_calls;

	if (*value != UINT32_MAX)
		(*value)++;
}

static byte RuneHook_Heading(float dx, float dy)
{
	float angle = atan2f(dy, dx);

	if (angle < 0.0f)
		angle += (float)(2.0 * M_PI);
	return (byte)(((int)(angle / (float)(2.0 * M_PI) * 256.0f)) & 255);
}

static void RuneHook_Place(sg_phantom_t *phantom, const vec3_t origin)
{
	vec3_t mutable_origin;

	VectorCopy(origin, mutable_origin);
	SG_OraclePlace(phantom, mutable_origin);
}

static rune_link_t *RuneHook_AddLink(rune_hook_frontier_state_t *state,
	int from, int to, rune_action_t action, short cost_ms, byte exit_speed)
{
	const sg_rune_hook_frontier_input_t *input = state->input;
	rune_link_t *link;
	int from_water;
	int to_water;

	if (from < 0 || from >= input->seed_count || to < 0 ||
	    to >= input->seed_count || from == to || cost_ms <= 0)
		return NULL;
	from_water = (input->seeds[from].flags & RSF_WATER) != 0;
	to_water = (input->seeds[to].flags & RSF_WATER) != 0;
	if (!SG_ActionEndpointAllowed((int)action, from_water, to_water))
		return NULL;
	/* Hook shortcuts are proved before declared mechanisms and late topology
	 * closure. Preserve one later link slot per seed instead of allowing valid
	 * hook abundance to consume the entire wire array. Candidate proof still
	 * runs to finite exhaustion; only lower-ranked serialization is compacted. */
	if (*input->link_count >= state->retention_limit)
	{
		state->capacity_skips++;
		return NULL;
	}
	link = &input->links[(*input->link_count)++];
	memset(link, 0, sizeof(*link));
	link->from = from;
	link->to = to;
	link->action = (byte)action;
	link->provenance = RL_PROVEN;
	link->cost_ms = cost_ms;
	link->exit_speed = exit_speed;
	link->heading_slack = 255;
	link->mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	return link;
}

static void RuneHook_SetEnvelope(rune_hook_frontier_state_t *state,
	rune_link_t *link, const vec3_t control)
{
	const rune_seed_t *source = &state->input->seeds[link->from];

	link->heading = RuneHook_Heading(
		cosf(control[YAW] * (float)M_PI / 180.0f),
		sinf(control[YAW] * (float)M_PI / 180.0f));
	link->heading_slack = (source->flags & RSF_WATER)
		? RUNE_WATER_HOOK_CONTROL_MARKER : RUNE_HOOK_CONTROL_SLACK;
	link->min_speed = 0;
	(*state->input->hook_envelope_count)++;
}

static qboolean RuneHook_ProveRay(rune_hook_frontier_state_t *state,
	int from, int to, float pitch, float yaw, float ray_length,
	vec3_t control_out, short *cost_ms, byte *exit_speed)
{
	const rune_seed_t *seeds = state->input->seeds;
	sg_phantom_t phantom;
	sg_hook_proof_t proof;
	vec3_t source, view_angles, forward, right;
	vec3_t muzzle, shot_end, bite, residue;
	trace_t trace;
	int flight_ms;

	source[0] = (short)(seeds[from].origin[0] * 8.0f) * 0.125f;
	source[1] = (short)(seeds[from].origin[1] * 8.0f) * 0.125f;
	source[2] = (short)(seeds[from].origin[2] * 8.0f) * 0.125f;
	view_angles[PITCH] = SHORT2ANGLE((short)ANGLE2SHORT(pitch));
	view_angles[YAW] = SHORT2ANGLE((short)ANGLE2SHORT(yaw));
	view_angles[ROLL] = 0.0f;
	if (view_angles[PITCH] < -89.0f || view_angles[PITCH] > 89.0f)
		return false;

	RuneHook_Place(&phantom, seeds[from].origin);
	AngleVectors(view_angles, forward, right, NULL);
	CTF_HookMuzzle(source, 22.0f, RIGHT_HANDED, forward, right, muzzle);
	VectorNormalize(forward);
	trace = sg_host.trace(source, NULL, NULL, muzzle, NULL,
		Q2_MASK_SHOT_GEN);
	if (trace.fraction < 1.0f || trace.startsolid)
		return false;
	VectorMA(muzzle, ray_length, forward, shot_end);
	trace = sg_host.trace(muzzle, NULL, NULL, shot_end, NULL,
		Q2_MASK_SHOT_GEN);
	if (trace.startsolid || trace.fraction >= 1.0f ||
		trace.ent != g_edicts ||
		(trace.surface && (trace.surface->flags & SURF_SKY)))
		return false;

	VectorCopy(trace.endpos, bite);
	VectorSubtract(bite, muzzle, residue);
	control_out[PITCH] = view_angles[PITCH];
	control_out[YAW] = view_angles[YAW];
	control_out[ROLL] = DotProduct(residue, forward);
	if (control_out[ROLL] < 1.0f ||
		control_out[ROLL] > RUNE_HOOK_MAX_RAY)
		return false;
	VectorMA(muzzle, control_out[ROLL], forward, shot_end);
	VectorSubtract(bite, shot_end, residue);
	if (VectorLength(residue) > RUNE_HOOK_BITE_TOLERANCE)
		return false;
	VectorCopy(shot_end, bite);
	if (CTF_HookPullVelocity(muzzle, bite, residue) < 150 ||
		!SG_OracleHookFlightClear(muzzle, bite))
		return false;

	flight_ms = (int)ceilf(control_out[ROLL] /
		RUNE_HOOK_FRAME_DISTANCE) * 100;
	if (!SG_OracleHookTraverse(&phantom, bite, seeds[to].origin,
		view_angles, RIGHT_HANDED, flight_ms, RUNE_HOOK_DRY_SETTLE_MS,
		0.0f, &proof, NULL, true))
		return false;
	if (flight_ms + proof.pull_ms + proof.settle_ms > 32767)
		return false;
	*cost_ms = (short)(flight_ms + proof.pull_ms + proof.settle_ms);
	*exit_speed = proof.exit_speed;
	return true;
}

static qboolean RuneHook_ProveSurfaceVolume(
	rune_hook_frontier_state_t *state, int from, int to,
	vec3_t control_out, short *cost_ms, byte *exit_speed)
{
	static const float yaw_offsets[] = {
		0.0f, 30.0f, -30.0f, 60.0f, -60.0f,
		90.0f, -90.0f, 135.0f, -135.0f, 180.0f
	};
	static const float pitch_offsets[] = {
		0.0f, -25.0f, 25.0f, -50.0f, 50.0f, -75.0f, 75.0f
	};
	const rune_seed_t *seeds = state->input->seeds;
	vec3_t delta;
	float horizontal, base_yaw, base_pitch;
	size_t pitch_index, yaw_index;

	/* At ordinary gravity this is the expensive connectivity repair after the
	 * fast ceiling and lateral proposals. Base links already connect members of
	 * one component. Low-gravity LMCTF keeps the full surface sweep because its
	 * long hook shortcuts are primary movement, not only topology repair. */
	if (SG_RuneProofGravity() > 200 && state->input->component &&
		state->input->component[from] == state->input->component[to])
		return false;
	VectorSubtract(seeds[to].origin, seeds[from].origin, delta);
	horizontal = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
	base_yaw = atan2f(delta[1], delta[0]) * 180.0f / (float)M_PI;
	base_pitch = -atan2f(delta[2], horizontal) * 180.0f /
		(float)M_PI;
	for (pitch_index = 0;
		pitch_index < sizeof(pitch_offsets) / sizeof(pitch_offsets[0]);
		pitch_index++)
	{
		for (yaw_index = 0;
			yaw_index < sizeof(yaw_offsets) / sizeof(yaw_offsets[0]);
			yaw_index++)
		{
			if (RuneHook_ProveRay(state, from, to,
				base_pitch + pitch_offsets[pitch_index],
				base_yaw + yaw_offsets[yaw_index], RUNE_HOOK_MAX_RAY,
				control_out, cost_ms, exit_speed))
				return true;
		}
	}
	return false;
}

static qboolean RuneHook_ProveOrdinary(rune_hook_frontier_state_t *state,
	int from, int to, vec3_t control_out, short *cost_ms, byte *exit_speed)
{
	const sg_rune_hook_frontier_input_t *input = state->input;
	const rune_seed_t *seeds = input->seeds;
	sg_phantom_t phantom;
	usercmd_t source_command;
	vec3_t source, fire_source, overhead, aim, bite, view_angles;
	vec3_t forward, right, muzzle, residue;
	sg_hook_proof_t proof;
	trace_t trace;
	int flight_ms, source_step, open_overhead = 0;
	qboolean source_water = (seeds[from].flags & RSF_WATER) != 0;

	RuneHook_CountProver(state);
	if ((!source_water && (input->source_waterlevel[from] != 0 ||
		!input->source_stable[from])) ||
		(source_water && (input->source_waterlevel[from] < 2 ||
		(seeds[to].flags & RSF_WATER))))
		return false;

	source[0] = (short)(seeds[from].origin[0] * 8.0f) * 0.125f;
	source[1] = (short)(seeds[from].origin[1] * 8.0f) * 0.125f;
	source[2] = (short)(seeds[from].origin[2] * 8.0f) * 0.125f;
	{
		static const float backs[] = { 0.0f, 0.35f, 0.6f, 0.85f };
		size_t back_index;

		for (back_index = 0;
			back_index < sizeof(backs) / sizeof(backs[0]); back_index++)
		{
			vec3_t shot_end, miss, to_aim;
			trace_t muzzle_trace;
			float shot_length;

			VectorCopy(seeds[to].origin, overhead);
			overhead[0] += (seeds[from].origin[0] - overhead[0]) *
				backs[back_index];
			overhead[1] += (seeds[from].origin[1] - overhead[1]) *
				backs[back_index];
			overhead[2] = (backs[back_index] > 0.0f &&
				seeds[from].origin[2] < seeds[to].origin[2])
				? seeds[to].origin[2] : overhead[2];
			overhead[2] += 24.0f;
			VectorCopy(overhead, aim);
			aim[2] += 512.0f;
			trace = sg_host.trace(overhead, NULL, NULL, aim, NULL,
				MASK_PLAYERSOLID);
			if (trace.fraction >= 1.0f || trace.startsolid)
			{
				if (trace.fraction >= 1.0f && !trace.startsolid)
					open_overhead++;
				continue;
			}
			if (trace.surface && (trace.surface->flags & SURF_SKY))
			{
				open_overhead++;
				continue;
			}
			VectorCopy(trace.endpos, aim);
			aim[2] -= 4.0f;
			if (!SG_HookAimAngles(source, 22.0f, aim, view_angles) ||
				view_angles[PITCH] < -89.0f || view_angles[PITCH] > 89.0f)
				continue;
			RuneHook_Place(&phantom, seeds[from].origin);
			if (source_water)
			{
				memset(&source_command, 0, sizeof(source_command));
				source_command.msec = 0;
				if (!SG_OracleRunWorld(&phantom, &source_command, 1))
					continue;
				for (source_step = 0; source_step < 4; source_step++)
				{
					memset(&source_command, 0, sizeof(source_command));
					source_command.msec = 25;
					source_command.angles[PITCH] =
						ANGLE2SHORT(view_angles[PITCH]) -
						phantom.pms.delta_angles[PITCH];
					source_command.angles[YAW] =
						ANGLE2SHORT(view_angles[YAW]) -
						phantom.pms.delta_angles[YAW];
					source_command.angles[ROLL] =
						-phantom.pms.delta_angles[ROLL];
					if (!SG_OracleRunWorld(&phantom, &source_command, 1))
						break;
				}
				if (source_step != 4 || phantom.waterlevel < 2 ||
					!(phantom.watertype & CONTENTS_WATER) ||
					(phantom.watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
					continue;
				VectorSubtract(phantom.origin, seeds[from].origin, miss);
				if (miss[0] * miss[0] + miss[1] * miss[1] >
					20.0f * 20.0f || fabsf(miss[2]) > 16.0f)
					continue;
				VectorCopy(phantom.origin, fire_source);
			}
			else
			{
				VectorCopy(source, fire_source);
			}
			AngleVectors(view_angles, forward, right, NULL);
			CTF_HookMuzzle(fire_source, 22.0f, RIGHT_HANDED,
				forward, right, muzzle);
			VectorNormalize(forward);
			muzzle_trace = sg_host.trace(fire_source, NULL, NULL, muzzle,
				NULL, Q2_MASK_SHOT_GEN);
			if (muzzle_trace.fraction < 1.0f || muzzle_trace.startsolid)
				continue;
			VectorSubtract(aim, muzzle, to_aim);
			shot_length = VectorLength(to_aim) + 96.0f;
			VectorMA(muzzle, shot_length, forward, shot_end);
			trace = sg_host.trace(muzzle, NULL, NULL, shot_end, NULL,
				Q2_MASK_SHOT_GEN);
			VectorSubtract(trace.endpos, aim, miss);
			if (trace.startsolid || trace.fraction >= 1.0f ||
				trace.ent != g_edicts ||
				(trace.surface && (trace.surface->flags & SURF_SKY)) ||
				VectorLength(miss) > 48.0f)
				continue;
			VectorCopy(trace.endpos, bite);
			VectorSubtract(bite, muzzle, residue);
			control_out[PITCH] = view_angles[PITCH];
			control_out[YAW] = view_angles[YAW];
			control_out[ROLL] = DotProduct(residue, forward);
			if (control_out[ROLL] < 1.0f ||
				control_out[ROLL] > RUNE_HOOK_MAX_RAY)
				continue;
			VectorMA(muzzle, control_out[ROLL], forward, aim);
			VectorSubtract(bite, aim, residue);
			if (VectorLength(residue) > RUNE_HOOK_BITE_TOLERANCE)
				continue;
			VectorCopy(aim, bite);
			if (CTF_HookPullVelocity(muzzle, bite, residue) < 150 ||
				!SG_OracleHookFlightClear(muzzle, bite))
				continue;
			flight_ms = (int)ceilf(control_out[ROLL] /
				RUNE_HOOK_FRAME_DISTANCE) * 100;
			if (!SG_OracleHookTraverse(&phantom, bite, seeds[to].origin,
				view_angles, RIGHT_HANDED, flight_ms,
				source_water ? RUNE_HOOK_WATER_SETTLE_MS
					: RUNE_HOOK_DRY_SETTLE_MS,
				0.0f, &proof, NULL, true))
				continue;
			if (flight_ms + proof.pull_ms + proof.settle_ms > 32767)
				continue;
			*cost_ms = (short)(flight_ms + proof.pull_ms + proof.settle_ms);
			*exit_speed = proof.exit_speed;
			return true;
		}
	}

	if (!source_water && open_overhead == 4 &&
		seeds[to].origin[2] > seeds[from].origin[2])
	{
		static const float yaw_offsets[] = {
			15.0f, -15.0f, 60.0f, -60.0f
		};
		vec3_t delta;
		float horizontal, base_yaw, base_pitch;
		size_t yaw_index;

		VectorSubtract(seeds[to].origin, seeds[from].origin, delta);
		horizontal = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
		if (SG_RuneProofHookLateralWindow(horizontal, delta[2]))
		{
			base_yaw = atan2f(delta[1], delta[0]) * 180.0f /
				(float)M_PI;
			base_pitch = -atan2f(delta[2] + 2.0f, horizontal) *
				180.0f / (float)M_PI;
			for (yaw_index = 0;
				yaw_index < sizeof(yaw_offsets) / sizeof(yaw_offsets[0]);
				yaw_index++)
			{
				if (RuneHook_ProveRay(state, from, to,
					base_pitch + 10.0f,
					base_yaw + yaw_offsets[yaw_index],
					RUNE_HOOK_REACH, control_out, cost_ms,
					exit_speed))
					return true;
			}
		}
	}
	return RuneHook_ProveSurfaceVolume(state, from, to, control_out,
		cost_ms, exit_speed);
}

static qboolean RuneHook_ChainEligible(rune_hook_frontier_state_t *state,
	int from, int to)
{
	const sg_rune_hook_frontier_input_t *input = state->input;
	vec3_t delta;
	float horizontal;

	if (SG_RuneProofGravity() > 200 || from == to ||
	    (input->seeds[from].flags & RSF_WATER) ||
	    (input->seeds[to].flags & RSF_WATER) ||
	    !input->source_stable[from] || input->source_waterlevel[from] != 0)
		return false;
	VectorSubtract(input->seeds[to].origin, input->seeds[from].origin,
		delta);
	horizontal = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
	return horizontal > RUNE_HOOK_REACH &&
	       horizontal <= (float)SG_RUNE_PROOF_CHAIN_HOOK_MAX_HORIZONTAL &&
	       fabsf(delta[2]) <=
	           (float)SG_RUNE_PROOF_CHAIN_HOOK_MAX_VERTICAL;
}

static qboolean RuneHook_InputValid(
	const sg_rune_hook_frontier_input_t *input)
{
	return input && input->seeds && input->seed_count > 0 &&
		input->seed_count <= RUNE_MAX_SEEDS &&
		input->source_stable && input->source_waterlevel &&
		input->source_crouched &&
		input->component && input->objective_mask &&
		input->component_count > 0 && input->links && input->link_count &&
		input->link_overflow && input->hook_envelope_count &&
		input->prover_calls;
}

static qboolean RuneHook_SurfaceRay(const rune_seed_t *seed, float pitch,
	float yaw, vec3_t bite)
{
	vec3_t source, view, forward, right, muzzle, end, pull;
	trace_t trace;

	if (!seed || !bite)
		return false;
	source[0] = (short)(seed->origin[0] * 8.0f) * 0.125f;
	source[1] = (short)(seed->origin[1] * 8.0f) * 0.125f;
	source[2] = (short)(seed->origin[2] * 8.0f) * 0.125f;
	view[PITCH] = SHORT2ANGLE((short)ANGLE2SHORT(pitch));
	view[YAW] = SHORT2ANGLE((short)ANGLE2SHORT(yaw));
	view[ROLL] = 0.0f;
	AngleVectors(view, forward, right, NULL);
	CTF_HookMuzzle(source, 22.0f, RIGHT_HANDED, forward, right, muzzle);
	trace = sg_host.trace(source, NULL, NULL, muzzle, NULL, Q2_MASK_SHOT_GEN);
	if (trace.startsolid || trace.fraction < 1.0f)
		return false;
	VectorNormalize(forward);
	VectorMA(muzzle, RUNE_HOOK_MAX_RAY, forward, end);
	trace = sg_host.trace(muzzle, NULL, NULL, end, NULL, Q2_MASK_SHOT_GEN);
	if (trace.startsolid || trace.fraction >= 1.0f ||
	    trace.ent != g_edicts ||
	    (trace.surface && (trace.surface->flags & SURF_SKY)))
		return false;
	VectorCopy(trace.endpos, bite);
	return CTF_HookPullVelocity(muzzle, bite, pull) >= 150 &&
		SG_OracleHookFlightClear(muzzle, bite);
}

static void RuneHook_NearestSurfaceSeeds(
	const sg_rune_hook_frontier_input_t *input, int from, const vec3_t bite,
	int *cross_out, int *local_out)
{
	int cross = -1, local = -1;
	float cross_score = 0.0f, local_score = 0.0f;

	for (int to = 0; to < input->seed_count; to++)
	{
		vec3_t delta;
		float score;

		if (to == from || input->source_crouched[to] ||
		    ((!input->source_stable[to]) &&
		     !(input->seeds[to].flags & RSF_WATER)))
			continue;
		VectorSubtract(input->seeds[to].origin, bite, delta);
		/* A hook endpoint is a surface point, while a RUNE seed is a valid
		 * player-hull pose. Horizontal separation dominates; vertical distance
		 * admits the floor, ledge, or water volume below a ceiling/wall bite. */
		score = delta[0] * delta[0] + delta[1] * delta[1] +
			0.25f * delta[2] * delta[2];
		if (local < 0 || score < local_score ||
		    (score == local_score && to < local))
		{
			local = to;
			local_score = score;
		}
		if (input->component[from] != input->component[to] &&
		    (cross < 0 || score < cross_score ||
		     (score == cross_score && to < cross)))
		{
			cross = to;
			cross_score = score;
		}
	}
	*cross_out = cross;
	*local_out = local;
}

static qboolean RuneHook_LinkExists(
	const sg_rune_hook_frontier_input_t *input, int from, int to)
{
	for (int index = 0; index < *input->link_count; index++)
		if (input->links[index].from == from &&
		    input->links[index].to == to &&
		    (input->links[index].action == RL_HOOK ||
		     input->links[index].action == RL_CHAIN_HOOK))
			return true;
	return false;
}

static qboolean RuneHook_ProveSurfaceRay(
	rune_hook_frontier_state_t *state, int from, int to, float pitch,
	float yaw)
{
	vec3_t control;
	rune_link_t *link;
	short cost_ms;
	byte exit_speed;

	if (to < 0 || RuneHook_LinkExists(state->input, from, to) ||
	    !RuneHook_ProveRay(state, from, to, pitch, yaw,
	        RUNE_HOOK_MAX_RAY, control, &cost_ms, &exit_speed))
		return false;
	link = RuneHook_AddLink(state, from, to, RL_HOOK, cost_ms, exit_speed);
	if (!link)
		return false;
	VectorCopy(control, link->anchor);
	RuneHook_SetEnvelope(state, link, control);
	return true;
}

static qboolean RuneHook_ProveAirRay(
	rune_hook_frontier_state_t *state, int from, int to,
	const sg_phantom_t *fire_state, byte launch_heading, byte launch_frames,
	float pitch, float yaw)
{
	sg_phantom_t phantom;
	sg_hook_proof_t proof;
	vec3_t requested, control, view, bite;
	rune_link_t *link;
	int flight_ms;
	int total_ms;

	if (!fire_state || launch_frames == 0 || to < 0 ||
	    RuneHook_LinkExists(state->input, from, to))
		return false;
	phantom = *fire_state;
	requested[PITCH] = pitch;
	requested[YAW] = yaw;
	requested[ROLL] = 0.0f;
	if (!RuneHook_TraceControl(&phantom, requested, true, control, bite,
	        &flight_ms))
	{
		state->air_trace_rejects++;
		return false;
	}
	view[PITCH] = control[PITCH];
	view[YAW] = control[YAW];
	view[ROLL] = 0.0f;
	if (!SG_OracleHookTraverse(&phantom, bite,
	        state->input->seeds[to].origin, view, RIGHT_HANDED,
	        flight_ms, RUNE_HOOK_DRY_SETTLE_MS,
	        fire_state->velocity[2], &proof, NULL, true))
	{
		unsigned int reason = (unsigned int)proof.reason;

		if (reason < sizeof(state->air_replay_rejects) /
		                  sizeof(state->air_replay_rejects[0]))
			state->air_replay_rejects[reason]++;
		return false;
	}
	total_ms = ((int)launch_frames + 1) * 100 + flight_ms +
		proof.pull_ms + proof.settle_ms;
	if (total_ms <= 0 || total_ms > 32767)
		return false;
	link = RuneHook_AddLink(state, from, to, RL_HOOK,
	    (short)total_ms, proof.exit_speed);
	if (!link)
		return false;
	VectorCopy(control, link->anchor);
	link->heading = launch_heading;
	link->heading_slack = RUNE_AIR_HOOK_CONTROL_MARKER;
	link->min_speed = launch_frames;
	(*state->input->hook_envelope_count)++;
	return true;
}

static qboolean RuneHook_AirFrame(const rune_seed_t *seed, float yaw,
	byte frame, sg_phantom_t *phantom)
{
	byte heading = RuneHook_Heading(
		cosf(yaw * (float)M_PI / 180.0f),
		sinf(yaw * (float)M_PI / 180.0f));

	return SG_OracleAirHookLaunchFrame(seed->origin, heading, frame,
		phantom);
}

typedef struct rune_air_hook_candidate_s
{
	int from;
	int to;
	int group;
	float launch_yaw;
	float hook_pitch;
	float hook_yaw;
	float score;
	byte launch_heading;
	byte launch_frames;
} rune_air_hook_candidate_t;

typedef struct rune_air_hook_candidates_s
{
	rune_air_hook_candidate_t *items;
	size_t count;
	size_t capacity;
} rune_air_hook_candidates_t;

static void RuneHook_AirCandidatesFree(rune_air_hook_candidates_t *set)
{
	if (!set)
		return;
	if (set->items)
		sg_host.level_free(set->items);
	memset(set, 0, sizeof(*set));
}

static qboolean RuneHook_AirCandidatesAppend(
	rune_air_hook_candidates_t *set,
	const rune_air_hook_candidate_t *candidate)
{
	rune_air_hook_candidate_t *grown;
	size_t capacity;

	if (!set || !candidate)
		return false;
	if (set->count == set->capacity)
	{
		capacity = set->capacity ? set->capacity * 2U : 1024U;
		if (capacity < set->capacity ||
		    capacity > SIZE_MAX / sizeof(*set->items))
			return false;
		grown = sg_host.level_alloc(capacity * sizeof(*set->items));
		if (!grown)
			return false;
		if (set->items)
		{
			memcpy(grown, set->items, set->count * sizeof(*set->items));
			sg_host.level_free(set->items);
		}
		set->items = grown;
		set->capacity = capacity;
	}
	set->items[set->count++] = *candidate;
	return true;
}

static int RuneHook_AirCandidateCompare(const void *left, const void *right)
{
	const rune_air_hook_candidate_t *a = left;
	const rune_air_hook_candidate_t *b = right;

	if (a->score != b->score)
		return a->score > b->score ? -1 : 1;
	if (a->group != b->group)
		return a->group < b->group ? -1 : 1;
	if (a->launch_frames != b->launch_frames)
		return a->launch_frames < b->launch_frames ? -1 : 1;
	if (a->from != b->from)
		return a->from < b->from ? -1 : 1;
	if (a->to != b->to)
		return a->to < b->to ? -1 : 1;
	if (a->launch_heading != b->launch_heading)
		return a->launch_heading < b->launch_heading ? -1 : 1;
	if (a->hook_pitch != b->hook_pitch)
		return a->hook_pitch < b->hook_pitch ? -1 : 1;
	if (a->hook_yaw != b->hook_yaw)
		return a->hook_yaw < b->hook_yaw ? -1 : 1;
	return 0;
}

static float RuneHook_AirCandidateScore(const sg_phantom_t *airborne,
	const vec3_t bite, const vec3_t destination, float ray_length,
	byte launch_frames)
{
	vec3_t pull, travel, landing;
	float pull_length, travel_length, alignment;

	VectorSubtract(bite, airborne->origin, pull);
	VectorSubtract(destination, airborne->origin, travel);
	VectorSubtract(destination, bite, landing);
	pull_length = VectorNormalize(pull);
	travel_length = VectorNormalize(travel);
	alignment = pull_length > 0.0f && travel_length > 0.0f
		? DotProduct(pull, travel) : -1.0f;
	/* Higher alignment makes the first production pull accelerate toward the
	 * nominated landing.  Remaining terms prefer a nearby landing surface and
	 * the cheaper exact launch/bolt witness; they order candidates only and
	 * never admit a link. */
	return alignment * 1000000.0f - VectorLength(landing) * 100.0f -
		ray_length - (float)launch_frames * 100.0f;
}

static qboolean RuneHook_CollectAirCandidates(
	const sg_rune_hook_frontier_input_t *input, int source_component,
	const float *pitches, size_t pitch_count,
	const float *yaws, size_t yaw_count,
	const byte *cross_connected, rune_air_hook_candidates_t *cross_set,
	uint64_t *pose_count, uint64_t *ray_count, uint64_t *surface_count)
{
	if (!input || source_component < 0 ||
	    source_component >= input->component_count || !pitches ||
	    !pitch_count || !yaws || !yaw_count || !cross_connected ||
	    !cross_set || !pose_count || !ray_count || !surface_count)
		return false;
	for (int from = 0; from < input->seed_count; from++)
	{
		qboolean source_valid = !input->source_crouched[from] &&
			!(input->seeds[from].flags & RSF_WATER) &&
			input->source_stable[from] && input->source_waterlevel[from] == 0;

		if (!source_valid || input->component[from] != source_component)
			continue;
		for (size_t launch_index = 0; launch_index < yaw_count; launch_index++)
		{
			sg_phantom_t airborne;
			qboolean left_support = false;

			memset(&airborne, 0, sizeof(airborne));
			for (unsigned int frame = 0; frame < 255U; frame++)
			{
				sg_phantom_t fire_state;
				byte launch_frames = (byte)(frame + 1U);

				if (!RuneHook_AirFrame(&input->seeds[from], yaws[launch_index],
				        (byte)frame, &airborne))
					break;
				if (!airborne.groundentity)
					left_support = true;
				else if (left_support)
					break;
				if (!left_support || airborne.waterlevel > 0 ||
				    (airborne.watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
					continue;
				fire_state = airborne;
				/* Live execution spends one zero-movement frame acquiring the
				 * serialized hook view before Cmd_Hook_f consumes it.  The coast is
				 * part of the witnessed 3D firing pose and traversal time. */
				if (!SG_OracleAirHookCoastFrame(&fire_state) ||
				    fire_state.groundentity || fire_state.waterlevel > 0 ||
				    (fire_state.watertype &
				        (CONTENTS_LAVA | CONTENTS_SLIME)))
					continue;
				(*pose_count)++;
				for (size_t pitch_index = 0; pitch_index < pitch_count;
				     pitch_index++)
				for (size_t hook_yaw_index = 0; hook_yaw_index < yaw_count;
				     hook_yaw_index++)
				{
					rune_air_hook_candidate_t candidate;
					vec3_t requested, control, bite;
					int flight_ms;
					int cross, local_target;

					(*ray_count)++;
					requested[PITCH] = pitches[pitch_index];
					requested[YAW] = yaws[hook_yaw_index];
					requested[ROLL] = 0.0f;
					if (!RuneHook_TraceControl(&fire_state, requested, true,
					        control, bite, &flight_ms))
						continue;
					(*surface_count)++;
					RuneHook_NearestSurfaceSeeds(input, from, bite,
					    &cross, &local_target);
					if (cross >= 0 && cross != from &&
					    !RuneHook_LinkExists(input, from, cross) &&
					    !cross_connected[(size_t)source_component *
					        (size_t)input->component_count +
					        (size_t)input->component[cross]])
					{
						float launch_radians;

						memset(&candidate, 0, sizeof(candidate));
						candidate.from = from;
						candidate.to = cross;
						candidate.group = input->component[cross];
						candidate.launch_yaw = yaws[launch_index];
						candidate.hook_pitch = pitches[pitch_index];
						candidate.hook_yaw = yaws[hook_yaw_index];
						launch_radians = candidate.launch_yaw *
							(float)M_PI / 180.0f;
						candidate.launch_heading = RuneHook_Heading(
							cosf(launch_radians), sinf(launch_radians));
						candidate.launch_frames = launch_frames;
						candidate.score = RuneHook_AirCandidateScore(
							&fire_state, bite,
							input->seeds[cross].origin, control[ROLL],
							launch_frames);
						if (!RuneHook_AirCandidatesAppend(cross_set,
						        &candidate))
							return false;
					}
					(void)local_target;
				}
			}
		}
	}
	return true;
}

static qboolean RuneHook_ReplayAirCandidate(
	const sg_rune_hook_frontier_input_t *input,
	const rune_air_hook_candidate_t *candidate, sg_phantom_t *airborne)
{
	if (!input || !candidate || !airborne || candidate->launch_frames == 0)
		return false;
	memset(airborne, 0, sizeof(*airborne));
	for (unsigned int frame = 0; frame < candidate->launch_frames; frame++)
		if (!RuneHook_AirFrame(&input->seeds[candidate->from],
		        candidate->launch_yaw, (byte)frame, airborne))
			return false;
	return SG_OracleAirHookCoastFrame(airborne) &&
		!airborne->groundentity && airborne->waterlevel == 0;
}

static void RuneHook_CloseComponentReachability(byte *reachable, int count)
{
	if (!reachable || count <= 0)
		return;
	for (int through = 0; through < count; through++)
		for (int from = 0; from < count; from++)
		{
			if (!reachable[(size_t)from * (size_t)count +
			        (size_t)through])
				continue;
			for (int to = 0; to < count; to++)
				if (reachable[(size_t)through * (size_t)count +
				        (size_t)to])
					reachable[(size_t)from * (size_t)count +
					    (size_t)to] = 1;
		}
}

static uint64_t RuneHook_ProveAirCandidateGroups(
	rune_hook_frontier_state_t *state, rune_air_hook_candidates_t *set,
	byte *component_reachable, int component_count, uint64_t *proof_count)
{
	uint64_t links = 0U;
	size_t index;
	unsigned int reported_percent = 0U;

	if (!state || !set || !component_reachable || component_count <= 0 ||
	    !proof_count)
		return 0U;
	if (set->count > 1U)
		qsort(set->items, set->count, sizeof(*set->items),
		    RuneHook_AirCandidateCompare);
	for (index = 0; index < set->count; index++)
	{
		const rune_air_hook_candidate_t *candidate = &set->items[index];
		sg_phantom_t airborne;
		int source_component = state->input->component[candidate->from];

		if (source_component >= 0 && source_component < component_count &&
		    candidate->group >= 0 && candidate->group < component_count &&
		    !component_reachable[(size_t)source_component *
		        (size_t)component_count + (size_t)candidate->group])
		{
			(*proof_count)++;
			if (RuneHook_ReplayAirCandidate(state->input, candidate,
			        &airborne) &&
			    RuneHook_ProveAirRay(state, candidate->from, candidate->to,
			        &airborne, candidate->launch_heading,
			        candidate->launch_frames, candidate->hook_pitch,
			        candidate->hook_yaw))
			{
				component_reachable[(size_t)source_component *
				    (size_t)component_count +
				    (size_t)candidate->group] = 1;
				RuneHook_CloseComponentReachability(component_reachable,
				    component_count);
				links++;
			}
		}
		if (set->count != 0U)
		{
			unsigned int percent = (unsigned int)(((index + 1U) * 100U) /
				set->count);

			if (percent >= reported_percent + 5U || index + 1U == set->count)
			{
				reported_percent = percent;
				sg_host.dprint("rune: airborne hook proof progress=%u%% "
					"candidates=%llu/%llu proofs=%llu links=%llu\n",
					percent, (unsigned long long)(index + 1U),
					(unsigned long long)set->count,
					(unsigned long long)*proof_count,
					(unsigned long long)links);
			}
		}
	}
	return links;
}

qboolean SG_RuneProveHook(const sg_rune_hook_frontier_input_t *input,
	int from, int to, vec3_t control_out, short *cost_ms, byte *exit_speed)
{
	rune_hook_frontier_state_t state;

	if (!input || !input->seeds || !input->source_stable ||
		!input->source_waterlevel || !input->prover_calls ||
		from < 0 || from >= input->seed_count ||
		to < 0 || to >= input->seed_count || !control_out ||
		!cost_ms || !exit_speed)
		return false;
	memset(&state, 0, sizeof(state));
	state.input = input;
	return RuneHook_ProveOrdinary(&state, from, to, control_out,
		cost_ms, exit_speed);
}

static qboolean RuneHook_TargetInputValid(
	const sg_rune_hook_frontier_input_t *input, int from, int to)
{
	return input && input->seeds && input->seed_count > 0 &&
	       input->source_stable && input->source_waterlevel &&
	       input->prover_calls && from >= 0 && from < input->seed_count &&
	       to >= 0 && to < input->seed_count && from != to;
}

static qboolean RuneHook_AimFromPose(const sg_phantom_t *phantom,
	const vec3_t bite, vec3_t view)
{
	vec3_t forward, right, muzzle, direction;
	int iteration;

	if (!SG_HookAimAngles(phantom->origin, 22.0f, bite, view))
		return false;
	for (iteration = 0; iteration < 4; iteration++)
	{
		AngleVectors(view, forward, right, NULL);
		CTF_HookMuzzle(phantom->origin, 22.0f, RIGHT_HANDED,
			forward, right, muzzle);
		VectorSubtract(bite, muzzle, direction);
		if (VectorNormalize(direction) == 0.0f)
			return false;
		vectoangles(direction, view);
		view[PITCH] = SHORT2ANGLE((short)ANGLE2SHORT(view[PITCH]));
		view[YAW] = SHORT2ANGLE((short)ANGLE2SHORT(view[YAW]));
		view[ROLL] = 0.0f;
	}
	return view[PITCH] >= -89.0f && view[PITCH] <= 89.0f;
}

static qboolean RuneHook_PrepareSource(
	const sg_rune_hook_frontier_input_t *input, int from,
	const vec3_t view, sg_phantom_t *phantom)
{
	const rune_seed_t *seed = &input->seeds[from];
	qboolean water = (seed->flags & RSF_WATER) != 0;
	int step;

	if ((!water && (!input->source_stable[from] ||
	                  input->source_waterlevel[from] != 0)) ||
	    (water && input->source_waterlevel[from] < 2))
		return false;
	RuneHook_Place(phantom, seed->origin);
	if (!water)
		return true;
	for (step = 0; step < 4; step++)
	{
		usercmd_t command;

		memset(&command, 0, sizeof(command));
		command.msec = 25;
		command.angles[PITCH] = ANGLE2SHORT(view[PITCH]) -
			phantom->pms.delta_angles[PITCH];
		command.angles[YAW] = ANGLE2SHORT(view[YAW]) -
			phantom->pms.delta_angles[YAW];
		command.angles[ROLL] = -phantom->pms.delta_angles[ROLL];
		if (!SG_OracleRunWorld(phantom, &command, 1))
			return false;
	}
	if (phantom->waterlevel < 2 ||
	    !(phantom->watertype & CONTENTS_WATER) ||
	    (phantom->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		return false;
	{
		vec3_t miss;

		VectorSubtract(phantom->origin, seed->origin, miss);
		if (miss[0] * miss[0] + miss[1] * miss[1] > 20.0f * 20.0f ||
		    fabsf(miss[2]) > 16.0f)
			return false;
	}
	return true;
}

static qboolean RuneHook_TraceControl(const sg_phantom_t *phantom,
	const vec3_t requested, qboolean discover_distance, vec3_t control,
	vec3_t bite, int *flight_ms)
{
	vec3_t view, forward, right, muzzle, shot_end, delta;
	trace_t trace;
	float ray_length, distance;

	if (!phantom || !requested || !control || !bite || !flight_ms ||
	    !isfinite(requested[PITCH]) || !isfinite(requested[YAW]) ||
	    !isfinite(requested[ROLL]))
		return false;
	view[PITCH] = SHORT2ANGLE((short)ANGLE2SHORT(requested[PITCH]));
	view[YAW] = SHORT2ANGLE((short)ANGLE2SHORT(requested[YAW]));
	view[ROLL] = 0.0f;
	if (view[PITCH] < -89.0f || view[PITCH] > 89.0f ||
	    (!discover_distance &&
	     (view[PITCH] != requested[PITCH] || view[YAW] != requested[YAW] ||
	      requested[ROLL] < 1.0f || requested[ROLL] > RUNE_HOOK_MAX_RAY)))
		return false;
	AngleVectors(view, forward, right, NULL);
	CTF_HookMuzzle(phantom->origin, 22.0f, RIGHT_HANDED,
		forward, right, muzzle);
	trace = sg_host.trace(phantom->origin, NULL, NULL, muzzle, NULL,
		Q2_MASK_SHOT_GEN);
	if (trace.startsolid || trace.fraction < 1.0f)
		return false;
	VectorNormalize(forward);
	ray_length = discover_distance ? RUNE_HOOK_MAX_RAY :
		requested[ROLL] + 96.0f;
	if (ray_length > RUNE_HOOK_MAX_RAY)
		ray_length = RUNE_HOOK_MAX_RAY;
	VectorMA(muzzle, ray_length, forward, shot_end);
	trace = sg_host.trace(muzzle, NULL, NULL, shot_end, NULL,
		Q2_MASK_SHOT_GEN);
	if (trace.startsolid || trace.fraction >= 1.0f ||
	    trace.ent != g_edicts ||
	    (trace.surface && (trace.surface->flags & SURF_SKY)))
		return false;
	VectorSubtract(trace.endpos, muzzle, delta);
	distance = DotProduct(delta, forward);
	if (distance < 1.0f || distance > RUNE_HOOK_MAX_RAY ||
	    (!discover_distance &&
	     fabsf(distance - requested[ROLL]) > RUNE_HOOK_BITE_TOLERANCE))
		return false;
	control[PITCH] = view[PITCH];
	control[YAW] = view[YAW];
	control[ROLL] = distance;
	VectorMA(muzzle, distance, forward, bite);
	if (!SG_OracleHookFlightClear(muzzle, bite))
		return false;
	*flight_ms = (int)ceilf(distance / RUNE_HOOK_FRAME_DISTANCE) * 100;
	return true;
}

static void RuneHook_Q8Point(const int32_t q8[3], vec3_t point)
{
	point[0] = (float)q8[0] * 0.125f;
	point[1] = (float)q8[1] * 0.125f;
	point[2] = (float)q8[2] * 0.125f;
}

static qboolean RuneHook_ProveOneNomination(
	const sg_rune_hook_frontier_input_t *input, int from, int to,
	const int32_t bite_q8[3], sg_rune_hook_nomination_proof_t *out)
{
	sg_phantom_t phantom;
	sg_hook_proof_t proof;
	vec3_t nominated_bite, aim, bite;
	int flight_ms, settle_ms;

	RuneHook_Q8Point(bite_q8, nominated_bite);
	RuneHook_Place(&phantom, input->seeds[from].origin);
	if (!RuneHook_AimFromPose(&phantom, nominated_bite, aim) ||
	    !RuneHook_PrepareSource(input, from, aim, &phantom) ||
	    !RuneHook_AimFromPose(&phantom, nominated_bite, aim))
		return false;
	aim[ROLL] = 0.0f;
	if (!RuneHook_TraceControl(&phantom, aim, true, out->control[0],
	        bite, &flight_ms))
		return false;
	settle_ms = (input->seeds[from].flags & RSF_WATER) ?
		RUNE_HOOK_WATER_SETTLE_MS : RUNE_HOOK_DRY_SETTLE_MS;
	if (!SG_OracleHookTraverse(&phantom, bite, input->seeds[to].origin,
	        out->control[0], RIGHT_HANDED, flight_ms, settle_ms, 0.0f,
	        &proof, NULL, true) ||
	    flight_ms + proof.pull_ms + proof.settle_ms > 32767)
		return false;
	out->action = RL_HOOK;
	out->cost_ms = (short)(flight_ms + proof.pull_ms + proof.settle_ms);
	out->exit_speed = proof.exit_speed;
	return out->cost_ms > 0;
}

static qboolean RuneHook_ProveChainNomination(
	const sg_rune_hook_frontier_input_t *input, int from, int to,
	const int32_t bite_q8[2][3], sg_rune_hook_nomination_proof_t *out)
{
	rune_hook_frontier_state_t state;
	sg_phantom_t phantom;
	sg_hook_proof_t first_proof;
	sg_chain_hook_proof_t proof;
	vec3_t nominated[2], aim[2], first_bite, first_control;
	const vec3_t *aim_view = (const vec3_t *)aim;
	int first_flight;

	memset(&state, 0, sizeof(state));
	state.input = input;
	if (!RuneHook_ChainEligible(&state, from, to))
		return false;
	RuneHook_Q8Point(bite_q8[0], nominated[0]);
	RuneHook_Q8Point(bite_q8[1], nominated[1]);
	RuneHook_Place(&phantom, input->seeds[from].origin);
	if (!RuneHook_AimFromPose(&phantom, nominated[0], aim[0]))
		return false;
	aim[0][ROLL] = 0.0f;
	if (!RuneHook_TraceControl(&phantom, aim[0], true, first_control,
	        first_bite, &first_flight) ||
	    !SG_OracleHookTraverseMonitored(&phantom, first_bite,
	        input->seeds[to].origin, first_control, RIGHT_HANDED,
	        first_flight, 0, 0.0f, &first_proof, NULL, true, NULL, NULL,
	        true, SG_HOOK_REPLAY_TERMINAL_RELEASE_HANDOFF) ||
	    !RuneHook_AimFromPose(&phantom, nominated[1], aim[1]))
		return false;
	aim[1][ROLL] = 0.0f;
	RuneHook_Place(&phantom, input->seeds[from].origin);
	if (!SG_OracleChainHookDiscover(&phantom, aim_view,
	        input->seeds[to].origin, RIGHT_HANDED, 0.0f, out->control,
	        &proof, NULL, true) || proof.total_ms <= 0 ||
	    proof.total_ms > 32767)
		return false;
	out->action = RL_CHAIN_HOOK;
	out->cost_ms = (short)proof.total_ms;
	out->exit_speed = proof.exit_speed;
	return true;
}

qboolean SG_RuneProveHookNomination(
	const sg_rune_hook_frontier_input_t *input, int from, int to,
	uint8_t rope_count, const int32_t bite_q8[2][3],
	sg_rune_hook_nomination_proof_t *out)
{
	rune_hook_frontier_state_t state;
	qboolean proved;

	if (out)
		memset(out, 0, sizeof(*out));
	if (!out || !bite_q8 || !RuneHook_TargetInputValid(input, from, to) ||
	    (rope_count != 1U && rope_count != 2U))
		return false;
	memset(&state, 0, sizeof(state));
	state.input = input;
	RuneHook_CountProver(&state);
	proved = rope_count == 1U ?
		RuneHook_ProveOneNomination(input, from, to, bite_q8[0], out) :
		RuneHook_ProveChainNomination(input, from, to, bite_q8, out);
	if (!proved)
		memset(out, 0, sizeof(*out));
	return proved;
}

qboolean SG_RuneReproveHookControl(
	const sg_rune_hook_frontier_input_t *input, int from, int to,
	rune_action_t action, const vec3_t control[2],
	sg_rune_hook_nomination_proof_t *out)
{
	rune_hook_frontier_state_t state;
	sg_phantom_t phantom;
	qboolean proved = false;

	if (out)
		memset(out, 0, sizeof(*out));
	if (!out || !control || !RuneHook_TargetInputValid(input, from, to) ||
	    (action != RL_HOOK && action != RL_CHAIN_HOOK))
		return false;
	memset(&state, 0, sizeof(state));
	state.input = input;
	RuneHook_CountProver(&state);
	if (action == RL_HOOK)
	{
		sg_hook_proof_t proof;
		vec3_t bite;
		int flight_ms, settle_ms;

		if (!RuneHook_PrepareSource(input, from, control[0], &phantom) ||
		    !RuneHook_TraceControl(&phantom, control[0], false,
		        out->control[0], bite, &flight_ms))
			goto done;
		settle_ms = (input->seeds[from].flags & RSF_WATER) ?
			RUNE_HOOK_WATER_SETTLE_MS : RUNE_HOOK_DRY_SETTLE_MS;
		if (!SG_OracleHookTraverse(&phantom, bite,
		        input->seeds[to].origin, out->control[0], RIGHT_HANDED,
		        flight_ms, settle_ms, 0.0f, &proof, NULL, true) ||
		    flight_ms + proof.pull_ms + proof.settle_ms > 32767)
			goto done;
		out->action = RL_HOOK;
		out->cost_ms =
			(short)(flight_ms + proof.pull_ms + proof.settle_ms);
		out->exit_speed = proof.exit_speed;
		proved = out->cost_ms > 0;
	}
	else
	{
		sg_chain_hook_proof_t proof;

		RuneHook_Place(&phantom, input->seeds[from].origin);
		if (!SG_OracleChainHookTraverse(&phantom, control,
		        input->seeds[to].origin, RIGHT_HANDED, 0.0f, &proof,
		        NULL, true) || proof.total_ms <= 0 || proof.total_ms > 32767)
			goto done;
		out->action = RL_CHAIN_HOOK;
		VectorCopy(control[0], out->control[0]);
		VectorCopy(control[1], out->control[1]);
		out->cost_ms = (short)proof.total_ms;
		out->exit_speed = proof.exit_speed;
		proved = true;
	}

done:
	if (!proved)
		memset(out, 0, sizeof(*out));
	return proved;
}

qboolean SG_RuneGenerateHookFrontier(
	const sg_rune_hook_frontier_input_t *input)
{
	static const float pitches[] = {
		-75.0f, -50.0f, -25.0f, 0.0f, 25.0f, 50.0f, 75.0f
	};
	static const float yaws[] = {
		0.0f, 45.0f, 90.0f, 135.0f,
		180.0f, 225.0f, 270.0f, 315.0f
	};
	rune_hook_frontier_state_t state;
	rune_air_hook_candidates_t air_set;
	uint64_t rays_done = 0U;
	uint64_t rays_total;
	uint64_t surface_hits = 0U;
	uint64_t candidate_proofs = 0U;
	uint64_t links = 0U;
	uint64_t air_poses = 0U;
	uint64_t air_rays = 0U;
	uint64_t air_surfaces = 0U;
	uint64_t air_candidates = 0U;
	uint64_t air_proofs = 0U;
	uint64_t air_links = 0U;
	unsigned int reported_percent = 0U;
	byte *cross_connected = NULL;
	byte *local_direction_link = NULL;

	if (!RuneHook_InputValid(input))
	{
		sg_host.dprint("rune: hook volume unavailable reason=input\n");
		return false;
	}
	for (int seed = 0; seed < input->seed_count; seed++)
		if (input->component[seed] < 0 ||
		    input->component[seed] >= input->component_count)
		{
			sg_host.dprint("rune: hook volume unavailable "
				"reason=component-identity\n");
			return false;
		}
	memset(&state, 0, sizeof(state));
	memset(&air_set, 0, sizeof(air_set));
	state.input = input;
	state.retention_limit = RUNE_MAX_LINKS - input->seed_count;
	cross_connected = sg_host.level_alloc(
		(size_t)input->component_count * (size_t)input->component_count);
	local_direction_link = sg_host.level_alloc(
		(size_t)input->seed_count * (sizeof(yaws) / sizeof(yaws[0])));
	if (!cross_connected || !local_direction_link)
	{
		if (cross_connected) sg_host.level_free(cross_connected);
		if (local_direction_link) sg_host.level_free(local_direction_link);
		sg_host.dprint("rune: hook volume unavailable reason=allocation\n");
		return false;
	}
	memset(cross_connected, 0,
		(size_t)input->component_count * (size_t)input->component_count);
	for (int component = 0; component < input->component_count; component++)
		cross_connected[(size_t)component *
		    (size_t)input->component_count + (size_t)component] = 1;
	memset(local_direction_link, 0,
		(size_t)input->seed_count * (sizeof(yaws) / sizeof(yaws[0])));
	rays_total = (uint64_t)input->seed_count *
		(uint64_t)(sizeof(pitches) / sizeof(pitches[0])) *
		(uint64_t)(sizeof(yaws) / sizeof(yaws[0]));
	sg_host.dprint("rune: hook volume poses=%d rays=%llu gravity=%d\n",
		input->seed_count, (unsigned long long)rays_total,
		(int)SG_RuneProofGravity());
	for (int from = 0; from < input->seed_count; from++)
	{
		qboolean source_water =
			(input->seeds[from].flags & RSF_WATER) != 0;
		qboolean source_valid = !input->source_crouched[from] &&
			((!source_water && input->source_stable[from] &&
			  input->source_waterlevel[from] == 0) ||
			 (source_water && input->source_waterlevel[from] >= 2));

		for (size_t pitch_index = 0;
		     pitch_index < sizeof(pitches) / sizeof(pitches[0]);
		     pitch_index++)
		{
			for (size_t yaw_index = 0;
			     yaw_index < sizeof(yaws) / sizeof(yaws[0]); yaw_index++)
			{
				vec3_t bite;
				int cross, local;

				rays_done++;
				if (!source_valid || !RuneHook_SurfaceRay(
				        &input->seeds[from], pitches[pitch_index],
				        yaws[yaw_index], bite))
					goto progress;
				surface_hits++;
				RuneHook_NearestSurfaceSeeds(input, from, bite, &cross, &local);
				if (cross >= 0 &&
				    !cross_connected[(size_t)input->component[from] *
				        (size_t)input->component_count +
				        (size_t)input->component[cross]])
				{
					candidate_proofs++;
					if (RuneHook_ProveSurfaceRay(&state, from, cross,
					        pitches[pitch_index], yaws[yaw_index]))
					{
						cross_connected[(size_t)input->component[from] *
						    (size_t)input->component_count +
						    (size_t)input->component[cross]] = 1;
						RuneHook_CloseComponentReachability(cross_connected,
						    input->component_count);
						links++;
					}
				}
				{
					size_t direction = (size_t)from *
						(sizeof(yaws) / sizeof(yaws[0])) + yaw_index;

				if (local >= 0 && local != cross &&
				    !local_direction_link[direction])
				{
					candidate_proofs++;
					if (RuneHook_ProveSurfaceRay(&state, from, local,
					        pitches[pitch_index], yaws[yaw_index]))
					{
						local_direction_link[direction] = 1;
						links++;
					}
				}
				}

progress:
				if (rays_total != 0U)
				{
					unsigned int percent =
						(unsigned int)((rays_done * 100U) / rays_total);
					if (percent >= reported_percent + 5U ||
					    rays_done == rays_total)
					{
						reported_percent = percent;
						sg_host.dprint("rune: hook volume progress=%u%% "
							"rays=%llu/%llu surfaces=%llu proofs=%llu "
							"links=%llu\n", percent,
							(unsigned long long)rays_done,
							(unsigned long long)rays_total,
							(unsigned long long)surface_hits,
							(unsigned long long)candidate_proofs,
							(unsigned long long)links);
					}
				}
			}
		}
	}
	/* Candidate discovery uses real Pmove and exact BSP traces, but those cheap
	 * operations run before the multi-second hook oracle.  The global sweep
	 * ranks all candidate edges, then fully replays only until each missing
	 * component connection proves one edge.  An unsuccessful connection still
	 * exhausts every candidate after all better bridges have had their chance;
	 * this changes ordering, not completeness.
	 * Same-component airborne flings are runtime route enrichment, not missing
	 * topology; ordinary grounded hook discovery already retains those local
	 * directional shortcuts. */
	for (int component = 0; component < input->component_count; component++)
	{
		size_t candidates_before = air_set.count;
		uint64_t component_candidates;

		if (!RuneHook_CollectAirCandidates(input, component, pitches,
		        sizeof(pitches) / sizeof(pitches[0]), yaws,
		        sizeof(yaws) / sizeof(yaws[0]), cross_connected, &air_set,
		        &air_poses,
		        &air_rays, &air_surfaces))
		{
			RuneHook_AirCandidatesFree(&air_set);
			sg_host.level_free(local_direction_link);
			sg_host.level_free(cross_connected);
			sg_host.dprint("rune: airborne hook unavailable "
				"reason=candidate-allocation\n");
			return false;
		}
		component_candidates = (uint64_t)(air_set.count - candidates_before);
		air_candidates += component_candidates;
		sg_host.dprint("rune: airborne hook discovery progress=%d%% "
			"components=%d/%d poses=%llu rays=%llu surfaces=%llu "
			"candidates=%llu component_candidates=%llu\n",
			(int)(((uint64_t)(component + 1) * 100U) /
			    (uint64_t)input->component_count), component + 1,
			input->component_count, (unsigned long long)air_poses,
			(unsigned long long)air_rays,
			(unsigned long long)air_surfaces,
			(unsigned long long)air_candidates,
			(unsigned long long)component_candidates);
	}
	air_links = RuneHook_ProveAirCandidateGroups(&state, &air_set,
		cross_connected, input->component_count, &air_proofs);
	RuneHook_AirCandidatesFree(&air_set);
	sg_host.dprint("rune: hook volume complete poses=%d rays=%llu "
		"surfaces=%llu proofs=%llu retained_links=%llu "
		"air_poses=%llu air_rays=%llu air_surfaces=%llu "
		"air_candidates=%llu air_proofs=%llu "
		"air_links=%llu capacity_skips=%llu "
		"chain_source=human-learning\n", input->seed_count,
		(unsigned long long)rays_done,
		(unsigned long long)surface_hits,
		(unsigned long long)candidate_proofs,
		(unsigned long long)links,
		(unsigned long long)air_poses,
		(unsigned long long)air_rays,
		(unsigned long long)air_surfaces,
		(unsigned long long)air_candidates,
		(unsigned long long)air_proofs,
		(unsigned long long)air_links,
		(unsigned long long)state.capacity_skips);
	sg_host.level_free(local_direction_link);
	sg_host.level_free(cross_connected);
	return true;
}
