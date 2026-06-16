#!/usr/bin/env python3
"""Generate Mnemos game.toml files for clean CPS1 clone sets (parent-merge).

A clone's zip carries only its unique dumps; the shared dumps come from the
parent set's zip at load time (the parent-merge loader). This transcribes each
clone's region/file layout from the reference ROM_START tables, names each file
by the entry name in whichever local zip actually holds it (clone zip for the
unique dumps, parent zip for the shared ones -- their names differ), and reuses
the parent's CPS-B profile when the clone's board config matches the parent's.

Only emits a clone when: every CRC in the clone's zip belongs to the reference
set (no unofficial dumps), every shared dump is present in the parent's local
zip, the parent is already ported, and the clone's CPS-B config == the parent's
(so the profile id is reused -- clones needing a NEW profile are reported, not
emitted). Transcription aid, not part of the build.

Usage: python scripts/gen_cps1_clone_tomls.py [--write]
"""
import os, re, sys, zipfile

TMP = r"C:/Users/mkrol/AppData/Local/Temp"
MAME = open(TMP + "/mame_cps1.cpp", encoding="utf-8", errors="replace").read()
MAMEV = open(TMP + "/mame_cps1_v.cpp", encoding="utf-8", errors="replace").read()
ZIPDIR = r"D:/emu/capcom/cps1"
GAMES = r"C:/dev/emu/Mnemos-qsboard/src/manifests/capcom_cps1/games"
WRITE = "--write" in sys.argv
HAVE = set(os.path.splitext(f)[0] for f in os.listdir(GAMES) if f.endswith(".toml"))

# Cross-board profile map: a clone may run a DIFFERENT CPS-B board than its
# parent (the sf2 World/US/JP variants run CPS_B_05/12/13/14/15/17 on the same
# S9263B/STF29 gfx mapper as the CPS_B_11 parent). Map (cpsb, mapper) -> the
# Mnemos profile id that encodes exactly that board+mapper. STF29 and S9263B are
# byte-identical mappers, so both spellings map to the same profile.
CONFIG_PROFILE = {
    ("CPS_B_05", "mapper_STF29"): 45, ("CPS_B_05", "mapper_S9263B"): 45,
    ("CPS_B_12", "mapper_STF29"): 46, ("CPS_B_12", "mapper_S9263B"): 46,
    ("CPS_B_13", "mapper_STF29"): 47, ("CPS_B_13", "mapper_S9263B"): 47,
    ("CPS_B_14", "mapper_STF29"): 48, ("CPS_B_14", "mapper_S9263B"): 48,
    ("CPS_B_15", "mapper_STF29"): 49, ("CPS_B_15", "mapper_S9263B"): 49,
    ("CPS_B_17", "mapper_STF29"): 50, ("CPS_B_17", "mapper_S9263B"): 50,
    # Regional clones that reuse an existing profile: varthu's VA63B PAL is
    # byte-identical to varth's VA24B (profile 104); wofa runs the stock DEF board
    # (profile 29) rather than its parent wof's QS1.
    ("CPS_B_04", "mapper_VA63B"): 104,
    ("CPS_B_21_DEF", "mapper_TK263B"): 29,
    # The QSound "QS1" board (wofu/wofj). The wof parent boots on a DEF profile
    # (29) because its program reads the multiply-protection, but the US/JP
    # revisions read the QS1 protection and need the QS1 profile (40) -- verified
    # empirically: wofu/wofj render only on 40, wof only on 29.
    ("CPS_B_21_QS1", "mapper_TK263B"): 40,
}

# Bootleg/hack boards keyed by the FULL config tuple (they share (cpsb, mapper)
# with a stock board and differ only in the trailing bootleg_kludge field, so the
# (cpsb, mapper) map above cannot tell them apart). Profile 51 carries the kludge
# (the sf2 koryu/m5/m7/yyc hacks). NOTE: the in2='0x36' element is incidental
# input-mux wiring, not what selects the video kludge -- a same-board hack with a
# different in2 would fall through to "needs new profile" (a safe skip, never a
# mis-emit); add its own row pointing at 51 if one turns up.
KLUDGE_PROFILE = {
    ("CPS_B_21_DEF", "mapper_S9263B", "0x36", "0", "0", "0x41"): 51,
}

# clone -> parent
parent = {}
for m in re.finditer(r"^GAME\(\s*\d+,\s*(\w+),\s*(\w+),", MAME, re.M):
    parent[m.group(1)] = None if m.group(2) == "0" else m.group(2)

