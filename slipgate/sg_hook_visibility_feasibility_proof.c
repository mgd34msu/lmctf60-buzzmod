#include "sg_hook_visibility_feasibility_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SetError SG_HookVisibilityFeasibilitySetError
#define ShortSinCos SG_HookVisibilityFeasibilityShortSinCos
#define SourceDigest SG_HookVisibilityFeasibilitySourceDigest
#define VerifierSourceDigest SG_HookVisibilityFeasibilityVerifierSourceDigest

typedef sg_hook_visibility_build_context_t hook_build_t;

static int AllocationSize(hook_build_t *build, size_t count,
	size_t element_size, size_t *size_out)
{
	if (element_size && count > SIZE_MAX / element_size)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW, 0U);
		return 0;
	}
	*size_out = count * element_size;
	return 1;
}

static int MakeControlSpans(int16_t minimum, int16_t maximum, int yaw,
	sg_hook_visibility_i16_span_t spans[3], uint32_t *count_out)
{
	uint32_t count = 0U;
	int32_t value;

	if (yaw && minimum >= 32766)
	{
		spans[0].minimum = minimum;
		spans[0].maximum = maximum;
		*count_out = 1U;
		return 1;
	}
	for (value = minimum; value <= maximum; value++)
	{
		if (count >= 3U)
			return 0;
		spans[count].minimum = (int16_t)value;
		spans[count++].maximum = (int16_t)value;
	}
	*count_out = count;
	return 1;
}

static void Direction(int16_t pitch, int16_t yaw, float forward[3],
	float right[3])
{
	float sine_pitch, cosine_pitch, sine_yaw, cosine_yaw;

	ShortSinCos(pitch, &sine_pitch, &cosine_pitch);
	ShortSinCos(yaw, &sine_yaw, &cosine_yaw);
	forward[0] = cosine_pitch * cosine_yaw;
	forward[1] = cosine_pitch * sine_yaw;
	forward[2] = -sine_pitch;
	right[0] = sine_yaw;
	right[1] = -cosine_yaw;
	right[2] = 0.0f;
}

static void Muzzle(const hook_build_t *build, const float origin[3],
	sg_hook_visibility_hand_t hand, const float forward[3],
	const float right[3], float muzzle[3])
{
	float lateral = build->sources->fire_law.muzzle_lateral;
	float view_height = build->sources->stance == SG_RUNE_STANCE_STANDING ?
		build->sources->fire_law.standing_view_height :
		build->sources->fire_law.crouching_view_height;
	uint32_t axis;

	if (hand == SG_HOOK_VISIBILITY_HAND_LEFT)
		lateral = -lateral;
	else if (hand == SG_HOOK_VISIBILITY_HAND_CENTER)
		lateral = 0.0f;
	for (axis = 0U; axis < 3U; axis++)
		muzzle[axis] = origin[axis] +
			forward[axis] * build->sources->fire_law.muzzle_forward +
			right[axis] * lateral;
	muzzle[2] += view_height - build->sources->fire_law.muzzle_forward;
}

static uint32_t FindSurfaceRule(const hook_build_t *build, uint32_t model,
	uint32_t texinfo)
{
	uint32_t index;

	for (index = 0U; index < build->sources->surface_rule_count; index++)
		if (build->sources->surface_rules[index].model_index == model &&
			build->sources->surface_rules[index].texinfo == texinfo)
			return index;
	return UINT32_MAX;
}

