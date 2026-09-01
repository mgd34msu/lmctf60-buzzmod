/* One level-scoped owner for the accepted compact runtime publications. */
#ifndef SG_COMPACT_RUNTIME_LEVEL_H
#define SG_COMPACT_RUNTIME_LEVEL_H

#include <stdint.h>

#include "sg_bot_localization_owner.h"
#include "sg_host_law_owner.h"
#include "sg_rune_compact_field_service.h"
#include "sg_strategy_runtime_bridge.h"
#include "sg_tactic_runtime.h"

typedef struct sg_tactic_execution_owner_s sg_tactic_execution_owner_t;

typedef enum sg_compact_runtime_level_status_e
{
	SG_COMPACT_RUNTIME_LEVEL_OK = 0,
	SG_COMPACT_RUNTIME_LEVEL_INVALID_ARGUMENT,
	SG_COMPACT_RUNTIME_LEVEL_ALREADY_ACTIVE,
	SG_COMPACT_RUNTIME_LEVEL_FIELD_SERVICE_REJECTED,
	SG_COMPACT_RUNTIME_LEVEL_LOCALIZATION_REJECTED,
	SG_COMPACT_RUNTIME_LEVEL_LOCALIZATION_PROVIDER_REJECTED,
	SG_COMPACT_RUNTIME_LEVEL_STRATEGY_PROVIDER_REJECTED,
	SG_COMPACT_RUNTIME_LEVEL_TACTIC_PROVIDER_REJECTED,
	SG_COMPACT_RUNTIME_LEVEL_EXECUTION_OWNER_REJECTED,
	SG_COMPACT_RUNTIME_LEVEL_SCRATCH_REJECTED,
	SG_COMPACT_RUNTIME_LEVEL_STATUS_COUNT
} sg_compact_runtime_level_status_t;

/* The accepted artifact/model, exact spatial index, observation owner, and
 * host publication remain owned by the level loader. This level owner retains
 * the borrowed localization binding, cell_count scratch, and the unique bot
 * provider token. Call Clear before destroying any borrowed source. */
typedef struct sg_compact_runtime_level_s
{
	const sg_rune_compact_model_t *accepted_model;
	sg_compact_localization_binding_t localization;
	sg_compact_localization_scratch_t localization_scratch;
	sg_rune_compact_field_service_t *field_service;
	sg_tactic_execution_owner_t *execution_owner;
	uint64_t rune_identity;
	uint64_t model_generation;
	uint64_t provider_token;
	uint8_t active;
	uint8_t reserved[7];
} sg_compact_runtime_level_t;

#define SG_COMPACT_RUNTIME_LEVEL_INITIALIZER \
	{ 0 }

/* Install is the exact handoff after the artifact owner has accepted one
 * immutable compact model and matching spatial index and the host owner has
 * published its authority.
 * The order is service -> localization (which also installs CACO's compact
 * belief provider) -> strategy provider -> tactic provider -> sealed
 * execution owner. No legacy field is adapted. */
sg_compact_runtime_level_status_t SG_CompactRuntimeLevelInstall(
	sg_compact_runtime_level_t *runtime,
	const sg_rune_compact_model_t *accepted_model,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_spatial_index_t *spatial_index,
	const sg_compact_localization_observation_owner_t *observation_owner,
	const sg_host_law_runtime_authority_t *host_authority,
	uint64_t rune_identity, uint64_t topology_revision);

/* Clear is idempotent and must precede model/artifact teardown. The level owner
 * must first destroy every strategy caller/plan that resolved through this
 * service; Clear revokes new resolutions but does not own those caller
 * objects. It first destroys the sealed execution owner, then revokes the
 * tactic and strategy providers,
 * localization/compact belief, unbinds the borrowed binding, and finally
 * destroys the field service. */
void SG_CompactRuntimeLevelClear(sg_compact_runtime_level_t *runtime);

int SG_CompactRuntimeLevelCurrent(
	const sg_compact_runtime_level_t *runtime);

const sg_rune_compact_field_service_t *SG_CompactRuntimeLevelFieldService(
	const sg_compact_runtime_level_t *runtime);

sg_tactic_execution_owner_t *SG_CompactRuntimeLevelExecutionOwner(
	sg_compact_runtime_level_t *runtime);

sg_localization_status_t SG_CompactRuntimeLevelObserve(
	sg_compact_runtime_level_t *runtime,
	const sg_compact_localization_sample_t *sample,
	const sg_compact_localized_state_t *previous,
	sg_compact_localized_state_t *state_out);

const char *SG_CompactRuntimeLevelStatusString(
	sg_compact_runtime_level_status_t status);

#endif /* SG_COMPACT_RUNTIME_LEVEL_H */
