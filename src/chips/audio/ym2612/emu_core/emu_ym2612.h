/*
 * ym2612.h — Yamaha YM2612 (OPN2) FM Synthesizer
 *
 * 6-channel, 4-operator FM synthesis with:
 *   - 8 FM algorithms (operator connection topologies)
 *   - Per-operator ADSR envelope generator with rate scaling
 *   - Per-operator detune and frequency multiply
 *   - Channel 3 special mode (per-operator frequencies)
 *   - Channel 6 DAC mode (8-bit PCM playback)
 *   - LFO for vibrato (PM) and tremolo (AM)
 *   - Two programmable timers (A: 10-bit, B: 8-bit)
 *   - Stereo panning per channel
 *
 * Usage:
 *   1. Allocate a ym2612_t, call ym2612_init().
 *   2. Wire Z80/68K writes to ym2612_write().
 *   3. Call ym2612_update() to generate audio samples.
 *   4. Call ym2612_tick_timers() once per scanline for timer IRQs.
 */

#ifndef YM2612_H
#define YM2612_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define YM_CHANNELS    6
#define YM_OPERATORS   4    /* per channel */

/* ── Envelope phases ─────────────────────────────────────────────── */

typedef enum {
    EG_ATTACK,
    EG_DECAY,
    EG_SUSTAIN,
    EG_RELEASE,
    EG_OFF
} ym_eg_phase_t;

/* ── Operator state ──────────────────────────────────────────────── */

typedef struct {
    /* Parameters (from registers) */
    uint8_t  detune;       /* DT1: 0–7                             */
    uint8_t  multiply;     /* MUL: 0–15 (0 = ×0.5)                */
    uint8_t  total_level;  /* TL:  0–127 (attenuation in 0.75dB)  */
    uint8_t  key_scale;    /* KS:  0–3                             */
    uint8_t  attack_rate;  /* AR:  0–31                            */
    uint8_t  decay_rate;   /* D1R: 0–31 (first decay)             */
    uint8_t  sustain_rate; /* D2R: 0–31 (second decay / sustain)  */
    uint8_t  release_rate; /* RR:  0–15                            */
    uint8_t  sustain_level;/* SL:  0–15                            */
    bool     am_enable;    /* AMS-EN: LFO amplitude modulation     */
    uint8_t  ssg_eg;       /* SSG-EG: 0–15                        */

    /* Phase generator state */
    uint32_t phase;        /* 20-bit phase accumulator             */
    uint32_t phase_inc;    /* phase increment (computed from freq) */

    /* Envelope generator state */
    ym_eg_phase_t eg_phase;
    uint16_t eg_level;     /* 10-bit envelope attenuation (0=max vol, 1023=silent) */
    uint8_t  eg_rate;      /* current effective rate                */
    uint32_t eg_counter;   /* envelope update counter              */
    bool     key_on;       /* key on/off state                     */
    bool     ssg_inv;      /* SSG-EG: output inversion active      */

    /* Output */
    int32_t  output;       /* last computed output sample           */
    int32_t  prev_output;  /* previous output (for feedback)       */
} ym_operator_t;

/* ── Channel state ───────────────────────────────────────────────── */

typedef struct {
    ym_operator_t op[YM_OPERATORS];

    /* Channel parameters */
    uint16_t fnum;         /* 11-bit frequency number               */
    uint8_t  block;        /* 3-bit octave block                    */
    uint8_t  algorithm;    /* 0–7: operator connection topology     */
    uint8_t  feedback;     /* 0–7: operator 1 self-feedback level   */
    bool     left;         /* stereo: output to left                */
    bool     right;        /* stereo: output to right               */
    uint8_t  ams;          /* AM sensitivity: 0–3                   */
    uint8_t  pms;          /* PM sensitivity: 0–7                   */

    /* A4/A0 frequency-latch pair. Writing the frequency-MSB register
     * (A4) latches the high 3 fnum bits and the block; writing the
     * frequency-LSB register (A0) atomically commits
     * ((latch_fnum_hi << 8) | A0_value) into fnum together with the
     * latched block. Games commonly write A4 then A0; a bare A0 write
     * re-applies the standing latch with the new low byte. */
    uint8_t  fnum_hi_latch;
    uint8_t  block_latch;
} ym_channel_t;

/* ── Chip state ──────────────────────────────────────────────────── */

