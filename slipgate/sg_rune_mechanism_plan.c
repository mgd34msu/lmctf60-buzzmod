/* sg_rune_mechanism_plan.c -- exact post-prune mechanism plan materializer. */
#include "../q_shared.h"
#include "sg_rune_mechanism_plan.h"
#include "sg_crc32.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

typedef struct mechanism_edge_group_s
{
	uint32_t first;
	uint32_t count;
} mechanism_edge_group_t;

typedef struct mechanism_materializer_s
{
	rune_link_t *links;
	uint32_t num_links;
	const sg_mechanism_plan_binding_t *bindings;
	uint32_t num_bindings;
	const sg_mech_catalog_view_t *catalog;
	sg_mechanism_plan_buffers_t *buffers;
	sg_mechanism_plan_result_t result;
	uint32_t generation;
	uint32_t first_plan_edge;
	uint32_t master_indices[RUNE_MAX_MECHANISM_MEMBERS];
	uint32_t master_count;
	uint32_t relay_count;
} mechanism_materializer_t;

static int Mechanism_Fail(mechanism_materializer_t *state,
	sg_mechanism_plan_diagnostic_t diagnostic, uint32_t link_index,
	uint32_t plan_index)
{
	state->result.diagnostic = diagnostic;
	state->result.link_index = link_index;
	state->result.plan_index = plan_index;
	return 0;
}

static uint32_t Mechanism_FindNode(const mechanism_materializer_t *state,
	uint32_t key)
{
	uint32_t low = 0U;
	uint32_t high = state->catalog->num_nodes;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (state->catalog->nodes[middle].key < key)
			low = middle + 1U;
		else
			high = middle;
	}
	return low < state->catalog->num_nodes &&
	       state->catalog->nodes[low].key == key ? low : UINT32_MAX;
}

static int Mechanism_EdgeBefore(const rune_mechanism_edge_t *edge,
	uint32_t from_key, uint16_t kind)
{
	return edge->from_key < from_key ||
	       (edge->from_key == from_key && edge->kind < kind);
}

static mechanism_edge_group_t Mechanism_EdgeGroup(
	const mechanism_materializer_t *state, uint32_t from_key, uint16_t kind)
{
	mechanism_edge_group_t group;
	uint32_t low = 0U;
	uint32_t high = state->catalog->num_edges;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (Mechanism_EdgeBefore(&state->catalog->edges[middle],
		    from_key, kind))
			low = middle + 1U;
		else
			high = middle;
	}
	group.first = low;
	while (low < state->catalog->num_edges &&
	       state->catalog->edges[low].from_key == from_key &&
	       state->catalog->edges[low].kind == kind)
		low++;
	group.count = low - group.first;
	return group;
}

static int Mechanism_NodeExecutable(const rune_mechanism_node_t *node)
{
	return node && (node->flags & SG_MECH_NODEF_INVENTORY_ONLY) == 0U &&
	       node->touch_callback != SG_MECH_CALLBACK_UNKNOWN &&
	       node->use_callback != SG_MECH_CALLBACK_UNKNOWN &&
	       node->think_callback != SG_MECH_CALLBACK_UNKNOWN &&
	       node->blocked_callback != SG_MECH_CALLBACK_UNKNOWN;
}

static int Mechanism_SafeSpeaker(const rune_mechanism_node_t *node)
{
	return Mechanism_NodeExecutable(node) &&
	       node->kind == SG_MECH_NODE_TARGET_SPEAKER &&
	       node->use_callback == SG_MECH_CALLBACK_USE_TARGET_SPEAKER &&
	       node->touch_callback == SG_MECH_CALLBACK_NONE &&
	       node->think_callback == SG_MECH_CALLBACK_NONE &&
	       node->blocked_callback == SG_MECH_CALLBACK_NONE &&
	       (node->spawnflags & UINT32_C(3)) == 0U &&
	       node->target_offset == 0U && node->killtarget_offset == 0U &&
	       node->path_target_offset == 0U;
}

