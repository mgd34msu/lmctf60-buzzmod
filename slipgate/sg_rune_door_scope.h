/* sg_rune_door_scope.h -- checked temporary door-solid transaction. */
#ifndef SG_RUNE_DOOR_SCOPE_H
#define SG_RUNE_DOOR_SCOPE_H

#include <stddef.h>

typedef enum sg_rune_door_scope_status_e
{
	SG_RUNE_DOOR_SCOPE_OK = 0,
	SG_RUNE_DOOR_SCOPE_INVALID_ARGUMENT,
	SG_RUNE_DOOR_SCOPE_BUSY,
	SG_RUNE_DOOR_SCOPE_CAPACITY,
	SG_RUNE_DOOR_SCOPE_PREFLIGHT_FAILED,
	SG_RUNE_DOOR_SCOPE_OPEN_FAILED,
	SG_RUNE_DOOR_SCOPE_RESTORE_FAILED
} sg_rune_door_scope_status_t;

typedef struct sg_rune_door_scope_target_s
{
	void *entity;
	int key;
} sg_rune_door_scope_target_t;

typedef struct sg_rune_door_scope_ops_s
{
	void *(*allocate)(void *context, size_t size);
	void (*deallocate)(void *context, void *block);
	int (*identity_matches)(void *context, void *entity, int key);
	int (*get_solid)(void *context, void *entity);
	int (*get_linkcount)(void *context, void *entity);
	void (*set_solid)(void *context, void *entity, int solid);
	void (*set_linkcount)(void *context, void *entity, int linkcount);
	void (*link_entity)(void *context, void *entity);
} sg_rune_door_scope_ops_t;

typedef struct sg_rune_door_scope_entry_s
{
	void *entity;
	int key;
	int solid;
	int linkcount;
	int changed;
} sg_rune_door_scope_entry_t;

typedef struct sg_rune_door_scope_s
{
	sg_rune_door_scope_entry_t *entries;
	size_t count;
	int active;
} sg_rune_door_scope_t;

void SG_RuneDoorScopeInit(sg_rune_door_scope_t *scope);
int SG_RuneDoorScopeActive(const sg_rune_door_scope_t *scope);
sg_rune_door_scope_status_t SG_RuneDoorScopeOpen(
	sg_rune_door_scope_t *scope,
	const sg_rune_door_scope_target_t *targets, size_t target_count,
	int open_solid, const sg_rune_door_scope_ops_t *ops, void *context);
sg_rune_door_scope_status_t SG_RuneDoorScopeRestore(
	sg_rune_door_scope_t *scope,
	const sg_rune_door_scope_ops_t *ops, void *context);
const char *SG_RuneDoorScopeStatusName(sg_rune_door_scope_status_t status);

#endif /* SG_RUNE_DOOR_SCOPE_H */
