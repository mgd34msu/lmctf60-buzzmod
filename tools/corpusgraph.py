#!/usr/bin/env python3
"""Bind seed-indexed demo corpora to the ordered seed array used for mining.

Seed numbers are not durable map coordinates.  Any generator change can
insert, remove, or reorder seeds while leaving the map name unchanged, so a
corpus without this identity must never be baked against a new seed array.
Link-only proof changes do not invalidate observations, so this deliberately
uses a seed-payload CRC rather than the sidecar's full-graph CRC.
"""

import json
import math
import os
import re
import struct
import tempfile
import zlib

try:
    import rune_contracts_generated as contract
    from rune_contracts_generated import (
        RL_ADJUSTED, RL_DECLARED, RL_DOOR, RL_DROP, RL_HOOK, RL_JUMP,
        RL_LIFT, RL_PROVEN, RL_ROCKETJUMP, RL_RUN, RL_SWIM, RL_TELEPORT,
    )
except ModuleNotFoundError:  # also support `python -m tools.corpusgraph`
    from tools import rune_contracts_generated as contract
    from tools.rune_contracts_generated import (
        RL_ADJUSTED, RL_DECLARED, RL_DOOR, RL_DROP, RL_HOOK, RL_JUMP,
        RL_LIFT, RL_PROVEN, RL_ROCKETJUMP, RL_RUN, RL_SWIM, RL_TELEPORT,
    )
try:
    import runeio
except ModuleNotFoundError:  # also support `python -m tools.corpusgraph`
    from tools import runeio


HEADER_FMT = '<4i64s'
SEED_FMT = '<3f2h'
LINK_FMT = '<2i6Bh3f'
RUNE_MAGIC = 0x454E5552
RUNE_MAX_SEEDS = 32768
RUNE_MAX_LINKS = 262144
RUNE_HOOK_MAX_RAY = 8192.0
RUNE_TELEPORT_SEED_REACH = 128.0
RUNE_DECLARED_CONTROL_MARKER = 254
RUNE_DROP_CONTROL_MARKER = 254
RUNE_HOOK_CONTROL_SLACK = 24
RUNE_WATER_HOOK_CONTROL_MARKER = 253
PMOVE_COORD_MIN = -4096.0
PMOVE_COORD_MAX = 4095.875
HEADER_SIZE = struct.calcsize(HEADER_FMT)
SEED_SIZE = struct.calcsize(SEED_FMT)
LINK_SIZE = struct.calcsize(LINK_FMT)
MAX_CORPUS_COUNT = (1 << 63) - 1
MAX_CORPUS_BYTES = 256 * 1024 * 1024
TRANSITION_KEY = re.compile(r'(0|[1-9][0-9]*)>(0|[1-9][0-9]*)\Z')
SEED_KEY = re.compile(r'(0|[1-9][0-9]*)\Z')
MAP_NAME = re.compile(r'[A-Za-z0-9_][A-Za-z0-9_-]{0,62}\Z')

RSF_WATER = 1
RSF_TOMBSTONE = 2

# These are frozen legacy wire boundaries, not registry-size aliases. V3 has
# a different layout and remains unreadable here until its decoder lands. The
# 28-byte V1/V2 link has no mode field, so its only implicit mode is NONE.
_LEGACY_ACTION_MAX = {1: RL_ROCKETJUMP, 2: RL_DOOR}
_LEGACY_PROVENANCE_MAX = {1: RL_DECLARED, 2: RL_DECLARED}
READABLE_RUNE_VERSIONS = tuple(_LEGACY_ACTION_MAX)

assert HEADER_SIZE == 80, HEADER_SIZE
assert SEED_SIZE == 16, SEED_SIZE
assert LINK_SIZE == 28, LINK_SIZE


def _unique_object(pairs):
    document = {}
    for key, value in pairs:
        if key in document:
            raise ValueError(f'duplicate JSON key {key!r}')
        document[key] = value
    return document


def _reject_constant(value):
    raise ValueError(f'non-finite JSON value {value}')


def load_corpus(path):
    """Load strict JSON: duplicate keys and NaN/Infinity are malformed."""
    with open(path, encoding='utf-8') as stream:
        size = os.fstat(stream.fileno()).st_size
        if size > MAX_CORPUS_BYTES:
            raise ValueError(f'{path}: corpus is {size} bytes; limit is '
                             f'{MAX_CORPUS_BYTES}')
        try:
            return json.load(stream, object_pairs_hook=_unique_object,
                             parse_constant=_reject_constant)
        except (UnicodeError, ValueError) as error:
            raise ValueError(f'{path}: malformed JSON: {error}') from error


