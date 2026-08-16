/* Focused host fixture for dormant PREOPEN D_SWIM oracle replay. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_compound.h"
#include "slipgate/sg_compound_world.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_util.h"

game_export_t globals;
edict_t *g_edicts;
level_locals_t level;
sg_host_t sg_host;
cvar_t *sv_gravity;

short SG_RuneProofGravity(void)
{
	return 800;
}

typedef enum fixture_suffix_e
{
	FIXTURE_SUFFIX_SUCCESS = 0,
	FIXTURE_SUFFIX_NO_SWEEP,
	FIXTURE_SUFFIX_REENTRY,
	FIXTURE_SUFFIX_ARRIVE_BEFORE_CLEAR,
	FIXTURE_SUFFIX_ALWAYS_OUTSIDE,
	FIXTURE_SUFFIX_BETWEEN_RECROSS,
	FIXTURE_SUFFIX_PRECLEAR_CHORD,
	FIXTURE_SUFFIX_POSTCLEAR_CHORD
} fixture_suffix_t;

typedef struct fixture_config_s
{
	int touch_substep;
	fixture_suffix_t suffix;
	qboolean contaminate_trigger;
	qboolean contaminate_solid;
	qboolean hazard_ride;
	qboolean fall_ride;
	qboolean wrong_contact;
	qboolean unstable_contact;
	qboolean opening_drift;
	qboolean source_hazard;
	qboolean source_dry;
	qboolean force_foreign_trigger;
	qboolean suffix_hazard;
	float mechanism_x;
	float source_x;
} fixture_config_t;

typedef struct fixture_observation_s
{
	int pmove_calls;
	int approach_commands;
	int zero_commands;
	int ride_zero_commands;
	int suffix_commands;
	int trace_calls;
	int link_calls;
	float link_origins[16];
	qboolean stage_started;
	qboolean top_staged;
	qboolean first_snapinitial;
	int later_snapinitial;
	qboolean first_top_seen;
	usercmd_t first_top_command;
	int callback_calls;
	int last_pmove_mask;
	int loader_pmove_masks;
	int normal_pmove_masks;
	int pretop_contact_traces;
} fixture_observation_t;

static edict_t fixture_edicts[8];
static gclient_t fixture_clients[1];
static cvar_t fixture_gravity;
static fixture_config_t fixture_config;
static fixture_observation_t fixture_observation;
static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void Set3(vec3_t value, float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static qboolean CommandZero(const usercmd_t *command)
{
	return command->forwardmove == 0 && command->sidemove == 0 &&
	       command->upmove == 0;
}

void Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surface)
{
	(void)self; (void)other; (void)plane; (void)surface;
	fixture_observation.callback_calls++;
}

void Touch_Multi(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surface)
{
	(void)self; (void)other; (void)plane; (void)surface;
	fixture_observation.callback_calls++;
}

void Touch_Item(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surface)
{
	(void)self; (void)other; (void)plane; (void)surface;
	fixture_observation.callback_calls++;
}

void door_blocked(edict_t *self, edict_t *other)
{
	(void)self; (void)other;
	fixture_observation.callback_calls++;
}

void door_secret_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
	fixture_observation.callback_calls++;
}

void door_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
	fixture_observation.callback_calls++;
}

void door_go_down(edict_t *self)
{
	(void)self;
	fixture_observation.callback_calls++;
}

void trigger_relay_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
	fixture_observation.callback_calls++;
}

void Use_Target_Speaker(edict_t *self, edict_t *other,
	edict_t *activator)
{
	(void)self; (void)other; (void)activator;
	fixture_observation.callback_calls++;
}

qboolean SG_ImmutableSupport(edict_t *entity)
{
	return entity == &fixture_edicts[0];
}

edict_t *G_Find(edict_t *from, int field_offset, char *match)
{
	edict_t *candidate = from ? from + 1 : g_edicts;

	for (; candidate < &g_edicts[globals.num_edicts]; candidate++)
	{
		char *value;

		if (!candidate->inuse)
			continue;
		value = *(char **)((byte *)candidate + field_offset);
		if (value && match && !Q_stricmp(value, match))
			return candidate;
	}
	return NULL;
}

static trace_t HostTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int mask)
{
	trace_t trace;

	(void)start; (void)maxs; (void)end; (void)passent; (void)mask;
	memset(&trace, 0, sizeof(trace));
	fixture_observation.trace_calls++;
	trace.fraction = 1.0f;
	trace.ent = &fixture_edicts[0];
	if (!mins && !fixture_observation.top_staged)
		fixture_observation.pretop_contact_traces++;
	/* Exercise exact-member trace admission for every Pmove command.  Chest
	 * arrival traces have no hull and remain clear. */
	if (mins)
	{
		fixture_observation.last_pmove_mask = mask;
		if (mask == (MASK_PLAYERSOLID & ~CONTENTS_MONSTER))
			fixture_observation.loader_pmove_masks++;
		if (mask == MASK_PLAYERSOLID)
			fixture_observation.normal_pmove_masks++;
		trace.fraction = 0.5f;
		trace.ent = &fixture_edicts[1];
	}
	return trace;
}

static int HostPointContents(const vec3_t point)
{
	(void)point;
	return CONTENTS_WATER;
}

