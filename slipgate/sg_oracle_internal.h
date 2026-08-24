#ifndef SG_ORACLE_INTERNAL_H
#define SG_ORACLE_INTERNAL_H

#include "sg_replay.h"
#include "sg_rune_mechanism_catalog.h"
#include "sg_rune_mechanism_plan.h"

struct edict_s;
struct sg_phantom_s;

typedef struct sg_oracle_replay_scope_s
{
	struct edict_s *passent;
	qboolean world_only;
	qboolean contaminated;
} sg_oracle_replay_scope_t;

void SG_OracleReplayScopeBegin(sg_oracle_replay_scope_t *scope,
	struct edict_s *passent, qboolean world_only);
void SG_OracleReplayScopeEnd(const sg_oracle_replay_scope_t *scope);
qboolean SG_OracleReplayStartClear(struct sg_phantom_s *phantom);
qboolean SG_OracleReplayContaminated(void);
void SG_OracleReplayPose(const struct sg_phantom_s *phantom,
	sg_replay_pose_t *pose);
qboolean SG_OracleReplayContactClear(const vec3_t origin,
	const vec3_t destination, struct edict_s *passent);
qboolean SG_OracleTimedVaultClosureCurrent(
	const sg_mech_catalog_view_t *catalog,
	const sg_timed_vault_plan_witness_t *witness,
	qboolean top_pose_authenticated);
qboolean SG_OracleRotatorEntitySweepBlocks(const struct edict_s *rotator,
	const vec3_t start, const vec3_t hull_mins, const vec3_t hull_maxs,
	const vec3_t end, int contentmask);

#endif
