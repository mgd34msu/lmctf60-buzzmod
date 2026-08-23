/* Real sealed mechanism-catalog regressions. */
#include "g_local.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_rune.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define TEST_EDICTS 9

game_locals_t game;
level_locals_t level;
game_import_t gi;
game_export_t globals;
edict_t *g_edicts;
sg_host_t sg_host;

static edict_t test_edicts[TEST_EDICTS];
static int failures;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#condition_); \
		failures++; \
	} \
} while (0)

#define TOUCH_CALLBACK(name_) \
	void name_(edict_t *self, edict_t *other, cplane_t *plane, \
		csurface_t *surface) \
	{ \
		(void)self; (void)other; (void)plane; (void)surface; \
	}
#define USE_CALLBACK(name_) \
	void name_(edict_t *self, edict_t *other, edict_t *activator) \
	{ \
		(void)self; (void)other; (void)activator; \
	}
#define THINK_CALLBACK(name_) \
	void name_(edict_t *self) \
	{ \
		(void)self; \
	}
#define BLOCKED_CALLBACK(name_) \
	void name_(edict_t *self, edict_t *other) \
	{ \
		(void)self; (void)other; \
	}

TOUCH_CALLBACK(Touch_Multi)
TOUCH_CALLBACK(Touch_DoorTrigger)
TOUCH_CALLBACK(button_touch)
TOUCH_CALLBACK(Touch_Plat_Center)
TOUCH_CALLBACK(trigger_push_touch)
TOUCH_CALLBACK(teleporter_touch)
TOUCH_CALLBACK(path_corner_touch)
TOUCH_CALLBACK(Touch_Item)
TOUCH_CALLBACK(UnknownTouch)

USE_CALLBACK(Use_Multi)
USE_CALLBACK(button_use)
USE_CALLBACK(trigger_relay_use)
USE_CALLBACK(door_use)
USE_CALLBACK(trigger_enable)
USE_CALLBACK(Use_Plat)
USE_CALLBACK(train_use)
USE_CALLBACK(trigger_elevator_use)
USE_CALLBACK(door_secret_use)
USE_CALLBACK(Use_Target_Speaker)
USE_CALLBACK(Use_Areaportal)
USE_CALLBACK(UnknownUse)

THINK_CALLBACK(multi_wait)
THINK_CALLBACK(button_wait)
THINK_CALLBACK(button_return)
THINK_CALLBACK(button_done)
void button_killed(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point)
{
	(void)self; (void)inflictor; (void)attacker; (void)damage; (void)point;
}
void door_killed(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point)
{
	(void)self; (void)inflictor; (void)attacker; (void)damage; (void)point;
}
THINK_CALLBACK(Think_CalcMoveSpeed)
THINK_CALLBACK(Think_SpawnDoorTrigger)
THINK_CALLBACK(plat_go_down)
THINK_CALLBACK(plat_hit_top)
THINK_CALLBACK(plat_hit_bottom)
THINK_CALLBACK(door_go_down)
THINK_CALLBACK(door_hit_top)
THINK_CALLBACK(door_hit_bottom)
THINK_CALLBACK(Move_Begin)
THINK_CALLBACK(Move_Final)
THINK_CALLBACK(Move_Done)
THINK_CALLBACK(AngleMove_Begin)
THINK_CALLBACK(AngleMove_Final)
THINK_CALLBACK(AngleMove_Done)
THINK_CALLBACK(Think_AccelMove)
THINK_CALLBACK(train_next)
THINK_CALLBACK(train_wait)
THINK_CALLBACK(trigger_elevator_init)
THINK_CALLBACK(Think_Delay)

BLOCKED_CALLBACK(door_blocked)
BLOCKED_CALLBACK(plat_blocked)
BLOCKED_CALLBACK(train_blocked)
BLOCKED_CALLBACK(door_secret_blocked)

void SG_HooksInit(void)
{
}

int Q_stricmp(const char *left, const char *right)
{
	return strcasecmp(left, right);
}

/* The production catalog seals after func_train_find has run. */
void func_train_find(edict_t *self)
{
	edict_t *corner = NULL;
	int index;

	for (index = 1; self && self->target && index < globals.num_edicts; index++)
		if (test_edicts[index].inuse && test_edicts[index].targetname &&
		    !strcasecmp(test_edicts[index].targetname, self->target))
		{
			corner = &test_edicts[index];
			break;
		}
	CHECK(corner != NULL);
	if (!corner)
		return;
	self->target = corner->target;
	VectorSubtract(corner->s.origin, self->mins, self->s.origin);
}

static void *TestAlloc(int bytes)
{
	return bytes > 0 ? calloc(1U, (size_t)bytes) : NULL;
}

static void TestFree(void *memory)
{
	free(memory);
}

static const rune_mechanism_node_t *NodeByKey(
	const sg_mech_catalog_view_t *view, uint32_t key)
{
	uint32_t index;

	for (index = 0U; view && index < view->num_nodes; index++)
		if (view->nodes[index].key == key)
			return &view->nodes[index];
	return NULL;
}

static int HasEdge(const sg_mech_catalog_view_t *view, uint32_t from,
	uint32_t to, uint16_t kind, uint16_t ordinal)
{
	uint32_t index;

	for (index = 0U; view && index < view->num_edges; index++)
		if (view->edges[index].from_key == from &&
		    view->edges[index].to_key == to &&
		    view->edges[index].kind == kind &&
		    view->edges[index].ordinal == ordinal)
			return 1;
	return 0;
}

static void InitializeEntity(uint32_t key, const char *classname)
{
	edict_t *entity = &test_edicts[key];

	memset(entity, 0, sizeof(*entity));
	entity->inuse = true;
	entity->s.number = (int)key;
	entity->classname = (char *)classname;
	entity->wait = 1.0f;
	SG_MechCatalogEntityInitialized(entity);
	SG_MechCatalogDeclared(entity, key, classname);
}

static void BuildCatalog(sg_mech_catalog_view_t *view)
{
	edict_t *trigger;
	edict_t *master;
	edict_t *member;
	edict_t *speaker;
	edict_t *teleporter;
	edict_t *custom_teleport;
	edict_t *button;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = 8;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;

	SG_MechCatalogBegin();
	InitializeEntity(1U, "trigger_multiple");
	InitializeEntity(2U, "func_door");
	InitializeEntity(3U, "func_door");
	InitializeEntity(4U, "target_speaker");
	InitializeEntity(5U, "misc_teleporter");
	InitializeEntity(6U, "trigger_teleport");
	InitializeEntity(7U, "func_button");

	trigger = &test_edicts[1];
	master = &test_edicts[2];
	member = &test_edicts[3];
	speaker = &test_edicts[4];
	teleporter = &test_edicts[5];
	custom_teleport = &test_edicts[6];
	button = &test_edicts[7];

	trigger->target = "door-team";
	trigger->touch = Touch_Multi;
	trigger->use = Use_Multi;

	master->targetname = "door-team";
	master->target = "door-sound";
	master->movetype = MOVETYPE_PUSH;
	master->solid = SOLID_BSP;
	master->use = door_use;
	master->blocked = door_blocked;
	master->teammaster = master;
	master->teamchain = member;
	master->moveinfo.state = 1;
	master->moveinfo.wait = 1.0f;

	member->targetname = "door-team";
	member->movetype = MOVETYPE_PUSH;
	member->solid = SOLID_BSP;
	member->use = door_use;
	member->blocked = door_blocked;
	member->flags = FL_TEAMSLAVE;
	member->teammaster = master;
	member->moveinfo.state = 1;
	member->moveinfo.wait = 1.0f;

	speaker->targetname = "door-sound";
	speaker->use = Use_Target_Speaker;
	custom_teleport->touch = teleporter_touch;
	custom_teleport->owner = teleporter;

	button->target = "door-team";
	button->movetype = MOVETYPE_STOP;
	button->solid = SOLID_BSP;
	button->touch = button_touch;
	button->use = button_use;
	button->wait = 3.0f;
	button->moveinfo.wait = 3.0f;
	button->moveinfo.speed = 40.0f;
	button->moveinfo.accel = 40.0f;
	button->moveinfo.decel = 40.0f;
	button->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	VectorSet(button->moveinfo.start_origin, 10.0f, 20.0f, 30.0f);
	VectorSet(button->moveinfo.end_origin, 10.0f, 20.0f, 28.0f);
	VectorCopy(button->moveinfo.start_origin, button->s.origin);

	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(strcmp(SG_MechCatalogReason(), "ok") == 0);
	CHECK(SG_MechCatalogSnapshot(view) == SG_MECH_CATALOG_READY);
}

