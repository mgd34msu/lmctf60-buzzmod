/* sg_rune_v2_codec.h -- canonical, allocation-free RUNE v2 codec. */
#ifndef SG_RUNE_V2_CODEC_H
#define SG_RUNE_V2_CODEC_H

#include <stddef.h>

#include "sg_rune_model.h"
#include "sg_rune_v2_wire.h"

/* Decode storage mirrors only the variable arrays owned by sg_rune_model_t. */
typedef struct sg_rune_v2_codec_storage_s
{
	sg_rune_plane_t *planes;
	size_t plane_capacity;
	sg_rune_vec3_t *portal_vertices;
	size_t portal_vertex_capacity;
	sg_rune_phase_basis_t *phases;
	size_t phase_capacity;
	sg_rune_phase_transition_t *phase_transitions;
	size_t phase_transition_capacity;
	sg_rune_cell_t *cells;
	size_t cell_capacity;
	sg_rune_portal_t *portals;
	size_t portal_capacity;
	sg_rune_surface_t *surfaces;
	size_t surface_capacity;
	sg_rune_affordance_t *affordances;
	size_t affordance_capacity;
	sg_rune_capability_kernel_t *kernels;
	size_t kernel_capacity;
	sg_rune_landmark_t *landmarks;
	size_t landmark_capacity;
	sg_rune_mechanism_t *mechanisms;
	size_t mechanism_capacity;
} sg_rune_v2_codec_storage_t;

sg_rune_v2_wire_diagnostic_t SG_RuneV2CodecEncodedSize(
	const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence,
	size_t *encoded_size_out);

sg_rune_v2_wire_diagnostic_t SG_RuneV2CodecEncode(
	const sg_rune_v2_wire_binding_t *binding,
	const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence,
	unsigned char *encoded, size_t encoded_capacity,
	size_t *encoded_size_out);

/* Decode is transactional. Scratch is destructive staging, including on
 * semantic rejection. Published storage and scalar outputs remain unchanged
 * unless the complete canonical model validates and decode succeeds. */
sg_rune_v2_wire_diagnostic_t SG_RuneV2CodecDecode(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_codec_storage_t *scratch,
	const sg_rune_v2_codec_storage_t *published,
	sg_rune_v2_wire_binding_t *binding_out,
	sg_rune_model_t *model_out,
	sg_rune_validation_evidence_t *evidence_out);

#endif /* SG_RUNE_V2_CODEC_H */
