#include "sg_rune_compact_model.h"

#include "sg_rune_compact_source_surface_catalog.h"
#include "sg_rune_compact_mechanisms.h"
#include "sg_rune_compact_static.h"
#include "sg_rune_compact_weapon_field.h"
#include "sg_weapon_effect_profile.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum facet_polygon_result_e
{
	FACET_POLYGON_VALID = 0,
	FACET_POLYGON_INVALID_GEOMETRY,
	FACET_POLYGON_NONCANONICAL
} facet_polygon_result_t;

static void SetError(sg_rune_compact_error_t *error,
	sg_rune_compact_error_code_t code,
	sg_rune_compact_record_domain_t domain, uint32_t record)
{
	if (!error)
		return;
	error->code = code;
	error->domain = domain;
	error->record = record;
}

static int SpanWithin(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int ArrayPresent(const void *values, uint32_t count)
{
	return count == 0U || values != NULL;
}

static int ReservedBytesZero(const uint8_t reserved[3])
{
	return reserved[0] == 0U && reserved[1] == 0U && reserved[2] == 0U;
}

static int ReservedBytesZero2(const uint8_t reserved[2])
{
	return reserved[0] == 0U && reserved[1] == 0U;
}

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int CompareU64(uint64_t left, uint64_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int CompareI32(int32_t left, int32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

sg_rune_weapon_response_family_mask_t
SG_RuneCompactWeaponCanonicalProfileMask(uint32_t source_profile)
{
	static const sg_rune_weapon_response_family_mask_t masks[
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT + 1U] = {
		0U,
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_HITSCAN) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_HITSCAN) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_HITSCAN) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_HITSCAN) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_RAIL),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_BFG) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_SPECIAL),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_SPECIAL),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_SPECIAL),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT) |
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE),
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_SPECIAL)
	};

	return source_profile == 0U || source_profile >=
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT + 1U ? 0U :
		masks[source_profile];
}

int SG_RuneCompactWeaponCanonicalEventLaw(uint32_t source_profile,
	sg_rune_weapon_response_family_t family,
	sg_rune_weapon_event_law_t *law_out)
{
	sg_rune_weapon_runtime_requirement_mask_t requirements =
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE;
	sg_rune_weapon_event_law_kind_t kind;

	if (law_out == NULL ||
		(uint32_t)family >=
			(uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT ||
		(SG_RuneCompactWeaponCanonicalProfileMask(source_profile) &
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family)) == 0U)
		return 0;
	switch (family) {
	case SG_RUNE_WEAPON_RESPONSE_HITSCAN:
		kind = SG_RUNE_WEAPON_EVENT_HITSCAN_RAY;
		requirements |= SG_RUNE_WEAPON_RUNTIME_RANDOM_U15;
		break;
	case SG_RUNE_WEAPON_RESPONSE_RAIL:
		kind = SG_RUNE_WEAPON_EVENT_RAIL_PENETRATION;
		requirements |= SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION;
		break;
	case SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD:
		kind = SG_RUNE_WEAPON_EVENT_SPREAD_RAYS;
		requirements |= SG_RUNE_WEAPON_RUNTIME_RANDOM_U15 |
			SG_RUNE_WEAPON_RUNTIME_WEAPON_FRAME |
			SG_RUNE_WEAPON_RUNTIME_ATTACK_HELD |
			SG_RUNE_WEAPON_RUNTIME_AMMO_COUNT;
		break;
	case SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE:
		kind = SG_RUNE_WEAPON_EVENT_SPREAD_RAYS;
		requirements |= SG_RUNE_WEAPON_RUNTIME_RANDOM_U15;
		break;
	case SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT:
		kind = SG_RUNE_WEAPON_EVENT_STRAIGHT_PROJECTILE;
		requirements |= SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
			SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
			SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION;
		break;
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT:
		kind = source_profile == SG_WEAPON_PROFILE_BFG ?
			SG_RUNE_WEAPON_EVENT_BFG_COMPOSITE :
			SG_RUNE_WEAPON_EVENT_PROJECTILE_IMPACT;
		requirements |= SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
			SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
			SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION;
		if (source_profile == SG_WEAPON_PROFILE_ROCKET_LAUNCHER)
			requirements |= SG_RUNE_WEAPON_RUNTIME_RANDOM_U15;
		if (source_profile == SG_WEAPON_PROFILE_BFG)
			requirements |= SG_RUNE_WEAPON_RUNTIME_IMPACT_STATE |
				SG_RUNE_WEAPON_RUNTIME_ENTITY_QUERY;
		break;
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH:
		if (source_profile == SG_WEAPON_PROFILE_BFG) {
			kind = SG_RUNE_WEAPON_EVENT_BFG_COMPOSITE;
			requirements |= SG_RUNE_WEAPON_RUNTIME_IMPACT_STATE |
				SG_RUNE_WEAPON_RUNTIME_ENTITY_QUERY;
		} else if (source_profile == SG_WEAPON_PROFILE_PLASMA_REFLECT) {
			kind = SG_RUNE_WEAPON_EVENT_PLASMA_REFLECT;
			requirements |= SG_RUNE_WEAPON_RUNTIME_IMPACT_STATE;
		} else if (source_profile == SG_WEAPON_PROFILE_PLASMA_SPREAD) {
			kind = SG_RUNE_WEAPON_EVENT_PLASMA_SPREAD;
			requirements |= SG_RUNE_WEAPON_RUNTIME_IMPACT_STATE |
				SG_RUNE_WEAPON_RUNTIME_RANDOM_U15;
		} else {
			kind = SG_RUNE_WEAPON_EVENT_LINEAR_SPLASH;
		}
		requirements |= SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
			SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION;
		break;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT:
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE:
		kind = SG_RUNE_WEAPON_EVENT_GRENADE_BOUNCE_FUSE;
		requirements |= SG_RUNE_WEAPON_RUNTIME_RANDOM_U15 |
			SG_RUNE_WEAPON_RUNTIME_FUSE_DEADLINE |
			SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
			SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
			SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
			SG_RUNE_WEAPON_RUNTIME_EVENT_FRAME;
		if (source_profile == SG_WEAPON_PROFILE_HAND_GRENADE)
			requirements |= SG_RUNE_WEAPON_RUNTIME_WEAPON_FRAME |
				SG_RUNE_WEAPON_RUNTIME_ATTACK_HELD |
				SG_RUNE_WEAPON_RUNTIME_AMMO_COUNT;
		break;
	case SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER:
		kind = SG_RUNE_WEAPON_EVENT_HYPERBLASTER;
		requirements |= SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
			SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
			SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
			SG_RUNE_WEAPON_RUNTIME_WEAPON_FRAME |
			SG_RUNE_WEAPON_RUNTIME_ATTACK_HELD |
			SG_RUNE_WEAPON_RUNTIME_AMMO_COUNT;
		break;
	case SG_RUNE_WEAPON_RESPONSE_BFG:
		kind = SG_RUNE_WEAPON_EVENT_BFG_COMPOSITE;
		requirements |= SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
			SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
			SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
			SG_RUNE_WEAPON_RUNTIME_IMPACT_STATE |
			SG_RUNE_WEAPON_RUNTIME_ENTITY_QUERY;
		break;
	case SG_RUNE_WEAPON_RESPONSE_SPECIAL:
		if (source_profile == SG_WEAPON_PROFILE_HOOK) {
			kind = SG_RUNE_WEAPON_EVENT_HOOK_DAMAGE;
			requirements |= SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
				SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
				SG_RUNE_WEAPON_RUNTIME_EVENT_FRAME;
		} else if (source_profile == SG_WEAPON_PROFILE_BFG) {
			kind = SG_RUNE_WEAPON_EVENT_BFG_COMPOSITE;
			requirements |= SG_RUNE_WEAPON_RUNTIME_WEAPON_FRAME |
				SG_RUNE_WEAPON_RUNTIME_AMMO_COUNT |
				SG_RUNE_WEAPON_RUNTIME_ENTITY_QUERY;
		} else if (source_profile == SG_WEAPON_PROFILE_PLASMA_REFLECT) {
			kind = SG_RUNE_WEAPON_EVENT_PLASMA_REFLECT;
			requirements |= SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
				SG_RUNE_WEAPON_RUNTIME_IMPACT_STATE |
				SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION;
		} else if (source_profile == SG_WEAPON_PROFILE_PLASMA_SPREAD) {
			kind = SG_RUNE_WEAPON_EVENT_PLASMA_SPREAD;
			requirements |= SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
				SG_RUNE_WEAPON_RUNTIME_IMPACT_STATE |
				SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
				SG_RUNE_WEAPON_RUNTIME_RANDOM_U15;
		} else {
			return 0;
		}
		break;
	case SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT:
	default:
		return 0;
	}
	law_out->kind = kind;
	law_out->requirements = requirements;
	return 1;
}

int SG_RuneCompactWeaponRelationClassForProfile(uint32_t source_profile,
	sg_rune_weapon_response_family_t family,
	sg_rune_compact_weapon_relation_class_t *class_out)
{
	sg_rune_compact_weapon_relation_class_t relation_class;

	if (class_out == NULL || (uint32_t)family >=
		(uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT ||
		(SG_RuneCompactWeaponCanonicalProfileMask(source_profile) &
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family)) == 0U)
		return 0;
	switch (family) {
	case SG_RUNE_WEAPON_RESPONSE_HITSCAN:
	case SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD:
	case SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE:
	case SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT:
	case SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER:
		relation_class = SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT;
		break;
	case SG_RUNE_WEAPON_RESPONSE_RAIL:
		relation_class = SG_RUNE_COMPACT_WEAPON_RELATION_RAIL;
		break;
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT:
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH:
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT:
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE:
	case SG_RUNE_WEAPON_RESPONSE_BFG:
		relation_class = SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT;
		break;
	case SG_RUNE_WEAPON_RESPONSE_SPECIAL:
		relation_class = source_profile == SG_WEAPON_PROFILE_HOOK ?
			SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT :
			SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT;
		break;
	case SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT:
		return 0;
	}
	*class_out = relation_class;
	return 1;
}

int SG_RuneCompactWeaponStaticRelationValid(
	sg_rune_compact_static_visibility_class_t visibility,
	sg_rune_compact_static_visibility_reason_t reason,
	sg_rune_compact_static_relation_flags_t flags,
	uint8_t requires_exact_ray, uint8_t requires_area_state,
	uint32_t static_occluder_count)
{
	const sg_rune_compact_static_relation_flags_t certificate = flags &
		(SG_RUNE_COMPACT_STATIC_RELATION_DIRECT |
		 SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT);
	const sg_rune_compact_static_relation_flags_t area_flag = flags &
		SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING;

	if (requires_exact_ray > 1U || requires_area_state > 1U ||
		(flags & ~(sg_rune_compact_static_relation_flags_t)
			SG_RUNE_COMPACT_STATIC_RELATION_FLAGS_KNOWN) != 0U)
		return 0;
	/* The response partition retains only query-time candidates.  Exact rays
	 * are always required; a distinct-area candidate independently requires
	 * authenticated portal state.  A certified direct or static-impact result
	 * decorates that candidate rather than replacing its conditional evidence. */
	if (visibility != SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL ||
		(reason != SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED &&
		 reason != SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL) ||
		requires_exact_ray != 1U ||
		((requires_area_state == 0U && area_flag != 0U) ||
		 (requires_area_state == 1U && area_flag == 0U)) ||
		(certificate != 0U &&
		 certificate != SG_RUNE_COMPACT_STATIC_RELATION_DIRECT &&
		 certificate != SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT) ||
		(flags & SG_RUNE_COMPACT_STATIC_RELATION_PENETRATING) != 0U)
		return 0;
	return static_occluder_count ==
		(certificate == SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT ?
			1U : 0U);
}

int SG_RuneCompactWeaponProfileShapeValid(
	const sg_rune_weapon_profile_t *profile)
{
	if (profile == NULL || profile->projectile_count_min == 0U ||
		profile->projectile_count_max < profile->projectile_count_min ||
		(profile->direct_response_count != 1U &&
		 profile->direct_response_count != 2U) || profile->reserved != 0U)
		return 0;
	return profile->source_profile == SG_WEAPON_PROFILE_RAILGUN ||
		profile->auxiliary_trace_count == 0U;
}

static int WeaponEventFunctionReferenceCount(
	const sg_rune_weapon_profile_t *profile,
	sg_rune_weapon_response_family_t family, uint32_t *count_out)
{
	uint32_t count;

	if (count_out == NULL || !SG_RuneCompactWeaponProfileShapeValid(profile) ||
		(uint32_t)family >=
			(uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT ||
		(SG_RuneCompactWeaponCanonicalProfileMask(profile->source_profile) &
		 SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family)) == 0U)
		return 0;
	switch (family) {
	case SG_RUNE_WEAPON_RESPONSE_HITSCAN:
	case SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD:
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT:
	case SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER:
		count = profile->direct_response_count;
		break;
	case SG_RUNE_WEAPON_RESPONSE_RAIL:
		count = (uint32_t)profile->direct_response_count +
			(uint32_t)profile->auxiliary_trace_count;
		break;
	case SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE:
	case SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT:
		count = (uint32_t)profile->projectile_count_max *
			(uint32_t)profile->direct_response_count;
		break;
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH:
		count = 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT:
		/* z(t) and fuse(t) are analytic host laws; collision and bounce
		 * chronology remain explicit runtime event requirements. */
		count = 2U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE:
		count = 2U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_BFG:
		count = (uint32_t)profile->direct_response_count + 3U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_SPECIAL:
		count = profile->source_profile == SG_WEAPON_PROFILE_HOOK ?
			(uint32_t)profile->direct_response_count + 1U : 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT:
	default:
		return 0;
	}
	*count_out = count;
	return 1;
}

int SG_RuneCompactWeaponStaticLawSlotRequired(uint32_t source_profile,
	sg_rune_weapon_response_family_t family,
	sg_rune_weapon_static_law_slot_t slot)
{
	if ((uint32_t)family >=
		(uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT ||
		(uint32_t)slot >= (uint32_t)SG_RUNE_WEAPON_STATIC_LAW_COUNT ||
		(SG_RuneCompactWeaponCanonicalProfileMask(source_profile) &
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family)) == 0U)
		return 0;

	/* Cadence and ammunition fields are immutable eligibility/cost law. They
	 * deliberately describe thresholds and debits, never a player inventory. */
	if (slot == SG_RUNE_WEAPON_STATIC_LAW_CADENCE_MS ||
		slot == SG_RUNE_WEAPON_STATIC_LAW_CADENCE_KIND ||
		slot == SG_RUNE_WEAPON_STATIC_LAW_AMMO_READY_MINIMUM ||
		slot == SG_RUNE_WEAPON_STATIC_LAW_AMMO_LIVE_FIRE_MINIMUM ||
		slot == SG_RUNE_WEAPON_STATIC_LAW_AMMO_DEBIT ||
		slot == SG_RUNE_WEAPON_STATIC_LAW_AMMO_DEBIT_MAXIMUM ||
		slot == SG_RUNE_WEAPON_STATIC_LAW_AMMO_INFINITE_DEBIT)
		return 1;

	switch (family) {
	case SG_RUNE_WEAPON_RESPONSE_HITSCAN:
		return slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_RAY_DISTANCE;
	case SG_RUNE_WEAPON_RESPONSE_RAIL:
		return slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_RAY_DISTANCE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_AUXILIARY_TRACE_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_AUXILIARY_HORIZONTAL_SPREAD ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_AUXILIARY_VERTICAL_SPREAD;
	case SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD:
	case SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE:
		return slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_RAY_DISTANCE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_COUNT_MIN ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_COUNT_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_HORIZONTAL_SPREAD ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_VERTICAL_SPREAD ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_YAW_SPREAD_DEGREES;
	case SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT:
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT:
	case SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER:
		return slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_RETIRE_DISTANCE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_HALF_EXTENT ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_LIFETIME_MS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_COUNT_MIN ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_COUNT_MAX;
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH:
		return slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_RADIUS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_RADIUS_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_DAMAGE_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SELF_DAMAGE_SCALE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_TEAMMATE_RISK_SCALE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_KERNEL ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER_SCALE;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT:
		return slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_RETIRE_DISTANCE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_HALF_EXTENT ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_LIFETIME_MS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_COUNT_MIN ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_COUNT_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_LAUNCH_VERTICAL_SPEED ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_LAUNCH_JITTER ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_GRAVITY_SCALE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_FUSE_MS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_COOK_MS;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE:
		return slot == SG_RUNE_WEAPON_STATIC_LAW_FUSE_MS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_COOK_MS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_RADIUS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_RADIUS_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_DAMAGE_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SELF_DAMAGE_SCALE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_TEAMMATE_RISK_SCALE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_KERNEL ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER_SCALE;
	case SG_RUNE_WEAPON_RESPONSE_BFG:
		return slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_RETIRE_DISTANCE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_HALF_EXTENT ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_LIFETIME_MS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_RADIUS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_RADIUS_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_DAMAGE_MAX ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SELF_DAMAGE_SCALE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_TEAMMATE_RISK_SCALE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_KERNEL ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER_SCALE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_RADIUS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_KERNEL ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_OWNER ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_OWNER_SCALE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_DAMAGE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_RADIUS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_DISTANCE ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_INTERVAL_MS ||
			slot == SG_RUNE_WEAPON_STATIC_LAW_WINDUP_MS;
	case SG_RUNE_WEAPON_RESPONSE_SPECIAL:
		if (source_profile == SG_WEAPON_PROFILE_HOOK)
			return slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE_MAX ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_RETIRE_DISTANCE ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_HALF_EXTENT ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_LIFETIME_MS ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_HOOK_INITIAL_DAMAGE ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_HOOK_ATTACHED_DAMAGE ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_HOOK_PULL_SPEED ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_HOOK_HEALTH;
		if (source_profile == SG_WEAPON_PROFILE_BFG)
			return slot == SG_RUNE_WEAPON_STATIC_LAW_WINDUP_MS ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_DAMAGE ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_RADIUS ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_DISTANCE ||
				slot == SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_INTERVAL_MS;
		return SG_RuneCompactWeaponStaticLawSlotRequired(source_profile,
			SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH, slot);
	case SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT:
		return 0;
	}
	return 0;
}

int SG_RuneCompactWeaponKernelReferenceCount(
	const sg_rune_weapon_profile_t *profile,
	sg_rune_weapon_response_family_t family, uint32_t *count_out)
{
	uint32_t count;
	uint32_t slot;

	if (count_out == NULL || !SG_RuneCompactWeaponProfileShapeValid(profile) ||
		(uint32_t)family >=
			(uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT ||
		(SG_RuneCompactWeaponCanonicalProfileMask(profile->source_profile) &
		 SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family)) == 0U ||
		!WeaponEventFunctionReferenceCount(profile, family, &count))
		return 0;
	for (slot = 0U; slot < (uint32_t)SG_RUNE_WEAPON_STATIC_LAW_COUNT;
		slot++) {
		if (SG_RuneCompactWeaponStaticLawSlotRequired(profile->source_profile,
			family, (sg_rune_weapon_static_law_slot_t)slot)) {
			if (count == UINT32_MAX)
				return 0;
			count++;
		}
	}
	*count_out = count;
	return 1;
}

static int DirectFunctionRefExpected(
	const sg_rune_weapon_profile_t *profile, uint32_t ordinal,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance,
	sg_rune_weapon_effect_channel_t *channel_out, uint32_t *instance_out,
	sg_rune_analytic_output_meaning_t *output_out)
{
	if (ordinal >= profile->direct_response_count)
		return 0;
	*channel_out = channel;
	*instance_out = instance;
	*output_out = ordinal == 0U ?
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS :
		SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	return 1;
}

