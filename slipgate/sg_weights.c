/*
 * sg_weights.c -- the weight tables: the one fitted component.
 * Moved verbatim from sg_arach.c in the 2026-08-11 standards pass.
 */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_weights.h"
#include "slipgate/sg_hooks.h"
#include <errno.h>

/*
 * The weight tables: the one fitted component. Rows are roles; every other
 * number in the system is a measured fact. Starting values encode the
 * owner's specification directly -- attackers live for the enemy flag and
 * take what is on the way, defenders deny armour near home, carriers value
 * health and armour and the way home, everyone values interception when our
 * flag is out and believed seen.
 *
 * These are the SHIPPED rows -- what a fresh install runs and what a missing
 * or malformed weights file falls back to. The live table below is seeded
 * from here.
 */
static const sg_weights_t sg_weight_compiled[SG_ROLES] = {
	/* objective  weap  armr  ammo  hlth  rune  powr   support intercept */
	{ 1.00f, { 0.35f, 0.30f, 0.20f, 0.15f, 0.20f, 0.40f }, 0.10f, 0.60f },  /* attack */
	{ 1.00f, { 0.30f, 0.50f, 0.25f, 0.20f, 0.15f, 0.10f }, 0.40f, 0.80f },  /* defend */
	{ 1.00f, { 0.10f, 0.45f, 0.20f, 0.50f, 0.10f, 0.05f }, 0.30f, 0.00f },  /* carry */
	/*
	 * recover: our flag is out there. The objective is the flag where belief
	 * puts it, the shopping list is the defender's (armour near home, not
	 * powerups across the map), and the thief's believed position outweighs
	 * everything else a non-carrier can be doing -- intercept above 1.
	 */
	{ 1.00f, { 0.30f, 0.50f, 0.25f, 0.20f, 0.15f, 0.10f }, 0.00f, 1.20f },  /* recover */
	/*
	 * escort: the objective IS the carrier's field, so carrier_support is 0
	 * (it would price the same field twice). Items stay modest so the escort
	 * does not wander off the carrier's road for them; intercept keeps the
	 * escort between the carrier and whoever is believed to be hunting.
	 */
	{ 1.00f, { 0.10f, 0.25f, 0.10f, 0.25f, 0.05f, 0.10f }, 0.00f, 0.80f },  /* escort */
};

/*
 * The LIVE table the body actually reads. It starts as a copy of the
 * compiled rows above and stays that way unless <gamedir>/slipgate-weights.cfg
 * or this map's <gamedir>/slipgate-weights-<mapname>.cfg says otherwise --
 * no files, no difference, byte for byte.
 *
 * The reason this exists: the weights are the one fitted component, and
 * fitting them meant a rebuild and a fleet restart per candidate row. Ten
 * servers that are never supposed to stop cannot pay that, so the numbers
 * that get TUNED now live where they can be edited between maps, while the
 * numbers that get SHIPPED stay in the const table above as the thing a
 * fresh install runs and the thing a bad file falls back to.
 */
static sg_weights_t	sg_weight_table[SG_ROLES];
static qboolean		sg_weights_ready;

const char *sg_role_names[SG_ROLES] = {
	"attack", "defend", "carry", "recover", "escort"
};

/*
 * Key names ARE the struct's field identifiers, so a key in the file and a
 * member in sg_weights_t are the same word -- the item classes spell out
 * their SG_FC_ enum tails. Order is load-bearing: Weights_Slot below maps
 * this index onto the struct, and the two must agree.
 */
static const char *sg_weight_fields[] = {
	"objective",
	"weapon", "armor", "ammo", "health", "rune", "powerup",
	"carrier_support", "intercept"
};
#define SG_WEIGHT_FIELDS ((int)(sizeof(sg_weight_fields) / \
                                sizeof(sg_weight_fields[0])))
/* Weights multiply millisecond route fields and item prices. Fitted values
 * orbit 1.0 (the shipped maximum is 1.2); a generous finite ceiling still
 * permits deliberate tuning while preventing a syntactically valid 1e38 from
 * overflowing Surface_At into +/-Inf and destroying the route ordering. */
