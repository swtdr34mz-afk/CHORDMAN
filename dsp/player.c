/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's player: the chord asked for, voiced; the chord part's groove (a rhythm or an arp) and the bass part's;
 * the clock (internal, or MIDI clock with start / stop / continue; the clock out) and the sidechain's kicks. It
 * runs at the ticks (24 a quarter) inside the render. Part of dsp/engine.c. */
#define MAXHELD 16
typedef struct {
    uint8_t part, note;
    uint32_t off;                        /* the sample it ends at (0: held until told) */
} held_t;

static const eng_cfg_t *CFG;
static struct {
    volatile int want_idx, want_flv, want_vel;   /* the chord asked for (the app's) */
    volatile int want_bass;              /* a bass note the left keys hold, -1 the root */
    int idx, flv;                        /* the chord sounding (-1 none) */
    chord_t ch;
    uint8_t notes[5], n;                 /* its voicing */
    int centre;                          /* the last voicing's middle: the next one stays near it */
    int bass_note;                       /* the bass part's note now (-1 none) */
    int chords_held, bass_held;          /* sustained (no pattern): their notes on */
    held_t held[MAXHELD];
    uint32_t nheld;
    int open_c_len, open_b_len;          /* the hits under way: ticks left (a new chord takes them over) */
    uint32_t open_c_end, open_b_end;
    int lead_note;
    volatile int playing;
    uint32_t t_song, t_free, t_any;
    float acc;                           /* the internal clock: ticks owed */
    volatile uint32_t ext_pending;
    volatile int ext_start, ext_stop, ext_cont, start_req, stop_req;
    uint32_t sample, ext_at;
    float ext_period;
    uint32_t clocks_out;
    int was_ext;
    uint32_t arp_seed;
} PL;
static volatile int cfg_dirty = 1;

static int ext_active(void)
{
    if (CFG->sync == SYNC_INT)
        return 0;
    return PL.ext_at && PL.sample - PL.ext_at < (uint32_t)(SR * 0.35f);
}
static float samples_per_tick(void)
{
    if (ext_active() && PL.ext_period > 1.0f)
        return PL.ext_period;
    return SR * 60.0f / ((float)(CFG->bpm ? CFG->bpm : 120) * 24.0f);
}
static int key_shift(void) { return (int)CFG->key - 12; }
static int chord_part(void) { return CFG->mode == MODE_CHORD ? PART_R : PART_L; }
static int left_bass(void) { return (CFG->left_bass >> (CFG->mode & 1u)) & 1u; }   /* bit 0: CHORD mode's, bit 1: LEAD's */
static int bass_part_on(void) { return CFG->mode == MODE_CHORD || left_bass(); }
static int chords_on(void) { return CFG->mode == MODE_CHORD || !left_bass(); }

static const char *chord_name(int idx, int flv)
{
    const chordset_t *s = &SETS[CFG->set < NSETS ? CFG->set : 0];
    return flv ? s->flavor[idx & 7] : s->chord[idx & 7];
}

int eng_chord_notes(int idx, int flv, uint8_t notes[5])
{
    chord_t c;
    int oct = CFG->mode == MODE_CHORD ? 12 * CFG->octave : 0;
    if (idx < 0 || idx >= NCHORDS)
        return 0;
    chord_parse(chord_name(idx, flv), &c);
    {
        int n = voice_chord(&c, key_shift(), PL.centre - oct, notes), i;
        for (i = 0; i < n; i++)
            notes[i] = (uint8_t)clampi(notes[i] + oct, 12, 120);
        return n;
    }
}

/* the bass: a pitch class in the low register (A1 .. G#2) */
static int bass_of(int pc) { return 33 + ((pc - 9 + 24) % 12); }

