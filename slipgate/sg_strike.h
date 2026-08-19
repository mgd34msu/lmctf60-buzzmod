/* sg_strike.h -- deterministic team offense coordinator core. */
#ifndef SG_STRIKE_H
#define SG_STRIKE_H

#include <stdint.h>

#include "slipgate/sg_action_contract.generated.h"

#define SG_STRIKE_MAX_SLOTS 16
#define SG_STRIKE_MAX_MEMBERS 4

#define SG_STRIKE_USABLE_WEAPON_TIER 2
#define SG_STRIKE_WEAPON_DEADLINE_SECONDS 5.0f
#define SG_STRIKE_FORM_CAP_SECONDS 3.0f
#define SG_STRIKE_SYNC_SPREAD_MS 1500
#define SG_STRIKE_LEADER_WINDOW_MS 5000
#define SG_STRIKE_CLEAR_SECONDS 5.0f

typedef enum sg_strike_phase_e
{
	SG_STRIKE_IDLE = 0,
	SG_STRIKE_ARM,
	SG_STRIKE_FORM,
	SG_STRIKE_GO,
	SG_STRIKE_EGRESS
} sg_strike_phase_t;

typedef enum sg_strike_duty_e
{
	SG_STRIKE_DUTY_NONE = 0,
	SG_STRIKE_DUTY_BREACH,
	SG_STRIKE_DUTY_CLEAR,
	SG_STRIKE_DUTY_PRESS,
	SG_STRIKE_DUTY_ESCORT,
	SG_STRIKE_DUTY_RECOVER,
	SG_STRIKE_DUTY_CARRY
} sg_strike_duty_t;

/* Production route ownership uses this reducer verdict when the bounded
 * weapon diversion ends.  A controller which has already changed physical
 * state drains through its existing finite witness; every staged or ordinary
 * route is canceled immediately. */
typedef enum sg_strike_weapon_route_verdict_e
{
	SG_STRIKE_WEAPON_ROUTE_CLEAR = 0,
	SG_STRIKE_WEAPON_ROUTE_OWN,
	SG_STRIKE_WEAPON_ROUTE_DRAIN
} sg_strike_weapon_route_verdict_t;

typedef enum sg_strike_weapon_door_retirement_e
{
	SG_STRIKE_WEAPON_DOOR_RELEASE = 0,
	SG_STRIKE_WEAPON_DOOR_HOLD,
	SG_STRIKE_WEAPON_DOOR_TERMINAL
} sg_strike_weapon_door_retirement_t;

/* Minimal production controller snapshot used to distinguish cancelable
 * source staging from irreversible live physics.  Hook and rocket-jump state
 * are checked before link action because an optional speed hook may ride an
 * ordinary RUN commitment. */
typedef struct sg_strike_weapon_controller_state_s
{
	int action;
	int hook_phase;
	int rocketjump_phase;
	int jump_started;
	int drop_started;
	int swim_active;
	int swim_validated;
	int declared_started;
	int declared_touched;
	int declared_triggered;
	int declared_activated;
	int declared_guard_paused;
} sg_strike_weapon_controller_state_t;

enum
{
	SG_STRIKE_EVENT_NONE = 0u,
	SG_STRIKE_EVENT_PICKUP = 1u << 0,
	SG_STRIKE_EVENT_CARRIER_LOSS = 1u << 1,
	SG_STRIKE_EVENT_FLAG_RETURN = 1u << 2,
	SG_STRIKE_EVENT_CAPTURE = 1u << 3,
	SG_STRIKE_EVENT_LEVEL_RESET = 1u << 4
};

/* The production adapter publishes one immutable snapshot before the serial
 * bot-think loop. Costs are milliseconds; negative means unreachable or
 * unknown. attack_eligible is authoritative every frame: false removes an
 * incumbent even while the own flag is away, so recovery duty is assigned
 * only among role-policy-approved attackers.  It must exclude the two
 * reserved normal-5v5 defenders. */
typedef struct sg_strike_slot_input_s
{
	int present;
	int alive;
	int attack_eligible;
	int carrying;
	uint32_t life_id;
	int weapon_tier;
	int enemy_flag_goal_ms;
	int recover_goal_ms;
	int carrier_goal_ms;
	int direct_flag_touch;
} sg_strike_slot_input_t;

typedef struct sg_strike_frame_s
{
	float now;
	int own_flag_home;
	int enemy_flag_home;
	int enemy_flag_dropped;
	int recent_enemy_room_death;
	unsigned events;
	int carrier_slot;
	sg_strike_slot_input_t slot[SG_STRIKE_MAX_SLOTS];
} sg_strike_frame_t;

