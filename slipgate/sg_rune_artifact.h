/* Era-4 RUNE artifact.
 *
 * One file per map: an identity that ties it to the exact BSP bytes and the
 * host's movement laws, the law values the profiles were derived under, and
 * fixed-layout record arrays laid out in sections.  The decoded artifact
 * borrows every array from the image, so loading is one read and one
 * validation pass with no per-record allocation.  Little-endian hosts only;
 * the record sizes are checked against the image on load. */
#ifndef SG_RUNE_ARTIFACT_H
#define SG_RUNE_ARTIFACT_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_analytic.h"
#include "sg_rune_cx.h"
#include "sg_rune_law.h"
#include "sg_rune_fire.h"
#include "sg_rune_mechanisms.h"
#include "sg_rune_movement.h"

#define SG_RUNE_ARTIFACT_VERSION UINT32_C(7)
#define SG_RUNE_ARTIFACT_SCHEMA_ID UINT64_C(0x52554E4534000002)

typedef struct sg_rune_identity_s
{
	uint32_t bsp_crc32;       /* of the whole map file */
	uint32_t entity_crc32;    /* of its entity text */
	uint64_t bsp_bytes;
	uint32_t law_crc32;       /* of the law the profiles were derived under */
	uint32_t reserved;
	uint64_t schema_id;
} sg_rune_identity_t;

/* The law lives in sg_rune_law.h; the artifact carries one. */

typedef enum sg_rune_section_kind_e
{
	SG_RUNE_SECTION_CELLS = 0,
	SG_RUNE_SECTION_FACETS,
	SG_RUNE_SECTION_INCIDENCES,
	SG_RUNE_SECTION_CELL_INCIDENCES,
	SG_RUNE_SECTION_VERTICES,
	SG_RUNE_SECTION_PORTALS,
	SG_RUNE_SECTION_SURFACES,
	SG_RUNE_SECTION_SURFACE_VERTICES,
	SG_RUNE_SECTION_MOVE_CAPABILITIES,
	SG_RUNE_SECTION_MOVE_PROFILES,
	SG_RUNE_SECTION_FN_FUNCTIONS,
	SG_RUNE_SECTION_FN_TERMS,
	SG_RUNE_SECTION_FN_CLAUSES,
	SG_RUNE_SECTION_MECHANISMS,
	SG_RUNE_SECTION_MECHANISM_CELLS,
	SG_RUNE_SECTION_FIRE_CELLS,
	SG_RUNE_SECTION_FIRES,
	SG_RUNE_SECTION_COUNT
} sg_rune_section_kind_t;

typedef struct sg_rune_artifact_s
{
	sg_rune_identity_t identity;
	sg_rune_law_t law;
	sg_rune_cx_view_t cx;
	sg_rune_move_table_t movement;
	sg_rune_mech_table_t mechanisms;
	sg_rune_fire_table_t fires;        /* per cell: what can be hit from it */
	const unsigned char *image;
	size_t image_size;
	unsigned char *owned;      /* the image when this artifact loaded it */
} sg_rune_artifact_t;

typedef enum sg_rune_artifact_status_e
{
	SG_RUNE_ARTIFACT_OK = 0,
	SG_RUNE_ARTIFACT_INVALID_ARGUMENT,
	SG_RUNE_ARTIFACT_OUT_OF_MEMORY,
	SG_RUNE_ARTIFACT_FILE_ERROR,      /* os_error carries errno */
	SG_RUNE_ARTIFACT_BAD_MAGIC,
	SG_RUNE_ARTIFACT_BAD_VERSION,
	SG_RUNE_ARTIFACT_TRUNCATED,
	SG_RUNE_ARTIFACT_BAD_CHECKSUM,
	SG_RUNE_ARTIFACT_BAD_SECTION,
	SG_RUNE_ARTIFACT_BAD_RECORDS,     /* a reference or value out of range */
	SG_RUNE_ARTIFACT_IDENTITY_MISMATCH,
	SG_RUNE_ARTIFACT_LAW_MISMATCH,
	SG_RUNE_ARTIFACT_STATUS_COUNT
} sg_rune_artifact_status_t;

/* Lays the source's arrays out as one image (malloc'd; free with free()).
 * The source's image fields are ignored. */
sg_rune_artifact_status_t SG_RuneArtifactEncode(const sg_rune_artifact_t *source,
	unsigned char **image_out, size_t *image_size_out);

/* Decodes and validates; the artifact borrows the image. */
sg_rune_artifact_status_t SG_RuneArtifactDecode(const unsigned char *image,
	size_t image_size, sg_rune_artifact_t *artifact_out,
	sg_rune_fault_t *fault_out);

/* Reads the whole file, then decodes; the artifact owns the image. */
sg_rune_artifact_status_t SG_RuneArtifactLoadFile(const char *path,
	sg_rune_artifact_t *artifact_out, int *os_error_out,
	sg_rune_fault_t *fault_out);

void SG_RuneArtifactRelease(sg_rune_artifact_t *artifact);

/* Writes to a temporary beside the destination, flushes it to disk, and
 * renames over the destination as the single commit point. */
sg_rune_artifact_status_t SG_RuneArtifactWriteFile(const char *path,
	const unsigned char *image, size_t image_size, int *os_error_out);

/* Every complex reference in range, every capability over a real cell,
 * portal, and profile, every profile function of the right output, every
 * analytic value finite. */
int SG_RuneArtifactValid(const sg_rune_artifact_t *artifact,
	sg_rune_fault_t *fault_out);

int SG_RuneIdentityMatches(const sg_rune_identity_t *a,
	const sg_rune_identity_t *b);
int SG_RuneLawMatches(const sg_rune_law_t *a, const sg_rune_law_t *b);

/* <game_directory>/maps/<map>.rune; clears output and fails on truncation. */
int SG_RuneArtifactPath(char *output, size_t output_size,
	const char *game_directory, const char *map_name);

const char *SG_RuneArtifactStatusString(sg_rune_artifact_status_t status);

#endif /* SG_RUNE_ARTIFACT_H */
