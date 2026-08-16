/* sg_identity.c -- level identity captured once and published only on success. */
#include "g_local.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_identity.h"

#include <limits.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "level identity requires 8-bit bytes");
_Static_assert(MAX_QPATH == SG_LEVEL_IDENTITY_MAPNAME_BYTES,
	"level identity map-name width must match MAX_QPATH");
_Static_assert(SG_HOST_PHYSICS_EPOCH != 0,
	"the supported host-physics epoch must be nonzero");

typedef enum sg_identity_phase_e
{
	SG_IDENTITY_PHASE_EMPTY = 0,
	SG_IDENTITY_PHASE_FAILED,
	SG_IDENTITY_PHASE_HOST,
	SG_IDENTITY_PHASE_ENTITIES,
	SG_IDENTITY_PHASE_COMMITTED
} sg_identity_phase_t;

typedef struct sg_identity_state_s
{
	sg_level_identity_t identity;
	sg_identity_status_t status;
	sg_identity_phase_t phase;
} sg_identity_state_t;

static sg_identity_state_t sg_identity_state = {
	.status = SG_IDENTITY_UNAVAILABLE,
	.phase = SG_IDENTITY_PHASE_EMPTY
};

static int Identity_MapInitial(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
	       (c >= '0' && c <= '9') || c == '_';
}

static int Identity_MapTail(unsigned char c)
{
	return Identity_MapInitial(c) || c == '-';
}

static int Identity_MapName(const char *mapname, size_t *length)
{
	size_t i;

	if (!mapname || !Identity_MapInitial((unsigned char)mapname[0]))
		return 0;
	for (i = 1; i < SG_LEVEL_IDENTITY_MAPNAME_BYTES; i++)
	{
		if (mapname[i] == '\0')
		{
			if (length)
				*length = i;
			return 1;
		}
		if (!Identity_MapTail((unsigned char)mapname[i]))
			return 0;
	}
	return 0;
}

static sg_identity_status_t Identity_Fail(sg_identity_status_t status)
{
	memset(&sg_identity_state.identity, 0, sizeof(sg_identity_state.identity));
	sg_identity_state.status = status;
	sg_identity_state.phase = SG_IDENTITY_PHASE_FAILED;
	return status;
}

typedef enum identity_cvar_result_e
{
	IDENTITY_CVAR_OK = 0,
	IDENTITY_CVAR_MISSING,
	IDENTITY_CVAR_UNPROTECTED,
	IDENTITY_CVAR_NONCANONICAL
} identity_cvar_result_t;

static identity_cvar_result_t Identity_ParseCvar(const cvar_t *var,
	uint32_t *out)
{
	const char *s;
	uint32_t value = 0;
	size_t length = 0;

	if (!var || !var->string)
		return IDENTITY_CVAR_MISSING;
	if (!(var->flags & CVAR_NOSET))
		return IDENTITY_CVAR_UNPROTECTED;
	s = var->string;
	while (length <= 10 && s[length] != '\0')
		length++;
	if (length == 0 || length > 10 || (length > 1 && s[0] == '0'))
		return IDENTITY_CVAR_NONCANONICAL;
	for (size_t i = 0; i < length; i++)
	{
		uint32_t digit;

		if (s[i] < '0' || s[i] > '9')
			return IDENTITY_CVAR_NONCANONICAL;
		digit = (uint32_t)(s[i] - '0');
		if (value > (UINT32_MAX - digit) / UINT32_C(10))
			return IDENTITY_CVAR_NONCANONICAL;
		value = value * UINT32_C(10) + digit;
	}
	*out = value;
	return IDENTITY_CVAR_OK;
}

void SG_LevelIdentityReset(void)
{
	memset(&sg_identity_state, 0, sizeof(sg_identity_state));
	sg_identity_state.status = SG_IDENTITY_UNAVAILABLE;
	sg_identity_state.phase = SG_IDENTITY_PHASE_EMPTY;
}

