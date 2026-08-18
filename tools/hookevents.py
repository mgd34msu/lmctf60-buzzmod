"""Bounded readers and pairing authority for hook diagnostic logs.

The diagnostic writer has two machine-readable line families:

* ``HOOKFIRE``/``HOOKEND`` records, whose exact key order is part of the
  writer contract; and
* the ``SG`` telemetry records consumed by the offline hook reports; and
* bounded auxiliary markers (abort/bite/sky-hold/death) used by ``hookdiag``.

This module is deliberately the only parser for both families.  In
particular, every decimal token is checked for an ASCII, canonical, bounded
grammar before it is converted.  A malformed ``SG `` line is represented by a
typed :class:`TelemetryAnomaly`; consumers share the fatal/reporting policy
declared below.
"""

from dataclasses import dataclass, replace
import math
import re


FIRE_KEYS = ("id", "bot", "kind", "link", "role", "map", "anchor_q8")
END_KEYS = FIRE_KEYS + ("reason", "detail")
TERMINAL_REASONS = frozenset((
    "superseded", "burst", "graph-fail", "arrived", "apex", "landed",
    "landing_timeout", "burststall", "noattach", "death",
    "physics-incompatible", "declared-door-interrupt", "stale-host-rope",
    "slot-retirement", "map-transition",
))

# The C writer uses ``char line[640]``.  Keep that full capacity available to
# the reader.  SG output is currently much shorter, but gets a separate,
# generous ceiling so a future optional field cannot accidentally become an
# unbounded parser input.  Length checks happen before tokenization.
HOOK_LINE_MAX = 640
SG_LINE_MAX = 4096
# ``scan_file`` must cap the physical read itself, not merely reject after a
# text iterator has already allocated an arbitrarily long line.  One extra
# character distinguishes an exact-boundary unterminated line from overflow.
PHYSICAL_LINE_MAX = max(HOOK_LINE_MAX, SG_LINE_MAX)
TOKEN_MAX = 128
ID_MAX = 96

# Pairing is an offline diagnostic tool, but it must remain bounded when fed a
# hostile or accidentally concatenated stream.  These are public so tests and
# callers can construct exact-boundary probes without duplicating policy.
MAX_INPUT_LINES = 200_000
MAX_RECOGNIZED_EVENTS = 150_000
MAX_CANDIDATE_KEYS = 80_000
MAX_PAIRS = 30_000
MAX_INCOMPLETES = 120_000
MAX_ANOMALIES = 90_000

INT32_MIN, INT32_MAX = -2147483648, 2147483647
UINT64_MAX = 18446744073709551615

# Both consumers report malformed SG records and return nonzero.  They still
# ignore the bad sample for analytics, so a malformed sample cannot fabricate
# an origin, speed, role, action, or ground state.
TELEMETRY_ANOMALY_FATAL = True


@dataclass(frozen=True)
class HookEvent:
    event: str
    id: str
    bot: str
    kind: str
    link: int
    role: int
    map: str
    anchor_q8: tuple
    line: int
    reason: str = None
    detail: str = None

    @property
    def key(self):
        return (self.id, self.bot)


@dataclass(frozen=True)
class ProtocolAnomaly:
    line: int
    code: str
    message: str
    key: tuple = None
    keys: tuple = ()
    global_fatal: bool = False


@dataclass(frozen=True)
class TelemetryAnomaly:
    """A recognized ``SG `` line that failed the shared telemetry schema."""

    line: int
    code: str
    message: str
    fatal: bool = TELEMETRY_ANOMALY_FATAL


@dataclass(frozen=True)
class AuxMarker:
    """A bounded, validated auxiliary diagnostic marker.

    The hook report only needs the marker kind and owner for most records.
    ``HOOKABORT`` carries ``reason``; ``HOOKBITE`` carries ``offset`` and
    ``into`` plus the three producer vectors.  Keeping this record typed and
    line-numbered means consumers do not need a second copy of the auxiliary
    grammar (or a second physical pass over the log).
    """

    kind: str
    bot: str
    line: int
    reason: str = None
    offset: int = None
    into: str = None
    org: tuple = None
    want: tuple = None
    got: tuple = None


@dataclass(frozen=True)
class EOFMarker:
    """The final physical line position delivered to a scan observer."""

    line: int