int SG_RuneCompactWeaponFunctionRefExpected(
	const sg_rune_weapon_profile_t *profile,
	sg_rune_weapon_response_family_t family, uint32_t ordinal,
	sg_rune_weapon_effect_channel_t *channel_out, uint32_t *instance_out,
	sg_rune_analytic_output_meaning_t *output_out)
{
	uint32_t direct_count;
	uint32_t event_count;
	uint32_t primary_count;
	uint32_t slot;

	if (channel_out == NULL || instance_out == NULL || output_out == NULL ||
		!SG_RuneCompactWeaponProfileShapeValid(profile) ||
		!SG_RuneCompactWeaponKernelReferenceCount(profile, family,
			&primary_count) || ordinal >= primary_count)
		return 0;
	if (!WeaponEventFunctionReferenceCount(profile, family, &event_count))
		return 0;
	if (ordinal >= event_count) {
		ordinal -= event_count;
		for (slot = 0U; slot < (uint32_t)SG_RUNE_WEAPON_STATIC_LAW_COUNT;
			slot++) {
			if (!SG_RuneCompactWeaponStaticLawSlotRequired(
				profile->source_profile, family,
				(sg_rune_weapon_static_law_slot_t)slot))
				continue;
			if (ordinal-- != 0U)
				continue;
			*channel_out = SG_RUNE_WEAPON_EFFECT_CHANNEL_STATIC_LAW;
			*instance_out = slot;
			*output_out = SG_RUNE_ANALYTIC_OUTPUT_STATIC_WEAPON_LAW_VALUE;
			return 1;
		}
		return 0;
	}
	direct_count = profile->direct_response_count;
	switch (family) {
	case SG_RUNE_WEAPON_RESPONSE_HITSCAN:
	case SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD:
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT:
	case SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER:
		return DirectFunctionRefExpected(profile, ordinal,
			SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, channel_out,
			instance_out, output_out);
	case SG_RUNE_WEAPON_RESPONSE_RAIL:
		if (ordinal < direct_count)
			return DirectFunctionRefExpected(profile, ordinal,
				SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, channel_out,
				instance_out, output_out);
		*channel_out = SG_RUNE_WEAPON_EFFECT_CHANNEL_AUXILIARY_TRACE;
		*instance_out = ordinal - direct_count;
		*output_out = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
		return 1;
	case SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE:
	case SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT:
		return DirectFunctionRefExpected(profile, ordinal % direct_count,
			SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, ordinal / direct_count,
			channel_out, instance_out, output_out);
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH:
		*channel_out = SG_RUNE_WEAPON_EFFECT_CHANNEL_SPLASH;
		*instance_out = 0U;
		*output_out = SG_RUNE_ANALYTIC_OUTPUT_EFFECT_RADIUS;
		return 1;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT:
		*channel_out = SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY;
		*instance_out = 0U;
		*output_out = ordinal == 0U ?
			SG_RUNE_ANALYTIC_OUTPUT_POSITION_Z :
			SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
		return 1;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE:
		*instance_out = 0U;
		if (ordinal == 0U) {
			*channel_out = SG_RUNE_WEAPON_EFFECT_CHANNEL_SPLASH;
			*output_out = SG_RUNE_ANALYTIC_OUTPUT_EFFECT_RADIUS;
		} else {
			*channel_out = SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY;
			*output_out = SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
		}
		return 1;
	case SG_RUNE_WEAPON_RESPONSE_BFG:
		if (ordinal < direct_count)
			return DirectFunctionRefExpected(profile, ordinal,
				SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, channel_out,
				instance_out, output_out);
		*instance_out = 0U;
		*output_out = SG_RUNE_ANALYTIC_OUTPUT_EFFECT_RADIUS;
		*channel_out = ordinal == direct_count ?
			SG_RUNE_WEAPON_EFFECT_CHANNEL_SPLASH :
			ordinal == direct_count + 1U ?
			SG_RUNE_WEAPON_EFFECT_CHANNEL_SECONDARY_SPLASH :
			SG_RUNE_WEAPON_EFFECT_CHANNEL_PERIODIC_RAY;
		return 1;
	case SG_RUNE_WEAPON_RESPONSE_SPECIAL:
		if (profile->source_profile == SG_WEAPON_PROFILE_HOOK) {
			if (ordinal < direct_count)
				return DirectFunctionRefExpected(profile, ordinal,
					SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, channel_out,
					instance_out, output_out);
			*channel_out = SG_RUNE_WEAPON_EFFECT_CHANNEL_ATTACHED_EFFECT;
			*instance_out = 0U;
			*output_out = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
			return 1;
		}
		*channel_out = SG_RUNE_WEAPON_EFFECT_CHANNEL_ATTACHED_EFFECT;
		*instance_out = 0U;
		*output_out = profile->source_profile == SG_WEAPON_PROFILE_BFG ?
			SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS :
			SG_RUNE_ANALYTIC_OUTPUT_EFFECT_RADIUS;
		return 1;
	case SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT:
		return 0;
	}
	return 0;
}

int SG_RuneCompactWeaponFunctionRefAllowed(
	const sg_rune_weapon_profile_t *profile,
	sg_rune_weapon_response_family_t family,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance,
	sg_rune_analytic_output_meaning_t output)
{
	uint32_t count;
	uint32_t ordinal;

	if (!SG_RuneCompactWeaponKernelReferenceCount(profile, family, &count))
		return 0;
	for (ordinal = 0U; ordinal < count; ordinal++) {
		sg_rune_weapon_effect_channel_t expected_channel;
		uint32_t expected_instance;
		sg_rune_analytic_output_meaning_t expected_output;

		if (SG_RuneCompactWeaponFunctionRefExpected(profile, family, ordinal,
			&expected_channel, &expected_instance, &expected_output) &&
			expected_channel == channel && expected_instance == instance &&
			expected_output == output)
			return 1;
	}
	return 0;
}

static int Q8VecCompare(const sg_rune_q8_vec3_t *left,
	const sg_rune_q8_vec3_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		const int comparison = CompareI32(left->value[axis],
			right->value[axis]);

		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int Q8VecMatches(const sg_rune_q8_vec3_t *left,
	const sg_rune_q8_vec3_t *right)
{
	return left->value[0] == right->value[0] &&
		left->value[1] == right->value[1] &&
		left->value[2] == right->value[2];
}

static int Binary32CanonicalFinite(uint32_t bits)
{
	return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000) &&
		bits != UINT32_C(0x80000000);
}

static int Binary32Nonnegative(uint32_t bits)
{
	return Binary32CanonicalFinite(bits) &&
		(bits & UINT32_C(0x80000000)) == 0U;
}

static double Binary32Value(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return (double)value;
}

static int Sha256Present(const uint8_t digest[32])
{
	uint32_t index;

	for (index = 0U; index < 32U; index++)
		if (digest[index] != 0U)
			return 1;
	return 0;
}

static int BoundsValid(const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (!bounds)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int PointInHalfOpenBounds(const sg_rune_q8_vec3_t *point,
	const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (point->value[axis] < bounds->mins.value[axis] ||
			point->value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int ControllerLocationZero(
	const sg_rune_compact_mechanism_controller_t *controller)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (controller->activation_witness.value[axis] != 0 ||
			controller->activation_bounds.mins.value[axis] != 0 ||
			controller->activation_bounds.maxs.value[axis] != 0)
			return 0;
	return 1;
}

static int BoundsClosedValid(const sg_rune_q8_bounds_t *bounds)
{
	uint32_t axis;

	if (!bounds)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] > bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int ResponsePatchBoundsValid(
	const sg_rune_compact_response_patch_t *patch,
	const sg_rune_q8_vec3_t *vertices)
{
	uint32_t vertex;
	uint32_t axis;

	if (patch == NULL || vertices == NULL || patch->vertex_count < 3U ||
		!BoundsClosedValid(&patch->bounds))
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		int32_t minimum = vertices[patch->first_vertex].value[axis];
		int32_t maximum = minimum;

		for (vertex = 1U; vertex < patch->vertex_count; vertex++) {
			const int32_t value = vertices[patch->first_vertex + vertex].
				value[axis];

			if (value < minimum)
				minimum = value;
			if (value > maximum)
				maximum = value;
		}
		if (patch->bounds.mins.value[axis] != minimum ||
			patch->bounds.maxs.value[axis] != maximum)
			return 0;
	}
	return 1;
}

static int PlaneValid(const sg_rune_binary32_plane_t *plane)
{
	uint32_t axis;
	int has_normal = 0;

	if (!plane || !Binary32CanonicalFinite(plane->distance_bits))
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		if (!Binary32CanonicalFinite(plane->normal_bits[axis]))
			return 0;
		if ((plane->normal_bits[axis] & UINT32_C(0x7fffffff)) != 0U)
			has_normal = 1;
	}
	return has_normal;
}

static double CrossDot(const double normal[3],
	const sg_rune_q8_vec3_t *origin, const sg_rune_q8_vec3_t *left,
	const sg_rune_q8_vec3_t *right, double *scale_out)
{
	const double left_x = (double)left->value[0] - (double)origin->value[0];
	const double left_y = (double)left->value[1] - (double)origin->value[1];
	const double left_z = (double)left->value[2] - (double)origin->value[2];
	const double right_x = (double)right->value[0] - (double)origin->value[0];
	const double right_y = (double)right->value[1] - (double)origin->value[1];
	const double right_z = (double)right->value[2] - (double)origin->value[2];
	const double cross_x = left_y * right_z - left_z * right_y;
	const double cross_y = left_z * right_x - left_x * right_z;
	const double cross_z = left_x * right_y - left_y * right_x;

	*scale_out = fabs(cross_x * normal[0]) +
		fabs(cross_y * normal[1]) + fabs(cross_z * normal[2]);
	return cross_x * normal[0] + cross_y * normal[1] +
		cross_z * normal[2];
}

static int VertexOnPlane(const sg_rune_q8_vec3_t *vertex,
	const double normal[3], double distance)
{
	const double scaled_distance = distance * 8.0;
	const double terms[3] = {
		normal[0] * (double)vertex->value[0],
		normal[1] * (double)vertex->value[1],
		normal[2] * (double)vertex->value[2]
	};
	const double residual = terms[0] + terms[1] + terms[2] -
		scaled_distance;
	const double quantization_bound = 0.5 *
		(fabs(normal[0]) + fabs(normal[1]) + fabs(normal[2]));
	const double arithmetic_bound = 32.0 * DBL_EPSILON *
		(fabs(terms[0]) + fabs(terms[1]) + fabs(terms[2]) +
		 fabs(scaled_distance) + 1.0);

	return fabs(residual) <= quantization_bound + arithmetic_bound;
}

static facet_polygon_result_t ValidatePolygon(
	const sg_rune_binary32_plane_t *plane,
	const sg_rune_q8_vec3_t *vertices, uint32_t vertex_count)
{
	double normal[3];
	double area = 0.0;
	double area_scale = 0.0;
	double area_tolerance;
	double distance;
	uint32_t vertex_index;

	for (vertex_index = 0U; vertex_index < 3U; vertex_index++)
		normal[vertex_index] = Binary32Value(plane->normal_bits[vertex_index]);
	distance = Binary32Value(plane->distance_bits);
	for (vertex_index = 0U; vertex_index < vertex_count;
		vertex_index++) {
		const uint32_t next =
			(vertex_index + 1U) % vertex_count;
		const int first_comparison = vertex_index == 0U ? -1 :
			Q8VecCompare(&vertices[0], &vertices[vertex_index]);

		if (!VertexOnPlane(&vertices[vertex_index], normal, distance) ||
			Q8VecMatches(&vertices[vertex_index], &vertices[next]))
			return FACET_POLYGON_INVALID_GEOMETRY;
		if (first_comparison == 0)
			return FACET_POLYGON_INVALID_GEOMETRY;
		if (first_comparison > 0)
			return FACET_POLYGON_NONCANONICAL;
	}
	for (vertex_index = 1U; vertex_index + 1U < vertex_count;
		vertex_index++) {
		double scale;
		double triangle;
		double tolerance;

		triangle = CrossDot(normal, &vertices[0], &vertices[vertex_index],
			&vertices[vertex_index + 1U], &scale);
		tolerance = 32.0 * DBL_EPSILON * (scale + 1.0);
		if (triangle <= tolerance)
			return FACET_POLYGON_INVALID_GEOMETRY;
		area += triangle;
		area_scale += scale;
	}
	area_tolerance = 32.0 * DBL_EPSILON * (area_scale + 1.0) *
		(double)vertex_count;
	if (fabs(area) <= area_tolerance)
		return FACET_POLYGON_INVALID_GEOMETRY;
	if (area < 0.0)
		return FACET_POLYGON_NONCANONICAL;

	/* Strict positive turns encode a canonical convex BSP facet. Together with
	 * the unique minimum and distinct neighbors, they exclude every repeated
	 * nonadjacent vertex in one pass. */
	for (vertex_index = 0U; vertex_index < vertex_count;
		vertex_index++) {
		const uint32_t previous = vertex_index == 0U ?
			vertex_count - 1U : vertex_index - 1U;
		const uint32_t next =
			(vertex_index + 1U) % vertex_count;
		double scale;
		const double turn = CrossDot(normal, &vertices[vertex_index],
			&vertices[next], &vertices[previous], &scale);
		const double tolerance = 32.0 * DBL_EPSILON * (scale + 1.0);

		if (turn <= tolerance)
			return FACET_POLYGON_INVALID_GEOMETRY;
	}
	return FACET_POLYGON_VALID;
}

static facet_polygon_result_t ValidateFacetPolygon(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_facet_t *facet)
{
	return ValidatePolygon(&facet->plane,
		&model->vertices[facet->vertices.first], facet->vertices.count);
}

static int PlaneCompare(const sg_rune_binary32_plane_t *left,
	const sg_rune_binary32_plane_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		const int comparison = CompareU32(left->normal_bits[axis],
			right->normal_bits[axis]);

		if (comparison != 0)
			return comparison;
	}
	return CompareU32(left->distance_bits, right->distance_bits);
}

static uint32_t Binary32Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int ResponsePatchContainsPoint(
	const sg_rune_compact_response_projection_t *response,
	const sg_rune_compact_response_patch_t *patch,
	const sg_rune_q8_vec3_t *point)
{
	double previous = 0.0;
	double normal[3];
	double residual;
	uint32_t edge;
	uint32_t axis;

	if (response == NULL || patch == NULL || point == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		if (point->value[axis] < patch->bounds.mins.value[axis] ||
			point->value[axis] > patch->bounds.maxs.value[axis])
			return 0;
		normal[axis] = Binary32Value(patch->plane.normal_bits[axis]);
	}
	residual = -Binary32Value(patch->plane.distance_bits);
	for (axis = 0U; axis < 3U; axis++)
		residual += normal[axis] *
			((double)point->value[axis] / 8.0);
	if (fabs(residual) > 0.126)
		return 0;
	for (edge = 0U; edge < patch->vertex_count; edge++) {
		const sg_rune_q8_vec3_t *from = &response->target_vertices[
			patch->first_vertex + edge];
		const sg_rune_q8_vec3_t *to = &response->target_vertices[
			patch->first_vertex + (edge + 1U) % patch->vertex_count];
		double edge_vector[3];
		double point_vector[3];
		double cross[3];
		double side = 0.0;

		for (axis = 0U; axis < 3U; axis++) {
			edge_vector[axis] = (double)to->value[axis] -
				(double)from->value[axis];
			point_vector[axis] = (double)point->value[axis] -
				(double)from->value[axis];
		}
		cross[0] = edge_vector[1] * point_vector[2] -
			edge_vector[2] * point_vector[1];
		cross[1] = edge_vector[2] * point_vector[0] -
			edge_vector[0] * point_vector[2];
		cross[2] = edge_vector[0] * point_vector[1] -
			edge_vector[1] * point_vector[0];
		for (axis = 0U; axis < 3U; axis++)
			side += cross[axis] * normal[axis];
		if (fabs(side) <= 0.001)
			continue;
		if (previous != 0.0 && ((previous < 0.0) != (side < 0.0)))
			return 0;
		previous = side;
	}
	return 1;
}

static int ResponseTraceFiniteAndBound(
	const sg_rune_compact_response_projection_t *response,
	const sg_rune_compact_response_fact_t *fact)
{
	const sg_rune_compact_response_fragment_t *source =
		&response->source_fragments[fact->source_fragment];
	const sg_host_collision_trace_t *trace = &fact->trace;
	uint32_t axis;

	if (trace->allsolid != 0 || trace->startsolid != 0 ||
		!Binary32CanonicalFinite(Binary32Bits(trace->fraction)) ||
		trace->fraction <= 0.0f || trace->fraction > 1.0f ||
		!Binary32CanonicalFinite(Binary32Bits(trace->plane.distance)) ||
		trace->model_index != SG_HOST_COLLISION_MODEL_WORLD ||
		trace->instance_id != 0U ||
		!ResponsePatchContainsPoint(response,
			&response->target_patches[fact->target_patch],
			&fact->target_witness))
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		const double origin = (double)source->witness.value[axis] / 8.0;
		const double target = (double)fact->target_witness.value[axis] / 8.0;
		const double expected = origin + (double)trace->fraction *
			(target - origin);

		if (!Binary32CanonicalFinite(Binary32Bits(trace->end[axis])) ||
			!Binary32CanonicalFinite(Binary32Bits(trace->plane.normal[axis])) ||
			fabs((double)trace->end[axis] - expected) > 0.126)
			return 0;
	}
	return 1;
}

static int ResponseTraceCanonicalNoHit(const sg_host_collision_trace_t *trace)
{
	uint32_t axis;

	if (trace->allsolid != 0 || trace->startsolid != 0 ||
		Binary32Bits(trace->fraction) != Binary32Bits(1.0f) ||
		trace->contents != 0U || trace->texinfo !=
			SG_HOST_COLLISION_TEXINFO_NONE || trace->surface_flags != 0 ||
		trace->model_index != SG_HOST_COLLISION_MODEL_WORLD ||
		trace->instance_id != 0U ||
		trace->brush != SG_HOST_COLLISION_BRUSH_NONE ||
		trace->brush_side != SG_HOST_COLLISION_BRUSH_NONE ||
		Binary32Bits(trace->plane.distance) != 0U || trace->plane.type != 0)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (Binary32Bits(trace->plane.normal[axis]) != 0U)
			return 0;
	return 1;
}

static int ResponseTraceEndsAtTarget(const sg_host_collision_trace_t *trace,
	const sg_rune_q8_vec3_t *target)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (fabs((double)trace->end[axis] -
			(double)target->value[axis] / 8.0) > 0.126)
			return 0;
	return 1;
}

static int ResponseTracePlaneMatchesSplit(const sg_host_collision_trace_t *trace,
	const sg_rune_binary32_plane_t *split)
{
	double same = 0.0;
	double opposite = 0.0;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		const double normal = Binary32Value(split->normal_bits[axis]);
		const double same_axis = fabs((double)trace->plane.normal[axis] -
			normal);
		const double opposite_axis = fabs((double)trace->plane.normal[axis] +
			normal);

		if (same_axis > same)
			same = same_axis;
		if (opposite_axis > opposite)
			opposite = opposite_axis;
	}
	return (same <= 0.0001 &&
		fabs((double)trace->plane.distance -
			Binary32Value(split->distance_bits)) <= 0.001) ||
		(opposite <= 0.0001 &&
		 fabs((double)trace->plane.distance +
			Binary32Value(split->distance_bits)) <= 0.001);
}

static int ResponseImpactTraceValid(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_response_projection_t *response,
	const sg_rune_compact_response_fact_t *fact)
{
	const sg_rune_compact_response_split_t *split;
	const sg_rune_compact_static_occluder_t *occluder;
	double residual;
	uint32_t axis;

	if (fact->occluders.count != 1U ||
		fact->certificate_split >= response->split_count)
		return 0;
	split = &response->splits[fact->certificate_split];
	if (split->kind != SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE ||
		split->occluder != fact->occluders.first ||
		split->occluder >= response->occluder_count ||
		!PlaneValid(&split->plane))
		return 0;
	occluder = &response->occluders[split->occluder];
	if (fact->trace.brush != occluder->brush ||
		split->brush_side == SG_HOST_COLLISION_BRUSH_NONE ||
		split->brush_side >=
			model->identity.source_counts.brush_side_count ||
		fact->trace.brush_side != split->brush_side ||
		!ResponseTracePlaneMatchesSplit(&fact->trace, &split->plane) ||
		(fact->trace.contents &
			(SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW)) == 0U)
		return 0;
	residual = -Binary32Value(split->plane.distance_bits);
	for (axis = 0U; axis < 3U; axis++)
		residual += Binary32Value(split->plane.normal_bits[axis]) *
			(double)fact->trace.end[axis];
	return fabs(residual) <= 0.001;
}

static int ResponseSplitTargetSurfaceValid(
	const sg_rune_compact_response_projection_t *response,
	uint64_t target_surface_id, uint32_t edge, int require_edge)
{
	uint32_t patch;

	for (patch = 0U; patch < response->target_patch_count; patch++)
		if (response->target_patches[patch].visibility_surface_id ==
			target_surface_id && (!require_edge || edge <
			response->target_patches[patch].vertex_count))
			return 1;
	return 0;
}

static int ResponseSplitCompare(const sg_rune_compact_response_split_t *left,
	const sg_rune_compact_response_split_t *right)
{
	int comparison = PlaneCompare(&left->plane, &right->plane);

	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	if (comparison == 0)
		comparison = CompareU64(left->target_surface_id,
			right->target_surface_id);
	if (comparison == 0)
		comparison = CompareU32(left->occluder, right->occluder);
	if (comparison == 0)
		comparison = CompareU32(left->edge, right->edge);
	if (comparison == 0)
		comparison = CompareU32(left->brush_side, right->brush_side);
	return comparison;
}

