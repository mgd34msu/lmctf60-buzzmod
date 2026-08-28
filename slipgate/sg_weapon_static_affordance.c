#include "sg_weapon_static_affordance.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* These are RUNE representation limits, not work-ending budgets. */
#define SG_WEAPON_STATIC_MAX_PARTITIONS SG_RUNE_MODEL_MAX_CELLS
#define SG_WEAPON_STATIC_MAX_SURFACES SG_RUNE_MODEL_MAX_SURFACES
#define SG_WEAPON_STATIC_MAX_SURFACE_VERTICES \
	SG_RUNE_MODEL_MAX_PORTAL_VERTICES
#define SG_WEAPON_SURFACE_EPSILON 0.03125f

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
	if (!SG_WeaponStaticQueryPrepare(&input, &prepared))
		return 0;
	return query->target_origin.value[0] >=
			query->target_bounds.mins.value[0] &&
		query->target_origin.value[0] <= query->target_bounds.maxs.value[0] &&
		query->target_origin.value[1] >=
			query->target_bounds.mins.value[1] &&
		query->target_origin.value[1] <= query->target_bounds.maxs.value[1] &&
		query->target_origin.value[2] >=
			query->target_bounds.mins.value[2] &&
		query->target_origin.value[2] <= query->target_bounds.maxs.value[2];
}

static int StableIdCompare(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	if (left->source_set_identity != right->source_set_identity)
		return left->source_set_identity < right->source_set_identity ? -1 : 1;
	if (left->high != right->high)
		return left->high < right->high ? -1 : 1;
	if (left->low != right->low)
		return left->low < right->low ? -1 : 1;
	return 0;
}

static int FindModelCell(const sg_rune_model_t *model,
	const sg_rune_cell_ref_t *reference, uint32_t *index_out)
{
	/* SG_RuneModelValidate requires strictly increasing canonical order and
	 * derives these IDs directly from that order. Accepted models therefore
	 * have the exact lexicographic stable-ID order used here. */
	uint32_t low = 0U;
	uint32_t high = model->cell_count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		int comparison = StableIdCompare(&model->cells[middle].id.value,
			&reference->value);

		if (comparison < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= model->cell_count ||
		StableIdCompare(&model->cells[low].id.value, &reference->value) != 0)
		return 0;
	*index_out = low;
	return 1;
}

static int FindModelPhase(const sg_rune_model_t *model,
	const sg_rune_phase_ref_t *reference, uint32_t *index_out)
{
	uint32_t low = 0U;
	uint32_t high = model->phase_count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		int comparison = StableIdCompare(&model->phases[middle].id.value,
			&reference->value);

		if (comparison < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= model->phase_count ||
		StableIdCompare(&model->phases[low].id.value, &reference->value) != 0)
		return 0;
	*index_out = low;
	return 1;
}

