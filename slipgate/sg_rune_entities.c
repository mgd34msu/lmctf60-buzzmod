#include "sg_rune_entities.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- text ---------------------------------------------------------------------- */

typedef struct cursor_s
{
	const char *at;
	const char *end;
} cursor_t;

static void SkipSpace(cursor_t *c)
{
	while (c->at < c->end && (*c->at == ' ' || *c->at == '\t' || *c->at == '\n' ||
		*c->at == '\r'))
		c->at++;
}

/* A quoted token into out (truncated to the buffer), or a bare one. */
static int Token(cursor_t *c, char *out, size_t bytes)
{
	size_t n = 0U;

	SkipSpace(c);
	if (c->at >= c->end)
		return 0;
	if (*c->at == '"')
	{
		c->at++;
		while (c->at < c->end && *c->at != '"')
		{
			if (n + 1U < bytes)
				out[n++] = *c->at;
			c->at++;
		}
		if (c->at < c->end)
			c->at++;
	}
	else
	{
		while (c->at < c->end && *c->at != ' ' && *c->at != '\t' && *c->at != '\n' &&
			*c->at != '\r' && *c->at != '{' && *c->at != '}' && *c->at != '"')
		{
			if (n + 1U < bytes)
				out[n++] = *c->at;
			c->at++;
		}
	}
	out[n] = 0;
	return 1;
}

/* A bounded copy that always terminates. */
static void Name(char *out, size_t bytes, const char *value)
{
	size_t length = strlen(value);

	if (length >= bytes)
		length = bytes - 1U;
	memcpy(out, value, length);
	out[length] = 0;
}

static int Vec3(const char *text, float out[3])
{
	char *rest = (char *)text;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		out[axis] = strtof(rest, &rest);
		if (!isfinite(out[axis]))
			return 0;
	}
	return 1;
}

/* ---- kinds --------------------------------------------------------------------- */

static uint32_t Kind(const char *classname)
{
	if (!strcmp(classname, "worldspawn"))
		return SG_RUNE_ENTITY_WORLDSPAWN;
	if (!strcmp(classname, "func_door") || !strcmp(classname, "func_door_rotating"))
		return SG_RUNE_ENTITY_DOOR;
	if (!strcmp(classname, "func_door_secret"))
		return SG_RUNE_ENTITY_SECRET_DOOR;
	if (!strcmp(classname, "func_plat"))
		return SG_RUNE_ENTITY_PLATFORM;
	if (!strcmp(classname, "func_button"))
		return SG_RUNE_ENTITY_BUTTON;
	if (!strcmp(classname, "func_train"))
		return SG_RUNE_ENTITY_TRAIN;
	if (!strcmp(classname, "path_corner"))
		return SG_RUNE_ENTITY_PATH_CORNER;
	if (!strcmp(classname, "trigger_multiple") || !strcmp(classname, "trigger_once"))
		return SG_RUNE_ENTITY_TRIGGER;
	if (!strcmp(classname, "trigger_relay") || !strcmp(classname, "target_relay") ||
		!strcmp(classname, "trigger_counter"))
		return SG_RUNE_ENTITY_RELAY;
	if (!strcmp(classname, "trigger_teleport") || !strcmp(classname, "misc_teleporter"))
		return SG_RUNE_ENTITY_TELEPORT_TRIGGER;
	if (!strcmp(classname, "misc_teleporter_dest") ||
		!strcmp(classname, "info_teleport_destination"))
		return SG_RUNE_ENTITY_TELEPORT_DEST;
	if (!strcmp(classname, "trigger_push"))
		return SG_RUNE_ENTITY_PUSH;
	if (!strcmp(classname, "trigger_hurt"))
		return SG_RUNE_ENTITY_HURT;
	if (!strncmp(classname, "item_flag_team", 14))
		return SG_RUNE_ENTITY_FLAG;
	if (!strncmp(classname, "item_", 5) || !strncmp(classname, "weapon_", 7) ||
		!strncmp(classname, "ammo_", 5))
		return SG_RUNE_ENTITY_ITEM;
	if (!strncmp(classname, "info_player_", 12))
		return SG_RUNE_ENTITY_SPAWN;
	return SG_RUNE_ENTITY_OTHER;
}

/* The game's movedir: "angle" -1 is up, -2 is down, otherwise a yaw; the
 * angles key overrides with its yaw when no angle is given. */
static void MoveDirection(float angle, int angle_set, const float angles[3],
	float out[3])
{
	float yaw;

	out[0] = out[1] = out[2] = 0.0f;
	if (angle_set && angle == -1.0f)
	{
		out[2] = 1.0f;
		return;
	}
	if (angle_set && angle == -2.0f)
	{
		out[2] = -1.0f;
		return;
	}
	yaw = angle_set ? angle : angles[1];
	out[0] = cosf(yaw * (float)M_PI / 180.0f);
	out[1] = sinf(yaw * (float)M_PI / 180.0f);
	if (fabsf(out[0]) < 1e-6f)
		out[0] = 0.0f;
	if (fabsf(out[1]) < 1e-6f)
		out[1] = 0.0f;
}

/* ---- parse --------------------------------------------------------------------- */

static int Grow(void **array, uint32_t *capacity, uint32_t need, size_t bytes)
{
	void *grown;
	uint32_t capacity_now = *capacity;

	if (need <= capacity_now)
		return 1;
	while (capacity_now < need)
		capacity_now = capacity_now ? capacity_now * 2U : 64U;
	grown = realloc(*array, (size_t)capacity_now * bytes);
	if (!grown)
		return 0;
	*array = grown;
	*capacity = capacity_now;
	return 1;
}

