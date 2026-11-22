/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's player: the chord a key or the sequencer asks for, voiced; the groove that plays it; the chord
 * sequencer; the clock (internal, or MIDI clock with start / stop / continue) and the clock out. It runs at the
 * ticks (24 a quarter) inside the render. Part of dsp/engine.c. */
#define MAXHELD 12
static struct {
    volatile int live_slot, live_vel;    /* the chord key held (or latched), -1 none: the app's */
    int seq_slot;                        /* the sequencer's chord now, -1 none */
    int slot;                            /* the chord sounding, -1 none */
    uint8_t notes[4], n;                 /* its notes */
    uint8_t prev_upper[4], prev_n;       /* the last chord's notes (SMOOTH voices the next one near them) */
    int sustained;                       /* no groove: the chord's notes are on (as long as the slot is) */
    struct {
        uint8_t note;
        uint32_t off;                    /* the tick its note ends */
    } held[MAXHELD];                     /* the groove's notes sounding */
    uint32_t nheld;
    gev_t open;                          /* the groove's last hit, and where it ends (a new chord takes it over) */
    uint32_t open_end;
    volatile int playing;
    uint32_t t_song;                     /* ticks since the transport started */
    uint32_t t_free;                     /* the groove's own count while the transport is stopped */
    uint32_t t_any;                      /* every tick (the hits held count by it) */
    int step;                            /* the sequencer's step under way, -1 none */
    float acc;                           /* the internal clock: ticks owed */
    volatile uint32_t ext_pending;       /* MIDI clocks in, not yet played */
    volatile int ext_start, ext_stop, ext_cont;
    uint32_t sample, ext_at, ext_prev;   /* samples since boot; when the last clock came, the one before */
    float ext_period;                    /* samples a clock, smoothed */
    int start_req, stop_req;
    uint32_t clocks_out;
    int was_ext;
} PL;

static const eng_cfg_t *CFG;

static int ext_active(void)
{
    if (CFG->sync == SYNC_INT)
        return 0;
    return PL.ext_at && PL.sample - PL.ext_at < (uint32_t)(SR * 0.35f);
}

int eng_chord_notes(int slot, uint8_t out[4])
{
    const uint8_t *src;
    int n = 0, i, j, shift;
    uint8_t t[4];
    if (slot < 0 || slot > 11 || CFG->set >= NSETS)
        return 0;
    src = CHORDSETS[CFG->set].notes[slot];
    shift = (int)CFG->key - 12 + 12 * CFG->octave;
    for (i = 0; i < 4; i++)
        if (src[i])
            t[n++] = (uint8_t)clampi(src[i] + shift, 12, 115);
    switch ((CFG->fx[0] * NVOICINGS) >> 7) {
    case VC_OPEN:                        /* the second note up an octave: an open voicing */
        if (n >= 3)
            t[1] = (uint8_t)clampi(t[1] + 12, 12, 115);
        break;
    case VC_WIDE:                        /* the bass an octave down */
        if (n >= 2)
            t[0] = (uint8_t)clampi(t[0] - 12, 12, 115);
        break;
    case VC_SMOOTH:                      /* the notes above the bass near the last chord's */
        if (PL.prev_n && n >= 2) {
            int c = 0;
            for (i = 0; i < PL.prev_n; i++)
                c += PL.prev_upper[i];
            c /= PL.prev_n;
            for (i = 1; i < n; i++) {
                int v = t[i];
                while (v > c + 6)
                    v -= 12;
                while (v < c - 6)
                    v += 12;
                if (v <= t[0])
                    v += 12;
                t[i] = (uint8_t)v;
            }
        }
        break;
    default:
        break;
    }
    for (i = 1; i < n; i++)              /* lowest first */
        for (j = i; j > 0 && t[j] < t[j - 1]; j--) {
            uint8_t x = t[j];
            t[j] = t[j - 1];
            t[j - 1] = x;
        }
    for (i = 0; i < n; i++)
        out[i] = t[i];
    return n;
}

/* a note of the groove's: on now (after wait samples), off at tick off */
static void held_on(int note, float vel, uint32_t wait, uint32_t off)
{
    uint32_t i;
    for (i = 0; i < PL.nheld; i++)       /* the same note again: the old one ends */
        if (PL.held[i].note == note) {
            voice_off(note, 1);
            PL.held[i] = PL.held[--PL.nheld];
            break;
        }
    if (PL.nheld >= MAXHELD) {
        voice_off(PL.held[0].note, 1);
        PL.held[0] = PL.held[--PL.nheld];
    }
    voice_on(note, vel, 1, wait);
    PL.held[PL.nheld].note = (uint8_t)note;
    PL.held[PL.nheld].off = off;
    PL.nheld++;
}

static void held_all_off(void)
{
    voices_owner_off(1);
    PL.nheld = 0;
    PL.sustained = 0;
}

static uint32_t strum_samples(void) { return (uint32_t)((float)CFG->fx[1] * (0.045f / 127.0f) * SR); }

