#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_dir=$(mktemp -d)
reader_dir=$tmp_dir/reader
trap 'rm -r "$tmp_dir"' EXIT HUP INT TERM

python3 - "$tmp_dir" <<'PY'
import base64
import binascii
from pathlib import Path
import struct
import sys

out = Path(sys.argv[1])
count = 28
header = 720
descriptor = 24
sizes = (252, 80, 56, 20, 4, 12, 44, 24, 20, 8, 20, 4, 20, 4,
         4, 12, 4, 12, 4, 12, 16, 16, 100, 16, 60, 4, 8, 15)
magic = b"SGRCW001"
production_fixture_b85 = b"""\
Q%6!mS1>R!0RYef8~^|S;0pi%00000v>*~T000000RR91Qcgon0RR910000000000`~Uy|0RR9100000&;kGe000000RR91Pyhe`
0ssI200000&;tMf000000ssI2H~;_u0RR9100000a0CDV000000{{R36aWAK0ssI200000s0082000001ONa41ONa40ssI200000
&;$Sg000001poj53;+NC1ONa400000*aQFo000001^@s6EC2ui0RR91000002n7HD000002LJ#77ytkO0ssI200000I0XOz00000
2mk;86aWAK0ssI200000XaxWO000002><{92mk;83;+NC00000kOcq$000003IG5A6aWAK8UO$Q00000@C5(>000003jhEB1ONa4
asU7T00000_y+(0000003;+NC6aWAK2LJ#700000zzF~V000004FCWD1ONa40000000000PznG5000004gdfE1ONa42LJ#700000
PznG5000004*&oF3;+NC0000000000a0&nb000005C8xG1ONa40000000000a0&nb000005dZ)H3;+NC0000000000a0&nb00000
5&!@I1ONa40000000000a0&nb000006951J3;+NC0000000000a0&nb000006aWAK5C8xG0000000000a0&nb000006#xJL5C8xG
0000000000a0&nb00000761SMWB>pF0ssI200000a0&nb000007XSbN5C8xG0RR9100000I12y(000007ytkOJOBUy0ssI200000
NDBY}0000082|tP1ONa40ssI200000zzYBX000008UO$Q2mk;80RR9100000$O`}f000008vp<R4*&oF0RR9100000&<g+n00000
S^xk50000000000000000000000000000000000000aO4000000RaF20s#O30s;U4000000|Nj600000F)#oC00000GB5xD00000
GcW)E00000G%x@F000001Oos7000001p@#8000001_J;9000001Oxy800000Qbj{mL{Cys0RR910{{R31ONa41poj50RR910RR91
AOHXWfdBvhfdBvhK>z>$fB*mhfB*mh00961fdBvhfdBvhK>z>$fB*mhfB*mhfB*mh0078B001CC004kL004kM004SK005Rm0000#
002lt2mk;80RR9100000000000RR910ssI20{{R300000000000000000000KmY&$KmY&$KmY&$000000RR91000000RR9100000
0RR9100000000000{{R3000000ssI20ssI20{{R300000KmY&$0000000000fB*mhKmY&$KmY&$0RR910RR910RR910RR910RR91
0RR9100000000000{{R30RR91000000RR911ONa400000004kL00000000000000%000001ONa4000000ssI2000000000000000
00000000000RR910RR9100000000000RR9100000000000RR91KmY&$0000000000KmY&$KmY&$00000KmY&$KmY&$KmY&$KmY&$
00000KmY&$0{{R30000000000000000000000000000000RR91AOHXW000000{{R30000000000000000{{R30{{R3000000{{R3
0RR91|NsC00{{R30{{R30{{R30{{R300000000000RR91000004FCWD0RR910RR910RR914FCWD4FCWD0RR910RR910ssI20ssI2
0{{R31ONa41ONa42mk;81poj55C8xG1^@s6U;qFB2LJ#7KmY&$2mk;8fB*mh2><{9009613IG5A00IC23jhEB00aO43;+NC00;m8
0000000000000001^@s61ONa4000000RR910RR913IG5A1ONa4000000ssI20ssI24gdfE1ONa4000000{{R30{{R35&!@I1ONa4
000001ONa41ONa4761SM1ONa4000001poj51poj58UO$Q1ONa4000001poj51^@s69smFU1ONa4000001^@s61^@s6A^-pY1ONa4
000002LJ#72LJ#7CIA2c1poj5000002mk;82mk;8D*ylh1poj5000002><{92><{9FaQ7m1ONa4000003IG5A3IG5AGynhq1ONa4
000003jhEB3jhEBH~;_u1ONa40RR910000000000JOBUy1ONa40RR910RR910RR91KmY&$1ONa40RR910ssI20ssI2L;wH)1ONa4
0RR910{{R30{{R3NB{r;1ONa40RR911ONa41ONa4OaK4?1ONa40RR911poj51poj5Pyhe`1ONa40RR911poj51^@s6Q~&?~1ONa4
0RR911^@s61^@s6SO5S31ONa40RR912LJ#72LJ#7TmS$71poj50RR912mk;82mk;8VE_OC1poj50RR912><{92><{9W&i*H1ONa4
0RR913IG5A3IG5AY5)KL1ONa40RR913jhEB3jhEBZU6uP1ONa4000000RR911poj5000000RR911poj50RR910ssI20{{R31ONa4
0RR910ssI20{{R31ONa40RR910ssI20{{R31ONa40RR910ssI20{{R31ONa40RR910ssI20{{R31ONa40RR910ssI20{{R31ONa4
0RR910ssI20{{R31ONa40RR910ssI20{{R31ONa40RR910ssI20{{R31ONa41^@s60RR910ssI20{{R31ONa41^@s60RR910ssI2
0{{R31ONa40RR910ssI20{{R31ONa40RR910ssI20{{R31ONa40RR910ssI20{{R31ONa40RR910ssI20{{R31ONa40RR910ssI2
0{{R31ONa40RR910ssI20{{R31ONa40RR910ssI20{{R31ONa40RR910ssI20{{R31ONa40RR910ssI20{{R31ONa40RR910ssI2
0{{R31ONa40RR910ssI20{{R31ONa41^@s60RR910ssI20{{R31ONa41^@s60RR910ssI20{{R31ONa40RR910ssI20{{R31ONa4
0RR910ssI20{{R31ONa4000000000000000000000000000000000000RR910RR910000000000000000ssI23jhEB0000000000
000000{{R33;+NC0000000000000001ONa45C8xG0000000000000001poj55&!@I0000000000000001^@s66951J0000000000
004kL0000$002Nh004kM005vs006*1007`X000002LJ#72LJ#7000000RR91000005C8xG5C8xG00000AOHXWAOHXWFaQ7m00000
0RR9100000`Tzg`=mP)%00000&<6kj000001ONa4000000ssI2000000RR91000003jhEB3jhEB0000000000000005C8xG5C8xG
00000AOHXWAOHXWFaQ7m0RR910000000000000000000000000000000RR910ssI2000000ssI20000000000000003jhEB2LJ#7
000001ONa43jhEB000000RR910RR917ytkO7ytkO2mk;85C8xG5C8xG00000AOHXWAOHXW5C8xG2LJ#7000006951J0RR910RR91
|NsC0U;qFB7ytkO2mk;8SO5S35C8xG00000XaE2JAOHXW5C8xG0RR910RR91000000RR91000000RRI400000000000000000000
"""