sg_identity_status_t SG_LevelIdentityBegin(const char *mapname)
{
	cvar_t *mapchecksum;
	cvar_t *physics_id;
	identity_cvar_result_t result;
	size_t map_length;

	/* A transition attempt can never leave the previous map consumable, even if
	 * a caller violates the documented outer Reset/Begin sequence. */
	SG_LevelIdentityReset();
	if (!Identity_MapName(mapname, &map_length))
		return Identity_Fail(SG_IDENTITY_INVALID_MAPNAME);
	if (!sg_host.cvar)
		return Identity_Fail(SG_IDENTITY_HOST_CVAR_UNAVAILABLE);

	/* Flags must remain zero.  Cvar_Get ORs requested flags into an existing
	 * object, so requesting CVAR_NOSET here would bless an operator spoof. */
	mapchecksum = sg_host.cvar("sv_rune_mapchecksum", "", 0);
	physics_id = sg_host.cvar("sv_rune_physics_id", "", 0);

	result = Identity_ParseCvar(mapchecksum,
		&sg_identity_state.identity.bsp_checksum);
	if (result == IDENTITY_CVAR_MISSING)
		return Identity_Fail(SG_IDENTITY_MAPCHECKSUM_MISSING);
	if (result == IDENTITY_CVAR_UNPROTECTED)
		return Identity_Fail(SG_IDENTITY_MAPCHECKSUM_UNPROTECTED);
	if (result != IDENTITY_CVAR_OK)
		return Identity_Fail(SG_IDENTITY_MAPCHECKSUM_NONCANONICAL);

	result = Identity_ParseCvar(physics_id,
		&sg_identity_state.identity.host_physics_id);
	if (result == IDENTITY_CVAR_MISSING)
		return Identity_Fail(SG_IDENTITY_PHYSICS_ID_MISSING);
	if (result == IDENTITY_CVAR_UNPROTECTED)
		return Identity_Fail(SG_IDENTITY_PHYSICS_ID_UNPROTECTED);
	if (result != IDENTITY_CVAR_OK)
		return Identity_Fail(SG_IDENTITY_PHYSICS_ID_NONCANONICAL);
	if (sg_identity_state.identity.host_physics_id != SG_HOST_PHYSICS_EPOCH)
		return Identity_Fail(SG_IDENTITY_PHYSICS_ID_UNSUPPORTED);

	memset(sg_identity_state.identity.mapname, 0,
		sizeof(sg_identity_state.identity.mapname));
	memcpy(sg_identity_state.identity.mapname, mapname, map_length);
	sg_identity_state.status = SG_IDENTITY_NOT_COMMITTED;
	sg_identity_state.phase = SG_IDENTITY_PHASE_HOST;
	return SG_IDENTITY_OK;
}

sg_identity_status_t SG_LevelIdentityCaptureEntities(const char *mapname,
	const char *text)
{
	size_t length;

	if (!Identity_MapName(mapname, NULL))
		return Identity_Fail(SG_IDENTITY_INVALID_MAPNAME);
	if (sg_identity_state.phase == SG_IDENTITY_PHASE_COMMITTED)
	{
		if (strcmp(mapname, sg_identity_state.identity.mapname) != 0)
			return SG_IDENTITY_MAPNAME_MISMATCH;
		return SG_IDENTITY_ALREADY_COMMITTED;
	}
	if (sg_identity_state.phase == SG_IDENTITY_PHASE_FAILED)
		return sg_identity_state.status;
	if (sg_identity_state.phase != SG_IDENTITY_PHASE_HOST)
		return Identity_Fail(SG_IDENTITY_INVALID_TRANSITION);
	if (strcmp(mapname, sg_identity_state.identity.mapname) != 0)
		return Identity_Fail(SG_IDENTITY_MAPNAME_MISMATCH);
	if (!text)
		return Identity_Fail(SG_IDENTITY_ENTITY_TEXT_MISSING);

	for (length = 0; length < SG_LEVEL_ENTITY_TEXT_LIMIT; length++)
		if (text[length] == '\0')
			break;
	if (length == SG_LEVEL_ENTITY_TEXT_LIMIT)
		return Identity_Fail(SG_IDENTITY_ENTITY_TEXT_UNTERMINATED);

	if (!SG_CRC32Buffer(text, length,
	    &sg_identity_state.identity.entity_crc32))
		return Identity_Fail(SG_IDENTITY_CRC_FAILURE);
	sg_identity_state.phase = SG_IDENTITY_PHASE_ENTITIES;
	return SG_IDENTITY_OK;
}

