/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 swtdr34mz (CHORDMAN) */
/* CHORDMAN's grooves: the J-6's nine phrase styles (its manual's Phrase List), twelve variations each.
 *   1 ARP 8TH, 2 ARP 16TH   up / up-down / down over one or two octaves, straight (1-6) and triplets (7-12)
 *   3 BEAT                  the chord again every 2/1 .. 1/16, straight and triplets
 *   4 RHYTHM, 5 SYNCOPE     the J-6's 16th patterns (o a hit, ~ held on, _ a rest), the last six in threes
 *   6 PIANO, 7 STABS        chord phrases: bass and chord, simple and rhythmic
 *   8 STRUM, 9 FUNK STRUM   strummed, down (D) and up (P), simple and rhythmic
 * Time is in ticks, 24 a quarter (MIDI clock). Part of dsp/engine.c. */

enum { K_CHORD, K_BASS, K_UPPER, K_ARP, K_DOWN, K_UP };
typedef struct {
    uint8_t kind;                        /* K_* */
    uint8_t idx;                         /* K_ARP: the note in the arp's order */
    uint8_t accent;                      /* 1: louder */
    uint16_t len;                        /* ticks it sounds */
} gev_t;

const char *const STYLE_NAME[NSTYLES + 1] = {"OFF", "ARP 8TH", "ARP 16TH", "BEAT", "RHYTHM", "SYNCOPE",
                                             "PIANO", "STABS", "STRUM", "FUNK STRUM"};
static const char *const ARP_VAR[NVARS] = {"UP 1", "UP DN 1", "DOWN 1", "DOWN 2", "UP DN 2", "UP 2",
                                           "UP 1 T", "UP DN 1 T", "DOWN 1 T", "DOWN 2 T", "UP DN 2 T", "UP 2 T"};
static const char *const BEAT_VAR[NVARS] = {"2/1", "1/1", "1/2", "1/4", "1/8", "1/16",
                                            "2/1 T", "1/1 T", "1/2 T", "1/4 T", "1/8 T", "1/16 T"};
static const uint16_t BEAT_LEN[NVARS] = {192, 96, 48, 24, 12, 6, 128, 64, 32, 16, 8, 4};
/* the J-6's rhythms, as its Phrase List writes them: o a 16th hit, ~ the hit held a 16th more, _ a 16th rest */
static const char *const RHY[2][NVARS] = {
    {"o___", "_o__", "__o_", "___o", "o~o_", "o_o~", "o~~o", "_o~o", "ooo_", "oo_o", "o_oo", "_ooo"},
    {"oo__", "o_o_", "o__o", "_oo_", "_o_o", "__oo", "o__", "_o_", "__o", "oo_", "o_o", "_oo"},
};
/* the phrases, a bar of 16ths: C c the chord (loud, soft), B b the bass, U u the chord without its bass, D a strum down,
 * P a strum up, - the hit before held on, . a rest */
static const char *const PHR[4][NVARS] = {
    {"B---U---B---U---", "B---u-U-B---u-U-", "B-U-u-U-B-U-u-U-", "C---.-c-.-c-.-c-", "B---U-------U---",
     "B-u-U-u-B-u-U-u-", "C-----c-----c---", "B--U--U-B--U--U-", "C---c---C---c-c-", "B-------U---u---",
     "B-U---U-b-U---U-", "C---------------"},
    {"..C...C...C...C.", "C..C..C...C..C..", "C.cC.c.C.cC.c.c.", ".C.C.C.C.C.C.C.C", "C..C...C..C.C...",
     "..C..C..C..C.C..", "C.c.C.ccC.c.C.cc", "C...C..C..C.C...", ".cC..cC..cC..cC.", "C-.C-.C-.C-.C-C-",
     "B.C.B.CCB.C.BCCC", "C..c..C..c..C.c."},
    {"D---D---D---D---", "D-------D-------", "D---D-P-D---D-P-", "D-P-D-P-D-P-D-P-", "D---P---D---P---",
     "D-----P-P---D---", "D-----D-----D---", "D---DP--D---DP--", "D-DP-PDPD-DP-PDP", "D-------D---D---",
     "D---D---D-P-D-P-", "DP--DP--DP--DP--"},
    {"D-DP-PDPD-DP-PDP", "D.P.D.PDP.PDP.D.", "D..PD..PD..PD.P.", "D-P-.PDPD-P-.PDP", "D..D..D.P.P.D...",
     ".PD..PD..PD..PDP", "DPDPDPDPDPDPDPDP", "D.D.P.PDD.D.P.PD", "D...DP.PD...DP.P", "D-.PD-.PD-.PD-.P",
     "D.P..PD.P.D..PD.", "D..P..D..P..D.P."},
};
static const char *const PHR_VAR[NVARS] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"};