@dataclass(frozen=True)
class SGTelemetry:
    """The bounded SG fields used by ``hookclose`` and ``hookdiag``.

    The C producer prints ``role``, ``seed``, ``goal``, ``sgoal``, ``link``,
    ``act``, ``hp``, ``dh``, and ``dl`` with ``%d`` (an int32 lexical/range
    contract).  Coordinates are rounded decimal int32 values; ``spd`` is a
    nonnegative int32 speed; and ``gnd``/``eng`` are booleans in ``{0, 1}``.
    ``st`` is the one optional decimal field in the producer's full line; it
    is bounded and finite when present but is not used by either consumer.
    """

    bot: str
    role: int
    seed: int
    goal: int
    sgoal: int
    spd: int
    org: tuple
    link: int
    act: int
    hp: int
    gnd: int
    line: int = 0
    dh: int = None
    dl: int = None
    st: float = None
    eng: int = None


@dataclass(frozen=True)
class HookPair:
    fire: HookEvent
    end: HookEvent

    @property
    def key(self):
        return self.fire.key


@dataclass(frozen=True)
class PairingResult:
    pairs: tuple
    incomplete: tuple
    anomalies: tuple
    global_fatal: bool = False
    tainted_keys: tuple = ()
    telemetry_anomalies: tuple = ()

    @property
    def valid_ids(self):
        return tuple(pair.key for pair in self.pairs)

    def with_telemetry_anomalies(self, anomalies):
        """Return this result with the consumer-observed SG anomalies attached."""
        return replace(self, telemetry_anomalies=tuple(anomalies))


def _ascii_digits(value):
    """Whether *value* is nonempty ASCII decimal digits only."""
    if not isinstance(value, str) or not value:
        return False
    return all("0" <= char <= "9" for char in value)


def _parse_uint64(value, nonzero=False):
    """Parse canonical ``uint64_t`` text, or return ``None``.

    The width and lexicographic checks precede ``int``.  Thus a 4,299-digit
    token is rejected without ever reaching Python's integer conversion.
    """
    if not isinstance(value, str) or not value or len(value) > 20:
        return None
    if value == "0":
        return None if nonzero else 0
    if value[0] == "0" or not _ascii_digits(value):
        return None
    maximum = str(UINT64_MAX)
    if len(value) > len(maximum) or (
            len(value) == len(maximum) and value > maximum):
        return None
    number = int(value)  # lexical width is capped at 20 digits above
    return number if (not nonzero or number != 0) else None


def parse_uint64(value, nonzero=False):
    """Public bounded ``uint64`` helper used by tests and readers."""
    return _parse_uint64(value, nonzero=nonzero)


def _parse_int32(value):
    """Parse canonical signed ``int32_t`` text, or return ``None``.

    Accepted forms are exactly ``0`` or ``-?[1-9][0-9]{0,9}``, followed by an
    exact signed range check.  This rejects ``+1``, leading zeroes, ``-0``,
    Unicode digits, and all unbounded tokens before conversion.
    """
    # Eleven characters covers the sign plus ten decimal digits.  Check this
    # before slicing so a hostile 100,000-digit token is not copied.
    if not isinstance(value, str) or not value or len(value) > 11:
        return None
    negative = value[0] == "-"
    digits = value[1:] if negative else value
    if not digits or len(digits) > 10 or not _ascii_digits(digits):
        return None
    if digits == "0":
        return None if negative else 0
    if digits[0] == "0":
        return None
    limit = str(-INT32_MIN if negative else INT32_MAX)
    if len(digits) > len(limit) or (
            len(digits) == len(limit) and digits > limit):
        return None
    number = int(value)  # at most 11 characters, after lexical validation
    return number if INT32_MIN <= number <= INT32_MAX else None


def parse_int32(value):
    """Public bounded ``int32`` helper used by readers."""
    return _parse_int32(value)


# Kept as private compatibility names for the first-generation tests.
_parse_int = _parse_int32
_INT_MIN, _INT_MAX = INT32_MIN, INT32_MAX
_UINT64_MAX = UINT64_MAX


def _is_uint(value, nonzero=False):
    return _parse_uint64(value, nonzero=nonzero) is not None


def _parse_id(value):
    """Validate a writer id and its nonzero token/sequence components."""
    if not isinstance(value, str) or len(value) > ID_MAX or len(value) < 2:
        return False
    if value[0] == "i":
        parts = value[1:].split(".")
        if len(parts) != 3:
            return False
        token, epoch, sequence = (_parse_uint64(part) for part in parts)
        return (token is not None and token != 0 and epoch is not None and
                sequence is not None and sequence != 0)
    if value[0] == "z":
        parts = value[1:].split(".")
        if len(parts) != 4:
            return False
        fallback_epoch, token, sequence_epoch, sequence = (
            _parse_uint64(part) for part in parts)
        return (fallback_epoch is not None and token is not None and
                token != 0 and sequence_epoch is not None and
                sequence is not None and sequence != 0)
    return False