/* a groove hit with the chord sounding now */
static void play_event(const gev_t *e, uint32_t len, uint32_t wait)
{
    float vel = (e->accent ? 1.0f : 0.72f) * ((float)PL.live_vel > 0.0f && PL.live_slot >= 0 ? 0.6f + 0.4f * (float)PL.live_vel / 127.0f : 0.9f);
    uint32_t off = PL.t_any + (len ? len : 1u), st = strum_samples(), i;
    int n = PL.n;
    if (!n)
        return;
    switch (e->kind) {
    case K_ARP: {
        uint32_t mode = (uint32_t)CFG->var % 6u, L = arp_len((int)mode, (uint32_t)n);
        held_on(arp_note((int)mode, (uint32_t)n, e->idx % L, PL.notes), vel, wait, off);
        break;
    }
    case K_BASS:
        held_on(PL.notes[0], vel, wait, off);
        break;
    case K_UPPER:
        for (i = n > 1 ? 1u : 0u; i < (uint32_t)n; i++)
            held_on(PL.notes[i], vel * 0.9f, wait + st * i, off);
        break;
    case K_DOWN:                         /* a strum: low to high */
        for (i = 0; i < (uint32_t)n; i++)
            held_on(PL.notes[i], vel, wait + (st + (uint32_t)(0.012f * SR)) * i, off);
        break;
    case K_UP:                           /* and high to low, softer */
        for (i = 0; i < (uint32_t)n; i++)
            held_on(PL.notes[n - 1 - (int)i], vel * 0.7f, wait + (st + (uint32_t)(0.009f * SR)) * i, off);
        break;
    default:
        for (i = 0; i < (uint32_t)n; i++)
            held_on(PL.notes[i], vel, wait + st * i, off);
        break;
    }
}

static int active_slot(void)
{
    if (PL.live_slot >= 0)
        return PL.live_slot;
    if (PL.playing && CFG->seq_len)
        return PL.seq_slot;
    return -1;
}

static int groove_running(void) { return CFG->groove_on && CFG->style >= 1 && CFG->style <= NSTYLES; }

/* the chord changed (or the groove switched, or how chords are voiced): the notes follow */
static volatile int chord_dirty;         /* the set, the key, the octave or the voicing changed */
static void chord_update(void)
{
    int s = active_slot(), i, n, again = chord_dirty;
    uint8_t nn[4];
    chord_dirty = 0;
    if (!groove_running()) {
        /* no groove: the chord sounds while it is asked for */
        if (s == PL.slot && !again && (PL.sustained || s < 0))
            return;
    } else if (s == PL.slot && !again && !PL.sustained) {
        return;
    }
    n = s >= 0 ? eng_chord_notes(s, nn) : 0;
    if (s != PL.slot) {
        if (PL.n) {                      /* SMOOTH's memory: the last chord's notes above its bass */
            PL.prev_n = 0;
            for (i = PL.n > 1 ? 1 : 0; i < PL.n; i++)
                PL.prev_upper[PL.prev_n++] = PL.notes[i];
        }
        if (s >= 0 && PL.slot < 0 && !PL.playing && !PL.nheld) {
            PL.t_free = 0;               /* a chord out of silence: the groove from its start, now */
            if (!ext_active() && PL.acc < 1.0f)
                PL.acc = 1.0f;
        }
    }
    if (!groove_running()) {
        held_all_off();
        PL.slot = s;
        PL.n = (uint8_t)n;
        for (i = 0; i < n; i++)
            PL.notes[i] = nn[i];
        if (n) {
            uint32_t st = strum_samples();
            float vel = PL.live_slot >= 0 ? 0.6f + 0.4f * (float)PL.live_vel / 127.0f : 0.9f;
            for (i = 0; i < n; i++)
                voice_on(nn[i], vel, 1, st * (uint32_t)i);
            PL.sustained = 1;
        }
        return;
    }
    if (PL.sustained)
        held_all_off();
    PL.slot = s;
    PL.n = (uint8_t)n;
    for (i = 0; i < n; i++)
        PL.notes[i] = nn[i];
    if (!n) {                            /* nothing asked for: what plays ends */
        held_all_off();
        PL.open_end = 0;
        return;
    }
    /* a new chord inside a hit that lasts: the hit goes on with it */
    if (PL.open_end > PL.t_any + 1u && PL.open.kind != K_ARP) {
        uint32_t i2;
        for (i2 = 0; i2 < PL.nheld; i2++)
            voice_off(PL.held[i2].note, 1);
        PL.nheld = 0;
        play_event(&PL.open, PL.open_end - PL.t_any, 0);
    }
}

static float PL_samples_per_tick(void)
{
    if (ext_active() && PL.ext_period > 1.0f)
        return PL.ext_period;
    return SR * 60.0f / ((float)(CFG->bpm ? CFG->bpm : 120) * 24.0f);
}

