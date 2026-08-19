/* sg_rune_artifact_loader.h -- isolated transactional RUNE artifact publication. */
#ifndef SG_RUNE_ARTIFACT_LOADER_H
#define SG_RUNE_ARTIFACT_LOADER_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_codec.h"

/* A backing bank becomes loader-owned after a successful load.  The caller
 * must not mutate or reuse any published record/string storage until Reset or
 * a later successful load publishes a different, disjoint bank.  Keeping two
 * banks lets callers attempt reloads transactionally without allocation. */
typedef struct sg_rune_artifact_backing_s
{
	sg_rune_codec_seed_t *seeds;
	size_t seed_capacity;
	sg_rune_codec_link_t *links;
	size_t link_capacity;
	sg_rune_codec_activation_node_t *nodes;
	size_t node_capacity;
	sg_rune_codec_activation_edge_t *edges;
	size_t edge_capacity;
	sg_rune_codec_activation_plan_t *plans;
	size_t plan_capacity;
	unsigned char *strings;
	size_t string_capacity;
} sg_rune_artifact_backing_t;

/* Published fields are exposed only through const-returning queries.  The
 * structure is public solely so it can live in caller-provided static/stack
 * storage; callers initialize it with Reset and otherwise treat it as opaque. */
typedef struct sg_rune_artifact_loader_s
{
	uint32_t publication_state;
	uint32_t publication_state_inverse;
	sg_rune_codec_header_t header;
	const sg_rune_codec_seed_t *seeds;
	const sg_rune_codec_link_t *links;
	const sg_rune_codec_activation_node_t *nodes;
	const sg_rune_codec_activation_edge_t *edges;
	const sg_rune_codec_activation_plan_t *plans;
	const unsigned char *strings;
} sg_rune_artifact_loader_t;

/* Reset invalidates every publication/query and clears the owned header and
 * pointers.  It does not scrub caller backing arrays, which become reusable. */
void SG_RuneArtifactLoaderReset(sg_rune_artifact_loader_t *loader);

/* Decode and validate encoded into backing, require the complete expected map
 * and physics identity, then publish as the final operation.  backing must be
 * disjoint from encoded, loader, workspace, and the currently published bank.
 * The wire codec also requires all destination/workspace ranges to be pairwise
 * disjoint.  On every failure loader and its current published bank are
 * unchanged; backing and workspace are scratch and may be partially changed.
 * A successful reload therefore uses an inactive second bank.  This function
 * grants no runtime authority to RL_BUTTON_DOOR or any other action. */
sg_rune_codec_diagnostic_t SG_RuneArtifactLoaderLoad(
	sg_rune_artifact_loader_t *loader,
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_codec_identity_t *expected_identity,
	const sg_rune_artifact_backing_t *backing,
	sg_rune_codec_workspace_t *workspace);

int SG_RuneArtifactLoaderIsPublished(const sg_rune_artifact_loader_t *loader);
const sg_rune_codec_header_t *SG_RuneArtifactLoaderHeader(
	const sg_rune_artifact_loader_t *loader);
const sg_rune_codec_seed_t *SG_RuneArtifactLoaderSeedAt(
	const sg_rune_artifact_loader_t *loader, uint32_t index);
const sg_rune_codec_link_t *SG_RuneArtifactLoaderLinkAt(
	const sg_rune_artifact_loader_t *loader, uint32_t index);
const sg_rune_codec_activation_node_t *SG_RuneArtifactLoaderNodeAt(
	const sg_rune_artifact_loader_t *loader, uint32_t index);
const sg_rune_codec_activation_edge_t *SG_RuneArtifactLoaderEdgeAt(
	const sg_rune_artifact_loader_t *loader, uint32_t index);
const sg_rune_codec_activation_plan_t *SG_RuneArtifactLoaderPlanAt(
	const sg_rune_artifact_loader_t *loader, uint32_t index);

/* Nodes are authenticated in strictly ascending key order, so key lookup is
 * bounded binary search.  PlanForLink validates both the link and plan index. */
const sg_rune_codec_activation_node_t *SG_RuneArtifactLoaderNodeByKey(
	const sg_rune_artifact_loader_t *loader, uint32_t key);
const sg_rune_codec_activation_plan_t *SG_RuneArtifactLoaderPlanForLink(
	const sg_rune_artifact_loader_t *loader, uint32_t link_index);

/* Return only a canonical string start whose terminating NUL remains within
 * the authenticated pool.  Offset zero names the canonical empty string. */
const unsigned char *SG_RuneArtifactLoaderStringAt(
	const sg_rune_artifact_loader_t *loader, uint32_t offset);

#endif /* SG_RUNE_ARTIFACT_LOADER_H */
