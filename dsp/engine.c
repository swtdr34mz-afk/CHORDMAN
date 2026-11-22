/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's sound engine (engine.h): the voices, the effects, the chord sets, the grooves and the player, one unit.
 * No libc here: the device builds this unit on its own, at -O2. */
#include "engine.h"
#include "fastmath.h"
#include "plat.h"

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

#include "tables.c"

#include "chords.c"

#include "grooves.c"

#include "synth.c"

#include "fx.c"

#include "player.c"

/* ============================================================== the engine === */
static eng_cfg_t cfg0;
static volatile int snd_dirty = 1;
static float out_peak;

void eng_init(void)
{
    uint32_t i;
    CFG = &cfg0;
    cfg0.bpm = 120;
    cfg0.patch = PRESETS[0];
    for (i = 0; i < NVOICES; i++) {
        V[i].stage = 0;
        V[i].note = -1;
        V[i].subph = 1.0f;
    }
    PL.live_slot = -1;
    PL.slot = -1;
    PL.seq_slot = -1;
    PL.step = -1;
    snd_dirty = 1;
}

void eng_bind(const eng_cfg_t *cfg)
{
    CFG = cfg;
    snd_dirty = 1;
    chord_dirty = 1;
}

void eng_changed(void)
{
    snd_dirty = 1;
    chord_dirty = 1;
}

/* the main loop's notes reach the voices through a queue the render drains (the voices are the render's) */
#define EVQ 32u
static volatile uint32_t evq[EVQ];
static volatile uint32_t evq_w, evq_r;
enum { EV_ON = 1, EV_OFF, EV_ALL_OFF };
static void ev_push(uint32_t type, uint32_t note, uint32_t vel)
{
    uint32_t w = evq_w;
    if (w - evq_r >= EVQ)
        return;                          /* (full: dropped) */
    evq[w % EVQ] = type | note << 8 | vel << 16;
    evq_w = w + 1u;
}
void eng_note_on(int note, int vel)
{
    if (note >= 0 && note <= 127)
        ev_push(EV_ON, (uint32_t)note, (uint32_t)clampi(vel, 1, 127));
}
void eng_note_off(int note)
{
    if (note >= 0 && note <= 127)
        ev_push(EV_OFF, (uint32_t)note, 0);
}
void eng_all_off(void) { ev_push(EV_ALL_OFF, 0, 0); }
static void ev_drain(void)
{
    while (evq_r != evq_w) {
        uint32_t e = evq[evq_r % EVQ], note = (e >> 8) & 0x7Fu;
        evq_r++;
        if ((e & 0xFFu) == EV_ON) {
            voice_off((int)note, 0);
            voice_on((int)note, 0.35f + 0.65f * (float)(e >> 16) / 127.0f, 0, 0);
        } else if ((e & 0xFFu) == EV_OFF) {
            voice_off((int)note, 0);
        } else {
            voices_owner_off(0);
            held_all_off();
            PL.slot = -1;
            PL.n = 0;
        }
    }
}

void eng_panic(void)
{
    uint32_t i;
    for (i = 0; i < NVOICES; i++) {
        V[i].stage = 0;
        V[i].env = 0.0f;
        V[i].wait = 0;
    }
    PL.nheld = 0;
    PL.sustained = 0;
    PL.slot = -1;
    PL.n = 0;
}

int eng_voices(void)
{
    int i, n = 0;
    for (i = 0; i < NVOICES; i++)
        n += V[i].stage != 0;
    return n;
}
uint32_t eng_notes_played(void) { return notes_played; }
float eng_level(void)
{
    float p = out_peak;
    out_peak *= 0.8f;
    return p;
}

/* ------------------------------------------------------------- the render --- */
#define MAXBLOCK 256u
#define TCHUNK 32u                        /* samples between ticks looked at (0.7 ms) */
static float buf_l[2][MAXBLOCK], buf_r[2][MAXBLOCK];
static float lfo_buf[MAXBLOCK / CHUNK + 1];
static uint32_t job_n;

static void render_voices(int core, uint32_t from, uint32_t to, uint32_t n)
{
    uint32_t i;
    float *L = buf_l[core], *R = buf_r[core];
    for (i = 0; i < n; i++)
        L[i] = R[i] = 0.0f;
    for (i = from; i < to; i++)
        if (V[i].stage)
            voice_render(&V[i], L, R, n, lfo_buf);
}
static void job_voices(uint32_t arg)     /* the second core: voices 4 .. 7 */
{
    (void)arg;
    render_voices(1, NVOICES / 2u, NVOICES, job_n);
}

