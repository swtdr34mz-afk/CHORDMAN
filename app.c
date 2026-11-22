/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN: the app. The keys and the panel, the settings, MIDI in and the clock out, the screens and the game.
 *
 * The keyboard, split: LEFT the lower twelve keys (F3 .. E4), RIGHT the upper fifteen (F4 .. G5).
 *   CHORD mode  RIGHT: the set's eight chords on the white keys, each coloured on the black key after it
 *               LEFT:  the bass, on the white keys the set's scale (a bass arp on the note held, or on the chord's
 *                      root when none is)
 *   LEAD mode   LEFT:  the set's chords (seven), coloured on the black keys; played as chords or as a bass arp
 *               RIGHT: the lead, on the white keys the set's scale, the black keys a semitone over
 * The panel:
 *   PRESETS     the sound of the part in focus (RIGHT; LEFT on its pages, or while a left key is held)
 *   ALGORITHM   the chord set; with OCT- or OCT+ held: the key
 *   SELECT      the chords' groove (a rhythm or an arp)
 *   K1 .. K4    HOME and SOUND: TONE MOTION CHAR SPACE; each page its own four
 *   EDIT        SOUND (right, again: left, again: HOME)    ENV  the envelope (right, left, HOME)
 *   LFO         GROOVE      FX  SIDECHAIN      GLO  GLOBAL      HOME  home (held: the sound as designed)
 *   SEL         CHORD / LEAD     ARP  the left part: bass arp or not     SEQ  the groove on / off
 *   REC         HOLD (a chord stays)     PLAY  the transport (clock out)     SAVE  keep it all
 *   OCT- OCT+   the right part's octave; both: octave and key back to 0
 * Part of the unity build (device/unity.c, host/plat_host.c), after the platform: gfx.c and announce.c are in scope. */
#include "fastmath.h"
#include "engine.h"

/* --------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x434D4E32u            /* "CMN2" */
typedef struct {
    uint32_t magic;
    eng_cfg_t eng;
    uint8_t hold, unlocked, rsv[2];      /* unlocked: secret sets open (0 .. 5) */
    uint32_t xp, hi;                     /* points over all sessions; the best session */
} settings_t;
static settings_t set;
static uint8_t set_dirty;

static void part_default(part_t *p, int snd)
{
    const sound_t *s = &SOUNDS[snd];
    uint32_t i;
    p->snd = (uint8_t)snd;
    p->k[0] = 64;
    p->k[1] = 64;
    p->k[2] = s->type == T_JUNO ? (uint8_t)(s->chorus * 32u + 16u) : 64;
    p->k[3] = s->type == T_MOOG ? 30 : 56;
    for (i = 0; i < 4; i++)
        p->env[i] = s->env[i];
}

static void settings_defaults(void)
{
    uint32_t i;
    for (i = 0; i < sizeof set; i++)
        ((uint8_t *)&set)[i] = 0;
    set.magic = SET_MAGIC;
    set.eng.mode = MODE_CHORD;
    set.eng.left_bass = 1;
    set.eng.set = 2;                     /* HEARTBREAK */
    set.eng.key = 12;
    set.eng.groove_on = 1;
    set.eng.crhy = CR_EIGHTS;
    set.eng.bass = BA_OCTAVE;
    set.eng.swing = 20;
    set.eng.gate = 70;
    set.eng.sc[0] = 0;
    set.eng.sc[1] = SH_PUMP;
    set.eng.sc[2] = ST_VOLUME;
    set.eng.sc[3] = SK_44;
    set.eng.bpm = 122;
    set.eng.sync = SYNC_AUTO;
    part_default(&set.eng.part[MODE_CHORD][PART_L], 10);   /* MOOG BASS */
    part_default(&set.eng.part[MODE_CHORD][PART_R], 0);    /* JUNO POLY */
    part_default(&set.eng.part[MODE_LEAD][PART_L], 2);     /* JUNO PAD */
    part_default(&set.eng.part[MODE_LEAD][PART_R], 12);    /* MOOG LEAD */
}

static int settings_ok(void)
{
    uint32_t m, p;
    if (set.magic != SET_MAGIC || set.eng.mode > 1u || set.eng.set >= NSETS || set.eng.key > 24u ||
        set.eng.octave < -2 || set.eng.octave > 2 || set.eng.crhy >= NCRHY || set.eng.bass >= NBASS ||
        set.eng.sc[1] >= NSHAPES || set.eng.sc[2] >= NSCTGT || set.eng.sc[3] >= NSKICK || set.eng.bpm < 40u ||
        set.eng.bpm > 240u || set.eng.sync >= NSYNCS || set.unlocked > NSETS - NFREE || set.eng.left_bass > 3u)
        return 0;
    for (m = 0; m < 2u; m++)
        for (p = 0; p < NPARTS; p++)
            if (set.eng.part[m][p].snd >= NSOUNDS)
                return 0;
    return 1;
}
static void settings_load(void)
{
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || !settings_ok())
        settings_defaults();
    if (set.eng.set >= NFREE + set.unlocked)
        set.eng.set = 0;
}
static int settings_save(void)
{
    if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) != 0)
        return -1;
    set_dirty = 0;
    return 0;
}

/* ------------------------------------------------------------------ MIDI --- */
void app_clock_out(uint32_t st) { plat_midi_send(0x0Fu | st << 8, TO_USB); }

