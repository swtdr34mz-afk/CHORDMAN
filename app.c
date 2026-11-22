/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN: the app. The panel (what each control does), the settings, MIDI in and the clock out, the screens.
 *
 *   PRESETS      the sound: 32, eight per engine (DCO, VCO, SVF, LADDER)
 *   ALGORITHM    the chord set, 1 .. 100 (the J-6's); with OCT- or OCT+ held: the key, -12 .. +12
 *   SELECT       the groove: the J-6's nine styles, twelve variations each (turning it switches the groove on)
 *   K1 .. K4     at HOME: TONE, MOTION, CHAR (the engine's own), SPACE; on a page: that page's four
 *   FX ENV LFO EDIT GLO   a page of four knobs (the same button again: HOME)
 *   SEL          chord mode on / off (off: the 27 keys play notes)
 *   HOME         the play screen; held a second: the sound back as its preset had it
 *   SAVE         keeps everything (it survives switching off)
 *   ARP          HOLD: the chord stays when the key is let go
 *   SEQ          the groove on / off
 *   PLAY / REC   the chord sequencer (REC held a second: the sequence cleared)
 *   OCT- / OCT+  the octave; both: back to 0
 * Keys, chord mode: the lower twelve (F3 .. E4) play the set's chords, each the chord of its pitch class, as on the
 * J-6; the upper fifteen (F4 .. G5) play a melody that keeps to the chord's scale.
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. */
#include "fastmath.h"
#include "engine.h"

extern const char *const DELAY_NAME[5];

/* --------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x434D4E31u            /* "CMN1": the settings record; another magic loads defaults */
typedef struct {
    uint32_t magic;
    patch_t snd[NPRESETS];               /* the sounds, as edited */
    eng_cfg_t eng;                       /* what the engine reads (its patch: snd[preset]) */
    uint8_t preset;
    uint8_t chord_mode;                  /* SEL: 1 the lower keys play chords */
    uint8_t hold;                        /* ARP: HOLD */
    uint8_t rsv;
} settings_t;
static settings_t set;
static uint8_t set_dirty;

static void settings_defaults(void)
{
    uint32_t i;
    set.magic = SET_MAGIC;
    for (i = 0; i < NPRESETS; i++)
        set.snd[i] = PRESETS[i];
    set.preset = 0;
    set.eng.patch = set.snd[0];
    set.eng.set = 26;                    /* set 27, POP/SYNTH: C Em G Am Bm on the white keys */
    set.eng.key = 12;
    set.eng.octave = 0;
    set.eng.style = 1;
    set.eng.var = 0;
    set.eng.groove_on = 0;
    set.eng.fx[0] = 0;                   /* voicing: the J-6's */
    set.eng.fx[1] = 0;                   /* strum */
    set.eng.fx[2] = 0;                   /* swing */
    set.eng.fx[3] = 60;                  /* delay: 3/16 */
    set.eng.bpm = 120;
    set.eng.steplen = STEPLEN_1;
    set.eng.sync = SYNC_AUTO;
    set.eng.seq_len = 0;
    for (i = 0; i < NSTEPS; i++)
        set.eng.seq[i] = STEP_REST;
    set.chord_mode = 1;
    set.hold = 0;
}

static int settings_ok(void)
{
    uint32_t i;
    if (set.magic != SET_MAGIC || set.preset >= NPRESETS || set.eng.set >= NSETS || set.eng.key > 24u ||
        set.eng.octave < -2 || set.eng.octave > 2 || set.eng.style > NSTYLES || set.eng.var >= NVARS ||
        set.eng.bpm < 40u || set.eng.bpm > 240u || set.eng.steplen >= NSTEPLENS || set.eng.sync >= NSYNCS ||
        set.eng.seq_len > NSTEPS)
        return 0;
    for (i = 0; i < NPRESETS; i++)
        if (set.snd[i].eng >= NENGINES)
            return 0;
    return 1;
}

static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || !settings_ok())
        settings_defaults();
    set.snd[set.preset].name[13] = 0;
    set.eng.patch = set.snd[set.preset];
}

static int settings_save(void)
{
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) != 0)
        return -1;
    set_dirty = 0;
    return 0;
}

static patch_t *SND(void) { return &set.snd[set.preset]; }
static void sound_changed(void)          /* the edited sound to the engine */
{
    set.eng.patch = *SND();
    set_dirty = 1;
    eng_changed();
}

/* ------------------------------------------------------------------ MIDI --- */
static volatile uint32_t midi_notes_in;
void app_clock_out(uint32_t st) { plat_midi_send(0x0Fu | st << 8, TO_USB); }

static void midi_in(void)                /* audio interrupt: what came in */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st >= 0xF8u) {
            eng_midi_rt(st);
            continue;
        }
        if ((st & 0x0Fu) != 0u)          /* notes on channel 1 only (a drum machine's on 10 are not ours) */
            continue;
        if ((st & 0xF0u) == 0x90u && d2) {
            eng_note_on((int)d1, (int)d2);
            midi_notes_in++;
        } else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u) {
            eng_note_off((int)d1);
        }
    }
}

static float gain_now;
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 / 4096.0f * 8000000.0f;
    midi_in();
    gain_now += (gain - gain_now) * 0.3f;
    eng_render(lr, n, gain_now);
}

void app_panic(void) { eng_panic(); }

void app_init(void)
{
    settings_load();
    eng_init();
    eng_bind(&set.eng);
    plat_cpu2_set(1);                    /* half the voices on the second core (or all on this one) */
}