def require_safe_mapname(mapname):
    if not isinstance(mapname, str) or not MAP_NAME.fullmatch(mapname):
        raise ValueError(f'unsafe or invalid map name {mapname!r}')


def _signed_short(value):
    value &= 0xffff
    return value - 0x10000 if value & 0x8000 else value


def _short_to_angle(value):
    """q_shared.h SHORT2ANGLE after the loader's explicit short cast."""
    return _signed_short(value) * (360.0 / 65536.0)


def _canonical_short_angle(value):
    """Whether a control survives the C loader's ANGLE2SHORT round trip."""
    if not math.isfinite(value) or value < -180.0 or value >= 180.0:
        return False
    encoded = int(value * 65536.0 / 360.0) & 0xffff
    return value == _short_to_angle(encoded)


def _validate_rune_records(path, data, version, num_seeds, num_links):
    """Enforce Rune_Load's base record and complete v2 action contract."""
    seeds = []
    offset = HEADER_SIZE
    for index in range(num_seeds):
        seed = struct.unpack_from(SEED_FMT, data, offset)
        offset += SEED_SIZE
        x, y, z, area_hint, flags = seed
        if (not all(math.isfinite(value) for value in (x, y, z)) or
                not all(PMOVE_COORD_MIN <= value <= PMOVE_COORD_MAX
                        for value in (x, y, z)) or
                not 0 <= area_hint <= 255 or
                flags & ~(RSF_WATER | RSF_TOMBSTONE)):
            raise ValueError(
                f'{path}: seed {index} violates runtime geometry or flags')
        seeds.append(seed)

    linked_sources = set()
    for index in range(num_links):
        link = struct.unpack_from(LINK_FMT, data, offset)
        offset += LINK_SIZE
        (source, destination, action, provenance, min_speed, heading,
         heading_slack, exit_speed, cost_ms, ax, ay, az) = link
        anchor = (ax, ay, az)
        if (not 0 <= source < num_seeds or
                not 0 <= destination < num_seeds or source == destination or
                action > _LEGACY_ACTION_MAX[version] or
                provenance > _LEGACY_PROVENANCE_MAX[version] or
                cost_ms <= 0 or
                not all(math.isfinite(value) for value in anchor)):
            raise ValueError(f'{path}: link {index} violates runtime record contract')

        # V1 is retained only for legacy mining/migration. Its flat records
        # still receive the common safety checks above, while the semantics
        # below are the complete contract of the current v2 C loader.
        if version != 2:
            continue

        # A tombstone owns geometry outside the closed objective core and may
        # never participate in a route. Conversely every live seed must own an
        # outgoing link so runtime localization can distinguish the two states.
        if ((seeds[source][4] | seeds[destination][4]) & RSF_TOMBSTONE):
            raise ValueError(
                f'{path}: link {index} touches route-core tombstone')
        linked_sources.add(source)

        from_water = bool(seeds[source][4] & RSF_WATER)
        to_water = bool(seeds[destination][4] & RSF_WATER)
        anchor_zero = all(value == 0.0 for value in anchor)
        anchor_world = all(PMOVE_COORD_MIN <= value <= PMOVE_COORD_MAX
                           for value in anchor)
        dx = ax - seeds[source][0]
        dy = ay - seeds[source][1]
        dz = az - seeds[source][2]
        door_to_dx = seeds[destination][0] - ax
        door_to_dy = seeds[destination][1] - ay
        door_to_dz = seeds[destination][2] - az

        if ((action == RL_RUN and not anchor_zero and not anchor_world) or
                (action == RL_JUMP and not anchor_zero) or
                (action in (RL_LIFT, RL_TELEPORT, RL_DOOR) and
                 (not anchor_world or provenance != RL_DECLARED or
                  min_speed != 0 or heading != 0 or
                  heading_slack != RUNE_DECLARED_CONTROL_MARKER or
                  exit_speed != 0)) or
                (action == RL_TELEPORT and
                 (math.hypot(dx, dy) > RUNE_TELEPORT_SEED_REACH or
                  abs(dz) > RUNE_TELEPORT_SEED_REACH)) or
                (action == RL_DOOR and
                 (math.hypot(dx, dy) > 320.0 or abs(dz) > 48.0 or
                  math.hypot(door_to_dx, door_to_dy) > 768.0 or
                  abs(door_to_dz) > 96.0))):
            raise ValueError(
                f'{path}: link {index} has invalid action anchor/control')
        if (action == RL_DOOR and
                any(value != int(value * 8.0) * 0.125 for value in anchor)):
            raise ValueError(
                f'{path}: link {index} has noncanonical door wait point')

        # Resolving the serialized anchor to one unique repeatable trigger and
        # checking the door sweep require live map entities. Offline readers
        # can still enforce the complete graph-record portion of that contract.
        if action in (RL_RUN, RL_JUMP, RL_DOOR) and (from_water or to_water):
            raise ValueError(
                f'{path}: link {index} uses dry action on water endpoint')
        if action == RL_ROCKETJUMP and (from_water or to_water):
            raise ValueError(
                f'{path}: link {index} uses dry special from water')
        if (action == RL_SWIM and
                (not (from_water or to_water) or min_speed != 0 or
                 heading != 0 or heading_slack != 0 or
                 provenance not in (RL_PROVEN, RL_ADJUSTED) or not anchor_zero)):
            raise ValueError(f'{path}: link {index} has invalid swim control')
        if action == RL_ROCKETJUMP:
            raise ValueError(
                f'{path}: link {index} uses unsupported v2 rocket jump')
        if (action == RL_HOOK and
                (provenance != RL_PROVEN or min_speed != 0 or
                 (from_water and to_water) or
                 heading_slack != (RUNE_WATER_HOOK_CONTROL_MARKER
                                   if from_water else RUNE_HOOK_CONTROL_SLACK) or
                 not _canonical_short_angle(ax) or
                 not _canonical_short_angle(ay) or
                 ax < -89.0 or ax > 89.0 or
                 az < 1.0 or az > RUNE_HOOK_MAX_RAY)):
            raise ValueError(f'{path}: link {index} has invalid hook control')
        if action == RL_DROP:
            lip_horiz = math.hypot(dx, dy)
            lip_yaw = math.degrees(math.atan2(dy, dx))
            stored_yaw = heading * (360.0 / 256.0)
            yaw_delta = (lip_yaw - stored_yaw + 180.0) % 360.0 - 180.0
            if (from_water or min_speed != 0 or
                    heading_slack != RUNE_DROP_CONTROL_MARKER or
                    not 2.0 <= lip_horiz <= 256.0 or
                    abs(dz - 8.0) > 0.25 or
                    abs(yaw_delta) > 360.0 / 256.0):
                raise ValueError(f'{path}: link {index} has invalid drop control')
        if action == RL_JUMP and min_speed != 0:
            raise ValueError(
                f'{path}: link {index} has unsupported momentum jump')

    if version == 2:
        for index, seed in enumerate(seeds):
            tombstone = bool(seed[4] & RSF_TOMBSTONE)
            if tombstone == (index in linked_sources):
                raise ValueError(
                    f'{path}: seed {index} violates route-core ownership')


