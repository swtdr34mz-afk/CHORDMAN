/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's effects: the Juno's BBD chorus on each part that wants it (I, II, I+II: two delay lines on opposite
 * triangles, darkened as a bucket brigade is), the sidechain (the parts ducked on a kick pattern, their volume or
 * their filters), a ping-pong delay synced to the tempo, a plate-ish reverb, a soft limit. Part of dsp/engine.c. */
#define CH_N 1024u
#define DL_N 24576u
static float ch_buf[NPARTS][CH_N] PLAT_POOL;
static int16_t dl_l[DL_N] PLAT_POOL, dl_r[DL_N] PLAT_POOL;
static float rv_c[8][1700] PLAT_POOL;
static float rv_a[4][700] PLAT_POOL;
static const uint16_t RV_CN[8] = {1116, 1188, 1277, 1356, 1139, 1211, 1300, 1379};
static const uint16_t RV_AN[4] = {556, 441, 579, 464};

typedef struct {
    uint32_t i;
    float ph, rate, depth, mix, lp_l, lp_r;
} chorus_t;
static chorus_t CH[NPARTS];

static struct {
    uint32_t di, rci[8], rai[4];
    float dtime, dtime_t, dfb;
    float rlp[8];
    float hp_l, hp_r, peak;
    float duck;                          /* the sidechain's cut now (0..1) */
    float since;                         /* samples since the last kick */
    float beat;                          /* samples a beat */
    uint32_t kick_at[8], nkick;          /* this block's kicks: where they fall */
} FX;

/* the sidechain's shape after a kick, t in beats: SOFT, PUMP, HARD, GATE */
static float duck_shape(int shape, float t)
{
    switch (shape) {
    case SH_SOFT:
        return t < 0.5f ? 1.0f - t * t * 4.0f * (1.5f - t * 2.0f) : 0.0f;
    case SH_HARD:
        return t < 0.12f ? 1.0f : fm_expf(-(t - 0.12f) * 14.0f);
    case SH_GATE:
        return t < 0.45f ? 1.0f : 0.0f;
    default:
        return fm_expf(-t * 5.0f);
    }
}

static void chorus_set(chorus_t *c, int mode)
{
    if (mode == 1) {                     /* the Juno's I and II and both */
        c->rate = 0.513f * ISR;
        c->depth = 78.0f;
    } else if (mode == 2) {
        c->rate = 0.863f * ISR;
        c->depth = 110.0f;
    } else if (mode == 3) {
        c->rate = 9.75f * ISR;
        c->depth = 12.0f;
    }
    c->mix += ((mode ? 0.55f : 0.0f) - c->mix) * 0.25f;
}

static inline float ch_tap(const float *b, float pos)
{
    uint32_t a;
    float f;
    pos += (float)CH_N;
    a = (uint32_t)pos;
    f = pos - (float)a;
    return b[a % CH_N] + (b[(a + 1u) % CH_N] - b[a % CH_N]) * f;
}

static void chorus_run(int p, float *L, float *R, uint32_t n)
{
    chorus_t *c = &CH[p];
    float *b = ch_buf[p];
    uint32_t i;
    if (c->mix < 0.002f)
        return;
    for (i = 0; i < n; i++) {
        float m = 0.5f * (L[i] + R[i]), tri, w1, w2;
        b[c->i] = m;
        c->ph += c->rate;
        if (c->ph >= 1.0f)
            c->ph -= 1.0f;
        tri = 2.0f * fm_fabsf(c->ph - 0.5f);
        w1 = ch_tap(b, (float)c->i - 70.0f - c->depth * tri);
        w2 = ch_tap(b, (float)c->i - 70.0f - c->depth * (1.0f - tri));
        if (++c->i >= CH_N)
            c->i = 0;
        c->lp_l += (w1 - c->lp_l) * 0.45f;   /* the bucket brigade's darkness */
        c->lp_r += (w2 - c->lp_r) * 0.45f;
        L[i] = L[i] * (1.0f - c->mix * 0.45f) + c->lp_l * c->mix;
        R[i] = R[i] * (1.0f - c->mix * 0.45f) + c->lp_r * c->mix;
    }
}

