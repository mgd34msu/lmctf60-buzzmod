/* Private live-acceptance instrumentation. Never compile into production. */
#ifndef SG_ACCEPT_DROP_H
#define SG_ACCEPT_DROP_H

#ifdef SG_ACCEPT_DROP
struct sg_bot_s;
struct sg_drop_live_events_s;
struct sg_drop_live_result_s;
struct rune_link_s;

qboolean SG_AcceptDropQueue(const char *case_name, const char *slot_text,
	const char *link_text);
void SG_AcceptDropLevelReset(void);
void SG_AcceptDropFrameBegin(void);
void SG_AcceptDropEntityPass(void);
void SG_AcceptDropArm(void);
void SG_AcceptDropFrameEvent(const char *event);
void SG_AcceptDropPusher(const edict_t *ent, const char *phase);
void SG_AcceptDropAfterBot(struct sg_bot_s *bot);

qboolean SG_AcceptDropLegacyAuthority(const struct sg_bot_s *bot,
	int link_index);
void SG_AcceptDropActionBegin(const struct sg_bot_s *bot, int link_index,
	const char *authority);
qboolean SG_AcceptDropOwnsStep(const struct sg_bot_s *bot, int link_index);
/* Legacy A is a pure observer.  These hooks transfer only the host's already
 * latched DROP events into that observer; they never acquire production
 * reducer ownership or replay a host side effect. */
qboolean SG_AcceptDropObserverEventOwner(const struct sg_bot_s *bot);
qboolean SG_AcceptDropObserverBeginCommand(const struct sg_bot_s *bot,
	int link_index, struct sg_drop_live_events_s *events,
	qboolean *source_door_pending);
void SG_AcceptDropObserverTakeEvents(const struct sg_bot_s *bot,
	int link_index, int step, struct sg_drop_live_events_s *events);
void SG_AcceptDropCommandHistorical(const struct sg_bot_s *bot,
	int link_index, int step, const usercmd_t *command);
void SG_AcceptDropCommand(const struct sg_bot_s *bot, int link_index,
	int step, const usercmd_t *command);
void SG_AcceptDropPose(const struct sg_bot_s *bot, int link_index,
	int step, const edict_t *ent);
qboolean SG_AcceptDropAfterStep(struct sg_bot_s *bot, int link_index,
	int step, edict_t *ent);
qboolean SG_AcceptDropStopBeforeEmit(const struct sg_bot_s *bot,
	int link_index);
void SG_AcceptDropGenericHandoffBegin(const struct sg_bot_s *bot, int bestlink,
	const usercmd_t *command, int substeps);
void SG_AcceptDropGenericHandoffEnd(const struct sg_bot_s *bot, int bestlink,
	int substeps, int total_msec);
void SG_AcceptDropBoundary(const struct sg_bot_s *bot, int link_index,
	const char *edge, const struct sg_drop_live_result_s *result,
	const struct sg_drop_live_events_s *events);
void SG_AcceptDropCallback(const char *kind, const edict_t *ent,
	const struct rune_link_s *link);
void SG_AcceptDropPredicate(const char *kind, const edict_t *ent,
	const struct rune_link_s *link);
void SG_AcceptDropPredicateResult(const char *kind, const edict_t *ent,
	const struct rune_link_s *link, qboolean accepted);
void SG_AcceptDropTrace(const char *kind, const edict_t *ent,
	const struct rune_link_s *link, const trace_t *trace, qboolean accepted);
void SG_AcceptDropLegacyObserverBoundary(struct sg_bot_s *bot,
	int link_index, const edict_t *ent);
void SG_AcceptDropShelf(const struct sg_bot_s *bot, int link_index,
	const char *reason);
void SG_AcceptDropTeach(int link_index, const char *reason);
#endif

#endif /* SG_ACCEPT_DROP_H */
