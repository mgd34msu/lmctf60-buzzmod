#include "sg_hook_visibility_feasibility_internal.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define AUDIT_FNV_OFFSET UINT64_C(1469598103934665603)
#define AUDIT_FNV_PRIME UINT64_C(1099511628211)
#define AUDIT_PI 3.14159265358979323846
typedef struct audit_evaluation_s
{
	sg_hook_visibility_terminal_outcome_t outcome;
	uint32_t surface_rule;
	sg_hook_visibility_terminal_flags_t flags;
} audit_evaluation_t;
static uint64_t AuditHashByte(uint64_t hash, uint8_t value)
{
	return (hash ^ value) * AUDIT_FNV_PRIME;
}

static uint64_t AuditHashMemory(uint64_t hash, const void *memory, size_t size)
{
	const uint8_t *bytes = memory;
	size_t index;
	for (index = 0U; index < size; index++)
		hash = AuditHashByte(hash, bytes[index]);
	return hash;
}

static uint64_t AuditHashU64(uint64_t hash, uint64_t value)
{
	uint32_t byte;
	for (byte = 0U; byte < 8U; byte++)
		hash = AuditHashByte(hash, (uint8_t)(value >> (byte * 8U)));
	return hash;
}

static uint64_t AuditHashU32(uint64_t hash, uint32_t value)
{
	uint32_t byte;

	for (byte = 0U; byte < 4U; byte++)
		hash = AuditHashByte(hash, (uint8_t)(value >> (byte * 8U)));
	return hash;
}

static uint64_t AuditHashFloat(uint64_t hash, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return AuditHashU32(hash, bits);
}

static uint64_t AuditHashIdentity(uint64_t hash,
	const sg_rune_model_identity_t *identity)
{
	uint32_t axis;

	hash = AuditHashU64(hash, identity->bsp_content_id);
	hash = AuditHashU64(hash, identity->entity_semantics_id);
	hash = AuditHashU64(hash, identity->physics_abi_id);
	hash = AuditHashU64(hash, identity->source_set_identity);
	hash = AuditHashU64(hash, identity->schema_id);
	hash = AuditHashU64(hash, identity->producer_identity);
	for (axis = 0U; axis < 3U; axis++)
	{
		hash = AuditHashFloat(hash,
			identity->standing_hull.mins.value[axis]);
		hash = AuditHashFloat(hash,
			identity->standing_hull.maxs.value[axis]);
		hash = AuditHashFloat(hash,
			identity->crouching_hull.mins.value[axis]);
		hash = AuditHashFloat(hash,
			identity->crouching_hull.maxs.value[axis]);
	}
	hash = AuditHashFloat(hash, identity->physics.gravity);
	hash = AuditHashFloat(hash, identity->physics.ground_acceleration);
	hash = AuditHashFloat(hash, identity->physics.air_acceleration);
	hash = AuditHashFloat(hash, identity->physics.water_acceleration);
	hash = AuditHashFloat(hash, identity->physics.hook_acceleration);
	hash = AuditHashFloat(hash, identity->physics.external_acceleration);
	hash = AuditHashFloat(hash, identity->physics.water_drag);
	hash = AuditHashFloat(hash, identity->physics.max_velocity);
	hash = AuditHashU32(hash, identity->physics.frame_ms);
	return AuditHashU32(hash, identity->physics.substep_ms);
}

