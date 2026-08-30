#include "sg_bsp_completeness_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct point2_s
{
	double value[2];
} point2_t;

static uint32_t DominantAxis(const float normal[3])
{
	uint32_t axis = 0U;
	uint32_t candidate;

	for (candidate = 1U; candidate < 3U; candidate++)
		if (fabsf(normal[candidate]) > fabsf(normal[axis]))
			axis = candidate;
	return axis;
}

static int AppendPoint(point2_t **points, uint32_t *count,
	const double point[2])
{
	point2_t *grown;

	if (*count == UINT32_MAX)
		return 0;
	grown = realloc(*points, (size_t)(*count + 1U) * sizeof(*grown));
	if (!grown)
		return 0;
	*points = grown;
	grown[*count].value[0] = point[0];
	grown[*count].value[1] = point[1];
	(*count)++;
	return 1;
}

static int ClipHalfspace(point2_t **polygon, uint32_t *count,
	double coefficient_u, double coefficient_v, double distance)
{
	point2_t *next = NULL;
	uint32_t next_count = 0U;
	uint32_t index;

	if (coefficient_u == 0.0 && coefficient_v == 0.0)
	{
		if (distance >= 0.0)
			return 1;
		free(*polygon);
		*polygon = NULL;
		*count = 0U;
		return 1;
	}
	for (index = 0; index < *count; index++)
	{
		const point2_t *start = &(*polygon)[index];
		const point2_t *end = &(*polygon)[(index + 1U) % *count];
		double start_distance = coefficient_u * start->value[0] +
			coefficient_v * start->value[1] - distance;
		double end_distance = coefficient_u * end->value[0] +
			coefficient_v * end->value[1] - distance;
		int start_inside = start_distance <= 0.0f;
		int end_inside = end_distance <= 0.0f;

		if (start_inside && !AppendPoint(&next, &next_count, start->value))
			goto failure;
		if (start_inside != end_inside)
		{
			double denominator = start_distance - end_distance;
			double point[2];

			if (denominator == 0.0f)
				goto failure;
			point[0] = start->value[0] + start_distance / denominator *
				(end->value[0] - start->value[0]);
			point[1] = start->value[1] + start_distance / denominator *
				(end->value[1] - start->value[1]);
			if (!AppendPoint(&next, &next_count, point))
				goto failure;
		}
	}
	free(*polygon);
	*polygon = next;
	*count = next_count;
	return 1;

failure:
	free(next);
	return 0;
}

static int CellFacePolygon(sg_bsp_proof_context_t *proof,
	uint32_t cell_index, const sg_configuration_face_t *boundary,
	sg_rune_vec3_t **vertices_out, uint32_t *count_out)
{
	const sg_configuration_cell_t *cell = &proof->space->cells[cell_index];
	point2_t *polygon = NULL;
	uint32_t count = 0U;
	uint32_t drop = DominantAxis(boundary->plane.normal);
	uint32_t u = (drop + 1U) % 3U;
	uint32_t v = (drop + 2U) % 3U;
	uint32_t index;
	double boundary_normal[3], boundary_distance;
	double initial[4][2] = {
		{ SG_CONFIGURATION_PMOVE_ORIGIN_MIN,
			SG_CONFIGURATION_PMOVE_ORIGIN_MIN },
		{ SG_CONFIGURATION_PMOVE_ORIGIN_MAX,
			SG_CONFIGURATION_PMOVE_ORIGIN_MIN },
		{ SG_CONFIGURATION_PMOVE_ORIGIN_MAX,
			SG_CONFIGURATION_PMOVE_ORIGIN_MAX },
		{ SG_CONFIGURATION_PMOVE_ORIGIN_MIN,
			SG_CONFIGURATION_PMOVE_ORIGIN_MAX }
	};

	*vertices_out = NULL;
	*count_out = 0U;
	if (!SG_BspProofOrientedPlane(&boundary->plane, boundary_normal,
			&boundary_distance))
		goto invalid;
	for (index = 0; index < 4U; index++)
		if (!AppendPoint(&polygon, &count, initial[index]))
			goto failure;
	for (index = 0; index < cell->face_count && count >= 3U; index++)
	{
		const sg_configuration_plane_t *clip =
			&proof->space->faces[cell->first_face + index].plane;
		double clip_normal[3], clip_distance;
		double ratio, coefficient_u, coefficient_v, distance;

		if (!SG_BspProofOrientedPlane(clip, clip_normal, &clip_distance))
			goto invalid;
		ratio = clip_normal[drop] / boundary_normal[drop];
		coefficient_u = clip_normal[u] - ratio * boundary_normal[u];
		coefficient_v = clip_normal[v] - ratio * boundary_normal[v];
		distance = clip_distance - ratio * boundary_distance;

		if (!ClipHalfspace(&polygon, &count, coefficient_u, coefficient_v,
				distance))
			goto failure;
	}
	if (count >= 3U)
	{
		sg_rune_vec3_t *vertices = calloc(count, sizeof(*vertices));

		if (!vertices)
			goto failure;
		for (index = 0; index < count; index++)
		{
			vertices[index].value[u] = (float)polygon[index].value[0];
			vertices[index].value[v] = (float)polygon[index].value[1];
			vertices[index].value[drop] = (float)((boundary_distance -
				boundary_normal[u] * polygon[index].value[0] -
				boundary_normal[v] * polygon[index].value[1]) /
				boundary_normal[drop]);
		}
		*vertices_out = vertices;
		*count_out = count;
	}
	free(polygon);
	return 1;

failure:
	free(polygon);
	SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY, cell_index);
	return 0;