/* --------------------------------------------------------------------- UI --- */
enum { PG_HOME, PG_FX, PG_ENV, PG_LFO, PG_EDIT, PG_GLO, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"HOME", "FX", "ENV", "LFO", "EDIT", "GLOBAL"};
static const char *const FX_KNOB[4] = {"VOICING", "STRUM", "SWING", "DELAY"};
static const char *const ENV_KNOB[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"};
static const char *const LFO_KNOB[4] = {"RATE", "DEPTH", "TARGET", "DELAY"};
static const char *const GLO_KNOB[4] = {"TEMPO", "STEP", "SYNC", "USB HOST"};
enum { OV_NONE, OV_SET, OV_GROOVE, OV_PRESET, OV_KEY };

static struct {
    int page, overlay;
    uint32_t overlay_until;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    int live_key;                        /* the chord key sounding (its key number), -1 none */
    uint8_t key_note[NKEYS];             /* the note each melody key plays (to let go of the same one) */
    uint32_t turned_at[NE];
    int rec, cursor;                     /* REC lit; where the next step written goes (stopped) */
    uint32_t rec_t, home_t;              /* REC / HOME went down (their hold) */
    uint8_t oct_used;                    /* OCT held turned ALGORITHM: no octave step on its release */
    int usb_host;
    uint32_t last_draw;
    uint8_t redraw;
    int kind_last;                       /* the last knob's page and number (the reader) */
} ui;

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void signed_text(int32_t v, char *b)
{
    if (v > 0) {
        *b++ = '+';
    }
    int_text(v, b);
}

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[48];
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1300u;
    ui.redraw = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void overlay(int ov)
{
    ui.overlay = ov;
    ui.overlay_until = plat_ms() + 1600u;
    ui.redraw = 1;
}

/* a knob's value as the screen shows it */
static void knob_text(int page, int k, char *t)
{
    const patch_t *p = SND();
    uint8_t v;
    t[0] = 0;
    switch (page) {
    case PG_HOME:
        v = p->k[k];
        if (k == 2 && p->eng == E_DCO) {
            static const char *const CH[4] = {"OFF", "I", "II", "I+II"};
            text_cat(t, CH[v >> 5]);
            return;
        }
        if (k == 2 && p->eng == E_SVF) {
            text_cat(t, v < 24 ? "LP" : v < 54 ? "LP>N" : v < 74 ? "NOTCH" : v < 104 ? "N>HP" : "HP");
            return;
        }
        break;
    case PG_FX:
        v = set.eng.fx[k];
        if (k == 0) {
            text_cat(t, VOICING_NAME[(v * NVOICINGS) >> 7]);
            return;
        }
        if (k == 3) {
            text_cat(t, DELAY_NAME[(v * 5) >> 7]);
            return;
        }
        if (k == 1 && !v) {
            text_cat(t, "OFF");
            return;
        }
        if (k == 2) {
            int_text(50 + (v * 25) / 127, t);
            text_cat(t, "%");
            return;
        }
        break;
    case PG_ENV:
        v = p->env[k];
        break;
    case PG_LFO:
        v = p->lfo[k];
        if (k == 2) {
            text_cat(t, LFO_TARGET_NAME[lfo_target(v)]);
            return;
        }
        break;
    case PG_EDIT:
        v = p->vo[k];
        if (p->eng == E_VCO && k == 1) {
            static const char *const IV[5] = {"UNISON", "+4TH", "+5TH", "OCT UP", "OCT DN"};
            text_cat(t, IV[(v * 5) >> 7]);
            return;
        }
        if (p->eng == E_LADDER && k == 2 && !v) {
            text_cat(t, "OFF");
            return;
        }
        break;
    default:
        if (k == 0) {
            int_text(set.eng.bpm, t);
            return;
        }
        if (k == 1) {
            text_cat(t, STEPLEN_NAME[set.eng.steplen]);
            return;
        }
        if (k == 2) {
            text_cat(t, SYNC_NAME[set.eng.sync]);
            return;
        }
        text_cat(t, ui.usb_host ? (plat_usb_host_state() == USBH_CONNECTED ? "LINKED" : "ON") : "OFF");
        return;
    }
    int_text(v, t);
}

static const char *knob_name(int page, int k)
{
    switch (page) {
    case PG_HOME:
        return k == 0 ? "TONE" : k == 1 ? "MOTION" : k == 2 ? ENGINE_CHAR[SND()->eng] : "SPACE";
    case PG_FX:
        return FX_KNOB[k];
    case PG_ENV:
        return ENV_KNOB[k];
    case PG_LFO:
        return LFO_KNOB[k];
    case PG_EDIT:
        return ENGINE_EDIT[SND()->eng][k];
    default:
        return GLO_KNOB[k];
    }
}

static uint8_t *knob_ref(int page, int k)
{
    patch_t *p = SND();
    switch (page) {
    case PG_HOME:
        return &p->k[k];
    case PG_FX:
        return &set.eng.fx[k];
    case PG_ENV:
        return &p->env[k];
    case PG_LFO:
        return &p->lfo[k];
    case PG_EDIT:
        return &p->vo[k];
    default:
        return 0;
    }
}

static void say_knob(int page, int k)
{
    char t[16];
    knob_text(page, k, t);
    if (ann_on())
        app_say(knob_name(page, k), t);
}

/* a knob turned: faster turns take bigger steps */
static void knob_turn(int k, int32_t e)
{
    uint32_t now = plat_ms(), dt = now - ui.turned_at[EN_K1 + k];
    int32_t step = dt < 25u ? 4 : dt < 60u ? 2 : 1;
    uint8_t *r;
    ui.turned_at[EN_K1 + k] = now;
    ui.redraw = 1;
    if (ui.page == PG_GLO) {
        if (k == 0) {
            set.eng.bpm = (uint8_t)clamp(set.eng.bpm + e * (step > 1 ? 2 : 1), 40, 240);
        } else if (k == 1) {
            set.eng.steplen = (uint8_t)clamp(set.eng.steplen + (e > 0 ? 1 : -1), 0, NSTEPLENS - 1);
        } else if (k == 2) {
            set.eng.sync = (uint8_t)clamp(set.eng.sync + (e > 0 ? 1 : -1), 0, NSYNCS - 1);
        } else {
            int on = e > 0;
            if (on != ui.usb_host) {
                ui.usb_host = on;
                plat_usb_host(on);
                app_say("USB HOST", on ? "ON" : "OFF");
                return;
            }
        }
        set_dirty = 1;
        say_knob(ui.page, k);
        return;
    }
    r = knob_ref(ui.page, k);
    if (!r)
        return;
    *r = (uint8_t)clamp((int32_t)*r + e * step, 0, 127);
    if (ui.page == PG_FX) {
        set_dirty = 1;
        if (k == 0)
            eng_changed();               /* the voicing: the chord sounding again */
    } else {
        sound_changed();
    }
    say_knob(ui.page, k);
}

static void preset_load(int p)
{
    set.preset = (uint8_t)((p + NPRESETS) % NPRESETS);
    set.eng.patch = set.snd[set.preset];
    set_dirty = 1;
    eng_changed();
    overlay(OV_PRESET);
    if (ann_on())
        app_say(ENGINE_NAME[SND()->eng], SND()->name);
}

static int groove_index(void) { return set.eng.style ? (set.eng.style - 1) * NVARS + set.eng.var : 0; }
static void groove_set_index(int gi)
{
    gi = (gi + NSTYLES * NVARS) % (NSTYLES * NVARS);
    set.eng.style = (uint8_t)(1 + gi / NVARS);
    set.eng.var = (uint8_t)(gi % NVARS);
    set.eng.groove_on = 1;
    set_dirty = 1;
    eng_changed();
    overlay(OV_GROOVE);
    if (ann_on())
        app_say(STYLE_NAME[set.eng.style], groove_name(set.eng.style, set.eng.var));
}

/* the melody keys keep to the chord's scale: a note off it goes to the nearest one on it (down first) */
static int follow(int note)
{
    uint32_t sc = eng_scale_now();
    int d;
    if (sc >> (note % 12) & 1u)
        return note;
    for (d = 1; d < 6; d++) {
        if (sc >> ((note - d + 120) % 12) & 1u)
            return note - d;
        if (sc >> ((note + d) % 12) & 1u)
            return note + d;
    }
    return note;
}

static int key_is_chord(uint32_t k) { return set.chord_mode && k < 12u; }
static int key_slot(uint32_t k) { return (int)((5u + k) % 12u); }   /* key 0 is F */

static void chord_from_keys(void)        /* the chord key held last, or the latched one, or none */
{
    int slot = -1;
    if (ui.live_key >= 0)
        slot = key_slot((uint32_t)ui.live_key);
    eng_live_chord(slot, 100);
}

static void seq_write(int slot)          /* REC: a chord into the sequence */
{
    if (!ui.rec)
        return;
    if (eng_playing()) {
        float pos = eng_step_pos();
        int len = set.eng.seq_len ? set.eng.seq_len : NSTEPS, s;
        if (!set.eng.seq_len)
            set.eng.seq_len = NSTEPS;
        s = ((int)(pos + 0.5f)) % len;
        set.eng.seq[s] = (uint8_t)slot;
    } else {
        set.eng.seq[ui.cursor] = (uint8_t)slot;
        ui.cursor++;
        if (ui.cursor > set.eng.seq_len)
            set.eng.seq_len = (uint8_t)ui.cursor;
        if (ui.cursor >= NSTEPS)
            ui.cursor = 0;
    }
    set_dirty = 1;
}

static void key_down(uint32_t k, uint32_t vel)
{
    if (key_is_chord(k)) {
        ui.live_key = (int)k;
        eng_live_chord(key_slot(k), (int)vel);
        seq_write(key_slot(k));
        return;
    }
    {
        int note = 53 + (int)k + 12 * set.eng.octave;
        if (set.chord_mode)
            note = follow(note + 12);    /* the melody an octave over the chords */
        ui.key_note[k] = (uint8_t)clamp(note, 0, 127);
        eng_note_on(ui.key_note[k], (int)vel);
    }
}

static void key_up(uint32_t k, uint32_t held)
{
    if (key_is_chord(k)) {
        if ((int)k != ui.live_key)
            return;
        if (set.hold)
            return;                      /* HOLD: it stays */
        ui.live_key = -1;
        {                                /* another chord key still held: that one */
            uint32_t j;
            for (j = 0; j < 12u; j++)
                if (held >> j & 1u)
                    ui.live_key = (int)j;
        }
        chord_from_keys();
        return;
    }
    eng_note_off(ui.key_note[k]);
}

static void octave_step(int d)
{
    int o = clamp(set.eng.octave + d, -2, 2);
    char t[6];
    if (o == set.eng.octave)
        return;
    eng_all_off();
    set.eng.octave = (int8_t)o;
    set_dirty = 1;
    eng_changed();
    signed_text(o, t);
    app_say("OCTAVE", o ? t : "0");
}

static void button_down(uint32_t b)
{
    switch (b) {
    case B_FX:
    case B_ENV:
    case B_LFO:
    case B_EDIT:
    case B_GLO: {
        int pg = b == B_FX ? PG_FX : b == B_ENV ? PG_ENV : b == B_LFO ? PG_LFO : b == B_EDIT ? PG_EDIT : PG_GLO;
        ui.page = ui.page == pg ? PG_HOME : pg;
        ui.overlay = OV_NONE;
        ui.redraw = 1;
        if (ann_on())
            app_say(PAGE_NAME[ui.page], ui.page == PG_EDIT ? ENGINE_NAME[SND()->eng] : "");
        break;
    }
    case B_HOME:
        ui.page = PG_HOME;
        ui.overlay = OV_NONE;
        ui.home_t = plat_ms();
        ui.redraw = 1;
        break;
    case B_SEL:
        if (ui.rec && !eng_playing()) {  /* step recording: a rest */
            set.eng.seq[ui.cursor] = STEP_REST;
            ui.cursor++;
            if (ui.cursor > set.eng.seq_len)
                set.eng.seq_len = (uint8_t)ui.cursor;
            if (ui.cursor >= NSTEPS)
                ui.cursor = 0;
            set_dirty = 1;
            app_say("STEP", "REST");
            break;
        }
        eng_all_off();
        set.chord_mode ^= 1u;
        ui.live_key = -1;
        chord_from_keys();
        set_dirty = 1;
        app_say("CHORDS", set.chord_mode ? "ON" : "OFF");
        break;
    case B_SAVE:
        if (settings_save() == 0)
            app_say("SAVED", "");
        else
            app_say("SAVE", "FAILED");
        break;
    case B_ARP:
        if (ui.rec && !eng_playing()) {  /* step recording: a tie */
            set.eng.seq[ui.cursor] = STEP_TIE;
            ui.cursor++;
            if (ui.cursor > set.eng.seq_len)
                set.eng.seq_len = (uint8_t)ui.cursor;
            if (ui.cursor >= NSTEPS)
                ui.cursor = 0;
            set_dirty = 1;
            app_say("STEP", "TIE");
            break;
        }
        set.hold ^= 1u;
        if (!set.hold && !((ui.keys & 0xFFFu) && set.chord_mode)) {
            ui.live_key = -1;
            chord_from_keys();
        }
        set_dirty = 1;
        app_say("HOLD", set.hold ? "ON" : "OFF");
        break;
    case B_SEQ:
        set.eng.groove_on ^= 1u;
        set_dirty = 1;
        eng_changed();
        app_say("GROOVE", set.eng.groove_on ? STYLE_NAME[set.eng.style] : "OFF");
        break;
    case B_PLAY:
        if (eng_playing()) {
            eng_play(0);
        } else {
            if (ui.rec && !set.eng.seq_len) {
                uint32_t i;
                set.eng.seq_len = NSTEPS;
                for (i = 0; i < NSTEPS; i++)
                    set.eng.seq[i] = STEP_REST;
            }
            eng_play(1);
        }
        ui.redraw = 1;
        break;
    case B_REC:
        ui.rec_t = plat_ms();
        break;
    default:
        break;
    }
}

static void button_up(uint32_t b, uint32_t held_ms)
{
    if (b == B_REC) {
        if (held_ms >= 1000u)
            return;                      /* (held: it cleared) */
        ui.rec = !ui.rec;
        if (ui.rec && !eng_playing())
            ui.cursor = 0;
        ui.redraw = 1;
        app_say("REC", ui.rec ? (eng_playing() ? "LIVE" : "STEP") : "OFF");
    } else if (b == B_OCTDN || b == B_OCTUP) {
        if (ui.oct_used) {
            if (!(ui.btn & (1u << B_OCTDN | 1u << B_OCTUP)))
                ui.oct_used = 0;
            return;
        }
        octave_step(b == B_OCTDN ? -1 : 1);
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, now = plat_ms(), lit = 0, dim = 0, dimk = 0, litk = 0;
    int32_t e;
    static uint32_t down_at[NB];
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit))
            key_down(k, touch_vel(k));
        else if (!(keys & bit) && (ui.keys & bit))
            key_up(k, keys);
    }
    /* both octave buttons: back to 0 */
    if ((btn & (1u << B_OCTDN)) && (btn & (1u << B_OCTUP)) && !ui.oct_used) {
        ui.oct_used = 1;
        if (set.eng.octave || set.eng.key != 12u) {
            eng_all_off();
            set.eng.octave = 0;
            set.eng.key = 12;
            set_dirty = 1;
            eng_changed();
        }
        app_say("OCTAVE KEY", "RESET");
    }
    {
        uint32_t pressed = btn & ~ui.btn, released = ui.btn & ~btn;
        ui.btn = btn;                    /* (button_up sees what is still held) */
        for (k = 0; k < NB; k++)
            if (pressed >> k & 1u) {
                down_at[k] = now;
                button_down(k);
            }
        for (k = 0; k < NB; k++)
            if (released >> k & 1u)
                button_up(k, now - down_at[k]);
    }
    /* REC held a second: the sequence cleared */
    if (ui.rec_t && (btn >> B_REC & 1u) && now - ui.rec_t >= 1000u) {
        uint32_t i;
        ui.rec_t = 0;
        set.eng.seq_len = 0;
        for (i = 0; i < NSTEPS; i++)
            set.eng.seq[i] = STEP_REST;
        ui.cursor = 0;
        set_dirty = 1;
        app_say("SEQUENCE", "CLEARED");
    }
    if (!(btn >> B_REC & 1u))
        ui.rec_t = 0;
    /* HOME held a second: the sound as its preset */
    if (ui.home_t && (btn >> B_HOME & 1u) && now - ui.home_t >= 1000u) {
        ui.home_t = 0;
        *SND() = PRESETS[set.preset];
        sound_changed();
        app_say("SOUND", "RESET");
    }
    if (!(btn >> B_HOME & 1u))
        ui.home_t = 0;
    /* the encoders */
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        if (k == EN_PRESET) {
            preset_load(set.preset + (e > 0 ? 1 : -1));
        } else if (k == EN_ALGO) {
            if (btn & (1u << B_OCTDN | 1u << B_OCTUP)) {
                set.eng.key = (uint8_t)clamp(set.eng.key + (e > 0 ? 1 : -1), 0, 24);
                ui.oct_used = 1;
                set_dirty = 1;
                eng_changed();
                overlay(OV_KEY);
            } else {
                uint32_t dt = now - ui.turned_at[k];
                int st = dt < 40u ? 5 : 1;
                set.eng.set = (uint8_t)((set.eng.set + (e > 0 ? st : -st) + 10 * NSETS) % NSETS);
                set_dirty = 1;
                eng_changed();
                overlay(OV_SET);
                if (ann_on()) {
                    char t[8];
                    int_text(set.eng.set + 1, t);
                    app_say(t, CHORDSETS[set.eng.set].style);
                }
            }
            ui.turned_at[k] = now;
        } else if (k == EN_SELECT) {
            groove_set_index(groove_index() + (e > 0 ? 1 : -1));
        } else {
            knob_turn((int)(k - EN_K1), e);
        }
    }
    if (keys != ui.keys)
        ui.redraw = 1;
    ui.keys = keys;
    /* the lights: the page, the modes, PLAY green while it plays and red while it records */
    if (ui.page == PG_FX) lit |= 1u << B_FX;
    if (ui.page == PG_ENV) lit |= 1u << B_ENV;
    if (ui.page == PG_LFO) lit |= 1u << B_LFO;
    if (ui.page == PG_EDIT) lit |= 1u << B_EDIT;
    if (ui.page == PG_GLO) lit |= 1u << B_GLO;
    if (ui.page == PG_HOME) lit |= 1u << B_HOME;
    if (set.chord_mode) lit |= 1u << B_SEL;
    if (set.hold) lit |= 1u << B_ARP;
    if (set.eng.groove_on) lit |= 1u << B_SEQ;
    if (set_dirty) lit |= 1u << B_SAVE;
    if (ui.rec) lit |= 1u << B_REC;
    if (eng_playing()) lit |= 1u << B_PLAY;
    if (set.eng.octave < 0) lit |= 1u << B_OCTDN;
    if (set.eng.octave > 0) lit |= 1u << B_OCTUP;
    dim = ~lit & ((1u << NB) - 1u);
    if (set.chord_mode) {
        uint8_t nn[4];
        int n = eng_chord_now(nn), i, s = eng_slot_now();
        dimk |= 0xFFFu;                  /* the chord keys glow */
        for (k = 0; k < 12u; k++)
            if (key_slot(k) == s)
                litk |= 1u << k;
        for (k = 12; k < NKEYS; k++)     /* the melody keys of the chord's notes */
            for (i = 0; i < n; i++)
                if ((53u + k) % 12u == nn[i] % 12u)
                    dimk |= 1u << k;
    }
    litk |= keys;
    plat_play_red(ui.rec);
    plat_lights(lit, litk, dim, dimk & ~litk, 1);
}

