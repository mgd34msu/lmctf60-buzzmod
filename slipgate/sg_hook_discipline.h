/* Host-free graph-hook launch policy. */
#ifndef SG_HOOK_DISCIPLINE_H
#define SG_HOOK_DISCIPLINE_H

#define SG_HOOK_DISCIPLINE_SERVED_FIELD_MS 300
#define SG_HOOK_DISCIPLINE_FIELD_INF 0x3fffffff

typedef enum sg_hook_ride_worth_s
{
	/* A missing live route field cannot authorize an irreversible fire. */
	SG_HOOK_RIDE_UNASSESSED = 0,
	SG_HOOK_RIDE_REJECT,
	SG_HOOK_RIDE_ALLOW
} sg_hook_ride_worth_t;

/* Require useful endpoint progress before staging a proved ride. */
static inline sg_hook_ride_worth_t SG_HookExpectedRideWorth(int from_goal,
	int to_goal)
{
	if (from_goal < 0 || to_goal < 0 ||
	    from_goal >= SG_HOOK_DISCIPLINE_FIELD_INF ||
	    to_goal >= SG_HOOK_DISCIPLINE_FIELD_INF)
		return SG_HOOK_RIDE_UNASSESSED;
	return from_goal > to_goal + SG_HOOK_DISCIPLINE_SERVED_FIELD_MS
	    ? SG_HOOK_RIDE_ALLOW : SG_HOOK_RIDE_REJECT;
}

/* Fire only while this edge remains no worse than the refreshed best route. */
static inline sg_hook_ride_worth_t SG_HookCurrentRideWorth(int from_goal,
	int to_goal, int hook_traversal_ms)
{
	sg_hook_ride_worth_t endpoint_worth;
	int complete_hook_ms;

	if (from_goal < 0 || to_goal < 0 || hook_traversal_ms < 0 ||
	    from_goal >= SG_HOOK_DISCIPLINE_FIELD_INF ||
	    to_goal >= SG_HOOK_DISCIPLINE_FIELD_INF ||
	    hook_traversal_ms >= SG_HOOK_DISCIPLINE_FIELD_INF - to_goal)
		return SG_HOOK_RIDE_UNASSESSED;
	endpoint_worth = SG_HookExpectedRideWorth(from_goal, to_goal);
	if (endpoint_worth != SG_HOOK_RIDE_ALLOW)
		return endpoint_worth;
	complete_hook_ms = to_goal + hook_traversal_ms;
	return complete_hook_ms <= from_goal
	    ? SG_HOOK_RIDE_ALLOW : SG_HOOK_RIDE_REJECT;
}

/* The stored RUNE proof establishes physical feasibility.  Launch authority
 * is narrower: the current route field must positively establish useful
 * progress.  An unavailable endpoint is not permission to spend a rope on a
 * stale objective. */
static inline int SG_HookRideLaunchAllowed(sg_hook_ride_worth_t worth)
{
	return worth == SG_HOOK_RIDE_ALLOW;
}

/* A proved hook source owns its medium.  Source-state drift is not evidence
 * that the graph edge is bad, but it is absolute authority to refuse generic
 * movement toward the landing and re-localize before selecting again. */
static inline int SG_HookStageSourceCompatible(int source_water,
	int destination_water, int live_dry, int live_water, int air_safe)
{
	if (source_water)
		return !destination_water && live_water && air_safe;
	return live_dry;
}

#endif /* SG_HOOK_DISCIPLINE_H */
