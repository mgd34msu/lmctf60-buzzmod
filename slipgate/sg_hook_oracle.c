#include "../g_local.h"
#include "sg_hooks.h"
#include "sg_local.h"
#include "sg_oracle_internal.h"

#include <math.h>
#include <string.h>

/*
 * One production end-frame pull. The fixed view is part of a proved graph
 * hook's command profile, so the current phantom origin plus that view yields
 * the same handed muzzle start the live weapon will use. Return the integer
 * rope length so the prover can make release decisions in the same units.
 */
static int HookOraclePullVelocity(const sg_phantom_t *ph,
	const vec3_t bite, const vec3_t view_angles, int hand, vec3_t velocity)
{
	vec3_t angles, forward, right, muzzle;

	VectorCopy(view_angles, angles);
	AngleVectors(angles, forward, right, NULL);
	CTF_HookMuzzle(ph->origin, 22.0f, hand, forward, right, muzzle);
	return CTF_HookPullVelocity(muzzle, bite, velocity);
}

int SG_OracleHookStep(sg_phantom_t *ph, const vec3_t bite,
	const vec3_t view_angles, int hand)
{
	int rope;

	rope = HookOraclePullVelocity(ph, bite, view_angles, hand,
		ph->velocity);

	/* write back into the fixed-point state Pmove will read */
	ph->pms.velocity[0] = (short)(ph->velocity[0] * 8.0f);
	ph->pms.velocity[1] = (short)(ph->velocity[1] * 8.0f);
	ph->pms.velocity[2] = (short)(ph->velocity[2] * 8.0f);
	return rope;
}

static qboolean HookOracleArrivalMayTrace(const sg_phantom_t *ph,
	const vec3_t destination)
{
	vec3_t delta;

	if (!ph)
		return false;
	VectorSubtract(destination, ph->origin, delta);
	return delta[0] * delta[0] + delta[1] * delta[1] <
	           SG_REPLAY_ARRIVE_RADIUS * SG_REPLAY_ARRIVE_RADIUS &&
	       delta[2] > -SG_REPLAY_ARRIVE_Z &&
	       delta[2] < SG_REPLAY_ARRIVE_Z &&
	       (ph->groundentity || ph->waterlevel >= 2) &&
	       !(ph->waterlevel > 0 &&
	         (ph->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)));
}

static void HookOracleObservation(const sg_phantom_t *ph,
	const vec3_t bite, const vec3_t destination, const vec3_t view_angles,
	int hand, qboolean check_rope, qboolean check_contact, edict_t *passent,
	sg_replay_observation_t *observation)
{
	vec3_t pull_velocity;

	if (!observation)
		return;
	memset(observation, 0, sizeof(*observation));
	if (!ph)
		return;
	observation->contaminated = SG_OracleReplayContaminated();
	observation->door_passed = ph->door_passed;
	observation->contact_clear = true;
	if (!observation->contaminated && check_contact &&
	    HookOracleArrivalMayTrace(ph, destination))
		observation->contact_clear = SG_OracleReplayContactClear(ph->origin,
			destination, passent);
	observation->hook_rope_valid = check_rope;
	if (check_rope)
		observation->hook_rope_length = HookOraclePullVelocity(ph, bite,
			view_angles, hand, pull_velocity);
}