static int SourcesBound(const sg_weapon_static_sources_t *sources,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile)
{
	const sg_host_collision_authority_t *authority = sources->authority;
	const sg_configuration_space_t *configuration = sources->configuration;
	const sg_configuration_semantics_t *semantics = sources->semantics;
	const sg_static_visibility_t *visibility = sources->visibility;
	const sg_rune_model_t *model = sources->model;
	const sg_weapon_static_source_audit_t *audit = sources->audit;
	sg_rune_model_flags_t required_flags = SG_RUNE_MODEL_IMMUTABLE |
		SG_RUNE_MODEL_EXACT_BOUND | SG_RUNE_MODEL_NO_RUNTIME_ACTORS;

	if (!authority->world || !configuration->cells || !semantics->regions ||
		!visibility->partitions || !model || !audit || !model->cells ||
		!model->phases ||
		configuration->cell_count == 0U ||
		configuration->cell_count > SG_CONFIGURATION_DEFAULT_MAX_CELLS ||
		semantics->region_count == 0U ||
		semantics->region_count > SG_WEAPON_STATIC_MAX_PARTITIONS ||
		semantics->hook_surface_count > SG_WEAPON_STATIC_MAX_SURFACES ||
		semantics->hook_vertex_count >
			SG_WEAPON_STATIC_MAX_SURFACE_VERTICES ||
		(semantics->hook_surface_count != 0U &&
		 !semantics->hook_surfaces) ||
		(semantics->hook_vertex_count != 0U && !semantics->hook_vertices) ||
		model->cell_count == 0U || model->phase_count == 0U ||
		model->cell_count > SG_RUNE_MODEL_MAX_CELLS ||
		model->phase_count > SG_RUNE_MODEL_MAX_PHASES ||
		!SG_WeaponStaticBindingValid(&sources->binding) ||
		!BindingEqual(&sources->binding, &query->binding) ||
		!BindingEqual(&sources->binding, &audit->binding) ||
		audit->visibility.code != SG_STATIC_VISIBILITY_AUDIT_OK ||
		audit->visibility.record != SG_STATIC_VISIBILITY_INDEX_NONE ||
		audit->configuration_cells != configuration->cell_count ||
		audit->semantic_regions != semantics->region_count ||
		audit->semantic_surfaces != semantics->hook_surface_count ||
		audit->semantic_surface_vertices != semantics->hook_vertex_count ||
		audit->model_cells != model->cell_count ||
		audit->model_phases != model->phase_count ||
		audit->visibility.reconstructed_partitions !=
			visibility->partition_count ||
		audit->visibility.reconstructed_areas != visibility->area_count ||
		audit->visibility.reconstructed_occluders !=
			visibility->occluder_count ||
		audit->visibility.reconstructed_surfaces !=
			visibility->surface_count ||
		visibility->partition_count != semantics->region_count ||
		visibility->surface_count != semantics->hook_surface_count ||
		configuration->cell_count != model->cell_count ||
		model->version != SG_RUNE_MODEL_VERSION ||
		model->schema_tag != SG_RUNE_MODEL_SCHEMA_TAG || model->reserved != 0U ||
		(model->flags & required_flags) != required_flags ||
		!SG_RuneModelCompletenessValid(&model->completeness) ||
		model->completeness.state != SG_RUNE_COMPLETENESS_COMPLETE ||
		model->completeness.covered_cells != model->cell_count ||
		!IdentityEqual(&authority->identity, &configuration->identity) ||
		!IdentityEqual(&authority->identity, &semantics->identity) ||
		!IdentityEqual(&authority->identity, &visibility->identity) ||
		!IdentityEqual(&authority->identity, &model->identity) ||
		query->binding.source_set_identity !=
			authority->identity.source_set_identity ||
		profile->build_identity != authority->identity.producer_identity ||
		profile->physics_abi_id != authority->identity.physics_abi_id)
		return 0;
	return 1;
}

static int PoseBindingValid(const sg_weapon_static_sources_t *sources,
	uint32_t partition_index, const sg_rune_cell_ref_t *cell_reference,
	const sg_rune_phase_ref_t *phase_reference)
{
	const sg_static_visibility_t *visibility = sources->visibility;
	const sg_configuration_space_t *configuration = sources->configuration;
	const sg_rune_model_t *model = sources->model;
	const sg_rune_phase_span_t *span;
	uint32_t configuration_cell, model_cell, model_phase;

	if (partition_index >= visibility->partition_count)
		return 0;
	configuration_cell = visibility->partitions[
		partition_index].configuration_cell;
	if (configuration_cell >= configuration->cell_count ||
		!SG_RuneModelStableIdEqual(
			&configuration->cells[configuration_cell].id.value,
			&cell_reference->value) ||
		!FindModelCell(model, cell_reference, &model_cell) ||
		!FindModelPhase(model, phase_reference, &model_phase) ||
		!SG_RuneModelPhaseValid(&model->phases[model_phase]))
		return 0;
	span = &model->cells[model_cell].phases;
	return span->first <= model->phase_count &&
		span->count <= model->phase_count - span->first &&
		model_phase >= span->first && model_phase - span->first < span->count;
}

