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
	/* Make complete diagnostic records visible through the host's logging
	 * route before returning.  This is a visibility boundary, not storage
	 * durability. */
	void		(*flush)(void);
	void		(*cprint)(edict_t *ent, int level, const char *fmt, ...);
	void		(*bprint)(int level, const char *fmt, ...);

	/* the physics oracle */
	trace_t		(*trace)(const vec3_t start, const vec3_t mins,
		                 const vec3_t maxs, const vec3_t end,
		                 edict_t *passent, int contentmask);
	int			(*pointcontents)(const vec3_t point);
	int			(*box_edicts)(const vec3_t mins, const vec3_t maxs,
			                 edict_t **list, int maxcount, int areatype);
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
	void		(*game_free)(void *block);

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

/*
 * Keep validation and host-boundary tests tied to the same inventory.  Adding
 * a service requires adding it here; SG_HostInstall() rejects a table with any
 * listed slot missing.
 */
#define SG_HOST_REQUIRED_SERVICES(X) \
	X(dprint) \
	X(flush) \
	X(cprint) \
	X(bprint) \
	X(trace) \
	X(pointcontents) \
	X(box_edicts) \
	X(in_pvs) \
	X(in_phs) \
	X(pmove) \
	X(level_alloc) \
	X(level_free) \
	X(cvar) \
	X(argv) \
	X(sound) \
	X(positioned_sound) \
	X(soundindex) \
	X(game_alloc) \
	X(game_free) \
	X(linkentity) \
	X(setmodel) \
	X(centerprint) \
	X(argc) \
	X(args) \
	X(write_char) \
	X(write_byte) \
	X(write_short) \
	X(write_long) \
	X(write_float) \
	X(write_string) \
	X(write_position) \
	X(write_dir) \
	X(write_angle) \
	X(unicast) \
	X(multicast)

#define SG_HOST_DECLARE_SERVICE_ID(name) SG_HOST_SERVICE_ID_##name,
enum {
	SG_HOST_REQUIRED_SERVICES(SG_HOST_DECLARE_SERVICE_ID)
	SG_HOST_SERVICE_COUNT
};
#undef SG_HOST_DECLARE_SERVICE_ID

_Static_assert(SG_HOST_SERVICE_COUNT == 35,
	"sg_host_t required-service inventory must contain exactly 35 unique slots");
/* Every member is a function pointer on the supported Quake II ABIs.  This
 * catches a new struct slot that was not added to the required-service list. */
_Static_assert(sizeof(sg_host_t) ==
	SG_HOST_SERVICE_COUNT * sizeof(((sg_host_t *)0)->dprint),
	"sg_host_t and its required-service inventory diverged");

extern sg_host_t sg_host;

/* Installs one complete host table into an empty boundary. */
qboolean SG_HostInstall(const sg_host_t *host);

/* fills the table from the LMCTF host; idempotent, call before any use */
void SG_HooksInit(void);

/*
 * Process-isolated boundary tests opt in explicitly; production modules expose
 * no reset seam.  This resets only sg_host, never consumer caches.  Callers
 * must prove that no consumer was initialized and no host allocation is live.
 */
#ifdef SG_HOST_TEST
void SG_HostResetForTest(void);
#endif

#endif /* SG_HOOKS_H */
