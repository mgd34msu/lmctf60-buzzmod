#include "sg_hook_visibility_feasibility_internal.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define HOOK_FNV_OFFSET UINT64_C(1469598103934665603)
#define HOOK_FNV_PRIME UINT64_C(1099511628211)
#define HOOK_PI 3.14159265358979323846
#define HOOK_TRACE_EPSILON (1.0f / 32.0f)
#define HOOK_STANDING_VIEW_HEIGHT 22.0f
#define HOOK_CROUCHING_VIEW_HEIGHT (-2.0f)
#define HOOK_MUZZLE_FORWARD 8.0f
#define HOOK_MUZZLE_LATERAL 8.0f

#define SetError SG_HookVisibilityFeasibilitySetError
#define UnsupportedError SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED
typedef sg_hook_visibility_build_context_t hook_build_t;
void SG_HookVisibilityFeasibilitySetError(
	sg_hook_visibility_build_context_t *build,
	sg_hook_visibility_feasibility_error_code_t code, uint32_t source_index)
{
	if (build->error.code == SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_NONE)
	{
		build->error.code = code;
		build->error.source_index = source_index;
	}
}

static uint64_t HashByte(uint64_t hash, uint8_t value)
{
	return (hash ^ value) * HOOK_FNV_PRIME;
}

static uint64_t HashU32(uint64_t hash, uint32_t value)
{
	uint32_t byte;

	for (byte = 0U; byte < 4U; byte++)
		hash = HashByte(hash, (uint8_t)(value >> (byte * 8U)));
	return hash;
}

static uint64_t HashU64(uint64_t hash, uint64_t value)
{
	uint32_t byte;

	for (byte = 0U; byte < 8U; byte++)
		hash = HashByte(hash, (uint8_t)(value >> (byte * 8U)));
	return hash;
}

static uint64_t HashFloat(uint64_t hash, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return HashU32(hash, bits);
}

static uint64_t HashIdentity(uint64_t hash,
	const sg_rune_model_identity_t *identity)
{
	uint32_t axis;

	hash = HashU64(hash, identity->bsp_content_id);
	hash = HashU64(hash, identity->entity_semantics_id);
	hash = HashU64(hash, identity->physics_abi_id);
	hash = HashU64(hash, identity->source_set_identity);
	hash = HashU64(hash, identity->schema_id);
	hash = HashU64(hash, identity->producer_identity);
	for (axis = 0U; axis < 3U; axis++)
	{
		hash = HashFloat(hash, identity->standing_hull.mins.value[axis]);
		hash = HashFloat(hash, identity->standing_hull.maxs.value[axis]);
		hash = HashFloat(hash, identity->crouching_hull.mins.value[axis]);
		hash = HashFloat(hash, identity->crouching_hull.maxs.value[axis]);
	}
	hash = HashFloat(hash, identity->physics.gravity);
	hash = HashFloat(hash, identity->physics.ground_acceleration);
	hash = HashFloat(hash, identity->physics.air_acceleration);
	hash = HashFloat(hash, identity->physics.water_acceleration);
	hash = HashFloat(hash, identity->physics.hook_acceleration);
	hash = HashFloat(hash, identity->physics.external_acceleration);
	hash = HashFloat(hash, identity->physics.water_drag);
	hash = HashFloat(hash, identity->physics.max_velocity);
	hash = HashU32(hash, identity->physics.frame_ms);
	hash = HashU32(hash, identity->physics.substep_ms);
	return hash;
}

