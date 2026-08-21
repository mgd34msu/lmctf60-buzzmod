#include "sg_compound_oracle_fixture.h"
#include "sg_compound_hook_oracle_fixture.h"

void SG_CompoundHookOracleRunScenario(
	const sg_compound_hook_oracle_request_t *request,
	sg_compound_hook_oracle_response_t *response)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { 280.0f, 0.0f, 0.0f };
	fixture_config_t scenario =
		DefaultConfig(2, FIXTURE_SUFFIX_ALWAYS_OUTSIDE);
	sg_compound_world_preopen_t resolved;
	sg_phantom_t phantom;
	edict_t member_before, client_before, projectile_before;
	game_export_t globals_before;
	game_locals_t game_before;
	edict_t *base_before;
	vec3_t view, muzzle;
	const float *expected;

	if (!response)
		return;
	memset(response, 0, sizeof(*response));
	if (!request)
	{
		response->reason = RLR_BAD_CONTROL_POLICY;
		return;
	}
	scenario.hook_suffix = true;
	scenario.hook_discover_control = !request->expected_control;
	scenario.hook_muzzle_blocked = request->muzzle_blocked;
	scenario.hook_shot_sky = request->shot_sky;
	scenario.hook_shot_nonworld = request->shot_nonworld;
	scenario.hook_bolt_trigger = request->bolt_trigger;
	scenario.suffix_hazard = request->suffix_hazard;
	scenario.suffix_fall = request->suffix_fall;
	scenario.suffix_foreign_trigger = request->suffix_foreign_trigger;
	scenario.contaminate_trigger = request->approach_foreign_trigger;
	scenario.suffix_foreign_solid = request->suffix_foreign_solid;
	scenario.suffix_nonfinite = request->suffix_nonfinite;
	scenario.hook_sweep_mode = request->sweep;
	scenario.loader_transient = request->loader_transient;
	scenario.loader_malformed = request->loader_malformed;
	scenario.loader_unowned = request->loader_unowned;
	scenario.top_drift_at_command = request->top_drift_command;
	scenario.identity_drift_at_command = request->identity_drift_command;
	ResetFixture(&scenario);
	if (Resolve(&resolved) != RLR_OK)
	{
		response->reason = RLR_MECHANISM_UNRESOLVED;
		return;
	}
	Set3(response->control, 0.0f, 0.0f,
	     160.0f + request->control_roll_delta);
	if (!SG_HookControlDecode(mechanism, 22.0f, RIGHT_HANDED,
	        response->control, view, muzzle, response->bite))
	{
		response->reason = RLR_BAD_CONTROL_POLICY;
		return;
	}
	VectorCopy(response->bite, fixture_config.hook_bite);
	member_before = fixture_edicts[1];
	client_before = fixture_edicts[5];
	projectile_before = fixture_edicts[6];
	globals_before = globals;
	game_before = game;
	base_before = g_edicts;
	InitPhantom(&phantom, false);
	expected = request->expected_control ? response->control : NULL;
	response->reason = SG_OracleCompoundHookPreopen(
		&phantom, &resolved, mechanism, destination,
		expected, request->old_frame_z,
		&response->proof, NULL, request->world_only,
		request->loader_replay);
	response->masked_shot_traces =
		fixture_observation.transient_masked_shot_traces;
	response->masked_contact_traces =
		fixture_observation.transient_masked_contact_traces;
	response->suffix_commands = fixture_observation.suffix_commands;
	response->member_restored =
		MemberRestored(&fixture_edicts[1], &member_before);
	response->transient_restored =
		memcmp(&fixture_edicts[5], &client_before,
		       sizeof(client_before)) == 0 &&
		memcmp(&fixture_edicts[6], &projectile_before,
		       sizeof(projectile_before)) == 0;
	response->globals_restored =
		memcmp(&globals, &globals_before, sizeof(globals)) == 0 &&
		memcmp(&game, &game_before, sizeof(game)) == 0 &&
		g_edicts == base_before;
	response->unowned_restored =
		!request->loader_unowned || fixture_edicts[7].solid == SOLID_BBOX;
}
