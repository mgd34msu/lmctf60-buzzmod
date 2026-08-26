#include "g_local.h"

int sg_rune_test_declared_door_members_result = -1;

int SG_DeclaredDoorMembers(edict_t *trigger, edict_t **members, int capacity)
{
	(void)trigger;
	(void)members;
	(void)capacity;
	return sg_rune_test_declared_door_members_result;
}