static int HostBoxEdicts(const vec3_t mins, const vec3_t maxs,
	edict_t **list, int max_count, int area_type)
{
	int count = 0;

	(void)mins; (void)maxs;
	if (max_count <= 0)
		return 0;
	if (area_type == AREA_TRIGGERS && !fixture_observation.top_staged &&
	    maxs[0] > fixture_edicts[2].absmin[0] &&
	    mins[0] < fixture_edicts[2].absmax[0] &&
	    maxs[1] > fixture_edicts[2].absmin[1] &&
	    mins[1] < fixture_edicts[2].absmax[1] &&
	    maxs[2] > fixture_edicts[2].absmin[2] &&
	    mins[2] < fixture_edicts[2].absmax[2])
	{
		list[count++] = &fixture_edicts[2];
		if (fixture_config.contaminate_trigger && count < max_count)
			list[count++] = &fixture_edicts[3];
	}
	if (area_type == AREA_TRIGGERS && fixture_config.force_foreign_trigger &&
	    count < max_count)
		list[count++] = &fixture_edicts[3];
	if (area_type == AREA_SOLID)
	{
		list[count++] = &fixture_edicts[1];
		if (fixture_config.contaminate_solid && count < max_count)
			list[count++] = &fixture_edicts[4];
	}
	return count;
}

static void HostLinkEntity(edict_t *entity)
{
	int index = fixture_observation.link_calls;

	if (index < (int)(sizeof(fixture_observation.link_origins) /
	                  sizeof(fixture_observation.link_origins[0])))
		fixture_observation.link_origins[index] = entity->s.origin[0];
	fixture_observation.link_calls++;
	fixture_observation.stage_started = true;
	if (entity->s.origin[0] == 80.0f)
		fixture_observation.top_staged = true;
	entity->linkcount++;
	Set3(entity->absmin, entity->s.origin[0] + entity->mins[0] - 1.0f,
	     entity->s.origin[1] + entity->mins[1] - 1.0f,
	     entity->s.origin[2] + entity->mins[2] - 1.0f);
	Set3(entity->absmax, entity->s.origin[0] + entity->maxs[0] + 1.0f,
	     entity->s.origin[1] + entity->maxs[1] + 1.0f,
	     entity->s.origin[2] + entity->maxs[2] + 1.0f);
	VectorSubtract(entity->maxs, entity->mins, entity->size);
}

static int SuffixX(void)
{
	int command = fixture_observation.suffix_commands;

	switch (fixture_config.suffix)
	{
	case FIXTURE_SUFFIX_SUCCESS:
		if (command <= 4)
			return 80;
		if (command < 8)
			return 0;
		if (command == 8)
			return -40;
		return command < 12 ? -60 : -80;
	case FIXTURE_SUFFIX_NO_SWEEP:
		return 160;
	case FIXTURE_SUFFIX_REENTRY:
		if (command <= 4)
			return 80;
		if (command < 8)
			return 0;
		if (command == 8)
			return -40;
		return 0;
	case FIXTURE_SUFFIX_ARRIVE_BEFORE_CLEAR:
		return command < 4 ? 100 : 80;
	case FIXTURE_SUFFIX_ALWAYS_OUTSIDE:
		if (command <= 4)
			return 200;
		return command < 8 ? 220 : 240;
	case FIXTURE_SUFFIX_BETWEEN_RECROSS:
		if (command <= 4)
			return 80;
		if (command < 8)
			return 0;
		if (command == 8)
			return -40;
		return 120;
	case FIXTURE_SUFFIX_PRECLEAR_CHORD:
		if (command == 0)
			return 80;
		if (command <= 3)
			return 120;
		return -80;
	case FIXTURE_SUFFIX_POSTCLEAR_CHORD:
		if (command <= 1)
			return 80;
		if (command <= 7)
			return 120;
		return -80;
	default:
		return 60;
	}
}

static void HostPmove(pmove_t *pmove)
{
	vec3_t start, mins, maxs, end;
	int x = 60;

	fixture_observation.pmove_calls++;
	if (fixture_observation.pmove_calls == 1)
		fixture_observation.first_snapinitial = pmove->snapinitial;
	else if (pmove->snapinitial)
		fixture_observation.later_snapinitial++;
	Set3(start, pmove->s.origin[0] * 0.125f,
	     pmove->s.origin[1] * 0.125f,
	     pmove->s.origin[2] * 0.125f);
	VectorCopy(start, end);
	Set3(mins, -16.0f, -16.0f, -24.0f);
	Set3(maxs, 16.0f, 16.0f, 32.0f);
	(void)pmove->trace(start, mins, maxs, end);

	pmove->groundentity = NULL;
	pmove->watertype = CONTENTS_WATER;
	pmove->waterlevel = 3;
	if (fixture_observation.top_staged)
	{
		if (!fixture_observation.first_top_seen)
		{
			fixture_observation.first_top_seen = true;
			fixture_observation.first_top_command = pmove->cmd;
		}
		fixture_observation.suffix_commands++;
		x = SuffixX();
		pmove->s.velocity[0] = 64;
		if (fixture_config.suffix_hazard)
		{
			pmove->watertype = CONTENTS_LAVA;
			pmove->waterlevel = 2;
		}
	}
	else if (!CommandZero(&pmove->cmd))
	{
		int contact_cycle;

		fixture_observation.approach_commands++;
		contact_cycle = fixture_config.touch_substep > 0 ?
			(fixture_observation.approach_commands /
			 fixture_config.touch_substep) : 0;
		if (fixture_config.touch_substep > 0 &&
		    (fixture_observation.approach_commands %
		     fixture_config.touch_substep) == 0)
			x = (int)fixture_config.mechanism_x -
			    ((fixture_config.wrong_contact ||
			      (fixture_config.unstable_contact &&
			       (contact_cycle % 2) == 0)) ? 8 : 0);
		else
			x = fixture_config.source_x < -24.0f ?
			    (int)fixture_config.mechanism_x : 180;
	}
	else
	{
		fixture_observation.zero_commands++;
		x = pmove->s.origin[0] / 8;
		if (fixture_observation.stage_started)
		{
			fixture_observation.ride_zero_commands++;
			if (fixture_observation.ride_zero_commands == 1 &&
			    fixture_config.opening_drift)
				x = 80;
			if (fixture_observation.ride_zero_commands == 4 &&
			    fixture_config.hazard_ride)
			{
				pmove->watertype = CONTENTS_LAVA;
				pmove->waterlevel = 2;
			}
			if (fixture_observation.ride_zero_commands == 4 &&
			    fixture_config.fall_ride)
			{
				pmove->s.velocity[2] = 0;
				pmove->watertype = 0;
				pmove->waterlevel = 0;
				pmove->groundentity = &fixture_edicts[0];
			}
		}
		else if (fixture_config.source_hazard)
		{
			pmove->watertype = CONTENTS_LAVA;
			pmove->waterlevel = 2;
		}
		else if (fixture_config.source_dry)
		{
			pmove->watertype = 0;
			pmove->waterlevel = 0;
		}
	}
	pmove->s.origin[0] = (short)(x * 8);
}