static uint64_t AuditHashWorldCanonical(uint64_t hash,
	const sg_bsp_world_t *world)
{
	uint32_t index, axis;

	hash = AuditHashU32(hash, world->plane_count);
	for (index = 0U; index < world->plane_count; index++)
	{
		for (axis = 0U; axis < 3U; axis++)
			hash = AuditHashFloat(hash,
				world->planes[index].normal.value[axis]);
		hash = AuditHashFloat(hash, world->planes[index].distance);
		hash = AuditHashU32(hash, (uint32_t)world->planes[index].type);
	}
	hash = AuditHashU32(hash, world->node_count);
	for (index = 0U; index < world->node_count; index++)
	{
		hash = AuditHashU32(hash, world->nodes[index].plane);
		hash = AuditHashU32(hash, (uint32_t)world->nodes[index].children[0]);
		hash = AuditHashU32(hash, (uint32_t)world->nodes[index].children[1]);
	}
	hash = AuditHashU32(hash, world->leaf_count);
	for (index = 0U; index < world->leaf_count; index++)
	{
		hash = AuditHashU32(hash, (uint32_t)world->leaves[index].contents);
		hash = AuditHashU32(hash, world->leaves[index].first_leaf_brush);
		hash = AuditHashU32(hash, world->leaves[index].leaf_brush_count);
	}
	hash = AuditHashU32(hash, world->leaf_brush_count);
	for (index = 0U; index < world->leaf_brush_count; index++)
		hash = AuditHashU32(hash, world->leaf_brushes[index]);
	hash = AuditHashU32(hash, world->model_count);
	for (index = 0U; index < world->model_count; index++)
	{
		hash = AuditHashU32(hash, (uint32_t)world->models[index].headnode);
		for (axis = 0U; axis < 3U; axis++)
		{
			hash = AuditHashFloat(hash,
				world->models[index].mins.value[axis]);
			hash = AuditHashFloat(hash,
				world->models[index].maxs.value[axis]);
			hash = AuditHashFloat(hash,
				world->models[index].origin.value[axis]);
		}
	}
	hash = AuditHashU32(hash, world->brush_count);
	for (index = 0U; index < world->brush_count; index++)
	{
		hash = AuditHashU32(hash, world->brushes[index].first_side);
		hash = AuditHashU32(hash, world->brushes[index].side_count);
		hash = AuditHashU32(hash, (uint32_t)world->brushes[index].contents);
	}
	hash = AuditHashU32(hash, world->brush_side_count);
	for (index = 0U; index < world->brush_side_count; index++)
	{
		hash = AuditHashU32(hash, world->brush_sides[index].plane);
		hash = AuditHashU32(hash, (uint32_t)world->brush_sides[index].texinfo);
	}
	hash = AuditHashU32(hash, world->texinfo_count);
	for (index = 0U; index < world->texinfo_count; index++)
		hash = AuditHashU32(hash, (uint32_t)world->texinfos[index].flags);
	return hash;
}

static uint64_t AuditHashSceneCanonical(uint64_t hash,
	const sg_host_collision_scene_t *scene)
{
	size_t index;
	uint32_t axis;

	if (!scene)
		return AuditHashU64(hash, 0U);
	hash = AuditHashU64(hash, (uint64_t)scene->instance_count);
	for (index = 0U; index < scene->instance_count; index++)
	{
		hash = AuditHashU64(hash, scene->instances[index].instance_id);
		hash = AuditHashU32(hash, scene->instances[index].model_index);
		for (axis = 0U; axis < 3U; axis++)
		{
			hash = AuditHashFloat(hash,
				scene->instances[index].transform.origin[axis]);
			hash = AuditHashFloat(hash,
				scene->instances[index].transform.angles[axis]);
		}
	}
	return hash;
}

static uint64_t AuditCatalogSourceDigest(
	const sg_hook_visibility_feasibility_sources_t *sources)
{
	const sg_hook_visibility_fire_law_t *law = &sources->fire_law;
	uint64_t hash = AUDIT_FNV_OFFSET;
	uint32_t index, axis;

	hash = AuditHashIdentity(hash, &sources->collision->identity);
	hash = AuditHashWorldCanonical(hash, sources->collision->world);
	hash = AuditHashSceneCanonical(hash, sources->scene);
	for (axis = 0U; axis < 3U; axis++)
	{
		hash = AuditHashU32(hash, (uint16_t)sources->origins.mins[axis]);
		hash = AuditHashU32(hash, (uint16_t)sources->origins.maxs[axis]);
	}
	hash = AuditHashU32(hash, (uint32_t)sources->stance);
	hash = AuditHashU32(hash, sources->control_count);
	for (index = 0U; index < sources->control_count; index++)
	{
		hash = AuditHashU32(hash, (uint16_t)sources->controls[index].pitch_min);
		hash = AuditHashU32(hash, (uint16_t)sources->controls[index].pitch_max);
		hash = AuditHashU32(hash, (uint16_t)sources->controls[index].yaw_min);
		hash = AuditHashU32(hash, (uint16_t)sources->controls[index].yaw_max);
	}
	hash = AuditHashU32(hash, sources->surface_rule_count);
	for (index = 0U; index < sources->surface_rule_count; index++)
	{
		const sg_hook_visibility_surface_rule_t *rule =
			&sources->surface_rules[index];

		hash = AuditHashU64(hash, rule->surface_id);
		hash = AuditHashU32(hash, rule->model_index);
		hash = AuditHashU32(hash, rule->brush_index);
		hash = AuditHashU32(hash, rule->texinfo);
		hash = AuditHashU32(hash, (uint32_t)rule->classification);
	}
	hash = AuditHashU64(hash, law->identity);
	hash = AuditHashU64(hash, law->angle_authority_id);
	hash = AuditHashU64(hash, law->mover_domain_identity);
	hash = AuditHashFloat(hash, law->standing_view_height);
	hash = AuditHashFloat(hash, law->crouching_view_height);
	hash = AuditHashFloat(hash, law->muzzle_forward);
	hash = AuditHashFloat(hash, law->muzzle_lateral);
	hash = AuditHashFloat(hash, law->maximum_range);
	hash = AuditHashFloat(hash, law->trace_epsilon);
	hash = AuditHashU32(hash, law->shot_mask);
	hash = AuditHashU32(hash, law->moving_model_count);
	hash = AuditHashU64(hash, sources->producer_identity);
	return AuditHashU64(hash, sources->verifier_identity);
}