/* the clock: the ticks of this chunk (the internal clock's, or the MIDI clocks that came in) */
static void clock_chunk(uint32_t n, int first)
{
    int ext = ext_active();
    PL.sample += n;
    if (ext != PL.was_ext) {
        PL.was_ext = ext;
        PL.acc = 0.0f;
    }
    if (first) {
        chord_update();                  /* a key's chord at once, not at the next tick */
        /* the transport: the player's own (PLAY), or MIDI's */
        if (PL.ext_start) {
            PL.ext_start = 0;
            transport_start(1);
        }
        if (PL.ext_cont) {
            PL.ext_cont = 0;
            transport_start(0);
        }
        if (PL.ext_stop) {
            PL.ext_stop = 0;
            transport_stop();
        }
        if (PL.start_req) {
            PL.start_req = 0;
            transport_start(1);
            if (!ext) {
                app_clock_out(0xFAu);
                PL.acc = 1.0f;           /* the first tick at once */
            }
        }
        if (PL.stop_req) {
            PL.stop_req = 0;
            if (PL.playing && !ext)
                app_clock_out(0xFCu);
            transport_stop();
        }
        while (PL.ext_pending) {         /* the MIDI clocks: a tick each */
            uint32_t now = PL.sample;
            PL.ext_pending--;
            if (PL.ext_at) {
                float d = (float)(now - PL.ext_at);
                if (d > 1.0f && d < SR)
                    PL.ext_period = PL.ext_period > 1.0f ? PL.ext_period + (d - PL.ext_period) * 0.1f : d;
            }
            PL.ext_at = now;
            if (CFG->sync != SYNC_INT)
                tick();
        }
    }
    if (!ext) {
        if (CFG->sync == SYNC_EXT)
            return;                      /* EXT: nothing moves without a clock */
        PL.acc += (float)n * (float)(CFG->bpm ? CFG->bpm : 120) * 24.0f / (60.0f * SR);
        while (PL.acc >= 1.0f) {
            PL.acc -= 1.0f;
            if (PL.playing) {
                app_clock_out(0xF8u);
                PL.clocks_out++;
            }
            tick();
        }
    }
}

void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t i, c, at;
    if (n > MAXBLOCK)
        n = MAXBLOCK;
    if (snd_dirty) {
        snd_dirty = 0;
        sound_from(&CFG->patch);
    }
    fx_block(&CFG->patch, CFG->fx, ext_active() && PL.ext_period > 1.0f ? SR * 60.0f / (PL.ext_period * 24.0f)
                                                                       : (float)(CFG->bpm ? CFG->bpm : 120));
    /* the LFO, one for all, a value a chunk (a sine) */
    for (c = 0; c <= n / CHUNK; c++) {
        lfo_buf[c] = fm_sin_turns(lfo_ph);
        lfo_ph += S.lfo_inc * (float)CHUNK;
        if (lfo_ph >= 1.0f)
            lfo_ph -= 1.0f;
    }
    lfo_val = lfo_buf[0];
    ev_drain();
    /* the clock and the player, chunk by chunk; the voices render the block after: what a chunk's ticks start
     * waits for the chunk's place in the block (voice_on adds chunk_at) */
    for (at = 0; at < n; at += TCHUNK) {
        uint32_t m = n - at < TCHUNK ? n - at : TCHUNK;
        chunk_at = at;
        clock_chunk(m, at == 0u);
    }
    chunk_at = 0;
    job_n = n;
    {
        int on2 = plat_cpu2_on() && plat_cpu2_run(job_voices, 0);
        render_voices(0, 0, NVOICES / 2u, n);
        if (!on2 || plat_cpu2_wait() != 0)
            job_voices(0);
    }
    for (i = 0; i < n; i++) {
        buf_l[0][i] += buf_l[1][i];
        buf_r[0][i] += buf_r[1][i];
    }
    for (i = 0; i < n; i++) {            /* the voices' mix up to the effects' level */
        buf_l[0][i] *= 1.8f;
        buf_r[0][i] *= 1.8f;
    }
    fx_render(buf_l[0], buf_r[0], n);
    if (FX.peak > out_peak)
        out_peak = FX.peak;
    FX.peak = 0.0f;
    for (i = 0; i < n; i++) {
        lr[2 * i] = (int32_t)(buf_l[0][i] * gain);
        lr[2 * i + 1] = (int32_t)(buf_r[0][i] * gain);
    }
}
