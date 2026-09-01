#ifndef SG_RUNE_COMPACT_MECHANISMS_TRANSITIONS_H
#define SG_RUNE_COMPACT_MECHANISMS_TRANSITIONS_H

#include "sg_rune_compact_mechanisms.h"

/* Private, independently owned transition derivation result.  Spans are
 * parallel to the supplied authority array and index transitions. */
typedef struct sg_rune_compact_mechanism_transitions_result_s
{
	sg_rune_compact_mechanism_transition_t *transitions;
	uint32_t transition_count;
	sg_rune_compact_mechanism_span_t *spans;
	uint32_t mechanism_count;
} sg_rune_compact_mechanism_transitions_result_t;

int SG_RuneCompactMechanismTransitionsDerive(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_mechanism_authority_t *mechanisms,
	uint32_t mechanism_count,
	sg_rune_compact_mechanism_transitions_result_t *result_out,
	sg_rune_compact_mechanisms_error_t *error_out);
/* Re-derives from the authenticated builder and geometry and accepts only an
 * exact candidate transition image and its per-authority spans. */
int SG_RuneCompactMechanismTransitionsValidate(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_mechanism_authority_t *mechanisms,
	uint32_t mechanism_count,
	const sg_rune_compact_mechanism_transition_t *transitions,
	uint32_t transition_count,
	sg_rune_compact_mechanisms_error_t *error_out);
void SG_RuneCompactMechanismTransitionsRelease(
	sg_rune_compact_mechanism_transitions_result_t *result);

#if defined(SG_RUNE_COMPACT_MECHANISM_TRANSITIONS_TESTING)
void SG_RuneCompactMechanismTransitionsTestFailAfter(size_t allocation);
size_t SG_RuneCompactMechanismTransitionsTestAllocationCount(void);
#endif

#endif
