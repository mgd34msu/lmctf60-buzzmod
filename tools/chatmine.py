#!/usr/bin/env python3
"""Extract and normalize player chat from LMCTF demo corpora.

The tool parses chat records, removes protocol and control text, and emits the
corpus consumed by SLIPGATE chat tooling. Generated output is development data,
not a runtime or release claim.
"""
import json
import os
import re
import sys
from collections import Counter
from multiprocessing import Pool

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from demoprints import walk_prints                          # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_DEMOS = os.path.expanduser('~/Games/Quake2/lmctf-hooktest/demos')
OUT_JSON = os.path.join(HERE, 'chat-corpus.json')

MAX_LEN = 40
MIN_LEN = 2

PRINT_CHAT = 3

# --------------------------------------------------------------- extraction

SAY_TEAM_RE = re.compile(r'^\((.+?)\):\s(.*)$')
SAY_RE = re.compile(r'^(.+?):\s(.*)$')


def demo_chat(path):
    """(names, [(is_team, speaker, text)]) for one demo. Never raises."""
    try:
        _map, _nf, prints, skins = walk_prints(path)
    except Exception as exc:                        # a truncated demo
        return set(), [], '%s: %s' % (os.path.basename(path), exc)

    names = set()
    for v in skins.values():
        n = v.split('\\')[0].strip()
        if n:
            names.add(n)

    said = []
    for _frame, lvl, blob in prints:
        if lvl != PRINT_CHAT:
            continue
        # one svc_print can carry several newline-joined lines
        for raw in blob.split('\n'):
            raw = raw.strip()
            if not raw:
                continue
            m = SAY_TEAM_RE.match(raw)
            if m:
                said.append((True, m.group(1), m.group(2)))
                continue
            m = SAY_RE.match(raw)
            if m:
                said.append((False, m.group(1), m.group(2)))
    return names, said, None


# ------------------------------------------------------------------ filters

def name_tokens(names):
    """Word-ish fragments of every handle seen, for the name filter.

    Handles carry clan brackets and punctuation ("[420]seedless",
    "{5G|8L}maj[2]", "nh2fs!Sluggy"), so each splits into several tokens
    and the filter matches on whole tokens only.
    """
    # fragments that are also ordinary chat words -- matching these would
    # cost real lines ("brb" is both a handle fragment and the line we most
    # want to keep), and the handles they come from are not identifying on
    # their own.
    exempt = {'me', 'inc', 'sit', 'brb', 'afk', 'leg', 'zero', 'mf', 'jj'}
    toks = set()
    for n in names:
        for t in re.split(r'[^A-Za-z0-9]+', n):
            t = t.lower()
            if len(t) >= 2 and not t.isdigit() and t not in exempt:
                toks.add(t)
    return toks


# item-report binds and other client scripts talking on the team channel
MACRO_RE = re.compile(
    r'armor taken|armor picked|available run|power armor|power shield'
    r'|quad taken|quad picked|quad has been|quad in \d|quad \[|incoming quad'
    r'|artifact|dropping tech|dropped .*(at|west|east|north|south|above|below)'
    r'|respawn in|timed -|\[\d+ secs?\]|mega health in|megahealth in'
    r'|here is a|here is super|\*\* |<:|--quad--|copy that'
    r'|take flag at|power screen', re.I)

# match logistics, team assignment, map votes, admin and console talk
ADMIN_RE = re.compile(
    r'\bmaps?\b|\bvote|\brestart|\bobserve|\bobs\b|\bspectat|\bspec\b'
    r'|\bdemos?\b|\brecord|\bserver|\bping\b|\blag|\bport\b|\bpassword'
    r'|\bteams?\b|\bgo (blue|red)\b|\bin for\b|\bsub\b|\bref\b|\bresched'
    r'|\bforfeit|\bdisqualif|\blive\b|\bconnect|\bjoin\b|\bstart(ed|s)?\b'
    r'|\beta\b|\bmins?\b|\bminutes?\b|\bnetgraph|\bconsole|\bcommand'
    r'|\bskin\b|\bstream|\bvoice\b|\bchatroom|\bcomms\b|\bcable|\bvideo'
    r'|\bclient\b|\bmenu\b|\bswitch to\b|\bplay second\b|\bsit this out'
    r'|\bwhich one|\bor 20|\b15 (now|or)|\bplayer\b', re.I)

# a console variable or command typed into the wrong window
CONSOLE_RE = re.compile(
    r'^(cl_|r_|gl_|scr_|vid_|menu_|snd_|_[a-z0-9])|^b/din|^use_|^type\b'
    r'|^goto$|^shownet|^loadshader|\.wav$|^\.', re.I)

URL_RE = re.compile(
    r'https?://|www\.|\.(com|net|org|io|gg)\b|\b\d{1,3}(\.\d{1,3}){3}\b'
    r'|:\d{4,5}\b', re.I)

