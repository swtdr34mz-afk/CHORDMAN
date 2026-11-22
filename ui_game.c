/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's game, the strip under every screen: a drive in the spirit of the 80s arcade racers, all of it drawn
 * here. Your convertible, seen from behind, cruises a road that runs to the horizon, its stripes passing in time
 * with the groove. The scenery changes every 16 bars: BEACH (sea, sand, palms), CITY (skyline, towers), SUNSET (the
 * sun on the sea, palm silhouettes), NIGHT (neon). Every note you play is judged against the grid: on it, PERFECT;
 * near it, GOOD; else a miss. Hits build the COMBO and the speed, and close in on the rival car ahead: pass it for a
 * bonus. A miss breaks the combo and lets it pull away. A combo of 32 is TURBO: eight bars, double points.
 * Points add up over every session and unlock the five secret chord sets; the best session is the high score.
 * Part of app.c (included there). */
#define COMBO_FEVER 32
static const uint32_t UNLOCK_AT[NSETS - NFREE] = {3000, 10000, 25000, 50000, 100000};

#define CAR_W 47
#define CAR_H 18
static const char *const CAR[CAR_H] = {
    "...............HHHH.........YYYY...............",
    "..............HHHHHH.......YYYYYY..............",
    "..............HHHHHH.......YYYYYY..............",
    ".....KKKKKKKKKrrrrKKKKKKKKKKKrrrrKKKKKKKK......",
    "....KhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhhK....",
    "...KRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRK...",
    "..KRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRK..",
    ".KRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRK.",
    "KRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRK",
    "KrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrrK",
    "KLLLLLLlKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKlLLLLLLK",
    "KLLLLLLlKKKKKKKKKKKKPPPPPPPKKKKKKKKKKKKlLLLLLLK",
    "KrrrrrrrrrrrrrrrrrrrPPPPPPPrrrrrrrrrrrrrrrrrrrK",
    "KRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRK",
    "KKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKKK",
    "GgGgGgG.................................GgGgGgG",
    "gGgGgGg.................................gGgGgGg",
    "GgGgGgG.................................GgGgGgG",
};

static uint16_t car_rgb(char c, int rival)
{
    switch (c) {
    case 'K': return RGB(24, 18, 24);
    case 'R': return rival ? RGB(225, 228, 235) : RGB(220, 36, 44);
    case 'r': return rival ? RGB(150, 155, 170) : RGB(140, 18, 32);
    case 'h': return rival ? RGB(255, 255, 255) : RGB(255, 110, 100);
    case 'H': return RGB(120, 66, 34);
    case 'Y': return rival ? RGB(40, 40, 40) : RGB(250, 212, 92);
    case 'L': return RGB(255, 56, 40);
    case 'l': return RGB(255, 190, 120);
    case 'P': return RGB(240, 238, 220);
    case 'G': return RGB(30, 30, 34);
    case 'g': return RGB(70, 70, 78);
    default: return 0;
    }
}

enum { ST_BEACH, ST_CITY, ST_SUNSET, ST_NIGHT, NSTAGES };
static const char *const STAGE_NAME[NSTAGES] = {"BEACH", "CITY", "SUNSET", "NIGHT"};
#define NOBJ 6
static struct {
    uint32_t score, combo, best_combo;
    uint8_t meter[8];
    uint32_t nmeter;
    uint32_t fever_until;
    char msg[28];
    uint32_t msg_until;
    uint32_t idle_since;
    uint32_t flash_until;
    uint8_t last_q;
    float scroll, last_beat, rival_z, speed;
    float obj_z[NOBJ];
    int8_t obj_side[NOBJ];
    uint8_t obj_kind[NOBJ];
    uint32_t seed;
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
static uint32_t grnd(void)
{
    G.seed = G.seed * 1103515245u + 12345u;
    return G.seed >> 16;
}

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
        G.rival_z -= q == 2 ? 0.6f : 0.3f;   /* closing in */
        game_points((q == 2 ? 100u : 50u) * (1u + G.combo / 8u) * (game_fever() ? 2u : 1u));
        if (G.combo % COMBO_FEVER == 0u) {
            G.fever_until = eng_bar() + 8u;
            game_msg("TURBO!");
        }
    } else {
        G.combo = 0;
        G.rival_z += 2.5f;               /* it pulls away */
    }
}

