#include "../slipgate/sg_bsp_completeness_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
			#condition); \
		failures++; \
	} \
} while (0)

static uint64_t AuditFan(uint32_t count, int alternating,
	uint64_t *candidates_out)
{
	sg_configuration_cell_t *cells = calloc(count, sizeof(*cells));
	sg_configuration_face_t *faces = calloc(count, sizeof(*faces));
	sg_configuration_space_t space;
	sg_bsp_proof_context_t proof;
	uint32_t index;
	int audited;

	if (!cells || !faces)
	{
		free(cells);
		free(faces);
		fputs("portal scaling allocation failed\n", stderr);
		exit(2);
	}
	memset(&space, 0, sizeof(space));
	memset(&proof, 0, sizeof(proof));
	for (index = 0; index < count; index++)
	{
		uint32_t pair = index / 2U;
		float base = -0.75f + (float)pair * 1.5f /
			(float)(count / 2U);
		float slope = base + ((alternating && (index & 1U)) ?
			0.000005f : 0.0f);
		float sign = alternating && (index & 1U) ? -1.0f : 1.0f;

		cells[index].stance = SG_RUNE_STANCE_STANDING;
		cells[index].first_face = index;
		cells[index].face_count = 1U;
		faces[index].plane.normal[0] = sign;
		faces[index].plane.normal[1] = sign * slope;
		faces[index].plane.source_kind = SG_CONFIGURATION_PLANE_BSP;
	}
	space.cells = cells;
	space.cell_count = count;
	space.faces = faces;
	space.face_count = count;
	proof.space = &space;
	audited = SG_BspProofAuditPortals(&proof);
	CHECK(audited);
	*candidates_out = proof.result.portal_face_candidates;
	free(cells);
	free(faces);
	return proof.result.portal_face_pair_visits;
}

static void TestFanScaling(void)
{
	static const uint32_t counts[] = { 64U, 128U, 256U, 512U, 1024U };
	uint32_t scale;

	for (scale = 0; scale < sizeof(counts) / sizeof(counts[0]); scale++)
	{
		uint64_t same_candidates;
		uint64_t alternating_candidates;
		uint64_t same_visits = AuditFan(counts[scale], 0, &same_candidates);
		uint64_t alternating_visits = AuditFan(counts[scale], 1,
			&alternating_candidates);

		CHECK(same_visits == counts[scale] / 2U);
		CHECK(alternating_visits == counts[scale] / 2U);
		CHECK(same_candidates == 0U);
		CHECK(alternating_candidates == alternating_visits);
		printf("portal fan %u: same=%llu alternating=%llu\n", counts[scale],
			(unsigned long long)same_visits,
			(unsigned long long)alternating_visits);
	}
}

static void TestDominantTieKey(void)
{
	sg_configuration_cell_t cells[2];
	sg_configuration_face_t faces[2];
	sg_configuration_space_t space;
	sg_bsp_proof_context_t proof;
	sg_bsp_proof_face_ref_t *refs = NULL;
	uint32_t ref_count = 0U;

	memset(cells, 0, sizeof(cells));
	memset(faces, 0, sizeof(faces));
	memset(&space, 0, sizeof(space));
	memset(&proof, 0, sizeof(proof));
	cells[0].face_count = 1U;
	cells[1].first_face = 1U;
	cells[1].face_count = 1U;
	faces[0].plane.normal[0] = 1.0f;
	faces[0].plane.normal[1] = 1.0f;
	faces[1].plane.normal[0] = -1.3f;
	faces[1].plane.normal[1] = -1.3f;
	space.cells = cells;
	space.cell_count = 2U;
	space.faces = faces;
	space.face_count = 2U;
	proof.space = &space;
	CHECK(SG_BspProofBuildFaceRefs(&proof, &refs, &ref_count));
	CHECK(ref_count == 2U);
	if (ref_count == 2U)
	{
		CHECK(refs[0].dominant == 0U);
		CHECK(refs[1].dominant == 0U);
		CHECK(refs[0].normal_buckets[0] == refs[1].normal_buckets[0]);
		CHECK(refs[0].normal_buckets[1] == refs[1].normal_buckets[1]);
		CHECK(refs[0].normal_buckets[2] == refs[1].normal_buckets[2]);
		CHECK(refs[0].plane_bucket == refs[1].plane_bucket);
		CHECK(refs[0].orientation != refs[1].orientation);
	}
	SG_BspProofFreeFaceRefs(refs, ref_count);
}

static void TestNearTieCompatibility(void)
{
	sg_configuration_cell_t cells[2];
	sg_configuration_face_t faces[2];
	sg_configuration_space_t space;
	sg_bsp_proof_context_t proof;
	sg_bsp_proof_face_ref_t *refs = NULL;
	uint32_t ref_count = 0U;
	uint32_t axis;

	memset(cells, 0, sizeof(cells));
	memset(faces, 0, sizeof(faces));
	memset(&space, 0, sizeof(space));
	memset(&proof, 0, sizeof(proof));
	cells[0].face_count = 1U;
	cells[1].first_face = 1U;
	cells[1].face_count = 1U;
	faces[0].plane.normal[0] = 1.0f;
	faces[0].plane.normal[1] = 1.0000005f;
	faces[1].plane.normal[0] = -1.0000005f;
	faces[1].plane.normal[1] = -1.0f;
	space.cells = cells;
	space.cell_count = 2U;
	space.faces = faces;
	space.face_count = 2U;
	proof.space = &space;
	CHECK(SG_BspProofBuildFaceRefs(&proof, &refs, &ref_count));
	CHECK(ref_count == 2U);
	if (ref_count == 2U)
	{
		CHECK(refs[0].dominant != refs[1].dominant);
		for (axis = 0; axis < 3U; axis++)
			CHECK(llabs(refs[0].normal_buckets[axis] -
				refs[1].normal_buckets[axis]) <= 1);
		CHECK(llabs(refs[0].plane_bucket - refs[1].plane_bucket) <= 1);
	}
	SG_BspProofFreeFaceRefs(refs, ref_count);
}

int main(void)
{
	TestFanScaling();
	TestDominantTieKey();
	TestNearTieCompatibility();
	if (failures)
	{
		fprintf(stderr, "%d portal index scaling checks failed\n", failures);
		return 1;
	}
	puts("BSP portal index scaling checks passed");
	return 0;
}
