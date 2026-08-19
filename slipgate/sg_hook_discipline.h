/*
 * sg_hook_discipline.h -- pure, live-controller rope admission laws.
 *
 * Keep this header host-free: the controller can use the policy without
 * changing RUNE topology or adding a production object dependency.
 */
#ifndef SG_HOOK_DISCIPLINE_H
#define SG_HOOK_DISCIPLINE_H

#define SG_HOOK_DISCIPLINE_SERVED_FIELD_MS 300
#define SG_HOOK_DISCIPLINE_FIELD_INF 0x3fffffff
#define SG_HOOK_DISCIPLINE_FAILURE_LIMIT 2
#define SG_HOOK_DISCIPLINE_BAN_SECONDS 20

typedef enum sg_hook_ride_worth_s
{
	/* A missing live route field cannot authorize an irreversible fire. */
	SG_HOOK_RIDE_UNASSESSED = 0,
	SG_HOOK_RIDE_REJECT,
	SG_HOOK_RIDE_ALLOW
} sg_hook_ride_worth_t;

/* The landing controller already names more than 300 field-ms as a served
 * ride.  Use the same strict boundary before firing; the static ropecost is
 * already represented in the fields and must not be charged again here. */
static inline sg_hook_ride_worth_t SG_HookExpectedRideWorth(int from_goal,
	int to_goal)
{
	if (from_goal >= SG_HOOK_DISCIPLINE_FIELD_INF ||
	    to_goal >= SG_HOOK_DISCIPLINE_FIELD_INF)
		return SG_HOOK_RIDE_UNASSESSED;
	return from_goal > to_goal + SG_HOOK_DISCIPLINE_SERVED_FIELD_MS
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

/* Return the stored streak after one graph-only failure.  A ban reports its
 * existing duration through ban_seconds and resets the streak exactly as the
 * legacy landing path did. */
static inline int SG_HookFailureStreakAdvance(int streak, int *ban_seconds)
{
	if (ban_seconds)
		*ban_seconds = 0;
	if (streak < 0)
		streak = 0;
	if (streak >= SG_HOOK_DISCIPLINE_FAILURE_LIMIT - 1)
	{
		if (ban_seconds)
			*ban_seconds = SG_HOOK_DISCIPLINE_BAN_SECONDS;
		return 0;
	}
	return streak + 1;
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
