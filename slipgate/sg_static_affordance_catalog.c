#include "sg_static_affordance_catalog.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sg_configuration_audit.h"

#define SG_STATIC_AFFORDANCE_CATALOG_MAGIC UINT64_C(0x5341434154303032)
#define SG_STATIC_AFFORDANCE_CATALOG_FNV_OFFSET UINT64_C(1469598103934665603)
#define SG_STATIC_AFFORDANCE_CATALOG_FNV_PRIME UINT64_C(1099511628211)

typedef struct sg_static_affordance_catalog_layout_s
{
	size_t partitions;
	size_t area_components;
	size_t occluders;
	size_t surfaces;
	size_t classifications;
	size_t weapons;
	size_t controls;
	size_t surface_rules;
	size_t terminals;
	size_t relations;
	size_t relation_domains;
	size_t allocation_size;
	uint64_t classification_count;
	uint32_t relation_domain_count;
} sg_static_affordance_catalog_layout_t;

typedef struct sg_static_affordance_catalog_hook_source_s
{
	sg_hook_visibility_catalog_evidence_view_t evidence;
	uint32_t terminal_count;
	uint32_t relation_count;
	uint32_t relation_domain_count;
} sg_static_affordance_catalog_hook_source_t;

struct sg_static_affordance_catalog_s
{
	uint64_t magic;
	uint64_t magic_inverse;
	const sg_static_affordance_catalog_t *self;
	size_t allocation_size;
	size_t allocation_size_inverse;
	sg_static_affordance_catalog_evidence_view_t evidence;
};

static const sg_static_affordance_catalog_authority_t authority_order[] = {
	SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY,
	SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE,
	SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY
};

static const sg_weapon_static_relation_t weapon_relation_order[] = {
	SG_WEAPON_STATIC_DIRECT_VISIBILITY,
	SG_WEAPON_STATIC_PROJECTILE_CORRIDOR,
	SG_WEAPON_STATIC_IMPACT_SURFACE,
	SG_WEAPON_STATIC_BLAST_REACH,
	SG_WEAPON_STATIC_BOUNCE_SURFACE,
	SG_WEAPON_STATIC_SECONDARY_BLAST_REACH,
	SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY
};

_Static_assert(sizeof(authority_order) / sizeof(authority_order[0]) ==
	SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT,
	"static-affordance authority order must be complete");
_Static_assert((int)SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT_VALUE ==
	(int)SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT,
	"static-affordance authority count mismatch");
_Static_assert(sizeof(weapon_relation_order) / sizeof(weapon_relation_order[0]) ==
	SG_WEAPON_STATIC_RELATION_COUNT,
	"weapon relation order must be complete");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_HOOKABLE == 0,
	"hook outcome order mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED == 4,
	"hook outcome order mismatch");
_Static_assert(SG_STATIC_VISIBILITY_CONDITIONAL <= UINT8_MAX,
	"static visibility class must fit the immutable catalog");
_Static_assert(SG_STATIC_VISIBILITY_REASON_SKY <= UINT8_MAX,
	"static visibility reason must fit the immutable catalog");

static void SetError(sg_static_affordance_catalog_error_t *error_out,
	sg_static_affordance_catalog_error_code_t code,
	sg_static_affordance_catalog_authority_t authority)
{
	if (!error_out || error_out->code != SG_STATIC_AFFORDANCE_CATALOG_ERROR_NONE)
		return;
	error_out->code = code;
	error_out->authority = authority;
}

static int AddArray(size_t *size, size_t count, size_t element_size,
	size_t alignment, size_t *offset_out)
{
	size_t aligned;

	if (!size || !offset_out || !alignment ||
		(alignment & (alignment - 1U)) != 0U ||
		*size > SIZE_MAX - (alignment - 1U))
		return 0;
	aligned = (*size + alignment - 1U) & ~(alignment - 1U);
	if (element_size != 0U && count > (SIZE_MAX - aligned) / element_size)
		return 0;
	*offset_out = aligned;
	*size = aligned + count * element_size;
	return 1;
}

static int ClassificationCount(uint32_t partition_count,
	uint64_t *count_out)
{
	const uint64_t count = (uint64_t)partition_count;

	if (!count_out || (count != 0U && count > UINT64_MAX / count))
		return 0;
	*count_out = count * count;
	return 1;
}

static int BuildLayout(uint32_t partition_count, uint32_t area_count,
	uint32_t occluder_count, uint32_t surface_count, uint32_t weapon_count,
	uint32_t control_count, uint32_t surface_rule_count,
	uint32_t terminal_count, uint32_t relation_count,
	uint32_t relation_domain_count,
	sg_static_affordance_catalog_layout_t *layout_out)
{
	sg_static_affordance_catalog_layout_t layout;
	size_t size = sizeof(sg_static_affordance_catalog_t);

	if (!layout_out)
		return 0;
	memset(&layout, 0, sizeof(layout));
	if (!ClassificationCount(partition_count, &layout.classification_count) ||
		layout.classification_count > (uint64_t)SIZE_MAX ||
		!AddArray(&size, partition_count,
			sizeof(sg_static_visibility_partition_t),
			_Alignof(sg_static_visibility_partition_t), &layout.partitions) ||
		!AddArray(&size, area_count, sizeof(uint32_t), _Alignof(uint32_t),
			&layout.area_components) ||
		!AddArray(&size, occluder_count,
			sizeof(sg_static_visibility_occluder_t),
			_Alignof(sg_static_visibility_occluder_t), &layout.occluders) ||
		!AddArray(&size, surface_count,
			sizeof(sg_static_visibility_surface_t),
			_Alignof(sg_static_visibility_surface_t), &layout.surfaces) ||
		!AddArray(&size, (size_t)layout.classification_count,
			sizeof(sg_static_affordance_catalog_visibility_classification_t),
			_Alignof(sg_static_affordance_catalog_visibility_classification_t),
			&layout.classifications) ||
		!AddArray(&size, weapon_count,
			sizeof(sg_static_affordance_catalog_weapon_evidence_t),
			_Alignof(sg_static_affordance_catalog_weapon_evidence_t),
			&layout.weapons) ||
		!AddArray(&size, control_count,
			sizeof(sg_hook_visibility_control_root_t),
			_Alignof(sg_hook_visibility_control_root_t), &layout.controls) ||
		!AddArray(&size, surface_rule_count,
			sizeof(sg_hook_visibility_surface_rule_t),
			_Alignof(sg_hook_visibility_surface_rule_t),
			&layout.surface_rules) ||
		!AddArray(&size, terminal_count,
			sizeof(sg_static_affordance_catalog_hook_terminal_t),
			_Alignof(sg_static_affordance_catalog_hook_terminal_t),
			&layout.terminals) ||
		!AddArray(&size, relation_count,
			sizeof(sg_static_affordance_catalog_hook_relation_t),
			_Alignof(sg_static_affordance_catalog_hook_relation_t),
			&layout.relations) ||
		!AddArray(&size, relation_domain_count,
			sizeof(sg_hook_visibility_domain_term_t),
			_Alignof(sg_hook_visibility_domain_term_t),
			&layout.relation_domains))
		return 0;
	layout.allocation_size = size;
	layout.relation_domain_count = relation_domain_count;
	*layout_out = layout;
	return 1;
}

static void *CatalogAt(sg_static_affordance_catalog_t *catalog,
	size_t offset)
{
	return (void *)((unsigned char *)catalog + offset);
}

