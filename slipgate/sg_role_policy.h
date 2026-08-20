/* sg_role_policy.h -- pure roster laws shared by role assignment and tests. */
#ifndef SG_ROLE_POLICY_H
#define SG_ROLE_POLICY_H

#include "g_ctffunc.h"

static inline int SG_CoordinationBodyLive(int active, int inuse, int deadflag,
	int health)
{
	return active && inuse && !deadflag && health > 0;
}

/* Rank one slot among the currently live same-team bodies.  A dead/missing
 * self ranks after every live body, so it cannot reserve a live defender post
 * while waiting to respawn. */
static inline int SG_RoleLiveRank(const unsigned char *eligible, int count,
	int self, int *live_count)
{
	int rank = 0;
	int live = 0;
	int index;

	if (live_count)
		*live_count = 0;
	if (!eligible || count <= 0 || self < 0 || self >= count)
		return -1;
	for (index = 0; index < count; index++)
	{
		if (index == self)
			rank = live;
		if (!eligible[index])
			continue;
		live++;
	}
	if (!eligible[self])
		rank = live;
	if (live_count)
		*live_count = live;
	return rank;
}

static inline int SG_RoleOutsideDefenderQuota(const unsigned char *eligible,
	int count, int slot, int defenders_wanted)
{
	int live_count;
	int rank;

	if (!eligible || count <= 0 || slot < 0 || slot >= count ||
	    defenders_wanted < 0 || !eligible[slot])
		return 0;
	rank = SG_RoleLiveRank(eligible, count, slot, &live_count);
	return rank >= defenders_wanted && rank < live_count;
}

/* A relay scoop keeps escort team semantics but owns a physical enemy-flag
 * touch, so movement and route-failure policy treat it as urgent. */
static inline int SG_EnemyFlagTouchMissionActive(int strike_pressure,
	int scoop_mission)
{
	if ((strike_pressure != 0 && strike_pressure != 1) ||
	    (scoop_mission != 0 && scoop_mission != 1))
		return 0;
	return strike_pressure || scoop_mission;
}

static inline int SG_OrderedEscortDirectAimAllowed(int target_live,
	int terminal)
{
	if ((target_live != 0 && target_live != 1) ||
	    (terminal != 0 && terminal != 1))
		return 0;
	return target_live && terminal;
}

/* Near-goal defenders and escorts are intentionally stationed. A flag-touch
 * mission may not inherit that stationary exemption. */
static inline int SG_RoleMissionHold(int role, int goal_cost,
	int ordered_escort_terminal, int enemy_flag_touch_mission)
{
	if (enemy_flag_touch_mission != 0 && enemy_flag_touch_mission != 1)
		return 0;
	if (enemy_flag_touch_mission)
		return 0;
	if (role == SG_ROLE_ESCORT && ordered_escort_terminal)
		return 1;
	return (role == SG_ROLE_DEFEND || role == SG_ROLE_ESCORT) &&
	       goal_cost >= 0 && goal_cost < 1500;
}

/* The generic wedge clock measures navigation deadlock, not a stationary
 * firefight.  A current retained duel or combat ownership from the preceding
 * frame is positive activity even when the body has not translated. */
static inline int SG_WedgeClockReset(float displacement, int duel,
	int engaged_last)
{
	return displacement > 96.0f || duel || engaged_last;
}

/* Only a hold that is active on this movement frame can excuse a stationary
 * body from the last-resort wedge recovery.  The backing timestamps are
 * histories shared by several bounded policies; their mere presence does not
 * mean that any of those policies still owns the body. */
static inline int SG_WedgeKillHoldClear(int rally_hold, int rail_hold)
{
	return !rally_hold && !rail_hold;
}

/* A carrier orbit is always failed objective movement. Enemy-stand pressure
 * may use the same recovery only while the flag is still home and the entire
 * recorded loop was navigation-owned; a real fight remains resistance, not a
 * bad road. */
static inline int SG_ObjectiveOrbitMayShelf(int role, int enemy_pressure,
	int enemy_flag_home, int combat_since_visit)
{
	return role == SG_ROLE_CARRY ||
	       (role != SG_ROLE_DEFEND && enemy_pressure && enemy_flag_home &&
	        !combat_since_visit);
}

/* Route-failure clocks may judge only navigation-owned motion.  Mission holds
 * and combat-owned frames cannot prove that the selected graph link failed. */
static inline int SG_RouteFailureWatchSuppressed(int role, int goal_cost,
	int ordered_escort_terminal, int enemy_flag_touch_mission, int duel,
	int engaged_last)
{
	return SG_RoleMissionHold(role, goal_cost, ordered_escort_terminal,
	           enemy_flag_touch_mission) ||
	       duel || engaged_last;
}

static inline int SG_RoleOwnsDefenseState(int role)
{
	return role == SG_ROLE_DEFEND;
}

/* Carrier-spacing applies to bodies without the effective escort mission.
 * The coordinator may grant escort to an organic attacker or replace an
 * organic escort with another duty, so the organic role is not authority. */
static inline int SG_AntiLingerEligible(int role, int escort_mission)
{
	if (role < SG_ROLE_ATTACK || role >= SG_ROLES ||
	    (escort_mission != 0 && escort_mission != 1))
		return 0;
	return role != SG_ROLE_CARRY && !escort_mission;
}

static inline int SG_EscortSupportFullStrength(int escort_mission)
{
	return escort_mission == 1;
}

/* Interposition dose 1 is the legacy carrier/threat midpoint, dose 2 the
 * static exit, and dose 3+ the moving lead/trail formation.  Nonpositive and
 * nonfinite-like comparison failures disable the optional override. */