static void TestSealedCatalog(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *trigger_node;
	const rune_mechanism_node_t *master_node;
	const rune_mechanism_node_t *member_node;
	const rune_mechanism_node_t *custom_node;
	const rune_mechanism_node_t *button_node;
	sg_mech_button_endpoints_t endpoints;
	rune_mechanism_node_t *node_copy;
	rune_mechanism_edge_t *edge_copy;
	unsigned char *string_copy;
	void (*saved_touch)(edict_t *, edict_t *, cplane_t *, csurface_t *);
	void (*saved_use)(edict_t *, edict_t *, edict_t *);
	void (*saved_think)(edict_t *);
	void (*saved_blocked)(edict_t *, edict_t *);
	edict_t *saved_pointer;
	char *saved_string;

	memset(&view, 0, sizeof(view));
	BuildCatalog(&view);
	CHECK(view.num_nodes == 7U);
	CHECK(view.num_edges == 7U);
	CHECK(view.string_bytes > 1U);
	CHECK(HasEdge(&view, 1U, 2U, SG_MECH_EDGE_TARGET, 0U));
	CHECK(HasEdge(&view, 1U, 3U, SG_MECH_EDGE_TARGET, 1U));
	CHECK(HasEdge(&view, 2U, 4U, SG_MECH_EDGE_TARGET, 0U));
	CHECK(HasEdge(&view, 2U, 3U, SG_MECH_EDGE_TEAM, 0U));
	CHECK(HasEdge(&view, 6U, 5U, SG_MECH_EDGE_OWNER, 0U));
	CHECK(HasEdge(&view, 7U, 2U, SG_MECH_EDGE_TARGET, 0U));
	CHECK(HasEdge(&view, 7U, 3U, SG_MECH_EDGE_TARGET, 1U));
	trigger_node = NodeByKey(&view, 1U);
	master_node = NodeByKey(&view, 2U);
	member_node = NodeByKey(&view, 3U);
	custom_node = NodeByKey(&view, 6U);
	button_node = NodeByKey(&view, 7U);
	CHECK(trigger_node != NULL);
	CHECK(master_node != NULL);
	CHECK(member_node != NULL);
	CHECK(custom_node != NULL);
	CHECK(button_node != NULL);
	CHECK(custom_node && custom_node->kind == SG_MECH_NODE_OTHER_TRIGGER);
	CHECK(custom_node &&
	    (custom_node->flags & SG_MECH_NODEF_INVENTORY_ONLY) != 0U);
	CHECK(custom_node && custom_node->speed_q8 == 0U &&
	    custom_node->accel_q8 == 0U && custom_node->decel_q8 == 0U);
	CHECK(button_node && button_node->speed_q8 == 320U &&
	    button_node->accel_q8 == 320U && button_node->decel_q8 == 320U);
	CHECK(SG_MechCatalogMatches(view.nodes, view.num_nodes, view.edges,
	    view.num_edges, view.strings, view.string_bytes));
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, trigger_node));
	CHECK(SG_MechCatalogEntityTopologyMatches(2U, master_node));
	CHECK(SG_MechCatalogEntityTopologyMatches(3U, member_node));
	CHECK(SG_MechCatalogEntityTopologyMatches(6U, custom_node));
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, trigger_node,
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR));
	CHECK(SG_MechCatalogEntityExecutionMatches(2U, master_node,
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR));
	/* Nonmovers stay outside executable-mover motion authentication. */
	test_edicts[1].moveinfo.speed = NAN;
	test_edicts[1].moveinfo.accel = INFINITY;
	test_edicts[1].moveinfo.decel = 1.0e30f;
	test_edicts[1].moveinfo.wait = 0.0f;
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, trigger_node));
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, trigger_node,
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR));
	test_edicts[1].moveinfo.speed = 0.0f;
	test_edicts[1].moveinfo.accel = 0.0f;
	test_edicts[1].moveinfo.decel = 0.0f;
	test_edicts[1].moveinfo.wait = 0.0f;
	CHECK(SG_MechCatalogResolveEntity(2U, master_node) == &test_edicts[2]);

	memset(&endpoints, 0, sizeof(endpoints));
	CHECK(SG_MechCatalogButtonBottomEndpoints(7U, button_node,
	    &test_edicts[7], &endpoints));
	CHECK(endpoints.start_q8[0] == 80 && endpoints.start_q8[1] == 160 &&
	    endpoints.start_q8[2] == 240);
	CHECK(endpoints.end_q8[0] == 80 && endpoints.end_q8[1] == 160 &&
	    endpoints.end_q8[2] == 224);
	test_edicts[7].moveinfo.end_origin[2] = 27.875f;
	CHECK(!SG_MechCatalogButtonEndpoints(7U, button_node,
	    &test_edicts[7], &endpoints));
	test_edicts[7].moveinfo.end_origin[2] = 28.0f;
	CHECK(SG_MechCatalogButtonBottomEndpoints(7U, button_node,
	    &test_edicts[7], &endpoints));
	test_edicts[7].moveinfo.end_origin[2] = 28.03125f;
	CHECK(!SG_MechCatalogButtonEndpoints(7U, button_node,
	    &test_edicts[7], &endpoints));
	test_edicts[7].moveinfo.end_origin[2] = 28.0f;
	test_edicts[7].moveinfo.state = SG_PLAT_STATE_TOP;
	VectorCopy(test_edicts[7].moveinfo.end_origin,
	    test_edicts[7].s.origin);
	CHECK(SG_MechCatalogButtonEndpoints(7U, button_node, &test_edicts[7],
	    &endpoints));
	CHECK(!SG_MechCatalogButtonBottomEndpoints(7U, button_node,
	    &test_edicts[7], &endpoints));
	test_edicts[7].moveinfo.state = SG_PLAT_STATE_BOTTOM;
	VectorCopy(test_edicts[7].moveinfo.start_origin,
	    test_edicts[7].s.origin);
	CHECK(SG_MechCatalogButtonBottomEndpoints(7U, button_node,
	    &test_edicts[7], &endpoints));
	test_edicts[7].moveinfo.wait = 4.0f;
	CHECK(!SG_MechCatalogButtonBottomEndpoints(7U, button_node,
	    &test_edicts[7], &endpoints));
	test_edicts[7].moveinfo.wait = 3.0f;
	CHECK(SG_MechCatalogButtonBottomEndpoints(7U, button_node,
	    &test_edicts[7], &endpoints));

	saved_touch = test_edicts[1].touch;
	test_edicts[1].touch = UnknownTouch;
	CHECK(!SG_MechCatalogEntityTopologyMatches(1U, trigger_node));
	test_edicts[1].touch = saved_touch;
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, trigger_node));

	saved_use = test_edicts[1].use;
	test_edicts[1].use = UnknownUse;
	CHECK(!SG_MechCatalogEntityTopologyMatches(1U, trigger_node));
	test_edicts[1].use = saved_use;
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, trigger_node));

	saved_think = test_edicts[2].think;
	test_edicts[2].think = Think_Delay;
	CHECK(!SG_MechCatalogEntityTopologyMatches(2U, master_node));
	test_edicts[2].think = saved_think;
	CHECK(SG_MechCatalogEntityTopologyMatches(2U, master_node));

	saved_blocked = test_edicts[2].blocked;
	test_edicts[2].blocked = plat_blocked;
	CHECK(!SG_MechCatalogEntityTopologyMatches(2U, master_node));
	test_edicts[2].blocked = saved_blocked;
	CHECK(SG_MechCatalogEntityTopologyMatches(2U, master_node));

	saved_string = test_edicts[1].target;
	test_edicts[1].target = "different-door";
	CHECK(!SG_MechCatalogEntityTopologyMatches(1U, trigger_node));
	test_edicts[1].target = saved_string;
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, trigger_node));

	saved_pointer = test_edicts[6].owner;
	test_edicts[6].owner = NULL;
	CHECK(!SG_MechCatalogEntityTopologyMatches(6U, custom_node));
	test_edicts[6].owner = saved_pointer;
	CHECK(SG_MechCatalogEntityTopologyMatches(6U, custom_node));

	saved_pointer = test_edicts[3].teammaster;
	test_edicts[3].teammaster = &test_edicts[3];
	CHECK(!SG_MechCatalogEntityTopologyMatches(3U, member_node));
	test_edicts[3].teammaster = saved_pointer;
	CHECK(SG_MechCatalogEntityTopologyMatches(3U, member_node));

	saved_pointer = test_edicts[2].teamchain;
	test_edicts[2].teamchain = NULL;
	CHECK(!SG_MechCatalogEntityTopologyMatches(2U, master_node));
	test_edicts[2].teamchain = saved_pointer;
	CHECK(SG_MechCatalogEntityTopologyMatches(2U, master_node));

	saved_string = test_edicts[4].targetname;
	test_edicts[4].targetname = "different-sound";
	CHECK(!SG_MechCatalogEntityTopologyMatches(2U, master_node));
	test_edicts[4].targetname = saved_string;
	CHECK(SG_MechCatalogEntityTopologyMatches(2U, master_node));

	node_copy = malloc((size_t)view.num_nodes * sizeof(*node_copy));
	edge_copy = malloc((size_t)view.num_edges * sizeof(*edge_copy));
	string_copy = malloc(view.string_bytes);
	CHECK(node_copy != NULL);
	CHECK(edge_copy != NULL);
	CHECK(string_copy != NULL);
	if (node_copy && edge_copy && string_copy)
	{
		memcpy(node_copy, view.nodes,
		    (size_t)view.num_nodes * sizeof(*node_copy));
		memcpy(edge_copy, view.edges,
		    (size_t)view.num_edges * sizeof(*edge_copy));
		memcpy(string_copy, view.strings, view.string_bytes);
		node_copy[0].spawnflags ^= 1U;
		CHECK(!SG_MechCatalogMatches(node_copy, view.num_nodes, view.edges,
		    view.num_edges, view.strings, view.string_bytes));
		CHECK(!SG_MechCatalogEntityMatches(node_copy[0].key,
		    &node_copy[0]));
		edge_copy[0].delay_ms ^= 1U;
		CHECK(!SG_MechCatalogMatches(view.nodes, view.num_nodes, edge_copy,
		    view.num_edges, view.strings, view.string_bytes));
		string_copy[1] ^= 1U;
		CHECK(!SG_MechCatalogMatches(view.nodes, view.num_nodes, view.edges,
		    view.num_edges, string_copy, view.string_bytes));
	}
	free(node_copy);
	free(edge_copy);
	free(string_copy);

	/* An unselected transient slot has no authority over the sealed inventory,
	 * whether it is allocated, retired, or reused. */
	test_edicts[8].inuse = true;
	SG_MechCatalogEntityInitialized(&test_edicts[8]);
	CHECK(SG_MechCatalogMatches(view.nodes, view.num_nodes, view.edges,
	    view.num_edges, view.strings, view.string_bytes));
	SG_MechCatalogInvalidate(&test_edicts[8]);
	test_edicts[8].inuse = false;
	CHECK(SG_MechCatalogMatches(view.nodes, view.num_nodes, view.edges,
	    view.num_edges, view.strings, view.string_bytes));
	test_edicts[8].inuse = true;
	SG_MechCatalogEntityInitialized(&test_edicts[8]);
	CHECK(SG_MechCatalogMatches(view.nodes, view.num_nodes, view.edges,
	    view.num_edges, view.strings, view.string_bytes));

	/* A slot that merely disappears without the authoritative invalidation is
	 * neither current nor an exact retired incarnation. */
	test_edicts[4].inuse = false;
	CHECK(!SG_MechCatalogMatches(view.nodes, view.num_nodes, view.edges,
	    view.num_edges, view.strings, view.string_bytes));
	test_edicts[4].inuse = true;
	CHECK(SG_MechCatalogMatches(view.nodes, view.num_nodes, view.edges,
	    view.num_edges, view.strings, view.string_bytes));

	SG_MechCatalogInvalidate(&test_edicts[6]);
	/* Invalidation alone is not retirement while the old body remains live. */
	CHECK(!SG_MechCatalogMatches(view.nodes, view.num_nodes, view.edges,
	    view.num_edges, view.strings, view.string_bytes));
	test_edicts[6].inuse = false;
	CHECK(!SG_MechCatalogEntityMatches(6U, custom_node));
	CHECK(SG_MechCatalogEntityRetired(6U, custom_node));
	CHECK(SG_MechCatalogMatches(view.nodes, view.num_nodes, view.edges,
	    view.num_edges, view.strings, view.string_bytes));
	/* Retirement never weakens the serialized inventory identity. */
	node_copy = malloc((size_t)view.num_nodes * sizeof(*node_copy));
	CHECK(node_copy != NULL);
	if (node_copy)
	{
		ptrdiff_t custom_index = custom_node - view.nodes;

		memcpy(node_copy, view.nodes,
		    (size_t)view.num_nodes * sizeof(*node_copy));
		CHECK(custom_index >= 0 && (uint32_t)custom_index < view.num_nodes);
		if (custom_index >= 0 && (uint32_t)custom_index < view.num_nodes)
			node_copy[custom_index].spawnflags ^= 1U;
		CHECK(!SG_MechCatalogMatches(node_copy, view.num_nodes, view.edges,
		    view.num_edges, view.strings, view.string_bytes));
	}
	free(node_copy);
	test_edicts[6].inuse = true;
	SG_MechCatalogEntityInitialized(&test_edicts[6]);
	CHECK(!SG_MechCatalogEntityMatches(6U, custom_node));
	CHECK(!SG_MechCatalogEntityRetired(6U, custom_node));
	CHECK(!SG_MechCatalogMatches(view.nodes, view.num_nodes, view.edges,
	    view.num_edges, view.strings, view.string_bytes));
}

