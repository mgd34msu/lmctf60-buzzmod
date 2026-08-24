/* sg_timed_vault_game_runtime.h -- live authenticated timed-vault bridge. */
#ifndef SG_TIMED_VAULT_GAME_RUNTIME_H
#define SG_TIMED_VAULT_GAME_RUNTIME_H

struct edict_s;
struct usercmd_s;

typedef enum sg_timed_vault_runtime_target_result_e
{
	SG_TIMED_VAULT_RUNTIME_NOT_OWNED = 0,
	SG_TIMED_VAULT_RUNTIME_ALLOW_STOCK,
	SG_TIMED_VAULT_RUNTIME_HANDLED
} sg_timed_vault_runtime_target_result_t;

sg_timed_vault_runtime_target_result_t SG_TimedVaultRuntimeHandleTargets(
	struct edict_s *source, struct edict_s *activator);
void SG_TimedVaultRuntimeTagDelayedTarget(struct edict_s *source,
	struct edict_s *activator, struct edict_s *delayed);
int SG_TimedVaultRuntimeDelayedUseDurable(const struct edict_s *delayed);
int SG_TimedVaultRuntimeDelayedUseDeferred(const struct edict_s *delayed);
void SG_TimedVaultRuntimeRetireActivator(struct edict_s *delayed,
	struct edict_s *activator);
void SG_TimedVaultRuntimeEntityFreed(struct edict_s *entity);
int SG_TimedVaultRuntimeApplyCommand(struct edict_s *activator,
	struct usercmd_s *command);
int SG_TimedVaultRuntimeCommandFor(const struct edict_s *activator);

#endif /* SG_TIMED_VAULT_GAME_RUNTIME_H */