static int Mechanism_SafeAreaportal(const rune_mechanism_node_t *node)
{
	return Mechanism_NodeExecutable(node) &&
	       node->kind == SG_MECH_NODE_AREAPORTAL &&
	       node->use_callback == SG_MECH_CALLBACK_USE_AREAPORTAL &&
	       node->touch_callback == SG_MECH_CALLBACK_NONE &&
	       node->think_callback == SG_MECH_CALLBACK_NONE &&
	       node->blocked_callback == SG_MECH_CALLBACK_NONE &&
	       node->target_offset == 0U && node->killtarget_offset == 0U &&
	       node->path_target_offset == 0U;
}

static int Mechanism_RelayShape(const rune_mechanism_node_t *node)
{
	return Mechanism_NodeExecutable(node) &&
	       node->kind == SG_MECH_NODE_RELAY &&
	       node->use_callback == SG_MECH_CALLBACK_USE_TRIGGER_RELAY &&
	       node->touch_callback == SG_MECH_CALLBACK_NONE &&
	       node->think_callback == SG_MECH_CALLBACK_NONE &&
	       node->blocked_callback == SG_MECH_CALLBACK_NONE &&
	       node->delay_ms >= 0 && node->target_offset != 0U &&
	       node->killtarget_offset == 0U &&
	       node->path_target_offset == 0U;
}

static int Mechanism_SafeRelay(const rune_mechanism_node_t *node)
{
	return Mechanism_RelayShape(node) && node->delay_ms == 0;
}

/* A positive-delay sound relay is intentionally invoked but never expanded:
 * SG_HandleMechanismTargets consumes that bound source before G_UseTargets can
 * allocate DelayedUse.  Prove the omitted inventory suffix is audio-only so
 * this execution law cannot hide a later mover, killtarget, or mutable target.
 * Match the live oracle's depth-four fail-closed relay bound. */
static int Mechanism_DelayedSoundOnlyRelay(
	const mechanism_materializer_t *state, uint32_t node_index, int depth)
{
	const rune_mechanism_node_t *node;
	mechanism_edge_group_t target;
	uint32_t i;

	if (!state || node_index >= state->catalog->num_nodes || depth > 4)
		return 0;
	node = &state->catalog->nodes[node_index];
	if (!Mechanism_RelayShape(node) || node->delay_ms < 0)
		return 0;
	target = Mechanism_EdgeGroup(state, node->key, SG_MECH_EDGE_TARGET);
	if (target.count == 0U)
		return 0;
	for (i = 0U; i < target.count; i++)
	{
		const rune_mechanism_edge_t *edge =
			&state->catalog->edges[target.first + i];
		uint32_t destination_index;

		if (edge->delay_ms != (uint32_t)node->delay_ms)
			return 0;
		destination_index = Mechanism_FindNode(state, edge->to_key);
		if (destination_index == UINT32_MAX)
			return 0;
		if (Mechanism_SafeSpeaker(
		        &state->catalog->nodes[destination_index]))
			continue;
		if (!Mechanism_DelayedSoundOnlyRelay(state, destination_index,
		        depth + 1))
			return 0;
	}
	return 1;
}

static int Mechanism_AppendInventoryEdge(mechanism_materializer_t *state,
	uint32_t inventory_index)
{
	uint32_t plan_edges = state->result.num_edges - state->first_plan_edge;

	if (inventory_index >= state->catalog->num_edges ||
	    state->catalog->edges[inventory_index].delay_ms != 0U ||
	    state->buffers->edge_marks[inventory_index] == state->generation ||
	    state->result.num_edges >= state->buffers->edge_capacity ||
	    plan_edges >= RUNE_MAX_MECHANISM_PLAN_EDGES)
		return 0;
	state->buffers->edge_marks[inventory_index] = state->generation;
	state->buffers->edges[state->result.num_edges++] =
		state->catalog->edges[inventory_index];
	return 1;
}

