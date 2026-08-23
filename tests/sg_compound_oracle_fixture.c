#include <stdlib.h>

#include "sg_compound_oracle_fixture.h"

static void *GameAllocate(int size)
{
	return malloc((size_t)size);
}

static void GameFree(void *block)
{
	free(block);
}

void Door(edict_t *door)
{
	memset(door, 0, sizeof(*door));
	door->inuse = true;
	door->classname = "func_door";
	door->solid = SOLID_BSP;
	door->movetype = MOVETYPE_PUSH;
	door->use = door_use;
	door->blocked = door_blocked;
	door->teammaster = door;
	door->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	door->moveinfo.speed = 200.0f;
	door->moveinfo.accel = 200.0f;
	door->moveinfo.decel = 200.0f;
	door->moveinfo.wait = 3.0f;
	door->wait = 3.0f;
	door->moveinfo.distance = 80.0f;
	Set3(door->mins, -8.0f, -16.0f, -24.0f);
	Set3(door->maxs, 8.0f, 16.0f, 32.0f);
	Set3(door->s.origin, 0.0f, 0.0f, 0.0f);
	Set3(door->s.old_origin, -3.0f, -4.0f, -5.0f);
	Set3(door->pos1, 0.0f, 0.0f, 0.0f);
	Set3(door->pos2, 80.0f, 0.0f, 0.0f);
	Set3(door->moveinfo.start_origin, 0.0f, 0.0f, 0.0f);
	Set3(door->moveinfo.end_origin, 80.0f, 0.0f, 0.0f);
	Set3(door->movedir, 1.0f, 0.0f, 0.0f);
	Set3(door->absmin, -91.0f, -92.0f, -93.0f);
	Set3(door->absmax, 91.0f, 92.0f, 93.0f);
	Set3(door->size, 17.0f, 18.0f, 19.0f);
	door->linkcount = 7;
}

void Trigger(edict_t *trigger, edict_t *door, float mechanism_x)
{
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->classname = "noclass";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_DoorTrigger;
	trigger->owner = door;
	Set3(trigger->absmin, mechanism_x - 1.0f, -24.0f, -40.0f);
	Set3(trigger->absmax, mechanism_x + 1.0f, 24.0f, 40.0f);
}

edict_t *GuardDoor(int key)
{
	edict_t *door = &fixture_edicts[key];

	Door(door);
	door->s.number = key;
	door->area.prev = &fixture_edicts[0].area;
	door->area.next = &fixture_edicts[0].area;
	Set3(door->size, 16.0f, 32.0f, 56.0f);
	Set3(door->absmin, -9.0f, -17.0f, -25.0f);
	Set3(door->absmax, 9.0f, 17.0f, 33.0f);
	door->moveinfo.state = SG_PLAT_STATE_TOP;
	door->think = door_go_down;
	door->nextthink = level.time + 0.1f;
	return door;
}

fixture_config_t DefaultConfig(int touch, fixture_suffix_t suffix)
{
	fixture_config_t config;

	memset(&config, 0, sizeof(config));
	config.touch_substep = touch;
	config.suffix = suffix;
	config.mechanism_x = 160.0f;
	config.source_x = 200.0f;
	return config;
}

