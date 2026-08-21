#ifndef SG_COMPOUND_PUBLICATION_INTERNAL_H
#define SG_COMPOUND_PUBLICATION_INTERNAL_H

#include "sg_compound_publication.h"
#include "sg_local.h"

struct sg_compound_publication_s
{
	size_t binding_count;
	size_t mechanism_count;
	sg_compound_publication_binding_t *bindings;
	sg_compound_world_preopen_t *mechanisms;
	sg_compound_hook_publication_proof_t *hook_proofs;
	sg_compound_publication_free_fn deallocate;
};

sg_compound_publication_result_t CompoundPublicationResult(
	sg_compound_publication_status_t status, rune_reject_reason_t reason,
	uint32_t link_index);
int CompoundPublicationFloatBitsEqual(float first, float second);
int CompoundPublicationVectorEqual(const float first[3],
	const float second[3]);
int CompoundPublicationRuneShapeValid(const rune_t *rune);
int CompoundPublicationNativeLinkValid(const rune_t *rune,
	const rune_link_t *link);
int CompoundPublicationMechanismEqual(
	const sg_compound_world_preopen_t *first,
	const sg_compound_world_preopen_t *second);
int CompoundPublicationProofValid(const rune_link_t *link,
	const sg_compound_swim_proof_t *proof);

#endif