static int ResponseSplitValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_response_projection_t *response,
	const sg_rune_compact_response_split_t *split)
{
	if (!PlaneValid(&split->plane) || (uint32_t)split->kind >=
		(uint32_t)SG_RUNE_COMPACT_RESPONSE_SPLIT_KIND_COUNT)
		return 0;
	switch (split->kind)
	{
	case SG_RUNE_COMPACT_RESPONSE_SPLIT_TARGET_EDGE:
		return split->target_surface_id != UINT64_MAX &&
			split->occluder == SG_RUNE_COMPACT_INDEX_NONE &&
			split->brush_side == SG_HOST_COLLISION_BRUSH_NONE &&
			ResponseSplitTargetSurfaceValid(response, split->target_surface_id,
				split->edge, 1);
	case SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE:
		return split->target_surface_id == UINT64_MAX &&
			split->occluder < response->occluder_count &&
			split->edge != SG_RUNE_COMPACT_INDEX_NONE &&
			split->brush_side != SG_HOST_COLLISION_BRUSH_NONE &&
			split->brush_side <
				model->identity.source_counts.brush_side_count;
	case SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_EDGE:
		return split->target_surface_id == UINT64_MAX &&
			split->occluder < response->occluder_count &&
			split->edge != SG_RUNE_COMPACT_INDEX_NONE &&
			split->brush_side == SG_HOST_COLLISION_BRUSH_NONE;
	case SG_RUNE_COMPACT_RESPONSE_SPLIT_FIRST_HIT_TIE:
		return split->target_surface_id != UINT64_MAX &&
			split->occluder < response->occluder_count &&
			split->edge != SG_RUNE_COMPACT_INDEX_NONE &&
			split->brush_side == SG_HOST_COLLISION_BRUSH_NONE &&
			ResponseSplitTargetSurfaceValid(response, split->target_surface_id,
				split->edge, 0);
	case SG_RUNE_COMPACT_RESPONSE_SPLIT_KIND_COUNT:
	default:
		return 0;
	}
}

static int HullValid(const sg_rune_compact_hull_t *hull)
{
	const sg_rune_q8_bounds_t bounds = { hull->mins, hull->maxs };

	return BoundsValid(&bounds);
}

static int HullMatches(const sg_rune_compact_hull_t *left,
	const sg_rune_compact_hull_t *right)
{
	return Q8VecMatches(&left->mins, &right->mins) &&
		Q8VecMatches(&left->maxs, &right->maxs);
}

static int PhysicsValid(const sg_rune_compact_physics_t *physics)
{
	const uint32_t values[] = {
		physics->gravity_bits, physics->ground_acceleration_bits,
		physics->air_acceleration_bits, physics->water_acceleration_bits,
		physics->hook_acceleration_bits, physics->external_acceleration_bits,
		physics->water_drag_bits, physics->max_velocity_bits
	};
	uint32_t index;

	for (index = 0U; index < sizeof(values) / sizeof(values[0]); index++)
		if (!Binary32Nonnegative(values[index]))
			return 0;
	return physics->gravity_bits != 0U && physics->max_velocity_bits != 0U &&
		physics->frame_ms != 0U && physics->substep_ms != 0U &&
		physics->substep_ms <= physics->frame_ms;
}

static int PhysicsMatches(const sg_rune_compact_physics_t *left,
	const sg_rune_compact_physics_t *right)
{
	return left->gravity_bits == right->gravity_bits &&
		left->ground_acceleration_bits == right->ground_acceleration_bits &&
		left->air_acceleration_bits == right->air_acceleration_bits &&
		left->water_acceleration_bits == right->water_acceleration_bits &&
		left->hook_acceleration_bits == right->hook_acceleration_bits &&
		left->external_acceleration_bits == right->external_acceleration_bits &&
		left->water_drag_bits == right->water_drag_bits &&
		left->max_velocity_bits == right->max_velocity_bits &&
		left->frame_ms == right->frame_ms &&
		left->substep_ms == right->substep_ms;
}

static int SourceCountsValid(const sg_rune_compact_source_counts_t *counts)
{
	return counts->model_count != 0U && counts->leaf_count != 0U &&
		counts->area_count != 0U && counts->plane_count != 0U &&
		counts->entity_count != 0U &&
		(counts->brush_count == 0U) == (counts->brush_side_count == 0U);
}

static int SourceCountsMatch(const sg_rune_compact_source_counts_t *left,
	const sg_rune_compact_source_counts_t *right)
{
	return left->model_count == right->model_count &&
		left->leaf_count == right->leaf_count &&
		left->area_count == right->area_count &&
		left->plane_count == right->plane_count &&
		left->brush_count == right->brush_count &&
		left->brush_side_count == right->brush_side_count &&
		left->entity_count == right->entity_count;
}

int SG_RuneCompactIdentityMatches(
	const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	uint32_t digest_byte;

	if (!actual || !expected)
		return 0;
	for (digest_byte = 0U; digest_byte < 32U; digest_byte++)
		if (actual->bsp_sha256[digest_byte] !=
			expected->bsp_sha256[digest_byte])
			return 0;
	return actual->bsp_bytes == expected->bsp_bytes &&
		actual->bsp_checksum == expected->bsp_checksum &&
		actual->entity_crc32 == expected->entity_crc32 &&
		actual->entity_semantics_id == expected->entity_semantics_id &&
		actual->physics_abi_id == expected->physics_abi_id &&
		actual->collision_law_id == expected->collision_law_id &&
		actual->pmove_law_id == expected->pmove_law_id &&
		actual->gravity_law_id == expected->gravity_law_id &&
		actual->hook_law_id == expected->hook_law_id &&
		actual->mechanism_law_id == expected->mechanism_law_id &&
		actual->weapon_law_id == expected->weapon_law_id &&
		actual->weapon_profile_catalog_id ==
			expected->weapon_profile_catalog_id &&
		actual->construction_id == expected->construction_id &&
		actual->schema_id == expected->schema_id &&
		actual->producer_identity == expected->producer_identity &&
		SourceCountsMatch(&actual->source_counts, &expected->source_counts) &&
		HullMatches(&actual->standing_hull, &expected->standing_hull) &&
		HullMatches(&actual->crouching_hull, &expected->crouching_hull) &&
		PhysicsMatches(&actual->physics, &expected->physics);
}

static int IdentityValid(const sg_rune_compact_identity_t *identity)
{
	return identity && Sha256Present(identity->bsp_sha256) &&
		identity->bsp_bytes != 0U && identity->entity_semantics_id != 0U &&
		identity->physics_abi_id != 0U && identity->collision_law_id != 0U &&
		identity->pmove_law_id != 0U && identity->gravity_law_id != 0U &&
		identity->hook_law_id != 0U && identity->mechanism_law_id != 0U &&
		identity->weapon_law_id != 0U &&
		identity->weapon_profile_catalog_id != 0U &&
		identity->construction_id != 0U &&
		identity->schema_id != 0U && identity->producer_identity != 0U &&
		SourceCountsValid(&identity->source_counts) &&
		HullValid(&identity->standing_hull) &&
		HullValid(&identity->crouching_hull) && PhysicsValid(&identity->physics);
}

static int StancesValid(sg_rune_stance_validity_t stances)
{
	return stances != 0U &&
		(stances & (sg_rune_stance_validity_t)~SG_RUNE_STANCE_VALID_ALL) == 0U;
}

static int CellSourceValid(const sg_rune_compact_cell_source_t *source,
	const sg_rune_compact_source_counts_t *counts)
{
	return source && source->model < counts->model_count &&
		source->leaf < counts->leaf_count && source->area < counts->area_count &&
		source->cluster >= -1 &&
		source->split_ordinal != UINT32_MAX;
}

static int CellSourceCompare(const sg_rune_compact_cell_source_t *left,
	const sg_rune_compact_cell_source_t *right)
{
	int comparison = CompareU32(left->model, right->model);

	if (comparison == 0)
		comparison = CompareU32(left->leaf, right->leaf);
	if (comparison == 0)
		comparison = CompareU32(left->area, right->area);
	if (comparison == 0)
		comparison = CompareI32(left->cluster, right->cluster);
	if (comparison == 0)
		comparison = CompareU32(left->split_ordinal, right->split_ordinal);
	return comparison;
}

static int SourceValid(const sg_rune_compact_source_t *source,
	uint32_t split_parent_limit,
	const sg_rune_compact_source_counts_t *counts)
{
	if (!source || source->kind < 0 ||
		source->kind >= SG_RUNE_COMPACT_SOURCE_KIND_COUNT)
		return 0;
	switch (source->kind) {
	case SG_RUNE_COMPACT_SOURCE_DOMAIN:
		return source->value.domain.axis < 3U &&
			source->value.domain.maximum_side < 2U;
	case SG_RUNE_COMPACT_SOURCE_BSP_PLANE:
		return source->value.bsp_plane.model < counts->model_count &&
			source->value.bsp_plane.leaf < counts->leaf_count &&
			source->value.bsp_plane.plane < counts->plane_count;
	case SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE:
		return source->value.brush_side.model < counts->model_count &&
			source->value.brush_side.brush < counts->brush_count &&
			source->value.brush_side.brush_side < counts->brush_side_count &&
			source->value.brush_side.plane < counts->plane_count;
	case SG_RUNE_COMPACT_SOURCE_SPLIT:
		return source->value.split.parent_facet.value < split_parent_limit &&
			source->value.split.ordinal != UINT32_MAX;
	case SG_RUNE_COMPACT_SOURCE_KIND_COUNT:
		break;
	}
	return 0;
}

static int SourceCompare(const sg_rune_compact_source_t *left,
	const sg_rune_compact_source_t *right)
{
	int comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);

	if (comparison != 0)
		return comparison;
	switch (left->kind) {
	case SG_RUNE_COMPACT_SOURCE_DOMAIN:
		comparison = CompareU32(left->value.domain.axis,
			right->value.domain.axis);
		return comparison != 0 ? comparison :
			CompareU32(left->value.domain.maximum_side,
				right->value.domain.maximum_side);
	case SG_RUNE_COMPACT_SOURCE_BSP_PLANE:
		comparison = CompareU32(left->value.bsp_plane.model,
			right->value.bsp_plane.model);
		if (comparison == 0)
			comparison = CompareU32(left->value.bsp_plane.leaf,
				right->value.bsp_plane.leaf);
		if (comparison == 0)
			comparison = CompareU32(left->value.bsp_plane.plane,
				right->value.bsp_plane.plane);
		return comparison;
	case SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE:
		comparison = CompareU32(left->value.brush_side.model,
			right->value.brush_side.model);
		if (comparison == 0)
			comparison = CompareU32(left->value.brush_side.brush,
				right->value.brush_side.brush);
		if (comparison == 0)
			comparison = CompareU32(left->value.brush_side.brush_side,
				right->value.brush_side.brush_side);
		if (comparison == 0)
			comparison = CompareU32(left->value.brush_side.plane,
				right->value.brush_side.plane);
		return comparison;
	case SG_RUNE_COMPACT_SOURCE_SPLIT:
		comparison = CompareU32(left->value.split.parent_facet.value,
			right->value.split.parent_facet.value);
		return comparison != 0 ? comparison :
			CompareU32(left->value.split.ordinal,
				right->value.split.ordinal);
	case SG_RUNE_COMPACT_SOURCE_KIND_COUNT:
		break;
	}
	return 0;
}

static int FacetCompare(const sg_rune_compact_model_t *model,
	const sg_rune_compact_facet_t *left,
	const sg_rune_compact_facet_t *right)
{
	int comparison = SourceCompare(&left->source, &right->source);

	if (comparison == 0)
		comparison = PlaneCompare(&left->plane, &right->plane);
	if (comparison == 0)
		comparison = CompareU32(left->vertices.first, right->vertices.first);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	if (comparison == 0 &&
		left->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY)
		comparison = CompareU32(
			model->incidences[left->incidences.first].cell.value,
			model->incidences[right->incidences.first].cell.value);
	return comparison;
}

static int PortalTouchesCell(const sg_rune_compact_model_t *model,
	uint32_t portal_index, uint32_t cell_index)
{
	const sg_rune_compact_portal_t *portal;

	if (portal_index >= model->portal_count)
		return 0;
	portal = &model->portals[portal_index];
	if (portal->negative_incidence.value >= model->incidence_count ||
		portal->positive_incidence.value >= model->incidence_count)
		return 0;
	return model->incidences[portal->negative_incidence.value].cell.value ==
			cell_index ||
		model->incidences[portal->positive_incidence.value].cell.value ==
			cell_index;
}

static int ValidateCounts(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	if (model->analytic == NULL || model->static_data == NULL) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (model->cell_count == 0U || model->facet_count == 0U ||
		model->incidence_count == 0U || model->cell_incidence_count == 0U ||
		model->movement_capability_count == 0U ||
		model->weapon_profile_count == 0U ||
		model->weapon_kernel_count == 0U ||
		model->weapon_attachment_count == 0U ||
		model->weapon_relation_span_count == 0U ||
		model->weapon_relation_ref_count == 0U ||
		model->weapon_function_ref_count == 0U ||
		model->response.fact_count == 0U ||
		model->cell_count > SG_RUNE_COMPACT_MAX_CELLS ||
		model->facet_count > SG_RUNE_COMPACT_MAX_FACETS ||
		model->incidence_count > SG_RUNE_COMPACT_MAX_INCIDENCES ||
		model->cell_incidence_count > SG_RUNE_COMPACT_MAX_INCIDENCES ||
		model->vertex_count > SG_RUNE_COMPACT_MAX_VERTICES ||
		model->portal_count > SG_RUNE_COMPACT_MAX_PORTALS ||
		model->source_surface_count >
			SG_RUNE_COMPACT_MAX_SOURCE_SURFACES ||
		model->source_surface_vertex_count >
			SG_RUNE_COMPACT_MAX_SOURCE_SURFACE_VERTICES ||
		model->movement_capability_count > SG_RUNE_COMPACT_MAX_MOVEMENT_FIELDS ||
		model->movement_state_count > SG_RUNE_COMPACT_MAX_MOVEMENT_STATES ||
		model->movement_fiber_count > SG_RUNE_COMPACT_MAX_MOVEMENT_FIBERS ||
		model->movement_hook_target_count >
			SG_RUNE_COMPACT_MAX_MOVEMENT_HOOK_TARGETS ||
		model->movement_fiber_function_ref_count >
			SG_RUNE_COMPACT_MAX_MOVEMENT_FIBER_FUNCTION_REFS ||
		model->movement_angular_schedule_count >
			SG_RUNE_COMPACT_MAX_MOVEMENT_ANGULAR_SCHEDULES ||
		model->weapon_profile_count > SG_RUNE_COMPACT_MAX_WEAPON_PROFILES ||
		model->weapon_kernel_count > SG_RUNE_COMPACT_MAX_WEAPON_KERNELS ||
		model->weapon_attachment_count >
			SG_RUNE_COMPACT_MAX_WEAPON_ATTACHMENTS ||
		model->weapon_relation_span_count >
			SG_RUNE_COMPACT_MAX_WEAPON_RELATION_SPANS ||
		model->weapon_relation_ref_count >
			SG_RUNE_COMPACT_MAX_WEAPON_RELATION_REFS ||
		model->weapon_function_ref_count >
			SG_RUNE_COMPACT_MAX_WEAPON_FUNCTION_REFS ||
		model->mechanism_authority_count >
			SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITIES ||
		model->mechanism_authority_controller_count >
			SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_CONTROLLERS ||
		model->mechanism_authority_topology_edge_count >
			SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_TOPOLOGY_EDGES ||
		model->mechanism_authority_transition_count >
			SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_TRANSITIONS ||
		model->response.source_fragment_count >
			SG_RUNE_COMPACT_MAX_RESPONSE_FRAGMENTS ||
		model->response.source_halfspace_count >
			SG_RUNE_COMPACT_MAX_RESPONSE_HALFSPACES ||
		model->response.target_patch_count >
			SG_RUNE_COMPACT_MAX_RESPONSE_PATCHES ||
		model->response.target_vertex_count >
			SG_RUNE_COMPACT_MAX_RESPONSE_PATCH_VERTICES ||
		model->response.split_count > SG_RUNE_COMPACT_MAX_RESPONSE_SPLITS ||
		model->response.fact_count > SG_RUNE_COMPACT_MAX_RESPONSE_FACTS ||
		model->response.candidate_group_count >
			SG_RUNE_COMPACT_MAX_RESPONSE_CANDIDATE_GROUPS ||
		model->response.source_endpoint_group_count >
			SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_GROUPS ||
		model->response.target_endpoint_group_count >
			SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_GROUPS ||
		model->response.source_endpoint_member_count >
			SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_MEMBERS ||
		model->response.target_endpoint_member_count >
			SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_MEMBERS ||
		model->response.occluder_count >
			SG_RUNE_COMPACT_MAX_STATIC_OCCLUDERS) {
		SetError(error, SG_RUNE_COMPACT_ERROR_LIMIT_EXCEEDED,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (!ArrayPresent(model->cells, model->cell_count) ||
		!ArrayPresent(model->facets, model->facet_count) ||
		!ArrayPresent(model->incidences, model->incidence_count) ||
		!ArrayPresent(model->cell_incidences, model->cell_incidence_count) ||
		!ArrayPresent(model->vertices, model->vertex_count) ||
		!ArrayPresent(model->portals, model->portal_count) ||
		!ArrayPresent(model->source_surfaces, model->source_surface_count) ||
		!ArrayPresent(model->source_surface_vertices,
			model->source_surface_vertex_count) ||
		!ArrayPresent(model->movement_capabilities,
			model->movement_capability_count) ||
		!ArrayPresent(model->movement_states, model->movement_state_count) ||
		!ArrayPresent(model->movement_fibers, model->movement_fiber_count) ||
		!ArrayPresent(model->movement_hook_targets,
			model->movement_hook_target_count) ||
		!ArrayPresent(model->movement_fiber_function_refs,
			model->movement_fiber_function_ref_count) ||
		!ArrayPresent(model->movement_angular_schedules,
			model->movement_angular_schedule_count) ||
		!ArrayPresent(model->weapon_profiles, model->weapon_profile_count) ||
		!ArrayPresent(model->weapon_kernels, model->weapon_kernel_count) ||
		!ArrayPresent(model->weapon_attachments,
			model->weapon_attachment_count) ||
		!ArrayPresent(model->weapon_relation_spans,
			model->weapon_relation_span_count) ||
		!ArrayPresent(model->weapon_relation_refs,
			model->weapon_relation_ref_count) ||
		!ArrayPresent(model->weapon_function_refs,
			model->weapon_function_ref_count) ||
		!ArrayPresent(model->mechanism_authorities,
			model->mechanism_authority_count) ||
		!ArrayPresent(model->mechanism_authority_controllers,
			model->mechanism_authority_controller_count) ||
		!ArrayPresent(model->mechanism_authority_topology_edges,
			model->mechanism_authority_topology_edge_count) ||
		!ArrayPresent(model->mechanism_authority_transitions,
			model->mechanism_authority_transition_count) ||
		!ArrayPresent(model->mechanism_authority_transition_static_indices,
			model->mechanism_authority_transition_count) ||
		!ArrayPresent(model->static_transition_authority_indices,
			model->mechanism_authority_transition_count)) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

static int ValidateAnalyticUses(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t *uses;
	uint32_t index;

	uses = (uint32_t *)calloc(model->analytic->function_count, sizeof(*uses));
	if (uses == NULL) {
		SetError(error, SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	for (index = 0U; index < model->weapon_function_ref_count; index++) {
		const uint32_t function = model->weapon_function_refs[index].function.value;

		if (function >= model->analytic->function_count) {
			free(uses);
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
				SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, index);
			return 0;
		}
		uses[function]++;
	}
	for (index = 0U; index < model->movement_fiber_function_ref_count;
		index++) {
		const uint32_t function =
			model->movement_fiber_function_refs[index].value;

		if (function >= model->analytic->function_count) {
			free(uses);
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, index);
			return 0;
		}
		uses[function]++;
	}
	for (index = 0U; index < model->analytic->piecewise_count; index++) {
		const sg_rune_analytic_piecewise_t *piecewise =
			&model->analytic->piecewise[index];
		uint32_t clause;

		uses[piecewise->default_function.value]++;
		for (clause = piecewise->clauses.first;
			clause < piecewise->clauses.first + piecewise->clauses.count;
			clause++)
			uses[model->analytic->piecewise_clauses[clause].function.value]++;
	}
	for (index = 0U; index < model->analytic->function_count; index++) {
		if (uses[index] == 0U) {
			free(uses);
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_MODEL, index);
			return 0;
		}
	}
	free(uses);
	return 1;
}

static int ValidateCells(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t cell_index;
	uint32_t incidence_cursor = 0U;
	uint32_t movement_cursor = 0U;

	for (cell_index = 0U; cell_index < model->cell_count; cell_index++) {
		const sg_rune_compact_cell_t *cell = &model->cells[cell_index];
		uint32_t local;

		if (!CellSourceValid(&cell->source, &model->identity.source_counts)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if (cell_index != 0U && CellSourceCompare(
				&model->cells[cell_index - 1U].source, &cell->source) >= 0) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if (!BoundsValid(&cell->bounds)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if (!StancesValid(cell->valid_stances)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_STANCE,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if (!ReservedBytesZero(cell->reserved)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if ((cell->contents &
				~(sg_rune_compact_contents_mask_t)
					SG_RUNE_COMPACT_CONTENTS_KNOWN) != 0U ||
			(cell->semantics &
				~(sg_rune_compact_cell_semantics_t)
					SG_RUNE_COMPACT_CELL_SEMANTICS_KNOWN) != 0U) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		if (cell->incidences.count == 0U || cell->movement_fields.count == 0U ||
			cell->incidences.first != incidence_cursor ||
			!SpanWithin(cell->incidences.first, cell->incidences.count,
				model->cell_incidence_count) ||
			cell->movement_fields.first != movement_cursor ||
			!SpanWithin(cell->movement_fields.first, cell->movement_fields.count,
				model->movement_capability_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_CELL, cell_index);
			return 0;
		}
		for (local = 0U; local < cell->incidences.count; local++) {
			const uint32_t reference = cell->incidences.first + local;
			const uint32_t incidence = model->cell_incidences[reference].value;

			if (incidence >= model->incidence_count ||
				model->incidences[incidence].cell.value != cell_index ||
				model->incidences[incidence].cell_ordinal != local) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_CELL, cell_index);
				return 0;
			}
		}
		incidence_cursor += cell->incidences.count;
		movement_cursor += cell->movement_fields.count;
	}
	if (incidence_cursor != model->cell_incidence_count ||
		incidence_cursor != model->incidence_count ||
		movement_cursor != model->movement_capability_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

static int ValidateFacetsAndIncidences(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t facet_index;
	uint32_t incidence_cursor = 0U;
	uint32_t vertex_cursor = 0U;

	for (facet_index = 0U; facet_index < model->facet_count; facet_index++) {
		const sg_rune_compact_facet_t *facet = &model->facets[facet_index];
		uint32_t incidence_index;

		if (!SourceValid(&facet->source, facet_index,
			&model->identity.source_counts)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE,
				SG_RUNE_COMPACT_RECORD_FACET, facet_index);
			return 0;
		}
		if (facet->kind < 0 || facet->kind >= SG_RUNE_COMPACT_FACET_KIND_COUNT ||
			!PlaneValid(&facet->plane) ||
			facet->vertices.first != vertex_cursor ||
			!SpanWithin(facet->vertices.first, facet->vertices.count,
				model->vertex_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RECORD_FACET, facet_index);
			return 0;
		}
		if (facet->kind == SG_RUNE_COMPACT_FACET_POLYGON) {
			facet_polygon_result_t polygon_result;

			if (facet->vertices.count < 3U) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
					SG_RUNE_COMPACT_RECORD_FACET, facet_index);
				return 0;
			}
			polygon_result = ValidateFacetPolygon(model, facet);
			if (polygon_result != FACET_POLYGON_VALID) {
				SetError(error,
					polygon_result == FACET_POLYGON_NONCANONICAL ?
						SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER :
						SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
					SG_RUNE_COMPACT_RECORD_FACET, facet_index);
				return 0;
			}
		} else if (facet->vertices.count != 0U) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RECORD_FACET, facet_index);
			return 0;
		}
		if (facet->incidences.first != incidence_cursor ||
			(facet->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY ?
				facet->incidences.count != 1U :
				(facet->incidences.count != 1U &&
				 facet->incidences.count != 2U)) ||
			!SpanWithin(facet->incidences.first, facet->incidences.count,
				model->incidence_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
				SG_RUNE_COMPACT_RECORD_FACET, facet_index);
			return 0;
		}
		for (incidence_index = facet->incidences.first;
			incidence_index < facet->incidences.first + facet->incidences.count;
			incidence_index++) {
			const sg_rune_compact_incidence_t *incidence =
				&model->incidences[incidence_index];

			if (incidence->cell.value >= model->cell_count ||
				incidence->facet.value != facet_index ||
				incidence->side < 0 ||
				incidence->side >= SG_RUNE_FACET_SIDE_COUNT ||
				incidence->boundary < 0 ||
				incidence->boundary >= SG_RUNE_BOUNDARY_OWNERSHIP_COUNT ||
				incidence->cell_ordinal >=
					model->cells[incidence->cell.value].incidences.count ||
				model->cell_incidences[
					model->cells[incidence->cell.value].incidences.first +
					incidence->cell_ordinal].value != incidence_index) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_INCIDENCE, incidence_index);
				return 0;
			}
			if (incidence_index != facet->incidences.first) {
				const sg_rune_compact_incidence_t *previous = incidence - 1;

				if (previous->side > incidence->side ||
					(previous->side == incidence->side &&
					 previous->cell.value >= incidence->cell.value)) {
					SetError(error,
						SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
						SG_RUNE_COMPACT_RECORD_INCIDENCE,
						incidence_index);
					return 0;
				}
			}
		}
		if (facet->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY ||
			facet->incidences.count == 1U) {
			if (facet->portal.value != SG_RUNE_COMPACT_INDEX_NONE) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
					SG_RUNE_COMPACT_RECORD_FACET, facet_index);
				return 0;
			}
		} else {
			const sg_rune_compact_incidence_t *negative =
				&model->incidences[facet->incidences.first];
			const sg_rune_compact_incidence_t *positive = negative + 1;

			if (facet->portal.value >= model->portal_count ||
				model->portals[facet->portal.value].facet.value != facet_index ||
				negative->cell.value == positive->cell.value ||
				negative->side != SG_RUNE_FACET_NEGATIVE_SIDE ||
				positive->side != SG_RUNE_FACET_POSITIVE_SIDE ||
				negative->boundary == positive->boundary) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
					SG_RUNE_COMPACT_RECORD_FACET, facet_index);
				return 0;
			}
		}
		if (facet_index != 0U && FacetCompare(model,
				&model->facets[facet_index - 1U], facet) >= 0) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_FACET, facet_index);
			return 0;
		}
		vertex_cursor += facet->vertices.count;
		incidence_cursor += facet->incidences.count;
	}
	if (vertex_cursor != model->vertex_count ||
		incidence_cursor != model->incidence_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

static int SourceSurfaceProvenanceCompare(
	const sg_rune_compact_source_surface_t *left,
	const sg_rune_compact_source_surface_t *right)
{
	int comparison;

	comparison = CompareU32(left->source.model, right->source.model);
	if (comparison != 0)
		return comparison;
	comparison = CompareU32(left->source.brush, right->source.brush);
	if (comparison != 0)
		return comparison;
	comparison = CompareU32(left->source.brush_side,
		right->source.brush_side);
	if (comparison != 0)
		return comparison;
	return CompareU32(left->source.plane, right->source.plane);
}

static int SourceSurfaceRootValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_source_surface_t *surface)
{
	return surface->cell.value == SG_RUNE_COMPACT_INDEX_NONE &&
		surface->parent_surface == SG_RUNE_COMPACT_INDEX_NONE &&
		surface->split_ordinal == 0U &&
		surface->frame == (surface->source.model == 0U ?
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD :
			SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL) &&
		surface->source.model < model->identity.source_counts.model_count &&
		surface->source.brush < model->identity.source_counts.brush_count &&
		surface->source.brush_side <
			model->identity.source_counts.brush_side_count &&
		surface->source.plane < model->identity.source_counts.plane_count;
}