static int Mechanism_AddMaster(mechanism_materializer_t *state,
	uint32_t node_index)
{
	uint32_t i;

	if (node_index >= state->catalog->num_nodes ||
	    state->catalog->nodes[node_index].kind != SG_MECH_NODE_DOOR_MASTER ||
	    state->master_count >= RUNE_MAX_MECHANISM_MEMBERS)
		return 0;
	for (i = 0U; i < state->master_count; i++)
		if (state->master_indices[i] == node_index)
			return 0;
	state->master_indices[state->master_count++] = node_index;
	return 1;
}

static int Mechanism_AddSideEffect(mechanism_materializer_t *state,
	uint32_t inventory_index, int allow_areaportal)
{
	const rune_mechanism_edge_t *edge;
	const rune_mechanism_node_t *destination;
	uint32_t destination_index;

	if (inventory_index >= state->catalog->num_edges)
		return 0;
	edge = &state->catalog->edges[inventory_index];
	if (edge->kind != SG_MECH_EDGE_TARGET || edge->delay_ms != 0U ||
	    !Mechanism_AppendInventoryEdge(state, inventory_index))
		return 0;
	destination_index = Mechanism_FindNode(state, edge->to_key);
	if (destination_index == UINT32_MAX)
		return 0;
	destination = &state->catalog->nodes[destination_index];
	if (Mechanism_SafeSpeaker(destination) ||
	    (allow_areaportal && Mechanism_SafeAreaportal(destination)))
		return 1;
	/* Keep this inbound edge in the executable plan so original G_Find target
	 * ordinals remain contiguous.  The delayed relay itself is a terminal: its
	 * outgoing delayed inventory edges are authenticated as sound-only above
	 * but deliberately absent from the synchronous plan closure. */
	if (destination->delay_ms > 0 &&
	    Mechanism_DelayedSoundOnlyRelay(state, destination_index, 1))
		return 1;
	if (!Mechanism_SafeRelay(destination) ||
	    state->buffers->node_marks[destination_index] == state->generation ||
	    state->relay_count >= state->buffers->node_queue_capacity)
		return 0;
	state->buffers->node_marks[destination_index] = state->generation;
	state->buffers->node_queue[state->relay_count++] = destination_index;
	return 1;
}

static int Mechanism_MaterializePlatform(mechanism_materializer_t *state,
	const sg_mechanism_plan_binding_t *binding)
{
	mechanism_edge_group_t owner = Mechanism_EdgeGroup(state,
		binding->entry_key, SG_MECH_EDGE_OWNER);
	uint32_t entry = Mechanism_FindNode(state, binding->entry_key);
	uint32_t mover = Mechanism_FindNode(state, binding->mover_key);

	return entry != UINT32_MAX && mover != UINT32_MAX &&
	       state->catalog->nodes[entry].kind == SG_MECH_NODE_PLATFORM_TRIGGER &&
	       state->catalog->nodes[entry].touch_callback ==
	           SG_MECH_CALLBACK_TOUCH_PLAT_CENTER &&
	       (state->catalog->nodes[entry].flags & SG_MECH_NODEF_SYNTHETIC) != 0U &&
	       state->catalog->nodes[mover].kind == SG_MECH_NODE_PLATFORM &&
	       binding->destination_key == SG_MECH_NO_KEY &&
	       binding->expected_members == 1U && binding->cooldown_ms == 0U &&
	       owner.count == 1U &&
	       state->catalog->edges[owner.first].to_key == binding->mover_key &&
	       Mechanism_AppendInventoryEdge(state, owner.first);
}