static uint64_t AuditSourceDigest(
	const sg_hook_visibility_feasibility_sources_t *sources)
{
	const sg_bsp_world_t *world = sources->collision->world;
	uint64_t hash = AuditHashMemory(AUDIT_FNV_OFFSET,
		&sources->collision->identity, sizeof(sources->collision->identity));
	hash = AuditHashMemory(hash, world->planes,
		(size_t)world->plane_count * sizeof(*world->planes));
	hash = AuditHashMemory(hash, world->nodes,
		(size_t)world->node_count * sizeof(*world->nodes));
	hash = AuditHashMemory(hash, world->leaves,
		(size_t)world->leaf_count * sizeof(*world->leaves));
	hash = AuditHashMemory(hash, world->leaf_brushes,
		(size_t)world->leaf_brush_count * sizeof(*world->leaf_brushes));
	hash = AuditHashMemory(hash, world->models,
		(size_t)world->model_count * sizeof(*world->models));
	hash = AuditHashMemory(hash, world->brushes,
		(size_t)world->brush_count * sizeof(*world->brushes));
	hash = AuditHashMemory(hash, world->brush_sides,
		(size_t)world->brush_side_count * sizeof(*world->brush_sides));
	hash = AuditHashMemory(hash, world->texinfos,
		(size_t)world->texinfo_count * sizeof(*world->texinfos));
	hash = AuditHashMemory(hash, &sources->origins, sizeof(sources->origins));
	hash = AuditHashMemory(hash, &sources->stance, sizeof(sources->stance));
	hash = AuditHashMemory(hash, sources->controls,
		(size_t)sources->control_count * sizeof(*sources->controls));
	hash = AuditHashMemory(hash, sources->surface_rules,
		(size_t)sources->surface_rule_count * sizeof(*sources->surface_rules));
	hash = AuditHashMemory(hash, &sources->fire_law, sizeof(sources->fire_law));
	hash = AuditHashU64(hash, sources->producer_identity);
	hash = AuditHashU64(hash, sources->verifier_identity);
	return hash;
}

static void AuditShortSinCos(int16_t code, float *sine_out,
	float *cosine_out)
{
	float degrees = (float)((double)(uint16_t)code * (360.0 / 65536.0));
	float radians = (float)((double)degrees * (AUDIT_PI * 2.0 / 360.0));

	*sine_out = (float)sin((double)radians);
	*cosine_out = (float)cos((double)radians);
}

static uint64_t AuditAngleAuthorityId(void)
{
	uint64_t hash = AUDIT_FNV_OFFSET;
	uint32_t code;
	for (code = 0U; code <= UINT16_MAX; code++)
	{
		float sine, cosine;
		uint32_t sine_bits, cosine_bits, byte;

		AuditShortSinCos((int16_t)(uint16_t)code, &sine, &cosine);
		memcpy(&sine_bits, &sine, sizeof(sine_bits));
		memcpy(&cosine_bits, &cosine, sizeof(cosine_bits));
		for (byte = 0U; byte < 4U; byte++)
			hash = AuditHashByte(hash, (uint8_t)(code >> (byte * 8U)));
		for (byte = 0U; byte < 4U; byte++)
			hash = AuditHashByte(hash,
				(uint8_t)(sine_bits >> (byte * 8U)));
		for (byte = 0U; byte < 4U; byte++)
			hash = AuditHashByte(hash,
				(uint8_t)(cosine_bits >> (byte * 8U)));
	}
	return hash;
}

static int Multiply(uint64_t left, uint64_t right, uint64_t *result)
{
	if (right && left > UINT64_MAX / right)
		return 0;
	*result = left * right;
	return 1;
}

static int BoxContains(const sg_hook_visibility_q8_box_t *outer,
	const sg_hook_visibility_q8_box_t *inner)
{
	uint32_t axis;
	for (axis = 0U; axis < 3U; axis++)
		if (inner->mins[axis] < outer->mins[axis] ||
			inner->maxs[axis] > outer->maxs[axis] ||
			inner->mins[axis] > inner->maxs[axis])
			return 0;
	return 1;
}

