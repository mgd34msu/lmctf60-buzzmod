/* Exact native mechanism-plan binding regressions. */
#include "q_shared.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_codec.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_train_station_plan.h"

#include <stdio.h>
#include <string.h>

#define TEST_NODES 16U
#define TEST_INVENTORY_EDGES 30U

struct edict_s
{
	uint32_t key;
};

typedef struct fixture_s
{
	rune_t rune;
	rune_seed_t seeds[2];
	rune_link_t link;
	rune_mechanism_node_t nodes[TEST_NODES];
	rune_mechanism_edge_t edges[TEST_INVENTORY_EDGES * 2U];
	rune_mechanism_plan_t plan;
	int first_link[2];
	int next_link[1];
	byte linked_seed[2];
	unsigned char strings[3];
	struct edict_s entities[TEST_NODES];
	uint32_t num_nodes;
	uint32_t num_inventory_edges;
} fixture_t;

static fixture_t *active_fixture;
static uint32_t topology_failure_key = SG_MECH_NO_KEY;
static uint32_t execution_failure_key = SG_MECH_NO_KEY;
static uint32_t incarnation_failure_key = SG_MECH_NO_KEY;
static uint32_t retired_key = SG_MECH_NO_KEY;
static int catalog_ready = 1;
static int failures;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#condition_); \
		failures++; \
	} \
} while (0)

static void PutU16(unsigned char *output, uint16_t value)
{
	output[0] = (unsigned char)(value & UINT16_C(0xff));
	output[1] = (unsigned char)(value >> 8);
}

static void PutU32(unsigned char *output, uint32_t value)
{
	output[0] = (unsigned char)(value & UINT32_C(0xff));
	output[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	output[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	output[3] = (unsigned char)(value >> 24);
}

static uint32_t ClosureCRC(const fixture_t *fixture)
{
	unsigned char encoded[16];
	uint32_t state = SG_CRC32Init();
	uint32_t ordinal;

	for (ordinal = 0U; ordinal < fixture->plan.num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			&fixture->edges[fixture->plan.first_edge + ordinal];

		PutU32(encoded + 0U, edge->from_key);
		PutU32(encoded + 4U, edge->to_key);
		PutU16(encoded + 8U, edge->kind);
		PutU16(encoded + 10U, edge->ordinal);
		PutU32(encoded + 12U, edge->delay_ms);
		CHECK(SG_CRC32Update(&state, encoded, sizeof(encoded)));
	}
	return SG_CRC32Final(state);
}

static rune_mechanism_node_t *Node(fixture_t *fixture, uint32_t key,
	uint16_t kind, uint16_t flags)
{
	rune_mechanism_node_t *node;

	CHECK(fixture->num_nodes < TEST_NODES);
	if (fixture->num_nodes != 0U)
		CHECK(fixture->nodes[fixture->num_nodes - 1U].key < key);
	node = &fixture->nodes[fixture->num_nodes];
	memset(node, 0, sizeof(*node));
	node->key = key;
	node->kind = kind;
	node->flags = flags;
	node->owner_key = SG_MECH_NO_KEY;
	node->team_master_key = SG_MECH_NO_KEY;
	fixture->entities[fixture->num_nodes].key = key;
	fixture->num_nodes++;
	return node;
}

static void Edge(fixture_t *fixture, uint32_t from_key, uint32_t to_key,
	uint16_t kind, uint16_t ordinal)
{
	rune_mechanism_edge_t *edge;

	CHECK(fixture->num_inventory_edges < TEST_INVENTORY_EDGES);
	edge = &fixture->edges[fixture->num_inventory_edges++];
	memset(edge, 0, sizeof(*edge));
	edge->from_key = from_key;
	edge->to_key = to_key;
	edge->kind = kind;
	edge->ordinal = ordinal;
}

static void FixtureBegin(fixture_t *fixture, int action,
	uint16_t controller, uint32_t cooldown_ms, uint16_t expected_members)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->link.from = 0;
	fixture->link.to = 1;
	fixture->link.action = (byte)action;
	fixture->link.provenance = RL_DECLARED;
	fixture->link.cost_ms = 100;
	fixture->link.mechanism_plan = 0U;
	fixture->plan.controller_kind = controller;
	fixture->plan.flags = SG_MechanismControllerPlanFlags(controller);
	fixture->plan.cooldown_ms = cooldown_ms;
	fixture->plan.expected_members = expected_members;
}

static void FixtureFinishPlanEdges(fixture_t *fixture, uint32_t entry_key,
	uint32_t mover_key, uint32_t plan_edge_count)
{
	rune_t *rune = &fixture->rune;
	uint32_t inventory = fixture->num_inventory_edges;
	uint32_t node_index;

	CHECK(inventory != 0U ||
		fixture->plan.controller_kind == SG_MECHANISM_CONTROLLER_PUSH);
	CHECK(plan_edge_count <= inventory);
	memcpy(fixture->edges + inventory, fixture->edges,
		plan_edge_count * sizeof(fixture->edges[0]));
	fixture->plan.entry_key = entry_key;
	fixture->plan.mover_key = mover_key;
	fixture->plan.first_edge = inventory;
	fixture->plan.num_edges = plan_edge_count;
	if (fixture->plan.controller_kind == SG_MECHANISM_CONTROLLER_PUSH)
	{
		for (node_index = 0U; node_index < fixture->num_nodes; node_index++)
			if (fixture->nodes[node_index].key == entry_key)
				break;
		CHECK(node_index < fixture->num_nodes);
		CHECK(node_index < fixture->num_nodes &&
			SG_RuneCodecPushClosureCRC32(entry_key,
				fixture->nodes[node_index].push_velocity,
				&fixture->plan.closure_crc32) == RLCODEC_OK);
	}
	else
		fixture->plan.closure_crc32 = ClosureCRC(fixture);

	rune->artifact.magic = RUNE_ARTIFACT_MAGIC;
	rune->artifact.header_crc32 = 1U;
	rune->artifact.num_seeds = 2U;
	rune->artifact.num_links = 1U;
	rune->artifact.num_mechanism_nodes = fixture->num_nodes;
	rune->artifact.num_mechanism_edges = inventory + plan_edge_count;
	rune->artifact.num_inventory_edges = inventory;
	rune->artifact.num_mechanism_plans = 1U;
	fixture->strings[1] = 'x';
	rune->artifact.string_bytes = sizeof(fixture->strings);
	memcpy(rune->artifact.identity.map_name, "binding-test", 13U);
	rune->hdr.magic = (int)RUNE_ARTIFACT_MAGIC;
	rune->hdr.num_seeds = 2;
	rune->hdr.num_links = 1;
	memcpy(rune->hdr.mapname, "binding-test", 13U);
	rune->seeds = fixture->seeds;
	rune->links = &fixture->link;
	rune->mechanism_nodes = fixture->nodes;
	rune->mechanism_edges = fixture->edges;
	rune->mechanism_plans = &fixture->plan;
	rune->mechanism_strings = fixture->strings;
	rune->first_link = fixture->first_link;
	rune->next_link = fixture->next_link;
	rune->linked_seed = fixture->linked_seed;
	active_fixture = fixture;
	topology_failure_key = SG_MECH_NO_KEY;
	execution_failure_key = SG_MECH_NO_KEY;
	incarnation_failure_key = SG_MECH_NO_KEY;
	retired_key = SG_MECH_NO_KEY;
	catalog_ready = 1;
	CHECK(SG_RunePublishedShapeValid(rune));
}

static void FixtureFinish(fixture_t *fixture, uint32_t entry_key,
	uint32_t mover_key)
{
	FixtureFinishPlanEdges(fixture, entry_key, mover_key,
	    fixture->num_inventory_edges);
}

static int NodeIndex(const fixture_t *fixture, uint32_t key)
{
	uint32_t index;

	for (index = 0U; index < fixture->num_nodes; index++)
		if (fixture->nodes[index].key == key)
			return (int)index;
	return -1;
}

int SG_MechCatalogMatches(const struct rune_mechanism_node_s *nodes,
	uint32_t num_nodes, const struct rune_mechanism_edge_s *inventory_edges,
	uint32_t num_inventory_edges, const unsigned char *strings,
	uint32_t string_bytes)
{
	return catalog_ready && active_fixture &&
	       nodes == active_fixture->nodes &&
	       num_nodes == active_fixture->num_nodes &&
	       inventory_edges == active_fixture->edges &&
	       num_inventory_edges == active_fixture->num_inventory_edges &&
	       strings == active_fixture->strings &&
	       string_bytes == sizeof(active_fixture->strings);
}

int SG_MechCatalogEntityTopologyMatches(uint32_t key,
	const struct rune_mechanism_node_s *node)
{
	int index;

	if (!catalog_ready || !active_fixture || !node ||
	    key == topology_failure_key || key == retired_key ||
	    (index = NodeIndex(active_fixture, key)) < 0)
		return 0;
	return node == &active_fixture->nodes[index];
}

int SG_MechCatalogEntityExecutionMatches(uint32_t key,
	const struct rune_mechanism_node_s *node, uint16_t controller_kind)
{
	int index;

	if (!catalog_ready || !active_fixture || !node ||
	    controller_kind == SG_MECHANISM_CONTROLLER_NONE ||
	    key == execution_failure_key || key == retired_key ||
	    (index = NodeIndex(active_fixture, key)) < 0)
		return 0;
	return node == &active_fixture->nodes[index];
}

struct edict_s *SG_MechCatalogResolveEntity(uint32_t key,
	const struct rune_mechanism_node_s *node)
{
	int index;

	if (!catalog_ready || !active_fixture || !node ||
	    key == incarnation_failure_key || key == retired_key ||
	    (index = NodeIndex(active_fixture, key)) < 0 ||
	    node != &active_fixture->nodes[index])
		return NULL;
	return &active_fixture->entities[index];
}

int SG_MechCatalogStationTrainImmutableMatches(uint32_t key,
	const struct rune_mechanism_node_s *node)
{
	int index;

	if (!catalog_ready || !active_fixture || !node ||
	    node->kind != SG_MECH_NODE_TRAIN || key == incarnation_failure_key ||
	    key == retired_key || (index = NodeIndex(active_fixture, key)) < 0)
		return 0;
	return node == &active_fixture->nodes[index];
}

struct edict_s *SG_MechCatalogResolveStationEntity(uint32_t key,
	const struct rune_mechanism_node_s *node)
{
	int index;

	if (node && node->kind != SG_MECH_NODE_TRAIN)
		return SG_MechCatalogResolveEntity(key, node);
	if (!SG_MechCatalogStationTrainImmutableMatches(key, node) ||
	    (index = NodeIndex(active_fixture, key)) < 0)
		return NULL;
	return &active_fixture->entities[index];
}

static void CheckDoorMovers(const sg_rune_mechanism_binding_t *binding,
	uint32_t first, uint32_t second)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count = 0U;

	CHECK(SG_RuneMechanismBindingMoverKeys(binding, keys, &count));
	CHECK(count == (second == SG_MECH_NO_KEY ? 1U : 2U));
	CHECK(keys[0] == first);
	if (second != SG_MECH_NO_KEY)
		CHECK(keys[1] == second);
}