static void TestEntityGeneration(void)
{
	sg_mech_catalog_view_t view;
	edict_t foreign;
	edict_t *entity;
	uint32_t key;
	uint32_t generation;
	uint32_t first_generation;

	/* A live allocation is not incarnation authority until the post-spawn
	 * catalog has sealed. */
	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = 2;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	SG_MechCatalogBegin();
	InitializeEntity(1U, "func_wall");
	key = generation = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(&test_edicts[1], &key,
	    &generation));
	CHECK(key == 0U && generation == 0U);

	memset(&view, 0, sizeof(view));
	BuildCatalog(&view);
	entity = &test_edicts[2];
	key = UINT32_MAX;
	generation = UINT32_MAX;
	CHECK(SG_MechCatalogEntityGeneration(entity, &key, &generation));
	CHECK(key == 2U && generation != 0U);
	first_generation = generation;

	/* Output state is deterministic on every rejection boundary. */
	key = UINT32_MAX;
	generation = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(NULL, &key, &generation));
	CHECK(key == 0U && generation == 0U);
	CHECK(!SG_MechCatalogEntityGeneration(entity, NULL, &generation));
	CHECK(generation == 0U);
	key = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(entity, &key, NULL));
	CHECK(key == 0U);
	key = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(entity, &key, &key));
	CHECK(key == 0U);

	entity->s.number = 3;
	key = generation = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(entity, &key, &generation));
	CHECK(key == 0U && generation == 0U);
	entity->s.number = 2;
	entity->inuse = false;
	key = generation = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(entity, &key, &generation));
	CHECK(key == 0U && generation == 0U);
	entity->inuse = true;

	/* Invalidation retires the captured incarnation immediately.  Reusing
	 * the same slot creates a distinct nonzero generation, never the old
	 * ticket authority. */
	SG_MechCatalogInvalidate(entity);
	key = generation = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(entity, &key, &generation));
	CHECK(key == 0U && generation == 0U);
	SG_MechCatalogEntityInitialized(entity);
	CHECK(SG_MechCatalogEntityGeneration(entity, &key, &generation));
	CHECK(key == 2U && generation != 0U &&
	    generation != first_generation);

	key = generation = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(&test_edicts[0], &key,
	    &generation));
	CHECK(key == 0U && generation == 0U);
	memset(&foreign, 0, sizeof(foreign));
	foreign.inuse = true;
	foreign.s.number = 2;
	key = generation = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(&foreign, &key, &generation));
	CHECK(key == 0U && generation == 0U);
	key = generation = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(
	    (edict_t *)((unsigned char *)&test_edicts[2] + 1U), &key,
	    &generation));
	CHECK(key == 0U && generation == 0U);

	/* Selection into the serialized mechanism inventory is not required.
	 * A newly initialized ordinary map entity receives the same exact
	 * process-local incarnation authority once it is inside the live range. */
	InitializeEntity(8U, "func_wall");
	key = generation = UINT32_MAX;
	CHECK(!SG_MechCatalogEntityGeneration(&test_edicts[8], &key,
	    &generation));
	CHECK(key == 0U && generation == 0U);
	globals.num_edicts = 9;
	CHECK(SG_MechCatalogEntityGeneration(&test_edicts[8], &key,
	    &generation));
	CHECK(key == 8U && generation != 0U);
}

