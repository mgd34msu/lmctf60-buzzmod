/* sg_rune_mechanism_plan.h -- exact post-prune mechanism plan materializer. */
#ifndef SG_RUNE_MECHANISM_PLAN_H
#define SG_RUNE_MECHANISM_PLAN_H

#include "sg_rune.h"
#include "sg_rune_mechanism_catalog.h"

#include <stdint.h>

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

const char *SG_MechanismPlanDiagnosticName(
	sg_mechanism_plan_diagnostic_t diagnostic);

#endif /* SG_RUNE_MECHANISM_PLAN_H */
