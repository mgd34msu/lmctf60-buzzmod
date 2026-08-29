#include "sg_bsp_entity_semantics.h"

#include "sg_bsp_entity_semantics_storage_internal.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef enum token_kind_e
{
	TOKEN_END = 0,
	TOKEN_TEXT,
	TOKEN_OPEN,
	TOKEN_CLOSE,
	TOKEN_ERROR
} token_kind_t;

typedef struct token_s
{
	token_kind_t kind;
	char *text;
} token_t;

typedef struct parser_s
{
	char *text;
	size_t length;
	size_t cursor;
} parser_t;

typedef struct key_value_s
{
	char *key;
	char *value;
} key_value_t;

typedef struct parsed_entity_s
{
	key_value_t *pairs;
	size_t pair_count;
	size_t pair_capacity;
} parsed_entity_t;

typedef struct parsed_entities_s
{
	parsed_entity_t *values;
	size_t count;
	size_t capacity;
	char *text;
} parsed_entities_t;

typedef struct work_record_s
{
	sg_bsp_entity_semantic_t semantic;
	const char *classname;
	const char *targetname;
	const char *target;
	const char *killtarget;
	const char *pathtarget;
	const char *team;
	const char *required_item;
	const char *destination_map;
} work_record_t;

typedef struct work_records_s
{
	work_record_t *values;
	size_t count;
	size_t capacity;
} work_records_t;

typedef struct work_edge_s
{
	sg_bsp_entity_semantic_edge_t semantic;
	const char *name;
} work_edge_t;

typedef struct work_edges_s
{
	work_edge_t *values;
	size_t count;
	size_t capacity;
} work_edges_t;

typedef struct string_storage_record_s
{
	const sg_bsp_entity_semantics_t *owner;
	const char *base;
	uint32_t capacity;
	struct string_storage_record_s *next;
} string_storage_record_t;

static string_storage_record_t *string_storage_records;

static string_storage_record_t *FindStringStorage(
	const sg_bsp_entity_semantics_t *semantics)
{
	string_storage_record_t *record;

	for (record = string_storage_records; record; record = record->next)
		if (record->owner == semantics)
			return record;
	return NULL;
}

int SG_BspEntitySemanticsStringStorageRegister(
	sg_bsp_entity_semantics_t *semantics)
{
	string_storage_record_t *record;

	if (!semantics)
		return 0;
	if (!semantics->string_bytes && !semantics->strings)
		return 1;
	if (!semantics->string_bytes || !semantics->strings ||
		FindStringStorage(semantics))
		return 0;
	record = malloc(sizeof(*record));
	if (!record)
		return 0;
	record->owner = semantics;
	record->base = semantics->strings;
	record->capacity = semantics->string_bytes;
	record->next = string_storage_records;
	string_storage_records = record;
	return 1;
}

int SG_BspEntitySemanticsStringStorageValid(
	const sg_bsp_entity_semantics_t *semantics)
{
	string_storage_record_t *record;

	if (!semantics)
		return 0;
	if (!semantics->string_bytes && !semantics->strings)
		return 1;
	record = FindStringStorage(semantics);
	return record && record->base == semantics->strings &&
		semantics->string_bytes <= record->capacity;
}

void SG_BspEntitySemanticsStringStorageForget(
	sg_bsp_entity_semantics_t *semantics)
{
	string_storage_record_t **link;

	if (!semantics)
		return;
	for (link = &string_storage_records; *link; link = &(*link)->next)
		if ((*link)->owner == semantics)
		{
			string_storage_record_t *record = *link;

			*link = record->next;
			free(record);
			return;
		}
}

enum
{
	HOST_TOKEN_CHARS = 128
};

static void SetError(sg_bsp_entity_semantics_error_t *error,
	sg_bsp_entity_semantics_error_code_t code, uint32_t entity,
	uint32_t detail)
{
	if (!error)
		return;
	error->code = code;
	error->entity_ordinal = entity;
	error->detail_ordinal = detail;
}

static int SizeMultiply(size_t left, size_t right, size_t *result)
{
	if (!result || (right && left > SIZE_MAX / right))
		return 0;
	*result = left * right;
	return 1;
}

static int GrowArray(void **values, size_t *capacity, size_t required,
	size_t element_size)
{
	size_t next;
	size_t bytes;
	void *grown;

	if (required <= *capacity)
		return 1;
	next = *capacity ? *capacity : 8U;
	while (next < required)
	{
		if (next > SIZE_MAX / 2U)
		{
			next = required;
			break;
		}
		next *= 2U;
	}
	if (!SizeMultiply(next, element_size, &bytes))
		return 0;
	grown = realloc(*values, bytes);
	if (!grown)
		return -1;
	*values = grown;
	*capacity = next;
	return 1;
}

static int AsciiEqualFold(const char *left, const char *right)
{
	unsigned char a;
	unsigned char b;

	if (!left || !right)
		return left == right;
	do
	{
		a = (unsigned char)*left++;
		b = (unsigned char)*right++;
		if (a >= 'A' && a <= 'Z')
			a = (unsigned char)(a + ('a' - 'A'));
		if (b >= 'A' && b <= 'Z')
			b = (unsigned char)(b + ('a' - 'A'));
		if (a != b)
			return 0;
	} while (a);
	return 1;
}

static void AsciiFold(char *text)
{
	unsigned char *cursor = (unsigned char *)text;

	while (cursor && *cursor)
	{
		if (*cursor >= 'A' && *cursor <= 'Z')
			*cursor = (unsigned char)(*cursor + ('a' - 'A'));
		cursor++;
	}
}

static token_t NextToken(parser_t *parser)
{
	token_t token = { TOKEN_END, NULL };
	size_t start;
	size_t read;
	size_t written;

	for (;;)
	{
		while (parser->cursor < parser->length &&
			(unsigned char)parser->text[parser->cursor] <= 32U)
			parser->cursor++;
		if (parser->cursor >= parser->length)
			return token;
		if (parser->text[parser->cursor] == '/' &&
			parser->cursor + 1U < parser->length &&
			parser->text[parser->cursor + 1U] == '/')
		{
			parser->cursor += 2U;
			while (parser->cursor < parser->length &&
				parser->text[parser->cursor] != '\n')
				parser->cursor++;
			continue;
		}
		if (parser->text[parser->cursor] == '/' &&
			parser->cursor + 1U < parser->length &&
			parser->text[parser->cursor + 1U] == '*')
		{
			parser->cursor += 2U;
			while (parser->cursor + 1U < parser->length &&
				!(parser->text[parser->cursor] == '*' &&
				parser->text[parser->cursor + 1U] == '/'))
				parser->cursor++;
			if (parser->cursor + 1U >= parser->length)
			{
				token.kind = TOKEN_ERROR;
				return token;
			}
			parser->cursor += 2U;
			continue;
		}
		break;
	}
	if (parser->text[parser->cursor] == '"')
	{
		start = parser->cursor + 1U;
		read = start;
		written = 0U;
		while (read < parser->length && parser->text[read] != '"')
		{
			if (written < HOST_TOKEN_CHARS - 1U)
				parser->text[start + written++] = parser->text[read];
			read++;
		}
		if (read >= parser->length)
		{
			token.kind = TOKEN_ERROR;
			return token;
		}
		parser->text[start + written] = '\0';
		parser->cursor = read + 1U;
		token.kind = TOKEN_TEXT;
		token.text = parser->text + start;
		return token;
	}
	start = parser->cursor;
	read = start;
	written = 0U;
	while (read < parser->length &&
		(unsigned char)parser->text[read] > 32U)
	{
		if (written < HOST_TOKEN_CHARS - 1U)
			parser->text[start + written++] = parser->text[read];
		read++;
	}
	parser->text[start + written] = '\0';
	parser->cursor = read;
	token.text = parser->text + start;
	if (!strcmp(token.text, "{"))
		token.kind = TOKEN_OPEN;
	else if (!strcmp(token.text, "}"))
		token.kind = TOKEN_CLOSE;
	else
		token.kind = TOKEN_TEXT;
	return token;
}

static int HostLiteralString(char *text)
{
	size_t read;
	size_t write = 0U;
	size_t length;

	if (!text)
		return 0;
	length = strlen(text);
	for (read = 0U; read < length; read++)
	{
		if (text[read] != '\\')
		{
			text[write++] = text[read];
			continue;
		}
		if (read + 1U >= length)
			return 0;
		read++;
		text[write++] = text[read] == 'n' ? '\n' : '\\';
	}
	text[write] = '\0';
	return 1;
}

static int HostLiteralKey(const char *key)
{
	static const char *const keys[] = {
		"classname", "model", "target", "targetname", "pathtarget",
		"deathtarget", "killtarget", "combattarget", "message", "team",
		"map", "noise", "item", "gravity", "sky", "nextmap"
	};
	size_t index;

	for (index = 0U; index < sizeof(keys) / sizeof(keys[0]); index++)
		if (!strcmp(key, keys[index]))
			return 1;
	return 0;
}

static void ParsedEntitiesDestroy(parsed_entities_t *entities)
{
	size_t index;

	if (!entities)
		return;
	for (index = 0; index < entities->count; index++)
		free(entities->values[index].pairs);
	free(entities->values);
	free(entities->text);
	memset(entities, 0, sizeof(*entities));
}

