#include "sg_tactic_pmove_control_runtime_private.h"

#include <string.h>

static void SetError(sg_rune_pmove_control_error_t *error_out,
	sg_rune_pmove_control_error_t error)
{
	if (error_out)
		*error_out = error;
}

static int StateEqual(const sg_rune_pmove_control_state_t *left,
	const sg_rune_pmove_control_state_t *right)
{
	return left && right && left->origin_q8[0] == right->origin_q8[0] &&
		left->origin_q8[1] == right->origin_q8[1] &&
		left->origin_q8[2] == right->origin_q8[2] &&
		left->velocity_q8[0] == right->velocity_q8[0] &&
		left->velocity_q8[1] == right->velocity_q8[1] &&
		left->velocity_q8[2] == right->velocity_q8[2] &&
		left->cell == right->cell && left->standing == right->standing &&
		left->dry == right->dry && left->supported == right->supported &&
		left->support_is_static_world == right->support_is_static_world;
}

static void StateFromPmove(const pmove_state_t *state, uint32_t cell,
	int grounded, uint32_t support_model_index, int water_level,
	sg_rune_pmove_control_state_t *out)
{
	uint32_t axis;

	memset(out, 0, sizeof(*out));
	for (axis = 0U; axis < 3U; axis++)
	{
		out->origin_q8[axis] = state->origin[axis];
		out->velocity_q8[axis] = state->velocity[axis];
	}
	out->cell = cell;
	out->standing = (state->pm_flags & PMF_DUCKED) == 0 ? 1U : 0U;
	out->dry = water_level == 0 ? 1U : 0U;
	out->supported = grounded ? 1U : 0U;
	out->support_is_static_world = grounded &&
		support_model_index == SG_HOST_COLLISION_MODEL_WORLD ? 1U : 0U;
}

