/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's sounds: seven after famous synths, two variations each. Part of dsp/engine.c.
 *   JUNO POLY, JUNO PAD   a Juno-106: one DCO (saw, pulse with PWM, sub), its soft 24 dB filter, its BBD chorus
 *   OB BRASS              an Oberheim: two saws a few cents apart into a 12 dB state-variable filter that swells
 *   DX E.PIANO, DX BELL   a DX7: six operators in its algorithm 5 (three pairs), the tine and the bell
 *   MOOG BASS, MOOG LEAD  a Minimoog Model D: three oscillators into the driven ladder, mono, glide */
const char *const CHAR_NAME[NTYPES] = {"CHORUS", "SPREAD", "DRIVE", "SHINE"};

#define ZFM {0, 0, 0, 0, 0, 0}
#define NOFM ZFM, ZFM, ZFM, ZFM, ZFM, ZFM, 0.0f
const sound_t SOUNDS[NSOUNDS] = {
    /* name          family  type    mono  env {A D S R}     chorus
     * saw  pulse pw   pwm   sub   noise o2   o2s  o3   o3s   det
     * cutoff reso fenv keytrk  fa    fd    fs    fr     drive glide vib vibr vibd */
    {"JUNO POLY", "JUNO", T_JUNO, 0, {17, 72, 89, 68}, 1,
     1.0f, 0.0f, 0.5f, 0.0f, 0.35f, 0.0f, 0, 0, 0, 0, 0,
     1800.0f, 0.15f, 2.2f, 0.6f, 0.003f, 0.45f, 0.35f, 0.3f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f, NOFM},
    {"JUNO POLY II", "JUNO", T_JUNO, 0, {17, 81, 76, 70}, 2,
     0.8f, 0.6f, 0.4f, 0.0f, 0.2f, 0.0f, 0, 0, 0, 0, 0,
     2600.0f, 0.2f, 1.6f, 0.6f, 0.003f, 0.6f, 0.4f, 0.35f, 1.0f, 0.0f, 0.0f, 0.5f, 0.0f, NOFM},
    {"JUNO PAD", "JUNO", T_JUNO, 0, {104, 98, 114, 99}, 2,
     0.5f, 1.0f, 0.5f, 0.35f, 0.2f, 0.02f, 0, 0, 0, 0, 0,
     900.0f, 0.2f, 1.5f, 0.5f, 1.2f, 2.5f, 0.5f, 1.8f, 1.0f, 0.0f, 0.0f, 0.55f, 0.0f, NOFM},
    {"JUNO PAD WIDE", "JUNO", T_JUNO, 0, {109, 102, 108, 102}, 3,
     1.0f, 0.6f, 0.45f, 0.25f, 0.15f, 0.03f, 0, 0, 0, 0, 0,
     1400.0f, 0.15f, 1.2f, 0.5f, 1.5f, 3.0f, 0.6f, 2.2f, 1.0f, 0.0f, 0.0f, 0.4f, 0.0f, NOFM},
    {"OB BRASS", "OB", T_OB, 0, {46, 78, 108, 65}, 0,
     1.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0, 0, 0, 9.0f,
     500.0f, 0.15f, 3.2f, 0.5f, 0.08f, 0.6f, 0.45f, 0.3f, 1.4f, 0.0f, 0.12f, 5.0f, 0.6f, NOFM},
    {"OB BRASS STAB", "OB", T_OB, 0, {21, 61, 51, 57}, 0,
     1.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0, 0, 0, 12.0f,
     700.0f, 0.2f, 3.0f, 0.5f, 0.005f, 0.25f, 0.2f, 0.2f, 1.5f, 0.0f, 0.0f, 5.0f, 0.0f, NOFM},
    /* the DX7s: ratio, level (carriers: loudness; modulators: index in radians), detune (cents), decay (s),
     * sustain, velocity on the level; feedback on op 6 */
    {"DX E.PIANO", "DX", T_FM, 0, {11, 64, 0, 72}, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 5.0f, 0.0f,
     {1.0f, 14.0f, 1.0f, 1.0f, 1.0f, 1.0f},
     {0.55f, 1.3f, 0.8f, 1.7f, 0.5f, 0.9f},
     {0.0f, 0.0f, -2.5f, 0.0f, 2.5f, 0.0f},
     {1.4f, 0.22f, 3.5f, 1.6f, 3.0f, 1.2f},
     {0.0f, 0.0f, 0.0f, 0.15f, 0.0f, 0.1f},
     {0.3f, 0.9f, 0.3f, 0.85f, 0.3f, 0.6f}, 0.35f},
    {"DX E.PIANO II", "DX", T_FM, 0, {11, 64, 0, 70}, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 5.0f, 0.0f,
     {1.0f, 14.0f, 1.0f, 1.0f, 1.0f, 1.0f},
     {0.6f, 1.9f, 0.8f, 2.6f, 0.45f, 1.4f},
     {0.0f, 0.0f, -3.5f, 0.0f, 3.5f, 0.0f},
     {1.1f, 0.18f, 2.6f, 1.1f, 2.2f, 0.9f},
     {0.0f, 0.0f, 0.0f, 0.2f, 0.0f, 0.15f},
     {0.3f, 1.0f, 0.3f, 0.95f, 0.3f, 0.7f}, 0.45f},
    {"DX BELL", "DX", T_FM, 0, {11, 64, 0, 105}, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 5.0f, 0.0f,
     {1.0f, 3.5f, 1.0f, 3.5f, 2.0f, 1.41f},
     {0.6f, 2.2f, 0.55f, 2.0f, 0.3f, 1.2f},
     {0.0f, 0.0f, 4.0f, 0.0f, -3.0f, 0.0f},
     {6.0f, 3.5f, 5.0f, 3.0f, 2.5f, 1.5f},
     {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
     {0.3f, 0.6f, 0.3f, 0.6f, 0.3f, 0.5f}, 0.2f},
    {"DX GLASS BELL", "DX", T_FM, 0, {11, 64, 0, 95}, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
     0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0.0f, 5.0f, 0.0f,
     {1.0f, 4.0f, 2.0f, 7.0f, 3.0f, 1.0f},
     {0.6f, 1.6f, 0.35f, 1.4f, 0.25f, 0.8f},
     {0.0f, 0.0f, 3.0f, 0.0f, -4.0f, 0.0f},
     {3.0f, 1.2f, 2.2f, 0.8f, 1.6f, 1.0f},
     {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
     {0.3f, 0.8f, 0.3f, 0.8f, 0.3f, 0.6f}, 0.1f},
    {"MOOG BASS", "MOOG", T_MOOG, 1, {11, 64, 102, 40}, 0,
     1.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.8f, 0, 0.6f, -12.0f, 6.0f,
     220.0f, 0.25f, 3.2f, 0.4f, 0.002f, 0.28f, 0.15f, 0.15f, 2.5f, 0.03f, 0.0f, 5.0f, 0.0f, NOFM},
    {"MOOG SUB BASS", "MOOG", T_MOOG, 1, {11, 68, 76, 44}, 0,
     1.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.5f, 0, 0.9f, -12.0f, 4.0f,
     140.0f, 0.1f, 2.2f, 0.3f, 0.002f, 0.18f, 0.0f, 0.12f, 2.0f, 0.02f, 0.0f, 5.0f, 0.0f, NOFM},
    {"MOOG LEAD", "MOOG", T_MOOG, 1, {25, 83, 121, 61}, 0,
     1.0f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0, 0.5f, 12.0f, 7.0f,
     900.0f, 0.35f, 2.5f, 0.6f, 0.01f, 0.6f, 0.5f, 0.3f, 2.0f, 0.07f, 0.25f, 5.5f, 0.45f, NOFM},
    {"MOOG LEAD SQUARE", "MOOG", T_MOOG, 1, {25, 83, 114, 57}, 0,
     0.0f, 1.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.6f, 0, 0.0f, 0, 5.0f,
     1200.0f, 0.45f, 2.0f, 0.6f, 0.01f, 0.5f, 0.55f, 0.3f, 1.8f, 0.06f, 0.2f, 5.5f, 0.45f, NOFM},
};