typedef struct target_log_s
{
	uint32_t keys[TEST_NODES];
	uint32_t ordinals[TEST_NODES];
	uint32_t count;
	uint32_t fail_key_after_first;
} target_log_t;

static int RecordTarget(void *raw_context, struct edict_s *target,
	uint32_t target_key, uint32_t target_ordinal)
{
	target_log_t *log = raw_context;

	CHECK(log != NULL);
	CHECK(target != NULL);
	CHECK(log->count < TEST_NODES);
	if (!log || !target || log->count >= TEST_NODES)
		return 0;
	CHECK(target->key == target_key);
	log->keys[log->count] = target_key;
	log->ordinals[log->count] = target_ordinal;
	log->count++;
	if (log->count == 1U && log->fail_key_after_first != SG_MECH_NO_KEY)
		execution_failure_key = log->fail_key_after_first;
	return 1;
}

static void TestPlatform(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *entry;
	uint32_t failure_index = UINT32_MAX;

	FixtureBegin(&fixture, RL_LIFT, SG_MECHANISM_CONTROLLER_PLATFORM, 0U, 1U);
	entry = Node(&fixture, 10U, SG_MECH_NODE_PLATFORM_TRIGGER,
		SG_MECH_NODEF_SYNTHETIC | SG_MECH_NODEF_TOUCHABLE);
	entry->owner_key = 20U;
	entry->touch_callback = SG_MECH_CALLBACK_TOUCH_PLAT_CENTER;
	Node(&fixture, 20U, SG_MECH_NODE_PLATFORM, SG_MECH_NODEF_MOVER);
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_OWNER, 0U);
	FixtureFinish(&fixture, 10U, 20U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(binding.entry_entity == &fixture.entities[0]);
	CHECK(binding.mover_entity == &fixture.entities[1]);
	CheckDoorMovers(&binding, 20U, SG_MECH_NO_KEY);
	CHECK(SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == UINT32_MAX);
	topology_failure_key = 10U;
	CHECK(!SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == 0U);
	topology_failure_key = SG_MECH_NO_KEY;
	incarnation_failure_key = 20U;
	CHECK(!SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == 0U);
	incarnation_failure_key = SG_MECH_NO_KEY;
}

