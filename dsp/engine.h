/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's sound engine and player: four analog-style synth engines on eight voices, the master effects (chorus,
 * delay, reverb), the chord sets, the grooves (the J-6's phrases), the chord sequencer and the clock (internal, or
 * MIDI clock and transport from USB: a computer, or another FM-1 with CHORDMAN as its USB host).
 * The app (app.c) owns the settings and the screens; the engine reads them through eng_bind and works in the audio
 * interrupt. No libc here: the device builds the engine as one unit at -O2. */
#pragma once
#include <stdint.h>

/* ------------------------------------------------------------ the engines --- */
enum { E_DCO, E_VCO, E_SVF, E_LADDER, NENGINES };
extern const char *const ENGINE_NAME[NENGINES];
extern const uint16_t ENGINE_RGB[NENGINES];   /* each engine's colour on the screen (RGB565) */
/* what the CHAR knob (K3 at home) and the EDIT page's four knobs are on each engine */
extern const char *const ENGINE_CHAR[NENGINES];
extern const char *const ENGINE_EDIT[NENGINES][4];

#define NVOICES 8
#define NPRESETS 32                      /* 8 an engine, in engine order */
typedef struct {                         /* a sound (a preset, and the player's edit of it) */
    char name[14];
    uint8_t eng;                         /* E_* */
    uint8_t rsv;
    uint8_t k[4];                        /* home: TONE, MOTION, CHAR, SPACE */
    uint8_t env[4];                      /* ENV: attack, decay, sustain, release */
    uint8_t lfo[4];                      /* LFO: rate, depth, target (zones: LFO_*), delay */
    uint8_t vo[4];                       /* EDIT: the engine's own four (ENGINE_EDIT) */
} patch_t;
extern const patch_t PRESETS[NPRESETS];
enum { LFO_PITCH, LFO_FILTER, LFO_SHAPE, LFO_AMP, NLFOTGT };
extern const char *const LFO_TARGET_NAME[NLFOTGT];
int lfo_target(uint8_t v);               /* the knob's zone: LFO_* */

/* ------------------------------------------------------------- the chords --- */
#define NSETS 100
typedef struct {
    const char *style;
    uint8_t notes[12][4];                /* per pitch class C .. B: up to four MIDI notes, lowest first, 0 none */
    const char *name[12];
} chordset_t;
extern const chordset_t CHORDSETS[NSETS];
enum { VC_J6, VC_OPEN, VC_WIDE, VC_SMOOTH, NVOICINGS };   /* FX page K1 */
extern const char *const VOICING_NAME[NVOICINGS];

/* ------------------------------------------------------------ the grooves --- */
/* the J-6's nine phrase styles, twelve variations each; style 0: no groove (a chord sounds as long as it is held) */
#define NSTYLES 9
#define NVARS 12
extern const char *const STYLE_NAME[NSTYLES + 1];       /* [0] "OFF" */
const char *groove_name(int style, int var);            /* e.g. "UP 1 8TH" */
int groove_hits(int style, int var, uint32_t *bar16);   /* which 16ths of a bar it plays (bit s), for the screen */

/* ----------------------------------------------------- the sequencer step --- */
#define NSTEPS 16
#define STEP_REST 0xFFu                  /* nothing in the step */
#define STEP_TIE 0xFEu                   /* the step before goes on */
enum { SYNC_AUTO, SYNC_INT, SYNC_EXT, NSYNCS };
extern const char *const SYNC_NAME[NSYNCS];
enum { STEPLEN_4, STEPLEN_2, STEPLEN_1, NSTEPLENS };       /* a quarter, half a bar, a bar */
extern const char *const STEPLEN_NAME[NSTEPLENS];
extern const uint16_t STEPLEN_TICKS[NSTEPLENS];           /* at 24 a quarter */

typedef struct {                         /* what the engine reads (the app's settings) */
    patch_t patch;                       /* the sound */
    uint8_t set, key;                    /* chord set 0..99; key: transpose -12..12 stored + 12 */
    int8_t octave;                       /* -2 .. 2 */
    uint8_t style, var;                  /* the groove: style 0 off, 1..9; var 0..11 */
    uint8_t groove_on;                   /* SEQ lit */
    uint8_t fx[4];                       /* FX page: voicing, strum, swing, delay time */
    uint8_t bpm;                         /* 40 .. 240 */
    uint8_t steplen;                     /* STEPLEN_* */
    uint8_t sync;                        /* SYNC_* */
    uint8_t seq_len;                     /* 0 (empty) .. NSTEPS */
    uint8_t seq[NSTEPS];                 /* chord slot 0..11 (pitch class), STEP_REST, STEP_TIE */
} eng_cfg_t;

void eng_init(void);
void eng_bind(const eng_cfg_t *cfg);
void eng_changed(void);                  /* the sound changed (a knob, a preset): worked out at the next block */
/* the chord keys: slot 0..11 (the pitch class C .. B of the key), -1 none. The app latches it for HOLD */
void eng_live_chord(int slot, int vel);
/* a single note (the melody keys, MIDI in): through the engine's voices */
void eng_note_on(int note, int vel);
void eng_note_off(int note);
void eng_all_off(void);
/* the transport (PLAY, or MIDI start / stop / continue): the sequencer from step 0, stopped, going on */
void eng_play(int on);
int eng_playing(void);
/* where the sequencer is: the step under way and how far into it (0..1), for recording a chord into the nearest step */
float eng_step_pos(void);
/* MIDI real-time in (the app hands over 0xF8 clock, 0xFA start, 0xFB continue, 0xFC stop from the render) */
void eng_midi_rt(uint32_t status);
/* the chord sounding now, for the screen and the melody keys: its slot (-1 none), notes and count */
int eng_chord_now(uint8_t notes[4]);
int eng_slot_now(void);
/* the chord the current settings give a slot (transposed, voiced, in the octave): the notes, their count */
int eng_chord_notes(int slot, uint8_t notes[4]);
/* the scale that fits the chord sounding now: bit p = pitch class p (the melody keys follow it) */
uint32_t eng_scale_now(void);
/* the clock, for the screen: ticks since the transport started (24 a quarter), the step under way (-1 stopped),
 * the 16th of the bar under way, whether the clock comes from outside, the tempo heard (bpm x 10, 0 none) */
uint32_t eng_ticks(void);
int eng_step(void);
int eng_16th(void);
int eng_ext(void);
uint32_t eng_ext_bpm10(void);
int eng_voices(void);                    /* voices sounding */
uint32_t eng_notes_played(void);         /* note-ons since the start (the host's checks) */
uint32_t eng_clocks_out(void);           /* MIDI clocks sent */
float eng_level(void);                   /* the output's peak, 0..1 (the screen) */
/* the clock out: called by the engine (the app sends it to USB) */
void app_clock_out(uint32_t status);

/* the render: lr interleaved stereo 24-bit, n frames, gain = full scale of the output */
void eng_render(int32_t *lr, uint32_t n, float gain);
void eng_panic(void);
