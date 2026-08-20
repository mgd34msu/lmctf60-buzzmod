/* Deterministic, host-free team offense coordinator. */
#include "sg_strike.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define SG_STRIKE_EVENT_MASK (SG_STRIKE_EVENT_PICKUP | \
	SG_STRIKE_EVENT_CARRIER_LOSS | SG_STRIKE_EVENT_FLAG_RETURN | \
	SG_STRIKE_EVENT_CAPTURE | SG_STRIKE_EVENT_LEVEL_RESET)

static int Strike_SlotValid(int slot)
{
	return slot >= 0 && slot < SG_STRIKE_MAX_SLOTS;
}

int SG_StrikeDutyEnemyPressure(sg_strike_duty_t duty)
{
	return duty == SG_STRIKE_DUTY_BREACH ||
	    duty == SG_STRIKE_DUTY_CLEAR || duty == SG_STRIKE_DUTY_PRESS;
}

int SG_StrikeEnemyPressureActive(int ordinary_attack, int strike_active,
	sg_strike_duty_t duty)
{
	return strike_active ? SG_StrikeDutyEnemyPressure(duty)
	                     : ordinary_attack;
}

int SG_StrikeDutyCombatPursuit(sg_strike_duty_t duty)
{
	/* BREACH owns the first physical flag entry.  It still fights every
	 * currently visible enemy through SG_CombatFrame, but a contact that has
	 * already broken sight may not replace its objective route with the
	 * bounded corner camp.  CLEAR and PRESS are the pressure bodies whose
	 * mission actually includes chasing that defender out of the room. */
	return duty == SG_STRIKE_DUTY_CLEAR ||
	    duty == SG_STRIKE_DUTY_PRESS || duty == SG_STRIKE_DUTY_RECOVER;
}

int SG_StrikeCombatPursuitActive(int ordinary_pursuit, int strike_active,
	sg_strike_duty_t duty)
{
	return strike_active ? SG_StrikeDutyCombatPursuit(duty)
	                     : ordinary_pursuit;
}

static int Strike_ThresholdHoldPriority(sg_strike_duty_t duty)
{
	switch (duty)
	{
	case SG_STRIKE_DUTY_CLEAR:
		return 3;
	case SG_STRIKE_DUTY_PRESS:
		return 2;
	case SG_STRIKE_DUTY_BREACH:
	case SG_STRIKE_DUTY_NONE:
		return 1;
	case SG_STRIKE_DUTY_ESCORT:
	case SG_STRIKE_DUTY_RECOVER:
	case SG_STRIKE_DUTY_CARRY:
	default:
		return 0;
	}
}

int SG_StrikeThresholdMateOwnsHold(sg_strike_duty_t self_duty,
	int self_entity, sg_strike_duty_t mate_duty, int mate_entity)
{
	int self_priority = Strike_ThresholdHoldPriority(self_duty);
	int mate_priority = Strike_ThresholdHoldPriority(mate_duty);

	if (self_entity <= 0 || mate_entity <= 0 || mate_entity == self_entity ||
	    self_priority <= 0 || mate_priority <= 0)
		return 0;
	return mate_priority > self_priority ||
	    (mate_priority == self_priority && mate_entity < self_entity);
}

int SG_StrikeDutyRearguard(sg_strike_duty_t duty)
{
	return SG_StrikeDutyEnemyPressure(duty) ||
	    duty == SG_STRIKE_DUTY_ESCORT;
}

int SG_StrikeRearguardActive(int ordinary_rearguard, int strike_active,
	sg_strike_duty_t duty)
{
	return strike_active ? SG_StrikeDutyRearguard(duty)
	                     : ordinary_rearguard;
}

int SG_StrikeEscortActive(int ordinary_escort, int strike_active,
	sg_strike_duty_t duty)
{
	return strike_active ? duty == SG_STRIKE_DUTY_ESCORT
	                     : ordinary_escort;
}

int SG_StrikeDutyRetiresOptionalErrand(sg_strike_duty_t duty)
{
	switch (duty)
	{
	case SG_STRIKE_DUTY_BREACH:
	case SG_STRIKE_DUTY_CLEAR:
	case SG_STRIKE_DUTY_PRESS:
	case SG_STRIKE_DUTY_ESCORT:
	case SG_STRIKE_DUTY_RECOVER:
	case SG_STRIKE_DUTY_CARRY:
		return 1;
	case SG_STRIKE_DUTY_NONE:
	default:
		return 0;
	}
}

int SG_StrikePrebreachApproachAllowed(int strike_active,
	int strike_pressure, int organic_attack, int goal_ms)
{
	if ((strike_active != 0 && strike_active != 1) ||
	    (strike_pressure != 0 && strike_pressure != 1) ||
	    (organic_attack != 0 && organic_attack != 1) ||
	    goal_ms <= 2000 || goal_ms >= 5000)
		return 0;
	/* A concrete pressure duty may override organic RECOVER/ESCORT.  The
	 * inverse matters too: a concrete recovery/escort duty suppresses the
	 * obsolete organic ATTACK premise. */
	return strike_active ? strike_pressure : organic_attack;
}

