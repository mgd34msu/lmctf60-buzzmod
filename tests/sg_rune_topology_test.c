#include "q_shared.h"
#include "slipgate/sg_rune_topology.h"
#include "slipgate/sg_rune_topology_game.h"
#include "slipgate/sg_rune_mechanism_catalog.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (0)

typedef struct fixture_s
{
	sg_rune_topology_graph_t graph;
	int calls;
	int reject_from;
	int reject_to;
} fixture_t;

typedef struct game_fixture_s
{
	rune_link_t *links;
	int *link_count;
	int actions[16];
	int froms[16];
	int tos[16];
	int call_count;
	int accept_action[2];
	int fatal_from;
	int fatal_to;
} game_fixture_t;

static sg_rune_topology_proof_result_t Prove(void *data,
	const sg_rune_collision_contact_t *contact, int from, int to)
{
	fixture_t *fixture = data;
	sg_rune_topology_proof_result_t result = {
		SG_RUNE_TOPOLOGY_REJECTED, (uint16_t)(contact->kind + 1U), 7U
	};

	fixture->calls++;
	if (from == fixture->reject_from && to == fixture->reject_to)
		return result;
	if (contact->kind != SG_RUNE_CONTACT_STATIC_DRY)
	{
		result.status = SG_RUNE_TOPOLOGY_DEFERRED;
		return result;
	}
	if (*fixture->graph.link_count >= (int)fixture->graph.link_capacity)
	{
		result.status = SG_RUNE_TOPOLOGY_FATAL;
		return result;
	}
	memset(&fixture->graph.links[*fixture->graph.link_count], 0,
		sizeof(*fixture->graph.links));
	fixture->graph.links[*fixture->graph.link_count].from = from;
	fixture->graph.links[*fixture->graph.link_count].to = to;
	fixture->graph.links[*fixture->graph.link_count].action = RL_RUN;
	fixture->graph.links[*fixture->graph.link_count].cost_ms = 100;
	(*fixture->graph.link_count)++;
	result.status = SG_RUNE_TOPOLOGY_ADDED;
	result.reason = 0U;
	return result;
}

static int InitLedger(sg_rune_contact_ledger_t *ledger,
	sg_rune_collision_contact_t *contacts, uint32_t capacity,
	uint32_t *slots, uint32_t slot_capacity)
{
	return SG_RuneTopologyLedgerInit(ledger, contacts, capacity, slots,
		slot_capacity) == SG_RUNE_TOPOLOGY_OK;
}

static int Reconcile(sg_rune_contact_ledger_t *ledger, fixture_t *fixture,
	sg_rune_topology_outcome_t *outcomes, uint32_t capacity,
	sg_rune_topology_report_t *report)
{
	sg_rune_topology_proof_ops_t ops = { Prove, fixture };

	memset(report, 0, sizeof(*report));
	report->outcomes = outcomes;
	report->outcome_capacity = capacity;
	return SG_RuneTopologyReconcile(ledger, &fixture->graph, &ops, report,
		malloc, free) == SG_RUNE_TOPOLOGY_OK;
}

static int TestLedgerDeduplicatesAndTightens(void)
{
	sg_rune_collision_contact_t contacts[4];
	sg_rune_contact_ledger_t ledger;
	uint32_t slots[8];

	CHECK(InitLedger(&ledger, contacts, 4U, slots, 8U));
	CHECK(SG_RuneTopologyRecordContact(&ledger, 3, 1,
		SG_RUNE_CONTACT_FLOOD_CHILD, SG_RUNE_CONTACT_STATIC_DRY) ==
		SG_RUNE_TOPOLOGY_OK);
	CHECK(SG_RuneTopologyRecordContact(&ledger, 1, 3,
		SG_RUNE_CONTACT_FLOOD_MEETING, SG_RUNE_CONTACT_WATER_BOUNDARY) ==
		SG_RUNE_TOPOLOGY_OK);
	CHECK(ledger.contact_count == 1U);
	CHECK(ledger.contacts[0].low_seed == 1 && ledger.contacts[0].high_seed == 3);
	CHECK(ledger.contacts[0].provenance == 3U);
	CHECK(ledger.contacts[0].kind == SG_RUNE_CONTACT_WATER_BOUNDARY);
	return 0;
}