static int SourceSurfaceChildValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_source_surface_t *surface,
	const sg_rune_compact_source_surface_t *root, uint32_t root_index,
	uint32_t surface_index)
{
	return surface->parent_surface == root_index &&
		root->parent_surface == SG_RUNE_COMPACT_INDEX_NONE &&
		surface_index > root_index && surface->split_ordinal != 0U &&
		surface->cell.value < model->cell_count &&
		model->cells[surface->cell.value].source.model == 0U &&
		surface->source.model == 0U &&
		surface->frame == SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD &&
		memcmp(&surface->source, &root->source,
			sizeof(surface->source)) == 0 &&
		PlaneCompare(&surface->plane, &root->plane) == 0;
}

static int ValidateSourceSurfaces(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t vertex_cursor = 0U;
	uint32_t current_root = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t last_root = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t last_child = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t index;

	for (index = 0U; index < model->source_surface_count; index++) {
		const sg_rune_compact_source_surface_t *surface =
			&model->source_surfaces[index];
		const int is_root =
			surface->parent_surface == SG_RUNE_COMPACT_INDEX_NONE;

		if (!PlaneValid(&surface->plane) ||
			surface->vertices.first != vertex_cursor ||
			surface->vertices.count < 3U ||
			!SpanWithin(surface->vertices.first, surface->vertices.count,
				model->source_surface_vertex_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE, index);
			return 0;
		}
		if (is_root) {
			if (!SourceSurfaceRootValid(model, surface) ||
				(last_root != SG_RUNE_COMPACT_INDEX_NONE &&
				 SourceSurfaceProvenanceCompare(
					&model->source_surfaces[last_root], surface) >= 0)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE,
					SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE, index);
				return 0;
			}
			last_root = index;
			current_root = index;
			last_child = SG_RUNE_COMPACT_INDEX_NONE;
		} else {
			const sg_rune_compact_source_surface_t *root;

			if (current_root == SG_RUNE_COMPACT_INDEX_NONE ||
				surface->parent_surface != current_root) {
				SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
					SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE, index);
				return 0;
			}
			root = &model->source_surfaces[current_root];
			if (!SourceSurfaceChildValid(model, surface, root, current_root,
				index) ||
				(last_child != SG_RUNE_COMPACT_INDEX_NONE &&
				 (model->source_surfaces[last_child].cell.value >
					surface->cell.value ||
				  (model->source_surfaces[last_child].cell.value ==
					surface->cell.value &&
				   model->source_surfaces[last_child].split_ordinal >=
					surface->split_ordinal)))) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE,
					SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE, index);
				return 0;
			}
			last_child = index;
		}
		if (ValidatePolygon(&surface->plane,
			&model->source_surface_vertices[surface->vertices.first],
			surface->vertices.count) != FACET_POLYGON_VALID) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE, index);
			return 0;
		}
		vertex_cursor += surface->vertices.count;
	}
	if (vertex_cursor != model->source_surface_vertex_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
			SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE, vertex_cursor);
		return 0;
	}
	return 1;
}

static int ValidatePortals(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t portal_index;

	for (portal_index = 0U; portal_index < model->portal_count; portal_index++) {
		const sg_rune_compact_portal_t *portal = &model->portals[portal_index];
		const sg_rune_compact_facet_t *facet;
		const sg_rune_compact_incidence_t *negative;
		const sg_rune_compact_incidence_t *positive;
		sg_rune_stance_validity_t shared_stances;

		if (!SourceValid(&portal->source, model->facet_count,
			&model->identity.source_counts)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
		if (portal->facet.value >= model->facet_count ||
			portal->negative_incidence.value >= model->incidence_count ||
			portal->positive_incidence.value >= model->incidence_count) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
		if (portal_index != 0U &&
			model->portals[portal_index - 1U].facet.value >= portal->facet.value) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
		facet = &model->facets[portal->facet.value];
		negative = &model->incidences[portal->negative_incidence.value];
		positive = &model->incidences[portal->positive_incidence.value];
		if (facet->portal.value != portal_index || facet->incidences.count != 2U ||
			negative->facet.value != portal->facet.value ||
			positive->facet.value != portal->facet.value ||
			negative->side != SG_RUNE_FACET_NEGATIVE_SIDE ||
			positive->side != SG_RUNE_FACET_POSITIVE_SIDE ||
			negative->boundary == positive->boundary ||
			portal->direction < 0 ||
			portal->direction >= SG_RUNE_PORTAL_CONTINUITY_COUNT ||
			portal->clearance_q8 == 0U) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
		shared_stances = (sg_rune_stance_validity_t)(
			model->cells[negative->cell.value].valid_stances &
			model->cells[positive->cell.value].valid_stances);
		if (!StancesValid(portal->valid_stances) ||
			(portal->valid_stances & shared_stances) != portal->valid_stances) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_STANCE,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
		if (!ReservedBytesZero(portal->reserved)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_RECORD_PORTAL, portal_index);
			return 0;
		}
	}
	return 1;
}

static int MovementCapabilityCompare(const sg_rune_movement_capability_t *left,
	const sg_rune_movement_capability_t *right)
{
	int comparison = CompareU32(left->cell.value, right->cell.value);

	if (comparison == 0)
		comparison = CompareU32(left->boundary_portal.value,
			right->boundary_portal.value);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->source_stances,
			(uint32_t)right->source_stances);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->destination_stances,
			(uint32_t)right->destination_stances);
	if (comparison == 0)
		comparison = CompareU32(left->fibers.first, right->fibers.first);
	if (comparison == 0)
		comparison = CompareU32(left->fibers.count, right->fibers.count);
	return comparison;
}

static int MovementOutputValid(sg_rune_analytic_output_meaning_t output)
{
	return output == SG_RUNE_ANALYTIC_OUTPUT_COST ||
		output == SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS ||
		(output >= SG_RUNE_ANALYTIC_OUTPUT_POSITION_X &&
		 output <= SG_RUNE_ANALYTIC_OUTPUT_ACCELERATION_Z) ||
		output == SG_RUNE_ANALYTIC_OUTPUT_CLEARANCE ||
		output == SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
}

static int SingleStance(sg_rune_stance_validity_t stance)
{
	return StancesValid(stance) &&
		(stance & (sg_rune_stance_validity_t)(stance - 1U)) == 0U;
}

static int MovementFunctionSpanValid(const sg_rune_compact_model_t *model,
	sg_rune_analytic_function_span_t span, uint32_t *cursor,
	int require_traversal_outputs)
{
	uint32_t offset;
	int has_cost = 0;
	int has_time = 0;
	int has_reachability = 0;

	if (cursor == NULL || span.first != *cursor ||
		!SpanWithin(span.first, span.count,
			model->movement_fiber_function_ref_count) || span.count == 0U)
		return 0;
	for (offset = 0U; offset < span.count; offset++) {
		const uint32_t reference = span.first + offset;
		const uint32_t function =
			model->movement_fiber_function_refs[reference].value;
		const sg_rune_analytic_output_meaning_t output =
			function < model->analytic->function_count ?
			model->analytic->functions[function].output :
			SG_RUNE_ANALYTIC_OUTPUT_MEANING_COUNT;

		if (function >= model->analytic->function_count ||
			!MovementOutputValid(output) ||
			(offset != 0U && model->analytic->functions[
				model->movement_fiber_function_refs[reference - 1U].value].output >=
					output))
			return 0;
		has_cost |= output == SG_RUNE_ANALYTIC_OUTPUT_COST;
		has_time |= output == SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
		has_reachability |= output ==
			SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
	}
	if (require_traversal_outputs != 0 &&
		(!has_cost || !has_time || !has_reachability))
		return 0;
	*cursor += span.count;
	return 1;
}

static int MovementPmoveRuntimeValid(const sg_rune_compact_model_t *model)
{
	const sg_host_engine_pmove_abi_t *abi = &model->movement_pmove_abi;

	return abi->version != 0U && abi->game_api_version != 0U &&
		abi->import_size != 0U && abi->pmove_offset != 0U &&
		abi->pmove_size != 0U && abi->state_size ==
			(uint32_t)sizeof(pmove_state_t) && abi->command_size ==
			(uint32_t)sizeof(usercmd_t) && abi->fraction_bits != 0U &&
		abi->substep_ms == model->identity.physics.substep_ms &&
		abi->identity == model->identity.physics_abi_id &&
		model->movement_pmove_behavior_fingerprint != 0U &&
		model->movement_host_level_generation != 0U &&
		model->movement_physics_abi_id == model->identity.physics_abi_id &&
		model->movement_collision_law_id == model->identity.collision_law_id &&
		model->movement_pmove_law_id == model->identity.pmove_law_id &&
		model->movement_gravity_law_id == model->identity.gravity_law_id &&
		model->movement_hook_law_id == model->identity.hook_law_id &&
		model->movement_mechanism_law_id == model->identity.mechanism_law_id;
}

static int ResponseRefCompare(const sg_rune_compact_response_ref_t *left,
	const sg_rune_compact_response_ref_t *right)
{
	const int kind = CompareU32((uint32_t)left->kind, (uint32_t)right->kind);

	return kind != 0 ? kind : CompareU32(left->index, right->index);
}

static int ResponseFragmentSupportsField(
	const sg_rune_compact_response_projection_t *response,
	uint32_t fragment_index, const sg_rune_movement_capability_t *field)
{
	const sg_rune_compact_response_fragment_t *fragment;

	if (fragment_index >= response->source_fragment_count)
		return 0;
	fragment = &response->source_fragments[fragment_index];
	return fragment->parent_cell.value == field->cell.value &&
		(fragment->valid_stances & field->source_stances) != 0U;
}

static int ResponseReferenceSupportsField(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_response_ref_t *reference,
	const sg_rune_movement_capability_t *field)
{
	const sg_rune_compact_response_projection_t *response = &model->response;

	if (reference->kind == SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT) {
		if (reference->index >= response->fact_count)
			return 0;
		return ResponseFragmentSupportsField(response,
			response->facts[reference->index].source_fragment, field);
	}
	if (reference->kind == SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP) {
		const sg_rune_compact_response_endpoint_group_t *group;
		uint32_t member;

		if (reference->index >= response->candidate_group_count ||
			response->candidate_groups[reference->index].source_group >=
				response->source_endpoint_group_count)
			return 0;
		group = &response->source_endpoint_groups[
			response->candidate_groups[reference->index].source_group];
		for (member = group->first_member;
			member < group->first_member + group->member_count; member++)
			if (ResponseFragmentSupportsField(response,
				response->source_endpoint_members[member], field))
				return 1;
	}
	return 0;
}

static int MovementAngularScheduleCompare(
	const sg_rune_compact_movement_angular_schedule_t *left,
	const sg_rune_compact_movement_angular_schedule_t *right)
{
	const int mechanism = CompareU32(left->static_mechanism.value,
		right->static_mechanism.value);

	if (mechanism != 0)
		return mechanism;
	return CompareU32(left->authority_mechanism.value,
		right->authority_mechanism.value);
}

static int MovementAngularScheduleValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	const sg_rune_compact_movement_angular_schedule_t *schedule)
{
	const sg_rune_compact_mechanism_t *mechanism;
	uint32_t axis;

	if (schedule->static_mechanism.value >= static_data->mechanism_count ||
		schedule->authority_mechanism.value >= model->mechanism_authority_count ||
		schedule->source_entity >= model->identity.source_counts.entity_count ||
		schedule->mover_model >= model->identity.source_counts.model_count ||
		(schedule->flags & ~(sg_bsp_entity_angular_mover_flags_t)
			(SG_BSP_ENTITY_ANGULAR_MOVER_START_ON |
			 SG_BSP_ENTITY_ANGULAR_MOVER_REVERSE |
			 SG_BSP_ENTITY_ANGULAR_MOVER_STOP_ON_BLOCK |
			 SG_BSP_ENTITY_ANGULAR_MOVER_TOUCH_DAMAGE)) != 0U ||
		schedule->frame_ms == 0U ||
		schedule->frame_ms != model->identity.physics.frame_ms ||
		!Binary32Nonnegative(schedule->speed_bits) ||
		schedule->speed_bits == 0U)
		return 0;
	mechanism = &static_data->mechanisms[schedule->static_mechanism.value];
	if (mechanism->kind != SG_RUNE_COMPACT_MECHANISM_ROTATOR ||
		mechanism->source.entity_ordinal != schedule->source_entity ||
		model->mechanism_authorities[schedule->authority_mechanism.value].kind !=
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR ||
		model->mechanism_authorities[
			schedule->authority_mechanism.value].source.entity_ordinal !=
			schedule->source_entity ||
		(mechanism->flags & SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) !=
			0U)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!Binary32CanonicalFinite(schedule->initial_angles_bits[axis]) ||
			!Binary32CanonicalFinite(schedule->axis_bits[axis]) ||
			!Binary32CanonicalFinite(schedule->angular_velocity_bits[axis]) ||
			!Binary32CanonicalFinite(schedule->frame_angular_delta_bits[axis]))
			return 0;
	return 1;
}

static int ValidateMovementAngularSchedules(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data,
	sg_rune_compact_error_t *error)
{
	uint32_t index;

	for (index = 0U; index < model->movement_angular_schedule_count; index++)
		if (!MovementAngularScheduleValid(model, static_data,
			&model->movement_angular_schedules[index]) ||
			(index != 0U && MovementAngularScheduleCompare(
				&model->movement_angular_schedules[index - 1U],
				&model->movement_angular_schedules[index]) >= 0)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, index);
			return 0;
		}
	return 1;
}