void app_input_only(void) { input(); }

/* ---------------------------------------------------------------- screens --- */
#define C_BG RGB(10, 10, 12)
#define C_PANEL RGB(30, 30, 34)
#define C_RULE RGB(52, 52, 58)
#define C_SOFT RGB(120, 120, 128)
#define C_TEXT RGB(236, 234, 228)
#define C_REC RGB(240, 70, 60)
#define C_SYNC RGB(90, 210, 170)

static uint16_t accent(void) { return ENGINE_RGB[SND()->eng]; }
static uint16_t dark_of(uint16_t c)      /* the accent, dark: text on it */
{
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return (uint16_t)(((r / 5u) << 11) | ((g / 5u) << 5) | (b / 5u));
}

static void tag(int32_t x, int32_t y, const char *s, uint16_t bg, uint16_t fg)
{
    int32_t w = text_w(&FONT_XS, s) + 8;
    cv_rect(x, y, w, 15, bg);
    cv_text(x + 4, y + 1, &FONT_XS, s, fg);
}

static void header(const char *title)
{
    char t[20];
    uint32_t now = plat_ms(), b10 = eng_ext_bpm10();
    int32_t x;
    cv_begin(240, 26, C_BG);
    if (now < ui.msg_until) {
        x = cv_text(8, 4, &FONT_B, ui.msg[0], C_TEXT);
        cv_text(x + 8, 4, &FONT_B, ui.msg[1], accent());
    } else {
        tag(6, 5, ENGINE_NAME[SND()->eng], accent(), dark_of(accent()));
        x = 6 + text_w(&FONT_XS, ENGINE_NAME[SND()->eng]) + 14;
        cv_text(x, 4, &FONT_B, title ? title : SND()->name, C_TEXT);
        if (b10) {
            int_text((int32_t)((b10 + 5u) / 10u), t);
        } else {
            int_text(set.eng.bpm, t);
        }
        x = 234 - text_w(&FONT_B, t);
        cv_text(x, 4, &FONT_B, t, eng_ext() ? C_SYNC : C_TEXT);
        if (eng_ext())
            tag(x - text_w(&FONT_XS, ui.usb_host ? "LINK" : "EXT") - 14, 5, ui.usb_host ? "LINK" : "EXT", C_SYNC,
                RGB(4, 52, 44));
        else if (ui.usb_host)
            tag(x - text_w(&FONT_XS, "HOST") - 14, 5, "HOST", C_PANEL, C_SOFT);
    }
    cv_rect(0, 25, 240, 1, C_RULE);
    cv_blit(0, 0);
}