static const void *CatalogAtConst(
	const sg_static_affordance_catalog_t *catalog, size_t offset)
{
	return (const void *)((const unsigned char *)catalog + offset);
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left && right && left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		memcmp(&left->standing_hull, &right->standing_hull,
			sizeof(left->standing_hull)) == 0 &&
		memcmp(&left->crouching_hull, &right->crouching_hull,
			sizeof(left->crouching_hull)) == 0 &&
		memcmp(&left->physics, &right->physics,
			sizeof(left->physics)) == 0;
}

static int BindingEqual(const sg_weapon_static_binding_t *left,
	const sg_weapon_static_binding_t *right)
{
	return left && right &&
		SG_RuneV2ContentIdEqual(&left->artifact_identity,
			&right->artifact_identity) &&
		SG_RuneV2ContentIdEqual(&left->bsp_identity,
			&right->bsp_identity) &&
		SG_RuneV2ContentIdEqual(&left->schema_identity,
			&right->schema_identity) &&
		left->source_set_identity == right->source_set_identity &&
		left->visibility_revision == right->visibility_revision;
}

static int PublicationSourcesAccepted(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility)
{
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_semantics_audit_result_t semantics_audit;
	sg_static_visibility_audit_result_t visibility_audit;

	return authority && configuration && semantics && visibility &&
		authority->identity.source_set_identity != 0U &&
		IdentityEqual(&authority->identity, &configuration->identity) &&
		IdentityEqual(&authority->identity, &semantics->identity) &&
		IdentityEqual(&authority->identity, &visibility->identity) &&
		SG_ConfigurationAudit(authority, configuration, &configuration_audit) &&
		SG_ConfigurationSemanticsAudit(authority, configuration, semantics,
			&semantics_audit) &&
		SG_StaticVisibilityAudit(authority, configuration, semantics, visibility,
			&visibility_audit);
}

static int HookSourceRead(const sg_hook_visibility_catalog_t *catalog,
	sg_static_affordance_catalog_hook_source_t *source_out)
{
	sg_static_affordance_catalog_hook_source_t source;
	uint32_t relation;

	if (!catalog || !source_out || !SG_HookVisibilityCatalogEvidence(catalog,
		&source.evidence))
		return 0;
	source.terminal_count = SG_HookVisibilityCatalogTerminalCount(catalog);
	source.relation_count = SG_HookVisibilityCatalogRelationCount(catalog);
	source.relation_domain_count = 0U;
	for (relation = 0U; relation < source.relation_count; relation++)
	{
		sg_hook_visibility_catalog_relation_view_t view;

		if (!SG_HookVisibilityCatalogRelation(catalog, relation, &view) ||
			source.relation_domain_count > UINT32_MAX - view.domain_count)
			return 0;
		source.relation_domain_count += view.domain_count;
	}
	*source_out = source;
	return 1;
}

static int CatalogHeaderValid(const sg_static_affordance_catalog_t *catalog)
{
	return catalog && catalog->magic == SG_STATIC_AFFORDANCE_CATALOG_MAGIC &&
		catalog->magic_inverse == ~SG_STATIC_AFFORDANCE_CATALOG_MAGIC &&
		catalog->self == catalog &&
		catalog->allocation_size >= sizeof(*catalog) &&
		catalog->allocation_size_inverse == ~catalog->allocation_size;
}

static int CatalogStorageValid(const sg_static_affordance_catalog_t *catalog)
{
	const sg_static_affordance_catalog_evidence_view_t *evidence;
	sg_static_affordance_catalog_layout_t layout;

	if (!CatalogHeaderValid(catalog))
		return 0;
	evidence = &catalog->evidence;
	if (!BuildLayout(evidence->static_visibility.partition_count,
		evidence->static_visibility.area_count,
		evidence->static_visibility.occluder_count,
		evidence->static_visibility.surface_count, evidence->weapon_count,
		evidence->hook.control_count, evidence->hook.surface_rule_count,
		evidence->hook.terminal_count, evidence->hook.relation_count,
		evidence->hook.relation_domain_count, &layout) ||
		layout.allocation_size != catalog->allocation_size ||
		evidence->static_visibility.classification_count !=
			layout.classification_count)
		return 0;
	return evidence->static_visibility.partitions ==
			CatalogAtConst(catalog, layout.partitions) &&
		evidence->static_visibility.area_components ==
			CatalogAtConst(catalog, layout.area_components) &&
		evidence->static_visibility.occluders ==
			CatalogAtConst(catalog, layout.occluders) &&
		evidence->static_visibility.surfaces ==
			CatalogAtConst(catalog, layout.surfaces) &&
		evidence->static_visibility.classifications ==
			CatalogAtConst(catalog, layout.classifications) &&
		evidence->weapons == CatalogAtConst(catalog, layout.weapons) &&
		evidence->hook.controls == CatalogAtConst(catalog, layout.controls) &&
		evidence->hook.surface_rules ==
			CatalogAtConst(catalog, layout.surface_rules) &&
		evidence->hook.terminals == CatalogAtConst(catalog, layout.terminals) &&
		evidence->hook.relations == CatalogAtConst(catalog, layout.relations) &&
		evidence->hook.relation_domains ==
			CatalogAtConst(catalog, layout.relation_domains);
}

static int VisibilityClassValid(sg_static_visibility_class_t classification)
{
	return classification >= SG_STATIC_VISIBILITY_OCCLUDED &&
		classification <= SG_STATIC_VISIBILITY_CONDITIONAL;
}

static int VisibilityReasonValid(sg_static_visibility_reason_t reason)
{
	return reason >= SG_STATIC_VISIBILITY_REASON_NONE &&
		reason <= SG_STATIC_VISIBILITY_REASON_SKY;
}

static int VisibilityClassificationValid(
	const sg_static_affordance_catalog_static_visibility_evidence_t *visibility,
	uint64_t index)
{
	const sg_static_affordance_catalog_visibility_classification_t *record;

	if (!visibility || !visibility->classifications ||
		visibility->partition_count == 0U ||
		index >= visibility->classification_count)
		return 0;
	record = &visibility->classifications[index];
	if (record->requires_exact_ray > 1U || record->requires_area_state > 1U ||
		!VisibilityClassValid((sg_static_visibility_class_t)
			record->classification) ||
		!VisibilityReasonValid((sg_static_visibility_reason_t)record->reason))
		return 0;
	switch ((sg_static_visibility_class_t)record->classification)
	{
	case SG_STATIC_VISIBILITY_VISIBLE:
		return record->reason == SG_STATIC_VISIBILITY_REASON_NONE &&
			record->requires_exact_ray == 0U &&
			record->requires_area_state == 0U;
	case SG_STATIC_VISIBILITY_CONDITIONAL:
		return record->requires_exact_ray == 1U &&
			(record->reason == SG_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED ||
			 record->reason == SG_STATIC_VISIBILITY_REASON_AREA_PORTAL_STATE) &&
			record->requires_area_state == (uint32_t)(record->reason ==
				SG_STATIC_VISIBILITY_REASON_AREA_PORTAL_STATE);
	case SG_STATIC_VISIBILITY_OCCLUDED:
		return (record->reason == SG_STATIC_VISIBILITY_REASON_PVS ||
			record->reason == SG_STATIC_VISIBILITY_REASON_AREA_GRAPH) &&
			record->requires_exact_ray == 0U &&
			record->requires_area_state == 0U;
	}
	return 0;
}