static void TestStockPlatformRideWithAutomaticDoorEgress(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *entry;
	rune_mechanism_node_t *egress;
	rune_mechanism_node_t *door;
	struct edict_s *egress_entity = NULL;
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	uint32_t delay_ms = UINT32_MAX;
	size_t key_count = 0U;

	FixtureBegin(&fixture, RL_LIFT, SG_MECHANISM_CONTROLLER_PLATFORM,
		0U, 2U);
	fixture.link.mode = RLCM_RIDE;
	entry = Node(&fixture, 10U, SG_MECH_NODE_PLATFORM_TRIGGER,
		SG_MECH_NODEF_SYNTHETIC | SG_MECH_NODEF_TOUCHABLE);
	entry->owner_key = 20U;
	entry->touch_callback = SG_MECH_CALLBACK_TOUCH_PLAT_CENTER;
	Node(&fixture, 20U, SG_MECH_NODE_PLATFORM, SG_MECH_NODEF_MOVER);
	egress = Node(&fixture, 30U, SG_MECH_NODE_AUTO_DOOR_TRIGGER,
		SG_MECH_NODEF_SYNTHETIC | SG_MECH_NODEF_TOUCHABLE);
	egress->owner_key = 40U;
	egress->touch_callback = SG_MECH_CALLBACK_TOUCH_DOOR_TRIGGER;
	door = Node(&fixture, 40U, SG_MECH_NODE_DOOR_MASTER,
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MASTER);
	door->team_master_key = 40U;
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_OWNER, 0U);
	Edge(&fixture, 30U, 40U, SG_MECH_EDGE_OWNER, 0U);
	FixtureFinish(&fixture, 10U, 20U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismBindingPlatformAutoDoorStage(&binding,
		&egress_entity, keys, &key_count));
	CHECK(egress_entity == &fixture.entities[2]);
	CHECK(key_count == 1U && keys[0] == 40U);
	CHECK(SG_RuneMechanismBindingLiftDoorStage(&binding,
		SG_CARRIER_DOOR_EGRESS, &egress_entity, keys, &key_count,
		&delay_ms));
	CHECK(delay_ms == 0U && egress_entity == &fixture.entities[2]);
	CHECK(!SG_RuneMechanismBindingLiftDoorStage(&binding,
		SG_CARRIER_DOOR_APPROACH, &egress_entity, keys, &key_count,
		&delay_ms));
	CHECK(SG_RuneMechanismBindingPlatformAutoDoorStageTriggerMatches(
		&binding, &fixture.entities[2]));
	CHECK(!SG_RuneMechanismBindingPlatformAutoDoorStageTriggerMatches(
		&binding, &fixture.entities[0]));

	fixture.link.mode = RLCM_NONE;
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismBindingLiftDoorStage(&binding,
		SG_CARRIER_DOOR_APPROACH, &egress_entity, keys, &key_count,
		&delay_ms));
	fixture.link.mode = RLCM_PREOPEN;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	fixture.link.mode = RLCM_RIDE;
	egress->touch_callback = SG_MECH_CALLBACK_TOUCH_MULTI;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
}

static void TestTriggeredVerticalDoorLift(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *entry;
	rune_mechanism_node_t *mover;
	uint32_t failure_index = UINT32_MAX;

	FixtureBegin(&fixture, RL_LIFT, SG_MECHANISM_CONTROLLER_PLATFORM, 200U, 1U);
	entry = Node(&fixture, 10U, SG_MECH_NODE_PLATFORM_TRIGGER,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		SG_MECH_NODEF_USABLE);
	entry->owner_key = 20U;
	entry->touch_callback = SG_MECH_CALLBACK_TOUCH_MULTI;
	entry->use_callback = SG_MECH_CALLBACK_USE_MULTI;
	entry->wait_ms = 200;
	mover = Node(&fixture, 20U, SG_MECH_NODE_PLATFORM,
		SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER |
		SG_MECH_NODEF_TEAM_MASTER);
	mover->team_master_key = 20U;
	mover->spawnflags = 5U;
	mover->use_callback = SG_MECH_CALLBACK_USE_DOOR;
	mover->blocked_callback = SG_MECH_CALLBACK_BLOCKED_DOOR;
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_OWNER, 0U);
	FixtureFinish(&fixture, 10U, 20U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(binding.entry_entity == &fixture.entities[0]);
	CHECK(binding.mover_entity == &fixture.entities[1]);
	CHECK(SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == UINT32_MAX);
	fixture.nodes[1].spawnflags = 3U;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
}

static void TestDescendingCarrierStagesUseAnchorIdentity(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *entry;
	rune_mechanism_node_t *mover;
	rune_mechanism_node_t *approach;
	rune_mechanism_node_t *egress;
	struct edict_s *trigger = NULL;
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t key_count = 0U;
	uint32_t delay_ms = 0U;

	FixtureBegin(&fixture, RL_LIFT, SG_MECHANISM_CONTROLLER_PLATFORM,
		200U, 3U);
	entry = Node(&fixture, 10U, SG_MECH_NODE_PLATFORM_TRIGGER,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		SG_MECH_NODEF_USABLE);
	entry->owner_key = 20U;
	entry->touch_callback = SG_MECH_CALLBACK_TOUCH_MULTI;
	entry->use_callback = SG_MECH_CALLBACK_USE_MULTI;
	entry->wait_ms = 200;
	mover = Node(&fixture, 20U, SG_MECH_NODE_PLATFORM,
		SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER |
		SG_MECH_NODEF_TEAM_MASTER);
	mover->team_master_key = 20U;
	mover->spawnflags = 4U;
	mover->use_callback = SG_MECH_CALLBACK_USE_DOOR;
	mover->blocked_callback = SG_MECH_CALLBACK_BLOCKED_DOOR;
	approach = Node(&fixture, 30U, SG_MECH_NODE_TRIGGER, 0U);
	approach->delay_ms = 1000;
	approach->target_offset = 1U;
	approach->absmin_q8[0] = -64;
	approach->absmax_q8[0] = 64;
	approach->absmin_q8[1] = approach->absmin_q8[2] = -64;
	approach->absmax_q8[1] = approach->absmax_q8[2] = 64;
	Node(&fixture, 40U, SG_MECH_NODE_DOOR_MASTER,
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MASTER)->team_master_key = 40U;
	egress = Node(&fixture, 50U, SG_MECH_NODE_TRIGGER, 0U);
	egress->target_offset = 2U;
	egress->absmin_q8[0] = 736;
	egress->absmax_q8[0] = 864;
	egress->absmin_q8[1] = egress->absmin_q8[2] = -64;
	egress->absmax_q8[1] = egress->absmax_q8[2] = 64;
	Node(&fixture, 60U, SG_MECH_NODE_DOOR_MASTER,
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MASTER)->team_master_key = 60U;
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_OWNER, 0U);
	Edge(&fixture, 30U, 40U, SG_MECH_EDGE_TARGET, 0U);
	fixture.edges[2].delay_ms = 1000U;
	Edge(&fixture, 50U, 60U, SG_MECH_EDGE_TARGET, 0U);
	FixtureFinish(&fixture, 10U, 20U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismBindingCarrierStage(&binding,
		SG_CARRIER_DOOR_APPROACH, &trigger, keys, &key_count, &delay_ms));
	CHECK(trigger == &fixture.entities[2]);
	CHECK(key_count == 1U && keys[0] == 40U && delay_ms == 1000U);
	CHECK(SG_RuneMechanismBindingCarrierStage(&binding,
		SG_CARRIER_DOOR_EGRESS, &trigger, keys, &key_count, &delay_ms));
	CHECK(trigger == &fixture.entities[4]);
	CHECK(key_count == 1U && keys[0] == 60U && delay_ms == 0U);
	CHECK(SG_RuneMechanismBindingCarrierStageTriggerMatches(&binding,
		SG_CARRIER_DOOR_APPROACH, &fixture.entities[2]));
	CHECK(!SG_RuneMechanismBindingCarrierStageTriggerMatches(&binding,
		SG_CARRIER_DOOR_APPROACH, &fixture.entities[4]));
}