void ResetFixture(const fixture_config_t *config)
{
	int key;

	memset(fixture_edicts, 0, sizeof(fixture_edicts));
	memset(fixture_clients, 0, sizeof(fixture_clients));
	memset(&fixture_gravity, 0, sizeof(fixture_gravity));
	memset(&fixture_observation, 0, sizeof(fixture_observation));
	memset(&globals, 0, sizeof(globals));
	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	fixture_config = *config;
	g_edicts = fixture_edicts;
	globals.edicts = fixture_edicts;
	globals.edict_size = sizeof(edict_t);
	globals.num_edicts = config->loader_unowned ? 8 :
	    ((config->loader_transient || config->loader_malformed) ? 7 : 6);
	globals.max_edicts = FIXTURE_EDICTS;
	game.maxentities = FIXTURE_EDICTS;
	game.maxclients = (config->loader_transient || config->loader_malformed ||
	                   config->loader_unowned) ? 5 : 1;
	game.clients = fixture_clients;
	SG_MoverCompletionReset();
	fixture_gravity.value = 777.0f;
	sv_gravity = &fixture_gravity;
	fixture_edicts[0].inuse = true;
	Door(&fixture_edicts[1]);
	fixture_edicts[1].s.number = 1;
	Trigger(&fixture_edicts[2], &fixture_edicts[1], config->mechanism_x);
	fixture_edicts[3].inuse = true;
	fixture_edicts[3].classname = "trigger_hurt";
	fixture_edicts[3].solid = SOLID_TRIGGER;
	fixture_edicts[3].touch = Touch_Multi;
	Set3(fixture_edicts[3].absmin, 1000.0f, 1000.0f, 1000.0f);
	Set3(fixture_edicts[3].absmax, 1010.0f, 1010.0f, 1010.0f);
	fixture_edicts[4].inuse = true;
	fixture_edicts[4].classname = "func_wall";
	fixture_edicts[4].solid = SOLID_BSP;
	fixture_edicts[4].movetype = MOVETYPE_PUSH;
	if (config->loader_transient || config->loader_malformed)
	{
		fixture_edicts[5].inuse = true;
		fixture_edicts[5].solid = SOLID_BBOX;
		fixture_edicts[5].client = config->loader_malformed
		    ? &fixture_clients[3] : &fixture_clients[4];
		fixture_edicts[5].area.prev = &fixture_edicts[0].area;
		fixture_edicts[5].area.next = &fixture_edicts[0].area;
		fixture_edicts[6].inuse = true;
		fixture_edicts[6].solid = SOLID_BBOX;
		fixture_edicts[6].owner = &fixture_edicts[5];
		fixture_edicts[6].area.prev = &fixture_edicts[0].area;
		fixture_edicts[6].area.next = &fixture_edicts[0].area;
	}
	if (config->loader_unowned)
	{
		fixture_edicts[7].inuse = true;
		fixture_edicts[7].solid = SOLID_BBOX;
		fixture_edicts[7].area.prev = &fixture_edicts[0].area;
		fixture_edicts[7].area.next = &fixture_edicts[0].area;
	}
	for (key = 0; key < FIXTURE_EDICTS; key++)
		fixture_edicts[key].s.number = key;

	memset(&sg_host, 0, sizeof(sg_host));
	sg_host.trace = HostTrace;
	sg_host.pointcontents = HostPointContents;
	sg_host.box_edicts = HostBoxEdicts;
	sg_host.pmove = HostPmove;
	sg_host.game_alloc = GameAllocate;
	sg_host.game_free = GameFree;
	sg_host.linkentity = HostLinkEntity;
}

void ResetGuardFixture(void)
{
	fixture_config_t config =
	    DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	int key;

	ResetFixture(&config);
	memset(&globals, 0, sizeof(globals));
	memset(&game, 0, sizeof(game));
	g_edicts = fixture_edicts;
	globals.edicts = fixture_edicts;
	globals.edict_size = sizeof(edict_t);
	globals.num_edicts = FIXTURE_EDICTS;
	globals.max_edicts = FIXTURE_EDICTS;
	game.maxentities = FIXTURE_EDICTS;
	game.maxclients = 1;
	game.clients = fixture_clients;
	level.time = 10.0f;
	for (key = 0; key < FIXTURE_EDICTS; key++)
		fixture_edicts[key].s.number = key;
}

void GuardDoorPair(edict_t **master_out, edict_t **member_out)
{
	edict_t *master;
	edict_t *member;

	ResetGuardFixture();
	master = GuardDoor(GUARD_MASTER_KEY);
	member = GuardDoor(GUARD_MEMBER_KEY);
	master->team = "guard-pair";
	member->team = "guard-pair";
	master->teamchain = member;
	member->teammaster = master;
	member->flags |= FL_TEAMSLAVE;
	if (master_out)
		*master_out = master;
	if (member_out)
		*member_out = member;
}

void InitPhantom(sg_phantom_t *phantom, qboolean damaging_fall)
{
	memset(phantom, 0, sizeof(*phantom));
	phantom->pms.pm_type = PM_NORMAL;
	phantom->pms.origin[0] = (short)(fixture_config.source_x * 8.0f);
	phantom->pms.velocity[1] = 64;
	phantom->pms.velocity[2] = damaging_fall ? -8000 : 0;
	phantom->pms.gravity = 777;
	phantom->pms.delta_angles[YAW] = 321;
	phantom->old_pms = phantom->pms;
	phantom->old_pms.origin[0] =
		(short)(fixture_config.source_x * 8.0f + 8.0f);
	phantom->origin[0] = fixture_config.source_x;
	phantom->velocity[1] = 8.0f;
	phantom->velocity[2] = damaging_fall ? -1000.0f : 0.0f;
	phantom->watertype = CONTENTS_WATER;
	phantom->waterlevel = 3;
}

