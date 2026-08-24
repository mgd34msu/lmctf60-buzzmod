/* Real-edict lifecycle test for the passive continuous-station adapter. */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_train_station_game.h"
#include "slipgate/sg_train_station_plan.h"
#include "slipgate/sg_util.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum test_key_e
{
	KEY_MASTER = 5,
	KEY_ENTRY = 28,
	KEY_DESTINATION = 35,
	KEY_COMPANION = 42,
	KEY_BOT = 43,
	TEST_EDICTS = 64
};

game_locals_t game;
level_locals_t level;
game_import_t gi;
game_export_t globals;
edict_t *g_edicts;
sg_bot_t sg_bots[SG_MAXBOTS];
cvar_t *maxclients;
cvar_t *sv_gravity;
int meansOfDeath;
sg_host_t sg_host;

static void *LevelAlloc(int size)
{
	return calloc(1U, (size_t)size);
}

static void LevelFree(void *memory)
{
	free(memory);
}

static trace_t Trace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t trace;

	(void)start;
	(void)mins;
	(void)maxs;
	(void)passent;
	(void)contentmask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	return trace;
}

int Q_stricmp(const char *left, const char *right)
{
	return strcmp(left, right);
}

void Move_Final(edict_t *entity)
{
	(void)entity;
}

void Move_Done(edict_t *entity)
{
	(void)entity;
}

void train_next(edict_t *entity)
{
	(void)entity;
}

static const uint32_t route[SG_TRAIN_STATION_ROUTE_CORNERS] = {
	28U, 29U, 30U, 31U, 32U, 33U, 34U,
	35U, 36U, 37U, 38U, 41U, 40U, 39U
};
static const vec3_t lmctf25_route_origins[SG_TRAIN_STATION_ROUTE_CORNERS] = {
	{-64.0f, -64.0f, -204.0f}, {840.0f, -64.0f, -204.0f},
	{876.0f, -64.0f, -212.0f}, {888.0f, -64.0f, -244.0f},
	{888.0f, -64.0f, -1572.0f}, {876.0f, -64.0f, -1604.0f},
	{840.0f, -64.0f, -1612.0f}, {-64.0f, -64.0f, -1612.0f},
	{-1020.0f, -64.0f, -1612.0f}, {-1056.0f, -64.0f, -1604.0f},
	{-1068.0f, -64.0f, -1572.0f}, {-1068.0f, -64.0f, -244.0f},
	{-1056.0f, -64.0f, -212.0f}, {-1020.0f, -64.0f, -204.0f}
};
static edict_t edicts[TEST_EDICTS];
static gclient_t bot_client;
static rune_t active_rune;
static rune_seed_t seeds[2];
static rune_link_t active_link;
static rune_mechanism_plan_t plan;
static rune_mechanism_node_t nodes[4];
static uint32_t generations[TEST_EDICTS];
static char route_names[SG_TRAIN_STATION_ROUTE_CORNERS][16];
static int generic_binding_current;
static int station_binding_current;
static int command_count;
static int declared_count;
static vec3_t last_declared_target;
static int failures;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
		    #condition_); \
		failures++; \
	} \
} while (0)

static edict_t *Entity(uint32_t key)
{
	return key < TEST_EDICTS ? &edicts[key] : NULL;
}

static void InitEntity(uint32_t key, const char *classname)
{
	edict_t *entity = Entity(key);

	memset(entity, 0, sizeof(*entity));
	entity->inuse = true;
	entity->s.number = (int)key;
	entity->classname = (char *)classname;
	generations[key] = key + 1000U;
}

rune_t *SG_Rune(void)
{
	return &active_rune;
}

static int CaptureBinding(const rune_t *rune, uint32_t link_index,
	sg_rune_mechanism_binding_t *binding)
{
	if (binding)
		memset(binding, 0, sizeof(*binding));
	if (!binding || rune != &active_rune || link_index != 0U)
		return 0;
	*binding = (sg_rune_mechanism_binding_t){
		.rune = &active_rune,
		.link = &active_link,
		.plan = &plan,
		.entry_node = &nodes[0],
		.mover_node = &nodes[1],
		.destination_node = &nodes[2],
		.egress_node = &nodes[3],
		.entry_entity = Entity(KEY_ENTRY),
		.mover_entity = Entity(KEY_MASTER),
		.destination_entity = Entity(KEY_DESTINATION),
		.egress_entity = Entity(KEY_COMPANION),
		.link_index = 0U
	};
	return 1;
}

int SG_RuneMechanismBindingCapture(const rune_t *rune, uint32_t link_index,
	sg_rune_mechanism_binding_t *binding)
{
	return generic_binding_current && CaptureBinding(rune, link_index,
		binding);
}

int SG_RuneMechanismStationBindingCapture(const rune_t *rune,
	uint32_t link_index, sg_rune_mechanism_binding_t *binding)
{
	return station_binding_current && CaptureBinding(rune, link_index,
		binding);
}

