#include "slipgate/sg_field_attractor.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
			#expression); \
		failures++; \
	} \
} while (0)

static sg_field_attractor_graph_t Graph(size_t state_count,
	const uint8_t *terminals, const sg_field_attractor_choice_t *choices,
	size_t choice_count, const uint32_t *destinations,
	size_t destination_count)
{
	sg_field_attractor_graph_t graph;

	memset(&graph, 0, sizeof(graph));
	graph.state_count = state_count;
	graph.terminal_states = terminals;
	graph.choices = choices;
	graph.choice_count = choice_count;
	graph.choice_destinations = destinations;
	graph.choice_destination_count = destination_count;
	return graph;
}

static void TestOrChoicesAndCompleteOutcomes(void)
{
	const uint8_t terminals[4] = { 1U, 0U, 0U, 0U };
	const uint32_t destinations[] = { 0U, 3U, 0U, 1U, 3U };
	const sg_field_attractor_choice_t choices[] = {
		{ 1U, { 0U, 2U } },
		{ 1U, { 2U, 1U } },
		{ 2U, { 3U, 2U } }
	};
	sg_field_attractor_graph_t graph = Graph(4U, terminals, choices, 3U,
		destinations, 5U);
	sg_field_attractor_result_t result = { 0 };

	/* Choice 0 cannot prove state 1 because its second outcome is cut.
	 * Choice 1 independently proves it. Choice 2 remains cut because every
	 * selectable alternative must keep all of its outcomes in the attractor. */
	graph.choice_count = 1U;
	graph.choice_destination_count = 2U;
	CHECK(SG_FieldAttractorSolve(&graph, &result) == SG_FIELD_ATTRACTOR_OK);
	CHECK(result.reachable[1] == 0U);
	SG_FieldAttractorResultDestroy(&result);
	graph.choice_count = 3U;
	graph.choice_destination_count = 5U;
	CHECK(SG_FieldAttractorSolve(&graph, &result) == SG_FIELD_ATTRACTOR_OK);
	CHECK(SG_FieldAttractorVerify(&graph, &result));
	CHECK(result.reachable[0] == 1U && result.rank[0] == 0U);
	CHECK(result.reachable[1] == 1U && result.rank[1] == 1U);
	CHECK(result.witness_kind[1] == SG_FIELD_ATTRACTOR_WITNESS_CHOICE);
	CHECK(result.witness_index[1] == 1U);
	CHECK(result.reachable[2] == 0U && result.reachable[3] == 0U);
	SG_FieldAttractorResultDestroy(&result);
}

static void TestZeroTimeCycles(void)
{
	const uint8_t terminals[5] = { 1U, 0U, 0U, 0U, 0U };
	const uint32_t destinations[] = { 1U, 0U, 1U, 4U, 3U };
	const sg_field_attractor_choice_t choices[] = {
		{ 1U, { 0U, 1U } }, /* legal self-loop, not selected */
		{ 1U, { 1U, 1U } }, /* exit */
		{ 2U, { 2U, 1U } }, /* reaches through state 1 */
		{ 3U, { 3U, 1U } }, /* closed two-state SCC */
		{ 4U, { 4U, 1U } }
	};
	sg_field_attractor_graph_t graph = Graph(5U, terminals, choices, 5U,
		destinations, 5U);
	sg_field_attractor_result_t result = { 0 };

	CHECK(SG_FieldAttractorSolve(&graph, &result) == SG_FIELD_ATTRACTOR_OK);
	CHECK(SG_FieldAttractorVerify(&graph, &result));
	CHECK(result.reachable[1] == 1U && result.rank[1] == 1U);
	CHECK(result.witness_index[1] == 1U);
	CHECK(result.reachable[2] == 1U && result.rank[2] == 2U);
	CHECK(result.reachable[3] == 0U && result.reachable[4] == 0U);
	result.reachable[3] = 1U;
	CHECK(!SG_FieldAttractorVerify(&graph, &result));
	result.reachable[3] = 0U;
	SG_FieldAttractorResultDestroy(&result);
}

static void TestLocalProgressWitness(void)
{
	const uint8_t terminals[4] = { 1U, 0U, 0U, 0U };
	const uint32_t choice_destinations[] = { 3U };
	const uint32_t progress_destinations[] = { 0U, 1U };
	const sg_field_attractor_choice_t choices[] = { { 3U, { 0U, 1U } } };
	const sg_field_attractor_progress_t progress[] = {
		{ 1U, { 0U, 1U } },
		{ 2U, { 1U, 1U } }
	};
	sg_field_attractor_graph_t graph = Graph(4U, terminals, choices, 1U,
		choice_destinations, 1U);
	sg_field_attractor_result_t result = { 0 };

	graph.progress = progress;
	graph.progress_count = 2U;
	graph.progress_destinations = progress_destinations;
	graph.progress_destination_count = 2U;
	CHECK(SG_FieldAttractorSolve(&graph, &result) == SG_FIELD_ATTRACTOR_OK);
	CHECK(SG_FieldAttractorVerify(&graph, &result));
	CHECK(result.reachable[1] == 1U && result.rank[1] == 1U);
	CHECK(result.witness_kind[1] ==
		SG_FIELD_ATTRACTOR_WITNESS_LOCAL_PROGRESS);
	CHECK(result.reachable[2] == 1U && result.rank[2] == 2U);
	CHECK(result.reachable[3] == 0U);
	SG_FieldAttractorResultDestroy(&result);
}

static void TestInvalidGraphsFailLoudly(void)
{
	const uint8_t terminals[2] = { 1U, 0U };
	uint32_t destinations[] = { 2U };
	sg_field_attractor_choice_t choice = { 1U, { 0U, 1U } };
	sg_field_attractor_graph_t graph = Graph(2U, terminals, &choice, 1U,
		destinations, 1U);
	sg_field_attractor_result_t result = { 0 };

	CHECK(SG_FieldAttractorSolve(&graph, &result) ==
		SG_FIELD_ATTRACTOR_INVALID);
	destinations[0] = 0U;
	choice.destinations.count = 0U;
	CHECK(SG_FieldAttractorSolve(&graph, &result) ==
		SG_FIELD_ATTRACTOR_INVALID);
}

int main(void)
{
	TestOrChoicesAndCompleteOutcomes();
	TestZeroTimeCycles();
	TestLocalProgressWitness();
	TestInvalidGraphsFailLoudly();
	if (failures != 0)
	{
		fprintf(stderr, "sg_field_attractor_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_field_attractor_test: ok");
	return 0;
}