static int MovementStateCompare(const sg_rune_compact_movement_state_t *left,
	const sg_rune_compact_movement_state_t *right)
{
	int result = CompareU32(left->stance, right->stance);

	if (result == 0)
		result = CompareU32((uint32_t)left->support, (uint32_t)right->support);
	if (result == 0)
		result = CompareU32((uint32_t)left->water, (uint32_t)right->water);
	if (result == 0)
		result = CompareU32((uint32_t)left->hook_phase,
			(uint32_t)right->hook_phase);
	if (result == 0)
		result = CompareU32(left->flags, right->flags);
	if (result == 0)
		result = CompareU32(left->mover_mechanism, right->mover_mechanism);
	return result;
}

static int MovementCapabilityUsesHook(sg_rune_movement_capability_kind_t kind)
{
	return kind >= SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT &&
		kind <= SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH;
}

static int MovementHookLifecycleValid(
	sg_rune_movement_capability_kind_t kind,
	const sg_rune_compact_movement_state_t *source,
	const sg_rune_compact_movement_state_t *destination)
{
	static const sg_host_hook_phase_t source_phases[6] = {
		SG_HOST_HOOK_IDLE, SG_HOST_HOOK_IN_FLIGHT, SG_HOST_HOOK_ATTACHED,
		SG_HOST_HOOK_ATTACHED, SG_HOST_HOOK_COAST, SG_HOST_HOOK_COAST
	};
	static const sg_host_hook_phase_t destination_phases[6] = {
		SG_HOST_HOOK_IN_FLIGHT, SG_HOST_HOOK_ATTACHED, SG_HOST_HOOK_ATTACHED,
		SG_HOST_HOOK_COAST, SG_HOST_HOOK_COAST, SG_HOST_HOOK_IN_FLIGHT
	};
	uint32_t phase;
	const sg_rune_movement_state_flags_t airborne =
		SG_RUNE_MOVEMENT_STATE_AIRBORNE;

	if (!MovementCapabilityUsesHook(kind) || source == NULL ||
		destination == NULL)
		return 0;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE) {
		if ((source->hook_phase != SG_HOST_HOOK_IN_FLIGHT &&
			 source->hook_phase != SG_HOST_HOOK_ATTACHED) ||
			destination->hook_phase != SG_HOST_HOOK_COAST ||
			source->support != destination->support ||
			(source->flags & SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) != 0U ||
			(destination->flags & SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) != 0U ||
			source->mover_mechanism != SG_RUNE_COMPACT_INDEX_NONE ||
			destination->mover_mechanism != SG_RUNE_COMPACT_INDEX_NONE)
			return 0;
		if (source->support == SG_RUNE_MOVEMENT_SUPPORT_NONE)
			return (source->flags & airborne) != 0U &&
				(destination->flags & airborne) != 0U;
		if (source->support == SG_RUNE_MOVEMENT_SUPPORT_STATIC)
			return (source->flags & airborne) == 0U &&
				(destination->flags & airborne) == 0U;
		return 0;
	}
	phase = (uint32_t)kind -
		(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT;
	return source->hook_phase == source_phases[phase] &&
		destination->hook_phase == destination_phases[phase];
}

static int MovementHookReleaseVariant(
	const sg_rune_compact_movement_state_t *source, uint32_t *bit_out)
{
	uint32_t bit;

	if (source == NULL || bit_out == NULL)
		return 0;
	if (source->hook_phase == SG_HOST_HOOK_IN_FLIGHT)
		bit = 0U;
	else if (source->hook_phase == SG_HOST_HOOK_ATTACHED)
		bit = 2U;
	else
		return 0;
	if (source->support == SG_RUNE_MOVEMENT_SUPPORT_STATIC)
		bit++;
	else if (source->support != SG_RUNE_MOVEMENT_SUPPORT_NONE)
		return 0;
	*bit_out = bit;
	return 1;
}

static int MovementFiberIsTeleport(const sg_rune_compact_model_t *model,
	const sg_rune_compact_movement_fiber_t *fiber)
{
	return model != NULL && fiber != NULL &&
		fiber->kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION &&
		fiber->mechanism_transition.value <
			model->mechanism_authority_transition_count &&
		model->mechanism_authority_transitions[
			fiber->mechanism_transition.value].kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT;
}

static sg_rune_movement_state_variables_t MovementStateVariablesForFiber(
	const sg_rune_compact_model_t *model,
	sg_rune_movement_capability_kind_t kind,
	const sg_rune_compact_movement_fiber_t *fiber)
{
	sg_rune_movement_state_variables_t variables =
		SG_RUNE_MOVEMENT_STATE_POSITION | SG_RUNE_MOVEMENT_STATE_VELOCITY |
		SG_RUNE_MOVEMENT_STATE_STANCE | SG_RUNE_MOVEMENT_STATE_TIME;

	if (kind <= SG_RUNE_MOVEMENT_CAPABILITY_AIR_CONTROL &&
		kind != SG_RUNE_MOVEMENT_CAPABILITY_SWIM)
		variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_SWIM)
		variables |= SG_RUNE_MOVEMENT_STATE_WATER |
			SG_RUNE_MOVEMENT_STATE_CURRENT;
	if (MovementCapabilityUsesHook(kind))
		variables |= SG_RUNE_MOVEMENT_STATE_HOOK;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE)
		variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_MOVER &&
		MovementFiberIsTeleport(model, fiber))
		variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT |
			SG_RUNE_MOVEMENT_STATE_WATER;
	else if (kind == SG_RUNE_MOVEMENT_CAPABILITY_MOVER ||
		kind == SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION)
		variables |= SG_RUNE_MOVEMENT_STATE_MOVER;
	if (kind == SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE)
		variables |= SG_RUNE_MOVEMENT_STATE_EXTERNAL_FORCE;
	return variables;
}

static int MovementTeleportStateValid(
	const sg_rune_compact_movement_state_t *state)
{
	if (state == NULL ||
		(state->support != SG_RUNE_MOVEMENT_SUPPORT_NONE &&
		 state->support != SG_RUNE_MOVEMENT_SUPPORT_STATIC) ||
		(state->water != SG_RUNE_MOVEMENT_WATER_DRY &&
		 state->water != SG_RUNE_MOVEMENT_WATER_SUBMERGED) ||
		(state->flags &
			~(sg_rune_movement_state_flags_t)
				SG_RUNE_MOVEMENT_STATE_AIRBORNE) != 0U ||
		state->mover_mechanism != SG_RUNE_COMPACT_INDEX_NONE)
		return 0;
	return state->support == SG_RUNE_MOVEMENT_SUPPORT_NONE ?
		(state->flags & SG_RUNE_MOVEMENT_STATE_AIRBORNE) != 0U :
		(state->flags & SG_RUNE_MOVEMENT_STATE_AIRBORNE) == 0U;
}

#if defined(SG_RUNE_COMPACT_MODEL_TEST_WRAP_CALLOC)
int SG_RuneCompactModelTestTeleportStateValid(
	const sg_rune_compact_movement_state_t *state);
int SG_RuneCompactModelTestControllerStateValid(
	const sg_rune_compact_movement_state_t *state);

int SG_RuneCompactModelTestTeleportStateValid(
	const sg_rune_compact_movement_state_t *state)
{
	return MovementTeleportStateValid(state);
}
#endif

static int MovementControllerStateValid(
	const sg_rune_compact_movement_state_t *state)
{
	if (state == NULL ||
		(state->support != SG_RUNE_MOVEMENT_SUPPORT_NONE &&
		 state->support != SG_RUNE_MOVEMENT_SUPPORT_STATIC) ||
		(state->water != SG_RUNE_MOVEMENT_WATER_DRY &&
		 state->water != SG_RUNE_MOVEMENT_WATER_SUBMERGED) ||
		state->hook_phase != SG_HOST_HOOK_IDLE ||
		(state->flags &
			~(sg_rune_movement_state_flags_t)
				SG_RUNE_MOVEMENT_STATE_AIRBORNE) != 0U ||
		state->mover_mechanism != SG_RUNE_COMPACT_INDEX_NONE)
		return 0;
	return state->support == SG_RUNE_MOVEMENT_SUPPORT_NONE ?
		(state->flags & SG_RUNE_MOVEMENT_STATE_AIRBORNE) != 0U :
		(state->flags & SG_RUNE_MOVEMENT_STATE_AIRBORNE) == 0U;
}

#if defined(SG_RUNE_COMPACT_MODEL_TEST_WRAP_CALLOC)
int SG_RuneCompactModelTestControllerStateValid(
	const sg_rune_compact_movement_state_t *state)
{
	return MovementControllerStateValid(state);
}
#endif

static int MovementFiberKindMatchesCapability(
	sg_rune_movement_fiber_kind_t fiber_kind,
	sg_rune_movement_capability_kind_t capability_kind)
{
	if (MovementCapabilityUsesHook(capability_kind))
		return fiber_kind == SG_RUNE_MOVEMENT_FIBER_HOOK;
	if (capability_kind == SG_RUNE_MOVEMENT_CAPABILITY_MOVER)
		return fiber_kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION ||
			fiber_kind == SG_RUNE_MOVEMENT_FIBER_ANGULAR_MOVER;
	if (capability_kind == SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE ||
		capability_kind == SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION)
		return fiber_kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION;
	return fiber_kind == SG_RUNE_MOVEMENT_FIBER_PMOVE;
}

static int MovementStateValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_movement_state_t *state)
{
	const int mover = state->support == SG_RUNE_MOVEMENT_SUPPORT_MOVER ||
		(state->flags & SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) != 0U;

	return SingleStance(state->stance) && ReservedBytesZero(state->reserved) &&
		state->support < SG_RUNE_MOVEMENT_SUPPORT_KIND_COUNT &&
		state->water < SG_RUNE_MOVEMENT_WATER_KIND_COUNT &&
		(uint32_t)state->hook_phase <= (uint32_t)SG_HOST_HOOK_COAST &&
		(state->flags &
			~(uint32_t)SG_RUNE_MOVEMENT_STATE_FLAGS_KNOWN) == 0U &&
		(mover ? state->mover_mechanism < model->mechanism_authority_count :
			state->mover_mechanism == SG_RUNE_COMPACT_INDEX_NONE);
}

static sg_rune_movement_hook_target_class_t MovementResponseClass(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_response_ref_t *reference)
{
	if (reference->kind == SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT &&
		reference->index < model->response.fact_count)
		return (sg_rune_movement_hook_target_class_t)
			model->response.facts[reference->index].visibility;
	if (reference->kind == SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP &&
		reference->index < model->response.candidate_group_count)
		return (sg_rune_movement_hook_target_class_t)
			model->response.candidate_groups[reference->index].classification;
	return SG_RUNE_MOVEMENT_HOOK_TARGET_CLASS_COUNT;
}

static sg_host_hook_target_kind_t MovementResponseTargetKind(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_response_ref_t *reference)
{
	uint32_t patch;
	uint32_t member;
	sg_host_hook_target_kind_t kind = SG_HOST_HOOK_TARGET_NONE;

	if (reference->kind == SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT) {
		if (reference->index >= model->response.fact_count)
			return SG_HOST_HOOK_TARGET_NONE;
		patch = model->response.facts[reference->index].target_patch;
		if (patch >= model->response.target_patch_count)
			return SG_HOST_HOOK_TARGET_NONE;
		return model->response.target_patches[patch].source_frame ==
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ?
			SG_HOST_HOOK_TARGET_WORLD : SG_HOST_HOOK_TARGET_FUNC;
	}
	if (reference->kind != SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP ||
		reference->index >= model->response.candidate_group_count)
		return SG_HOST_HOOK_TARGET_NONE;
	{
		const sg_rune_compact_response_candidate_group_t *candidate =
			&model->response.candidate_groups[reference->index];
		const sg_rune_compact_response_endpoint_group_t *group;

		if (candidate->target_group >=
			model->response.target_endpoint_group_count)
			return SG_HOST_HOOK_TARGET_NONE;
		group = &model->response.target_endpoint_groups[candidate->target_group];
		for (member = 0U; member < group->member_count; member++) {
			sg_host_hook_target_kind_t member_kind;

			patch = model->response.target_endpoint_members[
				group->first_member + member];
			if (patch >= model->response.target_patch_count)
				return SG_HOST_HOOK_TARGET_NONE;
			member_kind = model->response.target_patches[patch].source_frame ==
				SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ?
				SG_HOST_HOOK_TARGET_WORLD : SG_HOST_HOOK_TARGET_FUNC;
			if (kind != SG_HOST_HOOK_TARGET_NONE && kind != member_kind)
				return SG_HOST_HOOK_TARGET_NONE;
			kind = member_kind;
		}
	}
	return kind;
}

static int ValidateMovementFields(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_t *static_data, uint32_t *function_cursor,
	sg_rune_compact_error_t *error)
{
	uint32_t capability_index;
	uint32_t fiber_cursor = 0U;
	uint32_t target_cursor = 0U;
	uint32_t state_index;

	if (!MovementPmoveRuntimeValid(model) || function_cursor == NULL) {
		SetError(error, SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, 0U);
		return 0;
	}
	for (state_index = 0U; state_index < model->movement_state_count;
		state_index++)
		if (!MovementStateValid(model, &model->movement_states[state_index]) ||
			(state_index != 0U && MovementStateCompare(
				&model->movement_states[state_index - 1U],
				&model->movement_states[state_index]) >= 0)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, state_index);
			return 0;
		}
	if (!ValidateMovementAngularSchedules(model, static_data, error))
		return 0;
	for (capability_index = 0U;
		capability_index < model->movement_capability_count; capability_index++) {
		const sg_rune_movement_capability_t *capability =
			&model->movement_capabilities[capability_index];
		uint32_t fiber_index;
		uint32_t hook_release_coverage = 0U;

		if ((capability_index != 0U && MovementCapabilityCompare(
			&model->movement_capabilities[capability_index - 1U], capability) >= 0) ||
			capability->cell.value >= model->cell_count ||
			capability_index < model->cells[capability->cell.value].movement_fields.first ||
			capability_index >= model->cells[capability->cell.value].movement_fields.first +
				model->cells[capability->cell.value].movement_fields.count ||
			(capability->boundary_portal.value != SG_RUNE_COMPACT_INDEX_NONE &&
			 !PortalTouchesCell(model, capability->boundary_portal.value,
				 capability->cell.value)) || !ReservedBytesZero2(capability->reserved) ||
			capability->kind >= SG_RUNE_MOVEMENT_CAPABILITY_KIND_COUNT ||
			!StancesValid(capability->source_stances) ||
			!StancesValid(capability->destination_stances) ||
			(capability->source_stances &
			 model->cells[capability->cell.value].valid_stances) !=
				capability->source_stances || capability->fibers.first != fiber_cursor ||
			capability->fibers.count == 0U || !SpanWithin(capability->fibers.first,
				capability->fibers.count, model->movement_fiber_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, capability_index);
			return 0;
		}
		for (fiber_index = capability->fibers.first;
			fiber_index < capability->fibers.first + capability->fibers.count;
			fiber_index++) {
			const sg_rune_compact_movement_fiber_t *fiber =
				&model->movement_fibers[fiber_index];
			const sg_rune_compact_movement_state_t *source_state =
				fiber->source_state.value < model->movement_state_count ?
				&model->movement_states[fiber->source_state.value] : NULL;
			const sg_rune_compact_movement_state_t *destination_state =
				fiber->destination_state.value < model->movement_state_count ?
				&model->movement_states[fiber->destination_state.value] : NULL;

			if (fiber->capability.value != capability_index ||
				fiber->kind >= SG_RUNE_MOVEMENT_FIBER_KIND_COUNT ||
				fiber->source_state.value >= model->movement_state_count ||
				fiber->destination_state.value >= model->movement_state_count ||
				fiber->state_variables != MovementStateVariablesForFiber(model,
					capability->kind, fiber) ||
				source_state == NULL || destination_state == NULL ||
				(source_state->stance & capability->source_stances) == 0U ||
				(destination_state->stance & capability->destination_stances) == 0U ||
				!MovementFunctionSpanValid(model, fiber->functions,
					function_cursor, 1) || fiber->hook_targets.first != target_cursor ||
				!SpanWithin(fiber->hook_targets.first, fiber->hook_targets.count,
					model->movement_hook_target_count)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
					SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, fiber_index);
				return 0;
			}
			if (!MovementFiberKindMatchesCapability(fiber->kind,
					capability->kind) ||
				(fiber->kind == SG_RUNE_MOVEMENT_FIBER_HOOK &&
				 fiber->hook_targets.count == 0U &&
				 capability->kind != SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE &&
				 capability->kind != SG_RUNE_MOVEMENT_CAPABILITY_HOOK_COAST) ||
				(fiber->kind != SG_RUNE_MOVEMENT_FIBER_HOOK &&
				 fiber->hook_targets.count != 0U) ||
				(fiber->kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION ?
				 fiber->mechanism_transition.value >=
					model->mechanism_authority_transition_count :
				 fiber->mechanism_transition.value != SG_RUNE_COMPACT_INDEX_NONE) ||
				(fiber->kind == SG_RUNE_MOVEMENT_FIBER_ANGULAR_MOVER ?
				 fiber->angular_schedule >= model->movement_angular_schedule_count :
				 fiber->angular_schedule != SG_RUNE_COMPACT_INDEX_NONE) ||
				(capability->kind ==
					SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION ?
				 (fiber->controller_action_controller.value >=
					model->mechanism_authority_controller_count ||
				  fiber->controller_action_target.value >=
					model->mechanism_authority_count) :
				 (fiber->controller_action_controller.value !=
					SG_RUNE_COMPACT_INDEX_NONE ||
				  fiber->controller_action_target.value !=
					SG_RUNE_COMPACT_INDEX_NONE))) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, fiber_index);
				return 0;
			}
			if (capability->kind ==
				SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION) {
				const sg_rune_compact_mechanism_controller_t *controller =
					&model->mechanism_authority_controllers[
						fiber->controller_action_controller.value];
				const sg_rune_compact_mechanism_transition_t *transition =
					&model->mechanism_authority_transitions[
						fiber->mechanism_transition.value];

				if (controller->spatiality !=
						SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL ||
					controller->mechanism !=
						fiber->controller_action_target.value ||
					transition->mechanism !=
						fiber->controller_action_target.value ||
					controller->activation_cell.value !=
						capability->cell.value ||
					capability->boundary_portal.value !=
						SG_RUNE_COMPACT_INDEX_NONE ||
					fiber->source_state.value != fiber->destination_state.value ||
					!MovementControllerStateValid(source_state)) {
					SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, fiber_index);
					return 0;
				}
			}
			if (MovementCapabilityUsesHook(capability->kind)) {
				uint32_t release_variant;

				if (!MovementHookLifecycleValid(capability->kind, source_state,
						destination_state) ||
					(capability->kind ==
						SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE &&
					 (!MovementHookReleaseVariant(source_state,
						&release_variant) ||
					  (hook_release_coverage &
						(UINT32_C(1) << release_variant)) != 0U))) {
					SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, fiber_index);
					return 0;
				}
				if (capability->kind ==
					SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE) {
					hook_release_coverage |= UINT32_C(1) << release_variant;
					if (source_state->hook_phase == SG_HOST_HOOK_IN_FLIGHT &&
						fiber->hook_targets.count != 0U) {
						SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
							SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, fiber_index);
						return 0;
					}
				}
			}
			if (capability->kind == SG_RUNE_MOVEMENT_CAPABILITY_MOVER) {
				const int teleport = MovementFiberIsTeleport(model, fiber);
				uint32_t expected_authority;

				if (teleport) {
					const sg_rune_compact_mechanism_transition_t *transition =
						&model->mechanism_authority_transitions[
							fiber->mechanism_transition.value];

					if (capability->cell.value != transition->entry_cell.value ||
						capability->boundary_portal.value !=
							SG_RUNE_COMPACT_INDEX_NONE ||
						!MovementTeleportStateValid(source_state) ||
						!MovementTeleportStateValid(destination_state)) {
						SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
							SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, fiber_index);
						return 0;
					}
				} else {
					if (fiber->kind ==
						SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION) {
						expected_authority = model->mechanism_authority_transitions[
							fiber->mechanism_transition.value].mechanism;
					} else {
						const sg_rune_compact_movement_angular_schedule_t *schedule =
							&model->movement_angular_schedules[
								fiber->angular_schedule];

						if (capability->cell.value != static_data->mechanisms[
								schedule->static_mechanism.value].entry_cell.value ||
							capability->boundary_portal.value !=
								SG_RUNE_COMPACT_INDEX_NONE) {
							SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
								SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, fiber_index);
							return 0;
						}
						expected_authority = schedule->authority_mechanism.value;
					}
					if (source_state->mover_mechanism != expected_authority ||
						destination_state->mover_mechanism != expected_authority ||
						source_state->support != SG_RUNE_MOVEMENT_SUPPORT_MOVER ||
						destination_state->support != SG_RUNE_MOVEMENT_SUPPORT_MOVER ||
						(source_state->flags &
							SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) == 0U ||
						(destination_state->flags &
							SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) == 0U) {
						SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
							SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, fiber_index);
						return 0;
					}
				}
			}
			target_cursor += fiber->hook_targets.count;
		}
		if (capability->kind == SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE &&
			(capability->fibers.count != 4U || hook_release_coverage != 15U)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, capability_index);
			return 0;
		}
		fiber_cursor += capability->fibers.count;
	}
	if (fiber_cursor != model->movement_fiber_count ||
		target_cursor != model->movement_hook_target_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
			SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, fiber_cursor);
		return 0;
	}
	for (state_index = 0U; state_index < model->movement_hook_target_count;
		state_index++) {
		const sg_rune_compact_movement_hook_target_t *target =
			&model->movement_hook_targets[state_index];
		const sg_rune_compact_movement_fiber_t *fiber;
		const sg_rune_movement_capability_t *capability;
		const sg_rune_analytic_function_span_t spans[6] = {
			target->functions.bolt, target->functions.body,
			target->functions.pull, target->functions.release,
			target->functions.coast, target->functions.relaunch
		};
		uint32_t phase;

		if (target->fiber.value >= model->movement_fiber_count) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, state_index);
			return 0;
		}
		fiber = &model->movement_fibers[target->fiber.value];
		capability = &model->movement_capabilities[fiber->capability.value];
		if (state_index < fiber->hook_targets.first ||
			state_index >= fiber->hook_targets.first + fiber->hook_targets.count ||
			!ReservedBytesZero2(target->reserved) ||
			target->target_kind < SG_HOST_HOOK_TARGET_WORLD ||
			target->target_kind > SG_HOST_HOOK_TARGET_INFO_FLAG ||
			target->provenance >=
				SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_COUNT ||
			target->visibility_class >= SG_RUNE_MOVEMENT_HOOK_TARGET_CLASS_COUNT ||
			!StancesValid(target->source_stances) ||
			!StancesValid(target->target_stances) ||
			(target->source_stances & capability->source_stances) !=
				target->source_stances ||
			(target->target_stances & capability->destination_stances) !=
				target->target_stances ||
			(target->provenance ==
				SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_GENERIC ?
			 (target->response.kind != SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT ||
			  target->response.index != SG_RUNE_COMPACT_INDEX_NONE ||
			  target->visibility_class !=
				SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL ||
			  (target->target_kind != SG_HOST_HOOK_TARGET_PLAYER &&
			   target->target_kind != SG_HOST_HOOK_TARGET_BODYQUE &&
			   target->target_kind != SG_HOST_HOOK_TARGET_FUNC &&
			   target->target_kind != SG_HOST_HOOK_TARGET_INFO_FLAG)) :
			 (!ResponseReferenceSupportsField(model, &target->response, capability) ||
			  MovementResponseClass(model, &target->response) !=
				target->visibility_class ||
			  MovementResponseTargetKind(model, &target->response) !=
				target->target_kind))) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, state_index);
			return 0;
		}
		if (state_index != fiber->hook_targets.first) {
			const sg_rune_compact_movement_hook_target_t *previous =
				&model->movement_hook_targets[state_index - 1U];
			int order = CompareU32((uint32_t)previous->target_kind,
				(uint32_t)target->target_kind);

			if (order == 0)
				order = CompareU32((uint32_t)previous->provenance,
					(uint32_t)target->provenance);
			if (order == 0)
				order = ResponseRefCompare(&previous->response, &target->response);

			if (order == 0)
				order = CompareU32(previous->source_stances,
					target->source_stances);
			if (order == 0)
				order = CompareU32(previous->target_stances,
					target->target_stances);
			if (order == 0)
				order = CompareU32((uint32_t)previous->visibility_class,
					(uint32_t)target->visibility_class);
			if (previous->fiber.value != target->fiber.value || order >= 0) {
				SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
					SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, state_index);
				return 0;
			}
		}
		for (phase = 0U; phase < 6U; phase++)
			if (!MovementFunctionSpanValid(model, spans[phase], function_cursor,
				phase != 3U && phase != 4U)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
					SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD, state_index);
				return 0;
			}
	}
	return *function_cursor == model->movement_fiber_function_ref_count;
}