static int Mechanism_MaterializeTeleport(mechanism_materializer_t *state,
	const sg_mechanism_plan_binding_t *binding)
{
	mechanism_edge_group_t owner = Mechanism_EdgeGroup(state,
		binding->entry_key, SG_MECH_EDGE_OWNER);
	mechanism_edge_group_t target = Mechanism_EdgeGroup(state,
		binding->entry_key, SG_MECH_EDGE_TARGET);
	uint32_t entry = Mechanism_FindNode(state, binding->entry_key);
	uint32_t mover = Mechanism_FindNode(state, binding->mover_key);
	uint32_t destination = Mechanism_FindNode(state, binding->destination_key);

	return entry != UINT32_MAX && mover != UINT32_MAX &&
	       destination != UINT32_MAX &&
	       state->catalog->nodes[entry].kind == SG_MECH_NODE_TELEPORT_TRIGGER &&
	       state->catalog->nodes[entry].touch_callback ==
	           SG_MECH_CALLBACK_TELEPORTER_TOUCH &&
	       (state->catalog->nodes[entry].flags & SG_MECH_NODEF_SYNTHETIC) != 0U &&
	       state->catalog->nodes[mover].kind == SG_MECH_NODE_TELEPORTER &&
	       Mechanism_NodeExecutable(&state->catalog->nodes[destination]) &&
	       state->catalog->nodes[destination].kind == SG_MECH_NODE_TELEPORT_DEST &&
	       binding->expected_members == 1U && binding->cooldown_ms == 0U &&
	       owner.count == 1U && target.count == 1U &&
	       state->catalog->edges[owner.first].to_key == binding->mover_key &&
	       state->catalog->edges[target.first].to_key ==
	           binding->destination_key &&
	       Mechanism_AppendInventoryEdge(state, owner.first) &&
	       Mechanism_AppendInventoryEdge(state, target.first);
}