def _read_rune_v3(path, expected_map, stream, prefix, file_size):
    if file_size > runeio.MAX_V3_FILE_BYTES:
        raise runeio.RuneWireError(
            contract.RLW_BAD_FILE_SIZE,
            f'{file_size} bytes exceeds {runeio.MAX_V3_FILE_BYTES}',
        )
    data = prefix + stream.read(runeio.MAX_V3_FILE_BYTES + 1 - len(prefix))
    decoded = runeio.decode_v3(data)
    mapname = decoded.header.map_name
    if expected_map is not None and mapname != expected_map:
        raise runeio.RuneWireError(
            contract.RLW_MAPNAME_MISMATCH,
            f'header={mapname!r}, expected={expected_map!r}',
        )
    seed_end = decoded.header.num_seeds * runeio.SEED_STRUCT.size
    physics = {
        'flags': decoded.header.physics_flags,
        'gravity': decoded.header.gravity,
        'airaccelerate': decoded.header.airaccelerate,
        'maxvelocity': decoded.header.maxvelocity,
        'pmove_substep_ms': decoded.header.pmove_substep_ms,
        'server_frame_ms': decoded.header.server_frame_ms,
        'host_physics_id': decoded.header.host_physics_id,
    }
    return {
        'map': mapname,
        'version': decoded.header.version,
        'num_seeds': decoded.header.num_seeds,
        'num_links': decoded.header.num_links,
        'data': data,
        'graph_crc32': decoded.header.payload_crc32,
        'seed_crc32': zlib.crc32(decoded.payload[:seed_end]) & 0xffffffff,
        'header_bytes': decoded.header.header_bytes,
        'seed_bytes': decoded.header.seed_bytes,
        'link_bytes': decoded.header.link_bytes,
        'payload_crc32': decoded.header.payload_crc32,
        'header_crc32': decoded.header.header_crc32,
        'bsp_checksum': decoded.header.bsp_checksum,
        'entity_crc32': decoded.header.entity_crc32,
        'action_contract_crc32': decoded.header.action_contract_crc32,
        'physics': physics,
        'seeds': decoded.seeds,
        'links': decoded.links,
    }