static void TestTeleport(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *entry;
	uint32_t failure_index = UINT32_MAX;

	FixtureBegin(&fixture, RL_TELEPORT, SG_MECHANISM_CONTROLLER_TELEPORT,
		0U, 1U);
	entry = Node(&fixture, 10U, SG_MECH_NODE_TELEPORT_TRIGGER,
		SG_MECH_NODEF_SYNTHETIC | SG_MECH_NODEF_TOUCHABLE);
	entry->owner_key = 20U;
	entry->touch_callback = SG_MECH_CALLBACK_TELEPORTER_TOUCH;
	Node(&fixture, 20U, SG_MECH_NODE_TELEPORTER, 0U);
	Node(&fixture, 30U, SG_MECH_NODE_TELEPORT_DEST, 0U);
	entry->target_offset = 1U;
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_OWNER, 0U);
	Edge(&fixture, 10U, 30U, SG_MECH_EDGE_TARGET, 0U);
	FixtureFinish(&fixture, 10U, 20U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismBindingResolveDestination(&binding) ==
		&fixture.entities[2]);
	CHECK(SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	topology_failure_key = 30U;
	CHECK(!SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == 0U);
	topology_failure_key = SG_MECH_NO_KEY;
	incarnation_failure_key = 30U;
	CHECK(!SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == 0U);
	incarnation_failure_key = SG_MECH_NO_KEY;

	fixture.edges[fixture.plan.first_edge + 1U].kind = SG_MECH_EDGE_OWNER;
	fixture.plan.closure_crc32 = ClosureCRC(&fixture);
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
}

static void TestPush(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *entry;
	rune_link_t foreign_link;
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	uint32_t failure_index = UINT32_MAX;
	uint32_t link_index = UINT32_MAX;
	size_t count = 99U;

	FixtureBegin(&fixture, RL_PUSH, SG_MECHANISM_CONTROLLER_PUSH, 0U, 1U);
	entry = Node(&fixture, 10U, SG_MECH_NODE_PUSH,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE);
	entry->touch_callback = SG_MECH_CALLBACK_TRIGGER_PUSH_TOUCH;
	entry->speed_q8 = 680U;
	entry->push_velocity[0] = -59.2648315f;
	entry->push_velocity[2] = 846.765747f;
	FixtureFinishPlanEdges(&fixture, 10U, SG_MECH_NO_KEY, 0U);
	foreign_link = fixture.link;
	CHECK(SG_RuneLinkIndex(&fixture.rune, &fixture.link, &link_index));
	CHECK(link_index == 0U);
	CHECK(!SG_RuneLinkIndex(&fixture.rune, &foreign_link, &link_index));
	CHECK(link_index == UINT32_MAX);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(binding.entry_entity == &fixture.entities[0]);
	CHECK(binding.mover_node == NULL && binding.mover_entity == NULL);
	CHECK(SG_RuneMechanismBindingMoverKeys(&binding, keys, &count));
	CHECK(count == 0U);
	CHECK(SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == UINT32_MAX);
	entry->push_velocity[2] = 846.0f;
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
}

static void TestTrainGate(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *button;
	rune_mechanism_node_t *train;
	rune_mechanism_node_t *closed;
	rune_mechanism_node_t *open;

	FixtureBegin(&fixture, RL_TRAIN, SG_MECHANISM_CONTROLLER_TRAIN,
		2000U, 1U);
	button = Node(&fixture, 10U, SG_MECH_NODE_BUTTON,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER);
	button->touch_callback = SG_MECH_CALLBACK_BUTTON_TOUCH;
	button->use_callback = SG_MECH_CALLBACK_BUTTON_USE;
	button->wait_ms = 1000;
	button->target_offset = 1U;
	train = Node(&fixture, 20U, SG_MECH_NODE_TRAIN,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
		SG_MECH_NODEF_MOVER);
	train->spawnflags = 2U;
	train->use_callback = SG_MECH_CALLBACK_TRAIN_USE;
	train->blocked_callback = SG_MECH_CALLBACK_BLOCKED_TRAIN;
	train->speed_q8 = train->accel_q8 = train->decel_q8 = 2400U;
	train->target_offset = train->targetname_offset = 1U;
	closed = Node(&fixture, 30U, SG_MECH_NODE_PATH_CORNER,
		SG_MECH_NODEF_TOUCHABLE | SG_MECH_NODEF_ONE_SHOT);
	closed->touch_callback = SG_MECH_CALLBACK_PATH_CORNER_TOUCH;
	closed->wait_ms = -1000;
	closed->target_offset = 1U;
	open = Node(&fixture, 40U, SG_MECH_NODE_PATH_CORNER,
		SG_MECH_NODEF_TOUCHABLE | SG_MECH_NODEF_ONE_SHOT);
	open->touch_callback = SG_MECH_CALLBACK_PATH_CORNER_TOUCH;
	open->wait_ms = -1000;
	open->target_offset = 1U;
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 20U, 40U, SG_MECH_EDGE_ROUTE_TARGET, 0U);
	Edge(&fixture, 30U, 40U, SG_MECH_EDGE_ROUTE_TARGET, 0U);
	Edge(&fixture, 40U, 30U, SG_MECH_EDGE_ROUTE_TARGET, 0U);
	FixtureFinish(&fixture, 10U, 20U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(binding.destination_node == closed);
	CHECK(binding.egress_node == open);
	CHECK(binding.destination_entity == &fixture.entities[2]);
	CHECK(binding.egress_entity == &fixture.entities[3]);
	CheckDoorMovers(&binding, 20U, SG_MECH_NO_KEY);
	CHECK(SG_RuneMechanismBindingCaptureOwned(&fixture.rune, 0U, &binding));
	train->think_callback = SG_MECH_CALLBACK_FUNC_TRAIN_FIND;
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismBindingCaptureOwned(&fixture.rune, 0U, &binding));
	train->think_callback = SG_MECH_CALLBACK_TRAIN_NEXT;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	train->think_callback = SG_MECH_CALLBACK_FUNC_TRAIN_FIND;

	fixture.edges[1].to_key = 30U;
	fixture.edges[fixture.plan.first_edge + 1U].to_key = 30U;
	fixture.plan.closure_crc32 = ClosureCRC(&fixture);
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
}

