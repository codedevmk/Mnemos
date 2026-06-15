#!/usr/bin/env python3
"""Transcribe CPS1 per-game ROM-set declarations from the reference core.

A CPS1 game is a SET of dump files composed into named regions in board-
specific ways (even/odd 8-bit program pairs, a word-swapped 16-bit program
half, 16-bit graphics ROMs interleaved into 64-bit tile words, a sliced sound
program, OKIM6295 samples). Rather than hand-type dozens of game.toml files
(each an error-prone offset/size/lane table), this aid parses the reference Emu
C core's `cps1_zip_part_t` part tables + the `k_cps1_zip_rom_sets` descriptor
table and emits one src/manifests/capcom_cps1/games/<name>.toml per game,
pulling each file's CRC32 + size from the local ROM zip.

Only games the Mnemos CPS1 board can actually run are emitted: QSound and
Kabuki-encrypted-sound titles are skipped (no DSP16 / no z80 opcode-split
yet), as are timekeeper-RTC titles. A game is skipped if its local zip is
missing or lacks a named file, so every emitted set is fully CRC-verifiable.

Layout -> rom_set_file mapping (mirrors the loader in manifests/common):
  LINEAR          -> offset
  BYTE_INTERLEAVE -> offset, stride
  WORD_SWAP       -> offset, unit 2, stride 2, swap
  GFX_WORD_LANE   -> offset, unit 2, stride 8
  GFX_BYTE_LANE   -> offset, unit 1, stride 8
  (a SLICE adds source_offset + length)

It is a transcription aid, not part of the build: the reference cps1.c + the
ROM zips live in separate local checkouts, so this does not run in CI. Re-run
it (no clang-format needed -- TOML) when the reference tables change or to
extend coverage.

Usage:
    python scripts/gen_cps1_game_tomls.py [path/to/reference/cps1.c] [path/to/rom/zips]
"""
import os
import re
import sys
import zipfile

DEFAULT_SRC = r"C:/Users/mkrol/source/repos/Emu/Emu/systems/capcom/cps1/cps1.c"
DEFAULT_ZIPS = r"D:/emu/capcom/cps1"
SRC = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_SRC
ZIPS = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_ZIPS
OUT_DIR = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "src", "manifests", "capcom_cps1", "games")
)

# Emu region enum -> Mnemos romset region name. Regions absent here (QSOUND,
# TIMEKEEPER) mean the board cannot run the game -> skip it.
REGION_TOML = {
    "CPS1_ZIP_REGION_MAIN": "maincpu",
    "CPS1_ZIP_REGION_SOUND": "audiocpu",
    "CPS1_ZIP_REGION_OKI": "oki",
    "CPS1_ZIP_REGION_GFX": "gfx",
}
REGION_ORDER = ["maincpu", "gfx", "audiocpu", "oki"]


def num(tok):
    """Evaluate a CPS1_KIB(n) / hex / decimal integer literal."""
    t = tok.strip()
    m = re.match(r"CPS1_KIB\(\s*(\d+)\s*\)", t)
    if m:
        return int(m.group(1)) * 1024
    return int(t.rstrip("uUlL"), 0)


def split_top(s):
    """Split on top-level commas, respecting nested braces and string literals."""
    out, depth, cur, instr = [], 0, "", False
    i = 0
    while i < len(s):
        ch = s[i]
        if instr:
            cur += ch
            if ch == "\\" and i + 1 < len(s):
                cur += s[i + 1]
                i += 2
                continue
            if ch == '"':
                instr = False
        elif ch == '"':
            instr = True
            cur += ch
        elif ch == "{":
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
        i += 1
    if cur.strip():
        out.append(cur)
    return [t for t in out if t.strip()]


def unquote(tok):
    return tok.strip().strip('"')


with open(SRC, "r", encoding="utf-8", errors="replace") as _f:
    src = _f.read()

# profile enum: suffix-name -> numeric id (CPS1_CPS_B_PROFILE_<suffix> = <n>)
enum_block = re.search(r"typedef enum cps1_cps_b_profile\s*\{(.*?)\}", src, re.S).group(1)
prof_id = {
    m.group(1): int(m.group(2))
    for m in re.finditer(r"CPS1_CPS_B_PROFILE_(\w+)\s*=\s*(\d+)", enum_block)
}

def macro_args(text, start):
    """Return (args_string, end_index) for a call whose '(' is at `start`,
    scanning to the matching ')' (so nested CPS1_KIB(n) parens are kept whole)."""
    depth, j = 0, start
    while j < len(text):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return text[start + 1 : j], j + 1
        j += 1
    raise ValueError("unbalanced macro call")


# part tables: array-name -> [part dicts]
part_tables = {}
for m in re.finditer(r"k_cps1_(\w+?)_parts\[\]\s*=\s*\{(.*?)\};", src, re.S):
    parts = []
    body = m.group(2)
    for p in re.finditer(r"CPS1_ZIP_PART_(FULL|SLICE)\s*\(", body):
        kind = p.group(1)
        args_str, _ = macro_args(body, p.end() - 1)
        a = [x.strip() for x in args_str.split(",")]
        if kind == "FULL":
            # entry, region, layout, source_size, dest_offset, dest_stride
            entry, region, layout, ssize, doff, dstride = a[:6]
            soff, csize = "0", "0"
        else:
            # entry, region, layout, source_size, source_offset, copy_size, dest_offset, dest_stride
            entry, region, layout, ssize, soff, csize, doff, dstride = a[:8]
        parts.append(
            dict(
                entry=unquote(entry),
                region=region.strip(),
                layout=layout.strip(),
                source_size=num(ssize),
                source_offset=num(soff),
                copy_size=num(csize),
                dest_offset=num(doff),
                dest_stride=num(dstride),
            )
        )
    part_tables[m.group(1)] = parts

