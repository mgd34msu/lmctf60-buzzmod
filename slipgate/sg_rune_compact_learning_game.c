#include "../g_local.h"

#include "sg_rune_compact_learning_game.h"

#include <string.h>

int SG_RuneCompactLearningProductionInstall(
	sg_rune_compact_production_t *owner,
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *identity)
{
	sg_rune_compact_learning_consumer_t *learning = NULL;

	if (owner == NULL || owner->learning != NULL || model == NULL ||
		identity == NULL)
		return 0;
	if (SG_RuneCompactLearningConsumerCreate(model, identity, &learning, NULL) !=
		SG_RUNE_COMPACT_LEARNING_CONSUMER_OK)
		return 0;
	owner->learning = learning;
	return 1;
}

void SG_RuneCompactLearningProductionRetire(
	sg_rune_compact_production_t *owner)
{
	if (owner == NULL)
		return;
	SG_RuneCompactLearningConsumerDestroy(owner->learning);
	owner->learning = NULL;
}

sg_rune_compact_learning_consumer_status_t
SG_RuneCompactLearningProductionIngest(
	sg_rune_compact_production_t *owner, const char *expected_mapname,
	sg_rune_compact_learning_consumer_validate_fn validate,
	void *validate_context,
	sg_rune_compact_learning_consumer_report_t *report_out)
{
	if (report_out != NULL)
		memset(report_out, 0, sizeof(*report_out));
	if (!SG_RuneCompactProductionCurrent(owner) || expected_mapname == NULL ||
		expected_mapname[0] == '\0' || validate == NULL || report_out == NULL)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_INVALID_ARGUMENT;
	return SG_RuneCompactLearningConsumerIngestCurrentV3Collection(
		owner->learning, expected_mapname, validate, validate_context,
		report_out);
}

uint32_t SG_RuneCompactLearningProductionPriorCount(
	const sg_rune_compact_production_t *owner)
{
	return SG_RuneCompactProductionCurrent(owner) ?
		SG_RuneCompactLearningConsumerPriorCount(owner->learning) : 0U;
}

int SG_RuneCompactLearningProductionPriorRead(
	const sg_rune_compact_production_t *owner, uint32_t index,
	sg_rune_compact_learning_prior_t *prior_out)
{
	return SG_RuneCompactProductionCurrent(owner) ?
		SG_RuneCompactLearningConsumerPriorRead(owner->learning, index,
			prior_out) : 0;
}
