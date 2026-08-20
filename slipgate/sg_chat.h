/*
 * sg_chat.h -- SLIPGATE's voice: team callouts, personality, human orders.
 *
 * Include AFTER g_local.h: everything here is spelled in edict_t, vec3_t,
 * qboolean and int on purpose. No SLIPGATE-internal type appears in this
 * header, so bl_chat.c can include it without pulling in sg_local.h or
 * sg_rune.h.
 *
 * Three things live behind this header.
 *
 *   1. ONE authority over the say_team channel. Every SLIPGATE line to a
 *      team -- CACO's flag callouts included -- goes out through
 *      SG_ChatSayTeam, which holds the per-bot budget and the per-topic
 *      team cooldown. There is deliberately no second emitter: two of them
 *      cannot keep a channel readable, whatever each one's own limits say.
 *
 *   2. Belief discipline, unchanged (slipgate/SLIPGATE.md, sg_caco.c head
 *      comment). A callout is emitted by the bot that SAW the thing, about
 *      what its own team believes. The only two claims here that no
 *      teammate looked at are (a) a bot naming an item it picked up itself,
 *      and (b) a respawn clock read off ent->item->quantity, which is map
 *      knowledge every player has.
 *
 *   3. Orders from HUMAN teammates over chat, stored here and read back by
 *      the role code through SG_ChatOrderedRole / SG_ChatEscortTarget. This
 *      module never sets a role itself.
 */

#pragma once

/*
 * Role values, mirroring sg_role_t in slipgate/sg_local.h. Mirrored rather
 * than included so this header stays usable from the legacy chat glue; the
 * numbers are load-bearing there (the weight table's row order) and must not
 * be renumbered on either side.
 */
#define SG_CHAT_ROLE_NONE		(-1)    /* no standing order */
#define SG_CHAT_ROLE_ATTACK		0
#define SG_CHAT_ROLE_DEFEND		1
#define SG_CHAT_ROLE_CARRY		2
#define SG_CHAT_ROLE_RECOVER	3
#define SG_CHAT_ROLE_ESCORT		4

/*
 * Rate-limit topics. One line per team per topic at a time is queued, and
 * chat_topic_gap[] in sg_chat.c holds each topic's team cooldown.
 * SG_CHAT_TOPIC_ORDER is fully exempt from the per-bot budget: an
 * acknowledgement that arrives four seconds after the order is worse than
 * none, and the order parser caps it at one ack per order anyway. CACO flag
 * intelligence may preempt an older lower-priority use of its speaker's
 * budget, but it stamps the budget when it speaks so ordinary chatter after
 * the flag line remains suppressed.
 */
#define SG_CHAT_TOPIC_CACO			0   /* flag callouts queued by sg_caco.c */
#define SG_CHAT_TOPIC_CARRIER		1   /* enemy flag carrier position */
#define SG_CHAT_TOPIC_ITEM_UP		2   /* an item believed back up */
#define SG_CHAT_TOPIC_ITEM_GONE		3   /* an item believed taken */
#define SG_CHAT_TOPIC_ITEM_SOON		4   /* respawn clock says it is close */
#define SG_CHAT_TOPIC_ORDER			5   /* acknowledging a human's order */
#define SG_CHAT_TOPIC_STEAL			6   /* our team has their flag */
/*
 * The short-form major-item timer call (sg_timercall). Its own topic rather
 * than a second user of ITEM_SOON: the twenty-second gap that keeps one
 * voice on the clock must not be spendable by an "up in ~Ns" about a
 * shotgun, and the two forms are switched between per item, not per line.
 */
#define SG_CHAT_TOPIC_TIMER			7
/*
 * Majors take/witness calls (sg_itemcomm). Their own lane, not ITEM_GONE's
 * (smoke, 2026-08-05): the first quad call of a game got eaten by shotgun
 * chatter sharing the topic, and per Rule 19 an eaten call leaves the team
 * ignorant -- honest, but a human prioritizes "QUAD TAKEN" over "took
 * shells", so the channel should too.
 */