static int ControlContains(const sg_hook_visibility_control_root_t *root,
	const sg_hook_visibility_domain_term_t *domain)
{
	return domain->pitch_min >= root->pitch_min &&
		domain->pitch_max <= root->pitch_max &&
		domain->yaw_min >= root->yaw_min &&
		domain->yaw_max <= root->yaw_max &&
		domain->pitch_min <= domain->pitch_max &&
		domain->yaw_min <= domain->yaw_max;
}

static int DomainCardinality(const sg_hook_visibility_domain_term_t *domain,
	uint64_t *cardinality_out)
{
	uint64_t result = 1U;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (!Multiply(result,
				(uint64_t)((int32_t)domain->origins.maxs[axis] -
				domain->origins.mins[axis] + 1), &result))
			return 0;
	if (!Multiply(result,
			(uint64_t)((int32_t)domain->pitch_max - domain->pitch_min + 1),
			&result) ||
		!Multiply(result,
			(uint64_t)((int32_t)domain->yaw_max - domain->yaw_min + 1),
			&result))
		return 0;
	*cardinality_out = result;
	return 1;
}

static int DomainValid(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain)
{
	uint32_t control;
	int contained = 0;

	if (!BoxContains(&sources->origins, &domain->origins) ||
		!domain->hand_mask ||
		(domain->hand_mask & ~SG_HOOK_VISIBILITY_ALL_HANDS) != 0U ||
		(domain->hand_mask & (domain->hand_mask - 1U)) != 0U)
		return 0;
	for (control = 0U; control < sources->control_count; control++)
		contained += ControlContains(&sources->controls[control], domain);
	return contained == 1;
}

static int IntersectDomain(const sg_hook_visibility_domain_term_t *left,
	const sg_hook_visibility_domain_term_t *right,
	sg_hook_visibility_domain_term_t *intersection)
{
	uint32_t axis;

	memset(intersection, 0, sizeof(*intersection));
	intersection->hand_mask = left->hand_mask & right->hand_mask;
	if (!intersection->hand_mask)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		intersection->origins.mins[axis] =
			left->origins.mins[axis] > right->origins.mins[axis] ?
			left->origins.mins[axis] : right->origins.mins[axis];
		intersection->origins.maxs[axis] =
			left->origins.maxs[axis] < right->origins.maxs[axis] ?
			left->origins.maxs[axis] : right->origins.maxs[axis];
		if (intersection->origins.mins[axis] >
			intersection->origins.maxs[axis])
			return 0;
	}
	intersection->pitch_min = left->pitch_min > right->pitch_min ?
		left->pitch_min : right->pitch_min;
	intersection->pitch_max = left->pitch_max < right->pitch_max ?
		left->pitch_max : right->pitch_max;
	intersection->yaw_min = left->yaw_min > right->yaw_min ?
		left->yaw_min : right->yaw_min;
	intersection->yaw_max = left->yaw_max < right->yaw_max ?
		left->yaw_max : right->yaw_max;
	return intersection->pitch_min <= intersection->pitch_max &&
		intersection->yaw_min <= intersection->yaw_max;
}

static int RootCardinality(
	const sg_hook_visibility_feasibility_sources_t *sources,
	uint64_t *cardinality_out)
{
	uint64_t origins = 1U, controls = 0U, product;
	uint32_t axis, control;

	for (axis = 0U; axis < 3U; axis++)
		if (!Multiply(origins,
				(uint64_t)((int32_t)sources->origins.maxs[axis] -
				sources->origins.mins[axis] + 1), &origins))
			return 0;
	for (control = 0U; control < sources->control_count; control++)
	{
		uint64_t pitch = (uint64_t)((int32_t)
			sources->controls[control].pitch_max -
			sources->controls[control].pitch_min + 1);
		uint64_t yaw = (uint64_t)((int32_t)
			sources->controls[control].yaw_max -
			sources->controls[control].yaw_min + 1);

		if (!Multiply(pitch, yaw, &product) ||
			controls > UINT64_MAX - product)
			return 0;
		controls += product;
	}
	return Multiply(origins, controls, &product) &&
		Multiply(product, SG_HOOK_VISIBILITY_HAND_COUNT, cardinality_out);
}

static sg_hook_visibility_hand_t DomainHand(
	const sg_hook_visibility_domain_term_t *domain)
{
	uint32_t hand;

	for (hand = 0U; hand < SG_HOOK_VISIBILITY_HAND_COUNT; hand++)
		if (domain->hand_mask & SG_HOOK_VISIBILITY_HAND_BIT(hand))
			return (sg_hook_visibility_hand_t)hand;
	return SG_HOOK_VISIBILITY_HAND_COUNT;
}