static uint64_t HashWorld(uint64_t hash, const sg_bsp_world_t *world)
{
	uint32_t index, axis;

	hash = HashU32(hash, world->plane_count);
	for (index = 0U; index < world->plane_count; index++)
	{
		for (axis = 0U; axis < 3U; axis++)
			hash = HashFloat(hash, world->planes[index].normal.value[axis]);
		hash = HashFloat(hash, world->planes[index].distance);
		hash = HashU32(hash, (uint32_t)world->planes[index].type);
	}
	hash = HashU32(hash, world->node_count);
	for (index = 0U; index < world->node_count; index++)
	{
		hash = HashU32(hash, world->nodes[index].plane);
		hash = HashU32(hash, (uint32_t)world->nodes[index].children[0]);
		hash = HashU32(hash, (uint32_t)world->nodes[index].children[1]);
	}
	hash = HashU32(hash, world->leaf_count);
	for (index = 0U; index < world->leaf_count; index++)
	{
		hash = HashU32(hash, (uint32_t)world->leaves[index].contents);
		hash = HashU32(hash, world->leaves[index].first_leaf_brush);
		hash = HashU32(hash, world->leaves[index].leaf_brush_count);
	}
	hash = HashU32(hash, world->leaf_brush_count);
	for (index = 0U; index < world->leaf_brush_count; index++)
		hash = HashU32(hash, world->leaf_brushes[index]);
	hash = HashU32(hash, world->model_count);
	for (index = 0U; index < world->model_count; index++)
	{
		hash = HashU32(hash, (uint32_t)world->models[index].headnode);
		for (axis = 0U; axis < 3U; axis++)
		{
			hash = HashFloat(hash, world->models[index].mins.value[axis]);
			hash = HashFloat(hash, world->models[index].maxs.value[axis]);
			hash = HashFloat(hash, world->models[index].origin.value[axis]);
		}
	}
	hash = HashU32(hash, world->brush_count);
	for (index = 0U; index < world->brush_count; index++)
	{
		hash = HashU32(hash, world->brushes[index].first_side);
		hash = HashU32(hash, world->brushes[index].side_count);
		hash = HashU32(hash, (uint32_t)world->brushes[index].contents);
	}
	hash = HashU32(hash, world->brush_side_count);
	for (index = 0U; index < world->brush_side_count; index++)
	{
		hash = HashU32(hash, world->brush_sides[index].plane);
		hash = HashU32(hash, (uint32_t)world->brush_sides[index].texinfo);
	}
	hash = HashU32(hash, world->texinfo_count);
	for (index = 0U; index < world->texinfo_count; index++)
		hash = HashU32(hash, (uint32_t)world->texinfos[index].flags);
	return hash;
}

static uint64_t HashScene(uint64_t hash,
	const sg_host_collision_scene_t *scene)
{
	size_t index;
	uint32_t axis;

	if (!scene)
		return HashU64(hash, 0U);
	hash = HashU64(hash, (uint64_t)scene->instance_count);
	for (index = 0U; index < scene->instance_count; index++)
	{
		hash = HashU64(hash, scene->instances[index].instance_id);
		hash = HashU32(hash, scene->instances[index].model_index);
		for (axis = 0U; axis < 3U; axis++)
		{
			hash = HashFloat(hash,
				scene->instances[index].transform.origin[axis]);
			hash = HashFloat(hash,
				scene->instances[index].transform.angles[axis]);
		}
	}
	return hash;
}

uint64_t SG_HookVisibilityFeasibilitySourceDigest(
	const sg_hook_visibility_feasibility_sources_t *sources)
{
	uint64_t hash = HOOK_FNV_OFFSET;
	uint32_t index, axis;
	const sg_hook_visibility_fire_law_t *law = &sources->fire_law;

	hash = HashIdentity(hash, &sources->collision->identity);
	hash = HashWorld(hash, sources->collision->world);
	hash = HashScene(hash, sources->scene);
	for (axis = 0U; axis < 3U; axis++)
	{
		hash = HashU32(hash, (uint16_t)sources->origins.mins[axis]);
		hash = HashU32(hash, (uint16_t)sources->origins.maxs[axis]);
	}
	hash = HashU32(hash, (uint32_t)sources->stance);
	hash = HashU32(hash, sources->control_count);
	for (index = 0U; index < sources->control_count; index++)
	{
		hash = HashU32(hash, (uint16_t)sources->controls[index].pitch_min);
		hash = HashU32(hash, (uint16_t)sources->controls[index].pitch_max);
		hash = HashU32(hash, (uint16_t)sources->controls[index].yaw_min);
		hash = HashU32(hash, (uint16_t)sources->controls[index].yaw_max);
	}
	hash = HashU32(hash, sources->surface_rule_count);
	for (index = 0U; index < sources->surface_rule_count; index++)
	{
		const sg_hook_visibility_surface_rule_t *rule =
			&sources->surface_rules[index];

		hash = HashU64(hash, rule->surface_id);
		hash = HashU32(hash, rule->model_index);
		hash = HashU32(hash, rule->brush_index);
		hash = HashU32(hash, rule->texinfo);
		hash = HashU32(hash, (uint32_t)rule->classification);
	}
	hash = HashU64(hash, law->identity);
	hash = HashU64(hash, law->angle_authority_id);
	hash = HashU64(hash, law->mover_domain_identity);
	hash = HashFloat(hash, law->standing_view_height);
	hash = HashFloat(hash, law->crouching_view_height);
	hash = HashFloat(hash, law->muzzle_forward);
	hash = HashFloat(hash, law->muzzle_lateral);
	hash = HashFloat(hash, law->maximum_range);
	hash = HashFloat(hash, law->trace_epsilon);
	hash = HashU32(hash, law->shot_mask);
	hash = HashU32(hash, law->moving_model_count);
	hash = HashU64(hash, sources->producer_identity);
	hash = HashU64(hash, sources->verifier_identity);
	return hash;
}