qboolean SG_OracleHookTraverseMonitored(sg_phantom_t *ph,
	const vec3_t bite,
	const vec3_t destination, const vec3_t view_angles, int hand,
	int flight_ms, int settle_limit_ms, float old_frame_z,
	sg_hook_proof_t *proof, edict_t *passent, qboolean world_only,
	sg_oracle_hook_monitor_fn monitor, void *monitor_context,
	qboolean fling_release, sg_hook_replay_terminal_t terminal)
{
	sg_hook_replay_spec_t spec;
	sg_hook_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_replay_status_t status;
	usercmd_t cmd;
	qboolean result = false;
	qboolean fixed_point_valid = false;
	sg_phantom_t fixed_point_phantom;
	usercmd_t fixed_point_command;
	sg_oracle_replay_scope_t scope;

	if (!ph || !proof || flight_ms < SG_REPLAY_FRAME_MS ||
	    flight_ms > SG_REPLAY_HOOK_FLIGHT_MAX_MS ||
	    (flight_ms % SG_REPLAY_FRAME_MS) != 0 ||
	    (terminal != SG_HOOK_REPLAY_TERMINAL_SETTLE &&
	     terminal != SG_HOOK_REPLAY_TERMINAL_RELEASE_HANDOFF) ||
	    (terminal == SG_HOOK_REPLAY_TERMINAL_SETTLE &&
	     (settle_limit_ms < RUNE_HOOK_DRY_SETTLE_MS ||
	      settle_limit_ms > (fling_release ? SG_REPLAY_HOOK_FLING_SETTLE_MS :
	                                      RUNE_HOOK_WATER_SETTLE_MS))) ||
	    (terminal == SG_HOOK_REPLAY_TERMINAL_RELEASE_HANDOFF &&
	     settle_limit_ms != 0))
		return false;
	memset(proof, 0, sizeof(*proof));
	/* The server is single-threaded, but keep this API scoped so every return
	 * restores the default offline context. */
	SG_OracleReplayScopeBegin(&scope, passent, world_only);
	if (!SG_OracleReplayStartClear(ph))
		goto done;
	if (monitor && !monitor(monitor_context, ph, ph->origin, ph->origin, 0))
		goto done;
	memset(&spec, 0, sizeof(spec));
	VectorCopy(bite, spec.bite);
	VectorCopy(destination, spec.destination);
	VectorCopy(view_angles, spec.view_angles);
	spec.flight_ms = flight_ms;
	spec.settle_limit_ms = settle_limit_ms;
	spec.expected_release_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_pull_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_settle_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_settle_ms = SG_REPLAY_TIME_DISCOVER;
	spec.fling_release = fling_release;
	spec.terminal = terminal;
	SG_OracleReplayPose(ph, &pose);
	HookOracleObservation(ph, bite, destination, view_angles, hand,
		false, false, passent, &observation);
	status = SG_HookReplayBegin(&state, &spec, &pose, &observation,
		old_frame_z);
	while (status == SG_REPLAY_RUNNING)
	{
		qboolean check_contact;

		if (state.phase == SG_HOOK_REPLAY_WAIT_ATTACH)
		{
			if (monitor && !monitor(monitor_context, ph, ph->origin,
			                        ph->origin, state.progress.elapsed_ms))
				goto done;
			status = SG_HookReplayAttached(&state, &pose);
			if (status == SG_REPLAY_RUNNING)
			{
				proof->attach_pms = state.attach_pms;
				proof->attach_groundentity = state.attach_grounded;
				proof->attach_watertype = state.attach_watertype;
				proof->attach_waterlevel = state.attach_waterlevel;
			}
			continue;
		}
		if (state.phase == SG_HOOK_REPLAY_WAIT_PULL)
		{
			if (monitor && !monitor(monitor_context, ph, ph->origin,
			                        ph->origin, state.progress.elapsed_ms))
				goto done;
			SG_OracleHookStep(ph, bite, view_angles, hand);
			SG_OracleReplayPose(ph, &pose);
			if (monitor && !monitor(monitor_context, ph, ph->origin,
			                        ph->origin, state.progress.elapsed_ms))
				goto done;
			status = SG_HookReplayPullApplied(&state, &pose);
			continue;
		}
		/* Legacy settlement checks once at each frame start.  Reuse a
		 * post-command observation within the frame so a failed contact trace
		 * is not repeated before the next command. */
		if (state.phase == SG_HOOK_REPLAY_SETTLE &&
		    state.phase_step == 0 && !state.arrived_in_frame)
			HookOracleObservation(ph, bite, destination, view_angles, hand,
				false, true, passent, &observation);
		status = SG_HookReplayPreStep(&state, &pose, &observation, &cmd);
		if (status != SG_REPLAY_RUNNING)
			break;
		{
			vec3_t before;

			VectorCopy(ph->origin, before);
			if (monitor && !monitor(monitor_context, ph, before, before,
			                        state.progress.elapsed_ms))
				goto done;
			if (!(monitor == NULL && world_only && fixed_point_valid &&
			      memcmp(ph, &fixed_point_phantom, sizeof(*ph)) == 0 &&
			      memcmp(&cmd, &fixed_point_command, sizeof(cmd)) == 0))
			{
				sg_phantom_t step_start = *ph;

				SG_OracleRun(ph, &cmd, 1);
				if (monitor == NULL && world_only &&
				    !SG_OracleReplayContaminated() &&
				    memcmp(ph, &step_start, sizeof(*ph)) == 0)
				{
					fixed_point_phantom = *ph;
					fixed_point_command = cmd;
					fixed_point_valid = true;
				}
			}
			if (monitor && !monitor(monitor_context, ph, before, ph->origin,
			                        state.progress.elapsed_ms +
			                            SG_REPLAY_STEP_MS))
				goto done;
		}
		SG_OracleReplayPose(ph, &pose);
		check_contact = state.phase == SG_HOOK_REPLAY_SETTLE &&
			(!state.arrived_in_frame ||
			 (state.phase_step + 1 ==
			      SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS &&
			  SG_ReplayFallDelta(state.progress.old_frame_z,
			      pose.velocity[2], pose.grounded, pose.waterlevel) <=
			      SG_RUNE_PROOF_DAMAGING_FALL_DELTA));
		HookOracleObservation(ph, bite, destination, view_angles, hand,
			!SG_OracleReplayContaminated() &&
			    ((state.phase == SG_HOOK_REPLAY_ATTACH_FRAME &&
			      state.phase_step + 1 ==
			          SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS) ||
			     (state.phase == SG_HOOK_REPLAY_PULL_FRAME &&
			      !state.release_requested)),
			check_contact, passent, &observation);
		/* If substep four first discovers settlement, legacy immediately
		 * performs a second same-pose trace for terminal persistence after its
		 * boundary hazard checks.  Preserve that call only when those checks
		 * can pass; an already-latched arrival used the single trace above as
		 * its persistence check. */
		if (!observation.contaminated &&
		    state.phase == SG_HOOK_REPLAY_SETTLE &&
		    !state.arrived_in_frame &&
		    state.phase_step + 1 ==
		        SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS &&
		    HookOracleArrivalMayTrace(ph, destination) &&
		    observation.contact_clear &&
		    SG_ReplayFallDelta(state.progress.old_frame_z,
		        pose.velocity[2], pose.grounded, pose.waterlevel) <=
		        SG_RUNE_PROOF_DAMAGING_FALL_DELTA)
			observation.contact_clear = SG_OracleReplayContactClear(
				ph->origin, destination, passent);
		status = SG_HookReplayPostStep(&state, &pose, &observation);
		if (status == SG_REPLAY_RUNNING && state.release_requested &&
		    !state.release_applied)
		{
			/* ctf_hook_abort clears vertical velocity and oldvelocity Z on
			 * support.  The pure reducer owns the history; this adapter owns
			 * the exact external phantom write. */
			if (ph->groundentity)
			{
				ph->velocity[2] = 0.0f;
				ph->pms.velocity[2] = 0;
			}
			SG_OracleReplayPose(ph, &pose);
			status = SG_HookReplayReleaseApplied(&state, &pose);
		}
	}
	if (status == SG_REPLAY_ARRIVED || status == SG_REPLAY_RELEASED ||
	    (status == SG_REPLAY_FAILED &&
	     state.progress.reason == SG_REPLAY_REASON_DOOR_PASSED &&
	     state.progress.arrival_ms != SG_REPLAY_TIME_DISCOVER))
	{
		proof->pull_ms = state.pull_ms;
		proof->release_ms = state.release_ms;
		proof->settle_arrival_ms = state.settle_arrival_ms;
		proof->settle_ms = state.settle_ms;
		proof->exit_speed = state.progress.exit_speed;
		proof->fling_release = state.spec.fling_release;
		result = status == (terminal == SG_HOOK_REPLAY_TERMINAL_SETTLE
		    ? SG_REPLAY_ARRIVED : SG_REPLAY_RELEASED);
	}
done:
	if (ph->door_passed)
		result = false;
	SG_OracleReplayScopeEnd(&scope);
	return result;
}

