#include <float.h>
#include <stdio.h>
#include <string.h>

#include "sg_compound_hook_oracle_fixture.h"
#include "slipgate/sg_util.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_compound_hook_oracle_request_t LoaderRequest(void)
{
	sg_compound_hook_oracle_request_t request;

	memset(&request, 0, sizeof(request));
	request.expected_control = true;
	request.world_only = true;
	request.loader_replay = true;
	return request;
}

static void TestExpectedControlAndStablePopulation(void)
{
	sg_compound_hook_oracle_request_t request = LoaderRequest();
	sg_compound_hook_oracle_response_t baseline, transient;

	SG_CompoundHookOracleRunScenario(&request, &baseline);
	CHECK(baseline.reason == RLR_OK);
	CHECK(memcmp(baseline.proof.control, baseline.control,
	             sizeof(baseline.control)) == 0);
	CHECK(memcmp(baseline.proof.hook_spec.bite, baseline.bite,
	             sizeof(baseline.bite)) == 0);
	CHECK(baseline.proof.hook_spec.flight_ms == 200);
	CHECK(baseline.proof.touch_ms == 50);
	CHECK(baseline.proof.touch_frame_end_ms == 100);
	CHECK(baseline.proof.mover_top_ms == 500);
	CHECK(baseline.proof.suffix_start_ms == 400);
	CHECK(baseline.proof.sweep_clear_ms == 100);
	CHECK(baseline.proof.hook_spec.expected_pull_ms == 100);
	CHECK(baseline.proof.hook_spec.expected_release_ms == 25);
	CHECK(baseline.proof.hook_spec.expected_settle_arrival_ms == 0);
	CHECK(baseline.proof.hook_spec.expected_settle_ms == 100);
	CHECK(baseline.proof.arrival_ms == 500);
	CHECK(baseline.proof.exit_speed == 2);
	CHECK(baseline.proof.suffix_old_frame_z == -50.75f);
	CHECK(baseline.proof.suffix_pms.origin[0] == 1280);
	CHECK(baseline.proof.suffix_pms.origin[1] == 176);
	CHECK(baseline.proof.suffix_pms.origin[2] == 0);
	CHECK(baseline.proof.suffix_pms.velocity[0] == 0);
	CHECK(baseline.proof.suffix_pms.velocity[1] == 64);
	CHECK(baseline.proof.suffix_pms.velocity[2] == -296);
	CHECK(baseline.proof.suffix_watertype == CONTENTS_WATER);
	CHECK(baseline.proof.total_cost_ms ==
	      baseline.proof.touch_frame_end_ms +
	      baseline.proof.suffix_start_ms + baseline.proof.arrival_ms);
	CHECK(baseline.proof.total_cost_ms == 1000);
	CHECK(baseline.member_restored && baseline.globals_restored);
	CHECK(baseline.opening_commands == 22);
	CHECK(baseline.opening_zero_commands == 22);
	CHECK(baseline.opening_corrective_commands == 0);
	CHECK(baseline.suffix_captured_after_opening);
	CHECK(baseline.suffix_commands == 16);

	request.loader_transient = true;
	SG_CompoundHookOracleRunScenario(&request, &transient);
	CHECK(transient.reason == RLR_OK);
	CHECK(memcmp(&transient.proof, &baseline.proof,
	             sizeof(baseline.proof)) == 0);
	CHECK(transient.masked_shot_traces >= 2);
	CHECK(transient.masked_contact_traces >= 1);
	CHECK(transient.member_restored);
	CHECK(transient.transient_restored);
	CHECK(transient.globals_restored);
}

static void TestExpectedControlFailures(void)
{
	sg_compound_hook_oracle_request_t request = LoaderRequest();
	sg_compound_hook_oracle_response_t response;
	vec3_t bite, control, muzzle, origin, view;

	memset(origin, 0, sizeof(origin));
	memset(control, 0, sizeof(control));
	control[YAW] = FLT_MAX;
	control[ROLL] = 1.0f;
	CHECK(!SG_HookControlDecode(origin, 22.0f, RIGHT_HANDED, control,
	                            view, muzzle, bite));

	request.expected_control = false;
	SG_CompoundHookOracleRunScenario(&request, &response);
	CHECK(response.reason == RLR_BAD_CONTROL_POLICY);
	CHECK(response.proof.touch_ms == 0);
	CHECK(response.proof.hook_spec.flight_ms == 0);

	request.expected_control = true;
	request.control_roll_delta = 8.0f;
	SG_CompoundHookOracleRunScenario(&request, &response);
	CHECK(response.reason != RLR_OK);
	CHECK(response.proof.touch_ms == 0);
	CHECK(response.proof.hook_spec.flight_ms == 0);

	request.control_roll_delta = 0.0f;
	request.loader_malformed = true;
	SG_CompoundHookOracleRunScenario(&request, &response);
	CHECK(response.reason != RLR_OK);
	CHECK(response.proof.touch_ms == 0);
	CHECK(response.transient_restored);

	request.loader_malformed = false;
	request.loader_unowned = true;
	SG_CompoundHookOracleRunScenario(&request, &response);
	CHECK(response.reason != RLR_OK);
	CHECK(response.proof.touch_ms == 0);
	CHECK(response.unowned_restored);
}