int SG_TrainStationPlanDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, uint32_t mover_key, uint32_t *destination_key,
	uint32_t *companion_key, sg_train_station_plan_witness_t *witness)
{
	(void)catalog;
	if (!destination_key || !companion_key || !witness ||
	    entry_key != KEY_ENTRY || mover_key != KEY_MASTER)
		return 0;
	memset(witness, 0, sizeof(*witness));
	memcpy(witness->route_keys, route, sizeof(route));
	witness->route_count = SG_TRAIN_STATION_ROUTE_CORNERS;
	witness->station_keys[0] = KEY_ENTRY;
	witness->station_keys[1] = KEY_DESTINATION;
	*destination_key = KEY_DESTINATION;
	*companion_key = KEY_COMPANION;
	return 1;
}

int SG_MechCatalogEntityGeneration(const edict_t *entity,
	uint32_t *key_out, uint32_t *generation_out)
{
	uint32_t key;

	if (key_out)
		*key_out = 0U;
	if (generation_out)
		*generation_out = 0U;
	if (!entity || !entity->inuse || !key_out || !generation_out ||
	    entity->s.number <= 0 || entity->s.number >= TEST_EDICTS)
		return 0;
	key = (uint32_t)entity->s.number;
	if (entity != Entity(key) || generations[key] == 0U)
		return 0;
	*key_out = key;
	*generation_out = generations[key];
	return 1;
}

qboolean SG_DeclaredCommand(const vec3_t origin, const vec3_t target,
	const pmove_state_t *pmove, usercmd_t *command)
{
	(void)origin;
	(void)pmove;
	if (!target || !command)
		return false;
	VectorCopy(target, last_declared_target);
	declared_count++;
	return true;
}

qboolean SG_SupportedArrived(const vec3_t origin,
	const vec3_t destination, qboolean grounded, int watertype,
	int waterlevel, edict_t *passent)
{
	vec3_t delta;

	(void)watertype;
	(void)passent;
	VectorSubtract(origin, destination, delta);
	return grounded && waterlevel == 0 &&
	       delta[0] * delta[0] + delta[1] * delta[1] +
	           delta[2] * delta[2] <= 1.0f;
}

qboolean SG_LiftRider(edict_t *train, edict_t *body)
{
	return train && body && body->groundentity == train;
}

void ClientThink(edict_t *entity, usercmd_t *command)
{
	CHECK(entity == Entity(KEY_BOT));
	CHECK(command != NULL);
	CHECK(command->msec == 25U);
	command_count++;
}

static void SetBounds(edict_t *entity, float x, float half)
{
	VectorSet(entity->absmin, x - half, -half, 0.0f);
	VectorSet(entity->absmax, x + half, half, 32.0f);
}

static void SetSingleTrainFrame(edict_t *train, uint32_t corner, int moving)
{
	vec3_t delta;
	float distance;
	float speed;
	unsigned int route_index;
	int axis;

	train->target_ent = Entity(corner);
	train->target = train->target_ent->target;
	train->moveinfo.wait = train->target_ent->wait;
	if (moving)
	{
		for (route_index = 0U;
		     route_index < SG_TRAIN_STATION_ROUTE_CORNERS;
		     route_index++)
			if (route[route_index] == corner)
				break;
		CHECK(route_index < SG_TRAIN_STATION_ROUTE_CORNERS);
		if (route_index < SG_TRAIN_STATION_ROUTE_CORNERS)
			for (axis = 0; axis < 3; axis++)
				train->s.origin[axis] = Entity(route[(route_index +
					SG_TRAIN_STATION_ROUTE_CORNERS - 1U) %
					SG_TRAIN_STATION_ROUTE_CORNERS])->s.origin[axis] -
					train->mins[axis];
	}
	for (axis = 0; axis < 3; axis++)
	{
		train->velocity[axis] = 0.0f;
		if (moving)
		{
			train->moveinfo.end_origin[axis] =
				train->target_ent->s.origin[axis] - train->mins[axis];
		}
		else
			train->s.origin[axis] = train->target_ent->s.origin[axis] -
				train->mins[axis];
	}
	if (moving)
	{
		VectorCopy(train->s.origin, train->moveinfo.start_origin);
		VectorSubtract(train->moveinfo.end_origin,
			train->moveinfo.start_origin, delta);
		distance = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] +
			delta[2] * delta[2]);
		if (distance <= 400.0f * FRAMETIME)
		{
			speed = distance / FRAMETIME;
			train->moveinfo.remaining_distance = distance;
			train->nextthink = level.time + FRAMETIME;
			train->think = Move_Done;
		}
		else
		{
			speed = 400.0f;
			train->moveinfo.remaining_distance = distance -
				floorf(distance / (400.0f * FRAMETIME)) *
					400.0f * FRAMETIME;
			train->nextthink = level.time +
				floorf(distance / (400.0f * FRAMETIME)) *
					FRAMETIME;
			train->think = Move_Final;
		}
		for (axis = 0; axis < 3; axis++)
			train->velocity[axis] = delta[axis] * (speed / distance);
	}
	else
	{
		train->moveinfo.wait = train->target_ent->wait;
		train->nextthink = level.time + train->moveinfo.wait;
		train->think = train_next;
	}
}

static void SetTrainFrame(uint32_t master_corner, uint32_t companion_corner,
	int moving)
{
	SetSingleTrainFrame(Entity(KEY_MASTER), master_corner, moving);
	SetSingleTrainFrame(Entity(KEY_COMPANION), companion_corner, moving);
}