static int ParseEntities(const sg_bsp_world_t *world,
	parsed_entities_t *entities, sg_bsp_entity_semantics_error_t *error)
{
	parser_t parser;
	size_t logical_length;
	int growth;

	memset(entities, 0, sizeof(*entities));
	logical_length = 0U;
	while (logical_length < world->entity_byte_count &&
		world->entities[logical_length])
		logical_length++;
	if (logical_length < world->entity_byte_count)
	{
		size_t tail;

		for (tail = logical_length + 1U;
			tail < world->entity_byte_count; tail++)
			if (world->entities[tail])
			{
				SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT,
					UINT32_MAX, (uint32_t)tail);
				return 0;
			}
	}
	if (logical_length == SIZE_MAX)
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW,
			UINT32_MAX, UINT32_MAX);
		return 0;
	}
	entities->text = malloc(logical_length + 1U);
	if (!entities->text)
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
			UINT32_MAX, UINT32_MAX);
		return 0;
	}
	memcpy(entities->text, world->entities, logical_length);
	entities->text[logical_length] = '\0';
	parser.text = entities->text;
	parser.length = logical_length;
	parser.cursor = 0U;
	for (;;)
	{
		token_t opening = NextToken(&parser);
		parsed_entity_t *entity;

		if (opening.kind == TOKEN_END)
			return 1;
		if (opening.kind != TOKEN_OPEN)
			goto malformed;
		growth = GrowArray((void **)&entities->values, &entities->capacity,
			entities->count + 1U, sizeof(*entities->values));
		if (growth <= 0)
			goto growth_failed;
		entity = &entities->values[entities->count];
		memset(entity, 0, sizeof(*entity));
		entities->count++;
		if (entities->count > UINT32_MAX)
			goto overflow;
		for (;;)
		{
			token_t key = NextToken(&parser);
			token_t value;
			size_t previous;

			if (key.kind == TOKEN_CLOSE)
				break;
			if (key.kind != TOKEN_TEXT)
				goto malformed;
			value = NextToken(&parser);
			if (value.kind != TOKEN_TEXT)
				goto malformed;
			if (key.text[0] == '{' || key.text[0] == '}' ||
				value.text[0] == '{' || value.text[0] == '}')
				goto malformed;
			AsciiFold(key.text);
			if (key.text[0] == '_')
				continue;
			if (HostLiteralKey(key.text) && !HostLiteralString(value.text))
				goto malformed;
			for (previous = 0; previous < entity->pair_count; previous++)
				if (!strcmp(entity->pairs[previous].key, key.text))
				{
					SetError(error,
						SG_BSP_ENTITY_SEMANTICS_ERROR_DUPLICATE_KEY,
						(uint32_t)(entities->count - 1U),
						(uint32_t)previous);
					return 0;
				}
			growth = GrowArray((void **)&entity->pairs,
				&entity->pair_capacity, entity->pair_count + 1U,
				sizeof(*entity->pairs));
			if (growth <= 0)
				goto growth_failed;
			entity->pairs[entity->pair_count].key = key.text;
			entity->pairs[entity->pair_count].value = value.text;
			entity->pair_count++;
		}
	}

malformed:
	SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT,
		(uint32_t)entities->count, (uint32_t)parser.cursor);
	return 0;
overflow:
	SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW,
		UINT32_MAX, UINT32_MAX);
	return 0;
growth_failed:
	SetError(error, growth == 0
		? SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW
		: SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
		(uint32_t)entities->count, UINT32_MAX);
	return 0;
}

static const char *EntityValue(const parsed_entity_t *entity,
	const char *key)
{
	size_t index;

	for (index = 0; index < entity->pair_count; index++)
		if (!strcmp(entity->pairs[index].key, key))
			return entity->pairs[index].value;
	return NULL;
}

static int ParseFloatValue(const char *text, float *value_out)
{
	char *end;
	float value;

	if (!text || !value_out)
		return 0;
	errno = 0;
	value = strtof(text, &end);
	if (end == text || *end || errno == ERANGE || !isfinite(value))
		return 0;
	*value_out = value == 0.0f ? 0.0f : value;
	return 1;
}

static int ParseVector(const char *text, sg_rune_vec3_t *vector_out)
{
	char *end;
	const char *cursor;
	int axis;

	if (!text || !vector_out)
		return 0;
	cursor = text;
	for (axis = 0; axis < 3; axis++)
	{
		float value;

		while (*cursor == ' ' || *cursor == '\t')
			cursor++;
		errno = 0;
		value = strtof(cursor, &end);
		if (end == cursor || errno == ERANGE || !isfinite(value))
			return 0;
		vector_out->value[axis] = value == 0.0f ? 0.0f : value;
		cursor = end;
	}
	while (*cursor == ' ' || *cursor == '\t')
		cursor++;
	return *cursor == '\0';
}

static int ParseSpawnflags(const char *text, uint32_t *value_out)
{
	char *end;
	long value;

	if (!text)
	{
		*value_out = 0U;
		return 1;
	}
	errno = 0;
	value = strtol(text, &end, 10);
	if (end == text || *end || errno == ERANGE ||
		value < INT32_MIN || value > INT32_MAX)
		return 0;
	*value_out = (uint32_t)(int32_t)value;
	return 1;
}

static int ParseInt32Value(const char *text, int32_t *value_out)
{
	char *end;
	long value;

	if (!text || !value_out)
		return 0;
	errno = 0;
	value = strtol(text, &end, 10);
	if (end == text || *end || errno == ERANGE ||
		value < INT32_MIN || value > INT32_MAX)
		return 0;
	*value_out = (int32_t)value;
	return 1;
}

static int StringInTable(const char *text, const char *const *values,
	size_t value_count)
{
	size_t index;

	for (index = 0U; index < value_count; index++)
		if (!strcmp(text, values[index]))
			return 1;
	return 0;
}

static int RegisteredItemClass(const char *classname)
{
	static const char *const classes[] = {
		"item_armor_body", "item_armor_combat", "item_armor_jacket",
		"item_armor_shard", "item_power_screen", "item_power_shield",
		"weapon_blaster", "weapon_shotgun", "weapon_supershotgun",
		"weapon_machinegun", "weapon_chaingun", "ammo_grenades",
		"weapon_grenadelauncher", "weapon_rocketlauncher",
		"weapon_hyperblaster", "weapon_railgun", "weapon_bfg",
		"weapon_plasma", "ammo_shells", "ammo_bullets", "ammo_cells",
		"ammo_rockets", "ammo_slugs", "item_quad",
		"item_invulnerability", "item_silencer", "item_breather",
		"item_enviro", "item_ancient_head", "item_adrenaline",
		"item_bandolier", "item_pack", "key_data_cd", "key_power_cube",
		"key_pyramid", "key_data_spinner", "key_pass", "key_blue_key",
		"key_red_key", "key_commander_head", "key_airstrike_target",
		"flag", "weapon_hook", "damage_rune", "resist_rune",
		"haste_rune", "regen_rune", "vampire_rune"
	};

	return StringInTable(classname, classes,
		sizeof(classes) / sizeof(classes[0]));
}

static int ItemClass(const char *classname)
{
	static const char *const health[] = {
		"item_health", "item_health_small", "item_health_large",
		"item_health_mega"
	};

	return RegisteredItemClass(classname) || StringInTable(classname, health,
		sizeof(health) / sizeof(health[0]));
}

static int HostContextDestinationClass(const char *classname)
{
	static const char *const classes[] = {
		"light", "light_mine1", "light_mine2", "viewthing",
		"misc_explobox", "misc_banner", "misc_satellite_dish",
		"misc_gib_arm", "misc_gib_leg", "misc_gib_head",
		"misc_deadsoldier", "misc_bigviper", "misc_blackhole",
		"misc_eastertank", "misc_easterchick", "misc_easterchick2",
		"misc_ctf_banner", "misc_ctf_small_banner"
	};
	size_t index;

	if (ItemClass(classname))
		return 1;
	for (index = 0U; index < sizeof(classes) / sizeof(classes[0]); index++)
		if (!strcmp(classname, classes[index]))
			return 1;
	return 0;
}

static void ClassifyLandmark(const char *classname,
	sg_bsp_entity_semantic_t *semantic)
{
	static const char *const weapons[] = {
		"weapon_blaster", "weapon_shotgun", "weapon_supershotgun",
		"weapon_machinegun", "weapon_chaingun", "weapon_grenadelauncher",
		"weapon_rocketlauncher", "weapon_hyperblaster", "weapon_railgun",
		"weapon_bfg", "weapon_plasma", "weapon_hook"
	};
	static const char *const armor[] = {
		"item_armor_body", "item_armor_combat", "item_armor_jacket",
		"item_armor_shard"
	};
	static const char *const health[] = {
		"item_health", "item_health_small", "item_health_large",
		"item_health_mega"
	};
	static const char *const powerups[] = {
		"item_power_screen", "item_power_shield", "item_quad",
		"item_invulnerability", "item_silencer", "item_breather",
		"item_enviro", "item_ancient_head", "item_adrenaline",
		"damage_rune", "resist_rune", "haste_rune", "regen_rune",
		"vampire_rune"
	};
	static const char *const positions[] = {
		"info_player_start", "info_player_deathmatch", "info_player_coop",
		"info_player_intermission", "info_player_red", "info_player_team1",
		"info_player_blue", "info_player_team2", "info_position",
		"info_notnull"
	};

	if (!strcmp(classname, "info_flag_red") ||
		!strcmp(classname, "item_flag_team1"))
	{
		semantic->flags |= SG_BSP_ENTITY_HAS_LANDMARK |
			SG_BSP_ENTITY_FLAG_RED;
		semantic->landmark_kind = SG_RUNE_LANDMARK_FLAG_STAND;
	}
	else if (!strcmp(classname, "info_flag_blue") ||
		!strcmp(classname, "item_flag_team2"))
	{
		semantic->flags |= SG_BSP_ENTITY_HAS_LANDMARK |
			SG_BSP_ENTITY_FLAG_BLUE;
		semantic->landmark_kind = SG_RUNE_LANDMARK_FLAG_STAND;
	}
	else if (!strcmp(classname, "flag"))
	{
		semantic->flags |= SG_BSP_ENTITY_HAS_LANDMARK;
		semantic->landmark_kind = SG_RUNE_LANDMARK_FLAG_STAND;
	}
	else if (StringInTable(classname, weapons,
		sizeof(weapons) / sizeof(weapons[0])))
	{
		semantic->flags |= SG_BSP_ENTITY_HAS_LANDMARK;
		semantic->landmark_kind = SG_RUNE_LANDMARK_WEAPON;
	}
	else if (StringInTable(classname, armor,
		sizeof(armor) / sizeof(armor[0])))
	{
		semantic->flags |= SG_BSP_ENTITY_HAS_LANDMARK;
		semantic->landmark_kind = SG_RUNE_LANDMARK_ARMOR;
	}
	else if (StringInTable(classname, health,
		sizeof(health) / sizeof(health[0])))
	{
		semantic->flags |= SG_BSP_ENTITY_HAS_LANDMARK;
		semantic->landmark_kind = SG_RUNE_LANDMARK_HEALTH;
	}
	else if (StringInTable(classname, powerups,
		sizeof(powerups) / sizeof(powerups[0])))
	{
		semantic->flags |= SG_BSP_ENTITY_HAS_LANDMARK;
		semantic->landmark_kind = SG_RUNE_LANDMARK_POWERUP;
	}
	else if (RegisteredItemClass(classname))
	{
		semantic->flags |= SG_BSP_ENTITY_HAS_LANDMARK;
		semantic->landmark_kind = SG_RUNE_LANDMARK_ITEM;
	}
	else if (StringInTable(classname, positions,
		sizeof(positions) / sizeof(positions[0])))
	{
		semantic->flags |= SG_BSP_ENTITY_HAS_LANDMARK;
		semantic->landmark_kind = SG_RUNE_LANDMARK_DEFENSIVE_POSITION;
	}
}