typedef struct ym2612 {
    ym_channel_t ch[YM_CHANNELS];

    /* Channel 3 special mode: per-operator frequencies. Same
     * AC/A8 frequency-latch contract as the main A4/A0 pair —
     * AC[slot] latches (fnum_hi, block); A8[slot] commits together
     * with the new LSB. Slots 0..2 are used (slot 3 is invalid per
     * register layout). */
    uint16_t ch3_fnum[4];
    uint8_t  ch3_block[4];
    uint8_t  ch3_fnum_hi_latch[4];
    uint8_t  ch3_block_latch[4];

    /* Chip-clock timer accumulators (ym2612_tick_timers_master).
     * Timer A ticks every 72 YM master clocks; Timer B every 1152. */
    uint32_t timer_a_master_accum;
    uint32_t timer_b_master_accum;
    uint8_t  ch3_mode;     /* 0=normal, 0x40/0x80/0xC0=special     */

    /* DAC (channel 6 PCM mode) */
    bool     dac_enable;
    uint8_t  dac_data;

    /* LFO */
    bool     lfo_enable;
    uint8_t  lfo_freq;     /* 0–7                                  */
    uint32_t lfo_counter;  /* 7-bit LFO counter                    */
    uint32_t lfo_phase;    /* mirror of the 7-bit counter          */
    uint16_t lfo_divider;  /* sample divider for the next counter tick */

    /* Timers */
    uint16_t timer_a_load; /* 10-bit load value                    */
    uint16_t timer_a_cnt;  /* current counter                      */
    uint8_t  timer_b_load; /* 8-bit load value                     */
    uint16_t timer_b_cnt;  /* current counter (needs > 8 bits)     */
    uint8_t  timer_b_div;  /* Timer B advances once every 16 ticks */
    bool     timer_a_run;
    bool     timer_b_run;
    bool     timer_a_ovf;  /* overflow flag                        */
    bool     timer_b_ovf;
    bool     timer_a_irq_en;
    bool     timer_b_irq_en;

    /* Register addressing */
    uint8_t  addr_latch[2];/* address latch for port 0 and port 1  */

    /* Global envelope counter */
    uint32_t eg_timer;
    uint32_t eg_clock;

    /* Status register */
    uint8_t  status;       /* bits 0=Timer A ovf, 1=Timer B ovf    */

    /* CSM: Timer A overflow key-on pending for CH3 operators */
    bool     csm_key_pending;

    /* Analog output character — 1-pole IIR low-pass filter state, applied
     * to the post-mix stereo output. Models the external analog stage that
     * shapes real Genesis FM output (Model 1 ~3.4 kHz, Model 2 ~13 kHz,
     * Nomad/Genesis 3 in between). Disabled (pass-through) by default;
     * enable via ym2612_set_lowpass_cutoff_hz. lp_alpha_q15 == 0 disables
     * the filter; non-zero is the Q15 filter coefficient in [1..32767]. */
    int32_t  lp_alpha_q15;
    int32_t  lp_state_l;     /* left-channel filter memory  */
    int32_t  lp_state_r;     /* right-channel filter memory */
} ym2612_t;

/* ── Public API ──────────────────────────────────────────────────── */

void ym2612_init(ym2612_t *ym);
void ym2612_reset(ym2612_t *ym);

/* Register write.
 *   port: 0 = channels 1–3 ($4000/$4001)
 *         1 = channels 4–6 ($4002/$4003)
 *   addr_or_data: false = address write, true = data write
 *   value: the byte written */
void ym2612_write(ym2612_t *ym, int port, bool addr_or_data, uint8_t value);

/* Read status register ($4000 read). */
uint8_t ym2612_read_status(const ym2612_t *ym);

/* Generate `count` stereo samples into `buf`.
 * Output format: interleaved signed 16-bit, left then right.
 * Buffer must hold count × 2 int16_t values.
 * Sample rate = master_clock / 144  (≈53267 Hz for NTSC Genesis). */
void ym2612_update(ym2612_t *ym, int16_t *buf, int count);

/* Tick timers.  Call once per scanline (~67 µs).
 * Returns true if a timer interrupt should fire. */
bool ym2612_tick_timers(ym2612_t *ym);

/* Advance the Timer A / Timer B prescalers by `master_clocks` YM master
 * ticks. Chip-accurate cadence: Timer A ticks every 72 master clocks,
 * Timer B every 1152. Callers from cycle-accurate machine loops (e.g.
 * genesis.c) should pass the number of master clocks elapsed since the
 * previous call; the function takes care of the sub-tick accumulator
 * so short deltas still flush exactly when their cumulative total
 * crosses a tick boundary. Returns true if any Timer A/B overflow
 * raised an IRQ during the advance. */
bool ym2612_tick_timers_master(ym2612_t *ym, uint32_t master_clocks);

/* Configure a 1-pole low-pass filter applied to every stereo output sample.
 * Models the analog stage that shapes real Genesis FM output.
 *   sample_rate_hz: rate ym2612_update samples are produced at (~53267 Hz NTSC).
 *   cutoff_hz:      desired -3 dB cutoff frequency. Pass 0 to disable.
 * Typical values: ~3400 Hz for Model 1 (VA0-VA5), ~13000 Hz for Model 2.
 * Filter state (lp_state_l/r) is reset; coefficient is preserved across
 * subsequent ym2612_reset() calls (mirrors the SN76489 contract). */
void ym2612_set_lowpass_cutoff_hz(ym2612_t *ym, int sample_rate_hz, int cutoff_hz);

#ifdef __cplusplus
}
#endif

#endif /* YM2612_H */