qboolean SG_OracleHookTraverse(sg_phantom_t *ph, const vec3_t bite,
	const vec3_t destination, const vec3_t view_angles, int hand,
	int flight_ms, int settle_limit_ms, float old_frame_z,
	sg_hook_proof_t *proof, edict_t *passent, qboolean world_only)
{
	sg_phantom_t start;

	if (!ph)
		return false;
	start = *ph;
	if (SG_OracleHookTraverseMonitored(ph, bite, destination,
	    view_angles, hand, flight_ms, settle_limit_ms, old_frame_z, proof,
	    passent, world_only, NULL, NULL, false,
	    SG_HOOK_REPLAY_TERMINAL_SETTLE))
		return true;
	*ph = start;
	if (ph->pms.gravity > 200)
		return false;
	return SG_OracleHookTraverseMonitored(ph, bite, destination,
	    view_angles, hand, flight_ms, SG_REPLAY_HOOK_FLING_SETTLE_MS,
	    old_frame_z, proof, passent, world_only, NULL, NULL, true,
	    SG_HOOK_REPLAY_TERMINAL_SETTLE);
}

static qboolean HookOracleChainControl(const sg_phantom_t *ph,
	const vec3_t control, qboolean discover_distance, int hand,
	edict_t *passent, vec3_t canonical_control, vec3_t view_angles,
	vec3_t bite, int *flight_ms)
{
	vec3_t forward, right, muzzle, shot_end, delta;
	trace_t trace;
	float traced_distance, trace_distance;

	if (!ph || !control || !canonical_control || !view_angles || !bite ||
	    !flight_ms ||
	    !isfinite(control[PITCH]) || !isfinite(control[YAW]) ||
	    !isfinite(control[ROLL]) ||
	    (!discover_distance && (control[ROLL] < 1.0f ||
	                            control[ROLL] > RUNE_HOOK_MAX_RAY)))
		return false;
	view_angles[PITCH] = SHORT2ANGLE((short)ANGLE2SHORT(control[PITCH]));
	view_angles[YAW] = SHORT2ANGLE((short)ANGLE2SHORT(control[YAW]));
	view_angles[ROLL] = 0.0f;
	if (view_angles[PITCH] < -89.0f || view_angles[PITCH] > 89.0f ||
	    view_angles[PITCH] != control[PITCH] ||
	    view_angles[YAW] != control[YAW])
		return false;
	AngleVectors(view_angles, forward, right, NULL);
	CTF_HookMuzzle(ph->origin, 22.0f, hand, forward, right, muzzle);
	trace = sg_host.trace(ph->origin, NULL, NULL, muzzle, passent, MASK_SHOT);
	if (trace.startsolid || trace.fraction < 1.0f)
		return false;
	VectorNormalize(forward);
	trace_distance = discover_distance ? RUNE_HOOK_MAX_RAY :
	                                     control[ROLL] + 96.0f;
	if (trace_distance > RUNE_HOOK_MAX_RAY)
		trace_distance = RUNE_HOOK_MAX_RAY;
	VectorMA(muzzle, trace_distance, forward, shot_end);
	trace = sg_host.trace(muzzle, NULL, NULL, shot_end, passent, MASK_SHOT);
	if (trace.startsolid || trace.fraction >= 1.0f || trace.ent != g_edicts ||
	    (trace.surface && (trace.surface->flags & SURF_SKY)))
		return false;
	VectorSubtract(trace.endpos, muzzle, delta);
	traced_distance = DotProduct(delta, forward);
	if (traced_distance < 1.0f || traced_distance > RUNE_HOOK_MAX_RAY ||
	    (!discover_distance &&
	     fabsf(traced_distance - control[ROLL]) > RUNE_HOOK_BITE_TOLERANCE))
		return false;
	canonical_control[PITCH] = view_angles[PITCH];
	canonical_control[YAW] = view_angles[YAW];
	canonical_control[ROLL] = traced_distance;
	VectorMA(muzzle, traced_distance, forward, bite);
	if (!SG_OracleHookFlightClear(muzzle, bite))
		return false;
	*flight_ms = (int)ceilf(traced_distance /
	                           RUNE_HOOK_FRAME_DISTANCE) *
	             SG_REPLAY_FRAME_MS;
	return true;
}