static void TestShootDoor(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *master;
	rune_mechanism_node_t *member;

	FixtureBegin(&fixture, RL_TRAIN,
		SG_MECHANISM_CONTROLLER_TRAIN_SHOOT, 1200U, 2U);
	fixture.link.mode = RLCM_PREOPEN;
	master = Node(&fixture, 10U, SG_MECH_NODE_DOOR_MASTER,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MASTER |
		SG_MECH_NODEF_SHOOTABLE);
	master->team_master_key = 10U;
	master->use_callback = SG_MECH_CALLBACK_USE_DOOR;
	master->blocked_callback = SG_MECH_CALLBACK_BLOCKED_DOOR;
	member = Node(&fixture, 20U, SG_MECH_NODE_DOOR_MEMBER,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MEMBER |
		SG_MECH_NODEF_SHOOTABLE);
	member->team_master_key = 10U;
	member->use_callback = SG_MECH_CALLBACK_USE_DOOR;
	member->blocked_callback = SG_MECH_CALLBACK_BLOCKED_DOOR;
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_TEAM, 0U);
	FixtureFinish(&fixture, 10U, 10U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(binding.entry_node == master && binding.mover_node == master);
	CHECK(binding.destination_node == NULL && binding.egress_node == NULL);
	CheckDoorMovers(&binding, 10U, 20U);
	CHECK(SG_RuneMechanismBindingCaptureOwned(&fixture.rune, 0U, &binding));

	member->flags &= ~SG_MECH_NODEF_SHOOTABLE;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	member->flags |= SG_MECH_NODEF_SHOOTABLE;
	fixture.link.mode = RLCM_NONE;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
}

static void TestRetiredInventoryIsNeverExecutable(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *entry;
	uint32_t failure_index = UINT32_MAX;

	/* The relay/speaker suffix is part of the sealed inventory but omitted
	 * from this platform plan.  Its exact retirement must not invalidate the
	 * unrelated live plan, while direct resolution remains fail-closed. */
	FixtureBegin(&fixture, RL_LIFT, SG_MECHANISM_CONTROLLER_PLATFORM, 0U, 1U);
	entry = Node(&fixture, 10U, SG_MECH_NODE_PLATFORM_TRIGGER,
	    SG_MECH_NODEF_SYNTHETIC | SG_MECH_NODEF_TOUCHABLE);
	entry->owner_key = 20U;
	entry->touch_callback = SG_MECH_CALLBACK_TOUCH_PLAT_CENTER;
	Node(&fixture, 20U, SG_MECH_NODE_PLATFORM, SG_MECH_NODEF_MOVER);
	Node(&fixture, 30U, SG_MECH_NODE_RELAY,
	    SG_MECH_NODEF_INVENTORY_ONLY | SG_MECH_NODEF_USABLE);
	Node(&fixture, 40U, SG_MECH_NODE_TARGET_SPEAKER,
	    SG_MECH_NODEF_INVENTORY_ONLY | SG_MECH_NODEF_USABLE);
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_OWNER, 0U);
	Edge(&fixture, 30U, 40U, SG_MECH_EDGE_TARGET, 0U);
	FixtureFinishPlanEdges(&fixture, 10U, 20U, 1U);
	retired_key = 30U;
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == UINT32_MAX);
	CHECK(SG_RuneMechanismBindingResolveNode(&binding, 30U) == NULL);

	/* Retirement of any node the plan executes is terminal, even though the
	 * global sealed inventory match itself remains valid. */
	retired_key = 10U;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(!SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == 0U);
	retired_key = 20U;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(!SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == 0U);

	/* A retired node in the authenticated plan closure is executable state,
	 * even when it is neither the entry nor mover.  Exercise the production
	 * plan-edge ordinal loop rather than inferring this from those two keys. */
	FixtureFinishPlanEdges(&fixture, 10U, 20U, 2U);
	retired_key = 40U;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	failure_index = UINT32_MAX;
	CHECK(!SG_RuneMechanismBindingsReady(&fixture.rune, &failure_index));
	CHECK(failure_index == 0U);
	retired_key = SG_MECH_NO_KEY;
}

static void DoorNodes(fixture_t *fixture, uint16_t entry_kind,
	uint16_t entry_flags, uint16_t touch_callback)
{
	rune_mechanism_node_t *entry = Node(fixture, 10U, entry_kind,
		entry_flags);
	rune_mechanism_node_t *master;
	rune_mechanism_node_t *member;

	entry->touch_callback = touch_callback;
	master = Node(fixture, 20U, SG_MECH_NODE_DOOR_MASTER,
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MASTER);
	master->team_master_key = 20U;
	member = Node(fixture, 30U, SG_MECH_NODE_DOOR_MEMBER,
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MEMBER);
	member->team_master_key = 20U;
	Node(fixture, 40U, SG_MECH_NODE_TARGET_SPEAKER, 0U);
}

static void TestAutoDoor(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count = 0U;

	FixtureBegin(&fixture, RL_DOOR, SG_MECHANISM_CONTROLLER_AUTO_DOOR,
		1000U, 2U);
	DoorNodes(&fixture, SG_MECH_NODE_AUTO_DOOR_TRIGGER,
		SG_MECH_NODEF_SYNTHETIC | SG_MECH_NODEF_TOUCHABLE,
		SG_MECH_CALLBACK_TOUCH_DOOR_TRIGGER);
	fixture.nodes[0].owner_key = 20U;
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_OWNER, 0U);
	Edge(&fixture, 20U, 30U, SG_MECH_EDGE_TEAM, 0U);
	Edge(&fixture, 20U, 40U, SG_MECH_EDGE_TARGET, 0U);
	FixtureFinish(&fixture, 10U, 20U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismBindingDoorAction(&binding));
	CheckDoorMovers(&binding, 20U, 30U);
	CHECK(SG_RuneMechanismBindingTopologyCurrent(&binding));
	/* A controller-owned canonical callback transition no longer matches the
	 * sealed preactivation snapshot, but the captured binding remains current. */
	topology_failure_key = 20U;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismBindingCaptureOwned(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
	/* A synchronous oracle pose leaves the sealed topology exact but does not
	 * constitute a valid live controller state.  Only the topology view admits
	 * that scoped snapshot. */
	topology_failure_key = SG_MECH_NO_KEY;
	execution_failure_key = 20U;
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
	CHECK(SG_RuneMechanismBindingTopologyCurrent(&binding));
	CHECK(!SG_RuneMechanismBindingMoverKeys(&binding, keys, &count));
	CHECK(SG_RuneMechanismBindingTopologyMoverKeys(&binding, keys, &count));
	CHECK(count == 2U && keys[0] == 20U && keys[1] == 30U);
	CHECK(SG_RuneMechanismBindingResolveNode(&binding, 20U) == NULL);
	CHECK(SG_RuneMechanismBindingResolveTopologyNode(&binding, 20U) ==
		&fixture.entities[1]);
	/* Immutable callback or relation drift fails both views. */
	topology_failure_key = 20U;
	CHECK(!SG_RuneMechanismBindingTopologyCurrent(&binding));
	CHECK(!SG_RuneMechanismBindingTopologyMoverKeys(&binding, keys, &count));
	CHECK(SG_RuneMechanismBindingResolveTopologyNode(&binding, 20U) == NULL);
	execution_failure_key = SG_MECH_NO_KEY;
	topology_failure_key = SG_MECH_NO_KEY;
}

