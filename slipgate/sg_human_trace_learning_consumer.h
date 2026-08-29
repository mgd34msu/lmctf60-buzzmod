/* Narrow, read-only bridge for the continuous field-service owner. */
#ifndef SG_HUMAN_TRACE_LEARNING_CONSUMER_H
#define SG_HUMAN_TRACE_LEARNING_CONSUMER_H

#include "sg_human_trace_learning_contract.h"

#if defined _WIN32 || defined __CYGWIN__
#define SG_HUMAN_TRACE_LEARNING_CONSUMER_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define SG_HUMAN_TRACE_LEARNING_CONSUMER_EXPORT \
	__attribute__((visibility("default")))
#else
#define SG_HUMAN_TRACE_LEARNING_CONSUMER_EXPORT
#endif

/* A NULL parameter view is intentionally neutral. Runtime/model assembly and
 * field-service publication remain downstream ownership; this seam never
 * publishes a runtime or modifies a model. */
SG_HUMAN_TRACE_LEARNING_CONSUMER_EXPORT int
SG_HumanTraceLearningConsumerEffectiveKernelCost(
	const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_kernel_key_t *key, uint64_t static_cost_us,
	uint64_t *effective_cost_us_out);

#endif /* SG_HUMAN_TRACE_LEARNING_CONSUMER_H */