static void held_add(int part, int note, uint32_t off)
{
    uint32_t i;
    for (i = 0; i < PL.nheld; i++)       /* the same note again: the old one ends */
        if (PL.held[i].part == part && PL.held[i].note == note) {
            PL.held[i] = PL.held[--PL.nheld];
            break;
        }
    if (PL.nheld >= MAXHELD) {
        voice_off(PL.held[0].part, PL.held[0].note, 1);
        PL.held[0] = PL.held[--PL.nheld];
    }
    PL.held[PL.nheld].part = (uint8_t)part;
    PL.held[PL.nheld].note = (uint8_t)note;
    PL.held[PL.nheld].off = off;
    PL.nheld++;
}
static void held_part_off(int part)
{
    uint32_t i;
    for (i = 0; i < PL.nheld;)
        if (PL.held[i].part == part) {
            voice_off(part, PL.held[i].note, 1);
            PL.held[i] = PL.held[--PL.nheld];
        } else {
            i++;
        }
}
static void play(int part, int note, float vel, uint32_t wait, uint32_t len_samples)
{
    voice_on(part, note, vel, 1, wait);
    held_add(part, note, len_samples ? PL.sample + chunk_at + wait + len_samples : 0u);
}

static float gate_factor(void) { return 0.3f + (float)CFG->gate * (0.75f / 127.0f); }
static uint32_t len16(int sixteenths)
{
    float s = (float)sixteenths * 6.0f * samples_per_tick() * gate_factor();
    if (sixteenths > 1)                  /* a long hit: nearly its whole length */
        s = (float)sixteenths * 6.0f * samples_per_tick() * (0.55f + gate_factor() * 0.45f);
    return (uint32_t)s;
}

/* the chord asked for changed (or the set, the key, the mode): voice it, and what sounds follows */
static void chord_refresh(void)
{
    int idx = PL.want_idx, flv = PL.want_flv, i, changed;
    int cp = chord_part();
    if (cfg_dirty) {                     /* the set, the key, the mode, the groove: everything again */
        cfg_dirty = 0;
        held_part_off(PART_L);
        held_part_off(PART_R);
        PL.chords_held = PL.bass_held = 0;
        PL.bass_note = -2;
        PL.idx = -2;
    }
    changed = idx != PL.idx || flv != PL.flv;
    if (!changed && PL.bass_note != -2) {
        int bn = PL.want_bass >= 0 ? PL.want_bass : (PL.idx >= 0 ? bass_of((PL.ch.bass + key_shift() + 120) % 12) : -1);
        if (!bass_part_on() || bn == PL.bass_note)
            return;
    }
    if (changed) {
        PL.idx = idx;
        PL.flv = flv;
        if (idx >= 0) {
            chord_parse(chord_name(idx, flv), &PL.ch);
            PL.n = (uint8_t)eng_chord_notes(idx, flv, PL.notes);
            if (PL.n) {
                int sum = 0, oct = CFG->mode == MODE_CHORD ? 12 * CFG->octave : 0;
                for (i = 0; i < PL.n; i++)
                    sum += PL.notes[i];
                PL.centre = sum / PL.n - oct;
            }
        } else {
            PL.n = 0;
        }
        /* the chord part: held (FULL, no groove) starts again at once; in a pattern, the hit under way goes on
         * with the new chord */
        if (chords_on()) {
            int full = !CFG->groove_on || CFG->crhy == CR_FULL;
            held_part_off(cp);
            PL.chords_held = 0;
            if (PL.n && full) {
                float vel = 0.55f + 0.45f * (float)PL.want_vel / 127.0f;
                for (i = 0; i < PL.n; i++)
                    play(cp, PL.notes[i], vel, (uint32_t)i * 90u, 0);
                PL.chords_held = 1;
            } else if (PL.n && PL.open_c_end > PL.sample && CFG->crhy < CR_ARP_UP) {
                for (i = 0; i < PL.n; i++)
                    play(cp, PL.notes[i], 0.85f, 0, PL.open_c_end - PL.sample);
            }
        }
    }
    /* the bass: its note, held (no groove) or taken over by the hit under way */
    if (bass_part_on()) {
        int bn = PL.want_bass >= 0 ? PL.want_bass : (PL.idx >= 0 ? bass_of((PL.ch.bass + key_shift() + 120) % 12) : -1);
        if (bn != PL.bass_note) {
            PL.bass_note = bn;
            held_part_off(PART_L);
            PL.bass_held = 0;
            if (bn >= 0) {
                if (!CFG->groove_on || !left_bass()) {
                    play(PART_L, bn, 0.9f, 0, 0);
                    PL.bass_held = 1;
                } else if (PL.open_b_end > PL.sample) {
                    play(PART_L, bn, 0.9f, 0, PL.open_b_end - PL.sample);
                }
            }
        }
    } else if (PL.bass_note != -1) {
        PL.bass_note = -1;
    }
}