typedef struct sg_strike_team_s
{
	uint32_t epoch;
	sg_strike_phase_t phase;
	uint32_t member_mask;
	uint32_t weapon_ready_mask;
	uint32_t mission_ready_mask;
	uint32_t hold_mask;
	uint32_t rush_mask;
	uint32_t arrived_mask;
	uint32_t attempt_mask;
	sg_strike_duty_t duty[SG_STRIKE_MAX_SLOTS];
	/* Retained while a slot is outside member_mask so same-life re-selection
	 * cannot re-arm its immutable usable-weapon deadline. */
	uint32_t member_life[SG_STRIKE_MAX_SLOTS];
	float weapon_deadline[SG_STRIKE_MAX_SLOTS];
	float phase_since;
	float form_deadline;
	float go_since;
	float clear_until;
	int carrier_slot;
} sg_strike_team_t;

void SG_StrikeReset(sg_strike_team_t *team);

/* Advance exactly once from a pre-frame snapshot. Invalid input fails closed
 * without mutating the prior state. Returns non-zero on success. */
int SG_StrikeStep(sg_strike_team_t *team, const sg_strike_frame_t *frame);

int SG_StrikeMember(const sg_strike_team_t *team, int slot);
/* Participants are the capped attacker roster plus an actual external
 * carrier during EGRESS.  The carrier never enters member_mask. */
int SG_StrikeParticipant(const sg_strike_team_t *team, int slot);
int SG_StrikeMemberNeedsWeapon(const sg_strike_team_t *team, int slot,
	float now);
/* A weapon errand is subordinate to stand pressure.  A member already inside
 * the five-second enemy-flag band stays on the objective; outside that band,
 * the weapon must be both deadline-reachable and at least one second cheaper
 * than the current enemy-flag route. */
int SG_StrikeWeaponDetourAllowed(int needs_weapon, int strike_rush,
	int carrying, int combat_engaged, int direct_flag_touch,
	int enemy_flag_goal_ms, int weapon_goal_ms, int remaining_ms);
int SG_StrikeMemberShouldHold(const sg_strike_team_t *team, int slot);
int SG_StrikeMemberRushes(const sg_strike_team_t *team, int slot);
sg_strike_weapon_route_verdict_t SG_StrikeWeaponRouteVerdict(
	int exact_route_owned, int weapon_authority,
	int physical_controller_active, int retirement_latched);
int SG_StrikeWeaponControllerPhysical(
	const sg_strike_weapon_controller_state_t *state);
sg_strike_weapon_door_retirement_t SG_StrikeWeaponDoorRetirement(
	int release_proved_clear, int recovery_expired, int hold_open_ready);
int SG_StrikeGenericRailAllowed(int strike_active);

/* A concrete pressure duty persists independently of the transient HOLD/RUSH
 * release mask.  While the coordinator owns a bot, its duty overrides the
 * organic role for enemy-pressure policy. */
int SG_StrikeDutyEnemyPressure(sg_strike_duty_t duty);
int SG_StrikeEnemyPressureActive(int ordinary_attack, int strike_active,
	sg_strike_duty_t duty);
/* Corner pursuit follows the objective route it supports.  Pressure and
 * recovery duties may keep a recently lost contact; escort and carrier duties
 * may not spend their route clock camping. */
int SG_StrikeDutyCombatPursuit(sg_strike_duty_t duty);
int SG_StrikeCombatPursuitActive(int ordinary_pursuit, int strike_active,
	sg_strike_duty_t duty);
/* Rearguard duty belongs to enemy-room pressure and to the explicit escort,
 * never to recovery or the carrier itself. */
int SG_StrikeDutyRearguard(sg_strike_duty_t duty);
int SG_StrikeRearguardActive(int ordinary_rearguard, int strike_active,
	sg_strike_duty_t duty);
/* A concrete coordinator duty is an objective mission, so optional item-pad
 * preparation cannot remain live underneath its route and aim authority. */
int SG_StrikeDutyRetiresOptionalErrand(sg_strike_duty_t duty);

/* Bounded pricing correction for the last proved RUNs toward a home enemy
 * flag.  This never grants touch or movement authority; it only breaks a
 * near-stand field plateau in favor of physical progress toward the item. */
float SG_StrikeFlagApproachPrice(int flag_available, int run_link,
	float current_distance, float candidate_distance, float vertical_delta,
	int current_goal_ms, int candidate_goal_ms);

#endif /* SG_STRIKE_H */
