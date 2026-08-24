#ifndef SG_TIMED_VAULT_EGRESS_H
#define SG_TIMED_VAULT_EGRESS_H

#include "slipgate/sg_rune.h"
#include "slipgate/sg_rune_mechanism_catalog.h"

#define SG_TIMED_VAULT_CAPTURE_DISTANCE 8.0f
#define SG_TIMED_VAULT_CAPTURE_SPEED 8.0f

int SG_WaterEscapeIndexBuild(const rune_seed_t *seeds, int num_seeds,
	const rune_link_t *links, int num_links, int *next, int *dist,
	int *incoming, int *next_incoming, int *queue);
int SG_WaterEscapeTargetIndexBuild(const rune_seed_t *seeds, int num_seeds,
	const rune_link_t *links, int num_links, const vec3_t final_target,
	int *next, float *score, int *incoming, int *next_incoming, int *heap,
	int *heap_pos);

typedef qboolean (*sg_timed_vault_source_reachable_fn)(
	const vec3_t origin, const vec3_t source, void *context);
int SG_TimedVaultEgressSourceSelect(const rune_seed_t *seeds, int num_seeds,
	const int *next, const vec3_t origin, const vec3_t final_target,
	sg_timed_vault_source_reachable_fn reachable, void *context);

qboolean SG_TimedVaultEgressTarget(const rune_seed_t *seeds, int num_seeds,
	const int *next, int controller_kind, int waterlevel,
	const vec3_t origin, const vec3_t final_target, vec3_t target_out);
qboolean SG_TimedVaultEgressAdvance(const rune_seed_t *seeds, int num_seeds,
	const int *next, int controller_kind, int waterlevel,
	const vec3_t origin, const vec3_t final_target, int *route_seed,
	vec3_t target_out);
qboolean SG_TimedVaultEgressAdvancePose(const rune_seed_t *seeds,
	int num_seeds, const int *next, int controller_kind, int waterlevel,
	const vec3_t origin, const vec3_t velocity, const vec3_t final_target,
	int *route_seed, qboolean *exact_capture, vec3_t target_out);
int SG_TimedVaultEgressBudgetMs(int controller_kind);

qboolean SG_TimedVaultEgressScopeBegin(const rune_seed_t *seeds,
	int num_seeds, const rune_link_t *links, int num_links);
void SG_TimedVaultEgressScopeEnd(void);
qboolean SG_TimedVaultEgressScopeTarget(int controller_kind, int waterlevel,
	const vec3_t origin, const vec3_t velocity, const vec3_t final_target,
	int *route_seed, qboolean *exact_capture, vec3_t target_out);

#endif
