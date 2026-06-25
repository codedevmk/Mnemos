#!/usr/bin/env python3
"""Generate CPS-2 game.toml ROM-set declarations from the authentic zips.

CPS-2 ROMs load by a fixed file-slot convention (the trailing number after the
last '.', plus an 'm' mask suffix), so the per-set toml is fully derivable from
the zip's central directory (names + CRC32 + sizes) -- no per-game table:

  slots 3-10 (no m) -> maincpu : the encrypted 68000 program (word-swapped)
  slots 1-2  (no m) -> audiocpu: the QSound Z80 (low 0x8000 fixed, rest banked
                                  from 0x10000)
  slots 11-12 (m)   -> qsound  : DL-1425 samples (word-swapped)
  slots 13-20 (m)   -> gfx     : word-lane interleaved (the board then unshuffles
                                  each 0x200000 bank at load)

Later boards package gfx + QSound on SIMM modules instead of discrete chips. Those
parts carry "simm" in the name and their slot in the extension (".01a"/".simm1"):
gfx SIMMs (groups 1, 3) feed the 8-byte-lane interleave, QSound SIMMs (groups 5, 6)
concatenate word-swapped; program + audiocpu still come from the discrete files.

The 20-byte board key is an external asset resolved at runtime from a
keys/<set>*.key sidecar (validated by decrypting the reset vector), so it is not
referenced here.

Usage: python scripts/gen_cps2_game_tomls.py <rom_dir> <out_dir> [set ...]
"""
import os
import sys
import zipfile

# slot -> (group, lane) for the standard word-lane gfx layout.
GFX_SLOT_LANE = {13: (0, 0), 15: (0, 2), 17: (0, 4), 19: (0, 6),
                 14: (1, 0), 16: (1, 2), 18: (1, 4), 20: (1, 6)}

# SIMM layout: later CPS-2 boards package gfx + QSound on SIMM modules instead of
# discrete chips. A SIMM file name carries its physical slot ("simm.<group><sub>",
# e.g. "pl2-simm.01a"): the leading number is the SIMM group, the trailing a-d is
# the sub-chip. gfx SIMMs (groups 1, 3) feed the 8-byte-lane gfx interleave -- the
# 4 sub-chips of a group map to byte lanes [2,3,0,1], and group 3 sits 4 lanes up
# from group 1; QSound SIMMs (groups 5, 6) concatenate word-swapped. program +
# audiocpu still come from the discrete (non-SIMM) files.
SIMM_GFX_GROUP_LANES = {1: 0, 3: 4}          # group -> base byte lane
SIMM_SUB_BYTE_LANE = [2, 3, 0, 1]            # sub-chip a/b/c/d -> lane within group
SIMM_QSOUND_GROUPS = (5, 6)
VERTICAL_SET_PREFIXES = ("19xx", "1944")
INPUT_PROFILES = {
    "19xx": (2, "two_player_two_button"),
    "1944": (2, "two_player_two_button"),
    "armwar": (3, "three_player_three_button"),
    "avsp": (3, "three_player_three_button"),
    "batcir": (4, "four_player_two_button"),
    "choko": (1, "one_player_three_button"),
    "cybots": (2, "cybots_four_button"),
    "ddsom": (4, "four_player_four_button"),
    "ddtod": (4, "four_player_four_button"),
    "dimahoo": (2, "two_player_three_button"),
    "ecofghtr": (2, "ecofighters_spinner"),
    "gigawing": (2, "two_player_two_button"),
    "megaman2": (2, "two_player_three_button"),
    "mmatrix": (2, "two_player_one_button"),
    "mpang": (2, "two_player_one_button"),
    "progear": (2, "two_player_three_button"),
    "pzloop2": (2, "puzz_loop_2_paddle"),
    "sgemf": (2, "two_player_three_button"),
    "sfa3b": (2, "six_button_ticket_dispenser"),
    "sfa3h": (2, "six_button_ticket_dispenser"),
    "sfa3hr1": (2, "six_button_ticket_dispenser"),
    "spf2t": (2, "two_player_two_button"),
}
QSOUND_HLE_RATIONALE = (
    "Behavioral DL-1425 QSound PCM/ADPCM mixer; DSP16 instruction-level model "
    "is not implemented."
)