static int StaticVisibilityEvidenceValid(
	const sg_static_affordance_catalog_static_visibility_evidence_t *visibility)
{
	uint32_t index;
	uint64_t classification;

	if (!visibility || visibility->identity.source_set_identity == 0U ||
		visibility->revision == 0U ||
		(visibility->partition_count != 0U && !visibility->partitions) ||
		(visibility->area_count != 0U && !visibility->area_components) ||
		(visibility->occluder_count != 0U && !visibility->occluders) ||
		(visibility->surface_count != 0U && !visibility->surfaces) ||
		(visibility->classification_count != 0U &&
			!visibility->classifications) ||
		!ClassificationCount(visibility->partition_count, &classification) ||
		classification != visibility->classification_count)
		return 0;
	for (index = 0U; index < visibility->partition_count; index++)
	{
		const sg_static_visibility_partition_t *partition =
			&visibility->partitions[index];

		if (partition->configuration_region != index ||
			partition->bsp_area >= visibility->area_count)
			return 0;
	}
	for (index = 0U; index < visibility->area_count; index++)
		if (visibility->area_components[index] >= visibility->area_count)
			return 0;
	for (classification = 0U; classification <
		visibility->classification_count; classification++)
		if (!VisibilityClassificationValid(visibility, classification))
			return 0;
	return 1;
}

static sg_weapon_static_relation_t AllowedWeaponRelations(
	const sg_weapon_profile_t *profile)
{
	sg_weapon_static_relation_t allowed = 0U;

	if ((profile->effects & SG_WEAPON_EFFECT_HITSCAN) != 0U)
		allowed |= SG_WEAPON_STATIC_DIRECT_VISIBILITY;
	if ((profile->effects & SG_WEAPON_EFFECT_PROJECTILE) != 0U)
		allowed |= SG_WEAPON_STATIC_PROJECTILE_CORRIDOR;
	if (profile->supports_occluded_impact != 0U)
		allowed |= SG_WEAPON_STATIC_IMPACT_SURFACE;
	if ((profile->effects & SG_WEAPON_EFFECT_SPLASH) != 0U)
		allowed |= SG_WEAPON_STATIC_BLAST_REACH;
	if ((profile->effects & SG_WEAPON_EFFECT_SECONDARY_AREA) != 0U)
		allowed |= SG_WEAPON_STATIC_SECONDARY_BLAST_REACH;
	if ((profile->effects & SG_WEAPON_EFFECT_BOUNCE) != 0U)
		allowed |= SG_WEAPON_STATIC_BOUNCE_SURFACE;
	if ((profile->effects & SG_WEAPON_EFFECT_PERIODIC_RAY) != 0U)
		allowed |= SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY;
	return allowed;
}

static int WeaponStatusValid(sg_weapon_static_status_t status)
{
	return status >= SG_WEAPON_STATIC_NOT_REQUESTED &&
		status <= SG_WEAPON_STATIC_PROVEN;
}

static int WeaponReasonValid(sg_weapon_static_reason_t reason)
{
	return reason >= SG_WEAPON_STATIC_REASON_NONE &&
		reason <= SG_WEAPON_STATIC_REASON_OWNER_DAMAGE_VISIBILITY;
}

static int WeaponRelationResultValid(
	const sg_weapon_static_relation_result_t *relation,
	sg_weapon_static_relation_t expected_relation, int requested,
	int allowed, sg_weapon_static_relation_t *proven_out,
	sg_weapon_static_relation_t *rejected_out,
	sg_weapon_static_relation_t *conditional_out)
{
	if (!relation || !proven_out || !rejected_out || !conditional_out ||
		relation->relation != expected_relation ||
		relation->has_witness_point > 1U ||
		relation->reserved[0] != 0U || relation->reserved[1] != 0U ||
		relation->reserved[2] != 0U || !WeaponStatusValid(relation->status) ||
		!WeaponReasonValid(relation->reason) ||
		!VisibilityClassValid(relation->visibility.classification) ||
		!VisibilityReasonValid(relation->visibility.reason))
		return 0;
	if (relation->has_witness_point != 0U &&
		(!isfinite(relation->witness_point.value[0]) ||
		 !isfinite(relation->witness_point.value[1]) ||
		 !isfinite(relation->witness_point.value[2])))
		return 0;
	if (!requested)
		return relation->status == SG_WEAPON_STATIC_NOT_REQUESTED &&
			relation->reason == SG_WEAPON_STATIC_REASON_NONE &&
			relation->has_witness_point == 0U;
	if (relation->status == SG_WEAPON_STATIC_NOT_REQUESTED ||
		relation->reason == SG_WEAPON_STATIC_REASON_NONE)
		return 0;
	if (!allowed && (relation->status != SG_WEAPON_STATIC_REJECTED ||
		relation->reason != SG_WEAPON_STATIC_REASON_PROFILE_UNSUPPORTED))
		return 0;
	if (expected_relation == SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY &&
		allowed && (relation->status != SG_WEAPON_STATIC_CONDITIONAL ||
			relation->reason !=
				SG_WEAPON_STATIC_REASON_RUNTIME_PROJECTILE_ORIGIN))
		return 0;
	switch (relation->status)
	{
	case SG_WEAPON_STATIC_PROVEN:
		*proven_out |= expected_relation;
		break;
	case SG_WEAPON_STATIC_REJECTED:
		*rejected_out |= expected_relation;
		break;
	case SG_WEAPON_STATIC_CONDITIONAL:
		*conditional_out |= expected_relation;
		break;
	case SG_WEAPON_STATIC_NOT_REQUESTED:
		return 0;
	}
	return 1;
}

static int WeaponEvidenceRecordValid(
	const sg_static_affordance_catalog_weapon_evidence_t *weapon,
	const sg_weapon_static_binding_t *binding,
	const sg_rune_model_identity_t *identity)
{
	sg_weapon_profile_t resolved;
	sg_weapon_static_relation_t proven = 0U, rejected = 0U, conditional = 0U;
	sg_weapon_static_relation_t allowed;
	uint32_t relation;

	if (!weapon || !binding || !identity ||
		!SG_WeaponStaticBindingValid(binding) ||
		!SG_WeaponProfileResolve(weapon->profile.id, &weapon->law, &resolved) ||
		memcmp(&resolved, &weapon->profile, sizeof(resolved)) != 0 ||
		!SG_WeaponProfileValid(&weapon->profile) ||
		weapon->law.build_identity != identity->producer_identity ||
		weapon->law.physics_abi_id != identity->physics_abi_id ||
		!BindingEqual(&weapon->affordance.binding, binding) ||
		weapon->affordance.profile_id != weapon->profile.id ||
		weapon->affordance.family != weapon->profile.family ||
		weapon->affordance.exact_authenticated_live_prefire_trace_required != 1U ||
		weapon->affordance.reserved[0] != 0U ||
		weapon->affordance.reserved[1] != 0U ||
		weapon->affordance.reserved[2] != 0U ||
		weapon->affordance.requested_relations == 0U ||
		(weapon->affordance.requested_relations &
			~(uint32_t)SG_WEAPON_STATIC_RELATION_MASK) != 0U)
		return 0;
	allowed = AllowedWeaponRelations(&weapon->profile);
	if (weapon->affordance.allowed_relations != allowed ||
		(weapon->affordance.proven_relations &
			~(uint32_t)SG_WEAPON_STATIC_RELATION_MASK) != 0U ||
		(weapon->affordance.rejected_relations &
			~(uint32_t)SG_WEAPON_STATIC_RELATION_MASK) != 0U ||
		(weapon->affordance.conditional_relations &
			~(uint32_t)SG_WEAPON_STATIC_RELATION_MASK) != 0U)
		return 0;
	for (relation = 0U; relation < SG_WEAPON_STATIC_RELATION_COUNT;
		relation++)
	{
		const sg_weapon_static_relation_t requested =
			weapon->affordance.requested_relations &
			weapon_relation_order[relation];
		const sg_weapon_static_relation_t relation_allowed = allowed &
			weapon_relation_order[relation];

		if (!WeaponRelationResultValid(&weapon->affordance.relations[relation],
			weapon_relation_order[relation], requested != 0U,
			relation_allowed != 0U, &proven, &rejected, &conditional))
			return 0;
	}
	return weapon->affordance.proven_relations == proven &&
		weapon->affordance.rejected_relations == rejected &&
		weapon->affordance.conditional_relations == conditional &&
		(proven & rejected) == 0U && (proven & conditional) == 0U &&
		(rejected & conditional) == 0U;
}