def _is_token(value, maximum=TOKEN_MAX):
    """Validate the writer's bounded ASCII token alphabet."""
    if not isinstance(value, str) or not value or len(value) > maximum:
        return False
    for char in value:
        if not ("A" <= char <= "Z" or "a" <= char <= "z" or
                "0" <= char <= "9" or char in "_.-"):
            return False
    return True


def _is_sg_bot(value):
    """Validate a telemetry bot name (the producer may retain ``[SG]``)."""
    if not isinstance(value, str) or not value or len(value) > TOKEN_MAX:
        return False
    # SG telemetry is text emitted from a netname.  Keep it bounded and
    # printable while accepting the production ``[SG]Arach`` prefix.
    return all(0x21 <= ord(char) <= 0x7e and char != ":" for char in value)


def _parse_anchor(value):
    if not isinstance(value, str) or len(value) > 40:
        return None
    values = value.split(",")
    if len(values) != 3:
        return None
    parsed = tuple(_parse_int32(item) for item in values)
    return parsed if all(item is not None for item in parsed) else None


def _parse_bool(value):
    number = _parse_int32(value)
    return number if number in (0, 1) else None


def _parse_decimal(value):
    """Parse the bounded fixed-point form printed for SG ``st``."""
    if not isinstance(value, str) or not value or len(value) > 32:
        return None
    if value[0] == "-":
        digits = value[1:]
    else:
        digits = value
    if not digits or digits.count(".") > 1:
        return None
    whole, dot, fraction = digits.partition(".")
    if not _ascii_digits(whole) or (dot and not _ascii_digits(fraction)):
        return None
    # ``st`` is a telemetry diagnostic value, not a calculation input.  Keep
    # conversion bounded and reject non-finite/pathological magnitudes.
    number = float(value)  # at most 32 validated characters
    return number if math.isfinite(number) and abs(number) <= 1.0e9 else None


def _telemetry_bad(line_number, code, message):
    return None, TelemetryAnomaly(line_number, code, message)


def _split_kv(token):
    if not isinstance(token, str) or token.count("=") != 1:
        return None
    key, value = token.split("=", 1)
    if not key or not value or not _is_token(key):
        return None
    return key, value


