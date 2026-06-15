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
L.append("// never lookup keys. Some ids share a register layout / mapper (e.g. 23 and 25,")
L.append("// cd63b and tk263b); each is kept as its own row to mirror the per-board census")
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
