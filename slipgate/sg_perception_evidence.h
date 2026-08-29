#ifndef SG_PERCEPTION_EVIDENCE_H
#define SG_PERCEPTION_EVIDENCE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_belief_contract.h"

typedef enum sg_perception_source_e
{
	SG_PERCEPTION_SOURCE_SIGHT = 0,
	SG_PERCEPTION_SOURCE_SOUND,
	SG_PERCEPTION_SOURCE_DAMAGE,
	SG_PERCEPTION_SOURCE_ITEM,
	SG_PERCEPTION_SOURCE_FLAG,
	SG_PERCEPTION_SOURCE_TEAMMATE,
	SG_PERCEPTION_SOURCE_COUNT
} sg_perception_source_t;

typedef enum sg_perception_authority_e
{
	SG_PERCEPTION_AUTHORITY_HOST_SENSOR = 0,
	SG_PERCEPTION_AUTHORITY_HOST_TEAMMATE_REPORT,
	SG_PERCEPTION_AUTHORITY_COUNT
} sg_perception_authority_t;

typedef enum sg_perception_location_basis_e
{
	SG_PERCEPTION_LOCATION_EARNED_RUNTIME = 0,
	SG_PERCEPTION_LOCATION_RUNE_STATIC,
	SG_PERCEPTION_LOCATION_BASIS_COUNT
} sg_perception_location_basis_t;

typedef enum sg_perception_sound_kind_e
{
	SG_PERCEPTION_SOUND_FOOTSTEP = 0,
	SG_PERCEPTION_SOUND_WEAPON,
	SG_PERCEPTION_SOUND_MOVEMENT,
	SG_PERCEPTION_SOUND_ITEM,
	SG_PERCEPTION_SOUND_OTHER_SPATIAL,
	SG_PERCEPTION_SOUND_KIND_COUNT
} sg_perception_sound_kind_t;

typedef enum sg_perception_item_occurrence_e
{
	SG_PERCEPTION_ITEM_TARGET_PICKUP = 0,
	SG_PERCEPTION_ITEM_TARGET_SEEN_AT_LOCATION,
	SG_PERCEPTION_ITEM_OCCURRENCE_COUNT
} sg_perception_item_occurrence_t;

typedef enum sg_perception_flag_occurrence_e
{
	SG_PERCEPTION_FLAG_TARGET_PICKUP = 0,
	SG_PERCEPTION_FLAG_TARGET_DROP,
	SG_PERCEPTION_FLAG_TARGET_CARRY_SIGHTED,
	SG_PERCEPTION_FLAG_OCCURRENCE_COUNT
} sg_perception_flag_occurrence_t;

typedef struct sg_perception_authentication_s
{
	uint8_t authenticated;
	sg_perception_authority_t authority;
	uint8_t issuer_team;
	uint8_t audience_team;
	uint8_t reserved[6];
	sg_belief_life_identity_t issuer_life;
	uint64_t event_id;
	uint64_t evidence_sequence;
	uint64_t observed_at_ms;
	uint64_t authenticated_at_ms;
	uint64_t valid_until_ms;
	uint64_t rune_identity;
	uint64_t topology_revision;
} sg_perception_authentication_t;

typedef struct sg_perception_hypothesis_s
{
	sg_phase_coordinate_t phase;
	sg_perception_location_basis_t location_basis;
	sg_belief_motion_state_t movement_state;
	uint8_t weapon_state;
	uint8_t reserved[3];
	float position[3];
	float velocity[3];
	float acceleration[3];
	float orientation[3];
	float spread_radius;
	float likelihood;
} sg_perception_hypothesis_t;

typedef struct sg_perception_sight_s
{
	uint8_t in_pvs;
	uint8_t line_of_sight_proved;
	uint8_t reserved[2];
	sg_perception_hypothesis_t hypothesis;
} sg_perception_sight_t;

typedef struct sg_perception_sound_s
{
	uint8_t in_phs;
	uint8_t positional;
	uint16_t reserved;
	sg_perception_sound_kind_t kind;
	uint32_t sound_id;
	float listener_position[3];
	float heard_origin[3];
	float attenuation;
	float audible_radius;
	const sg_perception_hypothesis_t *hypotheses;
	size_t hypothesis_count;
} sg_perception_sound_t;

typedef struct sg_perception_damage_s
{
	uint8_t landed;
	uint8_t reserved[3];
	uint32_t damage;
	uint32_t means_of_death;
	float victim_position[3];
	float incoming_direction[3];
	const sg_perception_hypothesis_t *hypotheses;
	size_t hypothesis_count;
} sg_perception_damage_t;

typedef struct sg_perception_item_s
{
	sg_perception_item_occurrence_t occurrence;
	sg_destination_ref_t destination;
	const sg_perception_hypothesis_t *hypotheses;
	size_t hypothesis_count;
} sg_perception_item_t;

typedef struct sg_perception_flag_s
{
	sg_perception_flag_occurrence_t occurrence;
	sg_destination_ref_t destination;
	const sg_perception_hypothesis_t *hypotheses;
	size_t hypothesis_count;
} sg_perception_flag_t;

typedef struct sg_perception_teammate_s
{
	sg_perception_source_t reported_source;
	uint32_t report_kind;
	sg_destination_ref_t reported_destination;
	const sg_perception_hypothesis_t *hypotheses;
	size_t hypothesis_count;
} sg_perception_teammate_t;

/* All input pointers are borrowed for one call. No payload contains an actor,
 * controller, link, action, or mutable RUNE pointer. */
typedef struct sg_perception_observation_s
{
	sg_perception_authentication_t authentication;
	sg_perception_source_t source;
	sg_belief_evidence_kind_t evidence_kind;
	uint8_t target_team;
	uint8_t reserved[7];
	sg_belief_life_identity_t target_life;
	float confidence;
	union
	{
		sg_perception_sight_t sight;
		sg_perception_sound_t sound;
		sg_perception_damage_t damage;
		sg_perception_item_t item;
		sg_perception_flag_t flag;
		sg_perception_teammate_t teammate;
	} data;
} sg_perception_observation_t;

typedef enum sg_perception_adapt_result_e
{
	SG_PERCEPTION_ADAPT_APPLIED = 0,
	SG_PERCEPTION_ADAPT_REJECTED_INVALID,
	SG_PERCEPTION_ADAPT_REJECTED_AUTHORITY,
	SG_PERCEPTION_ADAPT_CAPACITY,
	SG_PERCEPTION_ADAPT_OVERFLOW
} sg_perception_adapt_result_t;

/* evidence.supports points at caller-owned support_storage after APPLIED and
 * stays valid only while that storage remains alive and unchanged. The
 * adapter never retains observation pointers. The observation object, every
 * external hypothesis span, the actual support_storage span, and out must not
 * overlap. Non-APPLIED results leave support_storage byte-identical. Alias or
 * byte-range overflow rejection writes none of those caller-owned objects. */
typedef struct sg_perception_adaptation_s
{
	sg_perception_adapt_result_t result;
	size_t required_support_capacity;
	sg_belief_evidence_t evidence;
} sg_perception_adaptation_t;

sg_perception_adapt_result_t SG_PerceptionEvidenceAdapt(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_perception_observation_t *observation,
	sg_belief_evidence_support_t *support_storage, size_t support_capacity,
	sg_perception_adaptation_t *out);

#endif /* SG_PERCEPTION_EVIDENCE_H */
