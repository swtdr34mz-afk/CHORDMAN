/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's grooves, a bar of 16ths each.
 * The chords: FULL (held), QUARTER, EIGHTS, OFFBEAT, PUMP, SYNCO (3-3-2), HALF, the arps (UP, DOWN, UP/DN, RANDOM over
 * two octaves in 16ths; CASCADE, the notes left ringing in 8ths), TRANCE (the gated chord).
 * The bass: ROOT, OCTAVE, OFFBEAT (house), ROLLING (the 16ths between the kicks), ROOT-5TH, DISCO, HALF.
 * In the patterns: C c a chord (loud, soft); R r the root, O o its octave, 5 its fifth; - the hit before held on;
 * . a rest. Part of dsp/engine.c. */
const char *const CRHY_NAME[NCRHY] = {"FULL", "QUARTER", "EIGHTS", "OFFBEAT", "PUMP", "SYNCO", "HALF",
                                      "ARP UP", "ARP DOWN", "ARP UP/DN", "ARP RANDOM", "CASCADE", "TRANCE"};
const char *const BASS_NAME[NBASS] = {"ROOT", "OCTAVE", "OFFBEAT", "ROLLING", "ROOT-5TH", "DISCO", "HALF"};
const char *const SHAPE_NAME[NSHAPES] = {"SOFT", "PUMP", "HARD", "GATE"};
const char *const SCTGT_NAME[NSCTGT] = {"VOLUME", "FILTER", "BOTH"};
const char *const SKICK_NAME[NSKICK] = {"4/4", "HALF", "2 + 4", "TRAP"};
const char *const SYNC_NAME[NSYNCS] = {"AUTO", "INT", "EXT"};

static const char *const CR_PAT[NCRHY] = {
    "C---------------", "C--.C--.C--.C--.", "C.c.C.c.C.c.C.c.", "..C...C...C...C.", "CcccCcccCcccCccc",
    "C-.C-.C-C-.C-.C-", "C------.C------.", 0, 0, 0, 0, 0, "CC.CC.C.CC.C.CC.",
};
static const char *const BA_PAT[NBASS] = {
    "R--.R--.R--.R--.", "R.O.R.O.R.O.R.O.", "..R...R...R...R.", ".rrr.rrr.rrr.rrr", "R..R..5.R..R..5.",
    "R.O.R.O.R.O.R.Oo", "R-------R-------",
};

int crhy_hits(int r, uint32_t *bar16)
{
    uint32_t m = 0, i;
    if (r < 0 || r >= NCRHY)
        return 0;
    if (!CR_PAT[r])                      /* the arps: every 16th (CASCADE every 8th) */
        m = r == CR_CASCADE ? 0x5555u : 0xFFFFu;
    else
        for (i = 0; i < 16u; i++)
            if (CR_PAT[r][i] != '.' && CR_PAT[r][i] != '-')
                m |= 1u << i;
    *bar16 = m;
    return 1;
}
int bass_hits(int b, uint32_t *bar16)
{
    uint32_t m = 0, i;
    if (b < 0 || b >= NBASS)
        return 0;
    for (i = 0; i < 16u; i++)
        if (BA_PAT[b][i] != '.' && BA_PAT[b][i] != '-')
            m |= 1u << i;
    *bar16 = m;
    return 1;
}

/* a hit at 16th s of a pattern: its kind (0 none), loud, and how many 16ths it lasts */
static int pat_hit(const char *p, uint32_t s, int *loud, int *len)
{
    char c = p[s % 16u];
    int k;
    if (c == '.' || c == '-')
        return 0;
    for (k = 1; k < 16 && p[(s + (uint32_t)k) % 16u] == '-'; k++)
        ;
    *len = k;
    *loud = c >= 'A' && c <= 'Z';
    return c;
}
