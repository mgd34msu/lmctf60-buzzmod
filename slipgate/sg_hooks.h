/*
 * sg_hooks.h -- the host boundary: every service SLIPGATE takes from the
 * game it lives in, gathered into one table the host fills at load.
 *
 * The bot code's only legitimate reach into its host is through this
 * table. Today the host is LMCTF and SG_HooksInit() fills the table
 * straight from the engine import block plus the handful of game
 * services the pipeline uses; a future host fills the same table from
 * whatever it has, and the bot code neither knows nor cares. Modules
 * migrate onto the table call by call -- a gi. reference in slipgate/
 * is coupling this header exists to retire.
 *
 * Include after g_local.h (q_shared.h has no include guard; the chain
 * convention is documented in ui_boards.h).
 */
#ifndef SG_HOOKS_H
#define SG_HOOKS_H

typedef struct sg_host_s
{
	/* diagnostics: the sanctioned print channels */
	void		(*dprint)(const char *fmt, ...);
	void		(*cprint)(edict_t *ent, int level, const char *fmt, ...);
	void		(*bprint)(int level, const char *fmt, ...);

	/* the physics oracle */
	trace_t		(*trace)(const vec3_t start, const vec3_t mins,
		                 const vec3_t maxs, const vec3_t end,
		                 edict_t *passent, int contentmask);
	int			(*pointcontents)(const vec3_t point);
	qboolean	(*in_pvs)(const vec3_t p1, const vec3_t p2);
	qboolean	(*in_phs)(const vec3_t p1, const vec3_t p2);
	void		(*pmove)(pmove_t *pmove);

	/* level-lifetime memory */
	void		*(*level_alloc)(int size);
	void		(*level_free)(void *block);

	/* configuration and the console line being executed */
	cvar_t		*(*cvar)(const char *name, const char *value, int flags);
	char		*(*argv)(int n);

	/* the world's voice */
	void		(*sound)(edict_t *ent, int channel, int soundindex,
		                 float volume, float attenuation, float timeofs);
	void		(*positioned_sound)(const vec3_t origin, edict_t *ent,
		                            int channel, int soundindex,
		                            float volume, float attenuation,
		                            float timeofs);
	int			(*soundindex)(const char *name);

	/* game-lifetime memory (rune generation) */
	void		*(*game_alloc)(int size);

	/* entity presentation */
	void		(*linkentity)(edict_t *ent);
	void		(*setmodel)(edict_t *ent, const char *name);
	void		(*centerprint)(edict_t *ent, const char *fmt, ...);

	/* the rest of the console line */
	int			(*argc)(void);
	char		*(*args)(void);

	/* the outbound message channel */
	void		(*write_char)(int c);
	void		(*write_byte)(int c);
	void		(*write_short)(int c);
	void		(*write_long)(int c);
	void		(*write_float)(float f);
	void		(*write_string)(const char *s);
	void		(*write_position)(const vec3_t pos);
	void		(*write_dir)(const vec3_t dir);
	void		(*write_angle)(float f);
	void		(*unicast)(edict_t *ent, qboolean reliable);
	void		(*multicast)(const vec3_t origin, multicast_t to);
} sg_host_t;

extern sg_host_t sg_host;

/* fills the table from the LMCTF host; idempotent, call before any use */
void SG_HooksInit(void);

#endif /* SG_HOOKS_H */
