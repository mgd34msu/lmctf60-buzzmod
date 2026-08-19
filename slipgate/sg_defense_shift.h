/* sg_defense_shift.h -- deterministic threat-responsive post movement. */
#ifndef SG_DEFENSE_SHIFT_H
#define SG_DEFENSE_SHIFT_H

#include <stddef.h>

typedef struct sg_defense_shift_candidate_s
{
	int link_index;
	int seed_index;
	int goal_ms;
	float delta_x;
	float delta_y;
	float delta_z;
} sg_defense_shift_candidate_t;

typedef struct sg_defense_shift_request_s
{
	float threat_x;
	float threat_y;
	float max_distance;
	int max_goal_ms;
	int previous_seed;
} sg_defense_shift_request_t;

typedef struct sg_defense_patrol_candidate_s
{
	int link_index;
	int seed_index;
	int goal_ms;
	int is_run;
} sg_defense_patrol_candidate_t;

/* Pure geometry and admission policy for the stand defender's live-combat
 * leg. Collision and floor truth remain in sg_move.c, where the real player
 * hull and current world are available. */
typedef struct sg_defense_combat_request_s
{
	int enabled;
	int hold_post;
	int defend_stand;
	int own_flag_home;
	int engaged;
	int live_enemy;
	int identity_valid;
	int movement_clear;
	float self_x, self_y;
	float stand_x, stand_y;
	float enemy_x, enemy_y;
	float camp_scale;
	int identity;
	int phase;
	/* The live writer owns this brief engagement-local preference. A valid
	 * value keeps the tangent from changing with the half-second phase; zero
	 * leaves the deterministic first-choice law in charge. */
	int preferred_tangent_sign;
} sg_defense_combat_request_t;

typedef struct sg_defense_combat_move_s
{
	float x, y;
	int tangent_sign;
} sg_defense_combat_move_t;

typedef struct sg_defense_combat_probe_s
{
	int body_clear;
	int player_clear;
	int floor_clear;
	float stand_distance;
	float vertical_step;
} sg_defense_combat_probe_t;

/* Produce the deterministic first tangent, including only the small radial
 * correction needed to keep the stand's ring clear. */
int SG_DefenseCombatChoose(const sg_defense_combat_request_t *request,
	sg_defense_combat_move_t *move_out);

/* The world adapter supplies trace truth; keep the reject law independently
 * testable so both tangent attempts are equally fail-closed. */
int SG_DefenseCombatProbeAllowed(const sg_defense_combat_probe_t *probe);

/* Choose one direct RUN step that stays in the post band and moves across,
 * rather than into, the believed attack line. Returns the link index or -1. */
int SG_DefenseShiftChoose(const sg_defense_shift_request_t *request,
	const sg_defense_shift_candidate_t *candidates, size_t candidate_count,
	int *seed_out);

/* Retire only the exact shift commitment after its selected link becomes
 * invalid. Returns non-zero when a shift was retired. */
int SG_DefenseShiftRetireIfInvalid(int shift_link, int link_ready,
	int *commit_link);

/* Choose one quiet-post patrol leg.  The draw is supplied by the live caller,
 * but filtering and immediate-reversal avoidance are deterministic and
 * independently executable. */
int SG_DefensePatrolChoose(const sg_defense_patrol_candidate_t *candidates,
	size_t candidate_count, int max_goal_ms, int previous_seed,
	unsigned draw, int *seed_out);

/* Complete a patrol leg only at its selected seed.  Clearing the target is the
 * caller's authority to start the post-arrival dwell clock. */
int SG_DefensePatrolFinishLeg(int current_seed, int *target_seed);

/* sg_patrol is the patrol walking throttle.  Invalid/off values disable the
 * behavior; enabled values are bounded away from both a shuffle and a sprint. */
float SG_DefensePatrolThrottle(float configured);

#endif /* SG_DEFENSE_SHIFT_H */