void SG_HookVisibilityFeasibilityShortSinCos(int16_t code, float *sine_out,
	float *cosine_out)
{
	float degrees = (float)((double)(uint16_t)code * (360.0 / 65536.0));
	float radians = (float)((double)degrees * (HOOK_PI * 2.0 / 360.0));

	*sine_out = (float)sin((double)radians);
	*cosine_out = (float)cos((double)radians);
}

void SG_HookVisibilityFeasibilityAngleBits(uint16_t code,
	uint32_t *sine_bits_out, uint32_t *cosine_bits_out)
{
	float sine, cosine;

	SG_HookVisibilityFeasibilityShortSinCos((int16_t)code, &sine, &cosine);
	if (sine_bits_out)
		memcpy(sine_bits_out, &sine, sizeof(*sine_bits_out));
	if (cosine_bits_out)
		memcpy(cosine_bits_out, &cosine, sizeof(*cosine_bits_out));
}

static uint64_t AngleAuthorityId(void)
{
	uint64_t hash = HOOK_FNV_OFFSET;
	uint32_t code;

	for (code = 0U; code <= UINT16_MAX; code++)
	{
		float sine, cosine;
		uint32_t sine_bits, cosine_bits;

		SG_HookVisibilityFeasibilityShortSinCos((int16_t)(uint16_t)code,
			&sine, &cosine);
		memcpy(&sine_bits, &sine, sizeof(sine_bits));
		memcpy(&cosine_bits, &cosine, sizeof(cosine_bits));
		hash = HashU32(hash, code);
		hash = HashU32(hash, sine_bits);
		hash = HashU32(hash, cosine_bits);
	}
	return hash;
}

static int ControlSupported(const sg_hook_visibility_control_root_t *control)
{
	int forward;
	int reverse;

	if (control->pitch_min > control->pitch_max ||
		control->yaw_min > control->yaw_max ||
		(control->pitch_min != control->pitch_max &&
		 control->yaw_min != control->yaw_max))
		return 0;
	forward = control->pitch_min >= -1 && control->pitch_max <= 1 &&
		control->yaw_min >= -1 && control->yaw_max <= 1;
	reverse = control->pitch_min >= -1 && control->pitch_max <= 1 &&
		control->yaw_min >= 32766;
	return forward || reverse;
}

static int ControlsOverlap(const sg_hook_visibility_control_root_t *left,
	const sg_hook_visibility_control_root_t *right)
{
	return left->pitch_min <= right->pitch_max &&
		right->pitch_min <= left->pitch_max &&
		left->yaw_min <= right->yaw_max &&
		right->yaw_min <= left->yaw_max;
}