static int TestCanonicalBatchAndFinalScc(void)
{
	rune_seed_t seeds[3] = {0};
	rune_link_t links[8] = {0};
	int final_components[3] = { -1, -1, -1 };
	sg_rune_collision_contact_t contacts[4];
	sg_rune_topology_outcome_t outcomes[8];
	sg_rune_contact_ledger_t ledger;
	sg_rune_topology_report_t report;
	sg_rune_topology_snapshot_t snapshot = {
		final_components, 3U, 0U, 0U, 0U, 0U
	};
	sg_rune_topology_proof_ops_t ops;
	uint32_t slots[8];
	int link_count = 0;
	fixture_t fixture = {
		{ seeds, 3U, links, &link_count, 8U }, 0, -1, -1
	};

	CHECK(InitLedger(&ledger, contacts, 4U, slots, 8U));
	CHECK(SG_RuneTopologyRecordContact(&ledger, 1, 2,
		SG_RUNE_CONTACT_FLOOD_CHILD, SG_RUNE_CONTACT_STATIC_DRY) ==
		SG_RUNE_TOPOLOGY_OK);
	CHECK(SG_RuneTopologyRecordContact(&ledger, 0, 1,
		SG_RUNE_CONTACT_FLOOD_MEETING, SG_RUNE_CONTACT_STATIC_DRY) ==
		SG_RUNE_TOPOLOGY_OK);
	memset(&report, 0, sizeof(report));
	report.outcomes = outcomes;
	report.outcome_capacity = 8U;
	report.final_snapshot = &snapshot;
	ops = (sg_rune_topology_proof_ops_t){ Prove, &fixture };
	CHECK(SG_RuneTopologyReconcile(&ledger, &fixture.graph, &ops, &report,
		malloc, free) == SG_RUNE_TOPOLOGY_OK);
	CHECK(fixture.calls == 4 && link_count == 4);
	CHECK(report.initial_sccs == 3U && report.final_sccs == 1U);
	CHECK(report.scc_builds == 2U && report.outcome_count == 4U);
	CHECK(snapshot.component_count == 1U);
	CHECK(final_components[0] == final_components[1] &&
		final_components[1] == final_components[2]);
	CHECK(SG_RuneTopologySnapshotCurrent(&fixture.graph, &snapshot));
	CHECK(outcomes[0].from == 0 && outcomes[0].to == 1);
	CHECK(outcomes[1].from == 1 && outcomes[1].to == 0);
	CHECK(outcomes[2].from == 1 && outcomes[2].to == 2);
	CHECK(outcomes[3].from == 2 && outcomes[3].to == 1);
	CHECK(!report.unresolved);
	return 0;
}

static int TestSccSnapshotRejectsStaleGraph(void)
{
	rune_seed_t seeds[3] = {0};
	rune_link_t links[4] = {0};
	int components[3] = { -1, -1, -1 };
	int link_count = 2;
	sg_rune_topology_graph_t graph = {
		seeds, 3U, links, &link_count, 4U
	};
	sg_rune_topology_snapshot_t snapshot = {
		components, 3U, 0U, 0U, 0U, 0U
	};
	uint64_t first_identity, changed_identity;

	links[0].from = 0; links[0].to = 1;
	links[1].from = 1; links[1].to = 0;
	CHECK(SG_RuneTopologySnapshotBuild(&graph, &snapshot, malloc, free) ==
		SG_RUNE_TOPOLOGY_OK);
	CHECK(snapshot.component_count == 2U);
	CHECK(components[0] == components[1] && components[1] != components[2]);
	CHECK(SG_RuneTopologySnapshotCurrent(&graph, &snapshot));
	CHECK(SG_RuneTopologyGraphIdentity(&graph, &first_identity) ==
		SG_RUNE_TOPOLOGY_OK);
	CHECK(first_identity == snapshot.graph_identity);
	links[1].to = 2;
	CHECK(SG_RuneTopologyGraphIdentity(&graph, &changed_identity) ==
		SG_RUNE_TOPOLOGY_OK);
	CHECK(changed_identity != first_identity);
	CHECK(!SG_RuneTopologySnapshotCurrent(&graph, &snapshot));
	links[1].to = 0;
	CHECK(SG_RuneTopologySnapshotCurrent(&graph, &snapshot));
	links[2].from = 1; links[2].to = 2; link_count = 3;
	CHECK(!SG_RuneTopologySnapshotCurrent(&graph, &snapshot));
	CHECK(SG_RuneTopologySnapshotBuild(&graph, &snapshot, malloc, free) ==
		SG_RUNE_TOPOLOGY_OK);
	CHECK(snapshot.component_count == 2U);
	CHECK(SG_RuneTopologySnapshotCurrent(&graph, &snapshot));
	return 0;
}

