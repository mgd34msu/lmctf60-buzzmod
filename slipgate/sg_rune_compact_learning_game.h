/* Game-facing lifecycle for level-scoped compact human learning. */
#ifndef SG_RUNE_COMPACT_LEARNING_GAME_H
#define SG_RUNE_COMPACT_LEARNING_GAME_H

#include "sg_rune_compact_learning_consumer.h"
#include "sg_rune_compact_production.h"

/* These two hooks are used only by the compact production owner.  They keep
 * recorder-facing types out of its artifact/runtime header surface. */
int SG_RuneCompactLearningProductionInstall(
	sg_rune_compact_production_t *owner,
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *identity);
void SG_RuneCompactLearningProductionRetire(
	sg_rune_compact_production_t *owner);

/* Called once at terminal match capture.  The supplied game validator may
 * retain only pre-existing compact cost, landing, tactical, or strategy facts;
 * it cannot create topology, geometry, or transitions. */
sg_rune_compact_learning_consumer_status_t
SG_RuneCompactLearningProductionIngest(
	sg_rune_compact_production_t *owner, const char *expected_mapname,
	sg_rune_compact_learning_consumer_validate_fn validate,
	void *validate_context,
	sg_rune_compact_learning_consumer_report_t *report_out);

/* Copy published level-scoped priors while their exact model remains current. */
uint32_t SG_RuneCompactLearningProductionPriorCount(
	const sg_rune_compact_production_t *owner);
int SG_RuneCompactLearningProductionPriorRead(
	const sg_rune_compact_production_t *owner, uint32_t index,
	sg_rune_compact_learning_prior_t *prior_out);

#endif /* SG_RUNE_COMPACT_LEARNING_GAME_H */