#define SG_WEIGHT_MAX 1000.0f

/*
 * Who set what, so `sv sg weights` can say. Three layers, applied in this
 * order, each one writing over the one before it: the compiled rows, then
 * the global file, then the per-map file. The tag is the LAST layer that
 * touched that entry, which is the one whose number is standing.
 */
#define SG_WSRC_COMPILED	0
#define SG_WSRC_FILE		1
#define SG_WSRC_MAP			2

static const char *sg_weight_srcnames[] = { "compiled", "file", "map" };

static byte	sg_weight_src[SG_ROLES][SG_WEIGHT_FIELDS];
/* the layer the parse currently running is speaking for */
static byte	sg_weight_srcnow;
/* the per-map file that was actually read, empty when there was none */
static char	sg_weight_mappath[MAX_OSPATH];

static float *Weights_Slot(int role, int fi)
{
	sg_weights_t *w = &sg_weight_table[role];

	if (fi == 0)
		return &w->objective;
	if (fi <= SG_FIELD_CLASSES)
		return &w->item[fi - 1];
	if (fi == SG_FIELD_CLASSES + 1)
		return &w->carrier_support;
	return &w->intercept;
}

/* the same gamedir the runes come out of (Rune_Load): the engine's own
 * "gamedir" cvar, not g_local.h's `gamedir` global, which is really the
 * "game" cvar and is a different string on a server started with +set game.
 * mapname NULL asks for the global file, a map name for that map's playbook
 * (<gamedir>/slipgate-weights-<mapname>.cfg). */
static void Weights_Path(char *buf, int size, const char *mapname)
{
	cvar_t		*gamedir = sg_host.cvar("gamedir", "", 0);
	const char	*dir = gamedir->string[0] ? gamedir->string : ".";

	if (mapname && mapname[0])
		Com_sprintf(buf, size, "%s/slipgate-weights-%s.cfg", dir, mapname);
	else
		Com_sprintf(buf, size, "%s/slipgate-weights.cfg", dir);
}

/*
 * "<role>.<field> <value>". Returns false for a key that names no row or no
 * member -- the caller reports it and keeps reading, because one fat-fingered
 * line should cost that line and not the other forty-four.
 */
static qboolean Weights_Set(const char *key, float v)
{
	char	buf[64], *dot;
	int		role, fi;

	Com_sprintf(buf, sizeof(buf), "%s", key);
	dot = strchr(buf, '.');
	if (!dot)
		return false;
	*dot++ = 0;

	for (role = 0; role < SG_ROLES; role++)
		if (Q_stricmp(buf, sg_role_names[role]) == 0)
			break;
	if (role >= SG_ROLES)
		return false;

	for (fi = 0; fi < SG_WEIGHT_FIELDS; fi++)
		if (Q_stricmp(dot, sg_weight_fields[fi]) == 0)
			break;
	if (fi >= SG_WEIGHT_FIELDS)
		return false;

	*Weights_Slot(role, fi) = v;
	sg_weight_src[role][fi] = sg_weight_srcnow;
	return true;
}

/*
 * One layer: parse `path` over whatever is already in the live table,
 * stamping every entry it sets with sg_weight_srcnow. A file that is not
 * there is the ordinary case and says nothing -- that silence is what makes
 * a server with no weights files behave exactly as it did before any of
 * this existed. Returns whether the file was there and read.
 */