static void midi_in(void)                /* audio interrupt */
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st >= 0xF8u) {
            eng_midi_rt(st);
            continue;
        }
        if ((st & 0x0Fu) != 0u)          /* channel 1 only (a drum machine's notes on 10 are not ours) */
            continue;
        if ((st & 0xF0u) == 0x90u && d2)
            eng_note_on(PART_R, (int)d1, (int)d2);
        else if ((st & 0xF0u) == 0x80u || (st & 0xF0u) == 0x90u)
            eng_note_off(PART_R, (int)d1);
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
    plat_cpu2_set(1);
}

/* --------------------------------------------------------------------- UI --- */
enum { PG_HOME, PG_SOUND, PG_ENV, PG_GROOVE, PG_SC, PG_GLO, NPAGES };
static const char *const PAGE_NAME[NPAGES] = {"", "SOUND", "ENV", "GROOVE", "SIDECHAIN", "GLOBAL"};
enum { OV_NONE, OV_SET, OV_KEY };

static struct {
    int page, focus;                     /* focus: the part a SOUND / ENV page and PRESETS work on */
    int overlay;
    uint32_t overlay_until;
    char msg[2][24];
    uint32_t msg_until;
    uint32_t keys, btn;
    int chord_key;                       /* the key giving the chord now (-1 none) */
    int bass_key;
    uint8_t key_note[NKEYS];             /* the note a lead key plays */
    uint32_t turned_at[NE];
    uint32_t home_t;
    uint8_t oct_used;
    int usb_host;
    uint32_t last_draw;
    uint8_t redraw;
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
static void note_text(int note, char *b)
{
    b[0] = 0;
    text_cat(b, NOTE_NAME[note % 12]);
    int_text(note / 12 - 1, b + (b[1] ? 2 : 1));
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

#include "ui_pixfont.c"

#include "ui_draw.c"

#include "ui_game.c"

static void game_points(uint32_t p)
{
    G.score += p;
    set.xp += p;
    while (set.unlocked < NSETS - NFREE && set.xp >= UNLOCK_AT[set.unlocked]) {
        char t[28] = "UNLOCKED: ";
        text_cat(t, SETS[NFREE + set.unlocked].name);
        set.unlocked++;
        game_msg(t);
        settings_save();                 /* an unlock is kept at once */
    }
}

static int LB(void) { return (set.eng.left_bass >> (set.eng.mode & 1u)) & 1u; }
static part_t *PART(int p) { return &set.eng.part[set.eng.mode & 1u][p]; }
static uint16_t part_rgb(int p)
{
    if (p == PART_L)
        return set.eng.mode == MODE_CHORD || LB() ? C_MINT : C_AMBER;
    return set.eng.mode == MODE_CHORD ? C_AMBER : C_CORAL;
}
static const char *part_role(int p)
{
    if (p == PART_R)
        return set.eng.mode == MODE_CHORD ? "CHORDS" : "LEAD";
    if (set.eng.mode == MODE_CHORD)
        return LB() ? "BASS ARP" : "BASS";
    return LB() ? "BASS ARP" : "CHORDS";
}

/* the keys: which white key (0 ..) of its half a key is, and whether it is black (then: the white before it) */
static int key_white(uint32_t k, int *black)
{
    static const int8_t W[12] = {0, 0, 1, 1, 2, 2, 3, 4, 4, 5, 5, 6};   /* F F# G G# A A# B C C# D D# E */
    static const uint8_t B[12] = {0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0};
    uint32_t r = k < 12u ? k : k - 12u;
    *black = B[r % 12u];
    return W[r % 12u] + (r >= 12u ? 7 : 0);
}

static void chord_set_from(int key)      /* the chord a key asks for (-1: none) */
{
    int black, w;
    if (key < 0) {
        eng_chord(-1, 0, 0);
        return;
    }
    w = key_white((uint32_t)key, &black);
    if (w >= NCHORDS)
        w = 0;                           /* the top white key: the first chord again */
    eng_chord(w, black, 100);
}

static void key_down(uint32_t k, uint32_t vel)
{
    int black, w = key_white(k, &black), left = k < 12u;
    game_hit();
    if (set.eng.mode == MODE_CHORD) {
        if (!left) {
            ui.chord_key = (int)k;
            eng_chord(w >= NCHORDS ? 0 : w, black, (int)vel);
        } else {                         /* the bass: the scale's degree, black an octave up */
            int note = eng_scale_note(w, 36) + (black ? 12 : 0);
            ui.bass_key = (int)k;
            eng_bass_note(note);
        }
        return;
    }
    if (left) {
        ui.chord_key = (int)k;
        eng_chord(w >= NCHORDS ? 0 : w, black, (int)vel);
        return;
    }
    {                                    /* the lead: the scale from the tonic under C4, black a semitone up */
        int note = eng_scale_note(w, 60 + 12 * set.eng.octave) + (black ? 1 : 0);
        ui.key_note[k] = (uint8_t)clamp(note, 0, 127);
        eng_note_on(PART_R, ui.key_note[k], (int)vel);
    }
}

static int chord_zone(uint32_t k) { return set.eng.mode == MODE_CHORD ? k >= 12u : k < 12u; }

static void key_up(uint32_t k, uint32_t held)
{
    uint32_t j;
    if (chord_zone(k)) {
        if ((int)k != ui.chord_key || set.hold)
            return;
        ui.chord_key = -1;
        for (j = 0; j < NKEYS; j++)      /* another chord key still held: that one */
            if ((held >> j & 1u) && chord_zone(j))
                ui.chord_key = (int)j;
        chord_set_from(ui.chord_key);
        return;
    }
    if (set.eng.mode == MODE_CHORD) {    /* a bass key */
        if ((int)k != ui.bass_key)
            return;
        ui.bass_key = -1;
        for (j = 0; j < 12u; j++)
            if (held >> j & 1u)
                ui.bass_key = (int)j;
        if (ui.bass_key >= 0) {
            int black, w = key_white((uint32_t)ui.bass_key, &black);
            eng_bass_note(eng_scale_note(w, 36) + (black ? 12 : 0));
        } else {
            eng_bass_note(-1);
        }
        return;
    }
    eng_note_off(PART_R, ui.key_note[k]);
}

static void release_all(void)
{
    ui.chord_key = -1;
    ui.bass_key = -1;
    eng_chord(-1, 0, 0);
    eng_bass_note(-1);
    eng_all_off();
}

/* a knob on a page: its value's reference, name and text */
static uint8_t *knob_ref(int page, int k)
{
    part_t *p = PART(ui.focus);
    switch (page) {
    case PG_HOME:
    case PG_SOUND:
        return &p->k[k];
    case PG_ENV:
        return &p->env[k];
    default:
        return 0;
    }
}
static const char *knob_name(int page, int k)
{
    static const char *const S[4] = {"TONE", "MOTION", "", "SPACE"}, *const E[4] = {"ATTACK", "DECAY", "SUSTAIN", "RELEASE"},
                      *const GR[4] = {"CHORDS", "BASS", "SWING", "GATE"}, *const SC[4] = {"DEPTH", "SHAPE", "TARGET", "KICK"},
                      *const GL[4] = {"TEMPO", "KEY", "SYNC", "USB HOST"};
    switch (page) {
    case PG_HOME:
    case PG_SOUND:
        return k == 2 ? CHAR_NAME[SOUNDS[PART(ui.focus)->snd].type] : S[k];
    case PG_ENV:
        return E[k];
    case PG_GROOVE:
        return GR[k];
    case PG_SC:
        return SC[k];
    default:
        return GL[k];
    }
}
/* its value as text, and where it stands 0..1 (for the dial) */
static float knob_val(int page, int k, char *t)
{
    uint8_t *r = knob_ref(page, k);
    t[0] = 0;
    if (r) {
        if ((page == PG_HOME || page == PG_SOUND) && k == 2 && SOUNDS[PART(ui.focus)->snd].type == T_JUNO) {
            static const char *const CH[4] = {"OFF", "I", "II", "I+II"};
            text_cat(t, CH[*r >> 5]);
        } else {
            int_text(*r, t);
        }
        return (float)*r / 127.0f;
    }
    switch (page) {
    case PG_GROOVE:
        if (k == 0) {
            text_cat(t, CRHY_NAME[set.eng.crhy]);
            return (float)set.eng.crhy / (float)(NCRHY - 1);
        }
        if (k == 1) {
            text_cat(t, BASS_NAME[set.eng.bass]);
            return (float)set.eng.bass / (float)(NBASS - 1);
        }
        if (k == 2) {
            int_text(50 + (set.eng.swing * 25) / 127, t);
            text_cat(t, "%");
            return (float)set.eng.swing / 127.0f;
        }
        int_text((set.eng.gate * 100) / 127, t);
        text_cat(t, "%");
        return (float)set.eng.gate / 127.0f;
    case PG_SC:
        if (k == 0) {
            if (!set.eng.sc[0])
                text_cat(t, "OFF");
            else {
                int_text((set.eng.sc[0] * 100) / 127, t);
                text_cat(t, "%");
            }
            return (float)set.eng.sc[0] / 127.0f;
        }
        if (k == 1) {
            text_cat(t, SHAPE_NAME[set.eng.sc[1]]);
            return (float)set.eng.sc[1] / (float)(NSHAPES - 1);
        }
        if (k == 2) {
            text_cat(t, SCTGT_NAME[set.eng.sc[2]]);
            return (float)set.eng.sc[2] / (float)(NSCTGT - 1);
        }
        text_cat(t, SKICK_NAME[set.eng.sc[3]]);
        return (float)set.eng.sc[3] / (float)(NSKICK - 1);
    default:
        if (k == 0) {
            int_text(set.eng.bpm, t);
            return (float)(set.eng.bpm - 40) / 200.0f;
        }
        if (k == 1) {
            if (set.eng.key == 12u)
                text_cat(t, "0");
            else {
                if (set.eng.key > 12u)
                    text_cat(t, "+");
                int_text((int32_t)set.eng.key - 12, t + (set.eng.key > 12u ? 1 : 0));
            }
            return (float)set.eng.key / 24.0f;
        }
        if (k == 2) {
            text_cat(t, SYNC_NAME[set.eng.sync]);
            return (float)set.eng.sync / (float)(NSYNCS - 1);
        }
        text_cat(t, ui.usb_host ? (plat_usb_host_state() == USBH_CONNECTED ? "LINKED" : "ON") : "OFF");
        return ui.usb_host ? 1.0f : 0.0f;
    }
}

static int step_of(int32_t e) { return e > 0 ? 1 : -1; }
static void knob_turn(int k, int32_t e)
{
    uint32_t now = plat_ms(), dt = now - ui.turned_at[EN_K1 + k];
    int32_t acc = dt < 25u ? 4 : dt < 60u ? 2 : 1;
    uint8_t *r = knob_ref(ui.page, k);
    char t[16];
    ui.turned_at[EN_K1 + k] = now;
    ui.redraw = 1;
    set_dirty = 1;
    if (r) {
        *r = (uint8_t)clamp((int32_t)*r + e * acc, 0, 127);
        eng_changed();
    } else if (ui.page == PG_GROOVE) {
        if (k == 0)
            set.eng.crhy = (uint8_t)clamp(set.eng.crhy + step_of(e), 0, NCRHY - 1);
        else if (k == 1)
            set.eng.bass = (uint8_t)clamp(set.eng.bass + step_of(e), 0, NBASS - 1);
        else if (k == 2)
            set.eng.swing = (uint8_t)clamp(set.eng.swing + e * acc, 0, 127);
        else
            set.eng.gate = (uint8_t)clamp(set.eng.gate + e * acc, 0, 127);
        if (k < 2)
            eng_rechord();
    } else if (ui.page == PG_SC) {
        if (k == 0)
            set.eng.sc[0] = (uint8_t)clamp(set.eng.sc[0] + e * acc, 0, 127);
        else
            set.eng.sc[k] = (uint8_t)clamp(set.eng.sc[k] + step_of(e), 0, k == 1 ? NSHAPES - 1 : k == 2 ? NSCTGT - 1 : NSKICK - 1);
    } else {
        if (k == 0) {
            set.eng.bpm = (uint8_t)clamp(set.eng.bpm + e * (acc > 1 ? 2 : 1), 40, 240);
        } else if (k == 1) {
            set.eng.key = (uint8_t)clamp(set.eng.key + step_of(e), 0, 24);
            eng_rechord();
        } else if (k == 2) {
            set.eng.sync = (uint8_t)clamp(set.eng.sync + step_of(e), 0, NSYNCS - 1);
        } else {
            int on = e > 0;
            if (on != ui.usb_host) {
                ui.usb_host = on;
                plat_usb_host(on);
                app_say("USB HOST", on ? "ON" : "OFF");
            }
            return;
        }
    }
    if (ann_on()) {
        knob_val(ui.page, k, t);
        app_say(knob_name(ui.page, k), t);
    }
}

static void sound_step(int part, int d)  /* PRESETS: the part's sound */
{
    part_t *p = &set.eng.part[set.eng.mode & 1u][part];
    part_default(p, (p->snd + d + NSOUNDS) % NSOUNDS);
    set_dirty = 1;
    eng_changed();
    app_say(part == PART_L ? "LEFT" : "RIGHT", SOUNDS[p->snd].name);
}

static void overlay(int ov)
{
    ui.overlay = ov;
    ui.overlay_until = plat_ms() + 1800u;
    ui.redraw = 1;
}

static void button_down(uint32_t b)
{
    switch (b) {
    case B_EDIT:
    case B_ENV: {
        int pg = b == B_EDIT ? PG_SOUND : PG_ENV;
        if (ui.page != pg) {
            ui.page = pg;
            ui.focus = PART_R;
        } else if (ui.focus == PART_R) {
            ui.focus = PART_L;
        } else {
            ui.page = PG_HOME;
            ui.focus = PART_R;
        }
        ui.overlay = OV_NONE;
        ui.redraw = 1;
        break;
    }
    case B_LFO:
    case B_FX:
    case B_GLO: {
        int pg = b == B_LFO ? PG_GROOVE : b == B_FX ? PG_SC : PG_GLO;
        ui.page = ui.page == pg ? PG_HOME : pg;
        ui.focus = PART_R;
        ui.overlay = OV_NONE;
        ui.redraw = 1;
        break;
    }
    case B_HOME:
        ui.page = PG_HOME;
        ui.focus = PART_R;
        ui.overlay = OV_NONE;
        ui.home_t = plat_ms();
        ui.redraw = 1;
        break;
    case B_SEL:
        release_all();
        set.eng.mode ^= 1u;
        set_dirty = 1;
        eng_changed();
        eng_rechord();
        app_say(set.eng.mode == MODE_CHORD ? "CHORD MODE" : "LEAD MODE", "");
        break;
    case B_ARP:
        set.eng.left_bass ^= (uint8_t)(1u << (set.eng.mode & 1u));
        if (set.eng.mode == MODE_LEAD) { /* the left part's sound goes with its job: a bass, or a pad */
            part_default(PART(PART_L), LB() ? 10 : 2);
            eng_changed();
        }
        set_dirty = 1;
        eng_rechord();
        app_say("LEFT", part_role(PART_L));
        break;
    case B_SEQ:
        set.eng.groove_on ^= 1u;
        set_dirty = 1;
        eng_rechord();
        app_say("GROOVE", set.eng.groove_on ? "ON" : "OFF");
        break;
    case B_REC:
        set.hold ^= 1u;
        set_dirty = 1;
        if (!set.hold) {
            uint32_t j;
            ui.chord_key = -1;
            for (j = 0; j < NKEYS; j++)
                if ((ui.keys >> j & 1u) && chord_zone(j))
                    ui.chord_key = (int)j;
            chord_set_from(ui.chord_key);
        }
        app_say("HOLD", set.hold ? "ON" : "OFF");
        break;
    case B_PLAY:
        eng_play(!eng_playing());
        ui.redraw = 1;
        break;
    case B_SAVE:
        if (settings_save() == 0)
            app_say("SAVED", "");
        else
            app_say("SAVE", "FAILED");
        break;
    default:
        break;
    }
}

static void button_up(uint32_t b)
{
    if (b == B_OCTDN || b == B_OCTUP) {
        if (ui.oct_used) {
            if (!(ui.btn & (1u << B_OCTDN | 1u << B_OCTUP)))
                ui.oct_used = 0;
            return;
        }
        {
            int o = clamp(set.eng.octave + (b == B_OCTDN ? -1 : 1), -2, 2);
            char t[6];
            if (o != set.eng.octave) {
                release_all();
                set.eng.octave = (int8_t)o;
                set_dirty = 1;
                eng_rechord();
            }
            t[0] = o > 0 ? '+' : 0;
            t[1] = 0;
            int_text(o, t + (o > 0 ? 1 : 0));
            app_say("OCTAVE", t);
        }
    }
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k, now = plat_ms(), lit = 0, dim, litk = 0, dimk = 0;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit))
            key_down(k, touch_vel(k));
        else if (!(keys & bit) && (ui.keys & bit))
            key_up(k, keys);
    }
    if ((btn & (1u << B_OCTDN)) && (btn & (1u << B_OCTUP)) && !ui.oct_used) {
        ui.oct_used = 1;
        release_all();
        set.eng.octave = 0;
        set.eng.key = 12;
        set_dirty = 1;
        eng_rechord();
        app_say("OCTAVE KEY", "RESET");
    }
    {
        uint32_t pressed = btn & ~ui.btn, released = ui.btn & ~btn;
        ui.btn = btn;
        for (k = 0; k < NB; k++)
            if (pressed >> k & 1u)
                button_down(k);
        for (k = 0; k < NB; k++)
            if (released >> k & 1u)
                button_up(k);
    }
    if (ui.home_t && (btn >> B_HOME & 1u) && now - ui.home_t >= 1000u) {
        ui.home_t = 0;
        part_default(PART(PART_R), PART(PART_R)->snd);
        part_default(PART(PART_L), PART(PART_L)->snd);
        set_dirty = 1;
        eng_changed();
        app_say("SOUNDS", "AS DESIGNED");
    }
    if (!(btn >> B_HOME & 1u))
        ui.home_t = 0;
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        if (k == EN_PRESET) {
            int part = ui.focus;
            if (ui.page == PG_HOME && (keys & 0xFFFu))
                part = PART_L;
            sound_step(part, step_of(e));
        } else if (k == EN_ALGO) {
            if (btn & (1u << B_OCTDN | 1u << B_OCTUP)) {
                set.eng.key = (uint8_t)clamp(set.eng.key + step_of(e), 0, 24);
                ui.oct_used = 1;
                overlay(OV_KEY);
            } else {
                int n = NFREE + set.unlocked;
                set.eng.set = (uint8_t)((set.eng.set + step_of(e) + n) % n);
                overlay(OV_SET);
                if (ann_on())
                    app_say("SET", SETS[set.eng.set].name);
            }
            set_dirty = 1;
            eng_rechord();
        } else if (k == EN_SELECT) {
            set.eng.crhy = (uint8_t)((set.eng.crhy + step_of(e) + NCRHY) % NCRHY);
            set_dirty = 1;
            eng_rechord();
            app_say("CHORDS", CRHY_NAME[set.eng.crhy]);
        } else {
            knob_turn((int)(k - EN_K1), e);
        }
        ui.turned_at[k] = now;
    }
    if (keys != ui.keys)
        ui.redraw = 1;
    ui.keys = keys;
    /* the lights */
    if (ui.page == PG_SOUND) lit |= 1u << B_EDIT;
    if (ui.page == PG_ENV) lit |= 1u << B_ENV;
    if (ui.page == PG_GROOVE) lit |= 1u << B_LFO;
    if (ui.page == PG_SC) lit |= 1u << B_FX;
    if (ui.page == PG_GLO) lit |= 1u << B_GLO;
    if (ui.page == PG_HOME) lit |= 1u << B_HOME;
    if (set.eng.mode == MODE_LEAD) lit |= 1u << B_SEL;
    if (LB()) lit |= 1u << B_ARP;
    if (set.eng.groove_on) lit |= 1u << B_SEQ;
    if (set.hold) lit |= 1u << B_REC;
    if (set_dirty) lit |= 1u << B_SAVE;
    if (eng_playing()) lit |= 1u << B_PLAY;
    if (set.eng.octave < 0) lit |= 1u << B_OCTDN;
    if (set.eng.octave > 0) lit |= 1u << B_OCTUP;
    dim = ~lit & ((1u << NB) - 1u);
    {                                    /* the chord keys glow, the one sounding lit; the bass and lead keys dim */
        int cn = eng_chord_now();
        for (k = 0; k < NKEYS; k++) {
            int black, w = key_white(k, &black);
            if (chord_zone(k)) {
                if (w < NCHORDS || !black)
                    dimk |= 1u << k;
                if (cn >= 0 && (cn & 7) == (w >= NCHORDS ? 0 : w) && (cn >> 3) == black)
                    litk |= 1u << k;
            } else if (!black) {
                dimk |= 1u << k;
            }
        }
    }
    litk |= keys;
    plat_play_red(0);
    plat_lights(lit, litk, dim, dimk & ~litk, 1);
}
void app_input_only(void) { input(); }

