#ifndef SG_RUNE_V2_CONTENT_IDENTITY_H
#define SG_RUNE_V2_CONTENT_IDENTITY_H

#include <stddef.h>

#include "sg_rune_v2_wire.h"

int SG_RuneV2ContentIdentitySHA256(const unsigned char *bytes, size_t size,
	sg_rune_v2_content_id_t *identity_out);

#endif
