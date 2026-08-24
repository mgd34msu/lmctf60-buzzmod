#include <stdio.h>
#include <string.h>

#include "q_shared.h"
#include "slipgate/sg_rune_reverse_boundary.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
		        #expression); \
		failures++; \
	} \
} while (0)

static void SetSeed(rune_seed_t *seed, float x)
{
	memset(seed, 0, sizeof(*seed));
	seed->origin[0] = x;
}

static void SetLink(rune_link_t *link, int from, int to, int action)
{
	memset(link, 0, sizeof(*link));
	link->from = (uint16_t)from;
	link->to = (uint16_t)to;
	link->action = (uint8_t)action;
}

static void TestRanksNativeBoundariesBeforeOrdinaryBoundaries(void)
{
	rune_seed_t seeds[9];
	rune_link_t links[6];
	int components[9] = { 0, 1, 2, 3, 4, 5, 2, 3, 6 };
	uint8_t red[9] =  { 1, 1, 1, 1, 1, 1, 1, 1, 1 };
	uint8_t blue[9] = { 1, 0, 1, 0, 1, 0, 1, 0, 1 };
	uint32_t component_sizes[7];
	sg_rune_reverse_boundary_candidate_t ranked[4];
	sg_rune_reverse_boundary_report_t report;
	uint32_t count;

	for (int i = 0; i < 9; i++)
		SetSeed(&seeds[i], (float)(i * 100));
	SetLink(&links[0], 0, 1, RL_RUN);       /* ordinary noise */
	SetLink(&links[1], 2, 3, RL_DROP);      /* native boundary */
	SetLink(&links[2], 4, 5, RL_HOOK);      /* native boundary */
	SetLink(&links[3], 6, 7, RL_DROP);      /* distinct pair, same SCC cut */
	SetLink(&links[4], 8, 8, RL_HOOK);      /* same SCC */
	SetLink(&links[5], 1, 0, RL_HOOK);      /* wrong partition direction */
	seeds[2].origin[0] = 0.0f;
	seeds[3].origin[0] = 500.0f;
	seeds[4].origin[0] = 0.0f;
	seeds[5].origin[0] = 300.0f;

	count = SG_RuneReverseBoundaryRank(seeds, 9U, links, 6U,
		components, 7U, component_sizes, 7U,
		red, blue, 768.0f * 768.0f, ranked, 4U, &report);
	CHECK(count == 4U);
	CHECK(ranked[0].link_index == 3U);
	CHECK(ranked[1].link_index == 2U);
	CHECK(ranked[2].link_index == 1U);
	CHECK(ranked[3].link_index == 0U);
	CHECK(ranked[0].boundary_action == RL_DROP);
	CHECK(ranked[1].boundary_action == RL_HOOK);
	CHECK(report.scanned == 6U);
	CHECK(report.crossing == 4U);
	CHECK(report.unique_ranked_pairs == 4U);
	CHECK(report.ranked == 4U);
}

static void TestRanksComponentGainAndCapsOutput(void)
{
	rune_seed_t seeds[66];
	rune_link_t links[33];
	int components[66];
	uint8_t red[66], blue[66];
	uint32_t component_sizes[66];
	sg_rune_reverse_boundary_candidate_t ranked[SG_RUNE_REVERSE_BOUNDARY_CAP];
	sg_rune_reverse_boundary_report_t report;
	uint32_t count;

	for (int i = 0; i < 66; i++)
	{
		SetSeed(&seeds[i], (float)(i & 1));
		components[i] = i;
		red[i] = 1U;
		blue[i] = (uint8_t)((i & 1) == 0);
	}
	for (int i = 0; i < 33; i++)
		SetLink(&links[i], i * 2, i * 2 + 1, RL_DROP);
	/* Component zero has two members, so its equal-distance merge wins the
	 * component-improvement tie-break. */
	components[64] = 0;

	count = SG_RuneReverseBoundaryRank(seeds, 66U, links, 33U,
		components, 66U, component_sizes, 66U, red, blue,
		768.0f * 768.0f, ranked,
		SG_RUNE_REVERSE_BOUNDARY_CAP, &report);
	CHECK(count == SG_RUNE_REVERSE_BOUNDARY_CAP);
	CHECK(ranked[0].link_index == 0U);
	CHECK(report.unique_ranked_pairs == SG_RUNE_REVERSE_BOUNDARY_CAP);
	CHECK(report.ranked == SG_RUNE_REVERSE_BOUNDARY_CAP);
}

static void TestRanksOneSidedToNeitherWithoutSharedPartition(void)
{
	rune_seed_t seeds[4];
	rune_link_t links[2];
	int components[4] = { 0, 1, 2, 3 };
	uint8_t red[4] = { 1, 0, 0, 0 };
	uint8_t blue[4] = { 0, 0, 1, 0 };
	uint32_t component_sizes[4];
	sg_rune_reverse_boundary_candidate_t ranked[2];
	sg_rune_reverse_boundary_report_t report;
	uint32_t count;

	for (int i = 0; i < 4; i++)
		SetSeed(&seeds[i], (float)(i * 100));
	SetLink(&links[0], 0, 1, RL_DROP);
	SetLink(&links[1], 2, 3, RL_HOOK);

	count = SG_RuneReverseBoundaryRank(seeds, 4U, links, 2U,
		components, 4U, component_sizes, 4U, red, blue,
		768.0f * 768.0f, ranked, 2U, &report);
	CHECK(count == 2U);
	CHECK(report.crossing == 2U);
	CHECK(report.ranked == 2U);
}