/* ---------------------------------------------------------------- screens --- */
static void header(const char *title)
{
    char t[12];
    uint32_t b10 = eng_ext_bpm10();
    int32_t x;
    cv_begin(240, 21, C_BG);
    if (plat_ms() < ui.msg_until) {
        x = cv_text(6, 1, &FONT_B, ui.msg[0], C_TEXT);
        cv_text(x + 8, 1, &FONT_B, ui.msg[1], part_rgb(ui.focus));
    } else {
        x = pill(4, 3, SOUNDS[PART(ui.focus)->snd].name, part_rgb(ui.focus), dark_of(part_rgb(ui.focus)));
        if (title)
            cv_text(x + 6, 1, &FONT_B, title, C_TEXT);
        int_text(b10 ? (int32_t)((b10 + 5u) / 10u) : set.eng.bpm, t);
        text_r(236, 1, &FONT_B, t, eng_ext() ? C_MINT : C_TEXT);
        if (eng_ext())
            pill(200 - text_w(&FONT_XS, "LINK"), 3, ui.usb_host ? "LINK" : "EXT", C_MINT, dark_of(C_MINT));
        else if (eng_playing())
            pill(200 - text_w(&FONT_XS, "PLAY"), 3, "PLAY", C_PANEL, C_SOFT);
    }
    cv_blit(0, 0);
}

