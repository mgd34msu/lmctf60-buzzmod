#include "sg_compound_publication_fixture.h"

fixture_t fixture;
static edict_t fixture_entities[4];
int failures;

void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

qboolean SG_HookControlDecode(const vec3_t origin, float viewheight, int hand,
	const vec3_t control, vec3_t view_angles, vec3_t muzzle, vec3_t bite)
{
	vec3_t forward, right;
	float pitch, yaw, distance;

	if (!origin || !control || !view_angles || !muzzle || !bite ||
	    !isfinite(control[PITCH]) || !isfinite(control[YAW]) ||
	    !isfinite(control[ROLL]) || control[PITCH] < -89.0f ||
	    control[PITCH] > 89.0f || control[YAW] < -180.0f ||
	    control[YAW] >= 180.0f ||
	    control[PITCH] != SHORT2ANGLE((short)ANGLE2SHORT(control[PITCH])) ||
	    control[YAW] != SHORT2ANGLE((short)ANGLE2SHORT(control[YAW])) ||
	    control[ROLL] < 1.0f || control[ROLL] > RUNE_HOOK_MAX_RAY)
		return false;
	pitch = control[PITCH] * (float)M_PI / 180.0f;
	yaw = control[YAW] * (float)M_PI / 180.0f;
	forward[0] = cosf(pitch) * cosf(yaw);
	forward[1] = cosf(pitch) * sinf(yaw);
	forward[2] = -sinf(pitch);
	right[0] = sinf(yaw);
	right[1] = -cosf(yaw);
	right[2] = 0.0f;
	view_angles[PITCH] = control[PITCH];
	view_angles[YAW] = control[YAW];
	view_angles[ROLL] = 0.0f;
	muzzle[0] = origin[0] + forward[0] * 8.0f + right[0] * 8.0f;
	muzzle[1] = origin[1] + forward[1] * 8.0f + right[1] * 8.0f;
	muzzle[2] = origin[2] + forward[2] * 8.0f + viewheight - 8.0f;
	distance = control[ROLL];
	bite[0] = muzzle[0] + distance * forward[0];
	bite[1] = muzzle[1] + distance * forward[1];
	bite[2] = muzzle[2] + distance * forward[2];
	(void)hand;
	return true;
}

rune_reject_reason_t SG_CompoundValidateLink(
	const rune_seed_t *seeds, int num_seeds, const rune_link_t *link)
{
	return seeds && num_seeds == 2 && link ? RLR_OK : RLR_BAD_CONTROL_POLICY;
}

static void *FixtureAllocate(int size)
{
	void *block;

	fixture.allocation_calls++;
	if (size <= 0 || fixture.allocation_calls == fixture.fail_allocation_call)
		return NULL;
	block = malloc((size_t)size);
	if (block)
		fixture.live_allocations++;
	return block;
}

static void FixtureFree(void *block)
{
	if (!block)
		return;
	fixture.free_calls++;
	fixture.live_allocations--;
	free(block);
}

static void FillResolved(sg_compound_world_preopen_t *resolved)
{
	memset(resolved, 0, sizeof(*resolved));
	resolved->trigger = &fixture_entities[1];
	resolved->member = &fixture_entities[2];
	Set3(resolved->bottom_origin, 0.0f, 0.0f, 0.0f);
	Set3(resolved->top_origin, 64.0f, 0.0f, 0.0f);
	Set3(resolved->member_mins, -16.0f, -16.0f, -24.0f);
	Set3(resolved->member_maxs, 16.0f, 16.0f, 32.0f);
	resolved->speed = 80.0f;
	resolved->wait = 3.0f;
	resolved->trigger_key = 1;
	resolved->mover_key = 2;
	resolved->axis = 0;
}

rune_reject_reason_t SG_CompoundWorldEnumeratePreopen(
	sg_compound_world_candidate_t *candidates, int capacity,
	int *count_out)
{
	fixture.enumerate_calls++;
	if (!count_out || fixture.fail_enumerate)
		return RLR_MECHANISM_UNRESOLVED;
	*count_out = fixture.inconsistent_second_mechanism ? 2 : 1;
	if (!candidates && capacity == 0)
		return RLR_OK;
	if (!candidates || capacity < *count_out)
		return RLR_BAD_CONTROL_POLICY;
	memset(candidates, 0, sizeof(*candidates) * (size_t)*count_out);
	FillResolved(&candidates[0].resolved);
	Set3(candidates[0].hints[0], 10.0f, 0.0f, 0.0f);
	Set3(candidates[0].hints[1], 11.0f, 0.0f, 0.0f);
	candidates[0].hint_count = 2;
	if (fixture.inconsistent_second_mechanism)
	{
		candidates[1] = candidates[0];
		candidates[1].resolved.wait = 4.0f;
	}
	return RLR_OK;
}