float SG_StrikeFlagTouchThrottle(int touch_authorized, float distance,
	float speed, float alignment)
{
	if ((touch_authorized != 0 && touch_authorized != 1) ||
	    !touch_authorized || !isfinite(distance) || !isfinite(speed) ||
	    !isfinite(alignment) || distance <= 1.0f || distance >= 220.0f ||
	    speed <= 120.0f)
		return 1.0f;
	if (alignment < 0.50f)
		return 0.30f;
	if (alignment < 0.85f)
		return 0.55f;
	return 1.0f;
}

int SG_StrikeCarrierOwnFlagAimAllowed(int flag_available, int flag_at_home,
	int direct_touch)
{
	if ((flag_available != 0 && flag_available != 1) ||
	    (flag_at_home != 0 && flag_at_home != 1) ||
	    (direct_touch != 0 && direct_touch != 1))
		return 0;
	return flag_available && (flag_at_home || direct_touch);
}

float SG_StrikeFlagApproachPrice(int flag_available, int touch_authorized,
	int run_link,
	float current_distance, float candidate_distance, float vertical_delta,
	int current_goal_ms, int candidate_goal_ms)
{
	float progress, price;

	if ((flag_available != 0 && flag_available != 1) ||
	    (touch_authorized != 0 && touch_authorized != 1) ||
	    !flag_available || touch_authorized || !run_link ||
	    !isfinite(current_distance) ||
	    !isfinite(candidate_distance) || !isfinite(vertical_delta) ||
	    current_distance < 0.0f || current_distance > 600.0f ||
	    candidate_distance < 0.0f || fabsf(vertical_delta) > 96.0f ||
	    current_goal_ms < 0 || candidate_goal_ms < 0 ||
	    candidate_goal_ms > current_goal_ms + 125)
		return 0.0f;
	progress = current_distance - candidate_distance;
	if (!isfinite(progress) || progress < 16.0f)
		return 0.0f;
	price = progress * 0.5f;
	if (price > 100.0f)
		price = 100.0f;
	return -price;
}

static uint32_t Strike_Bit(int slot)
{
	return (uint32_t)1u << (unsigned)slot;
}

static int Strike_Count(uint32_t mask)
{
	int count = 0;

	while (mask)
	{
		count += (int)(mask & 1u);
		mask >>= 1;
	}
	return count;
}

static int Strike_FrameValid(const sg_strike_frame_t *frame)
{
	int slot;

	if (!frame || !isfinite(frame->now) || frame->now < 0.0f ||
	    (frame->events & ~SG_STRIKE_EVENT_MASK) != 0u ||
	    (frame->enemy_flag_carried != 0 &&
	     frame->enemy_flag_carried != 1) ||
	    (frame->carrier_slot != -1 && !Strike_SlotValid(frame->carrier_slot)))
		return 0;
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
	{
		const sg_strike_slot_input_t *input = &frame->slot[slot];

		if (input->weapon_tier < 0 ||
		    input->enemy_flag_goal_ms < -1 ||
		    input->recover_goal_ms < -1 ||
		    input->carrier_goal_ms < -1)
			return 0;
	}
	return 1;
}

void SG_StrikeReset(sg_strike_team_t *team)
{
	if (!team)
		return;
	memset(team, 0, sizeof(*team));
	team->phase = SG_STRIKE_IDLE;
	team->form_deadline = -1.0f;
	team->clear_until = -1.0f;
	team->carrier_slot = -1;
}

static int Strike_InputViable(const sg_strike_frame_t *frame, int slot)
{
	const sg_strike_slot_input_t *input = &frame->slot[slot];

	if (!input->present || !input->alive || !input->attack_eligible ||
	    input->life_id == 0u)
		return 0;
	return input->enemy_flag_goal_ms >= 0 ||
	    (!frame->own_flag_home && input->recover_goal_ms >= 0) ||
	    ((frame->enemy_flag_carried || frame->carrier_slot >= 0) &&
	     input->carrier_goal_ms >= 0) ||
	    input->direct_flag_touch || input->carrying ||
	    slot == frame->carrier_slot;
}

static int Strike_EnemyPressureViable(
	const sg_strike_slot_input_t *input)
{
	return input &&
	    (input->enemy_flag_goal_ms >= 0 || input->direct_flag_touch);
}

static int Strike_GoalForSelection(const sg_strike_frame_t *frame,
	const sg_strike_slot_input_t *input)
{
	int goal = input->enemy_flag_goal_ms;

	if (input->direct_flag_touch)
		return -2;
	if (input->carrying)
		return -1;
	if (!frame->own_flag_home && input->recover_goal_ms >= 0 &&
	    (goal < 0 || input->recover_goal_ms < goal))
		goal = input->recover_goal_ms;
	if ((frame->enemy_flag_carried || frame->carrier_slot >= 0) &&
	    input->carrier_goal_ms >= 0 &&
	    (goal < 0 || input->carrier_goal_ms < goal))
		goal = input->carrier_goal_ms;
	return goal;
}

static void Strike_AddMember(sg_strike_team_t *team,
	const sg_strike_frame_t *frame, int slot)
{
	uint32_t bit = Strike_Bit(slot);

	team->member_mask |= bit;
	if (team->member_life[slot] != frame->slot[slot].life_id)
	{
		team->member_life[slot] = frame->slot[slot].life_id;
		team->weapon_deadline[slot] =
		    frame->now + SG_STRIKE_WEAPON_DEADLINE_SECONDS;
	}
	team->duty[slot] = SG_STRIKE_DUTY_NONE;
}