def parse_simm(name):
    """(group, sub) for a SIMM part name, or None. Two naming forms exist:
    A) ".<group><sub>"        e.g. "pl2-simm.01a"  (group + sub after the dot)
    B) ".simm<group>" + "_<sub>" before the dot   e.g. "tkoj1_a.simm1"
    """
    dot = name.rfind('.')
    if dot < 0:
        return None
    rest = name[dot + 1:]
    if rest[:1].isdigit():  # form A
        i = 0
        while i < len(rest) and rest[i].isdigit():
            i += 1
        sub = rest[i:i + 1].lower()
        if not ('a' <= sub <= 'd'):
            return None
        return int(rest[:i]), ord(sub) - ord('a')
    if rest[:4].lower() == "simm" and rest[4:5].isdigit():  # form B
        j = 4
        while j < len(rest) and rest[j].isdigit():
            j += 1
        group = int(rest[4:j])
        q = dot  # scan back from the dot for the a-d sub-chip (skip _ / - separators)
        while q > 0:
            q -= 1
            c = name[q].lower()
            if 'a' <= c <= 'd':
                return group, ord(c) - ord('a')
            if c not in ('_', '-'):
                break
        return None
    return None


def parse_slot(name):
    dot = name.rfind('.')
    if dot < 0:
        return None, False
    rest = name[dot + 1:]
    i = 0
    while i < len(rest) and rest[i].isdigit():
        i += 1
    if i == 0:
        return None, False
    return int(rest[:i]), (i < len(rest) and rest[i] in ('m', 'M'))


def entries(zip_path):
    out = []
    with zipfile.ZipFile(zip_path) as z:
        for info in z.infolist():
            if info.is_dir():
                continue
            out.append((os.path.basename(info.filename), info.CRC, info.file_size))
    return out


def hexc(crc):
    return "0x%08x" % (crc & 0xFFFFFFFF)


def is_vertical_set(set_name):
    return any(set_name.startswith(prefix) for prefix in VERTICAL_SET_PREFIXES)


def emit_region(name, size, files, fill=None):
    lines = ["", "[[region]]", 'name = "%s"' % name, "size = 0x%x" % size]
    if fill is not None:
        lines.append("fill = 0x%02x" % fill)
    for f in files:
        lines.append("")
        lines.append("[[region.file]]")
        for k, v in f:
            lines.append("%-13s = %s" % (k, v))
    return lines