static void knobs_footer(int page, int32_t y)    /* the four knobs' names and bars, a strip of 36 */
{
    int k;
    cv_begin(240, 36, C_BG);
    cv_rect(0, 0, 240, 1, C_RULE);
    for (k = 0; k < 4; k++) {
        int32_t x = k * 60 + 5;
        uint8_t *r = knob_ref(page, k);
        int lit = plat_ms() - ui.turned_at[EN_K1 + k] < 600u;
        cv_text(x, 5, &FONT_XS, knob_name(page, k), lit ? C_TEXT : C_SOFT);
        cv_rect(x, 24, 50, 4, C_PANEL);
        if (r)
            cv_rect(x, 24, (*r * 50 + 63) / 127, 4, accent());
    }
    cv_blit(0, (uint32_t)y);
}

static void center_text(int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text((240 - text_w(f, s)) / 2, y, f, s, c);
}

static void draw_home(void)
{
    uint8_t nn[4];
    char t[40];
    int s = eng_slot_now(), i, st, n;
    uint32_t hits = 0;
    const chordset_t *cs = &CHORDSETS[set.eng.set];
    header(0);
    /* the chord: its name, big; the set and the key under it */
    cv_begin(240, 80, C_BG);
    n = eng_chord_now(nn);
    if (ui.overlay == OV_PRESET) {
        center_text(10, &FONT_M, SND()->name, C_TEXT);
        t[0] = 0;
        int_text(set.preset % 8 + 1, t);
        text_cat(t, " / 8  ");
        text_cat(t, ENGINE_NAME[SND()->eng]);
        center_text(44, &FONT_XS, t, accent());
    } else if (s >= 0 && n) {
        center_text(8, &FONT_L, cs->name[s], C_TEXT);
        for (i = 0; i < n; i++)          /* its notes, a dot each */
            cv_rect(120 - n * 7 + i * 14 + 2, 48, 9, 4, accent());
    } else {
        center_text(14, &FONT_M, set.chord_mode ? "- - -" : "NOTES", C_RULE);
    }
    t[0] = 0;
    text_cat(t, "SET ");
    int_text(set.eng.set + 1, t + 4);
    text_cat(t, "  ");
    text_cat(t, cs->style);
    text_cat(t, "  KEY ");
    {
        char k[6];
        if (set.eng.key == 12u)
            text_cat(t, "0");
        else {
            signed_text((int32_t)set.eng.key - 12, k);
            text_cat(t, k);
        }
    }
    center_text(60, &FONT_XS, t, C_SOFT);
    cv_blit(0, 26);
    /* the twelve chord keys (F .. E, as on the keyboard), the one sounding lit; the groove and its bar */
    cv_begin(240, 56, C_BG);
    for (i = 0; i < 12; i++) {
        int slot = key_slot((uint32_t)i), on = slot == s;
        uint32_t black = (0x54Au >> i) & 1u;  /* F# G# A# C# D# of F .. E */
        cv_rect(6 + i * 19, 2, 17, 14, on ? accent() : black ? C_PANEL : C_RULE);
    }
    st = set.eng.style;
    if (set.eng.groove_on && st) {
        groove_hits(st, set.eng.var, &hits);
        t[0] = 0;
        text_cat(t, STYLE_NAME[st]);
        text_cat(t, "  ");
        text_cat(t, groove_name(st, set.eng.var));
        cv_text(6, 22, &FONT_XS, t, C_TEXT);
    } else {
        cv_text(6, 22, &FONT_XS, "GROOVE OFF", C_SOFT);
    }
    if (set.hold)
        tag(240 - 6 - text_w(&FONT_XS, "HOLD") - 8, 21, "HOLD", accent(), dark_of(accent()));
    for (i = 0; i < 16; i++) {
        int now16 = eng_16th() == i && (eng_playing() || s >= 0);
        uint16_t c = now16 ? C_TEXT : (set.eng.groove_on && (hits >> i & 1u)) ? accent() : C_PANEL;
        cv_rect(6 + i * 14, 42, 12, 5, c);
    }
    cv_blit(0, 106);
    /* the sequence: its steps, the one playing (or the cursor, recording) lit */
    cv_begin(240, 42, C_BG);
    {
        int stp = eng_step(), len = set.eng.seq_len;
        cv_text(6, 2, &FONT_XS, ui.rec ? (eng_playing() ? "REC LIVE" : "REC STEP") : "SEQUENCE",
                ui.rec ? C_REC : C_SOFT);
        if (len) {
            t[0] = 0;
            int_text(len, t);
            text_cat(t, " x ");
            text_cat(t, STEPLEN_NAME[set.eng.steplen]);
            cv_text(234 - text_w(&FONT_XS, t), 2, &FONT_XS, t, C_SOFT);
        }
        for (i = 0; i < NSTEPS; i++) {
            uint8_t c = set.eng.seq[i];
            int32_t x = 6 + i * 14;
            uint16_t col = i >= len ? C_BG : c < 12u ? C_RULE : c == STEP_TIE ? C_PANEL : C_PANEL;
            if (i < len && c < 12u && (i == stp))
                col = accent();
            else if (i == stp)
                col = C_TEXT;
            if (ui.rec && !eng_playing() && i == ui.cursor)
                col = C_REC;
            cv_rect(x, 18, 12, 16, i < len || (ui.rec && i == ui.cursor) ? col : C_PANEL);
            if (i < len && c == STEP_TIE)
                cv_rect(x, 25, 12, 2, C_SOFT);
            if (i >= len && !(ui.rec && i == ui.cursor))
                cv_rect(x + 1, 19, 10, 14, C_BG);
        }
    }
    cv_blit(0, 162);
    knobs_footer(PG_HOME, 204);
}