def parse_sg_line(line, line_number=0):
    """Return ``(SGTelemetry, None)`` or ``(None, TelemetryAnomaly)``.

    Only the fields used by the hook consumers are authoritative.  The
    producer's optional ``dh``, ``dl``, ``st``, and ``eng`` fields are checked
    when present; harmless legacy metadata tokens (such as fixture ``x``) are
    accepted but never converted.  A line not beginning with ``SG `` is not a
    telemetry candidate and returns ``(None, None)``.
    """
    if not isinstance(line, str) or not line.startswith("SG "):
        return None, None
    # Find the candidate separator without copying an adversarially long
    # non-telemetry line.  Human-readable ``SG ...`` chatter has no such
    # separator and is ignored before the bounded telemetry path.
    colon = line.find(":", 3)
    # ``SG ...`` is also used by human-readable server diagnostics (for
    # example ``SG source snap rejected ...``), which have no bot-colon
    # separator and are not telemetry candidates.
    if colon < 0:
        return None, None
    if colon == 3 or colon + 1 >= len(line) or line[colon + 1] != " ":
        return _telemetry_bad(line_number, "invalid-schema",
                              "missing bot separator")
    if colon - 3 > TOKEN_MAX:
        return _telemetry_bad(line_number, "invalid-bot",
                              "invalid SG bot token")
    payload_start = colon + 2
    first_end = line.find(" ", payload_start)
    # The game also emits human-readable ``SG itemcomm: ...`` messages.  They
    # share the prefix but are not telemetry records; only a key/value-looking
    # payload is a telemetry candidate.  This keeps production chatter out of
    # the anomaly stream while malformed numeric telemetry still reaches the
    # bounded schema below.
    first_token_end = len(line) if first_end < 0 else first_end
    if line.find("=", payload_start, first_token_end) < 0:
        return None, None
    if len(line) > SG_LINE_MAX:
        return _telemetry_bad(line_number, "line-too-long",
                              "SG line exceeds bounded length")
    if line != line.strip() or "\t" in line:
        return _telemetry_bad(line_number, "invalid-framing",
                              "invalid SG record framing")
    body = line[3:]
    bot = body[:colon - 3]
    if not _is_sg_bot(bot):
        return _telemetry_bad(line_number, "invalid-bot",
                              "invalid SG bot token")
    payload = line[payload_start:]
    if not payload:
        return _telemetry_bad(line_number, "invalid-schema",
                              "empty SG telemetry payload")
    tokens = payload.split(" ")
    if not tokens or any(not token for token in tokens):
        return _telemetry_bad(line_number, "invalid-spacing",
                              "invalid SG record spacing")
    index = 0
    values = {}
    seen_keys = set()

    def claim_key(key):
        """Track every key-shaped token, including ignored metadata."""
        if key in seen_keys:
            return False
        seen_keys.add(key)
        return True

    duplicate_key = False

    def consume_int(name):
        nonlocal duplicate_key, index
        if index >= len(tokens):
            return False
        field = _split_kv(tokens[index])
        index += 1
        if field is None:
            return False
        if field[0] in seen_keys:
            duplicate_key = True
            return False
        seen_keys.add(field[0])
        if field[0] != name:
            return False
        parsed = _parse_int32(field[1])
        if parsed is None:
            return False
        values[name] = parsed
        return True

    # This is the fixed prefix emitted by sg_move.c.  The order is part of the
    # telemetry schema, just as key order is part of the hook writer schema.
    for name in ("role", "seed", "goal", "sgoal", "spd"):
        if not consume_int(name):
            return _telemetry_bad(
                line_number,
                "duplicate-field" if duplicate_key else "invalid-schema",
                "duplicate SG telemetry field" if duplicate_key else
                "invalid SG %s field" % name)
    if values["spd"] < 0:
        return _telemetry_bad(line_number, "invalid-range",
                              "SG speed must be nonnegative")

    if index >= len(tokens):
        return _telemetry_bad(line_number, "invalid-schema",
                              "missing SG org field")
    org_field = _split_kv(tokens[index])
    index += 1
    if org_field is None:
        return _telemetry_bad(line_number, "invalid-schema",
                              "invalid SG org field")
    if org_field[0] in seen_keys:
        return _telemetry_bad(line_number, "duplicate-field",
                              "duplicate SG telemetry field")
    seen_keys.add(org_field[0])
    if org_field[0] != "org":
        return _telemetry_bad(line_number, "invalid-schema",
                              "invalid SG org field")
    org_value = org_field[1]
    if not org_value.startswith("(") or org_value.count("(") != 1:
        return _telemetry_bad(line_number, "invalid-org",
                              "invalid SG coordinate framing")
    if index + 2 >= len(tokens):
        return _telemetry_bad(line_number, "invalid-org",
                              "incomplete SG coordinates")
    x_value = org_value[1:]
    y_value = tokens[index]
    z_value = tokens[index + 1]
    index += 2
    if not z_value.endswith(")") or z_value.count(")") != 1:
        return _telemetry_bad(line_number, "invalid-org",
                              "invalid SG coordinate framing")
    z_value = z_value[:-1]
    coordinates = tuple(_parse_int32(item) for item in
                        (x_value, y_value, z_value))
    if any(item is None for item in coordinates):
        return _telemetry_bad(line_number, "invalid-range",
                              "SG coordinates must be bounded int32 values")

    for name in ("link", "act", "hp"):
        if not consume_int(name):
            return _telemetry_bad(
                line_number,
                "duplicate-field" if duplicate_key else "invalid-schema",
                "duplicate SG telemetry field" if duplicate_key else
                "invalid SG %s field" % name)

    optional = {}
    gnd = None
    while index < len(tokens):
        token = tokens[index]
        index += 1
        field = _split_kv(token)
        if field is None:
            # The fixture and a few historical SG emitters put an opaque
            # marker between hp and gnd.  Validate only its bounded token
            # shape; it is not telemetry authority and is never converted.
            if not _is_token(token):
                return _telemetry_bad(line_number, "invalid-schema",
                                      "invalid SG optional token")
            continue
        key, value = field
        if not claim_key(key):
            return _telemetry_bad(line_number, "duplicate-field",
                                  "duplicate SG telemetry field")
        if key == "gnd":
            gnd = _parse_bool(value)
            if gnd is None:
                return _telemetry_bad(line_number, "invalid-range",
                                      "SG gnd must be 0 or 1")
            continue
        if key in ("dh", "dl"):
            parsed = _parse_int32(value)
            if parsed is None:
                return _telemetry_bad(line_number, "invalid-range",
                                      "SG %s must be int32" % key)
            optional[key] = parsed
            continue
        if key == "st":
            parsed = _parse_decimal(value)
            if parsed is None:
                return _telemetry_bad(line_number, "invalid-range",
                                      "SG st must be bounded decimal")
            optional[key] = parsed
            continue
        if key == "eng":
            parsed = _parse_bool(value)
            if parsed is None:
                return _telemetry_bad(line_number, "invalid-range",
                                      "SG eng must be 0 or 1")
            optional[key] = parsed
            continue
        # Unknown key/value metadata is accepted only as bounded text.  It is
        # intentionally not parsed as a number by this authority.
        if not _is_token(value):
            return _telemetry_bad(line_number, "invalid-schema",
                                  "invalid SG optional value")

    if gnd is None:
        return _telemetry_bad(line_number, "invalid-schema",
                              "missing SG gnd field")
    return SGTelemetry(bot=bot, role=values["role"], seed=values["seed"],
                       goal=values["goal"], sgoal=values["sgoal"],
                       spd=values["spd"], org=coordinates,
                       link=values["link"], act=values["act"],
                       hp=values["hp"], gnd=gnd, line=line_number,
                       dh=optional.get("dh"), dl=optional.get("dl"),
                       st=optional.get("st"), eng=optional.get("eng")), None