static int WeaponEvidenceValid(
	const sg_static_affordance_catalog_weapon_evidence_t *weapons,
	uint32_t weapon_count, const sg_weapon_static_binding_t *binding,
	const sg_rune_model_identity_t *identity)
{
	uint32_t index;

	if (!weapons || weapon_count == 0U)
		return 0;
	for (index = 0U; index < weapon_count; index++)
		if (!WeaponEvidenceRecordValid(&weapons[index], binding, identity))
			return 0;
	return 1;
}

static int HookDomainValid(const sg_hook_visibility_domain_term_t *domain)
{
	uint32_t axis;

	if (!domain || domain->pitch_min > domain->pitch_max ||
		domain->yaw_min > domain->yaw_max || domain->hand_mask == 0U ||
		(domain->hand_mask & ~(uint32_t)SG_HOOK_VISIBILITY_ALL_HANDS) != 0U)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (domain->origins.mins[axis] > domain->origins.maxs[axis])
			return 0;
	return 1;
}

static int HookControlValid(const sg_hook_visibility_control_root_t *control)
{
	return control && control->pitch_min <= control->pitch_max &&
		control->yaw_min <= control->yaw_max;
}

static int HookSurfaceRuleValid(const sg_hook_visibility_surface_rule_t *rule)
{
	return rule && rule->surface_id != 0U &&
		rule->classification >= SG_HOOK_VISIBILITY_SURFACE_HOOKABLE &&
		rule->classification <= SG_HOOK_VISIBILITY_SURFACE_SKY;
}

static int HookTerminalValid(
	const sg_static_affordance_catalog_hook_evidence_t *hook,
	const sg_static_affordance_catalog_hook_terminal_t *terminal)
{
	const uint32_t allowed_flags = SG_HOOK_VISIBILITY_CATALOG_LOWER_DIMENSIONAL |
		SG_HOOK_VISIBILITY_CATALOG_EDGE | SG_HOOK_VISIBILITY_CATALOG_VERTEX |
		SG_HOOK_VISIBILITY_CATALOG_TIE;
	const sg_hook_visibility_surface_rule_t *rule;

	if (!hook || !terminal || !HookDomainValid(&terminal->domain) ||
		terminal->outcome < SG_HOOK_VISIBILITY_CATALOG_HOOKABLE ||
		terminal->outcome > SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED ||
		(terminal->flags & ~allowed_flags) != 0U)
		return 0;
	if (terminal->outcome == SG_HOOK_VISIBILITY_CATALOG_NO_HIT ||
		terminal->outcome == SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED)
		return terminal->surface_rule_index ==
			SG_STATIC_AFFORDANCE_CATALOG_INDEX_NONE;
	if (terminal->surface_rule_index >= hook->surface_rule_count)
		return 0;
	rule = &hook->surface_rules[terminal->surface_rule_index];
	if (!HookSurfaceRuleValid(rule))
		return 0;
	return (terminal->outcome == SG_HOOK_VISIBILITY_CATALOG_HOOKABLE &&
		rule->classification == SG_HOOK_VISIBILITY_SURFACE_HOOKABLE) ||
		(terminal->outcome == SG_HOOK_VISIBILITY_CATALOG_SKY &&
			rule->classification == SG_HOOK_VISIBILITY_SURFACE_SKY) ||
		(terminal->outcome == SG_HOOK_VISIBILITY_CATALOG_NONHOOKABLE &&
			rule->classification == SG_HOOK_VISIBILITY_SURFACE_NONHOOKABLE);
}

static int HookEvidenceValid(
	const sg_static_affordance_catalog_hook_evidence_t *hook)
{
	uint32_t outcomes[SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED + 1U];
	uint32_t lower_dimensional = 0U, edge = 0U, vertex = 0U, tie = 0U;
	uint32_t terminal, relation, domain;
	uint32_t expected_first = 0U;

	if (!hook || hook->acceptance.code !=
			SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OK ||
		hook->producer_identity == 0U || hook->verifier_identity == 0U ||
		hook->producer_identity == hook->verifier_identity ||
		hook->acceptance.producer_identity != hook->producer_identity ||
		hook->acceptance.verifier_identity != hook->verifier_identity ||
		hook->metrics.relation_count != hook->relation_count ||
		hook->metrics.legal_action_tuples !=
			hook->acceptance.reconstructed_action_tuples ||
		hook->metrics.predicate_domains !=
			hook->acceptance.reconstructed_predicate_domains ||
		hook->acceptance.clearance_blocked_terms > hook->terminal_count ||
		hook->acceptance.hookable_terms > hook->terminal_count ||
		(hook->control_count != 0U && !hook->controls) ||
		(hook->surface_rule_count != 0U && !hook->surface_rules) ||
		(hook->terminal_count != 0U && !hook->terminals) ||
		(hook->relation_count != 0U && !hook->relations) ||
		(hook->relation_domain_count != 0U && !hook->relation_domains))
		return 0;
	for (terminal = 0U; terminal < hook->control_count; terminal++)
		if (!HookControlValid(&hook->controls[terminal]))
			return 0;
	for (terminal = 0U; terminal < hook->surface_rule_count; terminal++)
		if (!HookSurfaceRuleValid(&hook->surface_rules[terminal]))
			return 0;
	memset(outcomes, 0, sizeof(outcomes));
	for (terminal = 0U; terminal < hook->terminal_count; terminal++)
	{
		const sg_static_affordance_catalog_hook_terminal_t *record =
			&hook->terminals[terminal];

		if (!HookTerminalValid(hook, record) ||
			outcomes[record->outcome] == UINT32_MAX ||
			((record->flags & SG_HOOK_VISIBILITY_CATALOG_LOWER_DIMENSIONAL) !=
				0U && lower_dimensional == UINT32_MAX) ||
			((record->flags & SG_HOOK_VISIBILITY_CATALOG_EDGE) != 0U &&
				edge == UINT32_MAX) ||
			((record->flags & SG_HOOK_VISIBILITY_CATALOG_VERTEX) != 0U &&
				vertex == UINT32_MAX) ||
			((record->flags & SG_HOOK_VISIBILITY_CATALOG_TIE) != 0U &&
				tie == UINT32_MAX))
			return 0;
		outcomes[record->outcome]++;
		if ((record->flags & SG_HOOK_VISIBILITY_CATALOG_LOWER_DIMENSIONAL) != 0U)
			lower_dimensional++;
		if ((record->flags & SG_HOOK_VISIBILITY_CATALOG_EDGE) != 0U)
			edge++;
		if ((record->flags & SG_HOOK_VISIBILITY_CATALOG_VERTEX) != 0U)
			vertex++;
		if ((record->flags & SG_HOOK_VISIBILITY_CATALOG_TIE) != 0U)
			tie++;
	}
	for (relation = 0U; relation < hook->relation_count; relation++)
	{
		const sg_static_affordance_catalog_hook_relation_t *record =
			&hook->relations[relation];
		const sg_hook_visibility_surface_rule_t *rule;

		if (record->surface_rule_index >= hook->surface_rule_count ||
			record->domain_count == 0U ||
			record->first_domain != expected_first ||
			record->domain_count > hook->relation_domain_count - expected_first)
			return 0;
		rule = &hook->surface_rules[record->surface_rule_index];
		if (!HookSurfaceRuleValid(rule) ||
			rule->classification != SG_HOOK_VISIBILITY_SURFACE_HOOKABLE ||
			record->surface_id != rule->surface_id ||
			record->model_index != rule->model_index ||
			record->texinfo != rule->texinfo)
			return 0;
		for (domain = 0U; domain < record->domain_count; domain++)
			if (!HookDomainValid(&hook->relation_domains[
				record->first_domain + domain]))
				return 0;
		expected_first += record->domain_count;
	}
	if (expected_first != hook->relation_domain_count ||
		hook->metrics.relation_term_count != hook->relation_domain_count ||
		hook->metrics.muzzle_clearance_traces != hook->terminal_count ||
		hook->metrics.first_hit_traces != hook->terminal_count -
			hook->acceptance.clearance_blocked_terms ||
		hook->metrics.complement_term_count != hook->terminal_count -
			hook->acceptance.hookable_terms)
		return 0;
	return outcomes[SG_HOOK_VISIBILITY_CATALOG_HOOKABLE] ==
			hook->acceptance.hookable_terms &&
		outcomes[SG_HOOK_VISIBILITY_CATALOG_SKY] == hook->acceptance.sky_terms &&
		outcomes[SG_HOOK_VISIBILITY_CATALOG_NONHOOKABLE] ==
			hook->acceptance.nonhookable_terms &&
		outcomes[SG_HOOK_VISIBILITY_CATALOG_NO_HIT] ==
			hook->acceptance.no_hit_terms &&
		outcomes[SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED] ==
			hook->acceptance.clearance_blocked_terms &&
		lower_dimensional == hook->acceptance.lower_dimensional_terms &&
		edge == hook->acceptance.edge_terms &&
		vertex == hook->acceptance.vertex_terms &&
		tie == hook->acceptance.tie_terms;
}

