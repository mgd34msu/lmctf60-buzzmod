/* Focused game-boundary contract for retained subjects versus full movers. */
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_compound_world.h"
#include "slipgate/sg_oracle_internal.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_rune_mechanism_plan.h"
#include "slipgate/sg_replay.h"
#include "slipgate/sg_timed_vault_egress.h"
#include "slipgate/sg_util.h"

#define TEST_EDICTS 16
#define MOVER_INDEX 11
#define HOOK_INDEX 12
#define BUTTON_INDEX 5
#define DOOR_INDEX 6
#define CELLAR_WITNESS_WATERTYPE 0x18000020

game_export_t globals;
game_locals_t game;
level_locals_t level;
edict_t *g_edicts;
sg_host_t sg_host;

qboolean SG_MoverSubjectSweepRealImmutableSupport(edict_t *ent);
void Pmove(pmove_t *pmove);

static edict_t ents[TEST_EDICTS];
static gclient_t clients[2];
static int failures;
static cvar_t replay_gravity;
cvar_t *sv_gravity;
static edict_t *contact_button;
static int contact_axis;
static int binding_current;
static const sg_rune_mechanism_binding_t *sibling_binding;
static uint32_t sibling_mover_keys[SG_RUNE_BINDING_MAX_MOVERS];
static size_t sibling_mover_count;
static edict_t *sibling_trigger_a;
static edict_t *sibling_trigger_b;
static edict_t *sibling_trigger_hits[4];
static int sibling_trigger_hit_count;
static float sibling_pmove_x;
static qboolean sibling_pmove_preserve_zero;
static qboolean sibling_pmove_airborne;
static int sibling_pmove_waterlevel;
static int sibling_pmove_watertype;
static qboolean ground_test_active;
static qboolean ground_test_blocked;
static qboolean ground_test_drift;
static qboolean ground_test_classification_drift;
static float ground_test_boundary;
static int ground_test_snap_axis;
static int ground_test_waterlevel;
static int ground_test_watertype;
static qboolean carry_test_blocked;
static qboolean carry_test_passent_ok;
static qboolean carry_test_coords_ok;
static int carry_test_mask;
static int contact_test_mask;
static edict_t *immutable_test_support;
static trace_t population_trace_result;
static int population_trace_calls;
static int population_trace_mask;
static int population_direct_index;
static int population_owned_index;
static int population_foreign_index;
static int population_immutable_index;
static solid_t population_direct_seen;
static solid_t population_owned_seen;
static solid_t population_foreign_seen;
static solid_t population_immutable_seen;
static qboolean population_reenter;
static qboolean population_reenter_independent;
static qboolean population_nested_result;
static qboolean population_mutate_identity;
static qboolean population_mutate_owner;
static qboolean population_mutate_area;
static qboolean population_mutate_base;
static vec3_t cellar_wade_start;
static vec3_t cellar_wade_target;
static int cellar_wade_step;
static int cellar_wade_steps;
static int cellar_wade_water_step;
static int cellar_wade_initial_water;
static int cellar_wade_mode;
static int timed_vault_discover;
static sg_mech_catalog_view_t timed_vault_catalog;
static sg_timed_vault_plan_witness_t timed_vault_witness;
static uint32_t timed_vault_execution_fail_key;
static uint32_t timed_vault_execution_calls;
static csurface_t vault_floor_surface;
static float vault_floor_z;
static float vault_shore_y;
static float vault_far_shore_y;
static qboolean vault_far_shore_enabled;
static qboolean vault_side_corridor_enabled;

enum cellar_wade_mode_e
{
	CELLAR_WADE_SAFE = 0,
	CELLAR_WADE_WATER2,
	CELLAR_WADE_LAVA,
	CELLAR_WADE_SLIME
};

void Com_DPrintf(const char *format, ...)
{
	(void)format;
}

static trace_t VaultFloorTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end)
{
	trace_t trace;
	float start_bottom = start[2] + mins[2];
	float end_bottom = end[2] + mins[2];

	(void)maxs;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	if (start_bottom < vault_floor_z)
	{
		trace.startsolid = true;
		trace.allsolid = end_bottom < vault_floor_z;
		trace.fraction = 0.0f;
		VectorCopy(start, trace.endpos);
	}
	else if (end_bottom < vault_floor_z)
	{
		float distance = start_bottom - end_bottom;
		float fraction = (start_bottom - vault_floor_z - 0.03125f) /
		    distance;

		if (fraction < 0.0f)
			fraction = 0.0f;
		if (fraction > 1.0f)
			fraction = 1.0f;
		trace.fraction = fraction;
		trace.endpos[0] = start[0] + fraction * (end[0] - start[0]);
		trace.endpos[1] = start[1] + fraction * (end[1] - start[1]);
		trace.endpos[2] = start[2] + fraction * (end[2] - start[2]);
	}
	if (trace.fraction < 1.0f)
	{
		trace.ent = &ents[0];
		trace.contents = CONTENTS_SOLID;
		trace.surface = &vault_floor_surface;
		trace.plane.normal[2] = 1.0f;
		trace.plane.dist = vault_floor_z;
	}
	return trace;
}

static int AllWaterPointContents(vec3_t point)
{
	(void)point;
	return CONTENTS_WATER;
}

static int VaultShorePointContents(vec3_t point)
{
	return point[1] < vault_shore_y &&
	        (!vault_far_shore_enabled || point[1] > vault_far_shore_y) &&
	        (!vault_side_corridor_enabled || fabsf(point[0]) < 128.0f)
	    ? CONTENTS_WATER : 0;
}

static qboolean VaultSourceReachable(const vec3_t origin,
	const vec3_t source, void *context)
{
	(void)origin;
	(void)source;
	(void)context;
	return true;
}

short SG_RuneProofGravity(void)
{
	return 800;
}

qboolean SG_ImmutableSupport(edict_t *ent)
{
	return ent == &ents[0] || ent == immutable_test_support;
}

void Touch_Item(edict_t *ent, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)ent;
	(void)other;
	(void)plane;
	(void)surf;
}

static int GroundPointContents(const vec3_t point)
{
	(void)point;
	return ground_test_watertype;
}

static trace_t StableGroundTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t trace;

	(void)maxs;
	(void)passent;
	(void)contentmask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	trace.ent = &ents[0];
	if (!ground_test_active || !mins)
		return trace;
	if (ground_test_blocked ||
	    (start[0] == end[0] && start[1] == end[1] &&
	     start[2] == end[2] &&
	     start[ground_test_snap_axis] <= ground_test_boundary))
	{
		trace.startsolid = true;
		trace.allsolid = true;
		trace.fraction = 0.0f;
	}
	return trace;
}

static void StableGroundPmove(pmove_t *pmove)
{
	vec3_t position, mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	trace_t trace;
	int axis;

	for (axis = 0; axis < 3; axis++)
		position[axis] = pmove->s.origin[axis] * 0.125f;
	if (pmove->snapinitial)
	{
		trace = pmove->trace(position, mins, maxs, position);
		if (trace.allsolid)
		{
			pmove->s.origin[ground_test_snap_axis]++;
			position[ground_test_snap_axis] =
				pmove->s.origin[ground_test_snap_axis] * 0.125f;
			trace = pmove->trace(position, mins, maxs, position);
			if (trace.allsolid)
				pmove->s.origin[ground_test_snap_axis]--;
		}
	}
	if (pmove->cmd.msec != 0 && ground_test_drift)
		pmove->s.origin[0]++;
	pmove->s.velocity[0] = pmove->s.velocity[1] =
		pmove->s.velocity[2] = 0;
	pmove->s.pm_flags |= PMF_ON_GROUND;
	pmove->groundentity = &ents[0];
	pmove->waterlevel = ground_test_waterlevel;
	pmove->watertype = ground_test_watertype;
	if (pmove->cmd.msec != 0 && ground_test_classification_drift)
		pmove->waterlevel = ground_test_waterlevel == 2 ? 1 : 2;
}

edict_t *G_Find(edict_t *from, int fieldofs, char *match)
{
	int index = from ? (int)(from - g_edicts) + 1 : 0;

	for (; index < globals.num_edicts; index++)
	{
		char *value;

		if (!g_edicts[index].inuse)
			continue;
		value = *(char **)((byte *)&g_edicts[index] + fieldofs);
		if (value && !Q_stricmp(value, match))
			return &g_edicts[index];
	}
	return NULL;
}

static trace_t ButtonContactTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t trace;
	float boundary;

	(void)mins;
	(void)passent;
	contact_test_mask = contentmask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	if (!contact_button)
		return trace;
	if (contact_axis == 4)
	{
		trace.fraction = 0.0f;
		trace.ent = contact_button;
	}
	else if (contact_axis == 3)
	{
		if (fabsf(start[0] + 32.125f) < 0.01f &&
		    fabsf(start[1] - 495.875f) < 0.01f &&
		    fabsf(start[2] - 44.125f) < 0.01f)
		{
			trace.fraction = 0.5f;
			trace.ent = contact_button;
		}
	}
	else if (contact_axis == 2)
	{
		boundary = contact_button->absmax[2] - mins[2];
		if (start[2] <= boundary)
		{
			trace.startsolid = true;
			trace.fraction = 0.0f;
			trace.ent = contact_button;
		}
		else if (end[2] <= boundary)
		{
			trace.fraction = (start[2] - boundary) /
			                 (start[2] - end[2]);
			trace.ent = contact_button;
		}
	}
	else
	{
		boundary = contact_button->absmin[0] - maxs[0];
		if (start[0] >= boundary)
		{
			trace.startsolid = true;
			trace.fraction = 0.0f;
			trace.ent = contact_button;
		}
		else if (end[0] >= boundary)
		{
			trace.fraction = (boundary - start[0]) /
			                 (end[0] - start[0]);
			trace.ent = contact_button;
		}
	}
	if (trace.fraction < 1.0f)
	{
		int axis;

		for (axis = 0; axis < 3; axis++)
			trace.endpos[axis] = start[axis] +
			    trace.fraction * (end[axis] - start[axis]);
	}
	return trace;
}

static trace_t ButtonCarryTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t trace;

	(void)mins;
	(void)maxs;
	carry_test_mask = contentmask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = carry_test_blocked ? 0.5f : 1.0f;
	VectorCopy(end, trace.endpos);
	trace.ent = &ents[0];
	if (carry_test_blocked)
		trace.ent = &ents[DOOR_INDEX + 1];
	carry_test_passent_ok = passent == contact_button;
	carry_test_coords_ok = start[2] == 44.125f && end[2] == 42.125f;
	return trace;
}

static trace_t PopulationNativeTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t nested;

	population_trace_calls++;
	population_trace_mask = contentmask;
	population_direct_seen = population_direct_index > 0
	    ? ents[population_direct_index].solid : SOLID_NOT;
	population_owned_seen = population_owned_index > 0
	    ? ents[population_owned_index].solid : SOLID_NOT;
	population_foreign_seen = population_foreign_index > 0
	    ? ents[population_foreign_index].solid : SOLID_NOT;
	population_immutable_seen = population_immutable_index > 0
	    ? ents[population_immutable_index].solid : SOLID_NOT;
	if (population_reenter)
	{
		population_reenter = false;
		population_nested_result = SG_OracleStablePopulationTrace(start,
		    mins, maxs, end, passent, population_reenter_independent,
		    &nested);
	}
	if (population_mutate_identity && population_direct_index > 0)
		ents[population_direct_index].s.number++;
	if (population_mutate_owner && population_direct_index > 0)
		ents[population_direct_index].owner = &ents[DOOR_INDEX + 1];
	if (population_mutate_area && population_direct_index > 0)
		ents[population_direct_index].area.prev = NULL;
	if (population_mutate_base)
		g_edicts = &ents[1];
	return population_trace_result;
}

void door_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self;
	(void)other;
	(void)activator;
}

void door_secret_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self;
	(void)other;
	(void)activator;
}

void door_blocked(edict_t *self, edict_t *other)
{
	(void)self;
	(void)other;
}

void button_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self;
	(void)other;
	(void)plane;
	(void)surf;
}

void button_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self;
	(void)other;
	(void)activator;
}

void Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self; (void)other; (void)plane; (void)surf;
}

void Touch_Multi(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self; (void)other; (void)plane; (void)surf;
}

void Use_Target_Speaker(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
}

void trigger_relay_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
}

int SG_RuneMechanismBindingCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	(void)binding;
	return binding_current;
}

int SG_RuneMechanismBindingTopologyCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	(void)binding;
	return binding_current;
}

int SG_MechCatalogButtonEndpoints(uint32_t key,
	const rune_mechanism_node_t *node, const edict_t *entity,
	sg_mech_button_endpoints_t *endpoints_out)
{
	int axis;

	(void)node;
	if (!endpoints_out || key != BUTTON_INDEX ||
	    entity != &ents[BUTTON_INDEX])
		return 0;
	memset(endpoints_out, 0, sizeof(*endpoints_out));
	for (axis = 0; axis < 3; axis++)
	{
		float start = entity->moveinfo.start_origin[axis] * 8.0f;
		float end = entity->moveinfo.end_origin[axis] * 8.0f;

		if (start != (float)(short)start || end != (float)(short)end)
			return 0;
		endpoints_out->start_q8[axis] = (short)start;
		endpoints_out->end_q8[axis] = (short)end;
	}
	return 1;
}

int SG_MechCatalogButtonBottomEndpoints(uint32_t key,
	const rune_mechanism_node_t *node, const edict_t *entity,
	sg_mech_button_endpoints_t *endpoints_out)
{
	return entity && entity->moveinfo.state == SG_PLAT_STATE_BOTTOM &&
	       SG_MechCatalogButtonEndpoints(key, node, entity, endpoints_out);
}

sg_mech_catalog_status_t SG_MechCatalogSnapshot(
	sg_mech_catalog_view_t *view_out)
{
	if (!timed_vault_discover || !view_out)
		return SG_MECH_CATALOG_NOT_READY;
	*view_out = timed_vault_catalog;
	return SG_MECH_CATALOG_READY;
}

