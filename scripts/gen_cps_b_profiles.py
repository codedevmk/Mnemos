#!/usr/bin/env python3
"""Transcribe the CPS1 CPS-B profile + gfx-mapper census from the reference core.

Mnemos keeps the CPS-B per-board configuration as hardware-keyed data
(src/manifests/capcom_cps1/cps_b_profiles.cpp). Rather than hand-type ~25 boards
of scrambled register offsets + gfx bank-range tables (the #1 correctness surface
for CPS1), this aid parses the reference Emu C core's tables and emits that census
mechanically, plus golden gfx-mapping tuples computed by an INDEPENDENT
reimplementation of the mapper algorithm (so the conformance test cross-checks
the C++ map_gfx_code instead of echoing the data back at itself).

The goldens are computed by a separate Python port of the mapper algorithm, so
the conformance test cross-checks the transcribed DATA against the reference
tables (both sides port the same algorithm, so this verifies data fidelity, not
the algorithm spec).

It is a transcription aid, not part of the build: the reference cps1.c lives in a
separate local checkout, so this does not run in CI. Re-run it (then clang-format)
when the reference tables change or to extend coverage (e.g. CPS2).

Usage:
    python scripts/gen_cps_b_profiles.py [path/to/reference/cps1.c]

It rewrites cps_b_profiles.cpp in place and prints the golden_rows() body for the
maintainer to paste into tests/cps_b_profile_test.cpp.
"""
import os
import re
import sys

DEFAULT_SRC = r"C:/Users/mkrol/source/repos/Emu/Emu/systems/capcom/cps1/cps1.c"
SRC = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
OUT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "src", "manifests", "capcom_cps1")
)

REG_NONE = 0xFF
GFX_BITS = {
    "CPS1_GFX_SPRITES": 1,
    "CPS1_GFX_SCROLL1": 2,
    "CPS1_GFX_SCROLL2": 4,
    "CPS1_GFX_SCROLL3": 8,
}


def num(tok):
    t = tok.strip().rstrip("uU")
    if t == "CPS1_CPS_B_REG_NONE":
        return REG_NONE
    if t == "NULL":
        return None
    return int(t, 0)


def split_top(s):
    """Split a brace-init body on top-level commas (respecting nested braces)."""
    out, depth, cur = [], 0, ""
    for ch in s:
        if ch == "{":
            depth += 1
            cur += ch
        elif ch == "}":
            depth -= 1
            cur += ch
        elif ch == "," and depth == 0:
            out.append(cur)
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur)
    return [t for t in out if t.strip()]


with open(SRC, "r", encoding="utf-8", errors="replace") as _f:
    src = _f.read()

# profile enum: suffix-name -> numeric id
enum_block = re.search(r"typedef enum cps1_cps_b_profile\s*\{(.*?)\}", src, re.S).group(1)
prof_id = {m.group(1): int(m.group(2)) for m in re.finditer(r"CPS1_CPS_B_PROFILE_(\w+)\s*=\s*(\d+)", enum_block)}

# mapper range arrays: mapper-name -> [(tmask, start, end, bank)]
mapper_ranges = {}
for m in re.finditer(r"k_cps1_gfx_mapper_(\w+)_ranges\[\]\s*=\s*\{(.*?)\};", src, re.S):
    ranges = []
    for r in re.finditer(r"\{([^}]*)\}", m.group(2)):
        parts = [p.strip() for p in r.group(1).split(",")]
        tmask = 0
        for t in parts[0].split("|"):
            tmask |= GFX_BITS[t.strip()]
        ranges.append((tmask, num(parts[1]), num(parts[2]), num(parts[3])))
    mapper_ranges[m.group(1)] = ranges

# mapper structs: mapper-name -> (bank_size[4], ranges-name)
mapper = {}
for m in re.finditer(
    r"k_cps1_gfx_mapper_(\w+)\s*=\s*\{\s*\{([^}]*)\}\s*,\s*k_cps1_gfx_mapper_(\w+)_ranges", src
):
    mapper[m.group(1)] = ([num(b) for b in m.group(2).split(",")], m.group(3))