def read_rune(path, expected_map=None, versions=(1, 2)):
    """Read one bounded, exact-size rune and return its decoded metadata."""
    with open(path, 'rb') as stream:
        # Twelve bytes include all three fixed v3 size fields.  That is enough
        # to retain v3 routing when header_bytes alone is corrupt without
        # confusing a legacy int32-version-3 forensic file with wire v3.
        prefix = stream.read(12)
        file_size = os.fstat(stream.fileno()).st_size
        if runeio.looks_like_v3_prefix(prefix):
            if 3 not in versions:
                raise ValueError(f'{path}: unsupported rune version 3')
            return _read_rune_v3(
                path, expected_map, stream, prefix, file_size)
        header = prefix + stream.read(HEADER_SIZE - len(prefix))
        if len(header) < HEADER_SIZE:
            raise ValueError(
                f'{path}: shorter than {HEADER_SIZE}-byte rune header')
        magic, version, num_seeds, num_links, raw_map = struct.unpack_from(
            HEADER_FMT, header, 0)
        if magic != RUNE_MAGIC:
            raise ValueError(f'{path}: bad rune magic 0x{magic:08x}')
        if (version not in READABLE_RUNE_VERSIONS or
                version not in versions):
            raise ValueError(f'{path}: unsupported rune version {version}')
        if not 0 < num_seeds <= RUNE_MAX_SEEDS:
            raise ValueError(f'{path}: invalid seed count {num_seeds}')
        if not 0 <= num_links <= RUNE_MAX_LINKS:
            raise ValueError(f'{path}: invalid link count {num_links}')
        if b'\0' not in raw_map:
            raise ValueError(f'{path}: unterminated map identity')
        mapname = raw_map.split(b'\0', 1)[0].decode('ascii', 'strict')
        if (expected_map is not None and
                mapname.casefold() != expected_map.casefold()):
            raise ValueError(f'{path}: rune map {mapname!r} does not match '
                             f'{expected_map!r}')
        expected_size = (HEADER_SIZE + num_seeds * SEED_SIZE +
                         num_links * LINK_SIZE)
        actual_size = os.fstat(stream.fileno()).st_size
        if actual_size != expected_size:
            raise ValueError(f'{path}: size {actual_size} does not match '
                             f'header size {expected_size}')
        payload = stream.read(expected_size - HEADER_SIZE)
        if len(payload) != expected_size - HEADER_SIZE:
            raise ValueError(f'{path}: short payload read')
    data = header + payload
    _validate_rune_records(path, data, version, num_seeds, num_links)
    seed_end = HEADER_SIZE + num_seeds * SEED_SIZE
    return {
        'map': mapname,
        'version': version,
        'num_seeds': num_seeds,
        'num_links': num_links,
        'data': data,
        'graph_crc32': zlib.crc32(data[HEADER_SIZE:]) & 0xffffffff,
        'seed_crc32': zlib.crc32(data[HEADER_SIZE:seed_end]) & 0xffffffff,
    }


def rune_identity_from_rune(rune):
    """Return the corpus stamp for one already-decoded rune snapshot."""
    identity = {
        'map': rune['map'],
        'rune_num_seeds': rune['num_seeds'],
        'rune_seed_crc32': rune['seed_crc32'],
    }
    if rune['version'] == 3:
        identity.update({
            'rune_version': 3,
            'rune_payload_crc32': rune['payload_crc32'],
            'rune_bsp_checksum': rune['bsp_checksum'],
            'rune_entity_crc32': rune['entity_crc32'],
            'rune_action_contract_crc32': rune['action_contract_crc32'],
            'rune_physics': dict(rune['physics']),
        })
    return identity


