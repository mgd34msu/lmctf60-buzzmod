/* sg_rune_binding.h -- authenticated native mechanism-plan bindings. */
#ifndef SG_RUNE_BINDING_H
#define SG_RUNE_BINDING_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune.h"

#define SG_RUNE_BINDING_MAX_MOVERS 16U

typedef enum sg_carrier_door_stage_e
{
	SG_CARRIER_DOOR_APPROACH = 0,
	SG_CARRIER_DOOR_EGRESS = 1
} sg_carrier_door_stage_t;

struct edict_s;

typedef struct sg_rune_mechanism_binding_s
{
	const rune_t *rune;
	const rune_link_t *link;
	const rune_mechanism_plan_t *plan;
	const rune_mechanism_node_t *entry_node;
	const rune_mechanism_node_t *mover_node;
	const rune_mechanism_node_t *destination_node;
	const rune_mechanism_node_t *egress_node;
	struct edict_s *entry_entity;
	struct edict_s *mover_entity;
	struct edict_s *destination_entity;
	struct edict_s *egress_entity;
	uint32_t link_index;
} sg_rune_mechanism_binding_t;

/* Capture succeeds only when the current native publication, complete sealed
 * inventory, action/controller pair, plan closure, and every live closure
 * endpoint still match.  No anchor or target-name search participates. */
int SG_RuneMechanismBindingCapture(const rune_t *rune, uint32_t link_index,
	sg_rune_mechanism_binding_t *binding_out);
/* Owned recapture keeps the sealed incarnation, strings, relations, and full
 * callback closure exact while admitting only canonical controller motion
 * callbacks.  Callers must already hold the corresponding transaction. */
int SG_RuneMechanismBindingCaptureOwned(const rune_t *rune,
	uint32_t link_index, sg_rune_mechanism_binding_t *binding_out);
int SG_RuneMechanismBindingCurrent(
	const sg_rune_mechanism_binding_t *binding);
/* Revalidate the sealed incarnation, immutable callbacks, strings, pointer
 * relations, and complete fanout without admitting a controller-owned motion
 * state.  This is for synchronous validation while the oracle has temporarily
 * posed a mover; live execution must use BindingCurrent. */
int SG_RuneMechanismBindingTopologyCurrent(
	const sg_rune_mechanism_binding_t *binding);

const rune_mechanism_edge_t *SG_RuneMechanismBindingEdgeAt(
	const sg_rune_mechanism_binding_t *binding, uint32_t edge_ordinal);
struct edict_s *SG_RuneMechanismBindingResolveNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key);
struct edict_s *SG_RuneMechanismBindingResolveTopologyNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key);
/* TELEPORT destinations are recovered only from the authenticated TARGET
 * edge in this plan's copied closure.  No target-name lookup participates. */
struct edict_s *SG_RuneMechanismBindingResolveDestination(
	const sg_rune_mechanism_binding_t *binding);

/* Visit one source's complete TARGET fanout in authenticated ordinal order.
 * The source and every target must belong to this copied plan closure and
 * remain current before and after each visitor call.  No name lookup occurs. */
typedef int (*sg_rune_mechanism_target_visitor_fn)(void *context,
	struct edict_s *target, uint32_t target_key, uint32_t target_ordinal);
int SG_RuneMechanismBindingDispatchTargets(
	const sg_rune_mechanism_binding_t *binding, uint32_t source_key,
	sg_rune_mechanism_target_visitor_fn visitor, void *context);

/* Return the sorted unique physical-mover union authenticated by the complete
 * plan closure.  Non-mover side-effect nodes remain part of binding validity
 * but never become lease keys. */
int SG_RuneMechanismBindingMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out);
int SG_RuneMechanismBindingTopologyMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out);

int SG_RuneMechanismBindingAuxDoorMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out);
struct edict_s *SG_RuneMechanismBindingAuxTrigger(
	const sg_rune_mechanism_binding_t *binding);
int SG_RuneMechanismBindingAuxTriggerMatches(
	const sg_rune_mechanism_binding_t *binding, const struct edict_s *entity);

int SG_RuneMechanismBindingCarrierStage(
	const sg_rune_mechanism_binding_t *binding,
	sg_carrier_door_stage_t stage, struct edict_s **trigger_out,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out,
	uint32_t *delay_ms_out);
int SG_RuneMechanismBindingCarrierStageTriggerMatches(
	const sg_rune_mechanism_binding_t *binding,
	sg_carrier_door_stage_t stage, const struct edict_s *entity);

/* Fail closed unless every link governed by an admitted map mechanism can be
 * captured against the sealed live catalog before graph publication. */
int SG_RuneMechanismBindingsReady(const rune_t *rune,
	uint32_t *failure_index_out);

int SG_RuneMechanismBindingDoorAction(
	const sg_rune_mechanism_binding_t *binding);

#endif /* SG_RUNE_BINDING_H */
