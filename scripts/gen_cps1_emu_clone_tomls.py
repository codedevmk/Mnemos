#!/usr/bin/env python3
"""Transcribe CPS1 clone game.tomls from the reference Emu core's part tables.

Companion to gen_cps1_clone_tomls.py (which derives clones from MAME's ROM_START
and refuses a clone whose gfx-mapper PAL label differs from its parent's). Several
Japanese clones run their own program board PAL (a different mapper *label*) yet
ship only their unique program ROMs and pull every gfx ROM from the parent set --
so the parent's gfx arrangement, hence the parent's CPS-B profile + mapper, is the
correct one to decode them. The MAME-based aid bails on the label mismatch; this
one transcribes straight from the Emu core's k_cps1_<set>_parts[] tables (the
proven reference transcription), reads CRCs from the local zips, and reuses the
parent's Mnemos profile id (read from the parent toml) -- except where a clone
genuinely needs a new board (daimakr2 runs CPS-B-21 / DAM63B over hybrid gfx).

Transcription aid, not part of the build (the reference cps1.c is a separate local
checkout, so this does not run in CI). Re-run, then clang-format, when the
reference tables change. Usage: python scripts/gen_cps1_emu_clone_tomls.py [--write]
"""
import os, re, sys, zipfile

EMU = r"C:/Users/mkrol/source/repos/Emu/Emu/systems/capcom/cps1/cps1.c"
ZIPDIR = r"D:/emu/capcom/cps1"
GAMES = os.path.normpath(os.path.join(os.path.dirname(__file__), "..",
                                      "src", "manifests", "capcom_cps1", "games"))
SRC = open(EMU, encoding="utf-8", errors="replace").read()

# clone -> (parent set, descr, profile override). A None override means "reuse the
# parent toml's cps_b_profile" (the clone shares the parent's gfx ROMs + board).
CLONES = {
    "wonder3":  ("3wonders", "Wonder 3 (Japan 910520)",              None),
    "lostwrld": ("forgottn", "Lost Worlds (Japan)",                  None),
    "daimakai": ("ghouls",   "Daimakaimura (Japan)",                 None),
    "stridrja": ("strider",  "Strider Hiryu (Japan)",                None),
    "daimakr2": ("ghouls",   "Daimakaimura (Japan Resale Ver.)",     36),  # CPS-B-21 / DAM63B
    "area88":   ("unsquad",  "Area 88 (Japan)",                      None),
    "chikij":   ("mtwins",   "Chiki Chiki Boys (Japan)",             None),
}

REGION = {"MAIN": "maincpu", "SOUND": "audiocpu", "OKI": "oki", "GFX": "gfx"}


def num(expr):
    """Parse a C integer literal, tolerating a trailing u/U suffix."""
    return int(expr.strip().rstrip("uU"), 0)


def kib(expr):
    m = re.match(r"CPS1_KIB\((\d+)\)", expr)
    return int(m.group(1)) * 1024 if m else num(expr)


def parent_profile(parent):
    txt = open(os.path.join(GAMES, parent + ".toml"), encoding="utf-8").read()
    return int(re.search(r"cps_b_profile\s*=\s*(\d+)", txt).group(1))


def parse_parts(clone):
    m = re.search(r"k_cps1_%s_parts\[\]\s*=\s*\{(.*?)\n\};" % clone, SRC, re.S)
    if not m:
        raise SystemExit(f"no parts table for {clone}")
    parts = []
    for line in m.group(1).splitlines():
        mm = re.match(r"\s*CPS1_ZIP_PART(_PARENT)?(_FULL|_SLICE)\((.*)\),?\s*$", line)
        if not mm:
            continue
        is_parent = bool(mm.group(1))
        is_slice = mm.group(2) == "_SLICE"
        a = [x.strip() for x in mm.group(3).split(",")]
        name, region, layout, source_size = a[0].strip('"'), \
            REGION[a[1].replace("CPS1_ZIP_REGION_", "")], \
            a[2].replace("CPS1_ZIP_LAYOUT_", ""), kib(a[3])
        if is_slice:
            source_offset, copy_size, dest_offset = num(a[4]), kib(a[5]), num(a[6])
        else:
            source_offset, copy_size, dest_offset = 0, source_size, num(a[4])
        parts.append(dict(name=name, parent=is_parent, region=region, layout=layout,
                          source_size=source_size, source_offset=source_offset,
                          copy_size=copy_size, dest_offset=dest_offset, slice=is_slice))
    return parts