static qboolean Weights_ReadFile(const char *path)
{
	char	line[256];
	FILE	*f;
	int		n = 0, bad = 0;

	f = fopen(path, "r");
	if (!f)
		return false;           /* the ordinary case: shipped values, silently */

	while (fgets(line, sizeof(line), f))
	{
		char *hash, *key, *val, *end;
		float parsed;

		hash = strchr(line, '#');
		if (hash)
			*hash = 0;
		key = strtok(line, " \t\r\n");
		if (!key)
			continue;           /* blank or comment-only */
		val = strtok(NULL, " \t\r\n");
		if (!val)
		{
			sg_host.dprint("slipgate: weights: %s has no value\n", key);
			bad++;
			continue;
		}
		errno = 0;
		end = NULL;
		parsed = strtof(val, &end);
		if (errno || end == val || (end && *end) || !isfinite(parsed) ||
		    parsed < 0.0f || parsed > SG_WEIGHT_MAX)
		{
			sg_host.dprint("slipgate: weights: invalid value %s for %s\n",
			               val, key);
			bad++;
			continue;
		}
		if (Weights_Set(key, parsed))
			n++;
		else
		{
			sg_host.dprint("slipgate: weights: unknown key %s\n", key);
			bad++;
		}
	}
	fclose(f);
	sg_host.dprint("slipgate: weights: %d from %s%s\n", n, path,
	           bad ? va(" (%d rejected)", bad) : "");
	return true;
}

/*
 * Always from the compiled rows up, then the global file, then this map's
 * playbook. A reload that DROPS a key has to put the shipped value back;
 * leaving the previous file's number standing would make the live table
 * depend on the order the admin edited things in, which is the kind of state
 * nobody can reason about at 2am mid-wave. The same argument is why the map
 * layer is re-applied from scratch here rather than patched on a map change:
 * the only way the table can say what q2dm1 means is to be rebuilt for it.
 *
 * Map layer last because it is the narrower statement. A map whose flag room
 * punishes armour detours wants ONE row different from the fleet's, and the
 * global file stays the place the fleet-wide sweep lives.
 */
void Weights_Load(void)
{
	char	path[MAX_OSPATH];

	memcpy(sg_weight_table, sg_weight_compiled, sizeof(sg_weight_table));
	memset(sg_weight_src, 0, sizeof(sg_weight_src));
	sg_weight_mappath[0] = 0;
	sg_weights_ready = true;

	sg_weight_srcnow = SG_WSRC_FILE;
	Weights_Path(path, sizeof(path), NULL);
	Weights_ReadFile(path);

	/* level.mapname is empty before the first map is up -- Weights_Row can
	 * arm the table that early. No map, no map layer. */
	if (!level.mapname[0])
		return;

	sg_weight_srcnow = SG_WSRC_MAP;
	Weights_Path(path, sizeof(path), level.mapname);
	if (Weights_ReadFile(path))
		Com_sprintf(sg_weight_mappath, sizeof(sg_weight_mappath), "%s", path);
}

/*
 * The read the body does. Self-arming so the table can never be read as the
 * zeroed BSS it starts life as: SG_LevelSetup calls Weights_Load explicitly,
 * but a zeroed objective would silently flatten every field in the system,
 * and that failure is far too quiet to leave to call order.
 */
const sg_weights_t *Weights_Row(int role)
{
	if (!sg_weights_ready)
		Weights_Load();
	return &sg_weight_table[role];
}

void SG_WeightsReload(void)
{
	Weights_Load();
}

void SG_WeightsPrint(void)
{
	char	path[MAX_OSPATH];
	int		role, fi;

	Weights_Path(path, sizeof(path), NULL);
	if (!sg_weights_ready)
		Weights_Load();

	sg_host.cprint(NULL, PRINT_HIGH, "slipgate weights (%s):\n", path);
	/* only when there IS one: a server running no map playbook prints
	 * exactly what it always printed */
	if (sg_weight_mappath[0])
		sg_host.cprint(NULL, PRINT_HIGH, "  over map file (%s):\n",
		           sg_weight_mappath);
	for (role = 0; role < SG_ROLES; role++)
		for (fi = 0; fi < SG_WEIGHT_FIELDS; fi++)
			sg_host.cprint(NULL, PRINT_HIGH, "  %-8s %-16s %6.2f  %s\n",
			           sg_role_names[role], sg_weight_fields[fi],
			           *Weights_Slot(role, fi),
			           sg_weight_srcnames[sg_weight_src[role][fi]]);
}
