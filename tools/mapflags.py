#!/usr/bin/env python3
"""mapflags.py -- inspect info_flag_red / info_flag_blue origins in map assets.

This is an offline inspection aid, not deployment authority: engine entity
override rules and collision settling can change the final spawned flag
positions.  Rune generation prints those authoritative post-spawn root seed
indices and runegen passes them directly to runelint's deployment gate.
Asset lookup follows the attested Yamagi search path and CRC-qualified ENT
override preference, then reports approximate origins/seeds.

Usage: mapflags.py [--out PATH] <gamedir> [<map> ...]
       (no maps = every loose rune found; stdout-only unless --out is given)
"""
import argparse
import binascii
import glob
import json
import math
import os
import re
import struct
import sys
import zipfile
from functools import lru_cache

from corpusgraph import (atomic_write_json, read_rune, require_safe_mapname,
                         rune_link_pairs,
                         rune_live_seed_indices, rune_seed_origins)

MAX_PAK_DIRECTORY = 64 * 1024 * 1024
MAX_GAME_FILE_BYTES = 512 * 1024 * 1024
BSP_HEADER_SIZE = 8 + 19 * 8
BSP_LUMP_COUNT = 19
YQ2_MAX_MAP_ENTSTRING = 0x40000
MAX_ENTITY_BYTES = YQ2_MAX_MAP_ENTSTRING - 1
MAX_ENTITY_PADDING_OVERRUN = 64
MAX_NUMBERED_PAKS = 100
PACK_SUFFIXES = ('pak', 'pk2', 'pk3', 'pkz', 'zip')