/* one tick (a 24th of a quarter) */
static void tick(void)
{
    uint32_t i, gt, swing_wait = 0;
    gev_t ev[2];
    int k, n;
    if (PL.playing) {
        uint32_t st = STEPLEN_TICKS[CFG->steplen < NSTEPLENS ? CFG->steplen : 0];
        if (CFG->seq_len && PL.t_song % st == 0u) {
            uint8_t c;
            PL.step = (int)((PL.t_song / st) % CFG->seq_len);
            c = CFG->seq[PL.step];
            if (c < 12u)
                PL.seq_slot = c;
            else if (c == STEP_REST)
                PL.seq_slot = -1;
        } else if (!CFG->seq_len) {
            PL.step = -1;
            PL.seq_slot = -1;
        }
    }
    chord_update();
    /* the groove's notes that end now */
    for (i = 0; i < PL.nheld;)
        if ((int32_t)(PL.t_any - PL.held[i].off) >= 0) {
            voice_off(PL.held[i].note, 1);
            PL.held[i] = PL.held[--PL.nheld];
        } else {
            i++;
        }
    if (groove_running() && PL.n) {
        gt = PL.playing ? PL.t_song : PL.t_free;
        n = groove_events(CFG->style, CFG->var, gt, ev);
        if (n && (gt / 6u) % 2u == 1u && gt % 6u == 0u && CFG->fx[2])
            swing_wait = (uint32_t)((float)CFG->fx[2] * (0.5f / 127.0f) * PL_samples_per_tick() * 6.0f);
        for (k = 0; k < n; k++) {
            play_event(&ev[k], ev[k].len, swing_wait);
            PL.open = ev[k];
            PL.open_end = PL.t_any + ev[k].len;
        }
    }
    PL.t_any++;
    PL.t_free++;
    if (PL.playing)
        PL.t_song++;
}

void eng_live_chord(int slot, int vel)
{
    PL.live_vel = vel;
    PL.live_slot = slot;
}

int eng_slot_now(void) { return PL.slot; }
int eng_chord_now(uint8_t notes[4])
{
    int i;
    for (i = 0; i < PL.n; i++)
        notes[i] = PL.notes[i];
    return PL.n;
}

/* the major scale that holds most of the chord (its bass the first or sixth degree when it can), and the chord */
uint32_t eng_scale_now(void)
{
    static const uint32_t MAJ = 0xAB5u;  /* C D E F G A B */
    uint32_t pcs = 0, best = MAJ, r;
    int i, bestn = -1;
    for (i = 0; i < PL.n; i++)
        pcs |= 1u << (PL.notes[i] % 12u);
    if (!pcs)
        return 0xFFFu;
    for (r = 0; r < 12u; r++) {
        uint32_t sc = ((MAJ << r) | (MAJ >> (12u - r))) & 0xFFFu, m = sc & pcs;
        int c = 0, b = PL.notes[0] % 12u;
        while (m) {
            c += 2 * (int)(m & 1u);
            m >>= 1;
        }
        if ((uint32_t)b == r || (uint32_t)b == (r + 9u) % 12u)
            c++;
        if (c > bestn) {
            bestn = c;
            best = sc;
        }
    }
    return best | pcs;
}

void eng_play(int on)
{
    if (on)
        PL.start_req = 1;
    else
        PL.stop_req = 1;
}
int eng_playing(void) { return PL.playing; }

void eng_midi_rt(uint32_t st)
{
    if (CFG->sync == SYNC_INT)
        return;
    if (st == 0xF8u) {
        PL.ext_pending++;
    } else if (st == 0xFAu) {
        PL.ext_start = 1;
    } else if (st == 0xFBu) {
        PL.ext_cont = 1;
    } else if (st == 0xFCu) {
        PL.ext_stop = 1;
    }
}

static void transport_start(int from0)
{
    if (from0) {
        PL.t_song = 0;
        PL.step = -1;
        PL.seq_slot = -1;
    }
    PL.playing = 1;
}
static void transport_stop(void)
{
    PL.playing = 0;
    PL.step = -1;
    PL.seq_slot = -1;
}

uint32_t eng_ticks(void) { return PL.t_song; }
int eng_step(void) { return PL.playing ? PL.step : -1; }
int eng_16th(void) { return (int)(((PL.playing ? PL.t_song : PL.t_free) % 96u) / 6u); }
int eng_ext(void) { return ext_active(); }
uint32_t eng_ext_bpm10(void) { return ext_active() && PL.ext_period > 1.0f ? (uint32_t)(SR * 600.0f / (PL.ext_period * 24.0f)) : 0u; }
uint32_t eng_clocks_out(void) { return PL.clocks_out; }
float eng_step_pos(void)
{
    uint32_t st = STEPLEN_TICKS[CFG->steplen < NSTEPLENS ? CFG->steplen : 0];
    return PL.playing ? (float)PL.t_song / (float)st : 0.0f;
}
