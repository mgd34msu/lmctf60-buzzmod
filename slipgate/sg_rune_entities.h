/* Era-4 entities: the map's entity text as records.
 *
 * The entity lump is a list of { "key" "value" ... } blocks.  This reads
 * every block into one record: its class as a kind the RUNE cares about,
 * the names it has and targets, its brush model and that model's bounds
 * placed at its origin, its movement direction from its angle, and the
 * numbers movers are built from (speed, wait, lip, height, health,
 * spawnflags).  Links are resolved once: every entity whose target names
 * another's targetname has a link to it.  Nothing else is interpreted;
 * the mechanisms builder decides what a door, lift, button, teleporter,
 * push or train does with these. */
#ifndef SG_RUNE_ENTITIES_H
#define SG_RUNE_ENTITIES_H

#include <stdint.h>

#include "sg_rune_bsp.h"

#define SG_RUNE_ENTITY_NAME_BYTES 48

typedef enum sg_rune_entity_kind_e
{
	SG_RUNE_ENTITY_OTHER = 0,
	SG_RUNE_ENTITY_WORLDSPAWN,
	SG_RUNE_ENTITY_DOOR,             /* func_door, func_door_rotating */
	SG_RUNE_ENTITY_SECRET_DOOR,      /* func_door_secret */
	SG_RUNE_ENTITY_PLATFORM,         /* func_plat */
	SG_RUNE_ENTITY_BUTTON,           /* func_button */
	SG_RUNE_ENTITY_TRAIN,            /* func_train */
	SG_RUNE_ENTITY_PATH_CORNER,      /* path_corner */
	SG_RUNE_ENTITY_TRIGGER,          /* trigger_multiple, trigger_once */
	SG_RUNE_ENTITY_RELAY,            /* trigger_relay, target_* relays */
	SG_RUNE_ENTITY_TELEPORT_TRIGGER, /* trigger_teleport, misc_teleporter */
	SG_RUNE_ENTITY_TELEPORT_DEST,    /* misc_teleporter_dest, info_teleport_destination */
	SG_RUNE_ENTITY_PUSH,             /* trigger_push */
	SG_RUNE_ENTITY_HURT,             /* trigger_hurt */
	SG_RUNE_ENTITY_ITEM,             /* item_*, weapon_*, ammo_* */
	SG_RUNE_ENTITY_FLAG,             /* item_flag_team1/2 */
	SG_RUNE_ENTITY_SPAWN,            /* info_player_* */
	SG_RUNE_ENTITY_KIND_COUNT
} sg_rune_entity_kind_t;

typedef struct sg_rune_entity_s
{
	uint32_t kind;                /* sg_rune_entity_kind_t */
	uint32_t ordinal;             /* position in the text, worldspawn 0 */
	char classname[SG_RUNE_ENTITY_NAME_BYTES];
	char targetname[SG_RUNE_ENTITY_NAME_BYTES];
	char target[SG_RUNE_ENTITY_NAME_BYTES];
	char pathtarget[SG_RUNE_ENTITY_NAME_BYTES];
	int32_t bmodel;               /* "*N" model, or -1 */
	int has_bounds;               /* the bmodel's bounds at the origin */
	float origin[3];
	float angles[3];
	float mins[3], maxs[3];       /* world space, when has_bounds */
	float move_direction[3];      /* from "angle": -1 up, -2 down, else yaw */
	float speed;                  /* 0 when unset */
	float wait;                   /* seconds; 0 when unset */
	float lip;
	int lip_set;
	float height;
	int32_t health;
	int32_t damage;
	uint32_t spawnflags;
	uint32_t first_link;          /* into links: what this targets */
	uint32_t link_count;
} sg_rune_entity_t;

typedef struct sg_rune_entities_s
{
	sg_rune_entity_t *records;
	uint32_t count;
	uint32_t *links;              /* destination entity per link */
	uint32_t link_count;
} sg_rune_entities_t;

int SG_RuneEntitiesParse(const sg_rune_bsp_t *bsp, sg_rune_entities_t *out);
void SG_RuneEntitiesFree(sg_rune_entities_t *entities);

/* The first entity whose targetname is name, from after; UINT32_MAX. */
uint32_t SG_RuneEntitiesFind(const sg_rune_entities_t *entities,
	const char *name, uint32_t after);
/* The first entity that targets destination, from after; UINT32_MAX. */
uint32_t SG_RuneEntitiesTargetedBy(const sg_rune_entities_t *entities,
	uint32_t destination, uint32_t after);
/* The first link target of an entity, or UINT32_MAX. */
uint32_t SG_RuneEntitiesTargetOf(const sg_rune_entities_t *entities,
	uint32_t source);

#endif /* SG_RUNE_ENTITIES_H */
