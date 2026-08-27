/* sg_rune_mechanism_plan.h -- exact post-prune mechanism plan materializer. */
#ifndef SG_RUNE_MECHANISM_PLAN_H
#define SG_RUNE_MECHANISM_PLAN_H

#include "sg_rune.h"
#include "sg_rune_mechanism_catalog.h"

#include <stddef.h>
#include <stdint.h>

typedef void *(*sg_mechanism_link_alloc_fn)(size_t size);
typedef void (*sg_mechanism_link_release_fn)(void *allocation);

/* The generator captures the exact live mechanism identity when it creates a
 * graph link.  Objective pruning copies only bindings owned by surviving
 * links and rewrites their indices densely before this materializer runs. */
typedef struct sg_mechanism_plan_binding_s
{
	uint32_t entry_key;
	uint32_t mover_key;
	uint32_t destination_key;
	uint32_t egress_key;
	uint16_t controller_kind;
	uint16_t expected_members;
	uint32_t cooldown_ms;
} sg_mechanism_plan_binding_t;

typedef enum sg_mechanism_plan_diagnostic_e
{
	SG_MECHANISM_PLAN_OK = 0,
	SG_MECHANISM_PLAN_INVALID_ARGUMENT,
	SG_MECHANISM_PLAN_BAD_BINDING,
	SG_MECHANISM_PLAN_BAD_ACTION,
	SG_MECHANISM_PLAN_BAD_CATALOG,
	SG_MECHANISM_PLAN_BAD_CLOSURE,
	SG_MECHANISM_PLAN_CAPACITY,
	SG_MECHANISM_PLAN_BAD_CRC
} sg_mechanism_plan_diagnostic_t;

typedef struct sg_mechanism_plan_buffers_s
{
	rune_mechanism_edge_t *edges;
	uint32_t edge_capacity;
	rune_mechanism_plan_t *plans;
	uint32_t plan_capacity;
	/* Scratch is caller-owned so production uses the game allocator and tests
	 * stay independent of engine allocation state. */
	uint32_t *edge_marks;
	uint32_t edge_mark_capacity;
	uint32_t *node_marks;
	uint32_t node_mark_capacity;
	uint32_t *node_queue;
	uint32_t node_queue_capacity;
} sg_mechanism_plan_buffers_t;

typedef struct sg_mechanism_plan_result_s
{
	sg_mechanism_plan_diagnostic_t diagnostic;
	uint32_t link_index;
	uint32_t plan_index;
	uint32_t num_inventory_edges;
	uint32_t num_edges;
	uint32_t num_plans;
} sg_mechanism_plan_result_t;

typedef struct sg_timed_vault_plan_witness_s
{
	uint32_t entry_key;
	uint32_t mover_key;
	uint32_t member_key;
	uint32_t short_relay_key;
	uint32_t restore_relay_key;
	uint32_t effect_keys[9];
	uint32_t touch_hold_ms;
	uint32_t readiness_ms;
	uint32_t usable_window_ms;
	uint32_t restore_ms;
} sg_timed_vault_plan_witness_t;

typedef struct sg_relay_wall_plan_witness_s
{
	uint32_t entry_key;
	uint32_t wall_key;
	uint32_t immediate_relay_key;
	uint32_t restore_relay_key;
	uint32_t touch_hold_ms;
	uint32_t cooldown_ms;
	uint32_t active_window_ms;
	uint32_t restore_ms;
} sg_relay_wall_plan_witness_t;

/* Identify one complete delayed-button, immediate/restore relay pair with
 * identical ordered fanout and exactly one toggle wall. */
int SG_RelayWallPlanDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, sg_relay_wall_plan_witness_t *witness_out);

/* Identify only the complete two-leaf, two-relay, eight-laser and one-speaker
 * timed-vault catalog shape. The result is suitable for generator binding;
 * the materializer independently revalidates the same closure. */
int SG_TimedVaultPlanDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, sg_timed_vault_plan_witness_t *witness_out);

/* Select the complete nonstandard button plan, if any, and return the exact
 * generator binding fields. Ordinary direct-door buttons are not admitted. */
int SG_ButtonMechanismPlanBindingDiscover(
	const sg_mech_catalog_view_t *catalog, uint32_t entry_key,
	sg_mechanism_plan_binding_t *binding_out);

/* Select either an authenticated nonstandard button transaction or the
 * ordinary direct button-to-door-team binding. */
int SG_ButtonDoorPlanBindingDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, sg_mechanism_plan_binding_t *binding_out);

/* Materialize one unique plan per surviving plan-required link. Inventory
 * edges remain the exact prefix; every appended plan edge is a byte-exact
 * inventory copy in controller execution order. Planless actions must retain
 * RUNE_NO_MECHANISM_PLAN. Nothing is published unless the entire graph is
 * admitted and all bindings are consumed exactly once in dense link order. */
int SG_MechanismPlansMaterialize(rune_link_t *links, uint32_t num_links,
	const sg_mechanism_plan_binding_t *bindings, uint32_t num_bindings,
	const sg_mech_catalog_view_t *catalog,
	sg_mechanism_plan_buffers_t *buffers,
	sg_mechanism_plan_result_t *result_out);

/* Collapse wire-equivalent links after provisional bindings become final plan
 * indexes. Relative order is stable and the cheapest exact proof survives. */
int SG_MechanismLinksDeduplicate(rune_link_t *links, int *num_links,
	sg_mechanism_link_alloc_fn allocate,
	sg_mechanism_link_release_fn release, uint32_t *removed_out);

const char *SG_MechanismPlanDiagnosticName(
	sg_mechanism_plan_diagnostic_t diagnostic);

#endif /* SG_RUNE_MECHANISM_PLAN_H */