# cps1_config_table: set -> FULL field tuple after the name (cpsb, mapper, in2,
# in3, out2, bootleg_kludge...). Capture every comma-separated field up to the
# closing brace, not just (cpsb, mapper): the trailing in2/in3/out2/kludge fields
# distinguish bootleg boards from their parent and MUST gate profile reuse.
config = {}
for m in re.finditer(r'\{\s*"(\w+)"\s*,([^}]*)\}', MAMEV):
    config[m.group(1)] = tuple(f.strip() for f in m.group(2).split(",") if f.strip())

# ROM_START blocks -> ordered region/file layout
def parse_sets():
    out = {}
    for m in re.finditer(r"ROM_START\(\s*(\w+)\s*\)(.*?)ROM_END", MAME, re.S):
        name, body = m.group(1), m.group(2)
        regions, cur = [], None
        for line in body.splitlines():
            rg = re.search(r'ROM_REGION\(\s*(\w+|0x[0-9A-Fa-f]+)\s*,\s*"(\w+)"', line)
            if rg:
                cur = {"name": rg.group(2), "size": rg.group(1), "files": []}
                regions.append(cur)
                continue
            ld = re.search(r'(ROM_LOAD\w*)\(\s*"([^"]+)"\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*,\s*CRC\((\w+)\)', line)
            if ld and cur is not None:
                cur["files"].append({"macro": ld.group(1), "fname": ld.group(2),
                                     "off": int(ld.group(3), 16), "len": int(ld.group(4), 16),
                                     "crc": int(ld.group(5), 16), "cont": []})
                continue
            ct = re.search(r'ROM_CONTINUE\(\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\)', line)
            if ct and cur is not None and cur["files"]:
                cur["files"][-1]["cont"].append((int(ct.group(1), 16), int(ct.group(2), 16)))
        out[name] = regions
    return out

SETS = parse_sets()
REGION_MAP = {"maincpu": "maincpu", "gfx": "gfx", "audiocpu": "audiocpu", "oki": "oki"}

def zip_names(path):
    with zipfile.ZipFile(path) as z:
        return {i.CRC: i.filename for i in z.infolist() if not i.is_dir()}

def emit_file(f, name):
    """One [[region.file]] block; layout from the load macro."""
    lines = [f'name   = "{name}"']
    fsize = f["len"] + sum(c[1] for c in f["cont"])  # total file bytes
    if f["cont"]:  # sliced (audiocpu): load + continue(s)
        out = []
        out.append("\n".join(["[[region.file]]", f'name   = "{name}"',
                              f"offset = 0x{f['off']:x}", "source_offset = 0x0",
                              f"length        = 0x{f['len']:x}", f"size   = 0x{fsize:x}",
                              f"crc32  = 0x{f['crc']:08x}"]))
        soff = f["len"]
        for (coff, clen) in f["cont"]:
            out.append("\n".join(["[[region.file]]", f'name   = "{name}"',
                                  f"offset = 0x{coff:x}", f"source_offset = 0x{soff:x}",
                                  f"length        = 0x{clen:x}", f"size   = 0x{fsize:x}",
                                  f"crc32  = 0x{f['crc']:08x}"]))
            soff += clen
        return "\n\n".join(out)
    lines = ["[[region.file]]", f'name   = "{name}"', f"offset = 0x{f['off']:x}"]
    if f["macro"] == "ROM_LOAD16_BYTE":
        lines.append("stride = 2")
    elif f["macro"] == "ROM_LOAD16_WORD_SWAP":
        lines += ["unit   = 2", "stride = 2", "swap   = true"]
    elif f["macro"] == "ROM_LOAD64_WORD":
        lines += ["unit   = 2", "stride = 8"]
    # plain ROM_LOAD (oki) = linear
    lines += [f"size   = 0x{fsize:x}", f"crc32  = 0x{f['crc']:08x}"]
    return "\n".join(lines)