static int TestExistingAndOneWayDirections(void)
{
	rune_seed_t seeds[2] = {0};
	rune_link_t links[4] = {0};
	sg_rune_collision_contact_t contacts[2];
	sg_rune_topology_outcome_t outcomes[4];
	sg_rune_contact_ledger_t ledger;
	sg_rune_topology_report_t report;
	uint32_t slots[4];
	int link_count = 1;
	fixture_t fixture = {
		{ seeds, 2U, links, &link_count, 4U }, 0, 1, 0
	};

	links[0].from = 0; links[0].to = 1;
	links[0].action = RL_DROP; links[0].cost_ms = 100;
	CHECK(InitLedger(&ledger, contacts, 2U, slots, 4U));
	CHECK(SG_RuneTopologyRecordContact(&ledger, 0, 1,
		SG_RUNE_CONTACT_FLOOD_CHILD, SG_RUNE_CONTACT_STATIC_DRY) ==
		SG_RUNE_TOPOLOGY_OK);
	CHECK(Reconcile(&ledger, &fixture, outcomes, 4U, &report));
	CHECK(fixture.calls == 1 && link_count == 1);
	CHECK(outcomes[0].result == SG_RUNE_TOPOLOGY_PRESENT);
	CHECK(outcomes[1].result == SG_RUNE_TOPOLOGY_REJECTED);
	CHECK(report.final_sccs == 2U && report.unresolved == 2U);
	return 0;
}

static int TestRestrictedOwnersAndOverflow(void)
{
	rune_seed_t seeds[3] = {0};
	rune_link_t links[4] = {0};
	sg_rune_collision_contact_t contacts[2];
	sg_rune_topology_outcome_t outcomes[4];
	sg_rune_contact_ledger_t ledger;
	sg_rune_topology_report_t report;
	uint32_t slots[4];
	int link_count = 0;
	fixture_t fixture = {
		{ seeds, 3U, links, &link_count, 4U }, 0, -1, -1
	};

	CHECK(InitLedger(&ledger, contacts, 1U, slots, 4U));
	CHECK(SG_RuneTopologyRecordContact(&ledger, 0, 1,
		SG_RUNE_CONTACT_FLOOD_CHILD, SG_RUNE_CONTACT_WATER_BOUNDARY) ==
		SG_RUNE_TOPOLOGY_OK);
	CHECK(Reconcile(&ledger, &fixture, outcomes, 4U, &report));
	CHECK(fixture.calls == 2 && link_count == 0 && report.deferred == 2U);
	CHECK(SG_RuneTopologyRecordContact(&ledger, 1, 2,
		SG_RUNE_CONTACT_FLOOD_CHILD, SG_RUNE_CONTACT_STATIC_DRY) ==
		SG_RUNE_TOPOLOGY_CAPACITY);
	CHECK(SG_RuneTopologyReconcile(&ledger, &fixture.graph,
		&(sg_rune_topology_proof_ops_t){ Prove, &fixture }, &report,
		malloc, free) == SG_RUNE_TOPOLOGY_CAPACITY);
	return 0;
}