sg_identity_status_t SG_LevelIdentityCommit(const char *mapname)
{
	if (!Identity_MapName(mapname, NULL))
		return Identity_Fail(SG_IDENTITY_INVALID_MAPNAME);
	if (sg_identity_state.phase == SG_IDENTITY_PHASE_COMMITTED)
	{
		if (strcmp(mapname, sg_identity_state.identity.mapname) != 0)
			return SG_IDENTITY_MAPNAME_MISMATCH;
		return SG_IDENTITY_ALREADY_COMMITTED;
	}
	if (sg_identity_state.phase == SG_IDENTITY_PHASE_FAILED)
		return sg_identity_state.status;
	if (sg_identity_state.phase != SG_IDENTITY_PHASE_ENTITIES)
		return Identity_Fail(SG_IDENTITY_NOT_COMMITTED);
	if (strcmp(mapname, sg_identity_state.identity.mapname) != 0)
		return Identity_Fail(SG_IDENTITY_MAPNAME_MISMATCH);

	sg_identity_state.status = SG_IDENTITY_OK;
	sg_identity_state.phase = SG_IDENTITY_PHASE_COMMITTED;
	return SG_IDENTITY_OK;
}

sg_identity_status_t SG_LevelIdentitySnapshot(const char *expected_mapname,
	sg_level_identity_t *out)
{
	if (!out)
		return SG_IDENTITY_INVALID_ARGUMENT;
	memset(out, 0, sizeof(*out));
	if (!Identity_MapName(expected_mapname, NULL))
		return SG_IDENTITY_INVALID_MAPNAME;
	if (sg_identity_state.phase == SG_IDENTITY_PHASE_FAILED)
		return sg_identity_state.status;
	if (sg_identity_state.phase == SG_IDENTITY_PHASE_EMPTY)
		return SG_IDENTITY_UNAVAILABLE;
	if (sg_identity_state.phase != SG_IDENTITY_PHASE_COMMITTED)
		return SG_IDENTITY_NOT_COMMITTED;
	if (strcmp(expected_mapname, sg_identity_state.identity.mapname) != 0)
		return SG_IDENTITY_MAPNAME_MISMATCH;
	*out = sg_identity_state.identity;
	return SG_IDENTITY_OK;
}

sg_identity_status_t SG_LevelIdentityMatch(const char *expected_mapname,
	uint32_t bsp_checksum, uint32_t entity_crc32, uint32_t physics_id)
{
	sg_level_identity_t identity;
	sg_identity_status_t status;

	status = SG_LevelIdentitySnapshot(expected_mapname, &identity);
	if (status != SG_IDENTITY_OK)
		return status;
	if (identity.bsp_checksum != bsp_checksum)
		return SG_IDENTITY_BSP_CHECKSUM_MISMATCH;
	if (identity.entity_crc32 != entity_crc32)
		return SG_IDENTITY_ENTITY_CRC_MISMATCH;
	if (identity.host_physics_id != physics_id)
		return SG_IDENTITY_PHYSICS_ID_MISMATCH;
	return SG_IDENTITY_OK;
}

const char *SG_LevelIdentityReason(sg_identity_status_t status)
{
	static const char *const reasons[SG_IDENTITY_STATUS_COUNT] = {
		"level identity is valid",
		"level identity is unavailable",
		"invalid identity API argument",
		"map name is not canonical artifact syntax",
		"invalid level-identity lifecycle transition",
		"level identity is already committed",
		"level identity was not committed by a successful spawn",
		"host cvar service is unavailable",
		"sv_rune_mapchecksum is missing",
		"sv_rune_mapchecksum lacks CVAR_NOSET",
		"sv_rune_mapchecksum is not canonical uint32",
		"sv_rune_physics_id is missing",
		"sv_rune_physics_id lacks CVAR_NOSET",
		"sv_rune_physics_id is not canonical uint32",
		"sv_rune_physics_id is unsupported by this game DLL",
		"effective entity text is missing",
		"effective entity text is not terminated within the engine limit",
		"level map name does not match",
		"authoritative BSP checksum does not match",
		"effective entity CRC32 does not match",
		"host physics contract ID does not match",
		"effective entity CRC32 could not be computed"
	};

	if (status < SG_IDENTITY_OK || status >= SG_IDENTITY_STATUS_COUNT)
		return "unknown level-identity status";
	return reasons[status];
}