static void SetFakeIncomingLeg(edict_t *train, uint32_t target_corner,
	uint32_t authored_predecessor)
{
	vec3_t delta;
	float distance;
	float speed;
	int axis;

	train->target_ent = Entity(target_corner);
	train->target = train->target_ent->target;
	train->moveinfo.wait = train->target_ent->wait;
	for (axis = 0; axis < 3; axis++)
	{
		train->moveinfo.end_origin[axis] =
			train->target_ent->s.origin[axis] - train->mins[axis];
		train->moveinfo.start_origin[axis] =
			2.0f * train->moveinfo.end_origin[axis] -
			(Entity(authored_predecessor)->s.origin[axis] -
			 train->mins[axis]);
		train->s.origin[axis] = train->moveinfo.start_origin[axis];
	}
	VectorSubtract(train->moveinfo.end_origin,
		train->moveinfo.start_origin, delta);
	distance = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] +
		delta[2] * delta[2]);
	if (distance <= 400.0f * FRAMETIME)
	{
		speed = distance / FRAMETIME;
		train->moveinfo.remaining_distance = distance;
		train->think = Move_Done;
		train->nextthink = level.time + FRAMETIME;
	}
	else
	{
		float frames = floorf(distance / (400.0f * FRAMETIME));

		speed = 400.0f;
		train->moveinfo.remaining_distance = distance -
			frames * 400.0f * FRAMETIME;
		train->think = Move_Final;
		train->nextthink = level.time + frames * FRAMETIME;
	}
	for (axis = 0; axis < 3; axis++)
		train->velocity[axis] = delta[axis] * (speed / distance);
}

static float RouteDistance(unsigned int from, unsigned int to)
{
	vec3_t delta;

	VectorSubtract(lmctf25_route_origins[to],
		lmctf25_route_origins[from], delta);
	return sqrtf(delta[0] * delta[0] + delta[1] * delta[1] +
		delta[2] * delta[2]);
}

static unsigned int StockMoveFrames(float distance)
{
	return (unsigned int)ceilf(distance / (400.0f * FRAMETIME));
}

static void SetAuthoredMovingLeg(edict_t *train, unsigned int from,
	unsigned int to, float traveled)
{
	vec3_t delta;
	float distance = RouteDistance(from, to);
	float fraction = traveled / distance;
	float velocity = 400.0f;
	float normal_distance = floorf(distance /
		(400.0f * FRAMETIME)) * 400.0f * FRAMETIME;
	float residual_distance = distance - normal_distance;
	float future_frames = roundf((distance - traveled - residual_distance) /
		(400.0f * FRAMETIME));
	int axis;

	VectorSubtract(lmctf25_route_origins[to],
		lmctf25_route_origins[from], delta);
	train->target_ent = Entity(route[to]);
	train->target = train->target_ent->target;
	train->moveinfo.wait = train->target_ent->wait;
	VectorCopy(lmctf25_route_origins[from], train->moveinfo.start_origin);
	VectorCopy(lmctf25_route_origins[to], train->moveinfo.end_origin);
	if (distance <= 400.0f * FRAMETIME || future_frames <= 0.0f)
	{
		train->moveinfo.remaining_distance = distance <=
			400.0f * FRAMETIME ? distance : residual_distance;
		velocity = train->moveinfo.remaining_distance / FRAMETIME;
		train->think = Move_Done;
		train->nextthink = level.time + FRAMETIME;
	}
	else
	{
		train->moveinfo.remaining_distance = residual_distance;
		train->think = Move_Final;
		train->nextthink = level.time +
			future_frames * FRAMETIME;
	}
	for (axis = 0; axis < 3; axis++)
	{
		train->s.origin[axis] = lmctf25_route_origins[from][axis] +
			delta[axis] * fraction;
		train->velocity[axis] = delta[axis] * (velocity / distance);
	}
}

static void CheckPassiveEmit(sg_bot_t *bot, int expected_commands)
{
	edict_t master_before = *Entity(KEY_MASTER);
	edict_t companion_before = *Entity(KEY_COMPANION);
	int before = command_count;

	CHECK(SG_TrainStationGameEmit(bot, 0));
	CHECK(command_count - before == expected_commands);
	CHECK(memcmp(&master_before, Entity(KEY_MASTER),
	    sizeof(master_before)) == 0);
	CHECK(memcmp(&companion_before, Entity(KEY_COMPANION),
	    sizeof(companion_before)) == 0);
}

