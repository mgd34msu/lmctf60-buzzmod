#ifndef SG_COMPOUND_DROP_LIVE_INTERNAL_H
#define SG_COMPOUND_DROP_LIVE_INTERNAL_H

#include "slipgate/sg_compound_drop_live.h"

sg_compound_drop_live_result_t CompoundDropLiveResult(
	sg_compound_drop_live_outcome_t outcome,
	sg_compound_drop_live_failure_t failure,
	sg_replay_reason_t replay_reason, qboolean command_ready);
qboolean CompoundDropLiveHostValid(
	const sg_compound_drop_live_host_t *host);
qboolean CompoundDropLivePoseValid(const sg_replay_pose_t *pose);
qboolean CompoundDropLiveObservationValid(
	const sg_replay_observation_t *observation);
qboolean CompoundDropLiveEventsFromObservation(
	const sg_replay_observation_t *observation,
	sg_drop_live_events_t *events);
sg_compound_drop_live_host_result_t CompoundDropLiveCurrent(
	const sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host);
sg_compound_drop_live_host_result_t CompoundDropLiveAuthorized(
	const sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host);
sg_compound_drop_live_result_t CompoundDropLiveOwnedFailure(
	sg_compound_drop_live_state_t *state,
	sg_compound_drop_live_failure_t failure,
	sg_replay_reason_t replay_reason);
sg_compound_drop_live_result_t CompoundDropLiveActiveResult(
	const sg_compound_drop_live_state_t *state, qboolean command_ready);
qboolean CompoundDropLiveAdvanceTime(int elapsed_ms, int *next_ms);
sg_compound_drop_live_result_t CompoundDropLiveConsumeSweep(
	sg_compound_drop_live_state_t *state,
	const sg_compound_drop_live_host_t *host,
	const sg_replay_pose_t *pose);

#endif