static int Mechanism_MaterializeDoorEntry(mechanism_materializer_t *state,
	const sg_mechanism_plan_binding_t *binding)
{
	const rune_mechanism_node_t *entry;
	uint32_t entry_index = Mechanism_FindNode(state, binding->entry_key);
	uint32_t mover_index = Mechanism_FindNode(state, binding->mover_key);
	uint32_t i;

	if (entry_index == UINT32_MAX || mover_index == UINT32_MAX ||
	    binding->destination_key != SG_MECH_NO_KEY)
		return 0;
	entry = &state->catalog->nodes[entry_index];
	switch (binding->controller_kind)
	{
	case SG_MECHANISM_CONTROLLER_AUTO_DOOR:
	{
		mechanism_edge_group_t owner = Mechanism_EdgeGroup(state,
			binding->entry_key, SG_MECH_EDGE_OWNER);

		if (entry->kind != SG_MECH_NODE_AUTO_DOOR_TRIGGER ||
		    entry->touch_callback != SG_MECH_CALLBACK_TOUCH_DOOR_TRIGGER ||
		    (entry->flags & SG_MECH_NODEF_SYNTHETIC) == 0U ||
		    binding->cooldown_ms != 1000U || owner.count != 1U ||
		    state->catalog->edges[owner.first].to_key != binding->mover_key ||
		    !Mechanism_AppendInventoryEdge(state, owner.first) ||
		    !Mechanism_AddMaster(state, mover_index))
			return 0;
		break;
	}

	case SG_MECHANISM_CONTROLLER_BUTTON_DOOR:
	{
		mechanism_edge_group_t target = Mechanism_EdgeGroup(state,
			binding->entry_key, SG_MECH_EDGE_TARGET);

		if (entry->kind != SG_MECH_NODE_BUTTON ||
		    entry->touch_callback != SG_MECH_CALLBACK_BUTTON_TOUCH ||
		    entry->use_callback != SG_MECH_CALLBACK_BUTTON_USE ||
		    (entry->flags & (SG_MECH_NODEF_SHOOTABLE |
		     SG_MECH_NODEF_FRAME_COMPLETE_MOVER)) != 0U ||
		    entry->wait_ms <= 0 ||
		    binding->cooldown_ms != (uint32_t)entry->wait_ms ||
		    target.count == 0U)
			return 0;
		/* G_UseTargets walks matching targetnames in edict order.  One button
		 * may therefore call its canonical master followed by same-team slaves;
		 * the master opens the whole team and those later slave calls are stock
		 * no-ops.  Preserve the complete ordered fanout in the authenticated
		 * closure, while admitting exactly one physical door team. */
		for (i = 0U; i < target.count; i++)
		{
			uint32_t edge_index = target.first + i;
			uint32_t destination_index = Mechanism_FindNode(state,
				state->catalog->edges[edge_index].to_key);
			const rune_mechanism_node_t *destination;

			if (destination_index == UINT32_MAX)
				return 0;
			destination = &state->catalog->nodes[destination_index];
			if (destination->kind == SG_MECH_NODE_DOOR_MASTER)
			{
				if (state->master_count != 0U ||
				    destination->key != binding->mover_key ||
				    !Mechanism_AppendInventoryEdge(state, edge_index) ||
				    !Mechanism_AddMaster(state, destination_index))
					return 0;
			}
			else if (destination->kind == SG_MECH_NODE_DOOR_MEMBER)
			{
				if (state->master_count != 1U ||
				    destination->team_master_key != binding->mover_key ||
				    !Mechanism_AppendInventoryEdge(state, edge_index))
					return 0;
			}
			else
				return 0;
		}
		if (state->master_count != 1U)
			return 0;
		break;
	}

	case SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR:
	{
		mechanism_edge_group_t target = Mechanism_EdgeGroup(state,
			binding->entry_key, SG_MECH_EDGE_TARGET);
		uint32_t cooldown;
		uint32_t smallest = UINT32_MAX;

		cooldown = entry->wait_ms > RUNE_MAX_COST_MS
		    ? (uint32_t)RUNE_MAX_COST_MS : (uint32_t)entry->wait_ms;

		if (entry->kind != SG_MECH_NODE_TRIGGER ||
		    entry->touch_callback != SG_MECH_CALLBACK_TOUCH_MULTI ||
		    (entry->flags & SG_MECH_NODEF_REPEATABLE) == 0U ||
		    entry->delay_ms != 0 || entry->wait_ms <= 0 ||
		    entry->killtarget_offset != 0U ||
		    entry->path_target_offset != 0U || target.count == 0U ||
		    binding->cooldown_ms != cooldown)
			return 0;
		for (i = 0U; i < target.count; i++)
		{
			uint32_t edge_index = target.first + i;
			uint32_t destination_index = Mechanism_FindNode(state,
				state->catalog->edges[edge_index].to_key);
			const rune_mechanism_node_t *destination;

			if (destination_index == UINT32_MAX)
				return 0;
			destination = &state->catalog->nodes[destination_index];
			if (destination->kind == SG_MECH_NODE_DOOR_MASTER)
			{
				if (!Mechanism_AppendInventoryEdge(state, edge_index) ||
				    !Mechanism_AddMaster(state, destination_index))
					return 0;
				if (destination->key < smallest)
					smallest = destination->key;
			}
			else if (destination->kind == SG_MECH_NODE_DOOR_MEMBER)
			{
				int seen_master = 0;
				uint32_t j;

				for (j = 0U; j < state->master_count; j++)
					if (state->catalog->nodes[
					    state->master_indices[j]].key ==
					    destination->team_master_key)
						seen_master = 1;
				if (!seen_master ||
				    !Mechanism_AppendInventoryEdge(state, edge_index))
					return 0;
			}
			else if (!Mechanism_AddSideEffect(state, edge_index, 0))
				return 0;
		}
		if (state->master_count == 0U || smallest != binding->mover_key)
			return 0;
		break;
	}

	default:
		return 0;
	}
	return 1;
}