static uint64_t HashBytes(uint64_t hash, const void *bytes, size_t size)
{
	const unsigned char *cursor = bytes;
	size_t index;

	for (index = 0U; index < size; index++)
	{
		hash ^= cursor[index];
		hash *= SG_STATIC_AFFORDANCE_CATALOG_FNV_PRIME;
	}
	return hash;
}

static uint64_t EvidenceDigest(
	const sg_static_affordance_catalog_evidence_view_t *evidence)
{
	uint64_t hash = SG_STATIC_AFFORDANCE_CATALOG_FNV_OFFSET;

	hash = HashBytes(hash, &evidence->coverage, sizeof(evidence->coverage));
	hash = HashBytes(hash, &evidence->authority_count,
		sizeof(evidence->authority_count));
	hash = HashBytes(hash, &evidence->static_visibility.identity,
		sizeof(evidence->static_visibility.identity));
	hash = HashBytes(hash, &evidence->static_visibility.revision,
		sizeof(evidence->static_visibility.revision));
	hash = HashBytes(hash, &evidence->static_visibility.partition_count,
		sizeof(evidence->static_visibility.partition_count));
	hash = HashBytes(hash, &evidence->static_visibility.area_count,
		sizeof(evidence->static_visibility.area_count));
	hash = HashBytes(hash, &evidence->static_visibility.occluder_count,
		sizeof(evidence->static_visibility.occluder_count));
	hash = HashBytes(hash, &evidence->static_visibility.surface_count,
		sizeof(evidence->static_visibility.surface_count));
	hash = HashBytes(hash, &evidence->static_visibility.classification_count,
		sizeof(evidence->static_visibility.classification_count));
	hash = HashBytes(hash, evidence->static_visibility.partitions,
		(size_t)evidence->static_visibility.partition_count *
			sizeof(*evidence->static_visibility.partitions));
	hash = HashBytes(hash, evidence->static_visibility.area_components,
		(size_t)evidence->static_visibility.area_count *
			sizeof(*evidence->static_visibility.area_components));
	hash = HashBytes(hash, evidence->static_visibility.occluders,
		(size_t)evidence->static_visibility.occluder_count *
			sizeof(*evidence->static_visibility.occluders));
	hash = HashBytes(hash, evidence->static_visibility.surfaces,
		(size_t)evidence->static_visibility.surface_count *
			sizeof(*evidence->static_visibility.surfaces));
	hash = HashBytes(hash, evidence->static_visibility.classifications,
		(size_t)evidence->static_visibility.classification_count *
			sizeof(*evidence->static_visibility.classifications));
	hash = HashBytes(hash, &evidence->weapon_binding,
		sizeof(evidence->weapon_binding));
	hash = HashBytes(hash, &evidence->weapon_count,
		sizeof(evidence->weapon_count));
	hash = HashBytes(hash, evidence->weapons,
		(size_t)evidence->weapon_count * sizeof(*evidence->weapons));
	hash = HashBytes(hash, &evidence->hook.source_digest,
		sizeof(evidence->hook.source_digest));
	hash = HashBytes(hash, &evidence->hook.verifier_source_digest,
		sizeof(evidence->hook.verifier_source_digest));
	hash = HashBytes(hash, &evidence->hook.producer_identity,
		sizeof(evidence->hook.producer_identity));
	hash = HashBytes(hash, &evidence->hook.verifier_identity,
		sizeof(evidence->hook.verifier_identity));
	hash = HashBytes(hash, &evidence->hook.collision_identity,
		sizeof(evidence->hook.collision_identity));
	hash = HashBytes(hash, evidence->hook.world_counts,
		sizeof(evidence->hook.world_counts));
	hash = HashBytes(hash, &evidence->hook.origins,
		sizeof(evidence->hook.origins));
	hash = HashBytes(hash, &evidence->hook.stance, sizeof(evidence->hook.stance));
	hash = HashBytes(hash, &evidence->hook.fire_law,
		sizeof(evidence->hook.fire_law));
	hash = HashBytes(hash, &evidence->hook.control_count,
		sizeof(evidence->hook.control_count));
	hash = HashBytes(hash, &evidence->hook.surface_rule_count,
		sizeof(evidence->hook.surface_rule_count));
	hash = HashBytes(hash, &evidence->hook.terminal_count,
		sizeof(evidence->hook.terminal_count));
	hash = HashBytes(hash, &evidence->hook.relation_count,
		sizeof(evidence->hook.relation_count));
	hash = HashBytes(hash, &evidence->hook.relation_domain_count,
		sizeof(evidence->hook.relation_domain_count));
	hash = HashBytes(hash, evidence->hook.controls,
		(size_t)evidence->hook.control_count * sizeof(*evidence->hook.controls));
	hash = HashBytes(hash, evidence->hook.surface_rules,
		(size_t)evidence->hook.surface_rule_count *
			sizeof(*evidence->hook.surface_rules));
	hash = HashBytes(hash, evidence->hook.terminals,
		(size_t)evidence->hook.terminal_count * sizeof(*evidence->hook.terminals));
	hash = HashBytes(hash, evidence->hook.relations,
		(size_t)evidence->hook.relation_count * sizeof(*evidence->hook.relations));
	hash = HashBytes(hash, evidence->hook.relation_domains,
		(size_t)evidence->hook.relation_domain_count *
			sizeof(*evidence->hook.relation_domains));
	hash = HashBytes(hash, &evidence->hook.metrics,
		sizeof(evidence->hook.metrics));
	hash = HashBytes(hash, &evidence->hook.acceptance,
		sizeof(evidence->hook.acceptance));
	return hash;
}