static void Door(edict_t *door)
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

static void Trigger(edict_t *trigger, edict_t *door, float mechanism_x)
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

static fixture_config_t DefaultConfig(int touch, fixture_suffix_t suffix)
{
	fixture_config_t config;

	memset(&config, 0, sizeof(config));
	config.touch_substep = touch;
	config.suffix = suffix;
	config.mechanism_x = 160.0f;
	config.source_x = 200.0f;
	return config;
}

static void ResetFixture(const fixture_config_t *config)
{
	memset(fixture_edicts, 0, sizeof(fixture_edicts));
	memset(fixture_clients, 0, sizeof(fixture_clients));
	memset(&fixture_gravity, 0, sizeof(fixture_gravity));
	memset(&fixture_observation, 0, sizeof(fixture_observation));
	memset(&level, 0, sizeof(level));
	fixture_config = *config;
	g_edicts = fixture_edicts;
	globals.num_edicts = 6;
	fixture_gravity.value = 777.0f;
	sv_gravity = &fixture_gravity;
	fixture_edicts[0].inuse = true;
	Door(&fixture_edicts[1]);
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

	memset(&sg_host, 0, sizeof(sg_host));
	sg_host.trace = HostTrace;
	sg_host.pointcontents = HostPointContents;
	sg_host.box_edicts = HostBoxEdicts;
	sg_host.pmove = HostPmove;
	sg_host.linkentity = HostLinkEntity;
}

static void InitPhantom(sg_phantom_t *phantom, qboolean damaging_fall)
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

