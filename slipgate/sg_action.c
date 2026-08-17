/* sg_action.c -- generated metadata exposed through fail-closed queries. */
#include "q_shared.h"

#include <stddef.h>

#include "slipgate/sg_action.h"

#define SG_ACTION_DESC_ROW(symbol_, id_, runtime_supported_, default_prov_, \
	provenance_mask_, mode_mask_, trait_mask_, endpoint_, suffix_anchor_, \
	preopen_anchor_, ride_anchor_, control_, mechanism_, effective_suffix_, \
	bias_policy_, bias_ms_, name_, short_name_, color_) \
	{ symbol_, runtime_supported_, default_prov_, provenance_mask_, mode_mask_, \
	  trait_mask_, endpoint_, suffix_anchor_, preopen_anchor_, ride_anchor_, \
	  control_, mechanism_, effective_suffix_, bias_policy_, bias_ms_, \
	  #symbol_, name_, short_name_, color_ },

static const sg_action_desc_t sg_action_descs[SG_ACTION_COUNT] =
{
	SG_ACTION_CONTRACT_ROWS(SG_ACTION_DESC_ROW)
};

#undef SG_ACTION_DESC_ROW

_Static_assert(sizeof(sg_action_descs) / sizeof(sg_action_descs[0]) ==
	SG_ACTION_COUNT, "action descriptor row count drift");

const sg_action_desc_t *SG_ActionDescribe(int action)
{
	const sg_action_desc_t *desc;

	if (action < 0 || action >= SG_ACTION_COUNT)
		return NULL;
	desc = &sg_action_descs[action];
	return (int)desc->action == action ? desc : NULL;
}

int SG_ActionKnown(int action)
{
	return SG_ActionDescribe(action) != NULL;
}

int SG_ActionRuntimeSupported(int action)
{
	const sg_action_desc_t *desc = SG_ActionDescribe(action);

	return desc != NULL && desc->runtime_supported != 0;
}

int SG_ActionWireValid(int action)
{
	return SG_ActionKnown(action) &&
	       action >= SG_RUNE_WIRE_ACTION_FIRST &&
	       action <= SG_RUNE_WIRE_ACTION_MAX;
}

int SG_ProvenanceKnown(int provenance)
{
	return provenance >= RL_PROVEN && provenance < SG_PROVENANCE_COUNT;
}

int SG_ProvenanceWireValid(int provenance)
{
	return SG_ProvenanceKnown(provenance) &&
	       provenance <= RL_CONTRACTED;
}

int SG_ModeKnown(int mode)
{
	return mode >= RLCM_NONE && mode < SG_COMPOUND_MODE_COUNT;
}

int SG_ModeWireValid(int mode)
{
	return SG_ModeKnown(mode) && mode <= RLCM_RIDE;
}

int SG_ActionAllowsProvenance(int action, int provenance)
{
	const sg_action_desc_t *desc = SG_ActionDescribe(action);

	if (!desc || !SG_ProvenanceKnown(provenance))
		return 0;
	return (desc->provenance_mask & (1U << (unsigned int)provenance)) != 0;
}

int SG_ActionAllowsMode(int action, int mode)
{
	const sg_action_desc_t *desc = SG_ActionDescribe(action);

	if (!desc || !SG_ModeKnown(mode))
		return 0;
	return (desc->mode_mask & (1U << (unsigned int)mode)) != 0;
}

int SG_ActionTraitKnown(unsigned int trait)
{
	return trait != 0 && (trait & (trait - 1U)) == 0 &&
	       (trait & ~SG_ACTION_TRAIT_ALL_MASK) == 0;
}

int SG_ActionHasTrait(int action, unsigned int trait)
{
	const sg_action_desc_t *desc = SG_ActionDescribe(action);

	return desc != NULL && SG_ActionTraitKnown(trait) &&
	       (desc->trait_mask & trait) != 0;
}

int SG_ActionEffectiveSuffix(int action)
{
	int current = action;
	int hops;

	/* Keep parity with the generated Python contract helper.  The generator
	 * rejects cycles, but this public boundary remains fail-closed if a stale
	 * or hand-modified generated table ever reaches a build. */
	for (hops = 0; hops < SG_ACTION_COUNT; hops++)
	{
		const sg_action_desc_t *desc = SG_ActionDescribe(current);
		int suffix;

		if (!desc)
			return -1;
		suffix = (int)desc->effective_suffix;
		if (suffix == current)
			return current;
		current = suffix;
	}
	return -1;
}

int SG_ActionEffectiveHasTrait(int action, unsigned int trait)
{
	int suffix = SG_ActionEffectiveSuffix(action);

	return suffix >= 0 && SG_ActionHasTrait(suffix, trait);
}

int SG_ActionRuntimeHasTrait(int action, unsigned int trait)
{
	return SG_ActionRuntimeSupported(action) &&
	       SG_ActionHasTrait(action, trait);
}

int SG_ActionUsesHookPolicy(int action)
{
	return SG_ActionEffectiveSuffix(action) == RL_HOOK;
}

int SG_ActionFieldBiasMs(int action, int rope_bias_ms)
{
	int current = action;
	int hops;

	/* Pricing is metadata, not execution authorization. In particular the
	 * legacy reserved RJ retains its +900 cost while runtime-unsupported, and
	 * compounds inherit suffix pricing before their controllers exist. */
	for (hops = 0; hops < SG_ACTION_COUNT; hops++)
	{
		const sg_action_desc_t *desc = SG_ActionDescribe(current);

		if (!desc)
			return 0;
		switch (desc->field_bias_policy)
		{
		case RLFB_NONE:
			return 0;
		case RLFB_FIXED:
			return desc->field_bias_ms;
		case RLFB_ROPE_CVAR:
			return rope_bias_ms > 0 ? rope_bias_ms : 0;
		case RLFB_INHERIT:
			if ((int)desc->effective_suffix == current)
				return 0;
			current = (int)desc->effective_suffix;
			break;
		default:
			return 0;
		}
	}
	return 0;
}

int SG_EndpointPolicyKnown(int policy)
{
	return policy >= 0 && policy < SG_ENDPOINT_POLICY_COUNT;
}

int SG_ActionEndpointPolicy(int action)
{
	const sg_action_desc_t *desc = SG_ActionDescribe(action);

	return desc ? (int)desc->endpoint_policy : -1;
}

int SG_ActionEndpointAllowed(int action, int from_water, int to_water)
{
	int policy = SG_ActionEndpointPolicy(action);

	if (!SG_EndpointPolicyKnown(policy) ||
	    (from_water != 0 && from_water != 1) ||
	    (to_water != 0 && to_water != 1))
		return 0;
	switch (policy)
	{
	case RLEP_ANY:
		return 1;
	case RLEP_DRY_BOTH:
		return !from_water && !to_water;
	case RLEP_FROM_DRY:
		return !from_water;
	case RLEP_AT_LEAST_ONE_WATER:
		return from_water || to_water;
	case RLEP_NOT_BOTH_WATER:
		return !(from_water && to_water);
	case RLEP_FROM_WATER:
		return from_water;
	case RLEP_WATER_TO_DRY:
		return from_water && !to_water;
	default:
		return 0;
	}
}
