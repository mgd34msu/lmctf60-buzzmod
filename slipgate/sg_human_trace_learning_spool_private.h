/* Host-only durable spool consumption. */
#ifndef SG_HUMAN_TRACE_LEARNING_SPOOL_PRIVATE_H
#define SG_HUMAN_TRACE_LEARNING_SPOOL_PRIVATE_H

#if !defined(SG_HUMAN_TRACE_LEARNING_SPOOL_INTERNAL) && \
	!defined(SG_HUMAN_TRACE_LEARNING_TEST)
#error "durable human-trace receipt mutation belongs to the host only"
#endif

#include "sg_human_trace.h"

#if defined(__GNUC__) || defined(__clang__)
#define SG_HUMAN_TRACE_LEARNING_SPOOL_LOCAL __attribute__((visibility("hidden")))
#else
#define SG_HUMAN_TRACE_LEARNING_SPOOL_LOCAL
#endif

#if defined(SG_HUMAN_TRACE_LEARNING_SPOOL_INTERNAL) || \
	defined(SG_HUMAN_TRACE_LEARNING_TEST)
typedef struct sg_human_trace_v3_scope_acceptance_s
	sg_human_trace_v3_scope_acceptance_t;

typedef struct sg_human_trace_v3_collection_visitor_s
{
	int (*begin_root)(void *context,
		const sg_human_trace_v3_spool_ref_t *spool);
	int (*event)(void *context,
		const sg_human_trace_v3_scope_acceptance_t *scope,
		const sg_human_trace_v3_event_t *event);
	int (*finish_root)(void *context);
} sg_human_trace_v3_collection_visitor_t;

SG_HUMAN_TRACE_LEARNING_SPOOL_LOCAL int
SG_HumanTraceVisitAcceptedV3Collection(const sg_level_identity_t *identity,
	const sg_human_trace_v3_collection_visitor_t *visitor, void *context);
SG_HUMAN_TRACE_LEARNING_SPOOL_LOCAL int
SG_HumanTraceAcceptedV3Directory(
	char directory[SG_HUMAN_TRACE_SPOOL_PATH_BYTES]);
SG_HUMAN_TRACE_LEARNING_SPOOL_LOCAL int
SG_HumanTraceAcceptedV3RootLearningCompatible(
	const sg_human_trace_v3_spool_ref_t *spool);
SG_HUMAN_TRACE_LEARNING_SPOOL_LOCAL int
SG_HumanTraceAcceptedV3ScopeView(
	const sg_human_trace_v3_scope_acceptance_t *scope,
	const sg_human_trace_v3_spool_ref_t **spool_out,
	uint32_t *client_id_out, uint64_t *spawn_generation_out);
#ifdef SG_HUMAN_TRACE_LEARNING_TEST
SG_HUMAN_TRACE_LEARNING_SPOOL_LOCAL int
SG_HumanTraceLearningSpoolTestFormatJsonPath(const char *directory,
	const sg_level_identity_t *identity, uint32_t segment,
	char path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES]);
SG_HUMAN_TRACE_LEARNING_SPOOL_LOCAL int
SG_HumanTraceLearningSpoolTestJsonNameSegment(const char *name,
	const sg_level_identity_t *identity, uint32_t *segment_out);
#endif
#endif

#endif /* SG_HUMAN_TRACE_LEARNING_SPOOL_PRIVATE_H */
