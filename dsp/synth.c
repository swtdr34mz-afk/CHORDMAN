/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's voices: the analog ones (Juno, Oberheim, Moog: band-limited oscillators, each filter run at twice the
 * rate) and the FM one (the DX7's six operators in algorithm 5). Ten voices shared by the two parts; a mono part
 * keeps one, legato, gliding. Part of dsp/engine.c. */
#define SR 44100.0f
#define ISR (1.0f / 44100.0f)
#define CHUNK 8u
#define NV 10

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
static inline float wrap1(float p) { return p >= 1.0f ? p - 1.0f : p; }
static inline float sat(float x)         /* tanh's shape */
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

/* the sine for the operators: a table and a straight line between its points */
#define SIN_N 1024u
static float sin_t[SIN_N + 1];
static void sin_init(void)
{
    uint32_t i;
    for (i = 0; i <= SIN_N; i++)
        sin_t[i] = fm_sin_turns((float)i / (float)SIN_N);
}
static inline float sinp(float ph)       /* ph in turns, any value */
{
    float x = ph - fm_floorf(ph);
    float f = x * (float)SIN_N;
    uint32_t i = (uint32_t)f;
    if (i >= SIN_N)
        i = SIN_N - 1u;
    f -= (float)i;
    return sin_t[i] + (sin_t[i + 1] - sin_t[i]) * f;
}

typedef struct {
    uint8_t stage;                       /* 0 idle, 1 attack, 2 decay / sustain, 3 release */
    float v;
} env_t;

typedef struct {
    uint8_t part, owner;                 /* owner: 0 a key or MIDI, 1 the player */
    int16_t note;
    uint32_t age, wait;                  /* wait: samples before it sounds (a strum, the swing, the chunk) */
    float vel, inc, tinc;                /* the pitch now and where glide takes it (cycles a sample) */
    env_t amp, flt;
    /* analog */
    float ph[3], subph, s[4], drift, dwalk, vib_t;
    uint32_t noise;
    /* FM */
    float oph[6], oenv[6], fbmem;
    uint8_t ostage;
    float pan;
    float outl, outr;                    /* its last output (a stolen voice fades that out: no click) */
    float taill, tailr;
} voice_t;
static voice_t V[NV];

typedef struct {                         /* a part's sound, worked out at a block */
    const sound_t *s;
    int type, mono, chorus;
    float a_inc, d_k, sus, r_k;          /* the amp envelope */
    float fa_inc, fd_k, fsus, fr_k;      /* the filter's (analog) */
    float cut_oct, fenv, reso, drive, detune_ratio, glide_k;
    float idx_scale, dec_scale, shine;   /* FM: TONE, MOTION, CHAR */
    float odk[6];                        /* FM: each operator's decay (before the key's scaling) */
    float rev, dly;                      /* SPACE: the sends */
    float level;
    float duck_oct;                      /* the sidechain's pull on the filter (octaves, negative) */
    int lfo_tgt, lfo_wave, lfo_rate;
    float lfo_depth;
} psnd_t;
static psnd_t PS[NPARTS];
static uint32_t vage, notes_played, chunk_at;

static inline float knob(uint8_t v) { return (float)v * (1.0f / 127.0f); }
static float ktime(uint8_t v, float lo, float hi) { return lo * fm_powf(hi / lo, knob(v)); }
static float ek(float t) { return fm_expf(-ISR * 4.6f / (t > 0.0005f ? t : 0.0005f)); }   /* ~1 % left after t */

static void psnd_from(int p, const part_t *pt)
{
    psnd_t *q = &PS[p];
    const sound_t *s = &SOUNDS[pt->snd < NSOUNDS ? pt->snd : 0];
    float t = (float)pt->k[0] - 64.0f, m = (float)pt->k[1] - 64.0f;
    uint32_t i;
    q->s = s;
    q->type = s->type;
    q->mono = s->mono;
    q->a_inc = ISR / ktime(pt->env[0], 0.001f, 4.0f);
    q->d_k = ek(ktime(pt->env[1], 0.02f, 8.0f));
    q->sus = knob(pt->env[2]);
    q->r_k = ek(ktime(pt->env[3], 0.01f, 8.0f));
    q->rev = knob(pt->k[3]) * 0.55f;
    q->dly = pt->k[3] > 70 ? (float)(pt->k[3] - 70) * (0.45f / 57.0f) : 0.0f;
    q->chorus = 0;
    q->level = 1.0f;
    q->lfo_rate = pt->lfo[0] < NLRATES ? pt->lfo[0] : LR_4TH;
    q->lfo_depth = knob(pt->lfo[1]);
    q->lfo_wave = pt->lfo[2] < NLWAVES ? pt->lfo[2] : 0;
    q->lfo_tgt = pt->lfo[3] < NLTGTS ? pt->lfo[3] : 0;
    if (s->type == T_FM) {
        q->idx_scale = fm_exp2f(t / 40.0f);
        q->dec_scale = fm_exp2f(((float)pt->env[1] - 64.0f) / 32.0f);
        q->shine = knob(pt->k[2]) * 2.0f;
        for (i = 0; i < 6; i++)
            q->odk[i] = s->op_dec[i] * (i & 1u ? fm_exp2f(m / 32.0f) : 1.0f);
        q->level = 0.8f;
        return;
    }
    q->fa_inc = ISR / (s->fa > 0.0005f ? s->fa : 0.0005f);
    q->fd_k = ek(s->fd);
    q->fsus = s->fs;
    q->fr_k = ek(s->fr);
    q->cut_oct = fm_log2f(s->cutoff) + t * (3.0f / 64.0f);
    q->fenv = s->fenv * fm_exp2f(m / 40.0f);
    q->reso = s->reso;
    q->drive = s->drive;
    q->detune_ratio = fm_exp2f(s->detune / 1200.0f);
    q->glide_k = s->glide > 0.0f ? 1.0f - fm_expf(-ISR * 4.6f / s->glide) : 1.0f;
    if (s->type == T_JUNO) {
        q->chorus = pt->k[2] >> 5;
        q->level = 0.85f;
    } else if (s->type == T_OB) {
        q->detune_ratio = fm_exp2f(s->detune * knob(pt->k[2]) * 2.0f / 1200.0f);
        q->level = 0.75f;
    } else {
        q->drive = s->drive * fm_exp2f(((float)pt->k[2] - 64.0f) / 48.0f);
        q->level = 0.7f;
    }
}

static uint32_t rnd_s = 0x2468ACEu;
static inline uint32_t rnd(void)
{
    rnd_s ^= rnd_s << 13;
    rnd_s ^= rnd_s >> 17;
    rnd_s ^= rnd_s << 5;
    return rnd_s;
}
static inline float rndf(void) { return (float)(rnd() >> 8) * (1.0f / 8388608.0f) - 1.0f; }
static float note_inc(float note) { return 440.0f * fm_exp2f((note - 69.0f) * (1.0f / 12.0f)) * ISR; }

static voice_t *voice_take(int part)
{
    uint32_t i, best = 0, old = 0xFFFFFFFFu;
    int pick = -1;
    for (i = 0; i < NV; i++)
        if (!V[i].amp.stage)
            return &V[i];
    {                                    /* the quietest released one */
        float lo = 2.0f;
        for (i = 0; i < NV; i++)
            if (V[i].amp.stage == 3 && V[i].amp.v < lo) {
                lo = V[i].amp.v;
                pick = (int)i;
            }
    }
    if (pick >= 0)
        return &V[pick];
    for (i = 0; i < NV; i++)
        if (V[i].part == part && V[i].age < old) {
            old = V[i].age;
            pick = (int)i;
        }
    if (pick >= 0)
        return &V[pick];
    for (i = 0; i < NV; i++)
        if (V[i].age < old) {
            old = V[i].age;
            best = i;
        }
    return &V[best];
}

static voice_t *mono_voice(int part)     /* a mono part's voice that sounds, if one does */
{
    uint32_t i;
    for (i = 0; i < NV; i++)
        if (V[i].part == part && V[i].amp.stage && V[i].amp.stage != 3)
            return &V[i];
    return 0;
}

static void voice_on(int part, int note, float vel, int owner, uint32_t wait)
{
    psnd_t *q = &PS[part];
    voice_t *v = 0;
    float inc = note_inc((float)note);
    uint32_t i;
    if (q->mono && (v = mono_voice(part)) != 0 && !v->wait) {
        /* legato: the same voice glides to the new note */
        v->tinc = inc;
        v->note = (int16_t)note;
        v->owner = (uint8_t)owner;
        v->age = ++vage;
        notes_played++;
        return;
    }
    if (q->mono)
        for (i = 0; i < NV; i++)         /* a mono part: its other voices let go */
            if (V[i].part == part && V[i].amp.stage && V[i].amp.stage != 3)
                V[i].amp.stage = 3;
    v = voice_take(part);
    if (v->amp.stage) {                  /* taken while it sounds: what it played fades out over a few ms */
        v->taill += v->outl;
        v->tailr += v->outr;
    }
    v->part = (uint8_t)part;
    v->owner = (uint8_t)owner;
    v->note = (int16_t)note;
    v->vel = vel;
    v->inc = v->tinc = inc;
    v->wait = wait + chunk_at;
    v->age = ++vage;
    v->amp.stage = 1;
    v->amp.v = 0.0f;
    v->flt.stage = 1;
    v->flt.v = 0.0f;
    v->vib_t = 0.0f;
    v->pan = ((float)(vage % 5u) - 2.0f) * (q->type == T_OB ? 0.16f : 0.1f);
    v->drift = rndf() * (q->type == T_JUNO ? 0.8f : 3.0f);
    v->dwalk = 0.0f;
    v->noise = rnd() | 1u;
    v->subph = 1.0f;
    for (i = 0; i < 4; i++)
        v->s[i] = 0.0f;
    if (q->type == T_FM) {
        for (i = 0; i < 6; i++) {
            v->oph[i] = 0.0f;
            v->oenv[i] = 0.0f;
        }
        v->fbmem = 0.0f;
        v->ostage = 1;
    } else {
        v->ph[0] = q->type == T_JUNO ? 0.0f : (float)(rnd() >> 9) * (1.0f / 8388608.0f);
        v->ph[1] = (float)(rnd() >> 9) * (1.0f / 8388608.0f);
        v->ph[2] = (float)(rnd() >> 9) * (1.0f / 8388608.0f);
    }
    notes_played++;
}

static void voice_release(voice_t *v)
{
    if (v->wait) {                       /* never started: gone */
        v->amp.stage = 0;
        v->wait = 0;
        return;
    }
    v->amp.stage = 3;
    v->flt.stage = 3;
    v->ostage = 3;
}

static void voice_off(int part, int note, int owner)
{
    uint32_t i;
    for (i = 0; i < NV; i++)
        if (V[i].part == part && V[i].note == note && V[i].owner == owner && V[i].amp.stage && V[i].amp.stage != 3)
            voice_release(&V[i]);
}
static void voices_off(int part, int owner)   /* owner -1: all of the part's */
{
    uint32_t i;
    for (i = 0; i < NV; i++)
        if (V[i].part == part && (owner < 0 || V[i].owner == owner) && V[i].amp.stage && V[i].amp.stage != 3)
            voice_release(&V[i]);
}

static inline float env_step(env_t *e, float a_inc, float d_k, float sus, float r_k)
{
    switch (e->stage) {
    case 1:
        e->v += a_inc;
        if (e->v >= 1.0f) {
            e->v = 1.0f;
            e->stage = 2;
        }
        break;
    case 2:
        e->v = sus + (e->v - sus) * d_k;
        break;
    case 3:
        e->v *= r_k;
        if (e->v < 0.0002f) {
            e->v = 0.0f;
            e->stage = 0;
        }
        break;
    default:
        break;
    }
    return e->v;
}

/* an analog voice into L / R */
static void render_analog(voice_t *v, const psnd_t *q, float *L, float *R, uint32_t n, const float *lfo, const float *ulfo)
{
    const sound_t *s = q->s;
    uint32_t c, i;
    float gl = (0.5f - v->pan) * q->level, gr = (0.5f + v->pan) * q->level;
    float r2 = fm_exp2f(s->o2_semi / 12.0f) * q->detune_ratio, r3 = fm_exp2f(s->o3_semi / 12.0f);
    float r1 = q->type == T_OB ? 1.0f / q->detune_ratio : 1.0f;
    for (c = 0; c < n; c += CHUNK) {
        float l = lfo[c / CHUNK], u = ulfo[c / CHUNK] * q->lfo_depth, vib = 0.0f, oct, fc, w, g, G, G2, G4, k, pw, pm,
              keyo, ag = 1.0f;
        if (v->wait >= CHUNK) {
            v->wait -= CHUNK;
            for (i = c; i < c + CHUNK; i++) {    /* (what it played before still fades) */
                L[i] += v->taill;
                R[i] += v->tailr;
                v->taill *= 0.992f;
                v->tailr *= 0.992f;
            }
            continue;
        }
        /* pitch: glide, drift (a slow walk), vibrato after its delay */
        v->dwalk += (rndf() * 0.02f - v->dwalk * 0.002f);
        v->vib_t += (float)CHUNK * ISR;
        if (s->vib > 0.0f && v->vib_t > s->vib_delay)
            vib = l * s->vib * fm_minf((v->vib_t - s->vib_delay) * 2.0f, 1.0f);
        if (q->lfo_tgt == LT_PITCH)
            vib += u * 2.0f;
        pm = fm_exp2f((v->drift + v->dwalk) * (1.0f / 1200.0f) + vib * (1.0f / 12.0f));
        pw = fm_clampf(s->pw - l * s->pwm * 0.4f - (q->lfo_tgt == LT_PWM ? u * 0.4f : 0.0f), 0.06f, 0.5f);
        if (q->lfo_tgt == LT_AMP)
            ag = 1.0f - q->lfo_depth * 0.5f + u * 0.5f;
        /* the filter: its envelope, the key, the sidechain; at twice the rate */
        keyo = ((float)v->note - 60.0f) * s->keytrk / 12.0f;
        oct = q->cut_oct + v->flt.v * q->fenv * (0.65f + 0.35f * v->vel) + keyo + q->duck_oct +
              (q->lfo_tgt == LT_FILTER ? u * 3.0f : 0.0f);
        oct = fm_clampf(oct, 4.3f, 14.2f);
        fc = fm_exp2f(oct);
        w = fc * (3.14159265f * 0.5f * ISR);
        if (w > 1.3f)
            w = 1.3f;
        g = fm_tanf(w);
        G = g / (1.0f + g);
        G2 = G * G;
        G4 = G2 * G2;
        k = q->type == T_OB ? q->reso * 0.97f : q->reso * 4.0f;
        for (i = c; i < c + CHUNK; i++) {
            float inc, x, y, e, ya;
            int h;
            if (v->wait) {
                v->wait--;
                L[i] += v->taill;
                R[i] += v->tailr;
                v->taill *= 0.992f;
                v->tailr *= 0.992f;
                continue;
            }
            e = env_step(&v->amp, q->a_inc, q->d_k, q->sus, q->r_k);
            if (!v->amp.stage)
                break;
            env_step(&v->flt, q->fa_inc, q->fd_k, q->fsus, q->fr_k);
            v->inc += (v->tinc - v->inc) * q->glide_k;
            inc = v->inc * pm;
            /* the oscillators */
            v->ph[0] += inc * r1;
            if (v->ph[0] >= 1.0f) {
                v->ph[0] -= 1.0f;
                v->subph = -v->subph;
            }
            x = s->saw * osc_saw(v->ph[0], inc * r1) + s->pulse * osc_pulse(v->ph[0], inc * r1, pw) + s->sub * v->subph;
            if (s->o2 > 0.0f) {
                float i2 = inc * r2;
                v->ph[1] = wrap1(v->ph[1] + i2);
                x += s->o2 * osc_saw(v->ph[1], i2);
            }
            if (s->o3 > 0.0f) {
                float i3 = inc * r3;
                v->ph[2] = wrap1(v->ph[2] + i3);
                x += s->o3 * osc_pulse(v->ph[2], i3, 0.5f);
            }
            if (s->noise > 0.0f) {
                v->noise ^= v->noise << 13;
                v->noise ^= v->noise >> 17;
                v->noise ^= v->noise << 5;
                x += s->noise * ((float)(v->noise >> 8) * (1.0f / 8388608.0f) - 1.0f);
            }
            x *= 0.5f;
            ya = 0.0f;
            for (h = 0; h < 2; h++) {    /* the filter, twice a sample */
                if (q->type == T_OB) {
                    /* two-pole state variable, low-pass, its input pushed a little */
                    float R2 = 1.0f - k, hp, bp, lp, xin = sat(x * q->drive);
                    hp = (xin - (2.0f * R2 + g) * v->s[0] - v->s[1]) / (1.0f + 2.0f * R2 * g + g * g);
                    bp = g * hp + v->s[0];
                    v->s[0] = g * hp + bp;
                    lp = g * bp + v->s[1];
                    v->s[1] = g * bp + lp;
                    y = lp;
                } else {
                    /* four-pole ladder (zero-delay feedback), driven; the Juno's softer, the Moog's harder */
                    float Sg = (G2 * G * v->s[0] + G2 * v->s[1] + G * v->s[2] + v->s[3]) / (1.0f + g), u, a, t2;
                    u = (sat(x * q->drive) - k * Sg) / (1.0f + k * G4);
                    u = sat(u * (q->type == T_MOOG ? 1.2f : 0.9f));
                    a = (u - v->s[0]) * G;
                    t2 = a + v->s[0];
                    v->s[0] = t2 + a;
                    a = (sat(t2) - v->s[1]) * G;
                    t2 = a + v->s[1];
                    v->s[1] = t2 + a;
                    a = (t2 - v->s[2]) * G;
                    t2 = a + v->s[2];
                    v->s[2] = t2 + a;
                    a = (t2 - v->s[3]) * G;
                    y = a + v->s[3];
                    v->s[3] = y + a;
                    y *= 1.0f + k * 0.4f;
                }
                ya += y;
            }
            y = ya * 0.5f * e * ag * (0.6f + 0.4f * v->vel);
            v->outl = y * gl;
            v->outr = y * gr;
            L[i] += v->outl + v->taill;
            R[i] += v->outr + v->tailr;
            v->taill *= 0.992f;
            v->tailr *= 0.992f;
        }
        if (!v->amp.stage)
            break;
    }
}

/* an FM voice: algorithm 5, three pairs (2 -> 1, 4 -> 3, 6 -> 5, 6 feeding back) */
static void render_fm(voice_t *v, const psnd_t *q, float *L, float *R, uint32_t n, const float *ulfo)
{
    const sound_t *s = q->s;
    uint32_t c, i, o;
    float gl = (0.5f - v->pan) * q->level, gr = (0.5f + v->pan) * q->level;
    float key = fm_exp2f(-((float)v->note - 60.0f) / 24.0f);   /* high notes die sooner */
    float inc[6], dk[6], lvl[6], idx_hi = fm_clampf(1.15f - ((float)v->note - 60.0f) / 60.0f, 0.4f, 1.3f);
    float rk = q->r_k, sus = q->sus;
    for (o = 0; o < 6; o++) {
        float vs = 1.0f - s->vel[o] + s->vel[o] * v->vel;
        inc[o] = v->inc * s->ratio[o] * fm_exp2f(s->fdet[o] / 1200.0f);
        dk[o] = ek(q->odk[o] * q->dec_scale * key);
        if (o & 1u) {                    /* a modulator: its index (radians to turns), TONE, CHAR on the tine */
            float sh = (o == 1u) ? (0.5f + q->shine) : 1.0f;
            lvl[o] = s->level[o] * q->idx_scale * vs * idx_hi * sh * (1.0f / 6.2831853f);
        } else {
            lvl[o] = s->level[o] * vs;
        }
    }
    for (c = 0; c < n; c += CHUNK) {
        float u = ulfo[c / CHUNK] * q->lfo_depth, im = 1.0f, ag = 1.0f, pmul = 1.0f;
        if (v->wait >= CHUNK) {
            v->wait -= CHUNK;
            for (i = c; i < c + CHUNK; i++) {    /* (what it played before still fades) */
                L[i] += v->taill;
                R[i] += v->tailr;
                v->taill *= 0.992f;
                v->tailr *= 0.992f;
            }
            continue;
        }
        if (q->lfo_tgt == LT_FILTER || q->lfo_tgt == LT_PWM)
            im = fm_maxf(1.0f + u, 0.0f);        /* the brightness */
        else if (q->lfo_tgt == LT_AMP)
            ag = 1.0f - q->lfo_depth * 0.5f + u * 0.5f;
        else
            pmul = fm_exp2f(u * 2.0f / 12.0f);
        for (i = c; i < c + CHUNK; i++) {
            float m2, m4, m6, y, ea;
            if (v->wait) {
                v->wait--;
                L[i] += v->taill;
                R[i] += v->tailr;
                v->taill *= 0.992f;
                v->tailr *= 0.992f;
                continue;
            }
            /* the envelopes: one attack for all, each its own decay to its sustain, one release */
            if (v->ostage == 1) {
                v->amp.v += q->a_inc;
                for (o = 0; o < 6; o++)
                    v->oenv[o] = v->amp.v;
                if (v->amp.v >= 1.0f) {
                    v->amp.v = 1.0f;
                    v->ostage = 2;
                }
            } else if (v->ostage == 2) {
                for (o = 0; o < 6; o++) {
                    float st = (o & 1u) ? s->op_sus[o] : (s->op_sus[o] > sus ? s->op_sus[o] : sus);
                    v->oenv[o] = st + (v->oenv[o] - st) * dk[o];
                }
            } else {
                for (o = 0; o < 6; o++)
                    v->oenv[o] *= rk;
            }
            ea = v->oenv[0] + v->oenv[2] + v->oenv[4];
            if (v->ostage != 1 && ea < 0.0006f) {
                v->amp.stage = 0;
                break;
            }
            for (o = 0; o < 6; o++) {
                v->oph[o] += inc[o] * pmul;
                if (v->oph[o] >= 1.0f)
                    v->oph[o] -= 1.0f;
            }
            m6 = sinp(v->oph[5] + v->fbmem * s->fb * 0.25f) * v->oenv[5] * lvl[5];
            v->fbmem = 0.5f * (v->fbmem + m6 / (lvl[5] > 0.0001f ? lvl[5] : 1.0f));
            m6 *= im;
            m2 = sinp(v->oph[1]) * v->oenv[1] * lvl[1] * im;
            m4 = sinp(v->oph[3]) * v->oenv[3] * lvl[3] * im;
            y = sinp(v->oph[0] + m2) * v->oenv[0] * lvl[0] + sinp(v->oph[2] + m4) * v->oenv[2] * lvl[2] +
                sinp(v->oph[4] + m6) * v->oenv[4] * lvl[4];
            y *= 0.55f * ag;
            v->outl = y * gl;
            v->outr = y * gr;
            L[i] += v->outl + v->taill;
            R[i] += v->outr + v->tailr;
            v->taill *= 0.992f;
            v->tailr *= 0.992f;
        }
        if (!v->amp.stage)
            break;
    }
}