invalid:
	free(polygon);
	SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVALID_CELL, cell_index);
	return 0;
}

int SG_BspProofPlaneKey(const sg_configuration_plane_t *plane,
	uint32_t *dominant_out, int64_t normal_buckets[3],
	int64_t *bucket_out, uint8_t *orientation)
{
	sg_bsp_proof_canonical_plane_t canonical;
	uint32_t dominant = DominantAxis(plane->normal);
	double bucket;
	uint32_t axis;

	if (!SG_BspProofCanonicalPlane(plane, &canonical))
		return 0;

	for (axis = 0; axis < 3U; axis++)
	{
		double normal_bucket = floor(canonical.normal[axis] /
			SG_BSP_PROOF_PLANE_BUCKET_SIZE);

		if (!isfinite(canonical.normal[axis]) ||
			normal_bucket < (double)INT64_MIN ||
			normal_bucket > (double)INT64_MAX)
			return 0;
		normal_buckets[axis] = (int64_t)normal_bucket;
	}
	bucket = floor(canonical.distance / SG_BSP_PROOF_PLANE_BUCKET_SIZE);
	if (!isfinite(canonical.distance) || bucket < (double)INT64_MIN ||
		bucket > (double)INT64_MAX)
		return 0;
	*dominant_out = dominant;
	*bucket_out = (int64_t)bucket;
	*orientation = canonical.orientation;
	return 1;
}

static int CompareFaceRef(const void *left_pointer,
	const void *right_pointer)
{
	const sg_bsp_proof_face_ref_t *left = left_pointer;
	const sg_bsp_proof_face_ref_t *right = right_pointer;

	if (left->stance != right->stance)
		return left->stance < right->stance ? -1 : 1;
	if (left->dominant != right->dominant)
		return left->dominant < right->dominant ? -1 : 1;
	if (left->normal_buckets[0] != right->normal_buckets[0])
		return left->normal_buckets[0] < right->normal_buckets[0] ? -1 : 1;
	if (left->normal_buckets[1] != right->normal_buckets[1])
		return left->normal_buckets[1] < right->normal_buckets[1] ? -1 : 1;
	if (left->normal_buckets[2] != right->normal_buckets[2])
		return left->normal_buckets[2] < right->normal_buckets[2] ? -1 : 1;
	if (left->plane_bucket != right->plane_bucket)
		return left->plane_bucket < right->plane_bucket ? -1 : 1;
	if (left->orientation != right->orientation)
		return left->orientation < right->orientation ? -1 : 1;
	if (left->sweep_min != right->sweep_min)
		return left->sweep_min < right->sweep_min ? -1 : 1;
	if (left->cell != right->cell)
		return left->cell < right->cell ? -1 : 1;
	return left->face == right->face ? 0 : (left->face < right->face ? -1 : 1);
}

static int SameFaceGroup(const sg_bsp_proof_face_ref_t *left,
	const sg_bsp_proof_face_ref_t *right)
{
	return left->stance == right->stance &&
		left->dominant == right->dominant &&
		left->normal_buckets[0] == right->normal_buckets[0] &&
		left->normal_buckets[1] == right->normal_buckets[1] &&
		left->normal_buckets[2] == right->normal_buckets[2] &&
		left->plane_bucket == right->plane_bucket &&
		left->orientation == right->orientation;
}