def gen(clone):
    cand = clone if clone in SETS else None
    # the reference set name may differ from the local zip name; the classifier
    # mapped by CRC -- redo that here to find the reference set.
    zc = zip_names(os.path.join(ZIPDIR, clone + ".zip"))
    if cand is None:
        cand = max(SETS, key=lambda s: len(set(zc) & {f["crc"] for r in SETS[s] for f in r["files"]}), default=None)
    par = parent.get(cand)
    if par is None or par not in HAVE:
        return None, f"parent '{par}' not ported"
    # Reuse the parent's profile ONLY if the clone's FULL board config (incl.
    # in2/in3/out2/bootleg_kludge) matches the parent's. A missing table entry
    # is not "matches" -- skip it (the bootleg sets that differ only in the
    # trailing kludge need a profile Mnemos does not model yet).
    ccfg, pcfg = config.get(cand), config.get(par)
    if ccfg is None or pcfg is None:
        return None, f"config unknown: clone {ccfg} parent {pcfg}"
    ptoml = open(os.path.join(GAMES, par + ".toml"), encoding="utf-8").read()
    # The parent must itself be standalone -- the fallback is single-level.
    if re.search(r"^\s*parent\s*=", ptoml, re.M):
        return None, f"parent '{par}' is itself a clone (grandparent chains unsupported)"
    # Pick the profile id. Identical board config -> reuse the parent's. A
    # different CPS-B board but the SAME trailing I/O fields (in2/in3/out2/kludge)
    # -> the board-specific profile from CONFIG_PROFILE (the sf2 regional case).
    # Any other difference (e.g. a bootleg kludge) -> skip, needs a new profile.
    if ccfg in KLUDGE_PROFILE:
        # A bootleg board distinguished by its full config (incl. bootleg_kludge).
        profile = str(KLUDGE_PROFILE[ccfg])
    elif (ccfg[0], ccfg[1]) in CONFIG_PROFILE and ccfg[2:] == pcfg[2:]:
        # The clone's board has a known Mnemos profile -> use it directly. This is
        # authoritative even when the parent toml is profiled differently (the
        # QSound parent wof boots on a DEF profile, but its QS1 clones need the QS1
        # profile). The trailing I/O fields must still match the parent so an
        # unmodelled bootleg kludge cannot slip through.
        profile = str(CONFIG_PROFILE[(ccfg[0], ccfg[1])])
    elif ccfg == pcfg:
        pm = re.search(r"cps_b_profile = (\d+)", ptoml)
        if not pm:
            return None, "parent has no cps_b_profile"
        profile = pm.group(1)
    else:
        return None, f"config {ccfg} != parent {pcfg} (needs new profile/kludge)"
    # whitespace-tolerant: parent tomls vary in key alignment (some are wide-padded)
    orient = re.search(r'^\s*orientation\s*=\s*"(\w+)"', ptoml, re.M)
    psound = re.search(r'^\s*sound\s*=\s*"(\w+)"', ptoml, re.M)
    pkabuki = re.search(r'^\s*kabuki\s*=\s*"(\w+)"', ptoml, re.M)
    par_names = zip_names(os.path.join(ZIPDIR, par + ".zip"))
    # build toml regions
    body = []
    for rg in SETS[cand]:
        rname = REGION_MAP.get(rg["name"])
        if rname is None:
            continue  # skip aboardplds/bboardplds/cboardplds
        size = "0x180000" if rname == "maincpu" else rg["size"]
        body.append(f"[[region]]\nname = \"{rname}\"\nsize = {size}\n")
        for f in rg["files"]:
            # name by whichever local zip holds this CRC (clone wins)
            nm = zc.get(f["crc"]) or par_names.get(f["crc"])
            if nm is None:
                return None, f"CRC {f['crc']:08x} ({f['fname']}) in neither zip"
            body.append(emit_file(f, nm) + "\n")
    head = [f"# {clone} -- Capcom CPS1 clone of {par} (parent-merge). Generated by",
            f"# scripts/gen_cps1_clone_tomls.py; CRCs from the reference tables, file names",
            f"# from the local zips (clone-unique from {clone}.zip, shared from {par}.zip).",
            "", "[set]", 'schema = "mnemos-romset/1"', f'name   = "{clone}"',
            'board  = "capcom_cps1"', f'parent = "{par}"', f"cps_b_profile = {profile}"]
    if psound:
        head.append(f'sound  = "{psound.group(1)}"')
    if pkabuki:
        head.append(f'kabuki = "{pkabuki.group(1)}"')
    if orient and orient.group(1) == "vertical":
        head.append('orientation = "vertical"')
    return "\n".join(head) + "\n\n" + "\n".join(body), None

if __name__ == "__main__":
    targets = sys.argv[1:]
    targets = [t for t in targets if not t.startswith("--")]
    if not targets:
        print("usage: gen_cps1_clone_tomls.py <clone> [<clone>...] [--write]")
        sys.exit(0)
    for clone in targets:
        toml, err = gen(clone)
        if err:
            print(f"SKIP {clone}: {err}")
            continue
        if WRITE:
            with open(os.path.join(GAMES, clone + ".toml"), "w", encoding="utf-8", newline="\n") as fh:
                fh.write(toml)
            print(f"WROTE {clone}.toml")
        else:
            print(f"===== {clone}.toml =====\n{toml}")
