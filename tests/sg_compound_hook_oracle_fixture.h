#ifndef SG_COMPOUND_HOOK_ORACLE_FIXTURE_H
#define SG_COMPOUND_HOOK_ORACLE_FIXTURE_H

#include "g_local.h"
#include "slipgate/sg_local.h"

typedef enum sg_compound_hook_oracle_sweep_e
{
	SG_HOOK_ORACLE_SWEEP_CLEAR = 0,
	SG_HOOK_ORACLE_SWEEP_PRECLEAR_CROSS,
	SG_HOOK_ORACLE_SWEEP_POSTCLEAR_RECROSS
} sg_compound_hook_oracle_sweep_t;

typedef struct sg_compound_hook_oracle_request_s
{
	qboolean expected_control;
	qboolean world_only;
	qboolean loader_replay;
	qboolean loader_transient;
	qboolean loader_malformed;
	qboolean loader_unowned;
	qboolean muzzle_blocked;
	qboolean shot_sky;
	qboolean shot_nonworld;
	qboolean bolt_trigger;
	qboolean suffix_hazard;
	qboolean suffix_fall;
	qboolean suffix_foreign_trigger;
	qboolean approach_foreign_trigger;
	qboolean suffix_foreign_solid;
	qboolean suffix_nonfinite;
	sg_compound_hook_oracle_sweep_t sweep;
	int top_drift_command;
	int identity_drift_command;
	float control_roll_delta;
	float old_frame_z;
} sg_compound_hook_oracle_request_t;

typedef struct sg_compound_hook_oracle_response_s
{
	rune_reject_reason_t reason;
	sg_compound_hook_proof_t proof;
	vec3_t control;
	vec3_t bite;
	qboolean member_restored;
	qboolean transient_restored;
	qboolean globals_restored;
	qboolean unowned_restored;
	int masked_shot_traces;
	int masked_contact_traces;
	int suffix_commands;
	int top_hold_commands;
	int top_zero_commands;
	int top_corrective_commands;
	qboolean suffix_captured_after_top_hold;
} sg_compound_hook_oracle_response_t;

void SG_CompoundHookOracleRunScenario(
	const sg_compound_hook_oracle_request_t *request,
	sg_compound_hook_oracle_response_t *response);
int SG_CompoundHookOracleFixtureRun(void);

#endif
