/* Production owner for one accepted compact RUNE and its runtime services. */
#ifndef SG_RUNE_COMPACT_PRODUCTION_H
#define SG_RUNE_COMPACT_PRODUCTION_H

#include <stdint.h>

#include "sg_compact_runtime_level.h"
#include "sg_rune_compact_artifact.h"
#include "sg_rune_compact_portal_snapshot.h"
#include "sg_rune_compact_portal_snapshot_source.h"

typedef struct sg_rune_source_authority_s sg_rune_source_authority_t;

typedef struct sg_rune_compact_learning_consumer_s
	sg_rune_compact_learning_consumer_t;

typedef enum sg_rune_compact_production_status_e
{
	SG_RUNE_COMPACT_PRODUCTION_OK = 0,
	SG_RUNE_COMPACT_PRODUCTION_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_PRODUCTION_ALREADY_ACTIVE,
	SG_RUNE_COMPACT_PRODUCTION_NOT_INITIALIZED,
	SG_RUNE_COMPACT_PRODUCTION_LOADER_REJECTED,
	SG_RUNE_COMPACT_PRODUCTION_ARTIFACT_REJECTED,
	SG_RUNE_COMPACT_PRODUCTION_MODEL_REJECTED,
	SG_RUNE_COMPACT_PRODUCTION_SPATIAL_INDEX_REJECTED,
	SG_RUNE_COMPACT_PRODUCTION_HOST_AUTHORITY_REJECTED,
	SG_RUNE_COMPACT_PRODUCTION_WEAPON_AUTHORITY_REJECTED,
	SG_RUNE_COMPACT_PRODUCTION_RUNTIME_REJECTED,
	SG_RUNE_COMPACT_PRODUCTION_PORTAL_SNAPSHOT_REJECTED,
	SG_RUNE_COMPACT_PRODUCTION_LEARNING_REJECTED,
	SG_RUNE_COMPACT_PRODUCTION_STATUS_COUNT
} sg_rune_compact_production_status_t;

typedef struct sg_rune_compact_production_result_s
{
	sg_rune_compact_production_status_t status;
	sg_rune_compact_artifact_load_result_t artifact;
	sg_host_law_result_t host;
	sg_compact_runtime_level_status_t runtime;
} sg_rune_compact_production_result_t;

typedef struct sg_rune_compact_production_s
{
	sg_rune_compact_artifact_loader_t loader;
	sg_compact_runtime_level_t runtime;
	sg_rune_compact_spatial_index_t *spatial_index;
	sg_rune_compact_portal_snapshot_t *portal_snapshot;
	sg_rune_compact_portal_snapshot_source_t *portal_snapshot_source;
	sg_rune_source_authority_t *source_authority;
	sg_rune_compact_learning_consumer_t *learning;
	sg_rune_compact_identity_t identity;
	uint8_t initialized;
	uint8_t active;
	uint8_t reserved[6];
} sg_rune_compact_production_t;

#define SG_RUNE_COMPACT_PRODUCTION_INITIALIZER { 0 }

sg_rune_compact_production_result_t SG_RuneCompactProductionInit(
	sg_rune_compact_production_t *owner);

/* Load owns the decoded model before the runtime handoff.  A rejected file or
 * host/runtime mismatch leaves the owner initialized but empty. */
sg_rune_compact_production_result_t SG_RuneCompactProductionLoad(
	sg_rune_compact_production_t *owner, const char *path);

/* Call only after every strategy caller/plan has been destroyed.  This also
 * retires all post-match priors before the borrowed compact model can change.
 * Runtime providers and borrowed services are cleared before the loader/model. */
void SG_RuneCompactProductionClear(sg_rune_compact_production_t *owner);

int SG_RuneCompactProductionCurrent(
	const sg_rune_compact_production_t *owner);
const sg_rune_compact_model_t *SG_RuneCompactProductionModel(
	const sg_rune_compact_production_t *owner);
int SG_RuneCompactProductionArtifactInfo(
	const sg_rune_compact_production_t *owner,
	sg_rune_compact_wire_info_t *info_out);

/* Publish the mechanism phases and complete portal-root authority for one
 * localized strategy frame.  If effective spawn semantics have drifted, this
 * still returns a structurally exact UNKNOWN root snapshot; callers never
 * recover by omitting the root gate. */
int SG_RuneCompactProductionFrameSnapshot(
	sg_rune_compact_production_t *owner, uint64_t frame_sequence,
	sg_rune_compact_portal_snapshot_frame_t *frame_out);

const char *SG_RuneCompactProductionStatusString(
	sg_rune_compact_production_status_t status);

#endif /* SG_RUNE_COMPACT_PRODUCTION_H */