@lru_cache(maxsize=64)
def pak_index(pakpath):
    """{lowercase name: (offset, length)} for one Quake2 .pak"""
    out = {}
    with open(pakpath, 'rb') as f:
        head = f.read(12)
        if len(head) != 12 or head[:4] != b'PACK':
            raise ValueError(f'{pakpath}: malformed PAK header')
        dirofs, dirlen = struct.unpack_from('<ii', head, 4)
        f.seek(0, os.SEEK_END)
        file_size = f.tell()
        if (dirofs < 12 or dirlen < 0 or dirlen % 64 or
                dirofs > file_size or dirlen > file_size - dirofs):
            raise ValueError(f'{pakpath}: malformed PAK directory')
        if dirlen > MAX_PAK_DIRECTORY:
            raise ValueError(f'{pakpath}: PAK directory is unreasonably large')
        f.seek(dirofs)
        d = f.read(dirlen)
        for i in range(dirlen // 64):
            name, pos, ln = struct.unpack_from('<56sii', d, i * 64)
            name = name.split(b'\0')[0].decode('latin-1').lower()
            if pos < 0 or ln < 0 or pos > file_size or ln > file_size - pos:
                raise ValueError(f'{pakpath}: malformed entry {name!r}')
            if name in out:
                raise ValueError(
                    f'{pakpath}: case-insensitive duplicate entry {name!r}')
            out[name] = (pos, ln)
    return out


@lru_cache(maxsize=64)
def zip_index(packpath):
    """Case-insensitive YQ2-style index for one ZIP-family package."""

    out = {}
    try:
        with zipfile.ZipFile(packpath) as archive:
            for info in archive.infolist():
                if info.is_dir():
                    continue
                name = info.filename.lower()
                if name in out:
                    raise ValueError(
                        f'{packpath}: case-insensitive duplicate entry {name!r}')
                out[name] = (info.filename, info.file_size)
    except zipfile.BadZipFile as exc:
        raise ValueError(f'{packpath}: malformed ZIP package') from exc
    return out


def _pack_paths(gamedir):
    """Return package paths grouped in YQ2's deterministic type order."""

    grouped = {suffix: [] for suffix in PACK_SUFFIXES}
    try:
        names = os.listdir(gamedir)
    except FileNotFoundError:
        return grouped
    for name in names:
        path = os.path.join(gamedir, name)
        if not os.path.isfile(path) or '.' not in name:
            continue
        suffix = name.rsplit('.', 1)[1]
        if suffix in grouped:
            grouped[suffix].append(path)
    return grouped


def _numbered_pack(path, suffix):
    match = re.fullmatch(r'pak(0|[1-9][0-9]*)\.' + re.escape(suffix),
                         os.path.basename(path))
    if not match:
        return None
    number = int(match.group(1))
    return number if number < MAX_NUMBERED_PAKS else None


def _package_hit(path, relpath):
    suffix = path.rsplit('.', 1)[1]
    if suffix == 'pak':
        hit = pak_index(path).get(relpath.lower())
        return ('pak', hit) if hit is not None else None
    hit = zip_index(path).get(relpath.lower())
    return ('zip', hit) if hit is not None else None


def _read_package_hit(path, hit):
    kind, location = hit
    if kind == 'pak':
        pos, length = location
        if length > MAX_GAME_FILE_BYTES:
            raise ValueError(f'{path}: packed map asset is unreasonably large')
        with open(path, 'rb') as stream:
            stream.seek(pos)
            data = stream.read(length)
        if len(data) != length:
            raise ValueError(f'{path}: truncated packed map asset')
        return data

    member, length = location
    if length > MAX_GAME_FILE_BYTES:
        raise ValueError(f'{path}: packed map asset is unreasonably large')
    try:
        with zipfile.ZipFile(path) as archive:
            data = archive.read(member)
    except (KeyError, zipfile.BadZipFile, RuntimeError) as exc:
        raise ValueError(f'{path}: cannot read packed map asset {member!r}') from exc
    if len(data) != length:
        raise ValueError(f'{path}: packed map asset length changed while reading')
    return data


def read_game_file_with_source(gamedir, relpath):
    """Resolve one YQ2 VFS file and return ``(bytes, provenance)``.

    Within one game directory Yamagi adds the loose directory first, then
    prepends numbered packages in numeric/type order, then prepends every
    non-numbered package.  Consequently every package wins over a loose file,
    non-numbered packages win over numbered packages, later package types win,
    and a higher numbered package wins within a type.  Enumeration order among
    same-type non-numbered packages is filesystem-dependent in YQ2; when two
    such packages contain the requested path, this offline authority fails
    closed instead of inventing an order.
    """

    relpath = relpath.replace('\\', '/').lstrip('/')
    grouped = _pack_paths(gamedir)
    numbered = []
    unnumbered = []
    for type_rank, suffix in enumerate(PACK_SUFFIXES):
        for path in grouped[suffix]:
            hit = _package_hit(path, relpath)
            if hit is None:
                continue
            number = _numbered_pack(path, suffix)
            row = (type_rank, path, hit)
            if number is None:
                unnumbered.append(row)
            else:
                numbered.append((type_rank, number, path, hit))

    if unnumbered:
        winning_type = max(row[0] for row in unnumbered)
        finalists = [row for row in unnumbered if row[0] == winning_type]
        if len(finalists) != 1:
            names = ', '.join(sorted(os.path.basename(row[1])
                                     for row in finalists))
            raise ValueError(
                f'{gamedir}: YQ2 non-numbered package order is ambiguous for '
                f'{relpath!r}: {names}')
        _, path, hit = finalists[0]
        return (_read_package_hit(path, hit),
                f'{os.path.basename(path)}!/{relpath.lower()}')

    if numbered:
        _, _, path, hit = max(numbered, key=lambda row: (row[0], row[1]))
        return (_read_package_hit(path, hit),
                f'{os.path.basename(path)}!/{relpath.lower()}')

    loose_candidates = (os.path.join(gamedir, relpath),
                        os.path.join(gamedir, relpath.lower()))
    for loose in dict.fromkeys(loose_candidates):
        if not os.path.isfile(loose):
            continue
        with open(loose, 'rb') as stream:
            size = os.fstat(stream.fileno()).st_size
            if size > MAX_GAME_FILE_BYTES:
                raise ValueError(f'{loose}: map asset is unreasonably large')
            return stream.read(), relpath
    return None, None


def read_game_file(gamedir, relpath):
    """Read one file through the attested Yamagi search-path law."""

    return read_game_file_with_source(gamedir, relpath)[0]


def game_package_names(gamedir):
    """Return all case-folded names present in supported game packages."""

    names = set()
    for suffix, paths in _pack_paths(gamedir).items():
        for path in paths:
            names.update(pak_index(path) if suffix == 'pak' else zip_index(path))
    return names


def bsp_lumps(data, source='<BSP>'):
    """Validate the common IBSP v38 header and return all lump pairs."""

    if data is None:
        raise FileNotFoundError(source)
    if len(data) < BSP_HEADER_SIZE or data[:4] != b'IBSP':
        raise ValueError(f'{source}: malformed BSP header')
    version = struct.unpack_from('<i', data, 4)[0]
    if version != 38:
        raise ValueError(f'{source}: unsupported BSP version {version}')
    return [struct.unpack_from('<ii', data, 8 + index * 8)
            for index in range(BSP_LUMP_COUNT)]


def bsp_entity_lump(data, source='<BSP>'):
    """Return the strictly in-bounds raw entity lump."""

    ofs, ln = bsp_lumps(data, source)[0]
    if (ofs < BSP_HEADER_SIZE or ln < 0 or ln > MAX_ENTITY_BYTES or
            ofs > len(data) or ln > len(data) - ofs):
        raise ValueError(f'{source}: entity lump is outside the file')
    return data[ofs:ofs + ln]


def bsp_entities_with_provenance(
        data, source='<BSP>', *, allow_truncated_zero_padding=False,
        recovery_validator=None):
    """Return entity text plus explicit bounded-recovery provenance.

    Recovery is limited to a final entity lump whose declared end overruns the
    file by at most :data:`MAX_ENTITY_PADDING_OVERRUN`.  It is never accepted
    without a caller-supplied complete entity-stream validator.
    """

    lumps = bsp_lumps(data, source)
    ofs, ln = lumps[0]
    normal = (ofs >= BSP_HEADER_SIZE and ln >= 0 and
              ln <= MAX_ENTITY_BYTES and ofs <= len(data) and
              ln <= len(data) - ofs)
    if normal:
        raw = data[ofs:ofs + ln]
        return raw.split(b'\0', 1)[0].decode('latin-1'), None

    if not allow_truncated_zero_padding:
        raise ValueError(f'{source}: entity lump is outside the file')
    if (ofs < BSP_HEADER_SIZE or ofs > len(data) or ln < 0 or
            ln > MAX_ENTITY_BYTES):
        raise ValueError(f'{source}: entity lump is outside the file')
    declared_end = ofs + ln
    overrun = declared_end - len(data)
    if overrun <= 0 or overrun > MAX_ENTITY_PADDING_OVERRUN:
        raise ValueError(f'{source}: entity lump is outside the file')

    for index, (other_ofs, other_len) in enumerate(lumps[1:], 1):
        if other_len < 0:
            raise ValueError(f'{source}: BSP lump {index} is outside the file')
        if other_len == 0:
            continue
        if (other_ofs < BSP_HEADER_SIZE or other_ofs > len(data) or
                other_len > len(data) - other_ofs):
            raise ValueError(f'{source}: BSP lump {index} is outside the file')

    available = data[ofs:]
    nul = available.find(b'\0')
    if nul < 0:
        raise ValueError(
            f'{source}: truncated entity lump has no in-file terminator')
    if any(available[nul + 1:]):
        raise ValueError(
            f'{source}: truncated entity lump has nonzero bytes after terminator')
    if recovery_validator is None:
        raise ValueError(
            f'{source}: truncated entity lump recovery requires full parsing')

    text = available[:nul].decode('latin-1')
    try:
        recovery_validator(text)
    except (TypeError, ValueError) as exc:
        raise ValueError(
            f'{source}: truncated entity lump fails complete entity parsing') from exc
    recovery = {
        'kind': 'truncated_zero_padding',
        'source': source,
        'entity_offset': ofs,
        'declared_length': ln,
        'available_length': len(available),
        'declared_end': declared_end,
        'file_size': len(data),
        'overrun_bytes': overrun,
        'terminator_offset': ofs + nul,
        'zero_padding_bytes': len(available) - nul - 1,
        'all_other_nonempty_lumps_in_bounds': True,
        'complete_entity_parse': True,
    }
    return text, recovery


def bsp_entities(data, source='<BSP>'):
    """Return strict entity text; historical recovery is opt-in elsewhere."""

    return bsp_entities_with_provenance(data, source)[0]


def entity_lump_crc16(data, source='<BSP>'):
    """Return YQ2's CRC_Block value used by ``maps/name@crc.ent``."""

    raw = bsp_entity_lump(data, source)
    if not raw:
        return 0
    return binascii.crc_hqx(raw[:-1], 0xffff)


def parse_ents(text):
    """list of dicts, one per { } block"""
    out = []
    for block in re.findall(r'\{(.*?)\}', text, re.S):
        d = {}
        for k, v in re.findall(r'"([^"]*)"\s+"([^"]*)"', block):
            d[k] = v
        out.append(d)
    return out


def entity_text_with_provenance(
        gamedir, mapname, *, allow_truncated_zero_padding=False,
        recovery_validator=None):
    """Return ``(text, source, recovery)`` using YQ2's ENT lookup law."""

    require_safe_mapname(mapname)
    bsp_rel = f'maps/{mapname}.bsp'
    bsp_data, bsp_source = read_game_file_with_source(gamedir, bsp_rel)
    if bsp_data is None:
        raise FileNotFoundError(
            f'{gamedir}: no {bsp_rel} in the Yamagi search path')

    recovery = None
    try:
        crc = entity_lump_crc16(bsp_data, bsp_source)
    except ValueError:
        if not allow_truncated_zero_padding:
            raise
        text, recovery = bsp_entities_with_provenance(
            bsp_data, bsp_source,
            allow_truncated_zero_padding=True,
            recovery_validator=recovery_validator)
        # The malformed declared length makes YQ2's CRC_Block read envelope
        # unavailable to an offline authority.  A qualified override could
        # therefore change the live entity source unpredictably; fail closed.
        prefix = f'maps/{mapname.lower()}@'
        qualified = sorted(name for name in game_package_names(gamedir)
                           if name.startswith(prefix) and name.endswith('.ent'))
        maps_dir = os.path.join(gamedir, 'maps')
        if os.path.isdir(maps_dir):
            qualified.extend(
                f'maps/{name.lower()}' for name in os.listdir(maps_dir)
                if name.lower().startswith(f'{mapname.lower()}@') and
                name.lower().endswith('.ent'))
        if qualified:
            raise ValueError(
                f'{bsp_source}: cannot resolve CRC-qualified ENT override for '
                f'truncated entity lump: {sorted(set(qualified))!r}')
    else:
        qualified_rel = f'maps/{mapname}@{crc:04x}.ent'
        ent_data, ent_source = read_game_file_with_source(gamedir, qualified_rel)
        if ent_data is not None:
            # CMod_LoadEntityString accepts only lengths 2..262142.  An
            # existing but invalid qualified override suppresses the plain
            # fallback and the engine uses the BSP entity lump instead.
            if 1 < len(ent_data) < YQ2_MAX_MAP_ENTSTRING - 1:
                return (ent_data.split(b'\0', 1)[0].decode('latin-1'),
                        ent_source, None)
            return bsp_entities(bsp_data, bsp_source), bsp_source, None
        text = bsp_entities(bsp_data, bsp_source)

    ent_rel = f'maps/{mapname}.ent'
    ent_data, ent_source = read_game_file_with_source(gamedir, ent_rel)
    if ent_data is not None:
        if 1 < len(ent_data) < YQ2_MAX_MAP_ENTSTRING - 1:
            return (ent_data.split(b'\0', 1)[0].decode('latin-1'),
                    ent_source, None)
    return text, bsp_source, recovery


def entity_text(gamedir, mapname):
    """Return strict ``(entity_text, source)`` under active YQ2 precedence."""

    text, source, _ = entity_text_with_provenance(gamedir, mapname)
    return text, source


@lru_cache(maxsize=64)
def flag_origins_with_source(gamedir, mapname):
    text, source = entity_text(gamedir, mapname)
    ents = parse_ents(text)
    res = {}
    for e in ents:
        cn = e.get('classname', '')
        if cn in ('info_flag_red', 'info_flag_blue', 'item_flag_team1',
                  'item_flag_team2'):
            key = 'red' if cn in ('info_flag_red', 'item_flag_team1') else 'blue'
            raw_origin = e.get('origin')
            if raw_origin is None:
                raise ValueError(f'{source}: {cn} has no origin')
            parts = raw_origin.split()
            if len(parts) != 3:
                raise ValueError(f'{source}: {cn} origin must have 3 values')
            try:
                o = [float(x) for x in parts]
            except ValueError as exc:
                raise ValueError(f'{source}: {cn} has malformed origin') from exc
            if not all(math.isfinite(value) for value in o):
                raise ValueError(f'{source}: {cn} has non-finite origin')
            if key in res:
                raise ValueError(f'{source}: multiple {key} flag entities')
            res[key] = o
    return res, source


@lru_cache(maxsize=64)
def flag_origins(gamedir, mapname):
    return flag_origins_with_source(gamedir, mapname)[0]


def read_graph_metadata(path, expected_map=None):
    rune = read_rune(path, expected_map)
    seeds = rune_seed_origins(rune)
    live = frozenset(rune_live_seed_indices(rune))
    linked = frozenset(source for source, _ in rune_link_pairs(rune))
    return rune, seeds, live & linked


@lru_cache(maxsize=64)
def load_graph_metadata(path, expected_map=None):
    return read_graph_metadata(path, expected_map)


@lru_cache(maxsize=64)
def load_graph(path):
    _, seeds, eligible = load_graph_metadata(path)
    return seeds, eligible


@lru_cache(maxsize=64)
def load_seeds(path):
    return load_graph(path)[0]


def nearest(seeds, p, linked=None):
    """Nearest runtime-eligible seed using Rune_NearestSeed's distance.

    The in-game lookup additionally rejects a seed when its chest-height
    world trace is blocked. Offline callers do not have the engine tracer,
    but matching its linked/z filters and weighted metric avoids the old
    plain-Euclidean disagreement on stacked geometry.
    """
    best, bd = -1, 1e18
    for i, s in enumerate(seeds):
        dz = s[2] - p[2]
        if dz > 96.0 or dz < -96.0:
            continue
        dx = s[0] - p[0]
        dy = s[1] - p[1]
        if dx * dx + dy * dy > 128.0 * 128.0:
            continue
        d = dx * dx + dy * dy + dz**2 * 0.25
        if d < bd:
            bd, best = d, i
    # Runtime first chooses the nearest geometry owner. If that exact owner is
    # a tombstone/unlinked seed, fail closed instead of substituting a farther
    # seed that the observed body may be unable to reach across a boundary.
    if best >= 0 and linked is not None and best not in linked:
        return -1, float('inf')
    return best, bd ** 0.5 if best >= 0 else float('inf')


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='Resolve CTF flags and their nearest rune seeds.')
    parser.add_argument(
        '--out', '-o', metavar='PATH',
        help='atomically write the aggregate JSON to PATH (default: no file)')
    parser.add_argument('gamedir')
    parser.add_argument('maps', nargs='*', metavar='MAP')
    args = parser.parse_args(sys.argv[1:] if argv is None else argv)

    gamedir = args.gamedir
    maps = args.maps
    if not maps:
        maps = sorted(os.path.basename(p)[:-5]
                      for p in glob.glob(os.path.join(gamedir, 'maps', '*.rune')))
    if not maps:
        print(f'mapflags: no loose rune files found under {gamedir}/maps',
              file=sys.stderr)
        return 1
    out = {}
    for m in maps:
        require_safe_mapname(m)
        fo = flag_origins(gamedir, m)
        rp = os.path.join(gamedir, 'maps', f'{m}.rune')
        rec = {'map': m, 'flags': fo}
        if os.path.exists(rp) and fo:
            _, seeds, linked = load_graph_metadata(rp, m)
            for team in ('red', 'blue'):
                if team in fo:
                    si, dist = nearest(seeds, fo[team], linked)
                    rec[f'{team}_seed'] = si
                    rec[f'{team}_seed_dist'] = round(dist, 1)
        out[m] = rec
        print(m, json.dumps(rec))
    if args.out:
        atomic_write_json(args.out, out)
        print(f'WROTE {args.out}: maps={len(out)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
