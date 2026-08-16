/* sg_rune_artifact_loader.c -- isolated transactional RUNE artifact publication. */
#include "sg_rune_artifact_loader.h"

#include <string.h>

#define SG_RUNE_ARTIFACT_PUBLICATION_STATE UINT32_C(0x34564c53)
#define SG_RUNE_ARTIFACT_PUBLICATION_STATE_INVERSE UINT32_C(0xcba9b3ac)

typedef struct loader_range_s
{
	const void *data;
	size_t bytes;
} loader_range_t;

static sg_rune_codec_diagnostic_t Loader_FromBaseDiagnostic(
	rune_wire_diagnostic_t diagnostic)
{
	return (sg_rune_codec_diagnostic_t)(int)diagnostic;
}

static int Loader_AddRange(loader_range_t *ranges, size_t range_capacity,
	size_t *range_count, const void *data, size_t elements,
	size_t element_size)
{
	loader_range_t *range;

	if (elements == 0U || !data)
		return 1;
	if (!ranges || !range_count || *range_count >= range_capacity ||
	    element_size == 0U || elements > SIZE_MAX / element_size)
		return 0;
	range = &ranges[*range_count];
	range->data = data;
	range->bytes = elements * element_size;
	(*range_count)++;
	return 1;
}

static int Loader_RangesOverlap(const loader_range_t *left,
	const loader_range_t *right)
{
	uintptr_t left_start;
	uintptr_t right_start;
	uintptr_t left_end;
	uintptr_t right_end;

	if (!left || !right || !left->data || !right->data ||
	    left->bytes == 0U || right->bytes == 0U)
		return 0;
	left_start = (uintptr_t)left->data;
	right_start = (uintptr_t)right->data;
	if (left->bytes > (size_t)(UINTPTR_MAX - left_start) ||
	    right->bytes > (size_t)(UINTPTR_MAX - right_start))
		return 1;
	left_end = left_start + (uintptr_t)left->bytes;
	right_end = right_start + (uintptr_t)right->bytes;
	return left_start < right_end && right_start < left_end;
}

static int Loader_CrossDisjoint(const loader_range_t *protected_ranges,
	size_t protected_count, const loader_range_t *write_ranges,
	size_t write_count)
{
	size_t protected_index;
	size_t write_index;

	for (protected_index = 0U; protected_index < protected_count;
	     protected_index++)
		for (write_index = 0U; write_index < write_count;
		     write_index++)
			if (Loader_RangesOverlap(
			    &protected_ranges[protected_index],
			    &write_ranges[write_index]))
				return 0;
	return 1;
}

static int Loader_StatePublished(const sg_rune_artifact_loader_t *loader)
{
	return loader &&
	       loader->publication_state == SG_RUNE_ARTIFACT_PUBLICATION_STATE &&
	       loader->publication_state_inverse ==
	           SG_RUNE_ARTIFACT_PUBLICATION_STATE_INVERSE;
}

static int Loader_ShapePublished(const sg_rune_artifact_loader_t *loader)
{
	if (!Loader_StatePublished(loader) ||
	    SG_RuneCodecMatchIdentity(&loader->header, NULL) != RLCODEC_OK ||
	    !loader->seeds || !loader->strings ||
	    (loader->header.num_links != 0U && !loader->links) ||
	    (loader->header.num_activation_nodes != 0U && !loader->nodes) ||
	    (loader->header.num_activation_edges != 0U && !loader->edges) ||
	    (loader->header.num_activation_plans != 0U && !loader->plans))
		return 0;
	return 1;
}

static int Loader_BackingCapacityReady(
	const sg_rune_artifact_backing_t *backing,
	const sg_rune_codec_header_t *header)
{
	if (!backing || !header || !backing->seeds ||
	    backing->seed_capacity < (size_t)header->num_seeds ||
	    !backing->strings ||
	    backing->string_capacity < (size_t)header->string_bytes)
		return 0;
	if (header->num_links != 0U &&
	    (!backing->links ||
	     backing->link_capacity < (size_t)header->num_links))
		return 0;
	if (header->num_activation_nodes != 0U &&
	    (!backing->nodes ||
	     backing->node_capacity <
	         (size_t)header->num_activation_nodes))
		return 0;
	if (header->num_activation_edges != 0U &&
	    (!backing->edges ||
	     backing->edge_capacity <
	         (size_t)header->num_activation_edges))
		return 0;
	if (header->num_activation_plans != 0U &&
	    (!backing->plans ||
	     backing->plan_capacity <
	         (size_t)header->num_activation_plans))
		return 0;
	return 1;
}