static void Strike_RemoveMember(sg_strike_team_t *team, int slot)
{
	uint32_t bit = Strike_Bit(slot);

	team->member_mask &= ~bit;
	team->weapon_ready_mask &= ~bit;
	team->mission_ready_mask &= ~bit;
	team->hold_mask &= ~bit;
	team->rush_mask &= ~bit;
	team->arrived_mask &= ~bit;
	team->attempt_mask &= ~bit;
	team->duty[slot] = SG_STRIKE_DUTY_NONE;
}

static void Strike_ReconcileMembers(sg_strike_team_t *team,
	const sg_strike_frame_t *frame)
{
	int slot;

	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
	{
		uint32_t bit = Strike_Bit(slot);

		if ((team->member_mask & bit) == 0u)
			continue;
		if (!Strike_InputViable(frame, slot))
		{
			Strike_RemoveMember(team, slot);
			continue;
		}
		if (team->member_life[slot] != frame->slot[slot].life_id)
		{
			team->member_life[slot] = frame->slot[slot].life_id;
			team->weapon_deadline[slot] =
			    frame->now + SG_STRIKE_WEAPON_DEADLINE_SECONDS;
			team->weapon_ready_mask &= ~bit;
			team->mission_ready_mask &= ~bit;
			team->arrived_mask &= ~bit;
			team->attempt_mask &= ~bit;
		}
	}

	while (Strike_Count(team->member_mask) < SG_STRIKE_MAX_MEMBERS)
	{
		int best = -1;
		int best_goal = 0;

		for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		{
			uint32_t bit = Strike_Bit(slot);
			int goal;

			if ((team->member_mask & bit) != 0u ||
			    !Strike_InputViable(frame, slot))
				continue;
			goal = Strike_GoalForSelection(frame, &frame->slot[slot]);
			if (best < 0 || goal < best_goal ||
			    (goal == best_goal && slot < best))
			{
				best = slot;
				best_goal = goal;
			}
		}
		if (best < 0)
			break;
		Strike_AddMember(team, frame, best);
	}

	/* Stable membership is subordinate to the only mission that can make a
	 * capture legal. If the roster was already full when our flag left home,
	 * admit the best finite recoverer exactly when nobody retained can recover.
	 * Never displace the actual carrier. */
	if (!frame->own_flag_home)
	{
		int have_recover = 0;
		int candidate = -1;
		int evict = -1;
		int worst_goal = 0;

		for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
			if ((team->member_mask & Strike_Bit(slot)) != 0u &&
			    slot != frame->carrier_slot &&
			    !frame->slot[slot].carrying &&
			    frame->slot[slot].recover_goal_ms >= 0)
			{
				have_recover = 1;
				break;
			}
		if (!have_recover)
		{
			for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
				if ((team->member_mask & Strike_Bit(slot)) == 0u &&
				    slot != frame->carrier_slot &&
				    !frame->slot[slot].carrying &&
				    Strike_InputViable(frame, slot) &&
				    frame->slot[slot].recover_goal_ms >= 0 &&
				    (candidate < 0 ||
				     frame->slot[slot].recover_goal_ms <
				         frame->slot[candidate].recover_goal_ms ||
				     (frame->slot[slot].recover_goal_ms ==
				          frame->slot[candidate].recover_goal_ms &&
				      slot < candidate)))
					candidate = slot;
			if (candidate >= 0)
			{
				for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
				{
					int goal;

					if ((team->member_mask & Strike_Bit(slot)) == 0u ||
					    slot == frame->carrier_slot ||
					    frame->slot[slot].carrying)
						continue;
					goal = Strike_GoalForSelection(frame,
					    &frame->slot[slot]);
					if (evict < 0 || goal > worst_goal ||
					    (goal == worst_goal && slot > evict))
					{
						evict = slot;
						worst_goal = goal;
					}
				}
				if (evict >= 0)
				{
					Strike_RemoveMember(team, evict);
					Strike_AddMember(team, frame, candidate);
				}
			}
		}
	}

	if (frame->enemy_flag_carried || frame->carrier_slot >= 0)
	{
		int have_escort = 0;
		int recovery_count = 0;
		int candidate = -1;
		int evict = -1;
		int worst_goal = 0;

		for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		{
			if ((team->member_mask & Strike_Bit(slot)) == 0u)
				continue;
			if (!frame->own_flag_home &&
			    slot != frame->carrier_slot &&
			    !frame->slot[slot].carrying &&
			    frame->slot[slot].recover_goal_ms >= 0)
				recovery_count++;
			if (slot != frame->carrier_slot && !frame->slot[slot].carrying &&
			    frame->slot[slot].carrier_goal_ms >= 0)
				have_escort = 1;
		}
		if (!have_escort)
		{
			for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
				if ((team->member_mask & Strike_Bit(slot)) == 0u &&
				    slot != frame->carrier_slot &&
				    !frame->slot[slot].carrying &&
				    Strike_InputViable(frame, slot) &&
				    frame->slot[slot].carrier_goal_ms >= 0 &&
				    (candidate < 0 ||
				     frame->slot[slot].carrier_goal_ms <
				         frame->slot[candidate].carrier_goal_ms ||
				     (frame->slot[slot].carrier_goal_ms ==
				          frame->slot[candidate].carrier_goal_ms &&
				      slot < candidate)))
					candidate = slot;
			if (candidate >= 0)
			{
				for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
				{
					int goal;

					if ((team->member_mask & Strike_Bit(slot)) == 0u ||
					    slot == frame->carrier_slot ||
					    frame->slot[slot].carrying ||
					    (!frame->own_flag_home && recovery_count <= 1 &&
					     frame->slot[slot].recover_goal_ms >= 0))
						continue;
					goal = Strike_GoalForSelection(frame,
					    &frame->slot[slot]);
					if (evict < 0 || goal > worst_goal ||
					    (goal == worst_goal && slot > evict))
					{
						evict = slot;
						worst_goal = goal;
					}
				}
				if (evict >= 0)
				{
					Strike_RemoveMember(team, evict);
					Strike_AddMember(team, frame, candidate);
				}
			}
		}
	}
}

