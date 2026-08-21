#include "sg_compound_oracle_fixture.h"

game_export_t globals;
game_locals_t game;
edict_t *g_edicts;
level_locals_t level;
sg_host_t sg_host;
cvar_t *sv_gravity;

int SG_RuneTestDoorCooldownGapMs(edict_t *trigger);

short SG_RuneProofGravity(void)
{
	return 800;
}

edict_t fixture_edicts[FIXTURE_EDICTS];
gclient_t fixture_clients[5];
cvar_t fixture_gravity;
fixture_config_t fixture_config;
fixture_observation_t fixture_observation;
int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

void Set3(vec3_t value, float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

void CTF_HookMuzzle(const vec3_t origin, float viewheight, int hand,
	const vec3_t forward, const vec3_t right, vec3_t start)
{
	vec3_t offset = { 8.0f, 8.0f, viewheight - 8.0f };

	if (hand == LEFT_HANDED)
		offset[1] = -offset[1];
	else if (hand == CENTER_HANDED)
		offset[1] = 0.0f;
	start[0] = origin[0] + forward[0] * offset[0] + right[0] * offset[1];
	start[1] = origin[1] + forward[1] * offset[0] + right[1] * offset[1];
	start[2] = origin[2] + forward[2] * offset[0] + right[2] * offset[1] +
	           offset[2];
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	int speed;

	VectorSubtract(bite, start, velocity);
	speed = (int)VectorLength(velocity);
	VectorNormalize(velocity);
	if (speed > 120)
		VectorScale(velocity, 800.0f, velocity);
	else if (speed > 100)
		VectorScale(velocity, speed * 5.0f, velocity);
	else if (speed > 80)
		VectorScale(velocity, speed * 4.0f, velocity);
	else if (speed > 40)
		VectorScale(velocity, speed * 3.0f, velocity);
	else if (speed > 20)
		VectorScale(velocity, speed * 2.0f, velocity);
	else if (speed > 10)
		VectorScale(velocity, (float)speed, velocity);
	return speed;
}

qboolean CommandZero(const usercmd_t *command)
{
	return command->forwardmove == 0 && command->sidemove == 0 &&
	       command->upmove == 0;
}

int SG_MechCatalogButtonEndpoints(uint32_t key,
	const rune_mechanism_node_t *node, const edict_t *entity,
	sg_mech_button_endpoints_t *endpoints_out)
{
	(void)key;
	(void)node;
	(void)entity;
	(void)endpoints_out;
	return 0;
}

int SG_MechCatalogButtonBottomEndpoints(uint32_t key,
	const rune_mechanism_node_t *node, const edict_t *entity,
	sg_mech_button_endpoints_t *endpoints_out)
{
	return SG_MechCatalogButtonEndpoints(key, node, entity, endpoints_out);
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

void button_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surface)
{
	(void)self; (void)other; (void)plane; (void)surface;
	fixture_observation.callback_calls++;
}

void button_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
	fixture_observation.callback_calls++;
}

int SG_RuneMechanismBindingCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	(void)binding;
	return 0;
}

int SG_RuneMechanismBindingTopologyCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	(void)binding;
	return 0;
}

int SG_RuneMechanismBindingMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out)
{
	(void)binding; (void)keys_out; (void)key_count_out;
	return 0;
}

int SG_RuneMechanismBindingTopologyMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out)
{
	(void)binding; (void)keys_out; (void)key_count_out;
	return 0;
}

edict_t *SG_RuneMechanismBindingResolveNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key)
{
	(void)binding; (void)key;
	return NULL;
}

edict_t *SG_RuneMechanismBindingResolveTopologyNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key)
{
	(void)binding; (void)key;
	return NULL;
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

void door_hit_top(edict_t *self)
{
	SG_MoverCompletionPublish(self, SG_MOVER_COMPLETION_TOP);
}
void door_hit_bottom(edict_t *self)
{
	SG_MoverCompletionPublish(self, SG_MOVER_COMPLETION_BOTTOM);
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