# config structs: config-name -> field dict
config = {}
for m in re.finditer(r"k_cps1_cps_b_(\w+)\s*=\s*\{(.*?)\};", src, re.S):
    t = split_top(m.group(2))
    mp = re.match(r"&k_cps1_gfx_mapper_(\w+)", t[10].strip())
    config[m.group(1)] = dict(
        id_offset=num(t[0]),
        id_value=num(t[1]),
        mult=[num(t[2]), num(t[3]), num(t[4]), num(t[5])],
        layer_control=num(t[6]),
        priority=[num(x) for x in t[7].strip().strip("{}").split(",")],
        palette=num(t[8]),
        enable=[num(x) for x in t[9].strip().strip("{}").split(",")],
        mapper=mp.group(1) if mp else None,
    )

# dispatch: numeric profile id -> config-name (authoritative, incl. aliases)
disp = re.search(r"cps1_get_cps_b_config[^{]*\{(.*?)\n\}", src, re.S).group(1)
id_to_config = {}
for m in re.finditer(r"CPS1_CPS_B_PROFILE_(\w+)\)\s*\{\s*return &k_cps1_cps_b_(\w+);", disp):
    id_to_config[prof_id[m.group(1)]] = m.group(2)

# Mnemos correction (not in the reference): the upstream core aliases PS63B
# (profile 25, The Punisher) to the bt3 config, which drops the board's CPS-B-21
# protection ID port -- punisher's boot reads register $0E expecting $0C00 and
# halts at a branch-to-self without it (the reference does not boot punisher
# either). Inject the hardware-faithful CPS-B-21 "QS3" config + the PS63B gfx
# mapper so the generated census matches real hardware, not the lossy alias.
mapper_ranges["ps63b"] = [
    (GFX_BITS["CPS1_GFX_SCROLL1"], 0x0000, 0x0FFF, 0),
    (GFX_BITS["CPS1_GFX_SPRITES"], 0x1000, 0x7FFF, 0),
    (GFX_BITS["CPS1_GFX_SPRITES"] | GFX_BITS["CPS1_GFX_SCROLL2"], 0x8000, 0xDBFF, 1),
    (GFX_BITS["CPS1_GFX_SCROLL3"], 0xDC00, 0xFFFF, 1),
]
mapper["ps63b"] = ([0x8000, 0x8000, 0, 0], "ps63b")
config["ps63b"] = dict(
    id_offset=0x0E,
    id_value=0x0C00,
    mult=[REG_NONE, REG_NONE, REG_NONE, REG_NONE],
    layer_control=0x12,
    priority=[0x14, 0x16, 0x08, 0x0A],
    palette=0x0C,
    enable=[0x04, 0x02, 0x20, 0, 0],
    mapper="ps63b",
)
id_to_config[25] = "ps63b"

