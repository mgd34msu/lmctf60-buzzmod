/* Loader-replayed suffix plans for compound door links. */
#include "sg_compound_action_publication.h"

#include <math.h>
#include <string.h>

#include "sg_compound.h"

static qboolean ActionPublicationFinite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
	       isfinite(value[2]);
}

static qboolean ActionPublicationTimingValid(
	const sg_compound_publication_binding_t *binding)
{
	long long touch_frame_end;
	long long total;

	if (!binding || binding->touch_ms <= 0 ||
	    binding->touch_ms > RUNE_MAX_COST_MS ||
	    binding->touch_ms % SG_RUNE_PROOF_PMOVE_SUBSTEP_MS != 0 ||
	    binding->touch_frame_end_ms <= 0 ||
	    binding->touch_frame_end_ms > RUNE_MAX_COST_MS ||
	    binding->touch_frame_end_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    binding->mover_top_ms < 2 * SG_RUNE_PROOF_SERVER_FRAME_MS ||
	    binding->mover_top_ms > RUNE_MAX_COST_MS ||
	    binding->mover_top_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    binding->suffix_start_ms < SG_RUNE_PROOF_SERVER_FRAME_MS ||
	    binding->suffix_start_ms > RUNE_MAX_COST_MS ||
	    binding->suffix_start_ms !=
	        binding->mover_top_ms - SG_RUNE_PROOF_SERVER_FRAME_MS ||
	    binding->arrival_ms <= 0 ||
	    binding->arrival_ms > RUNE_MAX_COST_MS ||
	    binding->arrival_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    binding->sweep_clear_ms <= 0 ||
	    binding->sweep_clear_ms > binding->arrival_ms ||
	    binding->sweep_clear_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0)
		return false;
	touch_frame_end =
		((long long)binding->touch_ms +
		 SG_RUNE_PROOF_SERVER_FRAME_MS - 1LL) /
		SG_RUNE_PROOF_SERVER_FRAME_MS * SG_RUNE_PROOF_SERVER_FRAME_MS;
	total = (long long)binding->touch_frame_end_ms +
	        binding->suffix_start_ms + binding->arrival_ms;
	return touch_frame_end == binding->touch_frame_end_ms &&
	       total == binding->total_cost_ms &&
	       binding->total_cost_ms == binding->link.cost_ms &&
	       binding->sweep_clear_ms == binding->link.sweep_clear_ms;
}

static qboolean ActionPublicationLinkValid(
	const sg_compound_publication_binding_t *binding, int expected_action)
{
	rune_seed_t seeds[2];
	rune_link_t link;

	if (!binding || binding->link.action != expected_action ||
	    binding->link.mechanism_plan != RUNE_NO_MECHANISM_PLAN ||
	    !ActionPublicationFinite3(binding->link.anchor) ||
	    !ActionPublicationFinite3(binding->link.mechanism_anchor) ||
	    !ActionPublicationFinite3(binding->source_seed.origin) ||
	    !ActionPublicationFinite3(binding->destination_seed.origin) ||
	    !ActionPublicationTimingValid(binding))
		return false;
	memset(seeds, 0, sizeof(seeds));
	seeds[0] = binding->source_seed;
	seeds[1] = binding->destination_seed;
	link = binding->link;
	link.from = 0;
	link.to = 1;
	return SG_CompoundValidateLink(seeds, 2, &link) == RLR_OK;
}