int SG_TimedVaultPlanDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, sg_timed_vault_plan_witness_t *witness_out)
{
	if (!timed_vault_discover || !catalog || !witness_out ||
	    entry_key != timed_vault_witness.entry_key)
		return 0;
	*witness_out = timed_vault_witness;
	return 1;
}

int SG_MechCatalogEntityExecutionMatches(uint32_t key,
	const rune_mechanism_node_t *node, uint16_t controller_kind)
{
	if (!node || node->key != key ||
	    controller_kind != SG_MECHANISM_CONTROLLER_TIMED_VAULT)
		return 0;
	timed_vault_execution_calls++;
	return key != timed_vault_execution_fail_key;
}

edict_t *SG_RuneMechanismBindingResolveNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key)
{
	if (binding != sibling_binding || key >= TEST_EDICTS)
		return NULL;
	return &ents[key];
}

edict_t *SG_RuneMechanismBindingResolveTopologyNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key)
{
	return SG_RuneMechanismBindingResolveNode(binding, key);
}

int SG_RuneMechanismBindingMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out)
{
	if (binding != sibling_binding || !keys_out || !key_count_out ||
	    sibling_mover_count == 0U)
		return 0;
	memcpy(keys_out, sibling_mover_keys,
	       sibling_mover_count * sizeof(sibling_mover_keys[0]));
	*key_count_out = sibling_mover_count;
	return 1;
}

int SG_RuneMechanismBindingTopologyMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out)
{
	return SG_RuneMechanismBindingMoverKeys(binding, keys_out, key_count_out);
}

int SG_MoverCompletionMatches(const edict_t *member,
	sg_mover_completion_kind_t kind)
{
	return member == &ents[MOVER_INDEX] &&
	       kind == SG_MOVER_COMPLETION_TOP;
}

void Move_Begin(edict_t *self) { (void)self; }
void Move_Final(edict_t *self) { (void)self; }
void Move_Done(edict_t *self) { (void)self; }
void AngleMove_Begin(edict_t *self) { (void)self; }
void AngleMove_Final(edict_t *self) { (void)self; }
void AngleMove_Done(edict_t *self) { (void)self; }
void Think_CalcMoveSpeed(edict_t *self) { (void)self; }
void Think_SpawnDoorTrigger(edict_t *self) { (void)self; }
void door_go_down(edict_t *self) { (void)self; }
void door_hit_top(edict_t *self) { (void)self; }
void door_hit_bottom(edict_t *self) { (void)self; }

void hook_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self;
	(void)other;
	(void)plane;
	(void)surf;
}

void hook_die(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point)
{
	(void)self;
	(void)inflictor;
	(void)attacker;
	(void)damage;
	(void)point;
}

static void TestPrethink(edict_t *self)
{
	(void)self;
}

static void TestThink(edict_t *self)
{
	(void)self;
}

static void TestBlocked(edict_t *self, edict_t *other)
{
	(void)self;
	(void)other;
}

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

static void SetLinkedBounds(edict_t *ent)
{
	float radius = 0.0f;
	qboolean rotated_bsp;
	int axis;

	rotated_bsp = ent->solid == SOLID_BSP &&
	              (ent->s.angles[0] != 0.0f || ent->s.angles[1] != 0.0f ||
	               ent->s.angles[2] != 0.0f);
	if (rotated_bsp)
	{
		for (axis = 0; axis < 3; axis++)
		{
			float lo = fabsf(ent->mins[axis]);
			float hi = fabsf(ent->maxs[axis]);

			if (lo > radius) radius = lo;
			if (hi > radius) radius = hi;
		}
	}
	for (axis = 0; axis < 3; axis++)
	{
		ent->size[axis] = ent->maxs[axis] - ent->mins[axis];
		ent->absmin[axis] = ent->s.origin[axis] +
		                    (rotated_bsp ? -radius : ent->mins[axis]) - 1.0f;
		ent->absmax[axis] = ent->s.origin[axis] +
		                    (rotated_bsp ? radius : ent->maxs[axis]) + 1.0f;
	}
	ent->area.prev = &ents[0].area;
	ent->area.next = &ents[0].area;
}

static void LiveEdict(edict_t *ent, int number, const char *classname)
{
	memset(ent, 0, sizeof(*ent));
	ent->s.number = number;
	ent->inuse = true;
	ent->linkcount = 1;
	ent->classname = (char *)classname;
}

static void ResetWorld(void)
{
	memset(ents, 0, sizeof(ents));
	memset(clients, 0, sizeof(clients));
	memset(&globals, 0, sizeof(globals));
	memset(&game, 0, sizeof(game));
	immutable_test_support = NULL;
	memset(&population_trace_result, 0, sizeof(population_trace_result));
	population_trace_result.fraction = 1.0f;
	population_trace_calls = 0;
	population_trace_mask = 0;
	population_direct_index = 0;
	population_owned_index = 0;
	population_foreign_index = 0;
	population_immutable_index = 0;
	population_direct_seen = SOLID_NOT;
	population_owned_seen = SOLID_NOT;
	population_foreign_seen = SOLID_NOT;
	population_immutable_seen = SOLID_NOT;
	population_reenter = false;
	population_reenter_independent = true;
	population_nested_result = false;
	population_mutate_identity = false;
	population_mutate_owner = false;
	population_mutate_area = false;
	population_mutate_base = false;
	binding_current = 0;
	sibling_binding = NULL;
	sibling_mover_count = 0U;
	sibling_trigger_a = NULL;
	sibling_trigger_b = NULL;
	sibling_trigger_hit_count = 0;
	sibling_pmove_x = 0.0f;
	sibling_pmove_preserve_zero = false;
	sibling_pmove_airborne = false;
	sv_gravity = NULL;
	g_edicts = ents;
	game.maxentities = TEST_EDICTS;
	game.maxclients = 2;
	game.clients = clients;
	globals.edicts = ents;
	globals.edict_size = sizeof(edict_t);
	globals.num_edicts = HOOK_INDEX + 1;
	globals.max_edicts = TEST_EDICTS;
	LiveEdict(&ents[0], 0, "worldspawn");
}

static int SiblingBoxEdicts(const vec3_t mins, const vec3_t maxs,
	edict_t **list, int maxcount, int areatype)
{
	edict_t *triggers[2] = { sibling_trigger_a, sibling_trigger_b };
	int count = 0;
	int index;

	if (areatype != AREA_TRIGGERS)
		return 0;
	for (index = 0; index < 2; index++)
	{
		edict_t *trigger = triggers[index];

		if (!trigger || maxs[0] <= trigger->absmin[0] ||
		    mins[0] >= trigger->absmax[0] ||
		    maxs[1] <= trigger->absmin[1] ||
		    mins[1] >= trigger->absmax[1] ||
		    maxs[2] <= trigger->absmin[2] ||
		    mins[2] >= trigger->absmax[2])
			continue;
		if (count < maxcount)
			list[count] = trigger;
		if (sibling_trigger_hit_count <
		    (int)(sizeof(sibling_trigger_hits) /
		          sizeof(sibling_trigger_hits[0])))
			sibling_trigger_hits[sibling_trigger_hit_count++] = trigger;
		count++;
	}
	return count;
}

static void SiblingEgressPmove(pmove_t *pmove)
{
	pmove->s.pm_type = PM_NORMAL;
	if (!sibling_pmove_preserve_zero || pmove->cmd.msec != 0)
		pmove->s.origin[0] = (short)(sibling_pmove_x * 8.0f);
	pmove->s.velocity[0] = 0;
	pmove->s.velocity[1] = 0;
	pmove->s.velocity[2] = 0;
	pmove->groundentity = sibling_pmove_airborne && pmove->cmd.msec != 0
	    ? NULL : &ents[0];
	pmove->waterlevel = sibling_pmove_waterlevel;
	pmove->watertype = sibling_pmove_watertype;
}

static void CellarWadePmove(pmove_t *pmove)
{
	float fraction;
	float horizontal_distance;
	float travel_fraction;
	int axis;
	int wet;

	if (pmove->cmd.msec != 0 && cellar_wade_step < cellar_wade_steps)
		cellar_wade_step++;
	horizontal_distance = hypotf(cellar_wade_target[0] - cellar_wade_start[0],
	    cellar_wade_target[1] - cellar_wade_start[1]);
	/* The captured CellarDoor3 routes settle inside the shared 40-unit
	 * supported-arrival envelope at exactly 1800/1100 ms.  Preserve that
	 * endpoint instead of linearly reaching the seed early. */
	travel_fraction = horizontal_distance > 39.0f
	    ? (horizontal_distance - 39.0f) / horizontal_distance : 1.0f;
	fraction = cellar_wade_steps > 0
	    ? ((float)cellar_wade_step / (float)cellar_wade_steps) *
	        travel_fraction : 0.0f;
	for (axis = 0; axis < 3; axis++)
	{
		float coordinate = cellar_wade_start[axis] +
		    (cellar_wade_target[axis] - cellar_wade_start[axis]) * fraction;
		float speed = cellar_wade_steps > 0
		    ? (cellar_wade_target[axis] - cellar_wade_start[axis]) *
		        travel_fraction *
		        (1000.0f / (25.0f * cellar_wade_steps)) : 0.0f;

		pmove->s.origin[axis] = (short)lroundf(coordinate * 8.0f);
		pmove->s.velocity[axis] = cellar_wade_step == cellar_wade_steps
		    ? 0 : (short)lroundf(speed * 8.0f);
	}
	pmove->s.pm_type = PM_NORMAL;
	VectorSet(pmove->mins, -16.0f, -16.0f, -24.0f);
	VectorSet(pmove->maxs, 16.0f, 16.0f, 32.0f);
	pmove->groundentity =
	    (cellar_wade_steps == 72 && cellar_wade_step >= 13 &&
	     cellar_wade_step <= 25) ? NULL : &ents[0];
	wet = cellar_wade_initial_water ||
	    (cellar_wade_water_step > 0 &&
	     cellar_wade_step >= cellar_wade_water_step);
	pmove->waterlevel = wet
	    ? (cellar_wade_mode == CELLAR_WADE_WATER2 ? 2 : 1) : 0;
	pmove->watertype = wet ? CELLAR_WITNESS_WATERTYPE : 0;
	if (wet && cellar_wade_mode == CELLAR_WADE_LAVA)
		pmove->watertype |= CONTENTS_LAVA;
	if (wet && cellar_wade_mode == CELLAR_WADE_SLIME)
		pmove->watertype |= CONTENTS_SLIME;
}

static edict_t *TranslationMover(void)
{
	edict_t *mover = &ents[MOVER_INDEX];

	LiveEdict(mover, MOVER_INDEX, "func_door");
	mover->solid = SOLID_BSP;
	mover->movetype = MOVETYPE_PUSH;
	mover->use = door_use;
	mover->blocked = door_blocked;
	Set3(mover->mins, -8.0f, -8.0f, 0.0f);
	Set3(mover->maxs, 8.0f, 8.0f, 8.0f);
	VectorClear(mover->s.origin);
	VectorClear(mover->moveinfo.start_origin);
	Set3(mover->moveinfo.end_origin, 64.0f, 0.0f, 0.0f);
	mover->moveinfo.speed = 100.0f;
	mover->moveinfo.accel = 100.0f;
	mover->moveinfo.decel = 100.0f;
	mover->moveinfo.distance = 64.0f;
	mover->moveinfo.wait = 3.0f;
	mover->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	VectorClear(mover->moveinfo.start_angles);
	VectorClear(mover->moveinfo.end_angles);
	SetLinkedBounds(mover);
	return mover;
}

static edict_t *PlayerSubject(float x, float y, float z, int movetype,
	float max_z);

static edict_t *AliasDoor(int index, float y)
{
	edict_t *door = &ents[index];

	LiveEdict(door, index, "func_door");
	door->solid = SOLID_BSP;
	door->movetype = MOVETYPE_PUSH;
	door->use = door_use;
	door->blocked = door_blocked;
	Set3(door->mins, -8.0f, -8.0f, 0.0f);
	Set3(door->maxs, 8.0f, 8.0f, 8.0f);
	Set3(door->s.origin, 0.0f, y, 0.0f);
	VectorCopy(door->s.origin, door->moveinfo.start_origin);
	Set3(door->moveinfo.end_origin, 64.0f, y, 0.0f);
	door->moveinfo.speed = 100.0f;
	door->moveinfo.accel = 100.0f;
	door->moveinfo.decel = 100.0f;
	door->moveinfo.distance = 64.0f;
	door->moveinfo.wait = 3.0f;
	door->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	SetLinkedBounds(door);
	return door;
}

static edict_t *AliasTrigger(int index, float x, const char *target)
{
	edict_t *trigger = &ents[index];

	LiveEdict(trigger, index, "trigger_multiple");
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 1.0f;
	trigger->target = (char *)target;
	Set3(trigger->mins, -8.0f, -32.0f, -32.0f);
	Set3(trigger->maxs, 8.0f, 32.0f, 32.0f);
	Set3(trigger->s.origin, x, 0.0f, 0.0f);
	SetLinkedBounds(trigger);
	return trigger;
}

static void ConfigureCellarWade(const vec3_t start, const vec3_t target,
	int steps, int water_step, int initial_water, int mode)
{
	VectorCopy(start, cellar_wade_start);
	VectorCopy(target, cellar_wade_target);
	cellar_wade_step = 0;
	cellar_wade_steps = steps;
	cellar_wade_water_step = water_step;
	cellar_wade_initial_water = initial_water;
	cellar_wade_mode = mode;
}

