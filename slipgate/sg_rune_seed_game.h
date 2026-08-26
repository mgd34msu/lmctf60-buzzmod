/* sg_rune_seed_game.h -- exact host-backed RUNE seed canonicalization. */
#pragma once

qboolean SG_RuneSeedGround(vec3_t candidate, vec3_t out);
typedef qboolean (*sg_rune_seed_surface_emit_fn)(void *context,
	const vec3_t origin, qboolean crouched);
qboolean SG_RuneSeedScanWorldSurfaces(sg_rune_seed_surface_emit_fn emit,
	void *context, uint32_t *scan_count);
qboolean SG_RuneSeedLocalContact(const vec3_t first, qboolean first_crouched,
	const vec3_t second, qboolean second_crouched);
int SG_RuneSeedSourceWaterlevel(vec3_t origin, int *watertype);
int SG_RuneSeedSourceWaterlevelPose(vec3_t origin, qboolean crouched,
	int *watertype);
qboolean SG_RuneSeedTriggerSafe(vec3_t origin);
qboolean SG_RuneSeedTriggerSafePose(vec3_t origin, qboolean crouched);
qboolean SG_RuneSeedSourceUnstable(vec3_t origin);
qboolean SG_RuneSeedSourceUnstablePose(vec3_t origin, qboolean crouched);
