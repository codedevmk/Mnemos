# Genesis VDP Renderer — Cell-Span Rewrite + Interlace Sprite-Y Correctness

Status: Draft, awaiting review
Date: 2026-06-10

Plan for two renderer work items from the 2026-06-10 code-review session
(session https://claude.ai/code/session_01HmyB2cK6EQXgXUvRVZvekF): the
per-pixel scroll-plane refetch (~16x redundant pattern decodes in the
hottest path) and the interlace sprite-Y semantics (IM2 offset, IM1
scaling). This document is design + sequencing for sign-off; it implements
nothing.

---

## Genesis VDP renderer: cell-span scroll-plane rewrite + interlace sprite-Y correctness

### 1. Context and architecture

All work is in one chip: `mnemos::chips::video::genesis_vdp` (`/home/user/Mnemos/src/chips/video/genesis_vdp/genesis_vdp.cpp`, header `genesis_vdp.hpp`). The per-line render path is:

- `run_scanline()` (genesis_vdp.cpp:1133) → `render_scanline(line)` (genesis_vdp.cpp:1055), which renders plane B, plane A, window into 8-bit line buffers, composites planes by priority (lines 1069–1092), calls `render_sprites` (genesis_vdp.cpp:873), then resolves CRAM→RGB per pixel (lines 1096–1101). The framebuffer row is `display_line_for_field(line)` (genesis_vdp.cpp:710–712) — interlace fields interleave rows.
- `render_scroll_plane` (genesis_vdp.cpp:751–821) is the hot spot: the per-pixel loop at lines 777–820 does, **for every one of up to 320 pixels**: a per-column VSRAM lookup (778–787), a nametable `vram_read16` (807), and a full `fetch_pattern_row` (816) — 4 VRAM byte reads + decoding all 8 pixels (genesis_vdp.cpp:734–749) — to consume a single pixel (817). That is ~8× redundant nametable/pattern work per plane, ×2 planes ×224 lines ×60 fps.
- `render_window` (genesis_vdp.cpp:823–871) is the in-repo model for the fix: it iterates 8-pixel cells, does one nametable read + one `fetch_pattern_row` per cell, and writes 8 pixels with a `x < screen_w` clamp (lines 843–869).

Why a cell-span restructure is semantically safe:

- `hscroll` is fetched once per line before the loop (genesis_vdp.cpp:760–775) and is constant across the line.
- `vscroll` changes only at 16-screen-pixel boundaries in per-column mode (`(col >> 4) * 2 + (is_plane_a ? 0 : 1)`, line 780), or never (line 786).
- `px = (col + hscroll) mod hsz_px` advances by exactly 1 per pixel; `hsz_px ∈ {256, 512, 1024}` (multiples of 8, `scroll_size` at genesis_vdp.cpp:117–120), so the plane-width wrap always coincides with an 8-pixel cell boundary — a span bounded by cell boundaries can never wrap mid-span.
- `render_scroll_plane` is `const noexcept` and side-effect-free (unlike `render_sprites`, which sets `sprite_overflow_`/`sprite_collision_`), so old/new outputs can be compared line-by-line as pure functions of (VRAM, VSRAM, CRAM, registers, line, odd-field).

#### New algorithm (design)

Per line, after the existing hscroll fetch (keep lines 753–775 byte-for-byte):

1. `x = 0`. Loop while `x < screen_w`:
   - `span_end = min(screen_w, next_tile_boundary, next_vscroll_boundary)` where
     `next_tile_boundary = x + (8 - ((x + hscroll) mod 8))` (mod taken non-negative) and
     `next_vscroll_boundary = (x & ~15) + 16` only when `vscroll_per_column()`.
   - Resolve `vscroll` exactly as lines 778–787 do (including the dead-in-practice clamp at 781–783 — `col >> 4 ≤ 19` so `idx ≤ 39 < vsram_entries`, but preserve it verbatim).
   - Compute `px`, `py`, `cell_x`, `cell_y`, `fine_y`, `row` exactly as lines 789–814 (these are constant across the span by construction).
   - One `vram_read16(nt_base + nt_offset)`, one `fetch_pattern_row`, then write `pix[fine_x0 .. fine_x0 + len)` into `linebuf[x .. span_end)` with the same `(pal * 16 + color) | (pri ? pix_priority : 0)` packing (lines 818–819).
2. Worst case spans: 41 cells + up to 20 extra splits at vscroll boundaries when the fine hscroll offset ≠ 0 → ≤ 61 nametable/pattern fetches vs 320 today (~5–8× less fetch+decode work per plane line; the H40 two-plane case drops from 640 to ≤ 122 pattern decodes per line).

Equivalence argument (to be stated in the PR): within a span, every quantity the old per-pixel body computed (`vscroll`, `px div 8`, `py`, `cell_x/cell_y`, `fine_y`, `row`, the nametable word, the decoded 8-pixel row) is invariant, and `fine_x = px & 7` advances by 1 — so the per-pixel writes are byte-identical.

### 2. Item 1 verification strategy: pixel-equivalence proof

Three candidate mechanisms; recommendation is (A):

**(A) One-off differential harness as a permanent unit test (recommended).** A new test file `src/chips/video/genesis_vdp/tests/genesis_vdp_render_equiv_test.cpp` contains a *verbatim copy of the pre-rewrite renderer* (`render_scroll_plane`, `render_window`, `render_sprites`, the plane compositing + CRAM resolve of `render_scanline`, `fetch_pattern_row`, `cram_to_rgb` helpers) as free functions over a snapshot struct populated entirely through the existing public accessors: `vram16()`, `cram()`, `vsram()`, `reg()` (genesis_vdp.hpp:141–159), odd-field via status bit 4 (genesis_vdp.cpp:626). The test:
   - Seeds the chip via the public ports (the `set_reg`/`set_command`/`write_vram` helpers already in `tests/genesis_vdp_test.cpp:19–46`) with **deterministic seeded LCG randomness** (no `std::random_device` — determinism is a standing rule, AGENTS.md; ADR `docs/adr/proposed/0018-deterministic-config-not-env.md`): random VRAM (full 64 KB), random VSRAM (11-bit), **64 distinct CRAM values** (so RGB uniquely identifies the winning 6-bit pixel index post-composite), randomized R2/R3/R4/R5/R7/R11/R12/R13/R16/R17/R18 with M5 + display-enable forced on; corpus spans H32/H40, all H-scroll modes, per-column vscroll on/off, all plane sizes, S/H on/off, window configs, NTSC/PAL, V28/V30, interlace off/IM1/IM2, sprites present (random SAT).
   - For each config, ticks line-by-line (`tick(master_clocks_per_line)`) through a full field and compares each rendered framebuffer row (`framebuffer()`, row = `display_line`) against the in-test reference renderer, pixel-exact.
   - Because the comparison is at the post-composite RGB level, priority-bit bugs surface as the wrong layer winning, and shadow/highlight as the wrong shade — they are observable, not masked.
   - This harness lands in a PR **before** the rewrite and must pass against the unmodified code — proving the reference copy is faithful by construction. It then stays green across the rewrite PR and remains as a permanent property/regression oracle.

**(B) Keep the old path in the .cpp behind a temporary debug accessor.** Rejected as primary: it requires a new public seam on the chip and leaves two shipping render paths in the hot file during the transition.

**(C) Build flag selecting old/new path.** Rejected: trace-affecting behavior must come from manifest config, not env/build flags (ADR 0018; AGENTS.md "behaviour that alters the execution trace must not depend on host env" precedent at genesis_vdp.cpp:1613–1616), and a flag-gated dual path is exactly the scaffolding the constitution forbids.

**Golden boot backstop (data-gated):** `tests/golden/genesis_boot_test.cpp` hashes the framebuffer after a deterministic boot, gated on `MNEMOS_GENESIS_ROM` / asserted via `MNEMOS_GENESIS_BOOT_SHA256`. The rewrite PR's checklist: a maintainer with ROM data records the hash on master, re-runs on the branch, and confirms **bit-identical** hashes for at least one NTSC and one PAL title. This complements (does not replace) the randomized harness, since boots rarely exercise per-column vscroll or interlace.

**Perf evidence:** a before/after wall-clock of the golden-boot run (or a frames-per-second sample from `mnemos_player` per AGENTS.md scratch conventions) recorded in the PR description. No new benchmark infrastructure (none exists in-repo; `metrics/` is session telemetry).

### 3. Item 2: interlace sprite-Y semantics

Current code, `render_sprites` (genesis_vdp.cpp:873–934):

- `spr_y = (w0 & 0x03FF) - 128;` (line 899) — unconditional.
- `spr_h_px = spr_h * (il ? 16 : 8);` (line 912) — doubled height in **both** IM1 and IM2.
- Coverage test against `display_line` (= `2*line + odd` in interlace, line 914).
- `source_row = il2 ? row_in_sprite : (il ? (row_in_sprite >> 1) : row_in_sprite);` (lines 933–934).

Bugs and fixes:

1. **IM2:** SAT Y is a 10-bit coordinate in doubled-line space whose screen top is **256**, not 128. With `-128`, a sprite written at Y=256 (intended top of screen) lands at doubled line 128 = field line 64 — exactly the "64 visible lines too low" symptom (Sonic 2 two-player). Fix: `spr_y = (w0 & 0x3FF) - (il2 ? 256 : 128)`. Everything else in the IM2 path (doubled-space comparison, `spr_h * 16`, `source_row = row_in_sprite`, 16-row cells at lines 935–936) is already correct.
2. **IM1:** SAT Y stays single-resolution (field-line space, top = 128), heights are `spr_h * 8` field lines, and both fields show the same rows. The current code compares single-res `spr_y` against doubled `display_line` with doubled height — the sprite appears at half its intended Y, and `row_in_sprite >> 1` introduces field-parity texture jitter for odd `spr_y`. Fix: in IM1 perform coverage and row math in **field-line space** — compare against `line`, use `spr_h_px = spr_h * 8`, `source_row = row_in_sprite` — i.e., IM1 sprites become identical to the non-interlace path (the doubled framebuffer placement is already handled by `render_scanline`'s `display_line` row addressing at line 1096). Concretely the `il` ternaries at lines 912 and 934 collapse to `il2` ternaries plus a field-space comparison line.

**Tests (new TEST_CASEs in `tests/genesis_vdp_test.cpp`, same style as the existing sprite test at lines 406–424):**

- IM2 (`set_reg(12, 0x06)`; note `interlace_field()` = (reg12>>1)&3, genesis_vdp.hpp:315): SAT Y=256, 1×2-cell sprite of solid tiles → assert framebuffer rows 0..31 (doubled space) carry the sprite color at its X, row 32 does not; repeat with Y=256+2k to pin the doubled-line granularity; render two consecutive fields (odd_frame_ flips at VBL entry, genesis_vdp.cpp:1202) and assert even rows come from the even field, odd rows from the odd field with the correct 16-row pattern rows (true 8×16 consumption).
- IM1 (`set_reg(12, 0x02)`): SAT Y=128+k → sprite covers framebuffer rows 2k..2k+2·8·spr_h−1, with **identical pattern rows in both fields** (interlace-1 repeats the image, comment at genesis_vdp.cpp:719–720); a vertical-flip case to pin `row_in_sprite` inversion against `spr_h_px = spr_h*8`.
- A non-interlace control case asserting unchanged behavior (already covered by the phase-0 harness, but a directed case documents intent).

**Parallel interlace audit (same pass, investigation first):** while studying the path I found these adjacent suspects — they belong in the audit phase, each needing hardware-reference confirmation before changing:

- **IM2 H-scroll table indexing:** `render_scroll_plane` indexes the hscroll table by `source_line` (genesis_vdp.cpp:762–766), which in IM2 is the **doubled** line (0..447, field-parity dependent): per-line mode reads up to offset 1788 (beyond the 896-byte table games allocate) and per-cell mode `(source_line & ~7)*4` changes every 4 field lines. Hardware fetches H-scroll per raster line of the field; the index should very likely be the field line.
- **IM2 VSRAM units:** `py = (vscroll + source_line) % vsz_px` (line 795) treats the raw 11-bit VSRAM value as doubled-line units; confirm against hardware docs whether IM2 interprets VSRAM bits differently (bit-0 semantics).
- **SAT Y/X masks outside IM2:** hardware effectively uses 9-bit Y (`& 0x1FF`) and 9-bit X in non-IM2 modes; the code masks `0x3FF` for both (lines 899, 910). Changing this affects non-interlace output → golden-hash risk → needs explicit sign-off.

### 4. Phased sequencing (PR-sized)

**Phase 0 — Differential render harness (test-only PR).**
Files: new `src/chips/video/genesis_vdp/tests/genesis_vdp_render_equiv_test.cpp`; add to `SOURCES` in `src/chips/video/genesis_vdp/CMakeLists.txt:19–27`. No production changes.
Proves: the in-test verbatim reference renderer matches production pixel-exactly over the randomized corpus (harness fidelity by construction), and pins current behavior — including the current (buggy) interlace sprite semantics — as the baseline oracle.

**Phase 1 — Cell-span rewrite of `render_scroll_plane` (production PR).**
Files: `genesis_vdp.cpp` only (lines 751–821 restructured; no header change, no state change, no save-state impact — rendering reads state only).
Proves: Phase-0 harness stays green unchanged (the pixel-equivalence proof — the test predates the rewrite and encodes the old per-pixel algorithm); golden boot hash bit-identical (maintainer runs the data-gated `tests/golden/genesis_boot_test.cpp` before/after); perf delta recorded in the PR. The PR description includes the per-span invariance argument from §1.

**Phase 2 — Interlace sprite-Y fixes + directed IM tests (production PR).**
Files: `genesis_vdp.cpp` (lines 899, 912, 914, 933–934 region); `tests/genesis_vdp_test.cpp` (new IM1/IM2 TEST_CASEs); `tests/genesis_vdp_render_equiv_test.cpp` reference sprite code updated **in lockstep** (the directed unit tests are the independent oracle for the semantic change; the harness then re-pins the new semantics).
Proves: IM2 sprites at SAT Y=256 render at screen top; IM1 sprites render at field-line Y with identical fields; non-interlace output unchanged (harness corpus configs with interlace off must be bit-identical to phase 1); golden boot hashes unchanged (boot screens are non-interlaced).

**Phase 3 — Interlace plane audit (investigation PR, fixes only if confirmed).**
Scope: the three suspects in §3 (IM2 hscroll indexing, IM2 VSRAM units, SAT Y/X masks), each backed by a hardware reference (behavioral references only — no code lifting; acknowledge sources in `THIRD-PARTY.md` per AGENTS.md). Each confirmed fix gets a directed TEST_CASE and a lockstep harness-reference update, as in phase 2.
Proves: every interlace plane-path change is individually pinned by a directed test and leaves non-interlace harness configs bit-identical.

Sequencing rule: **never change semantics and structure in the same PR** — phase 1 is structure-only under the unchanged-semantics oracle; phases 2–3 are semantics-only with directed tests as the new oracle.

### 5. Other hot-path / correctness observations (candidate follow-ups, explicitly out of scope)

1. **Per-line SAT re-walk:** `render_sprites` walks up to 80 SAT entries with 4 `vram_read16` each on every line (genesis_vdp.cpp:892–897), fetching all four words even for off-line sprites. Hardware does a Y-only pre-scan; fetching `w0`/`w1` first and deferring `w2`/`w3` (or a per-frame Y-cache rebuilt on SAT writes — must be a *view*, not storage, per `OPTIMIZATIONS.md`) is the next hot-path win.
2. **Per-pixel `cram_to_rgb`:** the final resolve (genesis_vdp.cpp:1097–1101) recomputes the 5:6:5 pack per pixel; a 64×3-shade LUT rebuilt on `cram_write` would be a legitimate derived view.
3. **Sprite-overflow early `break`** (line 920) abandons SAT evaluation entirely, which also skips later X=0 mask sprites; hardware semantics differ subtly.
4. **H-scroll mode 1** (prohibited mode) is treated as full-screen (genesis_vdp.cpp:768–770); hardware behaves like a masked per-line fetch.
5. **Per-column vscroll first-column quirk** (hardware uses an anomalous value for the left partial column when fine hscroll ≠ 0) is unmodelled; the rewrite must preserve the current column-0 behavior, not "fix" it.
6. **FIFO empty/full status bits** are stubbed always-empty (genesis_vdp.cpp:611–616) — already flagged in-code as a follow-up.

### 6. Open questions needing human sign-off

1. **SAT Y/X 9-bit masking outside IM2** (phase 3): changes non-interlace behavior → golden hashes could move for games that park sprites at Y≥512. Include or defer?
2. **IM2 hscroll/VSRAM indexing semantics** (phase 3): which hardware references are authoritative for sign-off, given the no-GPL-code / behavioral-reference-only rule?
3. **Harness lifetime:** keep `genesis_vdp_render_equiv_test.cpp` permanently as the renderer's property oracle (recommended), or delete after phase 2?
4. **Golden-data execution:** who runs the data-gated boot tests (ROMs never committed), and can an interlace-exercising title (Sonic 2, two-player VS) be added to the local golden set to validate phase 2 end-to-end?
5. **Corpus breadth vs runtime:** proposed ~32 seeded configs × full field per config in the harness; acceptable CI budget?

### 7. Non-goals

- No sprite SAT caching/pre-scan, no CRAM→RGB LUT, no compositing-loop vectorization (follow-ups §5).
- No FIFO status-bit modelling, no H-scroll mode-1 semantics, no per-column-vscroll left-column hardware quirk, no window-boundary hscroll artifact.
- No changes to save-state format, introspection surfaces (`plane_layer_impl`, asset extraction at genesis_vdp.cpp:1851–1920), timing/IRQ/DMA paths, or the scheduler.
- No build flag or runtime config selecting between render paths.

### Critical Files for Implementation

- /home/user/Mnemos/src/chips/video/genesis_vdp/genesis_vdp.cpp
- /home/user/Mnemos/src/chips/video/genesis_vdp/genesis_vdp.hpp
- /home/user/Mnemos/src/chips/video/genesis_vdp/tests/genesis_vdp_test.cpp
- /home/user/Mnemos/src/chips/video/genesis_vdp/CMakeLists.txt
- /home/user/Mnemos/tests/golden/genesis_boot_test.cpp