static void SyncRecoveryPassent(const sg_phantom_t *phantom,
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

static edict_t *InitRecoveryState(sg_phantom_t *phantom,
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
	member->think = door_go_down;
	level.time = 10.0f;
	member->nextthink = 11.0f;
	HostLinkEntity(member);

	InitPhantom(phantom, false);
	fixture_observation.suffix_commands = suffix_commands;
	x = SuffixX();
	phantom->pms.origin[0] = (short)(x * 8);
	phantom->pms.velocity[0] = 64;
	phantom->origin[0] = (float)x;
	phantom->velocity[0] = 8.0f;
	phantom->old_pms = phantom->pms;
	phantom->old_pms.origin[0] += 8; /* valid live snapinitial mismatch */

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

static rune_reject_reason_t Resolve(
	sg_compound_world_preopen_t *resolved)
{
	vec3_t anchor;

	Set3(anchor, fixture_config.mechanism_x, 0.0f, 0.0f);
	return SG_CompoundWorldResolvePreopen(anchor, resolved);
}

static qboolean CanonicalHint(sg_compound_world_preopen_t *resolved,
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

static qboolean MemberRestored(const edict_t *member,
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
	       member->solid == before->solid &&
	       member->linkcount == before->linkcount;
}

static void CheckStaticContextRestored(void)
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
	/* The exact compound member must no longer be admitted by an ordinary
	 * world-only traversal after either success or failure. */
	CHECK(!SG_OracleSwimTraverse(&phantom, destination, true, 0.0f,
	                             &proof, NULL, true));
	CHECK(fixture_observation.pmove_calls == calls);
}

static void TestTouchSubsteps(void)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	int touch;

	for (touch = 1; touch <= 4; touch++)
	{
		fixture_config_t config =
			DefaultConfig(touch, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_swim_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		rune_reject_reason_t result;
		int expected_zero = (4 - touch) + 16;
		int link;

		ResetFixture(&config);
		CHECK(Resolve(&resolved) == RLR_OK);
		member_before = fixture_edicts[1];
		InitPhantom(&phantom, false);
		result = SG_OracleCompoundSwimPreopen(&phantom, &resolved,
			mechanism, destination, true, 0.0f, &proof, NULL, true,
			false);
		if (result != RLR_OK)
			fprintf(stderr, "touch %d result %d calls %d approach %d zero %d suffix %d links %d\n",
			        touch, result, fixture_observation.pmove_calls,
			        fixture_observation.approach_commands,
			        fixture_observation.zero_commands,
			        fixture_observation.suffix_commands,
			        fixture_observation.link_calls);
		CHECK(result == RLR_OK);
		CHECK(proof.touch_ms == touch * 25);
		CHECK(proof.touch_frame_end_ms == 100);
		CHECK(proof.mover_top_ms == 500);
		CHECK(proof.suffix_start_ms == 400);
		CHECK(proof.sweep_clear_ms == 200);
		CHECK(proof.arrival_ms == 300);
		CHECK(proof.total_cost_ms == 800);
		CHECK(proof.suffix_pms.origin[0] == 160 * 8);
		CHECK(proof.suffix_pms.gravity == 777);
		CHECK(proof.suffix_pms.delta_angles[YAW] == 321);
		CHECK(memcmp(&proof.suffix_pms, &proof.suffix_old_pms,
		             sizeof(proof.suffix_pms)) == 0);
		CHECK(proof.suffix_origin[0] == 160.0f);
		CHECK(proof.suffix_velocity[1] == 8.0f);
		CHECK(proof.suffix_watertype == CONTENTS_WATER);
		CHECK(proof.suffix_waterlevel == 3);
		CHECK(proof.suffix_old_frame_z == 0.0f);
		CHECK(fixture_observation.zero_commands == expected_zero);
		CHECK(fixture_observation.ride_zero_commands == 16);
		CHECK(fixture_observation.pmove_calls == 32);
		CHECK(proof.total_cost_ms ==
		      fixture_observation.pmove_calls * 25);
		CHECK(fixture_observation.suffix_commands == 12);
		CHECK(fixture_observation.first_snapinitial);
		CHECK(fixture_observation.later_snapinitial == 0);
		CHECK(fixture_observation.first_top_seen);
		CHECK(fixture_observation.pretop_contact_traces == 0);
		CHECK(fixture_observation.first_top_command.msec == 25);
		CHECK(!CommandZero(&fixture_observation.first_top_command));
		CHECK(fixture_observation.link_calls == 6);
		for (link = 0; link < 5; link++)
			CHECK(fixture_observation.link_origins[link] ==
			      (link == 0 ? 0.0f : (float)(link * 20)));
		CHECK(fixture_observation.link_origins[5] == 0.0f);
		CHECK(MemberRestored(&fixture_edicts[1], &member_before));
		CHECK(fixture_observation.callback_calls == 0);
		CheckStaticContextRestored();
	}
}

static void RunFailure(const fixture_config_t *config,
	rune_reject_reason_t expected, const vec3_t destination,
	qboolean damaging_fall)
{
	vec3_t mechanism;
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	byte zero[sizeof(proof)];

	memset(zero, 0, sizeof(zero));
	ResetFixture(config);
	Set3(mechanism, config->mechanism_x, 0.0f, 0.0f);
	CHECK(Resolve(&resolved) == RLR_OK);
	member_before = fixture_edicts[1];
	InitPhantom(&phantom, damaging_fall);
	CHECK(SG_OracleCompoundSwimPreopen(&phantom, &resolved, mechanism,
		destination, true, damaging_fall ? -1000.0f : 0.0f,
		&proof, NULL, true, false) == expected);
	CHECK(memcmp(&proof, zero, sizeof(proof)) == 0);
	CHECK(MemberRestored(&fixture_edicts[1], &member_before));
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static void TestFailureTable(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t inside_destination = { 60.0f, 0.0f, 0.0f };
	const vec3_t outside_destination = { 240.0f, 0.0f, 0.0f };
	fixture_config_t no_sweep =
		DefaultConfig(2, FIXTURE_SUFFIX_NO_SWEEP);
	fixture_config_t reentry =
		DefaultConfig(2, FIXTURE_SUFFIX_REENTRY);
	fixture_config_t arrival_before =
		DefaultConfig(2, FIXTURE_SUFFIX_ARRIVE_BEFORE_CLEAR);
	fixture_config_t always_outside =
		DefaultConfig(2, FIXTURE_SUFFIX_ALWAYS_OUTSIDE);
	fixture_config_t between_recross =
		DefaultConfig(2, FIXTURE_SUFFIX_BETWEEN_RECROSS);
	fixture_config_t trigger_contamination =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t fanout =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t hazard =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t fall =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t wrong_contact =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t inside_approach =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t crossing_approach =
		DefaultConfig(1, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t opening_drift =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);

	trigger_contamination.contaminate_trigger = true;
	fanout.contaminate_solid = true;
	hazard.hazard_ride = true;
	fall.fall_ride = true;
	wrong_contact.wrong_contact = true;
	inside_approach.source_x = 0.0f;
	crossing_approach.source_x = -80.0f;
	opening_drift.opening_drift = true;

	RunFailure(&no_sweep, RLR_SUFFIX_REPLAY_FAILED, destination, false);
	RunFailure(&reentry, RLR_CLEAR_MISMATCH, destination, false);
	RunFailure(&arrival_before, RLR_CLEAR_MISMATCH,
	           inside_destination, false);
	RunFailure(&always_outside, RLR_CLEAR_MISMATCH,
	           outside_destination, false);
	RunFailure(&between_recross, RLR_CLEAR_MISMATCH,
	           destination, false);
	RunFailure(&trigger_contamination, RLR_APPROACH_REPLAY_FAILED,
	           destination, false);
	RunFailure(&fanout, RLR_APPROACH_REPLAY_FAILED, destination, false);
	RunFailure(&hazard, RLR_RIDE_REPLAY_FAILED, destination, false);
	RunFailure(&fall, RLR_RIDE_REPLAY_FAILED, destination, true);
	RunFailure(&wrong_contact, RLR_APPROACH_REPLAY_FAILED,
	           destination, false);
	RunFailure(&inside_approach, RLR_APPROACH_REPLAY_FAILED,
	           destination, false);
	RunFailure(&crossing_approach, RLR_APPROACH_REPLAY_FAILED,
	           destination, false);
	RunFailure(&opening_drift, RLR_RIDE_REPLAY_FAILED,
	           destination, false);
}

static void TestPreopenSweepChordBoundaries(void)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t preclear =
		DefaultConfig(2, FIXTURE_SUFFIX_PRECLEAR_CHORD);
	fixture_config_t postclear =
		DefaultConfig(2, FIXTURE_SUFFIX_POSTCLEAR_CHORD);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;

	ResetFixture(&preclear);
	CHECK(Resolve(&resolved) == RLR_OK);
	member_before = fixture_edicts[1];
	InitPhantom(&phantom, false);
	CHECK(SG_OracleCompoundSwimPreopen(&phantom, &resolved, mechanism,
	      destination, true, 0.0f, &proof, NULL, true, false) == RLR_OK);
	CHECK(proof.sweep_clear_ms == 100);
	CHECK(proof.arrival_ms == 100);
	CHECK(proof.total_cost_ms == 600);
	CHECK(MemberRestored(&fixture_edicts[1], &member_before));
	CheckStaticContextRestored();

	RunFailure(&postclear, RLR_CLEAR_MISMATCH, destination, false);
}

static void TestResolvedIdentityFailsClosed(void)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t config =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_proof_t proof;
	sg_phantom_t phantom;

	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	resolved.mover_key = 4;
	InitPhantom(&phantom, false);
	CHECK(SG_OracleCompoundSwimPreopen(&phantom, &resolved, mechanism,
		destination, true, 0.0f, &proof, NULL, true, false) ==
	      RLR_MECHANISM_UNRESOLVED);
	CHECK(fixture_observation.pmove_calls == 0);
	CHECK(fixture_observation.link_calls == 0);
}

static void TestApproachArrivalSuppressedUntilTouch(void)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t delayed =
		DefaultConfig(5, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t near =
		DefaultConfig(1, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t configs[2];
	int expected_touch[2] = { 125, 25 };
	int index;

	near.source_x = 123.0f; /* inside arrival radius, outside thin trigger */
	configs[0] = delayed;
	configs[1] = near;
	for (index = 0; index < 2; index++)
	{
		sg_compound_world_preopen_t resolved;
		sg_compound_swim_proof_t proof;
		sg_phantom_t phantom;

		ResetFixture(&configs[index]);
		CHECK(Resolve(&resolved) == RLR_OK);
		InitPhantom(&phantom, false);
		CHECK(SG_OracleCompoundSwimPreopen(&phantom, &resolved,
			mechanism, destination, true, 0.0f, &proof, NULL, true,
			false) == RLR_OK);
		CHECK(proof.touch_ms == expected_touch[index]);
		CHECK(proof.touch_frame_end_ms == (index == 0 ? 200 : 100));
		CHECK(proof.total_cost_ms == (index == 0 ? 900 : 800));
		CHECK(proof.total_cost_ms ==
		      fixture_observation.pmove_calls * 25);
	}
}

static void TestPrepareSource(void)
{
	fixture_config_t config =
		DefaultConfig(5, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_source_t prepared;
	sg_compound_swim_source_t zero;
	vec3_t source;

	memset(&zero, 0, sizeof(zero));
	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	Set3(source, config.source_x, 0.0f, 0.0f);
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, -17.0f,
	      &prepared, NULL, true, false) == RLR_OK);
	CHECK(prepared.old_frame_z == -17.0f);
	CHECK(prepared.phantom.pms.origin[0] == (short)(source[0] * 8.0f));
	CHECK(prepared.phantom.old_pms.origin[0] ==
	      prepared.phantom.pms.origin[0]);
	CHECK(prepared.phantom.origin[0] == source[0]);
	CHECK(prepared.phantom.waterlevel == 3);
	CHECK(prepared.phantom.watertype == CONTENTS_WATER);
	CHECK(fixture_observation.pmove_calls == 1);
	CHECK(fixture_observation.zero_commands == 1);
	CHECK(fixture_observation.callback_calls == 0);

	config.source_hazard = true;
	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, 0.0f,
	      &prepared, NULL, true, false) == RLR_APPROACH_REPLAY_FAILED);
	CHECK(memcmp(&prepared, &zero, sizeof(prepared)) == 0);
	config.source_hazard = false;
	config.source_dry = true;
	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, 0.0f,
	      &prepared, NULL, true, false) == RLR_APPROACH_REPLAY_FAILED);
	CHECK(memcmp(&prepared, &zero, sizeof(prepared)) == 0);
}

