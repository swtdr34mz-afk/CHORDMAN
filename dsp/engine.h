/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's sound engine and player.
 *
 * The keyboard is split in two parts, LEFT (the lower twelve keys) and RIGHT (the upper fifteen), each with its own
 * sound. Two modes:
 *   CHORD  the right part plays the set's chords (a key a chord), in a groove (a rhythm or an arp); the left part
 *          plays the bass: a bass arp on the chord's root, or on a note the left keys pick (locked to the key)
 *   LEAD   the right part plays a lead, locked to the set's scale; the left keys pick the chords, which the left
 *          part plays either as chords in the groove or as a bass arp on their root
 * Seven sounds after famous synths (two variations each): a Juno-106's poly and pad, an Oberheim's brass, a DX7's
 * electric piano and bell (six-operator FM), a Minimoog's bass and lead. A sidechain ducks the volume or the filter
 * on a kick pattern in time with the clock (internal, or MIDI clock from USB).
 * The engine works in the audio interrupt; the app talks to it through the calls below. No libc here. */
#pragma once
#include <stdint.h>

/* -------------------------------------------------------------- the sounds --- */
enum { T_JUNO, T_OB, T_MOOG, T_FM, NTYPES };
#define NSOUNDS 14
typedef struct {
    const char *name;
    const char *family;                  /* "JUNO", "OB", "DX", "MOOG" */
    uint8_t type, mono;                  /* T_*; mono: one voice, legato with glide */
    uint8_t env[4];                      /* the ENV page's defaults: attack, decay, sustain, release */
    uint8_t chorus;                      /* the Juno's chorus as designed: 0 off, 1 I, 2 II, 3 I+II */
    /* analog: the oscillators */
    float saw, pulse, pw, pwm;           /* osc 1: saw and pulse levels, pulse width, PWM by the LFO */
    float sub, noise;
    float o2, o2_semi, o3, o3_semi;      /* osc 2 and 3: levels, intervals (semitones) */
    float detune;                        /* cents between the oscillators (the Oberheim's two saws) */
    /* analog: the filter and its envelope */
    float cutoff, reso, fenv, keytrk;    /* Hz, 0..1, octaves, 0..1 */
    float fa, fd, fs, fr;                /* the filter's envelope (seconds, level) */
    float drive, glide;                  /* the ladder's drive; glide seconds */
    float vib, vib_rate, vib_delay;      /* vibrato: semitones, Hz, seconds before it */
    /* FM: six operators, algorithm 5 of the DX7 (three pairs: 2 -> 1, 4 -> 3, 6 -> 5, feedback on 6) */
    float ratio[6], level[6], fdet[6], op_dec[6], op_sus[6], vel[6];
    float fb;
} sound_t;
extern const sound_t SOUNDS[NSOUNDS];
extern const char *const CHAR_NAME[NTYPES];   /* what the CHAR knob is on each type */

/* -------------------------------------------------------------- the chords --- */
#define NSETS 25                         /* 20, then 5 secret ones the game unlocks */
#define NFREE 20
#define NCHORDS 8
typedef struct {
    const char *name;
    uint8_t tonic;                       /* pitch class */
    uint8_t scale;                       /* SC_* */
    const char *chord[NCHORDS];          /* "Am7", "G/B" ... */
    const char *flavor[NCHORDS];         /* the same chord, coloured (the black keys) */
} chordset_t;
enum { SC_MAJOR, SC_MINOR, SC_DORIAN, SC_LYDIAN, SC_PHRYGIAN, SC_HARMONIC, SC_MIXO, NSCALES };
extern const chordset_t SETS[NSETS];
extern const char *const SCALE_NAME[NSCALES];
extern const char *const NOTE_NAME[12];

/* -------------------------------------------------------------- the grooves --- */
enum { CR_FULL, CR_QUARTER, CR_EIGHTS, CR_OFFBEAT, CR_PUMP, CR_SYNCO, CR_HALF,
       CR_ARP_UP, CR_ARP_DOWN, CR_ARP_UPDN, CR_ARP_RANDOM, CR_CASCADE, CR_TRANCE, NCRHY };