static void BuildFixture(void)
{
	uint32_t index;
	edict_t *master;
	edict_t *companion;
	edict_t *bot;

	memset(edicts, 0, sizeof(edicts));
	memset(generations, 0, sizeof(generations));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(&active_rune, 0, sizeof(active_rune));
	memset(&active_link, 0, sizeof(active_link));
	memset(seeds, 0, sizeof(seeds));
	memset(&plan, 0, sizeof(plan));
	memset(&level, 0, sizeof(level));
	memset(nodes, 0, sizeof(nodes));
	memset(&bot_client, 0, sizeof(bot_client));
	memset(&sg_host, 0, sizeof(sg_host));
	sg_host.level_alloc = LevelAlloc;
	sg_host.level_free = LevelFree;
	sg_host.trace = Trace;
	g_edicts = edicts;
	globals.num_edicts = TEST_EDICTS;
	game.maxentities = TEST_EDICTS;
	game.maxclients = 1;
	generic_binding_current = 1;
	station_binding_current = 1;
	command_count = 0;
	declared_count = 0;
	level.framenum = 1;
	level.time = 0.1f;

	InitEntity(KEY_MASTER, "func_train");
	InitEntity(KEY_COMPANION, "func_train");
	for (index = 0U; index < SG_TRAIN_STATION_ROUTE_CORNERS; index++)
	{
		InitEntity(route[index], "path_corner");
		snprintf(route_names[index], sizeof(route_names[index]), "station%u",
			index);
		Entity(route[index])->targetname = route_names[index];
		Entity(route[index])->target = route_names[(index + 1U) %
			SG_TRAIN_STATION_ROUTE_CORNERS];
		Entity(route[index])->s.origin[0] = (float)(index * 16U);
	}
	Entity(KEY_ENTRY)->wait = 3.0f;
	Entity(KEY_DESTINATION)->wait = 3.0f;
	Entity(KEY_DESTINATION)->s.origin[0] = 128.0f;
	Entity(36U)->s.origin[0] = 144.0f;
	InitEntity(KEY_BOT, "player");
	master = Entity(KEY_MASTER);
	companion = Entity(KEY_COMPANION);
	bot = Entity(KEY_BOT);
	master->teammaster = master;
	master->teamchain = companion;
	companion->teammaster = master;
	companion->flags = FL_TEAMSLAVE;
	master->spawnflags = companion->spawnflags = 1;
	master->movetype = companion->movetype = MOVETYPE_PUSH;
	master->solid = companion->solid = SOLID_BSP;
	master->moveinfo.wait = companion->moveinfo.wait = 3.0f;
	master->moveinfo.speed = companion->moveinfo.speed = 400.0f;
	master->moveinfo.accel = companion->moveinfo.accel = 400.0f;
	master->moveinfo.decel = companion->moveinfo.decel = 400.0f;
	SetBounds(master, 0.0f, 32.0f);
	SetBounds(companion, 128.0f, 32.0f);
	VectorCopy(Entity(KEY_ENTRY)->s.origin, master->s.origin);
	VectorCopy(Entity(KEY_DESTINATION)->s.origin, companion->s.origin);
	bot->client = &bot_client;
	bot->deadflag = DEAD_NO;
	bot->health = 100;
	bot->groundentity = &edicts[0];
	VectorSet(bot->s.origin, -64.0f, 0.0f, 0.0f);
	SetBounds(bot, -64.0f, 16.0f);

	seeds[0].origin[0] = -64.0f;
	seeds[1].origin[0] = 200.0f;
	active_link.from = 0;
	active_link.to = 1;
	active_link.action = RL_TRAIN;
	active_link.mode = RLCM_RIDE;
	active_link.mechanism_plan = 0U;
	VectorSet(active_link.anchor, -96.0f, 0.0f, 0.0f);
	VectorSet(active_link.mechanism_anchor, 0.0f, 0.0f, 16.0f);
	plan.entry_key = KEY_ENTRY;
	plan.mover_key = KEY_MASTER;
	plan.controller_kind = SG_MECHANISM_CONTROLLER_TRAIN_STATION;
	plan.expected_members = 2U;
	plan.cooldown_ms = 3000U;
	plan.closure_crc32 = UINT32_C(0x13572468);
	nodes[0].key = KEY_ENTRY;
	nodes[1].key = KEY_MASTER;
	nodes[2].key = KEY_DESTINATION;
	nodes[3].key = KEY_COMPANION;
	active_rune.artifact.num_links = 1U;
	active_rune.artifact.num_mechanism_plans = 1U;
	active_rune.artifact.mechanism_contract_crc32 = UINT32_C(0x24681357);
	active_rune.artifact.identity.server_frame_ms = 100U;
	active_rune.hdr.num_links = 1;
	active_rune.hdr.num_seeds = 2;
	active_rune.seeds = seeds;
	active_rune.links = &active_link;
	active_rune.mechanism_plans = &plan;

	sg_bots[0].active = true;
	sg_bots[0].ent = bot;
	sg_bots[0].commit_link = 0;
	SetTrainFrame(29U, 36U, 1);
}