def align(value):
    return (value + 7) & ~7

def p32(data, offset, value):
    struct.pack_into("<I", data, offset, value)

def p64(data, offset, value):
    struct.pack_into("<Q", data, offset, value)

def u64(data, offset):
    return struct.unpack_from("<Q", data, offset)[0]

def checksum(data):
    p32(data, 24, 0)
    p32(data, 24, binascii.crc32(data) & 0xffffffff)

def build(counts):
    cursor = header
    offsets = []
    for record_bytes, records in zip(sizes, counts):
        cursor = align(cursor)
        offsets.append(cursor)
        cursor += record_bytes * records
    image = bytearray(align(cursor))
    image[:8] = magic
    struct.pack_into("<HHI", image, 8, 1, header, count)
    p64(image, 16, len(image))
    struct.pack_into("<H", image, 32, 1)
    p32(image, 36, 0x4d434e52)
    struct.pack_into("<H", image, 40, 1)
    for section, (record_bytes, records, offset) in enumerate(
            zip(sizes, counts, offsets)):
        struct.pack_into("<IIIIQ", image, 48 + section * descriptor,
                         section, record_bytes, records, 0, offset)
    identity = offsets[0]
    image[identity:identity + 32] = bytes(range(1, 33))
    p64(image, identity + 32, 1)
    p32(image, identity + 40, 1)
    p32(image, identity + 44, 1)
    for index in range(11):
        p64(image, identity + 48 + index * 8, index + 1)
    struct.pack_into("<7I", image, identity + 136, 1, 1, 1, 1, 0, 0, 1)
    struct.pack_into("<12i", image, identity + 164,
                     0, 0, 0, 1, 1, 1, 0, 0, 0, 1, 1, 1)
    for index in range(8):
        p32(image, identity + 212 + index * 4, 0x3f800000)
    p32(image, identity + 244, 1)
    p32(image, identity + 248, 1)
    checksum(image)
    return image, offsets

