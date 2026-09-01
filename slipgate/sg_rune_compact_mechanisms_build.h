#ifndef SG_RUNE_COMPACT_MECHANISMS_BUILD_H
#define SG_RUNE_COMPACT_MECHANISMS_BUILD_H

#include "sg_rune_compact_mechanisms.h"

/* Private handoff between mechanism derivation and the public immutable owner.
 * BuildCandidate returns an independently owned candidate.  The caller must
 * release it on every successful return, including when later validation
 * rejects the candidate. */
typedef struct sg_rune_compact_mechanisms_candidate_s
{
	sg_rune_compact_mechanism_authority_t *mechanisms;
	uint32_t mechanism_count;
	sg_rune_compact_mechanism_controller_t *controllers;
	uint32_t controller_count;
	sg_rune_compact_mechanism_topology_edge_t *topology_edges;
	uint32_t topology_edge_count;
	sg_rune_compact_mechanism_transition_t *transitions;
	uint32_t transition_count;
} sg_rune_compact_mechanisms_candidate_t;

int SG_RuneCompactMechanismsBuildCandidate(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_mechanisms_candidate_t *candidate_out,
	sg_rune_compact_mechanisms_error_t *error_out);
void SG_RuneCompactMechanismsReleaseCandidate(
	sg_rune_compact_mechanisms_candidate_t *candidate);

#if defined(SG_RUNE_COMPACT_MECHANISMS_BUILD_TESTING)
void SG_RuneCompactMechanismsBuildTestFailAfter(size_t allocation);
size_t SG_RuneCompactMechanismsBuildTestAllocationCount(void);
#endif

#endif