static int RunPermutation(int reverse, rune_link_t links[8], int *link_count,
	sg_rune_topology_outcome_t outcomes[8], sg_rune_topology_report_t *report)
{
	rune_seed_t seeds[3] = {0};
	sg_rune_collision_contact_t contacts[4];
	sg_rune_contact_ledger_t ledger;
	uint32_t slots[8];
	fixture_t fixture = {
		{ seeds, 3U, links, link_count, 8U }, 0, -1, -1
	};
	int pairs[2][2] = { { 0, 2 }, { 0, 1 } };

	CHECK(InitLedger(&ledger, contacts, 4U, slots, 8U));
	for (int i = 0; i < 2; i++)
	{
		int index = reverse ? 1 - i : i;

		CHECK(SG_RuneTopologyRecordContact(&ledger, pairs[index][0],
			pairs[index][1], SG_RUNE_CONTACT_FLOOD_CHILD,
			SG_RUNE_CONTACT_STATIC_DRY) == SG_RUNE_TOPOLOGY_OK);
	}
	CHECK(Reconcile(&ledger, &fixture, outcomes, 8U, report));
	return 0;
}

static int TestPermutationStable(void)
{
	rune_link_t first_links[8] = {0}, second_links[8] = {0};
	sg_rune_topology_outcome_t first_outcomes[8], second_outcomes[8];
	sg_rune_topology_report_t first_report, second_report;
	int first_count = 0, second_count = 0;

	CHECK(RunPermutation(0, first_links, &first_count, first_outcomes,
		&first_report) == 0);
	CHECK(RunPermutation(1, second_links, &second_count, second_outcomes,
		&second_report) == 0);
	CHECK(first_count == second_count);
	CHECK(memcmp(first_links, second_links,
		(size_t)first_count * sizeof(*first_links)) == 0);
	CHECK(first_report.outcome_count == second_report.outcome_count);
	CHECK(memcmp(first_outcomes, second_outcomes,
		first_report.outcome_count * sizeof(*first_outcomes)) == 0);
	CHECK(first_report.proof_calls == 4U && first_report.scc_builds == 2U);
	return 0;
}

static int GameTryAction(void *data, int from, int to, int action)
{
	game_fixture_t *fixture = data;
	int direction = from < to ? 0 : 1;
	int index = fixture->call_count++;

	if (index < 16)
	{
		fixture->actions[index] = action;
		fixture->froms[index] = from;
		fixture->tos[index] = to;
	}
	if (from == fixture->fatal_from && to == fixture->fatal_to)
		return -1;
	if (action != fixture->accept_action[direction])
		return 0;
	fixture->links[*fixture->link_count].from = from;
	fixture->links[*fixture->link_count].to = to;
	fixture->links[*fixture->link_count].action = (uint8_t)action;
	fixture->links[*fixture->link_count].cost_ms = 100;
	(*fixture->link_count)++;
	return 1;
}

static void SilentPrint(const char *format, ...)
{
	(void)format;
}

static sg_rune_topology_status_t GameRepair(
	sg_rune_contact_kind_t kind, rune_seed_t seeds[2], rune_link_t links[8],
	int *link_count, game_fixture_t *fixture,
	sg_rune_topology_outcome_t outcomes[4], sg_rune_topology_report_t *report)
{
	sg_rune_collision_contact_t contacts[2];
	sg_rune_contact_ledger_t ledger;
	sg_rune_topology_game_request_t request;
	uint32_t slots[4];

	if (!InitLedger(&ledger, contacts, 2U, slots, 4U))
		return SG_RUNE_TOPOLOGY_INVALID;
	if (SG_RuneTopologyRecordContact(&ledger, 0, 1,
	    SG_RUNE_CONTACT_FLOOD_MEETING, kind) != SG_RUNE_TOPOLOGY_OK)
		return SG_RUNE_TOPOLOGY_INVALID;
	memset(&request, 0, sizeof(request));
	request.graph = (sg_rune_topology_graph_t){
		seeds, 2U, links, link_count, 8U
	};
	request.ledger = &ledger;
	request.outcomes = outcomes;
	request.outcome_capacity = 4U;
	request.try_action = GameTryAction;
	request.context = fixture;
	request.allocate = malloc;
	request.release = free;
	request.print = SilentPrint;
	return SG_RuneTopologyGameRepair(&request, report);
}