def emit(name, image):
    (out / name).write_bytes(image)

production = base64.b85decode(b"".join(production_fixture_b85.split()))
if (len(production) != 3040
        or struct.unpack_from("<I", production, 24)[0] != 0x361220b4):
    raise SystemExit("invalid embedded production fixture")
emit("production.rune", production)

def mutation(name, image, edit, refresh=True):
    result = bytearray(image)
    edit(result)
    if refresh:
        checksum(result)
    emit(name, result)

minimal_counts = [0] * count
minimal_counts[0] = 1
minimal, minimal_offsets = build(minimal_counts)
emit("minimal.rune", minimal)

weapon_counts = [0] * count
weapon_counts[0] = 1
weapon_counts[1] = 1
weapon_counts[8] = 1
weapon_counts[9] = 1
weapon_counts[10] = 1
weapon, weapon_offsets = build(weapon_counts)
cell = weapon_offsets[1]
for offset in (32, 36, 40):
    p32(weapon, cell + offset, 1)
p32(weapon, cell + 64, 1)
region = weapon_offsets[8]
p32(weapon, region + 16, 1)
profile = weapon_offsets[9]
p32(weapon, profile, 1)
p32(weapon, profile + 4, 3)
kernel = weapon_offsets[10]
p32(weapon, kernel + 8, 1)
checksum(weapon)
emit("valid.rune", weapon)
(out / "valid.identity").write_bytes(weapon[weapon_offsets[0]:weapon_offsets[0] + 252])
bad_identity = bytearray((out / "valid.identity").read_bytes())
bad_identity[128] ^= 1
emit("bad.identity", bad_identity)

source_counts = [0] * count
source_counts[0] = 1
source_counts[2] = 1
source, source_offsets = build(source_counts)
facet = source_offsets[2]
p32(source, facet + 20, 0x3f800000)
p32(source, facet + 52, 0xffffffff)
checksum(source)
emit("source.rune", source)

analytic_counts = [0] * count
analytic_counts[0] = 1
analytic_counts[12] = 1
analytic_counts[14] = 1
analytic, analytic_offsets = build(analytic_counts)
checksum(analytic)
emit("analytic.rune", analytic)

dimension_counts = [0] * count
dimension_counts[0] = 1
dimension_counts[12] = 1
dimension_counts[13] = 1
dimension_counts[14] = 1
dimension, dimension_offsets = build(dimension_counts)
dimension_function = dimension_offsets[12]
dimension_input = dimension_offsets[13]
p32(dimension, dimension_function + 4, 1)
p32(dimension, dimension_input, 15)
checksum(dimension)
emit("dimension.rune", dimension)

ballistic_counts = [0] * count
ballistic_counts[0] = 1
ballistic_counts[12] = 1
ballistic_counts[13] = 1
ballistic_counts[19] = 1
ballistic, ballistic_offsets = build(ballistic_counts)
ballistic_function = ballistic_offsets[12]
p32(ballistic, ballistic_function + 4, 1)
p32(ballistic, ballistic_function + 16, 3)
p32(ballistic, ballistic_offsets[13], 9)
checksum(ballistic)
emit("ballistic.rune", ballistic)

movement_counts = [0] * count
movement_counts[0] = 1
movement_counts[1] = 1
movement_counts[7] = 2
movement, movement_offsets = build(movement_counts)
movement_cell = movement_offsets[1]
p32(movement, movement_cell + 52, 0)
p32(movement, movement_cell + 56, 2)
for index, stances in enumerate((1, 2)):
    field = movement_offsets[7] + index * sizes[7]
    p32(movement, field, 0)
    p32(movement, field + 4, 0xffffffff)
    movement[field + 12] = stances
checksum(movement)
emit("movement-pair.rune", movement)

mechanism_counts = [0] * count
mechanism_counts[0] = 1
mechanism_counts[1] = 1
mechanism_counts[22] = 2
mechanism, mechanism_offsets = build(mechanism_counts)
for index, controller in enumerate((1, 2)):
    record = mechanism_offsets[22] + index * sizes[22]
    p32(mechanism, record, 7)
    p32(mechanism, record + 4, controller)
    p32(mechanism, record + 8, 0)
    p32(mechanism, record + 12, 0)
    p32(mechanism, record + 16, 0xffffffff)
checksum(mechanism)
emit("mechanism-pair.rune", mechanism)

mutation("bad-section-count.rune", minimal,
         lambda data: p32(data, 12, 29))