static void BuildInventoryOnlyCatalog(sg_mech_catalog_view_t *view,
	float kinematics)
{
	edict_t *explosive;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = TEST_EDICTS;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;

	SG_MechCatalogBegin();
	InitializeEntity(1U, "func_explosive");
	InitializeEntity(2U, "target_speaker");
	InitializeEntity(3U, "func_door");
	InitializeEntity(4U, "trigger_multiple");
	InitializeEntity(5U, "misc_teleporter");
	InitializeEntity(6U, "trigger_teleport");
	InitializeEntity(7U, "func_door");
	InitializeEntity(8U, "func_door");

	explosive = &test_edicts[1];
	explosive->target = "explosive-target";
	explosive->targetname = "explosive-source";
	explosive->killtarget = "explosive-kill";
	explosive->pathtarget = "explosive-path";
	explosive->touch = Touch_Multi;
	explosive->use = Use_Multi;
	explosive->think = multi_wait;
	explosive->blocked = door_blocked;
	explosive->spawnflags = 37;
	explosive->delay = 12.5f;
	explosive->wait = 1.25f;
	explosive->speed = kinematics;
	explosive->accel = kinematics;
	explosive->decel = kinematics;
	VectorSet(explosive->s.origin, 4.0f, -8.0f, 12.0f);
	explosive->owner = &test_edicts[2];
	explosive->movetarget = &test_edicts[3];
	explosive->target_ent = &test_edicts[4];
	explosive->enemy = &test_edicts[5];
	explosive->teammaster = explosive;
	explosive->teamchain = &test_edicts[8];

	test_edicts[2].targetname = "explosive-target";
	test_edicts[3].targetname = "explosive-target";
	test_edicts[4].targetname = "explosive-target";
	test_edicts[5].targetname = "explosive-target";
	test_edicts[6].targetname = "explosive-kill";
	test_edicts[7].targetname = "explosive-path";
	test_edicts[8].flags = FL_TEAMSLAVE;
	test_edicts[8].teammaster = explosive;

	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(strcmp(SG_MechCatalogReason(), "ok") == 0);
	CHECK(SG_MechCatalogSnapshot(view) == SG_MECH_CATALOG_READY);
}

static void TestInventoryOnlyKinematicsCanonicalized(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *explosive;
	char *saved_target;

	memset(&view, 0, sizeof(view));
	BuildInventoryOnlyCatalog(&view, 1.0e30f);
	explosive = NodeByKey(&view, 1U);
	CHECK(explosive != NULL);
	CHECK(explosive && explosive->kind == SG_MECH_NODE_OTHER_MOVER);
	CHECK(explosive &&
	    (explosive->flags & SG_MECH_NODEF_INVENTORY_ONLY) != 0U);
	CHECK(explosive && explosive->speed_q8 == 0U &&
	    explosive->accel_q8 == 0U && explosive->decel_q8 == 0U);
	CHECK(explosive && explosive->touch_callback == SG_MECH_CALLBACK_TOUCH_MULTI &&
	    explosive->use_callback == SG_MECH_CALLBACK_USE_MULTI &&
	    explosive->think_callback == SG_MECH_CALLBACK_THINK_MULTI_WAIT &&
	    explosive->blocked_callback == SG_MECH_CALLBACK_BLOCKED_DOOR);
	CHECK(explosive && explosive->spawnflags == 37U &&
	    explosive->delay_ms == 12500 && explosive->wait_ms == 1250);
	CHECK(explosive && explosive->absmin_q8[0] == 32 &&
	    explosive->absmin_q8[1] == -64 && explosive->absmin_q8[2] == 96 &&
	    memcmp(explosive->absmin_q8, explosive->absmax_q8,
	        sizeof(explosive->absmin_q8)) == 0);
	CHECK(explosive && explosive->owner_key == 2U &&
	    explosive->team_master_key == 1U);
	CHECK(explosive && explosive->classname_offset != 0U &&
	    explosive->target_offset != 0U && explosive->targetname_offset != 0U &&
	    explosive->killtarget_offset != 0U && explosive->path_target_offset != 0U);
	CHECK(HasEdge(&view, 1U, 2U, SG_MECH_EDGE_TARGET, 0U));
	CHECK(HasEdge(&view, 1U, 3U, SG_MECH_EDGE_TARGET, 1U));
	CHECK(HasEdge(&view, 1U, 4U, SG_MECH_EDGE_TARGET, 2U));
	CHECK(HasEdge(&view, 1U, 5U, SG_MECH_EDGE_TARGET, 3U));
	CHECK(HasEdge(&view, 1U, 6U, SG_MECH_EDGE_KILLTARGET, 0U));
	CHECK(HasEdge(&view, 1U, 7U, SG_MECH_EDGE_PATH_TARGET, 0U));
	CHECK(HasEdge(&view, 1U, 2U, SG_MECH_EDGE_OWNER, 0U));
	CHECK(HasEdge(&view, 1U, 3U, SG_MECH_EDGE_MOVE_TARGET, 0U));
	CHECK(HasEdge(&view, 1U, 4U, SG_MECH_EDGE_TARGET_ENT, 0U));
	CHECK(HasEdge(&view, 1U, 5U, SG_MECH_EDGE_ENEMY, 0U));
	CHECK(HasEdge(&view, 1U, 8U, SG_MECH_EDGE_TEAM, 0U));
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, explosive));
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, explosive,
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR));
	saved_target = test_edicts[1].target;
	test_edicts[1].target = "changed-target";
	CHECK(!SG_MechCatalogEntityTopologyMatches(1U, explosive));
	test_edicts[1].target = saved_target;
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, explosive));
	SG_MechCatalogInvalidate(&test_edicts[1]);
	test_edicts[1].inuse = false;
	CHECK(!SG_MechCatalogEntityMatches(1U, explosive));
	CHECK(SG_MechCatalogEntityRetired(1U, explosive));
	test_edicts[1].inuse = true;
	SG_MechCatalogEntityInitialized(&test_edicts[1]);
	CHECK(!SG_MechCatalogEntityMatches(1U, explosive));
	CHECK(!SG_MechCatalogEntityRetired(1U, explosive));

	BuildInventoryOnlyCatalog(&view, INFINITY);
	explosive = NodeByKey(&view, 1U);
	CHECK(explosive && explosive->speed_q8 == 0U &&
	    explosive->accel_q8 == 0U && explosive->decel_q8 == 0U);
}

static void TestExecutableKinematicsFailClosed(const char *classname,
	float kinematics)
{
	edict_t *entity;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = 2;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	SG_MechCatalogBegin();
	InitializeEntity(1U, classname);
	entity = &test_edicts[1];
	entity->speed = kinematics;
	entity->accel = kinematics;
	entity->decel = kinematics;
	entity->moveinfo.speed = kinematics;
	entity->moveinfo.accel = kinematics;
	entity->moveinfo.decel = kinematics;
	if (!strcmp(classname, "func_button"))
	{
		entity->touch = button_touch;
		entity->use = button_use;
	}
	else if (!strcmp(classname, "func_door"))
	{
		entity->movetype = MOVETYPE_PUSH;
		entity->use = door_use;
		entity->blocked = door_blocked;
		entity->teammaster = entity;
	}
	else
	{
		entity->movetype = MOVETYPE_PUSH;
		entity->touch = Touch_Plat_Center;
		entity->use = Use_Plat;
		entity->blocked = plat_blocked;
	}
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_FAILED);
	CHECK(strcmp(SG_MechCatalogReason(),
	    "unrepresentable live mechanism field") == 0);
}

static void TestExecutableKinematicsRemainChecked(void)
{
	TestExecutableKinematicsFailClosed("func_button", 1.0e30f);
	TestExecutableKinematicsFailClosed("func_door", 1.0e30f);
	TestExecutableKinematicsFailClosed("func_plat", 1.0e30f);
	TestExecutableKinematicsFailClosed("func_button", NAN);
}