static void RunContactDiscovery(float source_x, int touch_substep,
	int expected_commands)
{
	fixture_config_t config =
		DefaultConfig(touch_substep, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_source_t prepared;
	sg_compound_swim_source_t before;
	vec3_t source, hint, anchor;
	short original_pms_x;
	rune_reject_reason_t result;

	config.source_x = source_x;
	ResetFixture(&config);
	CHECK(CanonicalHint(&resolved, hint));
	Set3(source, source_x, 0.0f, 0.0f);
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, -17.0f,
	      &prepared, NULL, true, false) == RLR_OK);
	/* The discovery seam must not collapse old_pms into pms.  Give each cloned
	 * replay a real snapinitial mismatch and prove the caller's two states are
	 * left byte-for-byte intact. */
	original_pms_x = prepared.phantom.pms.origin[0];
	prepared.phantom.old_pms.origin[0] += 8;
	CHECK(prepared.phantom.pms.origin[0] == original_pms_x);
	before = prepared;
	memset(&fixture_observation, 0, sizeof(fixture_observation));
	result = SG_OracleCompoundSwimDiscoverContact(&prepared, &resolved, hint,
	      anchor, NULL, true, false);
	if (result != RLR_OK)
		fprintf(stderr, "discovery source %.1f touch %d result %d calls %d approach %d hint %.3f\n",
		        source_x, touch_substep, result,
		        fixture_observation.pmove_calls,
		        fixture_observation.approach_commands, hint[0]);
	CHECK(result == RLR_OK);
	CHECK(anchor[0] == config.mechanism_x);
	CHECK(anchor[1] == 0.0f && !signbit(anchor[1]));
	CHECK(anchor[2] == 0.0f && !signbit(anchor[2]));
	CHECK(anchor[0] * 8.0f == floorf(anchor[0] * 8.0f));
	CHECK(memcmp(&prepared, &before, sizeof(prepared)) == 0);
	CHECK(fixture_observation.approach_commands == expected_commands);
	CHECK(fixture_observation.pmove_calls == expected_commands);
	CHECK(fixture_observation.first_snapinitial);
	CHECK(fixture_observation.later_snapinitial == 1);
	CHECK(fixture_observation.pretop_contact_traces == 0);
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static void TestContactDiscovery(void)
{
	/* The first case crosses a normal 100 ms completion boundary before its
	 * first contact; the second starts inside the ordinary SWIM arrival radius.
	 * Suppress-arrival discovery must continue in both cases. */
	RunContactDiscovery(200.0f, 5, 10);
	RunContactDiscovery(123.0f, 1, 2);
}