static int CopyStaticVisibilityEvidence(
	sg_static_affordance_catalog_t *catalog,
	const sg_static_affordance_catalog_layout_t *layout,
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint64_t revision)
{
	sg_static_affordance_catalog_static_visibility_evidence_t *target =
		&catalog->evidence.static_visibility;
	sg_static_visibility_partition_t *partitions = CatalogAt(catalog,
		layout->partitions);
	uint32_t *area_components = CatalogAt(catalog, layout->area_components);
	sg_static_visibility_occluder_t *occluders = CatalogAt(catalog,
		layout->occluders);
	sg_static_visibility_surface_t *surfaces = CatalogAt(catalog,
		layout->surfaces);
	sg_static_affordance_catalog_visibility_classification_t *classifications =
		CatalogAt(catalog, layout->classifications);
	uint32_t source, destination;

	memset(target, 0, sizeof(*target));
	target->identity = authority->identity;
	target->revision = revision;
	target->partitions = partitions;
	target->partition_count = visibility->partition_count;
	target->area_components = area_components;
	target->area_count = visibility->area_count;
	target->occluders = occluders;
	target->occluder_count = visibility->occluder_count;
	target->surfaces = surfaces;
	target->surface_count = visibility->surface_count;
	target->classifications = classifications;
	target->classification_count = layout->classification_count;
	if (visibility->partition_count != 0U)
		memcpy(partitions, visibility->partitions,
			(size_t)visibility->partition_count * sizeof(*partitions));
	if (visibility->area_count != 0U)
		memcpy(area_components, visibility->area_components,
			(size_t)visibility->area_count * sizeof(*area_components));
	if (visibility->occluder_count != 0U)
		memcpy(occluders, visibility->occluders,
			(size_t)visibility->occluder_count * sizeof(*occluders));
	if (visibility->surface_count != 0U)
		memcpy(surfaces, visibility->surfaces,
			(size_t)visibility->surface_count * sizeof(*surfaces));
	for (source = 0U; source < visibility->partition_count; source++)
		for (destination = 0U; destination < visibility->partition_count;
			destination++)
		{
			const uint64_t index = (uint64_t)source *
				(uint64_t)visibility->partition_count + destination;
			sg_static_visibility_error_t error;
			sg_static_visibility_result_t result;

			if (!SG_StaticVisibilityQueryRegions(authority, configuration,
				semantics, visibility, source, destination,
				&result, &error))
				return 0;
			classifications[index].classification =
				(uint8_t)result.classification;
			classifications[index].reason = (uint8_t)result.reason;
			classifications[index].requires_exact_ray =
				(uint8_t)result.requires_exact_ray;
			classifications[index].requires_area_state =
				(uint8_t)result.requires_area_state;
		}
	return StaticVisibilityEvidenceValid(target);
}

static int CopyWeaponEvidence(sg_static_affordance_catalog_t *catalog,
	const sg_static_affordance_catalog_layout_t *layout,
	const sg_static_affordance_catalog_weapon_evidence_t *weapons,
	uint32_t weapon_count)
{
	sg_static_affordance_catalog_weapon_evidence_t *target = CatalogAt(catalog,
		layout->weapons);

	catalog->evidence.weapons = target;
	catalog->evidence.weapon_count = weapon_count;
	memcpy(target, weapons, (size_t)weapon_count * sizeof(*target));
	return 1;
}

static int CopyHookEvidence(sg_static_affordance_catalog_t *catalog,
	const sg_static_affordance_catalog_layout_t *layout,
	const sg_hook_visibility_catalog_t *source_catalog,
	const sg_static_affordance_catalog_hook_source_t *source)
{
	sg_static_affordance_catalog_hook_evidence_t *target =
		&catalog->evidence.hook;
	sg_hook_visibility_control_root_t *controls = CatalogAt(catalog,
		layout->controls);
	sg_hook_visibility_surface_rule_t *surface_rules = CatalogAt(catalog,
		layout->surface_rules);
	sg_static_affordance_catalog_hook_terminal_t *terminals = CatalogAt(catalog,
		layout->terminals);
	sg_static_affordance_catalog_hook_relation_t *relations = CatalogAt(catalog,
		layout->relations);
	sg_hook_visibility_domain_term_t *relation_domains = CatalogAt(catalog,
		layout->relation_domains);
	uint32_t terminal, relation, first_domain = 0U;

	if (!catalog || !layout || !source_catalog || !source)
		return 0;
	memset(target, 0, sizeof(*target));
	target->source_digest = source->evidence.source_digest;
	target->verifier_source_digest = source->evidence.verifier_source_digest;
	target->producer_identity = source->evidence.producer_identity;
	target->verifier_identity = source->evidence.verifier_identity;
	target->collision_identity = source->evidence.collision_identity;
	memcpy(target->world_counts, source->evidence.world_counts,
		sizeof(target->world_counts));
	target->origins = source->evidence.origins;
	target->stance = source->evidence.stance;
	target->fire_law = source->evidence.fire_law;
	target->controls = controls;
	target->control_count = source->evidence.control_count;
	target->surface_rules = surface_rules;
	target->surface_rule_count = source->evidence.surface_rule_count;
	target->terminals = terminals;
	target->terminal_count = source->terminal_count;
	target->relations = relations;
	target->relation_count = source->relation_count;
	target->relation_domains = relation_domains;
	target->relation_domain_count = source->relation_domain_count;
	target->metrics = source->evidence.metrics;
	target->acceptance = source->evidence.acceptance;
	if (target->control_count != 0U)
		memcpy(controls, source->evidence.controls,
			(size_t)target->control_count * sizeof(*controls));
	if (target->surface_rule_count != 0U)
		memcpy(surface_rules, source->evidence.surface_rules,
			(size_t)target->surface_rule_count * sizeof(*surface_rules));
	for (terminal = 0U; terminal < target->terminal_count; terminal++)
	{
		sg_hook_visibility_catalog_terminal_view_t view;

		if (!SG_HookVisibilityCatalogTerminal(source_catalog, terminal, &view))
			return 0;
		terminals[terminal].domain = view.domain;
		terminals[terminal].outcome = view.outcome;
		terminals[terminal].flags = view.flags;
		terminals[terminal].surface_rule_index = view.surface_rule_index;
	}
	for (relation = 0U; relation < target->relation_count; relation++)
	{
		sg_hook_visibility_catalog_relation_view_t view;
		sg_static_affordance_catalog_hook_relation_t *record =
			&relations[relation];

		if (!SG_HookVisibilityCatalogRelation(source_catalog, relation, &view) ||
			!view.surface_rule ||
			view.domain_count > target->relation_domain_count - first_domain)
			return 0;
		record->surface_id = view.surface_rule->surface_id;
		record->model_index = view.surface_rule->model_index;
		record->texinfo = view.surface_rule->texinfo;
		record->surface_rule_index = view.surface_rule_index;
		record->first_domain = first_domain;
		record->domain_count = view.domain_count;
		if (view.domain_count != 0U)
			memcpy(relation_domains + first_domain, view.domains,
				(size_t)view.domain_count * sizeof(*relation_domains));
		first_domain += view.domain_count;
	}
	return first_domain == target->relation_domain_count &&
		HookEvidenceValid(target);
}