rune_reject_reason_t SG_CompoundWorldResolvePreopen(
	const float mechanism_anchor[3],
	sg_compound_world_preopen_t *resolved)
{
	fixture.resolve_calls++;
	if (fixture.fail_resolve || !mechanism_anchor || !resolved)
		return RLR_MECHANISM_UNRESOLVED;
	FillResolved(resolved);
	if (fixture.world_drift)
		resolved->speed = 81.0f;
	if (fixture.inconsistent_second_mechanism && fixture.resolve_calls == 2)
		resolved->wait = 4.0f;
	return RLR_OK;
}

int SG_CompoundWorldResolvedMember(
	const sg_compound_world_preopen_t *resolved, edict_t **member_out)
{
	sg_compound_world_preopen_t expected;

	fixture.resolved_member_calls++;
	if (member_out)
		*member_out = NULL;
	if (!resolved || !member_out || fixture.world_drift)
		return 0;
	FillResolved(&expected);
	if (resolved->trigger != expected.trigger ||
	    resolved->member != expected.member ||
	    resolved->trigger_key != expected.trigger_key ||
	    resolved->mover_key != expected.mover_key ||
	    resolved->speed != expected.speed || resolved->wait != expected.wait)
		return 0;
	*member_out = resolved->member;
	return 1;
}

int SG_CompoundWorldPreopenHintMatches(
	const sg_compound_world_preopen_t *resolved, const float hint[3])
{
	fixture.hint_match_calls++;
	return resolved && hint && !fixture.world_drift && !fixture.hint_drift &&
	       resolved->trigger == &fixture_entities[1] &&
	       resolved->member == &fixture_entities[2] && hint[0] == 11.0f &&
	       hint[1] == 0.0f && hint[2] == 0.0f;
}

static void FillPmove(pmove_state_t *pms, const float origin[3], int delta)
{
	int axis;

	memset(pms, 0, sizeof(*pms));
	pms->pm_type = PM_NORMAL;
	pms->pm_flags = PMF_TIME_WATERJUMP;
	pms->pm_time = 3;
	pms->gravity = 800;
	for (axis = 0; axis < 3; axis++)
	{
		pms->origin[axis] = (short)(origin[axis] * 8.0f);
		pms->velocity[axis] = (short)(8 + axis);
		pms->delta_angles[axis] = (short)(delta + axis * 100);
	}
}