int SG_TacticPmoveControlRuntimeAdmit(
	const sg_rune_pmove_control_model_t *model,
	const sg_host_engine_runtime_t *host,
	const sg_host_engine_subject_identity_t *subject, uint32_t region_index,
	uint32_t selected_portal,
	const sg_rune_pmove_control_state_t *live,
	uint64_t authenticated_tail_units,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_tactic_pmove_control_result_t *result_out,
	sg_rune_pmove_control_error_t *error_out)
{
	const sg_rune_pmove_control_region_t *region;
	sg_rune_pmove_control_gradient_t gradient;
	sg_host_engine_walk_gradient_t host_gradient;
	sg_host_pmove_replay_t replay;
	sg_host_pmove_error_t pmove_error;
	sg_host_collision_pose_t source_pose;
	sg_rune_pmove_control_state_t source;
	sg_rune_pmove_control_state_t target;
	float source_origin[3];
	uint32_t step;
	uint32_t transition;
	uint32_t crossing_count = 0U;
	uint64_t source_units;
	uint64_t next_units;

	SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_NONE);
	if (result_out)
		memset(result_out, 0, sizeof(*result_out));
	if (!model || !host || !subject || !live || !workspace || !result_out ||
		region_index >= model->region_count ||
		!SG_RunePmoveControlValidate(model, error_out) ||
		!SG_RunePmoveControlGradient(model, region_index, live, &gradient,
			error_out))
		return 0;
	region = &model->regions[region_index];
	if (selected_portal != region->target_portal)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_PORTAL_MISMATCH);
		return 0;
	}
	host_gradient.longitudinal = gradient.longitudinal;
	host_gradient.lateral_position = gradient.lateral_position;
	host_gradient.reversal_velocity = gradient.reversal_velocity;
	host_gradient.lateral_velocity = gradient.lateral_velocity;
	memset(&replay, 0, sizeof(replay));
	if (!SG_HostEngineRuntimeOwnerReplayLiveWalkGradient(host, subject,
		&host_gradient, workspace, &replay, &pmove_error))
	{
		SetError(error_out, pmove_error == SG_HOST_PMOVE_ERROR_CAPACITY ?
			SG_RUNE_PMOVE_CONTROL_ERROR_INCOMPLETE_REPLAY :
			SG_RUNE_PMOVE_CONTROL_ERROR_STALE_IDENTITY);
		return 0;
	}
	if (memcmp(replay.bsp_identity.bytes, model->identity.bsp_identity,
		SG_RUNE_PMOVE_CONTROL_BSP_IDENTITY_BYTES) != 0 ||
		replay.physics_abi_id != model->identity.physics_abi_id ||
		replay.frame_ms != model->identity.frame_ms ||
		replay.substep_ms != model->identity.substep_ms)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_STALE_IDENTITY);
		return 0;
	}
	if (replay.substep_count != model->identity.substep_count ||
		replay.result.evaluated_steps != model->identity.substep_count ||
		replay.result.elapsed_ms != model->identity.frame_ms)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INCOMPLETE_REPLAY);
		return 0;
	}
	for (step = 0U; step < 3U; step++)
		source_origin[step] = replay.request.state.origin[step] * 0.125f;
	if (!SG_HostEngineRuntimeOwnerClassifyPose(host, subject, source_origin,
		SG_RUNE_STANCE_STANDING, &source_pose) || !source_pose.valid ||
		!source_pose.supported || source_pose.support_is_mover ||
		source_pose.water_level != 0U)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE);
		return 0;
	}
	StateFromPmove(&replay.request.state, region->cell, 1,
		SG_HOST_COLLISION_MODEL_WORLD, 0, &source);
	if (!StateEqual(&source, live))
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_STALE_IDENTITY);
		return 0;
	}
	for (step = 0U; step < replay.trace_count; step++)
		if ((replay.traces[step].result.fraction < 1.0f ||
			replay.traces[step].result.startsolid ||
			replay.traces[step].result.allsolid) &&
			replay.traces[step].result.instance_id != 0U)
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_DYNAMIC_COLLISION);
			return 0;
		}
	for (step = 0U; step < replay.substep_count; step++)
		if (replay.substeps[step].stance != SG_RUNE_STANCE_STANDING ||
			!replay.substeps[step].grounded ||
			replay.substeps[step].support_model_index !=
				SG_HOST_COLLISION_MODEL_WORLD ||
			replay.substeps[step].support_instance_id != 0U ||
			replay.substeps[step].water_level != 0)
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE);
			return 0;
		}
	transition = region->first_transition;
	if (replay.result.state.origin[0] >= region->portal_q8)
	{
		for (step = 0U; step < replay.substep_count; step++)
		{
			const sg_host_pmove_substep_t *substep = &replay.substeps[step];
			int64_t delta_x;
			int64_t crossing_y_numerator;
			int64_t lower;
			int64_t upper;

			if (substep->before_state.origin[0] >= region->portal_q8 ||
				substep->state.origin[0] < region->portal_q8)
				continue;
			delta_x = (int64_t)substep->state.origin[0] -
				substep->before_state.origin[0];
			if (delta_x <= 0)
			{
				SetError(error_out,
					SG_RUNE_PMOVE_CONTROL_ERROR_PORTAL_MISMATCH);
				return 0;
			}
			crossing_y_numerator =
				(int64_t)substep->before_state.origin[1] * delta_x +
				((int64_t)substep->state.origin[1] -
					substep->before_state.origin[1]) *
				((int64_t)region->portal_q8 -
					substep->before_state.origin[0]);
			lower = (int64_t)region->lateral_min_q8 * delta_x;
			upper = (int64_t)region->lateral_max_q8 * delta_x;
			if (crossing_y_numerator < lower || crossing_y_numerator >= upper)
			{
				SetError(error_out,
					SG_RUNE_PMOVE_CONTROL_ERROR_PORTAL_MISMATCH);
				return 0;
			}
			crossing_count++;
		}
		if (source.origin_q8[0] >= region->portal_q8 || crossing_count != 1U)
		{
			SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_PORTAL_MISMATCH);
			return 0;
		}
		transition++;
		StateFromPmove(&replay.result.state, region->target_cell,
			replay.result.grounded, replay.result.support_model_index,
			replay.result.water_level, &target);
	}
	else
		StateFromPmove(&replay.result.state, region->cell,
			replay.result.grounded, replay.result.support_model_index,
			replay.result.water_level, &target);
	if (!SG_RunePmoveControlCheckDescent(model, region_index, &source, &target,
		transition, authenticated_tail_units, &source_units, &next_units,
		error_out))
		return 0;
	result_out->state = target;
	result_out->source_units = source_units;
	result_out->next_units = next_units;
	result_out->live_local_units = model->identity.frame_cost_units;
	result_out->transition = transition;
	return 1;
}
