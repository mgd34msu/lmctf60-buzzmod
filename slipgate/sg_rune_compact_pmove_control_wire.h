/* Canonical v13 RUNE section codec.  This is not a standalone artifact. */
#ifndef SG_RUNE_COMPACT_PMOVE_CONTROL_WIRE_H
#define SG_RUNE_COMPACT_PMOVE_CONTROL_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_compact_pmove_control.h"

#define SG_RUNE_PMOVE_CONTROL_SECTION_TAG UINT32_C(0x504d4352)

typedef struct sg_rune_pmove_control_storage_s
{
	sg_rune_pmove_control_region_t region;
	sg_rune_pmove_control_potential_t potential;
	sg_rune_pmove_control_certificate_t certificate;
	sg_rune_pmove_control_transition_t transitions[2];
} sg_rune_pmove_control_storage_t;

int SG_RunePmoveControlSectionMeasure(
	const sg_rune_pmove_control_model_t *model, size_t *size_out,
	sg_rune_pmove_control_error_t *error_out);
int SG_RunePmoveControlSectionEncode(
	const sg_rune_pmove_control_model_t *model, void *bytes, size_t size,
	sg_rune_pmove_control_error_t *error_out);
int SG_RunePmoveControlSectionInspect(const void *bytes, size_t size,
	sg_rune_pmove_control_identity_t *identity_out,
	sg_rune_pmove_control_error_t *error_out);
int SG_RunePmoveControlSectionDecode(const void *bytes, size_t size,
	sg_rune_pmove_control_storage_t *storage,
	sg_rune_pmove_control_model_t *model_out,
	sg_rune_pmove_control_error_t *error_out);

#endif /* SG_RUNE_COMPACT_PMOVE_CONTROL_WIRE_H */