static int TestGameContactClassification(void)
{
	rune_seed_t seeds[2] = {0};
	rune_mechanism_node_t mover = {0};

	seeds[1].origin[0] = 16.0f;
	CHECK(SG_RuneTopologyGameContactKind(seeds, 0, 1, NULL, 0U) ==
		SG_RUNE_CONTACT_STATIC_DRY);
	seeds[0].flags = RSF_WATER;
	CHECK(SG_RuneTopologyGameContactKind(seeds, 0, 1, NULL, 0U) ==
		SG_RUNE_CONTACT_WATER_BOUNDARY);
	mover.flags = SG_MECH_NODEF_MOVER;
	mover.absmin_q8[0] = 32;
	mover.absmax_q8[0] = 96;
	mover.absmin_q8[1] = mover.absmin_q8[2] = -8;
	mover.absmax_q8[1] = mover.absmax_q8[2] = 8;
	CHECK(SG_RuneTopologyGameContactKind(seeds, 0, 1, &mover, 1U) ==
		SG_RUNE_CONTACT_DECLARED_MOVER);
	return 0;
}

static int TestGameStaticActionRoutes(void)
{
	rune_seed_t seeds[2] = {0};
	rune_link_t links[8] = {0};
	sg_rune_topology_outcome_t outcomes[4];
	sg_rune_topology_report_t report;
	int link_count = 0;
	game_fixture_t fixture = {
		links, &link_count, {0}, {0}, {0}, 0,
		{ RL_RUN, RL_JUMP }, -1, -1
	};

	CHECK(GameRepair(SG_RUNE_CONTACT_STATIC_DRY, seeds, links, &link_count,
		&fixture, outcomes, &report) == SG_RUNE_TOPOLOGY_OK);
	CHECK(link_count == 2 && fixture.call_count == 3);
	CHECK(fixture.actions[0] == RL_RUN);
	CHECK(fixture.actions[1] == RL_RUN && fixture.actions[2] == RL_JUMP);
	CHECK(report.edges_added == 2U && report.scc_builds == 2U);

	memset(links, 0, sizeof(links));
	memset(&fixture, 0, sizeof(fixture));
	link_count = 0;
	seeds[0].origin[2] = 64.0f;
	fixture.links = links;
	fixture.link_count = &link_count;
	fixture.accept_action[0] = RL_DROP;
	fixture.accept_action[1] = RL_RUN;
	fixture.fatal_from = fixture.fatal_to = -1;
	CHECK(GameRepair(SG_RUNE_CONTACT_STATIC_DRY, seeds, links, &link_count,
		&fixture, outcomes, &report) == SG_RUNE_TOPOLOGY_OK);
	CHECK(fixture.call_count == 4 && link_count == 2);
	CHECK(fixture.actions[0] == RL_RUN && fixture.actions[1] == RL_JUMP);
	CHECK(fixture.actions[2] == RL_DROP && fixture.actions[3] == RL_RUN);
	return 0;
}

