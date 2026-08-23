/* sg_rune_binding.c -- authenticated native mechanism-plan bindings. */
#include "../q_shared.h"
#include "sg_rune_binding.h"

#include "sg_crc32.h"
#include "sg_rune_codec.h"
#include "sg_rune_mechanism_catalog.h"

#include <math.h>

static void Binding_PutU16(unsigned char *output, uint16_t value)
{
	output[0] = (unsigned char)(value & UINT16_C(0xff));
	output[1] = (unsigned char)(value >> 8);
}

static void Binding_PutU32(unsigned char *output, uint32_t value)
{
	output[0] = (unsigned char)(value & UINT32_C(0xff));
	output[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	output[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	output[3] = (unsigned char)(value >> 24);
}

static int Binding_ClosureCRC(const rune_t *rune,
	const rune_mechanism_plan_t *plan, uint32_t *crc_out)
{
	unsigned char encoded[16];
	uint32_t state;
	uint32_t ordinal;
	const rune_mechanism_node_t *entry;

	if (!rune || !plan || !crc_out ||
	    plan->first_edge < rune->artifact.num_inventory_edges ||
	    plan->first_edge > rune->artifact.num_mechanism_edges ||
	    plan->num_edges >
	        rune->artifact.num_mechanism_edges - plan->first_edge)
		return 0;
	if (plan->controller_kind == SG_MECHANISM_CONTROLLER_PUSH)
	{
		entry = SG_RuneMechanismNodeByKey(rune, plan->entry_key);
		return plan->num_edges == 0U && entry &&
		       SG_RuneCodecPushClosureCRC32(entry->key,
		           entry->push_velocity, crc_out) == RLCODEC_OK;
	}
	if (plan->num_edges == 0U)
		return 0;
	state = SG_CRC32Init();
	for (ordinal = 0U; ordinal < plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			&rune->mechanism_edges[plan->first_edge + ordinal];

		Binding_PutU32(encoded + 0U, edge->from_key);
		Binding_PutU32(encoded + 4U, edge->to_key);
		Binding_PutU16(encoded + 8U, edge->kind);
		Binding_PutU16(encoded + 10U, edge->ordinal);
		Binding_PutU32(encoded + 12U, edge->delay_ms);
		if (!SG_CRC32Update(&state, encoded, sizeof(encoded)))
			return 0;
	}
	*crc_out = SG_CRC32Final(state);
	return 1;
}

static int Binding_EdgeAuthenticated(const rune_t *rune,
	const rune_mechanism_edge_t *edge)
{
	uint32_t inventory;

	for (inventory = 0U;
	     inventory < rune->artifact.num_inventory_edges; inventory++)
		if (memcmp(edge, &rune->mechanism_edges[inventory],
		    sizeof(*edge)) == 0)
			return 1;
	return 0;
}

static int Binding_NodeDoorMover(const rune_mechanism_node_t *node)
{
	return node && (node->kind == SG_MECH_NODE_DOOR_MASTER ||
	       node->kind == SG_MECH_NODE_DOOR_MEMBER);
}

static int Binding_EdgeMatches(const rune_mechanism_edge_t *edge,
	uint32_t from_key, uint32_t to_key, uint16_t kind)
{
	return edge && edge->from_key == from_key && edge->to_key == to_key &&
	       edge->kind == kind;
}

static uint32_t Binding_EdgeCount(const rune_t *rune,
	const rune_mechanism_plan_t *plan, uint32_t from_key, uint32_t to_key,
	uint16_t kind)
{
	uint32_t ordinal;
	uint32_t count = 0U;

	if (!rune || !plan)
		return 0U;
	for (ordinal = 0U; ordinal < plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			&rune->mechanism_edges[plan->first_edge + ordinal];

		if (Binding_EdgeMatches(edge, from_key, to_key, kind))
			count++;
	}
	return count;
}

static int Binding_DirectDoorEntryReachesMover(const rune_t *rune,
	const rune_mechanism_plan_t *plan,
	const rune_mechanism_node_t *entry,
	const rune_mechanism_node_t *mover)
{
	uint32_t ordinal;
	uint32_t paths = Binding_EdgeCount(rune, plan, entry->key, mover->key,
		SG_MECH_EDGE_TARGET);

	for (ordinal = 0U; ordinal < plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			&rune->mechanism_edges[plan->first_edge + ordinal];
		const rune_mechanism_node_t *relay;

		if (edge->from_key != entry->key ||
		    edge->kind != SG_MECH_EDGE_TARGET ||
		    !(relay = SG_RuneMechanismNodeByKey(rune, edge->to_key)) ||
		    relay->kind != SG_MECH_NODE_RELAY ||
		    relay->use_callback != SG_MECH_CALLBACK_USE_TRIGGER_RELAY)
			continue;
		paths += Binding_EdgeCount(rune, plan, relay->key, mover->key,
			SG_MECH_EDGE_TARGET);
	}
	return paths == 1U;
}

static int Binding_TrainTerminals(const rune_t *rune,
	const rune_mechanism_plan_t *plan,
	const rune_mechanism_node_t **closed_out,
	const rune_mechanism_node_t **open_out)
{
	const rune_mechanism_edge_t *button_target;
	const rune_mechanism_edge_t *train_route;
	const rune_mechanism_edge_t *closed_route;
	const rune_mechanism_edge_t *open_route;
	const rune_mechanism_node_t *closed;
	const rune_mechanism_node_t *open;

	if (closed_out)
		*closed_out = NULL;
	if (open_out)
		*open_out = NULL;
	if (!rune || !plan || !closed_out || !open_out || plan->num_edges != 4U)
		return 0;
	button_target = &rune->mechanism_edges[plan->first_edge + 0U];
	train_route = &rune->mechanism_edges[plan->first_edge + 1U];
	closed_route = &rune->mechanism_edges[plan->first_edge + 2U];
	open_route = &rune->mechanism_edges[plan->first_edge + 3U];
	closed = SG_RuneMechanismNodeByKey(rune, closed_route->from_key);
	open = SG_RuneMechanismNodeByKey(rune, closed_route->to_key);
	if (!closed || !open ||
	    !Binding_EdgeMatches(button_target, plan->entry_key,
	        plan->mover_key, SG_MECH_EDGE_TARGET) ||
	    !Binding_EdgeMatches(train_route, plan->mover_key, open->key,
	        SG_MECH_EDGE_ROUTE_TARGET) ||
	    !Binding_EdgeMatches(closed_route, closed->key, open->key,
	        SG_MECH_EDGE_ROUTE_TARGET) ||
	    !Binding_EdgeMatches(open_route, open->key, closed->key,
	        SG_MECH_EDGE_ROUTE_TARGET) ||
	    button_target->ordinal != 0U || train_route->ordinal != 0U ||
	    closed_route->ordinal != 0U || open_route->ordinal != 0U ||
	    button_target->delay_ms != 0U || train_route->delay_ms != 0U ||
	    closed_route->delay_ms != 0U || open_route->delay_ms != 0U ||
	    closed == open || closed->kind != SG_MECH_NODE_PATH_CORNER ||
	    open->kind != SG_MECH_NODE_PATH_CORNER)
		return 0;
	*closed_out = closed;
	*open_out = open;
	return 1;
}

static const rune_mechanism_node_t *Binding_TeleportDestination(
	const rune_t *rune, const rune_mechanism_plan_t *plan)
{
	const rune_mechanism_node_t *destination = NULL;
	uint32_t ordinal;

	if (!rune || !plan)
		return NULL;
	for (ordinal = 0U; ordinal < plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			&rune->mechanism_edges[plan->first_edge + ordinal];
		const rune_mechanism_node_t *node;

		if (edge->from_key != plan->entry_key ||
		    edge->kind != SG_MECH_EDGE_TARGET)
			continue;
		node = SG_RuneMechanismNodeByKey(rune, edge->to_key);
		if (!node || node->kind != SG_MECH_NODE_TELEPORT_DEST || destination)
			return NULL;
		destination = node;
	}
	return destination;
}

static uint32_t Binding_DoorMoverCount(const rune_t *rune,
	const rune_mechanism_plan_t *plan,
	const rune_mechanism_node_t *primary)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	uint32_t count = 0U;
	uint32_t ordinal;

	if (!rune || !plan || !Binding_NodeDoorMover(primary))
		return 0U;
	keys[count++] = primary->key;
	for (ordinal = 0U; ordinal < plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			&rune->mechanism_edges[plan->first_edge + ordinal];
		const rune_mechanism_node_t *nodes[2];
		uint32_t endpoint;

		nodes[0] = SG_RuneMechanismNodeByKey(rune, edge->from_key);
		nodes[1] = SG_RuneMechanismNodeByKey(rune, edge->to_key);
		for (endpoint = 0U; endpoint < 2U; endpoint++)
		{
			uint32_t cursor;

			if (!Binding_NodeDoorMover(nodes[endpoint]))
				continue;
			for (cursor = 0U; cursor < count; cursor++)
				if (keys[cursor] == nodes[endpoint]->key)
					break;
			if (cursor != count)
				continue;
			if (count >= SG_RUNE_BINDING_MAX_MOVERS)
				return 0U;
			keys[count++] = nodes[endpoint]->key;
		}
	}
	return count;
}

static uint32_t Binding_PlatformMoverCount(const rune_t *rune,
	const rune_mechanism_plan_t *plan,
	const rune_mechanism_node_t *platform)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	uint32_t count = 0U;
	uint32_t ordinal;

	if (!rune || !plan || !platform ||
	    platform->kind != SG_MECH_NODE_PLATFORM)
		return 0U;
	keys[count++] = platform->key;
	for (ordinal = 0U; ordinal < plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			&rune->mechanism_edges[plan->first_edge + ordinal];
		const rune_mechanism_node_t *nodes[2];
		uint32_t endpoint;

		nodes[0] = SG_RuneMechanismNodeByKey(rune, edge->from_key);
		nodes[1] = SG_RuneMechanismNodeByKey(rune, edge->to_key);
		for (endpoint = 0U; endpoint < 2U; endpoint++)
		{
			uint32_t cursor;

			if (!Binding_NodeDoorMover(nodes[endpoint]))
				continue;
			for (cursor = 0U; cursor < count; cursor++)
				if (keys[cursor] == nodes[endpoint]->key)
					break;
			if (cursor != count)
				continue;
			if (count >= SG_RUNE_BINDING_MAX_MOVERS)
				return 0U;
			keys[count++] = nodes[endpoint]->key;
		}
	}
	return count;
}