# An explicit alias makes the shared authority easy to discover for callers
# that refer to telemetry rather than SG's wire prefix.
parse_telemetry_line = parse_sg_line
parse_sg = parse_sg_line


def _legacy_line(line):
    """Recognize the old presentation-only HOOKFIRE/HOOKEND forms."""
    if line.startswith("HOOKFIRE "):
        marker = " at ("
        head, separator, coordinates = line.partition(marker)
        return (bool(separator) and head.startswith("HOOKFIRE ") and
                _is_sg_bot(head[len("HOOKFIRE "):]) and
                coordinates.endswith(")") and coordinates.count("(") == 0)
    if line.startswith("HOOKEND "):
        fields = line.split(" ")
        return len(fields) == 3 and bool(fields[1]) and bool(fields[2])
    return False


def _candidate_keys(line):
    """Return every recoverable ownership candidate in a malformed record.

    Repeated id/bot fields are themselves malformed, so their positional
    relationship cannot be trusted.  Pair every syntactically valid id with
    every syntactically valid bot and taint all of them.  If either side is
    absent or invalid throughout, ownership cannot be safely recovered and
    the complete stream is unusable.
    """
    ids = []
    bots = []
    for token in line.split(" "):
        if token.count("=") == 1:
            key, value = token.split("=", 1)
            if key == "id" and _parse_id(value) and value not in ids:
                ids.append(value)
            elif key == "bot" and _is_token(value) and value not in bots:
                bots.append(value)
    return tuple((event_id, bot) for event_id in ids for bot in bots)


def _malformed(line_number, line, message, scan_candidates=True):
    keys = _candidate_keys(line) if scan_candidates else ()
    return ProtocolAnomaly(line_number, "malformed", message,
                           keys[0] if len(keys) == 1 else None, keys,
                           not keys)


def parse_event_line(line, line_number):
    """Return ``(event, anomaly)`` for one line, or ``(None, None)``.

    A recognized malformed writer record is always an anomaly.  Legacy forms
    are the sole ignored HOOKFIRE/HOOKEND forms.
    """
    prefix = None
    if isinstance(line, str) and line.startswith("HOOKFIRE"):
        prefix, expected = "HOOKFIRE", FIRE_KEYS
    elif isinstance(line, str) and line.startswith("HOOKEND"):
        prefix, expected = "HOOKEND", END_KEYS
    else:
        return None, None
    # Reject before split()/candidate scanning for adversarially long tokens.
    if len(line) > HOOK_LINE_MAX:
        return None, _malformed(line_number, line,
                                "record exceeds bounded length", False)
    if _legacy_line(line):
        return None, None
    if (not line.startswith(prefix + " ") or line != line.strip() or
            "\t" in line):
        return None, _malformed(line_number, line, "invalid record framing")
    tokens = line.split(" ")
    if any(not token for token in tokens[1:]):
        return None, _malformed(line_number, line, "invalid record spacing")
    fields = []
    for token in tokens[1:]:
        if token.count("=") != 1:
            return None, _malformed(line_number, line,
                                    "bare or invalid key/value token")
        key, value = token.split("=", 1)
        if not key or not value or not _is_token(key):
            return None, _malformed(line_number, line,
                                    "empty or invalid key/value")
        fields.append((key, value))
    keys = tuple(key for key, _ in fields)
    if len(set(keys)) != len(keys):
        return None, _malformed(line_number, line, "duplicate key")
    if keys != expected:
        return None, _malformed(line_number, line, "wrong key set or order")
    values = dict(fields)
    if not _parse_id(values["id"]):
        return None, _malformed(line_number, line, "invalid id")
    if not _is_token(values["bot"]) or not _is_token(values["map"]):
        return None, _malformed(line_number, line,
                                "invalid bot or map token")
    if values["kind"] not in ("graph", "speed"):
        return None, _malformed(line_number, line, "invalid kind")
    link, role = _parse_int32(values["link"]), _parse_int32(values["role"])
    anchor = _parse_anchor(values["anchor_q8"])
    if link is None or role is None:
        return None, _malformed(line_number, line, "invalid signed integer")
    if anchor is None:
        return None, _malformed(line_number, line, "invalid anchor_q8")
    if prefix == "HOOKEND":
        if values["reason"] not in TERMINAL_REASONS:
            return None, _malformed(line_number, line,
                                    "unknown terminal reason")
        if not _is_token(values["detail"]):
            return None, _malformed(line_number, line,
                                    "invalid detail token")
    return HookEvent(prefix, values["id"], values["bot"], values["kind"],
                     link, role, values["map"], anchor, line_number,
                     values.get("reason"), values.get("detail")), None