def rune_identity(path, expected_map=None):
    rune = read_rune(path, expected_map, versions=(1, 2, 3))
    return rune_identity_from_rune(rune)


def rune_seed_origins(rune):
    """Return ordered seed origins without assuming a native wire layout."""
    if rune['version'] == 3:
        return tuple(tuple(seed.origin) for seed in rune['seeds'])
    data = rune['data']
    return tuple(
        struct.unpack_from(SEED_FMT, data, HEADER_SIZE + index * SEED_SIZE)[:3]
        for index in range(rune['num_seeds'])
    )


def rune_link_pairs(rune):
    """Return ordered (source, destination) pairs for every encoded link."""
    if rune['version'] == 3:
        return tuple((link.source, link.destination) for link in rune['links'])
    offset = HEADER_SIZE + rune['num_seeds'] * SEED_SIZE
    return tuple(
        struct.unpack_from(LINK_FMT, rune['data'],
                           offset + index * LINK_SIZE)[:2]
        for index in range(rune['num_links'])
    )


def rune_tombstone_indices(rune):
    """Return encoded seed indices that are intentionally not route owners."""
    if rune['version'] == 3:
        return tuple(
            index for index, seed in enumerate(rune['seeds'])
            if seed.flags & RSF_TOMBSTONE
        )
    data = rune['data']
    return tuple(
        index for index in range(rune['num_seeds'])
        if struct.unpack_from(
            SEED_FMT, data, HEADER_SIZE + index * SEED_SIZE
        )[4] & RSF_TOMBSTONE
    )


def rune_live_seed_indices(rune):
    """Return stable original indices while excluding route tombstones."""
    tombstones = frozenset(rune_tombstone_indices(rune))
    return tuple(
        index for index in range(rune['num_seeds'])
        if index not in tombstones
    )


def require_current_rune_binding(path, expected_map, expected_binding):
    """Fail a sidecar precommit if its decoded rune snapshot went stale."""
    try:
        import sidecario
    except ModuleNotFoundError:
        from tools import sidecario
    try:
        current = read_rune(path, expected_map, versions=(3,))
        actual_binding = sidecario.binding_from_rune(current)
    except (OSError, ValueError, KeyError) as exc:
        raise sidecario.SidecarError(
            sidecario.SCD_STATE_DRIFT,
            f'{path}: target rune is no longer the decoded v3 snapshot',
        ) from exc
    if actual_binding != expected_binding:
        raise sidecario.SidecarError(
            sidecario.SCD_STATE_DRIFT,
            f'{path}: target rune binding changed before sidecar commit',
        )


_V3_CORPUS_IDENTITY_KEYS = (
    'rune_version',
    'rune_num_seeds',
    'rune_seed_crc32',
    'rune_bsp_checksum',
    'rune_entity_crc32',
    'rune_physics',
)


def require_corpus_identity(document, path, identity):
    if not isinstance(document, dict):
        raise ValueError(f'{path}: top level must be an object')
    corpus_map = document.get('map')
    exact_map = identity.get('rune_version') == 3
    map_matches = (corpus_map == identity['map'] if exact_map else
                   isinstance(corpus_map, str) and
                   corpus_map.casefold() == identity['map'].casefold())
    if not isinstance(corpus_map, str) or not map_matches:
        raise ValueError(f'{path}: map identity {corpus_map!r} does not match '
                         f"{identity['map']!r}")
    keys = (_V3_CORPUS_IDENTITY_KEYS if exact_map else
            ('rune_num_seeds', 'rune_seed_crc32'))
    for key in keys:
        value = document.get(key)
        expected = identity[key]
        if key == 'rune_physics':
            valid = isinstance(value, dict)
        else:
            valid = not isinstance(value, bool) and isinstance(value, int)
        if not valid:
            raise ValueError(f'{path}: missing or invalid {key}; re-mine this '
                             'corpus against the target rune')
        if value != expected:
            if key == 'rune_seed_crc32':
                got = f'0x{value:08x}'
                wanted = f"0x{expected:08x}"
            else:
                got, wanted = value, expected
            raise ValueError(f'{path}: {key} {got} does not match target '
                             f'{wanted}; re-mine instead of reindexing')