static int Binding_NodeExecutable(const rune_mechanism_node_t *node)
{
	return node && (node->flags & SG_MECH_NODEF_INVENTORY_ONLY) == 0U &&
	       node->touch_callback != SG_MECH_CALLBACK_UNKNOWN &&
	       node->use_callback != SG_MECH_CALLBACK_UNKNOWN &&
	       node->think_callback != SG_MECH_CALLBACK_UNKNOWN &&
	       node->blocked_callback != SG_MECH_CALLBACK_UNKNOWN;
}

static int Binding_TrainCornerShape(const rune_mechanism_node_t *corner)
{
	return Binding_NodeExecutable(corner) &&
	       corner->kind == SG_MECH_NODE_PATH_CORNER &&
	       corner->flags == (SG_MECH_NODEF_TOUCHABLE |
	           SG_MECH_NODEF_ONE_SHOT) && corner->spawnflags == 0U &&
	       corner->touch_callback == SG_MECH_CALLBACK_PATH_CORNER_TOUCH &&
	       corner->use_callback == SG_MECH_CALLBACK_NONE &&
	       corner->think_callback == SG_MECH_CALLBACK_NONE &&
	       corner->blocked_callback == SG_MECH_CALLBACK_NONE &&
	       corner->delay_ms == 0 && corner->wait_ms == -1000 &&
	       corner->target_offset != 0U && corner->killtarget_offset == 0U;
}