static void keys_strip(int32_t y)        /* the split: the left twelve, the right fifteen, what each plays */
{
    static const uint8_t WK[16] = PLAT_WHITE_KEYS;
    uint16_t lc = part_rgb(PART_L), rc = part_rgb(PART_R);
    int i, cn = eng_chord_now();
    cv_text(6, y, &FONT_XS, part_role(PART_L), lc);
    text_r(234, y, &FONT_XS, part_role(PART_R), rc);
    for (i = 0; i < 16; i++) {
        uint32_t k = WK[i];
        int32_t x = 6 + i * 14, black, w = key_white(k, &black);
        uint16_t c = k < 12u ? lc : rc;
        int on = (ui.keys >> k & 1u) != 0;
        if (!on && cn >= 0 && chord_zone(k) && (cn >> 3) == 0 && (cn & 7) == (w >= NCHORDS ? 0 : w))
            on = 1;
        cv_rect(x, y + 16, 12, 17, on ? c : dim3(c));
        (void)black;
    }
    cv_rect(6 + 7 * 14 - 2, y + 13, 2, 22, C_TEXT);
}

static void draw_home(void)
{
    char t[40];
    const chordset_t *s = &SETS[set.eng.set];
    int cn = eng_chord_now(), i;
    header(0);
    cv_begin(240, 121, C_BG);
    /* the set, the groove */
    t[0] = 0;
    if (set.eng.set + 1 < 10)
        text_cat(t, "0");
    int_text(set.eng.set + 1, t + (set.eng.set + 1 < 10 ? 1 : 0));
    text_cat(t, "  ");
    text_cat(t, s->name);
    cv_text(6, 1, &FONT_S, t, C_SOFT);
    if (set.eng.mode == MODE_CHORD)
        text_r(234, 1, &FONT_S, set.eng.groove_on ? CRHY_NAME[set.eng.crhy] : "HELD", C_AMBER);
    else
        text_r(234, 1, &FONT_S, SCALE_NAME[s->scale], C_CORAL);
    /* the big name: the chord (CHORD) or the lead's note (LEAD); and what the other part does */
    if (set.eng.mode == MODE_CHORD) {
        cv_text(4, 18, &FONT_L, cn >= 0 ? ((cn >> 3) ? s->flavor[cn & 7] : s->chord[cn & 7]) : "- -",
                cn >= 0 ? C_TEXT : C_RULE);
        text_r(234, 22, &FONT_XS, "BASS", C_MINT);
        t[0] = 0;
        if (eng_bass_now() >= 0) {
            note_text(eng_bass_now(), t);
            text_cat(t, "  ");
        }
        text_cat(t, LB() ? (set.eng.groove_on ? BASS_NAME[set.eng.bass] : "HELD") : "HELD");
        text_r(234, 36, &FONT_B, t, C_MINT);
    } else {
        int ln = eng_lead_now();
        if (ln >= 0)
            note_text(ln, t);
        cv_text(4, 18, &FONT_L, ln >= 0 ? t : "- -", ln >= 0 ? C_TEXT : C_RULE);
        text_r(234, 22, &FONT_XS, LB() ? "LEFT: BASS ARP" : "LEFT: CHORDS", part_rgb(PART_L));
        t[0] = 0;
        for (i = 0; i < 4; i++) {
            text_cat(t, s->chord[i]);
            if (i < 3)
                text_cat(t, "  ");
        }
        text_r(234, 36, &FONT_B, t, part_rgb(PART_L));
    }
    /* the tiles: the eight chords (CHORD) or the scale's notes (LEAD), the one sounding lit */
    if (set.eng.mode == MODE_CHORD) {
        for (i = 0; i < NCHORDS; i++) {
            int32_t x = 6 + i * 29;
            int on = cn >= 0 && (cn & 7) == i;
            const char *nm = on && (cn >> 3) ? s->flavor[i] : s->chord[i];
            rrect(x, 64, 27, 15, on ? C_AMBER : C_PANEL);
            text_c(x + 13, 64, &FONT_T, nm, on ? dark_of(C_AMBER) : C_TEXT);
        }
    } else {
        int ln = eng_lead_now();
        for (i = 0; i < 9; i++) {
            int32_t x = 6 + i * 26;
            int note = eng_scale_note(i, 60 + 12 * set.eng.octave), on = ln >= 0 && ln % 12 == note % 12;
            rrect(x, 64, 24, 15, on ? C_CORAL : C_PANEL);
            text_c(x + 12, 64, &FONT_XS, NOTE_NAME[note % 12], on ? dark_of(C_CORAL) : C_TEXT);
        }
    }
    keys_strip(84);
    cv_blit(0, 21);
    draw_game(142);
}