static void HookOracleChainSpec(sg_hook_replay_spec_t *spec,
	const vec3_t bite, const vec3_t destination, const vec3_t view_angles,
	int flight_ms, const sg_hook_proof_t *proof,
	sg_hook_replay_terminal_t terminal)
{
	memset(spec, 0, sizeof(*spec));
	VectorCopy(bite, spec->bite);
	VectorCopy(destination, spec->destination);
	VectorCopy(view_angles, spec->view_angles);
	spec->flight_ms = flight_ms;
	spec->settle_limit_ms = terminal ==
	    SG_HOOK_REPLAY_TERMINAL_RELEASE_HANDOFF ? 0 :
	    (proof->fling_release ? SG_REPLAY_HOOK_FLING_SETTLE_MS :
	                            RUNE_HOOK_DRY_SETTLE_MS);
	spec->expected_release_ms = proof->release_ms;
	spec->expected_pull_ms = proof->pull_ms;
	spec->expected_settle_arrival_ms = terminal ==
	    SG_HOOK_REPLAY_TERMINAL_RELEASE_HANDOFF ? SG_REPLAY_TIME_DISCOVER :
	    proof->settle_arrival_ms;
	spec->expected_settle_ms = terminal ==
	    SG_HOOK_REPLAY_TERMINAL_RELEASE_HANDOFF ? SG_REPLAY_TIME_DISCOVER :
	    proof->settle_ms;
	spec->fling_release = proof->fling_release;
	spec->terminal = terminal;
}

