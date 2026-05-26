/*
 * ym2612.c — Yamaha YM2612 (OPN2) FM Synthesizer
 *
 * Internal architecture:
 *   Phase Generator → Sine (log scale) → + Envelope (log) → Exp → Linear output
 *   Operators connected per algorithm topology, with feedback on Op1.
 *
 * Clock / sample-rate relationships on Genesis NTSC:
 *   master clock        = 53.69 MHz
 *   YM2612 input clock  = master / 7                ≈ 7.67 MHz
 *   chip /6 prescaler   = master / 7 / 6            ≈ 1.28 MHz machine cycle
 *   per-channel cost    = 4 machine cycles
 *   sample period       = 6 channels × 4 = 24 machine cycles
 *                       = 144 chip-input cycles
 *                       = 1008 master clocks
 *   sample rate         = master / 1008            ≈ 53.27 kHz
 * One full stereo sample is produced per ym2612_update() call.
 */

#include "emu_ym2612.h"
#include <string.h>
#include <math.h>

#ifndef YM2612_PI
#define YM2612_PI 3.14159265358979323846
#endif

/* ═══════════════════════════════════════════════════════════════════
 *  Lookup tables — computed once at init
 * ═══════════════════════════════════════════════════════════════════ */

/* Sine table: 10-bit phase → 12-bit log-sin output.
 * Only the first quadrant (256 entries) is stored; symmetry
 * handles the rest.  Value range: 0 (full volume) to ~2137. */
static uint16_t sin_table[1024];

/* Exponential table: 10-bit log input → 12-bit linear output.
 * Converts the combined (sin + envelope) attenuation back to a
 * linear amplitude.  exp_table[x] ≈ 2^13 × 2^(-x/256). */
static uint16_t exp_table[1024];

/* Detune table: indexed by [key_code][detune & 3].
 * Key code = block*2 + fnum_msb.  Returns phase increment delta. */
/* ═══════════════════════════════════════════════════════════════════
 *  DT1 detune table (polish.md §2)
 *
 *  Source-backed hardware values from the YM2608 Application Manual
 *  DT1 specification. Table indexed as dt1_table[kc & 31][dt & 3].
 *  The DT field's bit 2 selects sign (positive for dt 0-3, negative
 *  for dt 4-7 which is handled by the caller as inc +/-= delta).
 *  Identical to the table used by MAME, BlastEm, and Nuked-OPN2 —
 *  replaces an earlier linear approximation that was ~monotonic but
 *  diverged from hardware above kc = ~12.
 * ═══════════════════════════════════════════════════════════════════ */
static const uint8_t dt1_table[32][4] = {
    /* kc    dt=0 dt=1 dt=2 dt=3 */
    /*  0 */ {  0,   0,   1,   2 },
    /*  1 */ {  0,   0,   1,   2 },
    /*  2 */ {  0,   0,   1,   2 },
    /*  3 */ {  0,   0,   1,   2 },
    /*  4 */ {  0,   1,   2,   2 },
    /*  5 */ {  0,   1,   2,   3 },
    /*  6 */ {  0,   1,   2,   3 },
    /*  7 */ {  0,   1,   2,   3 },
    /*  8 */ {  0,   1,   2,   4 },
    /*  9 */ {  0,   1,   3,   4 },
    /* 10 */ {  0,   1,   3,   4 },
    /* 11 */ {  0,   1,   3,   5 },
    /* 12 */ {  0,   2,   4,   5 },
    /* 13 */ {  0,   2,   4,   6 },
    /* 14 */ {  0,   2,   4,   6 },
    /* 15 */ {  0,   2,   5,   7 },
    /* 16 */ {  0,   2,   5,   8 },
    /* 17 */ {  0,   3,   6,   8 },
    /* 18 */ {  0,   3,   6,   9 },
    /* 19 */ {  0,   3,   7,  10 },
    /* 20 */ {  0,   4,   8,  11 },
    /* 21 */ {  0,   4,   8,  12 },
    /* 22 */ {  0,   4,   9,  13 },
    /* 23 */ {  0,   5,  10,  14 },
    /* 24 */ {  0,   5,  11,  16 },
    /* 25 */ {  0,   6,  12,  17 },
    /* 26 */ {  0,   6,  13,  19 },
    /* 27 */ {  0,   7,  14,  20 },
    /* 28 */ {  0,   8,  16,  22 },
    /* 29 */ {  0,   8,  16,  22 },
    /* 30 */ {  0,   8,  16,  22 },
    /* 31 */ {  0,   8,  16,  22 },
};

/* LFO divider table: number of samples between 7-bit counter ticks. */
static const uint8_t lfo_divider_table[8] = {108, 77, 71, 67, 62, 44, 8, 5};

/* Vibrato lookup: indexed by PMS and the folded 3-bit LFO FM phase. */
static const uint8_t lfo_pm_table[8][8] = {
    { 0,  0,  0,  0,  0,  0,  0,  0},
    { 0,  0,  0,  0,  4,  4,  4,  4},
    { 0,  0,  0,  4,  4,  4,  8,  8},
    { 0,  0,  4,  4,  8,  8, 12, 12},
    { 0,  0,  4,  8,  8,  8, 12, 16},
    { 0,  0,  8, 12, 16, 16, 20, 24},
    { 0,  0, 16, 24, 32, 32, 40, 48},
    { 0,  0, 32, 48, 64, 64, 80, 96}
};

/* ═══════════════════════════════════════════════════════════════════
 *  Envelope-rate increment table (eg_inc) — ported verbatim from the
 *  canonical MAME / the reference emulator YM2612 core, itself derived from
 *  Nemesis's hardware tests on a real YM2612.
 *
 *  Indexed `eg_pattern[ eg_rate_select[rate] ][ (eg_cnt >> shift) & 7 ]`.
 *  The rate-dependent shift `11 − (rate >> 2)` (rates 0-47, 0 for 48+)
 *  is applied in eg_get_inc() below: it gates whether a slot fires this
 *  EG clock and selects which of the 8 pattern slots to read. Slow rates
 *  step rarely with small increments; fast rates fire every clock with
 *  progressively larger increments (1/2/4/8).
 *
 *  Each main rate group spans 4 sub-rates. Rates 4-47 share the inc-0/1
 *  family (rows 0-3, per-cycle sums 4/5/6/7); the 2× speed-up between
 *  adjacent groups comes entirely from the shift halving. Rates 48-63
 *  run at shift 0, so the 2× steps instead come from the increment
 *  values themselves doubling (rows 4-7 → 8-11 → 12-15 → 16).
 *
 *  Row 17 (inc 16) is attack-only and reached only by the fastest attack
 *  rates; our attack path special-cases rate ≥ 62 as an instant attack,
 *  so row 17 is never indexed. Row 18 is the "infinite time" zero row
 *  used by rates 0-1 (no decay), per Nemesis's hardware findings.
 * ═══════════════════════════════════════════════════════════════════ */

