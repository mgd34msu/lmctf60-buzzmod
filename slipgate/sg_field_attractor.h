#ifndef SG_FIELD_ATTRACTOR_H
#define SG_FIELD_ATTRACTOR_H

#include <stddef.h>
#include <stdint.h>

#define SG_FIELD_ATTRACTOR_NO_WITNESS UINT32_MAX

typedef struct sg_field_attractor_span_s
{
	uint32_t first;
	uint32_t count;
} sg_field_attractor_span_t;

typedef enum sg_field_attractor_witness_kind_e
{
	SG_FIELD_ATTRACTOR_WITNESS_TERMINAL = 0,
	SG_FIELD_ATTRACTOR_WITNESS_CHOICE,
	SG_FIELD_ATTRACTOR_WITNESS_LOCAL_PROGRESS,
	SG_FIELD_ATTRACTOR_WITNESS_CUT,
	SG_FIELD_ATTRACTOR_WITNESS_KIND_COUNT
} sg_field_attractor_witness_kind_t;

typedef struct sg_field_attractor_choice_s
{
	uint32_t source_state;
	sg_field_attractor_span_t destinations;
} sg_field_attractor_choice_t;

typedef struct sg_field_attractor_progress_s
{
	uint32_t source_state;
	sg_field_attractor_span_t destinations;
} sg_field_attractor_progress_t;

typedef struct sg_field_attractor_graph_s
{
	size_t state_count;
	const uint8_t *terminal_states;
	const sg_field_attractor_choice_t *choices;
	size_t choice_count;
	const uint32_t *choice_destinations;
	size_t choice_destination_count;
	const sg_field_attractor_progress_t *progress;
	size_t progress_count;
	const uint32_t *progress_destinations;
	size_t progress_destination_count;
} sg_field_attractor_graph_t;

typedef struct sg_field_attractor_result_s
{
	size_t state_count;
	uint8_t *reachable;
	uint32_t *rank;
	sg_field_attractor_witness_kind_t *witness_kind;
	uint32_t *witness_index;
} sg_field_attractor_result_t;

typedef enum sg_field_attractor_status_e
{
	SG_FIELD_ATTRACTOR_OK = 0,
	SG_FIELD_ATTRACTOR_INVALID,
	SG_FIELD_ATTRACTOR_STORAGE_FAILURE
} sg_field_attractor_status_t;

sg_field_attractor_status_t SG_FieldAttractorSolve(
	const sg_field_attractor_graph_t *graph,
	sg_field_attractor_result_t *result);
int SG_FieldAttractorVerify(const sg_field_attractor_graph_t *graph,
	const sg_field_attractor_result_t *result);
void SG_FieldAttractorResultDestroy(sg_field_attractor_result_t *result);

#endif
