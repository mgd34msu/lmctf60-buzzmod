/*
 * sg_defense_supply.h -- the small, deterministic policy core for a
 * watchman's bounded weapon sortie.  World/item truth remains in sg_goal.c;
 * this module owns only the phase transition and route-authority law so it
 * can be exercised without a running server.
 */
#ifndef SG_DEFENSE_SUPPLY_H
#define SG_DEFENSE_SUPPLY_H

/* Weapon pursuit may remain OUTBOUND for no more than five seconds.  RETURN
 * then keeps the immutable home route until the watchman reaches its stand;
 * it never resumes shopping.  Keep the outbound value in the deterministic
 * core so production and focused tests cannot silently diverge. */
#define SG_DEFENSE_SUPPLY_DEADLINE_SECONDS 5.0f

typedef enum sg_defense_supply_phase_e
{
	SG_DEFENSE_SUPPLY_PHASE_NONE = 0,
	SG_DEFENSE_SUPPLY_PHASE_OUTBOUND = 1,
	SG_DEFENSE_SUPPLY_PHASE_RETURN = 2
} sg_defense_supply_phase_t;

typedef struct sg_defense_supply_step_s
{
	int identity_valid;
	int owner_valid;       /* role, life, stand, and not carrying */
	int own_flag_home;
	int threat;
	int engaged;
	int other_owner;
	int target_valid;
	int weapon_field_valid;
	int deadline_pending;
	int weapon_available;
} sg_defense_supply_step_t;

typedef struct sg_defense_supply_neighbor_s
{
	int link_index;
	int to_seed;
	int route_cost_ms;
} sg_defense_supply_neighbor_t;

/* Advance one live transaction.  NONE means the transaction must be
 * cancelled and ordinary home objective logic resumes.  RETURN is retained
 * for every recoverable edge so the caller can route home without reopening
 * the item chooser. */
sg_defense_supply_phase_t SG_DefenseSupplyPhaseStep(
	sg_defense_supply_phase_t phase,
	const sg_defense_supply_step_t *step);

/* Select the route authority for a phase.  OUTBOUND is the current live field
 * flooded from the already-selected valid weapon pad; RETURN is the immutable
 * own-home/post field.  weapon_class_field is an optional arm/reach guard;
 * passing NULL lets an unrelated class refresh reflow without cancelling the
 * already-bound target.  A false result means the caller must enter RETURN
 * (or ordinary home fallback) and never substitute another item by detour
 * arithmetic. */
int SG_DefenseSupplyRoute(
	sg_defense_supply_phase_t phase,
	const int *weapon_class_field,
	const int *selected_target_field,
	const int *home_field,
	int current_seed,
	int max_route_ms,
	const int **route_out);

/* Pick the strictly descending outgoing neighbor from an already-admitted
 * production fan.  This is the final route-pure chooser: no item worth,
 * sticky link, or generic detour value participates. */
int SG_DefenseSupplyChooseNeighbor(
	const sg_defense_supply_neighbor_t *candidates,
	unsigned candidate_count,
	int current_route_cost_ms);

/* OUTBOUND is deliberately a short ordinary run: it may be retired on the
 * exact five-second frame.  Starting a jump, drop, hook, lift, or declared
 * mechanism would transfer command ownership to a longer transaction and
 * make that deadline unenforceable.  RETURN may use the normal home graph. */
int SG_DefenseSupplyActionAllowed(
	sg_defense_supply_phase_t phase,
	int ordinary_run);

/* The generic rail retry owns a different route surface.  It may run only
 * when no supply transaction exists; in particular it must not replace the
 * immutable RETURN route after that route has already passed its purity
 * fence. */
int SG_DefenseSupplyGenericRetryAllowed(
	sg_defense_supply_phase_t phase,
	int armed);

#endif /* SG_DEFENSE_SUPPLY_H */