static qboolean ActionPublicationHookSpecValid(
	const sg_hook_replay_spec_t *spec)
{
	int max_settle;

	if (!spec || !ActionPublicationFinite3(spec->bite) ||
	    !ActionPublicationFinite3(spec->destination) ||
	    !ActionPublicationFinite3(spec->view_angles) ||
	    spec->view_angles[PITCH] < -89.0f ||
	    spec->view_angles[PITCH] > 89.0f ||
	    spec->view_angles[ROLL] != 0.0f ||
	    SHORT2ANGLE((short)ANGLE2SHORT(spec->view_angles[PITCH])) !=
	        spec->view_angles[PITCH] ||
	    SHORT2ANGLE((short)ANGLE2SHORT(spec->view_angles[YAW])) !=
	        spec->view_angles[YAW] ||
	    spec->flight_ms < SG_REPLAY_FRAME_MS ||
	    spec->flight_ms > SG_REPLAY_HOOK_FLIGHT_MAX_MS ||
	    spec->flight_ms % SG_REPLAY_FRAME_MS != 0 ||
	    spec->settle_limit_ms < SG_RUNE_PROOF_HOOK_DRY_SETTLE_MS ||
	    spec->settle_limit_ms > SG_RUNE_PROOF_HOOK_WATER_SETTLE_MS ||
	    spec->expected_release_ms <= 0 ||
	    spec->expected_release_ms > SG_REPLAY_HOOK_PULL_LIMIT_MS ||
	    spec->expected_release_ms % SG_REPLAY_STEP_MS != 0 ||
	    spec->expected_pull_ms <= 0 ||
	    spec->expected_pull_ms > SG_REPLAY_HOOK_PULL_LIMIT_MS ||
	    spec->expected_pull_ms % SG_REPLAY_FRAME_MS != 0 ||
	    spec->expected_release_ms > spec->expected_pull_ms ||
	    spec->expected_settle_arrival_ms <= 0 ||
	    spec->expected_settle_arrival_ms % SG_REPLAY_STEP_MS != 0 ||
	    spec->expected_settle_ms <= 0 ||
	    spec->expected_settle_ms % SG_REPLAY_FRAME_MS != 0 ||
	    spec->expected_settle_arrival_ms > spec->expected_settle_ms)
		return false;
	max_settle = ((spec->settle_limit_ms + SG_REPLAY_FRAME_MS - 1) /
	              SG_REPLAY_FRAME_MS) * SG_REPLAY_FRAME_MS;
	return spec->expected_settle_arrival_ms <= max_settle &&
	       spec->expected_settle_ms <= max_settle;
}

qboolean SG_CompoundDropPublicationPlan(
	const sg_compound_publication_binding_t *binding,
	sg_drop_replay_spec_t *spec_out)
{
	sg_drop_replay_spec_t candidate;

	if (spec_out)
		memset(spec_out, 0, sizeof(*spec_out));
	if (!spec_out ||
	    !ActionPublicationLinkValid(binding, RL_DOOR_DROP) ||
	    binding->arrival_ms >= SG_REPLAY_DROP_TOTAL_MS)
		return false;
	memset(&candidate, 0, sizeof(candidate));
	memcpy(candidate.destination, binding->destination_seed.origin,
	       sizeof(candidate.destination));
	memcpy(candidate.lip, binding->link.anchor, sizeof(candidate.lip));
	candidate.heading = binding->link.heading;
	candidate.destination_water =
		(binding->destination_seed.flags & RSF_WATER) != 0;
	candidate.expected_arrival_ms = binding->arrival_ms;
	*spec_out = candidate;
	return true;
}

qboolean SG_CompoundHookPublicationPlan(
	const sg_compound_publication_binding_t *binding,
	const sg_compound_hook_publication_proof_t *proof,
	sg_hook_replay_spec_t *spec_out)
{
	long long suffix_ms;
	int flight_ms;

	if (spec_out)
		memset(spec_out, 0, sizeof(*spec_out));
	if (!spec_out || !binding || !proof ||
	    !ActionPublicationLinkValid(binding, RL_DOOR_HOOK))
		return false;
	flight_ms = (int)ceilf(binding->link.anchor[ROLL] /
	                         SG_RUNE_PROOF_HOOK_FRAME_DISTANCE) *
	            SG_REPLAY_FRAME_MS;
	suffix_ms = (long long)proof->spec.flight_ms +
	            proof->spec.expected_pull_ms +
	            proof->spec.expected_settle_ms;
	if (!ActionPublicationHookSpecValid(&proof->spec) ||
	    proof->spec.flight_ms != flight_ms ||
	    suffix_ms != binding->arrival_ms ||
	    memcmp(proof->spec.destination, binding->destination_seed.origin,
	           sizeof(proof->spec.destination)) != 0 ||
	    proof->spec.view_angles[PITCH] != binding->link.anchor[PITCH] ||
	    proof->spec.view_angles[YAW] != binding->link.anchor[YAW])
		return false;
	*spec_out = proof->spec;
	return true;
}