static void draw_set_overlay(void)       /* ALGORITHM: the set, its scale and its chords */
{
    const chordset_t *s = &SETS[set.eng.set];
    char t[24];
    int i;
    header("CHORD SET");
    cv_begin(240, 121, C_BG);
    t[0] = 0;
    int_text(set.eng.set + 1, t);
    cv_text(6, 2, &FONT_L, t, C_TEXT);
    cv_text(14 + text_w(&FONT_L, t), 6, &FONT_M, s->name, set.eng.set >= NFREE ? C_MINT : C_AMBER);
    t[0] = 0;
    text_cat(t, NOTE_NAME[(s->tonic + set.eng.key + 108) % 12]);
    text_cat(t, " ");
    text_cat(t, SCALE_NAME[s->scale]);
    if (set.eng.set >= NFREE)
        text_cat(t, "   SECRET");
    cv_text(8, 38, &FONT_XS, t, C_SOFT);
    for (i = 0; i < NCHORDS; i++) {
        int32_t x = 6 + (i % 4) * 58, y = 58 + (i / 4) * 30;
        rrect(x, y, 54, 26, i < 4 ? C_PANEL : RGB(22, 20, 40));
        text_c(x + 27, y + 5, &FONT_S, s->chord[i], i < 4 ? C_TEXT : C_SOFT);
    }
    cv_blit(0, 21);
    draw_game(142);
}

