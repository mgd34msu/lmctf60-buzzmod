/* sg_action.h -- canonical RUNE action metadata and compatibility gates. */
#ifndef SG_ACTION_H
#define SG_ACTION_H

#include "sg_action_contract.generated.h"

/* These are wire-format generations, not the active runtime format.  V1/V2
 * keep their historical maxima forever; V3 knows the complete current
 * registry even while an individual controller remains runtime-unsupported. */
#define SG_RUNE_WIRE_V1 1
#define SG_RUNE_WIRE_V2 2
#define SG_RUNE_WIRE_V3 SG_RUNE_V3_VERSION

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
	int controller_revision;
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

/* Wire admission is version-specific and does not imply runtime support.
 * V1 forever admits action 0..7; V2 admits action 0..8. Both admit provenance
 * 0..3 and only mode 0. V3 is likewise frozen at action 0..11, provenance
 * 0..4, and mode 0..2; appending registry rows cannot silently widen an old
 * wire format. Unknown versions always fail closed. */
int SG_ActionWireValid(int version, int action);
int SG_ProvenanceKnown(int provenance);
int SG_ProvenanceWireValid(int version, int provenance);
int SG_ModeKnown(int mode);
int SG_ModeWireValid(int version, int mode);

/* Declarative policy queries. Invalid action, enum, mask, or endpoint inputs
 * fail closed. Effective-suffix queries are for inherited classification and
 * pricing only; execution must always dispatch on the outer action. */
int SG_ActionAllowsProvenance(int action, int provenance);
int SG_ActionAllowsMode(int action, int mode);
int SG_ActionTraitKnown(unsigned int trait);
int SG_ActionHasTrait(int action, unsigned int trait);
int SG_ActionEffectiveHasTrait(int action, unsigned int trait);
int SG_ActionEffectiveSuffix(int action);
int SG_EndpointPolicyKnown(int policy);
int SG_ActionEndpointPolicy(int action);
int SG_ActionEndpointAllowed(int action, int from_water, int to_water);

#endif /* SG_ACTION_H */