static inline float comb(float *b, uint32_t n, uint32_t *i, float *lp, float in, float fb, float damp)
{
    float y = b[*i];
    *lp = y + (*lp - y) * damp;
    b[*i] = in + *lp * fb;
    if (++*i >= n)
        *i = 0;
    return y;
}
static inline float allpass(float *b, uint32_t n, uint32_t *i, float in)
{
    float y = b[*i];
    b[*i] = in + y * 0.5f;
    if (++*i >= n)
        *i = 0;
    return y - in;
}
static inline float dl_read(const int16_t *b, float pos)
{
    uint32_t i0, i1;
    float fr;
    if (pos < 0.0f)
        pos += (float)DL_N;
    i0 = (uint32_t)pos;
    fr = pos - (float)i0;
    i0 %= DL_N;
    i1 = (i0 + 1u) % DL_N;
    return ((float)b[i0] + ((float)b[i1] - (float)b[i0]) * fr) * (1.0f / 32768.0f);
}
static inline int16_t to16(float x) { return (int16_t)(fm_clampf(x, -0.999f, 0.999f) * 32767.0f); }

/* the parts (each a stereo bus, their sends) mixed through the delay and the reverb into L, R */
static void fx_mix(float *pl[2], float *pr[2], float *L, float *R, uint32_t n, float beat_s, const uint8_t *sc)
{
    uint32_t i, k;
    float vol_duck = (sc[2] == ST_VOLUME || sc[2] == ST_BOTH) ? 1.0f : 0.0f;
    float rl = PS[0].rev, rr = PS[1].rev, dlv = PS[0].dly, drv = PS[1].dly, pk = FX.peak;
    FX.dtime_t = fm_minf(beat_s * 0.75f * SR, (float)(DL_N - 4u));   /* a dotted 8th */
    FX.dfb = 0.35f;
    float depth = (float)sc[0] * (1.0f / 127.0f), ib = 1.0f / (FX.beat > 1.0f ? FX.beat : 22050.0f), dtarget;
    uint32_t kk = 0;
    for (i = 0; i < n; i++) {
        float g;
        if (kk < FX.nkick && FX.kick_at[kk] == i) {
            FX.since = 0.0f;
            kk++;
        }
        dtarget = FX.since < 4.0f * FX.beat ? depth * duck_shape(sc[1], FX.since * ib) : 0.0f;
        FX.since += 1.0f;
        FX.duck += (dtarget - FX.duck) * 0.08f;  /* (no click) */
        g = 1.0f - FX.duck * vol_duck * 0.92f;
        float l0 = pl[0][i] * g, r0 = pr[0][i] * g, l1 = pl[1][i] * g, r1 = pr[1][i] * g;
        float l = l0 + l1, r = r0 + r1;
        float rin = (l0 + r0) * rl + (l1 + r1) * rr, din = (l0 + r0) * dlv + (l1 + r1) * drv;
        /* the delay: ping-pong */
        if (din != 0.0f || FX.dtime > 2.0f) {
            float a, b;
            FX.dtime += (FX.dtime_t - FX.dtime) * 0.0005f;
            a = dl_read(dl_l, (float)FX.di - FX.dtime);
            b = dl_read(dl_r, (float)FX.di - FX.dtime);
            dl_l[FX.di] = to16(din * 0.5f + b * FX.dfb);
            dl_r[FX.di] = to16(a * FX.dfb);
            if (++FX.di >= DL_N)
                FX.di = 0;
            l += a;
            r += b;
            rin += (a + b) * 0.3f;
        }
        /* the reverb */
        {
            float in = rin * 0.12f, cl = 0.0f, cr = 0.0f;
            for (k = 0; k < 4u; k++) {
                cl += comb(rv_c[k], RV_CN[k], &FX.rci[k], &FX.rlp[k], in, 0.84f, 0.3f);
                cr += comb(rv_c[k + 4], RV_CN[k + 4], &FX.rci[k + 4], &FX.rlp[k + 4], in, 0.84f, 0.3f);
            }
            l += allpass(rv_a[1], RV_AN[1], &FX.rai[1], allpass(rv_a[0], RV_AN[0], &FX.rai[0], cl));
            r += allpass(rv_a[3], RV_AN[3], &FX.rai[3], allpass(rv_a[2], RV_AN[2], &FX.rai[2], cr));
        }
        FX.hp_l += (l - FX.hp_l) * 0.0005f;
        FX.hp_r += (r - FX.hp_r) * 0.0005f;
        l = sat(l - FX.hp_l);
        r = sat(r - FX.hp_r);
        if (fm_fabsf(l) > pk)
            pk = fm_fabsf(l);
        L[i] = l;
        R[i] = r;
    }
    FX.peak = pk;
    FX.nkick = 0;
}
