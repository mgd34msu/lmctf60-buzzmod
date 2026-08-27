/* sg_rune_seed_game.h -- exact host-backed RUNE seed canonicalization. */
#pragma once

#include "sg_rune_mechanism_catalog.h"
#include "sg_rune_topology.h"

qboolean SG_RuneSeedGround(vec3_t candidate, vec3_t out);
typedef qboolean (*sg_rune_seed_surface_emit_fn)(void *context,
	const vec3_t origin, qboolean crouched);
qboolean SG_RuneSeedReadMapWorldBounds(const char *game_directory,
	const char *mapname, vec3_t mins, vec3_t maxs);
qboolean SG_RuneSeedScanWorldSurfaces(const vec3_t world_mins,
	const vec3_t world_maxs, sg_rune_seed_surface_emit_fn emit,
	void *context, uint64_t *scan_count);
qboolean SG_RuneSeedScanMapFaceAnchors(const char *game_directory,
	const char *mapname, sg_rune_seed_surface_emit_fn emit, void *context,
	uint64_t *scan_count);
qboolean SG_RuneSeedLocalContact(const vec3_t first, qboolean first_crouched,
	const vec3_t second, qboolean second_crouched);
qboolean SG_RuneSeedRecordBspOverlay(const rune_seed_t *seeds,
	const byte *crouched, int seed_count,
	const rune_mechanism_node_t *mechanisms, uint32_t mechanism_count,
	sg_rune_contact_ledger_t *ledger, uint64_t *pairs_examined,
	uint32_t *contacts_recorded);
int SG_RuneSeedSourceWaterlevel(vec3_t origin, int *watertype);
int SG_RuneSeedSourceWaterlevelPose(vec3_t origin, qboolean crouched,
	int *watertype);
qboolean SG_RuneSeedTriggerSafe(vec3_t origin);
qboolean SG_RuneSeedTriggerSafePose(vec3_t origin, qboolean crouched);
qboolean SG_RuneSeedSourceUnstable(vec3_t origin);
qboolean SG_RuneSeedSourceUnstablePose(vec3_t origin, qboolean crouched);