int SG_StaticAffordanceCatalogIssue(
	const sg_static_affordance_catalog_input_t *input,
	sg_static_affordance_catalog_t **catalog_out,
	sg_static_affordance_catalog_error_t *error_out)
{
	sg_static_affordance_catalog_audit_report_t copied;
	sg_static_affordance_catalog_hook_source_t hook_source;
	sg_static_affordance_catalog_layout_t layout;
	sg_static_affordance_catalog_t *catalog;
	sg_weapon_static_binding_t weapon_binding;
	const sg_static_visibility_publication_t *weapon_publication;
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_static_visibility_t *visibility;
	uint64_t revision;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!input || !catalog_out || *catalog_out || !input->static_visibility ||
		!input->weapon_context || !input->weapons || input->weapon_count == 0U ||
		!input->hook_catalog)
	{
		SetError(error_out, SG_STATIC_AFFORDANCE_CATALOG_ERROR_INVALID_ARGUMENT,
			SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY);
		return 0;
	}
	if (!SG_StaticVisibilityPublicationRead(input->static_visibility,
			&authority, &configuration, &semantics, &visibility, &revision) ||
		revision == 0U || !PublicationSourcesAccepted(authority, configuration,
			semantics, visibility))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_STATIC_VISIBILITY_REJECTED,
			SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY);
		return 0;
	}
	if (!SG_WeaponStaticContextSource(input->weapon_context, &weapon_binding,
			&weapon_publication) || !SG_WeaponStaticBindingValid(&weapon_binding))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_CONTEXT_REJECTED,
			SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE);
		return 0;
	}
	if (weapon_publication != input->static_visibility ||
		weapon_binding.source_set_identity !=
			authority->identity.source_set_identity ||
		weapon_binding.visibility_revision != revision)
	{
		SetError(error_out, SG_STATIC_AFFORDANCE_CATALOG_ERROR_SOURCE_MISMATCH,
			SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE);
		return 0;
	}
	if (!WeaponEvidenceValid(input->weapons, input->weapon_count,
		&weapon_binding, &authority->identity))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_EVIDENCE_REJECTED,
			SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE);
		return 0;
	}
	if (!HookSourceRead(input->hook_catalog, &hook_source))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_HOOK_CATALOG_REJECTED,
			SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY);
		return 0;
	}
	if (!IdentityEqual(&authority->identity,
		&hook_source.evidence.collision_identity))
	{
		SetError(error_out, SG_STATIC_AFFORDANCE_CATALOG_ERROR_SOURCE_MISMATCH,
			SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY);
		return 0;
	}
	if (!BuildLayout(visibility->partition_count, visibility->area_count,
		visibility->occluder_count, visibility->surface_count,
		input->weapon_count, hook_source.evidence.control_count,
		hook_source.evidence.surface_rule_count, hook_source.terminal_count,
		hook_source.relation_count, hook_source.relation_domain_count, &layout))
	{
		SetError(error_out, SG_STATIC_AFFORDANCE_CATALOG_ERROR_OVERFLOW,
			SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY);
		return 0;
	}
	catalog = calloc(1U, layout.allocation_size);
	if (!catalog)
	{
		SetError(error_out, SG_STATIC_AFFORDANCE_CATALOG_ERROR_OUT_OF_MEMORY,
			SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY);
		return 0;
	}
	catalog->magic = SG_STATIC_AFFORDANCE_CATALOG_MAGIC;
	catalog->magic_inverse = ~SG_STATIC_AFFORDANCE_CATALOG_MAGIC;
	catalog->self = catalog;
	catalog->allocation_size = layout.allocation_size;
	catalog->allocation_size_inverse = ~layout.allocation_size;
	catalog->evidence.coverage = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_ONLY;
	catalog->evidence.authority_count =
		SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT;
	catalog->evidence.weapon_binding = weapon_binding;
	if (!CopyStaticVisibilityEvidence(catalog, &layout, authority, configuration,
			semantics, visibility, revision))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_STATIC_VISIBILITY_REJECTED,
			SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY);
		SG_StaticAffordanceCatalogDestroy(catalog);
		return 0;
	}
	CopyWeaponEvidence(catalog, &layout, input->weapons, input->weapon_count);
	if (!CopyHookEvidence(catalog, &layout, input->hook_catalog, &hook_source))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_HOOK_CATALOG_REJECTED,
			SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY);
		SG_StaticAffordanceCatalogDestroy(catalog);
		return 0;
	}
	catalog->evidence.content_digest = EvidenceDigest(&catalog->evidence);
	if (!SG_StaticAffordanceCatalogAudit(catalog, &copied))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_COPY_DISAGREEMENT,
			copied.authority);
		SG_StaticAffordanceCatalogDestroy(catalog);
		return 0;
	}
	*catalog_out = catalog;
	return 1;
}