mutation("bad-offset.rune", minimal,
         lambda data: p64(data, 48 + descriptor + 16,
                           u64(data, 48 + descriptor + 16) + 8))
mutation("bad-overflow.rune", minimal,
         lambda data: p32(data, 48 + 27 * descriptor + 8, 1))
mutation("bad-crc.rune", weapon,
         lambda data: data.__setitem__(header, data[header] ^ 1), False)
mutation("bad-kernel-size.rune", weapon,
         lambda data: p32(data, 48 + 10 * descriptor + 4, 16))
mutation("bad-profile-mask.rune", weapon,
         lambda data: p32(data, profile + 4, 0x1000))
mutation("bad-kernel-family.rune", weapon,
         lambda data: p32(data, kernel + 8, 12))
mutation("bad-profile-family-link.rune", weapon,
         lambda data: p32(data, kernel + 8, 2))
mutation("bad-kernel-span.rune", weapon,
         lambda data: (p32(data, kernel + 12, 1), p32(data, kernel + 16, 1)))
mutation("bad-kernel-profile.rune", weapon,
         lambda data: p32(data, kernel + 4, 1))
mutation("bad-cell-enum.rune", weapon,
         lambda data: data.__setitem__(cell + 76, 4))
mutation("bad-source-union.rune", source,
         lambda data: data.__setitem__(facet + 12, 1))
mutation("bad-analytic-target.rune", analytic,
         lambda data: p32(data, analytic_offsets[12] + 8, 1))
mutation("bad-input-dimension.rune", dimension,
         lambda data: p32(data, dimension_input, 16))
mutation("bad-analytic-output.rune", dimension,
         lambda data: p32(data, dimension_function + 12, 20))
emit("truncated.rune", weapon[:-1])
with (out / "too-large.rune").open("wb") as oversized:
    oversized.truncate(4294967297)
PY

strict='-std=c11 -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Wcast-qual -Wcast-align'

reject() {
    if "$1" "$tmp_dir/$2" >/dev/null 2>&1
    then
        printf '%s\n' "accepted hostile image: $2" >&2
        return 1
    fi
}

exercise() {
    reader=$1
    "$reader" "$tmp_dir/production.rune" > "$tmp_dir/production.json"
    grep -q '"analytic_function_refs":114' "$tmp_dir/production.json"
    grep -q '"movement_fields":2' "$tmp_dir/production.json"
    grep -q '"weapon_kernels":26' "$tmp_dir/production.json"
    "$reader" "$tmp_dir/minimal.rune" > /dev/null
    "$reader" "$tmp_dir/source.rune" > /dev/null
    "$reader" "$tmp_dir/analytic.rune" > /dev/null
    "$reader" "$tmp_dir/dimension.rune" > /dev/null
    "$reader" "$tmp_dir/ballistic.rune" > /dev/null
    "$reader" "$tmp_dir/movement-pair.rune" > /dev/null
    "$reader" "$tmp_dir/mechanism-pair.rune" > /dev/null
    "$reader" "$tmp_dir/valid.rune" > "$tmp_dir/valid.json"
    grep -q '"weapon_kernels":1' "$tmp_dir/valid.json"
    "$reader" --expected-identity-file "$tmp_dir/valid.identity" \
        "$tmp_dir/valid.rune" > /dev/null
    if "$reader" --expected-identity-file "$tmp_dir/bad.identity" \
        "$tmp_dir/valid.rune" >/dev/null 2>&1
    then
        printf '%s\n' 'accepted mismatched identity' >&2
        return 1
    fi
    for image in \
        truncated.rune too-large.rune bad-section-count.rune bad-offset.rune \
        bad-overflow.rune bad-crc.rune bad-kernel-size.rune \
        bad-profile-mask.rune bad-kernel-family.rune \
        bad-profile-family-link.rune bad-kernel-span.rune \
        bad-kernel-profile.rune bad-cell-enum.rune bad-source-union.rune \
        bad-analytic-target.rune bad-input-dimension.rune \
        bad-analytic-output.rune; do
        reject "$reader" "$image"
    done
}

cd "$repo_dir"
mkdir -p "$reader_dir"
cp tools/runecompactread.c tools/runecompactread.h "$reader_dir"
for cc in gcc clang; do
    $cc $strict "$reader_dir/runecompactread.c" \
        -o "$tmp_dir/runecompactread-$cc"
    exercise "$tmp_dir/runecompactread-$cc"
done

clang $strict -fno-omit-frame-pointer -fsanitize=address,undefined \
    "$reader_dir/runecompactread.c" -o "$tmp_dir/runecompactread-sanitize"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    exercise "$tmp_dir/runecompactread-sanitize"
