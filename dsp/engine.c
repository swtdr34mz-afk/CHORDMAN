/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's sound engine (engine.h): the sounds, the voices, the effects, the chords, the grooves and the player,
 * one unit. No libc here: the device builds this unit on its own, at -O2. */
#include "engine.h"
#include "fastmath.h"
#include "plat.h"

static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

#include "sounds.c"

#include "sets.c"

#include "chords.c"

#include "synth.c"

#include "fx.c"

#include "grooves.c"

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
    sin_init();
    for (i = 0; i < NV; i++) {
        V[i].amp.stage = 0;
        V[i].note = -1;
    }
    PL.want_idx = PL.idx = -1;
    PL.want_bass = -1;
    PL.bass_note = -1;
    PL.lead_note = -1;
    PL.centre = 62;
    PL.arp_seed = 1;
    FX.since = 1e9f;
    snd_dirty = 1;
}

void eng_bind(const eng_cfg_t *cfg)
{
    CFG = cfg;
    snd_dirty = 1;
    cfg_dirty = 1;
}
void eng_changed(void) { snd_dirty = 1; }
void eng_rechord(void) { cfg_dirty = 1; }

/* the main loop's notes reach the voices through a queue the render drains (the voices are the render's) */
#define EVQ 32u
static volatile uint32_t evq[EVQ];
static volatile uint32_t evq_w, evq_r;
enum { EV_ON = 1, EV_OFF, EV_ALL_OFF };
static void ev_push(uint32_t type, uint32_t part, uint32_t note, uint32_t vel)
{
    uint32_t f = plat_lock(), w = evq_w;
    if (w - evq_r < EVQ) {
        evq[w % EVQ] = type | part << 4 | note << 8 | vel << 16;
        evq_w = w + 1u;
    }
    plat_unlock(f);
}
void eng_note_on(int part, int note, int vel)
{
    if (note >= 0 && note <= 127)
        ev_push(EV_ON, (uint32_t)part & 1u, (uint32_t)note, (uint32_t)clampi(vel, 1, 127));
}
void eng_note_off(int part, int note)
{
    if (note >= 0 && note <= 127)
        ev_push(EV_OFF, (uint32_t)part & 1u, (uint32_t)note, 0);
}
void eng_all_off(void) { ev_push(EV_ALL_OFF, 0, 0, 0); }
static void ev_drain(void)
{
    while (evq_r != evq_w) {
        uint32_t e = evq[evq_r % EVQ], part = (e >> 4) & 1u, note = (e >> 8) & 0x7Fu;
        evq_r++;
        if ((e & 0xFu) == EV_ON) {
            if (!PS[part].mono)
                voice_off((int)part, (int)note, 0);
            voice_on((int)part, (int)note, 0.3f + 0.7f * (float)(e >> 16) / 127.0f, 0, 0);
            PL.lead_note = (int)note;
        } else if ((e & 0xFu) == EV_OFF) {
            voice_off((int)part, (int)note, 0);
            if (PL.lead_note == (int)note)
                PL.lead_note = -1;
        } else {
            voices_off(PART_L, -1);
            voices_off(PART_R, -1);
            PL.nheld = 0;
            PL.lead_note = -1;
            cfg_dirty = 1;
        }
    }
}

void eng_panic(void)
{
    uint32_t i;
    for (i = 0; i < NV; i++) {
        V[i].amp.stage = 0;
        V[i].wait = 0;
    }
    PL.nheld = 0;
}