static void draw_set(void)               /* the chord set: number, style, its twelve chords */
{
    const chordset_t *cs = &CHORDSETS[set.eng.set];
    char t[8];
    int i, s = eng_slot_now();
    header("CHORD SET");
    cv_begin(240, 144, C_BG);
    int_text(set.eng.set + 1, t);
    cv_text(8, 4, &FONT_L, t, C_TEXT);
    cv_text(16 + text_w(&FONT_L, t), 16, &FONT_B, cs->style, accent());
    for (i = 0; i < 12; i++) {           /* in keyboard order: F .. E */
        int slot = key_slot((uint32_t)i);
        int32_t x = 6 + (i % 4) * 58, y = 48 + (i / 4) * 30;
        int on = slot == s;
        cv_rect(x, y, 54, 26, on ? accent() : C_PANEL);
        cv_text(x + (54 - text_w(&FONT_XS, cs->name[slot])) / 2, y + 7, &FONT_XS, cs->name[slot],
                on ? dark_of(accent()) : C_TEXT);
    }
    cv_blit(0, 26);
    cv_begin(240, 70, C_BG);
    cv_text(8, 14, &FONT_XS, "OCT + ALGORITHM: KEY", C_SOFT);
    cv_text(8, 34, &FONT_XS, "A NEW SET PLAYS AT THE NEXT CHORD", C_SOFT);
    cv_blit(0, 170);
}