static inline int SG_InterposeMode(float dose)
{
	if (!(dose > 0.0f))
		return 0;
	if (dose >= 3.0f)
		return 3;
	if (dose >= 2.0f)
		return 2;
	return 1;
}

static inline int SG_InterposeFallbackSeed(int mode, int carrier_seed,
	int midpoint_seed)
{
	if (carrier_seed < 0 || mode < 1 || mode > 3)
		return -1;
	if (mode == 1 && midpoint_seed >= 0)
		return midpoint_seed;
	return carrier_seed;
}

/* Coordinator duty is known before objective selection.  Only effective
 * ESCORT replaces the organic role here; the coordinator applies every other
 * concrete duty route after the ordinary objective stage. */
static inline int SG_ObjectiveRole(int organic_role, int escort_mission)
{
	return escort_mission ? SG_ROLE_ESCORT : organic_role;
}

/* Item attraction is optional route shaping.  A live push or an exact
 * coordinator mission owns the route outright; neither may be bent by the
 * generic shopping surface.  The established healthy-carrier rule remains
 * the third independent suppression. */
static inline int SG_CarrierEscapeActive(int role)
{
	return role == SG_ROLE_CARRY;
}

static inline int SG_CarrierJinkThreat(int client, int seed, int seed_count,
	int heard_only, int recent, float distance)
{
	if ((heard_only != 0 && heard_only != 1) ||
	    (recent != 0 && recent != 1))
		return 0;
	return client >= 0 && seed >= 0 && seed < seed_count && !heard_only &&
	    recent && distance >= 0.0f && distance < 700.0f;
}

static inline int SG_CarrierJinkAllowed(int terminal,
	int flag_touch_terminal)
{
	return !terminal && !flag_touch_terminal;
}

static inline int SG_OptionalItemDetourAllowed(int push,
	int strike_blocks_optional, int role, int health)
{
	if (push || strike_blocks_optional)
		return 0;
	if (SG_CarrierEscapeActive(role) && health > 60)
		return 0;
	return 1;
}

/* Home-field cost falls toward the capture stand.  The one autonomous escort
 * screens the fresh threat that admitted interposition: lead against a defender
 * ahead of the carrier, trail against a chaser behind it.  An unusable threat
 * cost fails toward the lead, where retained evidence places most carrier
 * deaths, rather than deriving tactics from client-slot parity. */
static inline int SG_InterposeLeadStation(int carrier_home_ms,
	int threat_home_ms)
{
	if (carrier_home_ms < 0 || carrier_home_ms >= SG_FIELD_INF ||
	    threat_home_ms < 0 || threat_home_ms >= SG_FIELD_INF)
		return 1;
	return threat_home_ms <= carrier_home_ms;
}

/* Once our team has the enemy flag, one separately assigned ESCORT owns the
 * carrier field. Ordinary attackers keep pressure on the enemy stand instead
 * of turning into duplicate escorts or following an unseen-flag fallback
 * home. */
static inline int SG_AttackObjectiveUsesFixedStand(int own_carrier_client)
{
	return own_carrier_client >= 0;
}

/* A carrier-belief row names both a side and one exact objective.  Client
 * slots are reusable and a current occupant carrying some other flag must not
 * keep an old escort/intercept field alive.  The caller supplies exact_flag
 * only after comparing ClientHasFlag(holder) with the row's expected entity. */
static inline int SG_CarrierBeliefIdentityCurrent(int believing_team,
	int holder_team, int own_carrier, int exact_flag)
{
	int expected_holder;

	if ((believing_team != CTF_TEAM_RED &&
	     believing_team != CTF_TEAM_BLUE) ||
	    (holder_team != CTF_TEAM_RED && holder_team != CTF_TEAM_BLUE) ||
	    (own_carrier != 0 && own_carrier != 1) ||
	    (exact_flag != 0 && exact_flag != 1))
		return 0;
	expected_holder = own_carrier ? believing_team
	                              : (believing_team == CTF_TEAM_RED
	                                     ? CTF_TEAM_BLUE : CTF_TEAM_RED);
	return holder_team == expected_holder && exact_flag;
}

/* Carrier position is sighting-derived. When it is live, route cost—not an
 * omniscient read of the carrier edict—selects the useful escort. When it is
 * unknown, the capture stand is the honest rendezvous. */
static inline int SG_EscortRouteCost(int carrier_field_valid,
	int carrier_cost, int home_cost)
{
	int cost = carrier_field_valid ? carrier_cost : home_cost;

	return cost >= 0 && cost < SG_FIELD_INF ? cost : -1;
}

static inline int SG_EscortAssignmentScore(int route_cost, int incumbent)
{
	if (route_cost < 0)
		return -1;
	/* Shift every valid score up instead of clamping a near-carrier
	 * incumbent to zero. The ordering keeps the full 300 ms hysteresis. */
	return route_cost + (incumbent ? 0 : 300);
}

/* Autonomous escort selection must not nominate a teammate whose live human
 * order will win when that teammate evaluates its own role.  Such a phantom
 * winner suppresses every real escort because all bots share the same argmin. */
static inline int SG_AutonomousEscortCandidate(int live, int outside_defense,
	int is_carrier, int ordered_role, int route_cost)
{
	if ((live != 0 && live != 1) ||
	    (outside_defense != 0 && outside_defense != 1) ||
	    (is_carrier != 0 && is_carrier != 1))
		return 0;
	return live && outside_defense && !is_carrier && ordered_role < 0 &&
	       route_cost >= 0;
}

#endif /* SG_ROLE_POLICY_H */
