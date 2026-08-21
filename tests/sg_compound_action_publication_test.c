/* Publication-to-suffix plans for compound door links. */
#include <math.h>
#include <float.h>
#include <stdio.h>
#include <string.h>

#include "../q_shared.h"
#include "../slipgate/sg_compound_action_publication.h"

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static void ReplayInputs(sg_replay_pose_t *pose,
	sg_replay_observation_t *observation)
{
	memset(pose, 0, sizeof(*pose));
	memset(observation, 0, sizeof(*observation));
	pose->grounded = true;
}

static void CommonBinding(sg_compound_publication_binding_t *binding,
	int action, int mode)
{
	memset(binding, 0, sizeof(*binding));
	binding->link_index = 7;
	binding->link.from = 0;
	binding->link.to = 1;
	binding->link.action = action;
	binding->link.provenance = RL_CONTRACTED;
	binding->link.cost_ms = 900;
	binding->link.mechanism_anchor[0] = 8.0f;
	binding->link.mechanism_anchor[1] = 16.0f;
	binding->link.mechanism_anchor[2] = 24.0f;
	binding->link.sweep_clear_ms = 200;
	binding->link.mode = mode;
	binding->link.mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	binding->source_seed.origin[0] = 1.0f;
	binding->destination_seed.origin[0] = 96.0f;
	binding->touch_ms = 25;
	binding->touch_frame_end_ms = 100;
	binding->mover_top_ms = 500;
	binding->suffix_start_ms = 400;
	binding->arrival_ms = 400;
	binding->sweep_clear_ms = 200;
	binding->total_cost_ms = 900;
}

static void TestDropPlan(void)
{
	sg_compound_publication_binding_t binding;
	sg_drop_replay_spec_t spec;
	sg_drop_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;

	CommonBinding(&binding, RL_DOOR_DROP, RLCM_RIDE);
	binding.link.anchor[0] = 64.0f;
	binding.link.anchor[1] = 32.0f;
	binding.link.anchor[2] = 16.0f;
	binding.link.heading = 64;
	binding.link.heading_slack = SG_RUNE_PROOF_DROP_CONTROL_MARKER;
	memset(&spec, 0xa5, sizeof(spec));
	CHECK(SG_CompoundDropPublicationPlan(&binding, &spec));
	CHECK(spec.destination[0] == 96.0f);
	CHECK(spec.lip[0] == 64.0f && spec.lip[1] == 32.0f &&
	      spec.lip[2] == 16.0f);
	CHECK(spec.heading == 64);
	CHECK(spec.destination_water == false);
	CHECK(spec.expected_arrival_ms == 400);
	ReplayInputs(&pose, &observation);
	CHECK(SG_DropReplayBegin(&state, &spec, &pose, &observation, 0.0f) ==
	      SG_REPLAY_RUNNING);

	binding.arrival_ms = 0;
	memset(&spec, 0x5a, sizeof(spec));
	CHECK(!SG_CompoundDropPublicationPlan(&binding, &spec));
	CHECK(spec.expected_arrival_ms == 0);

	binding.arrival_ms = SG_REPLAY_DROP_TOTAL_MS;
	binding.total_cost_ms = binding.touch_frame_end_ms +
	                        binding.suffix_start_ms + binding.arrival_ms;
	binding.link.cost_ms = (short)binding.total_cost_ms;
	CHECK(!SG_CompoundDropPublicationPlan(&binding, &spec));
}

static void HookProof(sg_compound_hook_publication_proof_t *proof)
{
	memset(proof, 0, sizeof(*proof));
	proof->spec.bite[0] = 160.0f;
	proof->spec.bite[2] = 64.0f;
	proof->spec.destination[0] = 96.0f;
	proof->spec.view_angles[PITCH] =
		SHORT2ANGLE((short)ANGLE2SHORT(-15.0f));
	proof->spec.view_angles[YAW] = SHORT2ANGLE(ANGLE2SHORT(90.0f));
	proof->spec.flight_ms = 300;
	proof->spec.settle_limit_ms = RUNE_HOOK_WATER_SETTLE_MS;
	proof->spec.expected_release_ms = 200;
	proof->spec.expected_pull_ms = 200;
	proof->spec.expected_settle_arrival_ms = 0;
	proof->spec.expected_settle_ms = 100;
}