#define EG_PATTERN_COUNT 19
static const uint8_t eg_pattern[EG_PATTERN_COUNT][8] = {
    /*  0 */ { 0,1, 0,1, 0,1, 0,1 }, /* rates 4-47 sub-0 (inc 0/1) */
    /*  1 */ { 0,1, 0,1, 1,1, 0,1 }, /* rates 4-47 sub-1           */
    /*  2 */ { 0,1, 1,1, 0,1, 1,1 }, /* rates 2,4-47 sub-2         */
    /*  3 */ { 0,1, 1,1, 1,1, 1,1 }, /* rates 3,4-47 sub-3         */
    /*  4 */ { 1,1, 1,1, 1,1, 1,1 }, /* rate 48 (inc 1/2)          */
    /*  5 */ { 1,1, 1,2, 1,1, 1,2 }, /* rate 49                    */
    /*  6 */ { 1,2, 1,2, 1,2, 1,2 }, /* rate 50                    */
    /*  7 */ { 1,2, 2,2, 1,2, 2,2 }, /* rate 51                    */
    /*  8 */ { 2,2, 2,2, 2,2, 2,2 }, /* rate 52 (inc 2/4)          */
    /*  9 */ { 2,2, 2,4, 2,2, 2,4 }, /* rate 53                    */
    /* 10 */ { 2,4, 2,4, 2,4, 2,4 }, /* rate 54                    */
    /* 11 */ { 2,4, 4,4, 2,4, 4,4 }, /* rate 55                    */
    /* 12 */ { 4,4, 4,4, 4,4, 4,4 }, /* rate 56 (inc 4/8)          */
    /* 13 */ { 4,4, 4,8, 4,4, 4,8 }, /* rate 57                    */
    /* 14 */ { 4,8, 4,8, 4,8, 4,8 }, /* rate 58                    */
    /* 15 */ { 4,8, 8,8, 4,8, 8,8 }, /* rate 59                    */
    /* 16 */ { 8,8, 8,8, 8,8, 8,8 }, /* rates 60-63 (inc 8)        */
    /* 17 */ { 16,16,16,16,16,16,16,16 }, /* attack-only fast row  */
    /* 18 */ { 0,0, 0,0, 0,0, 0,0 }, /* infinite time (no step)    */
};

static const uint8_t eg_rate_select[64] = {
    /* 0-3   */ 18, 18,  2,  3,
    /* 4-7   */  0,  1,  2,  3,
    /* 8-11  */  0,  1,  2,  3,
    /* 12-15 */  0,  1,  2,  3,
    /* 16-19 */  0,  1,  2,  3,
    /* 20-23 */  0,  1,  2,  3,
    /* 24-27 */  0,  1,  2,  3,
    /* 28-31 */  0,  1,  2,  3,
    /* 32-35 */  0,  1,  2,  3,
    /* 36-39 */  0,  1,  2,  3,
    /* 40-43 */  0,  1,  2,  3,
    /* 44-47 */  0,  1,  2,  3,
    /* 48-51 */  4,  5,  6,  7,
    /* 52-55 */  8,  9, 10, 11,
    /* 56-59 */ 12, 13, 14, 15,
    /* 60-63 */ 16, 16, 16, 16,
};

static inline uint8_t eg_step_for(uint8_t rate, uint32_t eg_cnt)
{
    return eg_pattern[eg_rate_select[rate & 0x3Fu]][eg_cnt & 7u];
}

static bool tables_built = false;

/* Hardware YM2612 key-code derivation (YM2612 Application Manual, repeated
 * in Nuked-OPN2 / MAME). Yields a full 5-bit KCODE in range 0..31 from
 * the 3-bit BLOCK and the 11-bit FNUM — replaces the earlier 4-bit
 * approximation `block*2 + (fnum>>10)` which only reached KC=15 and so
 * never hit the upper half of the DT1 / envelope-rate hardware tables.
 *
 *   F11 = fnum[10]   (MSB of 11-bit fnum)
 *   F10 = fnum[9]
 *   F9  = fnum[8]
 *   F8  = fnum[7]
 *   N3  = (F11 AND (F10 OR F9 OR F8)) OR (NOT F11 AND F10 AND F9 AND F8)
 *   NOTE = (F11 << 1) | N3              (2 bits)
 *   KCODE = (BLOCK << 2) | NOTE         (5 bits) */
static inline uint8_t ym_kcode(uint8_t block, uint16_t fnum)
{
    uint8_t f11 = (uint8_t)((fnum >> 10) & 1u);
    uint8_t f10 = (uint8_t)((fnum >>  9) & 1u);
    uint8_t f9  = (uint8_t)((fnum >>  8) & 1u);
    uint8_t f8  = (uint8_t)((fnum >>  7) & 1u);
    uint8_t n3  = (uint8_t)((f11 & (f10 | f9 | f8)) |
                            ((f11 ^ 1u) & f10 & f9 & f8));
    uint8_t note = (uint8_t)((f11 << 1) | n3);
    return (uint8_t)(((block & 7u) << 2) | note);
}

static void build_tables(void)
{
    if (tables_built) return;
    tables_built = true;

    /* Sine table: first quadrant of -log2(sin(x)) × 256.
     * Phase input is 10 bits (0–1023 = full cycle).
     * We store the result as a 12-bit unsigned log value. */
    for (int i = 0; i < 256; i++) {
        double phase = ((double)i + 0.5) / 256.0 * (3.14159265358979 / 2.0);
        double s = sin(phase);
        if (s < 0.000001) s = 0.000001;
        double logval = -log2(s) * 256.0;
        sin_table[i] = (uint16_t)(logval + 0.5);
        if (sin_table[i] > 0x0FFF) sin_table[i] = 0x0FFF;
    }
    /* Mirror for all four quadrants */
    for (int i = 0; i < 256; i++) {
        sin_table[256 + i] = sin_table[255 - i];       /* Q2: mirror Q1 */
        sin_table[512 + i] = sin_table[i];              /* Q3: same as Q1 (sign handled separately) */
        sin_table[768 + i] = sin_table[255 - i];        /* Q4: mirror Q2 */
    }

    /* Exponential table: 2^(13 - x/256) for x in [0, 1023]. */
    for (int i = 0; i < 1024; i++) {
        double e = pow(2.0, 13.0 - (double)i / 256.0);
        exp_table[i] = (uint16_t)(e + 0.5);
    }

    /* DT1 detune table is now a source-backed hardware constant
     * (dt1_table at file scope); no runtime initialisation needed. */
}