# crude / hostile vocabulary. Conservative on purpose: this ships inside
# Mike's mod, so a borderline word loses its line rather than argues for it.
# "damn" is deliberately NOT here -- it is period grumble, not abuse.
CRUDE = {
    'fuck', 'fucking', 'fucked', 'fuk', 'fk', 'fkn', 'fu', 'fux', 'stfu',
    'shit', 'sht', 'shitty', 'ass', 'asshole', 'arse', 'bitch', 'bastard',
    'dick', 'cock', 'pp', 'pee', 'pees', 'peeing', 'piss', 'cunt', 'twat',
    'fag', 'faggot', 'gay', 'retard', 'retarded', 'tard', 'nig', 'nigga',
    'whore', 'slut', 'suckme', 'wank', 'jerk', 'idiot', 'moron', 'noob',
    'newb', 'scrub', 'trash', 'garbage', 'mfker', 'mfkers', 'mf',
    'honkey', 'honky', 'cracker', 'jew', 'nazi', 'rape', 'kys',
}

VOWELS = set('aeiouy')


def is_mash(text):
    """Keyboard mash / typo wreckage, as opposed to a short real word."""
    for w in re.findall(r"[a-z']+", text.lower()):
        if len(w) >= 5 and not (VOWELS & set(w)):
            return True                     # no vowel in a long word
        if re.search(r'[bcdfghjklmnpqrstvwxz]{5}', w):
            return True                     # five consonants running
        if len(w) >= 6 and len(set(w)) <= 2:
            return True                     # "aaaaaa", "yoyoyoyo"
    return False


def clean(text):
    """Normalise a raw say payload; '' means it is not worth considering."""
    text = ''.join(c for c in text if 32 <= ord(c) < 127)
    text = text.replace('\\', ' ').strip()
    text = re.sub(r'\s+', ' ', text)
    return text


def keep(text, names):
    """(True, '') to keep, (False, reason) to drop. Text is already clean."""
    low = text.lower()
    if not (MIN_LEN <= len(text) <= MAX_LEN):
        return False, 'length'
    if URL_RE.search(text):
        return False, 'url'
    if CONSOLE_RE.search(text):
        return False, 'console'
    if MACRO_RE.search(text):
        return False, 'macro'
    if ADMIN_RE.search(text):
        return False, 'admin'
    words = set(re.findall(r"[a-z0-9']+", low))
    if words & CRUDE:
        return False, 'crude'
    if words & names:
        return False, 'name'
    if is_mash(low):
        return False, 'mash'
    if re.fullmatch(r'[\d\s\-/]+', text):
        return False, 'numeric'
    if not re.search(r'[a-z]', low) and not re.fullmatch(
            r'[:;=x8][\'-]?[)(dop\]/\\|]+|\.{2,}|<3|\^_\^', low):
        return False, 'punct'           # stray punctuation, but keep emotes
    return True, ''


# ------------------------------------------------------------- bucketing

BUCKETS = ['GREETING', 'TAUNT', 'GRUMBLE', 'GG_ENDGAME', 'REACTION', 'CALL']

# The mechanical filters above take 4.4k lines said down to a few hundred
# survivors, but a survivor is not automatically a REUSABLE line: most of
# what two teams say to each other on a given night is about that night
# ("ES is running 5 behind", "gotta light candle for wife"). A catch-all
# bucket therefore fills with one-offs no bot could ever say again.
#
# So the last gate is a promotion list, matched on the lowercased line.
# Anything not on it is dropped as 'unsorted'. The list is the reviewable
# artifact -- every line here was read out of the corpus first, and the
# --audit run reports any entry that has stopped appearing in it, which is
# what keeps this from quietly drifting into invention.
PROMOTE = {
    'GREETING': [
        "gl hf", "glhf", "gl hf!", "glhf !", "glhf!", "gl!", "gl",
        "gl guys", "glhf guys", "gl hf yo", "good luck", "good luck hf",
        "hf", "hi", "hi all!", "hey", "hey!", "hey guys", "heya", "heyho",
        "oh hai", "yoyo", "whats up", "hello?", "been a while",
        "good to see you", "here we go", "sweet here we go",
        "ready", "ready!", "ready !", "ready?", "rdy",
        "we rdy", "we r rdy", "were ready", "we're ready", "we're ready!",
        "we good", "good here", "good to go", "im here", "we here",
    ],
    'TAUNT': [
        "zoom", "die !!", "die well", "no u", "never!", "dont u worry",
        "im fast", "we can beat you with 4", "we were born ready",
        "as i'll ever be", "bam", "word",
    ],
    'GRUMBLE': [
        "damn", "ouch", "oh", "sorry", "im sorry",
        "no worries", "no worries. :)", "nvm", "phew", "phew!",
        "shaking", "good lord", "good lord i'm shaking", "well...",
        "its not looking good", "you gotta be kidding me", "i guess",
        "nah.", "no!",
    ],
    'GG_ENDGAME': [
        "gg", "gg!", "gg !", "gg's", "ggs", "ggs!", "gggs", "gfg", "tgg",
        "gg wp", "np gg", "ggs all", "ggs guys", "ggs everyone",
        "gg's everyone", "ggs ya'll", "ggs gnite", "really ggs!",
        "gg guys well played", "well played", "well played!",
        "yeah, well played", "alright - well played",
        "good game", "good game!", "great game", "great game!",
        "great game guys", "great game all", "great games guys",
        "great night of games", "good stuff!", "insane game",
        "loved this game", "amazine game!", "that's game", "tough one",
        "that was tough!", "nicely done", "nicely done guys",
        "good to take the L", "nite",
    ],
    'REACTION': [
        "ok", "okay", "oh okay", "alright", "yes", "yeah", "yep", "yup",
        "yea really", "sure", "no", "nope", "i bet", "i suppose",
        "lol", "LOL", "lol wat", "lol yay", "lol close call", "haha",
        "hah", "rofl", "omg", "wow", "wow wow", "what", "good", "nice",
        "nice!", "n1", "nice one", "nice shot!", "awesome", "cool",
        "cool!", "crazy!", "wild stuff", "fun stuff", "i like it",
        "no problem", "thx", "thanks!", "shoot", "ns",
        "l8", "def weird", "there you go", "lets run it",
        ":)", ":D", ":/", ":o", ";)", "=]", "xD",
    ],
    'CALL': [
        "Take the flag!", "BASE IS CLEAR", "base is overrun",
        "back", "brb", "be back", "break", "need a mo",
    ],
}

