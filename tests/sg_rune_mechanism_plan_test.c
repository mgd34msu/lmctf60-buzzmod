/* Focused native generator-plan materialization and active-codec acceptance. */
#include "q_shared.h"
#include "slipgate/sg_rune_mechanism_plan.h"
#include "slipgate/sg_rune_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_NODES 16U
#define TEST_INVENTORY_EDGES 32U
#define TEST_EDGES 96U
#define TEST_PLANS 8U
#define TEST_STRINGS 256U

static int failures;
static uint32_t covered_actions;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#condition_); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	rune_mechanism_node_t nodes[TEST_NODES];
	rune_mechanism_edge_t inventory[TEST_INVENTORY_EDGES];
	rune_mechanism_edge_t edges[TEST_EDGES];
	rune_mechanism_plan_t plans[TEST_PLANS];
	uint32_t edge_marks[TEST_INVENTORY_EDGES];
	uint32_t node_marks[TEST_NODES];
	uint32_t node_queue[TEST_NODES];
	unsigned char strings[TEST_STRINGS];
	uint32_t num_nodes;
	uint32_t num_inventory;
	uint32_t string_bytes;
	rune_link_t links[2];
	sg_mechanism_plan_binding_t binding;
	sg_mech_catalog_view_t catalog;
	sg_mechanism_plan_buffers_t buffers;
	sg_mechanism_plan_result_t result;
} fixture_t;

static uint32_t StringOffset(const fixture_t *fixture, const char *value)
{
	uint32_t offset = 1U;

	if (!value || !value[0])
		return 0U;
	while (offset < fixture->string_bytes)
	{
		const char *candidate = (const char *)fixture->strings + offset;

		if (!strcmp(candidate, value))
			return offset;
		offset += (uint32_t)strlen(candidate) + 1U;
	}
	return UINT32_MAX;
}

static void Strings(fixture_t *fixture, const char *const *values,
	uint32_t count)
{
	uint32_t i;
	uint32_t offset = 1U;

	memset(fixture->strings, 0, sizeof(fixture->strings));
	for (i = 0U; i < count; i++)
	{
		size_t length = strlen(values[i]) + 1U;

		CHECK(offset + length <= sizeof(fixture->strings));
		if (i != 0U)
			CHECK(strcmp(values[i - 1U], values[i]) < 0);
		memcpy(fixture->strings + offset, values[i], length);
		offset += (uint32_t)length;
	}
	fixture->string_bytes = offset;
}

static rune_mechanism_node_t *Node(fixture_t *fixture, uint32_t key,
	uint16_t kind, const char *classname)
{
	rune_mechanism_node_t *node;

	CHECK(fixture->num_nodes < TEST_NODES);
	node = &fixture->nodes[fixture->num_nodes++];
	memset(node, 0, sizeof(*node));
	node->key = key;
	node->kind = kind;
	node->owner_key = SG_MECH_NO_KEY;
	node->team_master_key = SG_MECH_NO_KEY;
	node->classname_offset = StringOffset(fixture, classname);
	CHECK(node->classname_offset != UINT32_MAX);
	return node;
}

static void Target(rune_mechanism_node_t *node, const fixture_t *fixture,
	const char *value)
{
	node->target_offset = StringOffset(fixture, value);
	CHECK(node->target_offset != UINT32_MAX);
}

static void Targetname(rune_mechanism_node_t *node,
	const fixture_t *fixture, const char *value)
{
	node->targetname_offset = StringOffset(fixture, value);
	CHECK(node->targetname_offset != UINT32_MAX);
	if (node->kind == SG_MECH_NODE_DOOR_MASTER ||
	    node->kind == SG_MECH_NODE_DOOR_MEMBER)
		node->think_callback = SG_MECH_CALLBACK_THINK_CALC_MOVE_SPEED;
}

static void Edge(fixture_t *fixture, uint32_t from_key, uint32_t to_key,
	uint16_t kind, uint16_t ordinal)
{
	rune_mechanism_edge_t *edge;

	CHECK(fixture->num_inventory < TEST_INVENTORY_EDGES);
	edge = &fixture->inventory[fixture->num_inventory++];
	memset(edge, 0, sizeof(*edge));
	edge->from_key = from_key;
	edge->to_key = to_key;
	edge->kind = kind;
	edge->ordinal = ordinal;
}

static rune_mechanism_edge_t *InventoryEdge(fixture_t *fixture,
	uint32_t from_key, uint32_t to_key)
{
	uint32_t index;

	for (index = 0U; index < fixture->num_inventory; index++)
		if (fixture->inventory[index].from_key == from_key &&
		    fixture->inventory[index].to_key == to_key)
			return &fixture->inventory[index];
	return NULL;
}

static int EdgeCompare(const void *left_value, const void *right_value)
{
	const rune_mechanism_edge_t *left = left_value;
	const rune_mechanism_edge_t *right = right_value;

	if (left->from_key != right->from_key)
		return left->from_key < right->from_key ? -1 : 1;
	if (left->kind != right->kind)
		return left->kind < right->kind ? -1 : 1;
	if (left->ordinal != right->ordinal)
		return left->ordinal < right->ordinal ? -1 : 1;
	return 0;
}