/* ═══════════════════════════════════════════════════════════════════
 *  Phase generator
 *
 *  Phase increment = (fnum × 2^block) × multiply / 2
 *  with detune adjustment.  fnum is 11 bits, block is 3 bits.
 * ═══════════════════════════════════════════════════════════════════ */

static uint32_t calc_phase_inc_value(const ym_operator_t *op,
                                     uint16_t fnum,
                                     uint8_t block,
                                     uint8_t kc,
                                     bool extra_precision)
{
    /* Base increment */
    uint32_t inc = extra_precision
                 ? (((uint32_t)fnum << block) >> 2)
                 : (((uint32_t)fnum << block) >> 1);

    /* Multiply: 0 = ×0.5, 1–15 = ×1–×15 */
    if (op->multiply == 0)
        inc >>= 1;
    else
        inc *= op->multiply;

    /* Detune — hardware DT1 table from YM2608 manual, sign from DT bit 2. */
    uint8_t dt = op->detune & 3;
    if (dt) {
        int32_t delta = (int32_t)dt1_table[kc & 31][dt];
        if (op->detune & 4) inc -= delta;
        else                inc += delta;
    }

    return inc & 0xFFFFF;
}

static void calc_phase_inc(ym_operator_t *op, uint16_t fnum, uint8_t block, uint8_t kc)
{
    op->phase_inc = calc_phase_inc_value(op, fnum, block, kc, false);
}

static uint32_t calc_lfo_phase_inc(const ym_operator_t *op,
                                   uint16_t fnum,
                                   uint8_t block,
                                   uint8_t kc,
                                   uint8_t pms,
                                   uint8_t lfo_counter)
{
    uint8_t lfo_high;
    uint8_t lfo_idx;
    uint16_t multiplier;
    uint16_t delta = 0;
    uint16_t fm_fnum;

    if (pms == 0) {
        return op->phase_inc;
    }

    lfo_high = lfo_counter >> 2;
    lfo_idx = (lfo_high & 0x08) == 0 ? (lfo_high & 0x07) : (uint8_t)(7 - (lfo_high & 0x07));
    multiplier = lfo_pm_table[pms][lfo_idx];

    for (int bit = 4; bit <= 10; bit++) {
        if (fnum & (1u << bit)) {
            delta += (uint16_t)(multiplier >> (10 - bit));
        }
    }

    fm_fnum = (uint16_t)(fnum << 1);
    if (lfo_high & 0x10) {
        fm_fnum = (uint16_t)((fm_fnum - delta) & 0x0FFFu);
    } else {
        fm_fnum = (uint16_t)((fm_fnum + delta) & 0x0FFFu);
    }

    return calc_phase_inc_value(op, fm_fnum, block, kc, true);
}

/* ═══════════════════════════════════════════════════════════════════
 *  Envelope generator
 *
 *  Attenuation level: 0 = maximum volume, 1023 = silent.
 *  Rate is derived from the register rate + key scaling.
 *  Attack uses exponential curve; decay/sustain/release are linear.
 * ═══════════════════════════════════════════════════════════════════ */

static int calc_rate(ym_operator_t *op, uint8_t base_rate, uint8_t kc)
{
    if (base_rate == 0) return 0;
    int rate = base_rate * 2 + (kc >> (3 - op->key_scale));
    if (rate > 63) rate = 63;
    return rate;
}

static bool ssg_output_inverted(const ym_operator_t *op)
{
    if ((op->ssg_eg & 0x08) == 0 || !op->key_on) {
        return false;
    }

    return op->ssg_inv != ((op->ssg_eg & 0x04) != 0);
}

static uint16_t ssg_eg_inc(const ym_operator_t *op, uint8_t inc)
{
    if ((op->ssg_eg & 0x08) == 0) {
        return inc;
    }

    if (op->eg_level >= 0x200) {
        return 0;
    }

    return (uint16_t)(inc << 2);
}

static void ssg_boundary_step(ym_operator_t *op)
{
    bool hold;
    bool alternate;

    if ((op->ssg_eg & 0x08) == 0 || op->eg_level < 0x200) {
        return;
    }

    hold = (op->ssg_eg & 0x01) != 0;
    alternate = (op->ssg_eg & 0x02) != 0;

    if (alternate) {
        /* Hold+alternate forces the inversion latch high so the boundary
         * sticks to the intended held polarity instead of ping-ponging. */
        op->ssg_inv = hold ? true : !op->ssg_inv;
    }

    if (!alternate && !hold) {
        op->phase = 0;
    }

    if (!op->key_on) {
        op->eg_level = 0x3FF;
        op->eg_phase = EG_OFF;
        return;
    }

    if (!hold) {
        op->eg_phase = EG_ATTACK;
        op->eg_level = 1023;
        return;
    }

    if (op->eg_phase != EG_ATTACK && !ssg_output_inverted(op)) {
        op->eg_level = 0x3FF;
    }
}

static void eg_key_on(ym_operator_t *op)
{
    if (!op->key_on) {
        op->key_on = true;
        op->eg_phase = EG_ATTACK;
        op->eg_level = 1023; /* start at silent, attack will ramp up */
        op->phase = 0; /* reset phase on key-on */
        op->ssg_inv = false;
    }
}

static void eg_key_off(ym_operator_t *op)
{
    if (op->key_on) {
        op->key_on = false;
        if (op->eg_phase != EG_OFF) {
            /* SSG-EG: convert inverted level to real level for smooth release */
            if (ssg_output_inverted(op)) {
                op->eg_level = (uint16_t)((0x200u - op->eg_level) & 0x3FFu);
            }
            op->ssg_inv = false;
            op->eg_phase = EG_RELEASE;
        }
    }
}

/* Returns the rate shift for a given effective rate.
 * The EG counter is right-shifted by this amount before indexing the 8-step
 * pattern, so lower rates advance through the pattern less often.
 * Matches the Nuked-OPN2/MAME model: shift = 11 - (rate >> 2) for rates 0-47,
 * shift = 0 for rates 48-63 (pattern runs at eg_clock rate). */
static inline uint8_t eg_rate_shift(uint8_t rate)
{
    return (rate < 48u) ? (uint8_t)(11u - (rate >> 2)) : 0u;
}

/* Apply one EG update for a given effective rate and eg_cnt.
 * Mirrors the Nuked-OPN2 / MAME model:
 *   - shift = 11 - (rate >> 2)  for rates 0-47,  0 for 48-63
 *   - update gate: only fire when (eg_cnt & ((1<<shift)-1)) == 0
 *   - pattern index: (eg_cnt >> shift) & 7
 * This gives one formula application per 2^shift cycles; combined with
 * the 8-step pattern it produces one full envelope step per 8*2^shift
 * cycles. */