static void draw_key_overlay(void)
{
    char t[8];
    header("KEY");
    cv_begin(240, 121, C_BG);
    t[0] = 0;
    if (set.eng.key > 12u)
        text_cat(t, "+");
    int_text((int32_t)set.eng.key - 12, t + (set.eng.key > 12u ? 1 : 0));
    text_c(120, 30, &FONT_L, t, C_TEXT);
    text_c(120, 72, &FONT_XS, "SEMITONES: THE CHORDS, THE BASS, THE LEAD", C_SOFT);
    cv_blit(0, 21);
    draw_game(142);
}

static void page_extra(int page)         /* under the dials: the groove's bar, the sidechain's curve */
{
    int i;
    if (page == PG_GROOVE) {
        uint32_t ch = 0, ba = 0;
        crhy_hits(set.eng.crhy, &ch);
        bass_hits(set.eng.bass, &ba);
        for (i = 0; i < 16; i++) {
            int32_t x = 6 + i * 14;
            cv_rect(x, 102, 12, 5, (ch >> i & 1u) ? C_CORAL : C_PANEL);
            cv_rect(x, 111, 12, 5, (ba >> i & 1u) ? C_MINT : C_PANEL);
        }
    } else if (page == PG_SC) {
        int32_t x, py = -1;
        float d = (float)set.eng.sc[0] / 127.0f;
        for (x = 0; x < 228; x++) {
            float ph = (float)(x % 57) / 57.0f, v = 0.0f;
            int32_t y;
            switch (set.eng.sc[1]) {
            case SH_SOFT: v = ph < 0.5f ? 1.0f - ph * ph * 4.0f * (1.5f - ph * 2.0f) : 0.0f; break;
            case SH_HARD: v = ph < 0.12f ? 1.0f : fm_expf(-(ph - 0.12f) * 14.0f); break;
            case SH_GATE: v = ph < 0.45f ? 1.0f : 0.0f; break;
            default: v = fm_expf(-ph * 5.0f); break;
            }
            y = 100 + (int32_t)(v * d * 14.0f);
            cv_rect(6 + x, y, 1, 2, C_CORAL);
            if (py >= 0 && (y - py > 1 || py - y > 1))
                cv_rect(6 + x, y < py ? y : py, 1, (y < py ? py - y : y - py) + 1, C_CORAL);
            py = y;
        }
        {
            float duck = eng_duck();
            cv_rect(6, 118, (int32_t)(duck * 228.0f), 2, C_TEXT);
        }
    } else if (page == PG_SOUND || page == PG_ENV) {
        char t[32] = "";
        text_cat(t, ui.focus == PART_L ? "LEFT  " : "RIGHT  ");
        text_cat(t, part_role(ui.focus));
        text_cat(t, "   EDIT / ENV AGAIN: ");
        text_cat(t, ui.focus == PART_R ? "LEFT" : "HOME");
        cv_text(6, 104, &FONT_T, t, C_SOFT);
    } else if (page == PG_GLO) {
        char t[16] = "CPU ";
        int_text((int32_t)plat_cpu_pct(), t + 4);
        text_cat(t, "%");
        cv_text(6, 104, &FONT_T, t, C_SOFT);
    }
}