static void BuildExecutableMoverCatalog(sg_mech_catalog_view_t *view,
	const char *classname)
{
	edict_t *entity;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = 2;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	SG_MechCatalogBegin();
	InitializeEntity(1U, classname);
	entity = &test_edicts[1];
	entity->wait = 3.0f;
	entity->moveinfo.wait = 3.0f;
	entity->moveinfo.speed = 100.0f;
	entity->moveinfo.accel = 100.0f;
	entity->moveinfo.decel = 100.0f;
	entity->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	if (!strcmp(classname, "func_button"))
	{
		entity->movetype = MOVETYPE_STOP;
		entity->touch = button_touch;
		entity->use = button_use;
		VectorSet(entity->moveinfo.start_origin, 8.0f, 16.0f, 24.0f);
		VectorSet(entity->moveinfo.end_origin, 8.0f, 16.0f, 16.0f);
		VectorCopy(entity->moveinfo.start_origin, entity->s.origin);
	}
	else if (!strcmp(classname, "func_door"))
	{
		entity->movetype = MOVETYPE_PUSH;
		entity->use = door_use;
		entity->blocked = door_blocked;
		entity->teammaster = entity;
	}
	else
	{
		entity->movetype = MOVETYPE_PUSH;
		entity->use = Use_Plat;
		entity->blocked = plat_blocked;
	}
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(view) == SG_MECH_CATALOG_READY);
}

static void TestExecutableMoverCurrentness(const char *classname,
	uint16_t expected_kind, uint16_t controller_kind)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *node;
	edict_t *entity;

	memset(&view, 0, sizeof(view));
	BuildExecutableMoverCatalog(&view, classname);
	node = NodeByKey(&view, 1U);
	entity = &test_edicts[1];
	CHECK(node && node->kind == expected_kind);
	CHECK(node && node->speed_q8 == 800U && node->accel_q8 == 800U &&
	    node->decel_q8 == 800U && node->wait_ms == 3000);
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, node));
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	if (!strcmp(classname, "func_plat"))
	{
		entity->moveinfo.state = SG_PLAT_STATE_TOP;
		entity->think = door_go_down;
		entity->moveinfo.endfunc = door_hit_top;
		entity->nextthink = 10.0f;
		CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
			controller_kind));
		entity->moveinfo.state = SG_PLAT_STATE_BOTTOM;
		entity->think = NULL;
		entity->moveinfo.endfunc = NULL;
		entity->nextthink = 0.0f;
	}

	/* Runtime moveinfo, not the map-key shadow fields, is authenticated. */
	entity->speed = 17.0f;
	entity->accel = 19.0f;
	entity->decel = 23.0f;
	entity->wait = -1.0f;
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));

	entity->moveinfo.speed = 101.0f;
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, node));
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.speed = 100.0f;
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.speed = 0.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.speed = 100.0f;
	entity->moveinfo.speed = NAN;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.speed = 100.0f;
	entity->moveinfo.speed = INFINITY;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.speed = 100.0f;
	entity->moveinfo.speed = 1.0e30f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.speed = 100.0f;
	entity->moveinfo.accel = 101.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.accel = 100.0f;
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.accel = 0.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.accel = 100.0f;
	entity->moveinfo.accel = NAN;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.accel = 100.0f;
	entity->moveinfo.accel = INFINITY;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.accel = 100.0f;
	entity->moveinfo.accel = 1.0e30f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.accel = 100.0f;
	entity->moveinfo.decel = 101.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.decel = 100.0f;
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.decel = 0.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.decel = 100.0f;
	entity->moveinfo.decel = NAN;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.decel = 100.0f;
	entity->moveinfo.decel = INFINITY;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.decel = 100.0f;
	entity->moveinfo.decel = 1.0e30f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.decel = 100.0f;
	entity->moveinfo.wait = 4.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.wait = 3.0f;
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.wait = 0.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.wait = 3.0f;
	entity->moveinfo.wait = NAN;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.wait = 3.0f;
	entity->moveinfo.wait = INFINITY;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.wait = 3.0f;
	entity->moveinfo.wait = 1.0e30f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
	entity->moveinfo.wait = 3.0f;
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, node, controller_kind));
}

static void TestExecutableMoverKinematicsStayCurrent(void)
{
	TestExecutableMoverCurrentness("func_button", SG_MECH_NODE_BUTTON,
		SG_MECHANISM_CONTROLLER_BUTTON_DOOR);
	TestExecutableMoverCurrentness("func_door", SG_MECH_NODE_DOOR_MASTER,
		SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR);
	TestExecutableMoverCurrentness("func_plat", SG_MECH_NODE_PLATFORM,
		SG_MECHANISM_CONTROLLER_PLATFORM);
}

static void TestShootDoorOwnedPoseCurrentness(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *node;
	edict_t *door;

	BuildExecutableMoverCatalog(&view, "func_door");
	door = &test_edicts[1];
	door->health = door->max_health = 1;
	door->takedamage = DAMAGE_YES;
	door->die = door_killed;
	/* Health is part of catalog identity, so reseal the authentic shootable
	 * shape rather than mutating it after publication. */
	SG_MechCatalogBegin();
	InitializeEntity(1U, "func_door");
	door = &test_edicts[1];
	door->wait = door->moveinfo.wait = 3.0f;
	door->moveinfo.speed = door->moveinfo.accel = door->moveinfo.decel = 100.0f;
	door->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	door->movetype = MOVETYPE_PUSH;
	door->solid = SOLID_BSP;
	door->use = door_use;
	door->blocked = door_blocked;
	door->die = door_killed;
	door->health = door->max_health = 1;
	door->takedamage = DAMAGE_YES;
	door->teammaster = door;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	node = NodeByKey(&view, 1U);
	CHECK(node && (node->flags & SG_MECH_NODEF_SHOOTABLE) != 0U);
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_TRAIN_SHOOT));
	door->moveinfo.state = SG_PLAT_STATE_TOP;
	door->takedamage = DAMAGE_NO;
	door->think = door_go_down;
	door->nextthink = 1.0f;
	door->moveinfo.endfunc = door_hit_top;
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_TRAIN_SHOOT));
	door->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_TRAIN_SHOOT));
	door->moveinfo.state = SG_PLAT_STATE_TOP;
	door->takedamage = DAMAGE_YES;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_TRAIN_SHOOT));
}

static void SetupTriggeredVerticalDoorLiftCatalog(int spawnflags,
	float start_z, float end_z)
{
	edict_t *trigger;
	edict_t *mover;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = 3;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	SG_MechCatalogBegin();
	InitializeEntity(1U, "trigger_multiple");
	InitializeEntity(2U, "func_door");
	trigger = &test_edicts[1];
	mover = &test_edicts[2];
	trigger->target = "lift";
	trigger->touch = Touch_Multi;
	trigger->use = Use_Multi;
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->wait = 0.2f;
	VectorSet(trigger->absmin, -34.0f, -34.0f, start_z + 6.0f);
	VectorSet(trigger->absmax, 34.0f, 34.0f, start_z + 114.0f);
	mover->targetname = "lift";
	mover->spawnflags = spawnflags;
	mover->movetype = MOVETYPE_PUSH;
	mover->solid = SOLID_BSP;
	mover->use = door_use;
	mover->blocked = door_blocked;
	mover->teammaster = mover;
	mover->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	mover->moveinfo.wait = 5.0f;
	mover->moveinfo.speed = 100.0f;
	mover->moveinfo.accel = 100.0f;
	mover->moveinfo.decel = 100.0f;
	VectorSet(mover->mins, -50.0f, -50.0f, -10.0f);
	VectorSet(mover->maxs, 50.0f, 50.0f, 130.0f);
	VectorSet(mover->moveinfo.start_origin, 0.0f, 0.0f, start_z);
	VectorSet(mover->moveinfo.end_origin, 0.0f, 0.0f, end_z);
	VectorCopy(mover->moveinfo.start_origin, mover->s.origin);
}