static edict_t *CellarDoor3Binding(qboolean red,
	rune_link_t *link, rune_mechanism_plan_t *plan,
	rune_mechanism_node_t *entry_node, rune_mechanism_node_t *mover_node,
	sg_rune_mechanism_binding_t *binding)
{
	edict_t *trigger = &ents[3];
	edict_t *mover = &ents[MOVER_INDEX];

	ResetWorld();
	LiveEdict(trigger, 3, "trigger_multiple");
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_Multi;
	trigger->wait = 63.0f;
	if (red)
	{
		Set3(trigger->absmin, -2690.0f, 2663.0f, -513.0f);
		Set3(trigger->absmax, -2066.0f, 2852.0f, -391.0f);
	}
	else
	{
		Set3(trigger->absmin, 462.0f, 2240.0f, -513.0f);
		Set3(trigger->absmax, 1086.0f, 2429.0f, -391.0f);
	}
	LiveEdict(mover, MOVER_INDEX, "func_door_rotating");
	mover->solid = SOLID_BSP;
	mover->movetype = MOVETYPE_PUSH;
	mover->use = door_use;
	mover->blocked = door_blocked;
	mover->spawnflags = 4;
	if (red)
	{
		Set3(mover->s.origin, -2512.0f, 2794.0f, -452.0f);
		Set3(mover->mins, -4.0f, -76.0f, -60.0f);
		Set3(mover->maxs, 4.0f, 4.0f, 60.0f);
	}
	else
	{
		Set3(mover->s.origin, 908.0f, 2298.0f, -452.0f);
		Set3(mover->mins, -4.0f, -4.0f, -60.0f);
		Set3(mover->maxs, 4.0f, 76.0f, 60.0f);
	}
	VectorCopy(mover->s.origin, mover->moveinfo.start_origin);
	VectorCopy(mover->s.origin, mover->moveinfo.end_origin);
	VectorClear(mover->moveinfo.start_angles);
	Set3(mover->moveinfo.end_angles, 0.0f, 90.0f, 0.0f);
	VectorCopy(mover->moveinfo.end_angles, mover->s.angles);
	mover->moveinfo.speed = 100.0f;
	mover->moveinfo.accel = 100.0f;
	mover->moveinfo.decel = 100.0f;
	mover->moveinfo.distance = 90.0f;
	mover->moveinfo.wait = 60.0f;
	mover->moveinfo.state = SG_PLAT_STATE_TOP;
	mover->moveinfo.endfunc = door_hit_top;
	mover->think = door_go_down;
	mover->nextthink = 100.0f;
	SetLinkedBounds(mover);

	memset(link, 0, sizeof(*link));
	memset(plan, 0, sizeof(*plan));
	memset(entry_node, 0, sizeof(*entry_node));
	memset(mover_node, 0, sizeof(*mover_node));
	memset(binding, 0, sizeof(*binding));
	link->action = RL_DOOR;
	plan->controller_kind = SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
	entry_node->key = 3U;
	mover_node->key = MOVER_INDEX;
	binding->link = link;
	binding->plan = plan;
	binding->entry_node = entry_node;
	binding->mover_node = mover_node;
	binding->entry_entity = trigger;
	binding->mover_entity = mover;
	sibling_binding = binding;
	sibling_mover_keys[0] = MOVER_INDEX;
	sibling_mover_count = 1U;
	binding_current = 1;
	replay_gravity.value = 800.0f;
	sv_gravity = &replay_gravity;
	level.time = 1.0f;
	ground_test_active = false;
	ground_test_watertype = 0;
	sg_host.trace = StableGroundTrace;
	sg_host.pointcontents = GroundPointContents;
	sg_host.pmove = CellarWadePmove;
	return trigger;
}

static void CellarContinueSubject(const vec3_t origin, int waterlevel,
	int watertype)
{
	edict_t *player = PlayerSubject(origin[0], origin[1], origin[2],
	    MOVETYPE_WALK, 32.0f);
	int axis;

	player->health = 100;
	player->s.modelindex = 255;
	player->groundentity = &ents[0];
	player->waterlevel = waterlevel;
	player->watertype = watertype;
	memset(&player->client->ps.pmove, 0, sizeof(player->client->ps.pmove));
	player->client->ps.pmove.pm_type = PM_NORMAL;
	player->client->ps.pmove.gravity = 800;
	for (axis = 0; axis < 3; axis++)
		player->client->ps.pmove.origin[axis] =
		    (short)lroundf(origin[axis] * 8.0f);
	player->client->old_pmove = player->client->ps.pmove;
	VectorClear(player->client->oldvelocity);
}

static void TestDirectDoorShallowWadeParity(void)
{
	static const vec3_t red_approach_source = {
	    -2752.0f, 2694.0f, -486.875f };
	static const vec3_t red_approach_anchor = {
	    -2706.875f, 2701.75f, -486.875f };
	static const vec3_t blue_approach_source = {
	    1148.0f, 2398.0f, -486.875f };
	static const vec3_t blue_approach_anchor = {
	    1102.875f, 2390.125f, -486.875f };
	static const vec3_t red_source = { -2213.5f, 2757.5f, -443.875f };
	static const vec3_t red_mid = { -2408.5f, 2757.5f, -486.875f };
	static const vec3_t red_target = { -2752.0f, 2758.0f, -486.875f };
	static const vec3_t blue_source = { 609.5f, 2334.5f, -443.875f };
	static const vec3_t blue_mid = { 804.5f, 2334.0f, -486.875f };
	static const vec3_t blue_target = { 1148.0f, 2334.0f, -486.875f };
	const vec3_t *sources[2] = { &red_source, &blue_source };
	const vec3_t *middles[2] = { &red_mid, &blue_mid };
	const vec3_t *targets[2] = { &red_target, &blue_target };
	const vec3_t *approach_sources[2] = {
	    &red_approach_source, &blue_approach_source };
	const vec3_t *approach_anchors[2] = {
	    &red_approach_anchor, &blue_approach_anchor };
	rune_link_t link;
	rune_mechanism_plan_t plan;
	rune_mechanism_node_t entry_node, mover_node;
	sg_rune_mechanism_binding_t binding;
	edict_t *player;
	int arrival;
	int side;

	CHECK(SG_OracleDoorShallowWadeSafe(0, 0));
	CHECK(SG_OracleDoorShallowWadeSafe(1, CONTENTS_WATER));
	CHECK(SG_OracleDoorShallowWadeSafe(1,
	    CELLAR_WITNESS_WATERTYPE));
	CHECK(!SG_OracleDoorShallowWadeSafe(1, 0));
	CHECK(!SG_OracleDoorShallowWadeSafe(1, CONTENTS_MIST));
	CHECK(!SG_OracleDoorShallowWadeSafe(2, CONTENTS_WATER));
	CHECK(!SG_OracleDoorShallowWadeSafe(1,
	    CONTENTS_WATER | CONTENTS_LAVA));
	CHECK(!SG_OracleDoorShallowWadeSafe(1,
	    CONTENTS_WATER | CONTENTS_SLIME));
	CHECK(SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 1,
	    CELLAR_WITNESS_WATERTYPE));
	CHECK(!SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 1, 0));
	CHECK(!SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR, 1, CONTENTS_MIST));
	CHECK(!SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_AUTO_DOOR, 1,
	    CELLAR_WITNESS_WATERTYPE));
	CHECK(!SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR, 1,
	    CELLAR_WITNESS_WATERTYPE));
	CHECK(!SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_AUTO_DOOR, 0, CONTENTS_LAVA));
	CHECK(!SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR, 0, CONTENTS_SLIME));

	for (side = 0; side < 2; side++)
	{
		CellarDoor3Binding(side == 0, &link, &plan, &entry_node,
		    &mover_node, &binding);
		CHECK(SG_BoundDoorOutsideSweep(&binding, *sources[side]));
		CHECK(SG_BoundDoorOutsideSweep(&binding, *targets[side]));
		CHECK(SG_BoundDoorCrossesSweep(&binding, *sources[side],
		    *targets[side]));
		CHECK(SG_BoundDoorAtTop(&binding));

		/* Exact mirrored Door3 reverse candidates: installed seed 466 red
		 * and 321 blue begin at waterlevel one with raw type 0x18000020.
		 * Pin the sweep-clear source and exact trigger-contact anchor here;
		 * the execution test drives each natural wet Touch_Multi chain. */
		CHECK(SG_BoundDoorOutsideSweep(&binding,
		    *approach_sources[side]));
		CHECK(SG_BoundDoorOutsideSweep(&binding,
		    *approach_anchors[side]));
		CHECK(SG_BoundDoorEntryContactMatches(&binding,
		    *approach_anchors[side]));

		ConfigureCellarWade(*sources[side], *targets[side], 72, 23, 0,
		    CELLAR_WADE_SAFE);
		arrival = -1;
		CHECK(SG_OracleBoundDoorEgress(*sources[side], *targets[side],
		    &binding, NULL, &arrival));
		CHECK(arrival == 1800 && cellar_wade_step == 72);

		ConfigureCellarWade(*sources[side], *targets[side], 72, 23, 0,
		    CELLAR_WADE_WATER2);
		arrival = -1;
		CHECK(!SG_OracleBoundDoorEgress(*sources[side], *targets[side],
		    &binding, NULL, &arrival));
		CHECK(cellar_wade_step == 23);
		ConfigureCellarWade(*sources[side], *targets[side], 72, 23, 0,
		    CELLAR_WADE_LAVA);
		CHECK(!SG_OracleBoundDoorEgress(*sources[side], *targets[side],
		    &binding, NULL, &arrival));
		CHECK(cellar_wade_step == 23);
		ConfigureCellarWade(*sources[side], *targets[side], 72, 23, 0,
		    CELLAR_WADE_SLIME);
		CHECK(!SG_OracleBoundDoorEgress(*sources[side], *targets[side],
		    &binding, NULL, &arrival));
		CHECK(cellar_wade_step == 23);

		ConfigureCellarWade(*sources[side], *targets[side], 72, 23, 1,
		    CELLAR_WADE_WATER2);
		CHECK(!SG_OracleBoundDoorEgress(*sources[side], *targets[side],
		    &binding, NULL, &arrival));
		CHECK(cellar_wade_step == 0);

		CellarContinueSubject(*middles[side], 1,
		    CELLAR_WITNESS_WATERTYPE);
		ConfigureCellarWade(*middles[side], *targets[side], 44, 1, 1,
		    CELLAR_WADE_SAFE);
		arrival = -1;
		player = &ents[1];
		CHECK(SG_OracleBoundDoorContinue(player, *targets[side], &binding,
		    &arrival));
		CHECK(arrival == 1100 && cellar_wade_step == 44);

		CellarContinueSubject(*middles[side], 2, CONTENTS_WATER);
		ConfigureCellarWade(*middles[side], *targets[side], 44, 1, 1,
		    CELLAR_WADE_WATER2);
		CHECK(!SG_OracleBoundDoorContinue(player, *targets[side], &binding,
		    &arrival));
		CHECK(cellar_wade_step == 0);
		CellarContinueSubject(*middles[side], 1,
		    CONTENTS_WATER | CONTENTS_LAVA);
		ConfigureCellarWade(*middles[side], *targets[side], 44, 1, 1,
		    CELLAR_WADE_LAVA);
		CHECK(!SG_OracleBoundDoorContinue(player, *targets[side], &binding,
		    &arrival));
		CHECK(cellar_wade_step == 0);

		/* AUTO_DOOR and BUTTON_DOOR retain the legacy dry-only law. */
		plan.controller_kind = SG_MECHANISM_CONTROLLER_AUTO_DOOR;
		link.action = RL_DOOR;
		ConfigureCellarWade(*sources[side], *targets[side], 72, 23, 0,
		    CELLAR_WADE_SAFE);
		CHECK(!SG_OracleBoundDoorEgress(*sources[side], *targets[side],
		    &binding, NULL, &arrival));
		CHECK(cellar_wade_step == 23);
		CellarContinueSubject(*middles[side], 1,
		    CELLAR_WITNESS_WATERTYPE);
		ConfigureCellarWade(*middles[side], *targets[side], 44, 1, 1,
		    CELLAR_WADE_SAFE);
		CHECK(!SG_OracleBoundDoorContinue(player, *targets[side], &binding,
		    &arrival));
		CHECK(cellar_wade_step == 0);

		plan.controller_kind = SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
		link.action = RL_BUTTON_DOOR;
		ConfigureCellarWade(*sources[side], *targets[side], 72, 23, 0,
		    CELLAR_WADE_SAFE);
		CHECK(!SG_OracleBoundButtonDoorEgress(*sources[side],
		    *targets[side], &binding, NULL, &arrival,
		    SG_BUTTON_SUPPORT_STATIC));
		CHECK(cellar_wade_step == 23);
		CellarContinueSubject(*middles[side], 1,
		    CELLAR_WITNESS_WATERTYPE);
		ConfigureCellarWade(*middles[side], *targets[side], 44, 1, 1,
		    CELLAR_WADE_SAFE);
		CHECK(!SG_OracleBoundDoorContinue(player, *targets[side], &binding,
		    &arrival));
		CHECK(cellar_wade_step == 0);
	}

	memset(&sg_host, 0, sizeof(sg_host));
	sv_gravity = NULL;
}

static void TestTimedVaultEgressControlLaw(void)
{
	vec3_t source = { 0.0f, 0.0f, -220.0f };
	vec3_t target = { 100.0f, 0.0f, 0.0f };
	vec3_t velocity = { 0.0f, 0.0f, 0.0f };
	pmove_state_t pms;
	usercmd_t command;

	CHECK(SG_MechanismControllerUsesButton(
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR));
	CHECK(SG_MechanismControllerUsesButton(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT));
	CHECK(!SG_MechanismControllerUsesButton(
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR));
	CHECK(SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 0, 0));
	CHECK(SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 1,
	    CELLAR_WITNESS_WATERTYPE));
	CHECK(SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2,
	    CELLAR_WITNESS_WATERTYPE));
	CHECK(SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 3,
	    CELLAR_WITNESS_WATERTYPE));
	CHECK(!SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, CONTENTS_MIST));
	CHECK(!SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2,
	    CONTENTS_WATER | CONTENTS_LAVA));
	CHECK(!SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2,
	    CONTENTS_WATER | CONTENTS_SLIME));
	CHECK(!SG_OracleDoorEgressWaterSafe(
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR, 2,
	    CELLAR_WITNESS_WATERTYPE));

	memset(&pms, 0, sizeof(pms));
	memset(&command, 0, sizeof(command));
	command.msec = SG_SWIM_STEP_MSEC;
	CHECK(SG_DeclaredEgressCommand(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, source, target,
	    &pms, &command));
	CHECK(command.forwardmove == 400);
	CHECK(command.angles[PITCH] != 0);

	target[0] = 50.0f;
	target[2] = source[2];
	memset(&command, 0, sizeof(command));
	command.msec = SG_SWIM_STEP_MSEC;
	CHECK(SG_DeclaredEgressCommandMode(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, true, source, vec3_origin, target,
	    &pms, &command));
	CHECK(command.forwardmove > 0);
	VectorCopy(source, target);
	memset(&command, 0, sizeof(command));
	command.msec = SG_SWIM_STEP_MSEC;
	CHECK(SG_DeclaredEgressCommandMode(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, true, source, velocity, target,
	    &pms, &command));
	CHECK(command.forwardmove == 0);
	velocity[2] = -8.125f;
	memset(&command, 0, sizeof(command));
	command.msec = SG_SWIM_STEP_MSEC;
	CHECK(SG_DeclaredEgressCommandMode(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 2, true, source, velocity, target,
	    &pms, &command));
	CHECK(command.forwardmove > 0);
	target[0] = 100.0f;
	target[2] = 0.0f;

	memset(&command, 0, sizeof(command));
	command.msec = SG_SWIM_STEP_MSEC;
	CHECK(SG_DeclaredEgressCommand(
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 0, source, target,
	    &pms, &command));
	CHECK(command.forwardmove == 400);
	CHECK(command.angles[PITCH] == 0);
}

