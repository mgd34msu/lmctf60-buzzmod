/* Owner-only field-service projections. */
#ifndef SG_RUNE_COMPACT_FIELD_SERVICE_PRIVATE_H
#define SG_RUNE_COMPACT_FIELD_SERVICE_PRIVATE_H

#include "sg_rune_compact_field_plan_private.h"
#include "sg_rune_compact_field_service.h"

sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceVisitExactStepProbes(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle,
	const sg_rune_compact_field_local_context_t *context,
	const sg_rune_compact_field_result_t *expected_result,
	sg_rune_compact_field_exact_probe_visit_fn visit, void *visit_context,
	uint32_t *probe_count_out);

#endif /* SG_RUNE_COMPACT_FIELD_SERVICE_PRIVATE_H */
