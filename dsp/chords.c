/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's chords: a chord's name read into its notes ("Am7", "G/B", "E7b9", "C9sus4" ...), the scales, and the
 * voicing: four notes at most, the 5th dropped first, each chord voiced close to the one before it so progressions
 * move by steps, not jumps. Part of dsp/engine.c. */
typedef struct {
    uint8_t root, bass;                  /* pitch classes (bass: the slash note, else the root) */
    uint8_t n;
    uint8_t iv[6];                       /* intervals over the root, in the order they matter (root, 3rd, 7th, top ...) */
} chord_t;

static int pc_of(const char **s)
{
    static const int8_t L[7] = {9, 11, 0, 2, 4, 5, 7};   /* A .. G */
    const char *p = *s;
    int pc;
    if (*p < 'A' || *p > 'G')
        return -1;
    pc = L[*p - 'A'];
    p++;
    if (*p == '#') {
        pc++;
        p++;
    } else if (*p == 'b') {
        pc--;
        p++;
    }
    *s = p;
    return (pc + 12) % 12;
}
static int starts(const char *s, const char *w)
{
    while (*w)
        if (*s++ != *w++)
            return 0;
    return 1;
}

static void chord_parse(const char *s, chord_t *c)
{
    int third = 4, fifth = 7, sev = -1, ext[3], ne = 0, sus = 0, six = 0, i;
    int r = pc_of(&s);
    c->root = (uint8_t)(r < 0 ? 0 : r);
    c->bass = c->root;
    if (starts(s, "maj")) {
        s += 3;
        sev = 11;
        if (*s == '9') {
            ext[ne++] = 14;
            s++;
        } else if (starts(s, "13")) {
            ext[ne++] = 21;
            s += 2;
        } else if (*s == '7') {
            s++;
        }
    } else if (starts(s, "dim")) {
        third = 3;
        fifth = 6;
        s += 3;
        if (*s == '7') {
            sev = 9;
            s++;
        }
    } else if (starts(s, "aug")) {
        fifth = 8;
        s += 3;
    } else if (*s == 'm') {
        third = 3;
        s++;
    }
    /* the number: 5 6 7 9 11 13, and m(maj)7 */
    if (starts(s, "maj7")) {
        sev = 11;
        s += 4;
    } else if (starts(s, "13")) {
        sev = sev < 0 ? 10 : sev;
        ext[ne++] = 21;
        s += 2;
    } else if (starts(s, "11")) {
        sev = sev < 0 ? 10 : sev;
        ext[ne++] = 17;
        if (third == 3)
            ext[ne++] = 14;
        s += 2;
    } else if (*s == '9') {
        sev = sev < 0 ? 10 : sev;
        ext[ne++] = 14;
        s++;
    } else if (*s == '7') {
        sev = sev < 0 ? 10 : sev;
        s++;
    } else if (*s == '6') {
        six = 1;
        s++;
    }
    /* what follows: sus, add, altered notes */
    while (*s && *s != '/') {
        if (starts(s, "sus2")) {
            sus = 2;
            s += 4;
        } else if (starts(s, "sus4")) {
            sus = 5;
            s += 4;
        } else if (starts(s, "sus")) {
            sus = 5;
            s += 3;
        } else if (starts(s, "add9") || starts(s, "add2")) {
            if (ne < 3)
                ext[ne++] = 14;
            s += 4;
        } else if (starts(s, "add11")) {
            if (ne < 3)
                ext[ne++] = 17;
            s += 5;
        } else if (starts(s, "b5")) {
            fifth = 6;
            s += 2;
        } else if (starts(s, "#5")) {
            fifth = 8;
            s += 2;
        } else if (starts(s, "b9")) {
            if (ne < 3)
                ext[ne++] = 13;
            s += 2;
        } else if (starts(s, "#9")) {
            if (ne < 3)
                ext[ne++] = 15;
            s += 2;
        } else if (starts(s, "#11")) {
            if (ne < 3)
                ext[ne++] = 18;
            s += 3;
        } else if (starts(s, "b13")) {
            if (ne < 3)
                ext[ne++] = 20;
            s += 3;
        } else {
            s++;
        }
    }
    if (*s == '/') {
        s++;
        r = pc_of(&s);
        if (r >= 0)
            c->bass = (uint8_t)r;
    }
    /* the notes that matter, in order: root, 3rd (or the sus), the 7th or 6th, the top extensions, the 5th */
    c->n = 0;
    c->iv[c->n++] = 0;
    c->iv[c->n++] = (uint8_t)(sus ? sus : third);
    if (sev >= 0)
        c->iv[c->n++] = (uint8_t)sev;
    else if (six)
        c->iv[c->n++] = 9;
    for (i = 0; i < ne && c->n < 5; i++)
        c->iv[c->n++] = (uint8_t)ext[i];
    if (c->n < 6)
        c->iv[c->n++] = (uint8_t)fifth;
}

static const uint16_t SCALES[NSCALES] = {
    0xAB5u,                              /* major: 0 2 4 5 7 9 11 */
    0x5ADu,                              /* minor: 0 2 3 5 7 8 10 */
    0x6ADu,                              /* dorian: 0 2 3 5 7 9 10 */
    0xAD5u,                              /* lydian: 0 2 4 6 7 9 11 */
    0x5ABu,                              /* phrygian: 0 1 3 5 7 8 10 */
    0x9ADu,                              /* harmonic minor: 0 2 3 5 7 8 11 */
    0x6B5u,                              /* mixolydian: 0 2 4 5 7 9 10 */
};

/* a chord voiced: four notes at most (the 5th, then the top extension, dropped first), close position, its
 * inversion the one nearest centre (a MIDI note: the last chord's middle) */
static int voice_chord(const chord_t *c, int transpose, int centre, uint8_t *out)
{
    int pcs[4], n = c->n > 4 ? 4 : c->n, i, j, best = -1, bestcost = 1 << 30, inv, oct;
    uint8_t cand[4];
    if (c->n > 4) {                      /* five or six: keep root, 3rd, 7th and the first extension */
        n = 4;
    }
    for (i = 0; i < n; i++)
        pcs[i] = (c->root + c->iv[i] + transpose + 1200) % 12;
    /* close voicings: each note in turn at the bottom, the others stacked over it */
    for (inv = 0; inv < n; inv++)
        for (oct = 3; oct <= 5; oct++) {
            int lo = 12 * (oct + 1) + pcs[inv], sum = 0, cost;
            cand[0] = (uint8_t)lo;
            for (i = 1, j = 0; i < n; i++) {
                int p = pcs[(inv + i) % n], v = 12 * (oct + 1) + p;
                while (v <= cand[i - 1])
                    v += 12;
                cand[i] = (uint8_t)v;
            }
            for (i = 0; i < n; i++)
                sum += cand[i];
            cost = sum - centre * n;
            cost = cost < 0 ? -cost : cost;
            if (cand[0] < 50 || cand[n - 1] > 82)
                cost += 1000;
            if (cost < bestcost) {
                bestcost = cost;
                best = inv * 8 + oct;
                for (j = 0; j < n; j++)
                    out[j] = cand[j];
            }
        }
    (void)best;
    return n;
}