extern const char *const CRHY_NAME[NCRHY];
enum { BA_ROOT, BA_OCTAVE, BA_OFFBEAT, BA_ROLLING, BA_ROOT5, BA_DISCO, BA_HALF, NBASS };
extern const char *const BASS_NAME[NBASS];
enum { SH_SOFT, SH_PUMP, SH_HARD, SH_GATE, NSHAPES };
enum { ST_VOLUME, ST_FILTER, ST_BOTH, NSCTGT };
enum { SK_44, SK_HALF, SK_24, SK_TRAP, NSKICK };
extern const char *const SHAPE_NAME[NSHAPES];
extern const char *const SCTGT_NAME[NSCTGT];
extern const char *const SKICK_NAME[NSKICK];
int crhy_hits(int r, uint32_t *bar16);   /* which 16ths of a bar a chord rhythm plays (the screen) */
int bass_hits(int b, uint32_t *bar16);

enum { MODE_CHORD, MODE_LEAD };
enum { PART_L, PART_R, NPARTS };
enum { SYNC_AUTO, SYNC_INT, SYNC_EXT, NSYNCS };
extern const char *const SYNC_NAME[NSYNCS];

typedef struct {                         /* a part's sound as the player left it */
    uint8_t snd;                         /* SOUNDS[] */
    uint8_t k[4];                        /* TONE, MOTION, CHAR, SPACE (64: as designed) */
    uint8_t env[4];                      /* ATTACK, DECAY, SUSTAIN, RELEASE */
    uint8_t rsv[3];
} part_t;

typedef struct {                         /* what the engine reads (the app's settings) */
    uint8_t mode;                        /* MODE_* */
    uint8_t left_bass;                   /* the left part a bass arp (else chords in LEAD, a held root in CHORD): bit mode */
    uint8_t set, key;                    /* the set; key: transpose -12..12 stored + 12 */
    int8_t octave;                       /* the right part's: -2 .. 2 */
    uint8_t groove_on;                   /* SEQ: the patterns run (off: chords and bass held) */
    uint8_t crhy, bass, swing, gate;     /* GROOVE: chord rhythm, bass pattern, swing 0..127, gate 0..127 */
    uint8_t sc[4];                       /* SIDECHAIN: depth 0..127, shape, target, kick */
    uint8_t bpm, sync;
    uint8_t rsv[2];
    part_t part[2][NPARTS];              /* per mode: the left and the right part */
} eng_cfg_t;

void eng_init(void);
void eng_bind(const eng_cfg_t *cfg);
void eng_changed(void);                  /* a sound changed (a knob, a preset): worked out at the next block */
void eng_rechord(void);                  /* the set, key, octave, mode or groove changed: the chord again */
/* the chord asked for: 0..7 of the set, flavor 1 the coloured one; -1 none (the app latches it for HOLD) */
void eng_chord(int idx, int flavor, int vel);
/* CHORD mode: the bass note the left keys hold (MIDI), -1: the chord's root */
void eng_bass_note(int note);
/* notes played straight on a part (the lead, MIDI in) */
void eng_note_on(int part, int note, int vel);
void eng_note_off(int part, int note);
void eng_all_off(void);
void eng_play(int on);
int eng_playing(void);
void eng_midi_rt(uint32_t status);

/* for the screen and the game */
int eng_chord_now(void);                 /* the chord sounding: idx | flavor << 3, -1 none */
int eng_chord_notes(int idx, int flavor, uint8_t notes[5]);   /* its notes, as they would sound now */
int eng_bass_now(void);                  /* the bass note sounding (-1 none) */
int eng_lead_now(void);                  /* the lead's last note (-1 none) */
uint32_t eng_scale_bits(void);           /* the set's scale in its key: bit p = pitch class p */
int eng_scale_note(int degree, int base);/* the scale's degree (0 = the tonic, may be negative or past 7) at base */
float eng_pos16(void);                   /* where the bar is: 0 .. 16 (16ths), the fraction too */
uint32_t eng_bar(void);                  /* bars since the groove started */
int eng_running(void);                   /* the clock is running (a groove, the transport) */
int eng_ext(void);
uint32_t eng_ext_bpm10(void);
float eng_duck(void);                    /* the sidechain now: 0 none .. 1 full */
float eng_level(void);
int eng_voices(void);
uint32_t eng_notes_played(void);
uint32_t eng_clocks_out(void);
uint32_t eng_ticks(void);
void app_clock_out(uint32_t status);     /* the engine's clock out, the app sends it */

void eng_render(int32_t *lr, uint32_t n, float gain);
void eng_panic(void);