static void Strike_RefreshReadiness(sg_strike_team_t *team,
	const sg_strike_frame_t *frame)
{
	int slot;

	team->weapon_ready_mask = 0u;
	team->mission_ready_mask = 0u;
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
	{
		uint32_t bit = Strike_Bit(slot);

		if ((team->member_mask & bit) == 0u)
			continue;
		if (frame->slot[slot].weapon_tier >=
		    SG_STRIKE_USABLE_WEAPON_TIER)
			team->weapon_ready_mask |= bit;
		if ((team->weapon_ready_mask & bit) != 0u ||
		    frame->now >= team->weapon_deadline[slot])
			team->mission_ready_mask |= bit;
	}
}

static int Strike_LowestCost(const sg_strike_frame_t *frame, uint32_t mask,
	int cost_kind)
{
	int best = -1;
	int best_cost = 0;
	int slot;

	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
	{
		int cost;

		if ((mask & Strike_Bit(slot)) == 0u)
			continue;
		if (cost_kind == 1)
			cost = frame->slot[slot].recover_goal_ms;
		else if (cost_kind == 2)
			cost = frame->slot[slot].carrier_goal_ms;
		else
			cost = frame->slot[slot].enemy_flag_goal_ms;
		if (cost < 0)
			continue;
		if (best < 0 || cost < best_cost ||
		    (cost == best_cost && slot < best))
		{
			best = slot;
			best_cost = cost;
		}
	}
	return best;
}

static void Strike_ClearDuties(sg_strike_team_t *team)
{
	int slot;

	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		team->duty[slot] = SG_STRIKE_DUTY_NONE;
}

static void Strike_AssignAttackDuties(sg_strike_team_t *team,
	const sg_strike_frame_t *frame)
{
	sg_strike_duty_t old[SG_STRIKE_MAX_SLOTS];
	uint32_t attack_mask = team->member_mask;
	int recover = -1;
	int breach;
	int clear;
	int slot;

	memcpy(old, team->duty, sizeof(old));
	Strike_ClearDuties(team);
	/* SG_Role assigns every non-watchman to RECOVER while our flag is away.
	 * The strike overlay may keep pressure with the remaining bodies, but it
	 * must never erase recovery entirely merely because deaths left fewer than
	 * three members. One live member recovers; two leave one attacker; larger
	 * squads still assign exactly one recoverer. */
	if (!frame->own_flag_home && Strike_Count(attack_mask) >= 1)
	{
		for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
			if ((attack_mask & Strike_Bit(slot)) != 0u &&
			    old[slot] == SG_STRIKE_DUTY_RECOVER &&
			    frame->slot[slot].recover_goal_ms >= 0)
			{
				recover = slot;
				break;
			}
		if (recover < 0)
			recover = Strike_LowestCost(frame, attack_mask, 1);
		if (recover >= 0)
		{
			team->duty[recover] = SG_STRIKE_DUTY_RECOVER;
			attack_mask &= ~Strike_Bit(recover);
		}
	}
	/* Membership is deliberately broader than attack reachability: a body may
	 * be retained because it owns the only recovery or carrier route.  Do not
	 * turn that useful mission into PRESS on an infinite enemy-flag field.
	 * Direct physical touch remains sufficient even without a graph cost. */
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		if ((attack_mask & Strike_Bit(slot)) != 0u &&
		    !Strike_EnemyPressureViable(&frame->slot[slot]))
			attack_mask &= ~Strike_Bit(slot);

	breach = -1;
	clear = -1;
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		if ((attack_mask & Strike_Bit(slot)) != 0u &&
		    old[slot] == SG_STRIKE_DUTY_BREACH)
		{
			breach = slot;
			break;
		}
	if (breach < 0)
		breach = Strike_LowestCost(frame, attack_mask, 0);
	if (breach >= 0)
	{
		team->duty[breach] = SG_STRIKE_DUTY_BREACH;
		attack_mask &= ~Strike_Bit(breach);
	}
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		if ((attack_mask & Strike_Bit(slot)) != 0u &&
		    old[slot] == SG_STRIKE_DUTY_CLEAR)
		{
			clear = slot;
			break;
		}
	if (clear < 0)
		clear = Strike_LowestCost(frame, attack_mask, 0);
	if (clear >= 0)
	{
		team->duty[clear] = SG_STRIKE_DUTY_CLEAR;
		attack_mask &= ~Strike_Bit(clear);
	}
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		if ((attack_mask & Strike_Bit(slot)) != 0u)
			team->duty[slot] = SG_STRIKE_DUTY_PRESS;
}