static void TestHappyPathIsPassive(void)
{
	sg_bot_t *bot = &sg_bots[0];
	edict_t *entity = Entity(KEY_BOT);
	sg_train_station_board_path_t expected_path;

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	CHECK(SG_TrainStationGameOwns(bot));
	CHECK(bot->train_station.transaction.phase ==
	    SG_TRAIN_STATION_WAIT_SOURCE);
	CHECK(declared_count == 4);
	CHECK(last_declared_target[0] ==
	    bot->train_station.approach_path.points[0][0]);

	level.framenum = 10;
	level.time = 1.0f;
	VectorCopy(active_link.anchor, entity->s.origin);
	SetBounds(entity, active_link.anchor[0], 16.0f);
	SetTrainFrame(KEY_ENTRY, KEY_DESTINATION, 0);
	CheckPassiveEmit(bot, 4);
	CHECK(bot->train_station.transaction.phase ==
	    SG_TRAIN_STATION_SOURCE_DWELL);
	CHECK(SG_TrainStationApproachPathBuild(active_link.anchor,
	    active_link.mechanism_anchor, &expected_path));
	CHECK(last_declared_target[0] == expected_path.points[0][0]);
	CHECK(last_declared_target[1] == expected_path.points[0][1]);
	CHECK(last_declared_target[2] == expected_path.points[0][2]);
	CHECK(bot->train_station.boarding_path.count == expected_path.count);
	CHECK(memcmp(bot->train_station.boarding_path.points,
	    expected_path.points, sizeof(expected_path.points)) == 0);

	level.framenum = 39;
	level.time = 3.9f;
	entity->groundentity = Entity(KEY_MASTER);
	VectorSet(entity->s.origin, 0.0f, 0.0f, 16.0f);
	SetBounds(entity, 0.0f, 16.0f);
	CheckPassiveEmit(bot, 4);
	CHECK(bot->train_station.transaction.phase ==
	    SG_TRAIN_STATION_SOURCE_DWELL);

	level.framenum = 40;
	level.time = 4.0f;
	SetTrainFrame(29U, 36U, 1);
	generic_binding_current = 0;
	{
		sg_rune_mechanism_binding_t binding;

		CHECK(!SG_RuneMechanismBindingCapture(&active_rune, 0U,
		    &binding));
		CHECK(SG_RuneMechanismStationBindingCapture(&active_rune, 0U,
		    &binding));
	}
	CheckPassiveEmit(bot, 4);
	CHECK(bot->train_station.transaction.phase == SG_TRAIN_STATION_RIDE);

	level.framenum = 54;
	level.time = 5.4f;
	SetTrainFrame(34U, 39U, 1);
	CheckPassiveEmit(bot, 4);

	level.framenum = 55;
	level.time = 5.5f;
	SetBounds(Entity(KEY_MASTER), 128.0f, 32.0f);
	SetBounds(Entity(KEY_COMPANION), 0.0f, 32.0f);
	VectorSet(entity->s.origin, 128.0f, 0.0f, 16.0f);
	SetBounds(entity, 128.0f, 16.0f);
	SetTrainFrame(KEY_DESTINATION, KEY_ENTRY, 0);
	CheckPassiveEmit(bot, 4);
	CHECK(bot->train_station.transaction.phase ==
	    SG_TRAIN_STATION_DESTINATION_DWELL);

	level.framenum = 56;
	level.time = 5.6f;
	entity->groundentity = NULL;
	VectorSet(entity->s.origin, 176.0f, 0.0f, 0.0f);
	SetBounds(entity, 176.0f, 16.0f);
	CheckPassiveEmit(bot, 4);

	level.framenum = 57;
	level.time = 5.7f;
	entity->groundentity = &edicts[0];
	VectorSet(entity->s.origin, 200.0f, 0.0f, 0.0f);
	SetBounds(entity, 200.0f, 16.0f);
	CheckPassiveEmit(bot, 0);
	CHECK(!SG_TrainStationGameOwns(bot));
	CHECK(bot->train_station.transaction.phase ==
	    SG_TRAIN_STATION_COMPLETE);
	CHECK(bot->commit_link == -1);
}

static void TestApproachPathMatchesGeneration(void)
{
	sg_bot_t *bot = &sg_bots[0];
	edict_t *entity;
	sg_train_station_board_path_t expected;

	BuildFixture();
	entity = Entity(KEY_BOT);
	VectorSet(seeds[0].origin, 352.0f, -192.0f, 0.0f);
	VectorCopy(seeds[0].origin, entity->s.origin);
	VectorSet(active_link.anchor, -160.0f, -320.0f, 0.0f);
	CHECK(SG_TrainStationApproachPathBuild(seeds[0].origin,
	    active_link.anchor, &expected));
	CheckPassiveEmit(bot, 4);
	CHECK(memcmp(&bot->train_station.approach_path, &expected,
	    sizeof(expected)) == 0);
	CHECK(last_declared_target[0] == expected.points[0][0]);
	CHECK(last_declared_target[1] == expected.points[0][1]);
	CHECK(last_declared_target[2] == expected.points[0][2]);
}

static void TestIdentityAndSynchronizationDriftFailClosed(void)
{
	sg_bot_t *bot = &sg_bots[0];

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	generations[KEY_COMPANION]++;
	CheckPassiveEmit(bot, 0);
	CHECK(!SG_TrainStationGameOwns(bot));
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);
	CHECK(bot->commit_link == -1);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_COMPANION)->teammaster = NULL;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_MASTER)->nextthink += 1.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_COMPANION)->nextthink += 1.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_COMPANION)->nextthink = level.time;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_COMPANION)->target_ent = Entity(37U);
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	station_binding_current = 0;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_BINDING_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	active_link.anchor[0] -= 8.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_BINDING_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	active_link.mechanism_anchor[0] += 8.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_BINDING_DRIFT);
}

static void BuildReverseFixture(void)
{
	edict_t *entity;

	BuildFixture();
	seeds[0].origin[0] = 208.0f;
	seeds[1].origin[0] = -64.0f;
	active_link.mechanism_anchor[0] = 128.0f;
	active_link.anchor[0] = 192.0f;
	entity = Entity(KEY_BOT);
	VectorSet(entity->s.origin, 208.0f, 0.0f, 0.0f);
	SetBounds(entity, 208.0f, 16.0f);
}

