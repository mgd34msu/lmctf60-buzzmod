/* Solver-free localization values shared by compact runtime consumers. */
#ifndef SG_LOCALIZATION_RUNTIME_H
#define SG_LOCALIZATION_RUNTIME_H

#include <stdint.h>

#include "sg_host_law_owner.h"

#define SG_LOCALIZATION_SUPPORT_MODEL_NONE UINT32_MAX

typedef enum sg_localization_status_e
{
	SG_LOCALIZATION_OK = 0,
	SG_LOCALIZATION_INVALID_ARGUMENT,
	SG_LOCALIZATION_INVALID_BINDING,
	SG_LOCALIZATION_CAPACITY,
	SG_LOCALIZATION_UNAUTHENTICATED,
	SG_LOCALIZATION_IDENTITY_MISMATCH,
	SG_LOCALIZATION_STALE,
	SG_LOCALIZATION_NONFINITE,
	SG_LOCALIZATION_SOLID,
	SG_LOCALIZATION_OUTSIDE_CONFIGURATION,
	SG_LOCALIZATION_NO_SEMANTIC_REGION,
	SG_LOCALIZATION_AMBIGUOUS_INPUT,
	SG_LOCALIZATION_MOVER_UNBOUND,
	SG_LOCALIZATION_NO_PHASE,
	SG_LOCALIZATION_RECOVERY_PARAMETER,
	SG_LOCALIZATION_RECOVERY_REJECTED,
	SG_LOCALIZATION_RESET_REQUIRED
} sg_localization_status_t;

typedef enum sg_localization_observation_kind_e
{
	SG_LOCALIZATION_OBSERVATION_PRESENT = 0,
	SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT,
	SG_LOCALIZATION_OBSERVATION_DEAD,
	SG_LOCALIZATION_OBSERVATION_TELEPORTED,
	SG_LOCALIZATION_OBSERVATION_NEW_SPAWN,
	SG_LOCALIZATION_OBSERVATION_KIND_COUNT
} sg_localization_observation_kind_t;

typedef enum sg_localization_recovery_e
{
	SG_LOCALIZATION_RECOVERY_NONE = 0,
	SG_LOCALIZATION_RECOVERY_EXACT_CONTINUITY,
	SG_LOCALIZATION_RECOVERY_NUMERIC_DRIFT,
	SG_LOCALIZATION_RECOVERY_TEMPORARY_ABSENCE
} sg_localization_recovery_t;

/* Every result names the accepted model generation that authenticated it and
 * the exact frame in which that generation was observed. */
typedef struct sg_localization_model_stamp_s
{
	uint64_t identity;
	uint64_t generation;
	uint64_t frame_sequence;
} sg_localization_model_stamp_t;

typedef enum sg_localization_presence_e
{
	SG_LOCALIZATION_PRESENCE_PRESENT = 0,
	SG_LOCALIZATION_PRESENCE_TEMPORARILY_ABSENT,
	SG_LOCALIZATION_PRESENCE_DEAD,
	SG_LOCALIZATION_PRESENCE_COUNT
} sg_localization_presence_t;

typedef sg_host_law_subject_t sg_localization_subject_t;

#endif /* SG_LOCALIZATION_RUNTIME_H */
