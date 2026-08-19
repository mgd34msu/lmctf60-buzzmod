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

/* Near-goal defenders and escorts are intentionally stationed.  A human
 * escort's exact terminal hold remains authoritative even when its fallback
 * graph cost is not near the ordered teammate. */
static inline int SG_RoleMissionHold(int role, int goal_cost,
	int ordered_escort_terminal)
{
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

/* Route-failure clocks may judge only navigation-owned motion.  Mission holds
 * and combat-owned frames cannot prove that the selected graph link failed. */
static inline int SG_RouteFailureWatchSuppressed(int role, int goal_cost,
	int ordered_escort_terminal, int duel, int engaged_last)
{
	return SG_RoleMissionHold(role, goal_cost, ordered_escort_terminal) ||
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

/* Coordinator duty is known before objective selection.  Only effective
 * ESCORT replaces the organic role here; the coordinator applies every other
 * concrete duty route after the ordinary objective stage. */
static inline int SG_ObjectiveRole(int organic_role, int escort_mission)
{
	return escort_mission ? SG_ROLE_ESCORT : organic_role;
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
	if (incumbent && route_cost < 300)
		return 0;
	return route_cost - (incumbent ? 300 : 0);
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