static void TestTimedVaultExactCaptureConvergesUnderRealPmove(void)
{
	rune_seed_t seeds[3];
	int next[3] = { 1, 2, -1 };
	pmove_state_t state;
	qboolean exact_capture;
	qboolean next_hop_progress = false;
	qboolean next_hop_captured = false;
	vec3_t command_target, origin, velocity;
	int route_seed = -2;
	int step;

	memset(seeds, 0, sizeof(seeds));
	VectorSet(seeds[0].origin, 64.0f, -2624.0f, -635.875f);
	VectorSet(seeds[1].origin, 128.0f, -2624.0f, -635.875f);
	VectorSet(seeds[2].origin, 192.0f, -2624.0f, -635.875f);
	seeds[0].flags = seeds[1].flags = RSF_WATER;
	memset(&state, 0, sizeof(state));
	state.pm_type = PM_NORMAL;
	state.origin[0] = (short)(55.0f * 8.0f);
	state.origin[1] = (short)(-2621.0f * 8.0f);
	state.origin[2] = (short)(-635.25f * 8.0f);
	state.velocity[0] = (short)(4.375f * 8.0f);
	state.velocity[1] = (short)(-2.5f * 8.0f);
	state.velocity[2] = (short)(-0.25f * 8.0f);
	state.gravity = 800;
	vault_floor_z = -659.5f;
	VectorSet(origin, 55.0f, -2621.0f, -635.25f);
	VectorSet(velocity, 4.375f, -2.5f, -0.25f);
	CHECK(SG_TimedVaultEgressAdvancePose(seeds, 3, next,
	    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 3, origin, velocity,
	    seeds[2].origin, &route_seed, &exact_capture, command_target));
	CHECK(route_seed == -2);
	CHECK(exact_capture);
	CHECK(VectorCompare(command_target, seeds[0].origin));
	if (route_seed != -2)
		return;

	for (step = 0; step < 80; step++)
	{
		vec3_t before_delta, after_delta;
		pmove_t pmove;
		usercmd_t command;
		float before_distance, after_distance;
		int axis;

		for (axis = 0; axis < 3; axis++)
		{
			origin[axis] = state.origin[axis] * 0.125f;
			velocity[axis] = state.velocity[axis] * 0.125f;
		}
		memset(&command, 0, sizeof(command));
		command.msec = SG_SWIM_STEP_MSEC;
		CHECK(SG_DeclaredEgressCommandMode(
		    SG_MECHANISM_CONTROLLER_TIMED_VAULT, 3, exact_capture,
		    origin, velocity, command_target, &state, &command));
		VectorSubtract(seeds[1].origin, origin, before_delta);
		before_distance = VectorLength(before_delta);
		memset(&pmove, 0, sizeof(pmove));
		pmove.s = state;
		pmove.cmd = command;
		pmove.trace = VaultFloorTrace;
		pmove.pointcontents = AllWaterPointContents;
		Pmove(&pmove);
		state = pmove.s;
		for (axis = 0; axis < 3; axis++)
		{
			origin[axis] = state.origin[axis] * 0.125f;
			velocity[axis] = state.velocity[axis] * 0.125f;
		}
		VectorSubtract(seeds[1].origin, origin, after_delta);
		after_distance = VectorLength(after_delta);
		if (after_distance < before_distance)
			next_hop_progress = true;
		CHECK(SG_TimedVaultEgressAdvancePose(seeds, 3, next,
		    SG_MECHANISM_CONTROLLER_TIMED_VAULT, pmove.waterlevel,
		    origin, velocity, seeds[2].origin, &route_seed, &exact_capture,
		    command_target));
		if (route_seed == 1)
		{
			next_hop_captured = true;
			break;
		}
	}
	CHECK(next_hop_progress);
	CHECK(next_hop_captured);
}

static void TestTimedVaultEightHopRouteReachesShoreWithinLease(void)
{
	rune_seed_t seeds[9];
	int next[9] = { 1, 2, 3, 4, 5, 6, 7, 8, -1 };
	pmove_state_t state;
	qboolean exact_capture;
	qboolean arrived = false;
	vec3_t command_target, origin, velocity;
	int route_seed = 0;
	int step, waterlevel = 2;

	memset(seeds, 0, sizeof(seeds));
	for (step = 0; step < 9; step++)
		VectorSet(seeds[step].origin, 64.0f,
		    -2624.0f + 64.0f * (float)step, -635.875f);
	for (step = 0; step < 8; step++)
		seeds[step].flags = RSF_WATER;
	memset(&state, 0, sizeof(state));
	state.pm_type = PM_NORMAL;
	state.origin[0] = (short)(56.875f * 8.0f);
	state.origin[1] = (short)(-2620.5f * 8.0f);
	state.origin[2] = (short)(-635.25f * 8.0f);
	state.velocity[0] = (short)(4.125f * 8.0f);
	state.velocity[1] = (short)(-1.75f * 8.0f);
	state.velocity[2] = (short)(-0.25f * 8.0f);
	state.gravity = 800;
	vault_floor_z = -659.5f;
	vault_shore_y = -2144.0f;
	vault_far_shore_enabled = false;

	for (step = 0; step < (9000 - 3250) / SG_SWIM_STEP_MSEC; step++)
	{
		pmove_t pmove;
		usercmd_t command;
		vec3_t delta;
		int axis;

		for (axis = 0; axis < 3; axis++)
		{
			origin[axis] = state.origin[axis] * 0.125f;
			velocity[axis] = state.velocity[axis] * 0.125f;
		}
		CHECK(SG_TimedVaultEgressAdvancePose(seeds, 9, next,
		    SG_MECHANISM_CONTROLLER_TIMED_VAULT, waterlevel,
		    origin, velocity, seeds[8].origin, &route_seed, &exact_capture,
		    command_target));
		memset(&command, 0, sizeof(command));
		command.msec = SG_SWIM_STEP_MSEC;
		CHECK(SG_DeclaredEgressCommandMode(
		    SG_MECHANISM_CONTROLLER_TIMED_VAULT, waterlevel,
		    exact_capture, origin, velocity, command_target, &state,
		    &command));
		memset(&pmove, 0, sizeof(pmove));
		pmove.s = state;
		pmove.cmd = command;
		pmove.trace = VaultFloorTrace;
		pmove.pointcontents = VaultShorePointContents;
		Pmove(&pmove);
		state = pmove.s;
		waterlevel = pmove.waterlevel;
		for (axis = 0; axis < 3; axis++)
			origin[axis] = state.origin[axis] * 0.125f;
		VectorSubtract(seeds[8].origin, origin, delta);
		if (pmove.groundentity && pmove.waterlevel < 2 &&
		    delta[0] * delta[0] + delta[1] * delta[1] <
		        SG_REPLAY_ARRIVE_RADIUS * SG_REPLAY_ARRIVE_RADIUS &&
		    fabsf(delta[2]) < SG_REPLAY_ARRIVE_Z)
		{
			arrived = true;
			break;
		}
	}
	CHECK(arrived);
	CHECK(step * SG_SWIM_STEP_MSEC + 3250 < 9000);
}

static void TestTimedVaultTargetFacingRouteFromRealCandidateState(void)
{
	rune_seed_t seeds[19];
	rune_link_t links[13];
	int next[19], dist[19], incoming[19], next_incoming[13], queue[19];
	int heap[19], heap_pos[19];
	float score[19];
	pmove_state_t state;
	qboolean exact_capture;
	qboolean arrived = false;
	vec3_t command_target, origin, velocity;
	int source, route_seed, step, waterlevel = 2;
	int i;

	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	VectorSet(seeds[0].origin, 0.0f, -2624.0f, -635.875f);
	VectorSet(seeds[1].origin, 64.0f, -2624.0f, -635.875f);
	for (i = 2; i <= 9; i++)
		VectorSet(seeds[i].origin, 0.0f,
		    -2624.0f - 80.0f * (float)(i - 1), -635.875f);
	for (i = 10; i <= 14; i++)
		VectorSet(seeds[i].origin, 64.0f,
		    -2624.0f + 64.0f * (float)(i - 9), -635.875f);
	VectorSet(seeds[15].origin, 192.0f, -2304.0f, -635.875f);
	VectorSet(seeds[16].origin, 192.0f, -2432.0f, -635.875f);
	VectorSet(seeds[17].origin, 192.0f, -2560.0f, -635.875f);
	VectorSet(seeds[18].origin, 64.0f, -2560.0f, -635.875f);
	for (i = 0; i < 9; i++)
		seeds[i].flags = RSF_WATER;
	for (i = 10; i < 14; i++)
		seeds[i].flags = RSF_WATER;
	links[0].from = 0;
	links[0].to = 2;
	links[0].action = RL_SWIM;
	for (i = 1; i < 8; i++)
	{
		links[i].from = i + 1;
		links[i].to = i + 2;
		links[i].action = RL_SWIM;
	}
	links[8].from = 1;
	links[8].to = 10;
	links[8].action = RL_SWIM;
	for (i = 9; i < 13; i++)
	{
		links[i].from = i + 1;
		links[i].to = i + 2;
		links[i].action = RL_SWIM;
	}
	CHECK(SG_WaterEscapeIndexBuild(seeds, 19, links, 13, next, dist,
	    incoming, next_incoming, queue));
	CHECK(SG_WaterEscapeTargetIndexBuild(seeds, 19, links, 13,
	    seeds[18].origin, next, score, incoming, next_incoming, heap,
	    heap_pos));
	VectorSet(origin, 1.0f, -2619.375f, -580.0f);
	VectorSet(velocity, 10.125f, 14.625f, -585.5f);
	source = SG_TimedVaultEgressSourceSelect(seeds, 19, next, origin,
	    seeds[18].origin, VaultSourceReachable, NULL);
	CHECK(source == 11);
	if (source != 11)
		return;
	route_seed = -source - 2;
	memset(&state, 0, sizeof(state));
	state.pm_type = PM_NORMAL;
	for (i = 0; i < 3; i++)
	{
		state.origin[i] = (short)(origin[i] * 8.0f);
		state.velocity[i] = (short)(velocity[i] * 8.0f);
	}
	state.gravity = 800;
	vault_floor_z = -659.5f;
	vault_shore_y = -2336.0f;
	vault_far_shore_y = -2520.0f;
	vault_far_shore_enabled = true;
	vault_side_corridor_enabled = true;
	for (step = 0; step < (9000 - 1375) / SG_SWIM_STEP_MSEC; step++)
	{
		pmove_t pmove;
		usercmd_t command;
		vec3_t delta;

		for (i = 0; i < 3; i++)
		{
			origin[i] = state.origin[i] * 0.125f;
			velocity[i] = state.velocity[i] * 0.125f;
		}
		CHECK(SG_TimedVaultEgressAdvancePose(seeds, 19, next,
		    SG_MECHANISM_CONTROLLER_TIMED_VAULT, waterlevel, origin,
		    velocity, seeds[18].origin, &route_seed, &exact_capture,
		    command_target));
		memset(&command, 0, sizeof(command));
		command.msec = SG_SWIM_STEP_MSEC;
		CHECK(SG_DeclaredEgressCommandMode(
		    SG_MECHANISM_CONTROLLER_TIMED_VAULT, waterlevel, exact_capture,
		    origin, velocity, command_target, &state, &command));
		memset(&pmove, 0, sizeof(pmove));
		pmove.s = state;
		pmove.cmd = command;
		pmove.trace = VaultFloorTrace;
		pmove.pointcontents = VaultShorePointContents;
		Pmove(&pmove);
		state = pmove.s;
		waterlevel = pmove.waterlevel;
		for (i = 0; i < 3; i++)
			origin[i] = state.origin[i] * 0.125f;
		VectorSubtract(seeds[18].origin, origin, delta);
		if (pmove.groundentity && pmove.waterlevel < 2 &&
		    delta[0] * delta[0] + delta[1] * delta[1] <
		        SG_REPLAY_ARRIVE_RADIUS * SG_REPLAY_ARRIVE_RADIUS &&
		    fabsf(delta[2]) < SG_REPLAY_ARRIVE_Z)
		{
			arrived = true;
			break;
		}
	}
	vault_far_shore_enabled = false;
	vault_side_corridor_enabled = false;
	CHECK(arrived);
	CHECK(step * SG_SWIM_STEP_MSEC + 1375 < 9000);
}