static void TestTriggeredVerticalDoorLiftCatalog(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *trigger_node;
	const rune_mechanism_node_t *mover_node;
	edict_t *mover;

	SetupTriggeredVerticalDoorLiftCatalog(5, -1024.0f, 0.0f);
	mover = &test_edicts[2];

	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	trigger_node = NodeByKey(&view, 1U);
	mover_node = NodeByKey(&view, 2U);
	CHECK(trigger_node && trigger_node->kind == SG_MECH_NODE_PLATFORM_TRIGGER);
	CHECK(mover_node && mover_node->kind == SG_MECH_NODE_PLATFORM);
	CHECK(trigger_node && trigger_node->owner_key == 2U);
	CHECK(HasEdge(&view, 1U, 2U, SG_MECH_EDGE_TARGET, 0U));
	CHECK(HasEdge(&view, 1U, 2U, SG_MECH_EDGE_OWNER, 0U));
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, trigger_node,
		SG_MECHANISM_CONTROLLER_PLATFORM));
	CHECK(SG_MechCatalogEntityExecutionMatches(2U, mover_node,
		SG_MECHANISM_CONTROLLER_PLATFORM));

	mover->moveinfo.state = SG_PLAT_STATE_UP;
	mover->think = Move_Done;
	mover->moveinfo.endfunc = door_hit_top;
	mover->velocity[2] = 100.0f;
	CHECK(SG_MechCatalogEntityExecutionMatches(2U, mover_node,
		SG_MECHANISM_CONTROLLER_PLATFORM));
	mover->think = Move_Done;
	mover->moveinfo.endfunc = plat_hit_top;
	CHECK(!SG_MechCatalogEntityExecutionMatches(2U, mover_node,
		SG_MECHANISM_CONTROLLER_PLATFORM));
	mover->moveinfo.endfunc = door_hit_top;
	VectorClear(mover->velocity);
	mover->moveinfo.state = SG_PLAT_STATE_TOP;
	mover->think = door_go_down;
	mover->nextthink = 10.0f;
	CHECK(SG_MechCatalogEntityExecutionMatches(2U, mover_node,
		SG_MECHANISM_CONTROLLER_PLATFORM));
	mover->moveinfo.state = SG_PLAT_STATE_DOWN;
	mover->think = Move_Done;
	mover->moveinfo.endfunc = door_hit_bottom;
	mover->nextthink = 0.0f;
	mover->velocity[2] = -100.0f;
	CHECK(SG_MechCatalogEntityExecutionMatches(2U, mover_node,
		SG_MECHANISM_CONTROLLER_PLATFORM));
	VectorClear(mover->velocity);
	mover->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	mover->think = NULL;
	mover->moveinfo.endfunc = NULL;
	mover->spawnflags = 4;
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, trigger_node));
	CHECK(!SG_MechCatalogEntityTopologyMatches(2U, mover_node));
}

static void TestDescendingTriggeredVerticalDoorLiftCatalog(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *trigger_node;
	const rune_mechanism_node_t *mover_node;

	SetupTriggeredVerticalDoorLiftCatalog(4, 0.0f, -1024.0f);
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	trigger_node = NodeByKey(&view, 1U);
	mover_node = NodeByKey(&view, 2U);
	CHECK(trigger_node && trigger_node->kind == SG_MECH_NODE_PLATFORM_TRIGGER);
	CHECK(mover_node && mover_node->kind == SG_MECH_NODE_PLATFORM);
	CHECK(trigger_node && trigger_node->owner_key == 2U);
	CHECK(HasEdge(&view, 1U, 2U, SG_MECH_EDGE_TARGET, 0U));
	CHECK(HasEdge(&view, 1U, 2U, SG_MECH_EDGE_OWNER, 0U));
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, trigger_node,
		SG_MECHANISM_CONTROLLER_PLATFORM));
	CHECK(SG_MechCatalogEntityExecutionMatches(2U, mover_node,
		SG_MECHANISM_CONTROLLER_PLATFORM));
}

static void SetupToggleButtonLiftCatalog(void)
{
	edict_t *mover;
	edict_t *start_button;
	edict_t *end_button;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = 4;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	SG_MechCatalogBegin();
	InitializeEntity(1U, "func_door");
	InitializeEntity(2U, "func_button");
	InitializeEntity(3U, "func_button");
	mover = &test_edicts[1];
	start_button = &test_edicts[2];
	end_button = &test_edicts[3];
	mover->targetname = "toggle-lift";
	mover->spawnflags = 32;
	mover->movetype = MOVETYPE_PUSH;
	mover->solid = SOLID_BSP;
	mover->use = door_use;
	mover->blocked = door_blocked;
	mover->teammaster = mover;
	mover->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	mover->moveinfo.wait = 3.0f;
	mover->moveinfo.speed = 100.0f;
	mover->moveinfo.accel = 100.0f;
	mover->moveinfo.decel = 100.0f;
	VectorSet(mover->mins, -64.0f, -48.0f, -218.0f);
	VectorSet(mover->maxs, 64.0f, 48.0f, 218.0f);
	VectorClear(mover->moveinfo.start_origin);
	VectorSet(mover->moveinfo.end_origin, 0.0f, 0.0f, -418.0f);
	VectorCopy(mover->moveinfo.start_origin, mover->s.origin);
	VectorCopy(mover->mins, mover->absmin);
	VectorCopy(mover->maxs, mover->absmax);
	start_button->target = "toggle-lift";
	start_button->touch = button_touch;
	start_button->use = button_use;
	start_button->solid = SOLID_BSP;
	start_button->movetype = MOVETYPE_STOP;
	start_button->wait = 3.0f;
	start_button->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	VectorSet(start_button->absmin, -96.0f, -14.0f, 230.0f);
	VectorSet(start_button->absmax, -68.0f, 14.0f, 242.0f);
	end_button->target = "toggle-lift";
	end_button->touch = button_touch;
	end_button->use = button_use;
	end_button->solid = SOLID_BSP;
	end_button->movetype = MOVETYPE_STOP;
	end_button->wait = 3.0f;
	end_button->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	VectorSet(end_button->absmin, -14.0f, -14.0f, -212.0f);
	VectorSet(end_button->absmax, 14.0f, 14.0f, -200.0f);
}

static void TestToggleButtonLiftCatalog(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *mover;
	const rune_mechanism_node_t *start_button;
	const rune_mechanism_node_t *end_button;

	SetupToggleButtonLiftCatalog();
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	mover = NodeByKey(&view, 1U);
	start_button = NodeByKey(&view, 2U);
	end_button = NodeByKey(&view, 3U);
	CHECK(mover && mover->kind == SG_MECH_NODE_PLATFORM);
	CHECK(start_button && start_button->kind == SG_MECH_NODE_PLATFORM_TRIGGER);
	CHECK(end_button && end_button->kind == SG_MECH_NODE_PLATFORM_TRIGGER);
	CHECK(start_button && start_button->owner_key == 1U);
	CHECK(end_button && end_button->owner_key == 1U);
	CHECK(HasEdge(&view, 2U, 1U, SG_MECH_EDGE_TARGET, 0U));
	CHECK(HasEdge(&view, 2U, 1U, SG_MECH_EDGE_OWNER, 0U));
	CHECK(HasEdge(&view, 3U, 1U, SG_MECH_EDGE_TARGET, 0U));
	CHECK(HasEdge(&view, 3U, 1U, SG_MECH_EDGE_OWNER, 0U));

	SetupToggleButtonLiftCatalog();
	test_edicts[3].target = NULL;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	CHECK(NodeByKey(&view, 1U)->kind == SG_MECH_NODE_DOOR_MASTER);
	CHECK(NodeByKey(&view, 2U)->kind == SG_MECH_NODE_BUTTON);

	SetupToggleButtonLiftCatalog();
	test_edicts[3].delay = 0.1f;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	CHECK(NodeByKey(&view, 1U)->kind == SG_MECH_NODE_DOOR_MASTER);

	SetupToggleButtonLiftCatalog();
	VectorSet(test_edicts[3].absmin, -14.0f, -14.0f, 230.0f);
	VectorSet(test_edicts[3].absmax, 14.0f, 14.0f, 242.0f);
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	CHECK(NodeByKey(&view, 1U)->kind == SG_MECH_NODE_DOOR_MASTER);
}

static edict_t *InitializeFrameCompleteButton(float distance, float raw_speed)
{
	edict_t *button;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = 2;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	SG_MechCatalogBegin();
	InitializeEntity(1U, "func_button");
	button = &test_edicts[1];
	button->target = "speaker";
	button->wait = 30.0f;
	button->movetype = MOVETYPE_STOP;
	button->solid = SOLID_BSP;
	button->use = button_use;
	button->health = button->max_health = 30;
	button->takedamage = DAMAGE_YES;
	button->die = button_killed;
	button->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	button->moveinfo.wait = 30.0f;
	button->moveinfo.speed = raw_speed;
	button->moveinfo.accel = raw_speed;
	button->moveinfo.decel = raw_speed;
	VectorClear(button->moveinfo.start_origin);
	VectorSet(button->moveinfo.end_origin, distance, 0.0f, 0.0f);
	VectorCopy(button->moveinfo.start_origin, button->s.origin);
	return button;
}