static int Mechanism_MaterializeDoorClosure(mechanism_materializer_t *state,
	const sg_mechanism_plan_binding_t *binding)
{
	uint32_t physical_count = 0U;
	uint32_t i;
	uint32_t j;

	for (i = 0U; i < state->master_count; i++)
	{
		const rune_mechanism_node_t *master =
			&state->catalog->nodes[state->master_indices[i]];
		mechanism_edge_group_t team = Mechanism_EdgeGroup(state,
			master->key, SG_MECH_EDGE_TEAM);
		uint32_t actual_members = 0U;

		if (!Mechanism_NodeExecutable(master) ||
		    master->kind != SG_MECH_NODE_DOOR_MASTER ||
		    master->team_master_key != master->key)
			return 0;
		physical_count++;
		for (j = 0U; j < state->catalog->num_nodes; j++)
			if (state->catalog->nodes[j].kind == SG_MECH_NODE_DOOR_MEMBER &&
			    state->catalog->nodes[j].team_master_key == master->key)
				actual_members++;
		if (team.count != actual_members)
			return 0;
		for (j = 0U; j < team.count; j++)
		{
			uint32_t member_index = Mechanism_FindNode(state,
				state->catalog->edges[team.first + j].to_key);

			if (member_index == UINT32_MAX ||
			    !Mechanism_NodeExecutable(
			        &state->catalog->nodes[member_index]) ||
			    state->catalog->nodes[member_index].kind !=
			        SG_MECH_NODE_DOOR_MEMBER ||
			    state->catalog->nodes[member_index].team_master_key !=
			        master->key ||
			    !Mechanism_AppendInventoryEdge(state, team.first + j))
				return 0;
			physical_count++;
		}
	}
	if (physical_count != binding->expected_members)
		return 0;

	/* Door members call G_UseTargets after movement. Preserve every admitted
	 * speaker/areaportal/sound-only relay edge in stable node/fanout order. */
	for (i = 0U; i < state->catalog->num_nodes; i++)
	{
		const rune_mechanism_node_t *node = &state->catalog->nodes[i];
		int physical = 0;
		mechanism_edge_group_t target;

		for (j = 0U; j < state->master_count; j++)
		{
			uint32_t master_key = state->catalog->nodes[
				state->master_indices[j]].key;

			if (node->key == master_key ||
			    (node->kind == SG_MECH_NODE_DOOR_MEMBER &&
			     node->team_master_key == master_key))
				physical = 1;
		}
		if (!physical)
			continue;
		target = Mechanism_EdgeGroup(state, node->key, SG_MECH_EDGE_TARGET);
		for (j = 0U; j < target.count; j++)
			if (!Mechanism_AddSideEffect(state, target.first + j, 1))
				return 0;
	}

	for (i = 0U; i < state->relay_count; i++)
	{
		uint32_t relay_index = state->buffers->node_queue[i];
		mechanism_edge_group_t target = Mechanism_EdgeGroup(state,
			state->catalog->nodes[relay_index].key, SG_MECH_EDGE_TARGET);

		if (target.count == 0U)
			return 0;
		for (j = 0U; j < target.count; j++)
			if (!Mechanism_AddSideEffect(state, target.first + j, 0))
				return 0;
	}
	return 1;
}

static void Mechanism_PutU16(unsigned char *out, uint16_t value)
{
	out[0] = (unsigned char)(value & UINT16_C(0xff));
	out[1] = (unsigned char)(value >> 8);
}

