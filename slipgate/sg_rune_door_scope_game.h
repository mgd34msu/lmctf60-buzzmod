/* Game-world adapter for transactional generator door scopes. */
#ifndef SG_RUNE_DOOR_SCOPE_GAME_H
#define SG_RUNE_DOOR_SCOPE_GAME_H

#include "sg_rune_door_scope.h"

sg_rune_door_scope_status_t SG_RuneDoorScopeGameOpen(
	sg_rune_door_scope_t *scope);
sg_rune_door_scope_status_t SG_RuneDoorScopeGameRestore(
	sg_rune_door_scope_t *scope);

#endif /* SG_RUNE_DOOR_SCOPE_GAME_H */