static int ResponseBoundaryValid(const sg_rune_compact_model_t *model,
	sg_rune_compact_cell_index_t cell_index, uint32_t first, uint32_t count,
	int polygon_required)
{
	uint32_t offset;

	if (count == 0U)
		return 0;
	if (!SpanWithin(first, count, model->cell_incidence_count))
		return 0;
	for (offset = 0U; offset < count; offset++) {
		const uint32_t incidence_index = model->cell_incidences[
			first + offset].value;
		const sg_rune_compact_incidence_t *incidence;

		if (incidence_index >= model->incidence_count)
			return 0;
		incidence = &model->incidences[incidence_index];
		if (incidence->cell.value != cell_index.value ||
			incidence->facet.value >= model->facet_count ||
			(polygon_required != 0 && model->facets[incidence->facet.value].kind !=
				SG_RUNE_COMPACT_FACET_POLYGON))
			return 0;
	}
	return 1;
}

static int ResponseEndpointValid(const sg_rune_compact_model_t *model,
	uint32_t configuration_region,
	uint32_t configuration_cell, uint32_t leaf, uint32_t area,
	uint32_t cluster, const sg_rune_compact_cell_t *cell)
{
	/* The static-partition ID is an opaque semantic-region provenance token.
	 * The compact model does not own the source semantic-region table, so it
	 * must preserve that authenticated ID rather than synthesize one from the
	 * compact configuration indices. */
	return configuration_region != UINT32_MAX &&
		configuration_cell != UINT32_MAX &&
		leaf < model->identity.source_counts.leaf_count &&
		area < model->identity.source_counts.area_count && cell != NULL &&
		leaf == cell->source.leaf && area == cell->source.area &&
		cluster == (uint32_t)cell->source.cluster;
}

static int StaticOccluderCompare(const sg_rune_compact_static_occluder_t *left,
	const sg_rune_compact_static_occluder_t *right)
{
	int comparison = CompareU32(left->model, right->model);

	if (comparison == 0)
		comparison = CompareU32(left->brush, right->brush);
	if (comparison == 0)
		comparison = CompareU32(left->contents, right->contents);
	if (comparison == 0)
		comparison = CompareU32(left->conditional, right->conditional);
	return comparison;
}

static int StaticOccluderValid(const sg_rune_compact_model_t *model,
	const sg_rune_compact_static_occluder_t *occluder)
{
	return occluder->model < model->identity.source_counts.model_count &&
		occluder->brush < model->identity.source_counts.brush_count &&
		occluder->conditional <= 1U;
}

static int ResponseGroupArraysValid(
	const sg_rune_compact_response_endpoint_group_t *groups,
	uint32_t group_count, const uint32_t *members, uint32_t member_count,
	uint32_t value_count, const uint8_t *required_members)
{
	uint32_t group;
	uint32_t cursor = 0U;
	uint8_t *seen;
	int result = 0;

	if ((group_count == 0U) != (member_count == 0U))
		return 0;
	if (value_count != 0U && required_members == NULL)
		return 0;
	seen = value_count == 0U ? NULL : calloc(value_count, sizeof(*seen));
	if (value_count != 0U && seen == NULL)
		return -1;
	for (group = 0U; group < group_count; group++) {
		const sg_rune_compact_response_endpoint_group_t *record =
			&groups[group];
		uint32_t member;
		uint32_t previous = 0U;

		if (record->first_member != cursor || record->member_count == 0U ||
			!SpanWithin(record->first_member, record->member_count,
				member_count) || (record->flags &
				~(uint32_t)SG_RUNE_COMPACT_RESPONSE_ENDPOINT_MOVING) != 0U ||
			(group != 0U &&
				(CompareU32(groups[group - 1U].bsp_cluster,
					record->bsp_cluster) > 0 ||
					 (groups[group - 1U].bsp_cluster == record->bsp_cluster &&
					  CompareU32(groups[group - 1U].bsp_area,
						record->bsp_area) >= 0))))
			goto done;
		for (member = record->first_member;
			member < record->first_member + record->member_count; member++) {
			const uint32_t value = members[member];

			if (value >= value_count || required_members[value] == 0U ||
				seen[value] != 0U ||
				(member != record->first_member && value <= previous))
				goto done;
			seen[value] = 1U;
			previous = value;
		}
		cursor += record->member_count;
	}
	if (cursor == member_count) {
		for (group = 0U; group < value_count; group++)
			if (seen[group] != required_members[group])
				goto done;
		result = 1;
	}
done:
	free(seen);
	return result;
}

typedef struct response_fact_join_s
{
	uint64_t key;
	uint32_t fact;
} response_fact_join_t;

static uint64_t ResponseGroupPairKey(uint32_t source, uint32_t target)
{
	return ((uint64_t)source << 32U) | (uint64_t)target;
}

/* Eight stable byte passes give a deterministic O(n) join order without
 * relying on adversarially degradable hash probing. */
static void ResponseFactJoinsSort(response_fact_join_t *values,
	response_fact_join_t *scratch, uint32_t count)
{
	response_fact_join_t *source = values;
	response_fact_join_t *destination = scratch;
	uint32_t pass;

	for (pass = 0U; pass < 8U; pass++) {
		uint32_t offsets[256] = { 0U };
		uint32_t index;
		uint32_t total = 0U;

		for (index = 0U; index < count; index++)
			offsets[(uint32_t)((source[index].key >> (pass * 8U)) &
				UINT64_C(0xff))]++;
		for (index = 0U; index < 256U; index++) {
			const uint32_t occurrences = offsets[index];

			offsets[index] = total;
			total += occurrences;
		}
		for (index = 0U; index < count; index++) {
			const uint32_t bucket = (uint32_t)((source[index].key >>
				(pass * 8U)) & UINT64_C(0xff));

			destination[offsets[bucket]++] = source[index];
		}
		{
			response_fact_join_t *temporary = source;

			source = destination;
			destination = temporary;
		}
	}
}

static int ResponseFactsCompare(const sg_rune_compact_model_t *model,
	const sg_rune_compact_response_fact_t *left,
	const sg_rune_compact_response_fact_t *right)
{
	const sg_rune_compact_response_fragment_t *left_source =
		&model->response.source_fragments[left->source_fragment];
	const sg_rune_compact_response_fragment_t *right_source =
		&model->response.source_fragments[right->source_fragment];
	const sg_rune_compact_response_patch_t *left_target =
		&model->response.target_patches[left->target_patch];
	const sg_rune_compact_response_patch_t *right_target =
		&model->response.target_patches[right->target_patch];
	int comparison = CompareU32(left_source->parent_cell.value,
		right_source->parent_cell.value);

#define RESPONSE_FACT_KEY(a, b) \
	do { if (comparison == 0) comparison = CompareU32((a), (b)); } while (0)
	if (comparison == 0)
		comparison = CompareU32(left_target->target_cell.value,
			right_target->target_cell.value);
	if (comparison == 0)
		comparison = CompareU64(left_source->static_partition_id,
			right_source->static_partition_id);
	if (comparison == 0)
		comparison = CompareU64(left_target->static_partition_id,
			right_target->static_partition_id);
	RESPONSE_FACT_KEY(left_source->configuration_region,
		right_source->configuration_region);
	RESPONSE_FACT_KEY(left_source->configuration_cell,
		right_source->configuration_cell);
	RESPONSE_FACT_KEY(left_target->configuration_region,
		right_target->configuration_region);
	RESPONSE_FACT_KEY(left_target->configuration_cell,
		right_target->configuration_cell);
	RESPONSE_FACT_KEY(left_source->bsp_leaf, right_source->bsp_leaf);
	RESPONSE_FACT_KEY(left_source->bsp_area, right_source->bsp_area);
	RESPONSE_FACT_KEY(left_source->bsp_cluster, right_source->bsp_cluster);
	RESPONSE_FACT_KEY(left_target->bsp_leaf, right_target->bsp_leaf);
	RESPONSE_FACT_KEY(left_target->bsp_area, right_target->bsp_area);
	RESPONSE_FACT_KEY(left_target->bsp_cluster, right_target->bsp_cluster);
	RESPONSE_FACT_KEY(left_source->boundary_incidences.first,
		right_source->boundary_incidences.first);
	RESPONSE_FACT_KEY(left_source->boundary_incidences.count,
		right_source->boundary_incidences.count);
	RESPONSE_FACT_KEY(left_target->boundary_incidences.first,
		right_target->boundary_incidences.first);
	RESPONSE_FACT_KEY(left_target->boundary_incidences.count,
		right_target->boundary_incidences.count);
	RESPONSE_FACT_KEY(left->source_fragment, right->source_fragment);
	RESPONSE_FACT_KEY(left->target_patch, right->target_patch);
#undef RESPONSE_FACT_KEY
	return comparison;
}

static int ResponseHalfspaceCompare(
	const sg_rune_compact_response_halfspace_t *left,
	const sg_rune_compact_response_halfspace_t *right)
{
	int comparison = PlaneCompare(&left->plane, &right->plane);

	if (comparison == 0)
		comparison = CompareU32(left->split, right->split);
	if (comparison == 0)
		comparison = CompareU32(left->open, right->open);
	return comparison;
}

/* This is the serialized fragment key.  It deliberately excludes transient
 * owner indexes: the halfspace payload, endpoint snapshot, and bounds are
 * the canonical response geometry. */
static int ResponseFragmentCompare(
	const sg_rune_compact_response_projection_t *response,
	const sg_rune_compact_response_fragment_t *left,
	const sg_rune_compact_response_fragment_t *right)
{
	uint32_t axis;
	uint32_t halfspace;
	int comparison = CompareU32(left->parent_cell.value,
		right->parent_cell.value);

#define RESPONSE_FRAGMENT_KEY(a, b) \
	do { if (comparison == 0) comparison = CompareU32((a), (b)); } while (0)
	if (comparison == 0)
		comparison = CompareU64(left->static_partition_id,
			right->static_partition_id);
	RESPONSE_FRAGMENT_KEY(left->configuration_region,
		right->configuration_region);
	RESPONSE_FRAGMENT_KEY(left->configuration_cell,
		right->configuration_cell);
	RESPONSE_FRAGMENT_KEY(left->bsp_leaf, right->bsp_leaf);
	RESPONSE_FRAGMENT_KEY(left->bsp_area, right->bsp_area);
	RESPONSE_FRAGMENT_KEY(left->bsp_cluster, right->bsp_cluster);
	RESPONSE_FRAGMENT_KEY(left->valid_stances, right->valid_stances);
	for (axis = 0U; axis < 3U && comparison == 0; axis++) {
		comparison = CompareI32(left->bounds.mins.value[axis],
			right->bounds.mins.value[axis]);
		if (comparison == 0)
			comparison = CompareI32(left->bounds.maxs.value[axis],
				right->bounds.maxs.value[axis]);
	}
	RESPONSE_FRAGMENT_KEY(left->halfspace_count, right->halfspace_count);
	for (halfspace = 0U; halfspace < left->halfspace_count &&
		comparison == 0; halfspace++)
		comparison = ResponseHalfspaceCompare(
			&response->source_halfspaces[left->first_halfspace + halfspace],
			&response->source_halfspaces[right->first_halfspace + halfspace]);
#undef RESPONSE_FRAGMENT_KEY
	return comparison;
}

static int ResponsePatchCompare(
	const sg_rune_compact_response_projection_t *response,
	const sg_rune_compact_response_patch_t *left,
	const sg_rune_compact_response_patch_t *right)
{
	uint32_t vertex;
	int comparison = CompareU32(left->model, right->model);

#define RESPONSE_PATCH_KEY(a, b) \
	do { if (comparison == 0) comparison = CompareU32((a), (b)); } while (0)
	RESPONSE_PATCH_KEY(left->brush, right->brush);
	RESPONSE_PATCH_KEY(left->brush_side, right->brush_side);
	RESPONSE_PATCH_KEY(left->source_surface, right->source_surface);
	RESPONSE_PATCH_KEY((uint32_t)left->source_frame,
		(uint32_t)right->source_frame);
	RESPONSE_PATCH_KEY(left->bsp_leaf, right->bsp_leaf);
	if (comparison == 0)
		comparison = CompareU64(left->visibility_surface_id,
			right->visibility_surface_id);
	RESPONSE_PATCH_KEY(left->vertex_count, right->vertex_count);
	for (vertex = 0U; vertex < left->vertex_count && comparison == 0;
		vertex++)
		comparison = Q8VecCompare(
			&response->target_vertices[left->first_vertex + vertex],
			&response->target_vertices[right->first_vertex + vertex]);
#undef RESPONSE_PATCH_KEY
	return comparison;
}

