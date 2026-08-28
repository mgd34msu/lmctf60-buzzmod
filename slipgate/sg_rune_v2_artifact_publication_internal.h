#ifndef SG_RUNE_V2_ARTIFACT_PUBLICATION_INTERNAL_H
#define SG_RUNE_V2_ARTIFACT_PUBLICATION_INTERNAL_H

#include "sg_rune_v2_artifact_publication.h"

#define SG_RUNE_V2_PUBLICATION_MANIFEST_HEADER_BYTES 200U
#define SG_RUNE_V2_PUBLICATION_MANIFEST_ENTRY_BYTES 40U
#define SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES \
	(SG_RUNE_V2_PUBLICATION_MANIFEST_HEADER_BYTES + \
	 SG_RUNE_V2_MAX_SIDECARS * SG_RUNE_V2_PUBLICATION_MANIFEST_ENTRY_BYTES)

int SG_RuneV2PublicationCandidateValid(
	const sg_rune_v2_publication_candidate_t *candidate);
size_t SG_RuneV2PublicationManifestEncode(
	const sg_rune_v2_publication_candidate_t *candidate,
	unsigned char output[SG_RUNE_V2_PUBLICATION_MANIFEST_MAX_BYTES]);
int SG_RuneV2PublicationManifestDecode(const unsigned char *bytes, size_t size,
	sg_rune_v2_active_generation_t *active);

#endif /* SG_RUNE_V2_ARTIFACT_PUBLICATION_INTERNAL_H */