/* one tick (a 24th of a quarter) */
static void tick(void)
{
    uint32_t gt = PL.playing ? PL.t_song : PL.t_free, s16, i;
    float spt = samples_per_tick();
    chord_refresh();
    if (gt % 6u == 0u) {
        uint32_t swing = 0;
        s16 = (gt / 6u) % 16u;
        if (s16 & 1u)
            swing = (uint32_t)((float)CFG->swing * (0.33f / 127.0f) * 6.0f * spt);
        /* the chord part's groove */
        if (CFG->groove_on && chords_on() && PL.n && CFG->crhy != CR_FULL) {
            int cp = chord_part(), r = CFG->crhy;
            if (r >= CR_ARP_UP && r <= CR_ARP_RANDOM) {
                uint32_t m = 2u * PL.n, k = gt / 6u, j;
                if (r == CR_ARP_UP)
                    j = k % m;
                else if (r == CR_ARP_DOWN)
                    j = m - 1u - k % m;
                else if (r == CR_ARP_UPDN)
                    j = (k % (2u * m - 2u)) < m ? k % (2u * m - 2u) : 2u * m - 2u - k % (2u * m - 2u);
                else {
                    PL.arp_seed = PL.arp_seed * 1103515245u + 12345u;
                    j = (PL.arp_seed >> 16) % m;
                }
                held_part_off(cp);
                play(cp, PL.notes[j % PL.n] + 12 * (int)(j / PL.n), (s16 % 4u) ? 0.75f : 0.95f, swing, len16(1));
                PL.open_c_end = 0;
            } else if (r == CR_CASCADE) {
                if (!(s16 & 1u)) {
                    uint32_t k = (gt / 12u) % (2u * PL.n);
                    play(cp, PL.notes[k % PL.n] + 12 * (int)(k / PL.n), (s16 % 8u) ? 0.7f : 0.9f, 0, len16(6));
                }
            } else {
                int loud, len;
                if (pat_hit(CR_PAT[r], s16, &loud, &len)) {
                    held_part_off(cp);
                    for (i = 0; i < PL.n; i++)
                        play(cp, PL.notes[i], loud ? 0.95f : 0.7f, swing + i * 40u, len16(len));
                    PL.open_c_end = PL.sample + chunk_at + swing + len16(len);
                }
            }
        }
        /* the bass part's */
        if (CFG->groove_on && bass_part_on() && left_bass() && PL.bass_note >= 0) {
            int loud, len, c = pat_hit(BA_PAT[CFG->bass], s16, &loud, &len);
            if (c) {
                int note = PL.bass_note + (c == 'O' || c == 'o' ? 12 : c == '5' ? 7 : 0);
                held_part_off(PART_L);
                play(PART_L, note, loud ? 1.0f : 0.75f, swing, len16(len));
                PL.open_b_end = PL.sample + chunk_at + swing + len16(len);
            }
        }
    }
    /* the sidechain's kicks */
    if (CFG->sc[0]) {
        uint32_t b = gt % 96u;
        int kick = 0;
        switch (CFG->sc[3]) {
        case SK_HALF: kick = b == 0u || b == 48u; break;
        case SK_24: kick = b == 24u || b == 72u; break;
        case SK_TRAP: kick = b == 0u || b == 36u || b == 60u; break;
        default: kick = b % 24u == 0u; break;
        }
        if (kick && FX.nkick < 8u)
            FX.kick_at[FX.nkick++] = chunk_at;
    }
    PL.t_any++;
    PL.t_free++;
    if (PL.playing)
        PL.t_song++;
}

