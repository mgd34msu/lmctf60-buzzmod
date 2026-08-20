/*
 * sg_descend.h -- the descent module's face: the two stages the
 * orchestrator calls.
 */
#ifndef SG_DESCEND_H
#define SG_DESCEND_H

/* per-link human traffic tiers, loaded alongside the rune (0-255) --
 * the descent's own link-cost pricing terms */
extern unsigned char *sg_human_use;
extern unsigned char *sg_human_live;    /* cut from the 20s windows */
extern unsigned char *sg_human_escape;  /* the ESCAPEE's cut: only the flag */

/* per-seed human defensive dwell / steal-response END, per team */
extern unsigned char *sg_def_post[2];
extern unsigned char *sg_def_icept[2];

/* prices the link fan and picks the leg; reads its inputs from the think
 * context and writes bestval/incumbent/rail_* results back into it */
int Think_PickLink(sg_bot_t *bot, sg_think_t *tc);

/* holds or releases the committed leg; context in, context out, cmd stays
 * a parameter until the movement stage speaks context */
int Think_CommitLink(sg_bot_t *bot, sg_think_t *tc);

/* Ordinary RUN completion is kept separate from the mechanism handoff. The
 * plan-candidate predicate below proves only action/plan admission;
 * normal PickLink and the live binding/controller remain the runtime owners. */
typedef enum sg_run_completion_e
{
	SG_RUN_INCOMPLETE = 0,
	SG_RUN_ARRIVED,
	SG_RUN_OVERACHIEVED
} sg_run_completion_t;

sg_run_completion_t SG_RunCommitCompletion(const rune_t *rune,
	const rune_link_t *link, int localized_seed, const vec3_t body_origin,
	const int *goal_field);
qboolean SG_RunMechanismPlanCandidateValid(const rune_t *rune, int seed,
	int link_index);
qboolean SG_RunHasMechanismSuccessor(const rune_t *rune, int seed);
void SG_RunInvalidateCompletedCandidate(const rune_t *rune,
	int completed_link, sg_run_completion_t completion, int localized_seed,
	int *next_link);
qboolean SG_RunCompletionHandoff(const rune_t *rune, int completed_link,
	sg_run_completion_t completion, sg_bot_t *bot, sg_think_t *tc,
	int *next_link);

#ifdef SG_STRIKE_TRANSITION_TEST_API
qboolean SG_StrikeTestWeaponReconcile(sg_bot_t *bot, sg_think_t *tc);
void SG_StrikeTestWeaponCancelStaged(sg_bot_t *bot, int action);
qboolean SG_StrikeTestWeaponPrepareCommit(sg_bot_t *bot, sg_think_t *tc);
int SG_StrikeTestWeaponFilterFreshCandidate(const sg_bot_t *bot,
	const sg_think_t *tc, int bestlink);
void SG_StrikeTestCommitFreshLink(sg_bot_t *bot, const sg_think_t *tc,
	int bestlink);
void SG_StrikeTestRetireSupersededPureRouteCommit(sg_bot_t *bot,
	const sg_think_t *tc);
qboolean SG_StrikeTestRailLateOverrideAllowed(const sg_bot_t *bot,
	const sg_think_t *tc);
qboolean SG_StrikeTestRailWatchdogAllowed(const sg_bot_t *bot,
	const sg_think_t *tc);
#endif

#endif /* SG_DESCEND_H */