static void draw_page(int page)
{
    int k;
    header(PAGE_NAME[page]);
    cv_begin(240, 121, C_BG);
    for (k = 0; k < 4; k++) {
        int32_t cx = 30 + k * 60;
        char t[16], n[2] = {(char)('1' + k), 0};
        float v = knob_val(page, k, t);
        int lit = plat_ms() - ui.turned_at[EN_K1 + k] < 600u;
        text_c(cx, 2, &FONT_T, n, KNOB_RGB[k]);
        dial(cx, 33, 18, v, KNOB_RGB[k]);
        text_c(cx, 56, &FONT_B, t, lit ? KNOB_RGB[k] : C_TEXT);
        text_c(cx, 73, &FONT_T, knob_name(page, k), KNOB_RGB[k]);
    }
    page_extra(page);
    cv_blit(0, 21);
    draw_game(142);
}

static void draw(void)
{
    if (ui.overlay && plat_ms() >= ui.overlay_until)
        ui.overlay = OV_NONE;
    if (ui.overlay == OV_SET)
        draw_set_overlay();
    else if (ui.overlay == OV_KEY)
        draw_key_overlay();
    else if (ui.page == PG_HOME)
        draw_home();
    else
        draw_page(ui.page);
}

static void speak(void)
{
    char t[96];
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, set.eng.mode == MODE_CHORD ? "CHORD MODE. " : "LEAD MODE. ");
        text_cat(t, SETS[set.eng.set].name);
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