typedef enum hook_failure_case_e
{
	HOOK_FAIL_MUZZLE = 0,
	HOOK_FAIL_SKY,
	HOOK_FAIL_NONWORLD,
	HOOK_FAIL_BOLT_TRIGGER,
	HOOK_FAIL_HARMFUL_LIQUID,
	HOOK_FAIL_DAMAGING_FALL,
	HOOK_FAIL_FOREIGN_TRIGGER,
	HOOK_FAIL_FOREIGN_SOLID,
	HOOK_FAIL_NONFINITE,
	HOOK_FAIL_TOP_DRIFT,
	HOOK_FAIL_IDENTITY_DRIFT,
	HOOK_FAIL_POSTCLEAR_RECROSS,
	HOOK_FAIL_COUNT
} hook_failure_case_t;

static void ConfigureFailure(sg_compound_hook_oracle_request_t *request,
	hook_failure_case_t failure)
{
	switch (failure)
	{
	case HOOK_FAIL_MUZZLE: request->muzzle_blocked = true; break;
	case HOOK_FAIL_SKY: request->shot_sky = true; break;
	case HOOK_FAIL_NONWORLD: request->shot_nonworld = true; break;
	case HOOK_FAIL_BOLT_TRIGGER: request->bolt_trigger = true; break;
	case HOOK_FAIL_HARMFUL_LIQUID: request->suffix_hazard = true; break;
	case HOOK_FAIL_DAMAGING_FALL:
		request->suffix_fall = true;
		request->old_frame_z = -1000.0f;
		break;
	case HOOK_FAIL_FOREIGN_TRIGGER:
		request->suffix_foreign_trigger = true;
		break;
	case HOOK_FAIL_FOREIGN_SOLID:
		request->suffix_foreign_solid = true;
		break;
	case HOOK_FAIL_NONFINITE: request->suffix_nonfinite = true; break;
	case HOOK_FAIL_TOP_DRIFT: request->top_drift_command = 2; break;
	case HOOK_FAIL_IDENTITY_DRIFT:
		request->identity_drift_command = 2;
		break;
	case HOOK_FAIL_POSTCLEAR_RECROSS:
		request->sweep = SG_HOOK_ORACLE_SWEEP_POSTCLEAR_RECROSS;
		break;
	case HOOK_FAIL_COUNT: break;
	}
}

static void TestContinuousContraries(void)
{
	hook_failure_case_t failure;
	sg_compound_hook_proof_t zero;

	memset(&zero, 0, sizeof(zero));
	for (failure = HOOK_FAIL_MUZZLE; failure < HOOK_FAIL_COUNT; failure++)
	{
		sg_compound_hook_oracle_request_t request = LoaderRequest();
		sg_compound_hook_oracle_response_t response;

		ConfigureFailure(&request, failure);
		SG_CompoundHookOracleRunScenario(&request, &response);
		CHECK(response.reason != RLR_OK);
		CHECK(memcmp(&response.proof, &zero, sizeof(zero)) == 0);
		CHECK(response.member_restored);
		CHECK(response.globals_restored);
		if (failure == HOOK_FAIL_DAMAGING_FALL)
			CHECK(response.suffix_commands == 16);
	}
}

static void TestDiscoveredGenerationControl(void)
{
	sg_compound_hook_oracle_request_t request = LoaderRequest();
	sg_compound_hook_oracle_response_t response;

	request.expected_control = false;
	request.loader_replay = false;
	SG_CompoundHookOracleRunScenario(&request, &response);
	CHECK(response.reason == RLR_OK);
	CHECK(response.proof.control[ROLL] > 0.0f);
	CHECK(response.proof.touch_ms == 50);
	CHECK(response.proof.total_cost_ms == 1000);
	CHECK(response.member_restored && response.globals_restored);
}

static void TestSweepResetAndWorldOnlyPropagation(void)
{
	sg_compound_hook_oracle_request_t request = LoaderRequest();
	sg_compound_hook_oracle_response_t response;

	request.sweep = SG_HOOK_ORACLE_SWEEP_PRECLEAR_CROSS;
	request.old_frame_z = -37.0f;
	SG_CompoundHookOracleRunScenario(&request, &response);
	CHECK(response.reason == RLR_OK);
	CHECK(response.proof.sweep_clear_ms == 200);
	CHECK(response.proof.source_old_frame_z == -37.0f);
	CHECK(response.member_restored && response.globals_restored);

	request = LoaderRequest();
	request.world_only = false;
	SG_CompoundHookOracleRunScenario(&request, &response);
	CHECK(response.reason == RLR_OK);
	CHECK(response.proof.touch_ms == 50);
	CHECK(response.member_restored && response.globals_restored);

	request.approach_foreign_trigger = true;
	SG_CompoundHookOracleRunScenario(&request, &response);
	CHECK(response.reason == RLR_OK);
	CHECK(response.proof.arrival_ms == 500);
	CHECK(response.member_restored && response.globals_restored);

	request = LoaderRequest();
	request.approach_foreign_trigger = true;
	SG_CompoundHookOracleRunScenario(&request, &response);
	CHECK(response.reason == RLR_APPROACH_REPLAY_FAILED);
	CHECK(response.proof.touch_ms == 0);
	CHECK(response.member_restored && response.globals_restored);
}

int SG_CompoundHookOracleFixtureRun(void)
{
	TestExpectedControlAndStablePopulation();
	TestExpectedControlFailures();
	TestContinuousContraries();
	TestDiscoveredGenerationControl();
	TestSweepResetAndWorldOnlyPropagation();
	return failures;
}
