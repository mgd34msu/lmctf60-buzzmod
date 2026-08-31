/* Allocation-free point localization over an accepted compact RUNE model. */
#ifndef SG_RUNE_COMPACT_LOCALIZE_H
#define SG_RUNE_COMPACT_LOCALIZE_H

#include "sg_rune_compact_model.h"

typedef enum sg_rune_compact_localize_status_e
{
	SG_RUNE_COMPACT_LOCALIZE_OK = 0,
	SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND,
	SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_LOCALIZE_INVALID_MODEL,
	SG_RUNE_COMPACT_LOCALIZE_INVALID_CANDIDATE,
	SG_RUNE_COMPACT_LOCALIZE_STATUS_COUNT
} sg_rune_compact_localize_status_t;

typedef struct sg_rune_compact_location_s
{
	sg_rune_compact_cell_index_t cell;
	sg_rune_stance_validity_t valid_stances;
	uint8_t reserved[3];
} sg_rune_compact_location_t;

/* Scans every dense cell. The Q8 point is a player origin in the expanded
 * configuration space, so valid_stances names the player boxes valid there. */
sg_rune_compact_localize_status_t SG_RuneCompactLocalize(
	const sg_rune_compact_model_t *model, const sg_rune_q8_vec3_t *point,
	sg_rune_compact_location_t *location_out);

/* Tests a caller-supplied candidate set, such as the result of a spatial
 * index. Candidate order and duplicates do not affect the result. */
sg_rune_compact_localize_status_t SG_RuneCompactLocalizeIndexed(
	const sg_rune_compact_model_t *model, const sg_rune_q8_vec3_t *point,
	const sg_rune_compact_cell_index_t *candidate_cells,
	uint32_t candidate_count, sg_rune_compact_location_t *location_out);

const char *SG_RuneCompactLocalizeStatusString(
	sg_rune_compact_localize_status_t status);

#endif