static int Binding_TrainSealedThink(uint16_t callback)
{
	return callback == SG_MECH_CALLBACK_NONE ||
	       callback == SG_MECH_CALLBACK_FUNC_TRAIN_FIND;
}

static int Binding_ControllerShape(const rune_t *rune,
	const rune_link_t *link, const rune_mechanism_plan_t *plan,
	const rune_mechanism_node_t *entry,
	const rune_mechanism_node_t *mover)
{
	uint16_t required_flags;

	if (!rune || !link || !plan || !entry ||
	    !SG_ActionMechanismPlanAllowed(link->action, plan->controller_kind) ||
	    !(required_flags = SG_MechanismControllerPlanFlags(
	        plan->controller_kind)) || plan->flags != required_flags ||
	    plan->expected_members == 0U ||
	    plan->expected_members > SG_RUNE_BINDING_MAX_MOVERS)
		return 0;
	switch (plan->controller_kind)
	{
	case SG_MECHANISM_CONTROLLER_PUSH:
		return link->action == RL_PUSH && !mover &&
		       plan->mover_key == SG_MECH_NO_KEY && plan->num_edges == 0U &&
		       entry->kind == SG_MECH_NODE_PUSH &&
		       entry->flags == (SG_MECH_NODEF_REPEATABLE |
		           SG_MECH_NODEF_TOUCHABLE) &&
		       entry->touch_callback == SG_MECH_CALLBACK_TRIGGER_PUSH_TOUCH &&
		       plan->expected_members == 1U && plan->cooldown_ms == 0U;
	case SG_MECHANISM_CONTROLLER_PLATFORM:
	{
		uint32_t cooldown = entry->wait_ms > RUNE_MAX_COST_MS
			? RUNE_MAX_COST_MS : (uint32_t)entry->wait_ms;
		int stock = entry->touch_callback ==
		        SG_MECH_CALLBACK_TOUCH_PLAT_CENTER &&
		    (entry->flags & SG_MECH_NODEF_SYNTHETIC) != 0U &&
		    plan->cooldown_ms == 0U &&
		    Binding_EdgeCount(rune, plan, entry->key, mover->key,
		        SG_MECH_EDGE_TARGET) == 0U;
		int carrier = entry->touch_callback == SG_MECH_CALLBACK_TOUCH_MULTI &&
		    entry->use_callback == SG_MECH_CALLBACK_USE_MULTI &&
		    (entry->flags & (SG_MECH_NODEF_SYNTHETIC |
		        SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		        SG_MECH_NODEF_USABLE)) ==
		        (SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		         SG_MECH_NODEF_USABLE) &&
		    entry->delay_ms == 0 && entry->wait_ms > 0 &&
		    entry->killtarget_offset == 0U && entry->path_target_offset == 0U &&
		    mover->use_callback == SG_MECH_CALLBACK_USE_DOOR &&
		    mover->blocked_callback == SG_MECH_CALLBACK_BLOCKED_DOOR &&
		    SG_RuneCarrierDoorSpawnflags(mover->spawnflags) &&
		    (mover->flags & (SG_MECH_NODEF_MOVER |
		        SG_MECH_NODEF_TEAM_MASTER | SG_MECH_NODEF_SHOOTABLE)) ==
		        (SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MASTER) &&
		    plan->cooldown_ms == cooldown &&
		    Binding_EdgeCount(rune, plan, entry->key, mover->key,
		        SG_MECH_EDGE_TARGET) == 1U;

		return link->action == RL_LIFT &&
		       entry->kind == SG_MECH_NODE_PLATFORM_TRIGGER &&
		       mover->kind == SG_MECH_NODE_PLATFORM &&
		       entry->owner_key == mover->key &&
		       ((stock && plan->expected_members == 1U) ||
		        (carrier && Binding_PlatformMoverCount(rune, plan, mover) ==
		            plan->expected_members)) &&
		       Binding_EdgeCount(rune, plan, entry->key, mover->key,
		           SG_MECH_EDGE_OWNER) == 1U && (stock || carrier);
	}
	case SG_MECHANISM_CONTROLLER_TELEPORT:
		return link->action == RL_TELEPORT &&
		       entry->kind == SG_MECH_NODE_TELEPORT_TRIGGER &&
		       mover->kind == SG_MECH_NODE_TELEPORTER &&
		       entry->owner_key == mover->key &&
		       plan->expected_members == 1U && plan->cooldown_ms == 0U &&
		       Binding_EdgeCount(rune, plan, entry->key, mover->key,
		           SG_MECH_EDGE_OWNER) == 1U &&
		       Binding_TeleportDestination(rune, plan) != NULL;
	case SG_MECHANISM_CONTROLLER_TRAIN:
	case SG_MECHANISM_CONTROLLER_TRAIN_SHOOT:
	{
		const rune_mechanism_node_t *closed;
		const rune_mechanism_node_t *open;
		int shoot = plan->controller_kind ==
			SG_MECHANISM_CONTROLLER_TRAIN_SHOOT;

		return link->action == RL_TRAIN && plan->expected_members == 1U &&
		       plan->cooldown_ms > 0U &&
		       plan->cooldown_ms <= RUNE_MAX_COST_MS &&
		       Binding_TrainTerminals(rune, plan, &closed, &open) &&
		       Binding_NodeExecutable(entry) &&
		       entry->kind == SG_MECH_NODE_BUTTON &&
		       entry->flags == (SG_MECH_NODEF_REPEATABLE |
		           SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER |
		           (shoot ? SG_MECH_NODEF_SHOOTABLE :
		               SG_MECH_NODEF_TOUCHABLE)) && entry->spawnflags == 0U &&
		       entry->touch_callback == (shoot ? SG_MECH_CALLBACK_NONE :
		           SG_MECH_CALLBACK_BUTTON_TOUCH) &&
		       entry->use_callback == SG_MECH_CALLBACK_BUTTON_USE &&
		       entry->think_callback == SG_MECH_CALLBACK_NONE &&
		       entry->blocked_callback == SG_MECH_CALLBACK_NONE &&
		       entry->delay_ms == 0 && entry->wait_ms > 0 &&
		       entry->target_offset != 0U &&
		       entry->killtarget_offset == 0U &&
		       entry->path_target_offset == 0U &&
		       Binding_NodeExecutable(mover) &&
		       mover->kind == SG_MECH_NODE_TRAIN &&
		       mover->flags == (SG_MECH_NODEF_REPEATABLE |
		           SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER) &&
		       mover->spawnflags == 2U &&
		       mover->touch_callback == SG_MECH_CALLBACK_NONE &&
		       mover->use_callback == SG_MECH_CALLBACK_TRAIN_USE &&
		       Binding_TrainSealedThink(mover->think_callback) &&
		       mover->blocked_callback == SG_MECH_CALLBACK_BLOCKED_TRAIN &&
		       mover->delay_ms == 0 && mover->speed_q8 != 0U &&
		       mover->speed_q8 == mover->accel_q8 &&
		       mover->speed_q8 == mover->decel_q8 &&
		       mover->target_offset != 0U &&
		       mover->targetname_offset != 0U &&
		       mover->killtarget_offset == 0U &&
		       mover->path_target_offset == 0U &&
		       Binding_TrainCornerShape(closed) &&
		       Binding_TrainCornerShape(open);
	}
	case SG_MECHANISM_CONTROLLER_AUTO_DOOR:
		return link->action == RL_DOOR &&
		       entry->kind == SG_MECH_NODE_AUTO_DOOR_TRIGGER &&
		       entry->touch_callback ==
		           SG_MECH_CALLBACK_TOUCH_DOOR_TRIGGER &&
		       Binding_NodeDoorMover(mover) &&
		       entry->owner_key == mover->key && plan->cooldown_ms == 1000U &&
		       Binding_DoorMoverCount(rune, plan, mover) ==
		           plan->expected_members &&
		       Binding_EdgeCount(rune, plan, entry->key, mover->key,
		           SG_MECH_EDGE_OWNER) == 1U;
	case SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR:
		return link->action == RL_DOOR && entry->kind == SG_MECH_NODE_TRIGGER &&
		       entry->touch_callback == SG_MECH_CALLBACK_TOUCH_MULTI &&
		       (entry->flags & (SG_MECH_NODEF_REPEATABLE |
		           SG_MECH_NODEF_TOUCHABLE)) ==
		           (SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE) &&
		       Binding_NodeDoorMover(mover) && plan->cooldown_ms > 0U &&
		       Binding_DoorMoverCount(rune, plan, mover) ==
		           plan->expected_members &&
		       Binding_DirectDoorEntryReachesMover(rune, plan, entry, mover);
	case SG_MECHANISM_CONTROLLER_BUTTON_DOOR:
		return link->action == RL_BUTTON_DOOR &&
		       entry->kind == SG_MECH_NODE_BUTTON &&
		       entry->touch_callback == SG_MECH_CALLBACK_BUTTON_TOUCH &&
		       (entry->flags & SG_MECH_NODEF_TOUCHABLE) != 0U &&
		       Binding_NodeDoorMover(mover) && plan->cooldown_ms > 0U &&
		       Binding_DoorMoverCount(rune, plan, mover) ==
		           plan->expected_members &&
		       Binding_EdgeCount(rune, plan, entry->key, mover->key,
		           SG_MECH_EDGE_TARGET) == 1U;
	default:
		return 0;
	}
}

