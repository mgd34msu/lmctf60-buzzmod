/* runeaccept.c -- standalone production-loader acceptance for one artifact. */
#include "q_shared.h"
#include "slipgate/sg_rune_file.h"
#include "slipgate/sg_rune_codec.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *Accept_Allocate(int bytes)
{
	if (bytes <= 0)
		return NULL;
	return malloc((size_t)bytes);
}

static void Accept_Release(void *allocation)
{
	free(allocation);
}

static void Accept_FreeRune(rune_t *rune)
{
	if (!rune)
		return;
	free(rune->mechanism_strings);
	free(rune->mechanism_plans);
	free(rune->mechanism_edges);
	free(rune->mechanism_nodes);
	free(rune->links);
	free(rune->seeds);
	free(rune);
}

static int Accept_ReadIdentity(const char *path, rune_identity_t *identity)
{
	unsigned char encoded[SG_RUNE_CODEC_HEADER_BYTES];
	sg_rune_codec_header_t header;
	sg_rune_codec_diagnostic_t diagnostic;
	FILE *file;
	size_t read_size;

	memset(identity, 0, sizeof(*identity));
	file = fopen(path, "rb");
	if (!file)
	{
		fprintf(stderr, "runeaccept: cannot open %s: %s\n", path,
			strerror(errno));
		return 0;
	}
	read_size = fread(encoded, 1U, sizeof(encoded), file);
	if (read_size != sizeof(encoded) || ferror(file))
	{
		fprintf(stderr, "runeaccept: incomplete authenticated header\n");
		(void)fclose(file);
		return 0;
	}
	if (fclose(file) != 0)
	{
		fprintf(stderr, "runeaccept: close failed: %s\n", strerror(errno));
		return 0;
	}
	diagnostic = SG_RuneCodecDecodeHeader(encoded, sizeof(encoded), &header);
	if (diagnostic != RLCODEC_OK)
	{
		fprintf(stderr, "runeaccept: C header rejected (diagnostic=%d)\n",
			(int)diagnostic);
		return 0;
	}
	identity->bsp_checksum = header.bsp_checksum;
	identity->entity_crc32 = header.entity_crc32;
	identity->physics_flags = header.physics_flags;
	identity->gravity = header.gravity;
	identity->airaccelerate = header.airaccelerate;
	identity->maxvelocity = header.maxvelocity;
	identity->pmove_substep_ms = header.pmove_substep_ms;
	identity->server_frame_ms = header.server_frame_ms;
	identity->host_physics_id = header.host_physics_id;
	memcpy(identity->map_name, header.map_name, sizeof(identity->map_name));
	return 1;
}

static uint32_t Accept_TriggerCount(const rune_t *rune)
{
	uint32_t count = 0U;
	uint32_t index;

	for (index = 0U; index < rune->artifact.num_mechanism_nodes; index++)
		switch (rune->mechanism_nodes[index].kind)
		{
		case SG_RUNE_CODEC_NODE_TRIGGER:
		case SG_RUNE_CODEC_NODE_BUTTON:
		case SG_RUNE_CODEC_NODE_RELAY:
		case SG_RUNE_CODEC_NODE_AUTO_DOOR_TRIGGER:
		case SG_RUNE_CODEC_NODE_PLATFORM_TRIGGER:
		case SG_RUNE_CODEC_NODE_ELEVATOR:
		case SG_RUNE_CODEC_NODE_PUSH:
		case SG_RUNE_CODEC_NODE_TELEPORT_TRIGGER:
		case SG_RUNE_CODEC_NODE_OTHER_TRIGGER:
			count++;
			break;
		default:
			break;
		}
	return count;
}

static int Accept_EdgeEqual(const rune_mechanism_edge_t *left,
	const rune_mechanism_edge_t *right)
{
	return left->from_key == right->from_key &&
	       left->to_key == right->to_key && left->kind == right->kind &&
	       left->ordinal == right->ordinal &&
	       left->delay_ms == right->delay_ms;
}