static rune_mechanism_node_t *Door(fixture_t *fixture, uint32_t key,
	uint32_t master_key, int master)
{
	rune_mechanism_node_t *node = Node(fixture, key,
		master ? SG_MECH_NODE_DOOR_MASTER : SG_MECH_NODE_DOOR_MEMBER,
		"func_door");

	node->flags = SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER |
		(master ? SG_MECH_NODEF_TEAM_MASTER : SG_MECH_NODEF_TEAM_MEMBER);
	node->team_master_key = master_key;
	node->use_callback = SG_MECH_CALLBACK_USE_DOOR;
	/* Callback choice is per brush and follows its own targetname.  Door()
	 * creates the anonymous spawn shape; Targetname() switches either a
	 * captain or slave to Think_CalcMoveSpeed. */
	node->think_callback = SG_MECH_CALLBACK_THINK_SPAWN_DOOR_TRIGGER;
	node->blocked_callback = SG_MECH_CALLBACK_BLOCKED_DOOR;
	node->wait_ms = 3000;
	node->speed_q8 = 800U;
	node->accel_q8 = 800U;
	node->decel_q8 = 800U;
	return node;
}

static void FixtureInit(fixture_t *fixture, int action)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->links[0].from = 0;
	fixture->links[0].to = 1;
	fixture->links[0].action = (byte)action;
	fixture->links[0].provenance = RL_DECLARED;
	fixture->links[0].cost_ms = 100;
	fixture->links[0].mechanism_plan = 0U;
	if (action == RL_BUTTON_DOOR)
	{
		fixture->links[0].mode = RLCM_PREOPEN;
		fixture->links[0].mechanism_anchor[2] = -2.0f;
		fixture->links[0].sweep_clear_ms = 100U;
	}
	fixture->links[1].from = 1;
	fixture->links[1].to = 0;
	fixture->links[1].action = RL_RUN;
	fixture->links[1].provenance = RL_PROVEN;
	fixture->links[1].cost_ms = 100;
	fixture->links[1].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	fixture->binding.destination_key = SG_MECH_NO_KEY;
	covered_actions |= UINT32_C(1) << action;
}

static void FixtureFinish(fixture_t *fixture)
{
	qsort(fixture->inventory, fixture->num_inventory,
		sizeof(fixture->inventory[0]), EdgeCompare);
	fixture->catalog.nodes = fixture->nodes;
	fixture->catalog.num_nodes = fixture->num_nodes;
	fixture->catalog.edges = fixture->inventory;
	fixture->catalog.num_edges = fixture->num_inventory;
	fixture->catalog.strings = fixture->strings;
	fixture->catalog.string_bytes = fixture->string_bytes;
	fixture->buffers.edges = fixture->edges;
	fixture->buffers.edge_capacity = TEST_EDGES;
	fixture->buffers.plans = fixture->plans;
	fixture->buffers.plan_capacity = TEST_PLANS;
	fixture->buffers.edge_marks = fixture->edge_marks;
	fixture->buffers.edge_mark_capacity = TEST_INVENTORY_EDGES;
	fixture->buffers.node_marks = fixture->node_marks;
	fixture->buffers.node_mark_capacity = TEST_NODES;
	fixture->buffers.node_queue = fixture->node_queue;
	fixture->buffers.node_queue_capacity = TEST_NODES;
	CHECK(SG_MechanismPlansMaterialize(fixture->links, 2U,
		&fixture->binding, 1U, &fixture->catalog, &fixture->buffers,
		&fixture->result));
	CHECK(fixture->result.diagnostic == SG_MECHANISM_PLAN_OK);
	CHECK(fixture->result.num_inventory_edges == fixture->num_inventory);
	CHECK(fixture->result.num_plans == 1U);
	CHECK(fixture->plans[0].first_edge == fixture->num_inventory);
	CHECK(fixture->plans[0].num_edges != 0U);
	CHECK(fixture->links[0].mechanism_plan == 0U);
	CHECK(fixture->links[1].mechanism_plan == RUNE_NO_MECHANISM_PLAN);
}

static void ExpectTeleportDestinationMaterializationFailure(
	fixture_t *fixture)
{
	memset(fixture->edges, 0, sizeof(fixture->edges));
	memset(fixture->plans, 0, sizeof(fixture->plans));
	memset(fixture->edge_marks, 0, sizeof(fixture->edge_marks));
	memset(fixture->node_marks, 0, sizeof(fixture->node_marks));
	memset(fixture->node_queue, 0, sizeof(fixture->node_queue));
	memset(&fixture->result, 0, sizeof(fixture->result));
	fixture->links[0].mechanism_plan = 0U;
	fixture->links[1].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	CHECK(!SG_MechanismPlansMaterialize(fixture->links, 2U,
		&fixture->binding, 1U, &fixture->catalog, &fixture->buffers,
		&fixture->result));
	CHECK(fixture->result.diagnostic == SG_MECHANISM_PLAN_BAD_CLOSURE);
	CHECK(fixture->result.num_plans == 0U);
	CHECK(fixture->plans[0].num_edges == 0U);
}

static void ExpectDoorMaterializationFailure(fixture_t *fixture)
{
	memset(fixture->edges, 0, sizeof(fixture->edges));
	memset(fixture->plans, 0, sizeof(fixture->plans));
	memset(fixture->edge_marks, 0, sizeof(fixture->edge_marks));
	memset(fixture->node_marks, 0, sizeof(fixture->node_marks));
	memset(fixture->node_queue, 0, sizeof(fixture->node_queue));
	memset(&fixture->result, 0, sizeof(fixture->result));
	fixture->links[0].mechanism_plan = 0U;
	CHECK(!SG_MechanismPlansMaterialize(fixture->links, 2U,
		&fixture->binding, 1U, &fixture->catalog, &fixture->buffers,
		&fixture->result));
	CHECK(fixture->result.diagnostic == SG_MECHANISM_PLAN_BAD_CLOSURE);
	CHECK(fixture->result.num_plans == 0U);
}