static void SetMechanism(sg_bsp_entity_semantic_t *semantic,
	sg_rune_mechanism_kind_t kind, sg_mech_node_kind_t role)
{
	semantic->flags |= SG_BSP_ENTITY_HAS_MECHANISM |
		SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND;
	semantic->mechanism_kind = kind;
	semantic->mechanism_role = role;
}

static void SetMechanismRole(sg_bsp_entity_semantic_t *semantic,
	sg_mech_node_kind_t role)
{
	semantic->flags |= SG_BSP_ENTITY_HAS_MECHANISM;
	semantic->mechanism_role = role;
}

static void ClassifyMechanism(const parsed_entity_t *entity,
	const char *classname, sg_bsp_entity_semantic_t *semantic)
{
	if (!strcmp(classname, "func_door") ||
		!strcmp(classname, "func_door_secret"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_DOOR,
			!strcmp(classname, "func_door_secret")
				? SG_MECH_NODE_SECRET_DOOR : SG_MECH_NODE_DOOR_MASTER);
	else if (!strcmp(classname, "func_door_rotating"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_ROTATOR,
			SG_MECH_NODE_DOOR_MASTER);
	else if (!strcmp(classname, "func_water"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_DOOR,
			SG_MECH_NODE_DOOR_MASTER);
	else if (!strcmp(classname, "func_button"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_BUTTON,
			SG_MECH_NODE_BUTTON);
	else if (!strcmp(classname, "func_plat"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_LIFT,
			SG_MECH_NODE_PLATFORM);
	else if (!strcmp(classname, "func_train"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_TRAIN,
			SG_MECH_NODE_TRAIN);
	else if (!strcmp(classname, "func_rotating"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_ROTATOR,
			SG_MECH_NODE_OTHER_MOVER);
	else if (!strcmp(classname, "func_conveyor") ||
		!strcmp(classname, "func_object") ||
		!strcmp(classname, "func_explosive"))
		SetMechanismRole(semantic, SG_MECH_NODE_OTHER_MOVER);
	else if (!strcmp(classname, "trigger_push"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_PUSH,
			SG_MECH_NODE_PUSH);
	else if (!strcmp(classname, "misc_teleporter"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_TELEPORT,
			SG_MECH_NODE_TELEPORTER);
	else if (!strcmp(classname, "misc_teleporter_dest"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_TELEPORT,
			SG_MECH_NODE_TELEPORT_DEST);
	else if (!strcmp(classname, "path_corner"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_TRAIN,
			SG_MECH_NODE_PATH_CORNER);
	else if (!strcmp(classname, "trigger_elevator"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_LIFT,
			SG_MECH_NODE_ELEVATOR);
	else if (!strcmp(classname, "trigger_multiple") ||
		!strcmp(classname, "trigger_once"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_TRIGGER,
			SG_MECH_NODE_TRIGGER);
	else if (!strcmp(classname, "trigger_relay"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_TRIGGER,
			SG_MECH_NODE_RELAY);
	else if (!strcmp(classname, "func_timer") ||
		!strcmp(classname, "trigger_always") ||
		!strcmp(classname, "trigger_key") ||
		!strcmp(classname, "trigger_counter") ||
		!strcmp(classname, "func_clock") ||
		!strcmp(classname, "func_killbox") ||
		!strcmp(classname, "target_spawner") ||
		!strcmp(classname, "point_combat"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_TRIGGER,
			SG_MECH_NODE_CONTEXTUAL);
	else if (!strcmp(classname, "misc_viper") ||
		!strcmp(classname, "misc_strogg_ship"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_TRAIN,
			SG_MECH_NODE_TRAIN);
	else if (!strcmp(classname, "target_speaker"))
		SetMechanismRole(semantic, SG_MECH_NODE_TARGET_SPEAKER);
	else if (!strcmp(classname, "target_laser"))
		SetMechanismRole(semantic, SG_MECH_NODE_TARGET_LASER);
	else if (!strcmp(classname, "func_areaportal"))
		SetMechanismRole(semantic, SG_MECH_NODE_AREAPORTAL);
	else if (!strcmp(classname, "func_wall"))
		SetMechanismRole(semantic, SG_MECH_NODE_TOGGLE_WALL);
	else if (!strcmp(classname, "trigger_hurt"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_TRIGGER,
			SG_MECH_NODE_TRIGGER_HURT);
	else if (!strcmp(classname, "trigger_gravity") ||
		!strcmp(classname, "trigger_monsterjump"))
		SetMechanism(semantic, SG_RUNE_MECHANISM_TRIGGER,
			SG_MECH_NODE_OTHER_TRIGGER);
	else if (!strcmp(classname, "target_temp_entity") ||
		!strcmp(classname, "target_explosion") ||
		!strcmp(classname, "target_changelevel") ||
		!strcmp(classname, "target_secret") ||
		!strcmp(classname, "target_goal") ||
		!strcmp(classname, "target_splash") ||
		!strcmp(classname, "target_blaster") ||
		!strcmp(classname, "target_crosslevel_trigger") ||
		!strcmp(classname, "target_crosslevel_target") ||
		!strcmp(classname, "target_help") ||
		!strcmp(classname, "target_lightramp") ||
		!strcmp(classname, "target_earthquake") ||
		!strcmp(classname, "target_character") ||
		!strcmp(classname, "target_string") ||
		!strcmp(classname, "misc_satellite_dish") ||
		!strcmp(classname, "misc_viper_bomb"))
		SetMechanismRole(semantic, SG_MECH_NODE_CONTEXTUAL);
	else if (ItemClass(classname) &&
		(EntityValue(entity, "target") || EntityValue(entity, "killtarget")))
		SetMechanismRole(semantic, SG_MECH_NODE_CONTEXTUAL);
	else if (HostContextDestinationClass(classname) &&
		(EntityValue(entity, "targetname") || EntityValue(entity, "team")))
		SetMechanismRole(semantic, SG_MECH_NODE_CONTEXTUAL);
	if ((semantic->flags & SG_BSP_ENTITY_HAS_MECHANISM) &&
		(semantic->mechanism_role == SG_MECH_NODE_TRIGGER ||
		 semantic->mechanism_role == SG_MECH_NODE_PUSH ||
		 semantic->mechanism_role == SG_MECH_NODE_TRIGGER_HURT))
	{
		semantic->flags |= SG_BSP_ENTITY_HAS_LANDMARK |
			SG_BSP_ENTITY_TOUCH_ACTIVATED;
		semantic->landmark_kind = SG_RUNE_LANDMARK_TRIGGER;
	}
	if ((semantic->flags & SG_BSP_ENTITY_HAS_MECHANISM) &&
		(semantic->mechanism_role == SG_MECH_NODE_BUTTON ||
		 semantic->mechanism_role == SG_MECH_NODE_RELAY))
		semantic->flags |= SG_BSP_ENTITY_USE_ACTIVATED;
}

static int ParseBrushModel(const char *text, uint32_t model_count,
	uint32_t *model_out)
{
	char *end;
	const char *digit;
	unsigned long value;

	*model_out = SG_BSP_ENTITY_MODEL_NONE;
	if (!text || text[0] != '*')
		return 1;
	if (!text[1])
		return 0;
	for (digit = text + 1; *digit; digit++)
		if (*digit < '0' || *digit > '9')
			return 0;
	errno = 0;
	value = strtoul(text + 1, &end, 10);
	if (end == text + 1 || *end || errno == ERANGE || value == 0UL ||
		value >= model_count || value > UINT32_MAX)
		return 0;
	*model_out = (uint32_t)value;
	return 1;
}

static int ClassRequiresBrushModel(const char *classname)
{
	static const char *const classes[] = {
		"func_button", "func_conveyor", "func_door",
		"func_door_rotating", "func_door_secret", "func_explosive",
		"func_object", "func_plat", "func_rotating", "func_train",
		"func_wall", "func_water", "trigger_gravity", "trigger_hurt",
		"trigger_monsterjump", "trigger_multiple", "trigger_once",
		"trigger_push"
	};
	size_t index;

	for (index = 0; index < sizeof(classes) / sizeof(classes[0]); index++)
		if (!strcmp(classname, classes[index]))
			return 1;
	return 0;
}

static int FillAngles(const parsed_entity_t *entity,
	sg_bsp_entity_semantic_t *semantic, uint32_t source_ordinal,
	sg_bsp_entity_semantics_error_t *error)
{
	const char *angle = EntityValue(entity, "angle");
	const char *angles = EntityValue(entity, "angles");
	const double degrees_to_radians = 0.017453292519943295769;
	double pitch;
	double yaw;

	if (angle && angles)
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_AMBIGUOUS_IDENTITY,
			source_ordinal, UINT32_MAX);
		return 0;
	}
	if (angles)
	{
		if (!ParseVector(angles, &semantic->angles))
			goto invalid;
		semantic->flags |= SG_BSP_ENTITY_ANGLES_DEFINED;
	}
	else if (angle)
	{
		if (!ParseFloatValue(angle, &semantic->angles.value[1]))
			goto invalid;
		semantic->flags |= SG_BSP_ENTITY_ANGLES_DEFINED;
	}
	if (!(semantic->flags & SG_BSP_ENTITY_ANGLES_DEFINED))
	{
		semantic->move_direction.value[0] = 1.0f;
		return 1;
	}
	if (semantic->angles.value[0] == 0.0f &&
		semantic->angles.value[1] == -1.0f &&
		semantic->angles.value[2] == 0.0f)
	{
		semantic->move_direction.value[2] = 1.0f;
		return 1;
	}
	if (semantic->angles.value[0] == 0.0f &&
		semantic->angles.value[1] == -2.0f &&
		semantic->angles.value[2] == 0.0f)
	{
		semantic->move_direction.value[2] = -1.0f;
		return 1;
	}
	pitch = (double)semantic->angles.value[0] * degrees_to_radians;
	yaw = (double)semantic->angles.value[1] * degrees_to_radians;
	semantic->move_direction.value[0] = (float)(cos(pitch) * cos(yaw));
	semantic->move_direction.value[1] = (float)(cos(pitch) * sin(yaw));
	semantic->move_direction.value[2] = (float)-sin(pitch);
	if (!isfinite(semantic->move_direction.value[0]) ||
		!isfinite(semantic->move_direction.value[1]) ||
		!isfinite(semantic->move_direction.value[2]))
		goto invalid;
	return 1;

invalid:
	SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
		source_ordinal, UINT32_MAX);
	return 0;
}

static int FillOptionalFloat(const parsed_entity_t *entity, const char *key,
	float scale, float *value_out, uint32_t source_ordinal,
	sg_bsp_entity_semantics_error_t *error)
{
	const char *text = EntityValue(entity, key);
	float value;

	if (!text)
	{
		*value_out = 0.0f;
		return 1;
	}
	if (!ParseFloatValue(text, &value) || !isfinite(value * scale))
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
			source_ordinal, UINT32_MAX);
		return 0;
	}
	*value_out = value * scale;
	return 1;
}

static int FillOptionalInt(const parsed_entity_t *entity, const char *key,
	int32_t *value_out, uint32_t source_ordinal,
	sg_bsp_entity_semantics_error_t *error)
{
	const char *text = EntityValue(entity, key);

	if (!text)
	{
		*value_out = 0;
		return 1;
	}
	if (!ParseInt32Value(text, value_out))
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
			source_ordinal, UINT32_MAX);
		return 0;
	}
	return 1;
}

static int FillOptionalIntAsFloat(const parsed_entity_t *entity,
	const char *key, float *value_out, uint32_t source_ordinal,
	sg_bsp_entity_semantics_error_t *error)
{
	int32_t value;

	if (!FillOptionalInt(entity, key, &value, source_ordinal, error))
		return 0;
	*value_out = (float)value;
	return 1;
}

static int FillOptionalVector(const parsed_entity_t *entity, const char *key,
	sg_rune_vec3_t *value_out, uint32_t source_ordinal,
	sg_bsp_entity_semantics_error_t *error)
{
	const char *text = EntityValue(entity, key);

	if (!text)
	{
		memset(value_out, 0, sizeof(*value_out));
		return 1;
	}
	if (!ParseVector(text, value_out))
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
			source_ordinal, UINT32_MAX);
		return 0;
	}
	return 1;
}

static int ValidLightRamp(const parsed_entity_t *entity)
{
	const char *message = EntityValue(entity, "message");

	return EntityValue(entity, "target") && message && strlen(message) == 2U &&
		message[0] >= 'a' && message[0] <= 'z' &&
		message[1] >= 'a' && message[1] <= 'z' &&
		message[0] != message[1];
}

static int BuildWorldDeclaration(const parsed_entities_t *parsed,
	uint64_t source_set_identity, sg_bsp_world_entity_semantics_t *world,
	sg_bsp_entity_semantics_error_t *error)
{
	size_t index;
	uint32_t found = UINT32_MAX;

	memset(world, 0, sizeof(*world));
	world->source_set_identity = source_set_identity;
	world->source_entity_ordinal = UINT32_MAX;
	world->gravity = 800.0f;
	for (index = 0; index < parsed->count; index++)
	{
		const char *classname = EntityValue(&parsed->values[index], "classname");

		if (!classname || strcmp(classname, "worldspawn"))
			continue;
		if (found != UINT32_MAX)
		{
			SetError(error,
				SG_BSP_ENTITY_SEMANTICS_ERROR_AMBIGUOUS_IDENTITY,
				(uint32_t)index, found);
			return 0;
		}
		found = (uint32_t)index;
	}
	if (found == UINT32_MAX || found != 0U)
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_AMBIGUOUS_IDENTITY,
			found, 0U);
		return 0;
	}
	world->source_entity_ordinal = found;
	{
		const char *gravity = EntityValue(&parsed->values[found], "gravity");

		if (gravity)
		{
			if (!ParseFloatValue(gravity, &world->gravity))
			{
				SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
					found, UINT32_MAX);
				return 0;
			}
			world->flags |= SG_BSP_WORLD_GRAVITY_EXPLICIT;
		}
	}
	return 1;
}

static int BuildRecord(const sg_bsp_world_t *world,
	const parsed_entity_t *entity, uint32_t source_ordinal,
	uint64_t source_set_identity, uint8_t *used_models,
	work_record_t *record, sg_bsp_entity_semantics_error_t *error)
{
	const char *classname = EntityValue(entity, "classname");
	const char *origin = EntityValue(entity, "origin");
	const char *model = EntityValue(entity, "model");
	const char *wait = EntityValue(entity, "wait");
	const char *delay = EntityValue(entity, "delay");
	int axis;

	memset(record, 0, sizeof(*record));
	if (!classname || !classname[0])
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_AMBIGUOUS_IDENTITY,
			source_ordinal, UINT32_MAX);
		return 0;
	}
	if (!strcmp(classname, "worldspawn"))
		return 1;
	if (!strcmp(classname, "info_null") ||
		!strcmp(classname, "func_group") ||
		(!strcmp(classname, "misc_teleporter") &&
		 !EntityValue(entity, "target")) ||
		(!strcmp(classname, "path_corner") &&
		 !EntityValue(entity, "targetname")) ||
		(!strcmp(classname, "target_lightramp") &&
		 !ValidLightRamp(entity)) ||
		(!strcmp(classname, "target_help") &&
		 !EntityValue(entity, "message")) ||
		(!strcmp(classname, "target_changelevel") &&
		 !EntityValue(entity, "map")) ||
		(!strcmp(classname, "trigger_key") &&
		 (!EntityValue(entity, "target") || !EntityValue(entity, "item") ||
		  !RegisteredItemClass(EntityValue(entity, "item")))) ||
		((!strcmp(classname, "func_clock") ||
		  !strcmp(classname, "misc_viper") ||
		  !strcmp(classname, "misc_strogg_ship")) &&
		 !EntityValue(entity, "target")) ||
		(!strcmp(classname, "trigger_gravity") &&
		 !EntityValue(entity, "gravity")))
		return 1;
	record->classname = classname;
	record->targetname = EntityValue(entity, "targetname");
	record->target = EntityValue(entity, "target");
	record->killtarget = EntityValue(entity, "killtarget");
	record->pathtarget = EntityValue(entity, "pathtarget");
	record->team = EntityValue(entity, "team");
	record->required_item = EntityValue(entity, "item");
	record->destination_map = EntityValue(entity, "map");
	record->semantic.source_set_identity = source_set_identity;
	record->semantic.source_entity_ordinal = source_ordinal;
	record->semantic.classname = SG_BSP_ENTITY_STRING_NONE;
	record->semantic.targetname = SG_BSP_ENTITY_STRING_NONE;
	record->semantic.required_item = SG_BSP_ENTITY_STRING_NONE;
	record->semantic.spawned_classname = SG_BSP_ENTITY_STRING_NONE;
	record->semantic.destination_map = SG_BSP_ENTITY_STRING_NONE;
	record->semantic.bsp_model = SG_BSP_ENTITY_MODEL_NONE;
	record->semantic.landmark_kind = SG_RUNE_LANDMARK_KIND_COUNT;
	record->semantic.mechanism_kind = SG_RUNE_MECHANISM_KIND_COUNT;
	record->semantic.mechanism_role = SG_MECH_NODE_CONTEXTUAL;
	ClassifyLandmark(classname, &record->semantic);
	ClassifyMechanism(entity, classname, &record->semantic);
	if (!strcmp(classname, "trigger_push"))
		record->semantic.physics_kind = SG_BSP_ENTITY_PHYSICS_PUSH;
	else if (!strcmp(classname, "trigger_monsterjump"))
		record->semantic.physics_kind = SG_BSP_ENTITY_PHYSICS_MONSTER_JUMP;
	else if (!strcmp(classname, "trigger_gravity"))
		record->semantic.physics_kind = SG_BSP_ENTITY_PHYSICS_GRAVITY;
	else if (!strcmp(classname, "func_conveyor"))
		record->semantic.physics_kind = SG_BSP_ENTITY_PHYSICS_CONVEYOR;
	else if (!strcmp(classname, "trigger_hurt"))
		record->semantic.physics_kind = SG_BSP_ENTITY_PHYSICS_DAMAGE_VOLUME;
	else if (!strcmp(classname, "target_laser"))
		record->semantic.physics_kind = SG_BSP_ENTITY_PHYSICS_DAMAGE_BEAM;
	if (!strcmp(classname, "trigger_push") ||
		!strcmp(classname, "trigger_gravity") ||
		!strcmp(classname, "trigger_monsterjump") ||
		!strcmp(classname, "misc_teleporter"))
		record->semantic.flags |= SG_BSP_ENTITY_INITIALLY_ACTIVE;
	if (!strcmp(classname, "trigger_gravity") ||
		!strcmp(classname, "trigger_monsterjump"))
	{
		record->semantic.flags |= SG_BSP_ENTITY_HAS_LANDMARK |
			SG_BSP_ENTITY_TOUCH_ACTIVATED;
		record->semantic.landmark_kind = SG_RUNE_LANDMARK_TRIGGER;
	}
	if (!record->semantic.flags)
	{
		record->classname = NULL;
		return 1;
	}
	if (origin && !ParseVector(origin, &record->semantic.origin))
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
			source_ordinal, UINT32_MAX);
		return 0;
	}
	if (!ParseBrushModel(model, world->model_count,
		&record->semantic.bsp_model))
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_MODEL,
			source_ordinal, UINT32_MAX);
		return 0;
	}
	if (ClassRequiresBrushModel(classname) &&
		record->semantic.bsp_model == SG_BSP_ENTITY_MODEL_NONE)
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_MODEL,
			source_ordinal, UINT32_MAX);
		return 0;
	}
	if (record->semantic.bsp_model != SG_BSP_ENTITY_MODEL_NONE)
	{
		const sg_bsp_model_t *bsp_model =
			&world->models[record->semantic.bsp_model];

		if (used_models[record->semantic.bsp_model])
		{
			SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_DUPLICATE_MODEL,
				source_ordinal, record->semantic.bsp_model);
			return 0;
		}
		used_models[record->semantic.bsp_model] = 1U;
		record->semantic.flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL |
			SG_BSP_ENTITY_HAS_BOUNDS;
		for (axis = 0; axis < 3; axis++)
		{
			record->semantic.bounds.mins.value[axis] =
				bsp_model->mins.value[axis] +
				record->semantic.origin.value[axis];
			record->semantic.bounds.maxs.value[axis] =
				bsp_model->maxs.value[axis] +
				record->semantic.origin.value[axis];
			if (!isfinite(record->semantic.bounds.mins.value[axis]) ||
				!isfinite(record->semantic.bounds.maxs.value[axis]) ||
				record->semantic.bounds.mins.value[axis] >=
					record->semantic.bounds.maxs.value[axis])
			{
				SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
					source_ordinal, (uint32_t)axis);
				return 0;
			}
		}
	}
	if (!FillAngles(entity, &record->semantic, source_ordinal, error) ||
		!ParseSpawnflags(EntityValue(entity, "spawnflags"),
		&record->semantic.spawnflags) ||
		!FillOptionalFloat(entity, "delay", 1000.0f,
			&record->semantic.delay_ms, source_ordinal, error) ||
		!FillOptionalFloat(entity, "wait", 1000.0f,
			&record->semantic.dwell_ms, source_ordinal, error) ||
		!FillOptionalFloat(entity, "pausetime", 1000.0f,
			&record->semantic.pause_ms, source_ordinal, error) ||
		!FillOptionalFloat(entity, "speed", 1.0f,
			&record->semantic.speed, source_ordinal, error) ||
		!FillOptionalFloat(entity, "accel", 1.0f,
			&record->semantic.acceleration, source_ordinal, error) ||
		!FillOptionalFloat(entity, "decel", 1.0f,
			&record->semantic.deceleration, source_ordinal, error) ||
		!FillOptionalIntAsFloat(entity, "lip",
			&record->semantic.lip, source_ordinal, error) ||
		!FillOptionalIntAsFloat(entity, "height",
			&record->semantic.height, source_ordinal, error) ||
		!FillOptionalIntAsFloat(entity, "distance",
			&record->semantic.distance, source_ordinal, error) ||
		!FillOptionalFloat(entity, "random", 1.0f,
			&record->semantic.random, source_ordinal, error) ||
		!FillOptionalInt(entity, "dmg", &record->semantic.damage,
			source_ordinal, error) ||
		!FillOptionalInt(entity, "count", &record->semantic.count,
			source_ordinal, error) ||
		!FillOptionalInt(entity, "health", &record->semantic.health,
			source_ordinal, error) ||
		!FillOptionalInt(entity, "style", &record->semantic.style,
			source_ordinal, error) ||
		!FillOptionalVector(entity, "move_origin",
			&record->semantic.move_origin, source_ordinal, error) ||
		!FillOptionalVector(entity, "move_angles",
			&record->semantic.move_angles, source_ordinal, error))
	{
		if (error && error->code == SG_BSP_ENTITY_SEMANTICS_ERROR_NONE)
			SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
				source_ordinal, UINT32_MAX);
		return 0;
	}
	if (delay)
		record->semantic.flags |= SG_BSP_ENTITY_DELAY_DEFINED;
	if (wait)
		record->semantic.flags |= SG_BSP_ENTITY_DWELL_DEFINED;
	if (EntityValue(entity, "pausetime"))
		record->semantic.flags |= SG_BSP_ENTITY_PAUSE_DEFINED;
	if (EntityValue(entity, "speed"))
		record->semantic.flags |= SG_BSP_ENTITY_SPEED_DEFINED;
	if (EntityValue(entity, "accel"))
		record->semantic.flags |= SG_BSP_ENTITY_ACCELERATION_DEFINED;
	if (EntityValue(entity, "decel"))
		record->semantic.flags |= SG_BSP_ENTITY_DECELERATION_DEFINED;
	if (EntityValue(entity, "spawnflags"))
		record->semantic.flags |= SG_BSP_ENTITY_SPAWNFLAGS_DEFINED;
	if (EntityValue(entity, "lip"))
		record->semantic.flags |= SG_BSP_ENTITY_LIP_DEFINED;
	if (EntityValue(entity, "height"))
		record->semantic.flags |= SG_BSP_ENTITY_HEIGHT_DEFINED;
	if (EntityValue(entity, "distance"))
		record->semantic.flags |= SG_BSP_ENTITY_DISTANCE_DEFINED;
	if (EntityValue(entity, "random"))
		record->semantic.flags |= SG_BSP_ENTITY_RANDOM_DEFINED;
	if (EntityValue(entity, "dmg"))
		record->semantic.flags |= SG_BSP_ENTITY_DAMAGE_DEFINED;
	if (EntityValue(entity, "count"))
		record->semantic.flags |= SG_BSP_ENTITY_COUNT_DEFINED;
	if (EntityValue(entity, "health"))
		record->semantic.flags |= SG_BSP_ENTITY_HEALTH_DEFINED;
	if (EntityValue(entity, "style"))
		record->semantic.flags |= SG_BSP_ENTITY_STYLE_DEFINED;
	if (EntityValue(entity, "move_origin"))
		record->semantic.flags |= SG_BSP_ENTITY_MOVE_ORIGIN_DEFINED;
	if (EntityValue(entity, "move_angles"))
		record->semantic.flags |= SG_BSP_ENTITY_MOVE_ANGLES_DEFINED;
	if (EntityValue(entity, "gravity"))
	{
		int32_t gravity;

		if (!ParseInt32Value(EntityValue(entity, "gravity"), &gravity))
		{
			SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
				source_ordinal, UINT32_MAX);
			return 0;
		}
		record->semantic.gravity = (float)gravity;
		record->semantic.flags |= SG_BSP_ENTITY_GRAVITY_DEFINED;
	}
	else if (!strcmp(classname, "trigger_gravity"))
	{
		SetError(error, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE,
			source_ordinal, UINT32_MAX);
		return 0;
	}
	if (!strcmp(classname, "func_clock") &&
		(record->semantic.spawnflags & 2U) && record->semantic.count == 0)
	{
		record->classname = NULL;
		return 1;
	}
	if (!strcmp(classname, "trigger_key"))
	{
		record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED |
			SG_BSP_ENTITY_INVENTORY_GATED;
	}
	else if (!strcmp(classname, "trigger_always"))
		record->semantic.flags |= SG_BSP_ENTITY_AUTO_ACTIVATED;
	else if (!strcmp(classname, "trigger_counter") ||
		!strcmp(classname, "func_timer"))
		record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
	if (!strcmp(classname, "func_timer") &&
		(record->semantic.spawnflags & 1U))
		record->semantic.flags |= SG_BSP_ENTITY_AUTO_ACTIVATED |
			SG_BSP_ENTITY_INITIALLY_ACTIVE;
	if (!strcmp(classname, "func_water") ||
		!strcmp(classname, "func_areaportal") ||
		!strcmp(classname, "func_killbox"))
		record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
	if (!strcmp(classname, "func_object"))
		record->semantic.flags |= record->semantic.spawnflags & 1U
			? SG_BSP_ENTITY_USE_ACTIVATED
			: SG_BSP_ENTITY_AUTO_ACTIVATED |
				SG_BSP_ENTITY_INITIALLY_ACTIVE;
	if (!strcmp(classname, "func_explosive"))
	{
		record->semantic.flags |= record->targetname ||
			(record->semantic.spawnflags & 1U)
			? SG_BSP_ENTITY_USE_ACTIVATED : SG_BSP_ENTITY_DAMAGE_ACTIVATED;
		if (!(record->semantic.spawnflags & 1U))
			record->semantic.flags |= SG_BSP_ENTITY_INITIALLY_ACTIVE;
	}
	if (!strcmp(classname, "func_wall"))
	{
		if ((record->semantic.spawnflags & 7U) == 0U)
			record->semantic.flags |= SG_BSP_ENTITY_INITIALLY_ACTIVE;
		else
		{
			record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
			if (record->semantic.spawnflags & 4U)
				record->semantic.flags |= SG_BSP_ENTITY_INITIALLY_ACTIVE;
		}
	}
	if (!strcmp(classname, "func_conveyor"))
	{
		record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
		if (record->semantic.spawnflags & 1U)
			record->semantic.flags |= SG_BSP_ENTITY_INITIALLY_ACTIVE;
	}
	if (!strcmp(classname, "func_rotating") ||
		!strcmp(classname, "target_laser"))
	{
		record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
		if (record->semantic.spawnflags & 1U)
			record->semantic.flags |= SG_BSP_ENTITY_INITIALLY_ACTIVE;
	}
	if (!strcmp(classname, "trigger_hurt"))
	{
		if (!(record->semantic.spawnflags & 1U))
			record->semantic.flags |= SG_BSP_ENTITY_INITIALLY_ACTIVE;
		if (record->semantic.spawnflags & 2U)
			record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
	}
	if (!strcmp(classname, "trigger_multiple") ||
		!strcmp(classname, "trigger_once"))
	{
		uint32_t effective_spawnflags = record->semantic.spawnflags;

		if (!strcmp(classname, "trigger_once") &&
			(effective_spawnflags & 1U))
			effective_spawnflags = (effective_spawnflags & ~UINT32_C(1)) |
				UINT32_C(4);
		record->semantic.flags |= SG_BSP_ENTITY_TOUCH_ACTIVATED |
			SG_BSP_ENTITY_USE_ACTIVATED;
		if (!(effective_spawnflags & 4U))
			record->semantic.flags |= SG_BSP_ENTITY_INITIALLY_ACTIVE;
	}
	if (!strcmp(classname, "func_train"))
		record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
	if (!strcmp(classname, "misc_viper") ||
		!strcmp(classname, "misc_strogg_ship"))
		record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
	if (!strcmp(classname, "func_clock"))
	{
		if (record->semantic.spawnflags & 4U)
			record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
		else
			record->semantic.flags |= SG_BSP_ENTITY_AUTO_ACTIVATED |
				SG_BSP_ENTITY_INITIALLY_ACTIVE;
	}
	if (!strcmp(classname, "point_combat"))
		record->semantic.flags |= SG_BSP_ENTITY_TOUCH_ACTIVATED;
	if (record->semantic.mechanism_role == SG_MECH_NODE_DOOR_MASTER ||
		record->semantic.mechanism_role == SG_MECH_NODE_SECRET_DOOR)
	{
		record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
		if (record->semantic.mechanism_role == SG_MECH_NODE_SECRET_DOOR &&
			(!record->targetname || (record->semantic.spawnflags & 1U) ||
			 record->semantic.health))
			record->semantic.flags |= SG_BSP_ENTITY_DAMAGE_ACTIVATED;
		else if (record->semantic.health)
			record->semantic.flags |= SG_BSP_ENTITY_DAMAGE_ACTIVATED;
		else if (record->semantic.mechanism_role != SG_MECH_NODE_SECRET_DOOR &&
			!record->targetname && strcmp(classname, "func_water"))
			record->semantic.flags |= SG_BSP_ENTITY_TOUCH_ACTIVATED;
	}
	if (record->semantic.mechanism_role == SG_MECH_NODE_BUTTON)
	{
		if (record->semantic.health)
			record->semantic.flags |= SG_BSP_ENTITY_DAMAGE_ACTIVATED;
		else if (!record->targetname)
			record->semantic.flags |= SG_BSP_ENTITY_TOUCH_ACTIVATED;
	}
	if (record->semantic.mechanism_role == SG_MECH_NODE_PLATFORM ||
		record->semantic.mechanism_role == SG_MECH_NODE_TELEPORTER)
		record->semantic.flags |= SG_BSP_ENTITY_TOUCH_ACTIVATED;
	if (record->semantic.mechanism_role == SG_MECH_NODE_PLATFORM)
		record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
	return 1;
}