static int SourcesValid(sg_hook_visibility_build_context_t *build)
{
	const sg_hook_visibility_feasibility_sources_t *sources = build->sources;
	const sg_bsp_world_t *world;
	uint32_t axis, first, second;
	const sg_hook_visibility_fire_law_t *law;

	if (!sources || !sources->collision || !sources->collision->world ||
		!build->catalog || !sources->controls || !sources->control_count ||
		!sources->surface_rules || !sources->surface_rule_count)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_ARGUMENT,
			0U);
		return 0;
	}
	world = sources->collision->world;
	law = &sources->fire_law;
	if (sources->control_count >
			SG_HOOK_VISIBILITY_FEASIBILITY_MAX_CONTROL_ROOTS ||
		sources->surface_rule_count >
			SG_HOOK_VISIBILITY_FEASIBILITY_MAX_SURFACE_RULES)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW, 0U);
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
		if (sources->origins.mins[axis] > sources->origins.maxs[axis])
		{
			SetError(build,
				SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE, axis);
			return 0;
		}
	if (sources->stance != SG_RUNE_STANCE_STANDING &&
		sources->stance != SG_RUNE_STANCE_CROUCHING)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE,
			3U);
		return 0;
	}
	if (!law->identity || law->angle_authority_id !=
			SG_HOOK_VISIBILITY_ANGLE_AUTHORITY_ID ||
		law->standing_view_height != HOOK_STANDING_VIEW_HEIGHT ||
		law->crouching_view_height != HOOK_CROUCHING_VIEW_HEIGHT ||
		law->muzzle_forward != HOOK_MUZZLE_FORWARD ||
		law->muzzle_lateral != HOOK_MUZZLE_LATERAL ||
		law->trace_epsilon != HOOK_TRACE_EPSILON ||
		law->shot_mask != SG_HOOK_VISIBILITY_MASK_SHOT ||
		!isfinite(law->maximum_range) || law->maximum_range <= 0.0f)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE,
			4U);
		return 0;
	}
	if (law->maximum_range * 8.0f > (float)INT32_MAX ||
		law->maximum_range * 8.0f !=
			(float)(int32_t)(law->maximum_range * 8.0f))
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED, 6U);
		return 0;
	}
	if (law->moving_model_count && !law->mover_domain_identity)
	{
		SetError(build,
			SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_MOVING_MODEL_AUTHORITY, 0U);
		return 0;
	}
	if (law->moving_model_count || (sources->scene &&
		sources->scene->instance_count))
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED, 0U);
		return 0;
	}
	if (!sources->producer_identity || !sources->verifier_identity ||
		sources->producer_identity == sources->verifier_identity)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE,
			5U);
		return 0;
	}
	for (first = 0U; first < sources->control_count; first++)
	{
		if (!ControlSupported(&sources->controls[first]))
		{
			SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED,
				first);
			return 0;
		}
		for (second = first + 1U; second < sources->control_count; second++)
			if (ControlsOverlap(&sources->controls[first],
					&sources->controls[second]))
			{
				SetError(build,
					SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE,
					second);
				return 0;
			}
	}
	for (first = 0U; first < sources->surface_rule_count; first++)
	{
		const sg_hook_visibility_surface_rule_t *rule =
			&sources->surface_rules[first];

		if (!world->brushes || !world->brush_sides || !world->planes ||
			!world->texinfos || !rule->surface_id || rule->model_index != 0U ||
			rule->brush_index >= world->brush_count ||
			rule->texinfo >= world->texinfo_count ||
			rule->classification > SG_HOOK_VISIBILITY_SURFACE_SKY)
		{
			SetError(build,
				SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE, first);
			return 0;
		}
		{
			const sg_bsp_brush_t *brush = &world->brushes[rule->brush_index];
			uint32_t positive_axes[3] = {0U, 0U, 0U};
			uint32_t negative_axes[3] = {0U, 0U, 0U};
			uint32_t side_offset;

			if (brush->side_count != 6U)
			{
				SetError(build,
					SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED, first);
				return 0;
			}
			if (brush->first_side > world->brush_side_count ||
				brush->side_count >
					world->brush_side_count - brush->first_side)
			{
				SetError(build,
					SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE, first);
				return 0;
			}
			for (side_offset = 0U; side_offset < brush->side_count;
				side_offset++)
			{
				const sg_bsp_brush_side_t *side =
					&world->brush_sides[brush->first_side + side_offset];
				const sg_bsp_plane_t *plane;
				float distance_q8;
				uint32_t nonzero = 0U;
				uint32_t component;

				if (side->plane >= world->plane_count || side->texinfo < 0 ||
					(uint32_t)side->texinfo != rule->texinfo)
				{
					SetError(build,
						SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED,
						first);
					return 0;
				}
				plane = &world->planes[side->plane];
				distance_q8 = plane->distance * 8.0f;
				if (!isfinite(distance_q8) ||
					distance_q8 != truncf(distance_q8))
				{
					SetError(build,
						SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED,
						first);
					return 0;
				}
				for (component = 0U; component < 3U; component++)
					if (plane->normal.value[component] != 0.0f)
					{
						if (fabsf(plane->normal.value[component]) !=
							1.0f)
						{
							SetError(build,
								UnsupportedError,
								first);
							return 0;
						}
						if (plane->normal.value[component] > 0.0f)
							positive_axes[component]++;
						else
							negative_axes[component]++;
						nonzero++;
					}
				if (nonzero != 1U)
				{
					SetError(build,
						SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED,
						first);
					return 0;
				}
			}
			for (axis = 0U; axis < 3U; axis++)
				if (positive_axes[axis] != 1U || negative_axes[axis] != 1U)
				{
					SetError(build,
						SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED,
						first);
					return 0;
				}
		}
		for (second = first + 1U; second < sources->surface_rule_count;
			second++)
			if (sources->surface_rules[second].surface_id ==
					rule->surface_id ||
				sources->surface_rules[second].brush_index ==
					rule->brush_index ||
				sources->surface_rules[second].texinfo == rule->texinfo)
			{
				SetError(build,
					SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE,
					second);
				return 0;
			}
	}
	return 1;
}