static void TestReverseBoardsCompanion(void)
{
	sg_bot_t *bot = &sg_bots[0];
	edict_t *entity;
	sg_train_station_board_path_t expected_path;

	BuildReverseFixture();
	entity = Entity(KEY_BOT);
	CheckPassiveEmit(bot, 4);
	CHECK(bot->train_station.ride_key == KEY_COMPANION);
	CHECK(bot->train_station.transaction.spec.source_key ==
	    KEY_DESTINATION);
	CHECK(last_declared_target[0] == active_link.anchor[0]);

	level.framenum = 10;
	level.time = 1.0f;
	VectorCopy(active_link.anchor, entity->s.origin);
	SetBounds(entity, active_link.anchor[0], 16.0f);
	SetTrainFrame(KEY_ENTRY, KEY_DESTINATION, 0);
	CheckPassiveEmit(bot, 4);
	CHECK(SG_TrainStationApproachPathBuild(active_link.anchor,
	    active_link.mechanism_anchor, &expected_path));
	CHECK(last_declared_target[0] == expected_path.points[0][0]);
	CHECK(last_declared_target[1] == expected_path.points[0][1]);
	CHECK(last_declared_target[2] == expected_path.points[0][2]);
	CHECK(memcmp(bot->train_station.boarding_path.points,
	    expected_path.points, sizeof(expected_path.points)) == 0);
	level.framenum = 39;
	level.time = 3.9f;
	entity->groundentity = Entity(KEY_COMPANION);
	VectorSet(entity->s.origin, 128.0f, 0.0f, 16.0f);
	SetBounds(entity, 128.0f, 16.0f);
	CheckPassiveEmit(bot, 4);
	level.framenum = 40;
	level.time = 4.0f;
	SetTrainFrame(29U, 36U, 1);
	CheckPassiveEmit(bot, 4);
	CHECK(bot->train_station.transaction.phase == SG_TRAIN_STATION_RIDE);

	level.framenum = 54;
	level.time = 5.4f;
	SetTrainFrame(34U, 39U, 1);
	CheckPassiveEmit(bot, 4);

	level.framenum = 55;
	level.time = 5.5f;
	SetBounds(Entity(KEY_MASTER), 128.0f, 32.0f);
	SetBounds(Entity(KEY_COMPANION), 0.0f, 32.0f);
	VectorSet(entity->s.origin, 0.0f, 0.0f, 16.0f);
	SetBounds(entity, 0.0f, 16.0f);
	SetTrainFrame(KEY_DESTINATION, KEY_ENTRY, 0);
	CheckPassiveEmit(bot, 4);
	CHECK(bot->train_station.transaction.phase ==
	    SG_TRAIN_STATION_DESTINATION_DWELL);

	level.framenum = 56;
	level.time = 5.6f;
	entity->groundentity = NULL;
	VectorSet(entity->s.origin, -40.0f, 0.0f, 0.0f);
	SetBounds(entity, -40.0f, 16.0f);
	CheckPassiveEmit(bot, 4);

	level.framenum = 57;
	level.time = 5.7f;
	entity->groundentity = &edicts[0];
	VectorSet(entity->s.origin, -64.0f, 0.0f, 0.0f);
	SetBounds(entity, -64.0f, 16.0f);
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.phase ==
	    SG_TRAIN_STATION_COMPLETE);
	CHECK(!SG_TrainStationGameOwns(bot));
	CHECK(bot->commit_link == -1);
}

static void TestPhysicalPoseDriftFailsClosed(void)
{
	sg_bot_t *bot = &sg_bots[0];

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	level.framenum = 10;
	level.time = 1.0f;
	SetSingleTrainFrame(Entity(KEY_COMPANION), 36U, 0);
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	level.framenum = 10;
	level.time = 1.0f;
	SetTrainFrame(KEY_ENTRY, KEY_DESTINATION, 0);
	Entity(KEY_MASTER)->s.origin[0] += 1.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_MASTER)->s.origin[1] += 1.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_COMPANION)->s.origin[1] += 1.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	VectorSet(Entity(KEY_MASTER)->velocity, 0.0f, 400.0f, 0.0f);
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	VectorSet(Entity(KEY_COMPANION)->velocity, 0.0f, 400.0f, 0.0f);
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_MASTER)->velocity[0] *= 2.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_COMPANION)->velocity[0] *= 2.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_MASTER)->think = Move_Done;
	Entity(KEY_MASTER)->moveinfo.remaining_distance = 80.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	level.framenum = 10;
	level.time = 1.0f;
	SetTrainFrame(KEY_ENTRY, KEY_DESTINATION, 0);
	Entity(KEY_COMPANION)->s.origin[0] += 1.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_MASTER)->moveinfo.end_origin[0] += 1.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	Entity(KEY_COMPANION)->moveinfo.end_origin[0] += 1.0f;
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);
}