void app_ui_init(void)
{
    touch_on(1);
    lcd_fill(0, 0, 240, 240, C_BG);
    ui.chord_key = ui.bass_key = -1;
    ui.focus = PART_R;
    G.sour_d = 150.0f;
    ui.redraw = 1;
}

void app_frame(void)
{
    uint32_t now = plat_ms();
    int m;
    input();
    speak();
    game_frame();
    m = plat_usb_host_msg();
    if (m == USBH_MSG_MIDI)
        app_say("USB HOST", "LINKED");
    else if (m == USBH_MSG_LOST)
        app_say("USB HOST", "LOST");
    else if (m == USBH_MSG_NOMIDI)
        app_say("USB HOST", "NO MIDI");
    if (plat_usb_lost())
        release_all();
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.redraw = 1;
    }
    /* the whole screen when something on it changed; else the game strip alone, a frame every 33 ms */
    {
        static uint32_t sig;
        uint32_t s2 = (uint32_t)(eng_chord_now() + 1) | (uint32_t)(eng_bass_now() + 1) << 5 |
                      (uint32_t)(eng_lead_now() + 1) << 13 | (uint32_t)eng_playing() << 21 | (uint32_t)eng_ext() << 22 |
                      (uint32_t)(ui.overlay != 0) << 23;
        if (s2 != sig) {
            sig = s2;
            ui.redraw = 1;
        }
        if (ui.overlay && now >= ui.overlay_until)
            ui.redraw = 1;
        if (ui.page == PG_SC)
            ui.redraw = 1;               /* (the sidechain's bar moves) */
    }
    if (now - ui.last_draw >= 33u) {
        ui.last_draw = now;
        if (ui.redraw) {
            ui.redraw = 0;
            draw();
        } else {
            draw_game(142);
        }
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
    int cn = eng_chord_now();
    if (!strcmp(what, "page"))
        *got = ui.page;
    else if (!strcmp(what, "focus"))
        *got = ui.focus;
    else if (!strcmp(what, "mode"))
        *got = set.eng.mode;
    else if (!strcmp(what, "set"))
        *got = set.eng.set + 1;
    else if (!strcmp(what, "key"))
        *got = (int)set.eng.key - 12;
    else if (!strcmp(what, "octave"))
        *got = set.eng.octave;
    else if (!strcmp(what, "chord"))
        *got = cn >= 0 ? cn & 7 : -1;
    else if (!strcmp(what, "flavor"))
        *got = cn >= 0 ? cn >> 3 : -1;
    else if (!strcmp(what, "bass"))
        *got = eng_bass_now();
    else if (!strcmp(what, "lead"))
        *got = eng_lead_now();
    else if (!strcmp(what, "voices"))
        *got = eng_voices();
    else if (!strcmp(what, "notes"))
        *got = (int)eng_notes_played();
    else if (!strcmp(what, "playing"))
        *got = eng_playing();
    else if (!strcmp(what, "ext"))
        *got = eng_ext();
    else if (!strcmp(what, "ticks"))
        *got = (int)eng_ticks();
    else if (!strcmp(what, "clocks_sent"))
        *got = (int)eng_clocks_out();
    else if (!strcmp(what, "hold"))
        *got = set.hold;
    else if (!strcmp(what, "groove"))
        *got = set.eng.groove_on;
    else if (!strcmp(what, "crhy"))
        *got = set.eng.crhy;
    else if (!strcmp(what, "leftbass"))
        *got = LB();
    else if (!strcmp(what, "sndr"))
        *got = PART(PART_R)->snd;
    else if (!strcmp(what, "sndl"))
        *got = PART(PART_L)->snd;
    else if (!strcmp(what, "tone"))
        *got = PART(ui.focus)->k[0];
    else if (!strcmp(what, "sc"))
        *got = set.eng.sc[0];
    else if (!strcmp(what, "score"))
        *got = (int)G.score;
    else if (!strcmp(what, "combo"))
        *got = (int)G.combo;
    else if (!strcmp(what, "xp"))
        *got = (int)set.xp;
    else if (!strcmp(what, "unlocked"))
        *got = set.unlocked;
    else if (!strcmp(what, "duck100"))
        *got = (int)(eng_duck() * 100.0f);
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "usbhost"))
        *got = ui.usb_host;
    else if (!strcmp(what, "bpm"))
        *got = set.eng.bpm;
    else
        return 0;
    return 1;
}
#endif