static void draw_groove(void)
{
    char t[8];
    int st = set.eng.style, i;
    uint32_t hits = 0;
    header("GROOVE");
    cv_begin(240, 144, C_BG);
    for (i = 0; i < 3; i++) {            /* the J-6's three groups */
        static const char *const G[3] = {"ARP", "BEAT", "PHRASE"};
        int on = (i == 0 && st <= 2) || (i == 1 && st >= 3 && st <= 5) || (i == 2 && st >= 6);
        int32_t x = 8 + i * 60;
        cv_rect(x, 6, 56, 18, on ? accent() : C_PANEL);
        cv_text(x + (56 - text_w(&FONT_XS, G[i])) / 2, 8, &FONT_XS, G[i], on ? dark_of(accent()) : C_SOFT);
    }
    cv_text(8, 36, &FONT_M, STYLE_NAME[st], C_TEXT);
    t[0] = 0;
    text_cat(t, "VAR ");
    int_text(set.eng.var + 1, t + 4);
    cv_text(8, 66, &FONT_B, t, C_SOFT);
    cv_text(90, 66, &FONT_B, groove_name(st, set.eng.var), accent());
    groove_hits(st, set.eng.var, &hits);
    for (i = 0; i < 16; i++)
        cv_rect(8 + i * 14, 100, 12, (hits >> i & 1u) ? 34 : 6, (hits >> i & 1u) ? accent() : C_PANEL);
    cv_blit(0, 26);
    cv_begin(240, 70, C_BG);
    cv_text(8, 14, &FONT_XS, set.eng.groove_on ? "SEQ: GROOVE ON" : "SEQ: GROOVE OFF", C_SOFT);
    cv_blit(0, 170);
}