static uint32_t Strike_AttackMask(const sg_strike_team_t *team)
{
	uint32_t mask = 0u;
	int slot;

	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		if (team->duty[slot] == SG_STRIKE_DUTY_BREACH ||
		    team->duty[slot] == SG_STRIKE_DUTY_CLEAR ||
		    team->duty[slot] == SG_STRIKE_DUTY_PRESS)
			mask |= Strike_Bit(slot);
	return mask;
}

static void Strike_BeginEpoch(sg_strike_team_t *team, float now)
{
	team->epoch++;
	if (team->epoch == 0u)
		team->epoch = 1u;
	team->phase = SG_STRIKE_ARM;
	team->phase_since = now;
	team->form_deadline = -1.0f;
	team->go_since = 0.0f;
	team->clear_until = -1.0f;
	team->carrier_slot = -1;
	team->hold_mask = 0u;
	team->rush_mask = 0u;
	team->arrived_mask = 0u;
	team->attempt_mask = 0u;
	Strike_ClearDuties(team);
}

static void Strike_EnterGo(sg_strike_team_t *team, float now)
{
	if (team->phase != SG_STRIKE_GO)
	{
		team->phase = SG_STRIKE_GO;
		team->phase_since = now;
		team->go_since = now;
	}
	team->hold_mask = 0u;
	team->rush_mask = Strike_AttackMask(team);
}

static int Strike_Synchronized(const sg_strike_team_t *team,
	const sg_strike_frame_t *frame, uint32_t attack_mask)
{
	uint32_t ready = attack_mask & team->mission_ready_mask;
	int first;
	int second;

	for (first = 0; first < SG_STRIKE_MAX_SLOTS; first++)
	{
		int first_cost;

		if ((ready & Strike_Bit(first)) == 0u)
			continue;
		first_cost = frame->slot[first].enemy_flag_goal_ms;
		if (first_cost < 0)
			continue;
		for (second = first + 1; second < SG_STRIKE_MAX_SLOTS; second++)
		{
			int second_cost;
			int spread;

			if ((ready & Strike_Bit(second)) == 0u)
				continue;
			second_cost = frame->slot[second].enemy_flag_goal_ms;
			if (second_cost < 0 ||
			    (first_cost > SG_STRIKE_LEADER_WINDOW_MS &&
			     second_cost > SG_STRIKE_LEADER_WINDOW_MS))
				continue;
			spread = first_cost - second_cost;
			if (spread < 0)
				spread = -spread;
			if (spread <= SG_STRIKE_SYNC_SPREAD_MS)
				return 1;
		}
	}
	return 0;
}

static int Strike_ReadyLeader(const sg_strike_team_t *team,
	const sg_strike_frame_t *frame, uint32_t attack_mask)
{
	uint32_t ready = attack_mask & team->mission_ready_mask;
	int leader = Strike_LowestCost(frame, ready, 0);

	if (leader < 0 ||
	    frame->slot[leader].enemy_flag_goal_ms > SG_STRIKE_LEADER_WINDOW_MS)
		return -1;
	return leader;
}

static int Strike_FormPartnerReachable(const sg_strike_team_t *team,
	const sg_strike_frame_t *frame, uint32_t attack_mask, int leader)
{
	uint32_t ready = attack_mask & team->mission_ready_mask;
	int arrival_budget_ms = (int)(SG_STRIKE_FORM_CAP_SECONDS * 1000.0f);
	int leader_cost;
	int slot;

	if (!Strike_SlotValid(leader) || (ready & Strike_Bit(leader)) == 0u)
		return 0;
	if (team->form_deadline >= 0.0f)
	{
		float remaining = (team->form_deadline - frame->now) * 1000.0f;

		if (!isfinite(remaining) || remaining <= 0.0f)
			return 0;
		if (remaining < (float)arrival_budget_ms)
			arrival_budget_ms = (int)remaining;
	}
	leader_cost = frame->slot[leader].enemy_flag_goal_ms;
	if (leader_cost < 0)
		return 0;
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
	{
		int partner_cost;

		if (slot == leader || (ready & Strike_Bit(slot)) == 0u)
			continue;
		partner_cost = frame->slot[slot].enemy_flag_goal_ms;
		if (partner_cost >= 0 &&
		    partner_cost <= leader_cost +
		        arrival_budget_ms +
		        SG_STRIKE_SYNC_SPREAD_MS)
			return 1;
	}
	return 0;
}

