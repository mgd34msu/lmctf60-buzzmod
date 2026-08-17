#!/usr/bin/env python3
"""Bind seed-indexed demo corpora to the ordered seed array used for mining.

Seed numbers are not durable map coordinates.  Any generator change can
insert, remove, or reorder seeds while leaving the map name unchanged, so a
corpus without this identity must never be baked against a new seed array.
Link-only proof changes do not invalidate observations, so this deliberately
uses a seed-payload CRC rather than the sidecar's full-graph CRC.
"""

import json
import os
import re
import tempfile
import zlib

try:
    import rune_contracts_generated as contract
except ModuleNotFoundError:  # also support `python -m tools.corpusgraph`
    from tools import rune_contracts_generated as contract
try:
    import runeio
except ModuleNotFoundError:  # also support `python -m tools.corpusgraph`
    from tools import runeio


MAX_CORPUS_COUNT = (1 << 63) - 1
MAX_CORPUS_BYTES = 256 * 1024 * 1024
TRANSITION_KEY = re.compile(r'(0|[1-9][0-9]*)>(0|[1-9][0-9]*)\Z')
SEED_KEY = re.compile(r'(0|[1-9][0-9]*)\Z')
MAP_NAME = re.compile(r'[A-Za-z0-9_][A-Za-z0-9_-]{0,62}\Z')
RSF_TOMBSTONE = runeio.RSF_TOMBSTONE


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
    with open(path, encoding='utf-8') as stream:
        if os.fstat(stream.fileno()).st_size > MAX_CORPUS_BYTES:
            raise ValueError(f'{path}: corpus exceeds size limit')
        return json.load(stream, object_pairs_hook=_unique_object,
                         parse_constant=_reject_constant)


def require_safe_mapname(mapname):
    if not isinstance(mapname, str) or not MAP_NAME.fullmatch(mapname):
        raise ValueError(f'unsafe or invalid map name {mapname!r}')



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
def _artifact_mapping(path, expected_map, data):
    decoded = runeio.decode(data)
    if expected_map is not None and decoded.header.map_name != expected_map:
        raise runeio.RuneWireError(
            contract.RLW_MAPNAME_MISMATCH,
            f'header={decoded.header.map_name!r}, expected={expected_map!r}',
        )
    seed_end = decoded.header.num_seeds * runeio.SEED_STRUCT.size
    return {
        'map': decoded.header.map_name,
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
        'physics': {
            'flags': decoded.header.physics_flags,
            'gravity': decoded.header.gravity,
            'airaccelerate': decoded.header.airaccelerate,
            'maxvelocity': decoded.header.maxvelocity,
            'pmove_substep_ms': decoded.header.pmove_substep_ms,
            'server_frame_ms': decoded.header.server_frame_ms,
            'host_physics_id': decoded.header.host_physics_id,
        },
        'mechanism_contract_crc32': decoded.header.mechanism_contract_crc32,
        'num_activation_nodes': decoded.header.num_activation_nodes,
        'num_activation_edges': decoded.header.num_activation_edges,
        'num_inventory_edges': decoded.header.num_inventory_edges,
        'num_activation_plans': decoded.header.num_activation_plans,
        'seeds': decoded.seeds,
        'links': decoded.links,
        'activation_nodes': decoded.activation_nodes,
        'activation_edges': decoded.activation_edges,
        'activation_plans': decoded.activation_plans,
        'strings': decoded.strings,
    }


def read_rune(path, expected_map=None):
    """Read one bounded RUNE artifact."""
    with open(path, 'rb') as stream:
        data = stream.read(runeio.MAX_RUNE_FILE_BYTES + 1)
    if len(data) > runeio.MAX_RUNE_FILE_BYTES:
        raise runeio.RuneWireError(contract.RLW_BAD_FILE_SIZE, str(len(data)))
    return _artifact_mapping(path, expected_map, data)


def rune_identity_from_rune(rune):
    """Return the corpus stamp for one decoded RUNE snapshot."""
    return {
        'map': rune['map'],
        'rune_num_seeds': rune['num_seeds'],
        'rune_seed_crc32': rune['seed_crc32'],
        'rune_payload_crc32': rune['payload_crc32'],
        'rune_bsp_checksum': rune['bsp_checksum'],
        'rune_entity_crc32': rune['entity_crc32'],
        'rune_action_contract_crc32': rune['action_contract_crc32'],
        'rune_physics': dict(rune['physics']),
    }


def rune_identity(path, expected_map=None):
    return rune_identity_from_rune(read_rune(path, expected_map))


def rune_seed_origins(rune):
    return tuple(tuple(seed.origin) for seed in rune['seeds'])


def rune_link_pairs(rune):
    return tuple((link.source, link.destination) for link in rune['links'])


def rune_tombstone_indices(rune):
    return tuple(index for index, seed in enumerate(rune['seeds'])
                 if seed.flags & RSF_TOMBSTONE)


def rune_live_seed_indices(rune):
    tombstones = frozenset(rune_tombstone_indices(rune))
    return tuple(index for index in range(rune['num_seeds'])
                 if index not in tombstones)


def require_current_rune_binding(path, expected_map, expected_binding):
    try:
        import sidecario
    except ModuleNotFoundError:
        from tools import sidecario
    try:
        actual_binding = sidecario.binding_from_rune(read_rune(path, expected_map))
    except (OSError, ValueError, KeyError) as exc:
        raise sidecario.SidecarError(
            sidecario.SCD_STATE_DRIFT,
            f'{path}: target RUNE changed before sidecar commit',
        ) from exc
    if actual_binding != expected_binding:
        raise sidecario.SidecarError(
            sidecario.SCD_STATE_DRIFT,
            f'{path}: target RUNE binding changed before sidecar commit',
        )


_WIRE_CORPUS_IDENTITY_KEYS = (
    'rune_num_seeds', 'rune_seed_crc32', 'rune_payload_crc32',
    'rune_bsp_checksum', 'rune_entity_crc32',
    'rune_action_contract_crc32', 'rune_physics',
)


def require_corpus_identity(document, path, identity):
    if not isinstance(document, dict):
        raise ValueError(f'{path}: top level must be an object')
    if document.get('map') != identity['map']:
        raise ValueError(f'{path}: map identity does not match target RUNE')
    for key in _WIRE_CORPUS_IDENTITY_KEYS:
        if document.get(key) != identity[key]:
            raise ValueError(f'{path}: {key} does not match target RUNE; re-mine')


def stamp_corpus_identity(document, identity):
    for key in _WIRE_CORPUS_IDENTITY_KEYS:
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