static void draw_key(void)
{
    char t[8];
    header("KEY");
    cv_begin(240, 144, C_BG);
    if (set.eng.key == 12u) {
        t[0] = '0';
        t[1] = 0;
    } else {
        signed_text((int32_t)set.eng.key - 12, t);
    }
    center_text(40, &FONT_L, t, C_TEXT);
    center_text(90, &FONT_XS, "SEMITONES, THE CHORDS AND THE MELODY", C_SOFT);
    cv_blit(0, 26);
    cv_begin(240, 70, C_BG);
    cv_blit(0, 170);
}

static void draw_page(int page)          /* a page of four knobs: two by two */
{
    int k;
    header(PAGE_NAME[page]);
    cv_begin(240, 144, C_BG);
    for (k = 0; k < 4; k++) {
        int32_t x = 6 + (k % 2) * 116, y = 6 + (k / 2) * 70;
        char t[16];
        uint8_t *r = knob_ref(page, k);
        int lit = plat_ms() - ui.turned_at[EN_K1 + k] < 600u;
        cv_rect(x, y, 112, 64, C_PANEL);
        cv_rect(x, y, 112, 2, lit ? accent() : C_RULE);
        cv_text(x + 8, y + 6, &FONT_XS, knob_name(page, k), lit ? C_TEXT : C_SOFT);
        knob_text(page, k, t);
        cv_text(x + 8, y + 22, &FONT_M, t, C_TEXT);
        if (r) {
            cv_rect(x + 8, y + 52, 96, 5, C_RULE);
            cv_rect(x + 8, y + 52, (*r * 96 + 63) / 127, 5, accent());
        }
    }
    cv_blit(0, 26);
    cv_begin(240, 70, C_BG);
    if (page == PG_GLO) {                /* the load: how much of the audio's time the render takes */
        char t[16] = "CPU ";
        int_text((int32_t)plat_cpu_pct(), t + 4);
        text_cat(t, "%");
        cv_text(8, 30, &FONT_XS, t, C_SOFT);
    } else {
        cv_text(8, 30, &FONT_XS, page == PG_EDIT ? ENGINE_NAME[SND()->eng] : "K1 .. K4", C_SOFT);
    }
    cv_text(232 - text_w(&FONT_XS, "AGAIN: HOME"), 30, &FONT_XS, "AGAIN: HOME", C_SOFT);
    cv_blit(0, 170);
}