static void Strike_AdvanceAttack(sg_strike_team_t *team,
	const sg_strike_frame_t *frame)
{
	uint32_t attack_mask;
	uint32_t direct_mask = 0u;
	int leader;
	int slot;

	Strike_AssignAttackDuties(team, frame);
	attack_mask = Strike_AttackMask(team);
	team->hold_mask = 0u;
	team->rush_mask = attack_mask & team->mission_ready_mask;
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		if ((attack_mask & Strike_Bit(slot)) != 0u &&
		    frame->slot[slot].direct_flag_touch)
			direct_mask |= Strike_Bit(slot);

	if (attack_mask == 0u)
	{
		/* A lone recoverer is an active mission, not an empty strike epoch.
		 * Keeping ARM avoids clearing/restarting the epoch on every frame while
		 * its own-flag route remains authoritative. */
		team->phase = SG_STRIKE_ARM;
		team->phase_since = frame->now;
		return;
	}
	if (direct_mask != 0u || frame->recent_enemy_room_death)
	{
		Strike_EnterGo(team, frame->now);
		return;
	}
	if (team->phase == SG_STRIKE_GO)
	{
		team->rush_mask = attack_mask;
		return;
	}
	if (Strike_Count(attack_mask) == 1)
	{
		team->hold_mask = 0u;
		if ((attack_mask & team->mission_ready_mask) != 0u)
			Strike_EnterGo(team, frame->now);
		else
			team->phase = SG_STRIKE_ARM;
		return;
	}
	if (Strike_Synchronized(team, frame, attack_mask))
	{
		Strike_EnterGo(team, frame->now);
		return;
	}

	leader = Strike_ReadyLeader(team, frame, attack_mask);
	if (leader >= 0)
	{
		/* Holding is useful only while a ready partner can still reach the
		 * synchronization band before the bounded form clock expires.  Recompute
		 * against the remaining clock every frame: a partner that stalls or takes
		 * a longer route releases the leader immediately. */
		if (!Strike_FormPartnerReachable(team, frame, attack_mask, leader))
		{
			Strike_EnterGo(team, frame->now);
			return;
		}
		if (team->form_deadline < 0.0f)
		{
			team->form_deadline =
			    frame->now + SG_STRIKE_FORM_CAP_SECONDS;
			team->phase_since = frame->now;
		}
		team->phase = SG_STRIKE_FORM;
		team->hold_mask = Strike_Bit(leader);
		team->rush_mask &= ~team->hold_mask;
	}
	else if (team->phase != SG_STRIKE_FORM)
		team->phase = SG_STRIKE_ARM;

	if (team->phase == SG_STRIKE_FORM && team->form_deadline >= 0.0f &&
	    frame->now >= team->form_deadline)
		Strike_EnterGo(team, frame->now);
}

static void Strike_AssignEgress(sg_strike_team_t *team,
	const sg_strike_frame_t *frame, int entering)
{
	sg_strike_duty_t old[SG_STRIKE_MAX_SLOTS];
	uint32_t available = team->member_mask;
	int carrier = frame->carrier_slot;
	int carrier_live;
	int clear = -1;
	int escort = -1;
	int recover = -1;
	int slot;

	memcpy(old, team->duty, sizeof(old));
	/* carrier_slot comes from the live flag owner.  Keep the public reducer
	 * fail-closed too: a stale/dead/non-carrying slot is not an egress owner. */
	if (!Strike_SlotValid(carrier) || !frame->slot[carrier].present ||
	    !frame->slot[carrier].alive || !frame->slot[carrier].carrying)
		carrier = -1;
	carrier_live = carrier >= 0 || frame->enemy_flag_carried;
	if (entering)
	{
		team->phase = SG_STRIKE_EGRESS;
		team->phase_since = frame->now;
		if (carrier_live)
			team->clear_until = frame->now + SG_STRIKE_CLEAR_SECONDS;
	}
	team->carrier_slot = carrier;
	team->hold_mask = 0u;
	team->rush_mask = 0u;
	Strike_ClearDuties(team);

	if (Strike_SlotValid(carrier))
	{
		/* An opportunistic fifth attacker may take the flag while the stable
		 * four-person strike roster is full.  Give the real carrier home-field
		 * authority without changing member_mask or displacing clear/escort. */
		team->duty[carrier] = SG_STRIKE_DUTY_CARRY;
		available &= ~Strike_Bit(carrier);
	}

	if (carrier_live)
	{
		/* In a standoff the carrier cannot score until our flag returns, so
		 * recovery owns the first scarce body.  Normal egress keeps the original
		 * short clear-before-escort order; standoff egress orders the essential
		 * jobs RECOVER, ESCORT, then CLEAR. */
		if (!frame->own_flag_home)
		{
			uint32_t recovery_pool = available;

			for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
				if ((available & Strike_Bit(slot)) != 0u &&
				    old[slot] == SG_STRIKE_DUTY_RECOVER &&
				    frame->slot[slot].recover_goal_ms >= 0)
				{
					recover = slot;
					break;
				}
			/* A dead recoverer does not require shuffling the surviving
			 * carrier screen.  Prefer any other reachable helper, then fall
			 * back to the escort only when it is the sole recovery route. */
			for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
				if (old[slot] == SG_STRIKE_DUTY_ESCORT)
					recovery_pool &= ~Strike_Bit(slot);
			if (recover < 0)
				recover = Strike_LowestCost(frame, recovery_pool, 1);
			if (recover < 0)
				recover = Strike_LowestCost(frame, available, 1);
			if (recover >= 0)
			{
				team->duty[recover] = SG_STRIKE_DUTY_RECOVER;
				available &= ~Strike_Bit(recover);
			}
		}
		/* One surviving helper cannot clear and escort.  Give that scarce
		 * body to the live carrier; CLEAR is useful only when another body
		 * remains to own the screen. */
		if (frame->own_flag_home && frame->now < team->clear_until &&
		    (available & (available - 1u)) != 0u)
		{
			for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
				if ((available & Strike_Bit(slot)) != 0u &&
				    old[slot] == SG_STRIKE_DUTY_CLEAR)
				{
					clear = slot;
					break;
				}
			if (clear < 0)
				clear = Strike_LowestCost(frame, available, 0);
			if (clear >= 0)
			{
				team->duty[clear] = SG_STRIKE_DUTY_CLEAR;
				available &= ~Strike_Bit(clear);
			}
		}
			escort = Strike_LowestCost(frame, available, 2);
		if (escort >= 0)
		{
			team->duty[escort] = SG_STRIKE_DUTY_ESCORT;
			available &= ~Strike_Bit(escort);
		}
		if (!frame->own_flag_home && frame->now < team->clear_until)
		{
			for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
				if ((available & Strike_Bit(slot)) != 0u &&
				    old[slot] == SG_STRIKE_DUTY_CLEAR)
				{
					clear = slot;
					break;
				}
			if (clear < 0)
				clear = Strike_LowestCost(frame, available, 0);
			if (clear >= 0)
			{
				team->duty[clear] = SG_STRIKE_DUTY_CLEAR;
				available &= ~Strike_Bit(clear);
			}
		}
		for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
			if ((available & Strike_Bit(slot)) != 0u &&
			    Strike_EnemyPressureViable(&frame->slot[slot]))
				team->duty[slot] = SG_STRIKE_DUTY_PRESS;
		return;
	}

	/* A dead carrier leaves a live dropped objective. Re-form duties around
	 * the nearest surviving scooper without starting another timed wave. */
	Strike_AssignAttackDuties(team, frame);
	team->rush_mask = Strike_AttackMask(team);
}