static void TestContactDiscoveryRejectsNonFixedReplay(void)
{
	fixture_config_t config =
		DefaultConfig(5, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_source_t prepared;
	vec3_t source, hint, anchor, bad_hint;
	byte zero[sizeof(anchor)];

	memset(zero, 0, sizeof(zero));
	config.unstable_contact = true;
	ResetFixture(&config);
	CHECK(CanonicalHint(&resolved, hint));
	Set3(source, config.source_x, 0.0f, 0.0f);
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, 0.0f,
	      &prepared, NULL, true, false) == RLR_OK);
	memset(&fixture_observation, 0, sizeof(fixture_observation));
	CHECK(SG_OracleCompoundSwimDiscoverContact(&prepared, &resolved, hint,
	      anchor, NULL, true, false) == RLR_APPROACH_REPLAY_FAILED);
	CHECK(memcmp(anchor, zero, sizeof(anchor)) == 0);
	CHECK(fixture_observation.approach_commands == 10);

	config.unstable_contact = false;
	ResetFixture(&config);
	CHECK(CanonicalHint(&resolved, hint));
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, 0.0f,
	      &prepared, NULL, true, false) == RLR_OK);
	VectorCopy(hint, bad_hint);
	bad_hint[0] += 0.125f;
	memset(&fixture_observation, 0, sizeof(fixture_observation));
	CHECK(SG_OracleCompoundSwimDiscoverContact(&prepared, &resolved,
	      bad_hint, anchor, NULL, true, false) ==
	      RLR_BAD_MECHANISM_ANCHOR);
	CHECK(memcmp(anchor, zero, sizeof(anchor)) == 0);
	CHECK(fixture_observation.pmove_calls == 0);

	prepared.phantom.armed_door_count = 1;
	memset(&fixture_observation, 0, sizeof(fixture_observation));
	CHECK(SG_OracleCompoundSwimDiscoverContact(&prepared, &resolved, hint,
	      anchor, NULL, true, false) == RLR_APPROACH_REPLAY_FAILED);
	CHECK(memcmp(anchor, zero, sizeof(anchor)) == 0);
	CHECK(fixture_observation.pmove_calls == 0);
}

static void TestLoaderReplayModeAndRestoration(void)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t config =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_proof_t proof;
	sg_phantom_t phantom;
	usercmd_t command;

	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	InitPhantom(&phantom, false);
	CHECK(SG_OracleCompoundSwimPreopen(&phantom, &resolved, mechanism,
		destination, true, 0.0f, &proof, NULL, true, true) == RLR_OK);
	CHECK(fixture_observation.loader_pmove_masks > 0);
	CHECK(fixture_observation.normal_pmove_masks == 0);

	/* A direct command after the scoped proof observes the original ordinary
	 * mask, proving loader mode did not leak out of the transaction. */
	fixture_observation.last_pmove_mask = 0;
	InitPhantom(&phantom, false);
	memset(&command, 0, sizeof(command));
	command.msec = 25;
	SG_OracleRun(&phantom, &command, 1);
	CHECK(fixture_observation.last_pmove_mask == MASK_PLAYERSOLID);
}

