/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's voices: band-limited oscillators (polyBLEP), the envelope, the four engines' filters and the voice
 * render. Part of dsp/engine.c.
 *   DCO     one oscillator: saw and pulse (PWM), a square sub an octave down, noise; a soft four-pole low-pass
 *   VCO     two oscillators, their shape from triangle through saw and square to a narrow pulse, the second
 *           detuned or a fixed interval up, poly mod (it bends the first and the filter); a four-pole low-pass
 *   SVF     two saws (the second turning into a pulse), driven into a two-pole state-variable filter that morphs
 *           low-pass, notch, high-pass
 *   LADDER  three oscillators (two saws spread, a square sub), driven into a four-pole ladder; glide */
#define SR 44100.0f
#define ISR (1.0f / 44100.0f)
#define CHUNK 8u                          /* samples between the filters' coefficients */

static inline float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float osc_saw(float ph, float dt) { return 2.0f * ph - 1.0f - blep(ph, dt); }
static inline float osc_pulse(float ph, float dt, float pw)
{
    float q = ph + 1.0f - pw;
    if (q >= 1.0f)
        q -= 1.0f;
    return (ph < pw ? 1.0f : -1.0f) + blep(ph, dt) - blep(q, dt);
}
static inline float osc_tri(float ph) { return 4.0f * fm_fabsf(ph - 0.5f) - 1.0f; }
/* the VCO's shape: 0 triangle, 1/3 saw, 2/3 square, 1 a narrow pulse */
static inline float osc_shape(float ph, float dt, float sh)
{
    if (sh < 0.3333f) {
        float a = sh * 3.0f;
        return (1.0f - a) * osc_tri(ph) + a * osc_saw(ph, dt);
    }
    if (sh < 0.6667f) {
        float a = (sh - 0.3333f) * 3.0f;
        return (1.0f - a) * osc_saw(ph, dt) + a * osc_pulse(ph, dt, 0.5f);
    }
    return osc_pulse(ph, dt, 0.5f - (sh - 0.6667f) * 1.2f);
}
static inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }

/* soft saturation: tanh's shape, cheaper (exact enough below 3) */
static inline float sat(float x)
{
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    {
        float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }
}

typedef struct {
    float ph[3], inc[3], tinc;           /* the oscillators' phases and steps; tinc: the step glide goes to */
    float subph;                         /* the DCO's sub: its flip-flop */
    float s[4];                          /* the filter's state (the ladder's four; the SVF's two) */
    float env, vel;
    uint8_t stage;                       /* 0 idle, 1 attack, 2 decay / sustain, 3 release */
    uint8_t owner;                       /* 0 a key or MIDI (a melody note), 1 the player's chord */
    int16_t note;
    uint32_t age, wait;                  /* wait: samples before it starts (a strum, the swing) */
    float drift, fade, pan;              /* its own detune (cents), the LFO's fade-in 0..1, its place left .. right */
    float g, k;                          /* the filter's coefficient and resonance, this chunk */
    uint32_t noise;
} voice_t;
static voice_t V[NVOICES];

typedef struct {                         /* the sound, worked out from the patch at a block */
    int eng;
    float cut_oct, env_oct, key_oct;     /* the cutoff (log2 Hz), how far the envelope takes it, the key's pull */
    float reso;                          /* 0 .. ~3.9 */
    float a_inc, d_k, sus, r_k;          /* the envelope */
    float lfo_inc, lfo_depth, lfo_fade_inc;
    int lfo_tgt;
    float pw, sub, noise;                /* DCO */
    float shape, ratio2, det, polymod;   /* VCO (det: osc 2's detune as a ratio, ratio2 its interval) */
    float pulse2, drive, morph;          /* SVF, LADDER */
    float glide_k;                       /* LADDER: how fast a note glides to its pitch (1 none) */
    float level;                         /* the voice's level (the engines kept even) */
    int chorus;                          /* DCO's CHAR: 0 off, 1 I, 2 II, 3 I+II */
} snd_t;
static snd_t S;
static float lfo_ph, lfo_val;
static uint32_t vage;

static inline float knob(uint8_t v) { return (float)v * (1.0f / 127.0f); }
/* a time knob: 0 .. 127 to lo .. hi seconds, exponentially */
static float ktime(uint8_t v, float lo, float hi) { return lo * fm_powf(hi / lo, knob(v)); }