static void AuditDirection(int16_t pitch, int16_t yaw, float forward[3],
	float right[3])
{
	float sine_pitch, cosine_pitch, sine_yaw, cosine_yaw;
	float sine_roll, cosine_roll;

	AuditShortSinCos(pitch, &sine_pitch, &cosine_pitch);
	AuditShortSinCos(yaw, &sine_yaw, &cosine_yaw);
	AuditShortSinCos(0, &sine_roll, &cosine_roll);
	forward[0] = cosine_pitch * cosine_yaw;
	forward[1] = cosine_pitch * sine_yaw;
	forward[2] = -sine_pitch;
	right[0] = (-1.0f * sine_roll * sine_pitch * cosine_yaw +
		-1.0f * cosine_roll * -sine_yaw);
	right[1] = (-1.0f * sine_roll * sine_pitch * sine_yaw +
		-1.0f * cosine_roll * cosine_yaw);
	right[2] = -1.0f * sine_roll * cosine_pitch;
}

static void AuditMuzzle(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const float origin[3], sg_hook_visibility_hand_t hand,
	const float forward[3], const float right[3], float muzzle[3])
{
	float lateral = sources->fire_law.muzzle_lateral;
	float view_height = sources->stance == SG_RUNE_STANCE_STANDING ?
		sources->fire_law.standing_view_height :
		sources->fire_law.crouching_view_height;
	uint32_t axis;

	if (hand == SG_HOOK_VISIBILITY_HAND_LEFT)
		lateral = -lateral;
	else if (hand == SG_HOOK_VISIBILITY_HAND_CENTER)
		lateral = 0.0f;
	for (axis = 0U; axis < 3U; axis++)
		muzzle[axis] = origin[axis] +
			forward[axis] * sources->fire_law.muzzle_forward +
			right[axis] * lateral;
	muzzle[2] += view_height - sources->fire_law.muzzle_forward;
}

static int AuditPointInBrush(const sg_bsp_world_t *world,
	const sg_bsp_brush_t *brush, const float point[3])
{
	uint32_t side_offset;

	for (side_offset = 0U; side_offset < brush->side_count; side_offset++)
	{
		const sg_bsp_brush_side_t *side =
			&world->brush_sides[brush->first_side + side_offset];
		const sg_bsp_plane_t *plane = &world->planes[side->plane];
		float dot = point[0] * plane->normal.value[0] +
			point[1] * plane->normal.value[1] +
			point[2] * plane->normal.value[2];

		if (dot > plane->distance + 0.00001f)
			return 0;
	}
	return 1;
}

static sg_hook_visibility_terminal_flags_t AuditBoundaryFlags(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain, const float muzzle[3],
	const float direction[3], const sg_host_collision_trace_t *trace)
{
	float denominator = direction[0] * trace->plane.normal[0] +
		direction[1] * trace->plane.normal[1] +
		direction[2] * trace->plane.normal[2];
	float numerator = trace->plane.distance -
		(muzzle[0] * trace->plane.normal[0] +
		 muzzle[1] * trace->plane.normal[1] +
		 muzzle[2] * trace->plane.normal[2]);
	float point[3];
	uint32_t rule, axis, coincidences = 0U;
	sg_hook_visibility_terminal_flags_t flags = 0U;

	if (domain->origins.mins[0] == domain->origins.maxs[0] ||
		domain->origins.mins[1] == domain->origins.maxs[1] ||
		domain->origins.mins[2] == domain->origins.maxs[2])
		flags |= SG_HOOK_VISIBILITY_TERMINAL_LOWER_DIMENSIONAL;
	if (denominator == 0.0f)
		return flags;
	for (axis = 0U; axis < 3U; axis++)
		point[axis] = muzzle[axis] + direction[axis] *
			(numerator / denominator);
	for (rule = 0U; rule < sources->surface_rule_count; rule++)
		if (AuditPointInBrush(sources->collision->world,
				&sources->collision->world->brushes[
					sources->surface_rules[rule].brush_index], point))
			coincidences++;
	if (coincidences >= 2U)
		flags |= SG_HOOK_VISIBILITY_TERMINAL_EDGE |
			SG_HOOK_VISIBILITY_TERMINAL_TIE;
	if (coincidences >= 4U)
		flags |= SG_HOOK_VISIBILITY_TERMINAL_VERTEX;
	return flags;
}