static inline uint8_t eg_get_inc(uint8_t rate, uint32_t eg_cnt)
{
    uint8_t shift = eg_rate_shift(rate);
    /* Gate: fire only once per 2^shift cycles. */
    if (shift > 0 && (eg_cnt & ((1u << shift) - 1u)) != 0)
        return 0;
    return eg_step_for(rate, eg_cnt >> shift);
}

static void eg_step(ym_operator_t *op, uint8_t kc, uint32_t eg_cnt)
{
    int rate;
    uint16_t sl_threshold;
    uint16_t inc;

    ssg_boundary_step(op);

    switch (op->eg_phase) {
    case EG_ATTACK:
        rate = calc_rate(op, op->attack_rate, kc);
        if (rate >= 62) {
            /* Instant attack */
            op->eg_level = 0;
            op->eg_phase = EG_DECAY;
            return;
        }
        if (rate > 0 && op->eg_level > 0) {
            inc = eg_get_inc((uint8_t)rate, eg_cnt);
            if (inc) {
                /* Hardware-accurate exponential convergence toward 0.
                 * Mirrors Nuked-OPN2: eg_level += (~eg_level * inc) >> 4 */
                op->eg_level = (uint16_t)((int32_t)op->eg_level +
                    (((int32_t)(~(int32_t)op->eg_level) * (int32_t)inc) >> 4));
                if ((int16_t)op->eg_level <= 0) {
                    op->eg_level = 0;
                    op->eg_phase = EG_DECAY;
                }
            }
        }
        break;

    case EG_DECAY:
        rate = calc_rate(op, op->decay_rate, kc);
        sl_threshold = (op->sustain_level == 15) ? 1023u : (uint16_t)(op->sustain_level * 32u);
        if (rate > 0) {
            inc = ssg_eg_inc(op, eg_get_inc((uint8_t)(rate < 63 ? rate : 63), eg_cnt));
            op->eg_level += inc;
        }
        if (op->eg_level >= sl_threshold) {
            op->eg_level = sl_threshold;
            op->eg_phase = EG_SUSTAIN;
        }
        if (op->eg_level > 1023) op->eg_level = 1023;
        break;

    case EG_SUSTAIN:
        rate = calc_rate(op, op->sustain_rate, kc);
        if (rate > 0) {
            inc = ssg_eg_inc(op, eg_get_inc((uint8_t)(rate < 63 ? rate : 63), eg_cnt));
            op->eg_level += inc;
        }
        if (op->eg_level > 1023) op->eg_level = 1023;
        break;

    case EG_RELEASE:
        rate = calc_rate(op, op->release_rate * 2u + 1u, kc);
        if (rate > 0) {
            inc = ssg_eg_inc(op, eg_get_inc((uint8_t)(rate < 63 ? rate : 63), eg_cnt));
            op->eg_level += inc;
        }
        if (op->eg_level >= 1023) {
            op->eg_level = 1023;
            op->eg_phase = EG_OFF;
        }
        break;

    case EG_OFF:
        op->eg_level = 1023;
        break;
    }

}

/* ═══════════════════════════════════════════════════════════════════
 *  Operator output
 *
 *  Computes the FM output of one operator given a modulation input.
 *  The modulation shifts the phase; the sine table is looked up;
 *  the envelope attenuation is added in log domain; then converted
 *  to linear via the exp table.
 *
 *  Output range: roughly ±8192 (13-bit signed).
 * ═══════════════════════════════════════════════════════════════════ */

static int32_t op_calc(ym_operator_t *op, int32_t modulation, uint32_t am_offset)
{
    /* SSG-EG: optionally invert envelope output */
    uint16_t eg = op->eg_level;
    if (ssg_output_inverted(op)) {
        eg = (uint16_t)((0x200u - eg) & 0x3FFu);
    }

    /* Total attenuation = TL + EG level + AM */
    uint32_t atten = ((uint32_t)op->total_level << 3) + eg;
    if (op->am_enable) atten += am_offset;
    if (atten >= 1024) return 0; /* fully silent */

    /* Phase: 20-bit accumulator, top 10 bits index the sine table */
    uint32_t phase = (op->phase >> 10) & 0x3FF;

    /* Add modulation (from other operators or feedback) */
    phase = (phase + (uint32_t)((modulation >> 1) & 0x3FF)) & 0x3FF;

    /* Sine lookup (log domain) */
    uint16_t log_sin = sin_table[phase & 0x3FF];

    /* Add envelope attenuation (already in log domain) */
    uint32_t total_log = log_sin + (atten << 2); /* scale atten to match sine range */
    if (total_log >= 4096) return 0;

    /* Exponential conversion: log → linear.
     * exp_table[i] = 2^(13 - i/256) spans 4 octaves over its 1024 entries,
     * so each 1024-block of total_log is 4 octaves: the octave shift must
     * be (total_log >> 10) * 4. (Earlier code omitted the ×4, leaving
     * attenuated operators 8×/64×/512× too loud.) */
    int32_t linear = exp_table[total_log & 0x3FF] >> ((total_log >> 10) << 2);

    /* Sign: negative in the second half of the sine wave. The sin_table
     * stores absolute (folded) values across all four quadrants, so the
     * sign is decided by where the modulated phase index sits — the same
     * value already computed above for the table lookup. */
    if (phase >= 512) linear = -linear;

    return linear;
}

static void lfo_tick(ym2612_t *ym)
{
    if (!ym->lfo_enable) {
        ym->lfo_counter = 0;
        ym->lfo_phase = 0;
        ym->lfo_divider = 0;
        return;
    }

    ym->lfo_divider++;
    if (ym->lfo_divider >= lfo_divider_table[ym->lfo_freq]) {
        ym->lfo_divider = 0;
        ym->lfo_counter = (ym->lfo_counter + 1) & 0x7F;
    }

    ym->lfo_phase = ym->lfo_counter;
}

