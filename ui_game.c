/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's game, the strip under every screen. CHORD MAN (an open ring that bites, the chord's notes as four bars
 * inside) runs through the bar in time, eating its sixteen beat dots. Every note you play is judged against the
 * grid: on it, PERFECT; near it, GOOD; else a miss. Hits build the COMBO and push the SOURS (the wrong notes,
 * cracked diamonds) back; misses let them close in, and one that reaches CHORD MAN breaks the combo. A combo of 32
 * starts FEVER: eight bars, double points, the sours run. Points add up over every session and unlock the five
 * secret chord sets; the best session is the high score. Part of app.c (included there). */
#define COMBO_FEVER 32
static const uint32_t UNLOCK_AT[NSETS - NFREE] = {3000, 10000, 25000, 50000, 100000};

static struct {
    uint32_t score, combo, best_combo;
    uint8_t meter[8];                    /* the last hits: 0 miss, 1 good, 2 perfect */
    uint32_t nmeter;
    float sour_d;                        /* how far ahead the sour on CHORD MAN's lane is (pixels) */
    float sour_v;                        /* how fast it closes in */
    uint32_t fever_until;                /* the bar FEVER ends at (0: none) */
    char msg[28];
    uint32_t msg_until;
    uint32_t idle_since;
    uint32_t flash_until;                /* a hit's word shows until */
    uint8_t last_q;
} G;

static void game_msg(const char *s)
{
    uint32_t i;
    for (i = 0; s[i] && i < sizeof G.msg - 1u; i++)
        G.msg[i] = s[i];
    G.msg[i] = 0;
    G.msg_until = plat_ms() + 1800u;
}

static int game_fever(void) { return G.fever_until && eng_bar() < G.fever_until; }

static void game_points(uint32_t p);

/* a note played: how near the grid */
static void game_hit(void)
{
    float pos = eng_pos16(), off = pos - (float)(int32_t)(pos + 0.5f);
    int q;
    off = off < 0.0f ? -off : off;
    q = off < 0.17f ? 2 : off < 0.34f ? 1 : 0;
    G.meter[G.nmeter++ % 8u] = (uint8_t)q;
    G.last_q = (uint8_t)q;
    G.flash_until = plat_ms() + 450u;
    if (q) {
        G.combo++;
        if (G.combo > G.best_combo)
            G.best_combo = G.combo;
        G.sour_d += q == 2 ? 14.0f : 6.0f;
        if (G.sour_d > 170.0f)
            G.sour_d = 170.0f;
        game_points((q == 2 ? 100u : 50u) * (1u + G.combo / 8u) * (game_fever() ? 2u : 1u));
        if (G.combo % COMBO_FEVER == 0u) {
            G.fever_until = eng_bar() + 8u;
            game_msg("FEVER!");
        }
    } else {
        G.combo = 0;
        G.sour_v += 0.25f;
    }
}

/* every frame: the sour closes in; caught, the combo is gone */
static void game_frame(void)
{
    uint32_t now = plat_ms();
    if (!eng_running()) {
        if (!G.idle_since)
            G.idle_since = now;
        if (now - G.idle_since > 6000u && G.score) {   /* a session over: it may be the best */
            if (G.score > set.hi) {
                set.hi = G.score;
                set_dirty = 1;
            }
            G.score = 0;
            G.combo = 0;
        }
        return;
    }
    G.idle_since = 0;
    if (game_fever()) {
        G.sour_d += 0.6f;                /* they run */
        if (G.sour_d > 170.0f)
            G.sour_d = 170.0f;
        return;
    }
    G.sour_d -= 0.12f + G.sour_v;
    G.sour_v *= 0.96f;
    if (G.sour_d < 10.0f) {
        G.combo = 0;
        G.sour_d = 150.0f;
        G.sour_v = 0.0f;
        game_msg("OUCH  COMBO LOST");
    }
}

/* CHORD MAN at x, y (his box 14 s), the mouth open or shut, n of his four bars lit */
static void draw_chordman(int32_t x, int32_t y, int32_t s, int open, int lit)
{
    int32_t r = 7 * s, cx = x + r, cy = y + r, ri = r - (r * 32) / 100, px, py, i;
    for (py = -r; py <= r; py++)
        for (px = -r; px <= r; px++) {
            int32_t d2 = px * px + py * py;
            int mouth = px > 0 && (py < 0 ? -py : py) * 100 < px * (open ? 68 : 25);
            if (d2 <= r * r && d2 >= ri * ri && !mouth)
                cv_pset(cx + px, cy + py, C_AMBER);
        }
    for (i = 0; i < 4; i++) {            /* the chord's four notes */
        int32_t yy = cy - (ri * 62) / 100 + i * (ri * 42) / 100, L = (ri * ((i == 1 || i == 2) ? 95 : 70)) / 100;
        cv_rect(cx - (ri * 75) / 100, yy - s / 2, L, s > 1 ? s : 1, i < lit ? C_TEXT : RGB(70, 64, 96));
    }
}

