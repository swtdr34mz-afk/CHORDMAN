/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's drawing: the colours, pills, the knob dials, the arcade lettering. Part of app.c (included there),
 * after the platform's gfx.c. */
#define C_BG RGB(12, 12, 26)
#define C_PANEL RGB(28, 26, 52)
#define C_RULE RGB(52, 48, 90)
#define C_SOFT RGB(140, 136, 175)
#define C_TEXT RGB(246, 240, 228)
#define C_WALL RGB(124, 64, 220)
#define C_DOT RGB(255, 214, 150)
#define C_SOUR RGB(170, 230, 60)
#define C_CORAL RGB(255, 84, 112)
#define C_AMBER RGB(255, 186, 40)
#define C_MINT RGB(60, 224, 176)
#define C_SKY RGB(90, 184, 255)
#define C_FEVER_BG RGB(40, 10, 50)
static const uint16_t KNOB_RGB[4] = {C_CORAL, C_AMBER, C_MINT, C_SKY};

static uint16_t dim3(uint16_t c)         /* a colour at a third: an unlit key */
{
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return (uint16_t)(((r / 3u) << 11) | ((g / 3u) << 5) | (b / 3u));
}
static uint16_t dark_of(uint16_t c)      /* the colour, very dark: text on it */
{
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return (uint16_t)(((r / 6u) << 11) | ((g / 6u) << 5) | (b / 6u));
}

/* a rectangle with its corners cut (a rounded look at this size) */
static void rrect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    cv_rect(x + 1, y, w - 2, h, c);
    cv_rect(x, y + 1, 1, h - 2, c);
    cv_rect(x + w - 1, y + 1, 1, h - 2, c);
}
static void rframe(int32_t x, int32_t y, int32_t w, int32_t h, int32_t t, uint16_t c)
{
    cv_rect(x + 2, y, w - 4, t, c);
    cv_rect(x + 2, y + h - t, w - 4, t, c);
    cv_rect(x, y + 2, t, h - 4, c);
    cv_rect(x + w - t, y + 2, t, h - 4, c);
}

static int32_t pill(int32_t x, int32_t y, const char *s, uint16_t bg, uint16_t fg)
{
    int32_t w = text_w(&FONT_XS, s) + 10;
    rrect(x, y, w, 15, bg);
    cv_text(x + 5, y, &FONT_XS, s, fg);
    return x + w;
}

static void text_c(int32_t cx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(cx - text_w(f, s) / 2, y, f, s, c);
}
static void text_r(int32_t rx, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(rx - text_w(f, s), y, f, s, c);
}

/* a knob: a 270-degree track, its value lit along it, a dot at the end */
static void dial(int32_t cx, int32_t cy, int32_t r, float v, uint16_t c)
{
    int32_t a;
    for (a = 0; a <= 270; a += 3) {
        float rad = (float)(135 + a) * (3.14159265f / 180.0f), co = fm_cosf(rad), si = fm_sinf(rad);
        uint16_t col = (float)a <= v * 270.0f ? c : C_PANEL;
        int32_t t;
        for (t = 0; t < 5; t++) {
            float rr = (float)(r - 2 + t);
            cv_pset(cx + (int32_t)(co * rr), cy + (int32_t)(si * rr), col);
        }
    }
    {
        float rad = (float)(135.0f + v * 270.0f) * (3.14159265f / 180.0f);
        cv_line(cx, cy, cx + (int32_t)(fm_cosf(rad) * (float)(r - 7)), cy + (int32_t)(fm_sinf(rad) * (float)(r - 7)), C_TEXT);
        cv_line(cx + 1, cy, cx + 1 + (int32_t)(fm_cosf(rad) * (float)(r - 7)), cy + (int32_t)(fm_sinf(rad) * (float)(r - 7)), C_TEXT);
    }
}

/* the arcade lettering (ui_pixfont.c): s at x, y; scale 1 or 2 */
static int32_t pix_text(int32_t x, int32_t y, int big, const char *s, uint16_t c)
{
    int32_t w = big ? PIX16_W : PIX12_W, h = big ? PIX16_H : PIX12_H;
    for (; *s; s++) {
        uint32_t i;
        for (i = 0; PIX_CHARS[i] && PIX_CHARS[i] != *s; i++)
            ;
        if (PIX_CHARS[i]) {
            int32_t r, b;
            for (r = 0; r < h; r++) {
                uint32_t bits = big ? PIX16[i][r] : PIX12[i][r];
                for (b = 0; b < w; b++)
                    if (bits >> (w - 1 - b) & 1u)
                        cv_pset(x + b, y + r, c);
            }
        }
        x += w;
    }
    return x;
}
static int32_t pix_w(int big, const char *s)
{
    int32_t n = 0;
    while (s[n])
        n++;
    return n * (big ? PIX16_W : PIX12_W);
}