# Mnemos additions (absent from the reference core entirely): the QSound clones
# wofu / slammast / mbombrd. Their CPS-B-21 configs ("QS1", "QS4", "QS5") + the
# MB63B 3-bank gfx mapper are transcribed from the hardware-faithful CPS-B-21
# census. wofu (QS1) reuses the existing tk263b mapper; slammast (QS4) + mbombrd
# (QS5) share MB63B. slammast/mbombrd carry a board-ID protection port (like
# punisher) the games read at boot. IDs 40-42 sit above the reference enum range.
# (Extra player-3/4 input ports the QS4/QS5 configs also define are not modelled
# here -- attract/boot does not need them; they are a gameplay refinement.)
# MB63B (verified from a PAL dump): all four layer types share the full 3-bank
# range (0x8000 codes per bank). An earlier/lossy table restricted scroll1 to
# 0x0-0xfff, which garbled slammast's scroll1-heavy screens (its codes reach
# bank 1); mbombrd's blank scroll1 hid the bug.
_mb63b_all4 = (
    GFX_BITS["CPS1_GFX_SPRITES"] | GFX_BITS["CPS1_GFX_SCROLL1"] |
    GFX_BITS["CPS1_GFX_SCROLL2"] | GFX_BITS["CPS1_GFX_SCROLL3"]
)
mapper_ranges["mb63b"] = [
    (_mb63b_all4, 0x00000, 0x07FFF, 0),
    (_mb63b_all4, 0x08000, 0x0FFFF, 1),
    (_mb63b_all4, 0x10000, 0x17FFF, 2),
]
mapper["mb63b"] = ([0x8000, 0x8000, 0x8000, 0], "mb63b")
config["qs1_tk263b"] = dict(
    id_offset=REG_NONE, id_value=0x0000, mult=[REG_NONE, REG_NONE, REG_NONE, REG_NONE],
    layer_control=0x22, priority=[0x24, 0x26, 0x28, 0x2A], palette=0x2C,
    enable=[0x10, 0x08, 0x04, 0, 0], mapper="tk263b",
)
config["qs4_mb63b"] = dict(
    id_offset=0x2E, id_value=0x0C01, mult=[REG_NONE, REG_NONE, REG_NONE, REG_NONE],
    layer_control=0x16, priority=[0x00, 0x02, 0x28, 0x2A], palette=0x2C,
    enable=[0x04, 0x08, 0x10, 0, 0], mapper="mb63b",
)
config["qs5_mb63b"] = dict(
    id_offset=0x1E, id_value=0x0C02, mult=[REG_NONE, REG_NONE, REG_NONE, REG_NONE],
    layer_control=0x2A, priority=[0x2C, 0x2E, 0x30, 0x32], palette=0x1C,
    enable=[0x04, 0x08, 0x10, 0, 0], mapper="mb63b",
)
id_to_config[40] = "qs1_tk263b"
id_to_config[41] = "qs4_mb63b"
id_to_config[42] = "qs5_mb63b"

# The reference core later gained a profile 32 (cps_b 21_mb63b) that is
# byte-identical to the QS4 config already carried as profile 41 (slammast's
# board); nothing references id 32, so drop it to keep the census free of a
# duplicate row.
id_to_config.pop(32, None)

# Mnemos additions (two more CPS1 parents). sf2 (the original Street Fighter II)
# runs the CPS_B_11 register layout with the STF29 gfx mapper -- and STF29 is
# byte-identical to the S9263B mapper already transcribed (profile 21 / sf2ce),
# so it is reused here. megaman runs the default CPS-B-21 layout with the RCM63B
# 4-bank gfx mapper (all four layer types span the full four 0x8000-code banks),
# which is absent from the reference core.
_all4 = (
    GFX_BITS["CPS1_GFX_SPRITES"] | GFX_BITS["CPS1_GFX_SCROLL1"] |
    GFX_BITS["CPS1_GFX_SCROLL2"] | GFX_BITS["CPS1_GFX_SCROLL3"]
)
config["b11_s9263b"] = dict(
    id_offset=0x32, id_value=0x0401, mult=[REG_NONE, REG_NONE, REG_NONE, REG_NONE],
    layer_control=0x26, priority=[0x28, 0x2A, 0x2C, 0x2E], palette=0x30,
    enable=[0x08, 0x10, 0x20, 0, 0], mapper="s9263b",
)
mapper_ranges["rcm63b"] = [
    (_all4, 0x00000, 0x07FFF, 0),
    (_all4, 0x08000, 0x0FFFF, 1),
    (_all4, 0x10000, 0x17FFF, 2),
    (_all4, 0x18000, 0x1FFFF, 3),
]
mapper["rcm63b"] = ([0x8000, 0x8000, 0x8000, 0x8000], "rcm63b")
config["def_rcm63b"] = dict(
    id_offset=REG_NONE, id_value=0x0000, mult=[0x00, 0x02, 0x04, 0x06],
    layer_control=0x26, priority=[0x28, 0x2A, 0x2C, 0x2E], palette=0x30,
    enable=[0x02, 0x04, 0x08, 0x30, 0x30], mapper="rcm63b",
)
id_to_config[43] = "b11_s9263b"
id_to_config[44] = "def_rcm63b"