static int Binding_NodeTopologyMatches(const rune_mechanism_node_t *node,
	uint16_t controller_kind, int owned_execution)
{
	return node && (owned_execution
		? SG_MechCatalogEntityExecutionMatches(node->key, node,
		      controller_kind)
		: SG_MechCatalogEntityTopologyMatches(node->key, node));
}

static int Binding_Capture(const rune_t *rune, uint32_t link_index,
	sg_rune_mechanism_binding_t *binding_out, int owned_execution)
{
	sg_rune_mechanism_binding_t candidate;
	const rune_mechanism_plan_t *plan;
	const rune_link_t *link;
	uint32_t closure_crc;
	uint32_t ordinal;

	if (binding_out)
		memset(binding_out, 0, sizeof(*binding_out));
	if (!binding_out || !rune || !SG_RunePublishedShapeValid(rune) ||
	    link_index >= rune->artifact.num_links)
		return 0;
	link = &rune->links[link_index];
	plan = SG_RuneMechanismPlanForLink(rune, link_index);
	if (!plan || !SG_ActionRuntimeSupported((int)link->action) ||
	    !SG_ActionMechanismAdmitted((int)link->action) ||
	    !SG_ActionMechanismPlanRequired((int)link->action) ||
	    !SG_ActionMechanismPlanAllowed((int)link->action,
	        plan->controller_kind) ||
	    !Binding_ClosureCRC(rune, plan, &closure_crc) ||
	    closure_crc != plan->closure_crc32 ||
	    !SG_MechCatalogMatches(rune->mechanism_nodes,
	        rune->artifact.num_mechanism_nodes, rune->mechanism_edges,
	        rune->artifact.num_inventory_edges, rune->mechanism_strings,
	        rune->artifact.string_bytes))
		return 0;
	memset(&candidate, 0, sizeof(candidate));
	candidate.rune = rune;
	candidate.link = link;
	candidate.plan = plan;
	candidate.link_index = link_index;
	candidate.entry_node = SG_RuneMechanismNodeByKey(rune,
		plan->entry_key);
	candidate.mover_node = SG_RuneMechanismNodeByKey(rune,
		plan->mover_key);
	if ((plan->controller_kind == SG_MECHANISM_CONTROLLER_TRAIN ||
	     plan->controller_kind == SG_MECHANISM_CONTROLLER_TRAIN_SHOOT) &&
	    !Binding_TrainTerminals(rune, plan, &candidate.destination_node,
	        &candidate.egress_node))
		return 0;
	if (!candidate.entry_node ||
	    (plan->controller_kind != SG_MECHANISM_CONTROLLER_PUSH &&
	     !candidate.mover_node) ||
	    !Binding_ControllerShape(rune, link, plan, candidate.entry_node,
	        candidate.mover_node) ||
	    !Binding_NodeTopologyMatches(candidate.entry_node,
	        plan->controller_kind, owned_execution) ||
	    (candidate.mover_node &&
	     !Binding_NodeTopologyMatches(candidate.mover_node,
	         plan->controller_kind, owned_execution)) ||
	    !(candidate.entry_entity = SG_MechCatalogResolveEntity(
	        candidate.entry_node->key, candidate.entry_node)) ||
	    (candidate.mover_node &&
	     !(candidate.mover_entity = SG_MechCatalogResolveEntity(
	         candidate.mover_node->key, candidate.mover_node))) ||
	    (candidate.destination_node &&
	        !(candidate.destination_entity = SG_MechCatalogResolveEntity(
	            candidate.destination_node->key,
	            candidate.destination_node))) ||
	    (candidate.egress_node &&
	        !(candidate.egress_entity = SG_MechCatalogResolveEntity(
	            candidate.egress_node->key, candidate.egress_node))))
		return 0;
	for (ordinal = 0U; ordinal < plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			&rune->mechanism_edges[plan->first_edge + ordinal];
		const rune_mechanism_node_t *from =
			SG_RuneMechanismNodeByKey(rune, edge->from_key);
		const rune_mechanism_node_t *to =
			SG_RuneMechanismNodeByKey(rune, edge->to_key);

		if (!Binding_EdgeAuthenticated(rune, edge) || !from || !to ||
		    !Binding_NodeTopologyMatches(from, plan->controller_kind,
		        owned_execution) ||
		    !Binding_NodeTopologyMatches(to, plan->controller_kind,
		        owned_execution) ||
		    !SG_MechCatalogResolveEntity(from->key, from) ||
		    !SG_MechCatalogResolveEntity(to->key, to))
			return 0;
	}
	*binding_out = candidate;
	return 1;
}