static void sound_from(const patch_t *p)
{
    float a, d, r;
    int e = p->eng < NENGINES ? p->eng : 0;
    S.eng = e;
    S.cut_oct = 5.3f + knob(p->k[0]) * 9.0f;              /* ~40 Hz .. ~20 kHz */
    S.env_oct = knob(p->k[1]) * 6.0f;
    S.key_oct = 0.5f;
    a = ktime(p->env[0], 0.0015f, 4.0f);
    d = ktime(p->env[1], 0.01f, 8.0f);
    r = ktime(p->env[3], 0.01f, 10.0f);
    S.a_inc = ISR / a;
    S.d_k = fm_expf(-ISR * 4.6f / d);                     /* ~1 % left after the time */
    S.r_k = fm_expf(-ISR * 4.6f / r);
    S.sus = knob(p->env[2]);
    S.lfo_inc = ktime(p->lfo[0], 0.05f, 20.0f) * ISR;
    S.lfo_depth = knob(p->lfo[1]);
    S.lfo_tgt = lfo_target(p->lfo[2]);
    S.lfo_fade_inc = p->lfo[3] ? ISR / ktime(p->lfo[3], 0.05f, 4.0f) : 1.0f;
    S.chorus = 0;
    S.pw = 0.5f;
    S.sub = S.noise = S.polymod = S.pulse2 = S.morph = 0.0f;
    S.shape = 0.3333f;
    S.det = 1.0f;
    S.ratio2 = 1.0f;
    S.drive = 1.0f;
    S.glide_k = 1.0f;
    switch (e) {
    case E_DCO:
        S.chorus = p->k[2] >> 5;
        S.pw = 0.5f - knob(p->vo[0]) * 0.44f;
        S.sub = knob(p->vo[1]);
        S.noise = knob(p->vo[2]);
        S.reso = knob(p->vo[3]) * 3.6f;
        S.drive = 1.3f;
        S.level = 0.9f;
        break;
    case E_VCO: {
        static const float IV[5] = {1.0f, 1.3348f, 1.4983f, 2.0f, 0.5f};   /* unison, a 4th, a 5th, an octave up, down */
        S.shape = knob(p->k[2]);
        S.det = fm_exp2f(knob(p->vo[0]) * 30.0f / 1200.0f);   /* up to 30 cents */
        S.ratio2 = IV[(p->vo[1] * 5u) >> 7];
        S.polymod = knob(p->vo[2]);
        S.reso = knob(p->vo[3]) * 3.8f;
        S.drive = 1.6f;
        S.level = 0.8f;
        break;
    }
    case E_SVF:
        S.morph = knob(p->k[2]);
        S.det = fm_exp2f(knob(p->vo[0]) * 25.0f / 1200.0f);
        S.pulse2 = knob(p->vo[1]);
        S.drive = 1.0f + knob(p->vo[2]) * 4.0f;
        S.reso = knob(p->vo[3]) * 0.96f;                  /* the SVF's: 1 - its damping */
        S.level = 0.75f;
        break;
    default:
        S.drive = 1.0f + knob(p->k[2]) * 6.0f;
        S.det = fm_exp2f(knob(p->vo[0]) * 25.0f / 1200.0f);
        S.sub = knob(p->vo[1]);
        S.glide_k = p->vo[2] ? 1.0f - fm_expf(-ISR * 4.6f / ktime(p->vo[2], 0.01f, 1.0f)) : 1.0f;
        S.reso = knob(p->vo[3]) * 3.95f;
        S.level = 0.7f;
        break;
    }
}

static uint32_t rnd_state = 0x1234567u;
static inline uint32_t rnd(void)
{
    rnd_state ^= rnd_state << 13;
    rnd_state ^= rnd_state >> 17;
    rnd_state ^= rnd_state << 5;
    return rnd_state;
}
static inline float rndf(void) { return (float)(rnd() >> 8) * (1.0f / 8388608.0f) - 1.0f; }   /* -1 .. 1 */

static float note_inc(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)) * ISR; }

/* a voice for a note: a free one, else the oldest released, else the oldest */
static voice_t *voice_take(void)
{
    uint32_t i, best = 0, oldest = 0xFFFFFFFFu;
    int pick = -1;
    for (i = 0; i < NVOICES; i++)
        if (V[i].stage == 0)
            return &V[i];
    for (i = 0; i < NVOICES; i++)
        if (V[i].stage == 3 && V[i].age < oldest) {
            oldest = V[i].age;
            pick = (int)i;
        }
    if (pick >= 0)
        return &V[pick];
    oldest = 0xFFFFFFFFu;
    for (i = 0; i < NVOICES; i++)
        if (V[i].age < oldest) {
            oldest = V[i].age;
            best = i;
        }
    return &V[best];
}