# Mnemos additions: the SF2 regional CPS-B boards, each paired with the S9263B
# (== STF29, byte-identical) gfx mapper -- the board variants the sf2 World/US/JP
# program clones run on. The register layouts are the stock CPS_B_05/12/13/14/15
# (identical to profiles 5/12/13/14/15, whose boards differ only in the gfx
# mapper) plus CPS_B_17 (new); transcribed from the reference CPS_B_xx tables.
# (id_offset, id_value, layer_control, priority[4], palette, enable[5])
_sf2_boards = {
    45: ("b05_s9263b", 0x20, 0x0005, 0x28, [0x2A, 0x2C, 0x2E, 0x30], 0x32, [0x02, 0x08, 0x20, 0x14, 0x14]),
    46: ("b12_s9263b", 0x20, 0x0402, 0x2C, [0x2A, 0x28, 0x26, 0x24], 0x22, [0x02, 0x04, 0x08, 0, 0]),
    47: ("b13_s9263b", 0x2E, 0x0403, 0x22, [0x24, 0x26, 0x28, 0x2A], 0x2C, [0x20, 0x02, 0x04, 0, 0]),
    48: ("b14_s9263b", 0x1E, 0x0404, 0x12, [0x14, 0x16, 0x18, 0x1A], 0x1C, [0x08, 0x20, 0x10, 0, 0]),
    49: ("b15_s9263b", 0x0E, 0x0405, 0x02, [0x04, 0x06, 0x08, 0x0A], 0x0C, [0x04, 0x02, 0x20, 0, 0]),
    50: ("b17_s9263b", 0x08, 0x0407, 0x14, [0x12, 0x10, 0x0E, 0x0C], 0x0A, [0x08, 0x14, 0x02, 0, 0]),
}
for _pid, (_cn, _io, _iv, _lc, _pr, _pa, _en) in _sf2_boards.items():
    config[_cn] = dict(id_offset=_io, id_value=_iv,
                       mult=[REG_NONE, REG_NONE, REG_NONE, REG_NONE],
                       layer_control=_lc, priority=_pr, palette=_pa, enable=_en, mapper="s9263b")
    id_to_config[_pid] = _cn

# Mnemos addition: the sf2 DEF/S9263B hacks (koryu/m5/m7/yyc) differ from the
# stock CPS_B_21_DEF board only by a bootleg_kludge (0x41: forced object port
# 0x9100 + scroll nudge + reverse sprite order).
config["def_s9263b_k41"] = dict(
    id_offset=REG_NONE, id_value=0x0000, mult=[0x00, 0x02, 0x04, 0x06],
    layer_control=0x26, priority=[0x28, 0x2A, 0x2C, 0x2E], palette=0x30,
    enable=[0x02, 0x04, 0x08, 0x30, 0x30], mapper="s9263b", kludge=0x41,
)
id_to_config[51] = "def_s9263b_k41"

# independent reimplementation of map_gfx_code (the golden oracle)
SHIFT = {1: 1, 2: 0, 4: 1, 8: 3}  # sprites, scroll1, scroll2, scroll3
ABSENT = "absent"


def map_code(banks, ranges, type_bit, code):
    if not ranges:
        return code
    shift = SHIFT[type_bit]
    expanded = code << shift
    for (tmask, start, end, bank) in ranges:
        if not (tmask & type_bit) or expanded < start or expanded > end or bank >= 4:
            continue
        bs = banks[bank]
        if bs == 0 or (bs & (bs - 1)) != 0:
            return ABSENT
        return (sum(banks[:bank]) + (expanded & (bs - 1))) >> shift
    return ABSENT