int SG_RuneMechanismBindingCapture(const rune_t *rune, uint32_t link_index,
	sg_rune_mechanism_binding_t *binding_out)
{
	return Binding_Capture(rune, link_index, binding_out, 0);
}

int SG_RuneMechanismBindingCaptureOwned(const rune_t *rune,
	uint32_t link_index, sg_rune_mechanism_binding_t *binding_out)
{
	return Binding_Capture(rune, link_index, binding_out, 1);
}

static int Binding_Current(const sg_rune_mechanism_binding_t *binding,
	int owned_execution)
{
	sg_rune_mechanism_binding_t current;

	return binding && binding->rune &&
	       Binding_Capture(binding->rune, binding->link_index, &current,
	           owned_execution) &&
	       current.link == binding->link && current.plan == binding->plan &&
	       current.entry_node == binding->entry_node &&
	       current.mover_node == binding->mover_node &&
	       current.destination_node == binding->destination_node &&
	       current.egress_node == binding->egress_node &&
	       current.entry_entity == binding->entry_entity &&
	       current.mover_entity == binding->mover_entity &&
	       current.destination_entity == binding->destination_entity &&
	       current.egress_entity == binding->egress_entity;
}

int SG_RuneMechanismBindingCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	return Binding_Current(binding, 1);
}

int SG_RuneMechanismBindingTopologyCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	return Binding_Current(binding, 0);
}

const rune_mechanism_edge_t *SG_RuneMechanismBindingEdgeAt(
	const sg_rune_mechanism_binding_t *binding, uint32_t edge_ordinal)
{
	if (!binding || !binding->rune || !binding->plan ||
	    edge_ordinal >= binding->plan->num_edges)
		return NULL;
	return &binding->rune->mechanism_edges[
		binding->plan->first_edge + edge_ordinal];
}

static struct edict_s *Binding_ResolveNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key,
	int owned_execution)
{
	const rune_mechanism_node_t *node;

	if (!binding || !binding->rune ||
	    !Binding_Current(binding, owned_execution))
		return NULL;
	node = SG_RuneMechanismNodeByKey(binding->rune, key);
	if (!node || !binding->plan ||
	    !(owned_execution
	        ? SG_MechCatalogEntityExecutionMatches(key, node,
	              binding->plan->controller_kind)
	        : SG_MechCatalogEntityTopologyMatches(key, node)))
		return NULL;
	return SG_MechCatalogResolveEntity(key, node);
}

struct edict_s *SG_RuneMechanismBindingResolveNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key)
{
	return Binding_ResolveNode(binding, key, 1);
}

struct edict_s *SG_RuneMechanismBindingResolveTopologyNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key)
{
	return Binding_ResolveNode(binding, key, 0);
}

struct edict_s *SG_RuneMechanismBindingResolveDestination(
	const sg_rune_mechanism_binding_t *binding)
{
	const rune_mechanism_node_t *destination;

	if (!binding || !SG_RuneMechanismBindingCurrent(binding) ||
	    binding->plan->controller_kind != SG_MECHANISM_CONTROLLER_TELEPORT ||
	    !(destination = Binding_TeleportDestination(binding->rune,
	        binding->plan)))
		return NULL;
	return SG_RuneMechanismBindingResolveNode(binding, destination->key);
}

static int Binding_NodeInClosure(const sg_rune_mechanism_binding_t *binding,
	uint32_t key)
{
	uint32_t ordinal;

	if (!binding || !binding->plan)
		return 0;
	if (binding->plan->entry_key == key || binding->plan->mover_key == key)
		return 1;
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);

		if (edge && (edge->from_key == key || edge->to_key == key))
			return 1;
	}
	return 0;
}

static const rune_mechanism_edge_t *Binding_TargetEdgeAt(
	const sg_rune_mechanism_binding_t *binding, uint32_t source_key,
	uint32_t target_ordinal)
{
	const rune_mechanism_edge_t *match = NULL;
	uint32_t ordinal;

	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);

		if (edge && edge->from_key == source_key &&
		    edge->kind == SG_MECH_EDGE_TARGET &&
		    edge->ordinal == target_ordinal)
		{
			if (match)
				return NULL;
			match = edge;
		}
	}
	return match;
}