static int CompareOptionalString(const char *left, const char *right)
{
	if (!left || !right)
		return left ? 1 : right ? -1 : 0;
	return strcmp(left, right);
}

static int CompareFloat(float left, float right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int CompareVector(const sg_rune_vec3_t *left,
	const sg_rune_vec3_t *right)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		int comparison = CompareFloat(left->value[axis], right->value[axis]);

		if (comparison)
			return comparison;
	}
	return 0;
}

static int CompareSemanticKey(const sg_bsp_entity_semantic_t *left,
	const sg_bsp_entity_semantic_t *right)
{
	int comparison;

#define COMPARE_SCALAR(field) do { \
	if (left->field != right->field) \
		return left->field < right->field ? -1 : 1; \
} while (0)
	COMPARE_SCALAR(flags);
	COMPARE_SCALAR(bsp_model);
	COMPARE_SCALAR(landmark_kind);
	COMPARE_SCALAR(mechanism_kind);
	COMPARE_SCALAR(mechanism_role);
	COMPARE_SCALAR(physics_kind);
	comparison = CompareVector(&left->origin, &right->origin);
	if (comparison)
		return comparison;
	comparison = CompareVector(&left->angles, &right->angles);
	if (comparison)
		return comparison;
	comparison = CompareVector(&left->move_direction, &right->move_direction);
	if (comparison)
		return comparison;
	comparison = CompareVector(&left->move_origin, &right->move_origin);
	if (comparison)
		return comparison;
	comparison = CompareVector(&left->move_angles, &right->move_angles);
	if (comparison)
		return comparison;
	comparison = CompareVector(&left->bounds.mins, &right->bounds.mins);
	if (comparison)
		return comparison;
	comparison = CompareVector(&left->bounds.maxs, &right->bounds.maxs);
	if (comparison)
		return comparison;
	COMPARE_SCALAR(delay_ms);
	COMPARE_SCALAR(dwell_ms);
	COMPARE_SCALAR(pause_ms);
	COMPARE_SCALAR(speed);
	COMPARE_SCALAR(acceleration);
	COMPARE_SCALAR(deceleration);
	COMPARE_SCALAR(lip);
	COMPARE_SCALAR(height);
	COMPARE_SCALAR(distance);
	COMPARE_SCALAR(gravity);
	COMPARE_SCALAR(random);
	COMPARE_SCALAR(damage);
	COMPARE_SCALAR(count);
	COMPARE_SCALAR(health);
	COMPARE_SCALAR(style);
	COMPARE_SCALAR(spawnflags);