static void TestFrameCompleteButtonDistance(float distance,
	uint32_t expected_witness)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *node;
	edict_t *button = InitializeFrameCompleteButton(distance, 1.0e20f);

	memset(&view, 0, sizeof(view));
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	node = NodeByKey(&view, 1U);
	CHECK(node != NULL);
	CHECK(node && (node->flags & SG_MECH_NODEF_FRAME_COMPLETE_MOVER) != 0U);
	CHECK(node && (node->flags & SG_MECH_NODEF_SHOOTABLE) != 0U);
	CHECK(node && (node->flags & SG_MECH_NODEF_INVENTORY_ONLY) == 0U);
	CHECK(node && node->speed_q8 == expected_witness &&
	    node->accel_q8 == expected_witness &&
	    node->decel_q8 == expected_witness);
	CHECK(node && SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));
	CHECK(button->health == 30 && button->max_health == 30 &&
	    button->die == button_killed && button->takedamage == DAMAGE_YES);
}

static void TestFrameCompleteButtonSealGates(void)
{
	edict_t *button;
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *node;

	TestFrameCompleteButtonDistance(95.0f, 7600U);
	TestFrameCompleteButtonDistance(106.0f, 8480U);
	TestFrameCompleteButtonDistance(113.0f, 9040U);

	/* G_SetMovedir/VectorMA can leave cardinal trig residue on an otherwise
	 * exact stock endpoint (lmctf58).  It must canonicalize to the same Q8
	 * point without admitting a material diagonal. */
	button = InitializeFrameCompleteButton(95.0f, 1.0e20f);
	button->moveinfo.end_origin[1] = 1.0e-6f;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	node = NodeByKey(&view, 1U);
	CHECK(node && (node->flags & SG_MECH_NODEF_FRAME_COMPLETE_MOVER) != 0U);
	CHECK(node && node->speed_q8 == 7600U);

	/* A representable one-frame button retains ordinary moveinfo authority. */
	button = InitializeFrameCompleteButton(95.0f, 1000.0f);
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	node = NodeByKey(&view, 1U);
	CHECK(node && (node->flags & SG_MECH_NODEF_FRAME_COMPLETE_MOVER) == 0U);
	CHECK(node && node->speed_q8 == 8000U && node->accel_q8 == 8000U &&
	    node->decel_q8 == 8000U);
	(void)button;

	/* Unrepresentable raw motion never receives the flag without coverage. */
	InitializeFrameCompleteButton(4090.0f, 40000.0f);
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_FAILED);

	button = InitializeFrameCompleteButton(95.0f, 1.0e20f);
	button->moveinfo.accel = 9.0e19f;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_FAILED);
	button = InitializeFrameCompleteButton(95.0f, 1.0e20f);
	button->moveinfo.decel = NAN;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_FAILED);
	button = InitializeFrameCompleteButton(95.0f, 1.0e20f);
	VectorSet(button->moveinfo.end_origin, 95.0f, 1.0f, 0.0f);
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_FAILED);
	button = InitializeFrameCompleteButton(95.0f, 1.0e20f);
	VectorClear(button->moveinfo.end_origin);
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_FAILED);
	button = InitializeFrameCompleteButton(95.0f, 1.0e20f);
	button->moveinfo.end_origin[0] = 5000.0f;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_FAILED);
	button = InitializeFrameCompleteButton(95.0f, 1.0e20f);
	button->movetype = MOVETYPE_PUSH;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_FAILED);
	button = InitializeFrameCompleteButton(95.0f, 1.0e20f);
	button->solid = SOLID_NOT;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_FAILED);
	button = InitializeFrameCompleteButton(95.0f, 1.0e20f);
	button->die = NULL;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_FAILED);
}

static void TestFrameCompleteButtonCurrentness(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *node;
	edict_t *button = InitializeFrameCompleteButton(95.0f, 1.0e20f);
	float saved;

	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	node = NodeByKey(&view, 1U);
	CHECK(node && SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));

	saved = button->moveinfo.speed;
	button->moveinfo.speed = 0.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));
	button->moveinfo.speed = saved;
	button->moveinfo.accel = 9.0e19f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));
	button->moveinfo.accel = saved;
	button->moveinfo.decel = INFINITY;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));
	button->moveinfo.decel = saved;
	button->moveinfo.speed = button->moveinfo.accel =
		button->moveinfo.decel = 1000.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));
	button->moveinfo.speed = button->moveinfo.accel =
		button->moveinfo.decel = saved;
	button->moveinfo.end_origin[0] = 96.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));
	button->moveinfo.end_origin[0] = 95.0f;
	button->moveinfo.end_origin[1] = 1.0f;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));
	button->moveinfo.end_origin[1] = 0.0f;
	button->health = 29;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));
	button->health = 30;
	button->movetype = MOVETYPE_PUSH;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, node,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));
}

static void TestTinyPositiveDelayStaysAsynchronous(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *relay_node;
	const rune_mechanism_edge_t *relay_edge = NULL;
	edict_t *relay;
	uint32_t index;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = 3;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	SG_MechCatalogBegin();
	InitializeEntity(1U, "trigger_relay");
	relay = &test_edicts[1];
	relay->use = trigger_relay_use;
	relay->delay = 0.0001f;
	relay->target = "tiny-delay-sound";
	InitializeEntity(2U, "target_speaker");
	test_edicts[2].use = Use_Target_Speaker;
	test_edicts[2].targetname = "tiny-delay-sound";
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	relay_node = NodeByKey(&view, 1U);
	for (index = 0U; index < view.num_edges; index++)
		if (view.edges[index].from_key == 1U &&
		    view.edges[index].to_key == 2U &&
		    view.edges[index].kind == SG_MECH_EDGE_TARGET)
			relay_edge = &view.edges[index];
	CHECK(relay_node && relay_node->delay_ms == 1);
	CHECK(relay_edge && relay_edge->delay_ms == 1U);
	CHECK(SG_MechCatalogEntityTopologyMatches(1U, relay_node));
	relay->delay = 0.0f;
	CHECK(!SG_MechCatalogEntityTopologyMatches(1U, relay_node));
}

static void TestPushVelocitySealed(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *node;
	edict_t *push;
	vec3_t expected;
	float scale;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = 2;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	SG_MechCatalogBegin();
	InitializeEntity(1U, "trigger_push");
	push = &test_edicts[1];
	push->touch = trigger_push_touch;
	push->solid = SOLID_TRIGGER;
	push->speed = 85.0f;
	VectorSet(push->movedir, -0.0697246f, 0.0f, 0.9975641f);
	scale = push->speed * 10.0f;
	expected[0] = push->movedir[0] * scale;
	expected[1] = push->movedir[1] * scale;
	expected[2] = push->movedir[2] * scale;
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	node = NodeByKey(&view, 1U);
	CHECK(node && node->kind == SG_MECH_NODE_PUSH);
	CHECK(node && memcmp(node->push_velocity, expected,
		sizeof(expected)) == 0);
	CHECK(node && SG_MechCatalogEntityExecutionMatches(1U, node,
		SG_MECHANISM_CONTROLLER_PUSH));
	push->speed = 90.0f;
	CHECK(node && !SG_MechCatalogEntityTopologyMatches(1U, node));
}

static void SetupTrainGateCatalog(int shoot)
{
	edict_t *button;
	edict_t *train;
	edict_t *closed;
	edict_t *open;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	game.maxentities = TEST_EDICTS;
	globals.num_edicts = 5;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	SG_MechCatalogBegin();
	InitializeEntity(1U, "func_button");
	InitializeEntity(2U, "func_train");
	InitializeEntity(3U, "path_corner");
	InitializeEntity(4U, "path_corner");
	button = &test_edicts[1];
	train = &test_edicts[2];
	closed = &test_edicts[3];
	open = &test_edicts[4];

	button->target = "gate";
	button->touch = button_touch;
	button->use = button_use;
	button->movetype = MOVETYPE_STOP;
	button->solid = SOLID_BSP;
	button->wait = button->moveinfo.wait = 1.0f;
	button->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	button->moveinfo.speed = button->moveinfo.accel =
		button->moveinfo.decel = 40.0f;
	if (shoot)
	{
		button->touch = NULL;
		button->health = button->max_health = 1;
		button->takedamage = DAMAGE_YES;
		button->die = button_killed;
	}

	train->targetname = "gate";
	train->target = "closed";
	train->spawnflags = 2;
	train->movetype = MOVETYPE_PUSH;
	train->solid = SOLID_BSP;
	train->use = train_use;
	train->blocked = train_blocked;
	train->moveinfo.speed = train->moveinfo.accel =
		train->moveinfo.decel = 300.0f;
	VectorSet(train->mins, -2.0f, -2.0f, -2.0f);

	closed->targetname = "closed";
	closed->target = "open";
	closed->wait = -1.0f;
	closed->touch = path_corner_touch;
	VectorSet(closed->s.origin, 0.0f, 0.0f, 256.0f);
	VectorSet(closed->absmin, -8.0f, -8.0f, 248.0f);
	VectorSet(closed->absmax, 8.0f, 8.0f, 264.0f);

	open->targetname = "open";
	open->target = "closed";
	open->wait = -1.0f;
	open->touch = path_corner_touch;
	VectorSet(open->s.origin, 0.0f, 0.0f, 0.0f);
	VectorSet(open->absmin, -8.0f, -8.0f, -8.0f);
	VectorSet(open->absmax, 8.0f, 8.0f, 8.0f);

	func_train_find(train);
}