_PROMOTE_INDEX = {}
for _b, _lines in PROMOTE.items():
    for _l in _lines:
        _PROMOTE_INDEX[_l.lower()] = _b


def bucket_of(text):
    """Bucket name, or None when the line is not promotable."""
    return _PROMOTE_INDEX.get(text.lower())


# ------------------------------------------------------------------- main

def main(argv):
    demodir = argv[1] if len(argv) > 1 else DEFAULT_DEMOS
    files = sorted(
        os.path.join(demodir, f) for f in os.listdir(demodir)
        if f.lower().endswith('.dm2'))
    if not files:
        sys.exit('no demos under %s' % demodir)

    with Pool(min(12, len(files))) as pool:
        results = pool.map(demo_chat, files)

    names, said, bad = set(), [], []
    for nm, sd, err in results:
        names |= nm
        said.extend(sd)
        if err:
            bad.append(err)

    # speakers are a second source of handles: someone who talked before
    # the skin configstring landed still named himself in the print
    for _team, who, _text in said:
        who = clean(who)
        if who:
            names.add(who)
    tokens = name_tokens(names)

    kept = Counter()
    drops = Counter()
    for _team, _who, raw in said:
        text = clean(raw)
        if not text:
            drops['empty'] += 1
            continue
        ok, why = keep(text, tokens)
        if ok:
            kept[text] += 1
        else:
            drops[why] += 1

    buckets = {b: [] for b in BUCKETS}
    seen_promoted = set()
    for text, n in kept.most_common():
        b = bucket_of(text)
        if b is None:
            drops['unsorted'] += 1
            continue
        seen_promoted.add(text.lower())
        buckets[b].append([text, n])

    missing = sorted(set(_PROMOTE_INDEX) - seen_promoted)

    out = {
        'source': demodir,
        'demos': len(files),
        'players_seen': len(names),
        'chat_lines_seen': len(said),
        'kept_unique': len(kept),
        'drops': dict(drops.most_common()),
        'buckets': {b: [t for t, _n in buckets[b]] for b in BUCKETS},
        'counts': {b: len(buckets[b]) for b in BUCKETS},
        'promotions_absent_from_corpus': missing,
    }
    with open(OUT_JSON, 'w') as fh:
        json.dump(out, fh, indent=1)
        fh.write('\n')

    print('demos      %d  (%d unreadable)' % (len(files), len(bad)))
    print('players    %d handles, %d name tokens' % (len(names), len(tokens)))
    print('chat       %d lines said, %d unique kept' % (len(said), len(kept)))
    print('drops      %s' % ', '.join(
        '%s=%d' % kv for kv in drops.most_common()))
    print('promoted   %d lines in %d buckets' % (
        sum(len(v) for v in buckets.values()), len(BUCKETS)))
    if missing:
        print('WARNING    promotion entries absent from this corpus: %s'
              % ', '.join(repr(m) for m in missing))
    print('wrote      %s' % OUT_JSON)
    for b in BUCKETS:
        print('\n=== %s (%d) ===' % (b, len(buckets[b])))
        for text, n in buckets[b]:
            print('  %4d  %s' % (n, text))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