static int game_stage(void) { return eng_running() ? (int)((eng_bar() / 16u) % NSTAGES) : ST_BEACH; }

/* every frame: the road moves in time, faster with the combo; the scenery comes closer; the rival */
static void game_frame(void)
{
    uint32_t now = plat_ms(), i;
    float beat, d, mul;
    if (!G.seed) {
        G.seed = 7u;
        G.rival_z = 12.0f;
        for (i = 0; i < NOBJ; i++) {
            G.obj_z[i] = 2.0f + (float)i * 3.2f;
            G.obj_side[i] = (int8_t)((i & 1u) ? 1 : -1);
            G.obj_kind[i] = (uint8_t)(i % 3u);
        }
    }
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
        d = 0.02f;                       /* cruising, waiting */
        G.speed += (60.0f - G.speed) * 0.05f;
    } else {
        G.idle_since = 0;
        beat = (float)(eng_bar() * 16u) + eng_pos16();
        d = beat - G.last_beat;
        G.last_beat = beat;
        if (d < 0.0f || d > 4.0f)
            d = 0.0f;
        mul = game_fever() ? 3.0f : 1.0f + fm_minf((float)G.combo / 16.0f, 1.5f);
        d *= 0.5f * mul;
        G.speed += ((game_fever() ? 320.0f : 120.0f + fm_minf((float)G.combo * 5.0f, 170.0f)) - G.speed) * 0.08f;
        /* the rival: passed, a bonus; far behind, a new one */
        G.rival_z -= d * 0.1f;
        if (G.rival_z < 2.2f) {
            game_points(300u * (game_fever() ? 2u : 1u));
            game_msg("PASS!  +300");
            G.rival_z = 14.0f;
        } else if (G.rival_z > 20.0f) {
            G.rival_z = 20.0f;
        }
    }
    G.scroll += d;
    for (i = 0; i < NOBJ; i++) {         /* the roadside: closer each frame, a new one far off when passed */
        G.obj_z[i] -= d * 1.6f;
        if (G.obj_z[i] < 1.1f) {
            G.obj_z[i] += 19.0f;
            G.obj_side[i] = (int8_t)((grnd() & 1u) ? 1 : -1);
            G.obj_kind[i] = (uint8_t)(grnd() % 3u);
        }
    }
}

static uint16_t lerp_rgb(uint32_t a, uint32_t b, int32_t t, int32_t n)   /* a .. b, t of n (r, g, b 8 bits each) */
{
    int32_t r = (int32_t)(a >> 16) + (((int32_t)(b >> 16) - (int32_t)(a >> 16)) * t) / n;
    int32_t g = (int32_t)((a >> 8) & 255u) + (((int32_t)((b >> 8) & 255u) - (int32_t)((a >> 8) & 255u)) * t) / n;
    int32_t bl = (int32_t)(a & 255u) + (((int32_t)(b & 255u) - (int32_t)(a & 255u)) * t) / n;
    return RGB(r, g, bl);
}

#define YH 38                            /* the horizon */
static float road_cx(int32_t dy, float curve) { return 120.0f + curve * (float)((60 - dy) * (60 - dy)) / 60.0f; }
static float road_half(int32_t dy) { return 4.0f + (float)dy * 1.85f; }

/* a car (the sprite) at x, y, its width w (scaled, nearest) */
static void draw_car(int32_t x, int32_t y, int32_t w, int rival)
{
    int32_t h = (w * CAR_H) / CAR_W, px, py;
    if (w < 3)
        return;
    if (h < 2)
        h = 2;
    for (py = 0; py < h; py++)
        for (px = 0; px < w; px++) {
            char c = CAR[(py * CAR_H) / h][(px * CAR_W) / w];
            if (c != '.')
                cv_pset(x + px, y + py, car_rgb(c, rival));
        }
}