static void TestPublicSwimTraverseRegression(void)
{
	const vec3_t destination = { 160.0f, 0.0f, 0.0f };
	fixture_config_t config =
		DefaultConfig(99, FIXTURE_SUFFIX_SUCCESS);
	sg_swim_proof_t proof;
	sg_phantom_t phantom;

	ResetFixture(&config);
	InitPhantom(&phantom, false);
	CHECK(SG_OracleSwimTraverse(&phantom, destination, true, 0.0f,
	                            &proof, NULL, false));
	CHECK(proof.arrival_ms == 100);
	CHECK(fixture_observation.pmove_calls == 4);
	CHECK(fixture_observation.first_snapinitial);
	CHECK(fixture_observation.later_snapinitial == 0);
}

static qboolean RecoveryProofZero(
	const sg_compound_swim_recovery_proof_t *proof)
{
	sg_compound_swim_recovery_proof_t zero;

	memset(&zero, 0, sizeof(zero));
	return memcmp(proof, &zero, sizeof(zero)) == 0;
}

static void TestRecoveryFromLiveTop(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	static const int suffix_steps[] = { 1, 4, 5 };
	static const int expected_clear[] = { 200, 100, 100 };
	static const int expected_arrival[] = { 200, 200, 100 };
	int index;

	for (index = 0; index < 3; index++)
	{
		fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_swim_recovery_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		edict_t *passent;

		ResetFixture(&config);
		CHECK(Resolve(&resolved) == RLR_OK);
		passent = InitRecoveryState(&phantom, &resolved,
		                            suffix_steps[index]);
		member_before = fixture_edicts[1];
		CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
		      true, 0.0f, &proof, passent) == RLR_OK);
		CHECK(proof.sweep_clear_ms == expected_clear[index]);
		CHECK(proof.arrival_ms == expected_arrival[index]);
		CHECK(proof.sweep_clear_ms <= proof.arrival_ms);
		CHECK(fixture_observation.first_snapinitial);
		CHECK(fixture_observation.normal_pmove_masks > 0);
		CHECK(fixture_observation.loader_pmove_masks == 0);
		CHECK(memcmp(&fixture_edicts[1], &member_before,
		             sizeof(member_before)) == 0);
		CHECK(fixture_observation.callback_calls == 0);
		CheckStaticContextRestored();
	}
}

static void RunRecoveryFailure(const fixture_config_t *config,
	int suffix_commands, const vec3_t destination,
	rune_reject_reason_t expected)
{
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_recovery_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	edict_t *passent;
	rune_reject_reason_t result;

	ResetFixture(config);
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, suffix_commands);
	member_before = fixture_edicts[1];
	memset(&proof, 0xa5, sizeof(proof));
	result = SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
	                                    true, 0.0f, &proof, passent);
	if (result != expected)
		fprintf(stderr, "recovery failure suffix=%d mode=%d got=%d want=%d\n",
		        suffix_commands, (int)config->suffix, result, expected);
	CHECK(result == expected);
	CHECK(RecoveryProofZero(&proof));
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static void TestRecoveryTrajectoryFailures(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t inside_destination = { 0.0f, 0.0f, 0.0f };
	fixture_config_t foreign_trigger =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t foreign_solid =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t hazard = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t arrival_before =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t reentry = DefaultConfig(2, FIXTURE_SUFFIX_REENTRY);
	fixture_config_t no_clear =
		DefaultConfig(2, FIXTURE_SUFFIX_ARRIVE_BEFORE_CLEAR);
	fixture_config_t outside = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_recovery_proof_t proof;
	sg_phantom_t phantom;
	edict_t *passent;

	foreign_trigger.force_foreign_trigger = true;
	foreign_solid.contaminate_solid = true;
	hazard.suffix_hazard = true;
	RunRecoveryFailure(&foreign_trigger, 4, destination,
	                   RLR_SUFFIX_REPLAY_FAILED);
	RunRecoveryFailure(&foreign_solid, 4, destination,
	                   RLR_SUFFIX_REPLAY_FAILED);
	RunRecoveryFailure(&hazard, 4, destination, RLR_SUFFIX_REPLAY_FAILED);
	RunRecoveryFailure(&arrival_before, 1, inside_destination,
	                   RLR_CLEAR_MISMATCH);
	RunRecoveryFailure(&reentry, 4, destination, RLR_CLEAR_MISMATCH);
	RunRecoveryFailure(&no_clear, 1, destination, RLR_SUFFIX_REPLAY_FAILED);

	ResetFixture(&outside);
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 4);
	phantom.pms.origin[0] = 160 * 8;
	phantom.origin[0] = 160.0f;
	phantom.old_pms = phantom.pms;
	phantom.old_pms.origin[0] += 8;
	SyncRecoveryPassent(&phantom, passent);
	memset(&proof, 0xa5, sizeof(proof));
	CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
	      true, 0.0f, &proof, passent) == RLR_SUFFIX_REPLAY_FAILED);
	CHECK(RecoveryProofZero(&proof));
	CHECK(fixture_observation.pmove_calls == 0);
	CheckStaticContextRestored();
}

static void TestRecoverySweepChordBoundaries(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t preclear =
		DefaultConfig(2, FIXTURE_SUFFIX_PRECLEAR_CHORD);
	fixture_config_t postclear =
		DefaultConfig(2, FIXTURE_SUFFIX_POSTCLEAR_CHORD);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_recovery_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	edict_t *passent;

	/* The 25, 50, and 75 ms endpoints are outside.  The 75..100 ms
	 * outside-to-outside chord contacts the complete sweep, so 100 ms is
	 * the conservative last-contact boundary and is valid clear. */
	ResetFixture(&preclear);
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 0);
	member_before = fixture_edicts[1];
	CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
	      true, 0.0f, &proof, passent) == RLR_OK);
	CHECK(proof.sweep_clear_ms == 100);
	CHECK(proof.arrival_ms == 100);
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CheckStaticContextRestored();

	/* The same outside-to-outside chord is forbidden after clear. */
	RunRecoveryFailure(&postclear, 0, destination, RLR_CLEAR_MISMATCH);
}