static int Accept_PlanBindings(const rune_t *rune)
{
	uint32_t *references = NULL;
	uint32_t link_index;
	uint32_t plan_index;
	int valid = 0;

	if (rune->artifact.num_mechanism_plans != 0U)
	{
		references = calloc(rune->artifact.num_mechanism_plans,
			sizeof(*references));
		if (!references)
		{
			fprintf(stderr, "runeaccept: plan-reference allocation failed\n");
			return 0;
		}
	}
	for (link_index = 0U; link_index < rune->artifact.num_links; link_index++)
	{
		const rune_link_t *link = &rune->links[link_index];
		int has_plan = link->mechanism_plan != RUNE_NO_MECHANISM_PLAN;

		if (!SG_ActionRuntimeSupported((int)link->action) ||
		    !SG_ActionMechanismAdmitted((int)link->action) ||
		    SG_ActionMechanismPlanRequired((int)link->action) != has_plan)
		{
			fprintf(stderr,
				"runeaccept: link %" PRIu32 " violates plan admission\n",
				link_index);
			goto cleanup;
		}
		if (!has_plan)
			continue;
		if (link->mechanism_plan >= rune->artifact.num_mechanism_plans)
		{
			fprintf(stderr,
				"runeaccept: link %" PRIu32 " has an invalid plan index\n",
				link_index);
			goto cleanup;
		}
		{
			const rune_mechanism_plan_t *plan =
				&rune->mechanism_plans[link->mechanism_plan];

			if (!SG_ActionMechanismPlanAllowed((int)link->action,
			        plan->controller_kind))
			{
				fprintf(stderr,
					"runeaccept: link %" PRIu32
					" has a mismatched controller\n",
					link_index);
				goto cleanup;
			}
		}
		references[link->mechanism_plan]++;
	}
	for (plan_index = 0U;
	     plan_index < rune->artifact.num_mechanism_plans; plan_index++)
	{
		const rune_mechanism_plan_t *plan =
			&rune->mechanism_plans[plan_index];
		uint32_t edge_index;

		if (references[plan_index] != 1U ||
		    plan->first_edge > rune->artifact.num_mechanism_edges ||
		    plan->num_edges >
		        rune->artifact.num_mechanism_edges - plan->first_edge)
		{
			fprintf(stderr,
				"runeaccept: plan %" PRIu32
				" is not uniquely bound or has an invalid slice\n",
				plan_index);
			goto cleanup;
		}
		for (edge_index = 0U; edge_index < plan->num_edges; edge_index++)
		{
			const rune_mechanism_edge_t *edge =
				&rune->mechanism_edges[plan->first_edge + edge_index];
			uint32_t inventory_index;
			int found = 0;

			for (inventory_index = 0U;
			     inventory_index < rune->artifact.num_inventory_edges;
			     inventory_index++)
				if (Accept_EdgeEqual(edge,
				    &rune->mechanism_edges[inventory_index]))
				{
					found = 1;
					break;
				}
			if (!found)
			{
				fprintf(stderr,
					"runeaccept: plan %" PRIu32
					" contains a non-inventory edge\n",
					plan_index);
				goto cleanup;
			}
		}
	}
	valid = 1;

cleanup:
	free(references);
	return valid;
}

int main(int argc, char **argv)
{
	const char *path;
	int require_mechanisms = 0;
	rune_identity_t identity;
	rune_t *rune = NULL;
	sg_rune_file_load_result_t result;
	uint32_t trigger_count;
	uint32_t plan_edge_count;
	int exit_code = 1;

	if (argc == 3 && strcmp(argv[1], "--require-mechanisms") == 0)
	{
		require_mechanisms = 1;
		path = argv[2];
	}
	else if (argc == 2)
		path = argv[1];
	else
	{
		fprintf(stderr,
			"usage: runeaccept [--require-mechanisms] ARTIFACT\n");
		return 2;
	}
	if (!Accept_ReadIdentity(path, &identity))
		return 1;
	result = SG_RuneFileLoad(path, &identity, Accept_Allocate,
		Accept_Release, &rune);
	if (result.status != SG_RUNE_FILE_LOAD_READY || !rune)
	{
		fprintf(stderr, "runeaccept: C production loader rejected at %s: %s",
			result.stage ? result.stage : "unknown",
			result.reason ? result.reason : "unknown reason");
		if (result.index != UINT32_MAX)
			fprintf(stderr, " (index=%" PRIu32 ")", result.index);
		if (result.os_error)
			fprintf(stderr, " (os_error=%d)", result.os_error);
		fputc('\n', stderr);
		goto cleanup;
	}
	if (!Accept_PlanBindings(rune))
		goto cleanup;
	trigger_count = Accept_TriggerCount(rune);
	plan_edge_count = rune->artifact.num_mechanism_edges -
		rune->artifact.num_inventory_edges;
	if (require_mechanisms &&
	    (trigger_count == 0U || rune->artifact.num_mechanism_nodes == 0U ||
	     rune->artifact.num_inventory_edges == 0U ||
	     rune->artifact.num_mechanism_plans == 0U))
	{
		fprintf(stderr,
			"runeaccept: artifact lacks the required nonzero "
			"trigger/node/inventory-edge/plan counts\n");
		goto cleanup;
	}
	printf("{\"edge_count\":%" PRIu32 ",\"inventory_edge_count\":%" PRIu32
	       ",\"link_count\":%" PRIu32 ",\"map_name\":\"%s\""
	       ",\"node_count\":%" PRIu32 ",\"plan_count\":%" PRIu32
	       ",\"plan_edge_count\":%" PRIu32 ",\"seed_count\":%" PRIu32
	       ",\"trigger_count\":%" PRIu32 "}\n",
		rune->artifact.num_mechanism_edges,
		rune->artifact.num_inventory_edges, rune->artifact.num_links,
		rune->artifact.identity.map_name,
		rune->artifact.num_mechanism_nodes,
		rune->artifact.num_mechanism_plans, plan_edge_count,
		rune->artifact.num_seeds, trigger_count);
	exit_code = 0;

cleanup:
	Accept_FreeRune(rune);
	return exit_code;
}