# Auxiliary records are intentionally parsed here, beside the two
# authoritative families above.  The numeric widths are capped in the
# expressions before any conversion, so an ignored/hostile line cannot make
# the observer path do unbounded work.
_AUX_BITE_RE = re.compile(
    r"^HOOKBITE ([^ ]{1,%d}) off=(-?[0-9]{1,11}) into=([^ ]{1,%d}) "
    r"org=\((-?[0-9]{1,11}) (-?[0-9]{1,11}) (-?[0-9]{1,11})\) "
    r"want=\((-?[0-9]{1,11}) (-?[0-9]{1,11}) (-?[0-9]{1,11})\) "
    r"got=\((-?[0-9]{1,11}) (-?[0-9]{1,11}) (-?[0-9]{1,11})\)" %
    (TOKEN_MAX, TOKEN_MAX))


def _marker_token(value):
    """Validate one opaque, bounded marker token without interpreting it."""
    return (isinstance(value, str) and 0 < len(value) <= TOKEN_MAX and
            all(0x21 <= ord(char) <= 0x7e for char in value))


def _marker_vector(values):
    parsed = tuple(_parse_int32(value) for value in values)
    return parsed if all(value is not None for value in parsed) else None


def parse_aux_line(line, line_number=0):
    """Return a validated :class:`AuxMarker`, or ``None``.

    Auxiliary diagnostics are optional producer breadcrumbs.  Unlike the
    HOOKFIRE/HOOKEND protocol, a malformed auxiliary line is ignored (the
    historical consumer did the same); recognized protocol and SG anomalies
    remain the only fatal/tainting anomaly families.
    """
    if not isinstance(line, str):
        return None
    # Auxiliary markers share the hook writer's fixed line buffer.  Reject
    # before split/regex work so direct callers are bounded too, independently
    # of scan_file's bounded physical reader.
    if len(line) > HOOK_LINE_MAX:
        return None
    if line.startswith("HOOKABORT "):
        fields = line.split(" ", 3)
        if len(fields) < 3 or any(not field for field in fields[:3]):
            return None
        bot, reason = fields[1], fields[2]
        if _marker_token(bot) and _marker_token(reason):
            return AuxMarker("HOOKABORT", bot, line_number, reason=reason)
        return None
    if line.startswith("HOOKBITE "):
        match = _AUX_BITE_RE.match(line)
        if match is None:
            return None
        values = match.groups()
        bot, offset_text, into = values[:3]
        offset = _parse_int32(offset_text)
        org = _marker_vector(values[3:6])
        want = _marker_vector(values[6:9])
        got = _marker_vector(values[9:12])
        if (not _marker_token(bot) or not _marker_token(into) or
                offset is None or offset < 0 or org is None or
                want is None or got is None):
            return None
        return AuxMarker("HOOKBITE", bot, line_number, offset=offset,
                         into=into, org=org, want=want, got=got)
    if line.startswith("HOOKSKYHOLD "):
        fields = line.split(" ", 2)
        if len(fields) >= 2 and _marker_token(fields[1]):
            return AuxMarker("HOOKSKYHOLD", fields[1], line_number)
        return None
    if line.startswith("BOTDEATH: "):
        # The rest of a BOTDEATH record is human-readable and intentionally
        # opaque.  Only its bounded owner token is diagnostic authority.
        fields = line.split(" ", 2)
        if len(fields) >= 2 and _marker_token(fields[1]):
            return AuxMarker("BOTDEATH", fields[1], line_number)
    return None


