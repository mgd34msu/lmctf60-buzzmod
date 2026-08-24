/* Generator authority capture and source-locked install boundary. */
#include "../g_local.h"
#include "sg_local.h"
#include "sg_hooks.h"
#include "sg_rune_authority_game.h"
#include "sg_rune_file.h"
#include "sg_rune_proof.h"

#include <math.h>
#include <string.h>

static uint32_t AuthorityFloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static qboolean AuthorityPhysicsCapture(const sg_level_identity_t *level_id,
	rune_identity_t *identity)
{
	cvar_t *airaccelerate;
	float gravity;

	if (!level_id || !identity || !sg_host.cvar)
		return false;
	airaccelerate = sg_host.cvar("sv_airaccelerate", "0", 0);
	gravity = sv_gravity ? sv_gravity->value : 0.0f;
	if (!sv_gravity || !isfinite(gravity) ||
	    gravity < (float)SG_RUNE_PROOF_GRAVITY_MIN ||
	    gravity > (float)SG_RUNE_PROOF_GRAVITY_MAX ||
	    (SG_RUNE_PROOF_GRAVITY_INTEGRAL_REQUIRED &&
	     gravity != (float)(short)gravity) ||
	    !airaccelerate || !isfinite(airaccelerate->value) ||
	    (SG_RUNE_PROOF_AIRACCELERATE_ZERO_REQUIRED &&
	     airaccelerate->value != 0.0f) ||
	    !sv_maxvelocity || !isfinite(sv_maxvelocity->value) ||
	    sv_maxvelocity->value < (float)SG_RUNE_PROOF_MAXVELOCITY_MIN ||
	    (want_funky_gravity && want_funky_gravity->value != 0.0f) ||
	    FRAMETIME != (float)SG_RUNE_PROOF_SERVER_FRAME_MS / 1000.0f ||
	    level_id->host_physics_id != SG_HOST_PHYSICS_EPOCH)
		return false;
	memset(identity, 0, sizeof(*identity));
	memcpy(identity->map_name, level_id->mapname, RUNE_MAP_NAME_BYTES);
	identity->bsp_checksum = level_id->bsp_checksum;
	identity->entity_crc32 = level_id->entity_crc32;
	identity->physics_flags = SG_RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED;
	identity->gravity = gravity;
	identity->airaccelerate = airaccelerate->value;
	identity->maxvelocity = sv_maxvelocity->value;
	identity->pmove_substep_ms = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
	identity->server_frame_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	identity->host_physics_id = level_id->host_physics_id;
	return true;
}

qboolean SG_RuneAuthorityCapture(const char *mapname,
	sg_rune_authority_t *authority)
{
	if (!authority)
		return false;
	memset(authority, 0, sizeof(*authority));
	authority->identity_status = SG_LevelIdentitySnapshot(mapname,
		&authority->level);
	if (authority->identity_status != SG_IDENTITY_OK)
		return false;
	return AuthorityPhysicsCapture(&authority->level, &authority->identity);
}

static qboolean AuthorityIdentityEqual(const rune_identity_t *first,
	const rune_identity_t *second)
{
	return first && second &&
	       first->bsp_checksum == second->bsp_checksum &&
	       first->entity_crc32 == second->entity_crc32 &&
	       first->physics_flags == second->physics_flags &&
	       AuthorityFloatBits(first->gravity) ==
	           AuthorityFloatBits(second->gravity) &&
	       AuthorityFloatBits(first->airaccelerate) ==
	           AuthorityFloatBits(second->airaccelerate) &&
	       AuthorityFloatBits(first->maxvelocity) ==
	           AuthorityFloatBits(second->maxvelocity) &&
	       first->pmove_substep_ms == second->pmove_substep_ms &&
	       first->server_frame_ms == second->server_frame_ms &&
	       first->host_physics_id == second->host_physics_id &&
	       memcmp(first->map_name, second->map_name,
	           RUNE_MAP_NAME_BYTES) == 0;
}