static void CodecNode(const rune_mechanism_node_t *source,
	sg_rune_codec_activation_node_t *destination)
{
	memset(destination, 0, sizeof(*destination));
	destination->key = source->key;
	destination->kind = source->kind;
	destination->flags = source->flags;
	destination->classname_offset = source->classname_offset;
	destination->target_offset = source->target_offset;
	destination->targetname_offset = source->targetname_offset;
	destination->killtarget_offset = source->killtarget_offset;
	destination->owner_key = source->owner_key;
	destination->team_master_key = source->team_master_key;
	destination->spawnflags = source->spawnflags;
	destination->touch_callback = source->touch_callback;
	destination->use_callback = source->use_callback;
	destination->think_callback = source->think_callback;
	destination->blocked_callback = source->blocked_callback;
	destination->delay_ms = source->delay_ms;
	destination->wait_ms = source->wait_ms;
	destination->speed_q8 = source->speed_q8;
	destination->accel_q8 = source->accel_q8;
	destination->decel_q8 = source->decel_q8;
	memcpy(destination->absmin_q8, source->absmin_q8,
		sizeof(destination->absmin_q8));
	memcpy(destination->absmax_q8, source->absmax_q8,
		sizeof(destination->absmax_q8));
	destination->path_target_offset = source->path_target_offset;
}

static sg_rune_codec_diagnostic_t CodecValidationDiagnostic(
	const fixture_t *fixture)
{
	sg_rune_codec_seed_t seeds[2];
	sg_rune_codec_link_t links[2];
	sg_rune_codec_activation_node_t nodes[TEST_NODES];
	sg_rune_codec_activation_edge_t edges[TEST_EDGES];
	sg_rune_codec_activation_plan_t plans[TEST_PLANS];
	uint64_t graph_keys[2];
	uint8_t source_marks[2];
	uint32_t plan_references[TEST_PLANS];
	uint32_t node_references[TEST_NODES];
	uint32_t node_heads[TEST_NODES];
	uint32_t node_indegrees[TEST_NODES];
	uint32_t node_generations[TEST_NODES];
	uint32_t node_touched[TEST_NODES];
	uint32_t node_queue[TEST_NODES];
	uint32_t edge_next[TEST_EDGES];
	uint8_t string_marks[TEST_STRINGS];
	sg_rune_codec_workspace_t workspace;
	uint32_t i;

	memset(seeds, 0, sizeof(seeds));
	seeds[1].origin[0] = 64.0f;
	memset(links, 0, sizeof(links));
	links[0].source = 0U;
	links[0].destination = 1U;
	links[0].action = fixture->links[0].action;
	links[0].provenance = RL_DECLARED;
	links[0].cost_ms = 100;
	links[0].activation_plan = 0U;
	memcpy(links[0].suffix_anchor, fixture->links[0].anchor,
		sizeof(links[0].suffix_anchor));
	memcpy(links[0].mechanism_anchor,
		fixture->links[0].mechanism_anchor,
		sizeof(links[0].mechanism_anchor));
	links[0].sweep_clear_ms = fixture->links[0].sweep_clear_ms;
	links[0].mode = fixture->links[0].mode;
	links[1].source = 1U;
	links[1].destination = 0U;
	links[1].action = RL_RUN;
	links[1].provenance = RL_PROVEN;
	links[1].cost_ms = 100;
	links[1].activation_plan = SG_RUNE_CODEC_NO_ACTIVATION_PLAN;
	for (i = 0U; i < fixture->num_nodes; i++)
		CodecNode(&fixture->nodes[i], &nodes[i]);
	for (i = 0U; i < fixture->result.num_edges; i++)
	{
		edges[i].from_key = fixture->edges[i].from_key;
		edges[i].to_key = fixture->edges[i].to_key;
		edges[i].kind = fixture->edges[i].kind;
		edges[i].ordinal = fixture->edges[i].ordinal;
		edges[i].delay_ms = fixture->edges[i].delay_ms;
	}
	memset(plans, 0, sizeof(plans));
	plans[0].entry_key = fixture->plans[0].entry_key;
	plans[0].mover_key = fixture->plans[0].mover_key;
	plans[0].first_edge = fixture->plans[0].first_edge;
	plans[0].num_edges = fixture->plans[0].num_edges;
	plans[0].controller_kind = fixture->plans[0].controller_kind;
	plans[0].flags = fixture->plans[0].flags;
	plans[0].expected_members = fixture->plans[0].expected_members;
	plans[0].cooldown_ms = fixture->plans[0].cooldown_ms;
	plans[0].closure_crc32 = fixture->plans[0].closure_crc32;
	memset(&workspace, 0, sizeof(workspace));
	workspace.graph_link_keys = graph_keys;
	workspace.graph_link_key_capacity = 2U;
	workspace.graph_source_marks = source_marks;
	workspace.graph_source_mark_capacity = 2U;
	workspace.plan_references = plan_references;
	workspace.plan_reference_capacity = TEST_PLANS;
	workspace.node_references = node_references;
	workspace.node_reference_capacity = TEST_NODES;
	workspace.node_heads = node_heads;
	workspace.node_head_capacity = TEST_NODES;
	workspace.node_indegrees = node_indegrees;
	workspace.node_indegree_capacity = TEST_NODES;
	workspace.node_generations = node_generations;
	workspace.node_generation_capacity = TEST_NODES;
	workspace.node_touched = node_touched;
	workspace.node_touched_capacity = TEST_NODES;
	workspace.node_queue = node_queue;
	workspace.node_queue_capacity = TEST_NODES;
	workspace.edge_next = edge_next;
	workspace.edge_next_capacity = TEST_EDGES;
	workspace.string_marks = string_marks;
	workspace.string_mark_capacity = TEST_STRINGS;
	return SG_RuneCodecValidate(seeds, 2U, links, 2U, nodes,
		fixture->num_nodes, edges, fixture->result.num_edges, plans, 1U,
		fixture->strings, fixture->string_bytes, &workspace);
}