static int QueryPosesBound(const sg_weapon_static_sources_t *sources,
	const sg_weapon_static_query_t *query,
	const sg_static_visibility_result_t *visibility)
{
	return PoseBindingValid(sources, visibility->source_partition,
			&query->source_cell, &query->source_phase) &&
		PoseBindingValid(sources, visibility->destination_partition,
			&query->target_cell, &query->target_phase);
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

static float Dot3(const float left[3], const float right[3])
{
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
}

static void Cross3(const float left[3], const float right[3], float result[3])
{
	result[0] = left[1] * right[2] - left[2] * right[1];
	result[1] = left[2] * right[0] - left[0] * right[2];
	result[2] = left[0] * right[1] - left[1] * right[0];
}

static int ClosestPointOnSurface(
	const sg_configuration_semantics_t *semantics,
	const sg_configuration_hook_surface_t *surface,
	const float point[3], float closest[3])
{
	float projected[3], signed_distance;
	float winding = 0.0f;
	float best_distance_squared = 0.0f;
	uint32_t vertex, axis;
	int projected_inside = 1;
	int have_best = 0;

	if (surface->vertex_count < 3U ||
		surface->first_vertex > semantics->hook_vertex_count ||
		surface->vertex_count >
			semantics->hook_vertex_count - surface->first_vertex)
		return 0;
	{
		float normal_length_squared = Dot3(surface->normal, surface->normal);

		if (!isfinite(normal_length_squared) || normal_length_squared <= 0.0f)
			return 0;
		signed_distance = (Dot3(point, surface->normal) - surface->distance) /
			normal_length_squared;
	}
	for (axis = 0U; axis < 3U; axis++)
		projected[axis] = point[axis] - signed_distance * surface->normal[axis];
	if (!isfinite(projected[0]) || !isfinite(projected[1]) ||
		!isfinite(projected[2]))
		return 0;
	for (vertex = 0U; vertex < surface->vertex_count; vertex++)
	{
		const float *start = semantics->hook_vertices[
			surface->first_vertex + vertex].value;
		const float *end = semantics->hook_vertices[surface->first_vertex +
			(vertex + 1U) % surface->vertex_count].value;
		float edge[3], relative[3], cross[3], side;

		for (axis = 0U; axis < 3U; axis++)
		{
			edge[axis] = end[axis] - start[axis];
			relative[axis] = projected[axis] - start[axis];
		}
		Cross3(edge, relative, cross);
		side = Dot3(cross, surface->normal);
		if (fabsf(side) <= SG_WEAPON_SURFACE_EPSILON)
			continue;
		if (winding == 0.0f)
			winding = side;
		else if ((winding < 0.0f) != (side < 0.0f))
			projected_inside = 0;
	}
	if (projected_inside)
	{
		memcpy(closest, projected, sizeof(projected));
		return 1;
	}
	for (vertex = 0U; vertex < surface->vertex_count; vertex++)
	{
		const float *start = semantics->hook_vertices[
			surface->first_vertex + vertex].value;
		const float *end = semantics->hook_vertices[surface->first_vertex +
			(vertex + 1U) % surface->vertex_count].value;
		float edge[3], relative[3], candidate[3];
		float length_squared, parameter, distance_squared = 0.0f;

		for (axis = 0U; axis < 3U; axis++)
		{
			edge[axis] = end[axis] - start[axis];
			relative[axis] = point[axis] - start[axis];
		}
		length_squared = Dot3(edge, edge);
		if (!isfinite(length_squared) || length_squared <= 0.0f)
			return 0;
		parameter = Dot3(relative, edge) / length_squared;
		if (parameter < 0.0f)
			parameter = 0.0f;
		else if (parameter > 1.0f)
			parameter = 1.0f;
		for (axis = 0U; axis < 3U; axis++)
		{
			float delta;

			candidate[axis] = start[axis] + parameter * edge[axis];
			delta = point[axis] - candidate[axis];
			distance_squared += delta * delta;
		}
		if (!isfinite(distance_squared))
			return 0;
		if (!have_best || distance_squared < best_distance_squared)
		{
			memcpy(closest, candidate, sizeof(candidate));
			best_distance_squared = distance_squared;
			have_best = 1;
		}
	}
	return have_best;
}

static int WithinSplashReach(const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile, const float impact[3], float closest[3])
{
	float distance_squared = 0.0f;
	float radius = SplashRadius(profile);
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float delta = 0.0f;
		float coordinate = impact[axis];

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
	const sg_static_visibility_result_t *visibility, const float witness[3])
{
	if (relation->status == SG_WEAPON_STATIC_NOT_REQUESTED ||
		status > relation->status ||
		(status == SG_WEAPON_STATIC_CONDITIONAL &&
		 relation->reason == SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE &&
		 reason == SG_WEAPON_STATIC_REASON_VISIBILITY))
	{
		SetRelation(relation, status, reason, visibility);
		if (witness)
		{
			memcpy(relation->witness_point.value, witness,
				sizeof(relation->witness_point.value));
			relation->has_witness_point = 1U;
		}
	}
}

static int SurfaceBoundsOutsideSplash(
	const sg_configuration_hook_surface_t *surface,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile)
{
	float distance_squared = 0.0f;
	float radius = SplashRadius(profile);
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float delta = 0.0f;

		if (surface->bounds.maxs.value[axis] <
			query->target_bounds.mins.value[axis])
			delta = query->target_bounds.mins.value[axis] -
				surface->bounds.maxs.value[axis];
		else if (surface->bounds.mins.value[axis] >
			query->target_bounds.maxs.value[axis])
			delta = surface->bounds.mins.value[axis] -
				query->target_bounds.maxs.value[axis];
		distance_squared += delta * delta;
	}
	return isfinite(distance_squared) != 0 &&
		distance_squared > radius * radius;
}