static void TestPostFindTrainGateCatalog(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *button;
	const rune_mechanism_node_t *train;
	const rune_mechanism_node_t *closed;
	const rune_mechanism_node_t *open;

	SetupTrainGateCatalog(0);
	CHECK(!strcmp(test_edicts[2].target, "open"));
	CHECK(test_edicts[2].target_ent == NULL);
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	button = NodeByKey(&view, 1U);
	train = NodeByKey(&view, 2U);
	closed = NodeByKey(&view, 3U);
	open = NodeByKey(&view, 4U);
	CHECK(button && button->kind == SG_MECH_NODE_BUTTON);
	CHECK(train && train->kind == SG_MECH_NODE_TRAIN);
	CHECK(closed && closed->kind == SG_MECH_NODE_PATH_CORNER);
	CHECK(open && open->kind == SG_MECH_NODE_PATH_CORNER);
	CHECK(HasEdge(&view, 1U, 2U, SG_MECH_EDGE_TARGET, 0U));
	CHECK(HasEdge(&view, 2U, 4U, SG_MECH_EDGE_ROUTE_TARGET, 0U));
	CHECK(!HasEdge(&view, 2U, 3U, SG_MECH_EDGE_ROUTE_TARGET, 0U));
	CHECK(HasEdge(&view, 3U, 4U, SG_MECH_EDGE_ROUTE_TARGET, 0U));
	CHECK(HasEdge(&view, 4U, 3U, SG_MECH_EDGE_ROUTE_TARGET, 0U));
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, button,
		SG_MECHANISM_CONTROLLER_TRAIN));
	CHECK(SG_MechCatalogEntityExecutionMatches(2U, train,
		SG_MECHANISM_CONTROLLER_TRAIN));
	CHECK(SG_MechCatalogEntityExecutionMatches(3U, closed,
		SG_MECHANISM_CONTROLLER_TRAIN));
	CHECK(SG_MechCatalogEntityExecutionMatches(4U, open,
		SG_MECHANISM_CONTROLLER_TRAIN));
}

static void TestPostFindTrainShootGateCatalog(void)
{
	sg_mech_catalog_view_t view;
	const rune_mechanism_node_t *button;
	const rune_mechanism_node_t *train;
	const rune_mechanism_node_t *closed;
	const rune_mechanism_node_t *open;

	SetupTrainGateCatalog(1);
	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&view) == SG_MECH_CATALOG_READY);
	button = NodeByKey(&view, 1U);
	train = NodeByKey(&view, 2U);
	closed = NodeByKey(&view, 3U);
	open = NodeByKey(&view, 4U);
	CHECK(button && button->flags == (SG_MECH_NODEF_REPEATABLE |
		SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER |
		SG_MECH_NODEF_SHOOTABLE));
	CHECK(button && button->touch_callback == SG_MECH_CALLBACK_NONE);
	CHECK(SG_MechCatalogEntityExecutionMatches(1U, button,
		SG_MECHANISM_CONTROLLER_TRAIN_SHOOT));
	CHECK(SG_MechCatalogEntityExecutionMatches(2U, train,
		SG_MECHANISM_CONTROLLER_TRAIN_SHOOT));
	CHECK(SG_MechCatalogEntityExecutionMatches(3U, closed,
		SG_MECHANISM_CONTROLLER_TRAIN_SHOOT));
	CHECK(SG_MechCatalogEntityExecutionMatches(4U, open,
		SG_MECHANISM_CONTROLLER_TRAIN_SHOOT));
	test_edicts[1].health = 2;
	CHECK(!SG_MechCatalogEntityExecutionMatches(1U, button,
		SG_MECHANISM_CONTROLLER_TRAIN_SHOOT));
}

static sg_mech_train_gate_state_t TrainTuple(void)
{
	sg_mech_train_gate_state_t state;

	memset(&state, 0, sizeof(state));
	state.controller_kind = SG_MECHANISM_CONTROLLER_TRAIN;
	state.node_kind = SG_MECH_NODE_TRAIN;
	state.fixed_callbacks_match = 1;
	state.at_closed = 1;
	state.target_is_open = 1;
	state.target_ent_is_none = 1;
	state.stopped = 1;
	state.think_role = SG_MECH_EXEC_THINK_SEALED;
	state.end_role = SG_MECH_EXEC_END_NONE;
	return state;
}

static void TestTrainGateExecutionTuples(void)
{
	sg_mech_train_gate_state_t state = TrainTuple();

	CHECK(SG_MechTrainGatePose(&state) == SG_MECH_TRAIN_GATE_CLOSED);
	state.target_is_open = 0;
	state.target_is_closed = 1;
	state.target_ent_is_none = 0;
	state.target_ent_is_open = 1;
	state.think_role = SG_MECH_EXEC_THINK_LINEAR_FINAL;
	state.end_role = SG_MECH_EXEC_END_TRAIN_CORNER;
	CHECK(SG_MechTrainGatePose(&state) == SG_MECH_TRAIN_GATE_CLOSED);

	state.at_closed = 0;
	state.start_on = 1;
	state.stopped = 0;
	state.moving = 1;
	state.nextthink_pending = 1;
	CHECK(SG_MechTrainGatePose(&state) == SG_MECH_TRAIN_GATE_OPENING);
	CHECK(SG_MechTrainGateExecutionStateValid(&state));

	state.start_on = 0;
	state.moving = 0;
	state.stopped = 1;
	state.nextthink_pending = 0;
	state.at_open = 1;
	state.target_is_closed = 0;
	state.target_is_open = 1;
	state.target_ent_is_open = 0;
	state.target_ent_is_closed = 1;
	CHECK(SG_MechTrainGatePose(&state) == SG_MECH_TRAIN_GATE_OPEN);
	CHECK(SG_MechTrainGateExecutionStateValid(&state));

	state.at_open = 0;
	state.start_on = 1;
	state.stopped = 0;
	state.moving = 1;
	state.nextthink_pending = 1;
	CHECK(SG_MechTrainGatePose(&state) == SG_MECH_TRAIN_GATE_CLOSING);
	CHECK(!SG_MechTrainGateExecutionStateValid(&state));

	state.start_on = 0;
	state.stopped = 1;
	state.moving = 0;
	state.nextthink_pending = 0;
	CHECK(SG_MechTrainGatePose(&state) == SG_MECH_TRAIN_GATE_INTERRUPTED);
	CHECK(!SG_MechTrainGateExecutionStateValid(&state));
	state.at_closed = 1;
	state.at_open = 1;
	CHECK(SG_MechTrainGatePose(&state) == SG_MECH_TRAIN_GATE_INVALID);
}

int main(void)
{
	TestSealedCatalog();
	TestEntityGeneration();
	TestInventoryOnlyKinematicsCanonicalized();
	TestExecutableKinematicsRemainChecked();
	TestExecutableMoverKinematicsStayCurrent();
	TestShootDoorOwnedPoseCurrentness();
	TestTriggeredVerticalDoorLiftCatalog();
	TestDescendingTriggeredVerticalDoorLiftCatalog();
	TestToggleButtonLiftCatalog();
	TestFrameCompleteButtonSealGates();
	TestFrameCompleteButtonCurrentness();
	TestTinyPositiveDelayStaysAsynchronous();
	TestPushVelocitySealed();
	TestPostFindTrainGateCatalog();
	TestPostFindTrainShootGateCatalog();
	TestTrainGateExecutionTuples();
	if (failures != 0)
	{
		fprintf(stderr, "%d mechanism catalog test(s) failed\n", failures);
		return 1;
	}
	puts("mechanism catalog tests passed");
	return 0;
}