static void TestHookPlan(void)
{
	sg_compound_publication_binding_t binding;
	sg_hook_replay_spec_t spec;
	sg_hook_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;

	CommonBinding(&binding, RL_DOOR_HOOK, RLCM_PREOPEN);
	binding.arrival_ms = 600;
	binding.total_cost_ms = binding.touch_frame_end_ms +
	                        binding.suffix_start_ms + binding.arrival_ms;
	binding.link.cost_ms = (short)binding.total_cost_ms;
	binding.source_seed.flags = RSF_WATER;
	binding.link.anchor[PITCH] =
		SHORT2ANGLE((short)ANGLE2SHORT(-15.0f));
	binding.link.anchor[YAW] = SHORT2ANGLE(ANGLE2SHORT(90.0f));
	binding.link.anchor[ROLL] = 200.0f;
	binding.link.heading_slack = SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER;
	HookProof(&binding.hook_proof);
	memset(&spec, 0xa5, sizeof(spec));
	CHECK(SG_CompoundHookPublicationPlan(&binding, &spec));
	CHECK(memcmp(&spec, &binding.hook_proof.spec, sizeof(spec)) == 0);
	ReplayInputs(&pose, &observation);
	pose.grounded = false;
	pose.watertype = CONTENTS_WATER;
	pose.waterlevel = 2;
	CHECK(SG_HookReplayBegin(&state, &spec, &pose, &observation, 0.0f) ==
	      SG_REPLAY_RUNNING);
	binding.hook_proof.spec.expected_settle_arrival_ms = -1;
	CHECK(!SG_CompoundHookPublicationPlan(&binding, &spec));
	binding.hook_proof.spec.expected_settle_arrival_ms = 0;
	binding.hook_proof.spec.view_angles[YAW] = FLT_MAX;
	CHECK(!SG_CompoundHookPublicationPlan(&binding, &spec));
	binding.hook_proof.spec.view_angles[YAW] =
		SHORT2ANGLE(ANGLE2SHORT(90.0f));
	binding.link.anchor[YAW] = FLT_MAX;
	CHECK(!SG_CompoundHookPublicationPlan(&binding, &spec));
	binding.link.anchor[YAW] = SHORT2ANGLE(ANGLE2SHORT(90.0f));
	binding.link.anchor[PITCH] = FLT_MAX;
	CHECK(!SG_CompoundHookPublicationPlan(&binding, &spec));
	binding.link.anchor[PITCH] =
		SHORT2ANGLE((short)ANGLE2SHORT(-15.0f));

	binding.arrival_ms = 500;
	binding.total_cost_ms = binding.touch_frame_end_ms +
	                        binding.suffix_start_ms + binding.arrival_ms;
	binding.link.cost_ms = (short)binding.total_cost_ms;
	CHECK(!SG_CompoundHookPublicationPlan(&binding, &spec));
	binding.arrival_ms = 600;
	binding.total_cost_ms = binding.touch_frame_end_ms +
	                        binding.suffix_start_ms + binding.arrival_ms;
	binding.link.cost_ms = (short)binding.total_cost_ms;

	binding.hook_proof.spec.view_angles[YAW] = 0.0f;
	memset(&spec, 0x5a, sizeof(spec));
	CHECK(!SG_CompoundHookPublicationPlan(&binding, &spec));
	CHECK(spec.flight_ms == 0);
	binding.link.anchor[ROLL] = NAN;
	CHECK(!SG_CompoundHookPublicationPlan(&binding, &spec));
}

static void TestWrongActionOrSerializedPlanRejected(void)
{
	sg_compound_publication_binding_t binding;
	sg_hook_replay_spec_t hook_spec;
	sg_drop_replay_spec_t drop_spec;

	CommonBinding(&binding, RL_DOOR_SWIM, RLCM_PREOPEN);
	HookProof(&binding.hook_proof);
	CHECK(!SG_CompoundDropPublicationPlan(&binding, &drop_spec));
	CHECK(!SG_CompoundHookPublicationPlan(&binding, &hook_spec));

	CommonBinding(&binding, RL_DOOR_DROP, RLCM_PREOPEN);
	binding.link.mechanism_plan = 0;
	CHECK(!SG_CompoundDropPublicationPlan(&binding, &drop_spec));
}

int main(void)
{
	TestDropPlan();
	TestHookPlan();
	TestWrongActionOrSerializedPlanRejected();
	if (failures)
	{
		fprintf(stderr, "sg_compound_action_publication_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_action_publication_test: ok");
	return 0;
}
