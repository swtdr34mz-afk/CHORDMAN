/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's master effects: the BBD-style chorus (the DCO's CHAR: I, II, I+II), the ping-pong delay (synced to
 * the tempo) and the reverb (four combs and two all-passes a side), both fed by SPACE; a soft limit at the end.
 * Part of dsp/engine.c. */
#define CH_N 1024u                       /* the chorus: 23 ms of line */
#define DL_N 24576u                      /* the delay, per side: 0.56 s (int16) */
static float ch_buf[CH_N] PLAT_POOL;
static int16_t dl_l[DL_N] PLAT_POOL, dl_r[DL_N] PLAT_POOL;
static float rv_c[8][1700] PLAT_POOL;    /* the reverb's combs (four a side) */
static float rv_a[4][600] PLAT_POOL;     /* its all-passes (two a side) */
static const uint16_t RV_CN[8] = {1116, 1188, 1277, 1356, 1139, 1211, 1300, 1379};
static const uint16_t RV_AN[4] = {556, 441, 579, 464};

static struct {
    uint32_t ci, di, rci[8], rai[4];
    float cph, crate, cdepth, cmix;      /* the chorus's triangle, its rate (cycles a sample), depth (samples), mix */
    float dtime, dtime_t, dfb, dsend;    /* the delay (samples, gliding), its feedback and send */
    float rsend, rfb, rdamp, rlp[8];     /* the reverb's send, comb feedback, damping */
    float hp_l, hp_r;                    /* a DC blocker on the output */
    float peak;
} FX;

static const float DLY_BEATS[5] = {0.0f, 0.5f, 0.75f, 1.0f, 1.5f};   /* OFF, 1/8, 3/16, 1/4, 3/8 */
const char *const DELAY_NAME[5] = {"OFF", "1/8", "3/16", "1/4", "3/8"};

static void fx_block(const patch_t *p, const uint8_t *fx, float bpm)
{
    float sp = knob(p->k[3]);
    int ch = p->eng == E_DCO ? p->k[2] >> 5 : 0, dz = (fx[3] * 5) >> 7;
    /* the chorus: the Juno's I and II and both at once */
    if (ch == 1) {
        FX.crate = 0.513f * ISR;
        FX.cdepth = 80.0f;
    } else if (ch == 2) {
        FX.crate = 0.863f * ISR;
        FX.cdepth = 110.0f;
    } else if (ch == 3) {
        FX.crate = 9.75f * ISR;
        FX.cdepth = 14.0f;
    }
    FX.cmix += ((ch ? 0.5f : 0.0f) - FX.cmix) * 0.2f;
    /* SPACE: the reverb first, the delay joins past the middle */
    FX.rsend = sp < 0.5f ? sp * 0.7f : 0.35f + (sp - 0.5f) * 0.5f;
    FX.rfb = 0.70f + sp * 0.18f;
    FX.rdamp = 0.25f + (1.0f - sp) * 0.2f;
    FX.dsend = dz && sp > 0.35f ? (sp - 0.35f) * 0.55f : 0.0f;
    FX.dfb = 0.25f + sp * 0.3f;
    if (dz) {
        FX.dtime_t = DLY_BEATS[dz] * 60.0f / bpm * SR;
        if (FX.dtime_t > (float)(DL_N - 4u))
            FX.dtime_t = (float)(DL_N - 4u);
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
static inline int16_t to16(float x)
{
    x = fm_clampf(x, -0.999f, 0.999f) * 32767.0f;
    return (int16_t)x;
}

/* the voices' mix (L, R) through the effects, in place */
static void fx_render(float *L, float *R, uint32_t n)
{
    uint32_t i;
    float pk = FX.peak;
    for (i = 0; i < n; i++) {
        float l = L[i], r = R[i], m = 0.5f * (l + r);
        /* chorus: two taps on one line, their triangles opposite */
        if (FX.cmix > 0.001f) {
            float tri, d1, d2, w1, w2, p;
            uint32_t a;
            ch_buf[FX.ci] = m;
            FX.cph += FX.crate;
            if (FX.cph >= 1.0f)
                FX.cph -= 1.0f;
            tri = 2.0f * fm_fabsf(FX.cph - 0.5f);
            d1 = 70.0f + FX.cdepth * tri;
            d2 = 70.0f + FX.cdepth * (1.0f - tri);
            p = (float)FX.ci - d1 + (float)CH_N;
            a = (uint32_t)p;
            w1 = ch_buf[a % CH_N] + (ch_buf[(a + 1u) % CH_N] - ch_buf[a % CH_N]) * (p - (float)a);
            p = (float)FX.ci - d2 + (float)CH_N;
            a = (uint32_t)p;
            w2 = ch_buf[a % CH_N] + (ch_buf[(a + 1u) % CH_N] - ch_buf[a % CH_N]) * (p - (float)a);
            if (++FX.ci >= CH_N)
                FX.ci = 0;
            l = l * (1.0f - FX.cmix * 0.5f) + w1 * FX.cmix;
            r = r * (1.0f - FX.cmix * 0.5f) + w2 * FX.cmix;
            m = 0.5f * (l + r);
        }
        /* the delay: ping-pong, the left tap feeding the right */
        if (FX.dsend > 0.0f || FX.dtime > 1.0f) {
            float dl, dr;
            FX.dtime += (FX.dtime_t - FX.dtime) * 0.0005f;
            dl = dl_read(dl_l, (float)FX.di - FX.dtime);
            dr = dl_read(dl_r, (float)FX.di - FX.dtime);
            dl_l[FX.di] = to16(m * FX.dsend + dr * FX.dfb);
            dl_r[FX.di] = to16(dl * FX.dfb);
            if (++FX.di >= DL_N)
                FX.di = 0;
            l += dl;
            r += dr;
        }
        /* the reverb */
        if (FX.rsend > 0.0f) {
            float in = m * FX.rsend * 0.3f, cl = 0.0f, cr = 0.0f;
            uint32_t k;
            for (k = 0; k < 4u; k++) {
                cl += comb(rv_c[k], RV_CN[k], &FX.rci[k], &FX.rlp[k], in, FX.rfb, FX.rdamp);
                cr += comb(rv_c[k + 4], RV_CN[k + 4], &FX.rci[k + 4], &FX.rlp[k + 4], in, FX.rfb, FX.rdamp);
            }
            cl = allpass(rv_a[1], RV_AN[1], &FX.rai[1], allpass(rv_a[0], RV_AN[0], &FX.rai[0], cl));
            cr = allpass(rv_a[3], RV_AN[3], &FX.rai[3], allpass(rv_a[2], RV_AN[2], &FX.rai[2], cr));
            l += cl;
            r += cr;
        }
        /* DC out, and a soft limit */
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
}