# descriptor table: list of field-lists
table_body = re.search(r"k_cps1_zip_rom_sets\[\]\s*=\s*\{(.*)\n\};", src, re.S).group(1)
descriptors = []
for block in split_top(table_body):
    b = block.strip()
    if not b.startswith("{"):
        continue
    fields = split_top(b[1:b.rfind("}")])
    descriptors.append([f.strip() for f in fields])


def layout_fields(layout, dest_offset, dest_stride):
    """Map a reference layout to rom_set_file (offset,unit,stride,swap) extras."""
    if layout == "CPS1_ZIP_LAYOUT_LINEAR":
        return {}
    if layout == "CPS1_ZIP_LAYOUT_BYTE_INTERLEAVE":
        return {"stride": dest_stride}
    if layout == "CPS1_ZIP_LAYOUT_WORD_SWAP":
        return {"unit": 2, "stride": 2, "swap": True}
    if layout == "CPS1_ZIP_LAYOUT_GFX_WORD_LANE":
        return {"unit": 2, "stride": 8}
    if layout == "CPS1_ZIP_LAYOUT_GFX_BYTE_LANE":
        return {"unit": 1, "stride": 8}
    raise ValueError(f"unhandled layout {layout}")


def zip_crcs(path):
    """name -> (crc32, uncompressed size) for every entry in the zip."""
    with zipfile.ZipFile(path) as z:
        return {i.filename: (i.CRC, i.file_size) for i in z.infolist()}


def emit_toml(short_name, desc, profile_id, vertical, region_sizes, region_parts, crcs):
    lines = []
    lines.append(f"# {desc} -- Capcom CPS1. Generated by scripts/gen_cps1_game_tomls.py")
    lines.append("# (transcribed from the reference part tables; CRCs from the local ROM zip).")
    lines.append("")
    lines.append("[set]")
    lines.append('schema = "mnemos-romset/1"')
    lines.append(f'name   = "{short_name}"')
    lines.append('board  = "capcom_cps1"')
    if profile_id:
        lines.append(f"cps_b_profile = {profile_id}")
    if vertical:
        lines.append('orientation = "vertical"')
    for region in REGION_ORDER:
        parts = region_parts.get(region)
        if not parts:
            continue
        lines.append("")
        lines.append("[[region]]")
        lines.append(f'name = "{region}"')
        lines.append(f"size = {hex(region_sizes[region])}")
        for p in parts:
            extra = layout_fields(p["layout"], p["dest_offset"], p["dest_stride"])
            lines.append("")
            lines.append("[[region.file]]")
            lines.append(f'name   = "{p["entry"]}"')
            lines.append(f"offset = {hex(p['dest_offset'])}")
            if "unit" in extra:
                lines.append(f"unit   = {extra['unit']}")
            if "stride" in extra:
                lines.append(f"stride = {extra['stride']}")
            if extra.get("swap"):
                lines.append("swap   = true")
            if p["copy_size"]:  # a slice of a larger dump
                lines.append(f"source_offset = {hex(p['source_offset'])}")
                lines.append(f"length        = {hex(p['copy_size'])}")
            lines.append(f"size   = {hex(p['source_size'])}")
            lines.append(f"crc32  = 0x{crcs[p['entry']][0]:08x}")
    return "\n".join(lines) + "\n"


os.makedirs(OUT_DIR, exist_ok=True)
emitted, skipped = [], []
for d in descriptors:
    short_name = unquote(d[0])
    desc = unquote(d[1])
    sizes = {
        "maincpu": num(d[2]),
        "audiocpu": num(d[3]),
        "oki": num(d[4]),
        "gfx": num(d[6]),
    }
    qsound_size = num(d[5])
    profile_name = d[7].replace("CPS1_CPS_B_PROFILE_", "").strip()
    vertical = "ROTATE" in d[8]  # CPS1_SCREEN_ORIENTATION_ROTATE_{CW,CCW}
    sound_decode = d[15].strip() if len(d) > 15 else "CPS1_SOUND_DECODE_NONE"
    parts_ref = re.search(r"k_cps1_(\w+?)_parts", d[13])
    parts = part_tables.get(parts_ref.group(1)) if parts_ref else None

    if qsound_size or sound_decode != "CPS1_SOUND_DECODE_NONE":
        skipped.append((short_name, "qsound/kabuki sound"))
        continue
    if not parts:
        skipped.append((short_name, "no parts table"))
        continue
    if any(p["region"] not in REGION_TOML for p in parts):
        skipped.append((short_name, "needs an unsupported region (qsound/timekeeper)"))
        continue

    zip_path = os.path.join(ZIPS, short_name + ".zip")
    if not os.path.isfile(zip_path):
        skipped.append((short_name, "no local zip"))
        continue
    crcs = zip_crcs(zip_path)
    missing = sorted({p["entry"] for p in parts if p["entry"] not in crcs})
    if missing:
        skipped.append((short_name, f"zip missing {len(missing)} file(s): {missing[:3]}"))
        continue

    profile_id = prof_id.get(profile_name, 0)
    region_parts = {}
    for p in parts:
        region_parts.setdefault(REGION_TOML[p["region"]], []).append(p)

    text = emit_toml(short_name, desc, profile_id, vertical, sizes, region_parts, crcs)
    with open(os.path.join(OUT_DIR, short_name + ".toml"), "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    emitted.append(short_name)

print(f"emitted {len(emitted)} game.toml(s): {sorted(emitted)}")
print(f"skipped {len(skipped)}:")
for name, why in sorted(skipped):
    print(f"  {name}: {why}")