def stamp_corpus_identity(document, identity):
    keys = (_V3_CORPUS_IDENTITY_KEYS
            if identity.get('rune_version') == 3 else
            ('rune_num_seeds', 'rune_seed_crc32'))
    for key in keys:
        document[key] = identity[key]
    return document


def validate_transition_counts(document, path, num_seeds):
    """Validate and return a seed-indexed transition-count object."""
    transitions = document.get('transitions')
    if not isinstance(transitions, dict):
        raise ValueError(f'{path}: transitions must be an object')
    for key, count in transitions.items():
        match = TRANSITION_KEY.fullmatch(key) if isinstance(key, str) else None
        if not match:
            raise ValueError(f'{path}: malformed transition key {key!r}')
        source, destination = (int(value) for value in match.groups())
        if source >= num_seeds or destination >= num_seeds:
            raise ValueError(f'{path}: transition {key!r} is outside '
                             f'0..{num_seeds - 1}')
        if (isinstance(count, bool) or not isinstance(count, int) or
                count < 0 or count > MAX_CORPUS_COUNT):
            raise ValueError(f'{path}: transition {key!r} has invalid '
                             f'count {count!r}')
    return transitions


def validate_seed_weights(weights, path, label, num_seeds):
    """Validate a seed-keyed non-negative numeric plane."""
    if not isinstance(weights, dict):
        raise ValueError(f'{path}: {label} must be an object')
    normalized = {}
    for key, weight in weights.items():
        match = SEED_KEY.fullmatch(key) if isinstance(key, str) else None
        if not match:
            raise ValueError(f'{path}: {label} has malformed seed {key!r}')
        seed = int(match.group(1))
        if seed >= num_seeds:
            raise ValueError(f'{path}: {label} seed {seed} is outside '
                             f'0..{num_seeds - 1}')
        if (isinstance(weight, bool) or
                not isinstance(weight, (int, float)) or
                not math.isfinite(weight) or weight < 0):
            raise ValueError(f'{path}: {label} seed {seed} has invalid '
                             f'weight {weight!r}')
        normalized[seed] = weight
    return normalized


def _atomic_write(path, payload, precommit=None):
    """Durably replace path from a same-directory temporary file."""
    directory = os.path.dirname(os.path.abspath(path))
    if not os.path.isdir(directory):
        raise FileNotFoundError(f'{directory}: output directory does not exist')
    mode = 0o644
    try:
        mode = os.stat(path).st_mode & 0o777
    except FileNotFoundError:
        pass

    fd, temporary = tempfile.mkstemp(
        prefix=f'.{os.path.basename(path)}.', suffix='.tmp', dir=directory)
    try:
        try:
            os.fchmod(fd, mode)
        except AttributeError:
            pass
        with os.fdopen(fd, 'wb') as stream:
            fd = -1
            written = stream.write(payload)
            if written != len(payload):
                raise OSError(f'{temporary}: short write '
                              f'({written}/{len(payload)})')
            stream.flush()
            os.fsync(stream.fileno())
        if precommit is not None:
            if not callable(precommit):
                raise TypeError('precommit must be callable')
            precommit()
        os.replace(temporary, path)
        temporary = None
        try:
            dir_fd = os.open(directory, os.O_RDONLY | os.O_DIRECTORY)
        except (AttributeError, OSError):
            dir_fd = None
        if dir_fd is not None:
            try:
                try:
                    os.fsync(dir_fd)
                except OSError:
                    # Some otherwise atomic filesystems do not permit
                    # directory fsync. The file was already flushed and
                    # replaced; do not report a post-commit false failure.
                    pass
            finally:
                os.close(dir_fd)
    finally:
        if fd >= 0:
            os.close(fd)
        if temporary is not None:
            try:
                os.unlink(temporary)
            except FileNotFoundError:
                pass


def atomic_write_bytes(path, payload, *, precommit=None):
    if not isinstance(payload, bytes):
        raise TypeError('atomic_write_bytes payload must be bytes')
    _atomic_write(path, payload, precommit)


def atomic_write_json(path, document):
    payload = json.dumps(document, allow_nan=False,
                         separators=(',', ':')).encode('utf-8')
    _atomic_write(path, payload)