#define SG_CHAT_TOPIC_MAJOR			8
/*
 * Answering a human who typed this bot's name and no order the grammar
 * knows. Its own lane rather than ORDER's, which is exempt from the per-bot
 * say_team budget: an acknowledgement earns that exemption by being useless
 * four seconds late, and small talk does not. A reply that the budget eats is
 * a reply nobody was owed.
 */
#define SG_CHAT_TOPIC_REPLY			9
#define SG_CHAT_TOPICS				10

static inline int SG_ChatTopicBlocksOnBotGap(int topic)
{
	return topic != SG_CHAT_TOPIC_ORDER && topic != SG_CHAT_TOPIC_CACO;
}

static inline int SG_ChatTopicStampsBotGap(int topic)
{
	return topic != SG_CHAT_TOPIC_ORDER;
}

/* ------------------------------------------------- the integrator's calls
 *
 * These two are what SG_Role() in sg_arach.c wires in. Nothing else in this
 * module touches roles.
 *
 * SG_ChatOrderedRole(bot)
 *     SG_CHAT_ROLE_NONE (-1) when this bot has no live human order, else the
 *     ordered role as an sg_role_t value. A role returned here is meant to
 *     REPLACE whatever SG_Role would have chosen, not to bias it. The order
 *     dies on its own after SG_CHAT_ORDER_TTL seconds (90), when the human
 *     who gave it leaves, or when either of them changes team; the caller
 *     needs no expiry logic of its own.
 *
 *     Safe to call every frame for any edict: a NULL, a non-client, a
 *     non-bot or a bot nobody has ordered all answer -1.
 *
 * SG_ChatEscortTarget(bot)
 *     NULL unless the standing order is SG_CHAT_ROLE_ESCORT, in which case
 *     it is the edict of the HUMAN who asked to be escorted -- not our own
 *     flag carrier. The escort weights point at whoever this returns; a
 *     caller that assumes the carrier will follow the wrong player.
 */
int			SG_ChatOrderedRole(edict_t *bot);
edict_t		*SG_ChatEscortTarget(edict_t *bot);
void		SG_ChatResetClient(edict_t *client);

#define SG_CHAT_ORDER_TTL	90.0f   /* an unrepeated order stops binding */

/* Sight-based callbacks receive the observing bot. SG_ChatHear accepts human
 * and bot text but ignores bot-issued orders. */
void		SG_ChatReset(void);
void		SG_ChatFrame(void);
void		SG_ChatSee(edict_t *viewer);
void		SG_ChatCarrierSeen(edict_t *viewer, int team, edict_t *carrier);
void		SG_ChatItemSeen(edict_t *viewer, int index, qboolean up);

/* Taken-call sources select wording. Non-taker sources require a witness. */
#define SG_ITEMCALL_TAKER	0
#define SG_ITEMCALL_MATE	1
#define SG_ITEMCALL_ENEMY	2

void		SG_ChatMegaDeath(edict_t *victim); /* the obituary starts the mega clock */
void		SG_ChatItemTaken(edict_t *speaker, int team, edict_t *item,
                             int src,
		                 edict_t *taker);
qboolean	SG_ChatItemMajor(edict_t *e);   /* worth a bot's breath at all */
qboolean	SG_ChatBudgetClear(edict_t *bot);  /* Chat_Speaker's own test */
void		SG_ChatHear(edict_t *speaker, const char *msg, qboolean teamchat);
void		SG_ChatDeath(edict_t *victim, edict_t *attacker, int mod);
void		SG_ChatLevelEnd(void);

/*
 * The single say_team gate. Returns false when the line was suppressed --
 * the caller's own bookkeeping ("the team has heard this") must be
 * conditional on the return, or a dropped line will be recorded as said.
 */
qboolean	SG_ChatSayTeam(edict_t *speaker, const char *line, int topic);

/* the curated landmark namer (weapons/armour/powerups; never health,
 * never flags): the ONE way a position is put into words. sg_caco.c's
 * old nearest-anything namer produced "rune by the Health" spam and once
 * located a carrier by the flag he was carrying. */
void SG_ChatLocName(vec3_t pos, char *out, int len);