int eng_voices(void)
{
    int i, n = 0;
    for (i = 0; i < NV; i++)
        n += V[i].amp.stage != 0;
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
#define TCHUNK 32u
static float bus[2][NPARTS][2][MAXBLOCK];    /* [core][part][L, R] */
static float out_l[MAXBLOCK], out_r[MAXBLOCK];
static float lfo_buf[NPARTS][MAXBLOCK / CHUNK + 1];
static float lfo_ph[NPARTS];
static uint32_t job_n;

static void render_voices(int core, uint32_t from, uint32_t to, uint32_t n)
{
    uint32_t i, p;
    for (p = 0; p < NPARTS; p++)
        for (i = 0; i < n; i++)
            bus[core][p][0][i] = bus[core][p][1][i] = 0.0f;
    for (i = from; i < to; i++) {
        voice_t *v = &V[i];
        if (!v->amp.stage)
            continue;
        if (PS[v->part].type == T_FM)
            render_fm(v, &PS[v->part], bus[core][v->part][0], bus[core][v->part][1], n);
        else
            render_analog(v, &PS[v->part], bus[core][v->part][0], bus[core][v->part][1], n, lfo_buf[v->part]);
    }
}
static void job_voices(uint32_t arg)     /* the second core: half the voices */
{
    (void)arg;
    render_voices(1, NV / 2u, NV, job_n);
}

/* the clock for a chunk: the transport, the MIDI clocks that came in, the internal clock's ticks */
static void clock_chunk(uint32_t n, int first)
{
    int ext = ext_active();
    if (ext != PL.was_ext) {
        PL.was_ext = ext;
        PL.acc = 0.0f;
    }
    if (first) {
        chord_refresh();
        if (PL.ext_start) {              /* START: its clocks follow; the internal one waits for them */
            PL.ext_start = 0;
            PL.t_song = 0;
            PL.playing = 1;
            PL.ext_at = PL.sample ? PL.sample : 1u;
        }
        if (PL.ext_cont) {
            PL.ext_cont = 0;
            PL.playing = 1;
            PL.ext_at = PL.sample ? PL.sample : 1u;
        }
        if (PL.ext_stop) {
            PL.ext_stop = 0;
            PL.playing = 0;
        }
        if (PL.start_req) {
            PL.start_req = 0;
            PL.t_song = 0;
            PL.playing = 1;
            if (!ext) {
                app_clock_out(0xFAu);
                PL.acc = 1.0f;
            }
        }
        if (PL.stop_req) {
            PL.stop_req = 0;
            if (PL.playing && !ext)
                app_clock_out(0xFCu);
            PL.playing = 0;
        }
        while (PL.ext_pending) {
            PL.ext_pending--;
            if (PL.ext_at) {
                float d = (float)(PL.sample - PL.ext_at);
                if (d > 1.0f && d < SR)
                    PL.ext_period = PL.ext_period > 1.0f ? PL.ext_period + (d - PL.ext_period) * 0.15f : d;
            }
            PL.ext_at = PL.sample ? PL.sample : 1u;
            if (CFG->sync != SYNC_INT)
                tick();
        }
    }
    if (!ext && CFG->sync != SYNC_EXT) {
        PL.acc += (float)n / samples_per_tick();
        while (PL.acc >= 1.0f) {
            PL.acc -= 1.0f;
            if (PL.playing) {
                app_clock_out(0xF8u);
                PL.clocks_out++;
            }
            tick();
        }
    }
    held_expire(PL.sample + n);
    PL.sample += n;
}

void eng_render(int32_t *lr, uint32_t n, float gain)
{
    uint32_t i, c, at, p;
    float *pl[2], *pr[2];
    if (n > MAXBLOCK)
        n = MAXBLOCK;
    if (snd_dirty) {
        snd_dirty = 0;
        for (p = 0; p < NPARTS; p++)
            psnd_from((int)p, &CFG->part[CFG->mode & 1u][p]);
        for (p = 0; p < NPARTS; p++)
            chorus_set(&CH[p], PS[p].chorus);
    } else {
        for (p = 0; p < NPARTS; p++)     /* (the chorus's mix glides) */
            chorus_set(&CH[p], PS[p].chorus);
    }
    FX.beat = samples_per_tick() * 24.0f;
    /* the sidechain on the filters: where it is now */
    {
        float d = CFG->sc[2] == ST_FILTER || CFG->sc[2] == ST_BOTH ? FX.duck : 0.0f;
        for (p = 0; p < NPARTS; p++)
            PS[p].duck_oct = -d * 4.0f;
    }
    /* each part's LFO (its vibrato, its PWM), a value a chunk */
    for (p = 0; p < NPARTS; p++) {
        float rate = PS[p].s ? PS[p].s->vib_rate : 5.0f;
        for (c = 0; c <= n / CHUNK; c++) {
            lfo_buf[p][c] = fm_sin_turns(lfo_ph[p]);
            lfo_ph[p] += rate * ISR * (float)CHUNK;
            if (lfo_ph[p] >= 1.0f)
                lfo_ph[p] -= 1.0f;
        }
    }
    ev_drain();
    for (at = 0; at < n; at += TCHUNK) {
        uint32_t m = n - at < TCHUNK ? n - at : TCHUNK;
        chunk_at = at;
        clock_chunk(m, at == 0u);
    }
    chunk_at = 0;
    job_n = n;
    {
        int on2 = plat_cpu2_on() && plat_cpu2_run(job_voices, 0);
        render_voices(0, 0, NV / 2u, n);
        if (!on2 || plat_cpu2_wait() != 0)
            job_voices(0);
    }
    for (p = 0; p < NPARTS; p++) {
        for (i = 0; i < n; i++) {
            bus[0][p][0][i] = (bus[0][p][0][i] + bus[1][p][0][i]) * 1.6f;
            bus[0][p][1][i] = (bus[0][p][1][i] + bus[1][p][1][i]) * 1.6f;
        }
        chorus_run((int)p, bus[0][p][0], bus[0][p][1], n);
        pl[p] = bus[0][p][0];
        pr[p] = bus[0][p][1];
    }
    fx_mix(pl, pr, out_l, out_r, n, FX.beat / SR, CFG->sc);
    if (FX.peak > out_peak)
        out_peak = FX.peak;
    FX.peak = 0.0f;
    for (i = 0; i < n; i++) {
        lr[2 * i] = (int32_t)(out_l[i] * gain);
        lr[2 * i + 1] = (int32_t)(out_r[i] * gain);
    }
}
