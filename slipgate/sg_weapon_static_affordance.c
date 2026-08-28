#include "sg_weapon_static_affordance.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

static const sg_weapon_static_relation_t relation_order[] = {
	SG_WEAPON_STATIC_DIRECT_VISIBILITY,
	SG_WEAPON_STATIC_PROJECTILE_CORRIDOR,
	SG_WEAPON_STATIC_IMPACT_SURFACE,
	SG_WEAPON_STATIC_BLAST_REACH,
	SG_WEAPON_STATIC_BOUNCE_SURFACE
};

static void SetError(sg_weapon_static_affordance_error_t *error,
	sg_weapon_static_affordance_error_code_t code,
	const sg_static_visibility_error_t *visibility)
{
	if (!error)
		return;
	memset(error, 0, sizeof(*error));
	error->code = code;
	if (visibility)
		error->visibility = *visibility;
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		memcmp(&left->standing_hull, &right->standing_hull,
			sizeof(left->standing_hull)) == 0 &&
		memcmp(&left->crouching_hull, &right->crouching_hull,
			sizeof(left->crouching_hull)) == 0 &&
		memcmp(&left->physics, &right->physics, sizeof(left->physics)) == 0;
}

static int BindingEqual(const sg_weapon_static_binding_t *left,
	const sg_weapon_static_binding_t *right)
{
	return SG_RuneV2ContentIdEqual(&left->artifact_identity,
			&right->artifact_identity) &&
		SG_RuneV2ContentIdEqual(&left->bsp_identity,
			&right->bsp_identity) &&
		SG_RuneV2ContentIdEqual(&left->schema_identity,
			&right->schema_identity) &&
		left->source_set_identity == right->source_set_identity &&
		left->visibility_revision == right->visibility_revision;
}

static int PreparedQueryValid(const sg_weapon_static_query_t *query)
{
	sg_weapon_static_query_input_t input;
	sg_weapon_static_query_t prepared;

	if (!query || query->exact_live_prefire_trace_required != 1U)
		return 0;
	memset(&input, 0, sizeof(input));
	input.binding = query->binding;
	input.source_cell = query->source_cell;
	input.target_cell = query->target_cell;
	input.source_phase = query->source_phase;
	input.target_phase = query->target_phase;
	input.source_origin = query->source_origin;
	input.target_origin = query->target_origin;
	input.target_bounds = query->target_bounds;
	input.requested_relations = query->requested_relations;
	return SG_WeaponStaticQueryPrepare(&input, &prepared);
}

static int CellExists(const sg_configuration_space_t *configuration,
	const sg_rune_cell_ref_t *cell)
{
	uint32_t index;

	for (index = 0U; index < configuration->cell_count; index++)
		if (SG_RuneModelStableIdEqual(&configuration->cells[index].id.value,
				&cell->value))
			return 1;
	return 0;
}

static int SourcesBound(const sg_weapon_static_sources_t *sources,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile)
{
	const sg_host_collision_authority_t *authority = sources->authority;
	const sg_configuration_space_t *configuration = sources->configuration;
	const sg_configuration_semantics_t *semantics = sources->semantics;
	const sg_static_visibility_t *visibility = sources->visibility;

	if (!authority->world || !configuration->cells ||
		configuration->cell_count == 0U ||
		configuration->cell_count > SG_CONFIGURATION_DEFAULT_MAX_CELLS ||
		!SG_WeaponStaticBindingValid(&sources->binding) ||
		!BindingEqual(&sources->binding, &query->binding) ||
		!IdentityEqual(&authority->identity, &configuration->identity) ||
		!IdentityEqual(&authority->identity, &semantics->identity) ||
		!IdentityEqual(&authority->identity, &visibility->identity) ||
		query->binding.source_set_identity !=
			authority->identity.source_set_identity ||
		profile->build_identity != authority->identity.producer_identity ||
		profile->physics_abi_id != authority->identity.physics_abi_id)
		return 0;
	return 1;
}

