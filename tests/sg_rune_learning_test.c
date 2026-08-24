#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "q_shared.h"
#include "slipgate/sg_rune_learning.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void Fixture(rune_t *rune, rune_seed_t seeds[3],
	rune_link_t links[1])
{
	rune_identity_t *identity;

	memset(rune, 0, sizeof(*rune));
	memset(seeds, 0, sizeof(*seeds) * 3U);
	memset(links, 0, sizeof(*links));
	rune->seeds = seeds;
	rune->links = links;
	rune->hdr.num_seeds = 3;
	rune->hdr.num_links = 1;
	rune->artifact.magic = RUNE_ARTIFACT_MAGIC;
	rune->artifact.route_contract = RUNE_ROUTE_CONTRACT_LOCAL_ONLY;
	rune->artifact.payload_crc32 = 33;
	rune->artifact.header_crc32 = 44;
	rune->artifact.action_contract_crc32 = 55;
	rune->artifact.mechanism_contract_crc32 = 66;
	rune->artifact.num_seeds = 3;
	rune->artifact.num_links = 1;
	rune->artifact.num_mechanism_nodes = 7;
	rune->artifact.num_mechanism_edges = 8;
	rune->artifact.num_inventory_edges = 4;
	rune->artifact.num_mechanism_plans = 2;
	rune->artifact.string_bytes = 9;
	identity = &rune->artifact.identity;
	strcpy(identity->map_name, "testmap");
	identity->bsp_checksum = 11;
	identity->entity_crc32 = 22;
	identity->physics_flags = 0;
	identity->gravity = 800.0f;
	identity->airaccelerate = 0.0f;
	identity->maxvelocity = 2000.0f;
	identity->pmove_substep_ms = 25;
	identity->server_frame_ms = 100;
	identity->host_physics_id = 1;
	memset(rune->encoded_sha256, 'b', 64);
	rune->encoded_sha256[64] = '\0';
	seeds[0].origin[0] = 0.0f;
	seeds[1].origin[0] = 128.0f;
	seeds[1].flags = RSF_TOMBSTONE;
	seeds[2].origin[0] = 256.0f;
	links[0].from = 2;
	links[0].to = 0;
	links[0].action = RL_RUN;
	links[0].cost_ms = 100;
}

static void Header(FILE *file, unsigned format, unsigned candidates)
{
	fprintf(file, "rlearn_format %u\nmap testmap\n", format);
	fputs("bsp_checksum 11\nentity_crc 22\nphysics_flags 0\n", file);
	fputs("gravity 800\nairaccelerate 0\nmaxvelocity 2000\n", file);
	fputs("pmove_ms 25\nframe_ms 100\nhost_physics_id 1\n", file);
	fputs("source_route_contract 1\nrune_payload_crc 33\n", file);
	fputs("rune_header_crc 44\nrune_action_contract_crc 55\n", file);
	fputs("rune_mechanism_contract_crc 66\nrune_num_seeds 3\n", file);
	fputs("rune_num_links 1\nrune_num_mechanism_nodes 7\n", file);
	fputs("rune_num_mechanism_edges 8\nrune_num_inventory_edges 4\n", file);
	fputs("rune_num_mechanism_plans 2\nrune_string_bytes 9\n", file);
	fputs("rune_sha256 "
		"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n",
		file);
	fputs("trace_sha256 "
		"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n",
		file);
	fputs("replay_sha256 "
		"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\n",
		file);
	fprintf(file, "candidates %u\n", candidates);
}

static void Candidate(FILE *file)
{
	fputs("candidate 0 0 0 0 1 1024 0 0 1 1 512 512 0 1 4\n", file);
}

static void CandidateSameEndpointsDifferentEvidence(FILE *file)
{
	fputs("candidate 0 0 0 0 1 1024 0 0 1 1 640 512 0 2 5\n", file);
}

static void HookCandidate(FILE *file, int bite_y, int aim_yaw)
{
	fprintf(file, "hook_candidate 0 0 0 0 2 2048 0 0 1 -1024 %d 0 0 "
		"1000 %d 300 0 0 0\n", aim_yaw, bite_y);
}