int SG_RuneEntitiesParse(const sg_rune_bsp_t *bsp, sg_rune_entities_t *out)
{
	cursor_t c;
	uint32_t capacity = 0U, link_capacity = 0U, index, other;
	char key[64], value[256];

	if (!out)
		return 0;
	memset(out, 0, sizeof(*out));
	if (!bsp || !bsp->entities)
		return 0;
	c.at = bsp->entities;
	c.end = bsp->entities + bsp->entity_bytes;
	for (;;)
	{
		sg_rune_entity_t record;
		float angle = 0.0f;
		int angle_set = 0;

		SkipSpace(&c);
		if (c.at >= c.end)
			break;
		if (*c.at != '{')
		{
			c.at++;
			continue;
		}
		c.at++;
		memset(&record, 0, sizeof(record));
		record.bmodel = -1;
		record.ordinal = out->count;
		for (;;)
		{
			SkipSpace(&c);
			if (c.at >= c.end)
				break;
			if (*c.at == '}')
			{
				c.at++;
				break;
			}
			if (!Token(&c, key, sizeof(key)) || !Token(&c, value, sizeof(value)))
				break;
			if (!strcmp(key, "classname"))
				Name(record.classname, sizeof(record.classname), value);
			else if (!strcmp(key, "targetname"))
				Name(record.targetname, sizeof(record.targetname), value);
			else if (!strcmp(key, "target"))
				Name(record.target, sizeof(record.target), value);
			else if (!strcmp(key, "pathtarget"))
				Name(record.pathtarget, sizeof(record.pathtarget), value);
			else if (!strcmp(key, "model") && value[0] == '*')
				record.bmodel = (int32_t)strtol(value + 1, NULL, 10);
			else if (!strcmp(key, "origin"))
				Vec3(value, record.origin);
			else if (!strcmp(key, "angles"))
				Vec3(value, record.angles);
			else if (!strcmp(key, "angle"))
			{
				angle = strtof(value, NULL);
				angle_set = 1;
			}
			else if (!strcmp(key, "speed"))
				record.speed = strtof(value, NULL);
			else if (!strcmp(key, "wait"))
				record.wait = strtof(value, NULL);
			else if (!strcmp(key, "lip"))
			{
				record.lip = strtof(value, NULL);
				record.lip_set = 1;
			}
			else if (!strcmp(key, "height"))
				record.height = strtof(value, NULL);
			else if (!strcmp(key, "health"))
				record.health = (int32_t)strtol(value, NULL, 10);
			else if (!strcmp(key, "dmg"))
				record.damage = (int32_t)strtol(value, NULL, 10);
			else if (!strcmp(key, "spawnflags"))
				record.spawnflags = (uint32_t)strtoul(value, NULL, 10);
		}
		record.kind = Kind(record.classname);
		if (angle_set)
			record.angles[1] = angle > -0.5f ? angle : record.angles[1];
		MoveDirection(angle, angle_set, record.angles, record.move_direction);
		if (record.bmodel >= 0 && (uint32_t)record.bmodel < bsp->model_count)
		{
			const sg_rune_bsp_model_t *model = &bsp->models[record.bmodel];
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
			{
				record.mins[axis] = model->mins[axis] + record.origin[axis];
				record.maxs[axis] = model->maxs[axis] + record.origin[axis];
			}
			record.has_bounds = 1;
		}
		else
			record.bmodel = -1;
		if (!Grow((void **)&out->records, &capacity, out->count + 1U, sizeof(record)))
		{
			SG_RuneEntitiesFree(out);
			return 0;
		}
		out->records[out->count++] = record;
	}
	/* Links: every entity that names a target links to each entity so
	 * named, in text order. */
	for (index = 0U; index < out->count; index++)
	{
		sg_rune_entity_t *record = &out->records[index];

		record->first_link = out->link_count;
		record->link_count = 0U;
		if (!record->target[0])
			continue;
		for (other = 0U; other < out->count; other++)
		{
			if (other == index || strcmp(out->records[other].targetname, record->target))
				continue;
			if (!Grow((void **)&out->links, &link_capacity, out->link_count + 1U,
				sizeof(uint32_t)))
			{
				SG_RuneEntitiesFree(out);
				return 0;
			}
			out->links[out->link_count++] = other;
			record->link_count++;
		}
	}
	return 1;
}

void SG_RuneEntitiesFree(sg_rune_entities_t *entities)
{
	if (!entities)
		return;
	free(entities->records);
	free(entities->links);
	memset(entities, 0, sizeof(*entities));
}

uint32_t SG_RuneEntitiesFind(const sg_rune_entities_t *entities,
	const char *name, uint32_t after)
{
	uint32_t index;

	if (!entities || !name || !name[0])
		return UINT32_MAX;
	for (index = after; index < entities->count; index++)
		if (!strcmp(entities->records[index].targetname, name))
			return index;
	return UINT32_MAX;
}

uint32_t SG_RuneEntitiesTargetedBy(const sg_rune_entities_t *entities,
	uint32_t destination, uint32_t after)
{
	uint32_t index, k;

	if (!entities)
		return UINT32_MAX;
	for (index = after; index < entities->count; index++)
	{
		const sg_rune_entity_t *record = &entities->records[index];

		for (k = 0U; k < record->link_count; k++)
			if (entities->links[record->first_link + k] == destination)
				return index;
	}
	return UINT32_MAX;
}

uint32_t SG_RuneEntitiesTargetOf(const sg_rune_entities_t *entities,
	uint32_t source)
{
	if (!entities || source >= entities->count ||
		entities->records[source].link_count == 0U)
		return UINT32_MAX;
	return entities->links[entities->records[source].first_link];
}