static void CodecValidate(const fixture_t *fixture)
{
	CHECK(CodecValidationDiagnostic(fixture) == RLCODEC_OK);
}

static void TestPlatform(void)
{
	static const char *const strings[] = { "func_plat", "trigger_plat" };
	fixture_t fixture;
	rune_mechanism_node_t *entry;

	FixtureInit(&fixture, RL_LIFT);
	Strings(&fixture, strings, 2U);
	entry = Node(&fixture, 1U, SG_MECH_NODE_PLATFORM_TRIGGER,
		"trigger_plat");
	entry->flags = SG_MECH_NODEF_SYNTHETIC | SG_MECH_NODEF_TOUCHABLE;
	entry->owner_key = 2U;
	entry->touch_callback = SG_MECH_CALLBACK_TOUCH_PLAT_CENTER;
	Node(&fixture, 2U, SG_MECH_NODE_PLATFORM, "func_plat")->flags =
		SG_MECH_NODEF_MOVER;
	Edge(&fixture, 1U, 2U, SG_MECH_EDGE_OWNER, 0U);
	fixture.binding.entry_key = 1U;
	fixture.binding.mover_key = 2U;
	fixture.binding.controller_kind = SG_MECHANISM_CONTROLLER_PLATFORM;
	fixture.binding.expected_members = 1U;
	FixtureFinish(&fixture);
	CodecValidate(&fixture);
}

static void TestTeleport(void)
{
	static const char *const strings[] = {
		"dest", "misc_teleporter", "misc_teleporter_dest",
		"trigger_teleport"
	};
	fixture_t fixture;
	rune_mechanism_node_t *entry;
	rune_mechanism_node_t *destination;
	rune_mechanism_plan_t valid_plan;
	rune_mechanism_edge_t valid_edges[TEST_EDGES];
	sg_mechanism_plan_result_t valid_result;

	FixtureInit(&fixture, RL_TELEPORT);
	Strings(&fixture, strings, 4U);
	entry = Node(&fixture, 1U, SG_MECH_NODE_TELEPORT_TRIGGER,
		"trigger_teleport");
	entry->flags = SG_MECH_NODEF_SYNTHETIC | SG_MECH_NODEF_TOUCHABLE;
	entry->owner_key = 2U;
	entry->touch_callback = SG_MECH_CALLBACK_TELEPORTER_TOUCH;
	Target(entry, &fixture, "dest");
	Node(&fixture, 2U, SG_MECH_NODE_TELEPORTER, "misc_teleporter");
	destination = Node(&fixture, 3U, SG_MECH_NODE_TELEPORT_DEST,
		"misc_teleporter_dest");
	Targetname(destination, &fixture, "dest");
	Edge(&fixture, 1U, 2U, SG_MECH_EDGE_OWNER, 0U);
	Edge(&fixture, 1U, 3U, SG_MECH_EDGE_TARGET, 0U);
	fixture.binding.entry_key = 1U;
	fixture.binding.mover_key = 2U;
	fixture.binding.destination_key = 3U;
	fixture.binding.controller_kind = SG_MECHANISM_CONTROLLER_TELEPORT;
	fixture.binding.expected_members = 1U;
	FixtureFinish(&fixture);
	CodecValidate(&fixture);
	valid_plan = fixture.plans[0];
	memcpy(valid_edges, fixture.edges, sizeof(valid_edges));
	valid_result = fixture.result;

	destination->flags |= SG_MECH_NODEF_INVENTORY_ONLY;
	CHECK(CodecValidationDiagnostic(&fixture) == RLCODEC_BAD_ACTIVATION_EDGE);
	ExpectTeleportDestinationMaterializationFailure(&fixture);

	destination->flags = (uint16_t)(destination->flags &
		~SG_MECH_NODEF_INVENTORY_ONLY);
	destination->use_callback = SG_MECH_CALLBACK_UNKNOWN;
	fixture.plans[0] = valid_plan;
	memcpy(fixture.edges, valid_edges, sizeof(valid_edges));
	fixture.result = valid_result;
	CHECK(CodecValidationDiagnostic(&fixture) == RLCODEC_BAD_ACTIVATION_NODE);
	ExpectTeleportDestinationMaterializationFailure(&fixture);
}

