/*
 * Inventory slots, as indices into the game's own itemlist in g_items.c.
 *
 * These were the stock Quake II numbers, and this mod is not stock: it adds
 * weapon_plasma at slot 18, which pushes every ammo type and every powerup one
 * place along. The armor and health entries were worse -- they were still the
 * Quake III numbers this botlib came from, pointing at the backpack and the
 * key items.
 *
 * Nothing complains when these are wrong. The bot simply reads a count from
 * the wrong slot, and reading ammunition that is not there is indistinguishable
 * from having none: a bot with a full rocket launcher scored itself as unarmed,
 * refused every fight, and spent the match retreating.
 */
// Q2 inventory slot definitions for bot weapon/item weight scripts.
// Indices match g_items.c itemlist[] in the Q2 game DLL.

// Weapons (inventory slot = itemlist index)
#define INVENTORY_BLASTER           7
#define INVENTORY_SHOTGUN           8
#define INVENTORY_SUPERSHOTGUN      9
#define INVENTORY_MACHINEGUN        10
#define INVENTORY_CHAINGUN          11
#define INVENTORY_GRENADELAUNCHER   13
#define INVENTORY_ROCKETLAUNCHER    14
#define INVENTORY_HYPERBLASTER      15
#define INVENTORY_RAILGUN           16
#define INVENTORY_BFG10K            17

// Ammo
#define INVENTORY_SHELLS            19
#define INVENTORY_BULLETS           20
#define INVENTORY_CELLS             21
#define INVENTORY_ROCKETS           22
#define INVENTORY_SLUGS             23
#define INVENTORY_GRENADES          12

// Powerups
#define INVENTORY_QUAD              24
#define INVENTORY_INVULNERABILITY   24
#define INVENTORY_SILENCER          25
#define INVENTORY_REBREATHER        26
#define INVENTORY_ENVIRONMENTSUIT   27

// Health items (tracked by type, not slot)
#define INVENTORY_HEALTH            42

// Armor
#define INVENTORY_ARMOR_JACKET      3
#define INVENTORY_ARMOR_COMBAT      2
#define INVENTORY_ARMOR_BODY        1
#define INVENTORY_ARMOR_SHARD       4

// Special bot-logic slots (set by adapter before weapon selection)
#define ENEMY_HORIZONTAL_DIST       200
#define ENEMY_HEIGHT                201
