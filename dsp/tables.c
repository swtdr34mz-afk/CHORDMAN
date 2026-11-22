/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's names and presets: the four engines, what their knobs are called, the 32 factory sounds. Part of
 * dsp/engine.c (included there). */
#define RGB565(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))

/* the four engines, named for how they are built (the synths they lean on in the README) */
const char *const ENGINE_NAME[NENGINES] = {"DCO", "VCO", "SVF", "LADDER"};
const uint16_t ENGINE_RGB[NENGINES] = {RGB565(255, 170, 40), RGB565(250, 112, 84), RGB565(70, 206, 168),
                                       RGB565(160, 130, 255)};
const char *const ENGINE_CHAR[NENGINES] = {"CHORUS", "SHAPE", "MODE", "DRIVE"};
const char *const ENGINE_EDIT[NENGINES][4] = {
    {"PWM", "SUB", "NOISE", "RESO"},
    {"DETUNE", "OSC 2", "POLYMOD", "RESO"},
    {"DETUNE", "PULSE", "DRIVE", "RESO"},
    {"DETUNE", "SUB", "GLIDE", "RESO"},
};
const char *const LFO_TARGET_NAME[NLFOTGT] = {"PITCH", "FILTER", "SHAPE", "AMP"};
int lfo_target(uint8_t v) { return v >> 5; }
const char *const VOICING_NAME[NVOICINGS] = {"J-6", "OPEN", "WIDE", "SMOOTH"};
const char *const SYNC_NAME[NSYNCS] = {"AUTO", "INT", "EXT"};
const char *const STEPLEN_NAME[NSTEPLENS] = {"1/4", "1/2", "1 BAR"};
const uint16_t STEPLEN_TICKS[NSTEPLENS] = {24, 48, 96};

/*  name           eng       TONE MOT CHAR SPACE  A   D   S   R    RATE DEP TGT DLY   EDIT x4 */
#define P(n, e, k0, k1, k2, k3, a, d, s, r, lr, ld, lt, ly, v0, v1, v2, v3) \
    {n, e, 0, {k0, k1, k2, k3}, {a, d, s, r}, {lr, ld, lt, ly}, {v0, v1, v2, v3}}