static void TestAutoDoor(void)
{
	static const char *const strings[] = {
		"auto_sound", "func_door", "target_speaker", "trigger_auto"
	};
	fixture_t fixture;
	rune_mechanism_node_t *entry;
	rune_mechanism_node_t *member;
	rune_mechanism_node_t *speaker;

	FixtureInit(&fixture, RL_DOOR);
	Strings(&fixture, strings, 4U);
	entry = Node(&fixture, 1U, SG_MECH_NODE_AUTO_DOOR_TRIGGER,
		"trigger_auto");
	entry->flags = SG_MECH_NODEF_SYNTHETIC | SG_MECH_NODEF_TOUCHABLE;
	entry->owner_key = 2U;
	entry->touch_callback = SG_MECH_CALLBACK_TOUCH_DOOR_TRIGGER;
	Door(&fixture, 2U, 2U, 1);
	member = Door(&fixture, 3U, 2U, 0);
	Target(member, &fixture, "auto_sound");
	speaker = Node(&fixture, 4U, SG_MECH_NODE_TARGET_SPEAKER,
		"target_speaker");
	speaker->flags = SG_MECH_NODEF_USABLE;
	speaker->use_callback = SG_MECH_CALLBACK_USE_TARGET_SPEAKER;
	Targetname(speaker, &fixture, "auto_sound");
	Edge(&fixture, 1U, 2U, SG_MECH_EDGE_OWNER, 0U);
	Edge(&fixture, 2U, 3U, SG_MECH_EDGE_TEAM, 0U);
	Edge(&fixture, 3U, 4U, SG_MECH_EDGE_TARGET, 0U);
	fixture.binding.entry_key = 1U;
	fixture.binding.mover_key = 2U;
	fixture.binding.controller_kind = SG_MECHANISM_CONTROLLER_AUTO_DOOR;
	fixture.binding.expected_members = 2U;
	fixture.binding.cooldown_ms = 1000U;
	FixtureFinish(&fixture);
	CHECK(fixture.plans[0].num_edges == 3U);
	CodecValidate(&fixture);

}

static void TestButtonDoor(void)
{
	static const char *const strings[] = {
		"DoorButton", "func_button", "func_door"
	};
	fixture_t fixture;
	rune_mechanism_node_t *button;
	rune_mechanism_node_t *door;
	rune_mechanism_node_t *member;

	FixtureInit(&fixture, RL_BUTTON_DOOR);
	Strings(&fixture, strings, 3U);
	button = Node(&fixture, 1U, SG_MECH_NODE_BUTTON, "func_button");
	button->flags = SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER;
	button->touch_callback = SG_MECH_CALLBACK_BUTTON_TOUCH;
	button->use_callback = SG_MECH_CALLBACK_BUTTON_USE;
	button->wait_ms = 3000;
	button->speed_q8 = 800U;
	button->accel_q8 = 800U;
	button->decel_q8 = 800U;
	Target(button, &fixture, "DoorButton");
	door = Door(&fixture, 2U, 2U, 1);
	Targetname(door, &fixture, "DoorButton");
	member = Door(&fixture, 3U, 2U, 0);
	Targetname(member, &fixture, "DoorButton");
	Edge(&fixture, 1U, 2U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 1U, 3U, SG_MECH_EDGE_TARGET, 1U);
	Edge(&fixture, 2U, 3U, SG_MECH_EDGE_TEAM, 0U);
	fixture.binding.entry_key = 1U;
	fixture.binding.mover_key = 2U;
	fixture.binding.controller_kind = SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
	fixture.binding.expected_members = 2U;
	fixture.binding.cooldown_ms = 3000U;
	FixtureFinish(&fixture);
	CHECK(fixture.plans[0].num_edges == 3U);
	CodecValidate(&fixture);

	button->flags |= SG_MECH_NODEF_SHOOTABLE |
		SG_MECH_NODEF_FRAME_COMPLETE_MOVER;
	memset(fixture.edges, 0, sizeof(fixture.edges));
	memset(fixture.plans, 0, sizeof(fixture.plans));
	memset(&fixture.result, 0, sizeof(fixture.result));
	fixture.links[0].mechanism_plan = 0U;
	CHECK(!SG_MechanismPlansMaterialize(fixture.links, 2U,
		&fixture.binding, 1U, &fixture.catalog, &fixture.buffers,
		&fixture.result));
	CHECK(fixture.result.diagnostic == SG_MECHANISM_PLAN_BAD_CLOSURE);
}