def _dispatch(observer, kind, item):
    """Deliver one typed scan item to a callable or observer object.

    A plain callable receives the item itself, which keeps ``scan_file`` handy
    for small callers (``items.append`` is enough).  Structured observers can
    use ``on_event``, ``on_telemetry``, ``on_aux``, ``on_anomaly``, and
    ``on_eof``; the aliases make the public dispatch vocabulary explicit while
    retaining a tiny compatibility surface for marker-oriented consumers.
    """
    if observer is None:
        return
    if callable(observer):
        observer(item)
        return
    names = {
        "event": ("on_event", "on_hook_event"),
        "telemetry": ("on_telemetry", "on_sg"),
        "aux": ("on_aux", "on_marker"),
        "anomaly": ("on_anomaly",),
        "eof": ("on_eof",),
    }.get(kind, ())
    for name in names:
        callback = getattr(observer, name, None)
        if callback is not None:
            callback(item)
            return
    callback = getattr(observer, "dispatch", None)
    if callback is not None:
        callback(item)


def _scan_lines(lines, observer=None, observe_records=False):
    """Parse and pair one iterable, dispatching each validated scan item."""
    open_fires, completed, pair_by_key, tainted = {}, set(), {}, set()
    anomalies = []
    telemetry_anomalies = []
    global_fatal = False
    recognized_events = 0
    candidate_keys_seen = 0
    anomaly_count = 0
    last_line = 0

    def aggregate_failure(line_number, dimension, limit):
        """Stop with a bounded, visible anomaly without growing the list."""
        nonlocal global_fatal
        global_fatal = True
        control = ProtocolAnomaly(
            line_number, "aggregate-limit",
            "%s ceiling exceeded (%d)" % (dimension, limit),
            global_fatal=True)
        # Keep the anomaly list itself bounded.  If it is already full, the
        # final ordinary entry is replaced by the controlled fatal marker.
        if len(anomalies) < MAX_ANOMALIES:
            anomalies.append(control)
        elif anomalies:
            anomalies[-1] = control
        _dispatch(observer, "anomaly", control)

    def record_anomaly(anomaly):
        """Append one ordinary anomaly or stop at the anomaly ceiling."""
        nonlocal anomaly_count
        if anomaly_count >= MAX_ANOMALIES:
            aggregate_failure(anomaly.line, "anomalies", MAX_ANOMALIES)
            return False
        anomalies.append(anomaly)
        anomaly_count += 1
        _dispatch(observer, "anomaly", anomaly)
        return True

    def record_telemetry_anomaly(anomaly):
        """Count SG anomalies against the same bounded anomaly budget."""
        nonlocal anomaly_count
        if anomaly_count >= MAX_ANOMALIES:
            aggregate_failure(anomaly.line, "anomalies", MAX_ANOMALIES)
            return False
        telemetry_anomalies.append(anomaly)
        anomaly_count += 1
        _dispatch(observer, "anomaly", anomaly)
        return True

    for line_number, raw in enumerate(lines, 1):
        last_line = line_number
        if line_number > MAX_INPUT_LINES:
            aggregate_failure(line_number, "input lines", MAX_INPUT_LINES)
            break
        line = raw.rstrip("\n")
        event, anomaly = parse_event_line(line, line_number)
        if event is not None:
            recognized_events += 1
            if recognized_events > MAX_RECOGNIZED_EVENTS:
                aggregate_failure(line_number, "recognized events",
                                  MAX_RECOGNIZED_EVENTS)
                break
        if anomaly:
            # Candidate ownership records are the recoverable cross-product
            # extracted from malformed writer lines.  A normal duplicate or
            # orphan anomaly names one already-owned key and is counted only
            # against the anomaly ceiling.
            if anomaly.code == "malformed":
                candidate_keys_seen += len(anomaly.keys)
                if candidate_keys_seen > MAX_CANDIDATE_KEYS:
                    aggregate_failure(line_number, "candidate ownership keys",
                                      MAX_CANDIDATE_KEYS)
                    break
            if not record_anomaly(anomaly):
                break
            global_fatal = global_fatal or anomaly.global_fatal
            for key in anomaly.keys:
                tainted.add(key)
                open_fires.pop(key, None)
                pair_by_key.pop(key, None)
            continue
        if event is not None:
            _dispatch(observer, "event", event)
            key = event.key
            if event.event == "HOOKFIRE":
                if key in tainted or key in open_fires or key in completed:
                    if not record_anomaly(ProtocolAnomaly(
                            line_number, "duplicate-fire",
                            "duplicate FIRE for key", key, (key,))):
                        break
                    tainted.add(key)
                    open_fires.pop(key, None)
                    pair_by_key.pop(key, None)
                    continue
                open_fires[key] = event
                if len(open_fires) > MAX_INCOMPLETES:
                    aggregate_failure(line_number, "incomplete events",
                                      MAX_INCOMPLETES)
                    break
                continue
            if key in tainted:
                if not record_anomaly(ProtocolAnomaly(
                        line_number, "tainted-end",
                        "END for protocol-invalid key", key, (key,))):
                    break
                continue
            fire = open_fires.get(key)
            if fire is None:
                code = "duplicate-end" if key in completed else "orphan-end"
                if not record_anomaly(ProtocolAnomaly(
                        line_number, code, "END without open FIRE", key,
                        (key,))):
                    break
                tainted.add(key)
                pair_by_key.pop(key, None)
                continue
            immutable = ("kind", "link", "role", "map", "anchor_q8")
            if any(getattr(fire, field) != getattr(event, field)
                   for field in immutable):
                if not record_anomaly(ProtocolAnomaly(
                        line_number, "immutable-mismatch",
                        "END immutable fields differ from FIRE", key,
                        (key,))):
                    break
                tainted.add(key)
                open_fires.pop(key, None)
                continue
            if len(pair_by_key) >= MAX_PAIRS:
                aggregate_failure(line_number, "pairs", MAX_PAIRS)
                break
            open_fires.pop(key)
            completed.add(key)
            pair_by_key[key] = HookPair(fire, event)
            continue

        if observe_records:
            sample, telemetry_anomaly = parse_sg_line(line, line_number)
            if telemetry_anomaly is not None:
                if not record_telemetry_anomaly(telemetry_anomaly):
                    break
            elif sample is not None:
                _dispatch(observer, "telemetry", sample)
            marker = parse_aux_line(line, line_number)
            if marker is not None:
                _dispatch(observer, "aux", marker)

    if global_fatal:
        pairs, incomplete = (), ()
    else:
        incomplete = tuple(event for key, event in open_fires.items()
                           if key not in tainted)
        if len(incomplete) > MAX_INCOMPLETES:
            aggregate_failure(last_line, "incomplete events",
                              MAX_INCOMPLETES)
            pairs, incomplete = (), ()
        else:
            pairs = tuple(pair for key, pair in pair_by_key.items()
                          if key not in tainted)
    if observe_records:
        _dispatch(observer, "eof", EOFMarker(last_line))
    return PairingResult(pairs, incomplete, tuple(anomalies), global_fatal,
                         tuple(sorted(tainted)), tuple(telemetry_anomalies))