static void Write(const char *path, unsigned format, unsigned candidates,
	int duplicate, unsigned hooks, const char *tail)
{
	FILE *file = fopen(path, "wb");

	if (!file)
	{
		perror(path);
		exit(2);
	}
	Header(file, format, candidates);
	if (candidates)
		Candidate(file);
	if (duplicate)
		Candidate(file);
	if (format == 2U)
	{
		fprintf(file, "hook_candidates %u\n", hooks);
		if (hooks)
			HookCandidate(file, 64, 4096);
	}
	if (tail)
		fputs(tail, file);
	if (fclose(file) != 0)
		exit(2);
}

int main(void)
{
	const char *path = "/tmp/sg_rune_learning_test.input";
	rune_t rune;
	rune_seed_t seeds[3];
	rune_link_t links[1], before[1];
	sg_rune_learning_candidate_t runs[2];
	sg_rune_learning_hook_candidate_t hooks[9];
	sg_rune_learning_storage_t storage;
	sg_rune_learning_evidence_t evidence;

	Fixture(&rune, seeds, links);
	memset(&storage, 0, sizeof(storage));
	storage.runs = runs;
	storage.run_capacity = 2U;
	storage.hooks = hooks;
	storage.hook_capacity = 9U;
	memcpy(before, links, sizeof(before));
	Write(path, 1, 1, 0, 0, NULL);
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_READY);
	CHECK(evidence.candidate_count == 1);
	CHECK(evidence.candidates == runs);
	CHECK(evidence.hook_candidate_count == 0U);
	CHECK(evidence.hook_candidates == hooks);
	CHECK(runs[0].source_from == 0 && runs[0].source_to == 1);
	CHECK(runs[0].waypoint_q8[0] == 512);
	CHECK(memcmp(before, links, sizeof(before)) == 0);

	Write(path, 2, 1, 0, 1, NULL);
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_READY);
	CHECK(evidence.candidate_count == 1U);
	CHECK(evidence.hook_candidate_count == 1U);
	CHECK(hooks[0].source_from == 0U && hooks[0].source_to == 2U);
	CHECK(hooks[0].rope_count == 1U);
	CHECK(hooks[0].aim_short[0][0] == -1024);
	CHECK(hooks[0].aim_short[0][1] == 4096);
	CHECK(hooks[0].bite_q8[0][0] == 1000);
	CHECK(hooks[0].bite_q8[0][1] == 64);
	{
		FILE *file = fopen(path, "wb");
		int index;

		CHECK(file != NULL);
		if (file)
		{
			Header(file, 2, 0);
			fputs("hook_candidates 9\n", file);
			for (index = 0; index < 9; index++)
				HookCandidate(file, index, 4096 + index);
			CHECK(fclose(file) == 0);
		}
	}
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_REJECTED);
	{
		FILE *file = fopen(path, "wb");

		CHECK(file != NULL);
		if (file)
		{
			Header(file, 2, 0);
			fputs("hook_candidates 2\n", file);
			HookCandidate(file, 64, 4096);
			HookCandidate(file, 64, 8192);
			CHECK(fclose(file) == 0);
		}
	}
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_REJECTED);

	rune.artifact.payload_crc32++;
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_REJECTED);
	rune.artifact.payload_crc32--;
	rune.encoded_sha256[0] = 'd';
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_REJECTED);
	rune.encoded_sha256[0] = 'b';
	rune.artifact.route_contract = RUNE_ROUTE_CONTRACT_COMPLETE;
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_REJECTED);
	rune.artifact.route_contract = RUNE_ROUTE_CONTRACT_LOCAL_ONLY;

	Write(path, 1, 2, 1, 0, NULL);
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_REJECTED);
	{
		FILE *file = fopen(path, "wb");

		CHECK(file != NULL);
		if (file)
		{
			Header(file, 1, 2);
			Candidate(file);
			CandidateSameEndpointsDifferentEvidence(file);
			CHECK(fclose(file) == 0);
		}
	}
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_REJECTED);
	Write(path, 1, 1, 0, 0, "trailing\n");
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_REJECTED);
	Write(path, 2, 1, 0, 1, NULL);
	storage.hook_capacity = 0U;
	CHECK(SG_RuneLearningLoadFile(&rune, path, &storage, &evidence) ==
		SG_RUNE_LEARNING_REJECTED);
	storage.hook_capacity = 9U;
	CHECK(SG_RuneLearningLoadFile(&rune, "/tmp/no-rlearn-file", &storage,
		&evidence) == SG_RUNE_LEARNING_MISSING);

	remove(path);
	if (failures)
	{
		fprintf(stderr, "sg_rune_learning_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rune_learning_test: ok");
	return 0;
}