static void TestBoundDoorSiblingAliasReplay(void)
{
	static char alias_target[] = "alias_target";
	static char other_target[] = "other_target";
	static char alias_team[] = "alias_team";
	rune_link_t link;
	rune_mechanism_plan_t plan;
	rune_mechanism_node_t mover_node;
	sg_rune_mechanism_binding_t binding;
	edict_t *master;
	edict_t *member;
	edict_t *other;
	edict_t *button;
	edict_t *entry_trigger;
	edict_t *player;
	usercmd_t cmd;

	ResetWorld();
	master = AliasDoor(MOVER_INDEX, 0.0f);
	member = AliasDoor(HOOK_INDEX, 64.0f);
	other = AliasDoor(10, 128.0f);
	master->targetname = alias_target;
	member->targetname = alias_target;
	other->targetname = other_target;
	master->team = alias_team;
	member->team = alias_team;
	master->teammaster = master;
	master->teamchain = member;
	member->teammaster = master;
	member->flags = FL_TEAMSLAVE;
	sibling_trigger_a = AliasTrigger(3, -40.0f, alias_target);
	sibling_trigger_b = AliasTrigger(4, 96.0f, alias_target);
	player = PlayerSubject(-40.0f, 0.0f, 0.0f, MOVETYPE_WALK, 32.0f);
	memset(&link, 0, sizeof(link));
	memset(&plan, 0, sizeof(plan));
	memset(&mover_node, 0, sizeof(mover_node));
	memset(&binding, 0, sizeof(binding));
	link.action = RL_DOOR;
	plan.controller_kind = SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
	mover_node.key = MOVER_INDEX;
	binding.link = &link;
	binding.plan = &plan;
	binding.entry_entity = sibling_trigger_a;
	binding.mover_entity = master;
	binding.mover_node = &mover_node;
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 25;

	/* A complete mover-set sibling is not the serialized controller. */
	CHECK(!SG_DeclaredDoorEquivalentActivation(sibling_trigger_a,
	    sibling_trigger_b, master, sibling_trigger_b->s.origin));

	sibling_binding = &binding;
	sibling_mover_keys[0] = MOVER_INDEX;
	sibling_mover_keys[1] = HOOK_INDEX;
	sibling_mover_count = 2U;
	binding_current = 1;
	replay_gravity.value = 800.0f;
	sv_gravity = &replay_gravity;
	sg_host.box_edicts = SiblingBoxEdicts;
	sg_host.pmove = SiblingEgressPmove;

	/* Generation and bound replay both authenticate the exact entry. */
	sibling_pmove_x = -40.0f;
	sibling_trigger_hit_count = 0;
	CHECK(SG_OracleDeclaredDoorStepSafe(player, sibling_trigger_a, &cmd));
	sibling_pmove_x = 96.0f;
	sibling_trigger_hit_count = 0;
	CHECK(!SG_OracleDeclaredDoorStepSafe(player, sibling_trigger_a, &cmd));
	CHECK(sibling_trigger_hit_count == 2 &&
	      sibling_trigger_hits[0] == sibling_trigger_a &&
	      sibling_trigger_hits[1] == sibling_trigger_b);

	/* The exact bound trigger remains valid.  The two overlap samples are the
	 * initial entry into A and its unchanged physical replay position. */
	sibling_pmove_x = -40.0f;
	sibling_trigger_hit_count = 0;
	CHECK(SG_OracleBoundDoorStepSafe(player, &binding, &cmd));
	CHECK(sibling_trigger_hit_count == 2 &&
	      sibling_trigger_hits[0] == sibling_trigger_a &&
	      sibling_trigger_hits[1] == sibling_trigger_a);

	/* Shallow admission belongs to an authenticated bound DIRECT plan.  The
	 * legacy unbound wrapper cannot infer controller identity from a trigger
	 * shape, and AUTO/BUTTON remain dry-only. */
	player->waterlevel = 1;
	player->watertype = CONTENTS_WATER;
	sibling_pmove_waterlevel = 1;
	sibling_pmove_watertype = CONTENTS_WATER;
	sibling_pmove_x = -40.0f;
	CHECK(!SG_OracleDeclaredDoorStepSafe(player, sibling_trigger_a, &cmd));
	CHECK(SG_OracleBoundDoorStepSafe(player, &binding, &cmd));
	plan.controller_kind = SG_MECHANISM_CONTROLLER_AUTO_DOOR;
	CHECK(!SG_OracleBoundDoorStepSafe(player, &binding, &cmd));
	plan.controller_kind = SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
	player->waterlevel = 0;
	player->watertype = 0;
	sibling_pmove_waterlevel = 0;
	sibling_pmove_watertype = 0;

	/* A sealed direct-trigger approach owns exactly its serialized entry.
	 * Another trigger may target the same mover closure, but accepting it would
	 * let a sibling callback consume the one-command ticket. */
	sibling_pmove_x = 96.0f;
	sibling_trigger_hit_count = 0;
	CHECK(!SG_OracleBoundDoorStepSafe(player, &binding, &cmd));
	CHECK(sibling_trigger_hit_count == 2 &&
	      sibling_trigger_hits[0] == sibling_trigger_a &&
	      sibling_trigger_hits[1] == sibling_trigger_b);

	/* Once the exact entry has opened the bound mover set, crossing an
	 * opposite-side sibling with that same complete set is only a harmless
	 * refresh.  Generation already admits this egress; loader replay must use
	 * the sealed mover set to make the same decision. */
	{
		vec3_t source = { -40.0f, 0.0f, 0.0f };
		vec3_t target = { 96.0f, 0.0f, 0.0f };
		int arrival = -1;

		sg_host.trace = StableGroundTrace;
		sg_host.pointcontents = GroundPointContents;
		sibling_pmove_preserve_zero = true;
		sibling_pmove_x = 96.0f;
		sibling_trigger_hit_count = 0;
		CHECK(SG_OracleBoundDoorEgress(source, target, &binding, NULL,
		    &arrival));
		CHECK(arrival == 100);

		/* AUTO and BUTTON plans do not own an independent Touch_Multi sibling. */
		plan.controller_kind = SG_MECHANISM_CONTROLLER_AUTO_DOOR;
		arrival = -1;
		CHECK(!SG_OracleBoundDoorEgress(source, target, &binding, NULL,
		    &arrival));
		plan.controller_kind = SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
		arrival = -1;
		CHECK(!SG_OracleBoundButtonDoorEgress(source, target, &binding, NULL,
		    &arrival, SG_BUTTON_SUPPORT_STATIC));
		plan.controller_kind = SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;

		/* A sibling whose closure differs from the binding stays contaminating. */
		sibling_trigger_b->target = other_target;
		arrival = -1;
		CHECK(!SG_OracleBoundDoorEgress(source, target, &binding, NULL,
		    &arrival));
		sibling_trigger_b->target = alias_target;

		/* Live same-name geometry cannot broaden the sealed mover set. */
		sibling_mover_keys[0] = HOOK_INDEX;
		sibling_mover_count = 1U;
		arrival = -1;
		CHECK(!SG_OracleBoundDoorEgress(source, target, &binding, NULL,
		    &arrival));
		sibling_mover_keys[0] = MOVER_INDEX;
		sibling_mover_keys[1] = HOOK_INDEX;
		sibling_mover_count = 2U;
		sibling_pmove_preserve_zero = false;
	}

	/* A different automatic trigger is also not the direct plan entry. */
	sibling_trigger_b->touch = Touch_DoorTrigger;
	sibling_trigger_b->owner = master;
	CHECK(!SG_DeclaredDoorEquivalentActivation(sibling_trigger_a,
	    sibling_trigger_b, master, sibling_trigger_b->s.origin));
	sibling_trigger_hit_count = 0;
	CHECK(!SG_OracleBoundDoorStepSafe(player, &binding, &cmd));
	CHECK(sibling_trigger_hit_count == 2 &&
	      sibling_trigger_hits[0] == sibling_trigger_a &&
	      sibling_trigger_hits[1] == sibling_trigger_b);
	sibling_trigger_b->touch = Touch_Multi;
	sibling_trigger_b->owner = NULL;

	/* AUTO_DOOR keeps its ordinary grounded approach law.  The exact same
	 * bound route succeeds grounded and rejects one transient-air step. */
	plan.controller_kind = SG_MECHANISM_CONTROLLER_AUTO_DOOR;
	sibling_trigger_a->touch = Touch_DoorTrigger;
	sibling_trigger_a->owner = master;
	sg_host.trace = StableGroundTrace;
	sg_host.pointcontents = GroundPointContents;
	sibling_pmove_preserve_zero = true;
	sibling_pmove_x = -40.0f;
	{
		vec3_t source = { -80.0f, 0.0f, 0.0f };
		vec3_t anchor = { -40.0f, 0.0f, 0.0f };
		int arrival = -1;
		int touch = -1;

		sibling_pmove_airborne = false;
		CHECK(SG_OracleBoundDoorApproach(source, anchor, &binding,
		    &arrival, &touch));
		CHECK(arrival == 25 && touch == 25);
		sibling_pmove_airborne = true;
		arrival = touch = -1;
		CHECK(!SG_OracleBoundDoorApproach(source, anchor, &binding,
		    &arrival, &touch));
	}
	sibling_pmove_preserve_zero = false;
	sibling_pmove_airborne = false;
	sibling_trigger_a->touch = Touch_Multi;
	sibling_trigger_a->owner = NULL;
	plan.controller_kind = SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
	sibling_pmove_x = 96.0f;

	/* B may not retarget an independently safe but different fanout/set. */
	sibling_trigger_b->target = other_target;
	sibling_trigger_hit_count = 0;
	CHECK(!SG_OracleBoundDoorStepSafe(player, &binding, &cmd));
	CHECK(sibling_trigger_hit_count == 2 &&
	      sibling_trigger_hits[1] == sibling_trigger_b);
	sibling_trigger_b->target = alias_target;

	/* The binding's sealed mover keys remain authority even when B's visible
	 * target closure is unchanged.  Dropping the master key rejects the alias. */
	sibling_mover_keys[0] = HOOK_INDEX;
	sibling_mover_count = 1U;
	sibling_trigger_hit_count = 0;
	CHECK(!SG_OracleBoundDoorStepSafe(player, &binding, &cmd));
	CHECK(sibling_trigger_hit_count == 2 &&
	      sibling_trigger_hits[1] == sibling_trigger_b);
	sibling_mover_keys[0] = MOVER_INDEX;
	sibling_mover_keys[1] = HOOK_INDEX;
	sibling_mover_count = 2U;

	/* The sibling exception is exclusively DIRECT_TRIGGER_DOOR.  A valid stock
	 * button controller facing the same direct B trigger retains the normal
	 * rejection. */
	button = &ents[BUTTON_INDEX];
	LiveEdict(button, BUTTON_INDEX, "func_button");
	button->target = alias_target;
	button->movetype = MOVETYPE_STOP;
	button->solid = SOLID_BSP;
	button->touch = button_touch;
	button->use = button_use;
	button->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	button->moveinfo.speed = 40.0f;
	button->moveinfo.accel = 40.0f;
	button->moveinfo.decel = 40.0f;
	button->moveinfo.wait = 3.0f;
	Set3(button->mins, -8.0f, -8.0f, -4.0f);
	Set3(button->maxs, 8.0f, 8.0f, 4.0f);
	VectorClear(button->moveinfo.start_origin);
	Set3(button->moveinfo.end_origin, 0.0f, 0.0f, -2.0f);
	SetLinkedBounds(button);
	entry_trigger = sibling_trigger_a;
	link.action = RL_BUTTON_DOOR;
	plan.controller_kind = SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
	binding.entry_entity = button;
	sibling_trigger_a = NULL;
	sibling_trigger_hit_count = 0;
	CHECK(!SG_OracleBoundDoorStepSafe(player, &binding, &cmd));
	CHECK(sibling_trigger_hit_count == 1 &&
	      sibling_trigger_hits[0] == sibling_trigger_b);
	sibling_trigger_a = entry_trigger;
	binding.entry_entity = entry_trigger;
	link.action = RL_DOOR;
	plan.controller_kind = SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;

	/* Removing team members does not broaden entry identity: the sibling is
	 * still not the sealed controller. */
	member->inuse = false;
	master->team = NULL;
	master->teamchain = NULL;
	sibling_mover_count = 1U;
	sibling_mover_keys[0] = MOVER_INDEX;
	sibling_trigger_hit_count = 0;
	CHECK(!SG_OracleBoundDoorStepSafe(player, &binding, &cmd));
	CHECK(sibling_trigger_hit_count == 2 &&
	      sibling_trigger_hits[0] == sibling_trigger_a &&
	      sibling_trigger_hits[1] == sibling_trigger_b);

	memset(&sg_host, 0, sizeof(sg_host));
	sv_gravity = NULL;
}

static edict_t *PlayerSubject(float x, float y, float z, int movetype,
	float max_z)
{
	edict_t *player = &ents[1];

	LiveEdict(player, 1, "player");
	player->client = &clients[0];
	player->solid = SOLID_BBOX;
	player->movetype = movetype;
	Set3(player->s.origin, x, y, z);
	Set3(player->mins, -16.0f, -16.0f, -24.0f);
	Set3(player->maxs, 16.0f, 16.0f, max_z);
	SetLinkedBounds(player);
	return player;
}

static edict_t *BodySubject(float x, float y, float z)
{
	edict_t *body = &ents[3];

	LiveEdict(body, 3, "bodyque");
	body->solid = SOLID_BBOX;
	body->movetype = MOVETYPE_TOSS;
	Set3(body->s.origin, x, y, z);
	Set3(body->mins, -16.0f, -16.0f, -24.0f);
	Set3(body->maxs, 16.0f, 16.0f, -8.0f);
	SetLinkedBounds(body);
	return body;
}

static edict_t *HookSubject(float x, float y, float z, int solid)
{
	edict_t *owner = PlayerSubject(-128.0f, -128.0f, 0.0f,
	                               MOVETYPE_WALK, 32.0f);
	edict_t *hook = &ents[HOOK_INDEX];

	LiveEdict(hook, HOOK_INDEX, "noclass");
	hook->solid = solid;
	hook->movetype = MOVETYPE_FLYMISSILE;
	hook->owner = owner;
	hook->touch = hook_touch;
	hook->die = hook_die;
	Set3(hook->s.origin, x, y, z);
	VectorClear(hook->mins);
	VectorClear(hook->maxs);
	SetLinkedBounds(hook);
	owner->client->hook = hook;
	return hook;
}