static void TestDirectDoorFullClosure(void)
{
	static const char *const strings[] = {
		"DoorDirect", "area_direct", "func_areaportal", "func_door",
		"relay_sound", "target_speaker", "trigger_multiple",
		"trigger_relay"
	};
	fixture_t fixture;
	rune_mechanism_node_t *entry;
	rune_mechanism_node_t *master;
	rune_mechanism_node_t *member;
	rune_mechanism_node_t *speaker;
	rune_mechanism_node_t *relay;
	rune_mechanism_node_t *relay_speaker;
	rune_mechanism_node_t *areaportal;

	FixtureInit(&fixture, RL_DOOR);
	Strings(&fixture, strings, 8U);
	entry = Node(&fixture, 1U, SG_MECH_NODE_TRIGGER, "trigger_multiple");
	entry->flags = SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		SG_MECH_NODEF_USABLE;
	entry->touch_callback = SG_MECH_CALLBACK_TOUCH_MULTI;
	entry->use_callback = SG_MECH_CALLBACK_USE_MULTI;
	entry->wait_ms = 1000;
	Target(entry, &fixture, "DoorDirect");
	master = Door(&fixture, 2U, 2U, 1);
	Targetname(master, &fixture, "DoorDirect");
	member = Door(&fixture, 3U, 2U, 0);
	Targetname(member, &fixture, "DoorDirect");
	Target(member, &fixture, "area_direct");
	speaker = Node(&fixture, 4U, SG_MECH_NODE_TARGET_SPEAKER,
		"target_speaker");
	speaker->flags = SG_MECH_NODEF_USABLE;
	speaker->use_callback = SG_MECH_CALLBACK_USE_TARGET_SPEAKER;
	Targetname(speaker, &fixture, "DoorDirect");
	relay = Node(&fixture, 5U, SG_MECH_NODE_RELAY, "trigger_relay");
	relay->flags = SG_MECH_NODEF_USABLE;
	relay->use_callback = SG_MECH_CALLBACK_USE_TRIGGER_RELAY;
	Targetname(relay, &fixture, "DoorDirect");
	Target(relay, &fixture, "relay_sound");
	relay_speaker = Node(&fixture, 6U, SG_MECH_NODE_TARGET_SPEAKER,
		"target_speaker");
	relay_speaker->flags = SG_MECH_NODEF_USABLE;
	relay_speaker->use_callback = SG_MECH_CALLBACK_USE_TARGET_SPEAKER;
	Targetname(relay_speaker, &fixture, "relay_sound");
	areaportal = Node(&fixture, 7U, SG_MECH_NODE_AREAPORTAL,
		"func_areaportal");
	areaportal->flags = SG_MECH_NODEF_USABLE;
	areaportal->use_callback = SG_MECH_CALLBACK_USE_AREAPORTAL;
	Targetname(areaportal, &fixture, "area_direct");
	Edge(&fixture, 1U, 2U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 1U, 3U, SG_MECH_EDGE_TARGET, 1U);
	Edge(&fixture, 1U, 4U, SG_MECH_EDGE_TARGET, 2U);
	Edge(&fixture, 1U, 5U, SG_MECH_EDGE_TARGET, 3U);
	Edge(&fixture, 2U, 3U, SG_MECH_EDGE_TEAM, 0U);
	Edge(&fixture, 3U, 7U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 5U, 6U, SG_MECH_EDGE_TARGET, 0U);
	fixture.binding.entry_key = 1U;
	fixture.binding.mover_key = 2U;
	fixture.binding.controller_kind =
		SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
	fixture.binding.expected_members = 2U;
	fixture.binding.cooldown_ms = 1000U;
	FixtureFinish(&fixture);
	CHECK(fixture.plans[0].num_edges == 7U);
	CodecValidate(&fixture);

	/* Existing <=30-second plans are byte-semantic invariants of this format.
	 * Pin the complete 32-byte record as well as its closure CRC, so the new
	 * long-wait projection cannot silently reinterpret ordinary artifacts. */
	{
		static const unsigned char golden[32] = {
			0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
			0x07, 0x00, 0x00, 0x00, 0x07, 0x00, 0x00, 0x00,
			0x02, 0x00, 0x00, 0x00, 0x0d, 0x00, 0x02, 0x00,
			0xe8, 0x03, 0x00, 0x00, 0x1b, 0x75, 0x6a, 0x00
		};
		sg_rune_codec_activation_plan_t plan;
		unsigned char encoded[32];

		memset(&plan, 0, sizeof(plan));
		plan.entry_key = fixture.plans[0].entry_key;
		plan.mover_key = fixture.plans[0].mover_key;
		plan.first_edge = fixture.plans[0].first_edge;
		plan.num_edges = fixture.plans[0].num_edges;
		plan.controller_kind = fixture.plans[0].controller_kind;
		plan.flags = fixture.plans[0].flags;
		plan.expected_members = fixture.plans[0].expected_members;
		plan.cooldown_ms = fixture.plans[0].cooldown_ms;
		plan.closure_crc32 = fixture.plans[0].closure_crc32;
		CHECK(fixture.plans[0].closure_crc32 == UINT32_C(0x006a751b));
		CHECK(SG_RuneCodecEncodeActivationPlan(&plan, encoded,
			sizeof(encoded)) == RLCODEC_OK);
		CHECK(memcmp(encoded, golden, sizeof(golden)) == 0);
	}
}