#undef COMPARE_SCALAR
	return 0;
}

static int CompareWorkRecords(const void *left_value, const void *right_value)
{
	const work_record_t *left = left_value;
	const work_record_t *right = right_value;
	int comparison;

	comparison = strcmp(left->classname, right->classname);
	if (comparison)
		return comparison;
	comparison = CompareSemanticKey(&left->semantic, &right->semantic);
	if (comparison)
		return comparison;
	comparison = CompareOptionalString(left->targetname, right->targetname);
	if (comparison)
		return comparison;
	comparison = CompareOptionalString(left->target, right->target);
	if (comparison)
		return comparison;
	comparison = CompareOptionalString(left->killtarget, right->killtarget);
	if (comparison)
		return comparison;
	comparison = CompareOptionalString(left->pathtarget, right->pathtarget);
	if (comparison)
		return comparison;
	comparison = CompareOptionalString(left->team, right->team);
	if (comparison)
		return comparison;
	comparison = CompareOptionalString(left->required_item,
		right->required_item);
	if (comparison)
		return comparison;
	comparison = CompareOptionalString(left->destination_map,
		right->destination_map);
	if (comparison)
		return comparison;
	if (left->semantic.source_entity_ordinal !=
		right->semantic.source_entity_ordinal)
		return left->semantic.source_entity_ordinal <
			right->semantic.source_entity_ordinal ? -1 : 1;
	return 0;
}