static void Mechanism_PutU32(unsigned char *out, uint32_t value)
{
	out[0] = (unsigned char)(value & UINT32_C(0xff));
	out[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	out[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	out[3] = (unsigned char)(value >> 24);
}

static int Mechanism_ClosureCRC32(const rune_mechanism_edge_t *edges,
	uint32_t first, uint32_t count, uint32_t total, uint32_t *crc_out)
{
	unsigned char encoded[16];
	uint32_t state;
	uint32_t i;

	if (!edges || !crc_out || count == 0U ||
	    count > RUNE_MAX_MECHANISM_PLAN_EDGES || first > total ||
	    count > total - first)
		return 0;
	state = SG_CRC32Init();
	for (i = 0U; i < count; i++)
	{
		const rune_mechanism_edge_t *edge = &edges[first + i];

		Mechanism_PutU32(encoded + 0, edge->from_key);
		Mechanism_PutU32(encoded + 4, edge->to_key);
		Mechanism_PutU16(encoded + 8, edge->kind);
		Mechanism_PutU16(encoded + 10, edge->ordinal);
		Mechanism_PutU32(encoded + 12, edge->delay_ms);
		if (!SG_CRC32Update(&state, encoded, sizeof(encoded)))
			return 0;
	}
	*crc_out = SG_CRC32Final(state);
	return *crc_out != 0U;
}

static int Mechanism_MaterializeOne(mechanism_materializer_t *state,
	uint32_t link_index, uint32_t plan_index)
{
	const sg_mechanism_plan_binding_t *binding = &state->bindings[plan_index];
	rune_mechanism_plan_t *plan = &state->buffers->plans[plan_index];
	uint32_t entry_index = Mechanism_FindNode(state, binding->entry_key);
	uint32_t mover_index = Mechanism_FindNode(state, binding->mover_key);
	int closure_ok;

	state->generation = plan_index + 1U;
	state->first_plan_edge = state->result.num_edges;
	state->master_count = 0U;
	state->relay_count = 0U;
	if (entry_index == UINT32_MAX || mover_index == UINT32_MAX ||
	    !Mechanism_NodeExecutable(&state->catalog->nodes[entry_index]) ||
	    !Mechanism_NodeExecutable(&state->catalog->nodes[mover_index]))
		return Mechanism_Fail(state, SG_MECHANISM_PLAN_BAD_BINDING,
			link_index, plan_index);

	switch (binding->controller_kind)
	{
	case SG_MECHANISM_CONTROLLER_PLATFORM:
		closure_ok = Mechanism_MaterializePlatform(state, binding);
		break;
	case SG_MECHANISM_CONTROLLER_TELEPORT:
		closure_ok = Mechanism_MaterializeTeleport(state, binding);
		break;
	case SG_MECHANISM_CONTROLLER_AUTO_DOOR:
	case SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR:
	case SG_MECHANISM_CONTROLLER_BUTTON_DOOR:
		closure_ok = Mechanism_MaterializeDoorEntry(state, binding) &&
			Mechanism_MaterializeDoorClosure(state, binding);
		break;
	default:
		closure_ok = 0;
		break;
	}
	if (!closure_ok || state->result.num_edges == state->first_plan_edge)
		return Mechanism_Fail(state, SG_MECHANISM_PLAN_BAD_CLOSURE,
			link_index, plan_index);

	memset(plan, 0, sizeof(*plan));
	plan->entry_key = binding->entry_key;
	plan->mover_key = binding->mover_key;
	plan->first_edge = state->first_plan_edge;
	plan->num_edges = state->result.num_edges - state->first_plan_edge;
	plan->controller_kind = binding->controller_kind;
	if (!SG_ActionMechanismPlanAllowed(state->links[link_index].action,
	        plan->controller_kind))
		return Mechanism_Fail(state, SG_MECHANISM_PLAN_BAD_BINDING,
			link_index, plan_index);
	plan->flags = SG_MechanismControllerPlanFlags(binding->controller_kind);
	plan->expected_members = binding->expected_members;
	plan->cooldown_ms = binding->cooldown_ms;
	if (plan->flags == 0U || !Mechanism_ClosureCRC32(state->buffers->edges,
	    plan->first_edge, plan->num_edges, state->result.num_edges,
	    &plan->closure_crc32))
		return Mechanism_Fail(state, SG_MECHANISM_PLAN_BAD_CRC,
			link_index, plan_index);
	return 1;
}

int SG_MechanismPlansMaterialize(rune_link_t *links, uint32_t num_links,
	const sg_mechanism_plan_binding_t *bindings, uint32_t num_bindings,
	const sg_mech_catalog_view_t *catalog,
	sg_mechanism_plan_buffers_t *buffers,
	sg_mechanism_plan_result_t *result_out)
{
	mechanism_materializer_t state;
	uint32_t required = 0U;
	uint32_t i;

	memset(&state, 0, sizeof(state));
	state.links = links;
	state.num_links = num_links;
	state.bindings = bindings;
	state.num_bindings = num_bindings;
	state.catalog = catalog;
	state.buffers = buffers;
	state.result.diagnostic = SG_MECHANISM_PLAN_INVALID_ARGUMENT;
	state.result.link_index = UINT32_MAX;
	state.result.plan_index = UINT32_MAX;
	if (result_out)
		*result_out = state.result;
	if ((!links && num_links != 0U) || (!bindings && num_bindings != 0U) ||
	    !catalog || !buffers ||
	    (!catalog->nodes && catalog->num_nodes != 0U) ||
	    (!catalog->edges && catalog->num_edges != 0U) ||
	    catalog->num_nodes > RUNE_MAX_MECHANISM_NODES ||
	    catalog->num_edges > RUNE_MAX_MECHANISM_EDGES ||
	    num_bindings > RUNE_MAX_MECHANISM_PLANS ||
	    (!buffers->edges && catalog->num_edges != 0U) ||
	    buffers->edge_capacity < catalog->num_edges ||
	    (!buffers->plans && num_bindings != 0U) ||
	    buffers->plan_capacity < num_bindings ||
	    (num_bindings != 0U && (!buffers->edge_marks ||
	     buffers->edge_mark_capacity < catalog->num_edges ||
	     !buffers->node_marks ||
	     buffers->node_mark_capacity < catalog->num_nodes ||
	     !buffers->node_queue ||
	     buffers->node_queue_capacity < catalog->num_nodes)))
		goto done;

	for (i = 0U; i < num_links; i++)
	{
		const sg_action_desc_t *action_desc =
			SG_ActionDescribe(links[i].action);
		int needs_plan = SG_ActionMechanismPlanRequired(links[i].action);

		if (!needs_plan)
		{
			if (links[i].mechanism_plan != RUNE_NO_MECHANISM_PLAN)
			{
				Mechanism_Fail(&state, SG_MECHANISM_PLAN_BAD_ACTION, i,
					links[i].mechanism_plan);
				goto done;
			}
			continue;
		}
		if (required >= num_bindings || links[i].mechanism_plan != required ||
		    !action_desc || !SG_ActionMechanismPlanAllowed(links[i].action,
		        bindings[required].controller_kind))
		{
			Mechanism_Fail(&state, SG_MECHANISM_PLAN_BAD_BINDING, i,
				links[i].mechanism_plan);
			goto done;
		}
		required++;
	}
	if (required != num_bindings ||
	    (required != 0U && (catalog->num_nodes < 2U ||
	     catalog->num_edges == 0U)))
	{
		Mechanism_Fail(&state, SG_MECHANISM_PLAN_BAD_BINDING,
			UINT32_MAX, required);
		goto done;
	}
	if (catalog->num_edges != 0U)
		memcpy(buffers->edges, catalog->edges,
			(size_t)catalog->num_edges * sizeof(buffers->edges[0]));
	state.result.num_inventory_edges = catalog->num_edges;
	state.result.num_edges = catalog->num_edges;
	if (required != 0U)
	{
		memset(buffers->edge_marks, 0,
			(size_t)catalog->num_edges * sizeof(buffers->edge_marks[0]));
		memset(buffers->node_marks, 0,
			(size_t)catalog->num_nodes * sizeof(buffers->node_marks[0]));
	}
	required = 0U;
	for (i = 0U; i < num_links; i++)
	{
		if (!SG_ActionMechanismPlanRequired(links[i].action))
			continue;
		if (!Mechanism_MaterializeOne(&state, i, required))
			goto done;
		links[i].mechanism_plan = required;
		required++;
		state.result.num_plans = required;
	}
	state.result.diagnostic = SG_MECHANISM_PLAN_OK;

done:
	if (result_out)
		*result_out = state.result;
	return state.result.diagnostic == SG_MECHANISM_PLAN_OK;
}

const char *SG_MechanismPlanDiagnosticName(
	sg_mechanism_plan_diagnostic_t diagnostic)
{
	switch (diagnostic)
	{
	case SG_MECHANISM_PLAN_OK: return "ok";
	case SG_MECHANISM_PLAN_INVALID_ARGUMENT: return "invalid-argument";
	case SG_MECHANISM_PLAN_BAD_BINDING: return "bad-binding";
	case SG_MECHANISM_PLAN_BAD_ACTION: return "bad-action";
	case SG_MECHANISM_PLAN_BAD_CATALOG: return "bad-catalog";
	case SG_MECHANISM_PLAN_BAD_CLOSURE: return "bad-closure";
	case SG_MECHANISM_PLAN_CAPACITY: return "capacity";
	case SG_MECHANISM_PLAN_BAD_CRC: return "bad-crc";
	default: return "unknown";
	}
}
