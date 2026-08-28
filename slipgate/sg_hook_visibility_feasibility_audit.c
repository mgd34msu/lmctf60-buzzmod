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

static int EventStraddled(const sg_hook_visibility_domain_term_t *domain,
	uint32_t axis, float coordinate)
{
	float q8 = coordinate * 8.0f;
	int32_t event = (int32_t)q8;

	if (q8 != (float)event || event < domain->origins.mins[axis] ||
		event > domain->origins.maxs[axis])
		return 0;
	return domain->origins.mins[axis] != event ||
		domain->origins.maxs[axis] != event;
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

static int DomainRespectsEvents(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain)
{
	const sg_bsp_world_t *world = sources->collision->world;
	float view_height = sources->stance == SG_RUNE_STANCE_STANDING ?
		sources->fire_law.standing_view_height :
		sources->fire_law.crouching_view_height;
	sg_hook_visibility_hand_t hand = DomainHand(domain);
	float hand_shift = hand == SG_HOOK_VISIBILITY_HAND_LEFT ?
		sources->fire_law.muzzle_lateral :
		(hand == SG_HOOK_VISIBILITY_HAND_RIGHT ?
		-sources->fire_law.muzzle_lateral : 0.0f);
	uint32_t rule_index;

	if (domain->pitch_min != domain->pitch_max ||
		(domain->yaw_min != domain->yaw_max &&
		 !(domain->yaw_min >= 32766)))
		return 0;
	for (rule_index = 0U; rule_index < sources->surface_rule_count;
		rule_index++)
	{
		const sg_bsp_brush_t *brush = &world->brushes[
			sources->surface_rules[rule_index].brush_index];
		uint32_t side_offset;

		for (side_offset = 0U; side_offset < brush->side_count; side_offset++)
		{
			const sg_bsp_brush_side_t *side =
				&world->brush_sides[brush->first_side + side_offset];
			const sg_bsp_plane_t *plane = &world->planes[side->plane];
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
				if (fabsf(plane->normal.value[axis]) == 1.0f)
				{
					float coordinate = plane->distance /
						plane->normal.value[axis];

					if (axis == 0U)
					{
						float events[5];
						uint32_t event;

						events[0] = coordinate;
						events[1] = coordinate -
							sources->fire_law.muzzle_forward;
						events[2] = events[1] -
							sources->fire_law.maximum_range;
						events[3] = coordinate +
							sources->fire_law.muzzle_forward;
						events[4] = events[3] +
							sources->fire_law.maximum_range;
						for (event = 0U; event < 5U; event++)
							if (EventStraddled(domain, axis,
									events[event]))
								return 0;
					}
					else if (axis == 1U && EventStraddled(domain, axis,
						coordinate - hand_shift))
						return 0;
					else if (axis == 2U && EventStraddled(domain, axis,
						coordinate - (view_height -
							sources->fire_law.muzzle_forward)))
						return 0;
					break;
				}
		}
	}
	return 1;
}

static void AuditDirection(int16_t pitch, int16_t yaw, float forward[3],
	float right[3])
{
	float sine_pitch, cosine_pitch, sine_yaw, cosine_yaw;

	AuditShortSinCos(pitch, &sine_pitch, &cosine_pitch);
	AuditShortSinCos(yaw, &sine_yaw, &cosine_yaw);
	forward[0] = cosine_pitch * cosine_yaw;
	forward[1] = cosine_pitch * sine_yaw;
	forward[2] = -sine_pitch;
	right[0] = sine_yaw;
	right[1] = -cosine_yaw;
	right[2] = 0.0f;
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
	uint32_t terminal, other;

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
	for (terminal = 0U; terminal < catalog->terminal_count; terminal++)
	{
		const sg_hook_visibility_terminal_t *record =
			&catalog->terminals[terminal];
		audit_evaluation_t evaluation;
		uint64_t cardinality;

		if (!DomainValid(sources, &record->domain) ||
			!DomainRespectsEvents(sources, &record->domain) ||
			!DomainCardinality(&record->domain, &cardinality) ||
			terminal_cardinality > UINT64_MAX - cardinality)
		{
			report.code =
				SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_ROOT_DISAGREEMENT;
			report.record = terminal;
			*report_out = report;
			return 0;
		}
		terminal_cardinality += cardinality;
		for (other = 0U; other < terminal; other++)
		{
			sg_hook_visibility_domain_term_t intersection;

			if (IntersectDomain(&record->domain,
					&catalog->terminals[other].domain, &intersection))
			{
				report.code =
					SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_ROOT_DISAGREEMENT;
				report.record = terminal;
				*report_out = report;
				return 0;
			}
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
	report.code = SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OK;
	*report_out = report;
	return 1;
}