static uint32_t AuditFindRule(
	const sg_hook_visibility_feasibility_sources_t *sources, uint32_t model,
	uint32_t texinfo)
{
	uint32_t rule;

	for (rule = 0U; rule < sources->surface_rule_count; rule++)
		if (sources->surface_rules[rule].model_index == model &&
			sources->surface_rules[rule].texinfo == texinfo)
			return rule;
	return UINT32_MAX;
}

static int AuditEvaluate(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain,
	audit_evaluation_t *evaluation)
{
	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	float origin[3], forward[3], right[3], muzzle[3], end[3];
	sg_host_collision_trace_t clearance, hit;
	int16_t pitch = (int16_t)(((int32_t)domain->pitch_min +
		domain->pitch_max) / 2);
	int16_t yaw = (int16_t)(((int32_t)domain->yaw_min + domain->yaw_max) / 2);
	uint32_t axis;

	memset(evaluation, 0, sizeof(*evaluation));
	evaluation->surface_rule = UINT32_MAX;
	for (axis = 0U; axis < 3U; axis++)
		origin[axis] = (float)(((int32_t)domain->origins.mins[axis] +
			domain->origins.maxs[axis]) / 2) * 0.125f;
	AuditDirection(pitch, yaw, forward, right);
	AuditMuzzle(sources, origin, DomainHand(domain), forward, right, muzzle);
	if (!SG_HostCollisionTrace(sources->collision, sources->scene, origin, zero,
		zero, muzzle, sources->fire_law.shot_mask, &clearance))
		return 0;
	if (clearance.startsolid || clearance.allsolid || clearance.fraction < 1.0f)
	{
		evaluation->outcome =
			SG_HOOK_VISIBILITY_TERMINAL_CLEARANCE_BLOCKED;
		return 1;
	}
	for (axis = 0U; axis < 3U; axis++)
		end[axis] = muzzle[axis] + forward[axis] *
			sources->fire_law.maximum_range;
	if (!SG_HostCollisionTrace(sources->collision, sources->scene, muzzle, zero,
		zero, end, sources->fire_law.shot_mask, &hit))
		return 0;
	if (hit.fraction == 1.0f)
	{
		evaluation->outcome = SG_HOOK_VISIBILITY_TERMINAL_NO_HIT;
		return 1;
	}
	evaluation->surface_rule = AuditFindRule(sources, hit.model_index,
		hit.texinfo);
	if (evaluation->surface_rule == UINT32_MAX)
		return 0;
	if (hit.surface_flags & SG_HOST_SURFACE_SKY)
		evaluation->outcome = SG_HOOK_VISIBILITY_TERMINAL_SKY;
	else if (sources->surface_rules[evaluation->surface_rule].classification ==
		SG_HOOK_VISIBILITY_SURFACE_HOOKABLE)
		evaluation->outcome = SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE;
	else if (sources->surface_rules[evaluation->surface_rule].classification ==
		SG_HOOK_VISIBILITY_SURFACE_NONHOOKABLE)
		evaluation->outcome = SG_HOOK_VISIBILITY_TERMINAL_NONHOOKABLE;
	else
		return 0;
	evaluation->flags = AuditBoundaryFlags(sources, domain, muzzle, forward,
		&hit);
	return 1;
}

static int RelationSurfaceRule(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_relation_t *relation, uint32_t *rule_out)
{
	uint32_t rule;

	for (rule = 0U; rule < sources->surface_rule_count; rule++)
		if (sources->surface_rules[rule].surface_id == relation->surface_id &&
			sources->surface_rules[rule].model_index == relation->model_index &&
			sources->surface_rules[rule].texinfo == relation->texinfo &&
			sources->surface_rules[rule].classification ==
				SG_HOOK_VISIBILITY_SURFACE_HOOKABLE)
		{
			*rule_out = rule;
			return 1;
		}
	return 0;
}