static qboolean HookOracleChainExact(sg_phantom_t *ph,
	vec3_t control[SG_CHAIN_HOOK_ROPE_COUNT],
	const vec3_t destination, int hand, float old_frame_z,
	const sg_chain_hook_proof_t *proof, edict_t *passent)
{
	sg_chain_hook_replay_state_t state;
	sg_chain_hook_replay_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	usercmd_t command;
	vec3_t checked_view, checked_bite;
	int checked_flight, steps = 0;
	float frame_old_z = old_frame_z;

	SG_OracleReplayPose(ph, &pose);
	HookOracleObservation(ph, proof->replay.rope[0].bite, destination,
	    proof->replay.rope[0].view_angles, hand, false, false, passent,
	    &observation);
	result = SG_ChainHookReplayBegin(&state, &proof->replay, &pose,
	                                 &observation, old_frame_z);
	while (result.status == SG_REPLAY_RUNNING && steps++ < 1024)
	{
		int rope = state.phase == SG_CHAIN_HOOK_REPLAY_SECOND_ROPE ? 1 : 0;

		if ((state.phase == SG_CHAIN_HOOK_REPLAY_FIRST_ROPE ||
		     state.phase == SG_CHAIN_HOOK_REPLAY_SECOND_ROPE) &&
		    state.rope.phase == SG_HOOK_REPLAY_WAIT_ATTACH)
		{
			result = SG_ChainHookReplayEvent(&state,
			    SG_CHAIN_HOOK_REPLAY_EVENT_ATTACHED, &pose, &observation,
			    state.rope.progress.old_frame_z);
			continue;
		}
		if ((state.phase == SG_CHAIN_HOOK_REPLAY_FIRST_ROPE ||
		     state.phase == SG_CHAIN_HOOK_REPLAY_SECOND_ROPE) &&
		    state.rope.phase == SG_HOOK_REPLAY_WAIT_PULL)
		{
			SG_OracleHookStep(ph, proof->replay.rope[rope].bite,
			                  proof->replay.rope[rope].view_angles, hand);
			SG_OracleReplayPose(ph, &pose);
			result = SG_ChainHookReplayEvent(&state,
			    SG_CHAIN_HOOK_REPLAY_EVENT_PULL_APPLIED, &pose, &observation,
			    state.rope.progress.old_frame_z);
			continue;
		}
		HookOracleObservation(ph, proof->replay.rope[rope].bite,
		    destination, proof->replay.rope[rope].view_angles, hand,
		    state.phase != SG_CHAIN_HOOK_REPLAY_SECOND_AIM &&
		        state.rope.phase != SG_HOOK_REPLAY_SETTLE,
		    state.phase == SG_CHAIN_HOOK_REPLAY_SECOND_ROPE &&
		        state.rope.phase == SG_HOOK_REPLAY_SETTLE,
		    passent, &observation);
		result = SG_ChainHookReplayPreStep(&state, &pose, &observation,
		                                   &command);
		if (result.status != SG_REPLAY_RUNNING)
			break;
		SG_OracleRun(ph, &command, 1);
		SG_OracleReplayPose(ph, &pose);
		HookOracleObservation(ph, proof->replay.rope[rope].bite,
		    destination, proof->replay.rope[rope].view_angles, hand,
		    state.phase != SG_CHAIN_HOOK_REPLAY_SECOND_AIM &&
		        state.rope.phase != SG_HOOK_REPLAY_SETTLE,
		    state.phase == SG_CHAIN_HOOK_REPLAY_SECOND_ROPE &&
		        state.rope.phase == SG_HOOK_REPLAY_SETTLE,
		    passent, &observation);
		result = SG_ChainHookReplayPostStep(&state, &pose, &observation,
		                                     frame_old_z);
		if (result.effect == SG_CHAIN_HOOK_REPLAY_EFFECT_RELEASE)
		{
			if (ph->groundentity)
			{
				ph->velocity[2] = 0.0f;
				ph->pms.velocity[2] = 0;
			}
			SG_OracleReplayPose(ph, &pose);
			result = SG_ChainHookReplayEvent(&state,
			    SG_CHAIN_HOOK_REPLAY_EVENT_RELEASE_APPLIED, &pose,
			    &observation, state.rope.progress.old_frame_z);
			if (state.phase == SG_CHAIN_HOOK_REPLAY_SECOND_AIM)
				frame_old_z = proof->replay.refire_start.old_frame_z;
		}
		else if (result.effect == SG_CHAIN_HOOK_REPLAY_EFFECT_FIRE_NEXT)
		{
			vec3_t checked_control;

			if (!HookOracleChainControl(ph, control[1], false, hand, passent,
			        checked_control, checked_view, checked_bite, &checked_flight) ||
			    checked_flight != proof->replay.rope[1].flight_ms ||
			    memcmp(checked_bite, proof->replay.rope[1].bite,
			           sizeof(checked_bite)) != 0)
				return false;
			result = SG_ChainHookReplayEvent(&state,
			    SG_CHAIN_HOOK_REPLAY_EVENT_NEXT_FIRED, &pose, &observation,
			    frame_old_z);
		}
	}
	return result.status == SG_REPLAY_ARRIVED;
}