static sg_weapon_static_relation_t AllowedRelations(
	const sg_weapon_profile_t *profile)
{
	sg_weapon_static_relation_t allowed = 0U;

	if ((profile->effects & (SG_WEAPON_EFFECT_HITSCAN |
			SG_WEAPON_EFFECT_PERIODIC_RAY)) != 0U)
		allowed |= SG_WEAPON_STATIC_DIRECT_VISIBILITY;
	if ((profile->effects & SG_WEAPON_EFFECT_PROJECTILE) != 0U)
		allowed |= SG_WEAPON_STATIC_PROJECTILE_CORRIDOR;
	if (profile->supports_occluded_impact != 0U ||
		(profile->effects & SG_WEAPON_EFFECT_SPECIAL) != 0U)
		allowed |= SG_WEAPON_STATIC_IMPACT_SURFACE;
	if ((profile->effects & (SG_WEAPON_EFFECT_SPLASH |
			SG_WEAPON_EFFECT_SECONDARY_AREA)) != 0U)
		allowed |= SG_WEAPON_STATIC_BLAST_REACH;
	if ((profile->effects & SG_WEAPON_EFFECT_BOUNCE) != 0U)
		allowed |= SG_WEAPON_STATIC_BOUNCE_SURFACE;
	return allowed;
}

static sg_weapon_static_status_t VisibilityStatus(
	const sg_static_visibility_result_t *visibility)
{
	switch (visibility->classification)
	{
	case SG_STATIC_VISIBILITY_VISIBLE:
		return SG_WEAPON_STATIC_PROVEN;
	case SG_STATIC_VISIBILITY_CONDITIONAL:
		return SG_WEAPON_STATIC_CONDITIONAL;
	case SG_STATIC_VISIBILITY_OCCLUDED:
		return SG_WEAPON_STATIC_REJECTED;
	}
	return SG_WEAPON_STATIC_REJECTED;
}

static void InitAffordance(sg_weapon_static_affordance_t *affordance,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile)
{
	size_t index;

	memset(affordance, 0, sizeof(*affordance));
	affordance->binding = query->binding;
	affordance->profile_id = profile->id;
	affordance->family = profile->family;
	affordance->requested_relations = query->requested_relations;
	affordance->allowed_relations = AllowedRelations(profile);
	affordance->exact_authenticated_live_prefire_trace_required = 1U;
	for (index = 0U; index < SG_WEAPON_STATIC_RELATION_COUNT; index++)
	{
		sg_weapon_static_relation_result_t *relation =
			&affordance->relations[index];

		relation->relation = relation_order[index];
		relation->visibility.surface = SG_STATIC_VISIBILITY_INDEX_NONE;
		relation->visibility.source_partition =
			SG_STATIC_VISIBILITY_INDEX_NONE;
		relation->visibility.destination_partition =
			SG_STATIC_VISIBILITY_INDEX_NONE;
		relation->visibility.trace.fraction = 1.0f;
		if ((query->requested_relations & relation->relation) != 0U &&
			(affordance->allowed_relations & relation->relation) == 0U)
		{
			relation->status = SG_WEAPON_STATIC_REJECTED;
			relation->reason = SG_WEAPON_STATIC_REASON_PROFILE_UNSUPPORTED;
		}
	}
}

static void SetRelation(sg_weapon_static_relation_result_t *relation,
	sg_weapon_static_status_t status, sg_weapon_static_reason_t reason,
	const sg_static_visibility_result_t *visibility)
{
	relation->status = status;
	relation->reason = reason;
	if (visibility)
		relation->visibility = *visibility;
}

static int TraceBlocked(const sg_host_collision_trace_t *trace)
{
	return trace->startsolid || trace->allsolid || trace->fraction < 1.0f;
}

