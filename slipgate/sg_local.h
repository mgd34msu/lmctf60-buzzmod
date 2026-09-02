/* sg_local.h -- what the engine-side game files and the bot units share:
 * roles, the roster, the frame, and level setup. */
#pragma once

#include "sg_client_ownership.h"

typedef enum
{
	SG_ROLE_ATTACK = 0, SG_ROLE_DEFEND, SG_ROLE_CARRY,
	SG_ROLE_RECOVER, SG_ROLE_ESCORT,
	SG_ROLES
} sg_role_t;

/* strafe-jump landing adapter (sg_human_speed.c) */
void	SG_HumanSpeedClientThinkBegin(edict_t *ent);
void	SG_HumanSpeedPmoveBegin(edict_t *ent, pmove_state_t *pmove,
	unsigned command_msec);
void	SG_HumanSpeedPmoveEnd(edict_t *ent, const pmove_state_t *pmove,
	unsigned command_msec);

/* roster (sg_bot_roster.c) */
qboolean	SG_InternalClientConnect(edict_t *ent);
qboolean	SG_RetireBotForClient(edict_t *ent);
void		SG_DisownBot(edict_t *ent);
qboolean	SG_AddBot(void);
qboolean	SG_AddBotTeam(int teamnum);
int			SG_RemoveBots(void);
void		SG_ListBots(void);
qboolean	SG_RemoveBotNamed(const char *who);
qboolean	SG_KickWorst(void);
void		SG_RosterStorageReset(void);
void		Botfill_Reset(void);
void		Botfill_Frame(void);

/* the frame and the level (sg_bot_frame.c) */
void		SG_RunFrame(void);
qboolean	SG_LevelSetup(void);
void		SG_LevelSetupAfterRuneWrite(void);
void		SG_LevelChange(void);
void		SG_RuneLevelStorageWillFree(void);