static void TestAsymmetricStockLegsRemainAuthenticated(void)
{
	sg_bot_t *bot = &sg_bots[0];
	edict_t *master;
	edict_t *companion;
	unsigned int upper_frames = 0U;
	unsigned int lower_frames = 0U;
	unsigned int index;
	float master_nextthink;
	float companion_nextthink;

	BuildFixture();
	master = Entity(KEY_MASTER);
	companion = Entity(KEY_COMPANION);
	for (index = 0U; index < SG_TRAIN_STATION_ROUTE_CORNERS; index++)
	{
		VectorCopy(lmctf25_route_origins[index],
			Entity(route[index])->s.origin);
		if (index < SG_TRAIN_STATION_ROUTE_CORNERS / 2U)
			upper_frames += StockMoveFrames(RouteDistance(index,
				index + 1U));
		else
			lower_frames += StockMoveFrames(RouteDistance(index,
				(index + 1U) % SG_TRAIN_STATION_ROUTE_CORNERS));
	}
	CHECK(RouteDistance(0U, 1U) == 904.0f);
	CHECK(RouteDistance(7U, 8U) == 956.0f);
	CHECK(StockMoveFrames(RouteDistance(0U, 1U)) == 23U);
	CHECK(StockMoveFrames(RouteDistance(7U, 8U)) == 24U);
	CHECK(upper_frames == 84U);
	CHECK(lower_frames == 86U);
	VectorCopy(lmctf25_route_origins[0U], master->s.origin);
	VectorCopy(lmctf25_route_origins[7U], companion->s.origin);
	SetAuthoredMovingLeg(master, 0U, 1U, 0.0f);
	SetAuthoredMovingLeg(companion, 7U, 8U, 0.0f);
	master_nextthink = master->nextthink;
	companion_nextthink = companion->nextthink;
	CHECK(fabsf((master_nextthink - level.time) - 2.2f) < 0.001f);
	CHECK(fabsf((companion_nextthink - level.time) - 2.3f) < 0.001f);
	VectorCopy(lmctf25_route_origins[0U], seeds[0].origin);
	seeds[0].origin[0] -= 64.0f;
	VectorCopy(seeds[0].origin, active_link.anchor);
	active_link.anchor[0] += 32.0f;
	VectorCopy(active_link.anchor, active_link.mechanism_anchor);
	active_link.mechanism_anchor[0] += 16.0f;
	CheckPassiveEmit(bot, 4);
	level.framenum = 12;
	level.time = 1.2f;
	SetAuthoredMovingLeg(master, 0U, 1U, 440.0f);
	SetAuthoredMovingLeg(companion, 7U, 8U, 440.0f);
	CHECK(fabsf(master->nextthink - 2.3f) < 0.001f);
	CHECK(fabsf(companion->nextthink - 2.4f) < 0.001f);
	CheckPassiveEmit(bot, 4);
	CHECK(SG_TrainStationGameOwns(bot));
	level.framenum = 24;
	level.time = 2.4f;
	SetAuthoredMovingLeg(master, 1U, 2U, 0.0f);
	SetAuthoredMovingLeg(companion, 7U, 8U, 920.0f);
	CheckPassiveEmit(bot, 4);
	CHECK(SG_TrainStationGameOwns(bot));
	CHECK(bot->train_station.transaction.phase ==
	    SG_TRAIN_STATION_WAIT_SOURCE);

	BuildFixture();
	bot = &sg_bots[0];
	master = Entity(KEY_MASTER);
	companion = Entity(KEY_COMPANION);
	for (index = 0U; index < SG_TRAIN_STATION_ROUTE_CORNERS; index++)
		VectorCopy(lmctf25_route_origins[index],
			Entity(route[index])->s.origin);
	VectorCopy(lmctf25_route_origins[0U], master->s.origin);
	VectorCopy(lmctf25_route_origins[7U], companion->s.origin);
	SetAuthoredMovingLeg(master, 0U, 1U, 0.0f);
	SetAuthoredMovingLeg(companion, 7U, 8U, 0.0f);
	VectorCopy(lmctf25_route_origins[7U], seeds[0].origin);
	seeds[0].origin[0] += 64.0f;
	VectorCopy(lmctf25_route_origins[0U], seeds[1].origin);
	seeds[1].origin[0] -= 64.0f;
	VectorCopy(seeds[0].origin, Entity(KEY_BOT)->s.origin);
	SetBounds(Entity(KEY_BOT), seeds[0].origin[0], 16.0f);
	VectorCopy(seeds[0].origin, active_link.anchor);
	active_link.anchor[0] += 32.0f;
	VectorCopy(active_link.anchor, active_link.mechanism_anchor);
	active_link.mechanism_anchor[0] += 16.0f;
	CheckPassiveEmit(bot, 4);
	level.framenum = 24;
	level.time = 2.4f;
	SetAuthoredMovingLeg(master, 1U, 2U, 0.0f);
	SetAuthoredMovingLeg(companion, 7U, 8U, 920.0f);
	CheckPassiveEmit(bot, 4);
	CHECK(SG_TrainStationGameOwns(bot));
	CHECK(bot->train_station.ride_key == KEY_COMPANION);
}

static void TestUnauthoredIncomingLegsFailClosed(void)
{
	sg_bot_t *bot = &sg_bots[0];

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	SetFakeIncomingLeg(Entity(KEY_MASTER), 29U, KEY_ENTRY);
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	SetFakeIncomingLeg(Entity(KEY_COMPANION), 36U, KEY_DESTINATION);
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildReverseFixture();
	CheckPassiveEmit(bot, 4);
	SetFakeIncomingLeg(Entity(KEY_MASTER), 29U, KEY_ENTRY);
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);

	BuildReverseFixture();
	CheckPassiveEmit(bot, 4);
	SetFakeIncomingLeg(Entity(KEY_COMPANION), 36U, KEY_DESTINATION);
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);
}