BITNAME = [(1, "spr"), (2, "sc1"), (4, "sc2"), (8, "sc3")]
TYPENAME = {1: "sprites", 2: "scroll1", 4: "scroll2", 8: "scroll3"}


def tmask_cpp(t):
    return "all4" if t == 15 else " | ".join(n for b, n in BITNAME if t & b)


def goldens_for(banks, ranges):
    """Per range: a code at its start and end (covering identity / wrap /
    concatenation), plus one out-of-range reject."""
    seen, out = set(), []

    def add(bit, code):
        if (bit, code) in seen:
            return
        seen.add((bit, code))
        out.append((TYPENAME[bit], code, map_code(banks, ranges, bit, code)))

    max_end = 0
    for (tmask, start, end, bank) in ranges:
        max_end = max(max_end, end)
        # probe every layer the range serves, so a per-layer bug in a shared
        # range (all4 / spr|sc2 / sc2|sc3) cannot hide behind one layer bit.
        for bit, _ in BITNAME:
            if not (tmask & bit):
                continue
            shift = SHIFT[bit]
            for edge in (start, end):
                code = edge >> shift
                if (code << shift) < start:
                    code += 1
                if start <= (code << shift) <= end:
                    add(bit, code)
    last_bit = next(b for b, _ in BITNAME if ranges[-1][0] & b)
    probe = (max_end >> SHIFT[last_bit]) + 0x1000
    if map_code(banks, ranges, last_bit, probe) == ABSENT:
        add(last_bit, probe)
    return out


ids = sorted(id_to_config)
used = []
for pid in ids:
    mn = config[id_to_config[pid]]["mapper"]
    if mn and mn not in used:
        used.append(mn)


def hx(v, w=2):
    return f"0x{v:0{w}X}U"


def reg(v):
    return "reg_none" if v == REG_NONE else hx(v)


L = []
L.append('#include "cps_b_profiles.hpp"')
L.append("")
L.append("#include <array>")
L.append("")
L.append("// Transcribed from the reference CPS1 CPS-B config + gfx-mapper hardware tables,")
L.append("// mechanically (to avoid hand-transcription error) into this faithful, uniform")
L.append("// shape. Keyed by the numeric CPS-B profile id (a board / PAL identity); the PAL")
L.append("// / board names in comments are documentation only (see THIRD-PARTY-REFERENCES.md),")
L.append("// never lookup keys. Some ids share a register layout / mapper (e.g. cd63b and")
L.append("// tk263b); each is kept as its own row to mirror the per-board census")
L.append("// 1:1 -- the duplication is intentional, not a DRY slip.")
L.append("namespace mnemos::manifests::capcom_cps1 {")
L.append("    namespace {")
L.append("        using gfx_bank_range = chips::video::cps_a_b::gfx_bank_range;")
L.append("        constexpr std::uint8_t reg_none = chips::video::cps_a_b::reg_none;")
L.append("")
L.append("        // gfx-mapper layer bits (match cps_a_b::gfx_type_bit).")
L.append("        constexpr std::uint8_t spr = 0x01U;")
L.append("        constexpr std::uint8_t sc1 = 0x02U;")
L.append("        constexpr std::uint8_t sc2 = 0x04U;")
L.append("        constexpr std::uint8_t sc3 = 0x08U;")
L.append("        constexpr std::uint8_t all4 = spr | sc1 | sc2 | sc3;")
L.append("")
for mn in used:
    banks, rname = mapper[mn]
    rs = mapper_ranges[rname]
    L.append(f"        constexpr std::array<gfx_bank_range, {len(rs)}> ranges_{mn}{{{{")
    for (tmask, start, end, bank) in rs:
        L.append(f"            {{{tmask_cpp(tmask)}, 0x{start:04X}U, 0x{end:04X}U, {bank}U}},")
    L.append("        }};")
