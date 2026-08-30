#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_configuration_audit.h"
#include "../slipgate/sg_configuration_semantics.h"

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static sg_rune_model_identity_t Identity(const sg_bsp_world_t *world)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	memcpy(&identity.bsp_content_id, world->content_identity.bytes,
		sizeof(identity.bsp_content_id));
	identity.physics_abi_id = UINT64_C(0x434f4e4649474253);
	identity.source_set_identity = UINT64_C(0x434f4e4649475345);
	Set3(identity.standing_hull.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(identity.standing_hull.maxs.value, 16.0f, 16.0f, 32.0f);
	Set3(identity.crouching_hull.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(identity.crouching_hull.maxs.value, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = 100.0f;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = 100U;
	identity.physics.substep_ms = 10U;
	return identity;
}

int main(int argc, char **argv)
{
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t bsp_error;
	sg_rune_model_identity_t identity;
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t host_error;
	sg_configuration_space_t *configuration = NULL;
	sg_configuration_error_t configuration_error;
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_semantics_t *semantics = NULL;
	sg_configuration_semantics_limits_t limits;
	sg_configuration_semantics_error_t semantics_error;
	int result = 1;

	if (argc != 2)
	{
		fprintf(stderr, "usage: %s MAP.bsp\n", argv[0]);
		return 2;
	}
	if (!SG_BspWorldLoadFile(argv[1], &world, &bsp_error))
	{
		fprintf(stderr, "BSP load failed: %s lump=%u record=%u\n",
			SG_BspWorldErrorString(bsp_error.code), (unsigned)bsp_error.lump,
			bsp_error.record);
		goto done;
	}
	identity = Identity(world);
	if (!SG_HostCollisionInit(&authority, world, &identity, &host_error))
	{
		fprintf(stderr, "host init failed: %s\n",
			SG_HostCollisionErrorString(host_error));
		goto done;
	}
	if (!SG_ConfigurationBuild(&authority, NULL, &configuration,
			&configuration_error))
	{
		fprintf(stderr, "configuration build failed: %s source=%u\n",
			SG_ConfigurationErrorString(configuration_error.code),
			configuration_error.source_index);
		goto done;
	}
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	if (!SG_ConfigurationSemanticsBuild(&authority, configuration, &limits,
			&semantics, &semantics_error))
	{
		fprintf(stderr, "configuration semantics failed: %s source=%u\n",
			SG_ConfigurationSemanticsErrorString(semantics_error.code),
			semantics_error.source_index);
		goto done;
	}
	if (!SG_ConfigurationAudit(&authority, configuration,
			&configuration_audit))
	{
		fprintf(stderr, "configuration audit failed: %s record=%u\n",
			SG_ConfigurationAuditCodeString(configuration_audit.code),
			configuration_audit.record);
		goto done;
	}
	fprintf(stdout, "configuration semantics passed cells=%u faces=%u "
		"vertices=%u regions=%u\n", configuration->cell_count,
		configuration->face_count, configuration->vertex_count,
		semantics->region_count);
	result = 0;

done:
	SG_ConfigurationSemanticsDestroy(semantics);
	SG_ConfigurationDestroy(configuration);
	SG_BspWorldDestroy(world);
	return result;
}