static void TestDirectDoor(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	target_log_t log;

	FixtureBegin(&fixture, RL_DOOR,
		SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 500U, 2U);
	DoorNodes(&fixture, SG_MECH_NODE_TRIGGER,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE,
		SG_MECH_CALLBACK_TOUCH_MULTI);
	fixture.nodes[0].target_offset = 1U;
	fixture.nodes[1].target_offset = 1U;
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 10U, 30U, SG_MECH_EDGE_TARGET, 1U);
	Edge(&fixture, 10U, 40U, SG_MECH_EDGE_TARGET, 2U);
	Edge(&fixture, 20U, 30U, SG_MECH_EDGE_TEAM, 0U);
	Edge(&fixture, 20U, 40U, SG_MECH_EDGE_TARGET, 0U);
	FixtureFinish(&fixture, 10U, 20U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CheckDoorMovers(&binding, 20U, 30U);
	memset(&log, 0, sizeof(log));
	log.fail_key_after_first = SG_MECH_NO_KEY;
	CHECK(SG_RuneMechanismBindingDispatchTargets(&binding, 10U,
		RecordTarget, &log));
	CHECK(log.count == 3U);
	CHECK(log.keys[0] == 20U && log.ordinals[0] == 0U);
	CHECK(log.keys[1] == 30U && log.ordinals[1] == 1U);
	CHECK(log.keys[2] == 40U && log.ordinals[2] == 2U);
	/* A legacy name search could select some other matching entity.  The
	 * authenticated dispatcher never visits an entity absent from closure. */
	CHECK(NodeIndex(&fixture, 50U) < 0);
	memset(&log, 0, sizeof(log));
	log.fail_key_after_first = 30U;
	CHECK(!SG_RuneMechanismBindingDispatchTargets(&binding, 10U,
		RecordTarget, &log));
	CHECK(log.count == 1U);
	execution_failure_key = SG_MECH_NO_KEY;

	fixture.plan.expected_members = 1U;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	fixture.plan.expected_members = 2U;
	topology_failure_key = 30U;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	topology_failure_key = SG_MECH_NO_KEY;
	fixture.plan.closure_crc32 ^= UINT32_C(1);
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	fixture.plan.closure_crc32 ^= UINT32_C(1);
	catalog_ready = 0;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
}

static void TestButtonDoor(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;

	FixtureBegin(&fixture, RL_BUTTON_DOOR,
		SG_MECHANISM_CONTROLLER_BUTTON_DOOR, 3000U, 2U);
	DoorNodes(&fixture, SG_MECH_NODE_BUTTON,
		SG_MECH_NODEF_TOUCHABLE | SG_MECH_NODEF_MOVER,
		SG_MECH_CALLBACK_BUTTON_TOUCH);
	fixture.nodes[0].target_offset = 1U;
	fixture.nodes[1].target_offset = 1U;
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 20U, 30U, SG_MECH_EDGE_TEAM, 0U);
	Edge(&fixture, 20U, 40U, SG_MECH_EDGE_TARGET, 0U);
	FixtureFinish(&fixture, 10U, 20U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismBindingDoorAction(&binding));
	CheckDoorMovers(&binding, 20U, 30U);
	CHECK(binding.entry_entity == &fixture.entities[0]);
	CHECK(binding.mover_entity == &fixture.entities[1]);
}

static void TestRelayWall(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *node;
	uint32_t index;

	FixtureBegin(&fixture, RL_BUTTON_DOOR,
		SG_MECHANISM_CONTROLLER_RELAY_DOOR, 4000U, 1U);
	fixture.link.mode = RLCM_PREOPEN;
	node = Node(&fixture, 10U, SG_MECH_NODE_BUTTON,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER);
	node->touch_callback = SG_MECH_CALLBACK_BUTTON_TOUCH;
	node->use_callback = SG_MECH_CALLBACK_BUTTON_USE;
	node->target_offset = 1U;
	node->delay_ms = 200;
	node->wait_ms = 4000;
	node->speed_q8 = node->accel_q8 = node->decel_q8 = 800U;
	node = Node(&fixture, 20U, SG_MECH_NODE_RELAY,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE);
	node->use_callback = SG_MECH_CALLBACK_USE_TRIGGER_RELAY;
	node->target_offset = node->targetname_offset = 1U;
	node = Node(&fixture, 30U, SG_MECH_NODE_RELAY,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE);
	node->use_callback = SG_MECH_CALLBACK_USE_TRIGGER_RELAY;
	node->target_offset = node->targetname_offset = 1U;
	node->delay_ms = 4000;
	node = Node(&fixture, 40U, SG_MECH_NODE_TOGGLE_WALL,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
		SG_MECH_NODEF_MOVER);
	node->use_callback = SG_MECH_CALLBACK_USE_FUNC_WALL;
	node->spawnflags = 7U;
	node->target_offset = 1U;
	node->targetname_offset = 1U;
	node = Node(&fixture, 50U, SG_MECH_NODE_TARGET_SPEAKER,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE);
	node->spawnflags = 1U;
	node->use_callback = SG_MECH_CALLBACK_USE_TARGET_SPEAKER;
	node->targetname_offset = 1U;
	node = Node(&fixture, 60U, SG_MECH_NODE_TRIGGER_HURT,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		SG_MECH_NODEF_USABLE);
	node->touch_callback = SG_MECH_CALLBACK_TOUCH_HURT;
	node->use_callback = SG_MECH_CALLBACK_USE_HURT;
	node->spawnflags = 2U;
	node->targetname_offset = 1U;
	node = Node(&fixture, 70U, SG_MECH_NODE_TARGET_SPEAKER,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE);
	node->spawnflags = 1U;
	node->use_callback = SG_MECH_CALLBACK_USE_TARGET_SPEAKER;
	node->targetname_offset = 1U;

	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 10U, 30U, SG_MECH_EDGE_TARGET, 1U);
	Edge(&fixture, 20U, 40U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 20U, 50U, SG_MECH_EDGE_TARGET, 1U);
	Edge(&fixture, 20U, 60U, SG_MECH_EDGE_TARGET, 2U);
	Edge(&fixture, 20U, 70U, SG_MECH_EDGE_TARGET, 3U);
	Edge(&fixture, 30U, 40U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 30U, 50U, SG_MECH_EDGE_TARGET, 1U);
	Edge(&fixture, 30U, 60U, SG_MECH_EDGE_TARGET, 2U);
	Edge(&fixture, 30U, 70U, SG_MECH_EDGE_TARGET, 3U);
	fixture.edges[0].delay_ms = 200U;
	fixture.edges[1].delay_ms = 200U;
	for (index = 6U; index < 10U; index++)
		fixture.edges[index].delay_ms = 4000U;
	FixtureFinish(&fixture, 10U, 40U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(binding.destination_node && binding.destination_node->key == 20U);
	CHECK(binding.egress_node && binding.egress_node->key == 30U);
	CheckDoorMovers(&binding, 40U, SG_MECH_NO_KEY);
	fixture.nodes[4].spawnflags = 8U;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	fixture.nodes[4].spawnflags = 1U;

	fixture.edges[8].to_key = 70U;
	fixture.edges[fixture.plan.first_edge + 8U].to_key = 70U;
	fixture.plan.closure_crc32 = ClosureCRC(&fixture);
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	fixture.edges[8].to_key = 60U;
	fixture.edges[fixture.plan.first_edge + 8U].to_key = 60U;
	fixture.edges[8].ordinal = 3U;
	fixture.edges[fixture.plan.first_edge + 8U].ordinal = 3U;
	fixture.plan.closure_crc32 = ClosureCRC(&fixture);
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
}