static void TestWrongTrainIsNotAboard(void)
{
	sg_bot_t *bot = &sg_bots[0];
	edict_t *entity;

	BuildReverseFixture();
	entity = Entity(KEY_BOT);
	CheckPassiveEmit(bot, 4);
	level.framenum = 10;
	level.time = 1.0f;
	SetTrainFrame(KEY_ENTRY, KEY_DESTINATION, 0);
	CheckPassiveEmit(bot, 4);
	level.framenum = 39;
	level.time = 3.9f;
	entity->groundentity = Entity(KEY_MASTER);
	VectorSet(entity->s.origin, 0.0f, 0.0f, 16.0f);
	SetBounds(entity, 0.0f, 16.0f);
	CheckPassiveEmit(bot, 4);
	level.framenum = 40;
	level.time = 4.0f;
	SetTrainFrame(29U, 36U, 1);
	CheckPassiveEmit(bot, 0);
	CHECK(bot->train_station.transaction.reason ==
	    SG_TRAIN_STATION_REASON_BOARDING_MISSED);
	CHECK(!SG_TrainStationGameOwns(bot));
}

static void TestLifecycleResetIsPassive(void)
{
	sg_bot_t *bot = &sg_bots[0];
	edict_t master_before;
	edict_t companion_before;

	BuildFixture();
	CheckPassiveEmit(bot, 4);
	master_before = *Entity(KEY_MASTER);
	companion_before = *Entity(KEY_COMPANION);
	SG_TrainStationGameReset(bot);
	CHECK(!SG_TrainStationGameOwns(bot));
	CHECK(bot->commit_link == -1);
	CHECK(memcmp(&master_before, Entity(KEY_MASTER),
	    sizeof(master_before)) == 0);
	CHECK(memcmp(&companion_before, Entity(KEY_COMPANION),
	    sizeof(companion_before)) == 0);
}

static void TestRejectsWrongFrameLawAndStoppedBegin(void)
{
	sg_bot_t *bot = &sg_bots[0];

	BuildFixture();
	active_rune.artifact.identity.server_frame_ms = 50U;
	CHECK(SG_TrainStationGameEmit(bot, 0));
	CHECK(!SG_TrainStationGameOwns(bot));
	CHECK(command_count == 0);
	CHECK(bot->commit_link == -1);

	BuildFixture();
	SetTrainFrame(KEY_ENTRY, KEY_DESTINATION, 0);
	CHECK(SG_TrainStationGameEmit(bot, 0));
	CHECK(!SG_TrainStationGameOwns(bot));
	CHECK(command_count == 0);
}

static void TestRejectsAbsentOrAliasedApproach(void)
{
	sg_bot_t *bot = &sg_bots[0];

	BuildFixture();
	active_link.anchor[0] = NAN;
	CHECK(SG_TrainStationGameEmit(bot, 0));
	CHECK(!SG_TrainStationGameOwns(bot));
	CHECK(command_count == 0);
	CHECK(bot->commit_link == -1);

	BuildFixture();
	VectorCopy(seeds[0].origin, active_link.anchor);
	CHECK(SG_TrainStationGameEmit(bot, 0));
	CHECK(!SG_TrainStationGameOwns(bot));
	CHECK(command_count == 0);
	CHECK(bot->commit_link == -1);

	BuildFixture();
	VectorCopy(active_link.mechanism_anchor, active_link.anchor);
	CHECK(SG_TrainStationGameEmit(bot, 0));
	CHECK(!SG_TrainStationGameOwns(bot));
	CHECK(command_count == 0);
	CHECK(bot->commit_link == -1);
}

static void TestNonStationFallsThroughAndStationAuthFailsClosed(void)
{
	sg_bot_t *bot = &sg_bots[0];

	BuildFixture();
	plan.controller_kind = SG_MECHANISM_CONTROLLER_TRAIN;
	CHECK(!SG_TrainStationGameEmit(bot, 0));
	CHECK(command_count == 0);
	CHECK(bot->commit_link == 0);

	BuildFixture();
	station_binding_current = 0;
	CHECK(SG_TrainStationGameEmit(bot, 0));
	CHECK(command_count == 0);
	CHECK(bot->commit_link == -1);
}

int main(void)
{
	TestHappyPathIsPassive();
	TestApproachPathMatchesGeneration();
	TestIdentityAndSynchronizationDriftFailClosed();
	TestReverseBoardsCompanion();
	TestPhysicalPoseDriftFailsClosed();
	TestAsymmetricStockLegsRemainAuthenticated();
	TestUnauthoredIncomingLegsFailClosed();
	TestWrongTrainIsNotAboard();
	TestLifecycleResetIsPassive();
	TestRejectsWrongFrameLawAndStoppedBegin();
	TestRejectsAbsentOrAliasedApproach();
	TestNonStationFallsThroughAndStationAuthFailsClosed();
	if (failures)
	{
		fprintf(stderr, "sg_train_station_game_test: %d failures\n",
		    failures);
		return 1;
	}
	puts("sg_train_station_game_test: PASS");
	return 0;
}