static void TestRecoveryRejectsUnauthenticatedLiveState(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	int mutation;

	for (mutation = 0; mutation < 30; mutation++)
	{
		fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_swim_recovery_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		edict_t *passent;
		edict_t *argument;
		float old_frame_z = 0.0f;

		ResetFixture(&config);
		CHECK(Resolve(&resolved) == RLR_OK);
		passent = InitRecoveryState(&phantom, &resolved, 4);
		argument = passent;
		switch (mutation)
		{
		case 0: argument = NULL; break;
		case 1: passent->inuse = false; break;
		case 2: passent->health = 0; break;
		case 3: passent->deadflag = 1; break;
		case 4: passent->movetype = MOVETYPE_TOSS; break;
		case 5: passent->s.modelindex = 0; break;
		case 6: passent->client->chase_target = &fixture_edicts[0]; break;
		case 7: passent->client->hookstate = 1; break;
		case 8: passent->client->hook = &fixture_edicts[4]; break;
		case 9: passent->client->ps.pmove.pm_type = PM_DEAD; break;
		case 10: fixture_gravity.value = 778.0f; break;
		case 11: passent->s.origin[0] += 0.125f; break;
		case 12: passent->velocity[0] += 0.125f; break;
		case 13: passent->client->old_pmove.origin[0]++; break;
		case 14: phantom.origin[0] += 0.125f; break;
		case 15: phantom.velocity[0] += 0.125f; break;
		case 16: passent->groundentity = &fixture_edicts[0]; break;
		case 17: passent->watertype = CONTENTS_LAVA; break;
		case 18: old_frame_z = 1.0f; break;
		case 19: sv_gravity = NULL; break;
		case 20: passent->s.origin[0] = NAN; break;
		case 21: passent->velocity[0] = INFINITY; break;
		case 22: passent->s.origin[0] = 4096.0f; break;
		case 23: fixture_gravity.value = NAN; break;
		case 24: passent->waterlevel = 2; break;
		case 25: phantom.armed_door_count = 1; break;
		case 26: passent->s.origin[1] = 0.124f; break;
		case 27: passent->s.origin[1] = -0.124f; break;
		case 28: passent->velocity[2] = 0.124f; break;
		case 29: passent->velocity[2] = -0.124f; break;
		default: break;
		}
		member_before = fixture_edicts[1];
		memset(&proof, 0xa5, sizeof(proof));
		CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
		      true, old_frame_z, &proof, argument) == RLR_BAD_CONTROL_POLICY);
		CHECK(RecoveryProofZero(&proof));
		CHECK(fixture_observation.pmove_calls == 0);
		CHECK(memcmp(&fixture_edicts[1], &member_before,
		             sizeof(member_before)) == 0);
		CheckStaticContextRestored();
	}
}

static void TestRecoveryRejectsTopAuthorityDrift(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	int mutation;

	for (mutation = 0; mutation < 8; mutation++)
	{
		fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_swim_recovery_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		edict_t *passent;

		ResetFixture(&config);
		CHECK(Resolve(&resolved) == RLR_OK);
		passent = InitRecoveryState(&phantom, &resolved, 4);
		switch (mutation)
		{
		case 0: resolved.mover_key = 4; break;
		case 1: fixture_edicts[1].moveinfo.state = SG_PLAT_STATE_BOTTOM; break;
		case 2: fixture_edicts[1].s.origin[0] += 0.125f; break;
		case 3: fixture_edicts[1].velocity[0] = 1.0f; break;
		case 4: fixture_edicts[1].think = NULL; break;
		case 5:
			fixture_edicts[1].nextthink = level.time + FRAMETIME;
			fixture_edicts[1].nextthink += 0.001f;
			break;
		case 6: resolved.trigger = &fixture_edicts[3]; break;
		case 7: resolved.member = &fixture_edicts[4]; break;
		default: break;
		}
		member_before = fixture_edicts[1];
		memset(&proof, 0xa5, sizeof(proof));
		CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
		      true, 0.0f, &proof, passent) == RLR_MECHANISM_UNRESOLVED);
		CHECK(RecoveryProofZero(&proof));
		CHECK(fixture_observation.pmove_calls == 0);
		CHECK(memcmp(&fixture_edicts[1], &member_before,
		             sizeof(member_before)) == 0);
		CheckStaticContextRestored();
	}
}

int main(void)
{
	TestTouchSubsteps();
	TestFailureTable();
	TestPreopenSweepChordBoundaries();
	TestResolvedIdentityFailsClosed();
	TestApproachArrivalSuppressedUntilTouch();
	TestPrepareSource();
	TestContactDiscovery();
	TestContactDiscoveryRejectsNonFixedReplay();
	TestLoaderReplayModeAndRestoration();
	TestPublicSwimTraverseRegression();
	TestRecoveryFromLiveTop();
	TestRecoveryTrajectoryFailures();
	TestRecoverySweepChordBoundaries();
	TestRecoveryRejectsUnauthenticatedLiveState();
	TestRecoveryRejectsTopAuthorityDrift();
	if (failures)
	{
		fprintf(stderr, "sg_compound_swim_oracle_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_swim_oracle_test: ok");
	return 0;
}
