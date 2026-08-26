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
	uint64_t chain_pairs;
	uint64_t chain_replays;
	uint64_t chain_proofs;
} rune_hook_frontier_state_t;

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

	if (cost_ms <= 0)
		return NULL;
	if (*input->link_count >= RUNE_MAX_LINKS)
	{
		*input->link_overflow = true;
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

static qboolean RuneHook_ProveLowGravitySurface(
	rune_hook_frontier_state_t *state, int from, int to,
	vec3_t control_out, short *cost_ms, byte *exit_speed)
{
	static const float yaw_offsets[] = {
		0.0f, 45.0f, -45.0f, 90.0f, -90.0f, 135.0f, -135.0f, 180.0f
	};
	static const float pitch_offsets[] = {
		0.0f, -30.0f, 30.0f, -60.0f, 60.0f
	};
	const rune_seed_t *seeds = state->input->seeds;
	vec3_t delta;
	float horizontal, base_yaw, base_pitch;
	size_t pitch_index, yaw_index;

	if (SG_RuneProofGravity() > 200)
		return false;
	VectorSubtract(seeds[to].origin, seeds[from].origin, delta);
	horizontal = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
	if (horizontal <= RUNE_HOOK_REACH || horizontal > 1600.0f)
		return false;
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
	return RuneHook_ProveLowGravitySurface(state, from, to, control_out,
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

static qboolean RuneHook_ProveChain(rune_hook_frontier_state_t *state,
	int from, int to, vec3_t control_out[SG_CHAIN_HOOK_ROPE_COUNT],
	short *cost_ms, byte *exit_speed)
{
	static const float yaw_offsets[] = {
		0.0f, 45.0f, -45.0f, 90.0f, -90.0f, 135.0f, -135.0f, 180.0f
	};
	static const float pitch_offsets[] = { 0.0f, -30.0f, 30.0f };
	const sg_rune_hook_frontier_input_t *input = state->input;
	sg_chain_hook_proof_t proof;
	sg_phantom_t phantom;
	vec3_t delta, aim[SG_CHAIN_HOOK_ROPE_COUNT];
	const vec3_t *aim_view = (const vec3_t *)aim;
	float horizontal, base_yaw, base_pitch;
	size_t p0, y0, p1, y1;

	if (!RuneHook_ChainEligible(state, from, to))
		return false;
	VectorSubtract(input->seeds[to].origin, input->seeds[from].origin,
		delta);
	horizontal = sqrtf(delta[0] * delta[0] + delta[1] * delta[1]);
	base_yaw = atan2f(delta[1], delta[0]) * 180.0f / (float)M_PI;
	base_pitch = -atan2f(delta[2], horizontal) * 180.0f /
		(float)M_PI;
	for (p0 = 0; p0 < sizeof(pitch_offsets) / sizeof(pitch_offsets[0]); p0++)
	{
		for (y0 = 0; y0 < sizeof(yaw_offsets) / sizeof(yaw_offsets[0]); y0++)
		{
			for (p1 = 0;
				p1 < sizeof(pitch_offsets) / sizeof(pitch_offsets[0]); p1++)
			{
				for (y1 = 0;
					y1 < sizeof(yaw_offsets) / sizeof(yaw_offsets[0]); y1++)
				{
					VectorSet(aim[0],
						SHORT2ANGLE((short)ANGLE2SHORT(base_pitch +
							pitch_offsets[p0])),
						SHORT2ANGLE((short)ANGLE2SHORT(base_yaw +
							yaw_offsets[y0])), 0.0f);
					VectorSet(aim[1],
						SHORT2ANGLE((short)ANGLE2SHORT(base_pitch +
							pitch_offsets[p1])),
						SHORT2ANGLE((short)ANGLE2SHORT(base_yaw +
							yaw_offsets[y1])), 0.0f);
					if (aim[0][PITCH] < -89.0f ||
						aim[0][PITCH] > 89.0f ||
						aim[1][PITCH] < -89.0f ||
						aim[1][PITCH] > 89.0f)
						continue;
					state->chain_replays++;
					RuneHook_Place(&phantom,
						input->seeds[from].origin);
					if (!SG_OracleChainHookDiscover(&phantom, aim_view,
						input->seeds[to].origin, RIGHT_HANDED, 0.0f,
						control_out, &proof, NULL, true))
						continue;
					if (proof.total_ms <= 0 || proof.total_ms > 32767)
						continue;
					*cost_ms = (short)proof.total_ms;
					*exit_speed = proof.exit_speed;
					return true;
				}
			}
		}
	}
	return false;
}

static qboolean RuneHook_PublishCandidate(rune_hook_frontier_state_t *state,
	const sg_rune_proof_hook_candidate_t *candidate)
{
	vec3_t control, chain_control[SG_CHAIN_HOOK_ROPE_COUNT];
	rune_link_t *link;
	short cost_ms;
	byte exit_speed;

	if (RuneHook_ProveOrdinary(state, candidate->from, candidate->to,
		control, &cost_ms, &exit_speed))
	{
		link = RuneHook_AddLink(state, candidate->from, candidate->to,
			RL_HOOK, cost_ms, exit_speed);
		if (!link)
			return false;
		VectorCopy(control, link->anchor);
		RuneHook_SetEnvelope(state, link, control);
		return true;
	}
	if (!RuneHook_ChainEligible(state, candidate->from, candidate->to))
		return false;
	state->chain_pairs++;
	if (!RuneHook_ProveChain(state, candidate->from, candidate->to,
		chain_control, &cost_ms, &exit_speed))
		return false;
	link = RuneHook_AddLink(state, candidate->from, candidate->to,
		RL_CHAIN_HOOK, cost_ms, exit_speed);
	if (!link)
		return false;
	VectorCopy(chain_control[0], link->anchor);
	VectorCopy(chain_control[1], link->mechanism_anchor);
	RuneHook_SetEnvelope(state, link, chain_control[0]);
	state->chain_proofs++;
	return true;
}

static qboolean RuneHook_InputValid(
	const sg_rune_hook_frontier_input_t *input)
{
	return input && input->seeds && input->seed_count > 0 &&
		input->source_stable && input->source_waterlevel &&
		input->component && input->objective_mask &&
		input->component_count > 0 && input->links && input->link_count &&
		input->link_overflow && input->hook_envelope_count &&
		input->prover_calls;
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
	sg_rune_proof_hook_seed_t *seeds = NULL;
	sg_rune_proof_hook_candidate_t *candidates = NULL;
	uint16_t *component_trials = NULL, *source_trials = NULL;
	size_t *source_cursor = NULL, *component_source_cursor = NULL;
	sg_rune_proof_hook_frontier_t frontier;
	sg_rune_proof_hook_frontier_cursor_t cursor;
	rune_hook_frontier_state_t state;
	size_t selected = 0, trial;
	uint64_t candidates_total = 0, proofs = 0;
	uint32_t batches = 0;
	uint64_t active_component_batches = 0, max_component_trials = 0;
	uint64_t active_source_batches = 0, max_source_trials = 0;
	int index;
	uint64_t candidate_ranks[15] = { 0 };
	uint64_t proof_ranks[15] = { 0 };
	qboolean complete = false;

	if (!RuneHook_InputValid(input))
	{
		sg_host.dprint("rune: hook frontier unavailable reason=input\n");
		return false;
	}
	memset(&state, 0, sizeof(state));
	state.input = input;
	seeds = sg_host.level_alloc(sizeof(*seeds) * (size_t)input->seed_count);
	candidates = sg_host.level_alloc(sizeof(*candidates) *
		SG_RUNE_PROOF_HOOK_FRONTIER_MAX);
	component_trials = sg_host.level_alloc(sizeof(*component_trials) *
		(size_t)input->component_count);
	source_trials = sg_host.level_alloc(sizeof(*source_trials) *
		(size_t)input->seed_count);
	source_cursor = sg_host.level_alloc(sizeof(*source_cursor) *
		(size_t)input->seed_count);
	component_source_cursor = sg_host.level_alloc(
		sizeof(*component_source_cursor) * (size_t)input->component_count);
	if (!seeds || !candidates || !component_trials || !source_trials ||
		!source_cursor || !component_source_cursor)
	{
		sg_host.dprint("rune: hook frontier unavailable reason=allocation\n");
		goto done;
	}
	for (index = 0; index < input->seed_count; index++)
	{
		seeds[index].origin_q8[0] =
			(int32_t)lrintf(input->seeds[index].origin[0] * 8.0f);
		seeds[index].origin_q8[1] =
			(int32_t)lrintf(input->seeds[index].origin[1] * 8.0f);
		seeds[index].origin_q8[2] =
			(int32_t)lrintf(input->seeds[index].origin[2] * 8.0f);
		seeds[index].component = input->component[index];
		seeds[index].objective_mask = input->objective_mask[index];
		seeds[index].water =
			(input->seeds[index].flags & RSF_WATER) != 0;
		seeds[index].stable = input->source_stable[index] != 0;
		seeds[index].waterlevel = input->source_waterlevel[index];
	}
	memset(&frontier, 0, sizeof(frontier));
	frontier.seeds = seeds;
	frontier.seed_count = (size_t)input->seed_count;
	frontier.component_count = (size_t)input->component_count;
	frontier.global_limit = SIZE_MAX;
	frontier.component_limit = UINT16_MAX;
	frontier.source_limit = UINT16_MAX;
	frontier.component_trials = component_trials;
	frontier.source_trials = source_trials;
	frontier.source_cursor = source_cursor;
	frontier.component_source_cursor = component_source_cursor;
	frontier.output = candidates;
	frontier.output_capacity = SG_RUNE_PROOF_HOOK_FRONTIER_MAX;
	SG_RuneProofHookFrontierCursorReset(&cursor);
	frontier.cursor = &cursor;
	for (;;)
	{
		selected = SG_RuneProofSelectHookFrontier(&frontier);
		if (selected > 0)
			batches++;
		for (index = 0; index < input->component_count; index++)
		{
			if (component_trials[index] > 0)
				active_component_batches++;
			if (component_trials[index] > max_component_trials)
				max_component_trials = component_trials[index];
		}
		for (index = 0; index < input->seed_count; index++)
		{
			if (source_trials[index] > 0)
				active_source_batches++;
			if (source_trials[index] > max_source_trials)
				max_source_trials = source_trials[index];
		}
		for (trial = 0; trial < selected; trial++)
		{
			if (candidates[trial].rank < 15)
				candidate_ranks[candidates[trial].rank]++;
			if (RuneHook_PublishCandidate(&state, &candidates[trial]))
			{
				if (candidates[trial].rank < 15)
					proof_ranks[candidates[trial].rank]++;
				proofs++;
			}
			if (*input->link_overflow)
				goto done;
		}
		candidates_total += selected;
		if (cursor.exhausted)
		{
			complete = true;
			break;
		}
		if (selected == 0)
			goto done;
	}
	sg_host.dprint("rune: hook frontier components=%d candidates=%llu "
		"proved_links=%llu batches=%u schedule=rank-component-source-round-robin "
		"exhausted=1\n",
		input->component_count, (unsigned long long)candidates_total,
		(unsigned long long)proofs, batches);
	sg_host.dprint("rune: chain hook pairs=%llu replays=%llu links=%llu "
		"ropes=%u\n",
		(unsigned long long)state.chain_pairs,
		(unsigned long long)state.chain_replays,
		(unsigned long long)state.chain_proofs,
		(unsigned int)SG_CHAIN_HOOK_ROPE_COUNT);
	sg_host.dprint("rune: hook frontier distribution active_component_batches=%llu "
		"max_component_trials=%llu active_source_batches=%llu "
		"max_source_trials=%llu "
		"candidate_ranks=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
		"%llu,%llu,%llu,%llu,%llu,%llu,%llu "
		"proof_ranks=%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,"
		"%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
		(unsigned long long)active_component_batches,
		(unsigned long long)max_component_trials,
		(unsigned long long)active_source_batches,
		(unsigned long long)max_source_trials,
		(unsigned long long)candidate_ranks[0],
		(unsigned long long)candidate_ranks[1],
		(unsigned long long)candidate_ranks[2],
		(unsigned long long)candidate_ranks[3],
		(unsigned long long)candidate_ranks[4],
		(unsigned long long)candidate_ranks[5],
		(unsigned long long)candidate_ranks[6],
		(unsigned long long)candidate_ranks[7],
		(unsigned long long)candidate_ranks[8],
		(unsigned long long)candidate_ranks[9],
		(unsigned long long)candidate_ranks[10],
		(unsigned long long)candidate_ranks[11],
		(unsigned long long)candidate_ranks[12],
		(unsigned long long)candidate_ranks[13],
		(unsigned long long)candidate_ranks[14],
		(unsigned long long)proof_ranks[0],
		(unsigned long long)proof_ranks[1],
		(unsigned long long)proof_ranks[2],
		(unsigned long long)proof_ranks[3],
		(unsigned long long)proof_ranks[4],
		(unsigned long long)proof_ranks[5],
		(unsigned long long)proof_ranks[6],
		(unsigned long long)proof_ranks[7],
		(unsigned long long)proof_ranks[8],
		(unsigned long long)proof_ranks[9],
		(unsigned long long)proof_ranks[10],
		(unsigned long long)proof_ranks[11],
		(unsigned long long)proof_ranks[12],
		(unsigned long long)proof_ranks[13],
		(unsigned long long)proof_ranks[14]);

done:
	if (seeds)
		sg_host.level_free(seeds);
	if (candidates)
		sg_host.level_free(candidates);
	if (component_trials)
		sg_host.level_free(component_trials);
	if (source_trials)
		sg_host.level_free(source_trials);
	if (source_cursor)
		sg_host.level_free(source_cursor);
	if (component_source_cursor)
		sg_host.level_free(component_source_cursor);
	return complete;
}