static int AddString(sg_bsp_entity_semantics_t *semantics,
	const char *text, uint32_t *offset_out)
{
	size_t offset;
	size_t length;
	char *grown;

	if (!text)
	{
		*offset_out = SG_BSP_ENTITY_STRING_NONE;
		return 1;
	}
	for (offset = 0; offset < semantics->string_bytes;
		offset += strlen(semantics->strings + offset) + 1U)
		if (!strcmp(semantics->strings + offset, text))
		{
			*offset_out = (uint32_t)offset;
			return 1;
		}
	length = strlen(text) + 1U;
	if (length > UINT32_MAX - semantics->string_bytes)
		return 0;
	grown = realloc(semantics->strings,
		(size_t)semantics->string_bytes + length);
	if (!grown)
		return -1;
	semantics->strings = grown;
	*offset_out = semantics->string_bytes;
	memcpy(semantics->strings + semantics->string_bytes, text, length);
	semantics->string_bytes += (uint32_t)length;
	return 1;
}

static int AddWorkRecord(work_records_t *records,
	const work_record_t *record)
{
	int growth = GrowArray((void **)&records->values, &records->capacity,
		records->count + 1U, sizeof(*records->values));

	if (growth <= 0)
		return growth;
	records->values[records->count++] = *record;
	return 1;
}

static int AddEdge(work_edges_t *edges, uint32_t source,
	uint32_t destination, sg_mech_edge_kind_t kind,
	uint32_t fanout_ordinal, const char *name)
{
	int growth = GrowArray((void **)&edges->values, &edges->capacity,
		edges->count + 1U, sizeof(*edges->values));

	if (growth <= 0)
		return growth;
	memset(&edges->values[edges->count], 0, sizeof(*edges->values));
	edges->values[edges->count].semantic.source = source;
	edges->values[edges->count].semantic.destination = destination;
	edges->values[edges->count].semantic.kind = kind;
	edges->values[edges->count].semantic.name = SG_BSP_ENTITY_STRING_NONE;
	edges->values[edges->count].semantic.fanout_ordinal = fanout_ordinal;
	edges->values[edges->count].name = name;
	edges->count++;
	return 1;
}

