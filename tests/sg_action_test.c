/* Exhaustive host test for the canonical action descriptor boundary. */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_action.h"

_Static_assert(SG_RUNE_ACTION_CONTRACT_CRC32 == 0x1e9e6975U,
	"RUNE action contract drift");
_Static_assert(SG_RUNE_MECHANISM_CONTRACT_CRC32 == 0xbef56f72U,
	"RUNE mechanism contract drift");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct expected_action_s
{
	int runtime_supported;
	int default_provenance;
	unsigned int provenance_mask;
	unsigned int mode_mask;
	unsigned int trait_mask;
	int endpoint_policy;
	int suffix_anchor_policy;
	int preopen_mechanism_anchor_policy;
	int ride_mechanism_anchor_policy;
	int control_policy;
	int mechanism_policy;
	int effective_suffix;
	int field_bias_policy;
	int field_bias_ms;
	const char *symbol;
	const char *name;
	const char *short_name;
	const char *color;
} expected_action_t;

/* Independent adapter golden: this deliberately does not expand
 * SG_ACTION_CONTRACT_ROWS, so a positional initializer regression in
 * sg_action.c cannot bless its own test expectation. */
static const expected_action_t expected_actions[SG_ACTION_COUNT] =
{
	{ 1, RL_PROVEN, 0x000fU, 0x01U, 0x0000U, RLEP_DRY_BOTH,
	  RLAP_RUN_WAYPOINT, RLAP_ZERO, RLAP_ZERO, RLCP_RUN, RLMP_NONE,
		  RL_RUN, RLFB_NONE, 0, "RL_RUN", "RUN", "RUN", "#9a9a9a" },
	{ 1, RL_PROVEN, 0x000fU, 0x01U, 0x0003U, RLEP_DRY_BOTH,
	  RLAP_ZERO, RLAP_ZERO, RLAP_ZERO, RLCP_JUMP, RLMP_NONE,
		  RL_JUMP, RLFB_NONE, 0, "RL_JUMP", "JUMP", "JUMP", "#00c8d7" },
	{ 1, RL_PROVEN, 0x0001U, 0x01U, 0x0003U, RLEP_FROM_DRY,
	  RLAP_DROP_LIP, RLAP_ZERO, RLAP_ZERO, RLCP_DROP, RLMP_NONE,
		  RL_DROP, RLFB_FIXED, 150, "RL_DROP", "DROP", "DROP", "#e0c000" },
	{ 1, RL_PROVEN, 0x0001U, 0x01U, 0x0001U, RLEP_NOT_BOTH_WATER,
	  RLAP_HOOK_CONTROL, RLAP_ZERO, RLAP_ZERO, RLCP_HOOK, RLMP_NONE,
		  RL_HOOK, RLFB_ROPE_CVAR, 0, "RL_HOOK", "HOOK", "HOOK", "#ff8c1a" },
	{ 1, RL_PROVEN, 0x0001U, 0x01U, 0x0021U, RLEP_AT_LEAST_ONE_WATER,
	  RLAP_ZERO, RLAP_ZERO, RLAP_ZERO, RLCP_SWIM, RLMP_NONE,
		  RL_SWIM, RLFB_NONE, 0, "RL_SWIM", "SWIM", "SWIM", "#3d7dff" },
	{ 1, RL_DECLARED, 0x0008U, 0x01U, 0x0025U, RLEP_ANY,
	  RLAP_WORLD, RLAP_ZERO, RLAP_ZERO, RLCP_DECLARED, RLMP_NONE,
		  RL_LIFT, RLFB_NONE, 0, "RL_LIFT", "LIFT", "LIFT", "#8f5cff" },
	{ 1, RL_DECLARED, 0x0008U, 0x01U, 0x0025U, RLEP_ANY,
	  RLAP_TELEPORT_PAD, RLAP_ZERO, RLAP_ZERO, RLCP_DECLARED, RLMP_NONE,
		  RL_TELEPORT, RLFB_NONE, 0, "RL_TELEPORT", "TELEPORT", "TELE",
	  "#00d18a" },
	{ 1, RL_PROVEN, 0x000fU, 0x01U, 0x0003U, RLEP_DRY_BOTH,
	  RLAP_ROCKET_CONTROL, RLAP_ZERO, RLAP_ZERO, RLCP_ROCKETJUMP, RLMP_NONE,
		  RL_ROCKETJUMP, RLFB_FIXED, 900, "RL_ROCKETJUMP", "ROCKETJUMP",
	  "RJ", "#ff3b30" },
	{ 1, RL_DECLARED, 0x0008U, 0x01U, 0x0025U, RLEP_DRY_BOTH,
	  RLAP_DOOR_WAIT, RLAP_ZERO, RLAP_ZERO, RLCP_DECLARED, RLMP_NONE,
		  RL_DOOR, RLFB_NONE, 0, "RL_DOOR", "DOOR", "DOOR", "#ff66c4" },
	{ 1, RL_CONTRACTED, 0x0010U, 0x06U, 0x007dU, RLEP_FROM_DRY,
	  RLAP_DROP_LIP, RLAP_DOOR_PREOPEN_CONTACT, RLAP_DOOR_RIDE_INGRESS_LIP,
		  RLCP_DROP, RLMP_DOOR_WORLD_FIXED_1_8, RL_DROP, RLFB_INHERIT, 0,
	  "RL_DOOR_DROP", "DOOR_DROP", "D_DROP", "#d4a600" },
	{ 1, RL_CONTRACTED, 0x0010U, 0x02U, 0x007dU, RLEP_FROM_WATER,
	  RLAP_ZERO, RLAP_DOOR_PREOPEN_CONTACT, RLAP_ZERO, RLCP_SWIM,
		  RLMP_DOOR_WORLD_FIXED_1_8, RL_SWIM, RLFB_INHERIT, 0,
	  "RL_DOOR_SWIM", "DOOR_SWIM", "D_SWIM", "#5a9cff" },
	{ 1, RL_CONTRACTED, 0x0010U, 0x02U, 0x007dU, RLEP_WATER_TO_DRY,
	  RLAP_HOOK_CONTROL, RLAP_DOOR_PREOPEN_CONTACT, RLAP_ZERO, RLCP_HOOK,
		  RLMP_DOOR_WORLD_FIXED_1_8, RL_HOOK, RLFB_INHERIT, 0,
	  "RL_DOOR_HOOK", "DOOR_HOOK", "D_HOOK", "#ff5bbd" },
	{ 1, RL_DECLARED, 0x0008U, 0x06U, 0x007dU, RLEP_DRY_BOTH,
	  RLAP_DOOR_WAIT, RLAP_DOOR_PREOPEN_CONTACT,
	  RLAP_DOOR_RIDE_INGRESS_LIP, RLCP_DECLARED,
		  RLMP_DOOR_WORLD_FIXED_1_8, RL_DOOR, RLFB_INHERIT, 0,
	  "RL_BUTTON_DOOR", "BUTTON_DOOR",
	  "B_DOOR", "#ff9f0a" },
	{ 1, RL_DECLARED, 0x0008U, 0x01U, 0x0027U, RLEP_DRY_BOTH,
	  RLAP_ZERO, RLAP_ZERO, RLAP_ZERO, RLCP_DECLARED, RLMP_NONE,
		  RL_PUSH, RLFB_NONE, 0, "RL_PUSH", "PUSH", "PUSH", "#b76cff" },
	{ 1, RL_DECLARED, 0x0008U, 0x02U, 0x003dU, RLEP_DRY_BOTH,
	  RLAP_WORLD, RLAP_TRAIN_CROSS, RLAP_ZERO, RLCP_DECLARED,
	  RLMP_TRAIN_WORLD_FIXED_1_8,
		  RL_TRAIN, RLFB_NONE, 0, "RL_TRAIN", "TRAIN", "TRAIN", "#00a6a6" },
};

