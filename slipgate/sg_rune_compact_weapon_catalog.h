/* Shared immutable identities for the sealed compact weapon catalog. */
#ifndef SG_RUNE_COMPACT_WEAPON_CATALOG_H
#define SG_RUNE_COMPACT_WEAPON_CATALOG_H

#include <stdint.h>

#include "sg_rune_source_authority.h"
#include "sg_weapon_effect_profile.h"

/* Producer identity is part of every resolved profile and therefore of the
 * sealed weapon-law hash.  Production uses the same fixed value as the
 * offline builder; an artifact cannot select its own resolver identity. */
#define SG_RUNE_COMPACT_WEAPON_PRODUCER_ID UINT64_C(0x53474255494c4401)

/* Resolve every canonical host profile from the captured source-law switches.
 * Both construction and production admission use this path before hashing. */
int SG_RuneCompactWeaponProfilesResolve(
	const sg_rune_source_weapon_law_t *static_law,
	uint64_t physics_abi_id, sg_weapon_profile_t *profiles_out,
	uint32_t profile_count);

/* Hash every static source-law switch and every resolved host profile.  This
 * is deliberately shared by the builder and field admission boundary so a
 * stale ID can never certify changed weapon behavior. */
int SG_RuneCompactWeaponLawIdentity(
	const sg_rune_source_weapon_law_t *static_law,
	const sg_weapon_profile_t *profiles, uint32_t profile_count,
	uint64_t *identity_out);

#endif