int SG_RuneMechanismBindingDispatchTargets(
	const sg_rune_mechanism_binding_t *binding, uint32_t source_key,
	sg_rune_mechanism_target_visitor_fn visitor, void *context)
{
	const rune_mechanism_node_t *source;
	uint32_t target_count = 0U;
	uint32_t ordinal;

	if (!binding || !visitor || !SG_RuneMechanismBindingCurrent(binding) ||
	    !Binding_NodeInClosure(binding, source_key) ||
	    !(source = SG_RuneMechanismNodeByKey(binding->rune, source_key)))
		return 0;
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);

		if (!edge)
			return 0;
		if (edge->from_key == source_key &&
		    edge->kind == SG_MECH_EDGE_KILLTARGET)
			return 0;
		if (edge->from_key == source_key &&
		    edge->kind == SG_MECH_EDGE_TARGET)
			target_count++;
	}
	if ((source->target_offset == 0U) != (target_count == 0U))
		return 0;
	/* Validate the entire fanout before the first observable callback. */
	for (ordinal = 0U; ordinal < target_count; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			Binding_TargetEdgeAt(binding, source_key, ordinal);
		uint32_t prior;

		if (!edge || edge->to_key == source_key ||
		    !SG_RuneMechanismBindingResolveNode(binding, edge->to_key))
			return 0;
		for (prior = 0U; prior < ordinal; prior++)
		{
			const rune_mechanism_edge_t *previous =
				Binding_TargetEdgeAt(binding, source_key, prior);

			if (!previous || previous->to_key == edge->to_key)
				return 0;
		}
	}
	for (ordinal = 0U; ordinal < target_count; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			Binding_TargetEdgeAt(binding, source_key, ordinal);
		struct edict_s *target;

		if (!edge || !(target = SG_RuneMechanismBindingResolveNode(binding,
		        edge->to_key)) || !visitor(context, target, edge->to_key,
		        ordinal) || !SG_RuneMechanismBindingCurrent(binding))
			return 0;
	}
	return 1;
}

static int Binding_AddMover(const sg_rune_mechanism_binding_t *binding,
	const rune_mechanism_node_t *node,
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS], size_t *count)
{
	size_t cursor;

	if (!binding || !node || !keys || !count)
		return 0;
	/* A func_button is itself MOVETYPE_STOP, but a BUTTON_DOOR lease owns the
	 * authenticated door closure, not the switch brush.  Likewise side-effect
	 * movers in the callback closure never become door lease members. */
	if (binding->link->action == RL_TRAIN)
	{
		if (node->kind != SG_MECH_NODE_TRAIN)
			return 1;
	}
	else if (binding->link->action == RL_DOOR ||
	    binding->link->action == RL_BUTTON_DOOR)
	{
		if (!Binding_NodeDoorMover(node))
			return 1;
	}
	else if ((node->flags & SG_MECH_NODEF_MOVER) == 0U)
		return 1;
	for (cursor = 0U; cursor < *count; cursor++)
		if (keys[cursor] == node->key)
			return 1;
	if (*count >= SG_RUNE_BINDING_MAX_MOVERS)
		return 0;
	cursor = *count;
	while (cursor > 0U && keys[cursor - 1U] > node->key)
	{
		keys[cursor] = keys[cursor - 1U];
		cursor--;
	}
	keys[cursor] = node->key;
	(*count)++;
	return 1;
}

static int Binding_MoverKeys(const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out,
	int owned_execution)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count = 0U;
	uint32_t ordinal;

	if (key_count_out)
		*key_count_out = 0U;
	if (!binding || !keys_out || !key_count_out ||
	    !Binding_Current(binding, owned_execution))
		return 0;
	memset(keys, 0, sizeof(keys));
	if (binding->plan->controller_kind == SG_MECHANISM_CONTROLLER_PUSH)
		return 1;
	if (!Binding_AddMover(binding, binding->mover_node, keys, &count))
		return 0;
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);
		const rune_mechanism_node_t *from;
		const rune_mechanism_node_t *to;

		if (!edge)
			return 0;
		from = SG_RuneMechanismNodeByKey(binding->rune, edge->from_key);
		to = SG_RuneMechanismNodeByKey(binding->rune, edge->to_key);
		if (!Binding_AddMover(binding, from, keys, &count) ||
		    !Binding_AddMover(binding, to, keys, &count))
			return 0;
	}
	if (count == 0U ||
	    ((binding->link->action == RL_LIFT ||
	      binding->link->action == RL_TRAIN ||
	      binding->link->action == RL_DOOR ||
	      binding->link->action == RL_BUTTON_DOOR) &&
	     count != (size_t)binding->plan->expected_members))
		return 0;
	memcpy(keys_out, keys, count * sizeof(keys[0]));
	*key_count_out = count;
	return 1;
}

int SG_RuneMechanismBindingMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out)
{
	return Binding_MoverKeys(binding, keys_out, key_count_out, 1);
}

int SG_RuneMechanismBindingTopologyMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out)
{
	return Binding_MoverKeys(binding, keys_out, key_count_out, 0);
}

int SG_RuneMechanismBindingAuxDoorMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out)
{
	uint32_t all[SG_RUNE_BINDING_MAX_MOVERS];
	size_t all_count;
	size_t count = 0U;
	size_t index;

	if (key_count_out) *key_count_out = 0U;
	if (!binding || !keys_out || !key_count_out || !binding->plan ||
	    binding->plan->controller_kind != SG_MECHANISM_CONTROLLER_PLATFORM ||
	    binding->plan->expected_members <= 1U ||
	    !SG_RuneMechanismBindingMoverKeys(binding, all, &all_count))
		return 0;
	for (index = 0U; index < all_count; index++)
	{
		const rune_mechanism_node_t *node =
			SG_RuneMechanismNodeByKey(binding->rune, all[index]);

		if (all[index] == binding->mover_node->key)
			continue;
		if (!Binding_NodeDoorMover(node))
			return 0;
		keys_out[count++] = all[index];
	}
	if (count == 0U || count + 1U != binding->plan->expected_members)
		return 0;
	*key_count_out = count;
	return 1;
}