static int PointInBrush(const sg_bsp_world_t *world,
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

static sg_hook_visibility_terminal_flags_t BoundaryFlags(
	const hook_build_t *build, const sg_hook_visibility_domain_term_t *domain,
	const float muzzle[3], const float direction[3],
	const sg_host_collision_trace_t *trace)
{
	const sg_bsp_world_t *world = build->sources->collision->world;
	float denominator = direction[0] * trace->plane.normal[0] +
		direction[1] * trace->plane.normal[1] +
		direction[2] * trace->plane.normal[2];
	float numerator = trace->plane.distance -
		(muzzle[0] * trace->plane.normal[0] +
		 muzzle[1] * trace->plane.normal[1] +
		 muzzle[2] * trace->plane.normal[2]);
	float point[3];
	uint32_t rule, coincidences = 0U, axis;
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
	for (rule = 0U; rule < build->sources->surface_rule_count; rule++)
		if (PointInBrush(world,
				&world->brushes[build->sources->surface_rules[rule].brush_index],
				point))
			coincidences++;
	if (coincidences >= 2U)
		flags |= SG_HOOK_VISIBILITY_TERMINAL_EDGE |
			SG_HOOK_VISIBILITY_TERMINAL_TIE;
	if (coincidences >= 4U)
		flags |= SG_HOOK_VISIBILITY_TERMINAL_VERTEX;
	return flags;
}

static int EvaluateTerminal(hook_build_t *build,
	sg_hook_visibility_terminal_t *terminal)
{
	float origin[3], forward[3], right[3], muzzle[3], end[3];
	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	sg_host_collision_trace_t clearance, hit;
	int16_t pitch = (int16_t)(((int32_t)terminal->domain.pitch_min +
		terminal->domain.pitch_max) / 2);
	int16_t yaw = (int16_t)(((int32_t)terminal->domain.yaw_min +
		terminal->domain.yaw_max) / 2);
	uint32_t axis, hand = 0U;

	for (axis = 0U; axis < 3U; axis++)
		origin[axis] = (float)(((int32_t)terminal->domain.origins.mins[axis] +
			terminal->domain.origins.maxs[axis]) / 2) * 0.125f;
	while ((terminal->domain.hand_mask &
		SG_HOOK_VISIBILITY_HAND_BIT(hand)) == 0U)
		hand++;
	Direction(pitch, yaw, forward, right);
	Muzzle(build, origin, (sg_hook_visibility_hand_t)hand, forward, right,
		muzzle);
	if (!SG_HostCollisionTrace(build->sources->collision, build->sources->scene,
		origin, zero, zero, muzzle, build->sources->fire_law.shot_mask,
		&clearance))
	{
		SetError(build,
			SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_HOST_DISAGREEMENT,
			build->catalog->terminal_count);
		return 0;
	}
	build->catalog->metrics.muzzle_clearance_traces++;
	if (clearance.startsolid || clearance.allsolid || clearance.fraction < 1.0f)
	{
		terminal->outcome = SG_HOOK_VISIBILITY_TERMINAL_CLEARANCE_BLOCKED;
		terminal->surface_rule = UINT32_MAX;
		return 1;
	}
	for (axis = 0U; axis < 3U; axis++)
		end[axis] = muzzle[axis] + forward[axis] *
			build->sources->fire_law.maximum_range;
	if (!SG_HostCollisionTrace(build->sources->collision, build->sources->scene,
		muzzle, zero, zero, end, build->sources->fire_law.shot_mask, &hit))
	{
		SetError(build,
			SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_HOST_DISAGREEMENT,
			build->catalog->terminal_count);
		return 0;
	}
	build->catalog->metrics.first_hit_traces++;
	if (hit.fraction == 1.0f)
	{
		terminal->outcome = SG_HOOK_VISIBILITY_TERMINAL_NO_HIT;
		terminal->surface_rule = UINT32_MAX;
		return 1;
	}
	terminal->surface_rule = FindSurfaceRule(build, hit.model_index, hit.texinfo);
	if (terminal->surface_rule == UINT32_MAX)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED,
			hit.texinfo);
		return 0;
	}
	if (hit.surface_flags & SG_HOST_SURFACE_SKY)
		terminal->outcome = SG_HOOK_VISIBILITY_TERMINAL_SKY;
	else if (build->sources->surface_rules[terminal->surface_rule].classification ==
		SG_HOOK_VISIBILITY_SURFACE_HOOKABLE)
		terminal->outcome = SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE;
	else if (build->sources->surface_rules[terminal->surface_rule].classification ==
		SG_HOOK_VISIBILITY_SURFACE_SKY)
	{
		SetError(build,
			SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_HOST_DISAGREEMENT,
			terminal->surface_rule);
		return 0;
	}
	else
		terminal->outcome = SG_HOOK_VISIBILITY_TERMINAL_NONHOOKABLE;
	terminal->flags |= BoundaryFlags(build, &terminal->domain, muzzle, forward,
		&hit);
	return 1;
}

static int GrowTerminals(hook_build_t *build)
{
	uint32_t capacity;
	sg_hook_visibility_terminal_t *terminals;
	size_t allocation_size;

	if (build->catalog->terminal_count < build->catalog->terminal_capacity)
		return 1;
	if (build->catalog->terminal_capacity > UINT32_MAX / 2U)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW, 0U);
		return 0;
	}
	capacity = build->catalog->terminal_capacity ?
		build->catalog->terminal_capacity * 2U : 256U;
	if (!AllocationSize(build, capacity, sizeof(*terminals), &allocation_size))
		return 0;
	terminals = realloc(build->catalog->terminals, allocation_size);
	if (!terminals)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	build->catalog->terminals = terminals;
	build->catalog->terminal_capacity = capacity;
	return 1;
}