static float BuildFaceIntervalMax(sg_bsp_proof_face_ref_t *refs,
	uint32_t first, uint32_t count)
{
	uint32_t left_count;
	uint32_t middle;
	float maximum;

	if (!count)
		return -INFINITY;
	left_count = count / 2U;
	middle = first + left_count;
	maximum = refs[middle].sweep_max;
	if (left_count)
		maximum = fmaxf(maximum,
			BuildFaceIntervalMax(refs, first, left_count));
	if (count - left_count - 1U)
		maximum = fmaxf(maximum, BuildFaceIntervalMax(refs, middle + 1U,
			count - left_count - 1U));
	refs[middle].subtree_sweep_max = maximum;
	return maximum;
}

int SG_BspProofBuildFaceRefs(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_face_ref_t **refs_out, uint32_t *count_out)
{
	sg_bsp_proof_face_ref_t *refs;
	uint32_t cell, offset, count = 0U;

	*refs_out = NULL;
	*count_out = 0U;
	refs = calloc(proof->space->face_count ? proof->space->face_count : 1U,
		sizeof(*refs));
	if (!refs)
		return 0;
	for (cell = 0; cell < proof->space->cell_count; cell++)
		for (offset = 0; offset < proof->space->cells[cell].face_count; offset++)
		{
			uint32_t face = proof->space->cells[cell].first_face + offset;
			const sg_configuration_face_t *boundary = &proof->space->faces[face];
			sg_bsp_proof_face_ref_t *ref = &refs[count];
			uint32_t drop = DominantAxis(boundary->plane.normal);
			uint32_t u = (drop + 1U) % 3U;
			uint32_t v = (drop + 2U) % 3U;
			uint32_t vertex;

			if (!CellFacePolygon(proof, cell, boundary, &ref->vertices,
					&ref->vertex_count))
				goto failure;
			ref->cell = cell;
			ref->face = face;
			ref->stance = (uint32_t)proof->space->cells[cell].stance;
			if (!SG_BspProofPlaneKey(&boundary->plane, &ref->dominant,
					ref->normal_buckets, &ref->plane_bucket,
					&ref->orientation))
			{
				SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVALID_CELL, cell);
				goto failure;
			}
			if (ref->vertex_count >= 3U)
			{
				ref->sweep_min = ref->sweep_max = ref->vertices[0].value[u];
				ref->other_min = ref->other_max = ref->vertices[0].value[v];
				memcpy(ref->bounds_mins, ref->vertices[0].value,
					sizeof(ref->bounds_mins));
				memcpy(ref->bounds_maxs, ref->vertices[0].value,
					sizeof(ref->bounds_maxs));
				for (vertex = 1U; vertex < ref->vertex_count; vertex++)
				{
					uint32_t component;

					ref->sweep_min = fminf(ref->sweep_min,
						ref->vertices[vertex].value[u]);
					ref->sweep_max = fmaxf(ref->sweep_max,
						ref->vertices[vertex].value[u]);
					ref->other_min = fminf(ref->other_min,
						ref->vertices[vertex].value[v]);
					ref->other_max = fmaxf(ref->other_max,
						ref->vertices[vertex].value[v]);
					for (component = 0; component < 3U; component++)
					{
						ref->bounds_mins[component] = fminf(
							ref->bounds_mins[component],
							ref->vertices[vertex].value[component]);
						ref->bounds_maxs[component] = fmaxf(
							ref->bounds_maxs[component],
							ref->vertices[vertex].value[component]);
					}
				}
			}
			else
			{
				memcpy(ref->bounds_mins,
					proof->space->cells[cell].bounds.mins.value,
					sizeof(ref->bounds_mins));
				memcpy(ref->bounds_maxs,
					proof->space->cells[cell].bounds.maxs.value,
					sizeof(ref->bounds_maxs));
				ref->sweep_min = ref->bounds_mins[u];
				ref->sweep_max = ref->bounds_maxs[u];
				ref->other_min = ref->bounds_mins[v];
				ref->other_max = ref->bounds_maxs[v];
			}
			count++;
		}
	qsort(refs, count, sizeof(*refs), CompareFaceRef);
	for (cell = 0; cell < count; )
	{
		uint32_t end = cell + 1U;

		while (end < count && SameFaceGroup(&refs[cell], &refs[end]))
			end++;
		(void)BuildFaceIntervalMax(refs, cell, end - cell);
		cell = end;
	}
	*refs_out = refs;
	*count_out = count;
	return 1;

failure:
	SG_BspProofFreeFaceRefs(refs, count + 1U);
	return 0;
}

void SG_BspProofFreeFaceRefs(sg_bsp_proof_face_ref_t *refs, uint32_t count)
{
	uint32_t index;

	for (index = 0; index < count; index++)
		free(refs[index].vertices);
	free(refs);
}