const patch_t PRESETS[NPRESETS] = {
    /* DCO: one oscillator (saw, pulse with PWM, sub, noise), a soft four-pole filter, the BBD chorus (I, II, I+II) */
    P("JUNO PAD", E_DCO, 70, 30, 70, 70, 60, 70, 100, 75, 30, 15, 40, 60, 70, 30, 0, 20),
    P("BRASS 106", E_DCO, 40, 80, 40, 40, 20, 60, 80, 40, 0, 0, 40, 0, 20, 20, 0, 25),
    P("STRING MACHINE", E_DCO, 80, 15, 110, 80, 70, 70, 110, 85, 35, 30, 80, 0, 90, 0, 0, 0),
    P("SUB BASS", E_DCO, 30, 70, 0, 10, 0, 45, 40, 20, 0, 0, 40, 0, 50, 100, 0, 50),
    P("POLY KEYS", E_DCO, 55, 55, 40, 45, 0, 75, 30, 50, 0, 0, 40, 0, 40, 20, 0, 15),
    P("CHORUS LEAD", E_DCO, 75, 40, 64, 55, 5, 60, 90, 45, 60, 18, 10, 70, 30, 0, 0, 30),
    P("AIR PAD", E_DCO, 90, 20, 70, 100, 85, 80, 110, 100, 20, 25, 80, 0, 100, 0, 30, 10),
    P("PLUCK 106", E_DCO, 35, 85, 40, 60, 0, 50, 0, 45, 0, 0, 40, 0, 60, 30, 5, 35),
    /* VCO: two oscillators of any shape (triangle, saw, square, narrow pulse), a four-pole low-pass, poly mod */
    P("SIX POLY", E_VCO, 60, 50, 45, 50, 10, 70, 90, 60, 0, 0, 40, 0, 30, 0, 0, 20),
    P("SIX BRASS", E_VCO, 35, 90, 45, 35, 25, 65, 85, 45, 0, 0, 40, 0, 40, 0, 0, 15),
    P("SIX STAB", E_VCO, 45, 75, 70, 55, 0, 45, 20, 40, 0, 0, 40, 0, 20, 90, 10, 30),
    P("SIX PAD", E_VCO, 75, 25, 30, 95, 80, 80, 110, 95, 25, 20, 40, 50, 50, 0, 0, 10),
    P("POLY MOD", E_VCO, 55, 60, 50, 50, 0, 70, 50, 55, 0, 0, 40, 0, 10, 60, 70, 40),
    P("GLASS", E_VCO, 90, 30, 10, 85, 0, 85, 30, 80, 0, 0, 40, 0, 15, 90, 40, 20),
    P("FIFTHS", E_VCO, 50, 70, 45, 45, 5, 60, 70, 50, 0, 0, 40, 0, 20, 60, 0, 25),
    P("SIX LEAD", E_VCO, 70, 50, 80, 50, 0, 60, 100, 40, 55, 15, 10, 70, 25, 120, 0, 35),
    /* SVF: two detuned oscillators into a two-pole state-variable filter that morphs low-pass, notch, high-pass */
    P("OB BRASS", E_SVF, 40, 95, 0, 45, 20, 60, 90, 45, 0, 0, 40, 0, 55, 0, 40, 25),
    P("OB POLY", E_SVF, 60, 60, 0, 50, 5, 65, 95, 50, 0, 0, 40, 0, 45, 20, 30, 20),
    P("OB STRINGS", E_SVF, 70, 25, 10, 80, 70, 75, 110, 85, 40, 10, 10, 70, 70, 0, 10, 10),
    P("NOTCH PAD", E_SVF, 75, 30, 64, 90, 75, 80, 110, 95, 20, 45, 80, 0, 60, 40, 20, 50),
    P("SEM BASS", E_SVF, 30, 75, 0, 10, 0, 50, 50, 20, 0, 0, 40, 0, 25, 30, 60, 40),
    P("BAND SWEEP", E_SVF, 55, 70, 40, 60, 10, 80, 60, 60, 30, 30, 40, 0, 50, 10, 30, 70),
    P("HP SHIMMER", E_SVF, 50, 40, 110, 100, 40, 80, 100, 90, 0, 0, 40, 0, 40, 60, 10, 30),
    P("OB HORNS", E_SVF, 50, 85, 10, 55, 30, 70, 80, 55, 45, 12, 10, 80, 65, 0, 50, 15),
    /* LADDER: three oscillators (the third a sub), a driven four-pole ladder, glide */
    P("ONE PAD", E_LADDER, 65, 35, 30, 85, 70, 80, 110, 90, 0, 0, 40, 0, 45, 30, 0, 30),
    P("ONE BRASS", E_LADDER, 40, 90, 50, 40, 20, 65, 85, 45, 0, 0, 40, 0, 40, 40, 0, 20),
    P("FAT BASS", E_LADDER, 30, 65, 70, 10, 0, 55, 60, 25, 0, 0, 40, 0, 30, 100, 15, 45),
    P("ONE LEAD", E_LADDER, 70, 55, 60, 55, 0, 60, 100, 40, 55, 12, 10, 70, 35, 20, 35, 40),
    P("SQUELCH", E_LADDER, 35, 95, 80, 50, 0, 45, 20, 35, 0, 0, 40, 0, 15, 30, 10, 95),
    P("ONE STRINGS", E_LADDER, 75, 20, 20, 90, 75, 80, 115, 95, 30, 15, 40, 60, 70, 0, 0, 10),
    P("WARM KEYS", E_LADDER, 50, 55, 40, 50, 0, 75, 40, 55, 0, 0, 40, 0, 20, 30, 0, 25),
    P("BIG SWELL", E_LADDER, 45, 70, 45, 100, 100, 100, 110, 100, 0, 0, 40, 0, 55, 40, 0, 35),
};