int SG_StrikeStep(sg_strike_team_t *team, const sg_strike_frame_t *frame)
{
	sg_strike_team_t next;
	int close_epoch;
	int entering_egress;

	if (!team || !Strike_FrameValid(frame))
		return 0;
	if ((frame->events & SG_STRIKE_EVENT_LEVEL_RESET) != 0u)
	{
		SG_StrikeReset(team);
		return 1;
	}

	next = *team;
	Strike_ReconcileMembers(&next, frame);
	Strike_RefreshReadiness(&next, frame);
	if (next.member_mask == 0u)
	{
		next.phase = SG_STRIKE_IDLE;
		next.phase_since = frame->now;
		next.form_deadline = -1.0f;
		next.carrier_slot = -1;
		Strike_ClearDuties(&next);
		*team = next;
		return 1;
	}

	close_epoch = (frame->events & (SG_STRIKE_EVENT_FLAG_RETURN |
	    SG_STRIKE_EVENT_CAPTURE)) != 0u;
	if (!close_epoch && next.phase == SG_STRIKE_EGRESS &&
	    frame->enemy_flag_home && frame->carrier_slot < 0)
		close_epoch = 1;
	if (close_epoch || next.phase == SG_STRIKE_IDLE)
		Strike_BeginEpoch(&next, frame->now);

	entering_egress = next.phase != SG_STRIKE_EGRESS;
	if ((frame->events & SG_STRIKE_EVENT_PICKUP) != 0u ||
	    frame->carrier_slot >= 0 || frame->enemy_flag_carried)
	{
		Strike_AssignEgress(&next, frame, entering_egress);
		*team = next;
		return 1;
	}
	if ((frame->events & SG_STRIKE_EVENT_CARRIER_LOSS) != 0u ||
	    frame->enemy_flag_dropped || !frame->enemy_flag_home)
	{
		Strike_AssignEgress(&next, frame, entering_egress);
		*team = next;
		return 1;
	}

	Strike_AdvanceAttack(&next, frame);
	*team = next;
	return 1;
}

int SG_StrikeMember(const sg_strike_team_t *team, int slot)
{
	return team && Strike_SlotValid(slot) &&
	    (team->member_mask & Strike_Bit(slot)) != 0u;
}

int SG_StrikeParticipant(const sg_strike_team_t *team, int slot)
{
	if (!team || !Strike_SlotValid(slot))
		return 0;
	if (SG_StrikeMember(team, slot))
		return 1;
	return team->phase == SG_STRIKE_EGRESS && team->carrier_slot == slot &&
	    team->duty[slot] == SG_STRIKE_DUTY_CARRY;
}

int SG_StrikeCarrierScreened(const sg_strike_team_t *team)
{
	int slot;

	if (!team || team->phase != SG_STRIKE_EGRESS)
		return 0;
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		if (team->duty[slot] == SG_STRIKE_DUTY_ESCORT)
			return 1;
	return 0;
}

float SG_StrikeCarrierHookRisk(int carrier_screened)
{
	return carrier_screened == 1 ? 500.0f : 2000.0f;
}