static int VerifyRelations(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	uint32_t *bad_record)
{
	uint32_t relation, term, other_relation, other_term, terminal;

	for (relation = 0U; relation < catalog->relation_count; relation++)
	{
		const sg_hook_visibility_relation_t *record =
			&catalog->relations[relation];
		uint32_t surface_rule;

		if (!record->term_count || !record->terms ||
			(relation && catalog->relations[relation - 1U].surface_id >=
				record->surface_id) ||
			!RelationSurfaceRule(sources, record, &surface_rule))
		{
			*bad_record = relation;
			return 0;
		}
		for (term = 0U; term < record->term_count; term++)
		{
			uint64_t relation_cardinality, covered = 0U;

			if (!DomainValid(sources, &record->terms[term]) ||
				!DomainCardinality(&record->terms[term],
					&relation_cardinality))
			{
				*bad_record = term;
				return 0;
			}
			for (terminal = 0U; terminal < catalog->terminal_count; terminal++)
			{
				sg_hook_visibility_domain_term_t intersection;
				uint64_t intersection_cardinality;
				const sg_hook_visibility_terminal_t *candidate =
					&catalog->terminals[terminal];

				if (!IntersectDomain(&record->terms[term], &candidate->domain,
						&intersection))
					continue;
				if (candidate->outcome != SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE ||
					candidate->surface_rule != surface_rule ||
					!DomainCardinality(&intersection,
						&intersection_cardinality) ||
					covered > UINT64_MAX - intersection_cardinality)
				{
					*bad_record = term;
					return 0;
				}
				covered += intersection_cardinality;
			}
			if (covered != relation_cardinality)
			{
				*bad_record = term;
				return 0;
			}
		}
	}
	for (relation = 0U; relation < catalog->relation_count; relation++)
		for (term = 0U; term < catalog->relations[relation].term_count; term++)
			for (other_relation = relation;
				other_relation < catalog->relation_count; other_relation++)
				for (other_term = other_relation == relation ? term + 1U : 0U;
					other_term < catalog->relations[other_relation].term_count;
					other_term++)
				{
					sg_hook_visibility_domain_term_t intersection;
					const sg_hook_visibility_domain_term_t *other_domain =
						&catalog->relations[other_relation].terms[
							other_term];

					if (IntersectDomain(
							&catalog->relations[relation].terms[term],
							other_domain, &intersection))
					{
						*bad_record = term;
						return 0;
					}
				}
	for (terminal = 0U; terminal < catalog->terminal_count; terminal++)
		if (catalog->terminals[terminal].outcome ==
			SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE)
		{
			uint64_t terminal_cardinality, covered = 0U;
			uint32_t expected_rule = catalog->terminals[terminal].surface_rule;

			if (!DomainCardinality(&catalog->terminals[terminal].domain,
					&terminal_cardinality))
				return 0;
			for (relation = 0U; relation < catalog->relation_count; relation++)
			{
				uint32_t rule;

				if (!RelationSurfaceRule(sources, &catalog->relations[relation],
						&rule) || rule != expected_rule)
					continue;
				for (term = 0U; term < catalog->relations[relation].term_count;
					term++)
				{
					sg_hook_visibility_domain_term_t intersection;
					uint64_t amount;

					if (IntersectDomain(&catalog->terminals[terminal].domain,
							&catalog->relations[relation].terms[term],
							&intersection) &&
						DomainCardinality(&intersection, &amount))
						covered += amount;
				}
			}
			if (covered != terminal_cardinality)
			{
				*bad_record = terminal;
				return 0;
			}
		}
	return 1;
}