static void BuildDelayedSoundTerminalFixture(fixture_t *fixture)
{
	static const char *const strings[] = {
		"CloseRelay", "CloseSound", "FrontDoor", "OpenSound",
		"func_door", "target_speaker", "trigger_multiple",
		"trigger_relay"
	};
	rune_mechanism_node_t *entry;
	rune_mechanism_node_t *relay;
	rune_mechanism_node_t *door;
	rune_mechanism_node_t *speaker;
	rune_mechanism_edge_t *delayed;

	FixtureInit(fixture, RL_DOOR);
	Strings(fixture, strings, 8U);
	entry = Node(fixture, 1U, SG_MECH_NODE_TRIGGER, "trigger_multiple");
	entry->flags = SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		SG_MECH_NODEF_USABLE;
	entry->touch_callback = SG_MECH_CALLBACK_TOUCH_MULTI;
	entry->use_callback = SG_MECH_CALLBACK_USE_MULTI;
	entry->wait_ms = 312000;
	Target(entry, fixture, "FrontDoor");
	relay = Node(fixture, 2U, SG_MECH_NODE_RELAY, "trigger_relay");
	relay->flags = SG_MECH_NODEF_USABLE;
	relay->use_callback = SG_MECH_CALLBACK_USE_TRIGGER_RELAY;
	relay->delay_ms = 311000;
	Targetname(relay, fixture, "FrontDoor");
	Target(relay, fixture, "CloseRelay");
	door = Door(fixture, 3U, 3U, 1);
	Targetname(door, fixture, "FrontDoor");
	Target(door, fixture, "OpenSound");
	door->wait_ms = 300000;
	door->speed_q8 = door->accel_q8 = door->decel_q8 = 120U;
	speaker = Node(fixture, 4U, SG_MECH_NODE_TARGET_SPEAKER,
		"target_speaker");
	speaker->flags = SG_MECH_NODEF_USABLE;
	speaker->use_callback = SG_MECH_CALLBACK_USE_TARGET_SPEAKER;
	Targetname(speaker, fixture, "OpenSound");
	relay = Node(fixture, 5U, SG_MECH_NODE_RELAY, "trigger_relay");
	relay->flags = SG_MECH_NODEF_USABLE;
	relay->use_callback = SG_MECH_CALLBACK_USE_TRIGGER_RELAY;
	Targetname(relay, fixture, "CloseRelay");
	Target(relay, fixture, "CloseSound");
	speaker = Node(fixture, 6U, SG_MECH_NODE_TARGET_SPEAKER,
		"target_speaker");
	speaker->flags = SG_MECH_NODEF_USABLE;
	speaker->use_callback = SG_MECH_CALLBACK_USE_TARGET_SPEAKER;
	Targetname(speaker, fixture, "CloseSound");
	Edge(fixture, 1U, 2U, SG_MECH_EDGE_TARGET, 0U);
	Edge(fixture, 1U, 3U, SG_MECH_EDGE_TARGET, 1U);
	Edge(fixture, 2U, 5U, SG_MECH_EDGE_TARGET, 0U);
	delayed = InventoryEdge(fixture, 2U, 5U);
	CHECK(delayed != NULL);
	if (delayed)
		delayed->delay_ms = 311000U;
	Edge(fixture, 3U, 4U, SG_MECH_EDGE_TARGET, 0U);
	Edge(fixture, 5U, 6U, SG_MECH_EDGE_TARGET, 0U);
	fixture->binding.entry_key = 1U;
	fixture->binding.mover_key = 3U;
	fixture->binding.controller_kind =
		SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
	fixture->binding.expected_members = 1U;
	fixture->binding.cooldown_ms = RUNE_MAX_COST_MS;
	FixtureFinish(fixture);
	CodecValidate(fixture);
}

static void TestDelayedSoundTerminal(void)
{
	fixture_t fixture;
	rune_mechanism_edge_t *edge;
	rune_mechanism_node_t *node;
	uint32_t first;
	uint32_t crc;

	BuildDelayedSoundTerminalFixture(&fixture);
	first = fixture.plans[0].first_edge;
	crc = fixture.plans[0].closure_crc32;
	CHECK(fixture.num_inventory == 5U);
	CHECK(fixture.plans[0].cooldown_ms == RUNE_MAX_COST_MS);
	CHECK(fixture.plans[0].num_edges == 3U);
	CHECK(fixture.edges[first + 0U].from_key == 1U &&
		fixture.edges[first + 0U].to_key == 2U &&
		fixture.edges[first + 0U].ordinal == 0U);
	CHECK(fixture.edges[first + 1U].from_key == 1U &&
		fixture.edges[first + 1U].to_key == 3U &&
		fixture.edges[first + 1U].ordinal == 1U);
	CHECK(fixture.edges[first + 2U].from_key == 3U &&
		fixture.edges[first + 2U].to_key == 4U);
	/* The delayed callback edge remains exhaustive inventory, but never enters
	 * the synchronous executable closure. */
	CHECK(InventoryEdge(&fixture, 2U, 5U)->delay_ms == 311000U);

	/* A nested positive-delay sound relay is another terminal and changes no
	 * executable plan byte or closure CRC. */
	node = &fixture.nodes[4];
	node->delay_ms = 61000;
	edge = InventoryEdge(&fixture, 5U, 6U);
	CHECK(edge != NULL);
	if (edge)
		edge->delay_ms = 61000U;
	memset(fixture.edges, 0, sizeof(fixture.edges));
	memset(fixture.plans, 0, sizeof(fixture.plans));
	memset(fixture.edge_marks, 0, sizeof(fixture.edge_marks));
	memset(fixture.node_marks, 0, sizeof(fixture.node_marks));
	memset(fixture.node_queue, 0, sizeof(fixture.node_queue));
	memset(&fixture.result, 0, sizeof(fixture.result));
	CHECK(SG_MechanismPlansMaterialize(fixture.links, 2U,
		&fixture.binding, 1U, &fixture.catalog, &fixture.buffers,
		&fixture.result));
	CHECK(fixture.plans[0].first_edge == first);
	CHECK(fixture.plans[0].num_edges == 3U);
	CHECK(fixture.plans[0].closure_crc32 == crc);
	CodecValidate(&fixture);

	BuildDelayedSoundTerminalFixture(&fixture);
	fixture.nodes[4].killtarget_offset =
		StringOffset(&fixture, "FrontDoor");
	CHECK(CodecValidationDiagnostic(&fixture) ==
		RLCODEC_BAD_ACTIVATION_PLAN);
	ExpectDoorMaterializationFailure(&fixture);

	BuildDelayedSoundTerminalFixture(&fixture);
	fixture.nodes[5].use_callback = SG_MECH_CALLBACK_USE_DOOR;
	CHECK(CodecValidationDiagnostic(&fixture) ==
		RLCODEC_BAD_ACTIVATION_PLAN);
	ExpectDoorMaterializationFailure(&fixture);

	BuildDelayedSoundTerminalFixture(&fixture);
	node = &fixture.nodes[4];
	Target(node, &fixture, "FrontDoor");
	edge = InventoryEdge(&fixture, 5U, 6U);
	CHECK(edge != NULL);
	if (edge)
	{
		edge->to_key = 2U;
		/* Codec validation consumes the already materialized inventory prefix;
		 * mirror this malicious inventory mutation there before asking it to
		 * independently reject the recursive callback. */
		fixture.edges[(uint32_t)(edge - fixture.inventory)] = *edge;
	}
	CHECK(CodecValidationDiagnostic(&fixture) ==
		RLCODEC_BAD_ACTIVATION_PLAN);
	ExpectDoorMaterializationFailure(&fixture);
}