static uint32_t notes_played;
static uint32_t chunk_at;              /* the render's place in the block while the player runs (a note waits for it) */
static void voice_on(int note, float vel, int owner, uint32_t wait)
{
    voice_t *v = voice_take();
    float inc = note_inc((float)note);
    int glide = S.eng == E_LADDER && S.glide_k < 1.0f && v->stage != 0;
    if (!glide) {
        v->inc[0] = inc;
        v->ph[0] = v->ph[1] = v->ph[2] = 0.0f;
        if (S.eng != E_DCO) {                             /* free-running oscillators: no two voices alike */
            v->ph[1] = (float)(rnd() >> 9) * (1.0f / 8388608.0f);
            v->ph[2] = (float)(rnd() >> 9) * (1.0f / 8388608.0f);
        }
        v->s[0] = v->s[1] = v->s[2] = v->s[3] = 0.0f;
        v->env = 0.0f;
    }
    v->tinc = inc;
    v->note = (int16_t)note;
    v->vel = vel;
    v->owner = (uint8_t)owner;
    v->stage = 1;
    v->wait = wait + chunk_at;
    v->age = ++vage;
    v->fade = S.lfo_fade_inc >= 1.0f ? 1.0f : 0.0f;
    v->drift = rndf() * (S.eng == E_DCO ? 0.6f : 3.5f);
    v->pan = ((float)(vage % 5u) - 2.0f) * 0.12f;
    v->noise = rnd() | 1u;
    notes_played++;
}

static void voice_off(int note, int owner)
{
    uint32_t i;
    for (i = 0; i < NVOICES; i++)
        if (V[i].note == note && V[i].owner == owner && V[i].stage && V[i].stage != 3) {
            if (V[i].wait) {                              /* (not started yet: never mind) */
                V[i].stage = 0;
                V[i].wait = 0;
            } else {
                V[i].stage = 3;
            }
        }
}

static void voices_owner_off(int owner)
{
    uint32_t i;
    for (i = 0; i < NVOICES; i++)
        if (V[i].owner == owner && V[i].stage && V[i].stage != 3) {
            if (V[i].wait) {
                V[i].stage = 0;
                V[i].wait = 0;
            } else {
                V[i].stage = 3;
            }
        }
}

/* the filter for this chunk: cutoff from the knob, the envelope, the key, the LFO and the velocity */
static inline void voice_filter(voice_t *v, float lfo)
{
    float oct = S.cut_oct + v->env * S.env_oct * (0.6f + 0.4f * v->vel) + ((float)v->note - 60.0f) * (S.key_oct / 12.0f);
    float fc, w;
    if (S.lfo_tgt == LFO_FILTER)
        oct += lfo * 3.0f;
    if (S.eng == E_VCO && S.polymod > 0.0f)               /* poly mod: the envelope's share opens it further */
        oct += v->env * S.polymod * 2.0f;
    oct = fm_clampf(oct, 4.3f, 14.1f);
    fc = fm_exp2f(oct);
    w = fc * (3.14159265f * ISR);
    if (w > 1.35f)
        w = 1.35f;
    v->g = fm_tanf(w);
    v->k = S.reso;
}