void SyncRecoveryPassent(const sg_phantom_t *phantom,
	edict_t *passent)
{
	gclient_t *client = passent->client;

	VectorCopy(phantom->origin, passent->s.origin);
	VectorCopy(phantom->velocity, passent->velocity);
	passent->groundentity = phantom->groundentity ? &fixture_edicts[0] : NULL;
	passent->watertype = phantom->watertype;
	passent->waterlevel = phantom->waterlevel;
	client->ps.pmove = phantom->pms;
	client->old_pmove = phantom->old_pms;
}

edict_t *InitRecoveryState(sg_phantom_t *phantom,
	const sg_compound_world_preopen_t *resolved, int suffix_commands)
{
	edict_t *member = &fixture_edicts[1];
	edict_t *passent = &fixture_edicts[5];
	gclient_t *client = &fixture_clients[0];
	int x;

	VectorCopy(resolved->top_origin, member->s.origin);
	VectorCopy(resolved->top_origin, member->s.old_origin);
	VectorClear(member->velocity);
	VectorClear(member->avelocity);
	member->moveinfo.state = SG_PLAT_STATE_TOP;
	member->moveinfo.endfunc = door_hit_top;
	member->think = door_go_down;
	level.time = 10.0f;
	member->nextthink = 11.0f;
	HostLinkEntity(member);
	PublishDoorCompletion(member, SG_MOVER_COMPLETION_TOP);

	InitPhantom(phantom, false);
	fixture_observation.suffix_commands = suffix_commands;
	x = SuffixX();
	phantom->pms.origin[0] = (short)(x * 8);
	phantom->pms.velocity[0] = 64;
	phantom->origin[0] = (float)x;
	phantom->velocity[0] = 8.0f;
	phantom->old_pms = phantom->pms;
	phantom->old_pms.origin[0] += 8;

	memset(passent, 0, sizeof(*passent));
	passent->inuse = true;
	passent->client = client;
	passent->health = 100;
	passent->movetype = MOVETYPE_WALK;
	passent->s.modelindex = 255;
	client->oldvelocity[2] = 0.0f;
	SyncRecoveryPassent(phantom, passent);
	return passent;
}

rune_reject_reason_t Resolve(
	sg_compound_world_preopen_t *resolved)
{
	vec3_t anchor;

	Set3(anchor, fixture_config.mechanism_x, 0.0f, 0.0f);
	return SG_CompoundWorldResolvePreopen(anchor, resolved);
}

qboolean CanonicalHint(sg_compound_world_preopen_t *resolved,
	vec3_t hint)
{
	sg_compound_world_candidate_t candidate;
	int count = 0;

	if (SG_CompoundWorldEnumeratePreopen(&candidate, 1, &count) != RLR_OK ||
	    count != 1 || candidate.hint_count <= 0)
		return false;
	*resolved = candidate.resolved;
	VectorCopy(candidate.hints[0], hint);
	return true;
}

qboolean MemberRestored(const edict_t *member,
	const edict_t *before)
{
	return VectorCompare((vec_t *)member->s.origin,
	                     (vec_t *)before->s.origin) &&
	       VectorCompare((vec_t *)member->s.old_origin,
	                     (vec_t *)before->s.old_origin) &&
	       VectorCompare((vec_t *)member->absmin,
	                     (vec_t *)before->absmin) &&
	       VectorCompare((vec_t *)member->absmax,
	                     (vec_t *)before->absmax) &&
	       VectorCompare((vec_t *)member->size,
	                     (vec_t *)before->size) &&
	       VectorCompare((vec_t *)member->velocity,
	                     (vec_t *)before->velocity) &&
	       VectorCompare((vec_t *)member->avelocity,
	                     (vec_t *)before->avelocity) &&
	       member->solid == before->solid &&
	       member->linkcount == before->linkcount &&
	       member->moveinfo.state == before->moveinfo.state &&
	       member->moveinfo.endfunc == before->moveinfo.endfunc &&
	       member->think == before->think &&
	       member->nextthink == before->nextthink &&
	       member->s.number == before->s.number;
}

void CheckStaticContextRestored(void)
{
	sg_phantom_t phantom;
	sg_swim_proof_t proof;
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	int calls;

	InitPhantom(&phantom, false);
	fixture_observation.top_staged = false;
	fixture_observation.approach_commands = 0;
	fixture_config.touch_substep = 99;
	fixture_config.contaminate_trigger = false;
	fixture_config.contaminate_solid = false;
	fixture_config.force_foreign_trigger = false;
	calls = fixture_observation.pmove_calls;
	CHECK(!SG_OracleSwimTraverse(&phantom, destination, true, 0.0f,
	                             &proof, NULL, true));
	CHECK(fixture_observation.pmove_calls == calls);
}