static int ValidateResponseProjection(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	const sg_rune_compact_response_projection_t *response = &model->response;
	uint32_t index;
	uint32_t direct_count = 0U;
	uint32_t impact_count = 0U;
	uint32_t target_member_count = 0U;
	uint32_t halfspace_cursor = 0U;
	uint32_t target_vertex_cursor = 0U;
	uint8_t *source_required = NULL;
	uint8_t *target_required = NULL;
	int source_groups_valid;
	int target_groups_valid;
	uint32_t *source_group_by_fragment = NULL;
	uint32_t *target_group_by_patch = NULL;
	uint32_t *candidate_by_fact = NULL;
	response_fact_join_t *fact_joins = NULL;
	response_fact_join_t *fact_join_scratch = NULL;

	if (response->exact_live_prefire_trace_required != 1U ||
		response->reserved[0] != 0U || response->reserved[1] != 0U ||
		response->reserved[2] != 0U ||
		response->seal.version != SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION ||
		response->seal.reserved != 0U ||
		(response->seal.flags & SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED) !=
			SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED ||
		(response->seal.flags &
			~(sg_rune_compact_response_seal_flags_t)
				SG_RUNE_COMPACT_RESPONSE_SEAL_KNOWN) != 0U ||
		response->seal.split_frontier_count != 0U ||
		response->seal.source_fragment_count != response->source_fragment_count ||
		response->seal.target_patch_count != response->target_patch_count ||
		response->seal.split_count != response->split_count ||
		response->seal.response_pair_count != response->fact_count ||
		response->seal.unresolved_response_pair_count != 0U ||
		response->seal.unresolved_candidate_group_count !=
			response->candidate_group_count ||
		response->seal.source_endpoint_group_count !=
			response->source_endpoint_group_count ||
		response->seal.target_endpoint_group_count !=
			response->target_endpoint_group_count ||
		response->seal.source_endpoint_member_count !=
			response->source_endpoint_member_count ||
		response->seal.target_endpoint_member_count !=
			response->target_endpoint_member_count ||
		response->seal.static_occluder_count != response->occluder_count ||
		response->seal.compact_cell_count != model->cell_count ||
		response->seal.compact_facet_count != model->facet_count ||
		response->seal.compact_source_surface_count != model->source_surface_count ||
		response->seal.compact_source_surface_vertex_count !=
			model->source_surface_vertex_count ||
		response->seal.source_surface_catalog_seal == 0U ||
		response->seal.source_surface_catalog_seal !=
			SG_RuneCompactSourceSurfaceCatalogSeal(model->source_surfaces,
				model->source_surface_count, model->source_surface_vertices,
				model->source_surface_vertex_count) ||
		response->source_fragment_count == 0U ||
		response->target_patch_count == 0U ||
		!ArrayPresent(response->source_fragments, response->source_fragment_count) ||
		!ArrayPresent(response->source_halfspaces, response->source_halfspace_count) ||
		!ArrayPresent(response->target_patches, response->target_patch_count) ||
		!ArrayPresent(response->target_vertices, response->target_vertex_count) ||
		!ArrayPresent(response->splits, response->split_count) ||
		!ArrayPresent(response->facts, response->fact_count) ||
		!ArrayPresent(response->candidate_groups,
			response->candidate_group_count) ||
		!ArrayPresent(response->source_endpoint_groups,
			response->source_endpoint_group_count) ||
		!ArrayPresent(response->source_endpoint_members,
			response->source_endpoint_member_count) ||
		!ArrayPresent(response->target_endpoint_groups,
			response->target_endpoint_group_count) ||
		!ArrayPresent(response->target_endpoint_members,
			response->target_endpoint_member_count) ||
		!ArrayPresent(response->occluders, response->occluder_count)) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_RESPONSE, 0U);
		return 0;
	}
	for (index = 0U; index < response->source_fragment_count; index++) {
		const sg_rune_compact_response_fragment_t *fragment =
			&response->source_fragments[index];
		const sg_rune_compact_cell_t *cell;
		uint32_t halfspace_index;

		if (fragment->parent_cell.value >= model->cell_count ||
			fragment->reserved[0] != 0U || fragment->reserved[1] != 0U ||
			fragment->reserved[2] != 0U ||
			fragment->first_halfspace != halfspace_cursor ||
			!SpanWithin(fragment->first_halfspace, fragment->halfspace_count,
				response->source_halfspace_count) ||
			!BoundsValid(&fragment->bounds) || !StancesValid(fragment->valid_stances)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
		cell = &model->cells[fragment->parent_cell.value];
		if (!ResponseBoundaryValid(model, fragment->parent_cell,
			fragment->boundary_incidences.first,
			fragment->boundary_incidences.count, 0) ||
			!ResponseEndpointValid(model, fragment->configuration_region,
				fragment->configuration_cell,
				fragment->bsp_leaf, fragment->bsp_area, fragment->bsp_cluster,
				cell) || (fragment->valid_stances & cell->valid_stances) !=
				fragment->valid_stances) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
		for (halfspace_index = 0U;
			halfspace_index < fragment->halfspace_count; halfspace_index++) {
			const sg_rune_compact_response_halfspace_t *halfspace =
				&response->source_halfspaces[fragment->first_halfspace +
					halfspace_index];

			if (!PlaneValid(&halfspace->plane) ||
				(halfspace->split != SG_RUNE_COMPACT_INDEX_NONE &&
				 halfspace->split >= response->split_count) ||
				halfspace->open > 1U ||
				halfspace->reserved[0] != 0U ||
				halfspace->reserved[1] != 0U ||
				halfspace->reserved[2] != 0U) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_RESPONSE, index);
				return 0;
			}
		}
		if (index != 0U && ResponseFragmentCompare(response,
			&response->source_fragments[index - 1U], fragment) >= 0) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
		halfspace_cursor += fragment->halfspace_count;
	}
	if (halfspace_cursor != response->source_halfspace_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_RESPONSE, 0U);
		return 0;
	}
	for (index = 0U; index < response->target_patch_count; index++) {
		const sg_rune_compact_response_patch_t *patch =
			&response->target_patches[index];
		const sg_rune_compact_source_surface_t *surface;
		double normal[3];
		double distance;
		uint32_t vertex;

		if (patch->reserved[0] != 0U || patch->reserved[1] != 0U ||
			patch->reserved[2] != 0U || patch->source_surface >=
			model->source_surface_count || patch->source_frame >=
			SG_RUNE_COMPACT_SOURCE_SURFACE_FRAME_COUNT ||
			(patch->parent_facet.value != SG_RUNE_COMPACT_INDEX_NONE &&
			 (patch->parent_facet.value >= model->facet_count ||
			  model->facets[patch->parent_facet.value].kind !=
				SG_RUNE_COMPACT_FACET_POLYGON)) ||
			patch->vertex_count < 3U || !SpanWithin(patch->first_vertex,
				patch->vertex_count, response->target_vertex_count) ||
			patch->first_vertex != target_vertex_cursor ||
			!ResponsePatchBoundsValid(patch, response->target_vertices) ||
			!PlaneValid(&patch->plane) ||
			!StancesValid(patch->valid_stances) ||
			(patch->flags & ~(sg_rune_compact_response_patch_flags_t)
				(SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE |
				 SG_RUNE_COMPACT_RESPONSE_PATCH_SKY |
				 SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING)) != 0U ||
			model->source_surfaces[patch->source_surface].frame !=
				patch->source_frame ||
			((patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) != 0U &&
			 (patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE) != 0U)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
		surface = &model->source_surfaces[patch->source_surface];
		if (patch->model != surface->source.model ||
			patch->brush != surface->source.brush ||
			patch->brush_side != surface->source.brush_side ||
			PlaneCompare(&patch->plane, &surface->plane) != 0) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
		for (vertex = 0U; vertex < 3U; vertex++)
			normal[vertex] = Binary32Value(patch->plane.normal_bits[vertex]);
		distance = Binary32Value(patch->plane.distance_bits);
		for (vertex = 0U; vertex < patch->vertex_count; vertex++)
			if (!VertexOnPlane(&response->target_vertices[
				patch->first_vertex + vertex], normal, distance)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_RESPONSE, index);
				return 0;
			}
		if (index != 0U && ResponsePatchCompare(response,
			&response->target_patches[index - 1U], patch) >= 0) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
		target_vertex_cursor += patch->vertex_count;
		if ((patch->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) == 0U) {
			const sg_rune_compact_cell_t *cell;

			if (patch->target_cell.value >= model->cell_count ||
				(patch->parent_facet.value != SG_RUNE_COMPACT_INDEX_NONE &&
				 !ResponseBoundaryValid(model, patch->target_cell,
					patch->boundary_incidences.first,
					patch->boundary_incidences.count, 1)) ||
				(patch->parent_facet.value == SG_RUNE_COMPACT_INDEX_NONE &&
				 (patch->boundary_incidences.first != 0U ||
				  patch->boundary_incidences.count != 0U))) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_RESPONSE, index);
				return 0;
			}
			cell = &model->cells[patch->target_cell.value];
			if (!ResponseEndpointValid(model, patch->configuration_region,
				patch->configuration_cell,
				patch->bsp_leaf, patch->bsp_area, patch->bsp_cluster, cell) ||
				(patch->valid_stances & cell->valid_stances) !=
					patch->valid_stances) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_RESPONSE, index);
				return 0;
			}
			target_member_count++;
		}
	}
	if (target_vertex_cursor != response->target_vertex_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_RESPONSE, 0U);
		return 0;
	}
	source_required = calloc(response->source_fragment_count,
		sizeof(*source_required));
	target_required = calloc(response->target_patch_count,
		sizeof(*target_required));
	if (source_required == NULL || target_required == NULL) {
		free(source_required);
		free(target_required);
		SetError(error, SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_RECORD_RESPONSE, 0U);
		return 0;
	}
	memset(source_required, 1, response->source_fragment_count);
	for (index = 0U; index < response->target_patch_count; index++)
		if ((response->target_patches[index].flags &
			SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) == 0U)
			target_required[index] = 1U;
	source_groups_valid = ResponseGroupArraysValid(response->source_endpoint_groups,
		response->source_endpoint_group_count, response->source_endpoint_members,
		response->source_endpoint_member_count, response->source_fragment_count,
		source_required);
	target_groups_valid = ResponseGroupArraysValid(response->target_endpoint_groups,
		response->target_endpoint_group_count, response->target_endpoint_members,
		response->target_endpoint_member_count, response->target_patch_count,
		target_required);
	free(source_required);
	free(target_required);
	if (source_groups_valid < 0 || target_groups_valid < 0) {
		SetError(error, SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_RECORD_RESPONSE, 0U);
		return 0;
	}
	if (source_groups_valid == 0 || target_groups_valid == 0 ||
		response->source_endpoint_member_count != response->source_fragment_count ||
		response->target_endpoint_member_count != target_member_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_RESPONSE, 0U);
		return 0;
	}
	for (index = 0U; index < response->occluder_count; index++)
		if (!StaticOccluderValid(model, &response->occluders[index]) ||
			(index != 0U && StaticOccluderCompare(&response->occluders[index - 1U],
				&response->occluders[index]) >= 0)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
	for (index = 0U; index < response->split_count; index++)
		if (!ResponseSplitValid(model, response, &response->splits[index]) ||
			(index != 0U && ResponseSplitCompare(&response->splits[index - 1U],
				&response->splits[index]) >= 0)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
	for (index = 0U; index < response->candidate_group_count; index++) {
		const sg_rune_compact_response_candidate_group_t *candidate =
			&response->candidate_groups[index];
		const sg_rune_compact_response_endpoint_group_t *source;
		const sg_rune_compact_response_endpoint_group_t *target;
		int area_state;

		if (candidate->source_group >= response->source_endpoint_group_count ||
			candidate->target_group >= response->target_endpoint_group_count ||
			candidate->classification !=
				SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL ||
			candidate->requires_exact_ray != 1U ||
			candidate->reserved[0] != 0U || candidate->reserved[1] != 0U ||
			(candidate->relation_flags &
				~(sg_rune_compact_static_relation_flags_t)
					SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING) != 0U ||
			(index != 0U &&
				(CompareU32(response->candidate_groups[index - 1U].source_group,
					candidate->source_group) > 0 ||
				 (response->candidate_groups[index - 1U].source_group ==
					candidate->source_group &&
				  response->candidate_groups[index - 1U].target_group >=
					candidate->target_group)))) {
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
		source = &response->source_endpoint_groups[candidate->source_group];
		target = &response->target_endpoint_groups[candidate->target_group];
		area_state = source->bsp_area != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
			target->bsp_area != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
			source->bsp_area != target->bsp_area;
		if (candidate->reason != ((target->flags &
			SG_RUNE_COMPACT_RESPONSE_ENDPOINT_MOVING) != 0U ?
			SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL :
			SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED) ||
			candidate->requires_area_state != (uint8_t)area_state ||
			((candidate->relation_flags &
				SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING) != 0U) !=
				(area_state != 0)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
	}
	source_group_by_fragment = malloc((size_t)response->source_fragment_count *
		sizeof(*source_group_by_fragment));
	target_group_by_patch = malloc((size_t)response->target_patch_count *
		sizeof(*target_group_by_patch));
	if (response->fact_count != 0U) {
		candidate_by_fact = malloc((size_t)response->fact_count *
			sizeof(*candidate_by_fact));
		fact_joins = malloc((size_t)response->fact_count *
			sizeof(*fact_joins));
		fact_join_scratch = malloc((size_t)response->fact_count *
			sizeof(*fact_join_scratch));
	}
	if (source_group_by_fragment == NULL || target_group_by_patch == NULL ||
		(response->fact_count != 0U && (candidate_by_fact == NULL ||
		 fact_joins == NULL || fact_join_scratch == NULL))) {
		free(source_group_by_fragment);
		free(target_group_by_patch);
		free(candidate_by_fact);
		free(fact_joins);
		free(fact_join_scratch);
		SetError(error, SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_RECORD_RESPONSE, 0U);
		return 0;
	}
	for (index = 0U; index < response->source_fragment_count; index++)
		source_group_by_fragment[index] = SG_RUNE_COMPACT_INDEX_NONE;
	for (index = 0U; index < response->target_patch_count; index++)
		target_group_by_patch[index] = SG_RUNE_COMPACT_INDEX_NONE;
	for (index = 0U; index < response->source_endpoint_group_count; index++) {
		const sg_rune_compact_response_endpoint_group_t *group =
			&response->source_endpoint_groups[index];
		uint32_t member;

		for (member = group->first_member;
			member < group->first_member + group->member_count; member++)
			source_group_by_fragment[
				response->source_endpoint_members[member]] = index;
	}
	for (index = 0U; index < response->target_endpoint_group_count; index++) {
		const sg_rune_compact_response_endpoint_group_t *group =
			&response->target_endpoint_groups[index];
		uint32_t member;

		for (member = group->first_member;
			member < group->first_member + group->member_count; member++)
			target_group_by_patch[response->target_endpoint_members[member]] = index;
	}
	/* Validate fact endpoints and build their canonical group-pair join keys
	 * in one pass.  The fact order itself remains the model's geometric order. */
	for (index = 0U; index < response->fact_count; index++) {
		const sg_rune_compact_response_fact_t *fact = &response->facts[index];
		uint32_t source_group = SG_RUNE_COMPACT_INDEX_NONE;
		uint32_t target_group = SG_RUNE_COMPACT_INDEX_NONE;

		if (fact->source_fragment < response->source_fragment_count)
			source_group = source_group_by_fragment[fact->source_fragment];
		if (fact->target_patch < response->target_patch_count)
			target_group = target_group_by_patch[fact->target_patch];
		if (fact->source_fragment >= response->source_fragment_count ||
			fact->target_patch >= response->target_patch_count ||
			source_group == SG_RUNE_COMPACT_INDEX_NONE ||
			target_group == SG_RUNE_COMPACT_INDEX_NONE || (index != 0U &&
				ResponseFactsCompare(model, &response->facts[index - 1U], fact) >= 0)) {
			free(source_group_by_fragment);
			free(target_group_by_patch);
			free(candidate_by_fact);
			free(fact_joins);
			free(fact_join_scratch);
			SetError(error, SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
		fact_joins[index].key = ResponseGroupPairKey(source_group, target_group);
		fact_joins[index].fact = index;
	}
	ResponseFactJoinsSort(fact_joins, fact_join_scratch, response->fact_count);
	{
		uint32_t candidate_index = 0U;

		for (index = 0U; index < response->fact_count; index++) {
			const uint64_t fact_key = fact_joins[index].key;

			while (candidate_index < response->candidate_group_count &&
				ResponseGroupPairKey(response->candidate_groups[
					candidate_index].source_group, response->candidate_groups[
					candidate_index].target_group) < fact_key)
				candidate_index++;
			if (candidate_index >= response->candidate_group_count ||
				ResponseGroupPairKey(response->candidate_groups[
					candidate_index].source_group, response->candidate_groups[
					candidate_index].target_group) != fact_key) {
				const uint32_t fact_index = fact_joins[index].fact;

				free(source_group_by_fragment);
				free(target_group_by_patch);
				free(candidate_by_fact);
				free(fact_joins);
				free(fact_join_scratch);
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_RESPONSE, fact_index);
				return 0;
			}
			candidate_by_fact[fact_joins[index].fact] = candidate_index;
		}
	}
	for (index = 0U; index < response->fact_count; index++) {
		const sg_rune_compact_response_fact_t *fact = &response->facts[index];
		const sg_rune_compact_response_candidate_group_t *candidate =
			&response->candidate_groups[candidate_by_fact[index]];
		const uint32_t certificate = fact->flags &
			(SG_RUNE_COMPACT_STATIC_RELATION_DIRECT |
			 SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT);

		if (candidate == NULL ||
			fact->visibility != candidate->classification ||
			fact->visibility_reason != candidate->reason ||
			fact->requires_exact_ray != candidate->requires_exact_ray ||
			fact->requires_area_state != candidate->requires_area_state ||
			fact->reserved[0] != 0U || fact->reserved[1] != 0U ||
			fact->flags != (candidate->relation_flags | certificate) ||
			certificate == 0U || certificate ==
				(SG_RUNE_COMPACT_STATIC_RELATION_DIRECT |
				 SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT) ||
			!SpanWithin(fact->occluders.first, fact->occluders.count,
				response->occluder_count) ||
			!ResponseTraceFiniteAndBound(response, fact)) {
			free(source_group_by_fragment);
			free(target_group_by_patch);
			free(candidate_by_fact);
			free(fact_joins);
			free(fact_join_scratch);
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_RESPONSE, index);
			return 0;
		}
		if (certificate == SG_RUNE_COMPACT_STATIC_RELATION_DIRECT) {
			if (fact->occluders.count != 0U || fact->certificate_split !=
				SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
				!ResponseTraceCanonicalNoHit(&fact->trace) ||
				!ResponseTraceEndsAtTarget(&fact->trace,
					&fact->target_witness)) {
				free(source_group_by_fragment);
				free(target_group_by_patch);
				free(candidate_by_fact);
				free(fact_joins);
				free(fact_join_scratch);
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_RESPONSE, index);
				return 0;
			}
			direct_count++;
		} else {
			if (!ResponseImpactTraceValid(model, response, fact)) {
				free(source_group_by_fragment);
				free(target_group_by_patch);
				free(candidate_by_fact);
				free(fact_joins);
				free(fact_join_scratch);
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_RESPONSE, index);
				return 0;
			}
			impact_count++;
		}
	}
	free(source_group_by_fragment);
	free(target_group_by_patch);
	free(candidate_by_fact);
	free(fact_joins);
	free(fact_join_scratch);
	if (response->seal.certified_direct_pair_count != direct_count ||
		response->seal.certified_static_impact_pair_count != impact_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_RESPONSE, 0U);
		return 0;
	}
	return 1;
}

static int WeaponKernelCompare(const sg_rune_weapon_response_kernel_t *left,
	const sg_rune_weapon_response_kernel_t *right)
{
	int comparison = CompareU32(left->profile, right->profile);

	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->family,
			(uint32_t)right->family);
	if (comparison == 0)
		comparison = CompareU32(left->functions.first, right->functions.first);
	if (comparison == 0)
		comparison = CompareU32(left->functions.count, right->functions.count);
	return comparison;
}

static int WeaponAttachmentCompare(
	const sg_rune_compact_weapon_field_attachment_t *left,
	const sg_rune_compact_weapon_field_attachment_t *right)
{
	int comparison = CompareU32(left->cell.value, right->cell.value);

	if (comparison == 0)
		comparison = CompareU32(left->source_surface, right->source_surface);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->relation_class,
			(uint32_t)right->relation_class);
	return comparison;
}

static int WeaponFactSupportsRelationClass(
	sg_rune_compact_weapon_relation_class_t relation_class,
	const sg_rune_compact_response_fact_t *fact)
{
	const int direct = (fact->flags &
		SG_RUNE_COMPACT_STATIC_RELATION_DIRECT) != 0U;
	const int penetrating = (fact->flags &
		SG_RUNE_COMPACT_STATIC_RELATION_PENETRATING) != 0U;
	const int impact = (fact->flags &
		SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT) != 0U;

	switch (relation_class) {
	case SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT:
		return direct;
	case SG_RUNE_COMPACT_WEAPON_RELATION_RAIL:
		return direct || penetrating;
	case SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT:
		return direct || impact;
	case SG_RUNE_COMPACT_WEAPON_RELATION_CLASS_COUNT:
		return 0;
	}
	return 0;
}

static const sg_rune_compact_weapon_field_attachment_t *
FindWeaponAttachment(const sg_rune_compact_model_t *model, uint32_t cell,
	uint32_t source_surface,
	sg_rune_compact_weapon_relation_class_t relation_class)
{
	uint32_t lower = 0U;
	uint32_t upper = model->weapon_attachment_count;

	while (lower < upper) {
		const uint32_t index = lower + (upper - lower) / 2U;
		const sg_rune_compact_weapon_field_attachment_t *attachment =
			&model->weapon_attachments[index];
		int comparison = CompareU32(cell, attachment->cell.value);

		if (comparison == 0)
			comparison = CompareU32(source_surface,
				attachment->source_surface);
		if (comparison == 0)
			comparison = CompareU32((uint32_t)relation_class,
				(uint32_t)attachment->relation_class);
		if (comparison == 0)
			return attachment;
		if (comparison < 0)
			upper = index;
		else
			lower = index + 1U;
	}
	return NULL;
}

static int WeaponAttachmentReferencesFact(const sg_rune_compact_model_t *model,
	const sg_rune_compact_weapon_field_attachment_t *attachment,
	uint32_t fact_index)
{
	uint32_t lower = 0U;
	uint32_t upper = attachment->relations.count;

	while (lower < upper) {
		const uint32_t offset = lower + (upper - lower) / 2U;
		const uint32_t referenced_fact = model->weapon_relation_refs[
			attachment->relations.first + offset].index;

		if (referenced_fact == fact_index)
			return 1;
		if (fact_index < referenced_fact)
			upper = offset;
		else
			lower = offset + 1U;
	}
	return 0;
}