def _bounded_file_lines(stream):
    """Yield one bounded prefix per physical line while draining overflow.

    ``TextIOWrapper`` iteration performs an unbounded physical ``readline``.
    A newline-free concatenation can therefore consume memory proportional to
    the entire file before any parser length check runs.  This reader asks for
    at most ``PHYSICAL_LINE_MAX + 1`` characters at a time, drains the rest of
    an oversized physical line in equally bounded chunks, and yields it once.
    Recognized HOOK/SG prefixes then reach their existing line-too-long policy;
    unrelated oversized noise remains an ignored single input line.
    """
    read_limit = PHYSICAL_LINE_MAX + 1

    while True:
        prefix = stream.readline(read_limit)
        if prefix == "":
            return
        if prefix.endswith("\n") or len(prefix) <= PHYSICAL_LINE_MAX:
            yield prefix
            continue

        # The first bounded prefix is sufficient for family recognition and
        # length rejection.  Drain without concatenation so memory remains
        # independent of the physical line length.
        while True:
            suffix = stream.readline(read_limit)
            if suffix == "" or suffix.endswith("\n"):
                break
        yield prefix


def scan_file(path, observer=None):
    """Read, parse, dispatch, and pair one log in a single physical pass.

    ``observer`` receives validated :class:`HookEvent`, :class:`SGTelemetry`,
    :class:`AuxMarker`, protocol/telemetry anomaly, and EOF marker objects as
    they occur.  The returned :class:`PairingResult` is the sole authority for
    which event objects may be used for analytics; an observer must therefore
    finalize only its ``pairs`` and ``incomplete`` members.
    """
    with open(path, "r", errors="replace") as stream:
        return _scan_lines(_bounded_file_lines(stream), observer,
                           observe_records=True)


def pair_lines(lines):
    """Compatibility wrapper for callers that already have event lines.

    This retains the pre-scan_file in-memory contract: only HOOKFIRE/HOOKEND
    pairing is performed, while callers that need telemetry/aux dispatch use
    the physical-file ``scan_file`` authority.
    """
    return _scan_lines(lines)


def pair_file(path):
    """Compatibility wrapper around the one-pass file scanner."""
    return scan_file(path)