static sg_weapon_static_status_t CandidateStatus(
	const sg_static_visibility_result_t *visibility,
	sg_weapon_static_reason_t *reason_out)
{
	sg_weapon_static_status_t status = VisibilityStatus(visibility);

	*reason_out = SG_WEAPON_STATIC_REASON_VISIBILITY;
	if (status == SG_WEAPON_STATIC_REJECTED &&
		visibility->reason != SG_STATIC_VISIBILITY_REASON_SKY)
	{
		status = SG_WEAPON_STATIC_CONDITIONAL;
		*reason_out = SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE;
	}
	return status;
}

static int EvaluateSurfaceCandidate(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile,
	sg_weapon_static_relation_t requested, uint32_t surface,
	const float impact_point[3], sg_weapon_static_affordance_t *affordance,
	sg_weapon_static_affordance_error_t *error)
{
	sg_static_visibility_result_t surface_visibility;
	sg_static_visibility_error_t visibility_error;
	sg_weapon_static_reason_t reason;
	sg_weapon_static_status_t status;

	if (!SG_StaticVisibilityQuerySurface(authority, scene, configuration,
			semantics, visibility, query->source_origin.value, surface,
			impact_point, &surface_visibility, &visibility_error))
	{
		if (visibility_error.code ==
				SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY ||
			visibility_error.code ==
				SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH)
			return 2;
		SetError(error, SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY,
			&visibility_error);
		return 0;
	}
	status = CandidateStatus(&surface_visibility, &reason);
	if ((requested & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U)
		PreferSurfaceEvidence(&affordance->relations[2], status, reason,
			&surface_visibility, impact_point);
	if ((requested & SG_WEAPON_STATIC_BLAST_REACH) != 0U)
	{
		sg_weapon_static_relation_result_t *blast = &affordance->relations[3];
		float closest[3];

		if (WithinSplashReach(query, profile, impact_point, closest))
		{
			if (status == SG_WEAPON_STATIC_REJECTED ||
				(closest[0] == impact_point[0] &&
				 closest[1] == impact_point[1] &&
				 closest[2] == impact_point[2]))
				PreferSurfaceEvidence(blast, status, reason,
					&surface_visibility, impact_point);
			else
			{
				sg_static_visibility_result_t target_visibility;
				sg_weapon_static_reason_t target_reason;
				sg_weapon_static_status_t target_status;
				sg_weapon_static_status_t combined_status;

				if (!SG_StaticVisibilityQuerySurface(authority, scene,
						configuration, semantics, visibility, closest, surface,
						impact_point, &target_visibility, &visibility_error))
				{
					if (visibility_error.code ==
							SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY ||
						visibility_error.code ==
							SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH)
					{
						PreferSurfaceEvidence(blast,
							SG_WEAPON_STATIC_CONDITIONAL,
							SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE,
							&surface_visibility, impact_point);
						return 1;
					}
					SetError(error,
						SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY,
						&visibility_error);
					return 0;
				}
				target_status = CandidateStatus(&target_visibility,
					&target_reason);
				combined_status = target_status < status ? target_status : status;
				PreferSurfaceEvidence(blast, combined_status,
					target_status < status ? target_reason : reason,
					target_status < status ? &target_visibility :
						&surface_visibility, impact_point);
			}
		}
	}
	if ((requested & SG_WEAPON_STATIC_BOUNCE_SURFACE) != 0U)
		PreferSurfaceEvidence(&affordance->relations[4], status, reason,
			&surface_visibility, impact_point);
	return 1;
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
		const sg_configuration_hook_surface_t *surface_record =
			&semantics->hook_surfaces[surface];
		uint32_t candidate;
		uint32_t candidate_count = 1U + 3U * surface_record->vertex_count + 8U;
		float nearest[3];

		if (!ClosestPointOnSurface(semantics, surface_record,
				query->target_origin.value, nearest))
		{
			SetError(error, SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE,
				NULL);
			return 0;
		}
		matched = 1;
		if ((requested & SG_WEAPON_STATIC_BLAST_REACH) != 0U &&
			SurfaceBoundsOutsideSplash(surface_record, query, profile))
			PreferSurfaceEvidence(&affordance->relations[3],
				SG_WEAPON_STATIC_REJECTED,
				SG_WEAPON_STATIC_REASON_OUTSIDE_SPLASH_REACH, NULL, nearest);
		for (candidate = 0U; candidate < candidate_count; candidate++)
		{
			float impact_point[3];
			float seed[3];
			int evaluated;

			if (candidate == 0U)
				memcpy(impact_point, nearest, sizeof(impact_point));
			else if (candidate <= surface_record->vertex_count)
			{
				memcpy(seed, semantics->hook_vertices[
					surface_record->first_vertex + candidate - 1U].value,
					sizeof(seed));
				if (!ClosestPointOnSurface(semantics, surface_record, seed,
						impact_point))
				{
					SetError(error,
						SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE, NULL);
					return 0;
				}
			}
			else if (candidate <= 2U * surface_record->vertex_count)
			{
				uint32_t edge = candidate - surface_record->vertex_count - 1U;
				const float *start = semantics->hook_vertices[
					surface_record->first_vertex + edge].value;
				const float *end = semantics->hook_vertices[
					surface_record->first_vertex +
					(edge + 1U) % surface_record->vertex_count].value;
				uint32_t axis;

				for (axis = 0U; axis < 3U; axis++)
					seed[axis] = (start[axis] + end[axis]) * 0.5f;
				if (!ClosestPointOnSurface(semantics, surface_record, seed,
						impact_point))
				{
					SetError(error,
						SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE, NULL);
					return 0;
				}
			}
			else if (candidate <= 3U * surface_record->vertex_count)
			{
				uint32_t vertex = candidate -
					2U * surface_record->vertex_count - 1U;
				const float *point = semantics->hook_vertices[
					surface_record->first_vertex + vertex].value;
				uint32_t axis;

				for (axis = 0U; axis < 3U; axis++)
					seed[axis] = (nearest[axis] + point[axis]) * 0.5f;
				if (!ClosestPointOnSurface(semantics, surface_record, seed,
						impact_point))
				{
					SetError(error,
						SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE, NULL);
					return 0;
				}
			}
			else
			{
				float corner[3];
				uint32_t corner_index = candidate -
					3U * surface_record->vertex_count - 1U;
				uint32_t axis;

				for (axis = 0U; axis < 3U; axis++)
					corner[axis] = (corner_index & (1U << axis)) != 0U ?
						query->target_bounds.maxs.value[axis] :
						query->target_bounds.mins.value[axis];
				if (!ClosestPointOnSurface(semantics, surface_record, corner,
						impact_point))
				{
					SetError(error,
						SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE, NULL);
					return 0;
				}
			}
			evaluated = EvaluateSurfaceCandidate(authority, scene, configuration,
					semantics, visibility, query, profile, requested, surface,
					impact_point, affordance, error);
			if (evaluated == 0)
				return 0;
		}
		if ((requested & SG_WEAPON_STATIC_BLAST_REACH) != 0U &&
			!SurfaceBoundsOutsideSplash(surface_record, query, profile) &&
			(affordance->relations[3].status ==
				SG_WEAPON_STATIC_NOT_REQUESTED ||
			 affordance->relations[3].reason ==
				SG_WEAPON_STATIC_REASON_OUTSIDE_SPLASH_REACH))
			PreferSurfaceEvidence(&affordance->relations[3],
				SG_WEAPON_STATIC_CONDITIONAL,
				SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE, NULL,
				nearest);
	}
	if (matched)
	{
		size_t index;

		for (index = 2U; index < SG_WEAPON_STATIC_RELATION_COUNT; index++)
			if ((requested & relation_order[index]) != 0U &&
				affordance->relations[index].status ==
					SG_WEAPON_STATIC_NOT_REQUESTED)
				SetRelation(&affordance->relations[index],
					SG_WEAPON_STATIC_CONDITIONAL,
					SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE, NULL);
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
	sg_static_visibility_result_t point_visibility;
	sg_static_visibility_error_t visibility_error;
	sg_weapon_static_relation_t point_relations;
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_static_visibility_t *visibility;

	SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_NONE, NULL);
	if (!sources || !sources->authority || !sources->configuration ||
		!sources->semantics || !sources->visibility || !sources->model ||
		!sources->audit || !scene || !query || !profile || !affordance_out)
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
	if (!SG_StaticVisibilityQueryPoints(authority, scene, configuration,
			semantics, visibility, query->source_origin.value,
			query->target_origin.value, &point_visibility, &visibility_error))
	{
		SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY,
			&visibility_error);
		return 0;
	}
	if (!QueryPosesBound(sources, query, &point_visibility))
	{
		SetError(error_out, SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE,
			NULL);
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
