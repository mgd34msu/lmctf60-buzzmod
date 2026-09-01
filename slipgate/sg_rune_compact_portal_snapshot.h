/* Frame-scoped live authority for compact portal BLOCKS roots. */
#ifndef SG_RUNE_COMPACT_PORTAL_SNAPSHOT_H
#define SG_RUNE_COMPACT_PORTAL_SNAPSHOT_H

#include <stdint.h>

#include "sg_rune_compact_field_service.h"

typedef struct sg_rune_compact_portal_snapshot_s
	sg_rune_compact_portal_snapshot_t;

/* Both views are borrowed from the snapshot owner and remain valid until its
 * next publish or destroy.  Mechanism phases are authority-domain; portal
 * roots retain static mechanism references.  They are intended only for one
 * synchronous strategy resolution in frame_sequence. */
typedef struct sg_rune_compact_portal_snapshot_frame_s
{
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms;
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots;
} sg_rune_compact_portal_snapshot_frame_t;

/* The service contributes the field-owned portal-major root layout.  The
 * effective semantics pointer is an already-audited authority from the
 * production source owner; NULL is a valid closed state and publishes every
 * root as UNKNOWN. */
int SG_RuneCompactPortalSnapshotCreate(const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_service_t *service,
	const sg_bsp_entity_semantics_t *accepted_effective_semantics,
	sg_rune_compact_portal_snapshot_t **snapshot_out);

/* Revokes or replaces the accepted effective semantics.  NULL deliberately
 * retains a complete snapshot shape while publishing UNKNOWN root states. */
int SG_RuneCompactPortalSnapshotBindEffective(
	sg_rune_compact_portal_snapshot_t *snapshot,
	const sg_bsp_entity_semantics_t *accepted_effective_semantics);

/* Builds exactly one complete live frame.  The catalog is queried through
 * semantic source provenance for each mechanism; no canonical ordinal is an
 * edict index. */
int SG_RuneCompactPortalSnapshotPublish(
	sg_rune_compact_portal_snapshot_t *snapshot, uint64_t frame_sequence,
	sg_rune_compact_portal_snapshot_frame_t *frame_out);

void SG_RuneCompactPortalSnapshotDestroy(
	sg_rune_compact_portal_snapshot_t *snapshot);

#endif /* SG_RUNE_COMPACT_PORTAL_SNAPSHOT_H */