static int TestGameWaterAndFatal(void)
{
	rune_seed_t seeds[2] = {0};
	rune_link_t links[8] = {0};
	sg_rune_topology_outcome_t outcomes[4];
	sg_rune_topology_report_t report;
	int link_count = 0;
	int batch_mark;
	game_fixture_t fixture = {
		links, &link_count, {0}, {0}, {0}, 0,
		{ RL_SWIM, RL_SWIM }, -1, -1
	};

	seeds[0].flags = RSF_WATER;
	CHECK(GameRepair(SG_RUNE_CONTACT_WATER_BOUNDARY, seeds, links,
		&link_count, &fixture, outcomes, &report) == SG_RUNE_TOPOLOGY_OK);
	CHECK(fixture.call_count == 2 && link_count == 2);
	CHECK(fixture.actions[0] == RL_SWIM && fixture.actions[1] == RL_SWIM);

	memset(links, 0, sizeof(links));
	memset(&fixture, 0, sizeof(fixture));
	link_count = 0;
	seeds[0].flags = 0;
	fixture.links = links;
	fixture.link_count = &link_count;
	fixture.accept_action[0] = RL_RUN;
	fixture.accept_action[1] = RL_RUN;
	fixture.fatal_from = 1;
	fixture.fatal_to = 0;
	batch_mark = link_count;
	CHECK(GameRepair(SG_RUNE_CONTACT_STATIC_DRY, seeds, links, &link_count,
		&fixture, outcomes, &report) == SG_RUNE_TOPOLOGY_OWNER_FATAL);
	CHECK(link_count == 1);
	link_count = batch_mark;
	CHECK(link_count == 0);
	return 0;
}

static int TestGameMoverContactTriesExactOrdinaryMovement(void)
{
	rune_seed_t seeds[2] = {0};
	rune_link_t links[8] = {0};
	sg_rune_topology_outcome_t outcomes[4];
	sg_rune_topology_report_t report;
	int link_count = 0;
	game_fixture_t fixture = {
		links, &link_count, {0}, {0}, {0}, 0,
		{ RL_RUN, RL_JUMP }, -1, -1
	};

	CHECK(GameRepair(SG_RUNE_CONTACT_DECLARED_MOVER, seeds, links,
		&link_count, &fixture, outcomes, &report) == SG_RUNE_TOPOLOGY_OK);
	CHECK(link_count == 2 && fixture.call_count == 3);
	CHECK(outcomes[0].owner == 3U && outcomes[1].owner == 3U);
	CHECK(report.edges_added == 2U && report.deferred == 0U);

	memset(links, 0, sizeof(links));
	memset(&fixture, 0, sizeof(fixture));
	link_count = 0;
	fixture.links = links;
	fixture.link_count = &link_count;
	fixture.accept_action[0] = fixture.accept_action[1] = -1;
	fixture.fatal_from = fixture.fatal_to = -1;
	CHECK(GameRepair(SG_RUNE_CONTACT_DECLARED_MOVER, seeds, links,
		&link_count, &fixture, outcomes, &report) == SG_RUNE_TOPOLOGY_OK);
	CHECK(link_count == 0 && fixture.call_count == 4);
	CHECK(report.deferred == 2U && report.rejected == 0U);

	memset(links, 0, sizeof(links));
	memset(&fixture, 0, sizeof(fixture));
	link_count = 0;
	seeds[0].flags = RSF_WATER;
	fixture.links = links;
	fixture.link_count = &link_count;
	fixture.accept_action[0] = fixture.accept_action[1] = RL_SWIM;
	fixture.fatal_from = fixture.fatal_to = -1;
	CHECK(GameRepair(SG_RUNE_CONTACT_DECLARED_MOVER, seeds, links,
		&link_count, &fixture, outcomes, &report) == SG_RUNE_TOPOLOGY_OK);
	CHECK(link_count == 2 && fixture.call_count == 2);
	CHECK(fixture.actions[0] == RL_SWIM && fixture.actions[1] == RL_SWIM);
	CHECK(report.edges_added == 2U && report.deferred == 0U);

	memset(links, 0, sizeof(links));
	memset(&fixture, 0, sizeof(fixture));
	link_count = 0;
	seeds[0].flags = 0;
	seeds[0].origin[2] = 64.0f;
	fixture.links = links;
	fixture.link_count = &link_count;
	fixture.accept_action[0] = RL_DROP;
	fixture.accept_action[1] = RL_RUN;
	fixture.fatal_from = fixture.fatal_to = -1;
	CHECK(GameRepair(SG_RUNE_CONTACT_DECLARED_MOVER, seeds, links,
		&link_count, &fixture, outcomes, &report) == SG_RUNE_TOPOLOGY_OK);
	CHECK(link_count == 2 && fixture.call_count == 4);
	CHECK(fixture.actions[2] == RL_DROP && fixture.actions[3] == RL_RUN);
	CHECK(report.edges_added == 2U && report.deferred == 0U);
	return 0;
}