/* the roadside: a palm (its trunk leaning, five fronds), a tower with windows, a street light */
static void draw_obj(int32_t x, int32_t yb, int32_t s, int kind, int stage)
{
    int dark = stage == ST_SUNSET;
    if (s < 2)
        return;
    if (stage == ST_BEACH || stage == ST_SUNSET) {
        uint16_t tr = dark ? RGB(40, 16, 30) : RGB(120, 80, 40), fr = dark ? RGB(50, 20, 40) : RGB(40, 150, 60);
        int32_t top = yb - s * 2, i;
        if (kind == 2) {                 /* a bush */
            cv_rect(x - s / 2, yb - s / 2, s, s / 2 + 1, fr);
            return;
        }
        for (i = 0; i < s * 2; i++)
            cv_rect(x + (i * i) / (s * 6 + 1), yb - i, s / 6 + 1, 1, tr);
        {
            int32_t tx = x + (s * 2 * s * 2) / (s * 6 + 1);
            cv_line(tx, top, tx - s, top + s / 2, fr);
            cv_line(tx, top, tx + s, top + s / 2, fr);
            cv_line(tx, top, tx - s / 2, top + s, fr);
            cv_line(tx, top, tx + s / 2, top + s, fr);
            cv_line(tx, top, tx, top - s / 3, fr);
            cv_rect(tx - s / 4, top - 1, s / 2 + 1, 2, fr);
        }
        return;
    }
    if (kind == 2) {                     /* a street light */
        cv_rect(x, yb - s * 2, 1 + s / 8, s * 2, RGB(70, 70, 90));
        cv_rect(x - s / 3, yb - s * 2, s / 3 + 1, 1 + s / 8, RGB(70, 70, 90));
        cv_rect(x - s / 3, yb - s * 2 + 1, 2 + s / 10, 1 + s / 10, stage == ST_NIGHT ? RGB(255, 230, 140) : RGB(200, 200, 210));
        return;
    }
    {                                    /* a tower */
        int32_t w = s + s / 2, h = s * (kind ? 3 : 2), wx, wy;
        uint16_t body = stage == ST_NIGHT ? RGB(30, 26, 60) : RGB(110, 120, 150);
        cv_rect(x - w / 2, yb - h, w, h, body);
        for (wy = yb - h + 2; wy < yb - 2; wy += 3 + s / 6)
            for (wx = x - w / 2 + 2; wx < x + w / 2 - 1; wx += 3 + s / 6)
                cv_rect(wx, wy, 1 + s / 10, 1 + s / 10,
                        stage == ST_NIGHT ? (((wx + wy) & 4) ? RGB(255, 90, 180) : RGB(255, 220, 120)) : RGB(190, 220, 255));
    }
}

static void six(char *t, uint32_t v)
{
    int i;
    for (i = 5; i >= 0; i--) {
        t[i] = (char)('0' + v % 10u);
        v /= 10u;
    }
    t[6] = 0;
}
static void pix_shadow(int32_t x, int32_t y, int big, const char *s, uint16_t c)
{
    pix_text(x + 1, y + 1, big, s, RGB(10, 10, 20));
    pix_text(x, y, big, s, c);
}