static int RefineProjectileClearance(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile,
	sg_weapon_static_relation_result_t *relation)
{
	float mins[3], maxs[3];
	sg_host_collision_trace_t trace;
	uint32_t axis;

	if (relation->status != SG_WEAPON_STATIC_PROVEN ||
		profile->projectile_half_extent == 0.0f)
		return 1;
	for (axis = 0U; axis < 3U; axis++)
	{
		mins[axis] = -profile->projectile_half_extent;
		maxs[axis] = profile->projectile_half_extent;
	}
	if (!SG_HostCollisionTraceModel(authority, SG_HOST_COLLISION_MODEL_WORLD,
			NULL, query->source_origin.value, mins, maxs,
			query->target_origin.value,
			SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW, &trace))
		return 0;
	if (TraceBlocked(&trace))
	{
		relation->status = SG_WEAPON_STATIC_REJECTED;
		relation->reason = SG_WEAPON_STATIC_REASON_PROJECTILE_CLEARANCE;
		relation->visibility.classification =
			SG_STATIC_VISIBILITY_OCCLUDED;
		relation->visibility.reason =
			SG_STATIC_VISIBILITY_REASON_STATIC_WORLD;
		relation->visibility.trace = trace;
		return 1;
	}
	if (scene->instance_count == 0U)
		return 1;
	if (!SG_HostCollisionTrace(authority, scene, query->source_origin.value,
			mins, maxs, query->target_origin.value,
			SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW, &trace))
		return 0;
	if (TraceBlocked(&trace))
	{
		relation->status = SG_WEAPON_STATIC_CONDITIONAL;
		relation->reason = SG_WEAPON_STATIC_REASON_PROJECTILE_CLEARANCE;
		relation->visibility.classification =
			SG_STATIC_VISIBILITY_CONDITIONAL;
		relation->visibility.reason =
			SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL;
		relation->visibility.trace = trace;
	}
	return 1;
}

static float SplashRadius(const sg_weapon_profile_t *profile)
{
	return profile->secondary_splash_radius > profile->splash_radius ?
		profile->secondary_splash_radius : profile->splash_radius;
}

static int WithinSplashReach(const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile, float closest[3])
{
	float distance_squared = 0.0f;
	float radius = SplashRadius(profile);
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float delta = 0.0f;
		float coordinate = query->target_origin.value[axis];

		if (coordinate < query->target_bounds.mins.value[axis])
		{
			closest[axis] = query->target_bounds.mins.value[axis];
			delta = closest[axis] - coordinate;
		}
		else if (coordinate > query->target_bounds.maxs.value[axis])
		{
			closest[axis] = query->target_bounds.maxs.value[axis];
			delta = coordinate - closest[axis];
		}
		else
			closest[axis] = coordinate;
		distance_squared += delta * delta;
	}
	return isfinite(distance_squared) != 0 &&
		distance_squared <= radius * radius;
}

static void PreferSurfaceEvidence(
	sg_weapon_static_relation_result_t *relation,
	sg_weapon_static_status_t status, sg_weapon_static_reason_t reason,
	const sg_static_visibility_result_t *visibility)
{
	if (relation->status == SG_WEAPON_STATIC_NOT_REQUESTED ||
		status > relation->status)
		SetRelation(relation, status, reason, visibility);
}