int SG_HookVisibilityFeasibilityBuild(
	const sg_hook_visibility_feasibility_sources_t *sources,
	sg_hook_visibility_feasibility_catalog_t **catalog_out,
	sg_hook_visibility_feasibility_error_t *error_out)
{
	hook_build_t build;
	sg_hook_visibility_feasibility_audit_report_t audit;
	int success = 0;

	memset(&build, 0, sizeof(build));
	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!catalog_out || *catalog_out)
	{
		if (error_out)
			error_out->code =
				SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	build.sources = sources;
	build.catalog = calloc(1U, sizeof(*build.catalog));
	if (!build.catalog)
	{
		if (error_out)
			error_out->code =
				SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	if (!SourcesValid(&build))
		goto done;
	if (!SG_HookVisibilityFeasibilityFamilyValid(&build))
		goto done;
	if (AngleAuthorityId() != SG_HOOK_VISIBILITY_ANGLE_AUTHORITY_ID)
	{
		SetError(&build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_ANGLE_AUTHORITY,
			0U);
		goto done;
	}
	build.catalog->metrics.angle_authority_entries = UINT16_MAX + UINT64_C(1);
	if (!SG_HookVisibilityFeasibilityConstruct(&build))
		goto done;
	if (!SG_HookVisibilityFeasibilityAudit(sources, build.catalog, &audit))
	{
		SetError(&build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_HOST_DISAGREEMENT,
			audit.record);
		goto done;
	}
	*catalog_out = build.catalog;
	build.catalog = NULL;
	success = 1;

done:
	if (error_out)
		*error_out = build.error;
	SG_HookVisibilityFeasibilityDestroy(build.catalog);
	return success;
}

uint32_t SG_HookVisibilityFeasibilityRelationCount(
	const sg_hook_visibility_feasibility_catalog_t *catalog)
{
	if (!catalog || catalog->magic != SG_HOOK_VISIBILITY_CATALOG_MAGIC)
		return 0U;
	return catalog->relation_count;
}

int SG_HookVisibilityFeasibilityRelation(
	const sg_hook_visibility_feasibility_catalog_t *catalog, uint32_t index,
	sg_hook_visibility_relation_view_t *relation_out)
{
	const sg_hook_visibility_relation_t *relation;

	if (!catalog || catalog->magic != SG_HOOK_VISIBILITY_CATALOG_MAGIC ||
		!relation_out || index >= catalog->relation_count)
		return 0;
	relation = &catalog->relations[index];
	relation_out->surface_id = relation->surface_id;
	relation_out->model_index = relation->model_index;
	relation_out->texinfo = relation->texinfo;
	relation_out->terms = relation->terms;
	relation_out->term_count = relation->term_count;
	return 1;
}

int SG_HookVisibilityFeasibilityMetrics(
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	sg_hook_visibility_feasibility_metrics_t *metrics_out)
{
	if (!catalog || catalog->magic != SG_HOOK_VISIBILITY_CATALOG_MAGIC ||
		!metrics_out)
		return 0;
	*metrics_out = catalog->metrics;
	return 1;
}

static void WriteU16(uint8_t **cursor, uint16_t value)
{
	(*cursor)[0] = (uint8_t)value;
	(*cursor)[1] = (uint8_t)(value >> 8U);
	*cursor += 2;
}

static void WriteU32(uint8_t **cursor, uint32_t value)
{
	uint32_t byte;

	for (byte = 0U; byte < 4U; byte++)
		(*cursor)[byte] = (uint8_t)(value >> (byte * 8U));
	*cursor += 4;
}

static void WriteU64(uint8_t **cursor, uint64_t value)
{
	uint32_t byte;

	for (byte = 0U; byte < 8U; byte++)
		(*cursor)[byte] = (uint8_t)(value >> (byte * 8U));
	*cursor += 8;
}

int SG_HookVisibilityFeasibilitySerialize(
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	uint8_t **bytes_out, size_t *size_out)
{
	const size_t header_size = 24U;
	const size_t relation_size = 20U;
	const size_t term_size = 24U;
	size_t size = header_size;
	uint8_t *bytes, *cursor;
	uint32_t relation, term, axis;

	if (!catalog || catalog->magic != SG_HOOK_VISIBILITY_CATALOG_MAGIC ||
		!bytes_out || *bytes_out || !size_out)
		return 0;
	for (relation = 0U; relation < catalog->relation_count; relation++)
	{
		if (size > SIZE_MAX - relation_size ||
			catalog->relations[relation].term_count >
				(SIZE_MAX - size - relation_size) / term_size)
			return 0;
		size += relation_size +
			(size_t)catalog->relations[relation].term_count * term_size;
	}
	bytes = malloc(size);
	if (!bytes)
		return 0;
	cursor = bytes;
	WriteU64(&cursor, UINT64_C(0x454e555231465648));
	WriteU32(&cursor, 1U);
	WriteU32(&cursor, catalog->relation_count);
	WriteU64(&cursor, catalog->source_digest);
	for (relation = 0U; relation < catalog->relation_count; relation++)
	{
		const sg_hook_visibility_relation_t *record =
			&catalog->relations[relation];

		WriteU64(&cursor, record->surface_id);
		WriteU32(&cursor, record->model_index);
		WriteU32(&cursor, record->texinfo);
		WriteU32(&cursor, record->term_count);
		for (term = 0U; term < record->term_count; term++)
		{
			const sg_hook_visibility_domain_term_t *domain =
				&record->terms[term];

			for (axis = 0U; axis < 3U; axis++)
				WriteU16(&cursor, (uint16_t)domain->origins.mins[axis]);
			for (axis = 0U; axis < 3U; axis++)
				WriteU16(&cursor, (uint16_t)domain->origins.maxs[axis]);
			WriteU16(&cursor, (uint16_t)domain->pitch_min);
			WriteU16(&cursor, (uint16_t)domain->pitch_max);
			WriteU16(&cursor, (uint16_t)domain->yaw_min);
			WriteU16(&cursor, (uint16_t)domain->yaw_max);
			WriteU32(&cursor, domain->hand_mask);
		}
	}
	if ((size_t)(cursor - bytes) != size)
	{
		free(bytes);
		return 0;
	}
	*bytes_out = bytes;
	*size_out = size;
	return 1;
}

void SG_HookVisibilityFeasibilityDestroy(
	sg_hook_visibility_feasibility_catalog_t *catalog)
{
	uint32_t relation;

	if (!catalog)
		return;
	for (relation = 0U; relation < catalog->relation_count; relation++)
		free(catalog->relations[relation].terms);
	free(catalog->relations);
	free(catalog->terminals);
	free(catalog->surface_rules);
	free(catalog->controls);
	memset(catalog, 0, sizeof(*catalog));
	free(catalog);
}

const char *SG_HookVisibilityFeasibilityErrorString(
	sg_hook_visibility_feasibility_error_code_t code)
{
	switch (code)
	{
	case SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_NONE: return "none";
	case SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE:
		return "invalid source";
	case SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_SOURCE_MISMATCH:
		return "source mismatch";
	case SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_ANGLE_AUTHORITY:
		return "angle authority mismatch";
	case SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_MOVING_MODEL_AUTHORITY:
		return "moving model authority unavailable";
	case SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED:
		return "unsupported symbolic family";
	case SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_HOST_DISAGREEMENT:
		return "host disagreement";
	case SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW:
		return "representation overflow";
	case SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	default: return "unknown hook visibility feasibility error";
	}
}

const char *SG_HookVisibilityFeasibilityAuditCodeString(
	sg_hook_visibility_feasibility_audit_code_t code)
{
	switch (code)
	{
	case SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OK: return "ok";
	case SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_SOURCE_MISMATCH:
		return "source mismatch";
	case SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_PRODUCER_VERIFIER_ALIAS:
		return "producer/verifier identity alias";
	case SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_ROOT_DISAGREEMENT:
		return "legal root disagreement";
	case SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_TERMINAL_DISAGREEMENT:
		return "terminal disagreement";
	case SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_RELATION_DISAGREEMENT:
		return "relation disagreement";
	case SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_METRIC_DISAGREEMENT:
		return "metric disagreement";
	case SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_HOST_DISAGREEMENT:
		return "host disagreement";
	case SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OUT_OF_MEMORY:
		return "out of memory";
	default: return "unknown hook visibility feasibility audit error";
	}
}
