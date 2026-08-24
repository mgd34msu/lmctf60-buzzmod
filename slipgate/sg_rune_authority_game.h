/* Generator authority capture and pre-install transaction revalidation. */
#ifndef SG_RUNE_AUTHORITY_GAME_H
#define SG_RUNE_AUTHORITY_GAME_H

#include "sg_rune.h"

typedef enum sg_rune_authority_recheck_failure_e
{
	SG_RUNE_AUTHORITY_RECHECK_NONE = 0,
	SG_RUNE_AUTHORITY_RECHECK_IDENTITY,
	SG_RUNE_AUTHORITY_RECHECK_PROOF_LAW,
	SG_RUNE_AUTHORITY_RECHECK_LEARNING_SOURCE
} sg_rune_authority_recheck_failure_t;

typedef struct sg_rune_authority_recheck_s
{
	const char *mapname;
	const sg_rune_authority_t *captured;
	sg_rune_authority_recheck_failure_t failure;
	sg_identity_status_t identity_status;
	const char *source_path;
	const rune_t *source_rune;
	rune_artifact_t source_artifact;
	char source_sha256[65];
} sg_rune_authority_recheck_t;

int SG_RuneAuthorityGameRevalidate(void *context);

#endif /* SG_RUNE_AUTHORITY_GAME_H */