/* a SOUR: a cracked diamond; scared, hollow */
static void draw_sour(int32_t x, int32_t y, int32_t s, int scared)
{
    int32_t r = 6 * s, cx = x + r, cy = y + r, px, py;
    for (py = -r; py <= r; py++)
        for (px = -r; px <= r; px++) {
            int32_t d = (px < 0 ? -px : px) + (py < 0 ? -py : py);
            if (scared ? (d <= r && d >= r - s) : d <= r)
                cv_pset(cx + px, cy + py, scared ? RGB(200, 215, 255) : C_SOUR);
        }
    if (!scared) {
        cv_line(cx - r / 3, cy - r / 2, cx, cy, C_BG);
        cv_line(cx, cy, cx - r / 5, cy + r / 6, C_BG);
        cv_line(cx - r / 5, cy + r / 6, cx + r / 3, cy + r / 2, C_BG);
    }
}

static void six(char *t, uint32_t v)     /* six digits */
{
    int i;
    for (i = 5; i >= 0; i--) {
        t[i] = (char)('0' + v % 10u);
        v /= 10u;
    }
    t[6] = 0;
}

/* the strip: 240 x 98 at y */
static void draw_game(int32_t y)
{
    int fever = game_fever(), run = eng_running(), i;
    uint16_t wall = fever ? C_CORAL : C_WALL;
    int32_t ly = 20, lane = ly + 14, cmx;
    float pos = run ? eng_pos16() : 0.0f;
    char t[24], n[8];
    uint8_t nn[5];
    int lit = 0, cn = eng_chord_now();
    cv_begin(240, 98, fever ? C_FEVER_BG : C_BG);
    /* the scores */
    six(n, G.score);
    t[0] = 0;
    text_cat(t, "SCORE ");
    text_cat(t, n);
    pix_text(6, 3, 0, t, C_TEXT);
    six(n, G.score > set.hi ? G.score : set.hi);
    t[0] = 0;
    text_cat(t, "HI ");
    text_cat(t, n);
    pix_text(234 - pix_w(0, t), 3, 0, t, C_SOFT);
    /* the maze */
    rrect(4, ly, 232, 6, wall);
    rrect(4, ly + 46, 232, 6, wall);
    rframe(60, ly + 21, 61, 7, 2, wall);
    rframe(150, ly + 21, 51, 7, 2, wall);
    for (i = 0; i < 16; i++) {
        int32_t x = 14 + i * 14;
        if (run && (float)i < pos)
            continue;                    /* eaten */
        if (i % 4 == 0)
            cv_rect(x - 3, lane - 3, 7, 7, C_AMBER);
        else
            cv_rect(x - 1, lane - 1, 3, 3, C_DOT);
        cv_rect(x - 1, lane + 21, 3, 3, (i & 1) ? C_DOT : RGB(90, 80, 120));
    }
    /* CHORD MAN, the sours */
    if (cn >= 0)
        lit = eng_chord_notes(cn & 7, cn >> 3, nn);
    else if (eng_lead_now() >= 0 || eng_bass_now() >= 0)
        lit = 2;
    if (run) {
        cmx = 14 + (int32_t)(pos * 14.0f) - 24;
        draw_chordman(cmx, lane - 14, 2, ((int32_t)(pos * 2.0f)) & 1, lit);
        draw_sour(((cmx + (int32_t)G.sour_d) % 236), lane - 12, 2, fever);
        draw_sour(((cmx + 156) % 236), lane + 10, 2, fever);
    } else {                             /* waiting: he paces */
        uint32_t ph = (plat_ms() / 40u) % 160u;
        cmx = 10 + (int32_t)(ph < 80u ? ph : 160u - ph);
        draw_chordman(cmx, lane - 14, 2, (plat_ms() / 300u) & 1u, 0);
        draw_sour(200, lane - 12, 2, 0);
        draw_sour(30, lane + 10, 2, 0);
    }
    /* the bottom line: a message, FEVER, or the combo and the last hits */
    {
        int32_t yb = ly + 53;
        if (plat_ms() < G.msg_until) {
            pix_text(120 - pix_w(0, G.msg) / 2, yb + 3, 0, G.msg, C_MINT);
        } else if (fever) {
            pix_text(120 - pix_w(1, "FEVER x2") / 2, yb + 1, 1, "FEVER x2", C_AMBER);
        } else if (!run) {
            if ((plat_ms() / 500u) & 1u)
                pix_text(120 - pix_w(0, "PLAY A CHORD") / 2, yb + 3, 0, "PLAY A CHORD", C_AMBER);
        } else {
            t[0] = 0;
            text_cat(t, "COMBO x");
            int_text((int32_t)G.combo, t + 7);
            pix_text(6, yb + 1, 1, t, C_AMBER);
            for (i = 0; i < 8; i++) {
                uint32_t k = (G.nmeter + (uint32_t)i) % 8u;
                uint16_t c = G.nmeter + (uint32_t)i < 8u ? C_PANEL
                             : G.meter[k] == 2 ? C_MINT : G.meter[k] == 1 ? C_AMBER : C_CORAL;
                cv_rect(136 + i * 12, yb + 4, 9, 9, c);
            }
            if (plat_ms() < G.flash_until) {                   /* the last hit, said */
                const char *w = G.last_q == 2 ? "PERFECT" : G.last_q == 1 ? "GOOD" : "MISS";
                pix_text(90, yb + 3, 0, w, G.last_q == 2 ? C_MINT : G.last_q == 1 ? C_AMBER : C_CORAL);
            }
        }
    }
    cv_blit(0, (uint32_t)y);
}
