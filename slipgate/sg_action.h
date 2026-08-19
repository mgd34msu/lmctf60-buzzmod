/* sg_action.h -- canonical RUNE action metadata and admission gates. */
#ifndef SG_ACTION_H
#define SG_ACTION_H

#include "sg_action_contract.generated.h"

typedef struct sg_action_desc_s
{
	rune_action_t action;
	int runtime_supported;
	rune_provenance_t default_provenance;
	unsigned int provenance_mask;
	unsigned int mode_mask;
	unsigned int trait_mask;
	rune_endpoint_policy_t endpoint_policy;
	rune_anchor_policy_t suffix_anchor_policy;
	rune_anchor_policy_t preopen_mechanism_anchor_policy;
	rune_anchor_policy_t ride_mechanism_anchor_policy;
	rune_control_policy_t control_policy;
	rune_mechanism_policy_t mechanism_policy;
	rune_action_t effective_suffix;
	rune_field_bias_policy_t field_bias_policy;
	int field_bias_ms;
	const char *symbol;
	const char *name;
	const char *short_name;
	const char *color;
} sg_action_desc_t;

/* Registry identity and implementation support are deliberately independent.
 * Describe/Known include registered actions whose controllers are not yet
 * available; RuntimeSupported is the only execution-availability query. */
const sg_action_desc_t *SG_ActionDescribe(int action);
int SG_ActionKnown(int action);
int SG_ActionRuntimeSupported(int action);

/* Artifact admission is distinct from implementation support.  A
 * registered action can be valid in the RUNE file while its controller
 * remains unavailable; execution still gates on RuntimeSupported. */
int SG_ActionWireValid(int action);
int SG_ProvenanceKnown(int provenance);
int SG_ProvenanceWireValid(int provenance);
int SG_ModeKnown(int mode);
int SG_ModeWireValid(int mode);

/* Declarative policy queries. Invalid action, enum, mask, or endpoint inputs
 * fail closed. Effective-suffix queries are for inherited classification and
 * pricing only; execution must always dispatch on the outer action. */
int SG_ActionAllowsProvenance(int action, int provenance);
int SG_ActionAllowsMode(int action, int mode);
int SG_ActionTraitKnown(unsigned int trait);
int SG_ActionHasTrait(int action, unsigned int trait);
int SG_ActionEffectiveHasTrait(int action, unsigned int trait);
int SG_ActionEffectiveSuffix(int action);
/* Runtime traits belong to the outer action. This helper never follows the
 * effective suffix and therefore cannot authorize an unimplemented compound
 * controller. */
int SG_ActionRuntimeHasTrait(int action, unsigned int trait);
/* Policy-only inheritance. Neither helper authorizes execution or dispatch. */
int SG_ActionUsesHookPolicy(int action);
int SG_ActionFieldBiasMs(int action, int rope_bias_ms);
int SG_EndpointPolicyKnown(int policy);
int SG_ActionEndpointPolicy(int action);
int SG_ActionEndpointAllowed(int action, int from_water, int to_water);

#endif /* SG_ACTION_H */