def gen(set_name, zip_path):
    program, audio, qsound, gfx = [], [], [], []
    simm_gfx, simm_qsound = [], []  # (sort_key, byte_lane, name, crc, size) / (key, ...)
    for name, crc, size in entries(zip_path):
        if "simm" in name.lower():
            parsed = parse_simm(name)
            if parsed is None:
                continue
            group, sub = parsed
            if group in SIMM_GFX_GROUP_LANES:
                lane = SIMM_GFX_GROUP_LANES[group] + SIMM_SUB_BYTE_LANE[sub]
                simm_gfx.append((group * 4 + sub, lane, name, crc, size))
            elif group in SIMM_QSOUND_GROUPS:
                simm_qsound.append((group * 4 + sub, name, crc, size))
            continue
        slot, m = parse_slot(name)
        if slot is None:
            continue
        if 3 <= slot <= 10 and not m:
            program.append((slot, name, crc, size))
        elif slot in (1, 2) and not m:
            audio.append((slot, name, crc, size))
        elif slot in (11, 12) and m:
            qsound.append((slot, name, crc, size))
        elif 13 <= slot <= 20 and m:
            gfx.append((slot, name, crc, size))
    if not program or not (gfx or simm_gfx):
        return None, "no program/gfx slots"

    out = ["# %s -- Capcom CPS2 (generated by scripts/gen_cps2_game_tomls.py)" % set_name,
           "#",
           "# The 20-byte board key is an external asset, resolved at runtime from a",
           "# zip/parent-zip .key entry or keys/%s*.key sidecar (validated by decryption)," % set_name,
           "# never committed.",
           "", "[set]", 'schema = "mnemos-romset/1"',
           'name   = "%s"' % set_name, 'board  = "capcom_cps2"']
    if is_vertical_set(set_name):
        out.append('orientation = "vertical"')
    if set_name in INPUT_PROFILES:
        players, input_profile = INPUT_PROFILES[set_name]
        out.append("players = %d" % players)
        out.append('input   = "%s"' % input_profile)
    out += ["", "[[hle]]", 'chip = "capcom.qsound"',
            'rationale = "%s"' % QSOUND_HLE_RATIONALE]

    # --- maincpu: program slots word-swapped, concatenated ---
    program.sort()
    off, files = 0, []
    for _slot, name, crc, size in program:
        files.append([("name", '"%s"' % name), ("offset", "0x%x" % off),
                      ("unit", "2"), ("swap", "true"), ("stride", "2"),
                      ("size", "0x%x" % size), ("crc32", hexc(crc))])
        off += size
    out += emit_region("maincpu", off, files)

    # --- audiocpu: slot 1 maps low 0x8000 fixed + continuation at 0x10000;
    # additional slots continue linearly in the packed QSound Z80 ROM image.
    audio.sort()
    if audio:
        _slot, name, crc, size = audio[0]
        packed_size = 0
        if size > 0x8000:
            files = [[("name", '"%s"' % name), ("offset", "0x0"),
                      ("source_offset", "0x0"), ("length", "0x8000"),
                      ("size", "0x%x" % size), ("crc32", hexc(crc))],
                     [("name", '"%s"' % name), ("offset", "0x10000"),
                      ("source_offset", "0x8000"),
                      ("length", "0x%x" % (size - 0x8000)),
                      ("size", "0x%x" % size), ("crc32", hexc(crc))]]
            packed_size = 0x10000 + (size - 0x8000)
        else:
            files = [[("name", '"%s"' % name), ("offset", "0x0"),
                      ("size", "0x%x" % size), ("crc32", hexc(crc))]]
            packed_size = max(size, 0x10000)
        for _slot, name, crc, size in audio[1:]:
            files.append([("name", '"%s"' % name), ("offset", "0x%x" % packed_size),
                          ("size", "0x%x" % size), ("crc32", hexc(crc))])
            packed_size += size
        region = 0x50000 if 0x8000 < packed_size <= 0x50000 else max(packed_size, 0x10000)
        # CPS2 QSound CPU ROM maps leave holes in the expanded image;
        # reference loaders expose those holes as zeroes.
        out += emit_region("audiocpu", region, files, fill=0x00)

    # --- qsound: discrete slots 11/12, or SIMM groups 5/6; word-swapped + concat ---
    qsource = simm_qsound if simm_qsound else [(s, n, c, z) for s, n, c, z in qsound]
    qsource.sort()
    if qsource:
        off, files = 0, []
        for _key, name, crc, size in qsource:
            files.append([("name", '"%s"' % name), ("offset", "0x%x" % off),
                          ("unit", "2"), ("swap", "true"), ("stride", "2"),
                          ("size", "0x%x" % size), ("crc32", hexc(crc))])
            off += size
        out += emit_region("qsound", off, files)

    # --- gfx: SIMM 8-byte-lane interleave, else discrete word-lane interleave ---
    if simm_gfx:
        simm_gfx.sort()
        gfx_size = max((lane + (size - 1) * 8 + 1
                        for _k, lane, _n, _c, size in simm_gfx), default=0)
        files = []
        for _key, lane, name, crc, size in simm_gfx:
            files.append([("name", '"%s"' % name), ("offset", "0x%x" % lane),
                          ("unit", "1"), ("stride", "8"),
                          ("size", "0x%x" % size), ("crc32", hexc(crc))])
        out += emit_region("gfx", gfx_size, files)
    else:
        # word-lane interleave; group0 base 0, group1 base max(span, 0x800000)
        group0_span = max((size * 4 for slot, _n, _c, size in gfx
                           if GFX_SLOT_LANE.get(slot, (9, 0))[0] == 0), default=0)
        group1_base = max(group0_span, 0x800000)
        group1_span = max((size * 4 for slot, _n, _c, size in gfx
                           if GFX_SLOT_LANE.get(slot, (9, 0))[0] == 1), default=0)
        gfx_size = group1_base + group1_span if group1_span else group0_span
        gfx.sort()
        files = []
        for slot, name, crc, size in gfx:
            if slot not in GFX_SLOT_LANE:
                return None, "unexpected gfx slot %d" % slot
            group, lane = GFX_SLOT_LANE[slot]
            base = 0 if group == 0 else group1_base
            files.append([("name", '"%s"' % name), ("offset", "0x%x" % (base + lane)),
                          ("unit", "2"), ("stride", "8"),
                          ("size", "0x%x" % size), ("crc32", hexc(crc))])
        out += emit_region("gfx", gfx_size, files)

    return "\n".join(out) + "\n", None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    rom_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)
    if len(sys.argv) > 3:
        names = sys.argv[3:]
    else:
        names = sorted(os.path.splitext(f)[0] for f in os.listdir(rom_dir)
                       if f.lower().endswith(".zip"))
    ok, skipped = 0, 0
    for name in names:
        zip_path = os.path.join(rom_dir, name + ".zip")
        if not os.path.isfile(zip_path):
            print("skip %s: no zip" % name)
            skipped += 1
            continue
        text, err = gen(name, zip_path)
        if text is None:
            print("skip %s: %s" % (name, err))
            skipped += 1
            continue
        with open(os.path.join(out_dir, name + ".toml"), "w", newline="\n") as fp:
            fp.write(text)
        print("wrote %s.toml" % name)
        ok += 1
    print("done: %d written, %d skipped" % (ok, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