/* the strip: 240 x 98 at y */
static void draw_game(int32_t y)
{
    static const uint32_t SKY_TOP[NSTAGES] = {0x2F7FD8, 0x5A8CD0, 0x30205A, 0x08081C};
    static const uint32_t SKY_LOW[NSTAGES] = {0x8CC8F8, 0xB4C8E0, 0xFF8A50, 0x281848};
    int stage = game_stage(), fever = game_fever(), run = eng_running(), i;
    float curve = fm_sinf(G.scroll * 0.02f) * 1.1f, sc = G.scroll;
    int32_t yy, x;
    char t[24], n[8];
    cv_begin(240, 98, C_BG);
    /* the sky, the clouds or the sun or the stars */
    for (yy = 0; yy < YH; yy++)
        cv_rect(0, yy, 240, 1, lerp_rgb(fever ? 0x6A1C8C : SKY_TOP[stage], fever ? 0xFF6AA0 : SKY_LOW[stage], yy, YH));
    if (stage == ST_BEACH || stage == ST_CITY) {
        for (i = 0; i < 4; i++) {
            int32_t cx = (int32_t)((i * 70 + 20) - (int32_t)(sc * 0.3f) % 300 + 300) % 300 - 30;
            cv_rect(cx, 8 + (i & 1) * 6, 34, 6, RGB(250, 250, 255));
            cv_rect(cx + 6, 4 + (i & 1) * 6, 20, 5, RGB(250, 250, 255));
            cv_rect(cx + 2, 13 + (i & 1) * 6, 30, 2, RGB(220, 230, 245));
        }
    } else if (stage == ST_SUNSET) {
        for (yy = -12; yy <= 12; yy++) {
            int32_t w = 0;
            while ((w + 1) * (w + 1) + yy * yy <= 144)
                w++;
            if (yy > 2 && (yy % 3) == 0)
                continue;                /* the sun's stripes */
            cv_rect(170 - w, YH - 6 + yy, 2 * w + 1, 1, lerp_rgb(0xFFE070, 0xFF5070, yy + 12, 24));
        }
    } else {
        for (i = 0; i < 24; i++)
            cv_pset((i * 97 + 13) % 240, (i * 37 + 3) % (YH - 6), RGB(230, 230, 255));
    }
    /* the horizon: the far shore, the skyline */
    if (stage == ST_CITY || stage == ST_NIGHT) {
        for (x = 0; x < 240; x += 12) {
            int32_t h = 6 + ((x * 7 + 3) % 13);
            cv_rect(x, YH - h, 11, h, stage == ST_NIGHT ? RGB(60, 40, 110) : RGB(120, 130, 165));
            if (stage == ST_NIGHT) {
                cv_pset(x + 3, YH - h + 3, RGB(255, 100, 190));
                cv_pset(x + 7, YH - h + 6, RGB(255, 220, 120));
                cv_pset(x + 5, YH - h + 9, RGB(120, 220, 255));
            }
        }
    } else {
        cv_rect(0, YH - 3, 110, 3, stage == ST_SUNSET ? RGB(60, 20, 50) : RGB(40, 140, 70));
    }
    /* the ground, line by line: the sea and the sand (or the pavement), the rumble strips, the road, its lanes */
    for (yy = YH; yy < 98; yy++) {
        int32_t dy = yy - YH + 1, cx, h, rw, band, dash;
        float z = 80.0f / (float)dy;
        uint16_t side, far;
        cx = (int32_t)road_cx(dy, curve);
        h = (int32_t)road_half(dy);
        rw = h / 8 + 1;
        band = ((int32_t)(z * 2.0f + sc * 4.0f)) & 1;
        dash = (((int32_t)(z * 2.0f + sc * 4.0f)) & 3) < 2;
        switch (stage) {
        case ST_BEACH:
            side = band ? RGB(236, 222, 180) : RGB(226, 210, 166);
            far = band ? RGB(60, 180, 210) : RGB(80, 196, 222);
            break;
        case ST_SUNSET:
            side = band ? RGB(200, 120, 90) : RGB(186, 108, 84);
            far = band ? RGB(130, 60, 110) : RGB(150, 70, 120);
            break;
        case ST_CITY:
            side = band ? RGB(150, 150, 160) : RGB(136, 136, 148);
            far = RGB(100, 104, 120);
            break;
        default:
            side = band ? RGB(40, 34, 70) : RGB(32, 28, 60);
            far = RGB(20, 18, 40);
            break;
        }
        cv_rect(0, yy, 240, 1, side);
        if (stage == ST_BEACH || stage == ST_SUNSET)
            cv_rect(0, yy, cx - h - h / 2, 1, far);   /* the sea on the left */
        cv_rect(cx - h - rw, yy, rw, 1, band ? RGB(230, 40, 50) : RGB(245, 245, 245));
        cv_rect(cx + h, yy, rw, 1, band ? RGB(230, 40, 50) : RGB(245, 245, 245));
        cv_rect(cx - h, yy, 2 * h, 1, stage == ST_NIGHT ? (band ? RGB(56, 56, 70) : RGB(50, 50, 64))
                                                         : (band ? RGB(120, 120, 128) : RGB(112, 112, 120)));
        if (dash) {
            int32_t dw = h / 24 + 1;
            cv_rect(cx - h / 3 - dw / 2, yy, dw, 1, RGB(240, 240, 240));
            cv_rect(cx + h / 3 - dw / 2, yy, dw, 1, RGB(240, 240, 240));
        }
    }
    /* the roadside, far ones first */
    for (i = 0; i < NOBJ; i++) {
        int j, far = -1;
        float fz = 0.0f;
        static uint8_t drawn;
        if (!i)
            drawn = 0;
        for (j = 0; j < NOBJ; j++)
            if (!(drawn >> j & 1u) && G.obj_z[j] > fz) {
                fz = G.obj_z[j];
                far = j;
            }
        if (far < 0)
            break;
        drawn |= (uint8_t)(1u << far);
        {
            int32_t dy = (int32_t)(80.0f / G.obj_z[far]);
            if (dy >= 1 && dy < 98 - YH) {
                int32_t ox = (int32_t)road_cx(dy, curve) + G.obj_side[far] * ((int32_t)road_half(dy) + dy / 2 + 4);
                draw_obj(ox, YH + dy, dy / 3 + 1, G.obj_kind[far], stage);
            }
        }
    }
    /* the rival, then you */
    if (run) {
        int32_t dy = (int32_t)(80.0f / G.rival_z);
        if (dy >= 1 && dy < 98 - YH) {
            int32_t w = (int32_t)(47.0f * (float)dy / 60.0f), rx = (int32_t)road_cx(dy, curve) + (int32_t)road_half(dy) / 3;
            draw_car(rx - w / 2, YH + dy - (w * CAR_H) / CAR_W, w, 1);
        }
    }
    draw_car(120 - CAR_W / 2 - (int32_t)(curve * 6.0f), 98 - CAR_H - 1, CAR_W, 0);
    /* the dashboard: score, high score; the combo or a word; speed and stage */
    six(n, G.score);
    t[0] = 0;
    text_cat(t, "SCORE ");
    text_cat(t, n);
    pix_shadow(4, 2, 0, t, C_TEXT);
    six(n, G.score > set.hi ? G.score : set.hi);
    t[0] = 0;
    text_cat(t, "HI ");
    text_cat(t, n);
    pix_shadow(236 - pix_w(0, t), 2, 0, t, C_TEXT);
    if (plat_ms() < G.msg_until) {
        pix_shadow(120 - pix_w(0, G.msg) / 2, 16, 0, G.msg, C_AMBER);
    } else if (fever) {
        pix_shadow(120 - pix_w(1, "TURBO x2") / 2, 14, 1, "TURBO x2", C_AMBER);
    } else if (!run) {
        if ((plat_ms() / 500u) & 1u)
            pix_shadow(120 - pix_w(0, "PLAY A CHORD") / 2, 16, 0, "PLAY A CHORD", C_AMBER);
    } else {
        t[0] = 0;
        text_cat(t, "COMBO x");
        int_text((int32_t)G.combo, t + 7);
        pix_shadow(4, 15, 0, t, C_AMBER);
        if (plat_ms() < G.flash_until) {
            const char *w = G.last_q == 2 ? "PERFECT" : G.last_q == 1 ? "GOOD" : "MISS";
            pix_shadow(120 - pix_w(0, w) / 2, 15, 0, w, G.last_q == 2 ? C_MINT : G.last_q == 1 ? C_AMBER : C_CORAL);
        }
        for (i = 0; i < 8; i++) {
            uint32_t k = (G.nmeter + (uint32_t)i) % 8u;
            uint16_t c = G.nmeter + (uint32_t)i < 8u ? RGB(60, 60, 80)
                         : G.meter[k] == 2 ? C_MINT : G.meter[k] == 1 ? C_AMBER : C_CORAL;
            cv_rect(188 + i * 6, 17, 5, 7, c);
        }
    }
    int_text((int32_t)G.speed, t);
    pix_shadow(4, 98 - 17, 1, t, RGB(255, 120, 40));
    pix_shadow(4 + pix_w(1, t) + 2, 98 - 13, 0, "KM/H", RGB(255, 120, 40));
    t[0] = 0;
    text_cat(t, STAGE_NAME[stage]);
    pix_shadow(236 - pix_w(0, t), 98 - 13, 0, t, RGB(255, 230, 60));
    cv_blit(0, (uint32_t)y);
}