qboolean SG_RuneAuthorityMatchesArtifact(
	const sg_rune_authority_t *authority, const rune_artifact_t *artifact)
{
	return authority && authority->identity_status == SG_IDENTITY_OK &&
	       artifact && artifact->magic == RUNE_ARTIFACT_MAGIC &&
	       SG_RuneRouteContractValid(artifact->route_contract) &&
	       artifact->action_contract_crc32 == SG_RUNE_ACTION_CONTRACT_CRC32 &&
	       artifact->mechanism_contract_crc32 ==
	           SG_RUNE_MECHANISM_CONTRACT_CRC32 &&
	       AuthorityIdentityEqual(&authority->identity, &artifact->identity);
}

qboolean SG_RunePhysicsCompatible(const rune_t *rune)
{
	sg_rune_authority_t current;

	if (!SG_RunePublishedShapeValid(rune) ||
	    !SG_RuneAuthorityCapture(rune->artifact.identity.map_name, &current))
		return false;
	return SG_RuneAuthorityMatchesArtifact(&current, &rune->artifact);
}

static qboolean AuthorityLevelEqual(const sg_level_identity_t *first,
	const sg_level_identity_t *second)
{
	return first && second &&
	       first->bsp_checksum == second->bsp_checksum &&
	       first->entity_crc32 == second->entity_crc32 &&
	       first->host_physics_id == second->host_physics_id &&
	       memcmp(first->mapname, second->mapname,
	           SG_LEVEL_IDENTITY_MAPNAME_BYTES) == 0;
}

static qboolean AuthorityProofLawEqual(const rune_identity_t *first,
	const rune_identity_t *second)
{
	return first && second &&
	       first->physics_flags == second->physics_flags &&
	       AuthorityFloatBits(first->gravity) ==
	           AuthorityFloatBits(second->gravity) &&
	       AuthorityFloatBits(first->airaccelerate) ==
	           AuthorityFloatBits(second->airaccelerate) &&
	       AuthorityFloatBits(first->maxvelocity) ==
	           AuthorityFloatBits(second->maxvelocity) &&
	       first->pmove_substep_ms == second->pmove_substep_ms &&
	       first->server_frame_ms == second->server_frame_ms;
}

int SG_RuneAuthorityGameRevalidate(void *context)
{
	sg_rune_authority_recheck_t *recheck = context;
	sg_rune_authority_t current;
	rune_artifact_t disk_source;
	const rune_t *loaded_source;

	if (!recheck || !recheck->captured)
		return 0;
	recheck->failure = SG_RUNE_AUTHORITY_RECHECK_NONE;
	if (!SG_RuneProofScopeActive() ||
	    (float)SG_RuneProofGravity() != recheck->captured->identity.gravity)
	{
		recheck->failure = SG_RUNE_AUTHORITY_RECHECK_PROOF_LAW;
		return 0;
	}
	if (!SG_RuneAuthorityCapture(recheck->mapname, &current))
	{
		recheck->identity_status = current.identity_status;
		recheck->failure = current.identity_status == SG_IDENTITY_OK
			? SG_RUNE_AUTHORITY_RECHECK_PROOF_LAW
			: SG_RUNE_AUTHORITY_RECHECK_IDENTITY;
		return 0;
	}
	if (!AuthorityLevelEqual(&recheck->captured->level, &current.level))
	{
		recheck->failure = SG_RUNE_AUTHORITY_RECHECK_IDENTITY;
		recheck->identity_status = SG_IDENTITY_UNAVAILABLE;
		return 0;
	}
	if (!AuthorityProofLawEqual(&recheck->captured->identity,
	        &current.identity))
	{
		recheck->failure = SG_RUNE_AUTHORITY_RECHECK_PROOF_LAW;
		return 0;
	}
	if (!recheck->source_path)
		return 1;
	loaded_source = recheck->source_rune;
	if (!loaded_source ||
	    !SG_RuneArtifactsEqual(&loaded_source->artifact,
	        &recheck->source_artifact) ||
	    strcmp(loaded_source->encoded_sha256, recheck->source_sha256) != 0 ||
	    SG_RuneFileInspectExact(recheck->source_path,
	        &recheck->source_artifact.identity, recheck->source_sha256,
	        &disk_source, NULL) != SG_RUNE_FILE_INSPECT_MATCH ||
	    !SG_RuneArtifactsEqual(&disk_source, &recheck->source_artifact))
	{
		recheck->failure = SG_RUNE_AUTHORITY_RECHECK_LEARNING_SOURCE;
		return 0;
	}
	return 1;
}