static int ResolveSurfaceRelations(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile,
	sg_weapon_static_affordance_t *affordance,
	sg_weapon_static_affordance_error_t *error)
{
	sg_weapon_static_relation_t requested = query->requested_relations &
		affordance->allowed_relations &
		(SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH |
		 SG_WEAPON_STATIC_BOUNCE_SURFACE);
	uint32_t surface;
	int matched = 0;

	if (requested == 0U)
		return 1;
	for (surface = 0U; surface < visibility->surface_count; surface++)
	{
		const sg_configuration_hook_surface_t *surface_record;
		sg_static_visibility_result_t surface_visibility;
		sg_static_visibility_error_t visibility_error;
		sg_weapon_static_status_t status;

		if (!SG_StaticVisibilityQuerySurface(authority, scene, configuration,
				semantics, visibility, query->source_origin.value, surface,
				query->target_origin.value, &surface_visibility,
				&visibility_error))
		{
			if (visibility_error.code ==
				SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY)
				continue;
			SetError(error, SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY,
				&visibility_error);
			return 0;
		}
		matched = 1;
		status = VisibilityStatus(&surface_visibility);
		surface_record = &semantics->hook_surfaces[surface];
		if ((requested & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U)
		{
			sg_weapon_static_relation_result_t *impact =
				&affordance->relations[2];
			if (profile->family == SG_WEAPON_FAMILY_SPECIAL &&
				(surface_record->flags &
				 SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE) == 0U)
				PreferSurfaceEvidence(impact, SG_WEAPON_STATIC_REJECTED,
					SG_WEAPON_STATIC_REASON_TARGET_SURFACE_UNSUPPORTED,
					&surface_visibility);
			else
				PreferSurfaceEvidence(impact, status,
					SG_WEAPON_STATIC_REASON_VISIBILITY,
					&surface_visibility);
		}
		if ((requested & SG_WEAPON_STATIC_BLAST_REACH) != 0U)
		{
			sg_weapon_static_relation_result_t *blast =
				&affordance->relations[3];
			float closest[3];

			if (!WithinSplashReach(query, profile, closest))
				PreferSurfaceEvidence(blast, SG_WEAPON_STATIC_REJECTED,
					SG_WEAPON_STATIC_REASON_OUTSIDE_SPLASH_REACH,
					&surface_visibility);
			else if (status == SG_WEAPON_STATIC_REJECTED ||
				(closest[0] == query->target_origin.value[0] &&
				 closest[1] == query->target_origin.value[1] &&
				 closest[2] == query->target_origin.value[2]))
				PreferSurfaceEvidence(blast, status,
					SG_WEAPON_STATIC_REASON_VISIBILITY,
					&surface_visibility);
			else
			{
				sg_static_visibility_result_t target_visibility;
				sg_weapon_static_status_t target_status;
				sg_weapon_static_status_t combined_status = status;

				if (!SG_StaticVisibilityQuerySurface(authority, scene,
						configuration, semantics, visibility, closest, surface,
						query->target_origin.value, &target_visibility,
						&visibility_error))
				{
					if (visibility_error.code ==
						SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY)
					{
						PreferSurfaceEvidence(blast,
							SG_WEAPON_STATIC_REJECTED,
							SG_WEAPON_STATIC_REASON_VISIBILITY, NULL);
					}
					else
					{
						SetError(error,
						SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY,
						&visibility_error);
						return 0;
					}
				}
				else
				{
					target_status = VisibilityStatus(&target_visibility);
					if (target_status < combined_status)
						combined_status = target_status;
					PreferSurfaceEvidence(blast, combined_status,
						SG_WEAPON_STATIC_REASON_VISIBILITY,
						target_status < status ?
							&target_visibility : &surface_visibility);
				}
			}
		}
		if ((requested & SG_WEAPON_STATIC_BOUNCE_SURFACE) != 0U)
			PreferSurfaceEvidence(&affordance->relations[4], status,
				SG_WEAPON_STATIC_REASON_VISIBILITY, &surface_visibility);
	}
	if (!matched)
	{
		size_t index;
		for (index = 2U; index < SG_WEAPON_STATIC_RELATION_COUNT; index++)
			if ((requested & relation_order[index]) != 0U)
				SetRelation(&affordance->relations[index],
					SG_WEAPON_STATIC_REJECTED,
					SG_WEAPON_STATIC_REASON_TARGET_NOT_SURFACE, NULL);
	}
	return 1;
}

static void FinalizeMasks(sg_weapon_static_affordance_t *affordance)
{
	size_t index;

	for (index = 0U; index < SG_WEAPON_STATIC_RELATION_COUNT; index++)
	{
		const sg_weapon_static_relation_result_t *relation =
			&affordance->relations[index];

		switch (relation->status)
		{
		case SG_WEAPON_STATIC_PROVEN:
			affordance->proven_relations |= relation->relation;
			break;
		case SG_WEAPON_STATIC_REJECTED:
			affordance->rejected_relations |= relation->relation;
			break;
		case SG_WEAPON_STATIC_CONDITIONAL:
			affordance->conditional_relations |= relation->relation;
			break;
		case SG_WEAPON_STATIC_NOT_REQUESTED:
			break;
		}
	}
}

int SG_WeaponStaticAffordanceResolve(
	const sg_weapon_static_sources_t *sources,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile,
	sg_weapon_static_affordance_t *affordance_out,
	sg_weapon_static_affordance_error_t *error_out)
{
	sg_weapon_static_affordance_t affordance;
	sg_static_visibility_result_t source_visibility;
	sg_static_visibility_result_t point_visibility;
	sg_static_visibility_error_t visibility_error;
	sg_weapon_static_relation_t point_relations;
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_static_visibility_t *visibility;

	SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_NONE, NULL);
	if (!sources || !sources->authority || !sources->configuration ||
		!sources->semantics || !sources->visibility || !scene || !query ||
		!profile || !affordance_out)
	{
		SetError(error_out,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_ARGUMENT, NULL);
		return 0;
	}
	authority = sources->authority;
	configuration = sources->configuration;
	semantics = sources->semantics;
	visibility = sources->visibility;
	if (!PreparedQueryValid(query))
	{
		SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_QUERY,
			NULL);
		return 0;
	}
	if (!SG_WeaponProfileValid(profile) || profile->resolved != 1U)
	{
		SetError(error_out,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_PROFILE, NULL);
		return 0;
	}
	if (!SourcesBound(sources, query, profile))
	{
		SetError(error_out,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH, NULL);
		return 0;
	}
	if (!CellExists(configuration, &query->source_cell) ||
		!CellExists(configuration, &query->target_cell))
	{
		SetError(error_out,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE, NULL);
		return 0;
	}
	if (!SG_StaticVisibilityQueryPoints(authority, scene, configuration,
			semantics, visibility, query->source_origin.value,
			query->source_origin.value, &source_visibility, &visibility_error))
	{
		SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY,
			&visibility_error);
		return 0;
	}

	InitAffordance(&affordance, query, profile);
	point_relations = query->requested_relations &
		affordance.allowed_relations &
		(SG_WEAPON_STATIC_DIRECT_VISIBILITY |
		 SG_WEAPON_STATIC_PROJECTILE_CORRIDOR);
	if (point_relations != 0U)
	{
		sg_weapon_static_status_t status;

		if (!SG_StaticVisibilityQueryPoints(authority, scene, configuration,
				semantics, visibility, query->source_origin.value,
				query->target_origin.value, &point_visibility,
				&visibility_error))
		{
			SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY,
				&visibility_error);
			return 0;
		}
		status = VisibilityStatus(&point_visibility);
		if ((point_relations & SG_WEAPON_STATIC_DIRECT_VISIBILITY) != 0U)
			SetRelation(&affordance.relations[0], status,
				SG_WEAPON_STATIC_REASON_VISIBILITY, &point_visibility);
		if ((point_relations & SG_WEAPON_STATIC_PROJECTILE_CORRIDOR) != 0U)
		{
			SetRelation(&affordance.relations[1], status,
				SG_WEAPON_STATIC_REASON_VISIBILITY, &point_visibility);
			if (!RefineProjectileClearance(authority, scene, query, profile,
					&affordance.relations[1]))
			{
				SetError(error_out,
					SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE, NULL);
				return 0;
			}
		}
	}
	if (!ResolveSurfaceRelations(authority, scene, configuration, semantics,
			visibility, query, profile, &affordance, error_out))
		return 0;
	FinalizeMasks(&affordance);
	*affordance_out = affordance;
	return 1;
}

const char *SG_WeaponStaticAffordanceErrorString(
	sg_weapon_static_affordance_error_code_t code)
{
	switch (code)
	{
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_NONE:
		return "none";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_QUERY:
		return "invalid query";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_PROFILE:
		return "invalid profile";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE:
		return "invalid source";
	case SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY:
		return "static visibility query failed";
	}
	return "unknown static weapon-affordance error";
}