/* the groove's notes that end in this chunk */
static void held_expire(uint32_t upto)
{
    uint32_t i;
    for (i = 0; i < PL.nheld;)
        if (PL.held[i].off && (int32_t)(upto - PL.held[i].off) >= 0) {
            voice_off(PL.held[i].part, PL.held[i].note, 1);
            PL.held[i] = PL.held[--PL.nheld];
        } else {
            i++;
        }
}

void eng_chord(int idx, int flv, int vel)
{
    if (idx >= 0 && PL.want_idx < 0 && !PL.playing && !PL.nheld)
        PL.t_free = 0;                   /* a chord out of silence: the bar starts with it */
    PL.want_vel = vel;
    PL.want_flv = flv;
    PL.want_idx = idx;
}
void eng_bass_note(int note) { PL.want_bass = note; }
int eng_chord_now(void) { return PL.idx >= 0 ? PL.idx | PL.flv << 3 : -1; }
int eng_bass_now(void) { return PL.bass_note >= 0 ? PL.bass_note : -1; }
int eng_lead_now(void) { return PL.lead_note; }

uint32_t eng_scale_bits(void)
{
    const chordset_t *s = &SETS[CFG->set < NSETS ? CFG->set : 0];
    uint32_t sc = SCALES[s->scale % NSCALES], r = (uint32_t)((s->tonic + key_shift() + 120) % 12);
    return ((sc << r) | (sc >> (12u - r))) & 0xFFFu;
}
int eng_scale_note(int degree, int base)
{
    const chordset_t *s = &SETS[CFG->set < NSETS ? CFG->set : 0];
    uint32_t sc = SCALES[s->scale % NSCALES];
    int iv[7], n = 0, p, oct, d;
    for (p = 0; p < 12 && n < 7; p++)
        if (sc >> p & 1u)
            iv[n++] = p;
    oct = degree >= 0 ? degree / 7 : -((-degree + 6) / 7);
    d = degree - oct * 7;
    {
        int tonic = (s->tonic + key_shift() + 120) % 12, t0 = base - ((base - tonic + 1200) % 12);
        if (t0 < base)
            t0 += 0;
        return t0 + 12 * oct + iv[d];
    }
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
    if (st == 0xF8u)
        PL.ext_pending++;
    else if (st == 0xFAu)
        PL.ext_start = 1;
    else if (st == 0xFBu)
        PL.ext_cont = 1;
    else if (st == 0xFCu)
        PL.ext_stop = 1;
}

float eng_pos16(void)
{
    uint32_t gt = PL.playing ? PL.t_song : PL.t_free;
    float f = ext_active() ? 0.0f : fm_clampf(PL.acc, 0.0f, 0.999f);
    return (float)(gt % 96u) / 6.0f + f / 6.0f;
}
uint32_t eng_bar(void) { return (PL.playing ? PL.t_song : PL.t_free) / 96u; }
int eng_running(void) { return PL.playing || PL.idx >= 0 || PL.bass_note >= 0; }
int eng_ext(void) { return ext_active(); }
uint32_t eng_ext_bpm10(void)
{
    return ext_active() && PL.ext_period > 1.0f ? (uint32_t)(SR * 600.0f / (PL.ext_period * 24.0f)) : 0u;
}
uint32_t eng_clocks_out(void) { return PL.clocks_out; }
uint32_t eng_ticks(void) { return PL.t_song; }
float eng_duck(void) { return FX.duck; }