static qboolean HookOracleChainTraverse(sg_phantom_t *ph,
	const vec3_t control[SG_CHAIN_HOOK_ROPE_COUNT],
	const vec3_t destination, int hand, float old_frame_z,
	sg_chain_hook_proof_t *proof, edict_t *passent, qboolean world_only,
	qboolean discover_distance,
	vec3_t discovered_control[SG_CHAIN_HOOK_ROPE_COUNT])
{
	sg_phantom_t start, final_end;
	sg_replay_pose_t pose;
	usercmd_t command;
	vec3_t canonical[2], bite[2], view[2];
	int flight[2], aim_step;
	sg_oracle_replay_scope_t scope;
	qboolean result = false;

	if (!ph || !control || !destination || !proof || !isfinite(old_frame_z))
		return false;
	memset(proof, 0, sizeof(*proof));
	start = *ph;
	SG_OracleReplayScopeBegin(&scope, passent, world_only);
	if (!HookOracleChainControl(ph, control[0], discover_distance, hand,
	        passent, canonical[0], view[0], bite[0], &flight[0]) ||
	    !SG_OracleHookTraverseMonitored(ph, bite[0], destination, view[0],
	        hand, flight[0], 0, old_frame_z, &proof->rope[0], passent,
	        world_only, NULL, NULL, true,
	        SG_HOOK_REPLAY_TERMINAL_RELEASE_HANDOFF))
		goto done;
	SG_OracleReplayPose(ph, &pose);
	proof->replay.refire_start.pose = pose;
	proof->replay.refire_start.old_frame_z = ph->groundentity ? 0.0f :
	                                                         ph->velocity[2];
	for (aim_step = 0; aim_step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS;
	     aim_step++)
	{
		vec3_t second_view;

		second_view[PITCH] = control[1][PITCH];
		second_view[YAW] = control[1][YAW];
		second_view[ROLL] = 0.0f;
		SG_OracleReplayPose(ph, &pose);
		if (!SG_HookReplayFixedViewCommand(&pose, second_view, &command))
			goto done;
		SG_OracleRun(ph, &command, 1);
		if (SG_OracleReplayContaminated())
			goto done;
	}
	SG_OracleReplayPose(ph, &pose);
	proof->replay.second_fire.pose = pose;
	proof->replay.second_fire.old_frame_z =
	    proof->replay.refire_start.old_frame_z;
	if (!HookOracleChainControl(ph, control[1], discover_distance, hand,
	        passent, canonical[1], view[1], bite[1], &flight[1]) ||
	    !SG_OracleHookTraverse(ph, bite[1], destination, view[1], hand,
	        flight[1], RUNE_HOOK_DRY_SETTLE_MS,
	        proof->replay.second_fire.old_frame_z, &proof->rope[1], passent,
	        world_only))
		goto done;
	final_end = *ph;
	HookOracleChainSpec(&proof->replay.rope[0], bite[0], destination,
	    view[0], flight[0], &proof->rope[0],
	    SG_HOOK_REPLAY_TERMINAL_RELEASE_HANDOFF);
	HookOracleChainSpec(&proof->replay.rope[1], bite[1], destination,
	    view[1], flight[1], &proof->rope[1],
	    SG_HOOK_REPLAY_TERMINAL_SETTLE);
	*ph = start;
	if (!HookOracleChainExact(ph, canonical, destination, hand, old_frame_z,
	                            proof, passent))
		goto done;
	if (memcmp(&ph->pms, &final_end.pms, sizeof(ph->pms)) != 0)
		goto done;
	proof->total_ms = flight[0] + proof->rope[0].pull_ms +
	                  SG_REPLAY_FRAME_MS + flight[1] +
	                  proof->rope[1].pull_ms + proof->rope[1].settle_ms;
	proof->exit_speed = proof->rope[1].exit_speed;
	result = proof->total_ms > 0 && proof->total_ms <= 32767;
	if (result && discovered_control)
	{
		VectorCopy(canonical[0], discovered_control[0]);
		VectorCopy(canonical[1], discovered_control[1]);
	}
done:
	if (!result)
		*ph = start;
	SG_OracleReplayScopeEnd(&scope);
	return result;
}

qboolean SG_OracleChainHookTraverse(sg_phantom_t *ph,
	const vec3_t control[SG_CHAIN_HOOK_ROPE_COUNT],
	const vec3_t destination, int hand, float old_frame_z,
	sg_chain_hook_proof_t *proof, edict_t *passent, qboolean world_only)
{
	return HookOracleChainTraverse(ph, control, destination, hand,
	    old_frame_z, proof, passent, world_only, false, NULL);
}

qboolean SG_OracleChainHookDiscover(sg_phantom_t *ph,
	const vec3_t aim[SG_CHAIN_HOOK_ROPE_COUNT],
	const vec3_t destination, int hand, float old_frame_z,
	vec3_t control_out[SG_CHAIN_HOOK_ROPE_COUNT],
	sg_chain_hook_proof_t *proof, edict_t *passent, qboolean world_only)
{
	return HookOracleChainTraverse(ph, aim, destination, hand,
	    old_frame_z, proof, passent, world_only, true, control_out);
}