static void TestRanksExactProvenRunWithoutNativeBoundary(void)
{
	rune_seed_t seeds[2];
	rune_link_t links[1];
	int components[2] = { 0, 1 };
	uint8_t red[2] = { 1, 1 };
	uint8_t blue[2] = { 1, 0 };
	uint32_t component_sizes[2];
	sg_rune_reverse_boundary_candidate_t ranked[1];
	sg_rune_reverse_boundary_report_t report;
	uint32_t count;

	SetSeed(&seeds[0], 0.0f);
	SetSeed(&seeds[1], 100.0f);
	SetLink(&links[0], 0, 1, RL_RUN);

	count = SG_RuneReverseBoundaryRank(seeds, 2U, links, 1U,
		components, 2U, component_sizes, 2U, red, blue,
		768.0f * 768.0f, ranked, 1U, &report);
	CHECK(count == 1U);
	CHECK(ranked[0].link_index == 0U);
	CHECK(ranked[0].boundary_action == RL_RUN);
}

static void TestRanksAsymmetricRocketJumpBeforeShorterRun(void)
{
	rune_seed_t seeds[4]; rune_link_t links[2];
	int components[4] = { 0, 1, 2, 3 };
	uint8_t red[4] = { 1, 1, 1, 1 };
	uint8_t blue[4] = { 1, 0, 1, 0 };
	uint32_t component_sizes[4];
	sg_rune_reverse_boundary_candidate_t ranked[2];
	sg_rune_reverse_boundary_report_t report;
	uint32_t count;

	for (int i = 0; i < 4; i++) SetSeed(&seeds[i], 0.0f);
	seeds[1].origin[0] = 64.0f;
	seeds[3].origin[0] = 192.0f;
	SetLink(&links[0], 0, 1, RL_RUN);
	SetLink(&links[1], 2, 3, RL_ROCKETJUMP);
	count = SG_RuneReverseBoundaryRank(seeds, 4U, links, 2U,
		components, 4U, component_sizes, 4U, red, blue,
		768.0f * 768.0f, ranked, 2U, &report);
	CHECK(count == 2U);
	CHECK(ranked[0].link_index == 1U);
	CHECK(ranked[1].link_index == 0U);
}

static void TestReportsProvenanceDistanceAndEndpointRejections(void)
{
	rune_seed_t seeds[6]; rune_link_t links[3];
	int components[6] = { 0, 1, 2, 3, 4, 5 };
	uint8_t red[6] = { 1, 1, 1, 1, 1, 1 };
	uint8_t blue[6] = { 1, 0, 1, 0, 1, 0 };
	uint32_t component_sizes[6];
	sg_rune_reverse_boundary_candidate_t ranked[3];
	sg_rune_reverse_boundary_report_t report;
	uint32_t count;

	for (int i = 0; i < 6; i++) SetSeed(&seeds[i], 0.0f);
	seeds[1].origin[0] = 100.0f; seeds[3].origin[0] = 900.0f;
	seeds[5].origin[0] = 100.0f;
	seeds[4].flags = seeds[5].flags = RSF_WATER;
	SetLink(&links[0], 0, 1, RL_RUN); links[0].provenance = RL_OBSERVED;
	SetLink(&links[1], 2, 3, RL_DROP);
	SetLink(&links[2], 4, 5, RL_SWIM);

	count = SG_RuneReverseBoundaryRank(seeds, 6U, links, 3U,
		components, 6U, component_sizes, 6U, red, blue,
		768.0f * 768.0f, ranked, 3U, &report);
	CHECK(count == 0U);
	CHECK(report.crossing == 3U);
	CHECK(report.rejected_provenance == 1U);
	CHECK(report.rejected_distance == 1U);
	CHECK(report.rejected_endpoints == 1U);
	CHECK(report.action_counts[RL_RUN] == 1U);
	CHECK(report.action_counts[RL_DROP] == 1U);
	CHECK(report.action_counts[RL_SWIM] == 1U);
	CHECK(report.provenance_counts[RL_OBSERVED] == 1U);
	CHECK(report.provenance_counts[RL_PROVEN] == 2U);
}

int main(void)
{
	TestRanksNativeBoundariesBeforeOrdinaryBoundaries();
	TestRanksComponentGainAndCapsOutput();
	TestRanksOneSidedToNeitherWithoutSharedPartition();
	TestRanksExactProvenRunWithoutNativeBoundary();
	TestRanksAsymmetricRocketJumpBeforeShorterRun();
	TestReportsProvenanceDistanceAndEndpointRejections();
	if (failures)
		return 1;
	puts("sg_rune_reverse_boundary_test: ok");
	return 0;
}