static int AppendTerminal(hook_build_t *build,
	const sg_hook_visibility_domain_term_t *domain)
{
	sg_hook_visibility_terminal_t terminal;

	if (!GrowTerminals(build))
		return 0;
	memset(&terminal, 0, sizeof(terminal));
	terminal.domain = *domain;
	terminal.surface_rule = UINT32_MAX;
	if (!EvaluateTerminal(build, &terminal))
		return 0;
	build->catalog->terminals[build->catalog->terminal_count++] = terminal;
	build->catalog->metrics.predicate_domains++;
	return 1;
}

static int ConstructTerminals(hook_build_t *build)
{
	sg_hook_visibility_i16_span_t *x_spans = NULL;
	sg_hook_visibility_i16_span_t *y_spans = NULL;
	sg_hook_visibility_i16_span_t *z_spans = NULL;
	uint32_t x_count = 0U, y_count = 0U, z_count = 0U;
	uint32_t control, x, y, z, pitch, yaw, hand;
	int result = 0;

	if (!SG_HookVisibilityFeasibilityAxisSpans(build, 0U, &x_spans,
			&x_count) ||
		!SG_HookVisibilityFeasibilityAxisSpans(build, 1U, &y_spans,
			&y_count) ||
		!SG_HookVisibilityFeasibilityAxisSpans(build, 2U, &z_spans,
			&z_count))
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY, 0U);
		goto done;
	}
	for (control = 0U; control < build->sources->control_count; control++)
	{
		sg_hook_visibility_i16_span_t pitch_spans[3], yaw_spans[3];
		uint32_t pitch_count, yaw_count;
		const sg_hook_visibility_control_root_t *root =
			&build->sources->controls[control];

		if (!MakeControlSpans(root->pitch_min, root->pitch_max, 0,
				pitch_spans, &pitch_count) ||
			!MakeControlSpans(root->yaw_min, root->yaw_max, 1, yaw_spans,
				&yaw_count))
		{
			SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED,
				control);
			goto done;
		}
		for (x = 0U; x < x_count; x++)
			for (y = 0U; y < y_count; y++)
				for (z = 0U; z < z_count; z++)
				for (pitch = 0U; pitch < pitch_count; pitch++)
					for (yaw = 0U; yaw < yaw_count; yaw++)
						for (hand = 0U;
							hand < SG_HOOK_VISIBILITY_HAND_COUNT;
							hand++)
						{
							sg_hook_visibility_domain_term_t domain;

							memset(&domain, 0, sizeof(domain));
							domain.origins.mins[0] =
								x_spans[x].minimum;
							domain.origins.maxs[0] =
								x_spans[x].maximum;
							domain.origins.mins[1] =
								y_spans[y].minimum;
							domain.origins.maxs[1] =
								y_spans[y].maximum;
							domain.origins.mins[2] =
								z_spans[z].minimum;
							domain.origins.maxs[2] =
								z_spans[z].maximum;
							domain.pitch_min =
								pitch_spans[pitch].minimum;
							domain.pitch_max =
								pitch_spans[pitch].maximum;
							domain.yaw_min =
								yaw_spans[yaw].minimum;
							domain.yaw_max =
								yaw_spans[yaw].maximum;
							domain.hand_mask =
								SG_HOOK_VISIBILITY_HAND_BIT(hand);
							if (!AppendTerminal(build, &domain))
								goto done;
						}
	}
	result = 1;

done:
	free(z_spans);
	free(y_spans);
	free(x_spans);
	return result;
}

static int RelationCompare(const void *left, const void *right)
{
	const sg_hook_visibility_relation_t *left_relation = left;
	const sg_hook_visibility_relation_t *right_relation = right;

	return (left_relation->surface_id > right_relation->surface_id) -
		(left_relation->surface_id < right_relation->surface_id);
}

static int TermCompare(const void *left, const void *right)
{
	const sg_hook_visibility_domain_term_t *a = left;
	const sg_hook_visibility_domain_term_t *b = right;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		if (a->origins.mins[axis] != b->origins.mins[axis])
			return (a->origins.mins[axis] > b->origins.mins[axis]) ? 1 : -1;
		if (a->origins.maxs[axis] != b->origins.maxs[axis])
			return (a->origins.maxs[axis] > b->origins.maxs[axis]) ? 1 : -1;
	}
	if (a->hand_mask != b->hand_mask)
		return (a->hand_mask > b->hand_mask) ? 1 : -1;
	if (a->pitch_min != b->pitch_min)
		return (a->pitch_min > b->pitch_min) ? 1 : -1;
	if (a->pitch_max != b->pitch_max)
		return (a->pitch_max > b->pitch_max) ? 1 : -1;
	if (a->yaw_min != b->yaw_min)
		return (a->yaw_min > b->yaw_min) ? 1 : -1;
	return (a->yaw_max > b->yaw_max) - (a->yaw_max < b->yaw_max);
}