static int ValidateWeaponAttachments(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t attachment_index;
	uint32_t relation_cursor = 0U;
	uint32_t span_index;
	uint64_t expected_relation_ref_count = 0U;

	/* The producer seals one sorted class key per span.  Keeping both arrays in
	 * lockstep makes a span's provenance unambiguous and rejects aliasing. */
	if (model->weapon_attachment_count != model->weapon_relation_span_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT, 0U);
		return 0;
	}

	for (span_index = 0U; span_index < model->weapon_relation_span_count;
		span_index++) {
		const sg_rune_compact_weapon_relation_span_t *span =
			&model->weapon_relation_spans[span_index];
		uint32_t offset;

		if (span->references.first != relation_cursor ||
			span->references.count == 0U || !SpanWithin(span->references.first,
				span->references.count, model->weapon_relation_ref_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_WEAPON_RELATION_SPAN, span_index);
			return 0;
		}
		for (offset = 0U; offset < span->references.count; offset++) {
			const uint32_t relation_index = span->references.first + offset;
			const sg_rune_compact_response_ref_t *reference =
				&model->weapon_relation_refs[relation_index];

			if (reference->kind !=
					SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT ||
				reference->index >= model->response.fact_count ||
				(offset != 0U && ResponseRefCompare(
					&model->weapon_relation_refs[relation_index - 1U],
					reference) >= 0)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_WEAPON_RELATION_SPAN, span_index);
				return 0;
			}
		}
		relation_cursor += span->references.count;
	}
	if (relation_cursor != model->weapon_relation_ref_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_WEAPON_RELATION_SPAN, span_index);
		return 0;
	}

	for (attachment_index = 0U;
		attachment_index < model->weapon_attachment_count; attachment_index++) {
		const sg_rune_compact_weapon_field_attachment_t *attachment =
			&model->weapon_attachments[attachment_index];
		uint32_t offset;

		if ((attachment_index != 0U && WeaponAttachmentCompare(
				&model->weapon_attachments[attachment_index - 1U],
				attachment) >= 0) ||
			attachment->cell.value >= model->cell_count ||
			attachment->source_surface >= model->source_surface_count ||
			(uint32_t)attachment->relation_class >=
				(uint32_t)SG_RUNE_COMPACT_WEAPON_RELATION_CLASS_COUNT ||
			attachment->relation_span != attachment_index ||
			attachment->reserved0 != 0U || attachment->reserved1 != 0U ||
			attachment->relations.count == 0U ||
			!SpanWithin(attachment->relations.first,
				attachment->relations.count,
				model->weapon_relation_ref_count)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT, attachment_index);
			return 0;
		}
		if (memcmp(&attachment->relations,
			&model->weapon_relation_spans[attachment->relation_span].references,
			sizeof(attachment->relations)) != 0) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT, attachment_index);
			return 0;
		}
		for (offset = 0U; offset < attachment->relations.count; offset++) {
			const uint32_t relation_index = attachment->relations.first + offset;
			const sg_rune_compact_response_ref_t *reference =
				&model->weapon_relation_refs[relation_index];
			const sg_rune_compact_response_fact_t *fact;
			const sg_rune_compact_response_fragment_t *fragment;
			const sg_rune_compact_response_patch_t *patch;

			if (reference->kind !=
					SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT ||
				reference->index >= model->response.fact_count) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT,
					attachment_index);
				return 0;
			}
			fact = &model->response.facts[reference->index];
			fragment = &model->response.source_fragments[fact->source_fragment];
			patch = &model->response.target_patches[fact->target_patch];
			if (fragment->parent_cell.value != attachment->cell.value ||
				patch->source_surface != attachment->source_surface ||
				!WeaponFactSupportsRelationClass(
					attachment->relation_class, fact)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT,
					attachment_index);
				return 0;
			}
		}
	}

	/* Certified facts are the authoritative source of attachment membership.
	 * Attachments and their spans have already proved a canonical, sorted
	 * representation above; enumerate every fact-implied key so omission or
	 * substitution cannot survive as an otherwise well-formed sparse field. */
	for (span_index = 0U; span_index < model->response.fact_count;
		span_index++) {
		const sg_rune_compact_response_fact_t *fact =
			&model->response.facts[span_index];
		const sg_rune_compact_response_fragment_t *fragment =
			&model->response.source_fragments[fact->source_fragment];
		const sg_rune_compact_response_patch_t *patch =
			&model->response.target_patches[fact->target_patch];
		uint32_t relation_class;

		for (relation_class =
			(uint32_t)SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT;
			relation_class <
			(uint32_t)SG_RUNE_COMPACT_WEAPON_RELATION_CLASS_COUNT;
			relation_class++) {
			const sg_rune_compact_weapon_field_attachment_t *attachment;

			if (!WeaponFactSupportsRelationClass(
				(sg_rune_compact_weapon_relation_class_t)relation_class,
				fact))
				continue;
			expected_relation_ref_count++;
			attachment = FindWeaponAttachment(model,
				fragment->parent_cell.value, patch->source_surface,
				(sg_rune_compact_weapon_relation_class_t)relation_class);
			if (attachment == NULL || !WeaponAttachmentReferencesFact(model,
				attachment, span_index)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT, span_index);
				return 0;
			}
		}
	}
	if (expected_relation_ref_count != model->weapon_relation_ref_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_WEAPON_ATTACHMENT, 0U);
		return 0;
	}
	return 1;
}

static int ValidateWeaponFields(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint64_t catalog_id;
	uint32_t profile_index;
	uint32_t kernel_cursor = 0U;
	uint32_t reference_cursor = 0U;

	if (model->weapon_profile_count !=
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT ||
		!SG_RuneCompactWeaponProfileCatalogId(model->weapon_profiles,
			model->weapon_profile_count, &catalog_id) ||
		catalog_id != model->identity.weapon_profile_catalog_id) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE, 0U);
		return 0;
	}
	for (profile_index = 0U; profile_index < model->weapon_profile_count;
		profile_index++) {
		const sg_rune_weapon_profile_t *profile =
			&model->weapon_profiles[profile_index];
		uint32_t family;

		if (profile->source_profile != profile_index + 1U ||
			profile->response_families !=
				SG_RuneCompactWeaponCanonicalProfileMask(profile->source_profile) ||
			!SG_RuneCompactWeaponProfileShapeValid(profile)) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE, profile_index);
			return 0;
		}
		for (family = 0U;
			family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT; family++) {
			const sg_rune_weapon_response_family_mask_t bit =
				SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family);
			const sg_rune_weapon_response_kernel_t *kernel;
			sg_rune_weapon_event_law_t expected_law;
			uint32_t expected_count;
			uint32_t offset;

			if ((profile->response_families & bit) == 0U)
				continue;
			if (kernel_cursor >= model->weapon_kernel_count) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
				return 0;
			}
			kernel = &model->weapon_kernels[kernel_cursor];
			if ((kernel_cursor != 0U && WeaponKernelCompare(
				&model->weapon_kernels[kernel_cursor - 1U], kernel) >= 0) ||
				kernel->profile != profile_index ||
				(uint32_t)kernel->family != family ||
				kernel->functions.first != reference_cursor ||
				!SG_RuneCompactWeaponCanonicalEventLaw(profile->source_profile,
					kernel->family, &expected_law) ||
				kernel->event_law.kind != expected_law.kind ||
				kernel->event_law.requirements != expected_law.requirements ||
				!SG_RuneCompactWeaponKernelReferenceCount(profile, kernel->family,
					&expected_count) ||
				kernel->functions.count != expected_count ||
				!SpanWithin(kernel->functions.first, kernel->functions.count,
					model->weapon_function_ref_count)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
					SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
				return 0;
			}
			for (offset = 0U; offset < kernel->functions.count; offset++) {
				const sg_rune_weapon_function_ref_t *reference =
					&model->weapon_function_refs[reference_cursor + offset];
				sg_rune_weapon_effect_channel_t channel;
				uint32_t instance;
				sg_rune_analytic_output_meaning_t output;

				if (reference->function.value >= model->analytic->function_count ||
					!SG_RuneCompactWeaponFunctionRefExpected(profile,
						kernel->family, offset, &channel, &instance, &output) ||
					reference->channel != channel || reference->instance != instance ||
					model->analytic->functions[reference->function.value].output != output) {
					SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
						SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
					return 0;
				}
			}
			reference_cursor += kernel->functions.count;
			kernel_cursor++;
		}
	}
	if (kernel_cursor != model->weapon_kernel_count ||
		reference_cursor != model->weapon_function_ref_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, kernel_cursor);
		return 0;
	}
	return ValidateWeaponAttachments(model, error);
}

static int AuthorityStaticTransitionMatches(
	const sg_rune_compact_mechanism_authority_t *authority,
	const sg_rune_compact_mechanism_transition_t *transition,
	const sg_rune_compact_static_transition_t *static_transition)
{
	if (memcmp((const unsigned char *)transition +
			offsetof(sg_rune_compact_mechanism_transition_t, kind),
			(const unsigned char *)static_transition +
			offsetof(sg_rune_compact_static_transition_t, kind), 28U) != 0)
		return 0;
	if (transition->kind !=
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE)
		return memcmp((const unsigned char *)transition + 32U,
			(const unsigned char *)static_transition + 32U, 216U) == 0;
	return transition->value.portal_state.portal.value ==
			static_transition->value.portal_state.portal.value &&
		transition->value.portal_state.mover_model ==
			static_transition->value.portal_state.mover_model &&
		transition->value.portal_state.source_blocked ==
			static_transition->value.portal_state.source_blocked &&
		transition->value.portal_state.destination_blocked ==
			static_transition->value.portal_state.destination_blocked &&
		transition->value.portal_state.delay_ms == authority->delay_ms &&
		transition->value.portal_state.dwell_ms == authority->dwell_ms &&
		transition->value.portal_state.pause_ms == authority->pause_ms &&
		transition->value.portal_state.travel_ms == authority->travel_ms &&
		transition->value.portal_state.recovery_ms == authority->recovery_ms &&
		transition->elapsed_ms ==
			(uint64_t)transition->value.portal_state.travel_ms &&
		static_transition->value.portal_state.delay_ms == authority->delay_ms &&
		static_transition->value.portal_state.dwell_ms == authority->dwell_ms &&
		static_transition->value.portal_state.pause_ms == authority->pause_ms &&
		static_transition->value.portal_state.travel_ms == authority->travel_ms &&
		static_transition->value.portal_state.recovery_ms == authority->recovery_ms;
}

static int AuthorityTransitionStatesValid(
	const sg_rune_compact_mechanism_authority_t *authority,
	const sg_rune_compact_mechanism_transition_t *transition)
{
	sg_rune_compact_mechanism_authority_state_t return_state;

	if (authority == NULL || transition == NULL)
		return 0;
	switch (transition->kind) {
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
		if (authority->initial_state == authority->activated_state)
			return 0;
		if (transition->source_state == authority->initial_state &&
			transition->destination_state == authority->activated_state)
			return 1;
		if ((authority->flags &
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ONE_SHOT) != 0U)
			return 0;
		return_state = authority->reset_state == authority->activated_state ?
			authority->initial_state : authority->reset_state;
		return transition->source_state == authority->activated_state &&
			transition->destination_state == return_state;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
		return transition->source_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			transition->destination_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
		if (authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT ||
			(authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN &&
			 (authority->activation &
				SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO) == 0U))
			return transition->source_state == authority->initial_state &&
				transition->destination_state == authority->activated_state;
		if (authority->kind == SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN)
			return transition->source_state ==
					SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
				transition->destination_state ==
					SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
		return 0;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		break;
	}
	return 0;
}

static int ValidateMechanismAuthorities(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error)
{
	uint32_t authority_index;
	uint32_t controller_cursor = 0U;
	uint32_t topology_cursor = 0U;
	uint32_t transition_cursor = 0U;

	if (model->static_data == NULL ||
		model->mechanism_authority_transition_count !=
			model->static_data->transition_count) {
		SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_RECORD_MECHANISM_TRANSITION, 0U);
		return 0;
	}
	for (authority_index = 0U;
		authority_index < model->mechanism_authority_transition_count;
		authority_index++) {
		const uint32_t static_index =
			model->mechanism_authority_transition_static_indices[authority_index];

		if (static_index >= model->static_data->transition_count ||
			model->static_transition_authority_indices[static_index] !=
				authority_index) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_MECHANISM_TRANSITION, authority_index);
			return 0;
		}
	}
	for (authority_index = 0U;
		authority_index < model->static_data->transition_count;
		authority_index++) {
		const uint32_t authority_transition =
			model->static_transition_authority_indices[authority_index];

		if (authority_transition >=
				model->mechanism_authority_transition_count ||
			model->mechanism_authority_transition_static_indices[
				authority_transition] != authority_index) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_MECHANISM_TRANSITION,
				authority_transition);
			return 0;
		}
	}

	for (authority_index = 0U;
		authority_index < model->mechanism_authority_count; authority_index++) {
		const sg_rune_compact_mechanism_authority_t *authority =
			&model->mechanism_authorities[authority_index];
		uint32_t offset;

		if ((authority_index != 0U && model->mechanism_authorities[
			authority_index - 1U].source.entity_ordinal >=
				authority->source.entity_ordinal) ||
			authority->source.entity_ordinal >=
				model->identity.source_counts.entity_count ||
			authority->kind >= SG_RUNE_COMPACT_MECHANISM_AUTHORITY_KIND_COUNT ||
			authority->activation == 0U ||
			(authority->activation &
				~(uint32_t)SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_KNOWN) != 0U ||
			authority->activation_cell.value >= model->cell_count ||
			authority->controllers.first != controller_cursor ||
			authority->topology.first != topology_cursor ||
			authority->transitions.first != transition_cursor ||
			!SpanWithin(authority->controllers.first, authority->controllers.count,
				model->mechanism_authority_controller_count) ||
			!SpanWithin(authority->topology.first, authority->topology.count,
				model->mechanism_authority_topology_edge_count) ||
			!SpanWithin(authority->transitions.first, authority->transitions.count,
				model->mechanism_authority_transition_count) ||
			authority->initial_state >=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			authority->activated_state >=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			authority->reset_state >=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			(authority->flags &
				~(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_FLAGS_KNOWN) != 0U) {
			SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_RECORD_MECHANISM_AUTHORITY, authority_index);
			return 0;
		}
		for (offset = authority->controllers.first;
			offset < authority->controllers.first + authority->controllers.count;
			offset++) {
			const sg_rune_compact_mechanism_controller_t *controller =
				&model->mechanism_authority_controllers[offset];

			if (controller->mechanism != authority_index ||
				controller->controller.entity_ordinal >=
					model->identity.source_counts.entity_count ||
				controller->topology_edge < authority->topology.first ||
				controller->topology_edge >= authority->topology.first +
					authority->topology.count || controller->activation == 0U ||
				(controller->activation &
					~(uint32_t)SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_KNOWN) != 0U ||
				(controller->flags &
					~(uint32_t)SG_RUNE_COMPACT_MECHANISM_CONTROLLER_FLAGS_KNOWN) != 0U ||
				controller->spatiality >=
					SG_RUNE_COMPACT_MECHANISM_CONTROLLER_SPATIALITY_COUNT ||
				!ReservedBytesZero(controller->reserved) ||
				(controller->spatiality ==
					SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL ?
				 (controller->activation_cell.value != SG_RUNE_COMPACT_INDEX_NONE ||
				  !ControllerLocationZero(controller)) :
				 (controller->activation_cell.value >= model->cell_count ||
				  !BoundsValid(&controller->activation_bounds) ||
				  !PointInHalfOpenBounds(&controller->activation_witness,
					&controller->activation_bounds) ||
				  !PointInHalfOpenBounds(&controller->activation_witness,
					&model->cells[controller->activation_cell.value].bounds)))) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_MECHANISM_CONTROLLER, offset);
				return 0;
			}
		}
		for (offset = authority->topology.first;
			offset < authority->topology.first + authority->topology.count;
			offset++) {
			const sg_rune_compact_mechanism_topology_edge_t *edge =
				&model->mechanism_authority_topology_edges[offset];

			if (edge->source.entity_ordinal >=
					model->identity.source_counts.entity_count ||
				edge->destination.entity_ordinal >=
					model->identity.source_counts.entity_count ||
				edge->kind < SG_MECH_EDGE_TARGET ||
				edge->kind > SG_MECH_EDGE_ROUTE_TARGET) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_MECHANISM_TOPOLOGY, offset);
				return 0;
			}
		}
		for (offset = authority->transitions.first;
			offset < authority->transitions.first + authority->transitions.count;
			offset++) {
			const sg_rune_compact_mechanism_transition_t *transition =
				&model->mechanism_authority_transitions[offset];
			const uint32_t static_index =
				model->mechanism_authority_transition_static_indices[offset];
			const sg_rune_compact_static_transition_t *static_transition =
				&model->static_data->transitions[static_index];
			const sg_rune_compact_mechanism_t *static_owner;

			if (static_transition->mechanism.value >=
				model->static_data->mechanism_count) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_MECHANISM_TRANSITION, offset);
				return 0;
			}
			static_owner = &model->static_data->mechanisms[
				static_transition->mechanism.value];
			if (transition->mechanism != authority_index || transition->kind >=
					SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT ||
				transition->entry_cell.value >= model->cell_count ||
				transition->exit_cell.value >= model->cell_count ||
				transition->source_state >=
					SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
				transition->destination_state >=
					SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
				static_owner->source.entity_ordinal !=
					authority->source.entity_ordinal ||
				(uint32_t)static_owner->kind != (uint32_t)authority->kind ||
				!AuthorityTransitionStatesValid(authority, transition) ||
				!AuthorityStaticTransitionMatches(authority, transition,
					static_transition)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_MECHANISM_TRANSITION, offset);
				return 0;
			}
			if (transition->kind ==
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE &&
				(transition->value.portal_state.portal.value >= model->portal_count ||
				 transition->value.portal_state.source_blocked ==
					transition->value.portal_state.destination_blocked ||
				 transition->value.portal_state.reserved[0] != 0U ||
				 transition->value.portal_state.reserved[1] != 0U)) {
				SetError(error, SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_RECORD_MECHANISM_TRANSITION, offset);
				return 0;
			}
		}
		controller_cursor += authority->controllers.count;
		topology_cursor += authority->topology.count;
		transition_cursor += authority->transitions.count;
	}
	return controller_cursor == model->mechanism_authority_controller_count &&
		topology_cursor == model->mechanism_authority_topology_edge_count &&
		transition_cursor == model->mechanism_authority_transition_count;
}

int SG_RuneCompactModelValidate(const sg_rune_compact_model_t *model,
	sg_rune_compact_error_t *error_out)
{
	sg_rune_analytic_error_t analytic_error;
	sg_rune_compact_static_error_t static_error;
	uint32_t function_cursor = 0U;

	SetError(error_out, SG_RUNE_COMPACT_ERROR_NONE,
		SG_RUNE_COMPACT_RECORD_MODEL, 0U);
	if (!model) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (model->version != SG_RUNE_COMPACT_MODEL_VERSION ||
		model->schema_tag != SG_RUNE_COMPACT_MODEL_SCHEMA_TAG ||
		model->reserved != 0U) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_UNSUPPORTED_VERSION,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (!IdentityValid(&model->identity)) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (!ValidateCounts(model, error_out))
		return 0;
	if (!SG_RuneCompactAnalyticValidate(model->analytic, &analytic_error)) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
			SG_RUNE_COMPACT_RECORD_MODEL, analytic_error.record);
		return 0;
	}
	if (!ValidateAnalyticUses(model, error_out))
		return 0;
	if (!ValidateCells(model, error_out) ||
		!ValidateFacetsAndIncidences(model, error_out) ||
		!ValidateSourceSurfaces(model, error_out) ||
		!ValidatePortals(model, error_out) ||
		!ValidateResponseProjection(model, error_out) ||
		!ValidateMechanismAuthorities(model, error_out) ||
		!ValidateWeaponFields(model, error_out))
		return 0;
	if (!SG_RuneCompactStaticValidate(model, model->static_data,
		&static_error)) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_INVALID_STATIC_DATA,
			SG_RUNE_COMPACT_RECORD_MODEL, static_error.record);
		return 0;
	}
	if (!ValidateMovementFields(model, model->static_data, &function_cursor,
		error_out))
		return 0;
	if (function_cursor != model->movement_fiber_function_ref_count) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD,
			SG_RUNE_COMPACT_RECORD_MODEL, function_cursor);
		return 0;
	}
	return 1;
}

int SG_RuneCompactModelValidateBound(const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_error_t *error_out)
{
	if (!expected_identity) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	if (!SG_RuneCompactModelValidate(model, error_out))
		return 0;
	if (!SG_RuneCompactIdentityMatches(&model->identity, expected_identity)) {
		SetError(error_out, SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_RECORD_MODEL, 0U);
		return 0;
	}
	return 1;
}

const char *SG_RuneCompactModelErrorString(sg_rune_compact_error_code_t code)
{
	switch (code) {
	case SG_RUNE_COMPACT_ERROR_NONE:
		return "none";
	case SG_RUNE_COMPACT_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_ERROR_UNSUPPORTED_VERSION:
		return "unsupported version";
	case SG_RUNE_COMPACT_ERROR_NONZERO_RESERVED:
		return "nonzero reserved field";
	case SG_RUNE_COMPACT_ERROR_LIMIT_EXCEEDED:
		return "limit exceeded";
	case SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER:
		return "noncanonical order";
	case SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE:
		return "invalid provenance";
	case SG_RUNE_COMPACT_ERROR_INVALID_GEOMETRY:
		return "invalid geometry";
	case SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE:
		return "invalid reference";
	case SG_RUNE_COMPACT_ERROR_INVALID_TOPOLOGY:
		return "invalid topology";
	case SG_RUNE_COMPACT_ERROR_INVALID_STANCE:
		return "invalid stance";
	case SG_RUNE_COMPACT_ERROR_INVALID_ANALYTIC_FIELD:
		return "invalid analytic field";
	case SG_RUNE_COMPACT_ERROR_INVALID_STATIC_DATA:
		return "invalid static data";
	case SG_RUNE_COMPACT_ERROR_CODE_COUNT:
		break;
	}
	return "unknown compact RUNE model error";
}