static void TestOnePlanPerLink(void)
{
	static const char *const strings[] = {
		"DoorButton", "func_button", "func_door"
	};
	fixture_t fixture;
	rune_link_t links[3];
	sg_mechanism_plan_binding_t bindings[2];
	sg_mechanism_plan_result_t result;
	rune_mechanism_node_t *button;
	rune_mechanism_node_t *door;

	FixtureInit(&fixture, RL_BUTTON_DOOR);
	Strings(&fixture, strings, 3U);
	button = Node(&fixture, 1U, SG_MECH_NODE_BUTTON, "func_button");
	button->flags = SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER;
	button->touch_callback = SG_MECH_CALLBACK_BUTTON_TOUCH;
	button->use_callback = SG_MECH_CALLBACK_BUTTON_USE;
	button->wait_ms = 3000;
	button->speed_q8 = button->accel_q8 = button->decel_q8 = 800U;
	Target(button, &fixture, "DoorButton");
	door = Door(&fixture, 2U, 2U, 1);
	Targetname(door, &fixture, "DoorButton");
	Edge(&fixture, 1U, 2U, SG_MECH_EDGE_TARGET, 0U);
	qsort(fixture.inventory, fixture.num_inventory,
		sizeof(fixture.inventory[0]), EdgeCompare);
	fixture.catalog.nodes = fixture.nodes;
	fixture.catalog.num_nodes = fixture.num_nodes;
	fixture.catalog.edges = fixture.inventory;
	fixture.catalog.num_edges = fixture.num_inventory;
	fixture.catalog.strings = fixture.strings;
	fixture.catalog.string_bytes = fixture.string_bytes;
	fixture.buffers.edges = fixture.edges;
	fixture.buffers.edge_capacity = TEST_EDGES;
	fixture.buffers.plans = fixture.plans;
	fixture.buffers.plan_capacity = TEST_PLANS;
	fixture.buffers.edge_marks = fixture.edge_marks;
	fixture.buffers.edge_mark_capacity = TEST_INVENTORY_EDGES;
	fixture.buffers.node_marks = fixture.node_marks;
	fixture.buffers.node_mark_capacity = TEST_NODES;
	fixture.buffers.node_queue = fixture.node_queue;
	fixture.buffers.node_queue_capacity = TEST_NODES;
	memset(links, 0, sizeof(links));
	links[0].action = RL_BUTTON_DOOR;
	links[0].mechanism_plan = 0U;
	links[1].action = RL_BUTTON_DOOR;
	links[1].mechanism_plan = 1U;
	links[2].action = RL_RUN;
	links[2].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	memset(bindings, 0, sizeof(bindings));
	bindings[0].entry_key = bindings[1].entry_key = 1U;
	bindings[0].mover_key = bindings[1].mover_key = 2U;
	bindings[0].destination_key = bindings[1].destination_key =
		SG_MECH_NO_KEY;
	bindings[0].controller_kind = bindings[1].controller_kind =
		SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
	bindings[0].expected_members = bindings[1].expected_members = 1U;
	bindings[0].cooldown_ms = bindings[1].cooldown_ms = 3000U;
	CHECK(SG_MechanismPlansMaterialize(links, 3U, bindings, 2U,
		&fixture.catalog, &fixture.buffers, &result));
	CHECK(result.num_plans == 2U);
	CHECK(result.num_edges == fixture.num_inventory + 2U);
	CHECK(fixture.plans[0].first_edge == fixture.num_inventory);
	CHECK(fixture.plans[1].first_edge == fixture.num_inventory + 1U);
	CHECK(memcmp(&fixture.edges[fixture.plans[0].first_edge],
		&fixture.edges[fixture.plans[1].first_edge],
		sizeof(fixture.edges[0])) == 0);
	/* Diagnostics are optional; successful production materialization must not
	 * turn a NULL reporting sink into an authority or crash seam. */
	CHECK(SG_MechanismPlansMaterialize(links, 3U, bindings, 2U,
		&fixture.catalog, &fixture.buffers, NULL));
	links[1].mechanism_plan = 2U;
	CHECK(!SG_MechanismPlansMaterialize(links, 3U, bindings, 2U,
		&fixture.catalog, &fixture.buffers, &result));
	CHECK(result.diagnostic == SG_MECHANISM_PLAN_BAD_BINDING);
}

int main(void)
{
	TestPlatform();
	TestTeleport();
	TestAutoDoor();
	TestButtonDoor();
	TestDirectDoorFullClosure();
	TestDelayedSoundTerminal();
	TestOnePlanPerLink();
	CHECK((covered_actions & (UINT32_C(1) << RL_LIFT)) != 0U);
	CHECK((covered_actions & (UINT32_C(1) << RL_TELEPORT)) != 0U);
	CHECK((covered_actions & (UINT32_C(1) << RL_DOOR)) != 0U);
	CHECK((covered_actions & (UINT32_C(1) << RL_BUTTON_DOOR)) != 0U);
	if (failures)
	{
		fprintf(stderr, "sg_rune_mechanism_plan_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_mechanism_plan_test: PASS");
	return 0;
}