const char *groove_name(int style, int var)
{
    var = var < 0 ? 0 : var >= NVARS ? NVARS - 1 : var;
    switch (style) {
    case 1:
    case 2:
        return ARP_VAR[var];
    case 3:
        return BEAT_VAR[var];
    case 4:
    case 5:
        return RHY[style - 4][var];
    case 6:
    case 7:
    case 8:
    case 9:
        return PHR_VAR[var];
    default:
        return "";
    }
}

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* the arp's order over n notes (lowest first), one or two octaves: its length, and note i of it (an octave up: +12) */
static uint32_t arp_len(int mode, uint32_t n)
{
    uint32_t m = (mode == 3 || mode == 4 || mode == 5) ? 2u * n : n;   /* DOWN 2, UP DN 2, UP 2: two octaves */
    if (mode == 1 || mode == 4)
        return m > 1u ? 2u * m - 2u : 1u;                              /* up and down, the ends once */
    return m;
}
static int arp_note(int mode, uint32_t n, uint32_t i, const uint8_t *notes)
{
    uint32_t m = (mode == 3 || mode == 4 || mode == 5) ? 2u * n : n, k;
    if (mode == 1 || mode == 4)
        k = i < m ? i : 2u * m - 2u - i;
    else if (mode == 2 || mode == 3)
        k = m - 1u - i;
    else
        k = i;
    return notes[k % n] + 12 * (int)(k / n);
}

/* the events that start at tick t of the groove (t counts from its start): into ev, their count */
static int groove_events(int style, int var, uint32_t t, gev_t *ev)
{
    int n = 0;
    if (var < 0 || var >= NVARS)
        return 0;
    if (style == 1 || style == 2) {
        uint32_t step = style == 1 ? (var < 6 ? 12u : 8u) : (var < 6 ? 6u : 4u);
        if (t % step == 0u) {
            ev[0].kind = K_ARP;
            ev[0].idx = (uint8_t)((t / step) & 0xFFu);   /* the step's number: the player takes it mod the arp */
            ev[0].accent = t % 24u == 0u;
            ev[0].len = (uint16_t)(step - (step > 4u ? 2u : 1u));
            n = 1;
        }
    } else if (style == 3) {
        uint32_t len = BEAT_LEN[var];
        if (t % len == 0u) {
            ev[0].kind = K_CHORD;
            ev[0].idx = 0;
            ev[0].accent = t % 96u == 0u;
            ev[0].len = (uint16_t)(len - (len > 12u ? 3u : 1u));
            n = 1;
        }
    } else if (style >= 4 && style <= 9) {
        const char *p = style <= 5 ? RHY[style - 4][var] : PHR[style - 6][var];
        uint32_t L = slen(p), s, k;
        char c;
        if (t % 6u)
            return 0;
        s = (t / 6u) % L;
        c = p[s];
        if (style <= 5) {
            if (c != 'o')
                return 0;
            ev[0].kind = K_CHORD;
            ev[0].accent = s == 0u;
            for (k = 1; k < L && p[(s + k) % L] == '~'; k++)
                ;
        } else {
            switch (c) {
            case 'C': case 'c': ev[0].kind = K_CHORD; break;
            case 'B': case 'b': ev[0].kind = K_BASS; break;
            case 'U': case 'u': ev[0].kind = K_UPPER; break;
            case 'D': ev[0].kind = K_DOWN; break;
            case 'P': ev[0].kind = K_UP; break;
            default: return 0;
            }
            ev[0].accent = c == 'C' || c == 'B' || c == 'U' || c == 'D';
            for (k = 1; k < L && p[(s + k) % L] == '-'; k++)
                ;
        }
        ev[0].idx = 0;
        ev[0].len = (uint16_t)(6u * k - 2u);
        n = 1;
        /* the piano's bass under the first chord of the bar */
        if (style == 6 && c == 'C' && s == 0u) {
            ev[1] = ev[0];
            ev[1].kind = K_BASS;
            n = 2;
        }
    }
    return n;
}

int groove_hits(int style, int var, uint32_t *bar16)
{
    uint32_t t, m = 0;
    gev_t ev[2];
    for (t = 0; t < 96u; t++)
        if (groove_events(style, var, t, ev))
            m |= 1u << (t / 6u);
    *bar16 = m;
    return m != 0u;
}