/* one voice into L / R for n samples (n a multiple of CHUNK) */
static void voice_render(voice_t *v, float *L, float *R, uint32_t n, const float *lfo_buf)
{
    uint32_t i, c;
    float gl = (0.5f - v->pan) * S.level, gr = (0.5f + v->pan) * S.level;
    float sh = S.shape, pw = S.pw, morph = S.morph, drive = S.drive;
    for (c = 0; c < n; c += CHUNK) {
        float lfo = lfo_buf[c / CHUNK] * S.lfo_depth * v->fade, pmul, inc0, G, G2, G4, kk;
        if (v->wait >= CHUNK) {
            v->wait -= CHUNK;
            continue;
        }
        voice_filter(v, lfo);
        pmul = fm_exp2f(v->drift * (1.0f / 1200.0f) + (S.lfo_tgt == LFO_PITCH ? lfo * (0.5f / 12.0f) : 0.0f));
        if (S.lfo_tgt == LFO_SHAPE) {
            pw = fm_clampf(S.pw - lfo * 0.4f, 0.05f, 0.5f);
            sh = fm_clampf(S.shape + lfo * 0.33f, 0.0f, 1.0f);
            morph = fm_clampf(S.morph + lfo * 0.5f, 0.0f, 1.0f);
            drive = S.drive * (1.0f + lfo * 0.5f);
        }
        if (v->fade < 1.0f)
            v->fade = fm_minf(v->fade + S.lfo_fade_inc * (float)CHUNK, 1.0f);
        G = v->g / (1.0f + v->g);
        G2 = G * G;
        G4 = G2 * G2;
        kk = v->k;
        for (i = c; i < c + CHUNK; i++) {
            float x, y, e;
            if (v->wait) {
                v->wait--;
                continue;
            }
            /* the envelope */
            switch (v->stage) {
            case 1:
                v->env += S.a_inc;
                if (v->env >= 1.0f) {
                    v->env = 1.0f;
                    v->stage = 2;
                }
                break;
            case 2:
                v->env = S.sus + (v->env - S.sus) * S.d_k;
                break;
            case 3:
                v->env *= S.r_k;
                if (v->env < 0.0002f) {
                    v->env = 0.0f;
                    v->stage = 0;
                }
                break;
            default:
                break;
            }
            if (!v->stage)
                break;
            /* glide */
            v->inc[0] += (v->tinc - v->inc[0]) * S.glide_k;
            inc0 = v->inc[0] * pmul;
            /* the oscillators and the filter, by engine */
            switch (S.eng) {
            case E_DCO: {
                float sub;
                v->ph[0] += inc0;
                if (v->ph[0] >= 1.0f) {
                    v->ph[0] -= 1.0f;
                    v->subph = v->subph > 0.0f ? -1.0f : 1.0f;
                }
                sub = v->subph;
                v->noise ^= v->noise << 13;
                v->noise ^= v->noise >> 17;
                v->noise ^= v->noise << 5;
                x = 0.55f * osc_saw(v->ph[0], inc0) + 0.45f * osc_pulse(v->ph[0], inc0, pw) + sub * S.sub * 0.6f +
                    ((float)(v->noise >> 8) * (1.0f / 8388608.0f) - 1.0f) * S.noise * 0.5f;
                break;
            }
            case E_VCO: {
                float inc2 = inc0 * S.ratio2 * S.det, o2;
                v->ph[1] = wrap1(v->ph[1] + inc2);
                o2 = osc_shape(v->ph[1], inc2, sh);
                inc0 *= 1.0f + o2 * S.polymod * 0.35f;    /* poly mod: osc 2 bends osc 1 */
                if (inc0 < 0.0f)
                    inc0 = 0.0f;
                v->ph[0] = wrap1(v->ph[0] + inc0);
                x = 0.5f * (osc_shape(v->ph[0], inc0, sh) + o2);
                break;
            }
            case E_SVF: {
                float inc2 = inc0 * S.det;
                v->ph[0] = wrap1(v->ph[0] + inc0 / S.det);
                v->ph[1] = wrap1(v->ph[1] + inc2);
                x = 0.5f * (osc_saw(v->ph[0], inc0) + (1.0f - S.pulse2) * osc_saw(v->ph[1], inc2) +
                            S.pulse2 * osc_pulse(v->ph[1], inc2, 0.35f));
                break;
            }
            default: {
                float inc1 = inc0 * S.det, inc2 = inc0 * 0.5f;
                v->ph[0] = wrap1(v->ph[0] + inc0 / S.det);
                v->ph[1] = wrap1(v->ph[1] + inc1);
                v->ph[2] = wrap1(v->ph[2] + inc2);
                x = 0.45f * (osc_saw(v->ph[0], inc0) + osc_saw(v->ph[1], inc1)) +
                    S.sub * 0.6f * osc_pulse(v->ph[2], inc2, 0.5f);
                break;
            }
            }
            if (S.eng == E_SVF) {
                /* the two-pole state variable (TPT): low-pass, band-pass, high-pass; the mode morphs LP -> notch -> HP */
                float g = v->g, R = 1.0f - kk, hp, bp, lp;
                x = sat(x * drive * 0.7f);
                hp = (x - (2.0f * R + g) * v->s[0] - v->s[1]) / (1.0f + 2.0f * R * g + g * g);
                bp = g * hp + v->s[0];
                v->s[0] = g * hp + bp;
                lp = g * bp + v->s[1];
                v->s[1] = g * bp + lp;
                y = (1.0f - morph) * lp + morph * hp + bp * (morph * (1.0f - morph)) * 1.2f;
            } else {
                /* the four-pole ladder (zero-delay feedback), the input driven */
                float Sg = (G2 * G * v->s[0] + G2 * v->s[1] + G * v->s[2] + v->s[3]) / (1.0f + v->g), u, a, t;
                u = (sat(x * drive * 0.6f) - kk * Sg) / (1.0f + kk * G4);
                u = sat(u);
                a = (u - v->s[0]) * G;
                t = a + v->s[0];
                v->s[0] = t + a;
                a = (t - v->s[1]) * G;
                t = a + v->s[1];
                v->s[1] = t + a;
                a = (t - v->s[2]) * G;
                t = a + v->s[2];
                v->s[2] = t + a;
                a = (t - v->s[3]) * G;
                y = a + v->s[3];
                v->s[3] = y + a;
                y *= 1.0f + kk * 0.45f;                    /* the low end kept as the resonance rises */
            }
            e = v->env * (0.55f + 0.45f * v->vel);
            if (S.lfo_tgt == LFO_AMP)
                e *= 1.0f - 0.5f * (lfo + S.lfo_depth * v->fade) * 0.5f;
            y *= e;
            L[i] += y * gl;
            R[i] += y * gr;
        }
        if (!v->stage)
            break;
    }
}