static int Loader_AddBackingRanges(loader_range_t *ranges,
	size_t range_capacity, size_t *range_count,
	const sg_rune_artifact_backing_t *backing,
	const sg_rune_codec_header_t *header)
{
	return Loader_AddRange(ranges, range_capacity, range_count,
	           backing->seeds, header->num_seeds,
	           sizeof(backing->seeds[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           backing->links, header->num_links,
	           sizeof(backing->links[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           backing->nodes, header->num_activation_nodes,
	           sizeof(backing->nodes[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           backing->edges, header->num_activation_edges,
	           sizeof(backing->edges[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           backing->plans, header->num_activation_plans,
	           sizeof(backing->plans[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           backing->strings, header->string_bytes,
	           sizeof(backing->strings[0]));
}

static int Loader_AddPublishedRanges(loader_range_t *ranges,
	size_t range_capacity, size_t *range_count,
	const sg_rune_artifact_loader_t *loader)
{
	const sg_rune_codec_header_t *header = &loader->header;

	return Loader_AddRange(ranges, range_capacity, range_count,
	           loader->seeds, header->num_seeds,
	           sizeof(loader->seeds[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           loader->links, header->num_links,
	           sizeof(loader->links[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           loader->nodes, header->num_activation_nodes,
	           sizeof(loader->nodes[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           loader->edges, header->num_activation_edges,
	           sizeof(loader->edges[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           loader->plans, header->num_activation_plans,
	           sizeof(loader->plans[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           loader->strings, header->string_bytes,
	           sizeof(loader->strings[0]));
}

static int Loader_AddWorkspaceRanges(loader_range_t *ranges,
	size_t range_capacity, size_t *range_count,
	sg_rune_codec_workspace_t *workspace,
	const sg_rune_codec_header_t *header)
{
	if (!Loader_AddRange(ranges, range_capacity, range_count, workspace,
	    1U, sizeof(*workspace)))
		return 0;
	if (!workspace)
		return 1;
	return Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->graph_link_keys, header->num_links,
	           sizeof(workspace->graph_link_keys[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->graph_source_marks, header->num_seeds,
	           sizeof(workspace->graph_source_marks[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->plan_references,
	           header->num_activation_plans,
	           sizeof(workspace->plan_references[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->node_references,
	           header->num_activation_nodes,
	           sizeof(workspace->node_references[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->node_heads, header->num_activation_nodes,
	           sizeof(workspace->node_heads[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->node_indegrees,
	           header->num_activation_nodes,
	           sizeof(workspace->node_indegrees[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->node_generations,
	           header->num_activation_nodes,
	           sizeof(workspace->node_generations[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->node_touched,
	           header->num_activation_nodes,
	           sizeof(workspace->node_touched[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->node_queue, header->num_activation_nodes,
	           sizeof(workspace->node_queue[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->edge_next, header->num_activation_edges,
	           sizeof(workspace->edge_next[0])) &&
	       Loader_AddRange(ranges, range_capacity, range_count,
	           workspace->string_marks, header->string_bytes,
	           sizeof(workspace->string_marks[0]));
}

static int Loader_PublicationRangesSafe(
	const sg_rune_artifact_loader_t *loader,
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_codec_identity_t *expected_identity,
	const sg_rune_artifact_backing_t *backing,
	sg_rune_codec_workspace_t *workspace,
	const sg_rune_codec_header_t *header)
{
	loader_range_t protected_ranges[10];
	loader_range_t write_ranges[20];
	loader_range_t source_range;
	loader_range_t loader_range;
	loader_range_t backing_range;
	size_t protected_count = 0U;
	size_t write_count = 0U;

	memset(protected_ranges, 0, sizeof(protected_ranges));
	memset(write_ranges, 0, sizeof(write_ranges));
	memset(&source_range, 0, sizeof(source_range));
	memset(&loader_range, 0, sizeof(loader_range));
	memset(&backing_range, 0, sizeof(backing_range));
	if (!Loader_AddRange(protected_ranges,
	        sizeof(protected_ranges) / sizeof(protected_ranges[0]),
	        &protected_count, loader, 1U, sizeof(*loader)) ||
	    !Loader_AddRange(protected_ranges,
	        sizeof(protected_ranges) / sizeof(protected_ranges[0]),
	        &protected_count, backing, 1U, sizeof(*backing)) ||
	    (Loader_StatePublished(loader) &&
	     !Loader_AddPublishedRanges(protected_ranges,
	         sizeof(protected_ranges) / sizeof(protected_ranges[0]),
	         &protected_count, loader)) ||
	    !Loader_AddBackingRanges(write_ranges,
	        sizeof(write_ranges) / sizeof(write_ranges[0]), &write_count,
	        backing, header) ||
	    !Loader_AddWorkspaceRanges(write_ranges,
	        sizeof(write_ranges) / sizeof(write_ranges[0]), &write_count,
	        workspace, header) ||
	    !Loader_CrossDisjoint(protected_ranges, protected_count,
	        write_ranges, write_count))
		return 0;
	source_range.data = encoded;
	source_range.bytes = encoded_size;
	loader_range.data = loader;
	loader_range.bytes = sizeof(*loader);
	backing_range.data = backing;
	backing_range.bytes = sizeof(*backing);
	if (Loader_RangesOverlap(&source_range, &loader_range) ||
	    Loader_RangesOverlap(&source_range, &backing_range) ||
	    Loader_RangesOverlap(&loader_range, &backing_range))
		return 0;
	source_range.data = expected_identity;
	source_range.bytes = sizeof(*expected_identity);
	return !Loader_RangesOverlap(&source_range, &loader_range);
}

void SG_RuneArtifactLoaderReset(sg_rune_artifact_loader_t *loader)
{
	if (loader)
		memset(loader, 0, sizeof(*loader));
}

sg_rune_codec_diagnostic_t SG_RuneArtifactLoaderLoad(
	sg_rune_artifact_loader_t *loader,
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_codec_identity_t *expected_identity,
	const sg_rune_artifact_backing_t *backing,
	sg_rune_codec_workspace_t *workspace)
{
	sg_rune_artifact_backing_t candidate;
	sg_rune_artifact_loader_t publication;
	sg_rune_codec_header_t header;
	sg_rune_codec_diagnostic_t diagnostic;
	size_t expected_size;

	if (!loader || !encoded || !expected_identity || !backing || !workspace)
		return Loader_FromBaseDiagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size < SG_RUNE_CODEC_HEADER_BYTES)
		return Loader_FromBaseDiagnostic(RLW_BAD_FILE_SIZE);
	candidate = *backing;
	diagnostic = SG_RuneCodecDecodeHeader(encoded,
		SG_RUNE_CODEC_HEADER_BYTES, &header);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	diagnostic = SG_RuneCodecFileSize(header.num_seeds, header.num_links,
		header.num_activation_nodes, header.num_activation_edges,
		header.num_activation_plans, header.string_bytes,
		&expected_size);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	if (encoded_size != expected_size)
		return Loader_FromBaseDiagnostic(RLW_BAD_FILE_SIZE);
	if (!Loader_BackingCapacityReady(&candidate, &header))
		return Loader_FromBaseDiagnostic(RLW_ALLOCATION_FAILED);
	if (!Loader_PublicationRangesSafe(loader, encoded, encoded_size,
	    expected_identity, backing, workspace, &header))
		return Loader_FromBaseDiagnostic(RLW_INVALID_ARGUMENT);
	diagnostic = SG_RuneCodecDecode(encoded, encoded_size,
		expected_identity, &header, candidate.seeds,
		candidate.seed_capacity, candidate.links,
		candidate.link_capacity, candidate.nodes,
		candidate.node_capacity, candidate.edges,
		candidate.edge_capacity, candidate.plans,
		candidate.plan_capacity, candidate.strings,
		candidate.string_capacity, workspace);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;

	/* The decoded bank is now complete and infallible to publish.  Building a
	 * zeroed local value keeps padding deterministic for bytewise rollback
	 * checks and makes the structure assignment the sole publication step. */
	memset(&publication, 0, sizeof(publication));
	publication.publication_state = SG_RUNE_ARTIFACT_PUBLICATION_STATE;
	publication.publication_state_inverse =
		SG_RUNE_ARTIFACT_PUBLICATION_STATE_INVERSE;
	publication.header = header;
	publication.seeds = candidate.seeds;
	publication.links = candidate.links;
	publication.nodes = candidate.nodes;
	publication.edges = candidate.edges;
	publication.plans = candidate.plans;
	publication.strings = candidate.strings;
	*loader = publication;
	return RLCODEC_OK;
}

int SG_RuneArtifactLoaderIsPublished(const sg_rune_artifact_loader_t *loader)
{
	return Loader_ShapePublished(loader);
}

const sg_rune_codec_header_t *SG_RuneArtifactLoaderHeader(
	const sg_rune_artifact_loader_t *loader)
{
	return Loader_ShapePublished(loader) ? &loader->header : NULL;
}

const sg_rune_codec_seed_t *SG_RuneArtifactLoaderSeedAt(
	const sg_rune_artifact_loader_t *loader, uint32_t index)
{
	if (!Loader_ShapePublished(loader) || index >= loader->header.num_seeds)
		return NULL;
	return &loader->seeds[index];
}

const sg_rune_codec_link_t *SG_RuneArtifactLoaderLinkAt(
	const sg_rune_artifact_loader_t *loader, uint32_t index)
{
	if (!Loader_ShapePublished(loader) || index >= loader->header.num_links)
		return NULL;
	return &loader->links[index];
}

const sg_rune_codec_activation_node_t *SG_RuneArtifactLoaderNodeAt(
	const sg_rune_artifact_loader_t *loader, uint32_t index)
{
	if (!Loader_ShapePublished(loader) ||
	    index >= loader->header.num_activation_nodes)
		return NULL;
	return &loader->nodes[index];
}

const sg_rune_codec_activation_edge_t *SG_RuneArtifactLoaderEdgeAt(
	const sg_rune_artifact_loader_t *loader, uint32_t index)
{
	if (!Loader_ShapePublished(loader) ||
	    index >= loader->header.num_activation_edges)
		return NULL;
	return &loader->edges[index];
}

const sg_rune_codec_activation_plan_t *SG_RuneArtifactLoaderPlanAt(
	const sg_rune_artifact_loader_t *loader, uint32_t index)
{
	if (!Loader_ShapePublished(loader) ||
	    index >= loader->header.num_activation_plans)
		return NULL;
	return &loader->plans[index];
}

const sg_rune_codec_activation_node_t *SG_RuneArtifactLoaderNodeByKey(
	const sg_rune_artifact_loader_t *loader, uint32_t key)
{
	uint32_t low = 0U;
	uint32_t high;

	if (!Loader_ShapePublished(loader))
		return NULL;
	high = loader->header.num_activation_nodes;
	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		uint32_t middle_key = loader->nodes[middle].key;

		if (middle_key < key)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= loader->header.num_activation_nodes ||
	    loader->nodes[low].key != key)
		return NULL;
	return &loader->nodes[low];
}

const sg_rune_codec_activation_plan_t *SG_RuneArtifactLoaderPlanForLink(
	const sg_rune_artifact_loader_t *loader, uint32_t link_index)
{
	const sg_rune_codec_link_t *link = SG_RuneArtifactLoaderLinkAt(loader,
		link_index);

	if (!link ||
	    link->activation_plan == SG_RUNE_CODEC_NO_ACTIVATION_PLAN)
		return NULL;
	return SG_RuneArtifactLoaderPlanAt(loader, link->activation_plan);
}

const unsigned char *SG_RuneArtifactLoaderStringAt(
	const sg_rune_artifact_loader_t *loader, uint32_t offset)
{
	uint32_t cursor;

	if (!Loader_ShapePublished(loader) ||
	    offset >= loader->header.string_bytes ||
	    (offset != 0U &&
	     (loader->strings[offset] == 0U ||
	      loader->strings[offset - 1U] != 0U)))
		return NULL;
	for (cursor = offset; cursor < loader->header.string_bytes; cursor++)
		if (loader->strings[cursor] == 0U)
			return loader->strings + offset;
	return NULL;
}
