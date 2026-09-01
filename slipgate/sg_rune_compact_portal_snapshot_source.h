/* Production acquisition of an audited effective-entity semantic source. */
#ifndef SG_RUNE_COMPACT_PORTAL_SNAPSHOT_SOURCE_H
#define SG_RUNE_COMPACT_PORTAL_SNAPSHOT_SOURCE_H

#include "sg_rune_compact_model.h"

typedef struct sg_rune_compact_portal_snapshot_source_s
	sg_rune_compact_portal_snapshot_source_t;

/* A failed acquisition is intentionally non-fatal to compact production: the
 * caller can still publish a complete UNKNOWN portal snapshot. */
int SG_RuneCompactPortalSnapshotSourcePrepare(
	const sg_rune_compact_model_t *model,
	sg_rune_compact_portal_snapshot_source_t **source_out);

/* Currentness covers both the effective entity-source publication and its
 * host-law epoch.  A level/generation/topology drift revokes this source. */
int SG_RuneCompactPortalSnapshotSourceCurrent(
	const sg_rune_compact_portal_snapshot_source_t *source,
	const sg_rune_compact_model_t *model);

const sg_bsp_entity_semantics_t *
SG_RuneCompactPortalSnapshotSourceEffectiveSemantics(
	const sg_rune_compact_portal_snapshot_source_t *source);

void SG_RuneCompactPortalSnapshotSourceDestroy(
	sg_rune_compact_portal_snapshot_source_t *source);

#endif /* SG_RUNE_COMPACT_PORTAL_SNAPSHOT_SOURCE_H */