int SG_HookVisibilityFeasibilityAudit(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	sg_hook_visibility_feasibility_audit_report_t *report_out)
{
	sg_hook_visibility_feasibility_audit_report_t report;
	const sg_bsp_world_t *world;
	uint64_t root_cardinality, terminal_cardinality = 0U;
	uint32_t terminal, relation_terms = 0U, relation;
	int tiling;

	memset(&report, 0, sizeof(report));
	report.code = SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_INVALID_ARGUMENT;
	if (!report_out)
		return 0;
	if (!sources || !sources->collision || !sources->collision->world ||
		!sources->controls || !sources->surface_rules || !catalog ||
		catalog->magic != SG_HOOK_VISIBILITY_CATALOG_MAGIC)
	{
		*report_out = report;
		return 0;
	}
	world = sources->collision->world;
	report.producer_identity = catalog->producer_identity;
	report.verifier_identity = sources->verifier_identity;
	if (!sources->producer_identity || !sources->verifier_identity ||
		sources->producer_identity == sources->verifier_identity ||
		catalog->producer_identity == catalog->verifier_identity)
	{
		report.code =
			SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_PRODUCER_VERIFIER_ALIAS;
		*report_out = report;
		return 0;
	}
	if (sources->control_count >
			SG_HOOK_VISIBILITY_FEASIBILITY_MAX_CONTROL_ROOTS ||
		sources->surface_rule_count >
			SG_HOOK_VISIBILITY_FEASIBILITY_MAX_SURFACE_RULES ||
		!SG_HookVisibilityFeasibilityAuditFamilyValid(sources) ||
		catalog->control_count != sources->control_count ||
		catalog->surface_rule_count != sources->surface_rule_count ||
		catalog->world_counts[0] != world->plane_count ||
		catalog->world_counts[1] != world->node_count ||
		catalog->world_counts[2] != world->leaf_count ||
		catalog->world_counts[3] != world->leaf_brush_count ||
		catalog->world_counts[4] != world->model_count ||
		catalog->world_counts[5] != world->brush_count ||
		catalog->world_counts[6] != world->brush_side_count ||
		catalog->world_counts[7] != world->texinfo_count ||
		catalog->producer_identity != sources->producer_identity ||
		catalog->verifier_identity != sources->verifier_identity ||
		catalog->source_digest != AuditCatalogSourceDigest(sources) ||
		catalog->verifier_source_digest != AuditSourceDigest(sources) ||
		AuditAngleAuthorityId() != SG_HOOK_VISIBILITY_ANGLE_AUTHORITY_ID)
	{
		report.code = SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_SOURCE_MISMATCH;
		*report_out = report;
		return 0;
	}
	if (!RootCardinality(sources, &root_cardinality) ||
		root_cardinality != catalog->metrics.legal_action_tuples)
	{
		report.code = SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_ROOT_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	report.reconstructed_action_tuples = root_cardinality;
	tiling = SG_HookVisibilityFeasibilityAuditTiling(sources, catalog,
		&terminal_cardinality);
	if (tiling <= 0)
	{
		report.code = tiling < 0 ?
			SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OUT_OF_MEMORY :
			SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_ROOT_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	for (terminal = 0U; terminal < catalog->terminal_count; terminal++)
	{
		const sg_hook_visibility_terminal_t *record =
			&catalog->terminals[terminal];
		audit_evaluation_t evaluation;

		if (!DomainValid(sources, &record->domain) ||
			!SG_HookVisibilityFeasibilityAuditDomainUniform(sources,
				&record->domain))
		{
			report.code =
				SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_ROOT_DISAGREEMENT;
			report.record = terminal;
			*report_out = report;
			return 0;
		}
		if (!AuditEvaluate(sources, &record->domain, &evaluation))
		{
			report.code =
				SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_HOST_DISAGREEMENT;
			report.record = terminal;
			*report_out = report;
			return 0;
		}
		if (evaluation.outcome != record->outcome ||
			evaluation.surface_rule != record->surface_rule ||
			evaluation.flags != record->flags)
		{
			report.code =
				SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_TERMINAL_DISAGREEMENT;
			report.record = terminal;
			*report_out = report;
			return 0;
		}
		switch (evaluation.outcome)
		{
		case SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE:
			report.hookable_terms++;
			break;
		case SG_HOOK_VISIBILITY_TERMINAL_SKY:
			report.sky_terms++;
			break;
		case SG_HOOK_VISIBILITY_TERMINAL_NONHOOKABLE:
			report.nonhookable_terms++;
			break;
		case SG_HOOK_VISIBILITY_TERMINAL_NO_HIT:
			report.no_hit_terms++;
			break;
		case SG_HOOK_VISIBILITY_TERMINAL_CLEARANCE_BLOCKED:
			report.clearance_blocked_terms++;
			break;
		}
		if (evaluation.flags & SG_HOOK_VISIBILITY_TERMINAL_LOWER_DIMENSIONAL)
			report.lower_dimensional_terms++;
		if (evaluation.flags & SG_HOOK_VISIBILITY_TERMINAL_EDGE)
			report.edge_terms++;
		if (evaluation.flags & SG_HOOK_VISIBILITY_TERMINAL_VERTEX)
			report.vertex_terms++;
		if (evaluation.flags & SG_HOOK_VISIBILITY_TERMINAL_TIE)
			report.tie_terms++;
	}
	if (terminal_cardinality != root_cardinality ||
		catalog->terminal_count != catalog->metrics.predicate_domains)
	{
		report.code = SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_ROOT_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	report.reconstructed_predicate_domains = catalog->terminal_count;
	if (!VerifyRelations(sources, catalog, &report.record))
	{
		report.code =
			SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_RELATION_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	for (relation = 0U; relation < catalog->relation_count; relation++)
	{
		if (relation_terms > UINT32_MAX -
			catalog->relations[relation].term_count)
		{
			report.code = SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_METRIC_DISAGREEMENT;
			*report_out = report;
			return 0;
		}
		relation_terms += catalog->relations[relation].term_count;
	}
	if (catalog->metrics.angle_authority_entries != UINT64_C(65536) ||
		catalog->metrics.muzzle_clearance_traces != catalog->terminal_count ||
		catalog->metrics.first_hit_traces != catalog->terminal_count -
			report.clearance_blocked_terms ||
		catalog->metrics.relation_count != catalog->relation_count ||
		catalog->metrics.relation_term_count != relation_terms ||
		catalog->metrics.complement_term_count != catalog->terminal_count -
			report.hookable_terms)
	{
		report.code = SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_METRIC_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	report.code = SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OK;
	*report_out = report;
	return 1;
}