static int CompareEdges(const void *left_value, const void *right_value)
{
	const work_edge_t *left_work = left_value;
	const work_edge_t *right_work = right_value;
	const sg_bsp_entity_semantic_edge_t *left = &left_work->semantic;
	const sg_bsp_entity_semantic_edge_t *right = &right_work->semantic;

	if (left->source != right->source)
		return left->source < right->source ? -1 : 1;
	if (left->kind != right->kind)
		return left->kind < right->kind ? -1 : 1;
	if (left->destination != right->destination)
		return left->destination < right->destination ? -1 : 1;
	return 0;
}

typedef enum host_edge_policy_e
{
	HOST_EDGE_NONE = 0,
	HOST_EDGE_ALL,
	HOST_EDGE_FIRST,
	HOST_EDGE_PICK_EIGHT,
	HOST_EDGE_LAST_LIGHT
} host_edge_policy_t;

typedef struct host_entity_policy_s
{
	const char *classname;
	host_edge_policy_t target;
	host_edge_policy_t killtarget;
	host_edge_policy_t pathtarget;
} host_entity_policy_t;

static host_edge_policy_t HostEdgePolicy(const char *classname,
	sg_mech_edge_kind_t kind)
{
	static const host_entity_policy_t policies[] = {
		{ "func_button", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "func_door", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "func_door_rotating", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "func_door_secret", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "func_water", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "func_train", HOST_EDGE_PICK_EIGHT, HOST_EDGE_NONE, HOST_EDGE_NONE },
		{ "func_timer", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "func_explosive", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "func_clock", HOST_EDGE_FIRST, HOST_EDGE_ALL, HOST_EDGE_ALL },
		{ "trigger_always", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "trigger_multiple", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "trigger_once", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "trigger_relay", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "trigger_key", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "trigger_counter", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "trigger_elevator", HOST_EDGE_PICK_EIGHT, HOST_EDGE_NONE, HOST_EDGE_NONE },
		{ "path_corner", HOST_EDGE_PICK_EIGHT, HOST_EDGE_ALL, HOST_EDGE_ALL },
		{ "point_combat", HOST_EDGE_PICK_EIGHT, HOST_EDGE_ALL, HOST_EDGE_ALL },
		{ "misc_viper", HOST_EDGE_PICK_EIGHT, HOST_EDGE_NONE, HOST_EDGE_NONE },
		{ "misc_strogg_ship", HOST_EDGE_PICK_EIGHT, HOST_EDGE_NONE, HOST_EDGE_NONE },
		{ "misc_teleporter", HOST_EDGE_FIRST, HOST_EDGE_NONE, HOST_EDGE_NONE },
		{ "target_laser", HOST_EDGE_FIRST, HOST_EDGE_NONE, HOST_EDGE_NONE },
		{ "target_lightramp", HOST_EDGE_LAST_LIGHT, HOST_EDGE_NONE, HOST_EDGE_NONE },
		{ "target_explosion", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "target_secret", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "target_goal", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "target_crosslevel_target", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE },
		{ "misc_viper_bomb", HOST_EDGE_ALL, HOST_EDGE_ALL, HOST_EDGE_NONE }
	};
	size_t index;

	if (ItemClass(classname))
		return kind == SG_MECH_EDGE_PATH_TARGET
			? HOST_EDGE_NONE : HOST_EDGE_ALL;
	for (index = 0U; index < sizeof(policies) / sizeof(policies[0]); index++)
		if (!strcmp(classname, policies[index].classname))
		{
			if (kind == SG_MECH_EDGE_TARGET)
				return policies[index].target;
			if (kind == SG_MECH_EDGE_KILLTARGET)
				return policies[index].killtarget;
			return policies[index].pathtarget;
		}
	return HOST_EDGE_NONE;
}

static int BuildNamedEdges(const parsed_entities_t *parsed,
	const work_records_t *records, const uint32_t *entity_to_record,
	work_edges_t *edges, sg_bsp_entity_semantics_error_t *error)
{
	size_t source_entity;

	for (source_entity = 0; source_entity < parsed->count; source_entity++)
	{
		uint32_t source;
		const parsed_entity_t *entity;
		int key_index;

		if (entity_to_record[source_entity] == UINT32_MAX)
			continue;
		source = entity_to_record[source_entity];
		entity = &parsed->values[source_entity];
		for (key_index = 0; key_index < 3; key_index++)
		{
			static const char *const keys[] = {
				"target", "killtarget", "pathtarget"
			};
			static const sg_mech_edge_kind_t kinds[] = {
				SG_MECH_EDGE_TARGET, SG_MECH_EDGE_KILLTARGET,
				SG_MECH_EDGE_PATH_TARGET
			};
			const char *key = keys[key_index];
			const char *name = EntityValue(entity, key);
			sg_mech_edge_kind_t kind = kinds[key_index];
			host_edge_policy_t policy = HostEdgePolicy(
				records->values[source].classname, kind);
			size_t destination_entity;
			size_t first_edge = edges->count;

			if (!name || policy == HOST_EDGE_NONE)
				continue;
			if (kind == SG_MECH_EDGE_KILLTARGET &&
				(!strcmp(records->values[source].classname, "func_clock") ||
				 !strcmp(records->values[source].classname, "path_corner") ||
				 !strcmp(records->values[source].classname, "point_combat")) &&
				!EntityValue(entity, "pathtarget"))
				continue;
			if (policy == HOST_EDGE_LAST_LIGHT)
			{
				size_t selected = SIZE_MAX;

				for (destination_entity = 0U;
					destination_entity < parsed->count; destination_entity++)
				{
					const char *targetname = EntityValue(
						&parsed->values[destination_entity], "targetname");
					const char *destination_class = EntityValue(
						&parsed->values[destination_entity], "classname");

					if (targetname && destination_class &&
						AsciiEqualFold(name, targetname) &&
						!strcmp(destination_class, "light") &&
						entity_to_record[destination_entity] != UINT32_MAX)
						selected = destination_entity;
				}
				if (selected != SIZE_MAX)
				{
					int added = AddEdge(edges, source,
						entity_to_record[selected], kind, 0U, name);

					if (added <= 0)
					{
						SetError(error, added == 0
							? SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW
							: SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
							(uint32_t)source_entity, UINT32_MAX);
						return 0;
					}
				}
				continue;
			}
			for (destination_entity = 0;
				destination_entity < parsed->count; destination_entity++)
			{
				const char *targetname = EntityValue(
					&parsed->values[destination_entity], "targetname");
				uint32_t destination;
				int added;
				uint32_t fanout_ordinal;

				if (!targetname || !AsciiEqualFold(name, targetname) ||
					entity_to_record[destination_entity] == UINT32_MAX)
					continue;
				destination = entity_to_record[destination_entity];
				if (destination == source &&
					(kind == SG_MECH_EDGE_TARGET ||
					 kind == SG_MECH_EDGE_PATH_TARGET) &&
					policy == HOST_EDGE_ALL &&
					records->values[source].semantic.delay_ms == 0.0f)
					continue;
				if (kind == SG_MECH_EDGE_TARGET &&
					(records->values[source].semantic.mechanism_role ==
						SG_MECH_NODE_DOOR_MASTER ||
					 records->values[source].semantic.mechanism_role ==
						SG_MECH_NODE_SECRET_DOOR) &&
					records->values[destination].semantic.mechanism_role ==
						SG_MECH_NODE_AREAPORTAL &&
					strcmp(records->values[source].classname, "func_water") &&
					records->values[source].semantic.delay_ms == 0.0f)
					continue;
				fanout_ordinal = (uint32_t)(edges->count - first_edge);
				if (policy == HOST_EDGE_PICK_EIGHT && fanout_ordinal == 8U)
					break;
				if (policy == HOST_EDGE_FIRST && fanout_ordinal == 1U)
					break;
				added = AddEdge(edges, source, destination, kind,
					fanout_ordinal, name);
				if (added <= 0)
				{
					SetError(error, added == 0
						? SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW
						: SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
						(uint32_t)source_entity, UINT32_MAX);
					return 0;
				}
			}
		}
	}
	return 1;
}

static int BuildTeamEdges(const parsed_entities_t *parsed,
	const uint32_t *entity_to_record, work_edges_t *edges,
	sg_bsp_entity_semantics_error_t *error)
{
	size_t source_entity;

	for (source_entity = 0; source_entity < parsed->count; source_entity++)
	{
		const char *team = EntityValue(&parsed->values[source_entity], "team");
		size_t destination_entity;
		uint32_t master = UINT32_MAX;
		int added;

		if (!team || entity_to_record[source_entity] == UINT32_MAX)
			continue;
		for (destination_entity = 0;
			destination_entity < parsed->count; destination_entity++)
		{
			const char *candidate = EntityValue(
				&parsed->values[destination_entity], "team");

			if (candidate && !strcmp(team, candidate) &&
				entity_to_record[destination_entity] != UINT32_MAX)
			{
				master = entity_to_record[destination_entity];
				break;
			}
		}
		if (master == UINT32_MAX)
			continue;
		if (entity_to_record[source_entity] == master)
			continue;
		added = AddEdge(edges, entity_to_record[source_entity], master,
			SG_MECH_EDGE_TEAM, 0U, team);
		if (added <= 0)
		{
			SetError(error, added == 0
				? SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW
				: SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
				(uint32_t)source_entity, UINT32_MAX);
			return 0;
		}
	}
	return 1;
}

