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
 * SG_CHAT_TOPIC_ORDER is the one exemption from the per-bot budget: an
 * acknowledgement that arrives four seconds after the order is worse than
 * none, and the order parser caps it at one ack per order anyway.
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

#define SG_CHAT_ORDER_TTL	90.0f   /* an unrepeated order stops binding */

/* ------------------------------------------------------ hooks and emission
 *
 * SG_ChatReset / SG_ChatFrame / SG_ChatSee hang off the matching CACO
 * entry points (sg_caco.c), which are already the once-per-level, once-per-
 * frame and once-per-bot-eye moments. SG_ChatSee runs this module's own
 * sighting scan for the items CACO does not track (body armour, power
 * armour) using the same PVS-plus-trace test.
 *
 * SG_ChatCarrierSeen and SG_ChatItemSeen are belief TRANSITIONS, passed the
 * bot that saw them. Neither is called from anywhere but a sighting.
 *
 * SG_ChatHear takes chat from any speaker. teamchat says whether it arrived
 * on say_team: an unaddressed order ("defend") is only obeyed on team chat,
 * while an addressed one ("arach defend") is obeyed either way.
 *
 * Two readers sit behind it and they treat the speaker differently on
 * purpose. The ORDER parser ignores FL_BOT speakers outright -- bots do not
 * order bots. The ITEM-CALL parser does not look at FL_BOT at all: a team
 * line naming a major arms that team's respawn clock whether a human or one
 * of ours typed it, per the owner's ruling of 2026-08-05 ("there should be
 * no difference in parsing a human"). A bot's own say_team comes back
 * through here, so that path dedupes against the clock it is about to be
 * handed by Chat_ArmClock; see the war story over Chat_HearItemCall.
 *
 *     It also feeds the ADDRESSED REPLY. A line that names one of ours and
 *     carries no verb the grammar recognises is somebody talking TO that
 *     bot, and the bot answers once, on the channel it was spoken on, after
 *     a delay that models a typist. One reply per bot per 30 seconds, never
 *     while carrying a flag or in contact, and never to another bot -- the
 *     FL_BOT refusal above is what makes the last one structural rather
 *     than a rule somebody has to remember.
 *
 *     Wired in Cmd_Say_f (g_cmds.c), right after the message text is
 *     assembled into `temp`, as SG_ChatHear(ent, temp, team). It is called
 *     directly rather than through the legacy bot's chat hook so the team
 *     flag survives: routed as public chat, a bare team-chat verb would need
 *     an addressee before it was obeyed.
 *
 * SG_ChatDeath is the taunt/grumble feed; it takes the victim, the attacker
 * and the means of death. The means of death is read now rather than
 * ignored: a death nobody can take credit for -- the lava, the ledge, his
 * own rocket -- gets a BYSTANDER line out of one enemy who watched it, which
 * is the one death a pub server always has a word for.
 *
 *     Wired in player_die (p_client.c) as
 *     SG_ChatDeath(self, attacker, meansOfDeath). It fed nothing for several
 *     waves -- the legacy bot's own death hook, BotChat_NotifyDeath, fed that
 *     bot and never this one, so there was never a connection to remove.
 *
 * SG_ChatLevelEnd is the match-end feed: winners gloat, losers grumble, and a
 * close game is called close by both sides.
 *
 *     Wired in BeginIntermission (p_hud.c), immediately after Victory(), which
 *     is the one place every way of ending a level converges -- timelimit,
 *     fraglimit, a target_changelevel and a match end all arrive there, and
 *     its own `if (level.intermissiontime) return;` makes it fire once. The
 *     lines are booked, not spoken: they go out over the following four
 *     seconds from SG_ChatFrame, which keeps running through intermission
 *     (G_RunFrame calls SG_RunFrame past the exitintermission check), and
 *     four seconds fits inside the five a client must wait before it can end
 *     intermission.
 *
 *     Read the outcome the way the scoreboard reads it: summed per-player
 *     STATS_SCORE per team, the same sum Victory() announces.
 */
void		SG_ChatReset(void);
void		SG_ChatFrame(void);
void		SG_ChatSee(edict_t *viewer);
void		SG_ChatCarrierSeen(edict_t *viewer, int team, edict_t *carrier);
void		SG_ChatItemSeen(edict_t *viewer, int index, qboolean up);

/* ------------------------------------------------------ the taken callout
 *
 * The owner's ruling of 2026-08-05: a team learns when an item is coming back
 * IF AND ONLY IF one of its own bots called the take out loud, and a callout
 * that the channel swallowed teaches nobody anything. sg_caco.c owns the
 * pickup hook (SG_NoteItemTaken) and the eyes; this side owns the words, the
 * one-speaker discipline, and the clock -- which is armed at EMISSION, inside
 * Chat_Flush, and never when the line is merely queued.
 *
 * Who is speaking, which decides the wording and the sg_debug label:
 *
 *   SG_ITEMCALL_TAKER   one of ours took it and is saying so. Needs no
 *                       sighting from anybody: the bot is holding the thing.
 *   SG_ITEMCALL_MATE    a human or legacy bot on OUR side took it and one of
 *                       ours watched. Humans do not narrate their own pickups.
 *   SG_ITEMCALL_ENEMY   the other side took it and one of ours watched. The
 *                       team told is the WITNESS's, never the taker's.
 */
#define SG_ITEMCALL_TAKER	0
#define SG_ITEMCALL_MATE	1
#define SG_ITEMCALL_ENEMY	2

void		SG_ChatItemTaken(edict_t *speaker, int team, edict_t *item,
                             int src);
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