static edict_t *ButtonDoorFixture(void)
{
	edict_t *button = &ents[BUTTON_INDEX];
	edict_t *door = &ents[DOOR_INDEX];

	LiveEdict(door, DOOR_INDEX, "func_door");
	door->targetname = "button_target";
	door->teammaster = door;
	door->movetype = MOVETYPE_PUSH;
	door->solid = SOLID_BSP;
	door->use = door_use;
	door->blocked = door_blocked;
	door->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	door->moveinfo.distance = 10.0f;
	door->moveinfo.speed = 100.0f;
	door->moveinfo.accel = 100.0f;
	door->moveinfo.decel = 100.0f;
	door->moveinfo.wait = 3.0f;
	Set3(door->mins, -8.0f, -8.0f, 0.0f);
	Set3(door->maxs, 8.0f, 8.0f, 64.0f);
	Set3(door->moveinfo.start_origin, 256.0f, 256.0f, 0.0f);
	Set3(door->moveinfo.end_origin, 266.0f, 256.0f, 0.0f);
	VectorCopy(door->moveinfo.start_origin, door->s.origin);
	SetLinkedBounds(door);

	LiveEdict(button, BUTTON_INDEX, "func_button");
	button->target = "button_target";
	button->movetype = MOVETYPE_STOP;
	button->solid = SOLID_BSP;
	button->touch = button_touch;
	button->use = button_use;
	button->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	/* Stock SP_func_button leaves distance zero and publishes only endpoints. */
	button->moveinfo.distance = 0.0f;
	button->moveinfo.speed = 40.0f;
	button->moveinfo.accel = 40.0f;
	button->moveinfo.decel = 40.0f;
	button->moveinfo.wait = 3.0f;
	VectorClear(button->moveinfo.start_origin);
	Set3(button->moveinfo.end_origin, 0.0f, 0.0f, -2.0f);
	Set3(button->mins, -34.0f, -58.0f, -4.0f);
	Set3(button->maxs, 34.0f, 58.0f, 4.0f);
	SetLinkedBounds(button);
	return button;
}

static void TestButtonContactAndEndpointTiming(void)
{
	edict_t *button;
	rune_link_t link;
	rune_mechanism_plan_t plan;
	sg_rune_mechanism_binding_t binding;
	vec3_t boundary, outward, source, early, anchor;
	vec3_t trace_end, trace_mins = { -16.0f, -16.0f, -24.0f };
	vec3_t trace_maxs = { 16.0f, 16.0f, 32.0f };
	trace_t stable_trace;
	trace_t expected_trace;
	csurface_t expected_surface;

	ResetWorld();
	button = ButtonDoorFixture();
	contact_button = button;
	sg_host.trace = ButtonContactTrace;

	/* Thin floor plate: the q8 point exactly on the inclusive brush boundary
	 * is startsolid, while one q8 unit outward has a clear inward first hit. */
	Set3(button->absmin, -98.0f, 454.0f, 14.0f);
	Set3(button->absmax, -30.0f, 570.0f, 22.0f);
	contact_axis = 2;
	Set3(boundary, -64.0f, 512.0f, 46.0f);
	Set3(outward, -64.0f, 512.0f, 46.125f);
	CHECK(SG_DeclaredButtonDoorContactStatus(button, boundary) ==
	      SG_BUTTON_CONTACT_STARTSOLID);
	CHECK(SG_DeclaredButtonDoorContactStatus(button, outward) ==
	      SG_BUTTON_CONTACT_OK);

	/* The first generated xmap28 button link starts well left of its wide floor
	 * plate.  An expanded-AABB approximation reported contact at X=-114 and
	 * paused replay roughly 82 units before the exact recorded anchor.  Bound
	 * replay must use the same first-hit trace as generation. */
	memset(&link, 0, sizeof(link));
	memset(&plan, 0, sizeof(plan));
	memset(&binding, 0, sizeof(binding));
	link.action = RL_BUTTON_DOOR;
	plan.controller_kind = SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
	binding.link = &link;
	binding.plan = &plan;
	binding.entry_entity = button;
	Set3(button->absmin, -98.0f, 454.0f, 14.0f);
	Set3(button->absmax, -30.0f, 570.0f, 22.0f);
	Set3(source, -160.0f, 560.0f, 40.03125f);
	Set3(early, -114.0f, 560.0f, 40.03125f);
	Set3(anchor, -32.125f, 495.875f, 44.125f);
	contact_axis = 3;
	binding_current = 1;
	CHECK(!SG_BoundDoorEntryContactMatches(&binding, source));
	CHECK(!SG_BoundDoorEntryContactMatches(&binding, early));
	CHECK(SG_BoundDoorEntryContactMatches(&binding, anchor));
	/* Bound replay's inward contact probe is anchor authentication, not a
	 * synthetic button callback.  A lookahead-only match must keep walking
	 * until Pmove reports the physical solid touch; ordinary trigger volumes
	 * retain their bound containment fallback. */
	CHECK(!SG_OracleDoorApproachContactObserved(true, false, true));
	CHECK(!SG_OracleDoorApproachContactObserved(true, false, false));
	CHECK(SG_OracleDoorApproachContactObserved(true, true, true));
	CHECK(SG_OracleDoorApproachContactObserved(true, true, false));
	CHECK(SG_OracleDoorApproachContactObserved(false, false, true));
	CHECK(SG_OracleDoorApproachContactObserved(false, true, true));
	CHECK(SG_OracleDoorApproachContactObserved(false, true, false));
	CHECK(!SG_OracleDoorApproachContactObserved(false, false, false));
	binding_current = 0;

	/* The same inclusive-boundary law applies to a vertical button face. */
	Set3(button->absmin, 30.0f, -32.0f, 0.0f);
	Set3(button->absmax, 38.0f, 32.0f, 64.0f);
	contact_axis = 0;
	Set3(boundary, 14.0f, 0.0f, 32.0f);
	Set3(outward, 13.875f, 0.0f, 32.0f);
	CHECK(SG_DeclaredButtonDoorContactStatus(button, boundary) ==
	      SG_BUTTON_CONTACT_STARTSOLID);
	CHECK(SG_DeclaredButtonDoorContactStatus(button, outward) ==
	      SG_BUTTON_CONTACT_OK);

	/* 2 units at 40 u/s plus the stock completion margin is 250 ms.  The
	 * complete serialized cost therefore remains valid even though the unused
	 * moveinfo.distance field is zero. */
	CHECK(SG_DeclaredDoorContractCost(button, 500, 250, 500) == 2850);
	button->moveinfo.wait = 0.5f;
	CHECK(SG_DeclaredDoorContractCost(button, 500, 250, 500) < 0);
	button->moveinfo.wait = 3.0f;

	/* A RIDER witness is a continuous player-hull carry, not two clear
	 * teleported endpoints.  Ignore only the authenticated button pusher and
	 * reject any world/foreign obstruction in the middle. */
	Set3(source, -32.125f, 495.875f, 44.125f);
	Set3(anchor, -32.125f, 495.875f, 42.125f);
	contact_button = button;
	carry_test_blocked = false;
	carry_test_passent_ok = false;
	carry_test_coords_ok = false;
	sg_host.trace = ButtonCarryTrace;
	CHECK(SG_OracleButtonCarryClear(button, source, anchor, false));
	CHECK(carry_test_passent_ok && carry_test_coords_ok);
	CHECK(carry_test_mask == MASK_PLAYERSOLID);
	CHECK(SG_OracleButtonCarryClear(button, source, anchor, true));
	CHECK(carry_test_mask == MASK_PLAYERSOLID);
	carry_test_blocked = true;
	CHECK(!SG_OracleButtonCarryClear(button, source, anchor, true));
	carry_test_blocked = false;

	/* Loader replay keeps the native full collision trace authoritative while
	 * hiding only linked client and client-owned BBOXes for that synchronous
	 * call.  Deterministic BBOX/BSP order and every native trace field remain
	 * untouched; a foreign deterministic BBOX winner fails closed. */
	LiveEdict(&ents[1], 1, "player");
	ents[1].client = &clients[0];
	ents[1].solid = SOLID_BBOX;
	SetLinkedBounds(&ents[1]);
	LiveEdict(&ents[2], 2, "player");
	ents[2].client = &clients[1];
	LiveEdict(&ents[DOOR_INDEX + 1], DOOR_INDEX + 1, "foreign_box");
	ents[DOOR_INDEX + 1].solid = SOLID_BBOX;
	SetLinkedBounds(&ents[DOOR_INDEX + 1]);
	LiveEdict(&ents[DOOR_INDEX + 2], DOOR_INDEX + 2, "client_owned");
	ents[DOOR_INDEX + 2].solid = SOLID_BBOX;
	ents[DOOR_INDEX + 2].owner = &ents[2];
	SetLinkedBounds(&ents[DOOR_INDEX + 2]);
	LiveEdict(&ents[DOOR_INDEX + 3], DOOR_INDEX + 3,
	    "misc_teleporter_dest");
	ents[DOOR_INDEX + 3].solid = SOLID_BBOX;
	SetLinkedBounds(&ents[DOOR_INDEX + 3]);
	immutable_test_support = &ents[DOOR_INDEX + 3];
	population_direct_index = 1;
	population_owned_index = DOOR_INDEX + 2;
	population_foreign_index = DOOR_INDEX + 1;
	population_immutable_index = DOOR_INDEX + 3;
	sg_host.trace = PopulationNativeTrace;
	Set3(source, -32.125f, 495.875f, 44.125f);
	Set3(trace_end, 40.0f, 495.875f, 44.125f);
	memset(&expected_surface, 0, sizeof(expected_surface));
	expected_surface.flags = SURF_SLICK;
	memset(&expected_trace, 0, sizeof(expected_trace));
	expected_trace.fraction = 0.375f;
	expected_trace.ent = immutable_test_support;
	expected_trace.surface = &expected_surface;
	expected_trace.contents = CONTENTS_MONSTER;
	expected_trace.plane.normal[2] = 1.0f;
	expected_trace.plane.dist = 17.25f;
	Set3(expected_trace.endpos, -5.078125f, 495.875f, 44.125f);
	population_trace_result = expected_trace;
	CHECK(SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	CHECK(population_trace_calls == 1 &&
	      population_trace_mask == MASK_PLAYERSOLID);
	CHECK(population_direct_seen == SOLID_NOT &&
	      population_owned_seen == SOLID_NOT);
	CHECK(population_foreign_seen == SOLID_BBOX &&
	      population_immutable_seen == SOLID_BBOX);
	CHECK(ents[1].solid == SOLID_BBOX &&
	      ents[DOOR_INDEX + 2].solid == SOLID_BBOX);
	CHECK(memcmp(&stable_trace, &expected_trace,
	             sizeof(stable_trace)) == 0);

	/* A native foreign winner is deterministic contamination, while a foreign
	 * BBOX behind the native immutable/BSP winner needs no reconstruction and
	 * cannot override that engine result. */
	population_trace_result.ent = &ents[DOOR_INDEX + 1];
	CHECK(!SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	population_trace_result = expected_trace;
	CHECK(SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));

	/* A malicious host cannot smuggle a masked client winner back after the
	 * restore.  Ordinary generation/live traces perform no masking and retain
	 * the native foreign collision for their caller to handle normally. */
	population_trace_result.ent = &ents[1];
	CHECK(!SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	population_trace_result.ent = &ents[DOOR_INDEX + 1];
	CHECK(SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, false, &stable_trace));
	CHECK(population_direct_seen == SOLID_BBOX &&
	      population_owned_seen == SOLID_BBOX);

	/* Native trace recursion is forbidden while the temporary publication is
	 * visible.  The outer trace still restores both solids exactly. */
	memset(&population_trace_result, 0, sizeof(population_trace_result));
	population_trace_result.fraction = 1.0f;
	VectorCopy(trace_end, population_trace_result.endpos);
	population_trace_calls = 0;
	population_reenter = true;
	population_reenter_independent = true;
	CHECK(SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	CHECK(population_trace_calls == 1 && !population_nested_result);
	CHECK(ents[1].solid == SOLID_BBOX &&
	      ents[DOOR_INDEX + 2].solid == SOLID_BBOX);
	population_trace_calls = 0;
	population_reenter = true;
	population_reenter_independent = false;
	CHECK(SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	CHECK(population_trace_calls == 1 && !population_nested_result);
	CHECK(ents[1].solid == SOLID_BBOX &&
	      ents[DOOR_INDEX + 2].solid == SOLID_BBOX);

	/* Malformed pre-trace identity publishes nothing.  If an adversarial trace
	 * mutates identity while the scope is active, the owned solid field is
	 * nevertheless restored before the call fails closed. */
	population_trace_calls = 0;
	ents[1].s.number = 13;
	CHECK(!SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	CHECK(population_trace_calls == 0 && ents[1].solid == SOLID_BBOX &&
	      ents[DOOR_INDEX + 2].solid == SOLID_BBOX);
	ents[1].s.number = 1;
	population_mutate_identity = true;
	CHECK(!SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	CHECK(ents[1].solid == SOLID_BBOX &&
	      ents[DOOR_INDEX + 2].solid == SOLID_BBOX);
	population_mutate_identity = false;
	ents[1].s.number = 1;
	population_mutate_owner = true;
	CHECK(!SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	CHECK(ents[1].solid == SOLID_BBOX &&
	      ents[DOOR_INDEX + 2].solid == SOLID_BBOX);
	population_mutate_owner = false;
	ents[1].owner = NULL;
	population_mutate_area = true;
	CHECK(!SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	CHECK(ents[1].solid == SOLID_BBOX &&
	      ents[DOOR_INDEX + 2].solid == SOLID_BBOX);
	population_mutate_area = false;
	ents[1].area.prev = &ents[0].area;
	population_mutate_base = true;
	CHECK(!SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	CHECK(ents[1].solid == SOLID_BBOX &&
	      ents[DOOR_INDEX + 2].solid == SOLID_BBOX);
	population_mutate_base = false;
	g_edicts = ents;
	ents[1].client = &clients[1];
	population_trace_calls = 0;
	CHECK(!SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	CHECK(population_trace_calls == 0 && ents[1].solid == SOLID_BBOX);
	ents[1].client = &clients[0];
	ents[DOOR_INDEX + 2].owner = &ents[TEST_EDICTS - 1];
	CHECK(!SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	CHECK(ents[1].solid == SOLID_BBOX &&
	      ents[DOOR_INDEX + 2].solid == SOLID_BBOX);
	ents[DOOR_INDEX + 2].owner = &ents[2];
	/* Provenance is rejected before solid kind: a malformed client-shaped BSP
	 * returned by a hostile trace cannot bypass the transient-winner gate. */
	ents[1].solid = SOLID_BSP;
	population_trace_result.ent = &ents[1];
	population_trace_result.fraction = 0.5f;
	CHECK(!SG_OracleStablePopulationTrace(source, trace_mins, trace_maxs,
	    trace_end, NULL, true, &stable_trace));
	ents[1].solid = SOLID_BBOX;
	globals.num_edicts = MAX_EDICTS + 1;
	CHECK(!SG_OracleButtonCarryClear(button, source, anchor, true));
	globals.num_edicts = HOOK_INDEX + 1;
	contact_button = NULL;
	memset(&sg_host, 0, sizeof(sg_host));
}

static void TestTimedVaultTopPoseRetainsExactClosure(void)
{
	rune_mechanism_node_t nodes[14];
	uint32_t index;

	memset(nodes, 0, sizeof(nodes));
	memset(&timed_vault_catalog, 0, sizeof(timed_vault_catalog));
	memset(&timed_vault_witness, 0, sizeof(timed_vault_witness));
	for (index = 0U; index < 14U; index++)
		nodes[index].key = index + 1U;
	timed_vault_catalog.nodes = nodes;
	timed_vault_catalog.num_nodes = 14U;
	timed_vault_witness.entry_key = 1U;
	timed_vault_witness.mover_key = 2U;
	timed_vault_witness.member_key = 3U;
	timed_vault_witness.short_relay_key = 4U;
	timed_vault_witness.restore_relay_key = 5U;
	for (index = 0U; index < 9U; index++)
		timed_vault_witness.effect_keys[index] = index + 6U;

	/* The sealed BOTTOM pose validates all 14 live entities. */
	timed_vault_execution_fail_key = 0U;
	timed_vault_execution_calls = 0U;
	CHECK(SG_OracleTimedVaultClosureCurrent(&timed_vault_catalog,
	    &timed_vault_witness, false));
	CHECK(timed_vault_execution_calls == 14U);

	/* Generation has already authenticated the three physical movers at their
	 * exact TOP endpoints; only those poses may differ from the sealed state. */
	timed_vault_execution_fail_key = timed_vault_witness.entry_key;
	timed_vault_execution_calls = 0U;
	CHECK(SG_OracleTimedVaultClosureCurrent(&timed_vault_catalog,
	    &timed_vault_witness, true));
	CHECK(timed_vault_execution_calls == 11U);

	/* Both door leaves are in the same authenticated synchronous TOP pose. */
	timed_vault_execution_fail_key = timed_vault_witness.mover_key;
	timed_vault_execution_calls = 0U;
	CHECK(SG_OracleTimedVaultClosureCurrent(&timed_vault_catalog,
	    &timed_vault_witness, true));
	CHECK(timed_vault_execution_calls == 11U);
	timed_vault_execution_fail_key = timed_vault_witness.member_key;
	timed_vault_execution_calls = 0U;
	CHECK(SG_OracleTimedVaultClosureCurrent(&timed_vault_catalog,
	    &timed_vault_witness, true));
	CHECK(timed_vault_execution_calls == 11U);

	/* Every relay and effect in the remaining closure stays exact. */
	timed_vault_execution_fail_key = timed_vault_witness.short_relay_key;
	timed_vault_execution_calls = 0U;
	CHECK(!SG_OracleTimedVaultClosureCurrent(&timed_vault_catalog,
	    &timed_vault_witness, true));
	CHECK(timed_vault_execution_calls == 1U);

	/* Prevalidation never licenses a missing catalog node. */
	timed_vault_execution_fail_key = 0U;
	timed_vault_catalog.num_nodes = 13U;
	CHECK(!SG_OracleTimedVaultClosureCurrent(&timed_vault_catalog,
	    &timed_vault_witness, true));
	timed_vault_catalog.num_nodes = 14U;
}

static void TestStableGroundSource(void)
{
	vec3_t raw, stable;

	ResetWorld();
	ground_test_active = true;
	ground_test_blocked = false;
	ground_test_drift = false;
	ground_test_classification_drift = false;
	ground_test_waterlevel = 0;
	ground_test_watertype = 0;
	ground_test_snap_axis = 2;
	sg_host.trace = StableGroundTrace;
	sg_host.pmove = StableGroundPmove;
	sg_host.pointcontents = GroundPointContents;

	/* Positive floor epsilon truncates onto the solid boundary.  Initial snap
	 * must run before the post-snap overlap gate and choose the +1 q8 shell. */
	ground_test_boundary = 40.0f;
	Set3(raw, 0.0f, 0.0f, 40.03125f);
	CHECK(SG_OracleCanonicalGroundSource(raw, stable));
	CHECK(stable[0] == 0.0f && stable[1] == 0.0f && stable[2] == 40.125f);

	/* Signed truncation already puts a negative floor endpoint on the legal
	 * outward shell; it remains exact rather than being positive-ceiled. */
	ground_test_boundary = -168.0f;
	Set3(raw, -8.0f, 16.0f, -167.96875f);
	CHECK(SG_OracleCanonicalGroundSource(raw, stable));
	CHECK(stable[0] == -8.0f && stable[1] == 16.0f &&
	      stable[2] == -167.875f);

	ground_test_boundary = 12.0f;
	Set3(raw, 1.25f, -2.5f, 12.125f);
	CHECK(SG_OracleCanonicalGroundSource(raw, stable));
	CHECK(stable[0] == raw[0] && stable[1] == raw[1] &&
	      stable[2] == raw[2]);

	/* A slope/seam may need the engine's horizontal signed-q8 jitter rather
	 * than a positive-only Z ceiling. */
	ground_test_snap_axis = 0;
	ground_test_boundary = 3.0f;
	Set3(raw, 3.03125f, 4.0f, 12.125f);
	CHECK(SG_OracleCanonicalGroundSource(raw, stable));
	CHECK(stable[0] == 3.125f && stable[1] == 4.0f &&
	      stable[2] == 12.125f);
	ground_test_snap_axis = 2;
	ground_test_boundary = 12.0f;

	/* No clear initial-snap candidate models a floor/ceiling gap too tight for
	 * the standing hull and must fail closed. */
	ground_test_blocked = true;
	CHECK(!SG_OracleCanonicalGroundSource(raw, stable));
	ground_test_blocked = false;
	/* Canonicalization must not delete slick/conveyor/current sources.  Their
	 * subsequent zero-command drift and classification changes are recorded by
	 * Seed_SourceUnstable and gate only standstill actions. */
	ground_test_drift = true;
	CHECK(SG_OracleCanonicalGroundSource(raw, stable));
	ground_test_drift = false;
	ground_test_waterlevel = 2;
	ground_test_watertype = CONTENTS_WATER;
	CHECK(SG_OracleCanonicalGroundSource(raw, stable));
	ground_test_classification_drift = true;
	CHECK(SG_OracleCanonicalGroundSource(raw, stable));
	Set3(raw, -4096.0f, 0.0f, 12.125f);
	CHECK(!SG_OracleCanonicalGroundSource(raw, stable));
	Set3(raw, 4095.875f, 0.0f, 12.125f);
	CHECK(!SG_OracleCanonicalGroundSource(raw, stable));
	Set3(raw, 0.0f, 0.0f, 12.125f);
	sg_host.pmove = NULL;
	CHECK(!SG_OracleCanonicalGroundSource(raw, stable));
	sg_host.pmove = StableGroundPmove;
	sg_host.trace = NULL;
	CHECK(!SG_OracleCanonicalGroundSource(raw, stable));
	sg_host.trace = StableGroundTrace;
	sg_host.pointcontents = NULL;
	CHECK(!SG_OracleCanonicalGroundSource(raw, stable));
	sg_host.pointcontents = GroundPointContents;
	CHECK(!SG_OracleCanonicalGroundSource(NULL, stable));
	CHECK(!SG_OracleCanonicalGroundSource(raw, NULL));

	ground_test_active = false;
	ground_test_classification_drift = false;
	memset(&sg_host, 0, sizeof(sg_host));
}

static void RotatingPoint(edict_t *mover, const vec3_t local,
	int angle_axis, float angle, vec3_t point)
{
	vec3_t angles, forward, right, up;

	VectorCopy(mover->moveinfo.start_angles, angles);
	angles[angle_axis] = angle;
	AngleVectors(angles, forward, right, up);
	point[0] = mover->s.origin[0] + local[0] * forward[0] -
	           local[1] * right[0] + local[2] * up[0];
	point[1] = mover->s.origin[1] + local[0] * forward[1] -
	           local[1] * right[1] + local[2] * up[1];
	point[2] = mover->s.origin[2] + local[0] * forward[2] -
	           local[1] * right[2] + local[2] * up[2];
}

static void TestTranslationAndBoundary(void)
{
	edict_t *mover, *player, *body, *hook;

	ResetWorld();
	mover = TranslationMover();
	player = PlayerSubject(100.0f, 0.0f, 0.0f, MOVETYPE_WALK, 32.0f);
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));
	Set3(player->s.origin, 0.0f, 0.0f, 0.0f);
	SetLinkedBounds(player);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));

	/* The translating brush ends at X=64 with a local max of 8.  A player
	 * origin at 88 puts its -16 face exactly on that boundary: still unsafe. */
	Set3(player->s.origin, 88.0f, 0.0f, 0.0f);
	SetLinkedBounds(player);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->s.origin[0] = 88.125f;
	SetLinkedBounds(player);
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));

	/* Use the actual corpse height.  At the same origin the standing player
	 * reaches into the brush, while the copied body's -8 top remains below it. */
	player = PlayerSubject(0.0f, 0.0f, 0.0f, MOVETYPE_WALK, 32.0f);
	body = BodySubject(0.0f, 0.0f, 0.0f);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	CHECK(SG_MoverSubjectOutsideSweep(mover, body));
	player = PlayerSubject(0.0f, 0.0f, 0.0f, MOVETYPE_TOSS, -8.0f);
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));

	hook = HookSubject(72.0f, 0.0f, 4.0f, SOLID_BBOX);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	hook->s.origin[0] = 72.125f;
	SetLinkedBounds(hook);
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));
	hook->solid = SOLID_TRIGGER;
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));
}