static void ApplyResolvedActivation(work_records_t *records,
	const work_edges_t *edges)
{
	size_t record_index;

	for (record_index = 0U; record_index < records->count; record_index++)
	{
		work_record_t *record = &records->values[record_index];
		size_t edge_index;
		uint32_t targets = 0U;
		int all_targets_are_trains = 1;

		for (edge_index = 0U; edge_index < edges->count; edge_index++)
		{
			const sg_bsp_entity_semantic_edge_t *edge =
				&edges->values[edge_index].semantic;

			if (edge->source != record_index ||
				edge->kind != SG_MECH_EDGE_TARGET)
				continue;
			targets++;
			if (strcmp(records->values[edge->destination].classname,
				"func_train"))
				all_targets_are_trains = 0;
		}
		if ((!strcmp(record->classname, "func_train") ||
			 !strcmp(record->classname, "misc_viper") ||
			 !strcmp(record->classname, "misc_strogg_ship")) && targets &&
			(!record->targetname || (record->semantic.spawnflags & 1U)))
			record->semantic.flags |= SG_BSP_ENTITY_AUTO_ACTIVATED |
				SG_BSP_ENTITY_INITIALLY_ACTIVE;
		if (!strcmp(record->classname, "trigger_elevator") && targets &&
			all_targets_are_trains)
			record->semantic.flags |= SG_BSP_ENTITY_USE_ACTIVATED;
	}
}

int SG_BspEntitySemanticsBuild(const sg_bsp_world_t *world,
	uint64_t source_set_identity, sg_bsp_entity_semantics_t **semantics_out,
	sg_bsp_entity_semantics_error_t *error_out)
{
	parsed_entities_t parsed;
	work_records_t records = { 0 };
	work_edges_t edges = { 0 };
	sg_bsp_entity_semantics_t *result = NULL;
	uint8_t *used_models = NULL;
	uint32_t *entity_to_record = NULL;
	size_t index;
	size_t allocation_bytes;
	int success = 0;

	SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_NONE,
		UINT32_MAX, UINT32_MAX);
	if (!world || !world->entities || !world->entity_byte_count ||
		!world->models || !world->model_count || !semantics_out ||
		source_set_identity == 0 || source_set_identity == UINT64_MAX)
	{
		SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_ARGUMENT,
			UINT32_MAX, UINT32_MAX);
		return 0;
	}
	memset(&parsed, 0, sizeof(parsed));
	if (!ParseEntities(world, &parsed, error_out))
		goto done;
	used_models = calloc(world->model_count, sizeof(*used_models));
	if (!used_models)
	{
		SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
			UINT32_MAX, UINT32_MAX);
		goto done;
	}
	for (index = 0; index < parsed.count; index++)
	{
		work_record_t record;
		int added;

		if (!BuildRecord(world, &parsed.values[index], (uint32_t)index,
			source_set_identity, used_models, &record, error_out))
			goto done;
		if (!record.classname)
			continue;
		added = AddWorkRecord(&records, &record);
		if (added <= 0)
		{
			SetError(error_out, added == 0
				? SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW
				: SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
				(uint32_t)index, UINT32_MAX);
			goto done;
		}
	}
	if (records.count > UINT32_MAX)
	{
		SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW,
			UINT32_MAX, UINT32_MAX);
		goto done;
	}
	if (records.count > 1U)
		qsort(records.values, records.count, sizeof(*records.values),
			CompareWorkRecords);
	if (!SG_BspEntitySemanticsCountsRepresentable(records.count, 0U, 0U))
	{
		SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW,
			UINT32_MAX, UINT32_MAX);
		goto done;
	}
	if (!SizeMultiply(parsed.count, sizeof(*entity_to_record),
		&allocation_bytes))
	{
		SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW,
			UINT32_MAX, UINT32_MAX);
		goto done;
	}
	entity_to_record = malloc(allocation_bytes);
	if (parsed.count && !entity_to_record)
	{
		SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
			UINT32_MAX, UINT32_MAX);
		goto done;
	}
	for (index = 0; index < parsed.count; index++)
		entity_to_record[index] = UINT32_MAX;
	for (index = 0; index < records.count; index++)
	{
		records.values[index].semantic.canonical_ordinal = (uint32_t)index;
		entity_to_record[records.values[index].semantic.source_entity_ordinal] =
			(uint32_t)index;
	}
	if (!BuildNamedEdges(&parsed, &records, entity_to_record, &edges,
		error_out) || !BuildTeamEdges(&parsed, entity_to_record, &edges,
		error_out))
		goto done;
	ApplyResolvedActivation(&records, &edges);
	if (edges.count > UINT32_MAX)
	{
		SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW,
			UINT32_MAX, UINT32_MAX);
		goto done;
	}
	if (!SG_BspEntitySemanticsCountsRepresentable(
		records.count, edges.count, 0U))
	{
		SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW,
			UINT32_MAX, UINT32_MAX);
		goto done;
	}
	if (edges.count > 1U)
		qsort(edges.values, edges.count, sizeof(*edges.values), CompareEdges);
	result = calloc(1U, sizeof(*result));
	if (!result)
	{
		SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
			UINT32_MAX, UINT32_MAX);
		goto done;
	}
	result->source_set_identity = source_set_identity;
	if (!BuildWorldDeclaration(&parsed, source_set_identity, &result->world,
		error_out))
		goto done;
	result->entity_count = (uint32_t)records.count;
	result->edge_count = (uint32_t)edges.count;
	if (records.count)
	{
		result->entities = malloc(records.count * sizeof(*result->entities));
		if (!result->entities)
		{
			SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
				UINT32_MAX, UINT32_MAX);
			goto done;
		}
	}
	if (edges.count)
	{
		result->edges = malloc(edges.count * sizeof(*result->edges));
		if (!result->edges)
		{
			SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
				UINT32_MAX, UINT32_MAX);
			goto done;
		}
		for (index = 0; index < edges.count; index++)
			result->edges[index] = edges.values[index].semantic;
	}
	for (index = 0; index < records.count; index++)
	{
		result->entities[index] = records.values[index].semantic;
		int classname_added = AddString(result,
			records.values[index].classname,
			&result->entities[index].classname);
		int targetname_added = classname_added > 0
			? AddString(result, records.values[index].targetname,
				&result->entities[index].targetname) : classname_added;
		int item_added = targetname_added > 0
			? AddString(result, records.values[index].required_item,
				&result->entities[index].required_item) : targetname_added;
		int spawned_added = item_added > 0
			? AddString(result,
				!strcmp(records.values[index].classname, "target_spawner")
					? records.values[index].target : NULL,
				&result->entities[index].spawned_classname) : item_added;
		int map_added = spawned_added > 0
			? AddString(result,
				!strcmp(records.values[index].classname, "target_changelevel")
					? records.values[index].destination_map : NULL,
				&result->entities[index].destination_map) : spawned_added;

		if (classname_added <= 0 || targetname_added <= 0 || item_added <= 0 ||
			spawned_added <= 0 || map_added <= 0)
		{
			SetError(error_out, classname_added == 0 || targetname_added == 0 ||
				item_added == 0 || spawned_added == 0 || map_added == 0
				? SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW
				: SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
				(uint32_t)index, UINT32_MAX);
			goto done;
		}
	}
	for (index = 0; index < edges.count; index++)
	{
		int added = AddString(result, edges.values[index].name,
			&result->edges[index].name);

		if (added <= 0)
		{
			SetError(error_out, added == 0
				? SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW
				: SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
				result->entities[result->edges[index].source].source_entity_ordinal,
				(uint32_t)index);
			goto done;
		}
	}
	if (!SG_BspEntitySemanticsStringStorageRegister(result))
	{
		SetError(error_out, SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY,
			UINT32_MAX, UINT32_MAX);
		goto done;
	}
	*semantics_out = result;
	result = NULL;
	success = 1;

done:
	SG_BspEntitySemanticsDestroy(result);
	free(edges.values);
	free(records.values);
	free(entity_to_record);
	free(used_models);
	ParsedEntitiesDestroy(&parsed);
	return success;
}

void SG_BspEntitySemanticsDestroy(sg_bsp_entity_semantics_t *semantics)
{
	if (!semantics)
		return;
	SG_BspEntitySemanticsStringStorageForget(semantics);
	free(semantics->entities);
	free(semantics->edges);
	free(semantics->strings);
	free(semantics);
}

const char *SG_BspEntitySemanticsString(
	const sg_bsp_entity_semantics_t *semantics, uint32_t offset)
{
	if (!semantics || offset == SG_BSP_ENTITY_STRING_NONE ||
		!SG_BspEntitySemanticsStringStorageValid(semantics) ||
		offset >= semantics->string_bytes)
		return NULL;
	if (!memchr(semantics->strings + offset, '\0',
		(size_t)semantics->string_bytes - offset))
		return NULL;
	return semantics->strings + offset;
}

int SG_BspEntitySemanticsCountsRepresentable(size_t entity_count,
	size_t edge_count, size_t string_bytes)
{
	size_t bytes;

	return entity_count <= UINT32_MAX && edge_count <= UINT32_MAX &&
		string_bytes <= UINT32_MAX &&
		SizeMultiply(entity_count, sizeof(sg_bsp_entity_semantic_t), &bytes) &&
		SizeMultiply(edge_count, sizeof(sg_bsp_entity_semantic_edge_t), &bytes);
}

const char *SG_BspEntitySemanticsErrorString(
	sg_bsp_entity_semantics_error_code_t code)
{
	switch (code) {
	case SG_BSP_ENTITY_SEMANTICS_ERROR_NONE:
		return "no error";
	case SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT:
		return "malformed entity text";
	case SG_BSP_ENTITY_SEMANTICS_ERROR_DUPLICATE_KEY:
		return "duplicate entity key";
	case SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE:
		return "invalid entity value";
	case SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_MODEL:
		return "invalid brush model";
	case SG_BSP_ENTITY_SEMANTICS_ERROR_DUPLICATE_MODEL:
		return "duplicate brush model identity";
	case SG_BSP_ENTITY_SEMANTICS_ERROR_AMBIGUOUS_IDENTITY:
		return "ambiguous entity identity";
	case SG_BSP_ENTITY_SEMANTICS_ERROR_SIZE_OVERFLOW:
		return "entity semantics size overflow";
	case SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY:
		return "entity semantics allocation failed";
	}
	return "unknown entity semantics error";
}