static uint32_t lfo_am_offset(const ym2612_t *ym, uint8_t ams)
{
    uint32_t lfo_am;

    if (ams == 0) {
        return 0;
    }

    if ((ym->lfo_counter & 0x40) == 0) {
        lfo_am = 0x3F - (ym->lfo_counter & 0x3F);
    } else {
        lfo_am = ym->lfo_counter & 0x3F;
    }

    lfo_am <<= 1;
    switch (ams) {
    case 1: return lfo_am >> 3;
    case 2: return lfo_am >> 1;
    default: return lfo_am;
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Channel output — apply FM algorithm
 *
 *  The 8 algorithms define how the 4 operators (M1/C1/M2/C2 = 0/1/2/3)
 *  are connected.  Operators can modulate each other's phase.
 *
 *  Notation: → means "modulates", | means "output summed"
 *  Alg 0: M1→C1→M2→C2          | C2
 *  Alg 1: (M1+C1)→M2→C2        | C2
 *  Alg 2: (C1+M1→M2)→C2        | C2
 *  Alg 3: (M1→C1+M2)→C2        | C2
 *  Alg 4: M1→C1, M2→C2         | C1+C2
 *  Alg 5: M1→(C1+M2+C2)        | C1+M2+C2
 *  Alg 6: M1→C1, M2, C2        | C1+M2+C2
 *  Alg 7: M1, C1, M2, C2       | M1+C1+M2+C2
 * ═══════════════════════════════════════════════════════════════════ */

/* Map chip register-slot index → internal op[] array index.
 *
 * The YM26xx family stores operators in chip register order S1,S2,S3,S4
 * which correspond to algorithm positions M1,M2,C1,C2. Our internal array
 * indexes them in algorithm-graph order M1,C1,M2,C2 (so the channel_calc
 * algorithm switch can read op[0..3] sequentially as "modulator chain →
 * carriers"). The mapping is therefore not the identity:
 *
 *     register slot 0 (S1 = M1) → op[0]
 *     register slot 1 (S2 = M2) → op[2]
 *     register slot 2 (S3 = C1) → op[1]
 *     register slot 3 (S4 = C2) → op[3]
 *
 * Every register-space access that decodes a slot must go through this
 * map: per-operator $30+ writes, register $28 key-on/off bits 4-7, AND
 * the CH3 special-mode $A8/$A9/$AA frequency registers. Nuked-OPN2
 * uses the same slot[4] = {0,2,1,3} indirection at all three call
 * sites; iteration-12 fixed the $28 path, iteration-34 fixed the CH3
 * special-mode path that had been silently using the identity mapping. */
static const int op_map[4] = {0, 2, 1, 3};

static int32_t channel_calc(ym2612_t *ym, int ch_idx, uint32_t am_offset)
{
    ym_channel_t *ch = &ym->ch[ch_idx];
    ym_operator_t *op = ch->op;
    int32_t fb, out;
    uint8_t kc = ym_kcode(ch->block, ch->fnum);
    uint8_t lfo_counter = (uint8_t)ym->lfo_counter;

    /* Feedback on operator 0. Hardware (and the reference) add the feedback to
     * the phase WITHOUT the modulator >>1 — op_calc1 (feedback) vs
     * op_calc (inter-operator). Our op_calc applies >>1 to all
     * modulation, so pre-shift the feedback left 1 to cancel it; the
     * net feedback is then (prev + cur) >> (10 - FB), matching the reference.
     * (Previously the feedback went through the >>1 too, leaving it
     * 2× too weak.) */
    if (ch->feedback > 0) {
        fb = ((op[0].prev_output + op[0].output) >>
              (10 - ch->feedback)) << 1;
    } else {
        fb = 0;
    }

    /* Advance all operator phases (with PM modulation) */
    for (int i = 0; i < 4; i++) {
        uint16_t fn = ch->fnum;
        uint8_t bl = ch->block;
        uint8_t op_kc = kc;
        uint32_t inc;

        if (ch_idx == 2 && ym->ch3_mode && i < 3) {
            /* CH3 special mode: registers $A8/$A9/$AA store fnum at
             * register slot index 0/1/2. The chip dispatches those
             * slots to internal operators via the same {0,2,1,3}
             * map as the $30+ per-op writes — see op_map. Without
             * the remap, op[1] would receive $A9's fnum and op[2]
             * would receive $AA's, which is the same bug pattern as
             * the iteration-12 $28 key-on fix. */
            fn = ym->ch3_fnum[op_map[i]];
            bl = ym->ch3_block[op_map[i]];
            op_kc = ym_kcode(bl, fn);
        }

        inc = calc_lfo_phase_inc(&op[i], fn, bl, op_kc, ch->pms, lfo_counter);
        op[i].phase = (op[i].phase + inc) & 0xFFFFF;
    }

    /* Compute operator outputs per algorithm */
    int32_t m1, c1, m2, c2;

    m1 = op_calc(&op[0], fb, am_offset);
    op[0].prev_output = op[0].output;
    op[0].output = m1;

    switch (ch->algorithm) {
    case 0: /* M1→C1→M2→C2 */
        c1 = op_calc(&op[1], m1, am_offset);
        m2 = op_calc(&op[2], c1, am_offset);
        c2 = op_calc(&op[3], m2, am_offset);
        out = c2;
        break;
    case 1: /* (M1+C1)→M2→C2 */
        c1 = op_calc(&op[1], 0, am_offset);
        m2 = op_calc(&op[2], m1 + c1, am_offset);
        c2 = op_calc(&op[3], m2, am_offset);
        out = c2;
        break;
    case 2: /* (C1 + M1→M2)→C2 */
        c1 = op_calc(&op[1], 0, am_offset);
        m2 = op_calc(&op[2], m1, am_offset);
        c2 = op_calc(&op[3], c1 + m2, am_offset);
        out = c2;
        break;
    case 3: /* (M1→C1 + M2)→C2 */
        c1 = op_calc(&op[1], m1, am_offset);
        m2 = op_calc(&op[2], 0, am_offset);
        c2 = op_calc(&op[3], c1 + m2, am_offset);
        out = c2;
        break;
    case 4: /* M1→C1, M2→C2 */
        c1 = op_calc(&op[1], m1, am_offset);
        m2 = op_calc(&op[2], 0, am_offset);
        c2 = op_calc(&op[3], m2, am_offset);
        out = c1 + c2;
        break;
    case 5: /* M1→(C1+M2+C2) */
        c1 = op_calc(&op[1], m1, am_offset);
        m2 = op_calc(&op[2], m1, am_offset);
        c2 = op_calc(&op[3], m1, am_offset);
        out = c1 + m2 + c2;
        break;
    case 6: /* M1→C1, M2, C2 */
        c1 = op_calc(&op[1], m1, am_offset);
        m2 = op_calc(&op[2], 0, am_offset);
        c2 = op_calc(&op[3], 0, am_offset);
        out = c1 + m2 + c2;
        break;
    case 7: /* M1, C1, M2, C2 */
        c1 = op_calc(&op[1], 0, am_offset);
        m2 = op_calc(&op[2], 0, am_offset);
        c2 = op_calc(&op[3], 0, am_offset);
        out = m1 + c1 + m2 + c2;
        break;
    default:
        out = 0;
    }

    return out;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Register write handler
 * ═══════════════════════════════════════════════════════════════════ */

/* op_map definition + doc lives just above channel_calc — its earliest
 * caller. No second copy needed here. */

static void update_channel_freq(ym2612_t *ym, int ch_idx)
{
    ym_channel_t *ch = &ym->ch[ch_idx];
    uint8_t kc = ym_kcode(ch->block, ch->fnum);

    for (int i = 0; i < 4; i++) {
        uint16_t fn = ch->fnum;
        uint8_t  bl = ch->block;
        uint8_t  op_kc = kc;

        /* Channel 3 special mode: each operator has its own frequency.
         * Use the chip slot remap (op_map) so register slots 0/1/2
         * ($A8/$A9/$AA) hit the correct internal operators. */
        if (ch_idx == 2 && ym->ch3_mode) {
            if (i < 3) { /* operators 1-3 use special frequencies */
                fn = ym->ch3_fnum[op_map[i]];
                bl = ym->ch3_block[op_map[i]];
                op_kc = ym_kcode(bl, fn);
            }
            /* operator 4 uses the channel frequency */
        }

        calc_phase_inc(&ch->op[i], fn, bl, op_kc);
    }
}

static void write_reg(ym2612_t *ym, int port, uint8_t reg, uint8_t val)
{
    int ch_base = port * 3; /* port 0 → ch 0-2, port 1 → ch 3-5 */

    /* ── Global registers (port 0 only, $20–$2F) ──────────────── */
    if (reg < 0x30) {
        switch (reg) {
        case 0x22: /* LFO */
            ym->lfo_enable = (val >> 3) & 1;
            ym->lfo_freq = val & 7;
            if (!ym->lfo_enable) {
                ym->lfo_counter = 0;
                ym->lfo_phase = 0;
                ym->lfo_divider = 0;
            }
            break;
        case 0x24: /* Timer A MSB */
            ym->timer_a_load = (ym->timer_a_load & 3) | ((uint16_t)val << 2);
            break;
        case 0x25: /* Timer A LSB */
            ym->timer_a_load = (ym->timer_a_load & 0x3FC) | (val & 3);
            break;
        case 0x26: /* Timer B */
            ym->timer_b_load = val;
            break;
        case 0x27: /* Ch3 mode / Timer control */
            ym->ch3_mode = val & 0xC0;
            ym->timer_a_run = (val & 0x01) != 0;
            ym->timer_b_run = (val & 0x02) != 0;
            ym->timer_a_irq_en = (val >> 2) & 1;
            ym->timer_b_irq_en = (val >> 3) & 1;
            if (val & 0x10) { ym->timer_a_ovf = false; ym->status &= ~0x01; }
            if (val & 0x20) { ym->timer_b_ovf = false; ym->status &= ~0x02; }
            break;
        case 0x28: /* Key on/off */
        {
            int ch_idx = val & 3;
            if (ch_idx == 3) break; /* invalid */
            if (val & 4) ch_idx += 3; /* channels 4-6 */
            ym_channel_t *ch = &ym->ch[ch_idx];
            /* Bits 4-7 select chip slots S1..S4 in register-order;
             * the chip remaps that to its internal operator order via
             * the same {0,2,1,3} swap used by per-operator $30+ writes
             * (op_map). Without this, partial key-on patterns (e.g.
             * keying just S2 via bit 5) hit the wrong operator —
             * matches the explicit slot[] indirection in Nuked-OPN2. */
            for (int i = 0; i < 4; i++) {
                int op_idx = op_map[i];
                if (val & (0x10 << i))
                    eg_key_on(&ch->op[op_idx]);
                else
                    eg_key_off(&ch->op[op_idx]);
            }
            break;
        }
        case 0x2A: /* DAC data */
            ym->dac_data = val;
            break;
        case 0x2B: /* DAC enable */
            ym->dac_enable = (val >> 7) & 1;
            break;
        }
        return;
    }

    /* ── Per-operator registers ($30–$9F) ──────────────────────── */
    if (reg < 0xA0) {
        int ch_idx = (reg & 3);
        if (ch_idx == 3) return; /* invalid */
        ch_idx += ch_base;
        int op_idx = op_map[(reg >> 2) & 3];
        ym_operator_t *op = &ym->ch[ch_idx].op[op_idx];

        switch (reg & 0xF0) {
        case 0x30: /* DT1/MUL */
            op->detune = (val >> 4) & 7;
            op->multiply = val & 0x0F;
            update_channel_freq(ym, ch_idx);
            break;
        case 0x40: /* TL */
            op->total_level = val & 0x7F;
            break;
        case 0x50: /* RS/AR */
            op->key_scale = (val >> 6) & 3;
            op->attack_rate = val & 0x1F;
            break;
        case 0x60: /* AM/D1R */
            op->am_enable = (val >> 7) & 1;
            op->decay_rate = val & 0x1F;
            break;
        case 0x70: /* D2R */
            op->sustain_rate = val & 0x1F;
            break;
        case 0x80: /* SL/RR */
            op->sustain_level = (val >> 4) & 0x0F;
            op->release_rate = val & 0x0F;
            break;
        case 0x90: /* SSG-EG */
            op->ssg_eg = val & 0x0F;
            break;
        }
        return;
    }

    /* ── Per-channel registers ($A0–$BF) ──────────────────────── */
    {
        int ch_idx = (reg & 3);
        if (ch_idx == 3) return;
        ch_idx += ch_base;
        ym_channel_t *ch = &ym->ch[ch_idx];

        switch (reg & 0xFC) {
        case 0xA0: /* Frequency LSB */
            /* A0 = frequency LSB. Commits the outstanding A4 latch
             * (high 3 fnum bits + block) atomically with this low byte
             * into the live channel state. A bare A0 write without a
             * preceding A4 re-applies the standing latch (which starts
             * zeroed at init). This matches documented YM2612 behavior. */
            ch->fnum = ((uint16_t)(ch->fnum_hi_latch & 0x07) << 8) | val;
            ch->block = ch->block_latch;
            update_channel_freq(ym, ch_idx);
            break;
        case 0xA4: /* Frequency MSB + Block — latch only, apply on A0 */
            ch->fnum_hi_latch = val & 0x07;
            ch->block_latch   = (val >> 3) & 0x07;
            break;
        case 0xA8: /* Ch3 special-mode freq LSB — commits the AC latch */
            if (port == 0 && ch_idx < 3) {
                int slot = reg & 3; /* 0-2 */
                if (slot < 3) {
                    ym->ch3_fnum[slot] =
                        ((uint16_t)(ym->ch3_fnum_hi_latch[slot] & 0x07) << 8) |
                        val;
                    ym->ch3_block[slot] = ym->ch3_block_latch[slot];
                    if (ym->ch3_mode) update_channel_freq(ym, 2);
                }
            }
            break;
        case 0xAC: /* Ch3 special-mode freq MSB — latch only, apply on A8 */
            if (port == 0 && ch_idx < 3) {
                int slot = reg & 3;
                if (slot < 3) {
                    ym->ch3_fnum_hi_latch[slot] = val & 0x07;
                    ym->ch3_block_latch[slot]   = (val >> 3) & 0x07;
                }
            }
            break;
        case 0xB0: /* Feedback / Algorithm */
            ch->algorithm = val & 7;
            ch->feedback = (val >> 3) & 7;
            break;
        case 0xB4: /* Stereo / LFO sensitivity */
            ch->left  = (val >> 7) & 1;
            ch->right = (val >> 6) & 1;
            ch->ams   = (val >> 4) & 3;
            ch->pms   = val & 7;
            break;
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════
 *  Public API
 * ═══════════════════════════════════════════════════════════════════ */

void ym2612_init(ym2612_t *ym)
{
    build_tables();
    memset(ym, 0, sizeof(*ym));
    for (int i = 0; i < YM_CHANNELS; i++) {
        for (int j = 0; j < YM_OPERATORS; j++) {
            ym->ch[i].op[j].eg_phase = EG_OFF;
            ym->ch[i].op[j].eg_level = 1023;
        }
        ym->ch[i].left = true;
        ym->ch[i].right = true;
    }
    ym->lp_alpha_q15 = 0;   /* default: filter disabled (pass-through) */
    ym->lp_state_l = 0;
    ym->lp_state_r = 0;
}

void ym2612_reset(ym2612_t *ym)
{
    /* Soft reset must clear the digital chip state but preserve the analog
     * output-stage filter config — the RC network on real hardware doesn't
     * care about the chip RESET pin. Filter memory still resets because
     * the DAC drives toward zero while the chip is held in reset. */
    int32_t saved_alpha = ym->lp_alpha_q15;
    ym2612_init(ym);
    ym->lp_alpha_q15 = saved_alpha;
    ym->lp_state_l = 0;
    ym->lp_state_r = 0;
}

void ym2612_set_lowpass_cutoff_hz(ym2612_t *ym, int sample_rate_hz, int cutoff_hz)
{
    if (!ym) return;

    if (sample_rate_hz <= 0 || cutoff_hz <= 0) {
        ym->lp_alpha_q15 = 0; /* pass-through */
        ym->lp_state_l = 0;
        ym->lp_state_r = 0;
        return;
    }

    /* alpha = dt / (dt + RC) = 1 / (1 + fs / (2*pi*fc)).
     * For fc >= fs/2 the filter collapses to pass-through; clamp to Q15
     * range. A rounded-zero alpha is bumped to 1 to stay "enabled" — 0 is
     * the disabled sentinel. */
    double fs = (double)sample_rate_hz;
    double fc = (double)cutoff_hz;
    double rc = 1.0 / (2.0 * YM2612_PI * fc);
    double dt = 1.0 / fs;
    double alpha = dt / (dt + rc);
    if (alpha < 0.0) alpha = 0.0;
    if (alpha > 1.0) alpha = 1.0;

    int32_t q15 = (int32_t)(alpha * 32767.0 + 0.5);
    if (q15 == 0) q15 = 1;
    ym->lp_alpha_q15 = q15;
    ym->lp_state_l = 0;
    ym->lp_state_r = 0;
}

void ym2612_write(ym2612_t *ym, int port, bool addr_or_data, uint8_t value)
{
    port &= 1;
    if (!addr_or_data) {
        /* Address write */
        ym->addr_latch[port] = value;
    } else {
        /* Data write */
        write_reg(ym, port, ym->addr_latch[port], value);
    }
}

uint8_t ym2612_read_status(const ym2612_t *ym)
{
    return ym->status;
}

void ym2612_update(ym2612_t *ym, int16_t *buf, int count)
{
    for (int s = 0; s < count; s++) {
        int32_t left_acc = 0, right_acc = 0;

        /* CSM: key-off on the sample after the Timer A key-on */
        if (ym->csm_key_pending) {
            ym_channel_t *ch3 = &ym->ch[2];
            for (int i = 0; i < 4; i++)
                eg_key_off(&ch3->op[i]);
            ym->csm_key_pending = false;
        }

        lfo_tick(ym);

        /* Envelope generator advances at 1/3 the FM sample rate: the
         * chip's EG counter increments once per 3 FM samples (verified
         * on real YM2612 hardware; matches MAME / the reference emulator).
         * Stepping the EG every sample — as the code did before — made
         * every attack/decay/sustain/release evolve 3x too fast, which
         * showed up as spurious volume spikes and valleys in sustained
         * music. */
        bool eg_advance = false;
        if (++ym->eg_timer >= 3u) {
            ym->eg_timer = 0u;
            /* 12-bit counter; the all-zero value is skipped on real
             * hardware, so the wrap target is 1, not 0. */
            if (++ym->eg_clock >= 4096u)
                ym->eg_clock = 1u;
            eg_advance = true;
        }

        /* Process each channel */
        for (int i = 0; i < YM_CHANNELS; i++) {
            ym_channel_t *ch = &ym->ch[i];

            /* Step envelope generators on the EG tick only. CH3 special
             * mode pulls per-op key-code from the ch3_fnum array via the
             * same op_map remap that update_channel_freq + channel_calc
             * use, so EG and phase share one source-of-truth. */
            if (eg_advance) {
                uint8_t kc = ym_kcode(ch->block, ch->fnum);
                for (int j = 0; j < 4; j++) {
                    uint8_t op_kc = (i == 2 && ym->ch3_mode && j < 3)
                        ? ym_kcode(ym->ch3_block[op_map[j]],
                                   ym->ch3_fnum[op_map[j]])
                        : kc;
                    eg_step(&ch->op[j], op_kc, ym->eg_clock);
                }
            }

            /* Always run channel_calc so operator phases advance every
             * sample — on real hardware the phase generator runs
             * independently of the channel-6 DAC mux, so disabling DAC
             * mid-stream resumes FM at the correct phase instead of the
             * stale phase from when DAC was enabled. The FM result is
             * then overridden by the DAC value if DAC is engaged for
             * ch6. The wasted op_calc work is a few hundred cycles per
             * sample, negligible relative to the rest of the update. */
            int32_t fm_out = channel_calc(ym, i, lfo_am_offset(ym, ch->ams));
            /* DAC scale: dac_data is 8-bit unsigned (0-255). Center it on
             * 128 (silent) → signed -128..127, then shift left 6 to bring
             * the magnitude to ~±8192 — same scale as one fully-keyed FM
             * operator (op_calc returns ±8191). Keeping DAC and FM in the
             * same per-channel domain means the post-mix sums and the
             * panning math behave the same regardless of which mode ch6
             * is in. Asymmetric range (-8192..8128) follows from int8
             * conversion; the 64-unit skew is a constant DC offset that
             * the analog low-pass smooths out. */
            int32_t out = (i == 5 && ym->dac_enable)
                        ? (((int32_t)ym->dac_data - 128) << 6)
                        : fm_out;

            /* Stereo panning */
            if (ch->left)  left_acc  += out;
            if (ch->right) right_acc += out;
        }

        /* Hyperbolic soft-clip on the 6-channel sum before the int16
         * cast and the analog low-pass. Below |25000| the curve is
         * exactly identity, so single-channel and modest-mix tests
         * (every existing pin in the test suites) see bit-identical
         * output. Above the threshold, peaks asymptotically approach
         * the int16 limit instead of folding into hard-clip "tearing".
         *
         *   f(x) = T + (M − T)·(x − T) / ((M − T) + (x − T))   (x > T)
         *
         * with T = 25000, M = 32767, so f(T) = T (continuity), f(∞) →
         * M (asymptote), and the curve is monotone over [T, ∞). The
         * negative branch mirrors. Models the analog saturation in
         * real Genesis output amps without per-channel pre-attenuation
         * (which would lose 6 dB SNR). */
        {
            const int32_t T = 25000;
            const int32_t R = 32767 - T; /* 7767 */
            int32_t v;

            v = left_acc;
            if (v > T) {
                int32_t e = v - T;
                left_acc = T + (int32_t)(((int64_t)R * e) / (e + R));
            } else if (v < -T) {
                int32_t e = -T - v;
                left_acc = -T - (int32_t)(((int64_t)R * e) / (e + R));
            }

            v = right_acc;
            if (v > T) {
                int32_t e = v - T;
                right_acc = T + (int32_t)(((int64_t)R * e) / (e + R));
            } else if (v < -T) {
                int32_t e = -T - v;
                right_acc = -T - (int32_t)(((int64_t)R * e) / (e + R));
            }
        }

        /* Defensive clamp — soft-clip should already keep values inside
         * the int16 range, but guard against any extreme input that
         * the asymptote rounds toward 32767/−32768 from above. */
        if (left_acc >  32767) left_acc =  32767;
        if (left_acc < -32768) left_acc = -32768;
        if (right_acc >  32767) right_acc =  32767;
        if (right_acc < -32768) right_acc = -32768;

        /* Analog-character 1-pole IIR low-pass per channel:
         *   y[n] = y[n-1] + alpha * (x[n] - y[n-1])
         * Q15 fixed-point. alpha == 0 means filter disabled. */
        if (ym->lp_alpha_q15 != 0) {
            int32_t alpha = ym->lp_alpha_q15;
            int32_t y_l = ym->lp_state_l;
            int32_t y_r = ym->lp_state_r;
            y_l += (alpha * (left_acc  - y_l)) >> 15;
            y_r += (alpha * (right_acc - y_r)) >> 15;
            ym->lp_state_l = y_l;
            ym->lp_state_r = y_r;
            left_acc  = y_l;
            right_acc = y_r;
        }

        buf[s * 2 + 0] = (int16_t)left_acc;
        buf[s * 2 + 1] = (int16_t)right_acc;
    }
}

/* Hardware YM2612 timer cadence. Timer A advances once per FM output
 * sample; one FM sample is 1008 Genesis master clocks (master / 7 / 144).
 * Timer B advances once per 16 FM samples. Verified against Genesis
 * Plus GX (INTERNAL_TIMER_A is called once per output sample; Timer B
 * reload TBL = (256 - TB) << 4 — the <<4 is the 16-sample step).
 * Timer A period at LOAD=0 is 1024 × 1008 / 53.69 MHz ≈ 19.2 ms.
 * (Earlier values 72 / 1152 ran both timers 14× too fast — wrong
 * timer IRQ rate, CSM rate, and timer-driven music tempo.) */
#define YM_TIMER_A_MASTER_CLOCKS 1008u
#define YM_TIMER_B_MASTER_CLOCKS 16128u

static bool ym_timer_a_tick(ym2612_t *ym)
{
    if (!ym->timer_a_run) return false;
    ym->timer_a_cnt++;
    if (ym->timer_a_cnt < (1024u - ym->timer_a_load)) return false;
    ym->timer_a_cnt = 0;
    ym->timer_a_ovf = true;
    /* CSM mode: Timer A overflow forces key-on for all CH3 operators */
    if (ym->ch3_mode & 0x80) {
        ym_channel_t *ch3 = &ym->ch[2];
        for (int i = 0; i < 4; i++)
            eg_key_on(&ch3->op[i]);
        ym->csm_key_pending = true;
    }
    if (!ym->timer_a_irq_en) return false;
    ym->status |= 0x01;
    return true;
}

static bool ym_timer_b_tick(ym2612_t *ym)
{
    if (!ym->timer_b_run) return false;
    ym->timer_b_cnt++;
    if (ym->timer_b_cnt < (256u - ym->timer_b_load)) return false;
    ym->timer_b_cnt = 0;
    ym->timer_b_ovf = true;
    if (!ym->timer_b_irq_en) return false;
    ym->status |= 0x02;
    return true;
}

bool ym2612_tick_timers(ym2612_t *ym)
{
    /* Legacy scanline-tick semantics: one Timer A tick per call, and
     * one Timer B tick every 16 calls (the /16 prescaler). Preserved
     * verbatim for the existing test harness + scanline-stepped
     * callers; cycle-accurate callers should use the
     * ym2612_tick_timers_master() entry point instead. */
    bool irq = ym_timer_a_tick(ym);

    if (ym->timer_b_run) {
        ym->timer_b_div = (uint8_t)((ym->timer_b_div + 1) & 0x0F);
        if (ym->timer_b_div == 0) {
            irq = ym_timer_b_tick(ym) || irq;
        }
    }

    return irq;
}

bool ym2612_tick_timers_master(ym2612_t *ym, uint32_t master_clocks)
{
    bool irq = false;

    /* Timer A: accumulate, emit one tick per YM_TIMER_A_MASTER_CLOCKS. */
    ym->timer_a_master_accum += master_clocks;
    while (ym->timer_a_master_accum >= YM_TIMER_A_MASTER_CLOCKS) {
        ym->timer_a_master_accum -= YM_TIMER_A_MASTER_CLOCKS;
        if (ym_timer_a_tick(ym)) irq = true;
    }

    /* Timer B: independent accumulator at 1152-master-clock cadence. */
    ym->timer_b_master_accum += master_clocks;
    while (ym->timer_b_master_accum >= YM_TIMER_B_MASTER_CLOCKS) {
        ym->timer_b_master_accum -= YM_TIMER_B_MASTER_CLOCKS;
        if (ym_timer_b_tick(ym)) irq = true;
    }

    return irq;
}
