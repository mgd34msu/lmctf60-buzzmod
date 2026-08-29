/* Neutral learned-cost query; publication stays with downstream field owner. */
#include "sg_human_trace_learning_consumer.h"

int SG_HumanTraceLearningConsumerEffectiveKernelCost(
	const sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_kernel_key_t *key, uint64_t static_cost_us,
	uint64_t *effective_cost_us_out)
{
	return SG_HumanTraceLearningEffectiveKernelCost(parameters, key, static_cost_us,
		effective_cost_us_out);
}