int SG_StaticAffordanceCatalogAudit(
	const sg_static_affordance_catalog_t *catalog,
	sg_static_affordance_catalog_audit_report_t *report_out)
{
	sg_static_affordance_catalog_audit_report_t report;
	const sg_static_affordance_catalog_evidence_view_t *evidence;

	memset(&report, 0, sizeof(report));
	report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_INVALID_ARGUMENT;
	if (!catalog || !report_out)
	{
		if (report_out)
			*report_out = report;
		return 0;
	}
	if (!CatalogStorageValid(catalog))
	{
		report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_STORAGE_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	evidence = &catalog->evidence;
	if (evidence->coverage != SG_STATIC_AFFORDANCE_CATALOG_AUDIT_ONLY ||
		evidence->authority_count !=
			SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT)
	{
		report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_COVERAGE_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	if (!StaticVisibilityEvidenceValid(&evidence->static_visibility))
	{
		report.code =
			SG_STATIC_AFFORDANCE_CATALOG_AUDIT_STATIC_VISIBILITY_DISAGREEMENT;
		report.authority = SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY;
		*report_out = report;
		return 0;
	}
	if (!SG_WeaponStaticBindingValid(&evidence->weapon_binding) ||
		evidence->weapon_binding.source_set_identity !=
			evidence->static_visibility.identity.source_set_identity ||
		evidence->weapon_binding.visibility_revision !=
			evidence->static_visibility.revision)
	{
		report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_SOURCE_MISMATCH;
		report.authority = SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE;
		*report_out = report;
		return 0;
	}
	if (!WeaponEvidenceValid(evidence->weapons, evidence->weapon_count,
		&evidence->weapon_binding, &evidence->static_visibility.identity))
	{
		report.code =
			SG_STATIC_AFFORDANCE_CATALOG_AUDIT_WEAPON_EVIDENCE_DISAGREEMENT;
		report.authority = SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE;
		*report_out = report;
		return 0;
	}
	if (!IdentityEqual(&evidence->static_visibility.identity,
		&evidence->hook.collision_identity))
	{
		report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_SOURCE_MISMATCH;
		report.authority = SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY;
		*report_out = report;
		return 0;
	}
	if (!HookEvidenceValid(&evidence->hook))
	{
		report.code =
			SG_STATIC_AFFORDANCE_CATALOG_AUDIT_HOOK_EVIDENCE_DISAGREEMENT;
		report.authority = SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY;
		*report_out = report;
		return 0;
	}
	if (evidence->content_digest != EvidenceDigest(evidence))
	{
		report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_DIGEST_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_OK;
	*report_out = report;
	return 1;
}

int SG_StaticAffordanceCatalogEvidence(
	const sg_static_affordance_catalog_t *catalog,
	sg_static_affordance_catalog_evidence_view_t *evidence_out)
{
	sg_static_affordance_catalog_audit_report_t audit;

	if (!evidence_out || !SG_StaticAffordanceCatalogAudit(catalog, &audit))
		return 0;
	*evidence_out = catalog->evidence;
	return 1;
}

uint32_t SG_StaticAffordanceCatalogAuthorityCount(
	const sg_static_affordance_catalog_t *catalog)
{
	return CatalogHeaderValid(catalog) ?
		SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT : 0U;
}

int SG_StaticAffordanceCatalogAuthority(
	const sg_static_affordance_catalog_t *catalog, uint32_t index,
	sg_static_affordance_catalog_authority_t *authority_out)
{
	if (!CatalogHeaderValid(catalog) || !authority_out ||
		index >= SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT)
		return 0;
	*authority_out = authority_order[index];
	return 1;
}

uint64_t SG_StaticAffordanceCatalogVisibilityClassificationCount(
	const sg_static_affordance_catalog_t *catalog)
{
	return CatalogHeaderValid(catalog) ?
		catalog->evidence.static_visibility.classification_count : 0U;
}

int SG_StaticAffordanceCatalogVisibilityClassification(
	const sg_static_affordance_catalog_t *catalog, uint64_t index,
	sg_static_affordance_catalog_visibility_classification_t
		*classification_out)
{
	if (!CatalogHeaderValid(catalog) || !classification_out ||
		index >= catalog->evidence.static_visibility.classification_count)
		return 0;
	*classification_out =
		catalog->evidence.static_visibility.classifications[index];
	return 1;
}

uint32_t SG_StaticAffordanceCatalogWeaponCount(
	const sg_static_affordance_catalog_t *catalog)
{
	return CatalogHeaderValid(catalog) ? catalog->evidence.weapon_count : 0U;
}

int SG_StaticAffordanceCatalogWeapon(
	const sg_static_affordance_catalog_t *catalog, uint32_t index,
	sg_static_affordance_catalog_weapon_evidence_t *weapon_out)
{
	if (!CatalogHeaderValid(catalog) || !weapon_out ||
		index >= catalog->evidence.weapon_count)
		return 0;
	*weapon_out = catalog->evidence.weapons[index];
	return 1;
}

uint32_t SG_StaticAffordanceCatalogHookTerminalCount(
	const sg_static_affordance_catalog_t *catalog)
{
	return CatalogHeaderValid(catalog) ?
		catalog->evidence.hook.terminal_count : 0U;
}

int SG_StaticAffordanceCatalogHookTerminal(
	const sg_static_affordance_catalog_t *catalog, uint32_t index,
	sg_static_affordance_catalog_hook_terminal_t *terminal_out)
{
	if (!CatalogHeaderValid(catalog) || !terminal_out ||
		index >= catalog->evidence.hook.terminal_count)
		return 0;
	*terminal_out = catalog->evidence.hook.terminals[index];
	return 1;
}

uint32_t SG_StaticAffordanceCatalogHookRelationCount(
	const sg_static_affordance_catalog_t *catalog)
{
	return CatalogHeaderValid(catalog) ?
		catalog->evidence.hook.relation_count : 0U;
}

int SG_StaticAffordanceCatalogHookRelation(
	const sg_static_affordance_catalog_t *catalog, uint32_t index,
	sg_static_affordance_catalog_hook_relation_t *relation_out,
	const sg_hook_visibility_domain_term_t **domains_out)
{
	const sg_static_affordance_catalog_hook_relation_t *relation;

	if (!CatalogHeaderValid(catalog) || !relation_out || !domains_out ||
		index >= catalog->evidence.hook.relation_count)
		return 0;
	relation = &catalog->evidence.hook.relations[index];
	*relation_out = *relation;
	*domains_out = catalog->evidence.hook.relation_domains +
		relation->first_domain;
	return 1;
}

int SG_StaticAffordanceCatalogHookOutcomeCount(
	const sg_static_affordance_catalog_t *catalog,
	sg_hook_visibility_catalog_outcome_t outcome, uint32_t *count_out)
{
	const sg_hook_visibility_feasibility_audit_report_t *acceptance;

	if (!CatalogHeaderValid(catalog) || !count_out)
		return 0;
	acceptance = &catalog->evidence.hook.acceptance;
	switch (outcome)
	{
	case SG_HOOK_VISIBILITY_CATALOG_HOOKABLE:
		*count_out = acceptance->hookable_terms;
		return 1;
	case SG_HOOK_VISIBILITY_CATALOG_SKY:
		*count_out = acceptance->sky_terms;
		return 1;
	case SG_HOOK_VISIBILITY_CATALOG_NONHOOKABLE:
		*count_out = acceptance->nonhookable_terms;
		return 1;
	case SG_HOOK_VISIBILITY_CATALOG_NO_HIT:
		*count_out = acceptance->no_hit_terms;
		return 1;
	case SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED:
		*count_out = acceptance->clearance_blocked_terms;
		return 1;
	default:
		return 0;
	}
}

void SG_StaticAffordanceCatalogDestroy(sg_static_affordance_catalog_t *catalog)
{
	if (!CatalogHeaderValid(catalog))
		return;
	memset(catalog, 0, catalog->allocation_size);
	free(catalog);
}

const char *SG_StaticAffordanceCatalogErrorString(
	sg_static_affordance_catalog_error_code_t code)
{
	switch (code)
	{
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_NONE: return "none";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_STATIC_VISIBILITY_REJECTED:
		return "static visibility source rejected";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_CONTEXT_REJECTED:
		return "weapon context rejected";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_EVIDENCE_REJECTED:
		return "weapon evidence rejected";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_SOURCE_MISMATCH:
		return "static-affordance source mismatch";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_HOOK_CATALOG_REJECTED:
		return "hook visibility catalog rejected";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_COMPLEMENT_DISAGREEMENT:
		return "hook complement outcomes disagreed";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_OVERFLOW:
		return "representation overflow";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_COPY_DISAGREEMENT:
		return "owned audit copy disagreed";
	default: return "unknown static-affordance catalog error";
	}
}

const char *SG_StaticAffordanceCatalogAuditCodeString(
	sg_static_affordance_catalog_audit_code_t code)
{
	switch (code)
	{
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_OK: return "ok";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_STORAGE_DISAGREEMENT:
		return "storage disagreement";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_SOURCE_MISMATCH:
		return "source mismatch";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_STATIC_VISIBILITY_DISAGREEMENT:
		return "static visibility evidence disagreed";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_WEAPON_EVIDENCE_DISAGREEMENT:
		return "weapon evidence disagreed";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_HOOK_EVIDENCE_DISAGREEMENT:
		return "hook evidence disagreed";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_DIGEST_DISAGREEMENT:
		return "owned evidence digest disagreed";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_COVERAGE_DISAGREEMENT:
		return "unsupported coverage claim";
	default: return "unknown static-affordance catalog audit error";
	}
}