int SG_StrikeMemberNeedsWeapon(const sg_strike_team_t *team, int slot,
	float now)
{
	uint32_t bit;
	sg_strike_duty_t duty;

	if (!team || !Strike_SlotValid(slot) || !isfinite(now))
		return 0;
	bit = Strike_Bit(slot);
	if ((team->member_mask & bit) == 0u ||
	    (team->weapon_ready_mask & bit) != 0u ||
	    now >= team->weapon_deadline[slot])
		return 0;
	duty = team->duty[slot];
	return duty == SG_STRIKE_DUTY_BREACH || duty == SG_STRIKE_DUTY_CLEAR ||
	    duty == SG_STRIKE_DUTY_PRESS || duty == SG_STRIKE_DUTY_NONE;
}

int SG_StrikeWeaponDetourAllowed(int needs_weapon, int strike_rush,
	int carrying, int combat_engaged, int direct_flag_touch,
	int enemy_flag_goal_ms, int weapon_goal_ms, int remaining_ms)
{
	if ((needs_weapon != 0 && needs_weapon != 1) ||
	    (strike_rush != 0 && strike_rush != 1) ||
	    (carrying != 0 && carrying != 1) ||
	    (combat_engaged != 0 && combat_engaged != 1) ||
	    (direct_flag_touch != 0 && direct_flag_touch != 1))
		return 0;
	if (!needs_weapon || strike_rush || carrying || combat_engaged ||
	    direct_flag_touch ||
	    enemy_flag_goal_ms <= SG_STRIKE_LEADER_WINDOW_MS ||
	    weapon_goal_ms < 0 || remaining_ms < 0 ||
	    weapon_goal_ms > remaining_ms)
		return 0;
	/* Requiring a full-second route saving prevents a technically reachable
	 * weapon from replacing a nearly equal objective route.  Subtraction is
	 * safe because the enemy goal was already proved greater than 5000. */
	return weapon_goal_ms <= enemy_flag_goal_ms - 1000;
}

int SG_StrikeMemberShouldHold(const sg_strike_team_t *team, int slot)
{
	return team && Strike_SlotValid(slot) &&
	    (team->hold_mask & Strike_Bit(slot)) != 0u;
}

int SG_StrikeMemberRushes(const sg_strike_team_t *team, int slot)
{
	return team && Strike_SlotValid(slot) &&
	    (team->rush_mask & Strike_Bit(slot)) != 0u;
}

sg_strike_weapon_route_verdict_t SG_StrikeWeaponRouteVerdict(
	int exact_route_owned, int weapon_authority,
	int physical_controller_active, int retirement_latched)
{
	if (!exact_route_owned)
		return SG_STRIKE_WEAPON_ROUTE_CLEAR;
	/* DRAIN is a one-way retirement edge.  A transient combat/direct-touch
	 * condition cannot later restore the weapon errand while the same physical
	 * controller is still running. */
	if (retirement_latched)
		return physical_controller_active ? SG_STRIKE_WEAPON_ROUTE_DRAIN
		                                  : SG_STRIKE_WEAPON_ROUTE_CLEAR;
	if (weapon_authority)
		return SG_STRIKE_WEAPON_ROUTE_OWN;
	if (physical_controller_active)
		return SG_STRIKE_WEAPON_ROUTE_DRAIN;
	return SG_STRIKE_WEAPON_ROUTE_CLEAR;
}

int SG_StrikeWeaponControllerPhysical(
	const sg_strike_weapon_controller_state_t *state)
{
	if (!state)
		return 0;
	/* These controllers can be layered over another link identity.  Phase two
	 * is already irreversible: hook fire succeeded, or the rocket-jump fire
	 * command may already have launched a rocket before phase three. */
	if (state->hook_phase >= 2 || state->rocketjump_phase >= 2)
		return 1;
	switch (state->action)
	{
	case RL_JUMP:
		return state->jump_started != 0;
	case RL_DROP:
		return state->drop_started != 0;
	case RL_SWIM:
		return state->swim_active != 0 || state->swim_validated != 0;
	case RL_TELEPORT:
		if (state->swim_active || state->swim_validated)
			return 1;
		/* fall through */
	case RL_LIFT:
	case RL_DOOR:
	case RL_BUTTON_DOOR:
		/* declared_started is deliberately absent: canonical-source staging
		 * and door guard acquisition precede the irreversible touch. */
		return state->declared_touched != 0 ||
		    state->declared_triggered != 0 ||
		    state->declared_activated != 0 ||
		    state->declared_guard_paused != 0;
	case RL_RUN:
	case RL_HOOK:
	case RL_ROCKETJUMP:
	default:
		return 0;
	}
}

sg_strike_weapon_door_retirement_t SG_StrikeWeaponDoorRetirement(
	int release_proved_clear, int recovery_expired, int hold_open_ready)
{
	if (release_proved_clear)
		return SG_STRIKE_WEAPON_DOOR_RELEASE;
	if (recovery_expired || !hold_open_ready)
		return SG_STRIKE_WEAPON_DOOR_TERMINAL;
	return SG_STRIKE_WEAPON_DOOR_HOLD;
}

int SG_StrikeGenericRailAllowed(int strike_active)
{
	return strike_active == 0;
}