static void TestTimedVault(void)
{
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *node;
	uint32_t key;

	FixtureBegin(&fixture, RL_BUTTON_DOOR,
		SG_MECHANISM_CONTROLLER_TIMED_VAULT, 10000U, 2U);
	fixture.link.mode = RLCM_PREOPEN;
	node = Node(&fixture, 10U, SG_MECH_NODE_BUTTON,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE |
		SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER);
	node->touch_callback = SG_MECH_CALLBACK_BUTTON_TOUCH;
	node->use_callback = SG_MECH_CALLBACK_BUTTON_USE;
	node->target_offset = 1U;
	node->wait_ms = 10000;
	node = Node(&fixture, 20U, SG_MECH_NODE_RELAY,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE);
	node->use_callback = SG_MECH_CALLBACK_USE_TRIGGER_RELAY;
	node->target_offset = node->targetname_offset = 1U;
	node->delay_ms = 1000;
	node = Node(&fixture, 30U, SG_MECH_NODE_RELAY,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE);
	node->use_callback = SG_MECH_CALLBACK_USE_TRIGGER_RELAY;
	node->target_offset = node->targetname_offset = 1U;
	node->delay_ms = 10000;
	node = Node(&fixture, 40U, SG_MECH_NODE_DOOR_MASTER,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MASTER);
	node->use_callback = SG_MECH_CALLBACK_USE_DOOR;
	node->blocked_callback = SG_MECH_CALLBACK_BLOCKED_DOOR;
	node->team_master_key = 40U;
	node->target_offset = node->targetname_offset = 1U;
	node = Node(&fixture, 50U, SG_MECH_NODE_DOOR_MEMBER,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MEMBER);
	node->use_callback = SG_MECH_CALLBACK_USE_DOOR;
	node->blocked_callback = SG_MECH_CALLBACK_BLOCKED_DOOR;
	node->team_master_key = 40U;
	node->target_offset = node->targetname_offset = 1U;
	for (key = 60U; key <= 130U; key += 10U)
	{
		node = Node(&fixture, key, SG_MECH_NODE_TARGET_LASER,
			SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE);
		node->spawnflags = (key & 20U) ? 3U : 17U;
		node->use_callback = SG_MECH_CALLBACK_USE_TARGET_LASER;
		node->think_callback = SG_MECH_CALLBACK_THINK_TARGET_LASER;
		node->target_offset = node->targetname_offset = 1U;
	}
	node = Node(&fixture, 140U, SG_MECH_NODE_TARGET_SPEAKER,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE);
	node->spawnflags = 1U;
	node->use_callback = SG_MECH_CALLBACK_USE_TARGET_SPEAKER;
	node->targetname_offset = 1U;
	node = Node(&fixture, 150U, SG_MECH_NODE_AREAPORTAL,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE);
	node->use_callback = SG_MECH_CALLBACK_USE_AREAPORTAL;
	node->targetname_offset = 1U;
	Edge(&fixture, 10U, 20U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 10U, 30U, SG_MECH_EDGE_TARGET, 1U);
	Edge(&fixture, 10U, 40U, SG_MECH_EDGE_TARGET, 2U);
	Edge(&fixture, 10U, 50U, SG_MECH_EDGE_TARGET, 3U);
	for (key = 60U; key <= 140U; key += 10U)
	{
		Edge(&fixture, 20U, key, SG_MECH_EDGE_TARGET, (key - 60U) / 10U);
		fixture.edges[fixture.num_inventory_edges - 1U].delay_ms = 1000U;
	}
	for (key = 60U; key <= 140U; key += 10U)
	{
		Edge(&fixture, 30U, key, SG_MECH_EDGE_TARGET, (key - 60U) / 10U);
		fixture.edges[fixture.num_inventory_edges - 1U].delay_ms = 10000U;
	}
	Edge(&fixture, 40U, 150U, SG_MECH_EDGE_TARGET, 0U);
	Edge(&fixture, 40U, 50U, SG_MECH_EDGE_TEAM, 0U);
	Edge(&fixture, 50U, 150U, SG_MECH_EDGE_TARGET, 0U);
	FixtureFinish(&fixture, 10U, 40U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(binding.destination_node && binding.destination_node->key == 20U);
	CHECK(binding.egress_node && binding.egress_node->key == 30U);
	CheckDoorMovers(&binding, 40U, 50U);

	fixture.edges[17U].to_key = 130U;
	fixture.edges[fixture.plan.first_edge + 17U].to_key = 130U;
	fixture.plan.closure_crc32 = ClosureCRC(&fixture);
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
}

static void TestTrainStation(void)
{
	static const uint32_t route[SG_TRAIN_STATION_ROUTE_CORNERS] = {
		28U, 29U, 30U, 31U, 32U, 33U, 34U,
		35U, 36U, 37U, 38U, 41U, 40U, 39U
	};
	fixture_t fixture;
	sg_rune_mechanism_binding_t binding;
	rune_mechanism_node_t *node;
	uint32_t key;
	uint32_t index;

	FixtureBegin(&fixture, RL_TRAIN,
		SG_MECHANISM_CONTROLLER_TRAIN_STATION, 3000U, 2U);
	fixture.link.mode = RLCM_RIDE;
	node = Node(&fixture, 5U, SG_MECH_NODE_TRAIN,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MASTER);
	node->team_master_key = 5U;
	node->spawnflags = 1U;
	node->use_callback = SG_MECH_CALLBACK_TRAIN_USE;
	node->think_callback = SG_MECH_CALLBACK_TRAIN_NEXT;
	node->blocked_callback = SG_MECH_CALLBACK_BLOCKED_TRAIN;
	node->speed_q8 = node->accel_q8 = node->decel_q8 = 3200U;
	node->target_offset = 1U;
	for (key = 28U; key <= 41U; key++)
	{
		node = Node(&fixture, key, SG_MECH_NODE_PATH_CORNER,
			SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE);
		node->touch_callback = SG_MECH_CALLBACK_PATH_CORNER_TOUCH;
		node->wait_ms = key == 28U || key == 35U ? 3000 : 0;
		node->target_offset = node->targetname_offset = 1U;
	}
	node = Node(&fixture, 42U, SG_MECH_NODE_TRAIN,
		SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
		SG_MECH_NODEF_MOVER | SG_MECH_NODEF_TEAM_MEMBER);
	node->team_master_key = 5U;
	node->spawnflags = 1U;
	node->use_callback = SG_MECH_CALLBACK_TRAIN_USE;
	node->think_callback = SG_MECH_CALLBACK_TRAIN_NEXT;
	node->blocked_callback = SG_MECH_CALLBACK_BLOCKED_TRAIN;
	node->speed_q8 = node->accel_q8 = node->decel_q8 = 3200U;
	node->target_offset = 1U;
	Edge(&fixture, 5U, 42U, SG_MECH_EDGE_TEAM, 0U);
	Edge(&fixture, 5U, 29U, SG_MECH_EDGE_ROUTE_TARGET, 0U);
	Edge(&fixture, 42U, 36U, SG_MECH_EDGE_ROUTE_TARGET, 0U);
	for (index = 0U; index < SG_TRAIN_STATION_ROUTE_CORNERS; index++)
		Edge(&fixture, route[index],
			route[(index + 1U) % SG_TRAIN_STATION_ROUTE_CORNERS],
			SG_MECH_EDGE_ROUTE_TARGET, 0U);
	fixture.seeds[0].origin[0] = -64.0f;
	fixture.seeds[1].origin[0] = 200.0f;
	fixture.link.anchor[0] = -96.0f;
	fixture.link.mechanism_anchor[2] = 16.0f;
	FixtureFinishPlanEdges(&fixture, 28U, 5U, 17U);
	CHECK(SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(binding.destination_node && binding.destination_node->key == 35U);
	CHECK(binding.egress_node && binding.egress_node->key == 42U);
	CHECK(binding.plan && binding.plan->flags ==
		(SG_RUNE_CODEC_PLANF_ATOMIC |
		 SG_RUNE_CODEC_PLANF_REQUIRES_LEASE));
	CHECK(SG_RuneMechanismStationBindingCapture(&fixture.rune, 0U,
		&binding));
	fixture.link.anchor[0] += 8.0f;
	CHECK(!SG_RuneMechanismStationBindingCurrent(&binding));
	fixture.link.anchor[0] -= 8.0f;
	CHECK(SG_RuneMechanismStationBindingCapture(&fixture.rune, 0U,
		&binding));
	fixture.link.anchor[0] = fixture.seeds[0].origin[0];
	CHECK(!SG_RuneMechanismStationBindingCapture(&fixture.rune, 0U,
		&binding));
	fixture.link.anchor[0] = -96.0f;
	memcpy(fixture.link.anchor, fixture.link.mechanism_anchor,
		sizeof(fixture.link.anchor));
	CHECK(!SG_RuneMechanismStationBindingCapture(&fixture.rune, 0U,
		&binding));
	fixture.link.anchor[0] = -96.0f;
	fixture.link.anchor[1] = fixture.link.anchor[2] = 0.0f;
	execution_failure_key = 28U;
	CHECK(SG_RuneMechanismStationBindingCapture(&fixture.rune, 0U,
		&binding));
	execution_failure_key = SG_MECH_NO_KEY;
	topology_failure_key = 5U;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
	CHECK(SG_RuneMechanismStationBindingCapture(&fixture.rune, 0U,
		&binding));
	topology_failure_key = SG_MECH_NO_KEY;
	incarnation_failure_key = 5U;
	CHECK(!SG_RuneMechanismStationBindingCapture(&fixture.rune, 0U,
		&binding));
	incarnation_failure_key = SG_MECH_NO_KEY;

	fixture.nodes[8].wait_ms = 0;
	CHECK(!SG_RuneMechanismBindingCapture(&fixture.rune, 0U, &binding));
}

static void TestExecutionStates(void)
{
	sg_mech_execution_state_t state;

	memset(&state, 0, sizeof(state));
	state.controller_kind = SG_MECHANISM_CONTROLLER_AUTO_DOOR;
	state.node_kind = SG_MECH_NODE_DOOR_MASTER;
	state.fixed_callbacks_match = 1;
	state.touch_matches = 1;
	state.stopped = 1;
	state.motion_state = SG_MECH_MOTION_AT_ORIGIN;
	state.think_role = SG_MECH_EXEC_THINK_SEALED;
	CHECK(SG_MechExecutionStateValid(&state));
	state.stopped = 0;
	state.motion_state = SG_MECH_MOTION_TO_DESTINATION;
	state.think_role = SG_MECH_EXEC_THINK_LINEAR_BEGIN;
	state.end_role = SG_MECH_EXEC_END_DOOR_DESTINATION;
	CHECK(SG_MechExecutionStateValid(&state));
	state.think_role = SG_MECH_EXEC_THINK_LINEAR_FINAL;
	CHECK(SG_MechExecutionStateValid(&state));
	state.motion_state = SG_MECH_MOTION_AT_DESTINATION;
	state.think_role = SG_MECH_EXEC_THINK_LINEAR_DONE;
	state.stopped = 1;
	CHECK(SG_MechExecutionStateValid(&state));
	state.think_role = SG_MECH_EXEC_THINK_UNKNOWN;
	CHECK(!SG_MechExecutionStateValid(&state));
	state.think_role = SG_MECH_EXEC_THINK_LINEAR_DONE;
	state.fixed_callbacks_match = 0;
	CHECK(!SG_MechExecutionStateValid(&state));

	memset(&state, 0, sizeof(state));
	state.controller_kind = SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
	state.node_kind = SG_MECH_NODE_BUTTON;
	state.fixed_callbacks_match = 1;
	state.touch_matches = 1;
	state.motion_state = SG_MECH_MOTION_TO_DESTINATION;
	state.think_role = SG_MECH_EXEC_THINK_LINEAR_BEGIN;
	state.end_role = SG_MECH_EXEC_END_BUTTON_DESTINATION;
	CHECK(SG_MechExecutionStateValid(&state));
	state.motion_state = SG_MECH_MOTION_AT_DESTINATION;
	state.think_role = SG_MECH_EXEC_THINK_LINEAR_DONE;
	state.stopped = 1;
	CHECK(SG_MechExecutionStateValid(&state));
	state.think_role = SG_MECH_EXEC_THINK_BUTTON_RETURN;
	state.nextthink_pending = 1;
	CHECK(SG_MechExecutionStateValid(&state));
	state.think_role = SG_MECH_EXEC_THINK_ANGULAR_FINAL;
	CHECK(!SG_MechExecutionStateValid(&state));

	memset(&state, 0, sizeof(state));
	state.controller_kind = SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
	state.node_kind = SG_MECH_NODE_TRIGGER;
	state.fixed_callbacks_match = 1;
	state.touch_matches = 1;
	state.think_role = SG_MECH_EXEC_THINK_MULTI_WAIT;
	CHECK(SG_MechExecutionStateValid(&state));
	state.touch_matches = 0;
	CHECK(!SG_MechExecutionStateValid(&state));
}

int main(void)
{
	TestPlatform();
	TestStockPlatformRideWithAutomaticDoorEgress();
	TestTriggeredVerticalDoorLift();
	TestDescendingCarrierStagesUseAnchorIdentity();
	TestTeleport();
	TestPush();
	TestTrainGate();
	TestShootDoor();
	TestRetiredInventoryIsNeverExecutable();
	TestAutoDoor();
	TestDirectDoor();
	TestButtonDoor();
	TestRelayWall();
	TestTimedVault();
	TestTrainStation();
	TestExecutionStates();
	if (failures != 0)
	{
		fprintf(stderr, "%d rune binding test(s) failed\n", failures);
		return 1;
	}
	puts("rune mechanism binding tests passed");
	return 0;
}
