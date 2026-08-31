#ifndef SG_RUNE_SOURCE_AUTHORITY_OWNER_H
#define SG_RUNE_SOURCE_AUTHORITY_OWNER_H

#include "sg_rune_source_authority.h"

void SG_RuneSourceAuthorityReset(void);
sg_rune_source_status_t SG_RuneSourceAuthorityBegin(
	const char *mapname, const char *selected_entity_text);
sg_rune_source_status_t SG_RuneSourceAuthorityRecord(
	uint32_t source_ordinal, int32_t effective_spawnflags);
sg_rune_source_status_t SG_RuneSourceAuthorityPublish(const char *mapname);

#endif