static void draw(void)
{
    if (ui.overlay && plat_ms() >= ui.overlay_until)
        ui.overlay = OV_NONE;
    if (ui.overlay == OV_SET)
        draw_set();
    else if (ui.overlay == OV_GROOVE)
        draw_groove();
    else if (ui.overlay == OV_KEY)
        draw_key();
    else if (ui.page == PG_HOME)
        draw_home();
    else
        draw_page(ui.page);
}

static void speak(void)                  /* a screen reader that has just started listening hears the screen */
{
    char t[96];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, ENGINE_NAME[SND()->eng]);
        text_cat(t, " ");
        text_cat(t, SND()->name);
        text_cat(t, ". SET ");
        {
            char n[8];
            int_text(set.eng.set + 1, n);
            text_cat(t, n);
        }
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BG);
    ui.live_key = -1;
    ui.redraw = 1;
    eng_live_chord(-1, 100);
}

void app_frame(void)
{
    static int last_step = -2, last16 = -1, last_slot = -2, last_ext = -1, last_play = -1;
    uint32_t now = plat_ms();
    int m;
    input();
    speak();
    /* USB host: what happened, in words */
    m = plat_usb_host_msg();
    if (m == USBH_MSG_MIDI)
        app_say("USB HOST", "LINKED");
    else if (m == USBH_MSG_LOST)
        app_say("USB HOST", "LOST");
    else if (m == USBH_MSG_NOMIDI)
        app_say("USB HOST", "NO MIDI");
    if (plat_usb_lost())
        eng_all_off();
    if (eng_step() != last_step || eng_16th() != last16 || eng_slot_now() != last_slot || eng_ext() != last_ext ||
        eng_playing() != last_play) {
        last_step = eng_step();
        last16 = eng_16th();
        last_slot = eng_slot_now();
        last_ext = eng_ext();
        last_play = eng_playing();
        ui.redraw = 1;
    }
    if ((ui.msg_until && now >= ui.msg_until) || (ui.overlay && now >= ui.overlay_until)) {
        if (now >= ui.msg_until)
            ui.msg_until = 0;
        ui.redraw = 1;
    }
    if (ui.redraw && now - ui.last_draw >= 30u) {
        ui.redraw = 0;
        ui.last_draw = now;
        draw();
    }
}

/* ----------------------------------------------------------------- backup --- */
int app_backup_playing(void) { return eng_playing(); }
void app_backup_flush(void)
{
    if (set_dirty)
        settings_save();
}
void app_backup_hold(int on) { (void)on; }
void app_backup_restored(void)
{
    settings_load();
    eng_bind(&set.eng);
    ui.redraw = 1;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "preset"))
        *got = set.preset;
    else if (!strcmp(what, "engine"))
        *got = SND()->eng;
    else if (!strcmp(what, "set"))
        *got = set.eng.set + 1;
    else if (!strcmp(what, "key"))
        *got = (int)set.eng.key - 12;
    else if (!strcmp(what, "octave"))
        *got = set.eng.octave;
    else if (!strcmp(what, "style"))
        *got = set.eng.style;
    else if (!strcmp(what, "var"))
        *got = set.eng.var;
    else if (!strcmp(what, "groove"))
        *got = set.eng.groove_on;
    else if (!strcmp(what, "slot"))
        *got = eng_slot_now();
    else if (!strcmp(what, "voices"))
        *got = eng_voices();
    else if (!strcmp(what, "notes"))
        *got = (int)eng_notes_played();
    else if (!strcmp(what, "playing"))
        *got = eng_playing();
    else if (!strcmp(what, "ext"))
        *got = eng_ext();
    else if (!strcmp(what, "clocks_sent"))
        *got = (int)eng_clocks_out();
    else if (!strcmp(what, "ticks"))
        *got = (int)eng_ticks();
    else if (!strcmp(what, "step"))
        *got = eng_step();
    else if (!strcmp(what, "seqlen"))
        *got = set.eng.seq_len;
    else if (!strncmp(what, "seq", 3) && what[3] >= '0' && what[3] <= '9')
        *got = set.eng.seq[atoi(what + 3) % NSTEPS];
    else if (!strcmp(what, "hold"))
        *got = set.hold;
    else if (!strcmp(what, "chords"))
        *got = set.chord_mode;
    else if (!strcmp(what, "bpm"))
        *got = set.eng.bpm;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "rec"))
        *got = ui.rec;
    else if (!strcmp(what, "usbhost"))
        *got = ui.usb_host;
    else if (!strcmp(what, "k1"))
        *got = SND()->k[0];
    else if (!strcmp(what, "note0")) {    /* the chord's lowest note */
        uint8_t nn[4];
        *got = eng_chord_now(nn) ? nn[0] : -1;
    } else
        return 0;
    return 1;
}
#endif