L.append("")
L.append(f"        constexpr std::array<cps_b_profile, {len(ids)}> board_db{{{{")
for pid in ids:
    cname = id_to_config[pid]
    c = config[cname]
    banks = mapper[c["mapper"]][0]
    bs = ", ".join(hx(b, 4) if b else "0U" for b in banks)
    pr = ", ".join(reg(x) for x in c["priority"])
    en = ", ".join(hx(x, 2) if x else "0U" for x in c["enable"])
    mu = ", ".join(reg(x) for x in c["mult"])
    L.append(f"            // profile {pid} (cps_b {cname}, mapper {c['mapper']})")
    L.append("            cps_b_profile{")
    L.append("                .legacy = false,")
    L.append(f"                .layer_control_offset = {reg(c['layer_control'])},")
    L.append(f"                .priority_offset = {{{pr}}},")
    L.append(f"                .palette_control_offset = {reg(c['palette'])},")
    L.append(f"                .layer_enable_mask = {{{en}}},")
    L.append(f"                .id = {pid}U,")
    L.append(f"                .id_offset = {reg(c['id_offset'])},")
    L.append(f"                .id_value = {hx(c['id_value'], 4)},")
    L.append(f"                .mult_offset = {{{mu}}},")
    if c.get("kludge", 0):
        L.append(f"                .bootleg_kludge = {hx(c['kludge'], 2)},")
    L.append(f"                .mapper = {{.bank_size = {{{bs}}}, .ranges = ranges_{c['mapper']}}},")
    L.append("            },")
L.append("        }};")
L.append("    } // namespace")
L.append("")
L.append("    std::optional<cps_b_profile> profile_for_id(std::uint16_t profile_id) noexcept {")
L.append("        for (const cps_b_profile& profile : board_db) {")
L.append("            if (profile.id == profile_id) {")
L.append("                return profile;")
L.append("            }")
L.append("        }")
L.append("        return std::nullopt;")
L.append("    }")
L.append("")
L.append("    std::size_t profile_count() noexcept { return board_db.size(); }")
L.append("")
L.append("} // namespace mnemos::manifests::capcom_cps1")
L.append("")
with open(os.path.join(OUT_DIR, "cps_b_profiles.cpp"), "w", encoding="utf-8", newline="\n") as f:
    f.write("\n".join(L))

# print the profile_rows() body for the maintainer to paste into the test: each
# row carries the full register transcription + bank sizes + golden gfx mappings,
# so the test sweep guards every profile's scramble against drift.
GTYPE = {"sprites": "gfx::sprites", "scroll1": "gfx::scroll1", "scroll2": "gfx::scroll2",
         "scroll3": "gfx::scroll3"}
print("// --- paste into cps_b_profile_test.cpp profile_rows(), then clang-format ---")
for pid in ids:
    c = config[id_to_config[pid]]
    banks = mapper[c["mapper"]][0]
    rs = mapper_ranges[mapper[c["mapper"]][1]]
    pr = ", ".join(reg(x) for x in c["priority"])
    en = ", ".join(hx(x, 2) if x else "0U" for x in c["enable"])
    mu = ", ".join(reg(x) for x in c["mult"])
    bs = ", ".join(hx(b, 4) if b else "0U" for b in banks)
    gtext = ", ".join(
        "{%s, 0x%XU, %s}" % (GTYPE[t], code, ("absent" if mp == ABSENT else "0x%XU" % mp))
        for (t, code, mp) in goldens_for(banks, rs)
    )
    print("        {%dU, %s, {%s}, %s, {%s}, %s, %s, {%s}, {%s}, %dU, {%s}}," % (
        pid, reg(c["layer_control"]), pr, reg(c["palette"]), en, reg(c["id_offset"]),
        hx(c["id_value"], 4), mu, bs, len(rs), gtext))
print(f"// profiles: {len(ids)}  ids: {ids}", file=sys.stderr)
