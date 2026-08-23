/*
 * sg_cvars.h -- the one place a SLIPGATE cvar name or default lives.
 * Add a cvar with one X() entry. The struct field, registration, and lookup
 * derive from this list.
 */
#ifndef SG_CVARS_H
#define SG_CVARS_H

#define SG_CVAR_LIST(X) \
	X(aimtexture, "sg_aimtexture", "1") \
	X(airgain, "sg_airgain", "0") \
	X(airstrafe, "sg_airstrafe", "2") \
	X(approachcover, "sg_approachcover", "200") \
	X(atkobj, "sg_atkobj", "125") \
	X(beliefcone, "sg_beliefcone", "0") \
	X(beliefrange, "sg_beliefrange", "0") \
	X(breather, "sg_breather", "0") \
	X(carrycover, "sg_carrycover", "800") \
	X(carryhop, "sg_carryhop", "0") \
	X(carrypress, "sg_carrypress", "0") \
	X(clockplay, "sg_clockplay", "0") \
	X(crowdhold, "sg_crowdhold", "0") \
	X(dangerpersistport, "sg_dangerpersistport", "0") \
	X(debug, "sg_debug", "0") \
	X(defpost, "sg_defpost", "0") \
	X(defcombat, "sg_defcombat", "1") \
	X(defreact, "sg_defreact", "3") \
	X(defshift, "sg_defshift", "1") \
	X(depace, "sg_depace", "0") \
	X(drawplan, "sg_drawplan", "0") \
	X(duelroles, "sg_duelroles", "1") \
	X(edgeride, "sg_edgeride", "0") \
	X(escapeprior, "sg_escapeprior", "1") \
	X(escortdose, "sg_escortdose", "35") \
	X(exitasym, "sg_exitasym", "0") \
	X(fandense, "sg_fandense", "2") \
	X(firedisc, "sg_firedisc", "0") \
	X(flagprior, "sg_flagprior", "0") \
	X(flycook, "sg_flycook", "1") \
	X(freeride, "sg_freeride", "0") \
	X(handoff, "sg_handoff", "0") \
	X(hookpong, "sg_hookpong", "0") \
	X(humantrace, "sg_humantrace", "0") \
	X(humantracedir, "sg_humantrace_dir", "") \
	X(hopfire, "sg_hopfire", "0") \
	X(humanprior, "sg_humanprior", "0") \
	X(interpose, "sg_interpose", "3") \
	X(itemcomm, "sg_itemcomm", "1") \
	X(itemlead, "sg_itemlead", "1") \
	X(landlead, "sg_landlead", "1") \
	X(landtick, "sg_landtick", "0") \
	X(linklatch, "sg_linklatch", "0") \
	X(lonewolf, "sg_lonewolf", "1") \
	X(lookahead, "sg_lookahead", "0") \
	X(megaworth, "sg_megaworth", "0") \
	X(nadelead, "sg_nadelead", "1") \
	X(nakedcarry, "sg_nakedcarry", "0") \
	X(nobacktrack, "sg_nobacktrack", "60") \
	X(noweave, "sg_noweave", "0") \
	X(patrol, "sg_patrol", "0.55") \
	X(persona, "sg_persona", "1") \
	X(press, "sg_press", "1") \
	X(preturn, "sg_preturn", "1") \
	X(pursuit, "sg_pursuit", "0") \
	X(pursuitz, "sg_pursuitz", "8") \
	X(radio, "sg_radio", "1") \
	X(raillane, "sg_raillane", "1") \
	X(railrhythm, "sg_railrhythm", "0") \
	X(ribbon, "sg_ribbon", "48") \
	X(ropecost, "sg_ropecost", "1000") \
	X(ropetravel, "sg_ropetravel", "0") \
	X(routedither, "sg_routedither", "0") \
	X(routejitter, "sg_routejitter", "8") \
	X(runetoss, "sg_runetoss", "2") \
	X(scoop, "sg_scoop", "1") \
	X(sessiondb, "sg_sessiondb", "1") \
	X(shelfcost, "sg_shelfcost", "0") \
	X(smooth, "sg_smooth", "0") \
	X(soundfire, "sg_soundfire", "1") \
	X(spawnbeat, "sg_spawnbeat", "0") \
	X(sticky, "sg_sticky", "0") \
	X(strictgrab, "sg_strictgrab", "0") \
	X(subframes, "sg_subframes", "8") \
	X(tactics, "sg_tactics", "1") \
	X(tapvar, "sg_tapvar", "0") \
	X(teamskew, "sg_teamskew", "0") \
	X(termbrake, "sg_termbrake", "1") \
	X(tilt, "sg_tilt", "0") \
	X(timercall, "sg_timercall", "0") \
	X(turnrate, "sg_turnrate", "600") \
	X(unlinger, "sg_unlinger", "0") \
	X(watercarry, "sg_watercarry", "0") \
	X(wavepush, "sg_wavepush", "0") \
	X(wcommit, "sg_wcommit", "2") \
	X(wetwork, "sg_wetwork", "1") \
	X(wswitch, "sg_wswitch", "0")

typedef struct sg_cvars_s {
#define X(f, n, d) cvar_t *f;
	SG_CVAR_LIST(X)
#undef X
} sg_cvars_t;

extern sg_cvars_t sg_cv;

void SG_CvarsInit(void);   /* idempotent; call before any sg_cv use */

#endif