struct edict_s *SG_RuneMechanismBindingAuxTrigger(
	const sg_rune_mechanism_binding_t *binding)
{
	const rune_mechanism_node_t *best = NULL;
	uint32_t ordinal;

	if (!binding || !binding->plan ||
	    binding->plan->controller_kind != SG_MECHANISM_CONTROLLER_PLATFORM ||
	    binding->plan->expected_members <= 1U ||
	    !SG_RuneMechanismBindingCurrent(binding))
		return NULL;
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);
		const rune_mechanism_node_t *source;

		if (!edge || edge->kind != SG_MECH_EDGE_TARGET ||
		    edge->from_key == binding->entry_node->key)
			continue;
		source = SG_RuneMechanismNodeByKey(binding->rune, edge->from_key);
		if (source && source->kind == SG_MECH_NODE_TRIGGER &&
		    (!best || source->key < best->key))
			best = source;
	}
	return best ? SG_RuneMechanismBindingResolveNode(binding, best->key) : NULL;
}

int SG_RuneMechanismBindingAuxTriggerMatches(
	const sg_rune_mechanism_binding_t *binding, const struct edict_s *entity)
{
	uint32_t ordinal;

	if (!binding || !entity || !binding->plan ||
	    binding->plan->controller_kind != SG_MECHANISM_CONTROLLER_PLATFORM ||
	    binding->plan->expected_members <= 1U ||
	    !SG_RuneMechanismBindingCurrent(binding))
		return 0;
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);
		const rune_mechanism_node_t *source;

		if (!edge || edge->kind != SG_MECH_EDGE_TARGET ||
		    edge->from_key == binding->entry_node->key)
			continue;
		source = SG_RuneMechanismNodeByKey(binding->rune, edge->from_key);
		if (source && source->kind == SG_MECH_NODE_TRIGGER &&
		    SG_RuneMechanismBindingResolveNode(binding, source->key) == entity)
			return SG_RuneMechanismBindingCurrent(binding);
	}
	return 0;
}

static int Binding_CarrierTriggerContainsAnchor(
	const rune_mechanism_node_t *node, const rune_link_t *link)
{
	static const int hull_min_q8[3] = { -136, -136, -200 };
	static const int hull_max_q8[3] = { 136, 136, 264 };
	int axis;

	if (!node || !link || node->kind != SG_MECH_NODE_TRIGGER)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled = link->anchor[axis] * 8.0f;
		int anchor_q8;

		if (!isfinite(scaled) || scaled != (float)(int)scaled)
			return 0;
		anchor_q8 = (int)scaled;
		if (anchor_q8 + hull_max_q8[axis] <= node->absmin_q8[axis] ||
		    anchor_q8 + hull_min_q8[axis] >= node->absmax_q8[axis])
			return 0;
	}
	return 1;
}

static int Binding_CarrierTriggerClassMatches(
	const sg_rune_mechanism_binding_t *binding,
	const rune_mechanism_node_t *left, const rune_mechanism_node_t *right)
{
	uint32_t left_ordinal = 0U;
	uint32_t right_ordinal = 0U;
	uint32_t left_count = 0U;
	uint32_t right_count = 0U;

	if (!binding || !left || !right || left->kind != SG_MECH_NODE_TRIGGER ||
	    right->kind != SG_MECH_NODE_TRIGGER ||
	    left->delay_ms != right->delay_ms ||
	    left->target_offset != right->target_offset)
		return 0;
	while (left_ordinal < binding->plan->num_edges ||
	       right_ordinal < binding->plan->num_edges)
	{
		const rune_mechanism_edge_t *left_edge = NULL;
		const rune_mechanism_edge_t *right_edge = NULL;

		while (left_ordinal < binding->plan->num_edges)
		{
			left_edge = SG_RuneMechanismBindingEdgeAt(binding,
				left_ordinal++);
			if (left_edge && left_edge->kind == SG_MECH_EDGE_TARGET &&
			    left_edge->from_key == left->key)
				break;
			left_edge = NULL;
		}
		while (right_ordinal < binding->plan->num_edges)
		{
			right_edge = SG_RuneMechanismBindingEdgeAt(binding,
				right_ordinal++);
			if (right_edge && right_edge->kind == SG_MECH_EDGE_TARGET &&
			    right_edge->from_key == right->key)
				break;
			right_edge = NULL;
		}
		if (!left_edge || !right_edge)
			return left_edge == right_edge && left_count != 0U &&
			       left_count == right_count;
		if (left_edge->to_key != right_edge->to_key ||
		    left_edge->ordinal != right_edge->ordinal ||
		    left_edge->delay_ms != right_edge->delay_ms)
			return 0;
		left_count++;
		right_count++;
	}
	return left_count != 0U && left_count == right_count;
}

static const rune_mechanism_node_t *Binding_CarrierApproachTrigger(
	const sg_rune_mechanism_binding_t *binding)
{
	const rune_mechanism_node_t *approach = NULL;
	uint32_t ordinal;

	if (!binding || !binding->link || !binding->plan)
		return NULL;
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);
		const rune_mechanism_node_t *source;

		if (!edge || edge->kind != SG_MECH_EDGE_TARGET ||
		    edge->from_key == binding->entry_node->key)
			continue;
		source = SG_RuneMechanismNodeByKey(binding->rune, edge->from_key);
		if (!Binding_CarrierTriggerContainsAnchor(source, binding->link))
			continue;
		if (approach &&
		    !Binding_CarrierTriggerClassMatches(binding, approach, source))
			return NULL;
		if (!approach || source->key < approach->key)
			approach = source;
	}
	return approach;
}