static void TestActions(void)
{
	static const int runtime_owns_control[SG_ACTION_COUNT] =
		{ 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	static const int runtime_suppresses_localization[SG_ACTION_COUNT] =
		{ 0, 0, 0, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 };
	static const int uses_hook_policy[SG_ACTION_COUNT] =
		{ 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0 };
	static const int field_bias_at_rope_1000[SG_ACTION_COUNT] =
		{ 0, 0, 150, 1000, 0, 0, 0, 900, 0, 150, 0, 1000, 0, 0, 0 };
	static const unsigned int traits[] =
	{
		SG_ACTF_OWNS_CONTROL, SG_ACTF_BALLISTIC,
		SG_ACTF_MAP_MECHANISM, SG_ACTF_ATOMIC, SG_ACTF_DOOR_LEASE,
		SG_ACTF_SUPPRESS_LOCALIZATION, SG_ACTF_EFFECTIVE_SUFFIX
	};
	int action;
	unsigned int trait_mask = 0;
	unsigned int trait_index;

	CHECK(sizeof(traits) / sizeof(traits[0]) == SG_ACTION_TRAIT_COUNT);
	for (trait_index = 0;
	     trait_index < sizeof(traits) / sizeof(traits[0]); trait_index++)
		trait_mask |= traits[trait_index];
	CHECK(trait_mask == SG_ACTION_TRAIT_ALL_MASK);

	for (action = 0; action < SG_ACTION_COUNT; action++)
	{
		const sg_action_desc_t *desc = SG_ActionDescribe(action);
		const expected_action_t *expect = &expected_actions[action];
		int provenance, mode;
		unsigned int i;

		CHECK(desc != NULL);
		if (!desc)
			continue;
		CHECK((int)desc->action == action);
		CHECK(desc->runtime_supported == expect->runtime_supported);
		CHECK((int)desc->default_provenance == expect->default_provenance);
		CHECK(desc->provenance_mask == expect->provenance_mask);
		CHECK(desc->mode_mask == expect->mode_mask);
		CHECK(desc->trait_mask == expect->trait_mask);
		CHECK((int)desc->endpoint_policy == expect->endpoint_policy);
		CHECK((int)desc->suffix_anchor_policy ==
		      expect->suffix_anchor_policy);
		CHECK((int)desc->preopen_mechanism_anchor_policy ==
		      expect->preopen_mechanism_anchor_policy);
		CHECK((int)desc->ride_mechanism_anchor_policy ==
		      expect->ride_mechanism_anchor_policy);
		CHECK((int)desc->control_policy == expect->control_policy);
		CHECK((int)desc->mechanism_policy == expect->mechanism_policy);
		CHECK((int)desc->effective_suffix == expect->effective_suffix);
		CHECK((int)desc->field_bias_policy == expect->field_bias_policy);
		CHECK(desc->field_bias_ms == expect->field_bias_ms);
		CHECK(!strcmp(desc->symbol, expect->symbol));
		CHECK(!strcmp(desc->name, expect->name));
		CHECK(!strcmp(desc->short_name, expect->short_name));
		CHECK(!strcmp(desc->color, expect->color));
		CHECK(SG_ActionKnown(action));
		CHECK(SG_ActionRuntimeSupported(action) ==
		      expect->runtime_supported);
		CHECK(SG_ActionWireValid(action));
		CHECK(SG_ActionAllowsProvenance(action,
		      (int)desc->default_provenance));
		CHECK(SG_ActionEndpointPolicy(action) ==
		      (int)desc->endpoint_policy);
		CHECK(SG_ActionEffectiveSuffix(action) == expect->effective_suffix);
		CHECK(SG_ActionRuntimeHasTrait(action, SG_ACTF_OWNS_CONTROL) ==
		      runtime_owns_control[action]);
		CHECK(SG_ActionRuntimeHasTrait(
		          action, SG_ACTF_SUPPRESS_LOCALIZATION) ==
		      runtime_suppresses_localization[action]);
		CHECK(SG_ActionUsesHookPolicy(action) == uses_hook_policy[action]);
		CHECK(SG_ActionFieldBiasMs(action, 1000) ==
		      field_bias_at_rope_1000[action]);

		for (provenance = 0; provenance < SG_PROVENANCE_COUNT; provenance++)
			CHECK(SG_ActionAllowsProvenance(action, provenance) ==
			      ((desc->provenance_mask & (1U << provenance)) != 0));
		CHECK(!SG_ActionAllowsProvenance(action, -1));
		CHECK(!SG_ActionAllowsProvenance(action, INT_MAX));

		for (mode = 0; mode < SG_COMPOUND_MODE_COUNT; mode++)
			CHECK(SG_ActionAllowsMode(action, mode) ==
			      ((desc->mode_mask & (1U << mode)) != 0));
		CHECK(!SG_ActionAllowsMode(action, -1));
		CHECK(!SG_ActionAllowsMode(action, INT_MAX));

		for (i = 0; i < sizeof(traits) / sizeof(traits[0]); i++)
		{
			const sg_action_desc_t *suffix =
				SG_ActionDescribe(expect->effective_suffix);

			CHECK(SG_ActionTraitKnown(traits[i]));
			CHECK(SG_ActionHasTrait(action, traits[i]) ==
			      ((desc->trait_mask & traits[i]) != 0));
			CHECK(SG_ActionRuntimeHasTrait(action, traits[i]) ==
			      (expect->runtime_supported != 0 &&
			       (desc->trait_mask & traits[i]) != 0));
			CHECK(suffix != NULL);
			if (suffix)
				CHECK(SG_ActionEffectiveHasTrait(action, traits[i]) ==
				      ((suffix->trait_mask & traits[i]) != 0));
		}
	}

	/* Effective suffix classification remains independent of outer dispatch. */
	CHECK(SG_ActionRuntimeSupported(RL_DOOR_DROP));
	CHECK(!SG_ActionMechanismPlanRequired(RL_DOOR_DROP));
	CHECK(SG_ActionRuntimeSupported(SG_ActionEffectiveSuffix(RL_DOOR_DROP)));
	CHECK(!SG_ActionHasTrait(RL_DOOR_DROP, SG_ACTF_BALLISTIC));
	CHECK(SG_ActionEffectiveHasTrait(RL_DOOR_DROP, SG_ACTF_BALLISTIC));
	CHECK(SG_ActionRuntimeSupported(RL_DOOR_HOOK));
	CHECK(!SG_ActionMechanismPlanRequired(RL_DOOR_HOOK));
	CHECK(SG_ActionRuntimeSupported(SG_ActionEffectiveSuffix(RL_DOOR_HOOK)));
	CHECK(SG_ActionRuntimeHasTrait(RL_DOOR_HOOK, SG_ACTF_OWNS_CONTROL));
	CHECK(SG_ActionUsesHookPolicy(RL_DOOR_HOOK));
	CHECK(SG_ActionFieldBiasMs(RL_ROCKETJUMP, 1000) == 900);
	CHECK(SG_ActionFieldBiasMs(RL_DOOR_DROP, 1000) == 150);
	CHECK(SG_ActionFieldBiasMs(RL_HOOK, 1000) == 1000);
	CHECK(SG_ActionFieldBiasMs(RL_DOOR_HOOK, 1000) == 1000);
	CHECK(SG_ActionRuntimeSupported(RL_BUTTON_DOOR));
	CHECK(SG_ActionEffectiveSuffix(RL_BUTTON_DOOR) == RL_DOOR);
	CHECK(SG_ActionMechanismPlanRequired(RL_TRAIN));
	CHECK(SG_ActionFieldBiasMs(RL_HOOK, -1) == 0);
	CHECK(SG_ActionFieldBiasMs(RL_DOOR_HOOK, INT_MIN) == 0);
	CHECK(SG_ActionFieldBiasMs(RL_HOOK, INT_MAX) == INT_MAX);

	CHECK(SG_ActionDescribe(-1) == NULL);
	CHECK(SG_ActionDescribe(SG_ACTION_COUNT) == NULL);
	CHECK(SG_ActionDescribe(INT_MIN) == NULL);
	CHECK(SG_ActionDescribe(INT_MAX) == NULL);
	CHECK(!SG_ActionKnown(-1));
	CHECK(!SG_ActionKnown(SG_ACTION_COUNT));
	CHECK(!SG_ActionKnown(INT_MIN));
	CHECK(!SG_ActionKnown(INT_MAX));
	CHECK(!SG_ActionWireValid(-1));
	CHECK(!SG_ActionWireValid(SG_ACTION_COUNT));
	CHECK(!SG_ActionWireValid(INT_MIN));
	CHECK(!SG_ActionWireValid(INT_MAX));
	CHECK(!SG_ActionRuntimeSupported(INT_MIN));
	CHECK(!SG_ActionRuntimeSupported(INT_MAX));
	CHECK(!SG_ActionRuntimeHasTrait(INT_MIN, SG_ACTF_OWNS_CONTROL));
	CHECK(!SG_ActionRuntimeHasTrait(INT_MAX, SG_ACTF_OWNS_CONTROL));
	CHECK(!SG_ActionUsesHookPolicy(INT_MIN));
	CHECK(!SG_ActionUsesHookPolicy(INT_MAX));
	CHECK(SG_ActionFieldBiasMs(INT_MIN, 1000) == 0);
	CHECK(SG_ActionFieldBiasMs(INT_MAX, 1000) == 0);
	CHECK(!SG_ActionAllowsProvenance(INT_MIN, RL_PROVEN));
	CHECK(!SG_ActionAllowsProvenance(INT_MAX, RL_PROVEN));
	CHECK(!SG_ActionAllowsMode(INT_MIN, RLCM_NONE));
	CHECK(!SG_ActionAllowsMode(INT_MAX, RLCM_NONE));
	CHECK(SG_ActionEffectiveSuffix(INT_MIN) == -1);
	CHECK(SG_ActionEffectiveSuffix(INT_MAX) == -1);
	CHECK(SG_ActionEndpointPolicy(INT_MIN) == -1);
	CHECK(SG_ActionEndpointPolicy(INT_MAX) == -1);

	CHECK(!SG_ActionTraitKnown(0));
	CHECK(!SG_ActionTraitKnown(3));
	CHECK(!SG_ActionTraitKnown(128));
	CHECK(!SG_ActionTraitKnown(UINT_MAX));
	CHECK(!SG_ActionHasTrait(RL_RUN, 0));
	CHECK(!SG_ActionHasTrait(RL_RUN, UINT_MAX));
	CHECK(!SG_ActionRuntimeHasTrait(RL_JUMP, 0));
	CHECK(!SG_ActionRuntimeHasTrait(RL_JUMP, UINT_MAX));
	CHECK(!SG_ActionHasTrait(INT_MIN, SG_ACTF_OWNS_CONTROL));
	CHECK(!SG_ActionHasTrait(INT_MAX, SG_ACTF_OWNS_CONTROL));
	CHECK(!SG_ActionEffectiveHasTrait(INT_MIN, SG_ACTF_OWNS_CONTROL));
	CHECK(!SG_ActionEffectiveHasTrait(INT_MAX, SG_ACTF_OWNS_CONTROL));
}

static void TestProvenanceAndModes(void)
{
	int provenance, mode;

	for (provenance = 0; provenance < SG_PROVENANCE_COUNT; provenance++)
	{
		CHECK(SG_ProvenanceKnown(provenance));
		CHECK(SG_ProvenanceWireValid(provenance));
	}
	CHECK(!SG_ProvenanceKnown(-1));
	CHECK(!SG_ProvenanceKnown(SG_PROVENANCE_COUNT));
	CHECK(!SG_ProvenanceKnown(INT_MIN));
	CHECK(!SG_ProvenanceKnown(INT_MAX));
	CHECK(!SG_ProvenanceWireValid(-1));
	CHECK(!SG_ProvenanceWireValid(SG_PROVENANCE_COUNT));
	CHECK(!SG_ProvenanceWireValid(INT_MIN));
	CHECK(!SG_ProvenanceWireValid(INT_MAX));

	for (mode = 0; mode < SG_COMPOUND_MODE_COUNT; mode++)
	{
		CHECK(SG_ModeKnown(mode));
		CHECK(SG_ModeWireValid(mode));
	}
	CHECK(!SG_ModeKnown(-1));
	CHECK(!SG_ModeKnown(SG_COMPOUND_MODE_COUNT));
	CHECK(!SG_ModeKnown(INT_MIN));
	CHECK(!SG_ModeKnown(INT_MAX));
	CHECK(!SG_ModeWireValid(-1));
	CHECK(!SG_ModeWireValid(SG_COMPOUND_MODE_COUNT));
	CHECK(!SG_ModeWireValid(INT_MIN));
	CHECK(!SG_ModeWireValid(INT_MAX));
}

static void TestEndpointPolicies(void)
{
	/* Columns: dry->dry, dry->water, water->dry, water->water. */
	static const int expected[SG_ACTION_COUNT][4] =
	{
		{ 1, 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 1, 0, 0 },
		{ 1, 1, 1, 0 }, { 0, 1, 1, 1 }, { 1, 1, 1, 1 },
		{ 1, 1, 1, 1 }, { 1, 0, 0, 0 }, { 1, 0, 0, 0 },
		{ 1, 1, 0, 0 }, { 0, 0, 1, 1 }, { 0, 0, 1, 0 },
		{ 1, 0, 0, 0 }, { 1, 0, 0, 0 }, { 1, 0, 0, 0 }
	};
	int action, policy;

	CHECK(SG_ENDPOINT_POLICY_COUNT == RLEP_WATER_TO_DRY + 1);
	for (policy = 0; policy < SG_ENDPOINT_POLICY_COUNT; policy++)
		CHECK(SG_EndpointPolicyKnown(policy));
	CHECK(!SG_EndpointPolicyKnown(-1));
	CHECK(!SG_EndpointPolicyKnown(RLEP_WATER_TO_DRY + 1));
	CHECK(!SG_EndpointPolicyKnown(INT_MIN));
	CHECK(!SG_EndpointPolicyKnown(INT_MAX));

	for (action = 0; action < SG_ACTION_COUNT; action++)
	{
		CHECK(SG_ActionEndpointAllowed(action, 0, 0) == expected[action][0]);
		CHECK(SG_ActionEndpointAllowed(action, 0, 1) == expected[action][1]);
		CHECK(SG_ActionEndpointAllowed(action, 1, 0) == expected[action][2]);
		CHECK(SG_ActionEndpointAllowed(action, 1, 1) == expected[action][3]);
		CHECK(!SG_ActionEndpointAllowed(action, -1, 0));
		CHECK(!SG_ActionEndpointAllowed(action, 0, -1));
		CHECK(!SG_ActionEndpointAllowed(action, 2, 0));
		CHECK(!SG_ActionEndpointAllowed(action, 0, INT_MAX));
	}
	CHECK(!SG_ActionEndpointAllowed(INT_MIN, 0, 0));
	CHECK(!SG_ActionEndpointAllowed(INT_MAX, 0, 0));
}

int main(void)
{
	TestActions();
	TestProvenanceAndModes();
	TestEndpointPolicies();
	if (failures)
	{
		fprintf(stderr, "sg_action_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_action_test: ok");
	return 0;
}