static void TestRotatingAndSecretSweeps(void)
{
	edict_t *mover, *hook;

	ResetWorld();
	mover = TranslationMover();
	mover->classname = "func_door_rotating";
	Set3(mover->mins, 32.0f, -8.0f, -8.0f);
	Set3(mover->maxs, 64.0f, 8.0f, 8.0f);
	VectorClear(mover->moveinfo.start_origin);
	VectorClear(mover->moveinfo.end_origin);
	VectorClear(mover->moveinfo.start_angles);
	Set3(mover->moveinfo.end_angles, 0.0f, 90.0f, 0.0f);
	SetLinkedBounds(mover);
	hook = HookSubject(48.0f, 48.0f, 0.0f, SOLID_BBOX);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	Set3(hook->s.origin, -80.0f, -80.0f, 0.0f);
	SetLinkedBounds(hook);
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));

	ResetWorld();
	mover = TranslationMover();
	mover->use = door_secret_use;
	VectorClear(mover->moveinfo.end_origin);
	Set3(mover->pos1, 0.0f, 64.0f, 0.0f);
	Set3(mover->pos2, 64.0f, 64.0f, 0.0f);
	hook = HookSubject(64.0f, 64.0f, 4.0f, SOLID_BBOX);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	Set3(hook->s.origin, 100.0f, 100.0f, 4.0f);
	SetLinkedBounds(hook);
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));
}