static int TermsSameExceptYaw(const sg_hook_visibility_domain_term_t *left,
	const sg_hook_visibility_domain_term_t *right)
{
	return memcmp(&left->origins, &right->origins,
			sizeof(left->origins)) == 0 && left->hand_mask == right->hand_mask &&
		left->pitch_min == right->pitch_min &&
		left->pitch_max == right->pitch_max &&
		(int32_t)left->yaw_max + 1 == right->yaw_min;
}

static int TermsSameExceptPitch(const sg_hook_visibility_domain_term_t *left,
	const sg_hook_visibility_domain_term_t *right)
{
	return memcmp(&left->origins, &right->origins,
			sizeof(left->origins)) == 0 && left->hand_mask == right->hand_mask &&
		left->yaw_min == right->yaw_min && left->yaw_max == right->yaw_max &&
		(int32_t)left->pitch_max + 1 == right->pitch_min;
}

static void ReduceTerms(sg_hook_visibility_relation_t *relation)
{
	int changed;

	do
	{
		uint32_t read, write = 0U;

		changed = 0;
		qsort(relation->terms, relation->term_count, sizeof(*relation->terms),
			TermCompare);
		for (read = 0U; read < relation->term_count; read++)
		{
			if (write && TermsSameExceptYaw(&relation->terms[write - 1U],
					&relation->terms[read]))
			{
				relation->terms[write - 1U].yaw_max =
					relation->terms[read].yaw_max;
				changed = 1;
			}
			else if (write && TermsSameExceptPitch(
					&relation->terms[write - 1U], &relation->terms[read]))
			{
				relation->terms[write - 1U].pitch_max =
					relation->terms[read].pitch_max;
				changed = 1;
			}
			else
				relation->terms[write++] = relation->terms[read];
		}
		relation->term_count = write;
	} while (changed);
}

static int AppendRelationTerm(hook_build_t *build,
	sg_hook_visibility_relation_t *relation,
	const sg_hook_visibility_domain_term_t *term)
{
	uint32_t capacity;
	sg_hook_visibility_domain_term_t *terms;
	size_t allocation_size;

	if (relation->term_count == relation->term_capacity)
	{
		if (relation->term_capacity > UINT32_MAX / 2U)
		{
			SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW, 0U);
			return 0;
		}
		capacity = relation->term_capacity ? relation->term_capacity * 2U : 32U;
		if (!AllocationSize(build, capacity, sizeof(*terms), &allocation_size))
			return 0;
		terms = realloc(relation->terms, allocation_size);
		if (!terms)
		{
			SetError(build,
				SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
		relation->terms = terms;
		relation->term_capacity = capacity;
	}
	relation->terms[relation->term_count++] = *term;
	return 1;
}

static int BuildRelations(hook_build_t *build)
{
	uint32_t terminal, rule, relation_count = 0U;
	size_t allocation_size;

	if (!AllocationSize(build, build->sources->surface_rule_count,
			sizeof(*build->catalog->relations), &allocation_size))
		return 0;
	(void)allocation_size;
	build->catalog->relations = calloc(build->sources->surface_rule_count,
		sizeof(*build->catalog->relations));
	if (!build->catalog->relations)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	for (rule = 0U; rule < build->sources->surface_rule_count; rule++)
		if (build->sources->surface_rules[rule].classification ==
			SG_HOOK_VISIBILITY_SURFACE_HOOKABLE)
		{
			sg_hook_visibility_relation_t *relation =
				&build->catalog->relations[relation_count++];

			relation->surface_id =
				build->sources->surface_rules[rule].surface_id;
			relation->model_index =
				build->sources->surface_rules[rule].model_index;
			relation->texinfo = build->sources->surface_rules[rule].texinfo;
		}
	build->catalog->relation_count = relation_count;
	qsort(build->catalog->relations, relation_count,
		sizeof(*build->catalog->relations), RelationCompare);
	for (terminal = 0U; terminal < build->catalog->terminal_count; terminal++)
	{
		const sg_hook_visibility_terminal_t *record =
			&build->catalog->terminals[terminal];
		const sg_hook_visibility_surface_rule_t *surface;

		if (record->outcome != SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE)
			continue;
		surface = &build->sources->surface_rules[record->surface_rule];
		for (rule = 0U; rule < relation_count; rule++)
			if (build->catalog->relations[rule].surface_id == surface->surface_id)
				break;
		if (rule == relation_count || !AppendRelationTerm(build,
				&build->catalog->relations[rule], &record->domain))
			return 0;
	}
	build->catalog->metrics.relation_term_count = 0U;
	for (rule = 0U; rule < relation_count; rule++)
	{
		ReduceTerms(&build->catalog->relations[rule]);
		if (build->catalog->metrics.relation_term_count > UINT32_MAX -
			build->catalog->relations[rule].term_count)
		{
			SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW,
				rule);
			return 0;
		}
		build->catalog->metrics.relation_term_count +=
			build->catalog->relations[rule].term_count;
	}
	build->catalog->metrics.relation_count = relation_count;
	build->catalog->metrics.complement_term_count =
		build->catalog->terminal_count;
	for (terminal = 0U; terminal < build->catalog->terminal_count; terminal++)
		if (build->catalog->terminals[terminal].outcome ==
			SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE)
			build->catalog->metrics.complement_term_count--;
	return 1;
}