def crc_map(path):
    with zipfile.ZipFile(path) as z:
        return {i.filename: i.CRC for i in z.infolist() if not i.is_dir()}


def dest_end(p):
    o, ss, L = p["dest_offset"], p["source_size"], p["layout"]
    if L == "BYTE_INTERLEAVE": return (o & ~1) + ss * 2
    if L == "WORD_SWAP":       return o + ss
    if L == "GFX_WORD_LANE":   return (o & ~7) + ss * 4
    if L == "GFX_BYTE_LANE":   return (o & ~7) + ss * 8
    return o + (p["copy_size"] if p["slice"] else ss)  # LINEAR


def emit_file(p, crc):
    lines = ["[[region.file]]", f'name   = "{p["name"]}"', f"offset = 0x{p['dest_offset']:x}"]
    L = p["layout"]
    if L == "BYTE_INTERLEAVE":
        lines.append("stride = 2")
    elif L == "WORD_SWAP":
        lines += ["unit   = 2", "stride = 2", "swap   = true"]
    elif L == "GFX_WORD_LANE":
        lines += ["unit   = 2", "stride = 8"]
    elif L == "GFX_BYTE_LANE":
        lines += ["unit   = 1", "stride = 8"]
    if p["slice"]:
        lines += [f"source_offset = 0x{p['source_offset']:x}", f"length        = 0x{p['copy_size']:x}"]
    lines += [f"size   = 0x{p['source_size']:x}", f"crc32  = 0x{crc:08x}"]
    return "\n".join(lines)


def gen(clone):
    parent, descr, override = CLONES[clone]
    profile = override if override is not None else parent_profile(parent)
    parts = parse_parts(clone)
    cz = crc_map(os.path.join(ZIPDIR, clone + ".zip"))
    pz = crc_map(os.path.join(ZIPDIR, parent + ".zip"))
    order = []
    for p in parts:
        if p["region"] not in order:
            order.append(p["region"])
    if override is None:
        rationale = (f"# shared from {parent}.zip). Shares the parent's gfx ROMs, so it reuses the\n"
                     f"# parent's CPS-B profile ({profile}).")
    else:
        rationale = (f"# shared from {parent}.zip). Runs its own board, so it carries its own\n"
                     f"# CPS-B profile ({profile}), not the parent's.")
    head = [f"# {descr} -- Capcom CPS1 clone of {parent} (parent-merge). Transcribed from the",
            f"# reference part tables; CRCs from the local zips (clone-unique from {clone}.zip,",
            rationale, "",
            "[set]", 'schema = "mnemos-romset/1"', f'name   = "{clone}"',
            'board  = "capcom_cps1"', f'parent = "{parent}"', f"cps_b_profile = {profile}"]
    body = []
    for rg in order:
        rps = [p for p in parts if p["region"] == rg]
        size = (max(dest_end(p) for p in rps) + 0x7FFF) & ~0x7FFF
        body.append(f'[[region]]\nname = "{rg}"\nsize = 0x{size:x}\n')
        for p in rps:
            src = cz if (not p["parent"] and p["name"] in cz) else pz
            if p["name"] not in src:
                src = cz if p["name"] in cz else pz
            if p["name"] not in src:
                raise SystemExit(f"{clone}: {p['name']} in neither zip")
            body.append(emit_file(p, src[p["name"]]) + "\n")
    return "\n".join(head) + "\n\n" + "\n".join(body)


if __name__ == "__main__":
    write = "--write" in sys.argv
    targets = [a for a in sys.argv[1:] if not a.startswith("--")] or list(CLONES)
    for clone in targets:
        toml = gen(clone)
        if write:
            with open(os.path.join(GAMES, clone + ".toml"), "w", encoding="utf-8", newline="\n") as fh:
                fh.write(toml)
            print(f"WROTE {clone}.toml ({len(toml)} bytes)")
        else:
            print(f"===== {clone}.toml =====\n{toml}\n")