rune_reject_reason_t SG_OracleCompoundSwimPrepareSource(
	const vec3_t source, const sg_compound_world_preopen_t *resolved,
	float old_frame_z, sg_compound_swim_source_t *prepared,
	edict_t *passent, qboolean world_only, qboolean loader_replay)
{
	int axis;

	(void)resolved; (void)passent;
	fixture.source_calls++;
	if (fixture.fail_source || !source || !prepared || old_frame_z != 0.0f ||
	    !world_only || !loader_replay)
		return RLR_APPROACH_REPLAY_FAILED;
	memset(prepared, 0, sizeof(*prepared));
	FillPmove(&prepared->phantom.pms, source, 100);
	FillPmove(&prepared->phantom.old_pms, source, 90);
	for (axis = 0; axis < 3; axis++)
	{
		prepared->phantom.origin[axis] = source[axis];
		prepared->phantom.velocity[axis] =
			prepared->phantom.pms.velocity[axis] * 0.125f;
	}
	prepared->phantom.watertype = CONTENTS_WATER;
	prepared->phantom.waterlevel = 3;
	prepared->old_frame_z = fixture.prepared_old_frame_z;
	if (fixture.proof_mutation == PROOF_BAD_SOURCE_CHECKPOINT)
		prepared->phantom.origin[0] += 0.125f;
	if (fixture.proof_mutation == PROOF_BAD_SOURCE_OLD_Z)
		prepared->old_frame_z = -0.0f;
	if (fixture.proof_mutation == PROOF_BAD_SOURCE_WATER)
		prepared->phantom.waterlevel = 1;
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundSwimDiscoverContact(
	const sg_compound_swim_source_t *prepared,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t canonical_hint, vec3_t mechanism_anchor,
	edict_t *passent, qboolean world_only, qboolean loader_replay)
{
	(void)prepared; (void)resolved; (void)passent;
	fixture.discover_calls++;
	if (fixture.fail_discover || !canonical_hint || !mechanism_anchor ||
	    !world_only || !loader_replay)
		return RLR_APPROACH_REPLAY_FAILED;
	if (canonical_hint[0] == 11.0f)
		Set3(mechanism_anchor, 20.0f, 0.0f, 0.0f);
	else
		Set3(mechanism_anchor, 19.0f, 0.0f, 0.0f);
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundSwimPreopen(sg_phantom_t *phantom,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t mechanism_anchor, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_proof_t *proof, sg_replay_reason_t *replay_reason,
	edict_t *passent,
	qboolean world_only, qboolean loader_replay)
{
	int axis;

	(void)resolved; (void)mechanism_anchor; (void)passent;
	if (replay_reason)
		*replay_reason = SG_REPLAY_REASON_NONE;
	fixture.replay_calls++;
	if (fixture.fail_replay || !phantom || !destination || !proof ||
	    !destination_water || !isfinite(old_frame_z) ||
	    !world_only || !loader_replay)
		return RLR_SUFFIX_REPLAY_FAILED;
	memset(proof, 0, sizeof(*proof));
	proof->touch_ms = 25;
	proof->touch_frame_end_ms = 100;
	proof->mover_top_ms = 500;
	proof->suffix_start_ms = 400;
	proof->arrival_ms = 300;
	proof->sweep_clear_ms = 200;
	proof->total_cost_ms = 800;
	proof->exit_speed = 12;
	FillPmove(&proof->suffix_pms, mechanism_anchor, 400);
	FillPmove(&proof->suffix_old_pms, mechanism_anchor, 390);
	for (axis = 0; axis < 3; axis++)
	{
		proof->suffix_origin[axis] =
			proof->suffix_pms.origin[axis] * 0.125f;
		proof->suffix_velocity[axis] =
			proof->suffix_pms.velocity[axis] * 0.125f;
	}
	proof->suffix_watertype = CONTENTS_WATER;
	proof->suffix_waterlevel = 3;
	proof->suffix_old_frame_z = 1.0f;
	switch (fixture.proof_mutation)
	{
	case PROOF_TOUCH_ZERO: proof->touch_ms = 0; break;
	case PROOF_TOUCH_UNALIGNED: proof->touch_ms = 26; break;
	case PROOF_FRAME_WRONG: proof->touch_frame_end_ms = 200; break;
	case PROOF_TOP_SHORT: proof->mover_top_ms = 100;
		proof->suffix_start_ms = 0; break;
	case PROOF_TOP_UNALIGNED: proof->mover_top_ms = 550;
		proof->suffix_start_ms = 450; break;
	case PROOF_SUFFIX_WRONG: proof->suffix_start_ms = 300; break;
	case PROOF_ARRIVAL_UNALIGNED: proof->arrival_ms = 325; break;
	case PROOF_CLEAR_ZERO: proof->sweep_clear_ms = 0; break;
	case PROOF_CLEAR_AFTER_ARRIVAL: proof->sweep_clear_ms = 400; break;
	case PROOF_TOTAL_WRONG: proof->total_cost_ms = 900; break;
	case PROOF_COST_MISMATCH: proof->total_cost_ms = 900;
		proof->arrival_ms = 400; break;
	case PROOF_EXIT_MISMATCH: proof->exit_speed = 13; break;
	case PROOF_CLEAR_MISMATCH: proof->sweep_clear_ms = 100; break;
	case PROOF_BAD_SUFFIX_CHECKPOINT: proof->suffix_velocity[1] += 0.125f;
		break;
	default: break;
	}
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundDropDiscoverContact(
	const vec3_t source, const sg_compound_world_preopen_t *resolved,
	const vec3_t canonical_hint, vec3_t mechanism_anchor,
	qboolean loader_replay)
{
	(void)source; (void)resolved;
	fixture.discover_calls++;
	if (fixture.fail_discover || !canonical_hint || !mechanism_anchor ||
	    !loader_replay)
		return RLR_APPROACH_REPLAY_FAILED;
	if (canonical_hint[0] == 11.0f)
		Set3(mechanism_anchor, 20.0f, 0.0f, 0.0f);
	else
		Set3(mechanism_anchor, 19.0f, 0.0f, 0.0f);
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundDropPreopen(
	const vec3_t source, const sg_compound_world_preopen_t *resolved,
	const vec3_t mechanism_anchor, const vec3_t destination,
	const vec3_t lip, byte heading, qboolean destination_water,
	sg_compound_drop_proof_t *proof, qboolean loader_replay)
{
	int axis;

	(void)resolved; (void)destination;
	fixture.replay_calls++;
	if (fixture.fail_replay || !source || !mechanism_anchor || !lip ||
	    !proof || destination_water || !loader_replay)
		return RLR_SUFFIX_REPLAY_FAILED;
	memset(proof, 0, sizeof(*proof));
	FillPmove(&proof->source_pms, source, 100);
	FillPmove(&proof->source_old_pms, source, 90);
	FillPmove(&proof->suffix_pms, mechanism_anchor, 400);
	FillPmove(&proof->suffix_old_pms, mechanism_anchor, 390);
	for (axis = 0; axis < 3; axis++)
	{
		proof->source_origin[axis] = proof->source_pms.origin[axis] * 0.125f;
		proof->source_velocity[axis] =
			proof->source_pms.velocity[axis] * 0.125f;
		proof->suffix_origin[axis] = proof->suffix_pms.origin[axis] * 0.125f;
		proof->suffix_velocity[axis] =
			proof->suffix_pms.velocity[axis] * 0.125f;
	}
	proof->source_groundentity = true;
	proof->suffix_groundentity = true;
	proof->suffix_old_frame_z = 1.0f;
	proof->touch_ms = 25;
	proof->touch_frame_end_ms = 100;
	proof->mover_top_ms = 500;
	proof->suffix_start_ms = 400;
	proof->arrival_ms = 300;
	proof->sweep_clear_ms = 200;
	proof->total_cost_ms = 800;
	proof->heading = heading;
	proof->exit_speed = 12;
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundHookPreopen(
	sg_phantom_t *phantom, const sg_compound_world_preopen_t *resolved,
	const vec3_t mechanism_anchor, const vec3_t destination,
	const vec3_t expected_control, float old_frame_z,
	sg_compound_hook_proof_t *proof, edict_t *passent,
	qboolean world_only, qboolean loader_replay)
{
	int axis;
	vec3_t decoded_view, muzzle;

	(void)resolved; (void)phantom; (void)passent;
	fixture.replay_calls++;
	if (fixture.fail_replay || !phantom || !mechanism_anchor || !destination ||
	    !expected_control || !proof || !isfinite(old_frame_z) ||
	    !world_only || !loader_replay)
		return RLR_SUFFIX_REPLAY_FAILED;
	memset(proof, 0, sizeof(*proof));
	proof->source_pms = phantom->pms;
	proof->source_old_pms = phantom->old_pms;
	proof->source_groundentity = phantom->groundentity;
	proof->source_watertype = phantom->watertype;
	proof->source_waterlevel = phantom->waterlevel;
	proof->source_old_frame_z = old_frame_z;
	FillPmove(&proof->suffix_pms, mechanism_anchor, 600);
	FillPmove(&proof->suffix_old_pms, mechanism_anchor, 590);
	for (axis = 0; axis < 3; axis++)
	{
		proof->source_origin[axis] = phantom->origin[axis];
		proof->source_velocity[axis] = phantom->velocity[axis];
		proof->suffix_origin[axis] =
			proof->suffix_pms.origin[axis] * 0.125f;
		proof->suffix_velocity[axis] =
			proof->suffix_pms.velocity[axis] * 0.125f;
	}
	switch (fixture.hook_source_drift)
	{
	case HOOK_SOURCE_PMS:
		proof->source_pms.origin[0]++;
		break;
	case HOOK_SOURCE_OLD_PMS:
		proof->source_old_pms.origin[0]++;
		break;
	case HOOK_SOURCE_ORIGIN:
		proof->source_origin[0] += 0.125f;
		break;
	case HOOK_SOURCE_VELOCITY:
		proof->source_velocity[0] += 0.125f;
		break;
	case HOOK_SOURCE_GROUNDED:
		proof->source_groundentity = !proof->source_groundentity;
		break;
	case HOOK_SOURCE_WATERTYPE:
		proof->source_watertype ^= CONTENTS_WATER;
		break;
	case HOOK_SOURCE_WATERLEVEL:
		proof->source_waterlevel--;
		break;
	case HOOK_SOURCE_OLD_FRAME_Z:
	{
		uint32_t bits;

		memcpy(&bits, &proof->source_old_frame_z, sizeof(bits));
		bits ^= 1u;
		memcpy(&proof->source_old_frame_z, &bits, sizeof(bits));
		break;
	}
	case HOOK_SOURCE_STABLE:
	case HOOK_SOURCE_DRIFT_COUNT:
		break;
	}
	proof->suffix_watertype = CONTENTS_WATER;
	proof->suffix_waterlevel = 3;
	proof->suffix_old_frame_z = -37.0f;
	proof->touch_ms = 25;
	proof->touch_frame_end_ms = 100;
	proof->mover_top_ms = 500;
	proof->suffix_start_ms = 400;
	proof->arrival_ms = 600;
	proof->sweep_clear_ms = 200;
	proof->total_cost_ms = 1100;
	proof->exit_speed = 12;
	memcpy(proof->control, expected_control, sizeof(proof->control));
	if (!SG_HookControlDecode(proof->suffix_origin, 22.0f, RIGHT_HANDED,
	        expected_control, decoded_view, muzzle, proof->hook_spec.bite))
		return RLR_SUFFIX_REPLAY_FAILED;
	if (fixture.hook_bite_drift)
		proof->hook_spec.bite[0] += 1.0f;
	memcpy(proof->hook_spec.destination, destination,
	       sizeof(proof->hook_spec.destination));
	memcpy(proof->hook_spec.view_angles, decoded_view,
	       sizeof(proof->hook_spec.view_angles));
	proof->hook_spec.flight_ms = 300;
	proof->hook_spec.settle_limit_ms = RUNE_HOOK_WATER_SETTLE_MS;
	proof->hook_spec.expected_release_ms = 200;
	proof->hook_spec.expected_pull_ms = 200;
	proof->hook_spec.expected_settle_arrival_ms = 0;
	proof->hook_spec.expected_settle_ms = 100;
	return RLR_OK;
}

static rune_link_t CompoundLink(int from, int to)
{
	rune_link_t link;

	memset(&link, 0, sizeof(link));
	link.from = from;
	link.to = to;
	link.action = RL_DOOR_SWIM;
	link.provenance = RL_CONTRACTED;
	link.exit_speed = 12;
	link.cost_ms = 800;
	Set3(link.mechanism_anchor, 20.0f, 0.0f, 0.0f);
	link.sweep_clear_ms = 200;
	link.mode = RLCM_PREOPEN;
	link.mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	return link;
}

rune_link_t CompoundDropLink(int from, int to)
{
	rune_link_t link = CompoundLink(from, to);

	link.action = RL_DOOR_DROP;
	link.heading = 32;
	link.heading_slack = SG_RUNE_PROOF_DROP_CONTROL_MARKER;
	Set3(link.anchor, 24.0f, 0.0f, 0.0f);
	return link;
}

rune_link_t CompoundHookLink(int from, int to)
{
	rune_link_t link = CompoundLink(from, to);

	link.action = RL_DOOR_HOOK;
	link.cost_ms = 1100;
	link.heading_slack = SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER;
	link.anchor[PITCH] = SHORT2ANGLE((short)ANGLE2SHORT(-15.0f));
	link.anchor[YAW] = SHORT2ANGLE(ANGLE2SHORT(90.0f));
	link.anchor[ROLL] = 200.0f;
	return link;
}

rune_t RuneFixture(rune_seed_t seeds[3], rune_link_t links[3])
{
	rune_t rune;

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(rune_seed_t) * 3U);
	memset(links, 0, sizeof(rune_link_t) * 3U);
	Set3(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	Set3(seeds[1].origin, 40.0f, 0.0f, 0.0f);
	Set3(seeds[2].origin, 80.0f, 0.0f, 0.0f);
	seeds[0].flags = seeds[1].flags = seeds[2].flags = RSF_WATER;
	links[0] = CompoundLink(0, 1);
	links[1].from = 1;
	links[1].to = 2;
	links[1].action = RL_SWIM;
	links[1].provenance = RL_PROVEN;
	links[1].cost_ms = 100;
	links[2] = CompoundLink(2, 1);
	rune.hdr.num_seeds = 3;
	rune.hdr.num_links = 3;
	rune.seeds = seeds;
	rune.links = links;
	return rune;
}

void ResetFixture(void)
{
	memset(&fixture, 0, sizeof(fixture));
	memset(fixture_entities, 0, sizeof(fixture_entities));
}

sg_compound_publication_result_t Build(rune_t *rune)
{
	return SG_CompoundPublicationBuild(rune, FixtureAllocate, FixtureFree,
	                                  &rune->compound_publication);
}

void Destroy(rune_t *rune)
{
	SG_CompoundPublicationDestroy(rune->compound_publication);
	rune->compound_publication = NULL;
}