static int ComparePortalRef(const void *left_pointer,
	const void *right_pointer)
{
	const sg_bsp_proof_portal_ref_t *left = left_pointer;
	const sg_bsp_proof_portal_ref_t *right = right_pointer;

	if (left->low_cell != right->low_cell)
		return left->low_cell < right->low_cell ? -1 : 1;
	if (left->high_cell != right->high_cell)
		return left->high_cell < right->high_cell ? -1 : 1;
	if (left->stance != right->stance)
		return left->stance < right->stance ? -1 : 1;
	if (left->normal_buckets[0] != right->normal_buckets[0])
		return left->normal_buckets[0] < right->normal_buckets[0] ? -1 : 1;
	if (left->normal_buckets[1] != right->normal_buckets[1])
		return left->normal_buckets[1] < right->normal_buckets[1] ? -1 : 1;
	if (left->normal_buckets[2] != right->normal_buckets[2])
		return left->normal_buckets[2] < right->normal_buckets[2] ? -1 : 1;
	if (left->plane_bucket != right->plane_bucket)
		return left->plane_bucket < right->plane_bucket ? -1 : 1;
	return left->portal == right->portal ? 0 :
		(left->portal < right->portal ? -1 : 1);
}

int SG_BspProofBuildPortalRefs(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_portal_ref_t **refs_out)
{
	sg_bsp_proof_portal_ref_t *refs;
	uint32_t portal;

	refs = calloc(proof->space->portal_count ? proof->space->portal_count : 1U,
		sizeof(*refs));
	if (!refs)
		return 0;
	for (portal = 0; portal < proof->space->portal_count; portal++)
	{
		const sg_configuration_portal_t *record =
			&proof->space->portals[portal];

		refs[portal].low_cell = record->from_cell < record->to_cell ?
			record->from_cell : record->to_cell;
		refs[portal].high_cell = record->from_cell < record->to_cell ?
			record->to_cell : record->from_cell;
		refs[portal].stance = (uint32_t)record->stance;
		{
			uint32_t dominant;
			uint8_t orientation;

			if (!SG_BspProofPlaneKey(&record->plane, &dominant,
					refs[portal].normal_buckets,
					&refs[portal].plane_bucket, &orientation))
			{
				free(refs);
				return 0;
			}
		}
		refs[portal].portal = portal;
	}
	qsort(refs, proof->space->portal_count, sizeof(*refs), ComparePortalRef);
	*refs_out = refs;
	return 1;
}

static int ComparePortalGroup(const sg_bsp_proof_portal_ref_t *ref,
	uint32_t low_cell, uint32_t high_cell, uint32_t stance,
	int64_t normal_bucket_0, int64_t normal_bucket_1,
	int64_t normal_bucket_2, int64_t plane_bucket)
{
	if (ref->low_cell != low_cell)
		return ref->low_cell < low_cell ? -1 : 1;
	if (ref->high_cell != high_cell)
		return ref->high_cell < high_cell ? -1 : 1;
	if (ref->stance != stance)
		return ref->stance < stance ? -1 : 1;
	if (ref->normal_buckets[0] != normal_bucket_0)
		return ref->normal_buckets[0] < normal_bucket_0 ? -1 : 1;
	if (ref->normal_buckets[1] != normal_bucket_1)
		return ref->normal_buckets[1] < normal_bucket_1 ? -1 : 1;
	if (ref->normal_buckets[2] != normal_bucket_2)
		return ref->normal_buckets[2] < normal_bucket_2 ? -1 : 1;
	if (ref->plane_bucket != plane_bucket)
		return ref->plane_bucket < plane_bucket ? -1 : 1;
	return 0;
}

uint32_t SG_BspProofPortalGroupBound(const sg_bsp_proof_portal_ref_t *refs,
	uint32_t count, uint32_t low_cell, uint32_t high_cell, uint32_t stance,
	int64_t normal_bucket_0, int64_t normal_bucket_1,
	int64_t normal_bucket_2, int64_t plane_bucket, int upper)
{
	uint32_t first = 0U;
	uint32_t length = count;

	while (length)
	{
		uint32_t half = length / 2U;
		uint32_t middle = first + half;
		const sg_bsp_proof_portal_ref_t *ref = &refs[middle];
		int comparison = ComparePortalGroup(ref, low_cell, high_cell, stance,
			normal_bucket_0, normal_bucket_1, normal_bucket_2, plane_bucket);

		if (comparison < 0 || (upper && comparison == 0))
		{
			first = middle + 1U;
			length -= half + 1U;
		}
		else
			length = half;
	}
	return first;
}