static void TestProspectivePushSweep(void)
{
	edict_t *mover, *player, *hook;
	vec3_t local;

	/* At large level.time, Move_Final can leave a translating door one stock
	 * pusher step past its serialized end.  The complete nominal sweep says this
	 * player is clear; the exact next 10-unit quantized push does not. */
	ResetWorld();
	mover = TranslationMover();
	Set3(mover->moveinfo.end_origin, 15.0f, 0.0f, 0.0f);
	Set3(mover->s.origin, 15.0f, 0.0f, 0.0f);
	Set3(mover->velocity, 100.0f, 0.0f, 0.0f);
	mover->moveinfo.state = SG_PLAT_STATE_UP;
	mover->moveinfo.endfunc = door_hit_top;
	mover->think = Move_Final;
	mover->nextthink = 10.0f;
	SetLinkedBounds(mover);
	player = PlayerSubject(45.0f, 0.0f, 0.0f, MOVETYPE_WALK, 32.0f);
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	player->s.origin[0] = 49.0f;
	SetLinkedBounds(player);
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	player->s.origin[0] = 49.125f;
	SetLinkedBounds(player);
	CHECK(SG_MoverSubjectOutsideProspectivePush(mover, player));
	VectorClear(mover->velocity);
	CHECK(SG_MoverSubjectOutsideProspectivePush(mover, player));
	CHECK(SG_MoverProspectivePusherValid(mover));
	/* A successful push runs due think, and a rollback runs blocked.  Neither
	 * arbitrary callback may survive the same positive geometric proof. */
	mover->think = TestThink;
	mover->nextthink = 10.0f;
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	CHECK(!SG_MoverProspectivePusherValid(mover));
	mover->think = NULL;
	mover->nextthink = 0.0f;
	mover->blocked = TestBlocked;
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	CHECK(!SG_MoverProspectivePusherValid(mover));
	mover->blocked = door_blocked;
	mover->nextthink = NAN;
	CHECK(!SG_MoverProspectivePusherValid(mover));
	mover->nextthink = 0.0f;
	mover->moveinfo.endfunc = TestThink;
	CHECK(!SG_MoverProspectivePusherValid(mover));
	mover->moveinfo.endfunc = NULL;
	/* A canonical scheduled stock movement callback remains live. */
	mover->moveinfo.state = SG_PLAT_STATE_UP;
	mover->moveinfo.endfunc = door_hit_top;
	mover->think = Move_Final;
	mover->nextthink = 10.0f;
	CHECK(SG_MoverSubjectOutsideProspectivePush(mover, player));
	CHECK(SG_MoverProspectivePusherValid(mover));
	mover->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	mover->moveinfo.endfunc = NULL;
	mover->think = NULL;
	mover->nextthink = 0.0f;
	/* SV_Push moves riders without a broadphase/final-position rejection.  A
	 * stale but exact rider pointer therefore blocks even when geometry alone
	 * puts the subject far outside this step. */
	player->groundentity = mover;
	player->groundentity_linkcount = mover->linkcount;
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	player->groundentity = NULL;
	player = BodySubject(100.0f, 100.0f, 100.0f);
	player->groundentity = mover;
	player->groundentity_linkcount = mover->linkcount;
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	player->groundentity = NULL;
	player = PlayerSubject(49.125f, 0.0f, 0.0f, MOVETYPE_WALK, 32.0f);
	mover->prethink = TestPrethink;
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	CHECK(!SG_MoverProspectivePusherValid(mover));
	mover->prethink = NULL;
	mover->avelocity[YAW] = 1.0f;
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	CHECK(!SG_MoverProspectivePusherValid(mover));
	VectorClear(mover->avelocity);
	mover->velocity[0] = NAN;
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	CHECK(!SG_MoverProspectivePusherValid(mover));
	mover->velocity[0] = FLT_MAX;
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	CHECK(!SG_MoverProspectivePusherValid(mover));
	/* An earlier team leaf can push a human into the automatic trigger.  Stock
	 * human authority then reverses a DOWN team before a later leaf's SV_Push.
	 * Even though this later leaf is currently stopped, its one-frame reopen
	 * step must already cover the protected player. */
	VectorClear(mover->velocity);
	mover->moveinfo.state = SG_PLAT_STATE_DOWN;
	mover->moveinfo.endfunc = door_hit_bottom;
	mover->think = Move_Begin;
	mover->nextthink = 10.0f;
	mover->moveinfo.end_origin[0] = 64.0f;
	player->s.origin[0] = 45.0f;
	SetLinkedBounds(player);
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, player));
	CHECK(SG_MoverProspectivePusherValid(mover));
	player->s.origin[1] = 24.125f;
	SetLinkedBounds(player);
	CHECK(SG_MoverSubjectOutsideProspectivePush(mover, player));

	/* A rotating guard covers only this frame's angular interval.  A point on
	 * the immediate arc blocks, while a point on a later part of the legal full
	 * door sweep does not deadlock the current push. */
	ResetWorld();
	mover = TranslationMover();
	mover->classname = "func_door_rotating";
	Set3(mover->mins, 32.0f, -8.0f, -8.0f);
	Set3(mover->maxs, 64.0f, 8.0f, 8.0f);
	VectorClear(mover->moveinfo.start_origin);
	VectorClear(mover->moveinfo.end_origin);
	VectorClear(mover->moveinfo.start_angles);
	Set3(mover->moveinfo.end_angles, 0.0f, 90.0f, 0.0f);
	VectorClear(mover->s.angles);
	Set3(mover->avelocity, 0.0f, 100.0f, 0.0f);
	mover->moveinfo.state = SG_PLAT_STATE_UP;
	mover->moveinfo.endfunc = door_hit_top;
	mover->think = AngleMove_Final;
	mover->nextthink = 10.0f;
	SetLinkedBounds(mover);
	CHECK(SG_MoverProspectivePusherValid(mover));
	hook = HookSubject(0.0f, 0.0f, 0.0f, SOLID_BBOX);
	Set3(local, 48.0f, 0.0f, 0.0f);
	RotatingPoint(mover, local, YAW, 5.0f, hook->s.origin);
	SetLinkedBounds(hook);
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, hook));
	RotatingPoint(mover, local, YAW, 45.0f, hook->s.origin);
	SetLinkedBounds(hook);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	CHECK(SG_MoverSubjectOutsideProspectivePush(mover, hook));
	mover->avelocity[PITCH] = 1.0f;
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, hook));
	/* The same inter-member reopen is bounded on the legal rotating axis. */
	VectorClear(mover->avelocity);
	mover->mins[1] = -1.0f;
	mover->maxs[1] = 1.0f;
	mover->s.angles[YAW] = 10.0f;
	mover->moveinfo.state = SG_PLAT_STATE_DOWN;
	mover->moveinfo.endfunc = door_hit_bottom;
	mover->think = AngleMove_Begin;
	mover->nextthink = 10.0f;
	SetLinkedBounds(mover);
	RotatingPoint(mover, local, YAW, 15.0f, hook->s.origin);
	SetLinkedBounds(hook);
	CHECK(!SG_MoverSubjectOutsideProspectivePush(mover, hook));
	CHECK(SG_MoverProspectivePusherValid(mover));
}

static void TestRotatingBoundsRoundOutward(void)
{
	static const float samples[] = {
		0.0f, 22.5f, 45.0f, 89.875f, 90.0f,
		132.6239929f, 179.875f, 180.0f
	};
	int angle_axis, corner, sample;

	for (angle_axis = 0; angle_axis < 3; angle_axis++)
	{
		edict_t *mover, *hook;

		ResetWorld();
		mover = TranslationMover();
		mover->classname = "func_door_rotating";
		Set3(mover->mins, 32.0f, 38.0f, -81.0f);
		Set3(mover->maxs, 101.0f, 88.0f, 22.0f);
		VectorClear(mover->moveinfo.start_origin);
		VectorClear(mover->moveinfo.end_origin);
		VectorClear(mover->moveinfo.start_angles);
		VectorClear(mover->moveinfo.end_angles);
		mover->moveinfo.end_angles[angle_axis] = 180.0f;
		VectorClear(mover->s.angles);
		SetLinkedBounds(mover);
		hook = HookSubject(0.0f, 0.0f, 0.0f, SOLID_BBOX);

		for (corner = 0; corner < 8; corner++)
		{
			vec3_t local;

			local[0] = (corner & 1) ? mover->maxs[0] : mover->mins[0];
			local[1] = (corner & 2) ? mover->maxs[1] : mover->mins[1];
			local[2] = (corner & 4) ? mover->maxs[2] : mover->mins[2];
			for (sample = 0;
			     sample < (int)(sizeof(samples) / sizeof(samples[0]));
			     sample++)
			{
				RotatingPoint(mover, local, angle_axis, samples[sample],
				              hook->s.origin);
				SetLinkedBounds(hook);
				CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
			}
		}
	}

	/* Coefficient recovery used to lose two float steps against this large
	 * origin.  One nextafter still left the direct AngleVectors corner outside
	 * the supposedly inclusive partial-arc bound. */
	{
		edict_t *mover, *hook;
		vec3_t local;

		ResetWorld();
		mover = TranslationMover();
		mover->classname = "func_door_rotating";
		Set3(mover->s.origin, -6173.0f, 13968.0f, 7176.0f);
		Set3(mover->mins, 25.0f, -1869.0f, -252.0f);
		Set3(mover->maxs, 1427.0f, 3828.0f, 2660.0f);
		VectorClear(mover->moveinfo.start_origin);
		VectorClear(mover->moveinfo.end_origin);
		VectorClear(mover->moveinfo.start_angles);
		Set3(mover->moveinfo.end_angles, 0.0f, -230.0f, 0.0f);
		VectorClear(mover->s.angles);
		SetLinkedBounds(mover);
		hook = HookSubject(0.0f, 0.0f, 0.0f, SOLID_BBOX);
		Set3(local, 1427.0f, 3828.0f, -252.0f);
		RotatingPoint(mover, local, YAW, -69.5574875f, hook->s.origin);
		SetLinkedBounds(hook);
		CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	}

	/* Pose validation admits the bounded stock AngleMove final-roundoff past
	 * an endpoint.  At a large leaf radius that tiny angular sliver spans more
	 * than the generic numeric padding, so the sweep must explicitly include
	 * the authenticated current pose rather than stopping at the nominal end. */
	{
		edict_t *mover, *hook;
		vec3_t local;

		ResetWorld();
		mover = TranslationMover();
		mover->classname = "func_door_rotating";
		VectorClear(mover->mins);
		Set3(mover->maxs, 100000.0f, 0.0f, 0.0f);
		VectorClear(mover->moveinfo.start_origin);
		VectorClear(mover->moveinfo.end_origin);
		VectorClear(mover->moveinfo.start_angles);
		Set3(mover->moveinfo.end_angles, 0.0f, 200.0f, 0.0f);
		Set3(mover->s.angles, 0.0f, 200.0015f, 0.0f);
		SetLinkedBounds(mover);
		hook = HookSubject(0.0f, 0.0f, 0.0f, SOLID_BBOX);
		Set3(local, 100000.0f, 0.0f, 0.0f);
		RotatingPoint(mover, local, YAW, mover->s.angles[YAW],
		              hook->s.origin);
		SetLinkedBounds(hook);
		CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	}
}

static void TestInvalidIdentitiesFailClosed(void)
{
	edict_t *mover, *player, *hook;
	edict_t clone;

	ResetWorld();
	mover = TranslationMover();
	player = PlayerSubject(100.0f, 0.0f, 0.0f, MOVETYPE_WALK, 32.0f);
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));

	clone = *mover;
	CHECK(!SG_MoverSubjectOutsideSweep(&clone, player));
	clone = *player;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, &clone));
	ents[2] = *mover;
	ents[2].s.number = 2;
	ents[2].area.prev = &ents[0].area;
	ents[2].area.next = &ents[0].area;
	CHECK(!SG_MoverSubjectOutsideSweep(&ents[2], player));
	ents[4] = *player;
	ents[4].s.number = 4;
	ents[4].area.prev = &ents[0].area;
	ents[4].area.next = &ents[0].area;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, &ents[4]));
	player->client = &clients[1];
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->client = &clients[0];
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));
	mover->s.number = MOVER_INDEX + 1;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->s.number = MOVER_INDEX;
	player->s.number = 7;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->s.number = 1;

	mover->inuse = false;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->inuse = true;
	mover->linkcount = 0;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->linkcount = 1;
	mover->solid = SOLID_BBOX;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->solid = SOLID_BSP;
	mover->movetype = MOVETYPE_STOP;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->movetype = MOVETYPE_PUSH;
	mover->classname = "func_train";
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->classname = "func_door";
	mover->use = NULL;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->use = door_use;
	mover->mins[0] = NAN;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->mins[0] = -8.0f;
	mover->absmin[0] = mover->absmax[0] + 1.0f;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	SetLinkedBounds(mover);
	mover->s.origin[0] = 1000.0f;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->s.origin[0] = 0.0f;
	SetLinkedBounds(mover);
	mover->classname = "func_door_rotating";
	VectorClear(mover->moveinfo.start_angles);
	Set3(mover->moveinfo.end_angles, 0.0f, 90.0f, 0.0f);
	Set3(mover->s.angles, 0.0f, 180.0f, 0.0f);
	SetLinkedBounds(mover);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->classname = "func_door";
	VectorClear(mover->moveinfo.end_angles);
	VectorClear(mover->s.angles);
	SetLinkedBounds(mover);

	player->solid = SOLID_NOT;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->solid = SOLID_BBOX;
	player->classname = "item_armor_body";
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->classname = "player";
	player->movetype = MOVETYPE_NOCLIP;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->movetype = MOVETYPE_WALK;
	player->mins[0] = NAN;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->mins[0] = -16.0f;
	player->linkcount = 0;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->linkcount = 1;
	player->area.prev = NULL;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	SetLinkedBounds(player);
	player->size[0] += 0.125f;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	SetLinkedBounds(player);

	hook = HookSubject(100.0f, 0.0f, 4.0f, SOLID_BBOX);
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));
	hook->owner->client->hook = NULL;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	hook->owner->client->hook = hook;
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));
	hook->touch = NULL;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	hook->touch = hook_touch;
	hook->die = NULL;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	hook->die = hook_die;
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));

	globals.edicts = &ents[1];
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	globals.edicts = ents;
	globals.num_edicts = TEST_EDICTS + 1;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	globals.num_edicts = HOOK_INDEX + 1;
	globals.max_edicts = TEST_EDICTS - 1;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	globals.max_edicts = TEST_EDICTS;
	game.maxentities = 4;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
}

static void TestImmutableMapSupport(void)
{
	edict_t pedestal;
	edict_t wall;

	memset(&pedestal, 0, sizeof(pedestal));
	pedestal.inuse = true;
	pedestal.classname = "info_player_deathmatch";
	CHECK(SG_MoverSubjectSweepRealImmutableSupport(&pedestal));
	pedestal.classname = "info_player_start";
	CHECK(!SG_MoverSubjectSweepRealImmutableSupport(&pedestal));

	memset(&wall, 0, sizeof(wall));
	wall.inuse = true;
	wall.classname = "func_wall";
	wall.solid = SOLID_BSP;
	wall.movetype = MOVETYPE_PUSH;
	CHECK(SG_MoverSubjectSweepRealImmutableSupport(&wall));
	wall.spawnflags = 1;
	CHECK(!SG_MoverSubjectSweepRealImmutableSupport(&wall));
	wall.spawnflags = 0;
	wall.use = door_use;
	CHECK(!SG_MoverSubjectSweepRealImmutableSupport(&wall));
	wall.use = NULL;
	wall.solid = SOLID_NOT;
	CHECK(!SG_MoverSubjectSweepRealImmutableSupport(&wall));
}

int main(void)
{
	TestImmutableMapSupport();
	TestStableGroundSource();
	TestButtonContactAndEndpointTiming();
	TestTimedVaultTopPoseRetainsExactClosure();
	TestBoundDoorSiblingAliasReplay();
	TestDirectDoorShallowWadeParity();
	TestTimedVaultEgressControlLaw();
	TestTimedVaultExactCaptureConvergesUnderRealPmove();
	TestTimedVaultEightHopRouteReachesShoreWithinLease();
	TestTimedVaultTargetFacingRouteFromRealCandidateState();
	TestTranslationAndBoundary();
	TestRotatingAndSecretSweeps();
	TestProspectivePushSweep();
	TestRotatingBoundsRoundOutward();
	TestInvalidIdentitiesFailClosed();
	if (failures)
	{
		fprintf(stderr, "sg_mover_subject_sweep_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_mover_subject_sweep_test: ok");
	return 0;
}