static int Binding_CarrierStageNode(
	const sg_rune_mechanism_binding_t *binding,
	const rune_mechanism_node_t *node, sg_carrier_door_stage_t stage)
{
	const rune_mechanism_node_t *approach =
		Binding_CarrierApproachTrigger(binding);
	int same;

	if (!approach || !node || node->kind != SG_MECH_NODE_TRIGGER)
		return 0;
	same = Binding_CarrierTriggerClassMatches(binding, approach, node);
	return stage == SG_CARRIER_DOOR_APPROACH ? same : !same;
}

int SG_RuneMechanismBindingCarrierStage(
	const sg_rune_mechanism_binding_t *binding,
	sg_carrier_door_stage_t stage, struct edict_s **trigger_out,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out,
	uint32_t *delay_ms_out)
{
	const rune_mechanism_node_t *trigger = NULL;
	uint32_t master_keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t master_count = 0U;
	size_t key_count = 0U;
	uint32_t ordinal;

	if (trigger_out) *trigger_out = NULL;
	if (key_count_out) *key_count_out = 0U;
	if (delay_ms_out) *delay_ms_out = 0U;
	if (!binding || !trigger_out || !keys_out || !key_count_out ||
	    !delay_ms_out || !binding->plan ||
	    binding->plan->controller_kind != SG_MECHANISM_CONTROLLER_PLATFORM ||
	    binding->plan->expected_members <= 1U ||
	    (stage != SG_CARRIER_DOOR_APPROACH &&
	     stage != SG_CARRIER_DOOR_EGRESS) ||
	    !SG_RuneMechanismBindingCurrent(binding))
		return 0;
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);
		const rune_mechanism_node_t *source;
		const rune_mechanism_node_t *destination;
		uint32_t master_key;
		size_t index;

		if (!edge || edge->kind != SG_MECH_EDGE_TARGET ||
		    edge->from_key == binding->entry_node->key)
			continue;
		source = SG_RuneMechanismNodeByKey(binding->rune, edge->from_key);
		if (!Binding_CarrierStageNode(binding, source, stage))
			continue;
		if ((uint32_t)source->delay_ms != edge->delay_ms)
			return 0;
		if (!trigger || source->key < trigger->key)
			trigger = source;
		destination = SG_RuneMechanismNodeByKey(binding->rune,
			edge->to_key);
		if (!Binding_NodeDoorMover(destination))
			return 0;
		master_key = destination->kind == SG_MECH_NODE_DOOR_MASTER
			? destination->key : destination->team_master_key;
		for (index = 0U; index < master_count; index++)
			if (master_keys[index] == master_key)
				break;
		if (index == master_count)
		{
			if (master_count >= SG_RUNE_BINDING_MAX_MOVERS)
				return 0;
			master_keys[master_count++] = master_key;
		}
	}
	if (!trigger || master_count == 0U)
		return 0;
	for (ordinal = 0U; ordinal < binding->rune->artifact.num_mechanism_nodes;
	     ordinal++)
	{
		const rune_mechanism_node_t *node =
			&binding->rune->mechanism_nodes[ordinal];
		uint32_t master_key;
		size_t index;

		if (!Binding_NodeDoorMover(node))
			continue;
		master_key = node->kind == SG_MECH_NODE_DOOR_MASTER
			? node->key : node->team_master_key;
		for (index = 0U; index < master_count; index++)
			if (master_keys[index] == master_key)
				break;
		if (index == master_count)
			continue;
		if (!Binding_NodeInClosure(binding, node->key) ||
		    key_count >= SG_RUNE_BINDING_MAX_MOVERS)
			return 0;
		keys_out[key_count++] = node->key;
	}
	if (key_count == 0U ||
	    !SG_RuneMechanismBindingCurrent(binding))
		return 0;
	*trigger_out = SG_RuneMechanismBindingResolveNode(binding, trigger->key);
	if (!*trigger_out)
		return 0;
	*key_count_out = key_count;
	*delay_ms_out = (uint32_t)trigger->delay_ms;
	return 1;
}

int SG_RuneMechanismBindingCarrierStageTriggerMatches(
	const sg_rune_mechanism_binding_t *binding,
	sg_carrier_door_stage_t stage, const struct edict_s *entity)
{
	uint32_t ordinal;

	if (!entity || !binding || !binding->plan ||
	    !SG_RuneMechanismBindingCurrent(binding))
		return 0;
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);
		const rune_mechanism_node_t *source;

		if (!edge || edge->kind != SG_MECH_EDGE_TARGET ||
		    edge->from_key == binding->entry_node->key)
			continue;
		source = SG_RuneMechanismNodeByKey(binding->rune, edge->from_key);
		if (Binding_CarrierStageNode(binding, source, stage) &&
		    SG_RuneMechanismBindingResolveNode(binding, source->key) == entity)
			return SG_RuneMechanismBindingCurrent(binding);
	}
	return 0;
}

int SG_RuneMechanismBindingsReady(const rune_t *rune,
	uint32_t *failure_index_out)
{
	uint32_t index;

	if (failure_index_out)
		*failure_index_out = UINT32_MAX;
	if (!rune || !SG_RunePublishedShapeValid(rune))
		return 0;
	for (index = 0U; index < rune->artifact.num_links; index++)
	{
		const rune_link_t *link = &rune->links[index];
		sg_rune_mechanism_binding_t binding;

		if (!SG_ActionMechanismPlanRequired((int)link->action))
			continue;
		if (!SG_RuneMechanismBindingCapture(rune, index, &binding) ||
		    binding.link != link || binding.link_index != index ||
		    !SG_RuneMechanismBindingTopologyCurrent(&binding))
		{
			if (failure_index_out)
				*failure_index_out = index;
			return 0;
		}
	}
	return 1;
}

int SG_RuneMechanismBindingDoorAction(
	const sg_rune_mechanism_binding_t *binding)
{
	return binding && binding->link &&
	       (binding->link->action == RL_DOOR ||
	        binding->link->action == RL_BUTTON_DOOR) &&
	       SG_RuneMechanismBindingCurrent(binding);
}