static char *ReadSource(const char *path)
{
	FILE *file = fopen(path, "rb");
	char *source;
	long bytes;

	if (!file || fseek(file, 0L, SEEK_END) != 0)
		return NULL;
	bytes = ftell(file);
	if (bytes < 0 || fseek(file, 0L, SEEK_SET) != 0)
	{
		fclose(file);
		return NULL;
	}
	source = malloc((size_t)bytes + 1U);
	if (!source || fread(source, 1U, (size_t)bytes, file) != (size_t)bytes)
	{
		free(source);
		fclose(file);
		return NULL;
	}
	source[bytes] = '\0';
	fclose(file);
	return source;
}

static int TestGeneratorIntegrationOrder(void)
{
	char *source = ReadSource("slipgate/sg_rune.c");
	char *flood, *candidate, *ground, *meeting, *record;
	char *generate, *base, *compound, *rocket, *restore, *audit, *objective;

	CHECK(source != NULL);
	flood = strstr(source, "static qboolean Seed_Flood");
	candidate = flood ? strstr(flood,
		"candidate_near = Seed_NearbyIndex(candidate);") : NULL;
	ground = candidate ? strstr(candidate,
		"if (!Seed_Ground(candidate, ground))")
		: NULL;
	meeting = ground ? strstr(ground,
		"contact = Seed_NearbyIndexPose(ground, true, false);")
		: NULL;
	record = meeting ? strstr(meeting, "SG_RuneTopologyRecordContact") : NULL;
	CHECK(flood && candidate && ground && meeting && record);
	CHECK(strstr(source, "if (Seed_Nearby(cand)) continue;") == NULL);
	CHECK(strstr(candidate, "candidate[2] += 40.0f") == NULL ||
		strstr(candidate, "candidate[2] += 40.0f") > ground);
	CHECK(strstr(ground, "if (!Prove(frontier, contact, false") != NULL);
	CHECK(strstr(meeting,
		"candidate_near >= 0 && contact != candidate_near") != NULL);

	generate = strstr(source, "qboolean Rune_Generate");
	base = generate ? strstr(generate, "Prove_BaseLinks(&compound_topology)")
		: NULL;
	compound = base ? strstr(base, "Link_CompoundDrops()") : NULL;
	rocket = compound ? strstr(compound, "Prove_RocketJumps()") : NULL;
	restore = rocket ? strstr(rocket, "Doors_Restore(&doors)") : NULL;
	audit = restore ? strstr(restore, "SG_RuneTopologyGameRepair") : NULL;
	objective = audit ? strstr(audit,
		"Graph_PruneObjectiveCoreWithClosure") : NULL;
	CHECK(generate && base && compound && rocket && restore && audit &&
		objective);
	free(source);
	return 0;
}

int main(void)
{
	int line;

	if ((line = TestLedgerDeduplicatesAndTightens()) != 0 ||
	    (line = TestCanonicalBatchAndFinalScc()) != 0 ||
	    (line = TestSccSnapshotRejectsStaleGraph()) != 0 ||
	    (line = TestExistingAndOneWayDirections()) != 0 ||
	    (line = TestRestrictedOwnersAndOverflow()) != 0 ||
	    (line = TestPermutationStable()) != 0 ||
	    (line = TestGameContactClassification()) != 0 ||
	    (line = TestGameStaticActionRoutes()) != 0 ||
	    (line = TestGameWaterAndFatal()) != 0 ||
	    (line = TestGameMoverContactTriesExactOrdinaryMovement()) != 0 ||
	    (line = TestGeneratorIntegrationOrder()) != 0)
	{
		fprintf(stderr, "sg_rune_topology_test: failed at line %d\n", line);
		return 1;
	}
	puts("sg_rune_topology_test: ok");
	return 0;
}