static int MultiplyU64(uint64_t left, uint64_t right, uint64_t *result)
{
	if (right && left > UINT64_MAX / right)
		return 0;
	*result = left * right;
	return 1;
}

static int CountActions(hook_build_t *build)
{
	uint64_t origins = 1U, controls = 0U, product;
	uint32_t axis, control;

	for (axis = 0U; axis < 3U; axis++)
		if (!MultiplyU64(origins,
				(uint64_t)((int32_t)build->sources->origins.maxs[axis] -
				build->sources->origins.mins[axis] + 1), &origins))
			goto overflow;
	for (control = 0U; control < build->sources->control_count; control++)
	{
		uint64_t pitch = (uint64_t)((int32_t)
			build->sources->controls[control].pitch_max -
			build->sources->controls[control].pitch_min + 1);
		uint64_t yaw = (uint64_t)((int32_t)
			build->sources->controls[control].yaw_max -
			build->sources->controls[control].yaw_min + 1);

		if (!MultiplyU64(pitch, yaw, &product) ||
			controls > UINT64_MAX - product)
			goto overflow;
		controls += product;
	}
	if (!MultiplyU64(origins, controls, &product) ||
		!MultiplyU64(product, SG_HOOK_VISIBILITY_HAND_COUNT, &product))
		goto overflow;
	build->catalog->metrics.legal_action_tuples = product;
	return 1;

overflow:
	SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW, 0U);
	return 0;
}

static int CopySources(hook_build_t *build)
{
	sg_hook_visibility_feasibility_catalog_t *catalog = build->catalog;
	const sg_bsp_world_t *world = build->sources->collision->world;
	size_t control_bytes, rule_bytes;

	if (!AllocationSize(build, build->sources->control_count,
			sizeof(*catalog->controls), &control_bytes) ||
		!AllocationSize(build, build->sources->surface_rule_count,
			sizeof(*catalog->surface_rules), &rule_bytes))
		return 0;
	catalog->controls = calloc(build->sources->control_count,
		sizeof(*catalog->controls));
	catalog->surface_rules = calloc(build->sources->surface_rule_count,
		sizeof(*catalog->surface_rules));
	if (!catalog->controls || !catalog->surface_rules)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	memcpy(catalog->controls, build->sources->controls, control_bytes);
	memcpy(catalog->surface_rules, build->sources->surface_rules,
		rule_bytes);
	catalog->control_count = build->sources->control_count;
	catalog->surface_rule_count = build->sources->surface_rule_count;
	catalog->origins = build->sources->origins;
	catalog->stance = build->sources->stance;
	catalog->fire_law = build->sources->fire_law;
	catalog->collision_identity = build->sources->collision->identity;
	catalog->world_counts[0] = world->plane_count;
	catalog->world_counts[1] = world->node_count;
	catalog->world_counts[2] = world->leaf_count;
	catalog->world_counts[3] = world->leaf_brush_count;
	catalog->world_counts[4] = world->model_count;
	catalog->world_counts[5] = world->brush_count;
	catalog->world_counts[6] = world->brush_side_count;
	catalog->world_counts[7] = world->texinfo_count;
	catalog->producer_identity = build->sources->producer_identity;
	catalog->verifier_identity = build->sources->verifier_identity;
	catalog->source_digest = SourceDigest(build->sources);
	catalog->verifier_source_digest = VerifierSourceDigest(build->sources);
	catalog->magic = SG_HOOK_VISIBILITY_CATALOG_MAGIC;
	return 1;
}


int SG_HookVisibilityFeasibilityConstruct(
	sg_hook_visibility_build_context_t *build)
{
	return CountActions(build) && CopySources(build) &&
		ConstructTerminals(build) && BuildRelations(build);
}
